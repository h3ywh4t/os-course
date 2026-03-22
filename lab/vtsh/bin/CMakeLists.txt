add_executable(
    vtsh
    main.c
)

target_include_directories(
    vtsh
    PUBLIC
    .
)

target_link_libraries(
    vtsh
    PRIVATE
    libvtsh
)

add_executable(
    cpu-sort
    cpu_sort.c
)

add_executable(
    ema-join-sm
    ema_join_sm.c
)

add_executable(
    proc-clone3
    proc_clone3.c
)

add_executable(
    io-load
    io_load.c
)

add_executable(
    cpu-mt-load
    cpu_mt_load.c
)

target_link_libraries(
    cpu-mt-load
    PRIVATE pthread
)