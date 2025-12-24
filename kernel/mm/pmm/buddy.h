/* SPDX-License-Identifier: Apache-2.0 */
 
#ifndef BUDDY_H
#define BUDDY_H

void pmm_init(void);

/**
 * 分配伙伴块
 * 
 * @param order 分配的伙伴块大小
 * @param zone  首选内存区域：ZONE_DMA(0)、ZONE_DMA32(1)、ZONE_NORMAL(2)
 * @return 成功：pfn；失败：0
 * 
 * 
 * - order必须小于MAX_ORDER
 */
uint64_t pmm_alloc_pages(uint8_t order, uint8_t zone);

/**
 * 释放内存
 * 
 * @param pfn 被释放的伙伴块的页帧号
 */
void pmm_free_pages(uint64_t pfn);

#endif 