/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <task/types.h>
#include <asm/serial.h>
#include <bootboot.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <task.h>
#include <time.h>
#include <rcu.h>
#include <list_rcu.h>
#include <stdatomic.h>
#include <heap.h>
#include <stdbool.h>
#include <klibc.h>
#include <initcall.h>

#define TASK_TEST_PRINT(fmt, ...) \
    printk("[TASK_TEST]" fmt, ##__VA_ARGS__)

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

#define RCU_TEST_THREADS      32
#define RCU_TEST_ROUNDS       50
#define RCU_TEST_DATA_SIZE    64

// RCU 保护的数据节点
struct rcu_test_node {
    struct rcu_head head;
    atomic_int version;
    char data[RCU_TEST_DATA_SIZE];
};

// 保护全局 RCU 指针及其锁
static struct {
    spinlock_t lock;
    struct rcu_test_node * __rcu ptr;
} rcu_test_data = {
    .lock = SPIN_LOCK_INIT,
    .ptr  = NULL
};

// 写者轮流递增的版本号
static atomic_int global_version = ATOMIC_VAR_INIT(0);

// 读者发现的错误总数
static atomic_int error_count = ATOMIC_VAR_INIT(0);

// 已完成的线程计数器
static atomic_int threads_done = ATOMIC_VAR_INIT(0);

// RCU 释放回调
static void rcu_test_free(struct rcu_head *head) {
    struct rcu_test_node *node = container_of(head, struct rcu_test_node, head);
    kheap_free(node);
}

/**
 * 填充随机数据
 *
 * @param data 数据缓冲区
 * @param len 缓冲区大小
 * @param tid 线程 ID
 * @param version 当前版本号
 */
static void fill_chaos_data(char *data, size_t len, int tid, int version) {
    struct timespec ts;
    for (size_t i = 0; i < len; i++) {
        /*
         * 每次循环获取一次时间
         * 因为循环速度远快于时间更新
         * 相邻字节的时间差小
         *
         * 混入 tid 和 version 是为了防止不同线程在同一纳秒产生相同数据
         */
        time_get(&ts);
        data[i] = (char)(ts.tv_nsec ^ (ts.tv_nsec >> 7) ^ tid ^ version ^ i);
    }
}

/*
 * 用纳秒时间戳的最低几位决定是否为读者
 * 混入 tid 和轮次防止同步
 */
static bool is_reader(int tid, int round) {
    struct timespec ts;
    time_get(&ts);
    return ((ts.tv_nsec ^ tid ^ round) & 1) != 0;
}

// 用纳秒时间戳产生 1-3 个 tick 的延迟
static int chaos_delay(int tid, int round) {
    struct timespec ts;
    time_get(&ts);

    /*
     * 混入 tid 和 round 是为了让不同线程、不同轮次的延迟长度不同
     * 避免多个线程在同一个 tick 被唤醒后执行相同的操作
     */
    return (int)((ts.tv_nsec ^ (tid << 4) ^ (round << 8)) % 3) + 1;
}

// 测试线程入口，参数为线程 ID
static void test_thread(void *arg) {
    int tid = (int)(uintptr_t)arg;
    int rounds = 0;

    TASK_TEST_PRINT("Thread T%d started\n", tid);

    while (rounds < RCU_TEST_ROUNDS) {
        if (is_reader(tid, rounds)) {
            /*
             * 进入读临界区
             * 记录当前数据节点的版本号和完整数据
             * 同时保存节点指针用于后续地址比对
             */
            rcu_read_lock();
            struct rcu_test_node *node = rcu_dereference(rcu_test_data.ptr);
            if (!node) {
                rcu_read_unlock();
                goto next_round;
            }

            int ver = atomic_load(&node->version);
            char sample[RCU_TEST_DATA_SIZE];
            memcpy(sample, node->data, RCU_TEST_DATA_SIZE);
            
            // 保存第一次读取的节点地址
            struct rcu_test_node *first_node = node;
            rcu_read_unlock();

            /*
             * 在退出临界区后、再次进入之前插入延迟
             * 给写者留出窗口期，让它们有机会更新指针并释放旧节点
             */
            int delay = chaos_delay(tid, rounds);
            for (int i = 0; i < delay; i++)
                cpu_pause();

            /*
             * 再次进入临界区检查：如果节点地址已变
             * 说明全局指针在两次读取之间被替换
             * 本轮校验直接跳过
             *
             * 只有地址相同时才继续比较版本号和数据
             */
            rcu_read_lock();
            node = rcu_dereference(rcu_test_data.ptr);
            if (node != first_node) {
                rcu_read_unlock();
                goto next_round;
            }

            if (node && atomic_load(&node->version) == ver)
                if (memcmp(sample, node->data, RCU_TEST_DATA_SIZE) != 0) {
                    atomic_fetch_add(&error_count, 1);
                    TASK_TEST_PRINT(
                        "reader T%d: version %d unchanged, data modified\n",
                        tid, ver
                    );
                }

            rcu_read_unlock();
        } else {
            /*
             * 分配新节点
             * 递增版本号
             * 填充随机数据
             * 使用自旋锁保护全局指针更新
             *
             * 旧节点通过 rcu_call 延迟释放
             * 保证读者安全
             */
            struct rcu_test_node *new_node = kheap_alloc(sizeof(*new_node));
            if (!new_node) goto next_round;

            int new_ver = atomic_fetch_add(&global_version, 1);
            atomic_init(&new_node->version, new_ver);
            fill_chaos_data(new_node->data, RCU_TEST_DATA_SIZE, tid, new_ver);

            spin_lock(&rcu_test_data.lock);
            struct rcu_test_node *old = rcu_test_data.ptr;
            rcu_assign_pointer(rcu_test_data.ptr, new_node);
            spin_unlock(&rcu_test_data.lock);

            if (old)
                rcu_call(&old->head, rcu_test_free);
        }

next_round:
        rounds++;
        if (rounds % 10 == 0)
            TASK_TEST_PRINT("[T%d] round %d/%d\n", tid, rounds, RCU_TEST_ROUNDS);

        int delay = chaos_delay(tid, rounds);
        for (int i = 0; i < delay; i++)
            cpu_halt();
    }

    TASK_TEST_PRINT("Thread T%d succeeded\n", tid);
    atomic_fetch_add(&threads_done, 1);
}

static void test(void) {
    if (get_logical_id() != bootboot->bspid)
        return;

    // 创建初始数据节点
    struct rcu_test_node *initial = kheap_alloc(sizeof(*initial));
    if (initial) {
        atomic_init(&initial->version, 0);
        struct timespec now;
        time_get(&now);
        fill_chaos_data(initial->data, RCU_TEST_DATA_SIZE, 0, (int)now.tv_nsec);

        spin_lock(&rcu_test_data.lock);
        rcu_assign_pointer(rcu_test_data.ptr, initial);
        spin_unlock(&rcu_test_data.lock);
    }

    TASK_TEST_PRINT("Chaos test starting: %d threads, %d rounds\n", RCU_TEST_THREADS, RCU_TEST_ROUNDS);

    // 创建测试线程 T0 到 T31
    for (int i = 0; i < RCU_TEST_THREADS; i++)
        task_create_kernel_thread(test_thread, (void *)(uintptr_t)i);
}

INITCALL(kthreadtest, 0, test);