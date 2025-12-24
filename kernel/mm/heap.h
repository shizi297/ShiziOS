/* SPDX-License-Identifier: Apache-2.0 */

#ifndef HEAP_H
#define HEAD_H

#include <stdint.h>

/**
 * 内核堆分配
 * 
 * @param size 要分配的内存大小(字节)
 * @param zone 内存区域
 * 
 * @return 成功：pfn
 * @return 失败：0
 */
uint64_t _kheap_alloc(uint64_t size, uint8_t zone);

// 默认使用正常内存区域
#define kheap_alloc(size) _kheap_alloc((size), ZONE_NORMAL)

/**
 * 释放内核堆内存
 * 
 * @param pfn 被释放的伙伴块的页帧号
 */
void kheap_free(uint64_t pfn);

#endif // HEAD_H