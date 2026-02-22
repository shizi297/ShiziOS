/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// 注册时的标志位
typedef enum {
    IOAPIC_MASK         = 1 << 0,   // 中断是否屏蔽
    IOAPIC_POLARITY     = 1 << 1,   // 有效的电平
    IOAPIC_TRIG         = 1 << 2,   // 触发模式 0 = 边缘， 1 = 水平
} ioapic_flags_t;

/**
 * 初始化ioapic
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool ioapic_init(void);

/**
 * 注册指定的gsi的中断
 * 
 * @param gsi 全局中断号
 * @param vector 中断向量号
 * @param dest 目标cpu的apicid
 * @param flags 设置标志
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool ioapic_register_gsi(
    uint32_t gsi, 
    uint8_t vector, 
    uint32_t dest, 
    ioapic_flags_t flags
);

/**
 * 屏蔽指定gsi的中断
 * 
 * @return gsi 全局中断号
 */
void ioapic_mask_gsi(uint32_t gsi);

/**
 * 取消屏蔽gsi的中断
 * 
 * @return gsi 全局中断号
 */
void ioapic_unmask_gsi(uint32_t gsi);