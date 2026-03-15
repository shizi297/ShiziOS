/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <arch_processor.h>

struct task_struct;
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

// 任务管理数据初始化
bool task_data_init(void);

// 任务管理初始化
bool task_init(void);