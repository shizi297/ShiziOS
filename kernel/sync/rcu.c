/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <rcu.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include <spinlock.h>
#include <heap.h>
#include <task.h>
#include <mutex.h>
#include <asm/smp.h>
#include <asm/processor.h>
#include <bootboot.h>
#include <asm/serial.h>
#include <bitmap.h>

#define RCU_PANIC(fmt, ...) \
    printp("[RCU] ERROR: " fmt, ##__VA_ARGS__)

// 每 CPU RCU 数据
struct per_cpu_rcu {
    struct list_head cb_list;       // 回调链表头
    uint64_t gp_seq;                // 本 CPU 最后报告 QS 时记录的全局宽限期序列号
    uint64_t pending_gp_seq;        // 本 CPU 回调链表中最早的回调世代号，UINT64_MAX 表示空链表
    spinlock_t lock;                // 保护本 CPU 回调链表的自旋锁
    bool qs_pending;                // 本 CPU 是否欠当前宽限期一个 QS 报告
    uint32_t barrier_generation;    // 本 CPU 已处理的屏障请求世代号
    atomic_uint_least32_t cpu_nesting; // 本 CPU 上所有任务的读临界区嵌套计数总和
};

// 全局 RCU 状态
struct rcu_state {
    atomic_uint_least64_t gp_seq;   // 全局宽限期序列号，低 2 位状态，高位世代号
    atomic_uint_least64_t completed; // 已完成的最新宽限期世代号（纯世代，无状态位）
    spinlock_t lock;                // 保护全局状态变更的自旋锁
    uint32_t cpu_count;             // 在线 CPU 总数
    uint64_t qs_mask[];             // 位掩码，记录尚未报告 QS 的 CPU
};

static struct rcu_state *rcu_state;

// 回调处理工作项
struct rcu_work {
    struct list_head list;          // 待处理的回调链表头
    struct per_cpu_rcu *pcpu;       // 回调所属的 CPU 数据
};

// 状态位编码
#define RCU_GP_IDLE     0
#define RCU_GP_ONGOING  1
#define RCU_STATE_MASK  3ULL
#define RCU_SEQ_MASK    (~RCU_STATE_MASK)

// 全局屏障请求标志，为真时各 CPU 需在时钟中断中注册屏障回调
static atomic_bool rcu_barrier_pending = ATOMIC_VAR_INIT(false);

// 当前屏障请求的世代号，每次屏障递增
static atomic_uint_least32_t rcu_barrier_generation = ATOMIC_VAR_INIT(0);

// 屏障回调待完成计数，归零时表示所有 CPU 的屏障回调均已执行
static atomic_uint_least32_t rcu_barrier_count = ATOMIC_VAR_INIT(0);

// 等待屏障完成的任务，计数器归零时被唤醒
static struct task_struct *rcu_barrier_waiter = NULL;

// 保护 rcu_barrier 并发调用的互斥锁
static mutex_t rcu_barrier_mutex;

// 获取当前任务的 RCU 数据
static inline rcu_task_struct *_rcu_current_task(void) {
    return task_get_current_rcu();
}

// 获取当前 CPU 的 per_cpu_rcu 数据
static inline struct per_cpu_rcu *_rcu_this_cpu(void) {
    return smp_get_rcu();
}

// 宽限期完成逻辑，调用者必须持有 rcu_state->lock
static void _rcu_complete_gp_locked(void) {
    uint64_t cur_gp_seq = atomic_load(&rcu_state->gp_seq);

    if ((cur_gp_seq & RCU_STATE_MASK) != RCU_GP_ONGOING) return;

    // 提取当前世代号存入 completed，并将 gp_seq 置为下一代空闲状态
    uint64_t completed_gen = cur_gp_seq >> 2;
    atomic_store_explicit(&rcu_state->completed, completed_gen, memory_order_release);
    atomic_store(&rcu_state->gp_seq, (completed_gen + 1) << 2);
}

// 报告当前 CPU 经历了一次静止状态，调用者不持锁
static void _rcu_report_qs(void) {
    struct per_cpu_rcu *pcpu = _rcu_this_cpu();
    uint32_t id = get_logical_id();
    uint64_t flags;

    spin_lock_irqsave(&rcu_state->lock, &flags);

    // 检查本 CPU 是否欠 QS 报告
    if (!pcpu->qs_pending) {
        spin_unlock_irqrestore(&rcu_state->lock, flags);
        return;
    }

    // 清除位掩码中对应位
    bitmap_clear((bitmap_t *)rcu_state->qs_mask, id);

    // 更新本地标志和世代号
    pcpu->qs_pending = false;
    pcpu->gp_seq = atomic_load(&rcu_state->gp_seq);

    // 检查是否所有 CPU 均已报告，若全零则完成宽限期
    uint32_t first_set = bitmap_find((bitmap_t *)rcu_state->qs_mask, rcu_state->cpu_count, 0, 1);

    if (first_set == rcu_state->cpu_count) _rcu_complete_gp_locked();

    spin_unlock_irqrestore(&rcu_state->lock, flags);
}

// 处理当前 CPU 到期的回调
static void _rcu_do_callbacks(void *data) {
    struct rcu_work *work = (struct rcu_work *)data;
    struct per_cpu_rcu *pcpu = work->pcpu;
    struct list_head *pos, *tmp;
    uint64_t completed = atomic_load_explicit(&rcu_state->completed, memory_order_acquire);
    uint64_t flags;

    spin_lock_irqsave(&pcpu->lock, &flags);

    // 遍历工作项中的回调链表
    list_for_each_safe(pos, tmp, &work->list) {
        struct rcu_head *curr = list_entry(pos, struct rcu_head, node);
        list_del(pos);

        if (curr->gp_seq <= completed) {
            // 到期回调，释放锁后执行
            spin_unlock_irqrestore(&pcpu->lock, flags);
            curr->func(curr);
            spin_lock_irqsave(&pcpu->lock, &flags);
        } else {
            // 未到期，头插回原链表并更新最小世代号
            list_add(&curr->node, &pcpu->cb_list);

            if (curr->gp_seq < pcpu->pending_gp_seq) pcpu->pending_gp_seq = curr->gp_seq;
        }
    }

    spin_unlock_irqrestore(&pcpu->lock, flags);
    kheap_free(work);
}

// 将当前 CPU 的回调链表提交给工作线程处理
static void _rcu_submit_callbacks(struct per_cpu_rcu *pcpu) {
    struct rcu_work *work;
    struct list_head tmp_list;
    uint64_t flags;

    // 先在锁外分配工作项
    work = kheap_alloc(sizeof(struct rcu_work));
    if (!work) RCU_PANIC("failed to allocate rcu_work\n");

    INIT_LIST_HEAD(&tmp_list);

    // 持锁摘下链表
    spin_lock_irqsave(&pcpu->lock, &flags);

    if (list_empty(&pcpu->cb_list)) {
        spin_unlock_irqrestore(&pcpu->lock, flags);
        kheap_free(work);
        return;
    }

    list_splice_init(&pcpu->cb_list, &tmp_list);
    pcpu->pending_gp_seq = UINT64_MAX;
    spin_unlock_irqrestore(&pcpu->lock, flags);

    INIT_LIST_HEAD(&work->list);
    list_splice(&tmp_list, &work->list);
    work->pcpu = pcpu;
    task_submit_work(_rcu_do_callbacks, work);
}

// 屏障回调，由 rcu_barrier 内部使用
static void _rcu_barrier_callback(struct rcu_head *head) {
    if (atomic_fetch_sub(&rcu_barrier_count, 1) == 1) task_wakeup(rcu_barrier_waiter);

    kheap_free(head);
}

// 检查并报告 QS
static void _rcu_check_qs(void) {
    struct per_cpu_rcu *pcpu = _rcu_this_cpu();

    if (
        pcpu->qs_pending &&
        !atomic_load_explicit(&pcpu->cpu_nesting, memory_order_acquire)
    ) _rcu_report_qs();
}

// 检查并提交到期回调
static void _rcu_check_callbacks(void) {
    struct per_cpu_rcu *pcpu = _rcu_this_cpu();

    if (!list_empty(&pcpu->cb_list)) {
        uint64_t completed = atomic_load_explicit(&rcu_state->completed, memory_order_acquire);

        if (pcpu->pending_gp_seq <= completed) _rcu_submit_callbacks(pcpu);
    }
}

// 检查并处理宽限期启动所需的本地 qs_pending 设置
static void _rcu_check_gp_local(void) {
    uint64_t gp_seq = atomic_load(&rcu_state->gp_seq);

    if ((gp_seq & RCU_STATE_MASK) != RCU_GP_ONGOING) return;

    struct per_cpu_rcu *pcpu = _rcu_this_cpu();

    if (!pcpu->qs_pending) {
        pcpu->qs_pending = true;

        // 设置后立即尝试报告，若当前 CPU 上无读临界区则直接完成
        if (!atomic_load_explicit(&pcpu->cpu_nesting, memory_order_relaxed)) _rcu_report_qs();
    }
}

// 重置位图为全 1，调用者必须持有 rcu_state->lock
static void _rcu_reset_qs_mask_locked(void) {
    bitmap_fill((bitmap_t *)rcu_state->qs_mask, rcu_state->cpu_count);
}

// 检查并启动新宽限期
static void _rcu_check_gp_start(void) {
    uint64_t gp_seq = atomic_load(&rcu_state->gp_seq);
    uint64_t flags;

    // 仅当系统空闲且本 CPU 有待处理回调时才启动
    if ((gp_seq & RCU_STATE_MASK) != RCU_GP_IDLE) return;

    struct per_cpu_rcu *pcpu = _rcu_this_cpu();

    if (list_empty(&pcpu->cb_list)) return;

    // 持全局锁，再次检查状态并完成宽限期启动
    spin_lock_irqsave(&rcu_state->lock, &flags);
    gp_seq = atomic_load(&rcu_state->gp_seq);

    if ((gp_seq & RCU_STATE_MASK) == RCU_GP_IDLE) {
        // 递增世代，状态置为进行中
        uint64_t new_gp_seq = (gp_seq & RCU_SEQ_MASK) + 4 | RCU_GP_ONGOING;
        atomic_store(&rcu_state->gp_seq, new_gp_seq);

        // 初始化位图：所有 CPU 均未报告
        _rcu_reset_qs_mask_locked();
    }
    spin_unlock_irqrestore(&rcu_state->lock, flags);

    // 当前 CPU 立即设置本地标志并尝试报告
    _rcu_check_gp_local();
}

// 检查并处理屏障请求
static void _rcu_check_barrier(void) {
    if (!atomic_load_explicit(&rcu_barrier_pending, memory_order_acquire)) return;

    struct per_cpu_rcu *pcpu = _rcu_this_cpu();
    uint32_t cur_gen = atomic_load(&rcu_barrier_generation);

    // 若本 CPU 尚未为当前屏障世代注册回调，则立即注册
    if (pcpu->barrier_generation != cur_gen) {
        struct rcu_head *head = kheap_alloc(sizeof(struct rcu_head));
        if (!head) RCU_PANIC("failed to allocate barrier head\n");
        rcu_call(head, _rcu_barrier_callback);
        pcpu->barrier_generation = cur_gen;
    }
}

// 任务迁移时的 RCU 状态转移
void rcu_migrate_task(rcu_task_struct *rcu, uint32_t src_cpu, uint32_t dst_cpu) {
    uint32_t nest = atomic_load_explicit(&rcu->nesting, memory_order_relaxed);

    if (!nest) return;

    uint64_t flags;
    spin_lock_irqsave(&rcu_state->lock, &flags);

    struct per_cpu_rcu *dst_pcpu = smp_get_cpu_rcu(dst_cpu);

    // 处理目标 CPU：若原本无读锁，则需撤销其静止状态
    if (!atomic_load_explicit(&dst_pcpu->cpu_nesting, memory_order_relaxed)) {
        dst_pcpu->qs_pending = true;
        bitmap_set((bitmap_t *)rcu_state->qs_mask, dst_cpu);
    }

    atomic_fetch_add_explicit(&dst_pcpu->cpu_nesting, nest, memory_order_relaxed);

    // 处理源 CPU
    struct per_cpu_rcu *src_pcpu = smp_get_cpu_rcu(src_cpu);
    atomic_fetch_sub_explicit(&src_pcpu->cpu_nesting, nest, memory_order_relaxed);

    spin_unlock_irqrestore(&rcu_state->lock, flags);
}

// 进入读临界区
void rcu_read_lock(void) {
    rcu_task_struct *rcu = _rcu_current_task();
    struct per_cpu_rcu *pcpu = _rcu_this_cpu();

    atomic_fetch_add_explicit(&rcu->nesting, 1, memory_order_relaxed);

    uint32_t old = atomic_fetch_add_explicit(&pcpu->cpu_nesting, 1, memory_order_acquire);

    // 若 cpu_nesting 从 0 变为 1 且当前有进行中宽限期，则撤销本 CPU 的静止状态
    if (!old) {
        uint64_t gp_seq = atomic_load_explicit(&rcu_state->gp_seq, memory_order_acquire);

        if ((gp_seq & RCU_STATE_MASK) == RCU_GP_ONGOING) {
            uint64_t flags;
            spin_lock_irqsave(&rcu_state->lock, &flags);

            if (!pcpu->qs_pending) {
                pcpu->qs_pending = true;
                bitmap_set((bitmap_t *)rcu_state->qs_mask, get_logical_id());
            }

            spin_unlock_irqrestore(&rcu_state->lock, flags);
        }
    }
}

// 退出读临界区
void rcu_read_unlock(void) {
    rcu_task_struct *rcu = _rcu_current_task();
    struct per_cpu_rcu *pcpu = _rcu_this_cpu();

    atomic_fetch_sub_explicit(&pcpu->cpu_nesting, 1, memory_order_release);

    if (atomic_fetch_sub_explicit(&rcu->nesting, 1, memory_order_relaxed) == 1) {
        uint64_t gp_seq = atomic_load_explicit(&rcu_state->gp_seq, memory_order_acquire);

        if ((rcu->gp_seq ^ gp_seq) & RCU_SEQ_MASK) rcu->gp_seq = gp_seq;
    }
}

/**
 * 注册回调，在宽限期结束后执行
 *
 * @param head 要注册的回调节点头指针
 * @param func 回调函数指针
 */
void rcu_call(struct rcu_head *head, void (*func)(struct rcu_head *)) {
    head->func = func;

    uint64_t gp_seq = atomic_load(&rcu_state->gp_seq);
    uint64_t state = gp_seq & RCU_STATE_MASK;
    uint64_t gen = gp_seq >> 2;

    // 计算回调所属世代号：空闲时归入下一代，进行中归入当前代
    if (state == RCU_GP_IDLE) {
        head->gp_seq = gen + 1;
    } else {
        head->gp_seq = gen;
    }

    struct per_cpu_rcu *pcpu = _rcu_this_cpu();
    uint64_t flags;

    spin_lock_irqsave(&pcpu->lock, &flags);

    bool was_empty = list_empty(&pcpu->cb_list);
    list_add_tail(&head->node, &pcpu->cb_list);

    // 更新本 CPU 回调链表的最早世代号
    if (was_empty) {
        pcpu->pending_gp_seq = head->gp_seq;
    } else if (head->gp_seq < pcpu->pending_gp_seq) {
        pcpu->pending_gp_seq = head->gp_seq;
    }

    spin_unlock_irqrestore(&pcpu->lock, flags);

    // 若当前无活跃宽限期，启动新宽限期
    if ((gp_seq & RCU_STATE_MASK) == RCU_GP_IDLE) {
        spin_lock_irqsave(&rcu_state->lock, &flags);
        gp_seq = atomic_load(&rcu_state->gp_seq);

        if ((gp_seq & RCU_STATE_MASK) == RCU_GP_IDLE) {
            uint64_t new_gp_seq = (gp_seq & RCU_SEQ_MASK) + 4 | RCU_GP_ONGOING;
            atomic_store(&rcu_state->gp_seq, new_gp_seq);

            _rcu_reset_qs_mask_locked();
        }
        spin_unlock_irqrestore(&rcu_state->lock, flags);

        // 当前 CPU 立即设置本地标志并尝试报告
        _rcu_check_gp_local();
    }
}

/**
 * 调度器回调，当任务被切换出时调用
 *
 * @param prev_rcu 被切换出任务的 RCU 私有数据指针
 */
void rcu_note_context_switch(rcu_task_struct *prev_rcu) {
    // 仅当被切换出的任务不在读临界区内时，才同步世代号
    if (atomic_load_explicit(&prev_rcu->nesting, memory_order_relaxed)) return;

    uint64_t gp_seq = atomic_load_explicit(&rcu_state->gp_seq, memory_order_acquire);

    if ((prev_rcu->gp_seq ^ gp_seq) & RCU_SEQ_MASK) prev_rcu->gp_seq = gp_seq;
}

// 统一状态推进入口
void rcu_state_run(void) {
    // 检查本 CPU 是否可报告 QS
    _rcu_check_qs();

    // 检查本 CPU 是否有到期回调需提交
    _rcu_check_callbacks();

    // 若存在进行中宽限期且本地未设置 qs_pending，则设置并尝试报告
    _rcu_check_gp_local();

    // 检查是否需要启动新宽限期
    _rcu_check_gp_start();

    // 检查是否有屏障请求需处理
    _rcu_check_barrier();
}

// 等待所有已注册回调执行完毕
void rcu_barrier(void) {
    uint32_t num_cpus = rcu_state->cpu_count;

    mutex_lock(&rcu_barrier_mutex);

    // 递增屏障世代号，使各 CPU 的 barrier_generation 失效
    uint32_t cur_gen = atomic_fetch_add(&rcu_barrier_generation, 1) + 1;

    atomic_store(&rcu_barrier_pending, true);
    atomic_store(&rcu_barrier_count, num_cpus + 1);
    rcu_barrier_waiter = smp_get_task_current();

    mutex_unlock(&rcu_barrier_mutex);

    atomic_fetch_sub(&rcu_barrier_count, 1);

    // 等待所有 CPU 的屏障回调执行完毕
    while (atomic_load(&rcu_barrier_count) > 0) task_sleep(true);

    mutex_lock(&rcu_barrier_mutex);
    atomic_store(&rcu_barrier_pending, false);
    mutex_unlock(&rcu_barrier_mutex);
}

// 分配并初始化一个 per_cpu_rcu 结构体
struct per_cpu_rcu *rcu_per_cpu_alloc(void) {
    struct per_cpu_rcu *pcpu = kheap_alloc(sizeof(struct per_cpu_rcu));

    if (!pcpu) RCU_PANIC("failed to allocate per_cpu_rcu\n");

    INIT_LIST_HEAD(&pcpu->cb_list);
    pcpu->gp_seq = 0;
    pcpu->pending_gp_seq = UINT64_MAX;
    spinlock_init(&pcpu->lock);
    pcpu->qs_pending = false;
    pcpu->barrier_generation = 0;
    atomic_init(&pcpu->cpu_nesting, 0);

    return pcpu;
}

// 初始化 RCU
void rcu_init(void) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
    uint32_t num_cpus = bootboot->numcores;

    // 初始化屏障互斥锁
    mutex_init(&rcu_barrier_mutex);

    // 分配全局 RCU 状态
    size_t state_size = sizeof(struct rcu_state) + BITMAP_BYTES(num_cpus);
    rcu_state = kheap_alloc(state_size);

    if (!rcu_state) RCU_PANIC("failed to allocate rcu_state\n");

    atomic_init(&rcu_state->gp_seq, 0);
    atomic_init(&rcu_state->completed, 0);
    spinlock_init(&rcu_state->lock);
    rcu_state->cpu_count = num_cpus;

    // 初始化位图为全 0（当前无宽限期进行中，可以不用屏障）
    bitmap_zero((bitmap_t *)rcu_state->qs_mask, num_cpus);
}