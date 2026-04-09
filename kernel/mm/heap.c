/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <mm/buddy.h>
#include <mm/pmm.h>
#include <mm/linear_map.h>
#include <mm/vmm_map.h>

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
 * @return 成功：虚拟地址
 * @return 失败：NULL
 */
void* _kheap_alloc(uint64_t size, uint8_t zone) {
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

    if (pfn == 0) return 0;

    // 将pfn转换为虚拟地址并返回指针
    return (void *)PHYS_TO_LINEAR(pfn << PAGE_SHIFT);
}

/**
 * 释放内核堆内存
 * 
 * @param vaddr 被释放的伙伴块的虚拟地址
 */
void kheap_free(void *vaddr) {
    if (vaddr == NULL) return;

    // 将虚拟地址转换为物理页帧号
    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    pmm_free_pages(pfn);
}

/**
 * 增加内核堆内存引用计数
 * 
 * @param vaddr 要增加引用计数的虚拟地址
 */
void kheap_add_ref_count(void *vaddr) {
    if (vaddr == NULL) return;

    // 将虚拟地址转换为物理页帧号
    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    pmm_add_ref_count(pfn);
}

/**
 * 增加内核堆内存映射引用计数
 * 
 * @param vaddr 要增加映射引用计数的虚拟地址
 */
void kheap_add_map_count(void *vaddr) {
    if (vaddr == NULL) return;

    // 将虚拟地址转换为物理页帧号
    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    pmm_add_map_count(pfn);
}

/**
 * 减少内核堆内存映射映射计数
 * 
 * @param vaddr 要减少映射引用计数的虚拟地址
 */
void kheap_sub_map_count(void *vaddr) {
    if (vaddr == NULL) return;

    // 将虚拟地址转换为物理页帧号
    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    pmm_sub_map_count(pfn);
}

/**
 * 清零内核堆的映射计数
 * 
 * @param vaddr 要清零映射计数的虚拟地址
 */
void kheap_zero_map_count(void *vaddr) {
    if (vaddr == NULL) return;

    // 将虚拟地址转换为物理页帧号
    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    pmm_zero_map_count(pfn);
}

/*
 * 设置页表页的上层页表项指针
 *
 * @param vaddr 页表页的虚拟地址
 * @param pte_ptr 上层页表项的虚拟地址
 */
void kheap_set_on_pte_ptr(void *vaddr, uintptr_t pte_ptr) {
    if (vaddr == NULL) return;

    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    pmm_set_on_pte_ptr(pfn, pte_ptr);
}

/*
 * 获取页表页的上层页表项指针
 *
 * @param vaddr 页表页的虚拟地址
 * @return 上层页表项的虚拟地址
 */
uintptr_t kheap_get_on_pte_ptr(void *vaddr) {
    if (vaddr == NULL) return 0;

    uintptr_t phys = LINEAR_TO_PHYS((uintptr_t)vaddr);
    uint64_t pfn = phys >> PAGE_SHIFT;

    return pmm_get_on_pte_ptr(pfn);
}

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
void *vheap_alloc(as_t *as, void *addr, size_t size, vm_prot_t prot, uint8_t flags, int fd , uint64_t offset, bool alloc) {
    void *result_addr = 0;
    void *aligened_addr = 0;
    
    if (as == NULL || size == 0) return NULL;

    if (fd != 0 || offset != 0) {
        // 目前不支持文件映射
        return NULL;
    }

    if (flags != 0) {
        // 目前不支持特殊分配标志
        return NULL;
    }
    if (addr != NULL) {
        // 如果传入了地址，则使用该地址并对齐
        aligened_addr = (void *)((uintptr_t)addr & ~(PAGE_SIZE - 1));
    }

    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
 
    vmm_result_t map_result = vmm_map_anon(
        as,
        (uintptr_t)aligened_addr,
        aligned_size / PAGE_SIZE,
        prot,
        0,
        NULL,
        alloc,
        (uintptr_t *)&result_addr
    );

    return result_addr;
}

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
void vheap_free(as_t *as, void *addr, size_t size) {
    if (as == NULL || addr == NULL) return;

    // 释放内存
    vmm_unmap(as, (uintptr_t)addr);
}

/**
 * 释放进程所有虚拟堆内存
 * 
 * @param as 进程地址空间描述符
 */
void vheap_free_all(as_t *as) {
    if (as == NULL) return;
    vmm_destroy_as(as);
}

/**
 * 映射物理地址到mmio
 * 
 * @param phy 物理地址
 * @param len 要映射的大小
 * 
 * @return 成功：映射到的虚拟地址
 * @return 失败：NULL
 */
void *vheap_map_mmio(uint64_t phy_addr, uint64_t len) {
    uint64_t page_count = (len + PAGE_SIZE - 1) / PAGE_SIZE;

    return (void *)vmm_map_mmio(phy_addr, page_count);
}

/*
 * 创建一个新的进程地址空间
 * 自动分配页全局目录
 * 
 * @return 失败：NULL
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *vheap_create_as(void) {
    return vmm_create_as();
}

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vheap_destroy_as(as_t *as) {
    vmm_destroy_as(as);
}

/*
 * 复制进程地址空间
 * 自动分配新的页表页与物理内存
 *
 * @param as 源地址空间
 *
 * @return 失败：NULL
 * @return 成功：新的地址空间
 */
as_t *vheap_copy_as(as_t *as) {
    return vmm_copy_as(as);
}

// 添加as的引用计数
void vheap_as_add_ref(as_t *as) {
    vmm_as_add_ref(as);
}

// 获取内核地址空间
as_t *vheap_get_kernel_as(void) {
    return vmm_get_kernel_as();
}

// 获取内核的页全局目录
uintptr_t vheap_get_kernel_pgd(void) {
    return vmm_get_kernel_pgd();
}