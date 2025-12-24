/* SPDX-License-Identifier: Apache-2.0 */
 
#ifndef LINEAR_MAP_H
#define LINEAR_MAP_H

#include <stdint.h>
#include <stddef.h>
#include <mm/bootmem/bootmem.h>

/* 线性映射区域 */
#define LINEAR_MAP_START    0xffff808000000000ULL  
#define LINEAR_MAP_END      0xffff880000000000ULL  
#define LINEAR_MAP_SIZE     (8ULL << 40)           
/* 8TB需要的1GB页数 */
#define LINEAR_MAP_PAGES   (8ULL * 1024ULL)

/* 线性映射地址转物理地址 */
#define LINEAR_TO_PHYS(va) ((uintptr_t)(va) - LINEAR_MAP_START)

/* 物理地址转虚拟地址 */
#define PHYS_TO_LINEAR(pa) ((void*)((uintptr_t)(pa) + LINEAR_MAP_START))

void linear_map_setup(void);
temp_linear_map_t* linear_map_get_temp(void);

#endif /* LINEAR_MAP_H */