/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// 时钟事件结构体
typedef struct clockevent_struct {
    const char *name;   // 设备名称

    void (*shutdown)(void); // 停止
    void (*set_oneshot)(void);  // 设置为单次中断模式
    void (*set_periodic)(void); // 设置为周期模式

    void (*set_value)(uint64_t value);   // 定时设置

    uint64_t hz;    // 频率

    uint32_t mult;  // 乘数
    uint32_t shift; // 移位

    // 反向
    uint32_t mult_inv;
    uint32_t shift_inv;
    
    void (*event_handler)(void);    // 回调
}clockevent_struct;