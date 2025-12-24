/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PMM_TYPES_H
#define PMM_TYPES_H

#define CACHE_LINE_SIZE 64

#include <stdint.h>
#include <spinlock.h>

#include "pmm.h"


typedef struct free_list_node {
    struct free_list_node *prev;  
    struct free_list_node *next;  
} free_list_t;

/*
 * 空闲链表的头节点
 * 指向第一个链表节点
 */
typedef struct {
    free_list_t *head;                     
} free_area_t;

typedef struct {
    spinlock_t lock;  
    char _pad[CACHE_LINE_SIZE - sizeof(spinlock_t)];

    uint64_t start_pfn;         // 起始页帧号
    uint64_t end_pfn;           // 结束页帧号
    free_area_t free_areas[MAX_ORDER]; 
} zone_t;

//内存块结构体，多个页组成，order大小与空闲链表相关
typedef struct __attribute__((packed)) {
    uint8_t is_head:1;   //是否为块的首页
    uint8_t is_free:1;    //是否被分配
    uint8_t flags:6;    //预留
    
    uint8_t order:5;
    uint8_t zone:3;
    
    uint16_t map_count;    //映射计数
    uint32_t ref_count;     //引用计数
} mem_block_t;

typedef struct {
    uint64_t count;    
    char _pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];       

    spinlock_t lock;
    char _pad2[CACHE_LINE_SIZE - sizeof(spinlock_t)];

    mem_block_t blocks[];   
} mem_block_array_t;

#endif // PMM_TYPES_H 