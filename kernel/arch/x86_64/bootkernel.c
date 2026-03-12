/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <bootboot.h>
#include <stdbool.h>
#include <serial.h>
#include <spinlock.h>
#include <processor.h>
#include <apic.h>

#define BOOTKERNEL_PRINT(str) \
    serial_puts("[BOOTKERNEL] " str)

#define NEWLINE \
    serial_puts("\n")

#define BOOTKERNEL_PANIC(str) \
    panic("[BOOTKERNEL] ERROR:" str "\n")

#define BOOTKERNEL_DEC(value) \
    serial_put_dec(value)

#define CPU_SUPPORT \
    serial_puts("CPU supports running this system")

#define CPU_NOT_SUPPORT \
    BOOTKERNEL_PANIC("CPU does not support running this system")
    
extern void kernel_main(uint32_t logical_id, uint32_t apic_id);
extern void smp_init(uint32_t logical_id, uint32_t apic_id);

// 对齐缓存行，防止伪共享
__attribute__((aligned(64)))
/*
 * CPU就绪标志
 * 所有CPU在启动时等待该标志被置位
 * 当变为1时
 * 启动AP CPU继续执行
 */
volatile uint8_t cpu_ready_flag = 0;

/*
 * 用于早期无法确认bp时只需要单次初始化的步骤
 * 初始化后为ture
 * 其他cpu不会再次初始化
 */
bool boot_init = false;
spinlock_t boot_init_spin = SPIN_LOCK_INIT;

// logical_id_raw是bootboot引导传的当前逻辑cpuid
__attribute__((noreturn))
void _start(uint64_t logical_id_raw) {
    uint32_t logical_id = (uint32_t)logical_id_raw;
    uint32_t apic_id = 0;
    bool bpcpu_logical_flag = false;
    
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    // bootboot在加载内核前已经初始化串口，这里不初始化

    spin_lock(&boot_init_spin);
    apic_boot_init();
    if (!boot_init) {
        boot_init = true;
    }
    spin_unlock(&boot_init_spin);

    apic_id = apic_get_id();

    // 计算是否是BP CPU
    if (apic_id == bootboot->bspid) {
        bpcpu_logical_flag = true;
    }

    if (bpcpu_logical_flag) {
        BOOTKERNEL_PRINT("BP CPU logical_id : ");
        BOOTKERNEL_DEC(logical_id);
        NEWLINE;
        BOOTKERNEL_PRINT("BP CPU APIC_ID : ");
        BOOTKERNEL_DEC(apic_id);
        NEWLINE;
    } 

    // 数据错误
    if (logical_id >= bootboot->numcores) {
        BOOTKERNEL_PANIC("Logical CPU ID exceeds max cpu count");
    }

    /*
     * 检测硬件是否支持运行该系统
     * 如果支持
     * 设置cr4让系统能够使用一些东西
     * 不支持panic
     */
    if (!cpuid_fsgsbase()) {
        CPU_NOT_SUPPORT;
    }

    set_cr4();
    
    // BP CPU执行内核初始化
    if (bpcpu_logical_flag) {
        kernel_main(logical_id, apic_id);
    }
    
    // AP CPU等待初始化完成
    while (cpu_ready_flag == 0) {
        cpu_pause();
    }
    
    // 让AP CPU执行内核初始化
    smp_init(logical_id, apic_id);
   
    // 不应该到达这里
    __builtin_unreachable();
}