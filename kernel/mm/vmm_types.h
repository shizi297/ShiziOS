/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>

// 虚拟内存权限标志
typedef enum {
    VM_NONE  = 0,
    VM_READ  = 1 << 0,  // 可读
    VM_WRITE = 1 << 1,  // 可写
    VM_EXEC  = 1 << 2,  // 可执行
    VM_USER  = 1 << 3,  // 用户空间（否则为内核空间）
    VM_UC    = 1 << 4,
} vm_prot_t;

// 页表条目类型
typedef uint64_t pte_t;