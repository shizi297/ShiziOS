# kernel/lib 库模块
file(GLOB_RECURSE LIB_SOURCES "*.c")
add_library(kernel_lib STATIC ${LIB_SOURCES})