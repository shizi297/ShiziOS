# 内核主构建文件

# 内核专用编译选项
set(KERNEL_ARCH_FLAGS -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -fno-pic)
add_compile_options(${BASE_FLAGS} ${EXTRA_FLAGS} ${KERNEL_ARCH_FLAGS})
add_compile_definitions(KERNEL)

# 内核专用包含路径
include_directories(
    include                     # kernel/include
    arch/x86_64/include         # 架构内部头文件
    ${CMAKE_CURRENT_SOURCE_DIR} # kernel 根目录
    arch/x86_64                 # arch/x86_64 目录
    drivers/acpi/uacpi/include  # uACPI 头文件
)

# 添加模块子目录
add_subdirectory(arch/x86_64)      # x86_64架构模块
add_subdirectory(arch/x86_64/mm)   # x86_64架构特定内存管理
add_subdirectory(drivers)          # 通用驱动模块
add_subdirectory(mm)               # 通用内存管理模块
add_subdirectory(lib)              # 库模块
add_subdirectory(time)             # 时间管理模块
add_subdirectory(task)             # 任务管理模块
add_subdirectory(init)             # 内核初始化模块
add_subdirectory(ipc)              # IPC模块
add_subdirectory(sync)             # 同步原语模块
add_subdirectory(fs)               # 文件系统模块

# 测试模块
if(TEST)
    add_subdirectory(test)          # 测试模块
endif()

# 链接生成内核
set(EMPTY_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/empty.c)
file(WRITE ${EMPTY_SOURCE} "// Empty source file for linking libraries\n")

add_executable(kernel.elf ${EMPTY_SOURCE})

# 设置内核链接选项
target_link_options(kernel.elf PRIVATE -nostdlib -no-pie -T ${CMAKE_SOURCE_DIR}/linker.ld -Wl,-z,start-stop-visibility=default)

target_link_libraries(kernel.elf
    "-Wl,--start-group"
    kernel_init
    x86_64_arch
    x86_64_mm
    kernel_drivers
    kernel_mm
    kernel_lib
    kernel_time
    kernel_task
    kernel_ipc
    kernel_sync
    "-Wl,--whole-archive"
    kernel_fs
    "-Wl,--no-whole-archive"
    libc
    "-Wl,--end-group"
)

if(TEST)
    target_link_libraries(kernel.elf kernel_test)
endif()