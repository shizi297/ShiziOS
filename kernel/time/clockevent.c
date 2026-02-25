/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "clockevent.h"
#include <timecycle.h>
#include <stdint.h>
#include <stdbool.h>
#include <shizi/string.h>
#include <bootboot.h>
#include <serial.h>
#include <smp.h>
#include <list.h>
#include <heap.h>
#include <time.h>

#define CLOCKEVENT_PANIC(str) \
    panic("[CLOCKEVENT] ERROR : " str "\n") 

// 以json格式输出注册信息
#define CLOCKEVENT_INFO(name, hz) \
    serial_puts("[CLOCKEVENT] register clockevent : ["); \
    serial_puts("“name” = "); \
    serial_putchar('"'); \
    serial_puts(name); \
    serial_putchar('"'); \
    serial_puts(", "); \
    serial_puts("“hz” = "); \
    serial_putchar('"'); \
    serial_put_dec(hz); \
    serial_putchar('"'); \
    serial_puts("]\n")

#define CLOCKEVENT_FAIL(name, hz) \
    serial_puts("[CLOCKEVENT] register clockevent fail : ["); \
    serial_puts("“name” = "); \
    serial_putchar('"'); \
    serial_puts(name); \
    serial_putchar('"'); \
    serial_puts(", "); \
    serial_puts("“hz” = "); \
    serial_putchar('"'); \
    serial_put_dec(hz); \
    serial_putchar('"'); \
    serial_puts("]\n")


typedef struct {
    clockevent_struct clockevent;
    struct list_head node;
}clockevent_list_struct;

/*
 * clockevent链表头
 * 每个cpu有一个链表头
 * 每个链表放所有注册过的时钟指针
 */
typedef struct {
    uint64_t count;
    struct list_head head[];
}clockevent_list_head;

clockevent_list_head *clockevent_head = NULL;

// 时钟事件框架初始化
void clockevent_init(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    uint64_t cpu_count = bootboot->numcores;
    uint64_t size = sizeof(uint64_t) + sizeof(struct list_head) * cpu_count;
    clockevent_head = (clockevent_list_head *)kheap_alloc(size);
    if (!clockevent_head) {
        CLOCKEVENT_PANIC("memory alloc error");
    }

    clockevent_head->count = cpu_count;
    
    for (int i = 0;i < cpu_count;i++) {
        INIT_LIST_HEAD(&clockevent_head->head[i]);
    }
}

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
bool event_handler_register(void (*event_handler)(void), char *name) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clockevent_head->head[logical_id];

    // 使用最高精度的时钟
    if (!name) {
        if (list_empty(head)) {
            // 没有时钟设备
            CLOCKEVENT_PANIC("no clock device");
        }
        clockevent_list_struct *first = list_first_entry(head, clockevent_list_struct, node);
        first->clockevent.event_handler = event_handler;
        return true;
    }

    // 根据设备名称查找
    clockevent_list_struct *pos = NULL;
    list_for_each_entry_t(pos, head, clockevent_list_struct, node) {
        if (strcmp(pos->clockevent.name, name)) {  
            pos->clockevent.event_handler = event_handler;
            return true;
        }
    }

    return false;
}

/**
 * 设置中断值到设备
 * 
 * @param ns 纳秒(相对当前)
 * @param name 设备名称，当这个为NULL时，使用精度最高的设备
 * 
 * @return 失败： false
 * @return 成功： true
 */
bool set_value_to_dev(uint64_t ns, char *name) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clockevent_head->head[logical_id];

    clockevent_list_struct *pos = NULL;

    {
        bool found = false;

        // 使用最高精度的时钟
        if (!name) {
            if (list_empty(head)) {
                // 没有时钟设备
                CLOCKEVENT_PANIC("no clock device");
            }
            pos = list_first_entry(head, clockevent_list_struct, node);
            found = true;
        } else {
            // 根据设备名称查找
            list_for_each_entry_t(pos, head, clockevent_list_struct, node) {
                if (strcmp(pos->clockevent.name, name)) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) return false;
    }

    {
        void (*set_value)(uint64_t value) = pos->clockevent.set_value;
        uint32_t mult_inv = pos->clockevent.mult_inv;
        uint32_t shift_inv = pos->clockevent.shift_inv;

        /*
        * 在当前内核下
        * apic始终为TSC DEADLINE模式
        * 需要写入tsc绝对值
        * 所以这里让apic特殊处理
        */
        if (strcmp(pos->clockevent.name, "apic")) { 
            /*
            * 使用时钟源框架
            * 读取tsc的值
            * 与调用方传的值相加
            * 转为设备值再写入
            */
            uint64_t current_ns = 0;
            if (!clocksource_read(NULL, &current_ns)) {
                CLOCKEVENT_PANIC("read clocksource error");
            }
            set_value(timecycle_ns_to_cycles(current_ns + ns, mult_inv, shift_inv));
        } else {
            set_value(timecycle_ns_to_cycles(ns, mult_inv, shift_inv));
        }
    }
    return true;
}

