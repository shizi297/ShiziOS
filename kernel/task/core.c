/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "types.h" 
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
#include <stdatomic.h>

#define TASK_PRINT(fmt, ...) \
    printk("[TASK]" fmt, ##__VA_ARGS__)

#define TASK_PANIC(fmt, ...) \
    printp("[TASK] ERROR : " fmt, ##__VA_ARGS__)

#define INIT_TIME_SLICE_NS timecycle_msec_to_ns(20)

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

typedef struct {
    /*
     * 高32位为发送者cpuid
     * 低1位表示是否在执行迁移操作(活跃状态)
     */
    atomic_uint_least64_t state;    
} migration_info;

// 打包状态
#define MIG_STATE_PACK(sender, active) ((uint64_t)(sender) << 1) | ((active) ? 1 : 0)

// 获取发送者cpuid
#define MIG_STATE_SENDER(state) ((uint32_t)((state)) >> 1)

// 获取目标cpu的活跃状态
#define MIG_STATE_ACTIVE(state) ((state) & 1)

typedef struct {
    uint32_t cpu_count;
    migration_info infos[];
} migration_struct;

extern sched_func_t sched_func;

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
static per_cpu_task_state_struct *per_cpu_task_state_ptr = NULL;

INIT_BITMAP(id_bitmap, TASK_ID_MAX);
static spinlock_t id_lock = SPIN_LOCK_INIT;
static dynarr_t *id_map;
static migration_struct *migration_ptr = NULL; 

struct sched_class sched_class = {0};
struct sched_class *sched_class_ptr = &sched_class;

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
    if (id != TASK_ID_MAX) bitmap_set(id_bitmap, id);
    spin_unlock(&id_lock);

    return (id == TASK_ID_MAX) ? -1 : id;
}

/**
 * id释放
 * 
 * @param id 要释放的id
 */
static void id_free(id_t id) {
    if (id < 0 || id >= TASK_ID_MAX) return;

    spin_lock(&id_lock);
    bitmap_clear(id_bitmap, id);
    dynarr_set(id_map, id, NULL);
    spin_unlock(&id_lock);
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

// idle任务
static void task_idle(void *arg) {
    uint64_t count = 0;
    uint32_t cpu_id = get_logical_id();
    printk("[IDLE] CPU %d\n", cpu_id);
    while (1) {
        count++;
        if ((count % 500) == 0) {
            printk("[IDLE] CPU %d\n", cpu_id);
            printk("[IDLE] count : %lu\n", count);
        }
        cpu_halt();
    }
}

// 创建idle任务
static inline task_struct *task_create_idle(void) {
    return task_create_kernel_thread(task_idle, NULL);
}

// 时钟中断的回调
static void task_clock_event_handle(void) {
    smp_set_need_sched();
}

// 早期调度任务
static void task_boot_sched(void) {
    task_struct *task = sched_class.pick_next();
    if (!task) return;
    
    smp_set_task_current(task);

    irq_off();
    processor_boot_switch(task->thread);
}

// 尝试迁移任务到当前cpu,调用时需要关中断
static void task_try_migration(void) {
    uint32_t local = get_logical_id();

    // 获取当前所有cpu的运行任务数
    uint32_t max_cpu = migration_ptr->cpu_count;
    uint64_t task_count[max_cpu];
    smp_get_nr_running_all(task_count);

    uint32_t best = 0xFFFFFFFF, second = 0xFFFFFFFF,
             best_count = 0, second_count = 0;


    // 找到负载最大的两个cpu
    for (uint32_t i = 0;i < max_cpu;i++) {
        if (i == local) continue;
        uint32_t curr_count = task_count[i];

        // 任务太少，没有迁移的必要
        if (curr_count <= 2) continue;

        if (curr_count > best_count) {
            second = best;
            second_count = best_count;
            best = i;
            best_count = curr_count;
        } else if (curr_count > second_count) {
            second = i;
            second_count = curr_count;
        }
    }

    // 尝试迁移负债最高的cpu
    if (best != 0xFFFFFFFF) {
        uint64_t expected = MIG_STATE_PACK(0xFFFFFFFF, 0);
        uint64_t desired = MIG_STATE_PACK(local, 1);

        if (atomic_compare_exchange_strong(
                &migration_ptr->infos[best].state,
                &expected, desired)
        ) {
            smp_send_irq(best, IRQ_MIGRATION);
            while (1) {
                uint64_t cur = atomic_load(&migration_ptr->infos[best].state);

                // 当前选择的cpu已经与其他cpu配对
                if (!MIG_STATE_ACTIVE(cur)) break;

                cpu_pause();
            }

            // 将迁移的队列入就绪队列
            struct list_head *mig_list = smp_get_migration();
            while (!list_empty(mig_list)) {
                struct list_head *node = mig_list->next;
                struct task_struct *task = list_entry(node, struct task_struct, sched.list);
                list_del_init(node);
                sched_class_ptr->enqueue(task);
            }
            return;
        }
    }

    // 负债最高的cpu已经和别的cpu配对，尝试次选
    if (second != 0xFFFFFFFF) {
        uint64_t expected = MIG_STATE_PACK(0xFFFFFFFF, 0);
        uint64_t desired = MIG_STATE_PACK(local, 1);

        if (atomic_compare_exchange_strong(
                &migration_ptr->infos[second].state,
                &expected, desired)
        ) {
            smp_send_irq(second, IRQ_MIGRATION);
            while (1) {
                uint64_t cur = atomic_load(&migration_ptr->infos[second].state);

                // 当前选择的cpu已经与其他cpu配对
                if (!MIG_STATE_ACTIVE(cur)) break;

                cpu_pause();
            }

            // 将迁移的队列入就绪队列
            struct list_head *mig_list = smp_get_migration();
            while (!list_empty(mig_list)) {
                struct list_head *node = mig_list->next;
                struct task_struct *task = list_entry(node, struct task_struct, sched.list);
                list_del_init(node);
                sched_class_ptr->enqueue(task);
            }
            return;
        }
    }

    // 两个都失败，放弃本次迁移
}

// 迁移当前cpu的任务到目标cpu
static void task_do_migration() {
    uint32_t local = get_logical_id();

    // 读取状态，尝试设置为活跃
    uint64_t old = atomic_load(&migration_ptr->infos[local].state);
    if (!MIG_STATE_ACTIVE(old)) return; // 无效请求

    uint32_t sender = MIG_STATE_SENDER(old);

    uint32_t nr = smp_get_nr_running();
    if (nr <= 2) {
        // 队列太短，放弃迁移，清除标准让发送这重试
        uint64_t expected = old;
        uint64_t desired = MIG_STATE_PACK(0xFFFFFFFF, 0);
        atomic_compare_exchange_strong(
            &migration_ptr->infos[local].state,
            &expected, desired
        );
        return;    
    }

    // 取一半任务
    uint32_t take = nr >> 1;

    // 取出任务
    struct task_struct *tasks[take];
    for (uint32_t i = 0;i < take;i++) {
        tasks[i] = sched_class_ptr->dequeue_tail();
        TASK_PRINT("migrate pid %d from CPU %d to CPU %d\n", tasks[i]->pid, local, sender);
    }

    // 推送到发送方的迁移队列
    struct list_head *dest_mig = smp_get_cpu_migration(sender);
    for (uint32_t i = 0; i < take; i++) {
        list_add_tail(&tasks[i]->sched.list, dest_mig);
    }

    // 清除标志,表示任务以推送
    uint64_t expected = old;
    uint64_t desired = MIG_STATE_PACK(0xFFFFFFFF, 0);
    atomic_compare_exchange_strong(
        &migration_ptr->infos[local].state,
        &expected, desired
    );
}

// 任务迁移结构初始化
static inline bool task_migration_init(void) {
    uint32_t max_cpu = bootboot->numcores;

    uint32_t migration_size = sizeof(migration_struct) + max_cpu * sizeof(migration_info);

    migration_ptr = kheap_alloc(migration_size);
    if (!migration_ptr) return false;

    for (uint32_t i = 0; i < max_cpu; i++) {
        atomic_init(&migration_ptr->infos[i].state, MIG_STATE_PACK(0xFFFFFFFF, 0));
    }

    migration_ptr->cpu_count = max_cpu;

    smp_irq_register_handler(IRQ_MIGRATION, (uint64_t)task_do_migration);

    return true;
}

// 更新当前任务的时间
void task_add_current_tick(uint64_t tick) {
    task_struct *current = smp_get_task_current();
    if (!current || !sched_class_ptr || !sched_class_ptr->update_tick) return;
    sched_class_ptr->update_tick(current, tick);
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

    // 初始化调度器私有数据
    if (sched_class_ptr && sched_class_ptr->sched_init) sched_class_ptr->sched_init(new_task);

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
    if (sched_class_ptr && sched_class_ptr->enqueue) sched_class_ptr->enqueue(new_task);

    // 添加到映射表
    dynarr_set(id_map, new_task->pid, &new_task);

    return new_task;

fail:
    // 错误处理
    if (new_task->group_leader && new_task->group_leader != new_task)
        thread_group_remove(new_task->group_leader, new_task);

    if (new_task->father)
        task_remove_child(new_task->father, new_task);

    if (new_task->signal) signal_destroy(new_task->signal);
    if (!(flags & TASK_VM) && new_task->as) vheap_destroy_as(new_task->as);

    if (tgid_allocated && new_task->tgid != -1) id_free(new_task->tgid);
    if (new_task->pid != -1) id_free(new_task->pid);
    if (new_task->stack) kheap_free(new_task->stack);

    kheap_free(new_task);
    return NULL;
}

/**
 * 创建内核线程
 * 
 * @param func 线程入口函数
 * @param arg  传递给线程的参数
 * * 
 * @return 成功：任务结构体指针
 * @return 失败：NULL
 */
task_struct *task_create_kernel_thread(void (*func)(void *), void *arg) {
    task_struct *task = NULL;
    void *stack = NULL;
    struct thread_struct *thread = NULL;
    id_t pid = -1;

    // 分配任务结构体
    task = (task_struct *)kheap_alloc(sizeof(task_struct));
    if (!task) goto fail;
    spinlock_init(&task->list_lock);

    // 分配内核栈
    stack = kheap_alloc(KERNEL_START_SIZE);
    if (!stack) goto fail;
    task->stack = stack;

    // 分配线程上下文
    thread = thread_struct_create();
    if (!thread) goto fail;
    task->thread = thread;

    // 分配pid
    pid = id_alloc();
    if (pid == -1) goto fail;
    task->pid = pid;

    // 设置 tgid
    task->tgid = pid;

    // 设置地址空间（内核线程共享内核地址空间）
    task->as = vheap_get_kernel_as();

    // 获取内核页表物理地址
    uintptr_t kernel_pgd = vheap_get_kernel_pgd();

    // 初始化线程上下文
    void *stack_top = (void *)((uintptr_t)stack + KERNEL_START_SIZE);
    thread_struct_to_kernel_init(thread, stack_top, (void *)kernel_pgd, func, arg);

    // 初始化调度器私有数据
    if (sched_class_ptr && sched_class_ptr->sched_init) sched_class_ptr->sched_init(task);

    // 设置任务状态
    task->state = TASK_RUNNING;
    task->sigchld = false;

    // 初始化链表
    INIT_LIST_HEAD(&task->zombie);
    INIT_LIST_HEAD(&task->sibling);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sleep);
    INIT_LIST_HEAD(&task->thread_group);

    // 设置父子关系（无父进程）
    task->father = NULL;

    // 设置线程组关系（自己为主线程）
    task->group_leader = task;

    // 加入调度队列
    if (sched_class_ptr && sched_class_ptr->enqueue) sched_class_ptr->enqueue(task);

    // 添加到 id 映射表
    dynarr_set(id_map, pid, &task);

    return task;

fail:
    // 错误处理
    if (thread) thread_struct_destroy(thread);
    if (stack) kheap_free(stack);
    if (task) {
        if (pid != -1) id_free(pid);
        kheap_free(task);
    }
    return NULL;
}

// 任务退出
void task_exit(void) {
    // TODO
}

// 设置下一次中断
void task_set_next_timer(void) {
    sched_class_ptr->set_next_timer(smp_get_task_current());
}

// 重新调度任务
void task_sched(void) {
    uint64_t flags = get_cpu_flags();
    irq_off();

    struct task_struct *prev = smp_get_task_current();
    if (!prev) {
        goto out;
    }

    if (prev->state == TASK_RUNNING && prev != smp_get_idle()) {
        sched_class_ptr->enqueue(prev);
    }

    struct task_struct *next = sched_class_ptr->pick_next();
    if (!next) {
        goto out;
    }

    // 如果选择的任务是idle任务，尝试迁移任务
    if (next == smp_get_idle()) {
        task_try_migration();
    }

    // 调度后相同，不需要切换和保存上下文，直接设置中断后返回
    if (prev == next) {
        prev->sched.exec_ns = 0;
        task_set_next_timer();
        goto out;
    }

    fpu_save(prev->thread);
    smp_set_task_current(next);
    fpu_restore(next->thread);

    // 设置下一次中断
    task_set_next_timer();

    switch_to(prev->thread, next->thread);

out:
    write_cpu_flags(flags);
}

// 任务管理数据初始化
bool task_data_init(void) {
    uint32_t max_cpu = bootboot->numcores;
    uint64_t per_cpu_task_stopped_size = sizeof(uint32_t) + (sizeof(task_state_struct) * max_cpu); 
    per_cpu_task_state_ptr = kheap_alloc(per_cpu_task_stopped_size);
    if (!per_cpu_task_state_ptr) return false;

    // 初始化链表节点
    for (int i = 0; i < max_cpu; i++) INIT_LIST_HEAD(&per_cpu_task_state_ptr->state[i].stopped);

    // 创建id映射，用于找到对应id的task，最大容量为TASK_ID_MAX
    id_map = dynarr_create(sizeof(struct task_struct *), TASK_ID_MAX);
    if (!id_map) return false;

    if (!task_migration_init()) return false;

    sched_func();

    TASK_PRINT("task data init success\n");

    return true;
}

// 任务管理初始化
void task_init(void) {
    task_struct *idle = task_create_idle();

    if (!idle) goto error; 
    smp_set_idle(idle);

    if (sched_class_ptr && sched_class_ptr->init) sched_class_ptr->init();

    clockevent_handle_t clockevent = clockevent_get(NULL);
    if (!clockevent) goto error;

    if (task_test) task_test();

    smp_set_clockevent(clockevent);
    clockevent_set_handler(clockevent, task_clock_event_handle);
    clockevent_set_mode(clockevent, CLOCKEVENT_MODE_ONESHOT);

    TASK_PRINT("task init success\n");

    task_set_next_timer();
    task_boot_sched();

    // 不应该返回，如果返回说明代码错误
    error:
        TASK_PANIC("system error\n");
}