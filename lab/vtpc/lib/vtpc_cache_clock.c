#include "vtpc_internal.h"

static int vtpc_alloc_page_buf(vtpc_state* st, vtpc_page* p) {
  if (p->data != NULL) return 0;

  void* mem = NULL;
  int rc = posix_memalign(&mem, st->page_size, st->page_size);
  if (rc != 0 || mem == NULL) {
    mem = malloc(st->page_size);
    if (mem == NULL) {
      errno = ENOMEM;
      return -1;
    }
  }
  p->data = (unsigned char*)mem;
  return 0;
}

vtpc_page* vtpc_find_page(vtpc_state* st, off_t page_no) {
  for (size_t i = 0; i < st->cache_pages; ++i) {
    vtpc_page* p = &st->pages[i];
    if (p->valid && p->page_no == page_no) return p;
  }
  return NULL;
}

int vtpc_flush_page(vtpc_state* st, vtpc_page* p) {
  if (!p->valid || !p->dirty) return 0;

  const off_t page_off = p->page_no * (off_t)st->page_size;

  int fd_try = (st->use_direct && st->direct_fd >= 0) ? st->direct_fd : st->buffered_fd;
  if (fd_try < 0) fd_try = st->io_fd;

  ssize_t w = vtpc_pwrite_retry(fd_try, p->data, st->page_size, page_off);
  if (w < 0) {
    if (st->use_direct && st->buffered_fd >= 0 && st->buffered_fd != fd_try &&
        (errno == EINVAL || errno == EOPNOTSUPP || errno == ENOTTY)) {
      w = vtpc_pwrite_retry(st->buffered_fd, p->data, st->page_size, page_off);
      if (w < 0) return -1;
      vtpc_best_effort_drop_cache(st->buffered_fd, page_off, st->page_size);
    } else {
      return -1;
    }
  } else {
    if (!st->use_direct && st->buffered_fd >= 0) {
      vtpc_best_effort_drop_cache(st->buffered_fd, page_off, st->page_size);
    }
  }

  if ((size_t)w != st->page_size) {
    errno = EIO;
    return -1;
  }

  if (st->size < page_off + (off_t)st->page_size) {
    if (ftruncate(st->io_fd, st->size) != 0) return -1;
  }

  p->dirty = false;
  st->stats.wb_pages += 1;
  return 0;
}

int vtpc_load_page_from_disk(vtpc_state* st, vtpc_page* p) {
  const off_t page_off = p->page_no * (off_t)st->page_size;

  memset(p->data, 0, st->page_size);

  int fd_try = (st->use_direct && st->direct_fd >= 0) ? st->direct_fd : st->buffered_fd;
  if (fd_try < 0) fd_try = st->io_fd;

  ssize_t r = vtpc_pread_retry(fd_try, p->data, st->page_size, page_off);
  if (r < 0) {
    if (st->use_direct && st->buffered_fd >= 0 && st->buffered_fd != fd_try &&
        (errno == EINVAL || errno == EOPNOTSUPP || errno == ENOTTY)) {
      r = vtpc_pread_retry(st->buffered_fd, p->data, st->page_size, page_off);
      if (r < 0) return -1;
      vtpc_best_effort_drop_cache(st->buffered_fd, page_off, st->page_size);
    } else {
      return -1;
    }
  } else {
    if (!st->use_direct && st->buffered_fd >= 0) {
      vtpc_best_effort_drop_cache(st->buffered_fd, page_off, st->page_size);
    }
  }

  st->stats.rd_pages += 1;
  return 0;
}

vtpc_page* vtpc_get_or_make_page(vtpc_state* st, off_t page_no, int* out_err) {
  *out_err = 0;

  vtpc_page* existing = vtpc_find_page(st, page_no);
  if (existing != NULL) {
    existing->ref = true;
    st->stats.hits += 1;
    return existing;
  }

  st->stats.misses += 1;

  size_t victim = st->clock_hand;
  size_t tries = 0;

  for (;;) {
    vtpc_page* p = &st->pages[victim];

    if (!p->valid) {
      break;
    }
    if (p->ref) {
      p->ref = false;
    } else {
      break;
    }

    victim = (victim + 1) % st->cache_pages;

    if (++tries > st->cache_pages * 2U + 10U) {
      break;
    }
  }

  st->clock_hand = (victim + 1) % st->cache_pages;

  vtpc_page* p = &st->pages[victim];

  if (vtpc_alloc_page_buf(st, p) != 0) {
    *out_err = -1;
    return NULL;
  }

  if (p->valid) {
    st->stats.evict += 1;
    if (p->dirty) {
      if (vtpc_flush_page(st, p) != 0) {
        *out_err = -1;
        return NULL;
      }
    }
  }

  p->page_no = page_no;
  p->valid = true;
  p->dirty = false;
  p->ref = true;

  if (vtpc_load_page_from_disk(st, p) != 0) {
    *out_err = -1;
    return NULL;
  }

  return p;
}
