/* SPDX-License-Identifier: Apache-2.0 */

#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include <stdatomic.h>
#include <stdbool.h>

// 自旋锁结构
typedef struct {
    atomic_flag flag;
} spinlock_t;

// 锁初始化宏
#define SPIN_LOCK_INIT { ATOMIC_FLAG_INIT }

// 初始化锁
static inline void spinlock_init(spinlock_t *lock) {
    atomic_flag_clear(&lock->flag);
}

// 获取锁
static inline void spin_lock(spinlock_t *lock) {
    while (atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire)) {
        // 锁被占用时暂停指令，减少CPU占用
        __asm__ __volatile__ ("pause");
    }
}

// 释放锁
static inline void spin_unlock(spinlock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

// 尝试获取锁，返回结果
static inline bool spin_trylock(spinlock_t *lock) {
    return !atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire);
}

#endif