/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <kio.h>

#define TIME_PRINT(str) \
    printk("[TIME]" str "\n")

typedef enum {
    CLOCKEVENT_MODE_SHUTDOWN,   // 关闭定时器
    CLOCKEVENT_MODE_ONESHOT,    // 单次模式
    CLOCKEVENT_MODE_PERIODIC,   // 周期模式
} clockevent_mode_t;

typedef struct clockevent_device* clockevent_handle_t;
typedef struct clocksource_device* clocksource_handle_t;
typedef long long time_t;

struct clockevent_timer;

struct timespec {
    time_t tv_sec;   // 秒
    long tv_nsec;     // 纳秒
};

// 时钟事件框架初始化
void clockevent_init(void);

/**
 * 初始化软件定时器队列
 *
 * 必须在时钟事件设备注册完成后调用
 */
bool clockevent_timer_init(void);

/**
 * 分配一个定时器句柄
 *
 * @return 成功返回定时器句柄，失败返回NULL
 */
struct clockevent_timer *clockevent_timer_alloc(void);

/**
 * 初始化定时器的回调函数
 *
 * @param timer 定时器句柄
 * @param callback 超时回调函数
 * @param data 回调参数
 */
void clockevent_timer_init_callback(struct clockevent_timer *timer, void (*callback)(void *), void *data);

/**
 * 释放一个定时器句柄
 *
 * @param timer 定时器句柄
 */
void clockevent_timer_free(struct clockevent_timer *timer);

/**
 * 添加/重新添加一个定时器
 *
 * @param timer 定时器句柄
 * @param ns 相对超时时间（纳秒）
 */
void clockevent_timer_add(struct clockevent_timer *timer, uint64_t ns);

/**
 * 获取时钟事件设备句柄
 * 
 * @param name 设备名称，为 NULL 时选择当前CPU上最高频率且未被占用的设备
 * 
 * @return 成功：句柄
 * @return 失败：NULL
 */
clockevent_handle_t clockevent_get(const char *name);

/**
 * 设置时钟事件设备的中断处理函数
 * 
 * @param handle 设备句柄
 * @param handler 处理函数，若为 NULL 则清空调回函数
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_handler(clockevent_handle_t handle, void (*handler)(void));

/**
 * 触发时钟事件设备的中断处理（由驱动在中断中调用）
 * 
 * @param handle 设备句柄
 */
void clockevent_handle_irq(clockevent_handle_t handle);

/**
 * 设置下一次中断触发时间（相对当前时刻的纳秒数）
 * 
 * @param handle 设备句柄
 * @param ns 相对纳秒数
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_next(clockevent_handle_t handle, uint64_t ns);

/**
 * 设置时钟事件设备的工作模式
 * 
 * @param handle 设备句柄
 * @param mode 模式
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_mode(clockevent_handle_t handle, clockevent_mode_t mode);

/**
 * 释放时钟事件设备句柄
 * 
 * @param handle 设备句柄
 */
void clockevent_release(clockevent_handle_t handle);

/**
 * 注册时钟到时钟事件框架
 * 
 * @param name 设备名称
 * @param shutdown 停止的函数指针
 * @param set_oneshot 设置为单次中断模式的函数指针
 * @param set_periodic 设置为周期中断模式的函数指针
 * @param set_value 设置下一次中断的值的函数指针（设备单位）
 * @param hz 时钟频率
 */
void clockevent_register(
    const char *name,
    void (*shutdown)(void),
    void (*set_oneshot)(void),
    void (*set_periodic)(void),
    void (*set_value)(uint64_t value),
    uint64_t hz
);

// 时钟源框架初始化
void clocksource_init(void);

/**
 * 获取时钟源设备句柄
 * 
 * @param name 设备名称，为 NULL 时选择当前CPU上最高频率的设备
 * 
 * @return 成功：句柄
 * @return 失败：NULL
 */
clocksource_handle_t clocksource_get(const char *name);

/**
 * 读取时钟源当前值（返回纳秒）
 * 
 * @param handle 设备句柄
 * 
 * @return 成功：纳秒时间
 * @return 失败：0
 */
uint64_t clocksource_read(clocksource_handle_t handle);

/**
 * 获取时钟源设备的频率（Hz）
 * 
 * @param handle 设备句柄
 * 
 * @return 成功：频率
 * @return 失败：0
 */
uint64_t clocksource_get_hz(clocksource_handle_t handle);

/**
 * 获取当前CPU默认时钟源的纳秒读数
 * 
 * @return 成功：当前纳秒值
 * @return 失败：0（表示无可用时钟源）
 */
uint64_t clocksource_default_read(void);

/**
 * 注册时钟到时钟源框架
 * 
 * @param name 时钟名称
 * @param read 读取时钟源值的函数指针
 * @param hz 时钟源频率
 */
void clocksource_register(
    const char *name,
    uint64_t (*read)(void),
    uint64_t hz
);

// time系统初始化
static inline void time_init(void) {
    clocksource_init();
    clockevent_init();
    TIME_PRINT("time init success");
}

/**
 * 忙等待
 * 
 * @param usec 等待的微秒数
 */
void time_stall(uint8_t usec);

/**
 * 睡眠
 * 
 * @param msec 睡眠的毫秒数
 */
void time_sleep(uint64_t msec);

/**
 * 更新最后一次记录的时间
 * 
 * @param now 最后一次的时间
 */
void time_update(uint64_t now);

/**
 * 获取最后一次与上一次时间的差值（ns）
 * 
 * @return 纳秒
 */
uint64_t time_delta(void);

// 计算系统启动时的unix时间戳
void time_get_boot_unix(void);

// 获取当前系统时间
void time_get(struct timespec *ts);

// time数据初始化（必须在时钟事件设备注册完成后调用）
static inline bool time_data_init(void) {
    if (!clockevent_timer_init()) return false;
    time_get_boot_unix();
    TIME_PRINT("time data init success");
    return true;
}
