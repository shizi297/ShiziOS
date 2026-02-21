/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "vmm_types.h"
#include "vmm_as.h"

#define TLB_FLUSH_THRESHOLD_PAGES 32

// 初始化
void vmm_init(void);

/*
 * 创建一个新的进程地址空间
 * 自动分配页全局目录
 * 
 * @return 失败：NULL
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *vmm_create_as(void);

/*
 * 释放内存
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要释放的虚拟地址
 * 
 * @return vmm_result_t
 * 
 * 调用时需要加as锁
 */
vmm_result_t vmm_unmap_nolock(as_t *as, uintptr_t addr);

/*
 * 释放内存
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要释放的虚拟地址
 * 
 * @return vmm_result_t
 */
vmm_result_t vmm_unmap(as_t *as, uintptr_t addr);

/*
 * 切换到指定的进程地址空间
 * 
 * @param as 进程地址空间的虚拟地址
 */
void vmm_switch_as(as_t *as);

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vmm_destroy_as(as_t *as);

/*
 * 映射匿名内存区域
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要映射的虚拟地址，如果为0则自动分配
 * @param page 映射页数
 * @param prot 映射权限
 * @param flags 映射标志
 * @param anon_vma 匿名内存结构体指针
 * @param alloc 是否预分配
 * @param out_addr 输出的实际映射地址，如果不需要可以传入NULL
 * 
 * anon_vma目前没有用
 * 可以先传入NULL
 * 
 * @return vmm_result_t
 */
vmm_result_t vmm_map_anon(
    as_t *as, 
    uintptr_t addr, 
    uint64_t page, 
    vm_prot_t prot, 
    uint8_t flags, 
    anon_vma_t *anon_vma, 
    bool alloc,
    uintptr_t *out_addr
);

/*
 * 映射文件到内存
 * 暂时不支持
 * 
 * vmm_result_t vmm_map_file(){}
 */

/*
 * 映射设备内存
 * 暂时不支持
 * 
 * vmm_result_t vmm_map_device(){}
 */