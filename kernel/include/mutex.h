/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <list.h>
#include <spinlock.h>
#include <asm/smp.h>
#include <wait.h>

// 互斥锁结构体
typedef struct mutex {
    atomic_int state;               // 0：未锁定，1：已锁定
    struct task_struct *owner;      // 当前持有者
    wait_queue_head_t wait_queue;   // 等待队列
} mutex_t;

// 初始化互斥锁
static inline void mutex_init(mutex_t *m) {
    atomic_init(&m->state, 0);
    m->owner = NULL;
    waitqueue_head_init(&m->wait_queue);
}

static inline void mutex_lock(mutex_t *m) {
    // 快速路径
    int expected = 0;
    if (atomic_compare_exchange_strong(&m->state, &expected, 1)) {
        m->owner = smp_get_task_current();
        return;
    }

    // 慢速路径
    do {
        waitqueue_event(&m->wait_queue, atomic_load(&m->state) == 0);
        expected = 0;
    } while (!atomic_compare_exchange_strong(&m->state, &expected, 1));

    m->owner = smp_get_task_current();
}

// 尝试获取互斥锁
static inline bool mutex_trylock(mutex_t *m) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&m->state, &expected, 1)) {
        m->owner = smp_get_task_current();
        return true;
    }

    return false;
}

// 释放互斥锁
static inline void mutex_unlock(mutex_t *m) {
    atomic_store(&m->state, 0);
    m->owner = NULL;
    waitqueue_wake_up(&m->wait_queue);   // 唤醒一个等待者
}