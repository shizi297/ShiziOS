/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "clockevent.h"
#include "timecycle.h"
#include <stdint.h>
#include <bootboot.h>
#include <serial.h>
#include <smp.h>
#include <list.h>
#include <heap.h>

#define CLOCKEVENT_PANIC(str) \
    panic("[CLOCKEVENT] ERROR : " str "\n") 

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

clockevent_list_head * clockevent_head = NULL;

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
 */
void event_handler_register(void (*event_handler)(void), char *name) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();

    // TODO
}

/**
 * 注册时钟到时钟信号框架
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
    clockevent_list_struct *current_list = 
    (clockevent_list_struct *)kheap_alloc(sizeof(clockevent_list_struct));
    if (!current_list) {
        CLOCKEVENT_PANIC("memory alloc error");
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
    
    list_add(&current_list->node, &clockevent_head->head[logical_id]);
}