# kernel/drivers 通用驱动模块

# 递归查找当前目录及其所有子目录中的所有.c文件
file(GLOB_RECURSE DRIVERS_SOURCES "*.c")

# 创建静态库
add_library(kernel_drivers STATIC ${DRIVERS_SOURCES})

# 添加私有包含路径
target_include_directories(kernel_drivers PRIVATE .)