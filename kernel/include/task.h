/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <asm/processor.h>
#include <vfs.h>
#include <klibc.h>

struct task_struct;
typedef struct per_cpu_sched per_cpu_sched;
typedef struct task_struct task_struct;

// 用于线程id与线程组id
typedef int task_id; 
typedef task_id id_t;

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

typedef enum {
    TASK_IS_THREAD         = 1 << 0,   // 创建线程
} task_create_flags_t;

struct task_attrs {
    task_create_flags_t flags;   // 创建行为标志
    int *inherit_fds;            // 显式继承的文件描述符数组
    size_t fd_count;             // 数组长度

    union {
        struct {
            void *entry_point;   // 线程入口函数地址
            void *stack_base;    // 用户态栈底地址
            size_t stack_size;   // 用户态栈大小
        } thread;

        struct {
            const char *exec_path;  // 可执行文件路径
            const char **argv;      // 命令行参数（以 NULL 结尾）
            const char **envp;      // 环境变量（以 NULL 结尾）
        } process;
    };
};

// 更新当前任务的时间
void task_add_current_tick(uint64_t tick);

/*
 * 创建新任务
 *
 * @param attrs 任务属性
 * @param size 结构体大小
 *
 * @return 任务控制块指针
 */
__ktype(struct task_struct *)
kptr task_create_new(struct task_attrs *attrs, size_t size);

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

/*
 * 向指定任务发送信号
 *
 * @param task 目标任务
 * @param sig 要发送的信号
 */
void task_send_signal(struct task_struct *task, int sig);

// 任务退出
void task_exit(void);

// 提交一个工作任务
void task_submit_work(void (*func)(void *), void *data);

// 获取当前任务的文件系统上下文（使用后需要尽快增加path引用和拷贝）
void task_get_current_fs(struct path **root, struct path **pwd);

// 获取当前任务的gid和uid
void task_get_current_ugid(uid_t *uid, gid_t *gid);

// 获取当前任务的线程id
id_t task_get_current_thread_id(void);

// 任务管理数据初始化
bool task_data_init(void);

// 任务管理初始化
bool task_init(void);

// 启动任务调度
void task_run(void);