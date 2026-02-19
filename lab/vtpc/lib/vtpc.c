#include "vtpc.h"
#include "vtpc_internal.h"

#include <stdio.h>
#include <sys/stat.h>

static size_t vtpc_min_size(size_t a, size_t b) { return (a < b) ? a : b; }

int vtpc_open(const char* path, int mode, int access) {
  int io_fd = -1, direct_fd = -1, buffered_fd = -1;
  bool use_direct = false;

  if (vtpc_open_files(path, mode, access, &io_fd, &direct_fd, &buffered_fd, &use_direct) != 0) {
    return -1;
  }

  vtpc_state* st = (vtpc_state*)calloc(1, sizeof(vtpc_state));
  if (st == NULL) {
    int saved = errno;
    (void)close(io_fd);
    if (use_direct && buffered_fd >= 0 && buffered_fd != io_fd) (void)close(buffered_fd);
    errno = saved;
    return -1;
  }

  st->io_fd = io_fd;
  st->direct_fd = direct_fd;
  st->buffered_fd = buffered_fd;
  st->use_direct = use_direct;

  st->page_size = vtpc_get_page_size();
  st->cache_pages = vtpc_get_cache_pages();
  if (st->cache_pages == 0) st->cache_pages = 1;
  st->clock_hand = 0;
  st->pos = 0;

  st->pages = (vtpc_page*)calloc(st->cache_pages, sizeof(vtpc_page));
  if (st->pages == NULL) {
    int saved = errno;
    (void)close(io_fd);
    if (use_direct && buffered_fd >= 0 && buffered_fd != io_fd) (void)close(buffered_fd);
    free(st);
    errno = saved;
    return -1;
  }

  if (vtpc_refresh_size_from_fd(st) != 0) {
    int saved = errno;
    (void)close(io_fd);
    if (use_direct && buffered_fd >= 0 && buffered_fd != io_fd) (void)close(buffered_fd);
    free(st->pages);
    free(st);
    errno = saved;
    return -1;
  }

  if (vtpc_table_insert(io_fd, st) != 0) {
    int saved = errno;
    (void)close(io_fd);
    if (use_direct && buffered_fd >= 0 && buffered_fd != io_fd) (void)close(buffered_fd);
    for (size_t i = 0; i < st->cache_pages; ++i) free(st->pages[i].data);
    free(st->pages);
    free(st);
    errno = saved;
    return -1;
  }

  return io_fd;
}

int vtpc_close(int fd) {
  vtpc_state* st = vtpc_table_remove(fd);
  if (st == NULL) {
    errno = EBADF;
    return -1;
  }

  int rc = 0;
  int saved_errno = 0;

  for (size_t i = 0; i < st->cache_pages; ++i) {
    vtpc_page* p = &st->pages[i];
    if (p->valid && p->dirty) {
      if (vtpc_flush_page(st, p) != 0) {
        if (rc == 0) { rc = -1; saved_errno = errno; }
      }
    }
  }

  if (rc == 0) {
    if (fsync(st->io_fd) != 0) {
      rc = -1;
      saved_errno = errno;
    }
  }

  vtpc_dump_stats_if_enabled(st, "close");

  int c1 = close(st->io_fd);
  int c2 = 0;
  if (st->use_direct && st->buffered_fd >= 0 && st->buffered_fd != st->io_fd) {
    c2 = close(st->buffered_fd);
  }

  for (size_t i = 0; i < st->cache_pages; ++i) {
    free(st->pages[i].data);
    st->pages[i].data = NULL;
  }
  free(st->pages);
  free(st);

  if (rc != 0) {
    errno = saved_errno;
    return -1;
  }
  if (c1 != 0 || c2 != 0) {
    return -1;
  }
  return 0;
}

