#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef enum { IOLOAD_RW_READ, IOLOAD_RW_WRITE } ioload_rw_mode_t;

typedef enum { IOLOAD_ACCESS_SEQ, IOLOAD_ACCESS_RANDOM } ioload_access_mode_t;

typedef struct {
  ioload_rw_mode_t rw_mode;
  ioload_access_mode_t access_mode;
  size_t block_size;
  uint64_t block_count;
  const char* file_path;
  off_t range_start; /* inclusive */
  off_t range_end;   /* exclusive; 0 = "авто" до нормализации */
  bool use_direct;
} ioload_config_t;

static void print_usage(const char* prog) {
  fprintf(
      stderr,
      "Usage: %s rw=<read|write> block_size=<bytes> block_count=<n> "
      "file=<path> [range=<start>-<end>] [direct=<on|off>] "
      "[type=<sequence|random>]\n",
      prog
  );
  fprintf(
      stderr,
      "\nParameters:\n"
      "  rw            - I/O mode: read or write\n"
      "  block_size    - size of one I/O operation in bytes\n"
      "  block_count   - how many blocks to read/write\n"
      "  file          - path to file to use for I/O\n"
      "  range         - byte range <start>-<end> within the file; 0-0 or "
      "omitted = full file\n"
      "  direct        - on/off, use O_DIRECT if available (default: off)\n"
      "  type          - access pattern: sequence or random (default: "
      "sequence)\n"
  );
}

static int parse_ull(const char* str, unsigned long long* out) {
  errno = 0;
  char* endptr = NULL;
  unsigned long long val = strtoull(str, &endptr, 10);
  if (errno != 0 || endptr == str || *endptr != '\0') {
    return -1;
  }
  *out = val;
  return 0;
}

static int parse_range(const char* str, off_t* start, off_t* end) {
  char* dash = strchr(str, '-');
  if (!dash) {
    return -1;
  }
  char buf_start[64];
  char buf_end[64];

  size_t len_start = (size_t)(dash - str);
  size_t len_end = strlen(dash + 1);

  if (len_start == 0 || len_start >= sizeof(buf_start) || len_end == 0 ||
      len_end >= sizeof(buf_end)) {
    return -1;
  }

  memcpy(buf_start, str, len_start);
  buf_start[len_start] = '\0';
  memcpy(buf_end, dash + 1, len_end + 1);

  unsigned long long s = 0, e = 0;
  if (parse_ull(buf_start, &s) != 0 || parse_ull(buf_end, &e) != 0) {
    return -1;
  }

  *start = (off_t)s;
  *end = (off_t)e;
  return 0;
}

