/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <signal.h>
#include <spinlock.h>

// 信号集类型
#define _SIGSET_NUM_WORDS ((_NSIG + (8 * sizeof(unsigned long)) - 1) / (8 * sizeof(unsigned long)))

struct signal_t{
    unsigned long __bits[_SIGSET_NUM_WORDS];
};

typedef struct signal_t signal_t;

// 算出信号属于数组的那个元素
#define _SIGSET_WORD(sig)  (((sig) - 1) / (8 * sizeof(unsigned long)))

// 算出信号在这个元素的那个bit位
#define _SIGSET_BIT(sig)   (((sig) - 1) % (8 * sizeof(unsigned long)))

// 共享信号结构体（每个进程一个）
struct sig_shared {
    signal_t common_pending;          // 挂起信号集
    void (*sa[_NSIG])(int);       // 信号处理函数表
    atomic_uint_least16_t refcount;   // 引用计数
    spinlock_t lock;                   
};

// 信号结构体（每个线程一个）
struct signal_struct {
    signal_t pending;      // 线程私有挂起信号集
    signal_t blocked;      // 线程私有阻塞掩码
    spinlock_t priv_lock;  
    struct sig_shared *shared;  // 指向共享部分
};

// 清空信号集
static inline void sigemptyset(signal_t *set) {
    for (int i = 0; i < _SIGSET_NUM_WORDS; i++)
        set->__bits[i] = 0;
}

// 填满信号集
static inline void sigfillset(signal_t *set) {
    for (int i = 0; i < _SIGSET_NUM_WORDS - 1; i++)
        set->__bits[i] = ~0UL;
    set->__bits[_SIGSET_NUM_WORDS - 1] = (1UL << (_NSIG % (8*sizeof(unsigned long)))) - 1;
}

// 发送信号
static inline void sigaddset(signal_t *set, int sig) {
    set->__bits[_SIGSET_WORD(sig)] |= (1UL << _SIGSET_BIT(sig));
}

// 删除信号
static inline void sigdelset(signal_t *set, int sig) {
    set->__bits[_SIGSET_WORD(sig)] &= ~(1UL << _SIGSET_BIT(sig));
}

// 判断信号是否在信号集中
static inline int sigismember(const signal_t *set, int sig) {
    return (set->__bits[_SIGSET_WORD(sig)] >> _SIGSET_BIT(sig)) & 1;
}

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
                 int sig_num, bool has_private, bool force) {
    // TODO
    return false;
}

/**
 * 复制或设置为共享信号处理
 * 
 * @param parent_sig 父进程的信号结构体指针
 * @param new_sig 新的信号指针存放的地方
 * @param share 共享还是复制
 * 
 * @return 成功：true 
 * @return 失败：flase
 */
bool signal_copy(struct signal_struct *parent_sig, struct signal_struct **new_sig,
                 bool share) {
    // TODO
    return false;
}

/**
 * 销毁信号
 * 
 * @param sig 信号结构体指针
 */
void signal_destroy(struct signal_struct *sig) {
    // TODO
}

/**
 * 退出时的信号处理
 * 
 * @param task 要退出的任务
 * @param sig 要退出的任务的信号结构体指针
 * @param tell 是否向父进程发送 SIGCHLD
 */
void signal_exit(struct task_struct *task, struct signal_struct *sig,
                 bool tell) {
    // TODO
}

/**
 * 执行信号处理
 * 
 * @param task 要执行信号处理的任务
 * @param regs 当前任务的用户态寄存器状态
 */
void signal_exec(struct task_struct *task, struct pt_regs *regs) {
    // TODO
}

/***
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
                       const signal_t *set, signal_t *oldset) {
    // TODO
    return false;
}