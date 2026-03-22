add_library(vtpc SHARED
  vtpc.c
  vtpc_table.c
  vtpc_io.c
  vtpc_cache_clock.c
  vtpc_stats.c
)

target_include_directories(vtpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(vtpc PRIVATE _GNU_SOURCE)
