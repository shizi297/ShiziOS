/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "types.h" 
#include "files.h"
#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include <task.h>
#include <asm/smp.h>
#include <bootboot.h>
#include <heap.h>
#include <signal.h>
#include <timecycle.h>
#include <asm/processor.h>
#include <config.h>
#include <bitmap.h>
#include <libtree.h>
#include <dynarr.h>
#include <kio.h>
#include <stdatomic.h>
#include <initcall.h>
#include <asm/mm_addr.h>
#include <wait.h>
#include <exec.h>

#define TASK_PRINT(fmt, ...) \
    printk("[TASK]" fmt, ##__VA_ARGS__)

#define TASK_PANIC(fmt, ...) \
    printp("[TASK] ERROR : " fmt, ##__VA_ARGS__)

#define INIT_TIME_SLICE_NS timecycle_msec_to_ns(20)

// worker 线程数量
#define WORKER_THREAD_COUNT 2

// 打包状态
#define MIG_STATE_PACK(sender, active) ((uint64_t)(sender) << 1) | ((active) ? 1 : 0)

// 获取发送者cpuid
#define MIG_STATE_SENDER(state) ((uint32_t)((state)) >> 1)

// 获取目标cpu的活跃状态
#define MIG_STATE_ACTIVE(state) ((state) & 1)

// 退出状态打包（正常退出）
#define EXIT_STATUS_PACK(code)          (((code) & 0xFF) << 8)

// 退出状态打包（被信号杀死）
#define EXIT_STATUS_TERM(sig, core)     (((sig) & 0x7F) | ((core) ? (1 << 7) : 0))

// 获取退出值
#define EXIT_STATUS_CODE(status)        (((status) >> 8) & 0xFF)

// 获取终止信号
#define EXIT_STATUS_SIG(status)         ((status) & 0x7F)

// 是否产生 core dump
#define EXIT_STATUS_COREDUMP(status)    ((status) & (1 << 7))

// 预留 PID
#define TASK_KERNEL_ID  0   // 内核本身
#define TASK_INIT_ID    1   // init 进程

extern void ret_from_kernel_thread(void);

// 通用锁队列结构
struct lock_queue {
    struct list_head head;
    spinlock_t lock;
};

/*
 * 用于存储对应状态的任务
 * 运行/就绪调度器负责，这里不放
 * 这里的僵尸队列存放内核线程的僵尸任务和子线程待回收的任务
 */
static struct lock_queue stopped_queue;
static struct lock_queue zombie_queue;
static struct lock_queue sleep_queue;

// 工作任务队列（存放工作项）
struct work_item {
    void (*func)(void *data);
    void *data;
    struct list_head node;
};

static struct lock_queue work_queue;

// worker线程池，负责处理工作任务队列中的工作项
static struct {
    wait_queue_head_t waitq;    // 工作线程等待队列
    spinlock_t lock;    // 保护所有字段
    int idle_workers;   // 空闲的 worker 线程数量
    int active_workers; // 活跃的 worker 线程数量
    int pending_work;   // 待处理的工作项数量
    bool shutdown;  // 是否正在关闭线程池
} worker_pool = {0};

typedef struct {
    /*
     * 高32位为发送者cpuid
     * 低1位表示是否在执行迁移操作(活跃状态)
     */
    atomic_uint_least64_t state;    
} migration_info;

typedef struct {
    uint32_t cpu_count;
    migration_info infos[];
} migration_struct;

extern sched_func_t sched_func;

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

INIT_BITMAP(id_bitmap, TASK_ID_MAX);
static spinlock_t id_lock = SPIN_LOCK_INIT;
static dynarr_t *id_map;
static migration_struct *migration_ptr = NULL; 

struct sched_class sched_class = {0};
struct sched_class *sched_class_ptr = &sched_class;

// 任务系统初始化完成标志
static bool task_init_flag = false;

/**
 * id分配，可以用于任务管理的任何id
 * 
 * @return 成功：id
 * @return 失败：-1
 */
__attribute__((optnone))
static pid_t id_alloc(void) {
    pid_t id;

    spin_lock(&id_lock);
    id = (pid_t)bitmap_find(id_bitmap, TASK_ID_MAX, 0, 0);
    if (id != TASK_ID_MAX) bitmap_set(id_bitmap, id);
    spin_unlock(&id_lock);

    return (id == TASK_ID_MAX) ? -1 : id;
}

/**
 * id释放
 * 
 * @param id 要释放的id
 */
static void id_free(pid_t id) {
    if (id < 0 || id >= TASK_ID_MAX) return;

    spin_lock(&id_lock);
    bitmap_clear(id_bitmap, id);
    dynarr_set(id_map, id, NULL);
    spin_unlock(&id_lock);
}

/*
 * 获取 init 任务
 *
 * @return init 任务指针
 */
static struct task_struct *id_get_init(void) {
    return (struct task_struct *)dynarr_get(id_map, TASK_INIT_ID);
}

// 初始化锁队列
static void lock_queue_init(struct lock_queue *lq) {
    INIT_LIST_HEAD(&lq->head);
    spinlock_init(&lq->lock);
}

// 将节点加入锁队列尾部
static void lock_queue_add_tail(struct lock_queue *lq, struct list_head *node) {
    spin_lock(&lq->lock);
    list_add_tail(node, &lq->head);
    spin_unlock(&lq->lock);
}

// 从锁队列头部取出节点（不删除，仅获取）
static struct list_head *lock_queue_peek(struct lock_queue *lq) {
    spin_lock(&lq->lock);
    struct list_head *node = NULL;
    if (!list_empty(&lq->head))
        node = lq->head.next;
    spin_unlock(&lq->lock);
    return node;
}

// 从锁队列头部取出并删除节点
static struct list_head *lock_queue_pop(struct lock_queue *lq) {
    spin_lock(&lq->lock);
    struct list_head *node = NULL;
    if (!list_empty(&lq->head)) {
        node = lq->head.next;
        list_del_init(node);
    }
    spin_unlock(&lq->lock);
    return node;
}

// 从锁队列中删除指定节点
static void lock_queue_del(struct lock_queue *lq, struct list_head *node) {
    spin_lock(&lq->lock);
    list_del_init(node);
    spin_unlock(&lq->lock);
}

// 回收任务资源（仅供父进程 wait 或 worker 回收时调用）
static void task_release(struct task_struct *task) {
    if (task->stack) kheap_free(task->stack);
    if (task->thread) thread_struct_destroy(task->thread);
    if (task->as) vheap_destroy_as(task->as);
    if (task->signal) signal_destroy(task->signal);
    if (task->fs.root) vfs_path_put(task->fs.root);
    if (task->fs.pwd) vfs_path_put(task->fs.pwd);
}

// 用于 woeker 线程自适应唤醒的函数，计算 pending 工作项数量对应的期望活跃 worker 数量
static inline int worker_calc_expected(uint32_t x) {
    if (!x)
        return -1;   // 不应该调用这个函数，返回-1表示错误

    if (x > WORKER_THREAD_COUNT) 
        return WORKER_THREAD_COUNT; // 超过线程池容量，返回最大值

    if (x == 1) 
        return 1;   // 确保 pending 工作项为1时至少唤醒一个线程
        
    return 31 - __builtin_clz((unsigned int)x);
}

// 将工作项加入工作任务队列
static void work_enqueue(struct work_item *item) {
    lock_queue_add_tail(&work_queue, &item->node);
}

// 从工作任务队列中取出一个工作项
static struct work_item *work_dequeue(void) {
    struct list_head *node = lock_queue_pop(&work_queue);
    if (node)
        return list_entry(node, struct work_item, node);
    return NULL;
}

// 遍历全局 zombie 队列，释放所有任务资源
static void zombie_reclaim(void *unused) {
    struct list_head *head = &zombie_queue.head;

    spin_lock(&zombie_queue.lock);
    while (!list_empty(head)) {
        struct list_head *node = head->next;
        struct task_struct *task = list_entry(node, struct task_struct, zombie);
        list_del_init(node);
        spin_unlock(&zombie_queue.lock);

        task_release(task);
        id_free(task->pid);
        kheap_free(task);

        spin_lock(&zombie_queue.lock);
    }
    spin_unlock(&zombie_queue.lock);
}

// 将任务加入全局 zombie 队列并提交回收工作
static void zombie_enqueue(struct task_struct *task) {
    lock_queue_add_tail(&zombie_queue, &task->zombie);

    // 提交僵尸回收任务
    task_submit_work(zombie_reclaim, NULL);
}

// 通用 worker 线程
static void task_worker(void *arg) {
    while (1) {
        // 进入空闲状态
        spin_lock(&worker_pool.lock);

        worker_pool.idle_workers++;
        if (worker_pool.shutdown && worker_pool.pending_work == 0) {
            worker_pool.idle_workers--;
            spin_unlock(&worker_pool.lock);
            break;
        }

        spin_unlock(&worker_pool.lock);

        // 等待直到有工作项或 shutdown
        waitqueue_event(
            &worker_pool.waitq, 
            worker_pool.pending_work > 0 || worker_pool.shutdown
        );

        // 离开空闲，变为活跃
        spin_lock(&worker_pool.lock);
        worker_pool.idle_workers--;
        worker_pool.active_workers++;
        spin_unlock(&worker_pool.lock);

        // 处理所有待处理的工作项
        struct work_item *item;
        while ((item = work_dequeue()) != NULL) {
            spin_lock(&worker_pool.lock);
            worker_pool.pending_work--;
            spin_unlock(&worker_pool.lock);
            item->func(item->data);
            kheap_free(item);
        }

        // 变为空闲（循环顶部会再次增加 idle_workers，所以这里只减少 active_workers）
        spin_lock(&worker_pool.lock);
        worker_pool.active_workers--;
        spin_unlock(&worker_pool.lock);
    }
}

// 初始化 worker 线程池
static inline void task_worker_init(void) {
    waitqueue_head_init(&worker_pool.waitq);
    spinlock_init(&worker_pool.lock);
    worker_pool.idle_workers = 0;
    worker_pool.active_workers = 0;
    worker_pool.pending_work = 0;
    worker_pool.shutdown = false;

    lock_queue_init(&work_queue);

    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        task_struct *worker = task_create_kernel_thread(task_worker, NULL);
        // 不把worker加入worker线程池，调度到他时会自动加入
    }
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
        cpu_halt();
    }
}

