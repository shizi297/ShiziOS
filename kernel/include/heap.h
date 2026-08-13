/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#define NO_PMM
#include <stdint.h>
#include <stdbool.h>
#include <mm/vmm_types.h>
#include <mm/pmm.h>
#include <vfs.h>

typedef struct vmm_as as_t;

/**
 * 内核堆分配
 *
 * @param size 要分配的内存大小(字节)
 * @param zone 内存区域
 *
 * @return 虚拟地址
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

// 获取内存总页数
uint64_t kheap_max_page(void);

/*
 * 处理缺页异常
 *
 * @param as 进程地址空间
 * @param fault_addr 触发缺页的虚拟地址
 * @param access_flags 访问类型
 */
int vheap_handle_fault(as_t *as, uintptr_t fault_addr, uint32_t access_flags);

/**
 * 虚拟堆分配（匿名映射）
 *
 * @param as 进程地址空间描述符
 * @param addr 期望的虚拟地址(如果为NULL则由系统自动分配)
 * @param size 要分配的内存大小(字节)
 * @param prot 内存属性
 * @param flags 分配标志（预留扩展，当前必须传0）
 * @param alloc 是否分配物理内存
 *
 * @return 虚拟地址
 *
 * 如果addr不对齐则会自动对齐PAGE_SIZE
 */
void *vheap_alloc(
    as_t *as,
    void *addr,
    size_t size,
    vm_prot_t prot,
    uint8_t flags,
    bool alloc
);

/*
 * 虚拟堆分配（文件映射）
 *
 * @param as 进程地址空间描述符
 * @param addr 期望的虚拟地址(如果为NULL则由系统自动分配)
 * @param size 要映射的内存大小(字节)
 * @param prot 内存属性
 * @param flags 分配标志（预留扩展，当前必须传0）
 * @param path 文件路径
 * @param offset 文件内偏移（必须页对齐）
 * @param pwd 当前工作目录
 *
 * @return 虚拟地址
 *
 * 如果addr不对齐则会自动对齐PAGE_SIZE
 */
void *vheap_file_alloc(
    as_t *as,
    void *addr,
    size_t size,
    vm_prot_t prot,
    uint8_t flags,
    const char *path,
    uint64_t offset,
    const struct path *pwd
);

/**
 * 释放虚拟堆内存（整个映射区域）
 *
 * @param as 进程地址空间描述符
 * @param addr 被释放的虚拟地址
 */
void vheap_free(as_t *as, void *addr);

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
 * @return 虚拟地址
 */
void *vheap_map_mmio(uint64_t phy_addr, uint64_t len);

/**
 * 释放通过 MMIO 区域（调用者需要自己管理物理内存生命周期）
 *
 * @param virt_addr MMIO 的虚拟地址
 */
void vheap_unmap_mmio(void *virt_addr);

/*
 * 创建一个新的进程地址空间
 * 自动分配页全局目录
 *
 * @return 进程地址空间的虚拟地址
 */
as_t *vheap_create_as(void);

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vheap_destroy_as(as_t *as);

/*
 * 复制进程地址空间
 * 自动分配新的页表页与物理内存
 *
 * @param as 源地址空间
 *
 * @return 新的地址空间
 */
as_t *vheap_copy_as(as_t *as);

// 添加as的引用计数
void vheap_as_add_ref(as_t *as);

// 获取内核地址空间
as_t *vheap_get_kernel_as(void);

// 获取内核的页全局目录
uintptr_t vheap_get_kernel_pgd(void);