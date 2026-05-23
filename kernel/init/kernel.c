/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <mm/init.h>
#include <asm/smp.h>
#include <asm/processor.h>
#include <kernel.h>
#include <asm/serial.h>  
#include <time.h>
#include <drivers.h>
#include <config.h>

#define KERNEL_PRINT(fmt, ...) \
    printk("[KERNEL] " fmt, ##__VA_ARGS__)

#define KERNEL_PANIC(fmt, ...) \
    printp("[KERNEL] ERROR : " fmt, ##__VA_ARGS__)

__attribute__((section(".bss"), aligned(16))) 
uint8_t bp_stack[INIT_STACK_BYTE];

extern uint8_t cpu_ready_flag;
static uint32_t bp_logical_id = 0;
static uint32_t bp_apic_id = 0;

__attribute__((noreturn))
void kernel_main(uint32_t logical_id, uint32_t apic_id) {
    KERNEL_PRINT("ShiziOS KERNEL v%s\n", KERNEL_VERSION);

    bp_logical_id = logical_id;
    bp_apic_id = bp_apic_id;

    uint64_t bp_stack_top = (uint64_t)(bp_stack + INIT_STACK_BYTE) - 128;

    processor_set_stack(bp_stack_top);

    memory_init();

    processor_init();
    gdte *gdt_temp_addr = get_gdt_temp();
    struct idt_gate* idt_temp_addr = get_idt_temp();
    struct tss* tss_temp_addr = get_tss_temp(); 

    smp_data_init(gdt_temp_addr, idt_temp_addr, tss_temp_addr);
    
    // 初始化time系统
    time_init();

    // 初始化驱动框架的数据
    if (!drivers_data_init()) KERNEL_PANIC("drivers data init failed");

    // 设置标志位让ap启动
    cpu_ready_flag = 1;

    smp_init(bp_logical_id, bp_apic_id);

    while (1) {
        cpu_pause();
    }
}