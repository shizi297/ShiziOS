/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <asm/processor.h>

// 状态位
#define RWLOCK_READER_BITS   30u                                    // 读者计数占用的位数
#define RWLOCK_READER_MASK   ((1ull << RWLOCK_READER_BITS) - 1u)    // 读者计数掩码（低 30 位）
#define RWLOCK_WRITE_WAITING (1ull << (RWLOCK_READER_BITS + 1))     // 有写者在等待（第 31 位）
#define RWLOCK_WRITE_LOCKED  (1ull << (RWLOCK_READER_BITS + 0))     // 写者持有锁（第 30 位）

typedef struct {
    atomic_uint_least32_t state;     
} rwlock_t;

// 初始化
static inline void rwlock_init(rwlock_t *rw) {
    atomic_init(&rw->state, 0);
}

// 获取读锁
static inline void rwlock_read_lock(rwlock_t *rw) {
    while (1) {
        uint32_t s = atomic_load_explicit(&rw->state, memory_order_relaxed);
        // 没有写者持有且没有写者等待时可以获取读锁
        if (!(s & (RWLOCK_WRITE_LOCKED | RWLOCK_WRITE_WAITING))) {
            if (atomic_compare_exchange_weak_explicit(
                    &rw->state, &s, s + 1,
                    memory_order_acquire, memory_order_relaxed)) {
                return;
            }
        }
        // 有写者，自旋等待
    }
}

// 释放读锁
static inline void rwlock_read_unlock(rwlock_t *rw) {
    atomic_fetch_sub_explicit(&rw->state, 1, memory_order_release);
}

// 获取写锁
static inline void rwlock_write_lock(rwlock_t *rw) {
    while (1) {
        uint32_t s = atomic_load_explicit(&rw->state, memory_order_relaxed);
        if (s == 0) {
            // 没有任何持有者，尝试直接获取写锁
            if (atomic_compare_exchange_weak_explicit(
                    &rw->state, &s, RWLOCK_WRITE_LOCKED,
                    memory_order_acquire, memory_order_relaxed)) {
                return;
            }
        } else {
            // 标记写者等待，防止新的读者进入
            if (!(s & RWLOCK_WRITE_WAITING)) {
                atomic_fetch_or_explicit(&rw->state, RWLOCK_WRITE_WAITING, memory_order_relaxed);
            }
        }
        // 自旋等待
    }
}

// 释放写锁
static inline void rwlock_write_unlock(rwlock_t *rw) {
    atomic_store_explicit(&rw->state, 0, memory_order_release);
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