/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <mm/vmm_types.h>
#include <mm/linear_map.h>

/*
 * x86_64页表条目（PTE）标志位定义
 * 63-52:保留位 51-12:物理页帧号 11-9:软件可用位 8-0:标志位
 */

// 硬件标志位 
#define PTE_PRESENT         (1ULL << 0)   // 存在位（页面在内存中）
#define PTE_WRITABLE        (1ULL << 1)   // 可写位
#define PTE_USER            (1ULL << 2)   // 用户可访问位
#define PTE_PWT             (1ULL << 3)   // 写通缓存
#define PTE_PCD             (1ULL << 4)   // 缓存禁用
#define PTE_ACCESSED        (1ULL << 5)   // 已访问
#define PTE_DIRTY           (1ULL << 6)   // 已写入
#define PTE_HUGE            (1ULL << 7)   // 大页标志
#define PTE_GLOBAL          (1ULL << 8)   // 全局页
#define PTE_NX              (1ULL << 63)  // 不可执行

// 软件自定义标志位
#define PTE_SOFTWARE0       (1ULL << 9)  
#define PTE_SOFTWARE1       (1ULL << 10) 
#define PTE_SOFTWARE2       (1ULL << 11) 

// 常用标志组合
#define PTE_KERNEL_RO       (PTE_PRESENT)                    // 内核只读页
#define PTE_KERNEL_RW       (PTE_PRESENT | PTE_WRITABLE)     // 内核读写页
#define PTE_USER_RO         (PTE_PRESENT | PTE_USER)         // 用户只读页
#define PTE_USER_RW         (PTE_PRESENT | PTE_WRITABLE | PTE_USER) // 用户读写页
#define PTE_USER_X          (PTE_PRESENT | PTE_USER)         // 用户可执行页
#define PTE_USER_NX         (PTE_PRESENT | PTE_USER | PTE_NX) // 用户不可执行页

// PTE操作
#define PTE_PFN(pte) (((uint64_t)(pte) & 0x000FFFFFFFFFF000ULL) >> 12)  // 从PTE中提取物理页帧号(PFN)
#define PTE_IS_VALID(pte)   ((pte) & PTE_PRESENT)                           // 检查PTE是否有效（存在位为1）
#define PTE_IS_WRITABLE(pte) ((pte) & PTE_WRITABLE)                         // 检查PTE是否可写
#define PTE_IS_USER(pte)    ((pte) & PTE_USER)                              // 检查PTE是否用户可访问
#define PTE_IS_HUGE(pte)    ((pte) & PTE_HUGE)                              // 检查PTE是否为大页
#define PTE_IS_EXECUTABLE(pte) (!((pte) & PTE_NX))                          // 检查PTE是否可执行（NX位为0表示可执行）

#define HUGE_2MB_MASK       ((1ULL << 21) - 1)  // 2MB大页的掩码
#define HUGE_1GB_MASK       ((1ULL << 30) - 1)  // 1GB大页的掩码
#define PAGE_4KB_MASK       ((1ULL << 12) - 1)  // 4KB页的掩码

// 页表条目大小
#define PT_SIZE             (4UL * 1024UL)      // 页表大小
#define PD_SIZE             (2UL * 1024UL * 1024UL)    // 页目录大小
#define PDPT_SIZE           (1024UL * 1024UL *1024UL)  // PDPT大小
#define PUD_SHIFT           30UL                        // PUD移位

// 页表条目大小（PAGE_SIZE的数量）
#define PT_PAGE_SIZE          PT_SIZE / PAGE_SIZE
#define PD_PAGE_SIZE          PD_SIZE / PAGE_SIZE
#define PDPT_PAGE_SIZE        PDPT_SIZE / PAGE_SIZE   


