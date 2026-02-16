/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <mm/init.h>
#include <smp.h>
#include <processor.h>
#include <kernel.h>
#include <serial.h>  
#include <time.h>

extern void pit_init(void);
extern uint8_t cpu_ready_flag;

__attribute__((noreturn))
void kernel_main(uint32_t logical_id, uint32_t apic_id) {
    serial_puts("[KERNEL]ShiziOS KERNEL v");
    serial_puts(KERNEL_VERSION);
    serial_puts("\n");
    
    memory_init();

    processor_init();
    gdte *gdt_temp_addr = get_gdt_temp();
    struct idt_gate* idt_temp_addr = get_idt_temp();
    struct tss* tss_temp_addr = get_tss_temp(); 

    smp_data_init(gdt_temp_addr, idt_temp_addr, tss_temp_addr);
    
    // 初始化time系统
    time_init();

    // 设置标志位让ap启动
    cpu_ready_flag = 1;

    smp_init(logical_id, apic_id);

    while (1) {
        cpu_pause();
    }
}