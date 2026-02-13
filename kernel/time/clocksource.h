/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#ifndef CLOCKSOURCE_H
#define CLOCKSOURCE_H

#include <stdint.h>
#include <stdbool.h>

// 时钟源结构体
typedef struct {
    const char *name;   // 设备名称   
    uint64_t (*read)(void); // 获取时钟的值

    uint64_t hz;    // 频率

    uint32_t mult;  // 乘数
    uint32_t shift; // 移位

    // 反向
    uint32_t mult_inv;
    uint32_t shift_inv;
}clocksource_struct;

// 时钟源框架初始化
void clocksource_init(void);

/**
 * 获取时钟源的值(返回ns)
 * 
 * @param name 时钟名称，当这个为NULL时，使用精度最高的设备
 * 
 * @return 成功：true
 * @return 失败: false
 */
bool clocksource_read(char *name, uint64_t *value);

/**
 * 注册时钟到时钟源框架
 * 
 * @param name 时钟名称
 * @param read 读取时钟源值的函数指针
 * @param hz 时钟源频率
 */
void clocksource_register(
    char *name, 
    uint64_t (*read)(void),
    uint64_t hz
);

#endif  // CLOCKSOURCE_H