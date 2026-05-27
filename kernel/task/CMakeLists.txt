# kernel/task 任务管理模块
file(GLOB_RECURSE TASK_SOURCES "*.c")
add_library(kernel_task STATIC ${TASK_SOURCES})
