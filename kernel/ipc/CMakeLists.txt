# kernel/ipc IPC模块
file(GLOB_RECURSE IPC_SOURCES "*.c")
add_library(kernel_ipc STATIC ${IPC_SOURCES})
