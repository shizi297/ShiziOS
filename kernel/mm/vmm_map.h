/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "vmm_types.h"
#include "vmm_as.h"
#include <vfs.h>

#define TLB_FLUSH_THRESHOLD_PAGES 32

// 初始化
void vmm_init(void);

// 获取内核地址空间
as_t *vmm_get_kernel_as(void);

// 获取内核的页全局目录
uintptr_t vmm_get_kernel_pgd(void);

/*
 * 处理缺页异常
 *
 * @param as 进程地址空间
 * @param fault_addr 触发缺页的虚拟地址
 * @param access_flags 访问类型
 */
int vmm_handle_fault(as_t *as, uintptr_t fault_addr, uint32_t access_flags);

/*
 * 创建一个新的进程地址空间（自动分配页全局目录）
 *
 * @return 进程地址空间的虚拟地址
 */
as_t *vmm_create_as(void);

/*
 * 解除映射
 *
 * @param as 进程地址空间
 * @param addr 虚拟地址
 */
int vmm_unmap(as_t *as, uintptr_t addr);

/*
 * 切换到指定的进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vmm_switch_as(as_t *as);

/**
 * 映射mmio地址
 *
 * @param phy_addr 物理地址
 * @param page_count 大小
 *
 * @return 映射的虚拟内存
 */
uintptr_t vmm_map_mmio(uint64_t phy_addr, uint64_t page_count);

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vmm_destroy_as(as_t *as);

// 增加as的引用计数
void vmm_as_add_ref(as_t *as);

/**
 * 复制地址空间
 * 自动分配新的页表页与物理内存
 *
 * @param as 进程地址空间的虚拟地址
 *
 * @return 进程地址空间的虚拟地址
 */
as_t *vmm_copy_as(as_t *as);

/*
 * 映射匿名内存区域
 *
 * @param as 进程地址空间
 * @param addr 要映射的虚拟地址，如果为0则自动分配
 * @param page 映射页数
 * @param prot 映射权限
 * @param flags 映射标志（当前必须传 0）
 * @param anon_vma 匿名内存结构体指针（目前未使用，可传NULL）
 * @param alloc 是否预分配
 *
 * @return 映射的虚拟地址
 */
kuptr vmm_map_anon(
    as_t *as,
    uintptr_t addr,
    uint64_t page,
    vm_prot_t prot,
    uint8_t flags,
    anon_vma_t *anon_vma,
    bool alloc
);

/*
 * 映射文件到进程地址空间
 *
 * @param as 进程地址空间
 * @param addr 建议的虚拟地址（0 表示自动分配）
 * @param size 映射大小（字节）
 * @param prot 内存权限
 * @param flags 映射标志（当前必须传 0）
 * @param path 文件路径
 * @param offset 文件内偏移（必须页对齐）
 * @param pwd 当前工作目录
 *
 * @return 映射的虚拟地址
 */
kuptr vmm_map_file(
    as_t *as,
    uintptr_t addr,
    uint64_t size,
    vm_prot_t prot,
    uint8_t flags,
    const char *path,
    uint64_t offset,
    const struct path *pwd
);