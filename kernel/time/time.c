/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "timecycle.h"
#include <time.h>
#include <task.h>
#include <asm/processor.h>
#include <asm/smp.h>

// 睡眠超时回调参数
struct sleep_timeout_data {
    struct task_struct *task;
};

struct timespec time_boot_unix = {0};

static inline uint32_t bcd_to_bin(uint32_t bcd) {
    return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
}

// 睡眠超时回调函数
static void sleep_timeout_callback(void *data) {
    struct sleep_timeout_data *sd = (struct sleep_timeout_data *)data;
    task_wakeup(sd->task);
}

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
    struct task_struct *current = smp_get_task_current();
    struct clockevent_timer *timer = clockevent_timer_alloc();

    // 若定时器分配失败，降级为忙等待
    if (!timer) {
        clocksource_handle_t cs = clocksource_get(NULL);
        if (!cs) return;

        uint64_t start_ns = clocksource_read(cs);
        uint64_t end_ns = start_ns + timecycle_msec_to_ns(msec);

        while (clocksource_read(cs) < end_ns) {
            cpu_pause();
        }
        return;
    }

    struct sleep_timeout_data sd = { .task = current };

    clockevent_timer_init_callback(timer, sleep_timeout_callback, &sd);
    clockevent_timer_add(timer, timecycle_msec_to_ns(msec));

    // 不可中断睡眠，等待定时器唤醒
    task_sleep(false);

    // 被唤醒后释放定时器资源
    clockevent_timer_free(timer);
}

/**
 * 更新最后一次记录的时间
 * 
 * @param now 最后一次的时间
 */
void time_update(uint64_t now) {
    smp_set_last_ns(smp_get_current_ns());
    smp_set_current_ns(now);
}

/**
 * 获取最后一次与上一次时间的差值（ns）
 * 
 * @return 纳秒
 */
uint64_t time_delta(void) {
    return smp_get_current_ns() - smp_get_last_ns();
}

// 计算系统启动时的unix时间戳
void time_get_boot_unix(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
    uint32_t year, mon, day, hour, min, sec, days;

    year = bcd_to_bin(bootboot->datetime[0]) * 100 + bcd_to_bin(bootboot->datetime[1]);
    mon = bcd_to_bin(bootboot->datetime[2]);
    day = bcd_to_bin(bootboot->datetime[3]);
    hour = bcd_to_bin(bootboot->datetime[4]);
    min = bcd_to_bin(bootboot->datetime[5]);
    sec = bcd_to_bin(bootboot->datetime[6]);

    if (mon <= 2) { mon += 12; year--; }
    days = 365 * year + year / 4 - year / 100 + year / 400
           + (153 * (mon + 1)) / 5 + day - 719469;

    time_boot_unix.tv_sec  = (time_t)(days * 86400ULL + hour * 3600 + min * 60 + sec);
    time_boot_unix.tv_nsec = 0;
}

// 获取当前系统时间
void time_get(struct timespec *ts) {
    struct timespec uptime;
    
    // 获取启动后经过的纳秒数
    uptime.tv_sec = 0;
    uptime.tv_nsec = (time_t)clocksource_default_read();
    
    // 当前时间 = 启动基准时间 + 运行时间
    ts->tv_sec = time_boot_unix.tv_sec + uptime.tv_sec;
    ts->tv_nsec = time_boot_unix.tv_nsec + uptime.tv_nsec;
    
    // 处理纳秒进位
    if (ts->tv_nsec >= NSEC_PER_SEC) {
        ts->tv_nsec -= NSEC_PER_SEC;
        ts->tv_sec++;
    }
}