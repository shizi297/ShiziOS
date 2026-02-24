/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "vmm_as.h"
#include "vmm_map.h"
#include "vmm_types.h"
#include <heap.h>
#include <mm/bootmem/linear_map.h>
#include <mm/vmm/vmm_mmu.h>
#include <stdint.h>
#include <stddef.h>

static uintptr_t mmio_addr = MMIO_MAP;
static spinlock_t mmio_map_lock = SPIN_LOCK_INIT;

// 初始化
void vmm_init(void) {
    mmu_init();
}

/*
 * 创建一个新的进程地址空间
 * 自动分配页全局目录
 * 
 * @return 失败：NULL
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *vmm_create_as(void) {
    uintptr_t pgd_phys = 0;
    vmm_result_t result = mmu_add_pgd(&pgd_phys);
    if (result != VMM_OK || pgd_phys == 0) {
        return NULL;
    }

    // 创建地址空间描述符，存储PGD的物理地址
    as_t *as = as_create(pgd_phys);
    if (as == NULL) {
        // 创建地址空间失败，移除并释放刚才分配的PGD
        mmu_remove_pgd(pgd_phys);
        return NULL;
    }

    return as;
}

/*
 * 计算自动分配的地址
 * 
 * @param as 进程地址空间
 * @param size 需要的大小
 * @return 成功：分配的地址
 * @return 失败：0
 * 
 * 需要在as锁内调用
 */
