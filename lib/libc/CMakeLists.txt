# C 标准库

# 使用根目录定义的用户态专用标志
add_compile_options(${BASE_FLAGS} ${EXTRA_FLAGS} ${USER_EXTRA_FLAGS})

# 添加当前目录为包含路径s
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 递归查找 C 源文件
file(GLOB_RECURSE LIBC_C_SOURCES "*.c")

# 递归查找汇编源文件
file(GLOB_RECURSE LIBC_ASM_SOURCES "*.s" "*.S")

# 创建静态库
add_library(libc STATIC 
    ${LIBC_C_SOURCES}
    ${LIBC_ASM_SOURCES}
)

# 强制使用 C 编译器处理汇编文件，并抑制未使用的命令行参数警告
if(LIBC_ASM_SOURCES)
    set_source_files_properties(${LIBC_ASM_SOURCES} PROPERTIES LANGUAGE C COMPILE_FLAGS "-Wno-unused-command-line-argument")
endif()