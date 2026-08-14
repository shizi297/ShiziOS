# kernel/exec/elf ELF 加载器模块

file(GLOB_RECURSE ELF_SOURCES "*.c")
add_library(elf_objects OBJECT ${ELF_SOURCES})

target_sources(kernel_exec PRIVATE $<TARGET_OBJECTS:elf_objects>)