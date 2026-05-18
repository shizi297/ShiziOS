/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <list.h>
#include <spinlock.h>
#include <task.h>

typedef struct {
    struct list_head head;
    spinlock_t lock;
} wait_queue_head_t;

typedef struct {
    struct list_head node;
    struct task_struct *task;  
} wait_queue_entry_t;

/**
 * 初始化等待队列头
 * 
 * @param wq 待初始化的等待队列头指针
 *
 * 必须在使用等待队列前调用此函数
 */
static inline void waitqueue_head_init(wait_queue_head_t *wq) {
    INIT_LIST_HEAD(&wq->head);
    spinlock_init(&wq->lock);
}

/**
 * 将当前任务加入等待队列
 * 
 * @param wq 目标等待队列
 * @param entry 待插入的队列项
 */
static inline void waitqueue_add(wait_queue_head_t *wq, wait_queue_entry_t *entry) {
    entry->task = smp_get_task_current();
    spin_lock(&wq->lock);
    list_add_tail(&entry->node, &wq->head);
    spin_unlock(&wq->lock);
}

/**
 * 从等待队列中移除任务
 * 
 * @param wq 目标等待队列
 * @param entry 待移除的队列项
 */
static inline void waitqueue_del(wait_queue_head_t *wq, wait_queue_entry_t *entry) {
    spin_lock(&wq->lock);
    list_del_init(&entry->node);
    spin_unlock(&wq->lock);
}

/**
 * 唤醒等待队列中的第一个任务
 * 
 * @param wq 等待队列
 *
 * 移除队列中的第一个任务并将其标记为可运行
 */
static inline void waitqueue_wake_up(wait_queue_head_t *wq) {
    wait_queue_entry_t *entry = NULL;
    spin_lock(&wq->lock);
    if (!list_empty(&wq->head)) {
        entry = list_first_entry(&wq->head, wait_queue_entry_t, node);
        list_del_init(&entry->node);
    }
    spin_unlock(&wq->lock);
    if (entry)
        task_wakeup(entry->task);
}

/**
 * 让当前任务进入不可中断睡眠并让出 CPU
 * 
 * @param wq 等待队列
 * @param entry 对应的等待队列项
 *
 * 调用此函数前必须已经将任务加入等待队列
 * 唤醒后返回
 * 但任务需要自行从等待队列中移除
 */
static inline void waitqueue_sleep(wait_queue_head_t *wq, wait_queue_entry_t *entry) {
    (void)wq;
    (void)entry;
    smp_set_need_sched();
    task_sched();
}

/**
 * 将当前任务加入等待队列并进入睡眠
 * 
 * @param wq 等待队列
 * @param condition 布尔条件表达式
 */
#define waitqueue_event(wq, condition) \
do { \
    wait_queue_entry_t __entry; \
    waitqueue_add(&(wq), &__entry); \
    if (condition) { \
        waitqueue_del(&(wq), &__entry); \
        break; \
    } \
    for (;;) { \
        waitqueue_sleep(&(wq), &__entry); \
        if (condition) \
            break; \
    } \
    waitqueue_del(&(wq), &__entry); \
} while (0)