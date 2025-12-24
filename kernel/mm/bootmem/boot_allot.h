/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BOOT_ALLOC_H
#define BOOT_ALLOC_H

#include <stdint.h>
#include <stddef.h>
#include <mm/bootmem/linear_map.h>

//早期位图大小
#define BOOT_ALLOC_BITMAP_SIZE  (32 * 1024)  

//早期位图管理的区域边界
#define BOOT_ALLOC_MAX_PAGES    (1024 * 1024 * 1024 / 4096)  

// 初始化启动内存分配器
void boot_alloc_init(void);

// 分配连续物理页(返回虚拟地址)
void* boot_alloc(size_t pages);

// 获取分配器信息
void boot_alloc_info(void);

// 获取早期位图的虚拟地址
void* boot_alloc_get_bitmap(void);

#endif //boot_allot.h