static uintptr_t vmm_alloc_addr(as_t *as, uint64_t size) {
    if (as == NULL || size == 0) {
        return 0;
    }
    
    // 计算需要多少页
    uint64_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t total_size = page_count * PAGE_SIZE;
    
    // 查找最后一个VMA
    vm_area_t *last_vma = vma_find_end(as);
    
    uintptr_t start_addr;
    
    if (last_vma == NULL) {
        // 第一个VMA，从USER_START开始
        start_addr = USER_START;
    } else {
        // 在最后一个VMA之后分配，加上4KB间隙
        start_addr = last_vma->end + 4096;
        // 页对齐
        start_addr = (start_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }
    
    // 检查是否超过栈区域
    if (start_addr + total_size > USER_STACK_TOP) {
        return 0;
    }
    
    return start_addr;
}

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
vmm_result_t vmm_unmap_nolock(as_t *as, uintptr_t addr) {
    /*
     * 这里直接释放页表页并刷新TLB
     * 不清除上级页表项
     * 缺页处理程序会处理后续访问
     */

    if (as == NULL) {
        return VMM_INVALID_ARGUMENT;
    }

    // 查找对应的vma
    vm_area_t *vma = vma_find_nolock(as, addr);
    if (vma == NULL) {
        return VMM_INVALID_ADDRESS;
    }

    // 释放页表页
    mmu_remove_map(&vma->page_table_blocks);
    
    // 根据映射大小选择TLB刷新策略
    uint64_t page_count = (vma->end - vma->start) / PAGE_SIZE;
    if (page_count > TLB_FLUSH_THRESHOLD_PAGES) {
        // 大范围，刷新整个TLB
        mmu_invalidate_all();
    } else {
        // 小范围，逐页刷新
        for (uintptr_t va = vma->start; va < vma->end; va += PAGE_SIZE) {
            mmu_invalidate(va);
        }
    }
    
    // 释放数据页（如果有）
    if (vma->linear_addr != 0) {
        kheap_free((void *)vma->linear_addr);
    }
    
    // 从地址空间中移除vma
    return vma_remove_nolock(as, vma);
}

/*
 * 释放内存
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要释放的虚拟地址
 * 
 * @return vmm_result_t
 */
vmm_result_t vmm_unmap(as_t *as, uintptr_t addr) {
    spin_lock(&as->lock);
    vmm_result_t result = vmm_unmap_nolock(as, addr);
    spin_unlock(&as->lock);

    return result;
}

/*
 * 切换到指定的进程地址空间
 * 
 * @param as 进程地址空间的虚拟地址
 */
void vmm_switch_as(as_t *as) {
    if (as == NULL) {
        return;
    }
    mmu_set_pgd(as->pgd);
}

/**
 * 映射mmio地址
 * 
 * @param phy_addr 物理地址
 * @param page_count 大小
 * 
 * @return 成功 ： 映射的虚拟内存
 * @return 失败 ：0
 */
uintptr_t vmm_map_mmio(uint64_t phy_addr, uint64_t page_count) {
    spin_lock(&mmio_map_lock);

    vm_prot_t mmio_prot = VM_READ | VM_WRITE | VM_UC;
    uintptr_t current_mmio = mmio_addr;
     
    page_table_blocks_struct *ptb = kheap_alloc(PAGE_SIZE);
    if (!ptb) goto fail;

    vmm_result_t result = mmu_add_map(
        mmu_get_kernel_pgd(),
        mmio_addr,  
        (uintptr_t)phy_addr,
        page_count,   
        mmio_prot,  
        0,      // 没有特殊标志
        ptb     // 没有ptb
    );

    if (result != VMM_OK) goto fail;

    // 设置下一次映射的虚拟起始地址
    mmio_addr += page_count * PAGE_SIZE;
    spin_unlock(&mmio_map_lock);
    return current_mmio;

    fail:
        spin_unlock(&mmio_map_lock);
        return 0;
}

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vmm_destroy_as(as_t *as) {
    /*
     * 不需要刷新tlb
     * 因为销毁进程地址空间
     * 一般是进程退出
     * 所以在调用这个函数的时候
     * 已经切换到了内核地址空间
     * 不会再访问这个地址空间的内容
     */

    if (as == NULL) {
        return;
    }

    spin_lock(&as->lock);

    // 释放所有VMA及其映射
    while (as->vma_list != NULL) {
        vm_area_t *vma = as->vma_list;
        
        // 释放页表页
        mmu_remove_map(&vma->page_table_blocks);
        
        // 释放数据页（如果有）
        if (vma->linear_addr != 0) {
            kheap_free((void *)vma->linear_addr);
        }
        
        // 删除当前头节点
        vma_remove_nolock(as, vma);
    }

    spin_unlock(&as->lock);

    // 删除页全局目录
    mmu_remove_pgd(as->pgd);

    // 销毁地址空间结构体
    as_destroy(as);
}

/*
 * 映射匿名内存区域
 * 
 * @param as 进程地址空间
 * @param addr 要映射的虚拟地址，如果为0则自动分配
 * @param page 映射页数
 * @param prot 映射权限
 * @param flags 映射标志
 * @param anon_vma 匿名内存结构体指针
 * @param alloc 是否预分配
 * @param out_addr 输出的实际映射地址
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
) {
    if (as == NULL || page == 0) {
        if (out_addr != NULL) {
            *out_addr = 0;
        }
        return VMM_INVALID_ARGUMENT;
    }
    
    uint64_t size = page * PAGE_SIZE;
    
    spin_lock(&as->lock);
    
    // 如果addr为0，自动分配地址
    if (addr == 0) {
        addr = vmm_alloc_addr(as, size);
        if (addr == 0) {
            spin_unlock(&as->lock);
            if (out_addr != NULL) {
                *out_addr = 0;
            }
            return VMM_OUT_OF_ADDRESS_SPACE;
        }
        // 自动分配的地址已经满足所有条件，跳过后续检查
    } else {
        // 手动指定地址，需要检查一些参数
        if (addr < USER_START || addr + size > USER_STACK_TOP) {
            spin_unlock(&as->lock);
            if (out_addr != NULL) {
                *out_addr = 0;
            }
            return VMM_INVALID_ADDRESS;
        }
        
        // 检查地址是否页对齐
        if ((addr & (PAGE_SIZE - 1)) != 0) {
            spin_unlock(&as->lock);
            if (out_addr != NULL) {
                *out_addr = 0;
            }
            return VMM_INVALID_ADDRESS;
        }
    }
    
    // 添加VMA
    vmm_result_t result = vma_add_nolock(as, addr, addr + size, prot, flags);
    if (result != VMM_OK) {
        spin_unlock(&as->lock);
        if (out_addr != NULL) {
            *out_addr = 0;
        }
        return result;
    }
    
    // 查找刚添加的VMA
    vm_area_t *vma = vma_find_nolock(as, addr);
    if (vma == NULL) {
        spin_unlock(&as->lock);
        if (out_addr != NULL) {
            *out_addr = 0;
        }
        return VMM_INTERNAL_ERROR;
    }
    
    if (alloc) {
        // 预分配物理内存
        uintptr_t alloc_addr = (uintptr_t)kheap_alloc(size);
        if (alloc_addr == 0) {
            // 分配失败，移除VMA
            vma_remove_nolock(as, vma);
            spin_unlock(&as->lock);
            if (out_addr != NULL) {
                *out_addr = 0;
            }
            return VMM_OUT_OF_MEMORY;
        }
        
        // 建立映射，传入page_table_blocks指针用于接收页表页数组
        uintptr_t paddr_start = LINEAR_TO_PHYS(alloc_addr);
        result = mmu_add_map(as->pgd, addr, paddr_start, page, prot, flags, &vma->page_table_blocks);
        
        if (result != VMM_OK) {
            // 映射失败，释放物理内存并移除VMA
            kheap_free((void *)alloc_addr);
            vma_remove_nolock(as, vma);
            spin_unlock(&as->lock);
            if (out_addr != NULL) {
                *out_addr = 0;
            }
            return result;
        }
        
        // 保存线性地址
        vma->linear_addr = alloc_addr;
    } else {
        // 不预分配，页表块数组暂时为空
        vma->page_table_blocks.page_table_blocks = NULL;
        vma->page_table_blocks.page_table_blocks_count = 0;
    }
    
    spin_unlock(&as->lock);
    
    // 返回实际分配的地址
    if (out_addr != NULL) {
        *out_addr = addr;
    }
    
    return VMM_OK;
}

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