static double now_seconds(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static int normalize_config(
    ioload_config_t* cfg, int fd, uint64_t* blocks_in_region
) {
  struct stat st;
  if (fstat(fd, &st) != 0) {
    perror("fstat");
    return -1;
  }

  off_t file_size = st.st_size;
  off_t start = cfg->range_start;
  off_t end = cfg->range_end;

  if (start < 0) {
    fprintf(stderr, "ioload: negative range start is not allowed\n");
    return -1;
  }

  if (cfg->rw_mode == IOLOAD_RW_READ) {
    if (file_size == 0) {
      fprintf(stderr, "ioload: cannot read from empty file\n");
      return -1;
    }
    if (end == 0 || end > file_size) {
      end = file_size;
    }
    if (start >= end) {
      fprintf(
          stderr,
          "ioload: invalid range %lld-%lld for file size %lld\n",
          (long long)start,
          (long long)end,
          (long long)file_size
      );
      return -1;
    }
  } else {
    if (end == 0) {
      unsigned long long bytes = cfg->block_size * cfg->block_count;
      if ((unsigned long long)start + bytes < bytes) {
        fprintf(stderr, "ioload: range overflow\n");
        return -1;
      }
      end = start + (off_t)bytes;
    }
    if (end <= start) {
      fprintf(
          stderr,
          "ioload: invalid write range %lld-%lld\n",
          (long long)start,
          (long long)end
      );
      return -1;
    }

    off_t desired_size = end;
    if (file_size < desired_size) {
#if defined(__linux__)
      int res = posix_fallocate(fd, 0, desired_size);
      if (res != 0) {
        errno = res;
        perror("posix_fallocate");
        return -1;
      }
#else
      if (ftruncate(fd, desired_size) != 0) {
        perror("ftruncate");
        return -1;
      }
#endif
    }
  }

  off_t region_size = end - start;
  if (region_size <= 0) {
    fprintf(stderr, "ioload: non-positive region size\n");
    return -1;
  }

  uint64_t blocks = (uint64_t)(region_size / (off_t)cfg->block_size);
  if (blocks == 0) {
    fprintf(
        stderr,
        "ioload: range %lld-%lld is too small for block_size=%zu\n",
        (long long)start,
        (long long)end,
        cfg->block_size
    );
    return -1;
  }

  cfg->range_start = start;
  cfg->range_end = end;
  *blocks_in_region = blocks;
  return 0;
}

static uint64_t random_block(uint64_t max) {
  uint64_t r = 0;
  for (int i = 0; i < 4; ++i) {
    r = (r << 15) ^ (uint64_t)(rand() & 0x7fff);
  }
  return (max == 0) ? 0 : (r % max);
}

int main(int argc, char** argv) {
  ioload_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.rw_mode = IOLOAD_RW_READ;
  cfg.access_mode = IOLOAD_ACCESS_SEQ;
  cfg.block_size = 0;
  cfg.block_count = 0;
  cfg.file_path = NULL;
  cfg.range_start = 0;
  cfg.range_end = 0;
  cfg.use_direct = false;

  if (argc < 5) {
    print_usage(argv[0]);
    return 1;
  }

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (strncmp(arg, "--", 2) == 0) {
      arg += 2;
    }
    const char* eq = strchr(arg, '=');
    if (!eq || eq == arg || !*(eq + 1)) {
      fprintf(stderr, "ioload: invalid argument '%s'\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }

    size_t key_len = (size_t)(eq - arg);
    const char* val = eq + 1;

    if (key_len == 2 && strncmp(arg, "rw", 2) == 0) {
      if (strcmp(val, "read") == 0 || strcmp(val, "r") == 0) {
        cfg.rw_mode = IOLOAD_RW_READ;
      } else if (strcmp(val, "write") == 0 || strcmp(val, "w") == 0) {
        cfg.rw_mode = IOLOAD_RW_WRITE;
      } else {
        fprintf(stderr, "ioload: invalid rw mode '%s'\n", val);
        return 1;
      }
    } else if (key_len == 10 && strncmp(arg, "block_size", 10) == 0) {
      unsigned long long v;
      if (parse_ull(val, &v) != 0 || v == 0) {
        fprintf(stderr, "ioload: invalid block_size '%s'\n", val);
        return 1;
      }
      cfg.block_size = (size_t)v;
    } else if (key_len == 11 && strncmp(arg, "block_count", 11) == 0) {
      unsigned long long v;
      if (parse_ull(val, &v) != 0 || v == 0) {
        fprintf(stderr, "ioload: invalid block_count '%s'\n", val);
        return 1;
      }
      cfg.block_count = (uint64_t)v;
    } else if (key_len == 4 && strncmp(arg, "file", 4) == 0) {
      cfg.file_path = val;
    } else if (key_len == 5 && strncmp(arg, "range", 5) == 0) {
      if (parse_range(val, &cfg.range_start, &cfg.range_end) != 0) {
        fprintf(stderr, "ioload: invalid range '%s'\n", val);
        return 1;
      }
    } else if (key_len == 6 && strncmp(arg, "direct", 6) == 0) {
      if (strcmp(val, "on") == 0) {
        cfg.use_direct = true;
      } else if (strcmp(val, "off") == 0) {
        cfg.use_direct = false;
      } else {
        fprintf(stderr, "ioload: invalid direct value '%s'\n", val);
        return 1;
      }
    } else if (key_len == 4 && strncmp(arg, "type", 4) == 0) {
      if (strcmp(val, "sequence") == 0 || strcmp(val, "seq") == 0) {
        cfg.access_mode = IOLOAD_ACCESS_SEQ;
      } else if (strcmp(val, "random") == 0 || strcmp(val, "rand") == 0) {
        cfg.access_mode = IOLOAD_ACCESS_RANDOM;
      } else {
        fprintf(stderr, "ioload: invalid type '%s'\n", val);
        return 1;
      }
    } else {
      fprintf(stderr, "ioload: unknown parameter '%.*s'\n", (int)key_len, arg);
      print_usage(argv[0]);
      return 1;
    }
  }

  if (cfg.block_size == 0 || cfg.block_count == 0 || cfg.file_path == NULL) {
    fprintf(
        stderr, "ioload: rw, block_size, block_count and file are required\n"
    );
    print_usage(argv[0]);
    return 1;
  }

  if (cfg.use_direct) {
    if (cfg.block_size % 512 != 0) {
      fprintf(
          stderr,
          "ioload: with direct=on block_size must be multiple of 512 bytes\n"
      );
      return 1;
    }
    if (cfg.range_start % (off_t)512 != 0) {
      fprintf(
          stderr,
          "ioload: with direct=on range start must be multiple of 512 bytes\n"
      );
      return 1;
    }
  }

  int open_flags =
      (cfg.rw_mode == IOLOAD_RW_WRITE) ? (O_RDWR | O_CREAT) : O_RDONLY;
#ifdef O_DIRECT
  if (cfg.use_direct) {
    open_flags |= O_DIRECT;
  }
#else
  if (cfg.use_direct) {
    fprintf(
        stderr,
        "ioload: direct=on requested but O_DIRECT is not supported on this "
        "platform, ignoring\n"
    );
  }
#endif

  int fd = open(cfg.file_path, open_flags, 0644);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  uint64_t blocks_in_region = 0;
  if (normalize_config(&cfg, fd, &blocks_in_region) != 0) {
    close(fd);
    return 1;
  }

  void* buf = NULL;
  size_t alignment = 4096;
  int rc = posix_memalign(&buf, alignment, cfg.block_size);
  if (rc != 0 || buf == NULL) {
    fprintf(
        stderr,
        "ioload: posix_memalign failed (rc=%d), falling back to malloc; "
        "direct I/O may fail\n",
        rc
    );
    buf = malloc(cfg.block_size);
    if (!buf) {
      fprintf(
          stderr,
          "ioload: cannot allocate buffer of %zu bytes\n",
          cfg.block_size
      );
      close(fd);
      return 1;
    }
  }

  if (cfg.rw_mode == IOLOAD_RW_WRITE) {
    unsigned char* p = (unsigned char*)buf;
    for (size_t i = 0; i < cfg.block_size; ++i) {
      p[i] = (unsigned char)(i & 0xff);
    }
  }

  srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

  uint64_t total_bytes = 0;
  uint64_t ops_done = 0;
  uint64_t current_block = 0;

  double t0 = now_seconds();

  for (uint64_t i = 0; i < cfg.block_count; ++i) {
    uint64_t block_index;
    if (cfg.access_mode == IOLOAD_ACCESS_SEQ) {
      block_index = current_block;
      current_block++;
      if (current_block >= blocks_in_region) {
        current_block = 0;
      }
    } else {
      block_index = random_block(blocks_in_region);
    }

    off_t offset = cfg.range_start + (off_t)(block_index * cfg.block_size);
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
      perror("lseek");
      break;
    }

    size_t done = 0;
    while (done < cfg.block_size) {
      ssize_t ret;
      if (cfg.rw_mode == IOLOAD_RW_READ) {
        ret = read(fd, (char*)buf + done, cfg.block_size - done);
      } else {
        ret = write(fd, (const char*)buf + done, cfg.block_size - done);
      }

      if (ret < 0) {
        if (errno == EINTR) {
          continue;
        }
        perror(cfg.rw_mode == IOLOAD_RW_READ ? "read" : "write");
        goto out;
      } else if (ret == 0) {
        if (cfg.rw_mode == IOLOAD_RW_READ) {
          fprintf(
              stderr,
              "ioload: unexpected EOF at offset %lld\n",
              (long long)(offset + (off_t)done)
          );
        }
        goto out;
      } else {
        done += (size_t)ret;
      }
    }

    total_bytes += cfg.block_size;
    ops_done++;
  }

out:;
  {
    double t1 = now_seconds();
    double elapsed = t1 - t0;
    if (elapsed <= 0.0) {
      elapsed = 1e-9;
    }
    double mb = (double)total_bytes / (1024.0 * 1024.0);
    double throughput = mb / elapsed;
    double iops = (double)ops_done / elapsed;

    fprintf(
        stdout,
        "ioload summary:\n"
        "  file:        %s\n"
        "  mode:        %s\n"
        "  access:      %s\n"
        "  range:       %lld-%lld (%lld bytes)\n"
        "  block:       %zu bytes\n"
        "  count:       %" PRIu64
        " ops\n"
        "  direct:      %s\n"
        "  elapsed:     %.6f s\n"
        "  bytes:       %" PRIu64
        " (%.2f MiB)\n"
        "  throughput:  %.2f MiB/s\n"
        "  IOPS:        %.2f ops/s\n",
        cfg.file_path,
        (cfg.rw_mode == IOLOAD_RW_READ) ? "read" : "write",
        (cfg.access_mode == IOLOAD_ACCESS_SEQ) ? "sequence" : "random",
        (long long)cfg.range_start,
        (long long)cfg.range_end,
        (long long)(cfg.range_end - cfg.range_start),
        cfg.block_size,
        ops_done,
        cfg.use_direct ? "on" : "off",
        elapsed,
        total_bytes,
        mb,
        throughput,
        iops
    );
  }

  free(buf);
  close(fd);
  return 0;
}
