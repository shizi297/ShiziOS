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
    clocksource_handle_t cs = clocksource_get(NULL);
    if (!cs) return;
    uint64_t start_ns = clocksource_read(cs);
    uint64_t end_ns = start_ns + timecycle_usec_to_ns(usec);
    while (clocksource_read(cs) < end_ns) {
        cpu_pause();
    }
}

/**
 * 睡眠
 * 
 * @param msec 睡眠的毫秒数
 */
void time_sleep(uint64_t msec) {
    clocksource_handle_t cs = clocksource_get(NULL);
    if (!cs) return;
    uint64_t start_ns = clocksource_read(cs);
    uint64_t end_ns = start_ns + timecycle_msec_to_ns(msec);
    while (clocksource_read(cs) < end_ns) {
        cpu_pause();
    }
}