/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "clocksource.h"
#include <timecycle.h>
#include <stdint.h>
#include <stdbool.h>
#include <shizi/string.h>
#include <list.h>
#include <bootboot.h>
#include <heap.h>
#include <serial.h>
#include <smp.h>
#include <time.h>

#define CLOCKSOURCE_PANIC(str) \
    panic("[CLOCKSOURCE] ERROR : " str "\n")

// 以json格式输出注册信息
#define CLOCKSOURCE_INFO(name, hz) \
    serial_puts("[CLOCKSOURCE] register clocksource : ["); \
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

#define CLOCKSOURCE_FAIL(name, hz) \
    serial_puts("[CLOCKSOURCE] register clocksource fail : ["); \
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
    clocksource_struct clocksource;
    struct list_head node;
}clocksource_list_struct;

/*
 * clocksource链表头
 * 每个cpu有一个链表头
 * 每个链表放所有注册过的时钟指针
 */
typedef struct {
    uint64_t count;
    struct list_head head[];
}clocksource_list_head;

clocksource_list_head *clocksource_head = NULL;

// 时钟源框架初始化
void clocksource_init(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    uint64_t cpu_count = bootboot->numcores;
    uint64_t size = sizeof(uint64_t) + sizeof(struct list_head) * cpu_count;
    clocksource_head = (clocksource_list_head *)kheap_alloc(size);
    if (!clocksource_head) {
        CLOCKSOURCE_PANIC("memory alloc error");
    }

    clocksource_head->count = cpu_count;
    
    for (int i = 0;i < cpu_count;i++) {
        INIT_LIST_HEAD(&clocksource_head->head[i]);
    }
}

/**
 * 获取时钟源的值(返回ns)
 * 
 * @param name 时钟名称，当这个为NULL时，使用精度最高的设备
 * @param value 用于接收时钟源值的指针
 * 
 * @return 成功：true
 * @return 失败: false
 */
bool clocksource_read(char *name, uint64_t *value) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clocksource_head->head[logical_id];

    // 使用最高精度的时钟
    if (!name) {
        if (list_empty(head)) {
            // 没有时钟设备
            CLOCKSOURCE_PANIC("no clock device");
        }
        clocksource_list_struct *first = list_first_entry(head, clocksource_list_struct, node);
        uint64_t dev_value = first->clocksource.read();
        *value = timecycle_cycles_to_ns(
            dev_value,
            first->clocksource.mult,
            first->clocksource.shift
        );
        return true;
    }

    // 根据设备名称查找
    clocksource_list_struct *pos = NULL;
    list_for_each_entry_t(pos, head, clocksource_list_struct, node) {
        if (strcmp(pos->clocksource.name, name)) {
            uint64_t dev_value = pos->clocksource.read();
            *value = timecycle_cycles_to_ns(
                dev_value, 
                pos->clocksource.mult, 
                pos->clocksource.shift
            );
            return true;
        }
    }

    return false;
}

/**
 * 获取时钟源设备的hz
 * 
 * @param name 时钟名称，当这个为NULL时，使用精度最高的设备
 * @param value 用于接收时钟源hz的指针
 * 
 * @return 成功：true
 * @return 失败: false
 */
bool clocksource_get_dev_hz(char *name, uint64_t *hz) {
    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clocksource_head->head[logical_id];

    // 使用最高精度的时钟
    if (!name) {
        if (list_empty(head)) {
            // 没有时钟设备
            CLOCKSOURCE_PANIC("no clock device");
        }
        clocksource_list_struct *first = list_first_entry(head, clocksource_list_struct, node);
        *hz = first->clocksource.hz;
        return true;
    }

    // 根据设备名称查找
    clocksource_list_struct *pos = NULL;
    list_for_each_entry_t(pos, head, clocksource_list_struct, node) {
        if (strcmp(pos->clocksource.name, name)) {
            *hz = pos->clocksource.hz;
            return true;
        }
    }

    return false;
}

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
) {
    bool success = true;

    clocksource_list_struct *current_list = 
        (clocksource_list_struct *)kheap_alloc(sizeof(clocksource_list_struct));
    if (!current_list) {
        success = false;
        goto finish;
    }

    INIT_LIST_HEAD(&current_list->node);

    current_list->clocksource.name = name;
    current_list->clocksource.read = read;
    current_list->clocksource.hz = hz;

    // 获取转换参数
    timecycle_init_params(
        hz,
        3600 * 24 * 365,    // 一年
        &current_list->clocksource.mult,
        &current_list->clocksource.shift,
        &current_list->clocksource.mult_inv,
        &current_list->clocksource.shift_inv  
    );

    // 获取当前逻辑cpuid
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clocksource_head->head[logical_id];

    /*
     * 按hz降序插入
     * 找到第一个时钟的hz比新时钟小的节点
     * 将新时钟插入找到的时钟之前
     */
    if (!list_empty(head)) {
        clocksource_list_struct *pos = NULL;
        list_for_each_entry_t(pos, head, clocksource_list_struct, node) {
            if (pos->clocksource.hz < current_list->clocksource.hz) {
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
        // 输出注册信息
        CLOCKSOURCE_INFO(name, hz);
    } else {
        CLOCKSOURCE_FAIL(name, hz);
    }
}