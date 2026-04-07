/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <list.h>
#include <libtree.h>
#include <heap.h>

// 用于线程id与线程组id
typedef int task_id; 
typedef task_id id_t;

typedef void (*sched_func_t)(void);
typedef void (*task_test_func_t)(void);

// 测试函数
extern task_test_func_t task_test;

// 任务状态
typedef enum {
    TASK_RUNNING = 0,           // 运行
    TASK_INTERRUPTIBLE = 1,     // 可中断睡眠
    TASK_UNINTERRUPTIBLE = 2,   // 不可中断睡眠
    TASK_STOPPED = 3,           // 停止
    TASK_ZOMBIE = 4,            // 僵尸
} task_state;

// 调度器数据
typedef struct {
    uint64_t time_slice_ns; // 最大能执行的时间片
    uint64_t exec_start_ns; // 被调度的起始时间
    uint64_t exec_ns;   // 已经执行的时间

    int prio;   // 任务的优先级，影响任务被调度的时长

    struct {
        struct list_head list;  // 用于使用fifo的调度器
        struct rbtree_node rb;
    };
} sched_data;

// 任务结构体，每个线程有一个
typedef struct task_struct {

    // 调度器数据
    sched_data sched;

    id_t pid;  // 线程id，每个线程有一个
    id_t tgid; // 线程组id，每个进程有一个，同一进程的线程共享

    as_t *as;    // 进程地址空间描述符，线程间共享

    void *stack; // 内核栈指针

    task_state state;  // 任务当前的状态

    struct signal_struct *signal;  // 信号相关

    struct pt_regs *regs;    // 存储中断/异常/系统调用/信号处理信息
    struct thread_struct *thread;   // 任务切换时保存的信息

    struct list_head zombie;    // 僵尸队列头

    struct list_head sibling;     // 子进程节点
    struct list_head children;    // 子进程链表头

    struct list_head sleep; // 睡眠队列节点

    // 指向父进程
    struct task_struct *father;   
    
    struct list_head thread_group;  // 线程组节点
    struct task_struct *group_leader;  // 指向主线程

    spinlock_t list_lock;

    // 进程终止时是否向父进程发送SIGCHLD信号
    bool sigchld;
} task_struct;

// 调度器接口，调度器通过注册让内核调度框架统一调用
struct sched_class {
    void (*enqueue)(struct task_struct *task);  // 入队
    void (*dequeue)(struct task_struct *task);  // 出队
    struct task_struct *(*dequeue_tail)(void);  // 让最后一个任务出队
    struct task_struct *(*pick_next)(void); // 选择下一个任务
    void (*set_prio)(struct task_struct *task, int prio);   // 设置优先级
    void (*update_tick)(struct task_struct *task, uint64_t ns); // 更新时间片
    void (*sched_init)(struct task_struct *task);    // 初始化嵌入的 sched_data
    void (*set_next_timer)(struct task_struct *task);   // 设置中断
    void (*init)(void); // 初始化
};