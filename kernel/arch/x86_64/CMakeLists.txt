# kernel/arch/x86_64 架构模块

# 递归查找当前目录及其所有子目录中的所有.c文件
file(GLOB_RECURSE X86_C_SOURCES "*.c")

# 递归查找当前目录及其所有子目录中的所有汇编文件
file(GLOB_RECURSE X86_ASM_SOURCES "*.asm" "*.S" "*.s")

# 创建x86_64架构模块静态库
add_library(x86_64_arch STATIC 
    ${X86_C_SOURCES}
    ${X86_ASM_SOURCES}
)

# 设置汇编文件使用NASM编译
if(X86_ASM_SOURCES)
    set_source_files_properties(${X86_ASM_SOURCES} PROPERTIES
        LANGUAGE ASM_NASM
    )
endif()

# 为x86_64架构模块添加私有包含路径
target_include_directories(x86_64_arch PRIVATE .)

# target_compile_options(x86_64_arch PRIVATE -O2)