ssize_t vtpc_read(int fd, void* buf, size_t count) {
  if (count == 0) return 0;
  if (buf == NULL) { errno = EFAULT; return -1; }

  vtpc_state* st = vtpc_table_find(fd);
  if (st == NULL) { errno = EBADF; return -1; }

  if (st->pos >= st->size) return 0;

  size_t to_read = count;
  off_t remain = st->size - st->pos;
  if (remain < 0) remain = 0;
  if ((off_t)to_read > remain) to_read = (size_t)remain;
  if (to_read == 0) return 0;

  size_t done = 0;
  unsigned char* out = (unsigned char*)buf;

  while (done < to_read) {
    off_t cur = st->pos;
    off_t page_no = cur / (off_t)st->page_size;
    size_t in_page = (size_t)(cur % (off_t)st->page_size);
    size_t chunk = vtpc_min_size(st->page_size - in_page, to_read - done);

    int err = 0;
    vtpc_page* p = vtpc_get_or_make_page(st, page_no, &err);
    if (p == NULL) {
      if (done == 0) return -1;
      return (ssize_t)done;
    }

    memcpy(out + done, p->data + in_page, chunk);
    done += chunk;
    st->pos += (off_t)chunk;
  }

  return (ssize_t)done;
}

ssize_t vtpc_write(int fd, const void* buf, size_t count) {
  if (count == 0) return 0;
  if (buf == NULL) { errno = EFAULT; return -1; }

  vtpc_state* st = vtpc_table_find(fd);
  if (st == NULL) { errno = EBADF; return -1; }

  const unsigned char* in = (const unsigned char*)buf;
  size_t done = 0;

  while (done < count) {
    off_t cur = st->pos;
    off_t page_no = cur / (off_t)st->page_size;
    size_t in_page = (size_t)(cur % (off_t)st->page_size);
    size_t chunk = vtpc_min_size(st->page_size - in_page, count - done);

    off_t end_pos = st->pos + (off_t)chunk;
    if (end_pos > st->size) {
      st->size = end_pos;
      if (ftruncate(st->io_fd, st->size) != 0) {
        if (done == 0) return -1;
        return (ssize_t)done;
      }
    }

    int err = 0;
    vtpc_page* p = vtpc_get_or_make_page(st, page_no, &err);
    if (p == NULL) {
      if (done == 0) return -1;
      return (ssize_t)done;
    }

    memcpy(p->data + in_page, in + done, chunk);
    p->dirty = true;
    p->ref = true;

    done += chunk;
    st->pos += (off_t)chunk;
  }

  return (ssize_t)done;
}

off_t vtpc_lseek(int fd, off_t offset, int whence) {
  vtpc_state* st = vtpc_table_find(fd);
  if (st == NULL) { errno = EBADF; return (off_t)-1; }

  off_t newpos = 0;

  if (whence == SEEK_SET) {
    newpos = offset;
  } else if (whence == SEEK_CUR) {
    newpos = st->pos + offset;
  } else if (whence == SEEK_END) {
    if (vtpc_refresh_size_from_fd(st) != 0) return (off_t)-1;
    newpos = st->size + offset;
  } else {
    errno = EINVAL;
    return (off_t)-1;
  }

  if (newpos < 0) { errno = EINVAL; return (off_t)-1; }

  st->pos = newpos;

  if (lseek(fd, newpos, SEEK_SET) == (off_t)-1) return (off_t)-1;

  return newpos;
}

int vtpc_fsync(int fd) {
  vtpc_state* st = vtpc_table_find(fd);
  if (st == NULL) { errno = EBADF; return -1; }

  for (size_t i = 0; i < st->cache_pages; ++i) {
    vtpc_page* p = &st->pages[i];
    if (p->valid && p->dirty) {
      if (vtpc_flush_page(st, p) != 0) return -1;
    }
  }

  if (fsync(st->io_fd) != 0) return -1;

  if (vtpc_log_enabled() && vtpc_log_fsync_enabled()) {
    vtpc_dump_stats_if_enabled(st, "fsync");
  }

  return 0;
}
