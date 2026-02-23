/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <processor.h>
#include <time.h>
#include <timecycle.h>

#define CHECK_TIME_MS 30

uint64_t tsc_init_value = 0;
volatile bool is_backcall = false;
volatile uint64_t hz = 0; 

/**
 * tsc回调函数
 * 用于计算tsc的频率
 */
void tsc_init_callback(void) {
    uint64_t tsc_end = rdtsc();
    uint64_t tsc_delta = tsc_end - tsc_init_value;
    uint64_t interval_ns = timecycle_msec_to_ns(CHECK_TIME_MS);
    hz = (uint64_t)(timecycle_s_to_ns(tsc_delta)) / interval_ns;

    event_handler_register(NULL, "pit");
    is_backcall = true;
}

/**
 * 初始化tsc
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool tsc_init(void) {
    // 使用pit检测tsc的频率
    event_handler_register(tsc_init_callback, "pit");
    tsc_init_value = rdtsc();

    // 设置为单次中断模式
    bool is_mode = clockevent_set_mode("pit", CLOCKEVENT_MODE_ONESHOT);
    if (!is_mode) {
        goto fail;
    }

    uint64_t ns = timecycle_msec_to_ns(CHECK_TIME_MS);

    bool is_value = set_value_to_dev(ns, "pit");
    if (!is_value) {
        goto fail;
    }

    // 死循环，等待调用回调
    while (1) {
        cpu_pause();
        if (is_backcall) break;
    }

    // 注册到时钟源框架
    clocksource_register(
        "tsc",
        rdtsc,
        hz
    );

    return true;

    fail:
        event_handler_register(NULL, "pit");
        return false;
}