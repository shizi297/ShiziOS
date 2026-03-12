/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>

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

// 信号集类型
#define _SIGSET_NUM_WORDS ((_NSIG + (8 * sizeof(unsigned long)) - 1) / (8 * sizeof(unsigned long)))

struct signal;

// 算出信号属于数组的那个元素
#define _SIGSET_WORD(sig)  (((sig) - 1) / (8 * sizeof(unsigned long)))

// 算出信号在这个元素的那个bit位
#define _SIGSET_BIT(sig)   (((sig) - 1) % (8 * sizeof(unsigned long)))

// 清空信号集
#define sigemptyset(set) do { \
    int __i; \
    for (__i = 0; __i < _SIGSET_NUM_WORDS; ++__i) \
        (set)->__bits[__i] = 0; \
} while(0)

// 填充信号集（包含所有信号）
#define sigfillset(set) do { \
    int __i; \
    for (__i = 0; __i < _SIGSET_NUM_WORDS - 1; ++__i) \
        (set)->__bits[__i] = ~0UL; \
    (set)->__bits[_SIGSET_NUM_WORDS - 1] = \
        (1UL << (_NSIG % (8*sizeof(unsigned long)))) - 1; \
} while(0)

// 向信号集中添加信号
#define sigaddset(set, sig) \
    ((set)->__bits[_SIGSET_WORD(sig)] |= (1UL << _SIGSET_BIT(sig)))

// 从信号集中删除信号
#define sigdelset(set, sig) \
    ((set)->__bits[_SIGSET_WORD(sig)] &= ~(1UL << _SIGSET_BIT(sig)))

// 测试信号是否在信号集中
#define sigismember(set, sig) \
    ((set)->__bits[_SIGSET_WORD(sig)] & (1UL << _SIGSET_BIT(sig)))