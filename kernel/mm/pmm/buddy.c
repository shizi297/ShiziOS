/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <bootboot.h>
#include <mm/bootmem/boot_allot.h>
#include <serial.h>
#include <spinlock.h>
#include <stddef.h>
#include "pmm.h"



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
    size_t alloc_bitmap_size = ((max_pfn + 1) + (PAGE_SIZE * 8 - 1)) / (PAGE_SIZE * 8);
    
    void* alloc_bitmap = boot_alloc(alloc_bitmap_size);
    
    size_t qword_count = (alloc_bitmap_size * PAGE_SIZE) / sizeof(uint64_t);
    bitmap64 = (uint64_t*)alloc_bitmap;
    
    void* boot_bitmap = boot_alloc_get_bitmap();
    
    // 初始化位图，将所有位设置为1（默认已分配）
    for (size_t i = 0; i < qword_count; i++) {
        bitmap64[i] = 0xFFFFFFFFFFFFFFFF;
    }
    
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
            if (end_pfn > max_pfn + 1) end_pfn = max_pfn + 1;
            
            for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++) {
                size_t index = pfn / 64;           
                size_t bit = pfn % 64;             
                
                bitmap64[index] &= ~(1ULL << bit);
            }
        }
    }
    
    /*
     * 复制早期位图分配情况
     * 因为早期位图存储了一些mmap没有记录的内存
     */
    if (boot_bitmap) {
        for (size_t pfn = 0; pfn < BOOT_ALLOC_MAX_PAGES; pfn++) {
            size_t boot_byte_index = pfn / 8;
            size_t boot_bit_index = pfn % 8;
            uint8_t boot_byte = ((uint8_t*)boot_bitmap)[boot_byte_index];
            bool is_allocated = (boot_byte >> boot_bit_index) & 1;
            
            size_t index = pfn / 64;
            size_t bit = pfn % 64;
            uint64_t mask = 1ULL << bit;
            
            if (is_allocated) {
                bitmap64[index] |= mask;
            } else {
                bitmap64[index] &= ~mask;
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
            size_t index = pfn / 64;           
            size_t bit = pfn % 64;             
            
            bitmap64[index] &= ~(1ULL << bit);
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
        size_t index = i / 64;  
        size_t bit = i % 64;    
        
        if ((bitmap64[index] & (1ULL << bit)) == 0) {
            if (consecutive == 0) {
                start_pfn = i;
            }
            consecutive++;
            
            if (consecutive == pages) {
                for (uint64_t j = start_pfn; j < start_pfn + pages; j++) {
                    size_t idx = j / 64;
                    size_t b = j % 64;
                    bitmap64[idx] |= (1ULL << b);
                }
                return PHYS_TO_LINEAR(start_pfn * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }
    
    panic("[PMM] ERROR: Cannot allocate required memory for system initialization\n");
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
            zones[i].free_areas[j].head = NULL;
        }
    }
}

/**
 * 添加新内存块到空闲链表
 * 
 * @param free_lisr 要添加的伙伴块的虚拟地址
 * @param zone_count 伙伴块属于的zone区域
 * @param order_count 伙伴块属于的order区域
 * 
 * 调用时需要zone.lock锁
 * 因为访问了空闲链表
 */ 
static void add_free_lists(free_list_t *free_list, uint8_t zone_count, uint8_t order_count) {
    free_area_t *free_area = &zones[zone_count].free_areas[order_count];
    
    free_list->prev = NULL;
    free_list->next = NULL;
    
    if (free_area->head == NULL) {
        free_area->head = free_list;
    } else {
        free_list_t *current = free_area->head;
        free_list_t *prev = NULL;
        
        // 按地址顺序查找插入位置
        while (current != NULL && (uintptr_t)current < (uintptr_t)free_list) {
            prev = current;
            current = current->next;
        }
        
        if (prev == NULL) {
            free_list->next = free_area->head;
            free_area->head->prev = free_list;
            free_area->head = free_list;
        } else if (current == NULL) {
            prev->next = free_list;
            free_list->prev = prev;
        } else {
            prev->next = free_list;
            free_list->prev = prev;
            free_list->next = current;
            current->prev = free_list;
        }
    }
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
    free_list_t *node = (free_list_t *)PHYS_TO_LINEAR(phys_addr);
    uint8_t zone = mem_block->blocks[pfn].zone;
    uint8_t order = mem_block->blocks[pfn].order;

    //pfn不在任何zone范围内
    if (zone == 0xFF) {
        return;
    }
    
    free_list_t *next = node->next;
    free_list_t *prev = node->prev;

    if (node->prev == NULL) {
        zones[zone].free_areas[order].head = next;
    } else {
        prev->next = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }
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
 * 成功：返回合并后块的虚拟地址（free_list_t*）
 * 失败：返回NULL
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
    size_t index = pfn / 64;
    size_t bit = pfn % 64;
    return (bitmap64[index] >> bit) & 1ULL;
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
        
        for (int order = 0; order < MAX_ORDER; order++) {
            zone->free_areas[order].head = NULL;
        }
        
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
    serial_puts("[PMM] Zone DMA: 0-16MB");
    
    uint64_t total_memory = (max_pfn + 1) * PAGE_SIZE;
    uint64_t total_mb = total_memory / (1024 * 1024);
    
    if (total_memory > (4ULL * 1024 * 1024 * 1024)) {
        serial_puts(", DMA32: 16MB-4GB, NORMAL: 4GB-");
        serial_put_dec(total_mb);
        serial_puts("MB");
    } else {
        serial_puts(", DMA32: 16MB-");
        serial_put_dec(total_mb);
        serial_puts("MB");
    }
    
    serial_puts("\n");
}

//计算总空闲内存
static uint64_t calculate_total_free_pages(void) {
    uint64_t total_free_pages = 0;
    
    for (int z = ZONE_DMA; z <= ZONE_NORMAL; z++) {
        for (int o = 0; o < MAX_ORDER; o++) {
            free_list_t* node = zones[z].free_areas[o].head;  
            while (node) {
                total_free_pages += (1ULL << o);
                node = node->next;
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
            for (free_list_t *free_lists_ptr = zones[zone_id].free_areas[order_id].head; 
                 free_lists_ptr != NULL; 
                 free_lists_ptr = free_lists_ptr->next) {
                
                uintptr_t phys_addr = LINEAR_TO_PHYS((uintptr_t)free_lists_ptr);
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
 * @return 成功：pfn；失败：0
 * 
 * 
 * - order必须小于MAX_ORDER
 * 
 * 1. 在指定zone的对应order链表中查找空闲块
 * 2. 若找不到，尝试更高order（拆分）
 * 3. 若zone不足，回退到上一个zone
 * 4. 更新位图和mem_block元数据
 */
uint64_t pmm_alloc_pages(uint8_t order, uint8_t zone) {
    // 检查zone和order是否合规
    if (order >= MAX_ORDER || zone > ZONE_NORMAL) {
        return 0;
    }

    uint64_t pfn = 0;
    uint8_t find_order = 0;
    bool find = false;

    spin_lock(&mem_block->lock);

    // 检查zone是否有内存
    if (zones[zone].start_pfn >= zones[zone].end_pfn) {
        spin_unlock(&mem_block->lock);
        return 0;
    }

    spin_lock(&zones[zone].lock);

    /*
     * 寻找空闲伙伴块
     * 如果当前order没有空闲块
     * 会一直向上寻找
     */
    for (uint8_t current_order = order; current_order < MAX_ORDER; current_order++) {
        free_list_t *head = zones[zone].free_areas[current_order].head;

        if (head == NULL) {
            continue;  // 当前oarder没有空闲块
        } else {
            // 找到空闲块保存信息退出循环
            pfn = LINEAR_TO_PHYS((uintptr_t)head) >> PAGE_SHIFT;
            
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
    spin_unlock(&mem_block->lock);

    return pfn;
}

/**
 * 释放内存
 * 
 * @param pfn 被释放的伙伴块的页帧号
 * 
 * 释放后会尝试合并伙伴
 * 只要有一次合并成功
 * 就继续向上尝试合并
 * 直到到达MAX_ORDER或者没有伙伴块
 */
void pmm_free_pages(uint64_t pfn) {
    /*
     * 获取pfn的zone
     * 获取zone不需要锁
     * 因为zone在初始化后不变
     * 所以不会缓存不一致
     */
    uint8_t zone = mem_block->blocks[pfn].zone;

    spin_lock(&mem_block->lock);
    spin_lock(&zones[zone].lock);

    mem_block_t* block = &mem_block->blocks[pfn];
    
    if (block->is_head == 0 || block->is_free == 1) {
        spin_unlock(&mem_block->lock);
        spin_unlock(&zones[zone].lock);
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
        spin_unlock(&mem_block->lock);
        spin_unlock(&zones[zone].lock);
        return;
    }
    
    
    free_list_t *addr = (free_list_t *)PHYS_TO_LINEAR(pfn * PAGE_SIZE);
    add_free_lists(addr, zone, order);
    
    /*
     * 尝试合并伙伴块
     * 检测节点是左伙伴还是右伙伴
     * 
     * 如果是左伙伴
     * 就计算右伙伴的pfn
     * 检测右伙伴的pfn是否和前驱指针的pfn相同
     * 如果相同就合并
     * 不相同就继续检查
     * 
     * 如果是右伙伴
     * 就计算左伙伴的pfn
     * 检测左伙伴的pfn是否和后继指针的pfn相同
     * 如果相同就合并
     * 不相同就退出循环
     */ 
    {
        free_list_t *current_node = addr;
        uint64_t current_pfn = pfn;
        uint8_t current_order = order;
        bool can_merge = true;  
        
        while (can_merge && current_order < MAX_ORDER - 1) {
            can_merge = false;  // 默认下次不合并
            
            // 判断当前节点是左伙伴还是右伙伴
            bool is_left = ((current_pfn & (1ULL << current_order)) == 0);
            uint64_t buddy_pfn;
            
            if (is_left) {
                // 当前是左伙伴，计算右伙伴的pfn
                buddy_pfn = current_pfn + (1ULL << current_order);
                
                // 检查后继节点是否存在且是当前节点的右伙伴
                if (current_node->next != NULL) {
                    uint64_t next_pfn = LINEAR_TO_PHYS((uintptr_t)current_node->next) >> PAGE_SHIFT;
                    if (next_pfn == buddy_pfn) {
                        // 合并
                        free_list_t *merged_node = merge_buddy_block(current_pfn, buddy_pfn);
                        if (merged_node != NULL) {
                            // 合并成功，更新当前节点和状态
                            current_node = merged_node;
                            current_pfn = LINEAR_TO_PHYS((uintptr_t)merged_node) >> PAGE_SHIFT;
                            current_order = mem_block->blocks[current_pfn].order;
                            can_merge = true;   // 可以继续向上合并
                            continue;
                        }
                    }
                }
            } else {
                // 当前是右伙伴，计算左伙伴的pfn
                buddy_pfn = current_pfn - (1ULL << current_order);
                
                // 检查前驱节点是否存在且是当前节点左伙伴
                if (current_node->prev != NULL) {
                    uint64_t prev_pfn = LINEAR_TO_PHYS((uintptr_t)current_node->prev) >> PAGE_SHIFT;
                    if (prev_pfn == buddy_pfn) {
                        // 执行合并
                        free_list_t *merged_node = merge_buddy_block(buddy_pfn, current_pfn);
                        if (merged_node != NULL) {
                            // 合并成功，更新当前节点和状态
                            current_node = merged_node;
                            current_pfn = LINEAR_TO_PHYS((uintptr_t)merged_node) >> PAGE_SHIFT;
                            current_order = mem_block->blocks[current_pfn].order;
                            can_merge = true; // 可以继续向上合并
                            continue;
                        }
                    }
                }
            }
            
            // 如果无法合并，退出循环
        }
    }
    
    spin_unlock(&zones[zone].lock);
    spin_unlock(&mem_block->lock);

    return;
}

void pmm_init(void) {
    serial_puts("[PMM] Initializing physical memory manager\n");
    
    calculate_max_pfn();  
    serial_puts("[PMM] Physical memory: ");
    serial_put_dec((max_pfn + 1) * PAGE_SIZE / (1024 * 1024));
    serial_puts("MB detected\n");
    
    alloc_bitmap_init();
    
    zone_init();

    alloc_mem_block();
    
    print_zone_info();
    
    free_lists_init();

    mem_block_init();
    
    uint64_t total_free_pages = calculate_total_free_pages();
    
    serial_puts("[PMM] Buddy system initialized: ");
    serial_put_dec(total_free_pages * PAGE_SIZE / (1024 * 1024));
    serial_puts("MB free\n");
}