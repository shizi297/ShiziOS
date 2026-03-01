/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "types.h"
#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include <smp.h>
#include <bootboot.h>
#include <heap.h>
#include <signal.h>
#include <timecycle.h>
#include <arch_processor.h>
#include <config.h>
#include <bitmap.h>

#define INIT_TIME_SLICE_NS timecycle_msec_to_ns(20)

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
static per_cpu_task_state_struct *per_cpu_task_state_ptr = NULL;

INIT_BITMAP(pid_bitmap, PID_MAX);

/*
 * 用于存储对应状态的任务
 * 运行/就绪调度器负责，这里不放
 * 睡眠由调用睡眠的对应模块来管，这里也不放
 * 僵尸进程是task_struct的，不是per_cpu的，这里不做
 */
typedef struct {
    struct list_head stopped;           // 停止
} task_state_struct;

typedef struct {
    uint32_t count;
    task_state_struct state[];
} per_cpu_task_state_struct; 

// 任务标志，用于任务创建时指定行为
typedef enum {
    TASK_NONE       = 0,            // 无特殊标志
    TASK_SIGCHLD    = 1 << 0,       // 子进程终止时向父进程发送 SIGCHLD
    TASK_VM         = 1 << 1,       // 内存地址空间共享
    TASK_FS         = 1 << 2,       // 文件系统上下文共享
    TASK_FILES      = 1 << 3,       // 文件描述符表共享
    TASK_SIGHAND    = 1 << 4,       // 信号处理表共享
    TASK_THREAD     = 1 << 5,       // 将新任务加入同一线程组（tgid 相同）
    TASK_SETTLS     = 1 << 6,       // 设置tls
} task_flags;

/**
 * pid与tgid分配
 * 
 * @return 成功：pid
 * @return 失败：-1
 */
static pid_t pid_alloc(void) {
    pid_t pid = (pid_t)bitmap_find(&pid_bitmap, PID_MAX, 0, 1);
    if (pid == PID_MAX) return -1;

    return pid;
}

task_struct *task_create(void (*entry)(void *), void *arg, task_flags flags) {
    // TODO
}

// 任务管理数据初始化
bool task_data_init(void) {
    uint32_t max_cpu = bootboot->numcores;
    uint64_t per_cpu_task_stopped_size = sizeof(uint32_t) + (sizeof(task_state_struct) * max_cpu); 
    per_cpu_task_state_ptr = kheap_alloc(per_cpu_task_stopped_size);
    if (!per_cpu_task_state_ptr) return false;

    // 初始化链表节点
    for (int i = 0; i < max_cpu;i++) {
        INIT_LIST_HEAD(&per_cpu_task_state_ptr->state[i].stopped);
    }

    // TODO: 初始化调度器的数据结构

    return true;
}


