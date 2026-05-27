# kernel/sync 同步原语模块
file(GLOB_RECURSE SYNC_SOURCES "*.c")
add_library(kernel_sync STATIC ${SYNC_SOURCES})
