/* SPDX-License-Identifier: Apache-2.0 */

#ifndef MEM_INIT_H
#define MEM_INIT_H

#include "bootmem/linear_map.h"
#include "bootmem/boot_allot.h"
#include "pmm/buddy.h"

// 初始化内存管理
static inline void memory_init(void)
{
    linear_map_setup();    // 建立线性映射
    boot_alloc_init();     // 初始化启动分配器
    pmm_init();         //初始化伙伴系统
}

// 获取内存状态信息
static inline void memory_info(void)
{
    boot_alloc_info();
}

#endif /* MEM_INIT_H */