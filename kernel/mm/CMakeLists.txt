# kernel/mm 内存管理模块

# 递归查找当前目录及其所有子目录中的所有.c文件
file(GLOB_RECURSE MM_SOURCES "*.c")

# 创建内存管理模块静态库
add_library(kernel_mm STATIC ${MM_SOURCES})

target_include_directories(kernel_mm PRIVATE .)
