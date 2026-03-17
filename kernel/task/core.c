/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include <task.h>
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
#include <serial.h>

#define TASK_PRINT(str) \
    serial_puts("[TASK]" str)

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
    struct thread_struct *thread;   // 任务切换时保存的信息

    struct list_head zombie;    // 僵尸队列头

    struct list_head sibling;     // 子进程节点
    struct list_head children;    // 子进程链表头

    struct list_head sleep; // 睡眠队列节点

    // 指向父进程
    struct task_struct *father;   
    
    struct list_head thread_group;  // 线程组节点
    struct task_struct *group_leader;  // 指向主线程

    spinlock_t list_lock;

    // 进程终止时是否向父进程发送SIGCHLD信号
    bool sigchld;
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
static id_t kernel_id = 0;

/**
 * id分配，可以用于任务管理的任何id
 * 
 * @return 成功：id
 * @return 失败：-1
 */
__attribute__((optnone))
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
    dynarr_set(id_map, id, NULL);
    spin_unlock(&id_lock);
}

// 分配调度器私有数据
sched_data *sched_data_init(void) {
    // TODO
    return NULL;
}

/**
 * 将子任务添加到父任务的 children 链表
 * 
 * @param parent 父任务
 * @param child  子任务
 */
static void task_add_child(struct task_struct *parent, struct task_struct *child) {
    spin_lock(&parent->list_lock);
    list_add_tail(&child->sibling, &parent->children);
    spin_unlock(&parent->list_lock);
}

/**
 * 从父任务的 children 链表中移除子任务
 * 
 * @param parent 父任务
 * @param child  子任务
 */
static void task_remove_child(struct task_struct *parent, struct task_struct *child) {
    spin_lock(&parent->list_lock);
    list_del(&child->sibling);
    spin_unlock(&parent->list_lock);
}

/**
 * 将线程添加到线程组 leader 的 thread_group 链表
 * 
 * @param leader 线程组主线程
 * @param thread 要添加的线程
 */
static void thread_group_add(struct task_struct *leader, struct task_struct *thread) {
    spin_lock(&leader->list_lock);
    list_add_tail(&thread->thread_group, &leader->thread_group);
    spin_unlock(&leader->list_lock);
}

/**
 * 从线程组 leader 的 thread_group 链表中移除线程
 * 
 * @param leader 线程组主线程
 * @param thread 要移除的线程
 */
static void thread_group_remove(struct task_struct *leader, struct task_struct *thread) {
    spin_lock(&leader->list_lock);
    list_del(&thread->thread_group);
    spin_unlock(&leader->list_lock);
}

// 更新当前任务的时间
void task_add_current_tick(uint64_t tick) {
    task_struct *current = smp_get_task_current();
    if (!current || !current->sched || !sched_class || !sched_class->update_tick)
        return;
    sched_class->update_tick(current->sched, current, tick);
}

/**
 * 复制任务
 * 
 * @param task 要复制的任务
 * @param flags 标志位
 * 
 * @return 成功：task指针
 * @return 失败：NULL
 */
task_struct *task_copy(struct task_struct *task, task_flags flags) {
    struct task_struct *new_task = NULL;
    bool tgid_allocated = false;   // 标记是否为新进程分配了独立的tgid

    // 分配任务结构体
    new_task = (struct task_struct *)kheap_alloc(sizeof(struct task_struct));
    if (!new_task) return NULL;
    spinlock_init(&new_task->list_lock);   // 初始化锁

    // 分配内核栈
    new_task->stack = kheap_alloc(KERNEL_START_SIZE);
    if (!new_task->stack) goto fail;

    // 分配pid
    new_task->pid = id_alloc();
    if (new_task->pid == -1) goto fail;

    // 设置tgid
    if (flags & TASK_THREAD) {
        new_task->tgid = task->tgid;               // 线程：共享父任务的tgid
    } else {
        new_task->tgid = id_alloc();                // 新进程：分配新的tgid
        if (new_task->tgid == -1) goto fail;
        tgid_allocated = true;
    }

    // 复制地址空间
    if (flags & TASK_VM) {
        new_task->as = task->as;                     // 共享地址空间
        vheap_as_add_ref(new_task->as);
    } else {
        new_task->as = vheap_copy_as(task->as);      // 复制地址空间
        if (!new_task->as) goto fail;
    }

    // TODO: 文件系统上下文（TASK_FS）和文件描述符表（TASK_FILES）处理

    // 复制信号处理表
    bool share_signal = (flags & TASK_SIGHAND) ? true : false;
    if (!signal_copy(task->signal, &new_task->signal, share_signal)) goto fail;

    // 分配调度器私有数据
    new_task->sched = sched_data_init();
    if (!new_task->sched) goto fail;

    // 设置任务状态
    new_task->state = TASK_RUNNING;
    new_task->sigchld = (flags & TASK_SIGCHLD) ? true : false;

    // 初始化链表
    INIT_LIST_HEAD(&new_task->zombie);
    INIT_LIST_HEAD(&new_task->sibling);
    INIT_LIST_HEAD(&new_task->children);
    INIT_LIST_HEAD(&new_task->sleep);
    INIT_LIST_HEAD(&new_task->thread_group);

    // 设置父子关系
    new_task->father = task;
    task_add_child(task, new_task);  

    // 设置线程组关系
    if (flags & TASK_THREAD) {
        new_task->group_leader = task->group_leader;
        thread_group_add(new_task->group_leader, new_task);   
    } else {
        new_task->group_leader = new_task;
    }

    // 加入调度队列
    void *sched = smp_get_sched();
    if (!sched) goto fail;
    sched_class->enqueue(sched, new_task);

    // 添加到映射表
    dynarr_set(id_map, new_task->pid, &new_task);

    return new_task;

fail:
    // 错误处理
    if (new_task->group_leader && new_task->group_leader != new_task) {
        thread_group_remove(new_task->group_leader, new_task);
    }

    if (new_task->father) {
        task_remove_child(new_task->father, new_task);
    }

    if (new_task->sched) kheap_free(new_task->sched);
    if (new_task->signal) signal_destroy(new_task->signal);
    if (!(flags & TASK_VM) && new_task->as) {
        vheap_destroy_as(new_task->as);
    } 

    if (tgid_allocated && new_task->tgid != -1) id_free(new_task->tgid);
    if (new_task->pid != -1) id_free(new_task->pid);
    if (new_task->stack) kheap_free(new_task->stack);

    kheap_free(new_task);
    return NULL;
}

// 重新调度任务
void task_sched(void) {
    irq_off();

    task_struct *prev = smp_get_task_current();
    if (!prev) goto out;

    task_struct *next = sched_class->pick_next(smp_get_sched());
    if (!next) goto out;

    // 如果是同一个任务，不需要切换
    if (prev == next) goto out;

    // 保存fpu状态
    fpu_save(prev->thread);

    // 更新当前cpu运行的任务
    smp_set_task_current(next);

    // 切换上下文
    switch_to(prev->thread, next->thread);

    // 使用当前任务的fpu
    fpu_restore(next->thread);

    out:
        irq_on();
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

    kernel_id = id_alloc();
    if (kernel_id == -1) return false;

    // 创建id映射，用于找到对应id的task，最大容量为TASK_ID_MAX
    id_map = dynarr_create(sizeof(struct task_struct *), TASK_ID_MAX);
    if (!id_map) return false;

    TASK_PRINT("task data init success\n");

    return true;
}

// 任务管理初始化
bool task_init(void) {
    void *sched = sched_class->init();
    smp_set_sched(sched);

    TASK_PRINT("task init success\n");

    return true;
}