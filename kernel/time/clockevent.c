/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <time.h>
#include <timecycle.h>
#include <stdint.h>
#include <stdbool.h>
#include <shizi/string.h>
#include <bootboot.h>
#include <serial.h>
#include <smp.h>
#include <list.h>
#include <heap.h>

#define CLOCKEVENT_INFO(name, hz) \
    printk("[CLOCKEVENT] register clockevent : [\"name\" = \"%s\", \"hz\" = \"%llu\"]\n", name, hz)

#define CLOCKEVENT_FAIL(name, hz) \
    printk("[CLOCKEVENT] register clockevent fail : [\"name\" = \"%s\", \"hz\" = \"%llu\"]\n", name, hz)

#define CLOCKEVENT_PANIC(fmt, ...) \
    printp("[CLOCKEVENT] ERROR: " fmt, ##__VA_ARGS__)

// 时钟事件结构体
typedef struct clockevent_struct {
    const char *name;   // 设备名称

    void (*shutdown)(void); // 停止
    void (*set_oneshot)(void);  // 设置为单次中断模式
    void (*set_periodic)(void); // 设置为周期模式

    void (*set_value)(uint64_t value);   // 定时设置

    uint64_t hz;    // 频率

    uint32_t mult;  // 乘数
    uint32_t shift; // 移位

    // 反向
    uint32_t mult_inv;
    uint32_t shift_inv;
    
    void (*event_handler)(void);    // 回调

    bool occupied;   // 设备是否被占用
} clockevent_struct;

typedef struct {
    clockevent_struct clockevent;
    struct list_head node;
} clockevent_list_struct;

/*
 * clockevent链表头
 * 每个cpu有一个链表头
 * 每个链表放所有注册过的时钟指针
 */
typedef struct {
    uint64_t count;
    struct list_head head[];
} clockevent_list_head;

static clockevent_list_head *clockevent_head = NULL;

// 时钟事件框架初始化
void clockevent_init(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    uint64_t cpu_count = bootboot->numcores;
    uint64_t size = sizeof(uint64_t) + sizeof(struct list_head) * cpu_count;
    clockevent_head = (clockevent_list_head *)kheap_alloc(size);
    if (!clockevent_head) {
        CLOCKEVENT_PANIC("memory alloc error\n");
    }

    clockevent_head->count = cpu_count;
    
    for (int i = 0;i < cpu_count;i++) {
        INIT_LIST_HEAD(&clockevent_head->head[i]);
    }
}

/**
 * 获取时钟事件设备句柄
 * 
 * @param name 设备名称，为 NULL 时选择当前CPU上最高频率且未被占用的设备
 * 
 * @return 成功：句柄
 * @return 失败：NULL
 */
clockevent_handle_t clockevent_get(const char *name) {
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clockevent_head->head[logical_id];

    if (name) {
        clockevent_list_struct *pos;
        list_for_each_entry(pos, head, node) {
            if (strcmp(pos->clockevent.name, name))
                return (clockevent_handle_t)pos;
        }
        return NULL;
    } else {
        clockevent_list_struct *best = NULL;
        uint64_t best_hz = 0;
        clockevent_list_struct *pos;
        list_for_each_entry(pos, head, node) {
            if (!pos->clockevent.occupied && pos->clockevent.hz > best_hz) {
                best = pos;
                best_hz = pos->clockevent.hz;
            }
        }
        return (clockevent_handle_t)best;
    }
}

/**
 * 设置时钟事件设备的中断处理函数
 * 
 * @param handle 设备句柄
 * @param handler 处理函数，若为 NULL 则清空调回函数
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_handler(clockevent_handle_t handle, void (*handler)(void)) {
    clockevent_list_struct *dev = (clockevent_list_struct *)handle;
    if (!dev) return false;

    dev->clockevent.event_handler = handler;
    if (handler != NULL) {
        dev->clockevent.occupied = true;
    }
    return true;
}

/**
 * 触发时钟事件设备的中断处理（由驱动在中断中调用）
 * 
 * @param handle 设备句柄
 */
void clockevent_handle_irq(clockevent_handle_t handle) {
    clockevent_list_struct *dev = (clockevent_list_struct *)handle;
    if (dev && dev->clockevent.event_handler) {
        dev->clockevent.event_handler();
    }
}

/**
 * 设置下一次中断触发时间（相对当前时刻的纳秒数）
 * 
 * @param handle 设备句柄
 * @param ns 相对纳秒数
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_next(clockevent_handle_t handle, uint64_t ns) {
    clockevent_list_struct *dev = (clockevent_list_struct *)handle;
    if (!dev || !dev->clockevent.set_value) return false;

    uint64_t cycles = timecycle_ns_to_cycles(ns, dev->clockevent.mult_inv, dev->clockevent.shift_inv);
    dev->clockevent.set_value(cycles);
    return true;
}

/**
 * 设置时钟事件设备的工作模式
 * 
 * @param handle 设备句柄
 * @param mode 模式
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool clockevent_set_mode(clockevent_handle_t handle, clockevent_mode_t mode) {
    clockevent_list_struct *dev = (clockevent_list_struct *)handle;
    if (!dev) return false;

    switch (mode) {
        case CLOCKEVENT_MODE_SHUTDOWN:
            if (dev->clockevent.shutdown) dev->clockevent.shutdown();
            else return false;
            break;
        case CLOCKEVENT_MODE_ONESHOT:
            if (dev->clockevent.set_oneshot) dev->clockevent.set_oneshot();
            else return false;
            break;
        case CLOCKEVENT_MODE_PERIODIC:
            if (dev->clockevent.set_periodic) dev->clockevent.set_periodic();
            else return false;
            break;
        default:
            return false;
    }
    return true;
}

/**
 * 释放时钟事件设备句柄
 * 
 * @param handle 设备句柄
 */
void clockevent_release(clockevent_handle_t handle) {
    clockevent_list_struct *dev = (clockevent_list_struct *)handle;
    if (!dev) return;

    if (dev->clockevent.occupied) {
        if (dev->clockevent.shutdown)
            dev->clockevent.shutdown();
        dev->clockevent.event_handler = NULL;
        dev->clockevent.occupied = false;
    }
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
    const char *name,
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
    current_list->clockevent.occupied = false;

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
        list_for_each_entry(pos, head, node) {
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