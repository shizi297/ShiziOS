/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <asm/processor.h>
#include <task.h>

struct signal_struct;
typedef struct signal_t signal_t;

#define SIGHUP     1   // 终端挂断或控制进程终止
#define SIGINT     2   // 来自键盘的中断 (Ctrl+C)
#define SIGQUIT    3   // 来自键盘的退出 (Ctrl+\)
#define SIGILL     4   // 非法硬件指令
#define SIGTRAP    5   // 跟踪/断点陷阱
#define SIGABRT    6   // 异常终止 
#define SIGBUS     7   // 总线错误/内存访问错误
#define SIGFPE     8   // 浮点运算异常
#define SIGKILL    9   // 强制终止信号
#define SIGUSR1   10   // 用户自定义信号 1
#define SIGSEGV   11   // 无效内存引用 (段错误)
#define SIGUSR2   12   // 用户自定义信号 2
#define SIGPIPE   13   // 管道破裂：向无读端的管道写入
#define SIGALRM   14   // 定时器超时
#define SIGTERM   15   // 终止信号 

#define SIGCHLD   17   // 子进程状态改变
#define SIGCONT   18   // 继续已停止的进程
#define SIGSTOP   19   // 停止进程执行 
#define SIGTSTP   20   // 终端发来的停止信号 (Ctrl+Z)
#define SIGTTIN   21   // 后台进程尝试从控制终端读取
#define SIGTTOU   22   // 后台进程尝试向控制终端写入

#define SIGXCPU   24   // 进程CPU时间超限
#define SIGXFSZ   25   // 文件大小超限
#define SIGVTALRM 26   // 虚拟定时器超时
#define SIGPROF   27   // 性能分析定时器超时
#define SIGWINCH  28   // 终端窗口大小改变
#define SIGIO     29   // I/O就绪 
#define SIGPOLL   SIGIO   // I/O就绪 
#define SIGSYS    31   // 无效系统调用

// 信号编号范围定义
#define _NSIG     32   // 标准信号数量 
#define SIGRTMIN  32   // 实时信号起始编号 
#define SIGRTMAX  (_NSIG + 64) // 实时信号最大编号 

// 标准信号动作类型
#define SIG_ERR   ((void (*)(int))-1)  // 信号错误返回值
#define SIG_DFL   ((void (*)(int))0)   // 默认动作
#define SIG_IGN   ((void (*)(int))1)   // 忽略信号

/**
 * 发送信号
 * 
 * @param target 要发送的任务指针
 * @param sig 要发送的信号集
 * @param sig_num 要发送的信号
 * @param has_private 要发送的信号是否为线程私有的
 * @param force 是否强制发送
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool signal_send(struct task_struct *target, struct signal_struct *sig,
                 int sig_num, bool has_private, bool force);

/**
 * 复制或设置为共享信号处理
 * 
 * @param parent_sig 父进程的信号结构体指针
 * @param new_sig 新的信号指针存放的地方
 * @param share 共享还是复制
 * 
 * @return 成功：true 
 * @return 失败：false
 */
bool signal_copy(struct signal_struct *parent_sig, struct signal_struct **new_sig,
                 bool share);

/**
 * 销毁信号
 * 
 * @param sig 信号结构体指针
 */
void signal_destroy(struct signal_struct *sig);

/**
 * 退出时的信号处理
 * 
 * @param task 要退出的任务
 * @param sig 要退出的任务的信号结构体指针
 * @param tell 是否向父进程发送 SIGCHLD
 */
void signal_exit(struct task_struct *task, struct signal_struct *sig,
                 bool tell);

/**
 * 执行信号处理
 * 
 * @param task 要执行信号处理的任务
 * @param regs 当前任务的用户态寄存器状态
 */
void signal_exec(struct task_struct *task, struct pt_regs *regs);

/**
 * 信号堵塞操作
 * 
 * @param sig 信号结构体指针
 * @param how 执行的操作
 * @param set 设置的信号
 * @param oldset 修改前的堵塞状态
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool signal_blocked_op(struct signal_struct *sig, int how,
                       const signal_t *set, signal_t *oldset);
