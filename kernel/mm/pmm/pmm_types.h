/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include "pmm.h"
#include <stdint.h>
#include <spinlock.h>
#include <list.h>
#include <stdbool.h>  

typedef struct list_head free_list_t;

/*
 * 空闲链表的头节点
 * 指向第一个链表节点
 */
typedef struct {
    struct list_head head;
} free_area_t;

typedef struct {
    spinlock_t lock;  
    char _pad[CACHE_LINE_SIZE - sizeof(spinlock_t)];

    uint64_t start_pfn;         // 起始页帧号
    uint64_t end_pfn;           // 结束页帧号
    free_area_t free_areas[MAX_ORDER];
} zone_t;

//内存块结构体，多个页组成，order大小与空闲链表相关
typedef struct {
    uint8_t is_head:1;   //是否为块的首页
    uint8_t is_free:1;    //是否被分配
    uint8_t flags:6;    //预留
    
    uint8_t order:5;
    uint8_t zone:3;
    
    uint16_t map_count;    //映射计数
    uint32_t ref_count;     //引用计数

    uintptr_t on_pte_ptr;   // 页表页的上层页表项（不是页表页）指针（虚拟地址）
} mem_block_t;

typedef struct {
    uint64_t count;    
    char _pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];       

    spinlock_t lock;
    char _pad2[CACHE_LINE_SIZE - sizeof(spinlock_t)];

    mem_block_t blocks[];   
} mem_block_array_t;