/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <mm/vmm/vmm_mmu.h>
#include <mm/vmm/vmm_types.h>
#include <spinlock.h>
#include <list.h>
#include <libtree.h>
#include <shizi/string.h>

/*
 * anon_vma_t
 * file_t device_t
 * vm_area_t
 * 目前只是占位
 * 只包含基础字段
 * 后面会添加字段
 * 以及添加与链接函数 
 */
typedef struct anon_vma {
    struct anon_vma *next;
    uint32_t refcount;       // 引用计数
} anon_vma_t;

typedef struct file {
    int flags;              // 文件标志
    void *private;          // 文件私有数据指针
    uint32_t refcount;      // 引用计数
} file_t;

typedef struct device {
    int major;              // 主设备号
    int minor;              // 次设备号
    unsigned int type;      // 设备类型
    uint32_t refcount;      // 引用计数
} device_t;

typedef struct vm_area {
    uintptr_t start;      // 虚拟起始地址
    uintptr_t end;        // 虚拟结束地址
    uintptr_t linear_addr; // 映射区的线性虚拟地址（PHYS_TO_LINEAR(phys)），0 表示未预分配
    vm_prot_t prot;       // 权限标志
    uint8_t flags;      // 映射标志
    anon_vma_t *anon_vma;  
    file_t *file;         
    device_t *device;  
    page_table_blocks_struct page_table_blocks; // 页表块信息

    // 链表节点，用于遍历所有vma节点
    struct list_head list_node;
    // 红黑树节点，用于找到其中的一个vma节点
    struct rbtree_node rb_node;
} vm_area_t;

typedef struct vmm_as{
    uintptr_t pgd;
    struct list_head vma_list;
    struct rbtree vma_tree;
    spinlock_t lock;
} as_t;

// vma中的数据
typedef struct vma_data {
    uintptr_t linear;
    uintptr_t start_addr;
    uintptr_t end_addr;
} vma_data_t;

/*
 * 用于返回vma的所有条目的虚拟地址
 * 包含用户虚拟地址和线性映射地址
 */
typedef struct vma_result {
    uint64_t addr_count;
    vma_data_t vma_data[];
} vma_result_t;

/*
 * VMA字段枚举
 * 用于通用读写接口
 */
typedef enum vma_field {
    VMA_FIELD_START,
    VMA_FIELD_END,
    VMA_FIELD_LINEAR,
    VMA_FIELD_PROT,
    VMA_FIELD_FLAGS,
} vma_field_t;

/*
 * 读取VMA指定字段的值
 *
 * @param vma VMA指针
 * @param field 要读取的字段
 * @param out_data 输出缓冲区，必须指向正确类型的变量
 *
 * @return VMM_OK 成功
 * @return VMM_INVALID_ARGUMENT 参数无效
 */
vmm_result_t vma_read(const vm_area_t *vma, vma_field_t field, void *out_data);

/*
 * 写入VMA指定字段的值（仅可写字段有效）
 *
 * @param vma VMA指针
 * @param field 要写入的字段
 * @param data 指向待写入数据的指针
 *
 * @return VMM_OK 成功
 * @return VMM_INVALID_ARGUMENT 参数无效或字段不可写
 */
vmm_result_t vma_write(vm_area_t *vma, vma_field_t field, const void *data);

/*
 * 获取VMA的页表块指针
 *
 * @param vma VMA指针
 *
 * @return 页表块结构体指针
 */
static inline page_table_blocks_struct *vma_get_ptb(vm_area_t *vma) {
    return &vma->page_table_blocks;
}

/*
 * 创建进程地址空间描述符
 *
 * @param pgd 页全局目录的顶层目录虚拟地址
 * @return 失败：0
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *as_create(uintptr_t pgd);

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void as_destroy(as_t *as);

/*
 * 添加vma
 *
 * @param as 进程地址空间的虚拟地址
 * @param start 这段区域的起始虚拟地址
 * @param end 这段区域的结束虚拟地址
 * @param prot 权限标志
 * @param flags 映射标志
 * 
 * 调用时需要加as锁
 */
vmm_result_t vma_add_nolock(as_t *as, uintptr_t start, uintptr_t end, vm_prot_t prot, uint8_t flags);

/*
 * 添加vma
 *
 * @param as 进程地址空间的虚拟地址
 * @param start 这段区域的起始虚拟地址
 * @param end 这段区域的结束虚拟地址
 * @param prot 权限标志
 * @param flags 映射标志
 */
vmm_result_t vma_add(as_t *as, uintptr_t start, uintptr_t end, vm_prot_t prot, uint8_t flags);

/*
 * 查找包含这个虚拟地址的vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要查找的虚拟地址
 * 
 * @return 失败：0
 * @return 成功：vma的虚拟地址
 * 
 * 调用时需要加as锁
 */
vm_area_t* vma_find_nolock(as_t *as, uintptr_t addr);

/*
 * 查找包含这个虚拟地址的vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要查找的虚拟地址
 * 
 * @return 失败：0
 * @return 成功：vma的虚拟地址
 */
vm_area_t* vma_find(as_t *as, uintptr_t addr);

/*
 * 从地址空间中移除vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @param vma 要移除的vma的虚拟地址
 * 
 * @return 成功：VMM_OK
 * @return 失败：错误码
 * 
 * 调用时需要加as锁
 */
vmm_result_t vma_remove_nolock(as_t *as, vm_area_t *vma);

/*
 * 从地址空间中移除vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @param vma 要移除的vma的虚拟地址
 * 
 * @return 成功：VMM_OK
 * @return 失败：错误码
 */
vmm_result_t vma_remove(as_t *as, vm_area_t *vma);

/*
 * 获取地址空间中的所有VMA地址
 *
 * @param as 进程地址空间的虚拟地址
 * @param result 用于存储结果的结构体指针
 *
 * @return vmm_result_t
 */
vmm_result_t vma_get(as_t *as, vma_result_t **result);

/*
 * 获取地址空间中的所有VMA地址
 *
 * @param as 进程地址空间的虚拟地址
 * @param result 用于存储结果的结构体指针
 * @return vmm_result_t
 * 
 * 需要加as锁
 */
vmm_result_t vma_get_nolock(as_t *as, vma_result_t **result);

/*
 * 查找地址空间中最后一个vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @return 失败：NULL
 * @return 成功：vma的虚拟地址
 * 
 * 需要加as锁
 * 用于计算空的内存地址
 */
vm_area_t *vma_find_end(as_t *as);