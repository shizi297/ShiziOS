# kernel/fs/ext2 ext2文件系统模块

file(GLOB_RECURSE EXT2_SOURCES "*.c")
add_library(ext2_objects OBJECT ${EXT2_SOURCES})

target_sources(kernel_fs PRIVATE $<TARGET_OBJECTS:ext2_objects>)