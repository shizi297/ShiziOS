/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "timecycle.h"
#include <time.h>
#include <processor.h>

/**
 * 忙等待
 * 
 * @param usec 等待的微秒数
 */
void time_stall(uint8_t usec) {
    uint64_t start_ns = 0;
    if (!clocksource_read(NULL, &start_ns)) {
        return;
    }
    uint64_t now = 0;
    uint64_t end_ns = start_ns + timecycle_usec_to_ns(usec);
    while (clocksource_read(NULL, &now) && now < end_ns) {
        // 忙等待直到达到指定时间
        cpu_pause();
    }
}

/**
 * 睡眠
 * 
 * @param msec 睡眠的毫秒数
 */
void time_sleep(uint64_t msec) {
    // 这里先使用忙等待实现睡眠
    uint64_t start_ns = 0;
    if (!clocksource_read(NULL, &start_ns)) {
        return;
    }
    uint64_t end_ns = start_ns + timecycle_msec_to_ns(msec);
    uint64_t now = 0;
    while (clocksource_read(NULL, &now) && now < end_ns) {
        cpu_pause();
    }
}