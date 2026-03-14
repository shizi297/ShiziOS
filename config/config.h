/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */
 
#ifndef CONFIG_H
#define CONFIG_H

#define ARCH_X86_64   0
#define ARCH_ARM      1
#define ARCH_RISCV    2

// 所有线程的内核栈大小
#define KERNEL_START_SIZE 16384     // 内核栈数量
#define TASK_ID_MAX ((1ULL << 22) - 1)  // 任务管理ID的最大数量

#define ARCH          ARCH_X86_64

#endif // CONFIG_H 