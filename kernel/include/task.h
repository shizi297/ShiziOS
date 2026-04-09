/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <asm/processor.h>

struct task_struct;
typedef struct per_cpu_sched per_cpu_sched;
typedef struct task_struct task_struct;

// 任务标志，用于任务创建时指定行为
typedef enum {
    TASK_NONE       = 0,            // 无特殊标志
    TASK_SIGCHLD    = 1 << 0,       // 子进程终止时向父进程发送 SIGCHLD
    TASK_VM         = 1 << 1,       // 内存地址空间共享
    TASK_FS         = 1 << 2,       // 文件系统上下文共享
    TASK_FILES      = 1 << 3,       // 文件描述符表共享
    TASK_SIGHAND    = 1 << 4,       // 信号处理表共享
    TASK_THREAD     = 1 << 5,       // 将新任务加入同一线程组（tgid 相同）
} task_flags;

// 更新当前任务的时间
void task_add_current_tick(uint64_t tick);

/**
 * 复制任务
 * 
 * @param task 要复制的任务
 * @param flags 标志位
 * 
 * @return 成功：task指针
 * @return 失败：NULL
 */
task_struct *task_copy(struct task_struct *task, task_flags flags);

/**
 * 创建内核线程
 * 
 * @param func 线程入口函数
 * @param arg  传递给线程的参数
 * * 
 * @return 成功：任务结构体指针
 * @return 失败：NULL
 */
task_struct *task_create_kernel_thread(void (*func)(void *), void *arg);

// 等待子任务结束并回收资源
void task_wait(void);

// 设置下一次中断
void task_set_next_timer(void);

// 让当前任务睡眠
task_struct *task_sleep(bool interruptible);

// 唤醒睡眠的任务
void task_wakeup(task_struct *task);

// 重新调度任务
void task_sched(void);

// 任务退出
void task_exit(void);

// 提交一个工作任务
void task_submit_work(void (*func)(void *), void *data);

// 任务管理数据初始化
bool task_data_init(void);

// 任务管理初始化
void task_init(void);