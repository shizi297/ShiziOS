# kernel/fs/tmpfs tmpfs文件系统模块

file(GLOB_RECURSE TMPFS_SOURCES "*.c")
add_library(tmpfs_objects OBJECT ${TMPFS_SOURCES})

target_sources(kernel_fs PRIVATE $<TARGET_OBJECTS:tmpfs_objects>)
