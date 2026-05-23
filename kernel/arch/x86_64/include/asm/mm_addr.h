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

// mmio地址
#define MMIO_MAP  (0xFFFF000000000000ULL | ((uint64_t)384 << 39))

/* 线性映射地址转物理地址 */
#define LINEAR_TO_PHYS(va) ((uintptr_t)(va) - LINEAR_MAP_START)

/* 物理地址转虚拟地址 */
#define PHYS_TO_LINEAR(pa) ((void*)((uintptr_t)(pa) + LINEAR_MAP_START))