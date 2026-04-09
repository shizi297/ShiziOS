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
#include <asm/serial.h>
#include <asm/smp.h>
#include <list.h>
#include <heap.h>

#define CLOCKEVENT_INFO(name, hz) \
    printk("[CLOCKEVENT] register clockevent : [\"name\" = \"%s\", \"hz\" = \"%llu\"]\n", name, hz)

#define CLOCKEVENT_FAIL(name, hz) \
    printk("[CLOCKEVENT] register clockevent fail : [\"name\" = \"%s\", \"hz\" = \"%llu\"]\n", name, hz)

#define CLOCKEVENT_PANIC(fmt, ...) \
    printp("[CLOCKEVENT] ERROR: " fmt, ##__VA_ARGS__)

// 定时器结构体
struct clockevent_timer {
    uint64_t expire_time;           // 绝对超时时间（纳秒）
    void (*callback)(void *data);   // 超时回调函数
    void *data;                     // 回调参数
    struct list_head node;          // 链表节点
};

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

// 每个 CPU 的私有数据
typedef struct {
    struct list_head dev_list;      // 本 CPU 注册的设备链表头
    struct list_head timer_list;    // 本 CPU 的定时器队列
    clockevent_handle_t oneshot;    // 本 CPU 使用的单次触发设备
} clockevent_per_cpu;

/*
 * clockevent链表头
 * 每个cpu有一个 clockevent_percpu 结构体
 */
typedef struct {
    uint64_t count;
    clockevent_per_cpu cpu[];
} clockevent_list_head;

static clockevent_list_head *clockevent_head = NULL;

// 软件定时器中断处理函数
static void timer_softirq_handler(void) {
    uint32_t cpu_id = get_logical_id();
    clockevent_per_cpu *pcpu = &clockevent_head->cpu[cpu_id];
    uint64_t now = clocksource_default_read();
    struct clockevent_timer *timer, *tmp;
    struct list_head expired;

    INIT_LIST_HEAD(&expired);

    // 取出所有到期的定时器
    list_for_each_entry_safe(timer, tmp, &pcpu->timer_list, node) {
        if (timer->expire_time <= now) {
            list_del_init(&timer->node);
            list_add_tail(&timer->node, &expired);
        } else {
            break;
        }
    }

    // 执行到期定时器的回调
    list_for_each_entry_safe(timer, tmp, &expired, node) {
        list_del_init(&timer->node);
        if (timer->callback) timer->callback(timer->data);
    }

    // 重新设置硬件中断
    if (!list_empty(&pcpu->timer_list)) {
        struct clockevent_timer *first = list_first_entry(&pcpu->timer_list, struct clockevent_timer, node);
        uint64_t now2 = clocksource_default_read();
        uint64_t delta = (first->expire_time > now2) ? (first->expire_time - now2) : 0;
        if (delta == 0) delta = 1;
        clockevent_set_next(pcpu->oneshot, delta);
    } else {
        if (pcpu->oneshot)
            clockevent_set_mode(pcpu->oneshot, CLOCKEVENT_MODE_SHUTDOWN);
    }
}

// 时钟事件框架初始化
void clockevent_init(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    uint64_t cpu_count = bootboot->numcores;
    uint64_t size = sizeof(clockevent_list_head) + cpu_count * sizeof(clockevent_per_cpu);
    clockevent_head = (clockevent_list_head *)kheap_alloc(size);
    if (!clockevent_head) {
        CLOCKEVENT_PANIC("memory alloc error\n");
    }

    clockevent_head->count = cpu_count;
    
    for (uint64_t i = 0; i < cpu_count; i++) {
        INIT_LIST_HEAD(&clockevent_head->cpu[i].dev_list);
        INIT_LIST_HEAD(&clockevent_head->cpu[i].timer_list);
        clockevent_head->cpu[i].oneshot = NULL;
    }
}

/**
 * 初始化软件定时器队列
 *
 * 必须在时钟事件设备注册完成后调用
 */
bool clockevent_timer_init(void) {
    uint32_t cpu_id = get_logical_id();
    clockevent_per_cpu *pcpu = &clockevent_head->cpu[cpu_id];

    if (pcpu->oneshot != NULL) return true;

    pcpu->oneshot = clockevent_get(NULL);
    if (pcpu->oneshot == NULL) {
        CLOCKEVENT_PANIC("No available clockevent device for timer queue\n");
        return false;
    }

    if (!clockevent_set_mode(pcpu->oneshot, CLOCKEVENT_MODE_ONESHOT)) {
        CLOCKEVENT_PANIC("Failed to set oneshot mode for timer device\n");
        return false;
    }
    if (!clockevent_set_handler(pcpu->oneshot, timer_softirq_handler)) {
        CLOCKEVENT_PANIC("Failed to set handler for timer device\n");
        return false;
    }

    return true;
}

/**
 * 分配一个定时器句柄
 *
 * @return 成功返回定时器句柄，失败返回NULL
 */
struct clockevent_timer *clockevent_timer_alloc(void) {
    struct clockevent_timer *timer = (struct clockevent_timer *)kheap_alloc(sizeof(struct clockevent_timer));
    if (!timer) return NULL;

    INIT_LIST_HEAD(&timer->node);
    timer->expire_time = 0;
    timer->callback = NULL;
    timer->data = NULL;

    return timer;
}

/**
 * 初始化定时器的回调函数
 *
 * @param timer 定时器句柄
 * @param callback 超时回调函数
 * @param data 回调参数
 */
void clockevent_timer_init_callback(struct clockevent_timer *timer, void (*callback)(void *), void *data) {
    if (!timer) return;
    timer->callback = callback;
    timer->data = data;
}

/**
 * 释放一个定时器句柄
 *
 * @param timer 定时器句柄
 */
void clockevent_timer_free(struct clockevent_timer *timer) {
    if (!timer) return;

    uint32_t cpu_id = get_logical_id();
    clockevent_per_cpu *pcpu = &clockevent_head->cpu[cpu_id];
    if (!list_empty(&timer->node)) list_del_init(&timer->node);

    kheap_free(timer);
}

/**
 * 添加/重新添加一个定时器
 *
 * @param timer 定时器句柄
 * @param ns 相对超时时间（纳秒）
 */
void clockevent_timer_add(struct clockevent_timer *timer, uint64_t ns) {
    if (timer == NULL || timer->callback == NULL || ns == 0) return;

    uint32_t cpu_id = get_logical_id();
    clockevent_per_cpu *pcpu = &clockevent_head->cpu[cpu_id];
    if (pcpu->oneshot == NULL) return;

    uint64_t now = clocksource_default_read();
    timer->expire_time = now + ns;

    // 如果已经在队列中，先移除
    if (!list_empty(&timer->node)) list_del_init(&timer->node);

    // 按 expire_time 升序插入
    struct list_head *pos;
    list_for_each(pos, &pcpu->timer_list) {
        struct clockevent_timer *t = list_entry(pos, struct clockevent_timer, node);
        if (t->expire_time > timer->expire_time)
            break;
    }
    list_add_tail(&timer->node, pos);

    // 如果新定时器成为队首，需要重新设置硬件中断
    if (&timer->node == pcpu->timer_list.next) {
        uint64_t delta = (timer->expire_time > now) ? (timer->expire_time - now) : 0;
        if (delta == 0) delta = 1;
        clockevent_set_next(pcpu->oneshot, delta);
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
    struct list_head *head = &clockevent_head->cpu[logical_id].dev_list;

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
    if (handler != NULL) dev->clockevent.occupied = true;
    return true;
}

/**
 * 触发时钟事件设备的中断处理（由驱动在中断中调用）
 * 
 * @param handle 设备句柄
 */
void clockevent_handle_irq(clockevent_handle_t handle) {
    clockevent_list_struct *dev = (clockevent_list_struct *)handle;
    if (dev && dev->clockevent.event_handler) dev->clockevent.event_handler();
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
    struct list_head *head = &clockevent_head->cpu[logical_id].dev_list;

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