#include "vtpc_internal.h"

#include <stdio.h>
#include <inttypes.h>

bool vtpc_log_enabled(void) {
  const char* s = getenv("VTPC_LOG");
  return (s != NULL && s[0] != '\0' && s[0] != '0');
}

bool vtpc_log_fsync_enabled(void) {
  const char* s = getenv("VTPC_LOG_FSYNC");
  return (s != NULL && s[0] != '\0' && s[0] != '0');
}

void vtpc_dump_stats_if_enabled(const vtpc_state* st, const char* tag) {
  if (!vtpc_log_enabled()) return;

  uint64_t total = st->stats.hits + st->stats.misses;
  double hit_rate = (total == 0) ? 0.0 : (100.0 * (double)st->stats.hits / (double)total);

  fprintf(stderr,
          "[vtpc] %s fd=%d cap=%zu ps=%zu hits=%" PRIu64 " misses=%" PRIu64
          " evict=%" PRIu64 " rd_pages=%" PRIu64 " wb_pages=%" PRIu64
          " hit_rate=%.2f%%\n",
          tag, st->io_fd, st->cache_pages, st->page_size,
          st->stats.hits, st->stats.misses, st->stats.evict,
          st->stats.rd_pages, st->stats.wb_pages, hit_rate);
}
