/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <fault.h>
#include <processor.h>
#include <serial.h>

#define IRQ_PRINT(str) \
    serial_puts("[IRQ] " str "\n")

#define IRQ_WARN(str) \
    serial_puts("[IRQ] WARNING : " str "\n")

// 系统调用入口
void syscall_entry(void) {
    // TODO
}

/**
 * 中断/异常/信号处理入口
 *
 * @param regs 发生时的寄存器状态
 */
void irq_entry(struct pt_regs *regs) {
    uint64_t vector = regs->vector;
    uint64_t error_code = regs->error_code;

    // 判断是异常还是普通中断
    bool is_vector = false;
    if (vector < 32) is_vector = true;

    if (irq_table[vector]) {
        // 调用对应的处理程序
        void (*irq)(struct pt_regs *regs) = (void (*)(struct pt_regs *regs))irq_table[vector];
        irq(regs);
    } else {
        // 没有中断处理程序
        if (is_vector) {
            // 处理器异常
            panic("CPU ERROR");
        } else {
            // 未注册的中断
            IRQ_WARN("NO HANDLER FOR VECTOR");
        }
    }

    // 发送EOI，通知APIC中断处理已完成，对于异常不需要发送EOI
    if (!is_vector) processor_eoi();
}   