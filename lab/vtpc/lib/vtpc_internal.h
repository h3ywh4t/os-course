#ifndef VTPC_INTERNAL_H
#define VTPC_INTERNAL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>  
#include <unistd.h>      
#include <errno.h>
#include <fcntl.h>      
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef O_DIRECT
#ifdef __O_DIRECT
#define O_DIRECT __O_DIRECT
#endif
#endif

#ifndef VTPC_DEFAULT_CACHE_PAGES
#define VTPC_DEFAULT_CACHE_PAGES 64U
#endif


typedef struct vtpc_stats {
  uint64_t hits;
  uint64_t misses;
  uint64_t evict;
  uint64_t rd_pages;
  uint64_t wb_pages;
} vtpc_stats;

typedef struct vtpc_page {
  off_t page_no;             
  unsigned char* data;        
  bool valid;
  bool dirty;
  bool ref;                  
} vtpc_page;

typedef struct vtpc_state {
  int io_fd;                  
  int direct_fd;           
  int buffered_fd;         
  bool use_direct;            

  off_t pos;               
  off_t size;              

  size_t page_size;
  size_t cache_pages;
  size_t clock_hand;

  vtpc_page* pages;
  vtpc_stats stats;
} vtpc_state;

size_t vtpc_get_page_size(void);
size_t vtpc_get_cache_pages(void);

int vtpc_refresh_size_from_fd(vtpc_state* st);

int vtpc_table_insert(int fd, vtpc_state* st);
vtpc_state* vtpc_table_find(int fd);
vtpc_state* vtpc_table_remove(int fd);

int vtpc_open_files(const char* path, int mode, int access,
                    int* out_io_fd, int* out_direct_fd, int* out_buffered_fd,
                    bool* out_use_direct);

ssize_t vtpc_pread_retry(int fd, void* buf, size_t count, off_t off);
ssize_t vtpc_pwrite_retry(int fd, const void* buf, size_t count, off_t off);

void vtpc_best_effort_drop_cache(int fd, off_t off, size_t len);

vtpc_page* vtpc_find_page(vtpc_state* st, off_t page_no);

vtpc_page* vtpc_get_or_make_page(vtpc_state* st, off_t page_no, int* out_err);

int vtpc_flush_page(vtpc_state* st, vtpc_page* p);
int vtpc_load_page_from_disk(vtpc_state* st, vtpc_page* p);

bool vtpc_log_enabled(void);
bool vtpc_log_fsync_enabled(void);
void vtpc_dump_stats_if_enabled(const vtpc_state* st, const char* tag);

#endif