// 创建idle任务
static inline task_struct *task_create_idle(void) {
    return task_create_kernel_thread(task_idle, NULL);
}

// 时钟中断的回调
static void task_clock_event_handle(void *data) {
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
                &expected, desired)) {
            smp_send_irq(best, IRQ_MIGRATION);
            while (1) {
                uint64_t cur = atomic_load(&migration_ptr->infos[best].state);
                if (!MIG_STATE_ACTIVE(cur)) break;
                cpu_pause();
            }
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
                &expected, desired)) {
            smp_send_irq(second, IRQ_MIGRATION);
            while (1) {
                uint64_t cur = atomic_load(&migration_ptr->infos[second].state);
                if (!MIG_STATE_ACTIVE(cur)) break;
                cpu_pause();
            }
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
        // 队列太短，放弃迁移，清除标准让发送者重试
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
    for (uint32_t i = 0; i < take; i++) {
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
        &expected, desired);
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

/*
 * 创建新任务
 *
 * @param attrs 任务属性
 * @param size 结构体大小
 *
 * @return 任务控制块指针
 */
__ktype(struct task_struct *)
kptr task_create_new(struct task_attrs *attrs, size_t size) {
    struct task_struct *parent = smp_get_task_current();
    struct task_struct *task = NULL;
    struct thread_struct *thread = NULL;
    void *kernel_stack = NULL;
    as_t *as = NULL;
    struct files_descriptor *files = NULL;
    struct signal_struct *sig = NULL;
    pid_t pid = -1, tgid = -1;
    uintptr_t entry = 0, stack_top = 0, pgd_phys = 0;
    int err = 0;
    uint32_t flags = 0;

    // 参数校验：结构体大小必须有效，标志位必须合法
    if (!attrs || size < sizeof(struct task_attrs)) {
        err = -EINVAL;
        goto err_return;
    }

    flags = attrs->flags;
    if (flags & ~(TASK_IS_THREAD | TASK_WAIT_PARENT | TASK_WAKE_ON_EXIT)) {
        err = -EINVAL;
        goto err_return;
    }

    // 根据模式检查必需参数
    if (flags & TASK_IS_THREAD) {
        if (!attrs->thread.entry_point || !attrs->thread.stack_base) {
            err = -EINVAL;
            goto err_return;
        }
    } else {
        if (!attrs->process.exec_path) {
            err = -EINVAL;
            goto err_return;
        }
    }

    // 分配任务控制块
    task = (struct task_struct *)kheap_alloc(sizeof(*task));
    if (!task) {
        err = -ENOMEM;
        goto err_return;
    }
    memset(task, 0, sizeof(*task));
    spinlock_init(&task->list_lock);

    // 分配内核栈
    kernel_stack = kheap_alloc(KERNEL_START_SIZE);
    if (!kernel_stack) {
        err = -ENOMEM;
        goto err_task;
    }
    task->stack = kernel_stack;

    // 分配线程上下文结构体
    thread = thread_struct_create();
    if (!thread) {
        err = -ENOMEM;
        goto err_stack;
    }
    task->thread = thread;

    // 分配进程 ID
    pid = id_alloc();
    if (pid < 0) {
        err = -ENOMEM;
        goto err_thread;
    }
    task->pid = pid;

    // 确定线程组 ID
    if (flags & TASK_IS_THREAD) {
        tgid = parent->tgid;
    } else {
        tgid = pid;
    }
    task->tgid = tgid;

    // 从父任务继承用户 ID
    task->user_id.uid = parent->user_id.uid;
    task->user_id.gid = parent->user_id.gid;

    // 从父任务继承文件系统上下文
    if (parent->fs.root) {
        task->fs.root = parent->fs.root;
        vfs_path_get(task->fs.root);
    }
    if (parent->fs.pwd) {
        task->fs.pwd = parent->fs.pwd;
        vfs_path_get(task->fs.pwd);
    }

    // 处理地址空间
    if (flags & TASK_IS_THREAD) {
        // 线程：共享父任务的地址空间
        as = parent->as;
        if (as) vheap_as_add_ref(as);
        task->as = as;
        pgd_phys = vheap_get_as_pgd(as);
    } else {
        // 进程：创建新的独立地址空间
        as = vheap_create_as();
        if (!as) {
            err = -ENOMEM;
            goto err_pid;
        }
        task->as = as;
        pgd_phys = vheap_get_as_pgd(as);

        // 分配用户栈（默认大小 USER_STACK_SIZE）
        uintptr_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
        void *mapped = vheap_alloc(
            as,
            (void *)stack_bottom,
            USER_STACK_SIZE,
            VM_READ | VM_WRITE,
            0,
            false
        );
        if (!mapped) {
            err = -ENOMEM;
            goto err_as;
        }

        // 进程模式：加载可执行文件，获取用户态入口和栈顶地址
        uintptr_t out_stack_top;
        kuptr entry_res = exec_load(
            as,
            attrs->process.exec_path,
            parent->fs.pwd,
            attrs->process.argv,
            attrs->process.envp,
            USER_STACK_TOP,
            &out_stack_top
        );
        K_ERR_LABEL_AND_SAVE(entry_res, err_as, err);
        entry = (uintptr_t)entry_res.val;
        stack_top = out_stack_top;
    }

    // 初始化 thread_struct，供调度器首次切换时使用
    void *kernel_stack_top = (void *)((uintptr_t)kernel_stack + KERNEL_START_SIZE);
    thread_struct_to_user_init(thread, (void *)pgd_phys, kernel_stack_top);

    // 在内核栈上布局 pt_regs，填充用户态入口信息
    struct pt_regs *regs = processor_set_user_stack(thread, entry, stack_top);
    task->regs = regs;

    // 处理文件描述符表
    if (flags & TASK_IS_THREAD) {
        // 线程：共享父任务的文件描述符表
        files = parent->files;
        if (files) task_files_get(files);
        task->files = files;
    } else {
        // 进程：创建新的文件描述符表
        files = task_files_alloc();
        if (!files) {
            err = -ENOMEM;
            goto err_as;
        }
        task->files = files;

        // 继承用户指定的文件描述符
        if (attrs->fd_count > 0 && attrs->inherit_fds) {
            err = task_files_copy_list(
                files,
                parent->files,
                attrs->inherit_fds,
                attrs->fd_count
            );
            if (err < 0) goto err_files;
        }
    }

    // 处理信号
    if (flags & TASK_IS_THREAD) {
        // 线程：共享父任务的信号处理器表，但拥有独立的信号掩码
        if (!signal_copy(parent->signal, &sig, true)) {
            err = -ENOMEM;
            goto err_files;
        }
        task->signal = sig;
    } else {
        // 进程：复制父任务的信号处理器表
        if (!signal_copy(parent->signal, &sig, false)) {
            err = -ENOMEM;
            goto err_files;
        }
        task->signal = sig;
    }

    // 初始化调度器私有数据
    if (sched_class_ptr && sched_class_ptr->sched_init)
        sched_class_ptr->sched_init(task);

    // 设置任务状态为可运行
    task->state = TASK_RUNNING;

    // 根据 TASK_WAKE_ON_EXIT 设置 sigchld，决定退出时是否发送 SIGCHLD
    task->sigchld = (flags & TASK_WAKE_ON_EXIT) ? true : false;

    // 初始化链表头
    INIT_LIST_HEAD(&task->zombie);
    INIT_LIST_HEAD(&task->sibling);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sleep);
    INIT_LIST_HEAD(&task->thread_group);

    // 建立父子关系
    task->father = parent;
    task_add_child(parent, task);

    // 建立线程组关系
    if (flags & TASK_IS_THREAD) {
        task->group_leader = parent->group_leader;
        thread_group_add(task->group_leader, task);
    } else {
        task->group_leader = task;
    }

    // 将新任务加入调度器的运行队列
    if (sched_class_ptr && sched_class_ptr->enqueue)
        sched_class_ptr->enqueue(task);

    // 将任务指针存入 ID 映射表，方便通过 PID 查找
    dynarr_set(id_map, pid, &task);

    // 如果父进程要求等待子进程初始化完成，则父进程挂起
    if (flags & TASK_WAIT_PARENT) {
        task_sleep(true);   // 父进程睡眠
    }

    return (kptr)K_PTR(task);

err_signal:
    signal_destroy(sig);
err_files:
    task_files_put(files);
err_as:
    vheap_destroy_as(as);
err_pid:
    id_free(pid);
    if (tgid != pid) id_free(tgid);
err_thread:
    thread_struct_destroy(thread);
err_stack:
    kheap_free(kernel_stack);
err_task:
    kheap_free(task);
err_return:
    return (kptr)K_ERR(err);
}

