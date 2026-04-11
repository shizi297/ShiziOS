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

// 互斥锁结构体
typedef struct mutex {
    atomic_int state;               // 0：未锁定，1：已锁定
    struct task_struct *owner;      // 当前持有者
    struct list_head wait_list;     // 等待队列头
    spinlock_t wait_lock;           // 保护等待队列的自旋锁
} mutex_t;

// 初始化互斥锁
static inline void mutex_init(mutex_t *m) {
    atomic_init(&m->state, 0);
    m->owner = NULL;
    INIT_LIST_HEAD(&m->wait_list);
    spinlock_init(&m->wait_lock);
}

// 获取互斥锁
static inline void mutex_lock(mutex_t *m) {
    int expected = 0;

    // 尝试原子地获取锁
    if (atomic_compare_exchange_strong(&m->state, &expected, 1)) {
        m->owner = smp_get_task_current();
    } else {
        void __mutex_lock_slowpath(mutex_t *m);
        __mutex_lock_slowpath(m);
    }
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
    int expected = 1;

    // 尝试原子地释放锁
    if (atomic_compare_exchange_strong(&m->state, &expected, 0)) {
        m->owner = NULL;
    } else {
        void __mutex_unlock_slowpath(mutex_t *m);
        __mutex_unlock_slowpath(m);
    }
}