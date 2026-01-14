/* SPDX-License-Identifier: Apache-2.0 */

#include "vmm_as.h"
#include <mm/heap.h>
#include <mm/bootmem/linear_map.h>

/*
 * 创建进程地址空间描述符
 *
 * @param pgd 页全局目录的顶层目录虚拟地址
 * @return 失败：0
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *as_create(uintptr_t pgd) {
    as_t *vaddr = (as_t *)kheap_alloc(sizeof(as_t));
    if (vaddr == NULL) {
        return NULL;
    }

    // 初始化
    spinlock_init(&vaddr->lock);
    vaddr->pgd = pgd;
    vaddr->vma_list = NULL;

    return vaddr;
}

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void as_destroy(as_t *as) {
    if (as == NULL) {
        return;
    }
    
    /*
     * 原本是打算让这里释放所有VMA的
     * 但是上层需要一些信息
     * 所以改成上层负责释放VMA
     */

    // 释放as结构本身
    kheap_free(as);
}

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
vmm_result_t vma_add_nolock(as_t *as, uintptr_t start, uintptr_t end, vm_prot_t prot, uint8_t flags) {
    // 参数检查
    if (as == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    // 地址有效性检查
    if (start >= end) {
        return VMM_INVALID_ADDRESS;
    }
    
    if ((start & (PAGE_SIZE - 1)) != 0 || (end & (PAGE_SIZE - 1)) != 0) {
        return VMM_INVALID_ADDRESS;  // 地址未页对齐
    }
    
    // 分配VMA结构
    vm_area_t *new_vma = (vm_area_t *)kheap_alloc(sizeof(vm_area_t));
    if (new_vma == NULL) {
        return VMM_OUT_OF_MEMORY;
    }
    
    // 初始化VMA
    new_vma->start = start;
    new_vma->end = end;
    new_vma->prot = prot;
    new_vma->flags = flags;
    new_vma->linear_addr = 0;
    new_vma->page_table_blocks.page_table_blocks_count = 0;
    new_vma->page_table_blocks.page_table_blocks = NULL;
    new_vma->anon_vma = NULL;
    new_vma->file = NULL;
    new_vma->device = NULL;
    new_vma->prev = NULL;
    new_vma->next = NULL;
    
    //  链表为空，直接插入
    if (as->vma_list == NULL) {
        as->vma_list = new_vma;
        return VMM_OK;
    }
    
    vm_area_t *current = as->vma_list;
    vm_area_t *prev = NULL;
    
    // 遍历链表找到插入位置（按start地址升序）
    while (current != NULL) {
        // 找到插入位置：当前节点start大于新节点start
        if (current->start > start) {
            break;
        }
        
        // 移动到下一个节点
        prev = current;
        current = current->next;
    }
    
    // 插入新节点
    if (prev == NULL) {
        // 插入到链表头部
        new_vma->next = as->vma_list;
        as->vma_list->prev = new_vma;
        as->vma_list = new_vma;
    } else if (current == NULL) {
        // 插入到链表尾部
        prev->next = new_vma;
        new_vma->prev = prev;
    } else {
        // 插入到中间
        prev->next = new_vma;
        new_vma->prev = prev;
        new_vma->next = current;
        current->prev = new_vma;
    }
    
    return VMM_OK;
}

/*
 * 添加vma
 *
 * @param as 进程地址空间的虚拟地址
 * @param start 这段区域的起始虚拟地址
 * @param end 这段区域的结束虚拟地址
 * @param prot 权限标志
 * @param flags 映射标志
 */
vmm_result_t vma_add(as_t *as, uintptr_t start, uintptr_t end, vm_prot_t prot, uint8_t flags) {
    // 参数检查
    if (as == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    spin_lock(&as->lock);
    vmm_result_t result = vma_add_nolock(as, start, end, prot, flags);
    spin_unlock(&as->lock);
    
    return result;
}

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
vm_area_t* vma_find_nolock(as_t *as, uintptr_t addr) {
    if (as == NULL) {
        return NULL;  // 无效参数
    }
    
    vm_area_t *current = as->vma_list;
    
    while (current != NULL) {
        /* 
         * 因为链表按start升序排列
         * 如果当前节点的start已经大于目标地址
         * 后续节点的start更大
         * 不可能包含addr
         * 所以可以提前结束
         */
        if (addr < current->start) {
            break;
        }
        
        // 检查当前节点是否包含目标地址
        if (addr < current->end) {
            return current;
        }
        
        current = current->next;
    }
    
    return NULL;  // 未找到
}

/*
 * 查找包含这个虚拟地址的vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @param addr 要查找的虚拟地址
 * 
 * @return 失败：0
 * @return 成功：vma的虚拟地址
 */
vm_area_t* vma_find(as_t *as, uintptr_t addr) {
    if (as == NULL) {
        return NULL;  // 无效参数
    }
    
    spin_lock(&as->lock);
    vm_area_t *result = vma_find_nolock(as, addr);
    spin_unlock(&as->lock);
    
    return result;
}

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
vmm_result_t vma_remove_nolock(as_t *as, vm_area_t *vma) {
    // 参数检查
    if (as == NULL || vma == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    // 确保vma在as的链表中
    vm_area_t *current = as->vma_list;
    bool found = false;
    
    while (current != NULL) {
        if (current == vma) {
            found = true;
            break;
        }
        current = current->next;
    }
    
    if (!found) {
        return VMM_INVALID_ARGUMENT;  // vma不属于这个as
    }
    
    // 从链表中移除vma
    if (vma->prev == NULL) {
        // vma是链表头
        as->vma_list = vma->next;
        if (vma->next != NULL) {
            vma->next->prev = NULL;
        }
    } else {
        // vma在链表中间或尾部
        vma->prev->next = vma->next;
        if (vma->next != NULL) {
            vma->next->prev = vma->prev;
        }
    }
    
    // 释放vma关联的资源
    if (vma->anon_vma != NULL && vma->anon_vma->refcount > 0) {
        vma->anon_vma->refcount--;
        if (vma->anon_vma->refcount == 0) {
            kheap_free(vma->anon_vma); 
        }
    }
    if (vma->file != NULL && vma->file->refcount > 0) {
        vma->file->refcount--;
        if (vma->file->refcount == 0) {
            kheap_free(vma->file);
        }
    }
    if (vma->device != NULL && vma->device->refcount > 0) {
        vma->device->refcount--;
        if (vma->device->refcount == 0) {
            kheap_free(vma->device);
        }
    }
    
    kheap_free(vma);
    
    return VMM_OK;
}

/*
 * 从地址空间中移除vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @param vma 要移除的vma的虚拟地址
 * 
 * @return 成功：VMM_OK
 * @return 失败：错误码
 */
vmm_result_t vma_remove(as_t *as, vm_area_t *vma) {
    // 参数检查
    if (as == NULL || vma == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    spin_lock(&as->lock);
    vmm_result_t result = vma_remove_nolock(as, vma);
    spin_unlock(&as->lock);
    
    return result;
}

/*
 * 获取地址空间中的所有VMA地址
 *
 * @param as 进程地址空间的虚拟地址
 * @param result 用于存储结果的结构体指针
 * @return vmm_result_t
 */
vmm_result_t vma_get(as_t *as, vma_result_t **result) {
    spin_lock(&as->lock);
    vmm_result_t vma_get_result = vma_get_nolock(as, result);
    spin_unlock(&as->lock);
    return vma_get_result;
}

/*
 * 获取地址空间中的所有VMA地址
 *
 * @param as 进程地址空间的虚拟地址
 * @param result 用于存储结果的结构体指针
 * @return vmm_result_t
 * 
 * 需要加as锁
 */
vmm_result_t vma_get_nolock(as_t *as, vma_result_t **result) {

    if (as == NULL || result == NULL) {
        return VMM_INVALID_ARGUMENT;
    }

    // 遍历VMA链表，计算VMA数量
    uint64_t count = 0;
    vm_area_t *current = as->vma_list;
    while (current != NULL) {
        count++;
        current = current->next;
    }

    // 创建结构体用于返回
    uint64_t size = sizeof(vma_result_t) + count * sizeof(vma_data_t);
    vma_result_t *vma_result = kheap_alloc(size);

    if (vma_result == NULL) {
        return VMM_OUT_OF_MEMORY;
    }

    vma_result->addr_count = count;
    current = as->vma_list;
    for (uint64_t i = 0;current != NULL;i++) {
        vma_result->vma_data[i].start_addr = current->start;
        vma_result->vma_data[i].end_addr = current->end;
        vma_result->vma_data[i].linear = current->linear_addr;
        current = current->next;
    }

    *result = vma_result;

    return VMM_OK;
}

/*
 * 查找地址空间中最后一个vma
 * 
 * @param as 进程地址空间的虚拟地址
 * @return 失败：NULL
 * @return 成功：vma的虚拟地址
 * 
 * 需要加as锁
 */
vm_area_t *vma_find_end(as_t *as) {
    if (as == NULL) {
        return NULL;
    }

    vm_area_t *current = as->vma_list;
    while (current != NULL && current->next != NULL) {
        current = current->next;
    }

    return current;
}