/**
 * 创建内核线程
 * 
 * @param func 线程入口函数
 * @param arg  传递给线程的参数
 * 
 * @return 成功：任务结构体指针
 * @return 失败：NULL
 */
task_struct *task_create_kernel_thread(void (*func)(void *), void *arg) {
    task_struct *task = NULL;
    void *stack = NULL;
    struct thread_struct *thread = NULL;
    pid_t pid = -1;

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

    // 内核线程的用户 id 为 root
    task->user_id.uid = 0;
    task->user_id.gid = 0;

    // 设置文件系统上下文
    struct path *root_path = vfs_get_root_path();
    task->fs.pwd = root_path;
    task->fs.root = root_path;

    // 设置地址空间（内核线程共享内核地址空间）
    task->as = vheap_get_kernel_as();
    vheap_as_add_ref(task->as);

    // 获取内核页表物理地址
    uintptr_t kernel_pgd = vheap_get_kernel_pgd();

    // 计算原始栈顶
    void *stack_top = (void *)((uintptr_t)stack + KERNEL_START_SIZE);

    // 在栈顶预留一个位置存放返回地址，并写入 ret_from_kernel_thread
    uint64_t *ret_slot = (uint64_t *)stack_top - 1;
    *ret_slot = (uint64_t)ret_from_kernel_thread;

    // 新的栈顶（向下移动一个指针）
    void *new_stack_top = (void *)ret_slot;

    // 初始化线程上下文
    thread_struct_to_kernel_init(thread, new_stack_top, (void *)kernel_pgd, func, arg);

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
ku64 task_wait(pid_t pid, exit_status_t *status, wait_options_t options) {
    struct task_struct *current = smp_get_task_current();
    struct task_struct *child = NULL;
    struct list_head *pos, *n;
    int found = 0;
    int ret_pid = 0;

    if (!current || !status) {
        return (ku64)K_ERR(-EINVAL);
    }

    // 检查选项
    if (options & WCONTINUED) {
        return (ku64)K_ERR(-ENOSYS);
    }

    // 过滤 PID
    if (pid == 0) {
        // 等待同进程组的子进程（暂不支持进程组）
        return (ku64)K_ERR(-ENOSYS);
    } else if (pid < -1) {
        // 等待指定进程组的子进程（暂不支持进程组）
        return (ku64)K_ERR(-ENOSYS);
    }

    while (1) {
        // 遍历当前进程的僵尸子进程链表
        spin_lock(&current->list_lock);
        list_for_each_safe(pos, n, &current->zombie) {
            child = list_entry(pos, struct task_struct, zombie);
            // 过滤 PID
            if (pid != -1 && child->pid != pid) {
                continue;
            }
            // 找到匹配的僵尸子进程
            list_del_init(pos);
            spin_unlock(&current->list_lock);

            *status = child->exit_status;
            ret_pid = child->pid;

            // 回收资源
            task_release(child);
            id_free(child->pid);
            kheap_free(child);

            return (ku64)K_OK(ret_pid);
        }
        spin_unlock(&current->list_lock);

        // 如果设置了 WUNTRACED，查找停止的子进程
        if (options & WUNTRACED) {
            spin_lock(&current->list_lock);
            list_for_each_safe(pos, n, &current->children) {
                child = list_entry(pos, struct task_struct, sibling);
                // 过滤 PID
                if (pid != -1 && child->pid != pid) {
                    continue;
                }
                if (child->state == TASK_STOPPED) {
                    spin_unlock(&current->list_lock);
                    *status = child->exit_status;
                    return (ku64)K_OK(child->pid);
                }
            }
            spin_unlock(&current->list_lock);
        }

        // 如果设置了 WNOHANG，没有符合条件的子进程则返回 0
        if (options & WNOHANG) {
            return (ku64)K_OK(0);
        }

        // 没有找到符合条件的子进程，挂起父进程等待
        task_sleep(true);
        task_sched();
    }
}

// 用于需要内核线程才能测试的测试组件
static void kthread_test(void *arg) {
    initcall(kthreadtest, 0);
}

// 设置下一次中断
void task_set_next_timer(void) {
    sched_class_ptr->set_next_timer(smp_get_task_current());
}

// 让当前任务睡眠
task_struct *task_sleep(bool interruptible) {
    task_struct *current = smp_get_task_current();
    if (!current) return NULL;

    current->state = interruptible ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
    lock_queue_add_tail(&sleep_queue, &current->sleep);

    smp_set_need_sched();

    return current;
}

// 唤醒睡眠的任务
void task_wakeup(task_struct *task) {
    if (!task) return;

    if (task->state != TASK_INTERRUPTIBLE && task->state != TASK_UNINTERRUPTIBLE) return;

    lock_queue_del(&sleep_queue, &task->sleep);
    task->state = TASK_RUNNING;
    sched_class_ptr->enqueue(task);
}

// 重新调度任务
void task_sched(void) {
    uint64_t flags = get_cpu_flags();
    irq_off();

    struct task_struct *prev = smp_get_task_current();
    if (!prev)
        goto out;

    if (prev->state == TASK_RUNNING && prev != smp_get_idle())
        sched_class_ptr->enqueue(prev);

    struct task_struct *next = sched_class_ptr->pick_next();
    if (!next)
        goto out;

    // 如果选择的任务是idle任务，尝试迁移任务
    if (next == smp_get_idle())
        task_try_migration();

    // 调度后相同，不需要切换和保存上下文，直接设置中断后返回
    if (prev == next) {
        prev->sched.exec_ns = 0;
        task_set_next_timer();
        goto out;
    }

    fpu_save(prev->thread);
    smp_arch_update_state(next->thread);
    smp_set_task_current(next);
    fpu_restore(next->thread);

    // 设置下一次中断
    task_set_next_timer();
    
    smp_set_as(next->as);

    switch_to(prev->thread, next->thread);

out:
    write_cpu_flags(flags);
}

/*
 * 向指定任务发送信号
 *
 * @param task 目标任务
 * @param sig 要发送的信号
 */
void task_send_signal(struct task_struct *task, int sig) {
    if (!task || !task->signal) return;
    signal_send(task, task->signal, sig, true, false);
    if (task->state == TASK_INTERRUPTIBLE) {
        task_wakeup(task);
    }
}

// 任务退出
__attribute__((noreturn))
void task_exit(int code, bool signaled) {
    task_struct *task_current = smp_get_task_current();
    if (!task_current) TASK_PANIC("no current task\n");

    // 将当前任务从就绪队列中移除
    sched_class_ptr->dequeue(task_current);

    // 立即释放文件描述符表
    if (task_current->files) {
        task_files_put(task_current->files);
        task_current->files = NULL;
    }

    // 将当前任务标记为僵尸，防止被重新调度
    task_current->state = TASK_ZOMBIE;

    // 存储退出状态
    if (signaled) {
        task_current->exit_status = EXIT_STATUS_TERM(code, false);
    } else {
        task_current->exit_status = EXIT_STATUS_PACK(code);
    }

    // 内核线程：直接进入全局 zombie 队列，由 zombie_reclaim 异步回收
    if (task_current->father == NULL) {
        zombie_enqueue(task_current);
        task_sched();
        TASK_PANIC("task exit failed\n");
    }

    if (task_current->pid != task_current->tgid) {
        // 普通用户线程：直接进入全局 zombie 队列，由 zombie_reclaim 异步回收
        zombie_enqueue(task_current);
    } else {
        // 主线程退出，处理子进程和子线程

        struct task_struct *init = id_get_init();
        if (!init) {
            TASK_PANIC("init task not found\n");
        }
        struct list_head *pos, *n;

        // 处理已僵尸的子进程：移到 init->zombie
        list_for_each_safe(pos, n, &task_current->zombie) {
            struct task_struct *child = list_entry(pos, struct task_struct, zombie);
            list_del_init(&child->zombie);
            child->father = init;
            spin_lock(&init->list_lock);
            list_add_tail(&child->zombie, &init->zombie);
            spin_unlock(&init->list_lock);
        }

        // 处理正在运行的子进程：挂到 init->children
        list_for_each_safe(pos, n, &task_current->children) {
            struct task_struct *child = list_entry(pos, struct task_struct, sibling);
            list_del_init(&child->sibling);
            child->father = init;
            task_add_child(init, child);
            if (child->state == TASK_ZOMBIE) {
                spin_lock(&init->list_lock);
                list_add_tail(&child->zombie, &init->zombie);
                spin_unlock(&init->list_lock);
            }
        }

        // 处理线程组中的其他线程：设置退出标志并唤醒
        list_for_each_safe(pos, n, &task_current->thread_group) {
            struct task_struct *thread = list_entry(pos, struct task_struct, thread_group);
            if (thread == task_current) continue;
            list_del_init(&thread->thread_group);
            if (thread->state == TASK_INTERRUPTIBLE) {
                task_wakeup(thread);
            }
        }

        // 自身挂到父进程的 zombie 链表
        spin_lock(&task_current->father->list_lock);
        list_add_tail(&task_current->zombie, &task_current->father->zombie);
        spin_unlock(&task_current->father->list_lock);
        if (task_current->father->sigchld) {
            task_send_signal(task_current->father, SIGCHLD);
        }
    }

    task_sched();

    // 不应该返回，如果返回说明代码错误
    TASK_PANIC("task exit failed\n");
}

// 提交一个工作任务
void task_submit_work(void (*func)(void *), void *data) {
    struct work_item *item = kheap_alloc(sizeof(*item));
    if (!item) TASK_PANIC("out of memory");
    item->func = func;
    item->data = data;
    INIT_LIST_HEAD(&item->node);

    // 加入工作队列
    work_enqueue(item);

    spin_lock(&worker_pool.lock);
    worker_pool.pending_work++;
    int pending = worker_pool.pending_work;
    int active = worker_pool.active_workers;
    int idle = worker_pool.idle_workers;
    spin_unlock(&worker_pool.lock);

    // 计算期望活跃 worker 数量
    int expected = worker_calc_expected(pending);
    if (expected < 0) {
        // 不应该发生
        return;
    }

    int need = expected - active;
    if (need > idle) need = idle;
    if (need <= 0) return;

    for (int i = 0; i < need; i++) {
        waitqueue_wake_up(&worker_pool.waitq);
    }
}

// 获取当前任务的文件系统上下文（使用后需要尽快增加path引用和拷贝）
void task_get_current_fs(struct path **root, struct path **pwd) {
    // 如果任务子系统还没有初始化，返回空指针
    if (!task_init_flag) {
        if (root) *root = NULL;
        if (pwd) *pwd = NULL;
        return;
    }

    task_struct *current = smp_get_task_current();

    if (root) 
        *root = current->fs.root;

    if (pwd) 
        *pwd = current->fs.pwd;
}

// 获取当前任务的gid和uid
void task_get_current_ugid(uid_t *uid, gid_t *gid) {
    // 如果任务管理还没有初始化，返回 root 权限
    if (!task_init_flag) {
        if (uid) *uid = 0;
        if (gid) *gid = 0;
        return;
    }

    task_struct *current = smp_get_task_current();
    
    if (uid) *uid = current->user_id.uid;
    if (gid) *gid = current->user_id.gid;
}

// 获取当前任务的线程id
pid_t task_get_current_thread_id(void) {
    task_struct *curr = smp_get_task_current();
    return curr->pid;
}

// 获取任务的线程ID
pid_t task_get_pid(struct task_struct *task) {
    return task ? task->pid : -1;
}
// 获取任务的进程ID
pid_t task_get_tgid(struct task_struct *task) {
    return task ? task->tgid : -1;
}

// 任务管理数据初始化
bool task_data_init(void) {
    lock_queue_init(&stopped_queue);
    lock_queue_init(&zombie_queue);
    lock_queue_init(&sleep_queue);

    // 创建id映射，用于找到对应id的task，最大容量为TASK_ID_MAX
    id_map = dynarr_create(sizeof(struct task_struct *), TASK_ID_MAX);
    if (!id_map) return false;

    // 预留 PID 用于特殊任务
    dynarr_set(id_map, TASK_KERNEL_ID, NULL);
    dynarr_set(id_map, TASK_INIT_ID, NULL);
    bitmap_set(id_bitmap, TASK_KERNEL_ID);
    bitmap_set(id_bitmap, TASK_INIT_ID);

    if (!task_migration_init()) return false;

    // 初始化 worker 线程池
    task_worker_init();

    sched_func();

    TASK_PRINT("task data init success\n");

    return true;
}

// 任务管理初始化
bool task_init(void) {
    task_struct *idle = task_create_idle();

    if (!idle) return false;
    smp_set_idle(idle);

    if (sched_class_ptr && sched_class_ptr->init) sched_class_ptr->init();

    // 为当前CPU分配调度定时器句柄
    struct clockevent_timer *sched_timer = clockevent_timer_alloc();
    if (!sched_timer) return false;
    
    // 初始化定时器回调
    clockevent_timer_init_callback(sched_timer, task_clock_event_handle, NULL);
    smp_set_sched_timer(sched_timer);

    // 设置任务管理初始化完成
    task_init_flag = true;
    
    if (get_logical_id() == bootboot->bspid)
        task_create_kernel_thread(kthread_test, NULL);

    TASK_PRINT("task init success\n");

    return true;
}

// 启动任务调度
void task_run(void) {
    TASK_PRINT("Start task scheduling\n");

    task_set_next_timer();
    task_boot_sched();

    // 不应该返回，如果返回说明代码错误
    TASK_PANIC("system error\n");
}