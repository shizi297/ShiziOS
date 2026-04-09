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

#define TASK_TEST_PRINT(fmt, ...) \
    printk("[TASK_TEST]" fmt, ##__VA_ARGS__)

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
static void test(void);
task_test_func_t task_test = test;

static void test_thread(void *arg) {
    char id = (char)(uintptr_t)arg;
    uint64_t cnt = 0;

    TASK_TEST_PRINT("Thread %c started\n", id);

    while (1) {
        cnt++;
        if (cnt % 100 == 0) {
            TASK_TEST_PRINT("[%c] count=%llu\n", id, cnt);
            if (cnt == 1000) {
                TASK_TEST_PRINT("Thread %c succeeded\n", id);
                return;
            }

        }
        cpu_halt();
    }
}

static void test(void) {
    uint64_t flags;
    spin_lock_irqsave(&serial_lock, &flags);
    if (get_logical_id() == bootboot->bspid) {
        task_create_kernel_thread(test_thread, (void*)'A');
        task_create_kernel_thread(test_thread, (void*)'B');
        task_create_kernel_thread(test_thread, (void*)'C');
        task_create_kernel_thread(test_thread, (void*)'D');
        task_create_kernel_thread(test_thread, (void*)'E');
        task_create_kernel_thread(test_thread, (void*)'F');
        task_create_kernel_thread(test_thread, (void*)'G');
        task_create_kernel_thread(test_thread, (void*)'H');
        task_create_kernel_thread(test_thread, (void*)'I');
        task_create_kernel_thread(test_thread, (void*)'J');
        task_create_kernel_thread(test_thread, (void*)'K');
        task_create_kernel_thread(test_thread, (void*)'L');
        task_create_kernel_thread(test_thread, (void*)'M');
        task_create_kernel_thread(test_thread, (void*)'N');
        task_create_kernel_thread(test_thread, (void*)'O');
        task_create_kernel_thread(test_thread, (void*)'P');
        task_create_kernel_thread(test_thread, (void*)'Q');
        task_create_kernel_thread(test_thread, (void*)'R');
        task_create_kernel_thread(test_thread, (void*)'S');
        task_create_kernel_thread(test_thread, (void*)'T');
        task_create_kernel_thread(test_thread, (void*)'U');
        task_create_kernel_thread(test_thread, (void*)'V');
        task_create_kernel_thread(test_thread, (void*)'W');
        task_create_kernel_thread(test_thread, (void*)'X');
        task_create_kernel_thread(test_thread, (void*)'Y');
        task_create_kernel_thread(test_thread, (void*)'Z');
    }
    spin_unlock_irqrestore(&serial_lock, flags);
}