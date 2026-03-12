/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * 单播IPI发送接口
 * 处理单播IPI请求的队列管理和发送
 *
 * @param apic_id 目标CPU的apic_id
 * @param vector 中断向量号
 */
void apic_send_ipi(uint32_t apic_id, uint32_t vector);

/**
 * 广播IPI发送接口
 * 向所有CPU核心（除自身）发送IPI
 *
 * @param vector 中断向量号
 */
void apic_send_ipi_all(uint32_t vector);

// 用于通知apic当前中断已完成，需要在所有中断处理后面添加
void apic_eoi(void);

// 早期初始化，用于启动x2apic模式，让系统可以使用一些东西
void apic_boot_init(void);

// 初始化apic
bool apic_init(void);

// 获取当前cpu的apicid
uint32_t apic_get_id(void);