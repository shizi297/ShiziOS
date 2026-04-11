/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <fault.h>
#include <processor.h>
#include <asm/serial.h>
#include <apic.h>
#include <time.h>
#include <task.h>
#include <asm/smp.h>

#define IRQ_PRINT(fmt, ...) \
    printk("[IRQ] " fmt, ##__VA_ARGS__)

#define IRQ_WARN(fmt, ...) \
    printk("[IRQ] WARNING : " fmt, ##__VA_ARGS__)

// 系统调用入口
void syscall_entry(void) {
    // TODO : 解析调用号，并调用对应的处理函数，处理完后调用rcu_state_run函数
}

/**
 * 中断/异常/信号处理入口
 *
 * @param regs 发生时的寄存器状态
 */
void irq_entry(struct pt_regs *regs) {
    // 获取时间
    uint64_t now = smp_get_timestamp();
    
    /**
     * 更新时间
     * 更新后时间段为上次内核态出口到此次内核态入口
     * 即用户态运行时间
     */
    time_update(now);

    // 把用户态的时间计入任务
    uint64_t user_delta = time_delta();
    task_add_current_tick(user_delta);   

    uint64_t vector = regs->vector;
    bool is_vector = (vector < 32);

    if (irq_table[vector]) {
        void (*irq)(struct pt_regs *regs) = (void (*)(struct pt_regs *regs))irq_table[vector];
        irq(regs);
    } else {
        if (is_vector) {
            printp("CPU ERROR\n");
        } else {
            IRQ_WARN("NO HANDLER FOR VECTOR\n");
        }
    }

    // 统一更新时间戳
    now = smp_get_timestamp();
    time_update(now);

    if (is_vector) {
        // 异常处理完成，累加当前任务时间
        uint64_t kernel_delta = time_delta();
        task_add_current_tick(kernel_delta);
    } else {
        // 外部中断结束，只更新时间戳，不累加
        apic_eoi();
    }

    rcu_state_run();

    if (smp_check_need_sched()) task_sched();
}