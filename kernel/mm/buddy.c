/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "pmm.h"
#include <stdint.h>
#include <bootboot.h>
#include <mm/boot_allot.h>
#include <kio.h>
#include <spinlock.h>
#include <stddef.h>
#include <list.h>
#include <bitmap.h>
#include <minmax.h>

#define PMM_PRINT(fmt, ...) \
    printk("[PMM] " fmt, ##__VA_ARGS__)

#define PMM_PANIC(fmt, ...) \
    printp("[PMM] ERROR: " fmt, ##__VA_ARGS__)

static zone_t zones[3];

static uint64_t max_pfn = 0;

/*
 * 全局位图的虚拟起始地址的指针
 * 用于伙伴系统建立前的分配
 * 伙伴系统建立后用于快速获取内存占用率
 */
static uint64_t* bitmap64 = NULL;  

/*
 * 内存块
 * 每页内存有一个
 * 作用相当于page_struct
 * 当order == 1时
 * 退化为page_struct
 */
static mem_block_array_t* mem_block = NULL;

static inline void check_pfn_valid(uint64_t pfn) {
    if (pfn > max_pfn) {
        PMM_PANIC("invalid pfn\n");
    }
}

static void calculate_max_pfn(void) {
    size_t num_entries = (((BOOTBOOT*)BOOTBOOT_INFO)->size - 128) / sizeof(MMapEnt);
    MMapEnt* mmap = &((BOOTBOOT*)BOOTBOOT_INFO)->mmap;
    
    max_pfn = 0;
    
    for (size_t i = 0; i < num_entries; i++) {
        MMapEnt* entry = &mmap[i];
        uint64_t ptr = MMapEnt_Ptr(entry);
        uint64_t size = MMapEnt_Size(entry);
        uint8_t type = MMapEnt_Type(entry);
        
        /*
         * 只统计RAM区域
         * bootboot引导除空闲外的其他类型都包含非RAM区域
         * 所以不能用其他类型mmap的数据
         */
        if (type == MMAP_FREE) {
            uint64_t end_addr = ptr + size;
            uint64_t end_pfn = end_addr / PAGE_SIZE;
            
            if (end_pfn > max_pfn) {
                max_pfn = end_pfn;
            }
        }
    }
    
    // 如果max_pfn不为0，则减1
    if (max_pfn > 0) {
        max_pfn--;
    }
}

static void alloc_bitmap_init(void){
    // 计算创建位图所需的页数
    size_t alloc_bitmap_bytes = BITMAP_BYTES(max_pfn + 1);

    size_t alloc_bitmap_pages = (alloc_bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    void* alloc_bitmap = boot_alloc(alloc_bitmap_pages);
    if (!alloc_bitmap) 
        PMM_PANIC("Failed to allocate bitmap for PMM\n");
    
    bitmap64 = (uint64_t*)alloc_bitmap;
    
    void* boot_bitmap = boot_alloc_get_bitmap();
    
    // 初始化位图，将所有位设置为1（默认已分配）
    bitmap_fill((bitmap_t*)bitmap64, max_pfn + 1);
    
    // 遍历BOOTBOOT内存映射，将空闲区域标记为0
    MMapEnt* mmap_ent = &((BOOTBOOT*)BOOTBOOT_INFO)->mmap;
    size_t mmap_entries = (((BOOTBOOT*)BOOTBOOT_INFO)->size - 128) / sizeof(MMapEnt);
    
    for (size_t i = 0; i < mmap_entries; i++) {
        MMapEnt* entry = &mmap_ent[i];
        
        if (MMapEnt_Type(entry) == MMAP_FREE) {
            uint64_t start_addr = MMapEnt_Ptr(entry);
            uint64_t size = MMapEnt_Size(entry);
            uint64_t end_addr = start_addr + size;
            
            uint64_t start_pfn = start_addr / PAGE_SIZE;
            uint64_t end_pfn = (end_addr + PAGE_SIZE - 1) / PAGE_SIZE;
            
            if (start_pfn > max_pfn) continue;
            end_pfn = min(end_pfn, max_pfn + 1);
            
            for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++) {
                bitmap_clear((bitmap_t*)bitmap64, pfn);
            }
        }
    }
    
    /*
     * 复制早期位图分配情况
     * 因为早期位图存储了一些mmap没有记录的内存
     */
    if (boot_bitmap) {
        for (size_t pfn = 0; pfn < BOOT_ALLOC_MAX_PAGES; pfn++) {
            bool is_allocated = bitmap_check((bitmap_t*)boot_bitmap, pfn);
            if (is_allocated) {
                bitmap_set((bitmap_t*)bitmap64, pfn);
            } else {
                bitmap_clear((bitmap_t*)bitmap64, pfn);
            }
        }
    }
    
    /*
     * 将早期位图本身占用的内存标记为空闲
     * 早期位图不再被使用
     */
    if (boot_bitmap) {
        uintptr_t boot_bitmap_phys = LINEAR_TO_PHYS((uintptr_t)boot_bitmap);
        
        uint64_t start_pfn = boot_bitmap_phys / PAGE_SIZE;
        uint64_t end_pfn = (boot_bitmap_phys + BOOT_ALLOC_BITMAP_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
        
        for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++) {
            bitmap_clear((bitmap_t*)bitmap64, pfn);
        }
    }
}

/*
 * 全局位图内存分配
 * 内存占用信息获取
 * 伙伴系统建立后不再用于分配
 */
static void *bitmap_alloc(uint64_t pages) {
    uint64_t consecutive = 0;  
    uint64_t start_pfn = 0;    
    
    for (uint64_t i = 0; i <= max_pfn; i++) {
        if (!bitmap_check((bitmap_t*)bitmap64, i)) {
            if (consecutive == 0) {
                start_pfn = i;
            }
            consecutive++;
            
            if (consecutive == pages) {
                for (uint64_t j = start_pfn; j < start_pfn + pages; j++) {
                    bitmap_set((bitmap_t*)bitmap64, j);
                }
                return PHYS_TO_LINEAR(start_pfn * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }
    
    PMM_PANIC("Cannot allocate required memory for system initialization\n");
    return NULL;
}

/*
 * 初始化zone
 * zones[0] DMA区域 0-16mb 
 * 用于部分老设备
 * 他们的寻址范围只有16mb
 * zones[1] DMA32区域 上限为4gb
 * 边界原因同上
 * zones[2] NORMAL 正常区域的内存
 * 4GB 以上
 * 程序默认使用的区域
 */
static void zone_init(void){
    uint8_t i = 0;
    uint8_t j = 0;
    
    // DMA [0,16mb)
    zones[ZONE_DMA].start_pfn = 0;
    zones[ZONE_DMA].end_pfn = 4096;
    
    //检测是否小于4gb
    if(max_pfn < 1048576){
        //DMA32[16mb,max_pfn + 1)
        zones[ZONE_DMA32].start_pfn = 4096;
        zones[ZONE_DMA32].end_pfn = max_pfn + 1;
        //NORMAL区域为空
        zones[ZONE_NORMAL].start_pfn = 0;
        zones[ZONE_NORMAL].end_pfn = 0;
    }else{
        //DMA32[16mb,4gb)
        zones[ZONE_DMA32].start_pfn = 4096;
        zones[ZONE_DMA32].end_pfn = 1048576;
        //NORMAL[4gb,max_pfn + 1)
        zones[ZONE_NORMAL].start_pfn = 1048576;
        zones[ZONE_NORMAL].end_pfn = max_pfn + 1;
    }
    
    //初始化zone锁和free_areas链表头
    for(i = 0; i < 3; i++){
        spinlock_init(&zones[i].lock);  
        for(j = 0; j < MAX_ORDER; j++){
            INIT_LIST_HEAD(&zones[i].free_areas[j].head);
        }
    }
}

/**
 * 添加新内存块到空闲链表
 * 
 * @param free_list 要添加的伙伴块的虚拟地址
 * @param zone_count 伙伴块属于的zone区域
 * @param order_count 伙伴块属于的order区域
 * 
 * 调用时需要zone.lock锁
 * 因为访问了空闲链表
 */ 
static void add_free_lists(free_list_t *free_list, uint8_t zone_count, uint8_t order_count) {
    list_add(free_list, &zones[zone_count].free_areas[order_count].head);
}

/**
 * 删除空闲链表的内存块
 *
 * @param pfn 要移除的伙伴块的页帧号
 * 
 * 使用pfn来查询伙伴信息
 *
 * 调用时需要zone.lock锁
 * 因为访问了空闲链表
 */
static void remove_free_lists(uint64_t pfn) {
    uintptr_t phys_addr = pfn * PAGE_SIZE;
    struct list_head *node = (struct list_head *)PHYS_TO_LINEAR(phys_addr);
    uint8_t zone = mem_block->blocks[pfn].zone;
    uint8_t order = mem_block->blocks[pfn].order;

    //pfn不在任何zone范围内
    if (zone == 0xFF) {
        return;
    }
    
    list_del(node);
}

/*
 * 如果同时需要zone锁和mem_block锁
 * 必须先获取mem_block锁再获取zone锁
 * 为了避免死锁
 * 释放尽量以获取的反向顺序
 */

/*
 * 拆分空闲链表中的伙伴块
 * 调用者必须持有zone锁和mem_block锁
 */
static free_list_t *split_buddy_block(uint64_t pfn) {
    uint8_t zone = mem_block->blocks[pfn].zone;
    uint8_t order = mem_block->blocks[pfn].order;
    uint64_t buddy_pfn = pfn ^ (1ULL << (order - 1));  
    free_list_t *left = (free_list_t *)PHYS_TO_LINEAR(pfn * PAGE_SIZE);
    free_list_t *right = (free_list_t *)PHYS_TO_LINEAR(buddy_pfn * PAGE_SIZE);

    uint64_t block_size = 1ULL << (order - 1);

    remove_free_lists(pfn);

    for (uint64_t i = 0; i < block_size; i++) {
        // 更新左半部分
        mem_block->blocks[pfn + i].order = order - 1;
        mem_block->blocks[pfn + i].is_head = (i == 0) ? 1 : 0;
        mem_block->blocks[pfn + i].is_free = 1;
        mem_block->blocks[pfn + i].zone = zone;
        mem_block->blocks[pfn + i].ref_count = 0;
        
        // 更新右半部分
        mem_block->blocks[buddy_pfn + i].order = order - 1;
        mem_block->blocks[buddy_pfn + i].is_head = (i == 0) ? 1 : 0;
        mem_block->blocks[buddy_pfn + i].is_free = 1;
        mem_block->blocks[buddy_pfn + i].zone = zone;
        mem_block->blocks[buddy_pfn + i].ref_count = 0;
    }

    add_free_lists(left, zone, order - 1);
    add_free_lists(right, zone, order - 1);

    return left;
}

/*
 * 合并空闲链表中的伙伴块
 * 调用者必须持有zone锁和mem_block锁
 * 
 * @return 合并后块的虚拟地址
 */
static free_list_t* merge_buddy_block(uint64_t pfn1, uint64_t pfn2) {
    uint8_t order1 = mem_block->blocks[pfn1].order;
    uint8_t order2 = mem_block->blocks[pfn2].order;
    uint8_t zone1 = mem_block->blocks[pfn1].zone;
    uint8_t zone2 = mem_block->blocks[pfn2].zone;

    free_list_t *result = NULL;
    
    // 检查是否可以合并
    if (order1 != order2 || zone1 != zone2) {
        return NULL;
    }
    
    // 验证伙伴关系
    if ((pfn1 ^ pfn2) != (1ULL << order1)) {
        return NULL; // 不是伙伴
    }
    
    bool is_pfn1_first = (pfn1 < pfn2);
    uint64_t merged_pfn = is_pfn1_first ? pfn1 : pfn2;
    free_list_t *merged_node = (free_list_t *)(is_pfn1_first ? PHYS_TO_LINEAR(pfn1 * PAGE_SIZE) : PHYS_TO_LINEAR(pfn2 * PAGE_SIZE));
    uint8_t new_order = order1 + 1;
    uint64_t new_block_pages = 1ULL << new_order;
    
    remove_free_lists(pfn1);
    remove_free_lists(pfn2);
    add_free_lists(merged_node, zone1, new_order);
    
    for (uint64_t i = 0; i < new_block_pages; i++) {
        uint64_t current_pfn = merged_pfn + i;
        mem_block->blocks[current_pfn].is_head = (i == 0) ? 1 : 0;
        mem_block->blocks[current_pfn].order = new_order;
        mem_block->blocks[current_pfn].is_free = 1;
    }

    result = merged_node;

    return result;
}

/*
 * 检查页是否被分配
 * 只能用于伙伴系统建立之前
 * 因为伙伴系统建立后使用mem_block
 */ 
static inline bool page_is_alloc(uint64_t pfn) {
    return bitmap_check((bitmap_t*)bitmap64, pfn);
}

/*
 * 空闲链表初始化
 * 调用了add_free_lists没加锁
 * 因为初始化阶段没有多核
 * 所以不需要加锁
 */
static void free_lists_init(void) {
    // 遍历每个内存区域
    for (int zone_id = ZONE_DMA; zone_id <= ZONE_NORMAL; zone_id++) {
        zone_t* zone = &zones[zone_id];
        
        uint64_t pfn = zone->start_pfn;
        
        // 扫描zone
        while (pfn < zone->end_pfn) {
            // 跳过已分配的页
            while (pfn < zone->end_pfn && page_is_alloc(pfn)) {
                pfn++;
            }
            
            if (pfn >= zone->end_pfn) {
                break;
            }
            
            // 找到连续空闲区域的起点
            uint64_t free_start = pfn;
            uint64_t free_size = 0;
            
            while (pfn < zone->end_pfn && !page_is_alloc(pfn)) {
                free_size++;
                pfn++;
            }
            
            // 切割这个区域
            uint64_t current = free_start;
            uint64_t remaining = free_size;
            
            while (remaining > 0) {
                int best_order = -1;
                
                // 从大到小寻找合适的order
                for (int order = MAX_ORDER - 1; order >= 0; order--) {
                    uint64_t block_size = 1ULL << order;
                    
                    // 确保块大小不超过剩余页数，当前地址对齐到块大小
                    if (block_size <= remaining && (current & (block_size - 1)) == 0) {
                        best_order = order;
                        break;
                    }
                }
                
                // 没找到合适的order（理论上都能找到）
                if (best_order < 0) {
                    best_order = 0;
                }
                
                uint64_t block_size = 1ULL << best_order;
                
                free_list_t* node = (free_list_t*)PHYS_TO_LINEAR(current * PAGE_SIZE);
                add_free_lists(node, zone_id, best_order);
                
                current += block_size;
                remaining -= block_size;
            }
        }
    }
}

static void print_zone_info(void) {
    PMM_PRINT("Zone DMA: 0-16MB");
    
    uint64_t total_memory = (max_pfn + 1) * PAGE_SIZE;
    uint64_t total_mb = total_memory / (1024 * 1024);
    
    if (total_memory > (4ULL * 1024 * 1024 * 1024)) {
        printk(", DMA32: 16MB-4GB, NORMAL: 4GB-%luMB\n", total_mb);
    } else {
        printk(", DMA32: 16MB-%luMB\n", total_mb);
    }
}

//计算总空闲内存
static uint64_t calculate_total_free_pages(void) {
    uint64_t total_free_pages = 0;
    struct list_head *pos;
    
    for (int z = ZONE_DMA; z <= ZONE_NORMAL; z++) {
        for (int o = 0; o < MAX_ORDER; o++) {
            list_for_each(pos, &zones[z].free_areas[o].head) {
                total_free_pages += (1ULL << o);
            }
        }
    }
    
    return total_free_pages;
}

/* 
 * 分配内存创建mem_block结构体
 * 建立后通过这个来访问伙伴块信息
 */ 
static void alloc_mem_block(void) {
    size_t header_size = offsetof(mem_block_array_t, blocks);
    size_t array_size = (max_pfn + 1) * sizeof(mem_block_t);
    size_t total_size = header_size + array_size;
    size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    mem_block_array_t* array = (mem_block_array_t*)bitmap_alloc(pages);
    array->count = max_pfn;
    spinlock_init(&array->lock);
    
    for (uint64_t i = 0; i <= max_pfn; i++) {
        mem_block_t* block = &array->blocks[i];
        
        block->is_head = 0;
        block->is_free = 0;
        block->flags = 0;
        block->order = 0;
        block->zone = 0;
        block->map_count = 0;
        block->ref_count = 0;
    }
    
    mem_block = array;
}
/*
 * mem_block初始化
 * 访问数据不需要加锁
 * 因为初始化阶段没有多核
 */
static void mem_block_init(void) {
    uint64_t pfn = 0;

    for (uint8_t zone_id = ZONE_DMA; zone_id <= ZONE_NORMAL; zone_id++) {
        zone_t *zone = &zones[zone_id];

        if (zone->start_pfn >= zone->end_pfn) continue;

        pfn = zone->start_pfn;
        while (pfn < zone->end_pfn) {
            if (page_is_alloc(pfn)) {
                mem_block->blocks[pfn].is_head = 1;
                mem_block->blocks[pfn].is_free = 0;
                mem_block->blocks[pfn].order = 0;
                mem_block->blocks[pfn].zone = zone_id;
            }
            pfn++;
        }
    }

    for (uint8_t zone_id = ZONE_DMA; zone_id <= ZONE_NORMAL; zone_id++) {
        for (uint8_t order_id = 0; order_id < MAX_ORDER; order_id++) {
            struct list_head *pos;
            list_for_each(pos, &zones[zone_id].free_areas[order_id].head) {
                uintptr_t phys_addr = LINEAR_TO_PHYS((uintptr_t)pos);
                pfn = phys_addr >> PAGE_SHIFT;
                uint64_t pages = 1ULL << order_id;
                
                for (uint64_t i = 0; i < pages; i++) {
                    mem_block_t* block = &mem_block->blocks[pfn + i];
                    block->is_head = (i == 0) ? 1 : 0;
                    block->is_free = 1;
                    block->order = order_id;
                    block->zone = zone_id;
                }
            }
        }
    }
}

/**
 * 分配伙伴块
 * 
 * @param order 分配的伙伴块大小
 * @param zone  首选内存区域：ZONE_DMA(0)、ZONE_DMA32(1)、ZONE_NORMAL(2)
 * 
 * @return pfn
 * 
 * - order必须小于MAX_ORDER
 */
uint64_t pmm_alloc_pages(uint8_t order, uint8_t zone) {
    // 检查zone和order是否合规
    if (order >= MAX_ORDER || zone > ZONE_NORMAL) {
        return 0;
    }

    uint64_t pfn = 0;
    uint8_t find_order = 0;
    bool find = false;
    uint64_t flags;

    spin_lock_irqsave(&mem_block->lock, &flags);

    // 检查zone是否有内存
    if (zones[zone].start_pfn >= zones[zone].end_pfn) {
        spin_unlock_irqrestore(&mem_block->lock, flags);
        return 0;
    }

    spin_lock(&zones[zone].lock);

    /*
     * 寻找空闲伙伴块
     * 如果当前order没有空闲块
     * 会一直向上寻找
     */
    for (uint8_t current_order = order; current_order < MAX_ORDER; current_order++) {
        struct list_head *head = &zones[zone].free_areas[current_order].head;

        if (list_empty(head)) {
            continue;  // 当前oarder没有空闲块
        } else {
            // 找到空闲块保存信息退出循环
            struct list_head *first = head->next;
            pfn = LINEAR_TO_PHYS((uintptr_t)first) >> PAGE_SHIFT;
            
            if (mem_block->blocks[pfn].is_free == 0 ||
                mem_block->blocks[pfn].order != current_order ||
                mem_block->blocks[pfn].zone != zone) {
                continue;
            }
            
            find_order = current_order;
            find = true;
            break; 
        }
    }
    
    if (find) {
        /*
         * 如果找到的order比需要的等级高
         * 就拆分到需要的大小
         * 因为拆分函数返回的都是左伙伴
         * 所以pfn不变
         */
        for (uint8_t current_order = find_order; current_order > order; current_order--) {
            free_list_t *split = split_buddy_block(pfn);

            if (split == NULL) {
                pfn = 0;
                break;
            }
        }

        // 分配
        if (pfn != 0) {
            remove_free_lists(pfn);

            uint64_t block_pages = 1ULL << order;
            for (uint64_t i = 0; i < block_pages; i++) {
                mem_block_t* block = &mem_block->blocks[pfn + i];
                block->is_head = (i == 0) ? 1 : 0;
                block->is_free = 0;
                block->ref_count = 1;
            }
        }
    }

    spin_unlock(&zones[zone].lock);
    spin_unlock_irqrestore(&mem_block->lock, flags);

    if (pfn == 0) {
        return 0;
    }

    // 清零内存块
    uint64_t *alloc_mem_block_ptr = (uint64_t *)PHYS_TO_LINEAR(pfn << PAGE_SHIFT);
    for (uint64_t i = 0;i < (PAGE_SIZE * (1 << order));i += sizeof(uint64_t)) {
        alloc_mem_block_ptr[i >> 3] = 0;
    }

    return pfn;
}

/**
 * 释放内存
 * 
 * @param pfn 被释放的伙伴块的页帧号
 */
void pmm_free_pages(uint64_t pfn) {
    check_pfn_valid(pfn);
    uint64_t flags;
    /*
     * 获取pfn的zone
     * 获取zone不需要锁
     * 因为zone在初始化后不变
     * 所以不会缓存不一致
     */
    uint8_t zone = mem_block->blocks[pfn].zone;

    spin_lock_irqsave(&mem_block->lock, &flags);
    spin_lock(&zones[zone].lock);

    mem_block_t* block = &mem_block->blocks[pfn];
    
    if (block->is_head == 0 || block->is_free == 1) {
        spin_unlock(&zones[zone].lock);
        spin_unlock_irqrestore(&mem_block->lock, flags);
        return;
    }
    
    uint8_t order = block->order;
    uint16_t order_size = 1 << order;
    
    for (uint16_t i = 0; i < order_size; i++) {
        mem_block_t* current = &mem_block->blocks[pfn + i];
        if (current->ref_count > 0) {
            current->ref_count--;
        }
    }
    /*
     * 引用计数大于0
     * 说明还在被使用
     * 不应该释放
     */
    if (block->ref_count > 0) {
        spin_unlock(&zones[zone].lock);
        spin_unlock_irqrestore(&mem_block->lock, flags);
        return;
    }
    
    free_list_t *addr = (free_list_t *)PHYS_TO_LINEAR(pfn * PAGE_SIZE);
    
    uint64_t block_pages = 1ULL << order;
    for (uint64_t i = 0; i < block_pages; i++) {
        mem_block->blocks[pfn + i].is_free = 1;
    }
    
    add_free_lists(addr, zone, order);
    
    /*
     * 尝试合并伙伴块
     * 直接计算伙伴pfn并通过mem_block检查状态
     */
    {
        uint64_t current_pfn = pfn;
        uint8_t current_order = order;
        
        while (current_order < MAX_ORDER - 1) {
            uint64_t buddy_pfn = current_pfn ^ (1ULL << current_order);
            
            if (buddy_pfn <= max_pfn &&
                mem_block->blocks[buddy_pfn].is_free &&
                mem_block->blocks[buddy_pfn].order == current_order &&
                mem_block->blocks[buddy_pfn].zone == zone) {
                
                free_list_t *merged_node = merge_buddy_block(current_pfn, buddy_pfn);
                if (merged_node != NULL) {
                    current_pfn = current_pfn < buddy_pfn ? current_pfn : buddy_pfn;
                    current_order++;
                    continue;
                }
            }
            break;
        }
    }
    
    spin_unlock(&zones[zone].lock);
    spin_unlock_irqrestore(&mem_block->lock, flags);

    return;
}

/*
 * 增加内存块引用计数
 * @param pfn 要增加引用计数的页帧号
 */
void pmm_add_ref_count(uint64_t pfn) {
    check_pfn_valid(pfn);
    uint64_t flags;
    spin_lock_irqsave(&mem_block->lock, &flags);
    
    mem_block_t* block = &mem_block->blocks[pfn];
    uint8_t order = block->order;
    uint64_t block_size = 1 << order;
    
    // 增加所有页的引用计数
    for (uint64_t i = 0; i < block_size; i++) {
        mem_block_t* current = &mem_block->blocks[pfn + i];
        current->ref_count++;
    }
    
    spin_unlock_irqrestore(&mem_block->lock, flags);
}

/*
 * 增加内存块映射计数
 *
 * @param pfn 要增加映射计数的页帧号
 */
void pmm_add_map_count(uint64_t pfn) {
    check_pfn_valid(pfn);
    uint64_t flags;
    spin_lock_irqsave(&mem_block->lock, &flags);
    
    mem_block_t* block = &mem_block->blocks[pfn];
    uint8_t order = block->order;
    uint64_t block_size = 1 << order;
    
    // 增加所有页的映射计数
    for (uint64_t i = 0; i < block_size; i++) {
        mem_block_t* current = &mem_block->blocks[pfn + i];
        current->map_count++;
    }
    
    spin_unlock_irqrestore(&mem_block->lock, flags);
}

/*
 * 减少内存块映射计数
 *
 * @param pfn 要减少映射计数的页帧号
 */
void pmm_sub_map_count(uint64_t pfn) {
    check_pfn_valid(pfn);
    uint64_t flags;
    spin_lock_irqsave(&mem_block->lock, &flags);
    
    mem_block_t* block = &mem_block->blocks[pfn];
    uint8_t order = block->order;
    uint64_t block_size = 1 << order;
    
    // 减少所有页的映射计数
    for (uint64_t i = 0; i < block_size; i++) {
        mem_block_t* current = &mem_block->blocks[pfn + i];
        
        if (current->map_count > 0) {
            current->map_count--;
        }
    }
    
    spin_unlock_irqrestore(&mem_block->lock, flags);
}

/*
 * 清零内存块映射计数
 * 
 * @param pfn 要清零映射计数的页帧号
 */
void pmm_zero_map_count(uint64_t pfn) {
    check_pfn_valid(pfn);
    uint64_t flags;
    spin_lock_irqsave(&mem_block->lock, &flags);
    
    mem_block_t* block = &mem_block->blocks[pfn];
    uint8_t order = block->order;
    uint64_t block_size = 1 << order;
    
    // 清零所有页的映射计数
    for (uint64_t i = 0; i < block_size; i++) {
        mem_block_t* current = &mem_block->blocks[pfn + i];
        current->map_count = 0;
    }
    
    spin_unlock_irqrestore(&mem_block->lock, flags);
}

/*
 * 设置页表页的上层页表项指针
 *
 * @param pfn 页表页的页帧号
 * @param ptr 上层页表项的虚拟地址
 */
void pmm_set_on_pte_ptr(uint64_t pfn, uintptr_t ptr) {
    check_pfn_valid(pfn);
    uint64_t flags;
    spin_lock_irqsave(&mem_block->lock, &flags);
    
    mem_block_t* block = &mem_block->blocks[pfn];
    uint8_t order = block->order;
    uint64_t block_size = 1ULL << order;
    
    // 设置块内所有页的on_pte_ptr
    for (uint64_t i = 0; i < block_size; i++) {
        mem_block_t* current = &mem_block->blocks[pfn + i];
        current->on_pte_ptr = ptr;
    }
    
    spin_unlock_irqrestore(&mem_block->lock, flags);
}

/*
 * 获取页表页的上层页表项指针
 *
 * @param pfn 页表页的页帧号
 * @return 上层页表项的虚拟地址
 */
uintptr_t pmm_get_on_pte_ptr(uint64_t pfn) {
    check_pfn_valid(pfn);
    uint64_t flags;
    spin_lock_irqsave(&mem_block->lock, &flags);
    uintptr_t ptr = mem_block->blocks[pfn].on_pte_ptr;
    spin_unlock_irqrestore(&mem_block->lock, flags);
    return ptr;
}

// 获取物理内存总页数
uint64_t pmm_max_page(void) {
    return max_pfn + 1;
}

void pmm_init(void) {
    PMM_PRINT("Initializing physical memory manager\n");
    
    calculate_max_pfn();  
    printk("[PMM] Physical memory: %luMB detected\n", (max_pfn + 1) * PAGE_SIZE / (1024 * 1024));
    
    alloc_bitmap_init();
    
    zone_init();

    alloc_mem_block();
    
    print_zone_info();
    
    free_lists_init();

    mem_block_init();
    
    uint64_t total_free_pages = calculate_total_free_pages();
    
    printk("[PMM] Buddy system initialized: %luMB free\n", total_free_pages * PAGE_SIZE / (1024 * 1024));
}