// 权限检查
#define PTE_CAN_READ(pte)   (PTE_IS_VALID(pte))                               // 检查读权限
#define PTE_CAN_WRITE(pte)  (PTE_IS_VALID(pte) && PTE_IS_WRITABLE(pte))       // 检查写权限
#define PTE_CAN_EXEC(pte)   (PTE_IS_VALID(pte) && PTE_IS_EXECUTABLE(pte))     // 检查执行权限
#define PTE_CAN_USER_ACCESS(pte, is_user_mode) \
    (PTE_IS_VALID(pte) && (PTE_IS_USER(pte) || !(is_user_mode)))              // 检查页表项(PTE)对应的页是否允许当前模式访问

#define USER_END            0x00007FFFFFFFFFFFULL     // 用户空间结束地址
#define USER_START          0x0000000000400000ULL     // 用户空间起始地址
#define USER_STACK_TOP      0x00007FFFFF000000ULL     // 用户栈顶地址

// 页表块结构体，用于快速获取分配的页表页信息
typedef struct _page_table_blocks {
    uintptr_t *page_table_blocks;
    uint64_t page_table_blocks_count;
} page_table_blocks_struct;

// 初始化
void mmu_init(void);

// 获取内核pgd
uintptr_t mmu_get_kernel_pgd(void);

/*
 * 遍历页表
 * 找到虚拟地址对应的页表的虚拟地址
 *
 * @param pgd 页全局目录，cr3寄存器的值，一般是pml4的地址
 * @param addr 要找的虚拟地址
 * @param create 是否自动分配缺失的页
 * @param prot 创建页表时的权限（仅在create=true时使用，可以设置为0）
 * @param out_blocks 输出：包含3个页表页的虚拟地址[0]=PDPT,[1]=PD,[2]=PT
 *
 * @return PTE 的虚拟地址
 */
pte_t* mmu_walk(uintptr_t pgd, uintptr_t addr, bool create, vm_prot_t prot, uintptr_t out_blocks[3]);

/*
 * 设置PTE
 *
 * @param pte 页表项的虚拟地址指针
 * @param pfn 要映射的物理地址页帧号
 * @param huge 是否设置大页
 * @param prot 权限标志
 *
 * huge只在PDPTE和PDE中是有意义的
 */
void mmu_set_pte(pte_t *pte, uint64_t pfn, bool huge, vm_prot_t prot);

/*
 * 清除页表条目（设为0）
 *
 * @param pte 指向PTE的指针
 */
void mmu_clear_pte(pte_t *pte);

/*
 * 刷新TLB条目
 *
 * @param vaddr 要刷新的虚拟地址
 */
void mmu_invalidate(uintptr_t vaddr);

// 刷新所有TLB
void mmu_invalidate_all(void);

/*
 * 设置 CR3（切换页表）
 *
 * @param pgd_phys cr3 寄存器所需的物理 PML4 基地址（物理地址）
 */
void mmu_set_pgd(uintptr_t pgd_phys);

// 代表特殊映射，不需要ptb
#define NO_PTB ((page_table_blocks_struct *)1)

/*
 * 创建页表映射
 *
 * @param pgd 页全局目录物理地址
 * @param vaddr_start 需要映射到的虚拟地址起始位置
 * @param paddr_start 需要映射的物理地址
 * @param page_count 页数量
 * @param prot 权限
 * @param flags 映射标志
 * @param page_table_blocks 存储分配的页表页信息
 *
 * 调用需要刷新TLB
 */
int mmu_add_map(
    uintptr_t pgd,
    uintptr_t vaddr_start,
    uintptr_t paddr_start,
    uint64_t page_count,
    vm_prot_t prot,
    uint8_t flags,
    page_table_blocks_struct *page_table_blocks
);

/*
 * 创建新的页全局目录（PML4）
 *
 * @param pgd_ptr 指向存储PML4物理地址的指针
 */
int mmu_add_pgd(uintptr_t *pgd);

/*
 * 删除页全局目录
 *
 * @param pgd 页全局目录物理地址
 */
int mmu_remove_pgd(uintptr_t pgd);

/*
 * 移除页表映射
 *
 * @param page_table_blocks 存储分配的页表页信息
 *
 * 调用者需要自行刷新TLB
 */
int mmu_remove_map(page_table_blocks_struct *page_table_blocks);