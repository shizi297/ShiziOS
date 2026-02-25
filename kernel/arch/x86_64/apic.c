/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <processor.h>
#include <time.h>
#include <bootboot.h>
#include <smp.h>

/**
 * 单播IPI发送接口
 * 处理单播IPI请求的队列管理和发送
 *
 * @param apic_id 目标CPU的apic_id
 * @param vector 中断向量号
 */
void apic_send_ipi(uint32_t apic_id, uint32_t vector) {
    processor_send_ipi(apic_id, vector);
}

/**
 * 广播IPI发送接口
 * 向所有CPU核心（除自身）发送IPI
 *
 * @param vector 中断向量号
 */
void apic_send_ipi_all(uint32_t vector) {
    processor_send_ipi_all(vector, NO_ONESELF);
}

// 用于通知apic当前中断已完成，需要在所有中断处理后面添加
void apic_eoi(void) {
    processor_eoi();
}

// 设置apic的数值
void set_apic_timer(uint64_t value) {
    apic_set_tsc_deadline(value);
}

// 早期初始化，用于启动x2apic模式，让系统可以使用一些东西
void apic_boot_init(void) {
    // 设置为x2apic模式
    set_apic_x2apic();
}


// 关闭apic定时器（屏蔽中断）
static void apic_timer_shutdown(void) {
    apic_set_lvt_timer(IRQ_APIC, APIC_TSC_DEADLINE, 1);
}

// 设置为单次模式（取消屏蔽）
static void apic_timer_set_oneshot(void) {
    apic_set_lvt_timer(IRQ_APIC, APIC_TSC_DEADLINE, 0);
}

// 设置下一次中断的计数值
static void apic_timer_set_value(uint64_t value) {
    uint64_t now = rdtsc();
    apic_set_tsc_deadline(now + value);
}

// 定时器中断处理函数
static void apic_timer_irq(struct pt_regs *regs) {
    void (*handler)(void);
    get_event_handler("apic", &handler);
    if (handler) {
        handler();
    }
}

// 将当前cpu的apic定时器注册到时钟事件框架
static bool apic_clockevent_register(void) {
    uint64_t hz;

    // 从时钟源获取 TSC 频率
    if (!clocksource_get_dev_hz("tsc", &hz)) {
        return false;
    }

    clockevent_register(
        "apic",
        apic_timer_shutdown,
        apic_timer_set_oneshot,
        NULL,                       // 不支持周期模式
        apic_timer_set_value,
        hz
    );

    return true;
}

// 初始化apic
bool apic_init(void) {
    // 设置为TSC DEADLINE模式，中断号为IRQ_APIC
    apic_set_lvt_timer(IRQ_APIC, APIC_TSC_DEADLINE, 0);

    // 设置svr，让系统可以接收外部中断
    apic_set_svr(EXC_SPUR); 

    // 注册当前cpu的apic定时器到时钟事件框架
    if(!apic_clockevent_register()) {
        return false;
    }

    // 如果是BSP，注册中断处理函数
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
    if (get_apic_id() == bootboot->bspid) {
        smp_irq_register_handler(IRQ_APIC, (uint64_t)apic_timer_irq);
    }

    return true;
}