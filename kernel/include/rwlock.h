/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <mutex.h>
#include <wait.h>


typedef struct {
    atomic_uint_least32_t readers;  // 当前读者数量
    mutex_t write_mutex;    // 保证同一时间只有一个写者
    wait_queue_head_t write_wait;   // 写者等待队列（读者归零时唤醒）
} rwlock_t;

// 初始化
static inline void rwlock_init(rwlock_t *rw) {
    atomic_init(&rw->readers, 0);
    mutex_init(&rw->write_mutex);
    waitqueue_head_init(&rw->write_wait);
}

// 获取读锁
static inline void rwlock_read_lock(rwlock_t *rw) {
    atomic_fetch_add(&rw->readers, 1);
}

// 释放读锁
static inline void rwlock_read_unlock(rwlock_t *rw) {
    if (atomic_fetch_sub(&rw->readers, 1) == 1) {
        waitqueue_wake_up(&rw->write_wait);
    }
}

// 获取写锁
static inline void rwlock_write_lock(rwlock_t *rw) {
    mutex_lock(&rw->write_mutex);
    waitqueue_event(rw->write_wait, atomic_load(&rw->readers) == 0);
}

// 释放写锁
static inline void rwlock_write_unlock(rwlock_t *rw) {
    mutex_unlock(&rw->write_mutex);
}

// 获取读锁并关中断
static inline uint64_t rwlock_read_lock_irqsave(rwlock_t *rw) {
    uint64_t flags = get_cpu_flags();
    irq_off();
    rwlock_read_lock(rw);
    return flags;
}

// 释放读锁并恢复中断状态
static inline void rwlock_read_unlock_irqrestore(rwlock_t *rw, uint64_t flags) {
    rwlock_read_unlock(rw);
    write_cpu_flags(flags);
}

// 获取写锁并关中断
static inline uint64_t rwlock_write_lock_irqsave(rwlock_t *rw) {
    uint64_t flags = get_cpu_flags();
    irq_off();
    rwlock_write_lock(rw);
    return flags;
}

// 释放写锁并恢复中断状态
static inline void rwlock_write_unlock_irqrestore(rwlock_t *rw, uint64_t flags) {
    rwlock_write_unlock(rw);
    write_cpu_flags(flags);
}