/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <signal.h>

typedef struct {
    unsigned long __bits[_SIGSET_NUM_WORDS];
} signal_t;

struct signal_struct{
    signal_t signal;
    signal_t common_signal; 
    signal_t blocked;   // 线程私有的信号阻塞掩码
    void (*sa[_NSIG])(int signal);  // 信号处理函数指针
    atomic_uint_least16_t count;    // 引用计数
};