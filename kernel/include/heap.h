/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <mm/pmm/pmm.h>
#include <mm/vmm/vmm_map.h>

/**
 * 内核堆分配
 * 
 * @param size 要分配的内存大小(字节)
 * @param zone 内存区域
 * 
 * @return 成功：虚拟地址
 * @return 失败：NULL
 */
void* _kheap_alloc(uint64_t size, uint8_t zone);

// 默认使用正常内存区域
#define kheap_alloc(size) _kheap_alloc((size), ZONE_NORMAL)

/**
 * 释放内核堆内存
 * 
 * @param vaddr 被释放的伙伴块的虚拟地址
 */
void kheap_free(void *vaddr);

/**
 * 增加内核堆内存引用计数
 * 
 * @param vaddr 要增加引用计数的虚拟地址
 */
void kheap_add_ref_count(void *vaddr);

/**
 * 增加内核堆内存映射引用计数
 * 
 * @param vaddr 要增加映射引用计数的虚拟地址
 */
void kheap_add_map_count(void *vaddr);

/**
 * 减少内核堆内存映射映射计数
 * 
 * @param vaddr 要减少映射引用计数的虚拟地址
 */
void kheap_sub_map_count(void *vaddr);

/**
 * 清零内核堆的映射计数
 * 
 * @param vaddr 要清零映射计数的虚拟地址
 */
void kheap_zero_map_count(void *vaddr);

/*
 * 设置页表页的上层页表项指针
 *
 * @param vaddr 页表页的虚拟地址
 * @param pte_ptr 上层页表项的虚拟地址
 */
void kheap_set_on_pte_ptr(void *vaddr, uintptr_t pte_ptr);

/*
 * 获取页表页的上层页表项指针
 *
 * @param vaddr 页表页的虚拟地址
 * @return 上层页表项的虚拟地址
 */
uintptr_t kheap_get_on_pte_ptr(void *vaddr);

/**
 * 虚拟堆分配
 * 
 * @param as 进程地址空间描述符
 * @param addr 期望的虚拟地址(如果为NULL则由系统自动分配)
 * @param size 要分配的内存大小(字节)
 * @param prot 内存属性
 * @param flags 分配标志
 * @param fd 文件描述符(如果映射文件则需要传入)
 * @param offset 文件偏移(如果映射文件则需要传入)
 * @param alloc 是否分配物理内存
 * 
 * @return 成功：虚拟地址
 * @return 失败：NULL
 * 
 * 如果addr不对齐则会自动对齐PAGE_SIZE
 * 
 * 目前fd和offset没有用
 * 所以直接传0即可
 * flags目前也没有用
 * 也可以直接传0
 */
void *vheap_alloc(as_t *as, void *addr, size_t size, vm_prot_t prot, uint8_t flags, int fd , uint64_t offset, bool alloc);

/**
 * 释放虚拟堆内存
 * 
 * @param as 进程地址空间描述符
 * @param addr 被释放的虚拟地址
 * @param size 被释放的内存大小(字节)
 * 
 * size传0表示释放整个映射区域
 * 目前size没有用
 * 所以直接传0即可
 */
void vheap_free(as_t *as, void *addr, size_t size);

/**
 * 释放进程所有虚拟堆内存
 * 
 * @param as 进程地址空间描述符
 */
void vheap_free_all(as_t *as);

/**
 * 映射物理地址到mmio
 * 
 * @param phy 物理地址
 * @param len 要映射的大小
 * 
 * @return 成功：映射到的虚拟地址
 * @return 失败：NULL
 */
void *vheap_map_mmio(uint64_t phy_addr, uint64_t len);

/*
 * 创建一个新的进程地址空间
 * 自动分配页全局目录
 * 
 * @return 失败：NULL
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *vheap_create_as(void);

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vheap_destroy_as(as_t *as);