/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <processor.h>

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

// 初始化apic
void apic_init(void) {
    // 设置为TSC DEADLINE模式，中断号为IRQ_APIC
    apic_set_lvt_timer(IRQ_APIC,APIC_TSC_DEADLINE,0);
}

