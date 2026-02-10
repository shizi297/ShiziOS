/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#ifndef CLOCKSOURCE_H
#define CLOCKSOURCE_H

#include <stdint.h>
#include <stdbool.h>

// 时钟源结构体
struct clocksource {
    const char *name;   // 设备名称   
    uint64_t (*read)(void); // 获取时钟的值

    uint64_t hz;    // 频率

    uint32_t mult;  // 乘数
    uint32_t shift; // 移位

    // 反向
    uint32_t mult_inv;
    uint32_t shift_inv;
};

#endif  // CLOCKSOURCE_H