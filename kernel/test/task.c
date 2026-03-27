/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <task/types.h>
#include <serial.h>
#include <bootboot.h>
#include <arch_processor.h>
#include <smp.h>
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
        if (cnt % 5 == 0) {
            TASK_TEST_PRINT("[%c] count=%llu\n", id, cnt);
        }
        cpu_halt();
    }
}

static void test(void) {
    if (get_logical_id() == bootboot->bspid) {
        task_create_kernel_thread(test_thread, (void*)'A');
        task_create_kernel_thread(test_thread, (void*)'B');
        task_create_kernel_thread(test_thread, (void*)'C');
        task_create_kernel_thread(test_thread, (void*)'D');
        task_create_kernel_thread(test_thread, (void*)'E');
        task_create_kernel_thread(test_thread, (void*)'F');
    }
}