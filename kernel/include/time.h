/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdbool.h>
#include <stdint.h>
#include <serial.h>

#define TIME_PRINT(str) \
    serial_puts("[TIME]" str "\n")

// 时钟事件框架初始化
void clockevent_init(void);

/**
 * 注册回调函数到设备
 * 设备中断时调用
 * 
 * @param event_handler 回调函数的函数指针
 * @param name 设备名称，当这个为NULL时，使用精度最高的设备
 * 
 * @return 失败： false
 * @return 成功： true
 */
bool event_handler_register(void (*event_handler)(void), char *name);

/**
 * 设置中断值到设备
 * 
 * @param ns 纳秒(相对当前)
 * @param name 设备名称，当这个为NULL时，使用精度最高的设备
 * 
 * @return 失败： false
 * @return 成功： true
 */
bool set_value_to_dev(uint64_t ns, char *name);

/**
 * 获取设备的事件处理函数
 * 
 * @param name 设备名称，当这个为NULL时，使用精度最高的设备
 * @param event_handler 存储回调函数的函数指针
 * 
 * @return 失败： NULL
 * @return 成功： 回调函数的函数指针
 */
void get_event_handler(char *name, void (**event_handler)(void));

/**
 * 注册时钟到时钟事件框架
 * 
 * @param name 设备名称
 * @param shutdown 停止的函数指针
 * @param set_oneshot 设置为单次中断模式的函数指针
 * @param set_periodic 设置为周期中断模式的函数指针
 * @param set_value 设置下一次中断的值的函数指针
 * @param hz 时钟频率
 */
void clockevent_register(
    char *name, 
    void (*shutdown)(void),
    void (*set_oneshot)(void),
    void (*set_periodic)(void),
    void (*set_value)(uint64_t value),
    uint64_t hz
);

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