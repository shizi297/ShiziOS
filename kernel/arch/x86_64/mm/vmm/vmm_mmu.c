/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdbool.h>
#include <mm/heap.h>
#include <shizi/uint4_512t.h>
#include <stddef.h>
#include <mm/vmm/vmm_as.h>
#include <serial.h>
#include "vmm_mmu.h"

static uintptr_t kernel_pgd = 0;

// walk状态
enum walk_state {
    WALK_START,
    WALK_PML4,
    WALK_PDPT,
    WALK_PD,
    WALK_PT,
    WALK_DONE,
    WALK_ERROR,
    WALK_BREAK,
    WALK_PANIC
};

/*
 * 添加新页表需要的不同等级的页数
 * 通过计算后放在该结构体
 * 用于一次性分配
 */
struct need_page_tables {
    uint64_t pdpt;
    uint64_t pd;
    uint64_t pt;
    uintptr_t vaddr;
};

/*
 * 初始化
 * 获取内核页表页物理地址
 */
__attribute__((noinline)) void mmu_init(void) {
    __asm__ volatile("mov %%cr3, %0" : "=r" (kernel_pgd));
}

/*
 * 分配一个新的页表页
 * 
 * @return 成功：页表页的虚拟地址
 * @return 失败：0
 */
static uintptr_t alloc_page_table_page(void) {
    uintptr_t page = (uintptr_t)kheap_alloc(PAGE_SIZE);
    if (page == 0) {
        return 0;
    }
    
    // 清零
    pte_t *page_virt = (pte_t *)page;
    for (int i = 0; i < 512; i++) {
        mmu_clear_pte(&page_virt[i]);
    }
    
    return page;
}

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
 * @return 失败：NULL
 * @return 成功：PTE的虚拟地址
 */
