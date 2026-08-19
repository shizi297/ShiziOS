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
typedef id_t pid_t;

// 退出状态码类型（必须通过 EXIT_STATUS_* 宏打包/解包）
typedef int exit_status_t;

typedef enum {
    TASK_IS_THREAD         = 1 << 0,   // 创建线程
    TASK_WAIT_PARENT       = 1 << 1,   // 父进程等待子进程初始化完成
    TASK_WAKE_ON_EXIT      = 1 << 2,   // 子进程退出时唤醒父进程（通过 SIGCHLD）
} task_create_flags_t;

typedef enum : int {
    WNONE          = 0,
    WNOHANG        = 1U << 0,   // 非阻塞等待
    WUNTRACED      = 1U << 1,   // 也返回被停止的子进程
    WCONTINUED     = 1U << 2,   // 子进程从停止到继续执行时返回
} wait_options_t;

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

/*
 * 等待子进程状态变化
 *
 * @param pid 要等待的进程 ID
 * @param status  输出退出状态码
 * @param options 等待选项
 *
 * @return 子进程 PID
 */
__ktype(pid_t)
ku64 task_wait(pid_t pid, exit_status_t *status, wait_options_t options);

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
void task_exit(int code, bool signaled);

// 提交一个工作任务
void task_submit_work(void (*func)(void *), void *data);

// 获取当前任务的文件系统上下文（使用后需要尽快增加path引用和拷贝）
void task_get_current_fs(struct path **root, struct path **pwd);

// 获取当前任务的gid和uid
void task_get_current_ugid(uid_t *uid, gid_t *gid);

// 获取当前任务的线程id
pid_t task_get_current_thread_id(void);

// 获取任务的线程ID
pid_t task_get_pid(struct task_struct *task);

// 获取任务的进程ID
pid_t task_get_tgid(struct task_struct *task);

// 任务管理数据初始化
bool task_data_init(void);

// 任务管理初始化
bool task_init(void);

// 启动任务调度
void task_run(void);