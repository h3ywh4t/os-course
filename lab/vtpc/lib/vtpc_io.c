#include "vtpc_internal.h"

size_t vtpc_get_page_size(void) {
  long ps = sysconf(_SC_PAGESIZE);
  if (ps <= 0) return 4096U;
  return (size_t)ps;
}

size_t vtpc_get_cache_pages(void) {
  const char* s = getenv("VTPC_CACHE_PAGES");
  if (s != NULL && s[0] != '\0') {
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end != s && v > 0 && v < 1000000L) {
      return (size_t)v;
    }
  }
  return (size_t)VTPC_DEFAULT_CACHE_PAGES;
}

int vtpc_refresh_size_from_fd(vtpc_state* st) {
  struct stat sb;
  if (fstat(st->io_fd, &sb) != 0) {
    return -1;
  }
  st->size = sb.st_size;
  return 0;
}

ssize_t vtpc_pread_retry(int fd, void* buf, size_t count, off_t off) {
  for (;;) {
    ssize_t r = pread(fd, buf, count, off);
    if (r < 0 && errno == EINTR) continue;
    return r;
  }
}

ssize_t vtpc_pwrite_retry(int fd, const void* buf, size_t count, off_t off) {
  for (;;) {
    ssize_t r = pwrite(fd, buf, count, off);
    if (r < 0 && errno == EINTR) continue;
    return r;
  }
}

void vtpc_best_effort_drop_cache(int fd, off_t off, size_t len) {
#ifdef POSIX_FADV_DONTNEED
  (void)posix_fadvise(fd, off, (off_t)len, POSIX_FADV_DONTNEED);
#else
  (void)fd; (void)off; (void)len;
#endif
}

int vtpc_open_files(const char* path, int mode, int access,
                    int* out_io_fd, int* out_direct_fd, int* out_buffered_fd,
                    bool* out_use_direct) {
  int io_fd = -1;
  int direct_fd = -1;
  int buffered_fd = -1;
  bool use_direct = false;

#ifdef __APPLE__
  io_fd = open(path, mode, access);
  if (io_fd < 0) return -1;

#ifdef F_NOCACHE
  (void)fcntl(io_fd, F_NOCACHE, 1);
#endif
  direct_fd = io_fd;
  buffered_fd = io_fd;
  use_direct = true;

#else
#ifdef O_DIRECT
  direct_fd = open(path, mode | O_DIRECT, access);
  if (direct_fd >= 0) {
    use_direct = true;
    io_fd = direct_fd;

    buffered_fd = open(path, mode, access);
    if (buffered_fd < 0) {
      buffered_fd = direct_fd;
    }
  }
#endif

  if (io_fd < 0) {
    io_fd = open(path, mode, access);
    if (io_fd < 0) return -1;
    direct_fd = -1;
    buffered_fd = io_fd;
    use_direct = false;
  }
#endif

  *out_io_fd = io_fd;
  *out_direct_fd = direct_fd;
  *out_buffered_fd = buffered_fd;
  *out_use_direct = use_direct;
  return 0;
}
