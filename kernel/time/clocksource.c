/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <time.h>
#include <timecycle.h>
#include <stdint.h>
#include <stdbool.h>
#include <shizi/string.h>
#include <list.h>
#include <bootboot.h>
#include <heap.h>
#include <asm/serial.h>
#include <asm/smp.h>

#define CLOCKSOURCE_INFO(name, hz) \
    printk("[CLOCKSOURCE] register clocksource : [\"name\" = \"%s\", \"hz\" = \"%llu\"]\n", name, hz)

#define CLOCKSOURCE_FAIL(name, hz) \
    printk("[CLOCKSOURCE] register clocksource fail : [\"name\" = \"%s\", \"hz\" = \"%llu\"]\n", name, hz)

#define CLOCKSOURCE_PANIC(fmt, ...) \
    printp("[CLOCKSOURCE] ERROR: " fmt, ##__VA_ARGS__)

// 时钟源结构体
typedef struct {
    const char *name;   // 设备名称   
    uint64_t (*read)(void); // 获取时钟的值

    uint64_t hz;    // 频率

    uint32_t mult;  // 乘数
    uint32_t shift; // 移位

    // 反向
    uint32_t mult_inv;
    uint32_t shift_inv;
} clocksource_struct;

typedef struct {
    clocksource_struct clocksource;
    struct list_head node;
} clocksource_list_struct;

/*
 * clocksource链表头
 * 每个cpu有一个链表头
 * 每个链表放所有注册过的时钟指针
 */
typedef struct {
    uint64_t count;
    struct list_head head[];
} clocksource_list_head;

static clocksource_list_head *clocksource_head = NULL;

// 时钟源框架初始化
void clocksource_init(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    uint64_t cpu_count = bootboot->numcores;
    uint64_t size = sizeof(uint64_t) + sizeof(struct list_head) * cpu_count;
    clocksource_head = (clocksource_list_head *)kheap_alloc(size);
    if (!clocksource_head) {
        CLOCKSOURCE_PANIC("memory alloc error\n");
    }

    clocksource_head->count = cpu_count;
    
    for (int i = 0;i < cpu_count;i++) {
        INIT_LIST_HEAD(&clocksource_head->head[i]);
    }
}

/**
 * 获取时钟源设备句柄
 * 
 * @param name 设备名称，为 NULL 时选择当前CPU上最高频率的设备
 * 
 * @return 成功：句柄
 * @return 失败：NULL
 */
clocksource_handle_t clocksource_get(const char *name) {
    uint64_t logical_id = get_logical_id();
    struct list_head *head = &clocksource_head->head[logical_id];

    if (name) {
        clocksource_list_struct *pos;
        list_for_each_entry(pos, head, node) {
            if (strcmp(pos->clocksource.name, name))
                return (clocksource_handle_t)pos;
        }
        return NULL;
    } else {
        clocksource_list_struct *best = NULL;
        uint64_t best_hz = 0;
        clocksource_list_struct *pos;
        list_for_each_entry(pos, head, node) {
            if (pos->clocksource.hz > best_hz) {
                best = pos;
                best_hz = pos->clocksource.hz;
            }
        }
        return (clocksource_handle_t)best;
    }
}

/**
 * 读取时钟源当前值（返回纳秒）
 * 
 * @param handle 设备句柄
 * 
 * @return 成功：纳秒时间
 * @return 失败：0
 */
uint64_t clocksource_read(clocksource_handle_t handle) {
    clocksource_list_struct *dev = (clocksource_list_struct *)handle;
    if (!dev || !dev->clocksource.read) return 0;

    uint64_t cycles = dev->clocksource.read();
    return timecycle_cycles_to_ns(cycles, dev->clocksource.mult, dev->clocksource.shift);
}

/**
 * 获取时钟源设备的频率（Hz）
 * 
 * @param handle 设备句柄
 * 
 * @return 成功：频率
 * @return 失败：0
 */
uint64_t clocksource_get_hz(clocksource_handle_t handle) {
    clocksource_list_struct *dev = (clocksource_list_struct *)handle;
    return dev ? dev->clocksource.hz : 0;
}

/**
 * 获取当前CPU默认时钟源的纳秒读数
 * 
 * @return 成功：当前纳秒值
 * @return 失败：0（表示无可用时钟源）
 */
uint64_t clocksource_default_read(void) {
    clocksource_handle_t handle = clocksource_get(NULL);
    if (!handle) return 0;
    return clocksource_read(handle);
}

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
        list_for_each_entry(pos, head, node) {
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