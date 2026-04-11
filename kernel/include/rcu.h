/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <list.h>
#include <spinlock.h>

// 嵌入任务结构体的 RCU 私有数据
typedef struct rcu_task_struct {
    atomic_uint_least32_t nesting;    // 读临界区嵌套深度
    uint64_t gp_seq;                  // 任务见证的最新宽限期序列号
} rcu_task_struct;

// 回调头，嵌入受保护对象中
struct rcu_head {
    struct list_head node;          // 链表节点
    void (*func)(struct rcu_head *); // 用户回调函数指针
    uint64_t gp_seq;                // 该回调可被执行的宽限期世代号
};

// 每 CPU RCU 数据
struct per_cpu_rcu;

// 进入读临界区
void rcu_read_lock(void);

// 退出读临界区
void rcu_read_unlock(void);

/**
 * 注册回调，在宽限期结束后执行
 *
 * @param head 要注册的回调节点头指针
 * @param func 回调函数指针
 */
void rcu_call(struct rcu_head *head, void (*func)(struct rcu_head *));

/**
 * 调度器回调，当任务被切换出时调用
 *
 * @param prev_rcu 被切换出任务的 RCU 私有数据指针
 */
void rcu_note_context_switch(rcu_task_struct *prev_rcu);

// 统一状态推进入口
void rcu_state_run(void);

// 等待所有已注册回调执行完毕
void rcu_barrier(void);

// 分配并初始化一个 per_cpu_rcu 结构体
struct per_cpu_rcu *rcu_per_cpu_alloc(void);

// 初始化 RCU
void rcu_init(void);

/**
 * 初始化任务结构体中的 RCU 私有数据
 *
 * @param rcu 指向待初始化的 rcu_task_struct 结构体
 */
static inline void rcu_task_init(rcu_task_struct *rcu) {
    atomic_init(&rcu->nesting, 0);
    rcu->gp_seq = 0;
}

/**
 * 安全读取受 RCU 保护的指针
 *
 * @param p 要读取的指针变量地址
 *
 * @return 指针的当前值
 */
static inline void *rcu_dereference(void *p) {
    atomic_uint_least64_t *ap = (atomic_uint_least64_t *)p;
    uint64_t val = atomic_load_explicit(ap, memory_order_acquire);
    return (void *)val;
}

/**
 * 安全更新受 RCU 保护的指针
 *
 * @param p 要更新的指针变量地址
 * @param v 新值
 */
static inline void rcu_assign_pointer(void **p, void *v) {
    atomic_uint_least64_t *ap = (atomic_uint_least64_t *)p;
    atomic_store_explicit(ap, (uint64_t)v, memory_order_release);
}