pte_t* mmu_walk(uintptr_t pgd, uintptr_t addr, bool create, vm_prot_t prot, uintptr_t out_blocks[3]) {
    uint8_t walk = WALK_START;
    pte_t *pte_ptr = NULL;
    
    uint64_t current_phys = pgd;  // 当前页表的物理地址
    pte_t* current_virt = NULL;   // 当前页表的虚拟地址
    
    
    /*
     * 计算中间页表项权限
     * 中间页与最终页权限不同
     * 仅包含写权限和用户权限
     * 
     * 写权限如果中间页表项W=0
     * 则整个子树都不可写
     * 用户访问权限如果中间页表项U=0
     * 则整个子树只有内核可访问
     * 执行权限仅在最终PTE中有效
     * 中间页表项NX位被硬件忽略
     * 
     * 所以中间页表项不应设置NX位
     * 但是不设置为可执行会导致某些函数计算问题
     * 所以这里强制设置为可执行
     * 
     * 目前PTE_PFN的bug已修复
     * 不知道其他函数有没有这个bug
     * 所以目前这个设置也保留
     * 作为双重保险
     */ 
    vm_prot_t intermediate_prot = (prot & (VM_WRITE | VM_USER)) | VM_EXEC;
    
    /*
     * 检查create参数决定执行什么
     * 获取PML4E的虚拟地址
     * 找到PDPT页表
     * 获取PDPTE的虚拟地址
     * 检查PDPT是否是大页
     * 如果是就直接返回
     * 不是就继续执行
     * 找到PD页表
     * 获取PDE的虚拟地址
     * 检查大页同上
     * 找到PT页表
     * 获取PTE的虚拟地址
     * 直接返回PTE
     */
    while (walk != WALK_BREAK) {
        switch (walk) {
        case WALK_START:
            // 初始化out_blocks
            if (create && out_blocks) {
                out_blocks[0] = 0;
                out_blocks[1] = 0;
                out_blocks[2] = 0;
            }
            
            if (pgd == 0) {
                walk = WALK_PANIC;
                break;
            }
            
            walk = WALK_PML4;
            break;
            
        case WALK_PML4:
            // 物理地址转虚拟地址
            current_virt = (pte_t*)PHYS_TO_LINEAR(current_phys);
            
            // 获取PML4E
            pte_ptr = &current_virt[PML4_INDEX(addr)];
            
            // 原子读取PTE值
            pte_t pml4e_value = __atomic_load_n(pte_ptr, __ATOMIC_ACQUIRE);
            
            // 检查Present位
            if (!PTE_IS_VALID(pml4e_value)) {
                if (!create) {
                    walk = WALK_ERROR;
                    break;
                }
                
                // 分配PDPT页
                uintptr_t pdpt_page = alloc_page_table_page();
                if (pdpt_page == 0) {
                    walk = WALK_ERROR;
                    break;
                }
                
                // 设置PML4E
                uintptr_t pdpt_phys = LINEAR_TO_PHYS(pdpt_page);
                mmu_set_pte(pte_ptr, pdpt_phys >> 12, false, intermediate_prot);
                
                // 记录到out_blocks
                if (out_blocks) {
                    out_blocks[0] = pdpt_page;
                }
                
                // 更新当前物理地址为PDPT页的物理地址
                current_phys = pdpt_phys;
            } else {
                // 提取下一级物理地址
                current_phys = PTE_PFN(pml4e_value) << 12;
                
                // 如果create=true，增加引用计数并记录
                if (create) {
                    uintptr_t pdpt_vaddr = (uintptr_t)PHYS_TO_LINEAR(current_phys);
                    kheap_add_ref_count((void*)pdpt_vaddr);
                    
                    if (out_blocks) {
                        out_blocks[0] = pdpt_vaddr;
                    }
                }
            }
            
            walk = WALK_PDPT;
            break;
            
        case WALK_PDPT:
            // 物理地址转虚拟地址
            current_virt = (pte_t*)PHYS_TO_LINEAR(current_phys);
            
            // 获取PDPTE
            pte_ptr = &current_virt[PDPT_INDEX(addr)];
            
            // 原子读取PTE值
            pte_t pdpte_value = __atomic_load_n(pte_ptr, __ATOMIC_ACQUIRE);
            
            // 检查Present位
            if (!PTE_IS_VALID(pdpte_value)) {
                if (!create) {
                    walk = WALK_ERROR;
                    break;
                }
                
                // 分配PD页
                uintptr_t pd_page = alloc_page_table_page();
                if (pd_page == 0) {
                    walk = WALK_ERROR;
                    break;
                }
                
                // 设置PDPTE
                uintptr_t pd_phys = LINEAR_TO_PHYS(pd_page);
                mmu_set_pte(pte_ptr, pd_phys >> 12, false, intermediate_prot);
                
                // 记录到out_blocks
                if (out_blocks) {
                    out_blocks[1] = pd_page;
                }
                
                // 更新当前物理地址为PD页的物理地址
                current_phys = pd_phys;
            } else {
                // 检查是否为大页（1GB）
                if (PTE_IS_HUGE(pdpte_value)) {
                    /*
                     * 遇到大页
                     * 直接返回这个大页表项
                     * 让上层处理
                     * 不加入ptb
                     * 因为ptb用于快速释放页表页
                     * 而且因为没有东西指向他就没加引用计数
                     * 如果在没有引用计数增加的时候加入ptb会导致重复释放
                     */
                    walk = WALK_DONE;
                    break;
                }
                
                // 提取下一级物理地址
                current_phys = PTE_PFN(pdpte_value) << 12;
                
                // 如果create=true，增加引用计数并记录
                if (create) {
                    uintptr_t pd_vaddr = (uintptr_t)PHYS_TO_LINEAR(current_phys);
                    kheap_add_ref_count((void*)pd_vaddr);
                    
                    if (out_blocks) {
                        out_blocks[1] = pd_vaddr;
                    }
                }
            }
            
            walk = WALK_PD;
            break;
            
        case WALK_PD:
            // 物理地址转虚拟地址
            current_virt = (pte_t*)PHYS_TO_LINEAR(current_phys);
            
            // 获取PDE
            pte_ptr = &current_virt[PD_INDEX(addr)];
            
            // 原子读取PTE值
            pte_t pde_value = __atomic_load_n(pte_ptr, __ATOMIC_ACQUIRE);
            
            // 检查Present位
            if (!PTE_IS_VALID(pde_value)) {
                if (!create) {
                    walk = WALK_ERROR;
                    break;
                }
                
                // 分配PT页
                uintptr_t pt_page = alloc_page_table_page();
                if (pt_page == 0) {
                    walk = WALK_ERROR;
                    break;
                }
                
                // 设置PDE
                uintptr_t pt_phys = LINEAR_TO_PHYS(pt_page);
                mmu_set_pte(pte_ptr, pt_phys >> 12, false, intermediate_prot);
                
                // 记录到out_blocks
                if (out_blocks) {
                    out_blocks[2] = pt_page;
                }
                
                // 更新当前物理地址为PT页的物理地址
                current_phys = pt_phys;
            } else {
                // 检查是否为大页（2MB）
                if (PTE_IS_HUGE(pde_value)) {
                    // 遇到大页，直接返回这个大页表项，让上层处理，不加ptb，原因同上
                    walk = WALK_DONE;
                    break;
                }
                
                // 提取下一级物理地址
                current_phys = PTE_PFN(pde_value) << 12;
                
                // 如果create=true，增加引用计数并记录
                if (create) {
                    uintptr_t pt_vaddr = (uintptr_t)PHYS_TO_LINEAR(current_phys);
                    kheap_add_ref_count((void*)pt_vaddr);
                    
                    if (out_blocks) {
                        out_blocks[2] = pt_vaddr;
                    }
                }
            }
            
            walk = WALK_PT;
            break;
            
        case WALK_PT:
            // 物理地址转虚拟地址
            current_virt = (pte_t*)PHYS_TO_LINEAR(current_phys);
            
            // 获取PTE
            pte_ptr = &current_virt[PT_INDEX(addr)];
            
            walk = WALK_DONE;
            break;
            
        case WALK_DONE:
            // 成功完成，跳出循环
            walk = WALK_BREAK;
            break;
            
        case WALK_ERROR:
            // 失败，清理已分配的页表页
            if (create && out_blocks) {
                // 这里按照分配的逆序释放
                if (out_blocks[2] != 0) {
                    kheap_free((void*)out_blocks[2]);
                }
                if (out_blocks[1] != 0) {
                    kheap_free((void*)out_blocks[1]);
                }
                if (out_blocks[0] != 0) {
                    kheap_free((void*)out_blocks[0]);
                }
            }
            
            pte_ptr = NULL;
            walk = WALK_BREAK;
            break;
            
        case WALK_PANIC:
            // 严重错误，需要panic
            panic("[MMU] ERROR: page walk encountered fatal error (possibly huge page conflict)");
            walk = WALK_BREAK;
            pte_ptr = NULL;
            break;
            
        default:
            // 不应该到达这里
            panic("[MMU] ERROR: invalid walk state");
            walk = WALK_BREAK;
            pte_ptr = NULL;
            break;
        }
    }
    
    return pte_ptr;
}

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
void mmu_set_pte(pte_t *pte, uint64_t pfn, bool huge, vm_prot_t prot) {
    uint64_t flags = PTE_PRESENT;
    
    // 权限转换
    if (prot & VM_WRITE) flags |= PTE_WRITABLE;
    if (prot & VM_USER) flags |= PTE_USER;
    if (!(prot & VM_EXEC)) flags |= PTE_NX;
    
    // 大页设置
    if (huge) flags |= PTE_HUGE;
    
    // 构建PTE值并原子设置
    uint64_t pte_value = (pfn << 12) | flags;
    __atomic_store_n(pte, pte_value, __ATOMIC_SEQ_CST);
}

