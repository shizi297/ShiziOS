/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#ifndef MEM_INIT_H
#define MEM_INIT_H

#include <mm/bootmem/linear_map.h>
#include <mm/bootmem/boot_allot.h>
#include <mm/pmm/buddy.h>
#include <mm/vmm/vmm_map.h>

// 初始化内存管理
static inline void memory_init(void) {
    linear_map_setup();    // 建立线性映射
    boot_alloc_init();     // 初始化启动分配器
    pmm_init();         //初始化伙伴系统
    vmm_init();      // 初始化虚拟内存管理
}

// 获取内存状态信息
static inline void memory_info(void)
{
    boot_alloc_info();
}

#endif /* MEM_INIT_H */