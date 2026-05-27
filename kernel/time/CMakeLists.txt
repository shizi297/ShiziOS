# kernel/time 时间管理模块
file(GLOB_RECURSE TIME_SOURCES "*.c")
add_library(kernel_time STATIC ${TIME_SOURCES})
