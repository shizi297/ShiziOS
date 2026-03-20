cmake_minimum_required(VERSION 3.15)
project(ShiziOS)

# 工具链配置
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET x86_64-elf)

# 查找并启用 ccache
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    message(STATUS "Using ccache launcher for C compiler: ccache ${CMAKE_C_COMPILER}")
else()
    message(STATUS "No ccache found, using direct compiler: ${CMAKE_C_COMPILER}")
endif()

# 查找最快的链接器：优先 lld，否则默认
find_program(LLD_FOUND ld.lld)
if(LLD_FOUND)
    add_link_options("-fuse-ld=lld")
    message(STATUS "Using lld linker with multi-threading: ${LLD_FOUND}")
else()
    message(STATUS "No lld found, using default linker")
endif()

# 启用链接时优化 (LTO)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# 获取 CPU 核心数以加速链接（仅对 lld 有效）
include(ProcessorCount)
ProcessorCount(CPU_CORES)
if(NOT CPU_CORES)
  set(CPU_CORES 8)
endif()
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  add_link_options(-Wl,--threads=${CPU_CORES})
endif()

# 基础编译选项（所有模式共用）
set(BASE_FLAGS "-std=gnu11 -ffreestanding -fno-pic -mno-red-zone -mcmodel=kernel -O2 -mgeneral-regs-only")

# 调试模式选项
option(DEBUG "Enable debug mode" OFF)

if(DEBUG)
    set(EXTRA_FLAGS "-fstack-protector-all -g -fstack-check -fno-omit-frame-pointer -DDEBUG")
else()
    set(EXTRA_FLAGS "-fstack-protector-strong")
endif()

set(CMAKE_C_FLAGS "${BASE_FLAGS} ${EXTRA_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -static -T ${CMAKE_SOURCE_DIR}/linker.ld")

# 全局路径
include_directories(
    kernel/include
    kernel
    kernel/arch/x86_64
    config
    kernel/arch/x86_64/drivers
    kernel/drivers
    kernel/arch/x86_64/include
    kernel/drivers/acpi/uacpi/include/
)

# 添加模块子目录
add_subdirectory(kernel/arch/x86_64)      # x86_64架构模块
add_subdirectory(kernel/arch/x86_64/mm)   # x86_64架构特定内存管理
add_subdirectory(kernel/drivers)          # 通用驱动模块
add_subdirectory(kernel/mm)               # 通用内存管理模块
add_subdirectory(kernel/lib)               # 库模块
add_subdirectory(kernel/time)              # 时间管理模块
add_subdirectory(kernel/task)              # 任务管理模块
add_subdirectory(kernel/init)              # 内核初始化模块
add_subdirectory(kernel/ipc)               # IPC模块

# 链接生成内核
set(EMPTY_SOURCE ${CMAKE_BINARY_DIR}/empty.c)
file(WRITE ${EMPTY_SOURCE} "// Empty source file for linking libraries\n")

add_executable(kernel.elf ${EMPTY_SOURCE})

# 链接所有模块库
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
    "-Wl,--end-group"
)

# 生成磁盘镜像
add_custom_target(img
    DEPENDS kernel.elf
    COMMAND mkdir -p initrd_root/sys
    COMMAND cp ${CMAKE_BINARY_DIR}/kernel.elf initrd_root/sys/core
    COMMAND cp ${CMAKE_SOURCE_DIR}/config/CONFIG initrd_root/sys/config
    COMMAND ${CMAKE_SOURCE_DIR}/disk/mkbootimg ${CMAKE_SOURCE_DIR}/config/ShiziOS.json ${CMAKE_SOURCE_DIR}/disk/shizios.img
    COMMAND rm -rf initrd_root
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 运行QEMU
add_custom_target(run
    DEPENDS img
    COMMAND qemu-system-x86_64 
        -bios /usr/share/ovmf/OVMF.fd 
        -drive file=${CMAKE_SOURCE_DIR}/disk/shizios.img,format=raw,if=virtio 
        -m 2G 
        -smp 2 
        -cpu max,tsc-frequency=2400000000
        -device virtio-gpu-pci,xres=1280,yres=720 
        -net none 
        -serial stdio
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 用内存生成磁盘镜像
add_custom_target(ram-img
    DEPENDS kernel.elf
    COMMAND mkdir -p /dev/shm/shizios/initrd_root/sys
    COMMAND cp ${CMAKE_BINARY_DIR}/kernel.elf /dev/shm/shizios/initrd_root/sys/core
    COMMAND cp ${CMAKE_SOURCE_DIR}/config/CONFIG /dev/shm/shizios/initrd_root/sys/config
    COMMAND ${CMAKE_SOURCE_DIR}/disk/mkbootimg ${CMAKE_SOURCE_DIR}/config/ShiziOS.json /dev/shm/shizios/shizios.img
    COMMAND rm -rf /dev/shm/shizios/initrd_root
    WORKING_DIRECTORY /dev/shm/shizios
)

# 用内存的img运行qemu
add_custom_target(ram-run
    DEPENDS ram-img
    COMMAND qemu-system-x86_64 
        -bios /usr/share/ovmf/OVMF.fd 
        -drive file=/dev/shm/shizios/shizios.img,format=raw,if=virtio 
        -m 2G 
        -smp 2 
        -cpu max,tsc-frequency=2400000000
        -device virtio-gpu-pci,xres=1280,yres=720 
        -net none 
        -serial stdio
)

# 输出目录配置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})