/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <signal.h>
#include <processor.h>
#include <heap.h>
#include <list.h>
#include <libtree.h>

// 用于线程id与线程组id
typedef int pid_t; 

// 调度器接口，调度器通过注册让内核调度框架统一调用
struct sched_class {
    void (*enqueue)(void);  // 入队
    void (*dequeue)(void);  // 出队
    void (*pick_next)(void);    // 选择下一个任务
    void (*set_priovoid); // 设置任务优先级
    void (*init)(void); // 初始化调度器
};

// 任务状态
typedef enum {
    TASK_RUNNING = 0,           // 运行
    TASK_INTERRUPTIBLE = 1,     // 可中断睡眠
    TASK_UNINTERRUPTIBLE = 2,   // 不可中断睡眠
    TASK_STOPPED = 3,           // 停止
    EXIT_ZOMBIE = 4,            // 僵尸
} task_state;

// 任务结构体，每个线程有一个
typedef struct task_struct {
    uint64_t time_slice_ns;
    uint64_t exec_start;

    struct {
        struct list_head list;  // 用于使用fifo的调度器和睡眠队列
        struct rbtree_node rb;
    };

    pid_t pid;  // 线程id，每个线程有一个
    pid_t tgid; // 线程组id，每个进程有一个，同一进程的线程共享

    as_t *as;    // 进程地址空间描述符，线程间共享

    void *stack; // 内核栈指针

    task_state state;  // 任务当前的状态

    void (*wakeup_cb)(struct task_struct *);    // 睡眠队列的回调函数

    int prio;   // 任务的优先级，影响任务被调度的时长

    signal_t signal;    // 信号组，用于标记信号
    void (*sa[_NSIG])(int signal);   // 注册的信号处理函数

    signal_t *common_signal;    // 对于整个线程组的信号
    void (*common_sa[_NSIG])(int signal);   // 对于整个线程组的信号处理函数

    struct pt_regs *regs;    // 存储中断/异常/系统调用/信号处理信息
    struct thread_struct thread;   // 任务切换时保存的信息

    struct list_head zombie;    // 僵尸队列头
}task_struct;