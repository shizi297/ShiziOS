/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <mm/pmm/buddy.h>
#include <mm/pmm/pmm.h>

// 计算要分配的内存大小属于哪个order
static inline uint8_t size_to_order(uint64_t size) {
    // 计算页数量
    uint64_t page_count = (size + PAGE_SIZE - 1)/PAGE_SIZE;
    uint8_t order = 0;

    if (page_count <= 1) return 0;

    /*
     * 计算属于哪个order前
     * 需要先-1
     * 
     * 因为在size刚好是2的幂次方时
     * 向上取整时会多算
     * size-1可以确保我们不会多算
     */
    page_count--;

    // 统计右移多少次值会为0
    while (page_count > 0) {
        page_count >>= 1;
        order++;
    }

    return order;
}

/**
 * 内核堆分配
 * 
 * @param size 要分配的内存大小(字节)
 * @param zone 内存区域
 * 
 * @return 成功：pfn
 * @return 失败：0
 */
uint64_t _kheap_alloc(uint64_t size, uint8_t zone) {
    // 确保传入的size是有效的
    if (size == 0) return 0; 

    uint8_t order = size_to_order(size);
    uint64_t pfn = 0;

    // 需要的order太大了
    if (order >= MAX_ORDER) return 0;

    /*
     * 查找每个zone
     * 检查是否有我们需要的order
     * 没有就自动向下查找
     * 
     * 用int16防止溢出
     */
    for (int16_t current_zone = zone;current_zone >= ZONE_DMA;current_zone--) {
        uint64_t alloc = pmm_alloc_pages(order, current_zone);

        // 分配成功
        if (alloc != 0) {
            pfn = alloc;
            break;
        } 
    }
    
    return pfn;
}

/**
 * 释放内核堆内存
 * 
 * @param pfn 被释放的伙伴块的页帧号
 */
void kheap_free(uint64_t pfn) {
    pmm_free_pages(pfn);
}