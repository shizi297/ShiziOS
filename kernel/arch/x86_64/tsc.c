/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <processor.h>
#include <time.h>

// 先使用固定频率
#define TSC_HZ 2400000000ULL

/**
 * 初始化tsc
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool tsc_init(void) {
    uint64_t hz = TSC_HZ; 

    // 注册到时钟源框架
    clocksource_register(
        "tsc",
        rdtsc,
        hz
    );

    return true;
}