/*
 * 清除页表条目（设为0）
 * @param pte 指向PTE的指针
 */
void mmu_clear_pte(pte_t *pte) {
    // 使用原子存储0，RELEASE内存序确保之前的操作对其他CPU可见
    __atomic_store_n(pte, 0, __ATOMIC_RELEASE);
}

/*
 * 刷新TLB条目
 * @param vaddr 要刷新的虚拟地址
 */
void mmu_invalidate(uintptr_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/*
 * 刷新所有TLB
 * @param pgd_phys cr3 寄存器所需的物理 PML4 基地址（物理地址）
 */
void mmu_invalidate_all(void) {
    uintptr_t cr3_val;
    __asm__ volatile("mov %%cr3, %0" : "=r" (cr3_val));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3_val) : "memory");
}

/*
 * 设置 CR3（切换页表）
 * @param pgd_phys cr3 寄存器所需的物理 PML4 基地址（物理地址）
 */
void mmu_set_pgd(uintptr_t pgd_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pgd_phys) : "memory");
}


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
 * @return vmm_result_t
 * 
 * 调用需要刷新TLB
 */
vmm_result_t mmu_add_map(
    uintptr_t pgd, 
    uintptr_t vaddr_start, 
    uintptr_t paddr_start, 
    uint64_t page_count, 
    vm_prot_t prot, 
    uint8_t flags,
    page_table_blocks_struct *page_table_blocks
) {
    if (pgd == 0 || page_table_blocks == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    if (page_count == 0) {
        return VMM_INVALID_SIZE;
    }
    
    // 检查地址对齐
    if ((vaddr_start & (PAGE_SIZE - 1)) != 0 || (paddr_start & (PAGE_SIZE - 1)) != 0) {
        return VMM_INVALID_ADDRESS;
    }
    
    // 计算需要的最大存储空间（每个ptb有3个页表页指针）
    uint64_t max_blocks = page_count * 3;
    uintptr_t *blocks_array = (uintptr_t*)kheap_alloc(max_blocks * sizeof(uintptr_t));
    if (blocks_array == NULL) {
        return VMM_OUT_OF_MEMORY;
    }
    
    // 初始化数组为0
    for (uint64_t i = 0; i < max_blocks; i++) {
        blocks_array[i] = 0;
    }
    
    uint64_t blocks_index = 0;  // 当前存储位置
    vmm_result_t result = VMM_OK;
    
    // 循环映射每个页面
    for (uint64_t i = 0; i < page_count; i++) {
        uintptr_t vaddr = vaddr_start + i * PAGE_SIZE;
        uintptr_t paddr = paddr_start + i * PAGE_SIZE;
        
        uintptr_t page_blocks[3] = {0, 0, 0};
        pte_t* pte = mmu_walk(pgd, vaddr, true, prot, page_blocks);
        
        if (pte == NULL) {
            // 内存分配失败
            result = VMM_OUT_OF_MEMORY;
            break;
        }
        
        // 记录这个页面的页表页（如果有）
        for (int j = 0; j < 3; j++) {
            if (page_blocks[j] != 0) {
                if (blocks_index >= max_blocks) {
                    // 不应该发生，但如果发生则说明有严重bug
                    panic("[MMU] ERROR: blocks_index overflow, possible internal calculation error");
                    return VMM_INTERNAL_ERROR;
                }
                blocks_array[blocks_index++] = page_blocks[j];
            }
        }
        
        if (result != VMM_OK) {
            break;
        }
        
        // 如果不是大页，设置PTE
        if (!PTE_IS_HUGE(*pte)) {
            mmu_set_pte(pte, paddr >> 12, false, prot);
        }
        // 如果是大页，不设置PTE（已经有大页映射）
    }
    
    if (result != VMM_OK) {
        // 错误回滚：逆序释放所有已记录的页表页
        for (uint64_t i = 0; i < blocks_index; i++) {
            if (blocks_array[i] != 0) {
                kheap_free((void*)blocks_array[i]);
            }
        }
        kheap_free(blocks_array);
        return result;
    }
    
    // 成功，填充输出结构
    page_table_blocks->page_table_blocks = blocks_array;
    page_table_blocks->page_table_blocks_count = blocks_index;
    
    return VMM_OK;
}

/*
 * 创建新的页全局目录（PML4）
 * @param pgd_ptr 指向存储PML4物理地址的指针
 * @return vmm_result_t
 */
vmm_result_t mmu_add_pgd(uintptr_t *pgd) {
    uintptr_t pgd_page = (uintptr_t)kheap_alloc(PAGE_SIZE);
    if (pgd_page == 0) {
        *pgd = 0;
        return VMM_OUT_OF_MEMORY;
    }
    
    // 初始化
    uintptr_t *pml4 = (uintptr_t *)pgd_page;
    for (int i = 0; i < 512; i++) {
        mmu_clear_pte(&pml4[i]);
    }

    uintptr_t *kernel_pml4 = (uintptr_t *)PHYS_TO_LINEAR(kernel_pgd);
    // 复制内核空间映射
    for (int i = 256; i < 512; i++) {
        pml4[i] = kernel_pml4[i];
    }
    
    // 转换为物理地址返回
    *pgd = LINEAR_TO_PHYS(pgd_page);
    
    return VMM_OK;
}

/*
 * 删除页全局目录
 * @param pgd 页全局目录物理地址
 * @return vmm_result_t
 */
vmm_result_t mmu_remove_pgd(uintptr_t pgd) {
    if (pgd == 0) {
        return VMM_INVALID_ARGUMENT;
    }

    uintptr_t pgd_page = (uintptr_t)PHYS_TO_LINEAR(pgd);
    kheap_free((void *)pgd_page);

    return VMM_OK;
}

/*
 * 移除页表映射
 * 
 * @param page_table_blocks 分配的页表页信息
 * @return vmm_result_t
 * 
 * 调用者需要自行刷新TLB
 */
vmm_result_t mmu_remove_map(page_table_blocks_struct *page_table_blocks) {
    // 确保结构体是有效的
    if (page_table_blocks == NULL || page_table_blocks->page_table_blocks == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    // 遍历数组，释放页表页
    for (uint64_t i = 0; i < page_table_blocks->page_table_blocks_count; i++) {
        if (page_table_blocks->page_table_blocks[i] != 0) {
            kheap_free((void *)page_table_blocks->page_table_blocks[i]);
        }
    }
    kheap_free((void *)page_table_blocks->page_table_blocks);

    // 这里不刷新tlb

    return VMM_OK;
}
