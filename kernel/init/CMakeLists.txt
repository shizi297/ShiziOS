# kernel/init 初始化模块
file(GLOB_RECURSE INIT_SOURCES "*.c")
add_library(kernel_init STATIC ${INIT_SOURCES})
