/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <bootboot.h>
#include <stdbool.h>
#include <serial.h>
#include <spinlock.h>

#define BOOTKERNEL_PRINT(str) \
    serial_puts("[BOOTKERNEL] " str)

#define NEWLINE \
    serial_puts("\n")

#define BOOTKERNEL_PANIC(str) \
    panic("[BOOTKERNEL] ERROR:" str)

#define BOOTKERNEL_DEC(value) \
    serial_put_dec(value)
    
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
__attribute__((aligned(64)))

// 获取当前CPU的APIC ID
static inline uint32_t get_apic_id(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1));
    return ebx >> 24;
}

// CPU暂停,用于优化等待循环，防止过度占用执行资源
static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

// logical_id_raw是bootboot引导传的当前逻辑cpuid
__attribute__((noreturn))
void _start(uint64_t logical_id_raw) {
    uint32_t logical_id = (uint32_t)logical_id_raw;
    uint32_t apic_id;
    bool bpcpu_logical_flag = 0; 
    
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
    
    // 数据错误
    if (logical_id >= bootboot->numcores) {
        BOOTKERNEL_PANIC("Logical CPU ID exceeds max cpu count");
    }

    apic_id = get_apic_id();

    // 计算是否是BP CPU
    if (apic_id == bootboot->bspid) {
        bpcpu_logical_flag = 1;
    }

    if (bpcpu_logical_flag) {
        init_serial();

        BOOTKERNEL_PRINT("BP CPU logical_id : ");
        BOOTKERNEL_DEC(logical_id);
        NEWLINE;
        BOOTKERNEL_PRINT("BP CPU APIC_ID : ");
        BOOTKERNEL_DEC(apic_id);
        NEWLINE;
    } 

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