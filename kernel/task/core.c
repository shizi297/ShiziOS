/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

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
#include <libtree.h>
#include <dynarr.h>

#define INIT_TIME_SLICE_NS timecycle_msec_to_ns(20)

// 用于线程id与线程组id
typedef int task_id; 
typedef task_id id_t;

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

// copy函数的参数
struct task_copy_arg {
    task_flags flags;
    uint64_t user_stack;
    uint64_t tls;
};

// 任务状态
typedef enum {
    TASK_RUNNING = 0,           // 运行
    TASK_INTERRUPTIBLE = 1,     // 可中断睡眠
    TASK_UNINTERRUPTIBLE = 2,   // 不可中断睡眠
    TASK_STOPPED = 3,           // 停止
    EXIT_ZOMBIE = 4,            // 僵尸
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
    sched_data *sched;

    id_t pid;  // 线程id，每个线程有一个
    id_t tgid; // 线程组id，每个进程有一个，同一进程的线程共享

    as_t *as;    // 进程地址空间描述符，线程间共享

    void *stack; // 内核栈指针

    task_state state;  // 任务当前的状态

    void (*wakeup_cb)(struct task_struct *);    // 睡眠队列的回调函数

    struct signal_struct *signal;  // 信号相关

    struct pt_regs *regs;    // 存储中断/异常/系统调用/信号处理信息
    struct thread_struct thread;   // 任务切换时保存的信息

    struct list_head zombie;    // 僵尸队列头

    struct list_head sibling;     // 子进程节点
    struct list_head children;    // 子进程链表头

    struct list_head sleep; // 睡眠队列节点

    /**
     * 指向父进程
     * 如果为null
     * 表示进程终止时不向父进程发送SIGCHLD信号
     */
    struct task_struct *father;   
    
    struct list_head thread_group;  // 线程组节点
    struct task_struct *group_leader;  // 指向主线程
} task_struct;

// 调度器接口，调度器通过注册让内核调度框架统一调用
struct sched_class {
    void (*enqueue)(void *sched, struct task_struct *task);  // 入队
    void (*dequeue)(void *sched, struct task_struct *task);  // 出队
    struct task_struct *(*pick_next)(void *sched);     // 选择下一个任务
    void (*set_prio)(void *sched, struct task_struct *task, int prio);      // 设置任务优先级
    void (*update_tick)(void *sched, struct task_struct *task, uint64_t ns);   // 更新任务时间
    void *(*init)(void);            // 初始化调度器，返回节点头
};

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
static per_cpu_task_state_struct *per_cpu_task_state_ptr = NULL;

INIT_BITMAP(id_bitmap, TASK_ID_MAX);
static spinlock_t id_lock = SPIN_LOCK_INIT;
dynarr_t *id_map;
static struct sched_class *sched_class;

/**
 * id分配，可以用于任务管理的任何id
 * 
 * @return 成功：id
 * @return 失败：-1
 */
static id_t id_alloc(void) {
    id_t id;

    spin_lock(&id_lock);
    id = (id_t)bitmap_find(id_bitmap, TASK_ID_MAX, 0, 0);
    if (id != TASK_ID_MAX)
        bitmap_set(id_bitmap, id);
    spin_unlock(&id_lock);

    return (id == TASK_ID_MAX) ? -1 : id;
}

/**
 * id释放
 * 
 * @param id 要释放的id
 */
static void id_free(id_t id) {
    if (id < 0 || id >= TASK_ID_MAX)
        return;

    spin_lock(&id_lock);
    bitmap_clear(id_bitmap, id);
    spin_unlock(&id_lock);
}



/**
 * 复制任务
 * 
 * @param arg 参数
 */
task_struct *task_copy(struct task_copy_arg arg) {
    // TODO
    return NULL;
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

    // 创建id映射，用于找到对应id的task，最大容量为TASK_ID_MAX
    id_map = dynarr_create(sizeof(struct task_struct *), TASK_ID_MAX);
    if (!id_map) return false;

    return true;
}

// 任务管理初始化
bool task_init(void) {
    void *sched = sched_class->init();
    smp_set_sched(sched);

    return true;
}