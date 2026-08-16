/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <bootboot.h>

/* 线性映射区域 */
#define LINEAR_MAP_START    0xffff808000000000ULL  
#define LINEAR_MAP_END      0xffff880000000000ULL  
#define LINEAR_MAP_SIZE     (8ULL << 40)    

#define USER_END            0x00007FFFFFFFFFFFULL     // 用户空间结束地址
#define USER_START          0x0000000000400000ULL     // 用户空间起始地址
#define USER_STACK_TOP      0x00007FFFFF000000ULL     // 用户栈顶地址
#define USER_STACK_SIZE     (8 * 1024 * 1024)   // 用户栈大小

// mmio地址
#define MMIO_MAP  (0xFFFF000000000000ULL | ((uint64_t)384 << 39))

/* 线性映射地址转物理地址 */
#define LINEAR_TO_PHYS(va) ((uintptr_t)(va) - LINEAR_MAP_START)

/* 物理地址转虚拟地址 */
#define PHYS_TO_LINEAR(pa) ((void*)((uintptr_t)(pa) + LINEAR_MAP_START))