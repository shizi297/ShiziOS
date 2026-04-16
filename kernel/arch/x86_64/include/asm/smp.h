/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <task.h>
#include <asm/processor.h>
#include <time.h>
#include <bootboot.h>
#include <list.h>
#include <rcu.h>

struct sched_class;

typedef struct {
    uint64_t (*timestamp)(void);    // 时间戳获取
    task_struct *current;

    // 调度器私有数据
    per_cpu_sched *sched;

    // 当前cpuid
    uint64_t logical_id;

    // 当前cpu的idle任务
    task_struct *idle;

    uint64_t cancry;

    // 时间戳记录字段
    uint64_t last_ns;
    uint64_t current_ns;

    // 当前cpu的运行任务数量（不包含idle与正在运行的任务）
    uint64_t nr_running;

    // 时钟事件句柄
    clockevent_handle_t clockevent;

    // 用于设置下一次中断的时间
    struct clockevent_timer *sched_timer;

    // 用于在中断返回时判断是否需要重新调度
    uint64_t need_sched;

    // RCU 每 CPU 数据指针
    struct per_cpu_rcu *rcu_ptr;

    // 用于任务迁移
    struct list_head migration;
} __attribute__((aligned(64))) per_cpu;

enum per_cpu_offset {
    PER_CPU_TIMESTAMP_OFFSET    = offsetof(per_cpu, timestamp),
    PER_CPU_CURRENT_OFFSET      = offsetof(per_cpu, current),
    PER_CPU_SCHED_OFFSET        = offsetof(per_cpu, sched),
    PER_CPU_LOGICAL_ID_OFFSET   = offsetof(per_cpu, logical_id),
    PER_CPU_IDLE_OFFSET         = offsetof(per_cpu, idle),
    PER_CPU_CANCRY_OFFSET       = offsetof(per_cpu, cancry),
    PER_CPU_LAST_NS_OFFSET      = offsetof(per_cpu, last_ns),
    PER_CPU_CURRENT_NS_OFFSET   = offsetof(per_cpu, current_ns),
    PER_CPU_NR_RUNNING_OFFSET   = offsetof(per_cpu, nr_running),
    PER_CPU_CLOCKEVENT_OFFSET   = offsetof(per_cpu, clockevent),
    PER_CPU_SCHED_TIMER_OFFSET  = offsetof(per_cpu, sched_timer),
    PER_CPU_NEED_SCHED_OFFSET   = offsetof(per_cpu, need_sched),
    PER_CPU_RCU_PTR_OFFSET      = offsetof(per_cpu, rcu_ptr),
};

/*
 * 多核数据结构初始化
 * 负责给所有核心cpu提供基础数据结构
 * 
 * @param gdt_temp_addr 指向gdt模版的指针
 * @param idt_temp_addr 指向idt模版的指针
 * @param tss_temp_addr 指向tss模版的指针
 */
void smp_data_init(
    gdte *gdt_temp_addr, 
    struct idt_gate *idt_temp_addr, 
    struct tss *tss_temp_addr
);

/*
 * 初始化所有核心
 *
 * @param logical_id 当前cpu的逻辑cpuid
 * @param apic_id 当前cpu的apicid
 */
void smp_init(uint32_t logical_id, uint32_t apic_id);

/**
 * 注册中断处理函数
 * 
 * @param vector 中断向量号
 * @param handler_addr 处理函数地址
 * 
 * @return 注册成功：true
 * @return 注册失败：false
 * 
 * 使用中断门，DPL=0，IST=0
 */
bool smp_irq_register_handler(uint8_t vector, uint64_t handler_addr);

/**
 * 注销中断处理函数
 * 
 * @param vector 中断向量号
 */
void smp_irq_unregister_handler(uint8_t vector);

// 获取当前cpu的内核tls
per_cpu *smp_get_kernel_tls(void);

// 向目标cpu发送中断
void smp_send_irq(uint64_t logical_id, uint8_t vector);

// 更新架构相关的状态，用于切换上下文前
void smp_arch_update_state(struct thread_struct *thread);

// 获取cpu核心的逻辑id
static inline uint32_t get_logical_id(void) {
    return (uint32_t)PROCESSOR_READ_GS(PER_CPU_LOGICAL_ID_OFFSET);
}

// 设置调度器私有数据
static inline void smp_set_sched(void *sched) {
    PROCESSOR_WRITE_GS(PER_CPU_SCHED_OFFSET, sched);
}

// 获取当前cpu的调度器私有数据
static inline void *smp_get_sched(void) {
    return (void*)PROCESSOR_READ_GS(PER_CPU_SCHED_OFFSET);
}

// 设置当前cpu的上一次记录时间
static inline void smp_set_last_ns(uint64_t ns) {
    PROCESSOR_WRITE_GS(PER_CPU_LAST_NS_OFFSET, ns);
}

// 获取当前cpu的上一次记录时间
static inline uint64_t smp_get_last_ns(void) {
    return PROCESSOR_READ_GS(PER_CPU_LAST_NS_OFFSET);
}

// 设置当前cpu的当前记录时间
static inline void smp_set_current_ns(uint64_t ns) {
    PROCESSOR_WRITE_GS(PER_CPU_CURRENT_NS_OFFSET, ns);
}

