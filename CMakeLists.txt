cmake_minimum_required(VERSION 3.15)

set(CMAKE_SYSTEM_NAME "Generic")
set(CMAKE_SYSTEM_PROCESSOR "x86_64")

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

# 构建选项
option(OPTIMIZE "Enable optimization (LTO and -O2)" ON)          # 优化开关
option(DEBUG    "Enable debug mode" OFF)                         # 调试开关
option(STACK    "Enable stronger stack protection (-fstack-protector-all)" OFF)  # 栈保护强度
option(TEST     "Enable test module" OFF)                        # 测试模块开关
set(QEMU "" CACHE STRING "Additional QEMU arguments (space-separated)")    # QEMU 额外参数

# 根据 OPTIMIZE 设置优化级别和 LTO
if(OPTIMIZE)
    set(OPT_LEVEL -O2)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    set(OPT_LEVEL -O0)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

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
set(BASE_FLAGS -std=gnu11 -ffreestanding -fno-strict-aliasing ${OPT_LEVEL})

# 用户态专用编译选项
set(USER_EXTRA_FLAGS -fPIC -nostdlib -nostdlibinc)

# 栈保护标志
if(STACK)
    set(STACK_PROTECTOR_FLAGS -fstack-protector-all)
else()
    set(STACK_PROTECTOR_FLAGS -fstack-protector-strong)
endif()

# 调试标志
if(DEBUG)
    set(EXTRA_FLAGS -g -fstack-check -DDEBUG ${STACK_PROTECTOR_FLAGS})
else()
    set(EXTRA_FLAGS ${STACK_PROTECTOR_FLAGS})
endif()

# 全局路径（仅公共头文件）
include_directories(
    include
    include/libc
    config
)

# 添加子目录
add_subdirectory(kernel)          # 内核模块
add_subdirectory(lib/libc)        # C 标准库

# 生成磁盘镜像
add_custom_target(img
    DEPENDS kernel.elf
    COMMAND mkdir -p initrd_root/sys
    COMMAND cp ${CMAKE_BINARY_DIR}/kernel/kernel.elf initrd_root/sys/core
    COMMAND cp ${CMAKE_SOURCE_DIR}/config/CONFIG initrd_root/sys/config
    COMMAND ${CMAKE_SOURCE_DIR}/disk/mkbootimg ${CMAKE_SOURCE_DIR}/config/ShiziOS.json ${CMAKE_SOURCE_DIR}/disk/shizios.img
    COMMAND rm -rf initrd_root
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 解析 QEMU 参数列表
separate_arguments(QEMU_LIST UNIX_COMMAND "${QEMU}")
set(USER_SMP_SPECIFIED FALSE)
foreach(arg ${QEMU_LIST})
    if("${arg}" STREQUAL "-smp")
        set(USER_SMP_SPECIFIED TRUE)
        break()
    endif()
endforeach()
if(NOT USER_SMP_SPECIFIED)
    set(DEFAULT_SMP -smp 2)
else()
    set(DEFAULT_SMP "")
endif()

# 运行QEMU
add_custom_target(run
    DEPENDS img
    COMMAND qemu-system-x86_64 
        -machine q35
        -bios /usr/share/ovmf/OVMF.fd 
        -drive file=${CMAKE_SOURCE_DIR}/disk/shizios.img,format=raw,if=virtio 
        -m 2G 
        ${DEFAULT_SMP}
        -cpu max,tsc-frequency=2400000000
        -device virtio-gpu-pci,xres=1280,yres=720 
        -net none 
        -serial stdio
        ${QEMU_LIST}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 用内存生成磁盘镜像
add_custom_target(ram-img
    DEPENDS kernel.elf
    COMMAND mkdir -p /dev/shm/shizios/initrd_root/sys
    COMMAND cp ${CMAKE_BINARY_DIR}/kernel/kernel.elf /dev/shm/shizios/initrd_root/sys/core
    COMMAND cp ${CMAKE_SOURCE_DIR}/config/CONFIG /dev/shm/shizios/initrd_root/sys/config
    COMMAND ${CMAKE_SOURCE_DIR}/disk/mkbootimg ${CMAKE_SOURCE_DIR}/config/ShiziOS.json /dev/shm/shizios/shizios.img
    COMMAND rm -rf /dev/shm/shizios/initrd_root
    WORKING_DIRECTORY /dev/shm/shizios
)

# 用内存的img运行qemu
add_custom_target(ram-run
    DEPENDS ram-img
    COMMAND qemu-system-x86_64 
        -machine q35
        -bios /usr/share/ovmf/OVMF.fd 
        -drive file=/dev/shm/shizios/shizios.img,format=raw,if=virtio 
        -m 2G 
        ${DEFAULT_SMP}
        -cpu max,tsc-frequency=2400000000
        -device virtio-gpu-pci,xres=1280,yres=720 
        -net none 
        -serial stdio
        ${QEMU_LIST}
    WORKING_DIRECTORY /dev/shm/shizios
)

# 输出目录配置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})