/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BOOTMEM_H
#define BOOTMEM_H

#include <stdint.h>

#define PAGE_TABLE_ENTRIES 512
#define PAGE_TABLE_MASK    0x1FF

// 地址提取
#define PML4_INDEX(addr)   (((addr) >> 39) & PAGE_TABLE_MASK)
#define PDPT_INDEX(addr)   (((addr) >> 30) & PAGE_TABLE_MASK)
#define PD_INDEX(addr)     (((addr) >> 21) & PAGE_TABLE_MASK)
#define PT_INDEX(addr)     (((addr) >> 12) & PAGE_TABLE_MASK)

// 页表标志位定义
#define PAGE_PRESENT       (1ULL << 0)
#define PAGE_WRITABLE      (1ULL << 1) 
#define PAGE_SIZE_BIT      (1ULL << 7)  // PS位

// 页大小定义
#define PAGE_1GB_SIZE      (1ULL << 30)
#define PAGE_2MB_SIZE      (1ULL << 21)
#define PAGE_4KB_SIZE      (1ULL << 12)

// 页表标志组合
#define PAGE_1GB_FLAGS     (PAGE_PRESENT | PAGE_WRITABLE | PAGE_SIZE_BIT)
#define PAGE_2MB_FLAGS     (PAGE_PRESENT | PAGE_WRITABLE | PAGE_SIZE_BIT)
#define PAGE_4KB_FLAGS     (PAGE_PRESENT | PAGE_WRITABLE)

// 临时分配记录
#define TEMP_RECORD_MAX    504

typedef struct {
    uint64_t start_pfn[TEMP_RECORD_MAX];
    uint64_t count;
} temp_linear_map_t;

#endif // BOOTMEM_H