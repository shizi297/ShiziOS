/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <mm/vmm_mmu.h>
#include <mm/vmm_types.h>
#include <spinlock.h>
#include <list.h>
#include <libtree.h>
#include <klibc.h>
#include <vfs.h>

// vma结构体
typedef struct vm_area {
    uintptr_t start;      // 虚拟起始地址
    uintptr_t end;        // 虚拟结束地址
    uintptr_t linear_addr; // 映射区的线性虚拟地址（PHYS_TO_LINEAR(phys)），0 表示未预分配
    vm_prot_t prot;       // 权限标志
    uint8_t flags;      // 映射标志

    int64_t offset_or_anon; // >=0 表示文件偏移，<0 表示匿名映射
    union {
        struct file *file;
        struct anon_vma *anon_vma;
    };

    page_table_blocks_struct page_table_blocks; // 页表块信息

    struct list_head list_node;
    struct rbtree_node rb_node;
} vm_area_t;

// 地址空间结构体
typedef struct vmm_as {
    uintptr_t pgd;
    struct list_head vma_list;
    struct rbtree vma_tree;
    atomic_int refcount;
    spinlock_t lock;
} as_t;

// 占位
typedef struct anon_vma {
    struct anon_vma *next;
    uint32_t refcount;
} anon_vma_t;

// 获取 vma 的起止地址
static inline void vma_range(const vm_area_t *vma, uintptr_t *out_start, uintptr_t *out_end) {
    if (out_start) *out_start = vma->start;
    if (out_end) *out_end = vma->end;
}

// 设置 vma 的线性地址和页表块
static inline void vma_set_map(vm_area_t *vma, uintptr_t linear, const page_table_blocks_struct *ptb) {
    vma->linear_addr = linear;
    vma->page_table_blocks = *ptb;
}

// 获取地址空间锁
void as_lock(as_t *as);

// 释放地址空间锁
void as_unlock(as_t *as);

/*
 * 添加 vma 到地址空间
 *
 * @param as 进程地址空间
 * @param start 起始虚拟地址（页对齐）
 * @param end 结束虚拟地址（页对齐）
 * @param prot 权限标志
 * @param flags 映射标志
 */
int vma_add(
    as_t *as,
    uintptr_t start,
    uintptr_t end,
    vm_prot_t prot,
    uint8_t flags
);

/*
 * 读取 VMA 中一个页面的内容到目标内存地址
 *
 * @param vma VMA 指针
 * @param vaddr 要读取的虚拟地址（必须位于 vma 范围内）
 * @param dest 目标内存地址（用于存放读取的数据）
 */
int vma_read_page(vm_area_t *vma, uintptr_t vaddr, void *dest);

/*
 * 查找包含 addr 的 vma
 *
 * @param as 进程地址空间
 * @param addr 虚拟地址
 *
 * @return vma 指针
 */
vm_area_t *vma_find(as_t *as, uintptr_t addr);

/*
 * 从地址空间中移除 vma
 *
 * @param as 进程地址空间
 * @param vma 要移除的 vma 指针
 */
int vma_remove(as_t *as, vm_area_t *vma);

/*
 * 为地址空间分配一个新的虚拟地址
 *
 * @param as 进程地址空间
 * @param size 需要的大小（字节）
 *
 * @return 分配的虚拟地址
 */
uintptr_t as_alloc_addr(as_t *as, uint64_t size);

/*
 * 解除映射
 *
 * @param as 进程地址空间
 * @param addr 虚拟地址
 */
int as_unmap(as_t *as, uintptr_t addr);

/*
 * 创建进程地址空间描述符
 *
 * @param pgd 页全局目录物理地址
 *
 * @return 进程地址空间的虚拟地址
 */
as_t *as_create(uintptr_t pgd);

/*
 * 销毁进程地址空间（减少引用计数，归零时释放）
 *
 * @param as 进程地址空间的虚拟地址
 *
 * 调用前需持有 as 锁
 */
void as_destroy(as_t *as);

// 增加引用计数
void as_add_ref(as_t *as);

// 获取当前引用计数
int as_get_ref(as_t *as);

// 获取页全局目录物理地址
uintptr_t as_get_pgd(as_t *as);