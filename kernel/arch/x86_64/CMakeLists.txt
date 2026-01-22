# kernel/arch/x86_64 架构模块

file(GLOB_RECURSE X86_C_SOURCES "*.c")
file(GLOB_RECURSE X86_ASM_SOURCES "*.S" "*.s")

# 创建x86_64架构模块静态库
add_library(x86_64_arch STATIC 
    ${X86_C_SOURCES}
    ${X86_ASM_SOURCES}
)

# 为x86_64架构模块添加私有包含路径
target_include_directories(x86_64_arch PRIVATE .)

set_source_files_properties(${X86_ASM_SOURCES} PROPERTIES LANGUAGE C)