/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <mutex.h>
#include <task.h>
#include <stddef.h>
#include <stdbool.h>

// 内部等待项，分配在等待任务的栈上
struct mutex_waiter {
    struct task_struct *task;
    struct list_head node;
    bool granted;
};

/**
 * 将当前任务加入等待队列并睡眠，直到获得锁
 *
 * @param m 互斥锁指针
 */
static void mutex_wait_and_sleep(mutex_t *m) {
    struct task_struct *current = smp_get_task_current();
    struct mutex_waiter waiter = {
        .task = current,
        .granted = false
    };
    INIT_LIST_HEAD(&waiter.node);

    spin_lock(&m->wait_lock);

    while (!waiter.granted) {
        /*
         * 二次检查：在获取自旋锁期间，锁可能已被释放
         * 如果此时锁可用，则直接获取并返回，无需睡眠
         */
        int expected = 0;
        if (atomic_compare_exchange_strong(&m->state, &expected, 1)) {
            m->owner = current;
            spin_unlock(&m->wait_lock);
            return;
        }

        /*
         * 锁仍被占用，将当前任务加入等待队列
         * 通过检查 waiter.node 是否为空链表，确保每个等待者只添加一次
         */
        if (list_empty(&waiter.node)) {
            list_add_tail(&waiter.node, &m->wait_list);
        }

        spin_unlock(&m->wait_lock);

        // 不可中断睡眠，等待被唤醒
        task_sleep(false);

        // 被唤醒后重新获取 wait_lock，检查 granted 标志
        spin_lock(&m->wait_lock);
    }

    /*
     * 被授予锁：waiter.granted 为真。
     * 锁的所有权和 state 已由解锁者设置，无需额外操作。
     */
    spin_unlock(&m->wait_lock);
}

// 锁竞争失败时的慢路径处理
void __mutex_lock_slowpath(mutex_t *m) {
    mutex_wait_and_sleep(m);
}

/**
 * 释放锁时存在等待者时的慢路径处理
 *
 * @param m 互斥锁指针
 */
void __mutex_unlock_slowpath(mutex_t *m) {
    spin_lock(&m->wait_lock);

    // 再次检查等待队列（防止在获取自旋锁期间被其他 CPU 改变）
    if (list_empty(&m->wait_list)) {
        // 没有等待者，重置状态为未锁定
        atomic_store(&m->state, 0);
        spin_unlock(&m->wait_lock);
        return;
    }

    /*
     * 有等待者时，取出队列中的第一个任务，
     * 将锁的所有权转移给它并唤醒。
     */
    struct mutex_waiter *waiter = list_first_entry(
        &m->wait_list,
        struct mutex_waiter,
        node
    );
    list_del_init(&waiter->node);

    waiter->granted = true;
    m->owner = waiter->task;
    // state 保持为 1，因为锁已转交

    spin_unlock(&m->wait_lock);

    // 唤醒等待任务
    task_wakeup(waiter->task);
}