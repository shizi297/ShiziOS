cmake_minimum_required(VERSION 3.15)
project(ShiziOS)

# 工具链配置
set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_ASM_NASM_COMPILER nasm)
enable_language(ASM_NASM)

# 编译选项
set(CMAKE_C_FLAGS "-std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=kernel -O2")
set(CMAKE_ASM_NASM_FLAGS "-f elf64")
set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -static -T ${CMAKE_SOURCE_DIR}/linker.ld")

# 全局路径
include_directories(
    kernel/include
    kernel
    kernel/arch/x86_64
    config
    kernel/arch/x86_64/drivers
    kernel/drivers
)

# 编译kernel目录下的文件
file(GLOB KERNEL_C_SOURCES "kernel/*.c")
file(GLOB KERNEL_ASM_SOURCES "kernel/*.asm" "kernel/*.S" "kernel/*.s")

add_library(kernel_main STATIC ${KERNEL_C_SOURCES} ${KERNEL_ASM_SOURCES})

if(KERNEL_ASM_SOURCES)
    set_source_files_properties(${KERNEL_ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)
endif()

# 添加模块子目录
add_subdirectory(kernel/arch/x86_64)  # x86_64架构模块
add_subdirectory(kernel/drivers)      #通用驱动模块
add_subdirectory(kernel/mm)           # 内存管理模块

# 链接生成内核
set(EMPTY_SOURCE ${CMAKE_BINARY_DIR}/empty.c)
file(WRITE ${EMPTY_SOURCE} "// Empty source file for linking libraries\n")

add_executable(kernel.elf ${EMPTY_SOURCE})

# 链接所有模块库
target_link_libraries(kernel.elf
    "-Wl,--start-group"
    kernel_main
    x86_64_arch
    kernel_drivers  
    kernel_mm
    "-Wl,--end-group"
)

# 生成磁盘镜像
add_custom_target(img
    DEPENDS kernel.elf
    COMMAND mkdir -p initrd_root/sys
    COMMAND cp ${CMAKE_BINARY_DIR}/kernel.elf initrd_root/sys/core
    COMMAND cp ${CMAKE_SOURCE_DIR}/config/CONFIG initrd_root/sys/config
    COMMAND ${CMAKE_SOURCE_DIR}/disk/mkbootimg ${CMAKE_SOURCE_DIR}/config/ShiziOS.json ${CMAKE_SOURCE_DIR}/disk/shizios.img
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 运行QEMU
add_custom_target(run
    DEPENDS img
    COMMAND qemu-system-x86_64 
        -bios /usr/share/ovmf/OVMF.fd 
        -drive file=${CMAKE_SOURCE_DIR}/disk/shizios.img,format=raw,if=virtio 
        -m 2G 
        -cpu qemu64 
        -smp 2 
        -device virtio-gpu-pci,xres=1920,yres=1080 
        -net none 
        -serial stdio
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 输出目录配置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})