// 获取当前cpu的当前记录时间
static inline uint64_t smp_get_current_ns(void) {
    return PROCESSOR_READ_GS(PER_CPU_CURRENT_NS_OFFSET);
}

// 获取当前cpu的运行时间 
static inline uint64_t smp_get_timestamp(void) {
    return PROCESSOR_READ_GS(PER_CPU_TIMESTAMP_OFFSET);
}

// 设置当前cpu的时间戳获取函数
static inline void smp_set_timestamp(uint64_t (*ts)(void)) {
    PROCESSOR_WRITE_GS(PER_CPU_TIMESTAMP_OFFSET, ts);
}

// 设置当前cpu的运行任务数量
static inline void smp_set_nr_running(uint64_t set) {
    PROCESSOR_WRITE_GS(PER_CPU_NR_RUNNING_OFFSET, set);
}

// 设置指定cpu的运行任务数量
static inline void smp_set_cpu_nr_running(uint32_t logical_id, uint64_t set) {
    extern per_cpu *per_cpu_ptr;
    per_cpu_ptr[logical_id].nr_running = set;
}

// 获取指定cpu核心的运行任务数量
static inline uint64_t smp_get_cpu_nr_running(uint64_t logicalid) {
    extern per_cpu *per_cpu_ptr;
    return per_cpu_ptr[logicalid].nr_running;
}

// 获取当前cpu的运行任务数量
static inline uint64_t smp_get_nr_running(void) {
    return PROCESSOR_READ_GS(PER_CPU_NR_RUNNING_OFFSET);
}

// 获取所有cpu核心的运行任务数
static inline void smp_get_nr_running_all(uint64_t *nr_array) {
    extern per_cpu *per_cpu_ptr;
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
    uint32_t max_cpu = bootboot->numcores;
    for (uint32_t i = 0;i < max_cpu;i++) {
        nr_array[i] = per_cpu_ptr[i].nr_running;
    }
}

// 设置当前cpu的运行的任务结构体
static inline void smp_set_task_current(task_struct *task) {
    PROCESSOR_WRITE_GS(PER_CPU_CURRENT_OFFSET, task);
}

// 获取当前cpu运行的任务结构体
static inline task_struct *smp_get_task_current(void) {
    return (task_struct*)PROCESSOR_READ_GS(PER_CPU_CURRENT_OFFSET);
}

// 设置当前cpu的idle任务
static inline void smp_set_idle(task_struct *idle) {
    PROCESSOR_WRITE_GS(PER_CPU_IDLE_OFFSET, idle);
}

// 获取当前cpu的idle任务
static inline task_struct *smp_get_idle(void) {
    return (task_struct*)PROCESSOR_READ_GS(PER_CPU_IDLE_OFFSET);
}

// 设置重新调度
static inline void smp_set_need_sched(void) {
    PROCESSOR_WRITE_GS(PER_CPU_NEED_SCHED_OFFSET, 1);
}

// 设置不需要重新调度
static inline void smp_set_no_sched(void) {
    PROCESSOR_WRITE_GS(PER_CPU_NEED_SCHED_OFFSET, 0);
}

// 判断是否需要重新调度
static inline bool smp_check_need_sched(void) {
    return PROCESSOR_READ_GS(PER_CPU_NEED_SCHED_OFFSET) != 0;
}

// 设置时钟事件句柄
static inline void smp_set_clockevent(clockevent_handle_t clockevent) {
    PROCESSOR_WRITE_GS(PER_CPU_CLOCKEVENT_OFFSET, clockevent);
}

// 获取时钟事件句柄
static inline clockevent_handle_t smp_get_clockevent(void) {
    return (clockevent_handle_t)PROCESSOR_READ_GS(PER_CPU_CLOCKEVENT_OFFSET);
}

// 获取当前cpu的调度定时器句柄
static inline struct clockevent_timer *smp_get_sched_timer(void) {
    return (struct clockevent_timer*)PROCESSOR_READ_GS(PER_CPU_SCHED_TIMER_OFFSET);
}

// 设置当前cpu的调度定时器句柄
static inline void smp_set_sched_timer(struct clockevent_timer *timer) {
    PROCESSOR_WRITE_GS(PER_CPU_SCHED_TIMER_OFFSET, timer);
}

// 获取当前cpu迁移队列链表头
static inline struct list_head *smp_get_migration(void) {
    per_cpu *per_cpu_ptr = smp_get_kernel_tls();
    return &per_cpu_ptr->migration;
}

// 获取目标cpu的迁移队列链表头
static inline struct list_head *smp_get_cpu_migration(uint64_t logical_id) {
    extern per_cpu *per_cpu_ptr;
    return &per_cpu_ptr[logical_id].migration;
}

// 获取当前 CPU 的 RCU 每 CPU 数据指针
static inline struct per_cpu_rcu *smp_get_rcu(void) {
    return (struct per_cpu_rcu *)PROCESSOR_READ_GS(PER_CPU_RCU_PTR_OFFSET);
}

// 获取指定 CPU 的 RCU 每 CPU 数据指针
static inline struct per_cpu_rcu *smp_get_cpu_rcu(uint32_t logical_id) {
    extern per_cpu *per_cpu_ptr;
    return per_cpu_ptr[logical_id].rcu_ptr;
}