/**
 * 获取设备的事件处理函数
 * 
 * @param name 设备名称，当这个为NULL时，使用精度最高的设备
 * @param event_handler 存储回调函数的函数指针
 * 
 * @return 失败： NULL
 * @return 成功： 回调函数的函数指针
 */
void get_event_handler(char *name, void (**event_handler)(void)) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clockevent_head->head[logical_id];

    clockevent_list_struct *pos = NULL;

    {
        bool found = false;

        // 使用最高精度的时钟
        if (!name) {
            if (list_empty(head)) {
                // 没有时钟设备
                CLOCKEVENT_PANIC("no clock device");
            }
            pos = list_first_entry(head, clockevent_list_struct, node);
            found = true;
        } else {
            // 根据设备名称查找
            list_for_each_entry_t(pos, head, clockevent_list_struct, node) {
                if (strcmp(pos->clockevent.name, name)) { 
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            *event_handler = NULL;
            return;
        }
    }

     *event_handler = pos->clockevent.event_handler;
}

/**
 * 设置设备的中断模式
 * 
 * @param name 设备名称
 * @param mode 要设置的模式
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_mode(const char *name, clockevent_mode_t mode) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clockevent_head->head[logical_id];

    clockevent_list_struct *pos = NULL;

    {
        bool found = false;

        // 使用最高精度的时钟
        if (!name) {
            if (list_empty(head)) {
                // 没有时钟设备
                CLOCKEVENT_PANIC("no clock device");
            }
            pos = list_first_entry(head, clockevent_list_struct, node);
            found = true;
        } else {
            // 根据设备名称查找
            list_for_each_entry_t(pos, head, clockevent_list_struct, node) {
                if (strcmp(pos->clockevent.name, name)) {  
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            return false;
        }
    }

    // 根据模式调用对应的函数指针
    clockevent_struct *ce = &pos->clockevent;
    switch (mode) {
        case CLOCKEVENT_MODE_SHUTDOWN:
            if (ce->shutdown) ce->shutdown();
            else return false;
            break;
        case CLOCKEVENT_MODE_ONESHOT:
            if (ce->set_oneshot) ce->set_oneshot();
            else return false;
            break;
        case CLOCKEVENT_MODE_PERIODIC:
            if (ce->set_periodic) ce->set_periodic();
            else return false;
            break;
        default:
            return false;
    }
    return true;
}

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
) {
    bool success = true;

    clockevent_list_struct *current_list = 
        (clockevent_list_struct *)kheap_alloc(sizeof(clockevent_list_struct));
    if (!current_list) {
        success = false;
        goto finish;
    }

    INIT_LIST_HEAD(&current_list->node);

    current_list->clockevent.name = name;
    current_list->clockevent.shutdown = shutdown;
    current_list->clockevent.set_oneshot = set_oneshot;
    current_list->clockevent.set_periodic = set_periodic;
    current_list->clockevent.set_value = set_value;
    current_list->clockevent.hz = hz;
    current_list->clockevent.event_handler = NULL;

    // 获取转换参数
    timecycle_init_params(
        hz,
        3600 * 24 * 365,    // 一年
        &current_list->clockevent.mult,
        &current_list->clockevent.shift,
        &current_list->clockevent.mult_inv,
        &current_list->clockevent.shift_inv
    );

    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clockevent_head->head[logical_id];

    /*
     * 按hz降序插入
     * 找到第一个时钟的hz比新时钟小的节点
     * 将新时钟插入找到的时钟之前
     */
    if (!list_empty(head)) {
        clockevent_list_struct *pos = NULL;
        list_for_each_entry_t(pos, head, clockevent_list_struct, node) {
            if (pos->clockevent.hz < current_list->clockevent.hz) {
                // 插入到 pos 节点之前
                list_add_tail(&current_list->node, &pos->node);
                goto finish;
            }
        }
    }
    /*
     * 链表为空
     * 新时钟的 hz 不大于链表中任何节点的 hz
     * 则添加到链表末尾 
     */
    list_add_tail(&current_list->node, head);

finish:
    if (success) {
        // 打印注册驱动的信息
        CLOCKEVENT_INFO(name, hz);
    } else {
        CLOCKEVENT_FAIL(name, hz);
    }
}