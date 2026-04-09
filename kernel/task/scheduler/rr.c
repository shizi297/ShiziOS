/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <task/types.h>
#include <asm/smp.h>
#include <time.h>
#include <timecycle.h>

#define TIME_SLICE_MS 10

struct per_cpu_sched {
    struct list_head run_queue;
};

void rr_register(void);

sched_func_t sched_func = rr_register;

extern struct sched_class *sched_class_ptr;

// 入队
void rr_enqueue(struct task_struct *task) {
    per_cpu_sched *pcpu_sched = smp_get_sched();

    // 确保节点处于干净状态（防止残留指针）
    INIT_LIST_HEAD(&task->sched.list);

    // 重置时间片相关字段，保证下次运行时获得完整时间片
    task->sched.time_slice_ns = timecycle_msec_to_ns(TIME_SLICE_MS);
    task->sched.exec_ns = 0;
    task->sched.exec_start_ns = 0;

    list_add_tail(&task->sched.list, &pcpu_sched->run_queue);
    smp_set_nr_running(smp_get_nr_running() + 1);
}

// 出队
void rr_dequeue(struct task_struct *task) {
    list_del_init(&task->sched.list);
    smp_set_nr_running(smp_get_nr_running() - 1);
}

// 选择下一个任务
struct task_struct *rr_pick_next(void) {
    per_cpu_sched *pcpu_sched = smp_get_sched();

    // 如果没有任务返回idle
    if (list_empty(&pcpu_sched->run_queue)) return smp_get_idle();

    // 取出队头任务，并将其从就绪队列中移除
    struct task_struct *next = list_first_entry(
        &pcpu_sched->run_queue,
        struct task_struct,
        sched.list
    );
    list_del_init(&next->sched.list);
    smp_set_nr_running(smp_get_nr_running() - 1);

    // 重置时间片计数器，确保新任务开始运行时定时器能被设置
    next->sched.exec_ns = 0;
    next->sched.exec_start_ns = 0;

    return next;
}

// 让最后一个任务出队
static struct task_struct *rr_dequeue_tail(void) {
    per_cpu_sched *pcpu_sched = smp_get_sched();
    if (list_empty(&pcpu_sched->run_queue)) {
        return NULL;
    }

    // 取队尾
    struct list_head *tail = pcpu_sched->run_queue.prev;
    struct task_struct *task = list_entry(tail, struct task_struct, sched.list);
    list_del_init(tail);
    smp_set_nr_running(smp_get_nr_running() - 1);
    return task;
}

// 更新优先级
void rr_set_prio(struct task_struct *task, int prio) {
    // 目前的优先级没用
    task->sched.prio = prio;
}

// 更新时间片
void rr_update_tick(struct task_struct *task, uint64_t ns) {
    sched_data *sched = &task->sched;
    sched->exec_ns += ns;

    // 如果更新后运行时间超过任务的时间片
    if (sched->exec_ns >= sched->time_slice_ns) {
        sched->exec_ns = 0;
        smp_set_need_sched();
    }
}

// 初始化sched结构体
void rr_sched_init(struct task_struct *task) {
    sched_data *sched = &task->sched;
    sched->time_slice_ns = timecycle_msec_to_ns(TIME_SLICE_MS);
    sched->exec_ns = 0;
    sched->prio = 0;
    sched->exec_start_ns = 0;
    INIT_LIST_HEAD(&sched->list);
}

// 设置中断
void rr_set_next_timer(struct task_struct *task) {
    /*
     * 当前exec_ns就设置中断
     * 否则不设置
     * 
     * 因为当新任务的exec_ns为0时
     * 说明还没有设置中断
     * 如果exec_ns不为0,
     * 说明任务已经运行一段时间
     * 定时器还在工作
     * 没有必要重新设置定时器
     * 
     * 如果被抢占
     * 因为他在系统中总是在调用重新调度后才会被调用
     * 所以per_cpu的current已经为新任务的了
     * 新任务的exec_ns为0
     * 所以自然会被重新设置为新任务的
     */
    if (!task->sched.exec_ns) {
        struct clockevent_timer *timer = smp_get_sched_timer();
        clockevent_timer_add(timer, task->sched.time_slice_ns);
    }
}

// 初始化
void rr_init(void) {
    per_cpu_sched *sched = (per_cpu_sched *)kheap_alloc(sizeof(per_cpu_sched));

    rr_sched_init(smp_get_idle());

    smp_set_sched(sched);
    INIT_LIST_HEAD(&sched->run_queue);
}

// 注册到调度框架
void rr_register(void) {
    sched_class_ptr->dequeue = rr_dequeue;
    sched_class_ptr->enqueue = rr_enqueue;
    sched_class_ptr->init = rr_init;
    sched_class_ptr->pick_next = rr_pick_next;
    sched_class_ptr->dequeue_tail = rr_dequeue_tail;
    sched_class_ptr->sched_init = rr_sched_init;
    sched_class_ptr->set_next_timer = rr_set_next_timer;
    sched_class_ptr->set_prio = rr_set_prio;
    sched_class_ptr->update_tick = rr_update_tick;
}