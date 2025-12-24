# kernel/arch/x86_64/mm 架构特定内存管理模块

# 递归查找当前目录及其所有子目录中的所有.c文件
file(GLOB_RECURSE X86_MM_SOURCES "*.c")
file(GLOB_RECURSE X86_MM_ASM_SOURCES "*.asm" "*.S" "*.s")

# 创建架构特定内存管理模块静态库
add_library(x86_64_mm STATIC 
    ${X86_MM_SOURCES}
    ${X86_MM_ASM_SOURCES}
)

# 处理汇编文件
if(X86_MM_ASM_SOURCES)
    set_source_files_properties(${X86_MM_ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)
endif()

target_include_directories(x86_64_mm PRIVATE .)