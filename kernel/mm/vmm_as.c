/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "vmm_as.h"
#include <heap.h>
#include <mm/linear_map.h>
#include <list.h>
#include <libtree.h>
#include <klibc.h>

/**
 * 红黑树节点比较（按 start 地址）
 * 
 * @param a 要比较的红黑树的第一个节点
 * @param b 要比较的红黑树的第二个节点
 * 
 * @return -1 a小于b
 * @return 0 相等
 * @return 1 a大于b
 */
static int vma_rb_cmp(const struct rbtree_node *a, const struct rbtree_node *b) {
    const vm_area_t *va = container_of(a, vm_area_t, rb_node);
    const vm_area_t *vb = container_of(b, vm_area_t, rb_node);
    if (va->start < vb->start)
        return -1;
    if (va->start > vb->start)
        return 1;
    return 0;
}

/**
 * 在红黑树中查找第一个 start >= addr 的节点
 * 
 * @param tree 红黑树头节点
 * @param addr 要找的地址
 * 
 * @return vma指针
 */
static vm_area_t *vma_rb_find_ge(struct rbtree *tree, uintptr_t addr) {
    struct rbtree_node *cur = tree->root;
    vm_area_t *candidate = NULL;

    while (cur) {
        vm_area_t *vma = container_of(cur, vm_area_t, rb_node);
        if (vma->start >= addr) {
            candidate = vma;
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }
    return candidate;
}

/**
 * 在红黑树中查找最后一个 start <= addr 的节点
 * 
 * @param tree 红黑树头节点
 * @param addr 要找的地址
 * 
 * @return vma指针
 */
static vm_area_t *vma_rb_find_le(struct rbtree *tree, uintptr_t addr) {
    struct rbtree_node *cur = tree->root;
    vm_area_t *candidate = NULL;

    while (cur) {
        vm_area_t *vma = container_of(cur, vm_area_t, rb_node);
        if (vma->start <= addr) {
            candidate = vma;
            cur = cur->right;
        } else {
            cur = cur->left;
        }
    }
    return candidate;
}

// 查找地址空间中最后一个 vma
static vm_area_t *vma_find_end(as_t *as) {
    if (as == NULL) {
        return NULL;
    }

    struct rbtree_node *last_node = rbtree_last(&as->vma_tree);
    return last_node ? container_of(last_node, vm_area_t, rb_node) : NULL;
}

/*
 * 添加 vma 到地址空间
 *
 * @param as 进程地址空间
 * @param start 起始虚拟地址（页对齐）
 * @param end 结束虚拟地址（页对齐）
 * @param prot 权限标志
 * @param flags 映射标志
 *
 * @return VMM_OK 成功，否则错误码
 */
vmm_result_t vma_add(as_t *as, uintptr_t start, uintptr_t end,
                     vm_prot_t prot, uint8_t flags) {
    // 参数检查
    if (as == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    if (start >= end) {
        return VMM_INVALID_ARGUMENT;
    }
    if ((start & (PAGE_SIZE - 1)) != 0 || (end & (PAGE_SIZE - 1)) != 0) {
        return VMM_INVALID_ARGUMENT;
    }

    // 重叠检查
    vm_area_t *next = vma_rb_find_ge(&as->vma_tree, start);
    if (next) {
        // 检查 next 自身是否与新区间重叠
        if (next->start < end)
            return VMM_INVALID_ARGUMENT;

        // 检查 next 的前驱
        struct rbtree_node *prev_node = rbtree_prev(&next->rb_node);
        if (prev_node) {
            vm_area_t *prev = container_of(prev_node, vm_area_t, rb_node);
            if (prev->end > start)
                return VMM_INVALID_ARGUMENT;
        }
    } else {
        // 没有 start >= start 的节点，检查最后一个节点
        struct rbtree_node *last_node = rbtree_last(&as->vma_tree);
        if (last_node) {
            vm_area_t *last = container_of(last_node, vm_area_t, rb_node);
            if (last->end > start)
                return VMM_INVALID_ARGUMENT;
        }
    }

    // 分配vma结构
    vm_area_t *new_vma = (vm_area_t *)kheap_alloc(sizeof(vm_area_t));
    if (new_vma == NULL) {
        return VMM_OUT_OF_MEMORY;
    }
    
    // 初始化vma
    new_vma->start = start;
    new_vma->end = end;
    new_vma->prot = prot;
    new_vma->flags = flags;
    // kheap_alloc 已清零，其他字段已为0或NULL
    INIT_LIST_HEAD(&new_vma->list_node);

    // 插入红黑树
    struct rbtree_node *exist = rbtree_insert(&new_vma->rb_node, &as->vma_tree);
    if (exist) {
        // 重叠检查已做，若发生说明代码错误
        kheap_free(new_vma);
        return VMM_INVALID_ARGUMENT;
    }

    // 插入链表尾部
    list_add_tail(&new_vma->list_node, &as->vma_list);

    return VMM_OK;
}

/*
 * 查找包含 addr 的 vma
 *
 * @param as 进程地址空间
 * @param addr 虚拟地址
 *
 * @return 失败：NULL
 * @return 成功：vma 指针
 */
vm_area_t* vma_find(as_t *as, uintptr_t addr) {
    if (as == NULL) {
        return NULL;
    }

    vm_area_t *candidate = vma_rb_find_le(&as->vma_tree, addr);
    if (candidate && addr < candidate->end) {
        return candidate;
    }
    return NULL;
}

/*
 * 从地址空间中移除 vma
 *
 * @param as 进程地址空间
 * @param vma 要移除的 vma 指针
 *
 * @return VMM_OK 成功，否则错误码
 */
vmm_result_t vma_remove(as_t *as, vm_area_t *vma) {
    if (as == NULL || vma == NULL) {
        return VMM_INVALID_ARGUMENT;
    }

    // 从链表中移除
    list_del(&vma->list_node);

    // 从红黑树中移除
    rbtree_remove(&vma->rb_node, &as->vma_tree);

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

// 清理地址空间内部资源（释放所有 vma 和 pgd）
static void as_cleanup(as_t *as) {
    // 释放所有vma及其映射
    while (!list_empty(&as->vma_list)) {
        vm_area_t *vma = list_first_entry(&as->vma_list, vm_area_t, list_node);
        if (vma->linear_addr != 0) {
            kheap_free((void *)vma->linear_addr);
        }
        vma_remove(as, vma);
    }
    // 删除页全局目录
    mmu_remove_pgd(as_get_pgd(as));
}

/*
 * 创建进程地址空间描述符
 *
 * @param pgd 页全局目录物理地址
 *
 * @return 失败：NULL
 * @return 成功：进程地址空间的虚拟地址
 */
as_t *as_create(uintptr_t pgd) {
    as_t *vaddr = (as_t *)kheap_alloc(sizeof(as_t));
    if (vaddr == NULL) {
        return NULL;
    }

    spinlock_init(&vaddr->lock);
    vaddr->pgd = pgd;
    INIT_LIST_HEAD(&vaddr->vma_list);
    rbtree_init(&vaddr->vma_tree, vma_rb_cmp, 0);
    atomic_store(&vaddr->refcount, 1);

    return vaddr;
}

// 获取地址空间锁
void as_lock(as_t *as) {
    spin_lock(&as->lock);
}

// 释放地址空间锁
void as_unlock(as_t *as) {
    spin_unlock(&as->lock);
}

// 增加引用计数
void as_add_ref(as_t *as) {
    atomic_fetch_add(&as->refcount, 1);
}

// 减少引用计数（内部）
static int as_sub_ref(as_t *as) {
    return atomic_fetch_sub(&as->refcount, 1) - 1;
}

// 获取当前引用计数
int as_get_ref(as_t *as) {
    return atomic_load(&as->refcount);
}

// 获取页全局目录物理地址
uintptr_t as_get_pgd(as_t *as) {
    return as->pgd;
}

/*
 * 销毁进程地址空间（减少引用计数，归零时释放）
 *
 * @param as 进程地址空间的虚拟地址
 *
 * 调用前需持有 as 锁
 */
void as_destroy(as_t *as) {
    if (as == NULL) {
        return;
    }
    
    int new_ref = as_sub_ref(as);
    if (new_ref == 0) {
        as_cleanup(as);
        kheap_free(as);
    }
}

/*
 * 为地址空间分配一个新的虚拟地址
 *
 * @param as 进程地址空间
 * @param size 需要的大小（字节）
 *
 * @return 失败：0
 * @return 成功：分配的虚拟地址
 */
uintptr_t as_alloc_addr(as_t *as, uint64_t size) {
    if (as == NULL || size == 0) {
        return 0;
    }
    
    // 计算需要多少页
    uint64_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t total_size = page_count * PAGE_SIZE;
    
    // 查找最后一个vma
    vm_area_t *last_vma = vma_find_end(as);
    
    uintptr_t start_addr;
    
    if (last_vma == NULL) {
        // 第一个vma，从USER_START开始
        start_addr = USER_START;
    } else {
        // 在最后一个vma之后分配，加上4KB间隙，页对齐
        start_addr = last_vma->end + 4096;
        start_addr = (start_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }
    
    // 检查是否超过栈区域
    if (start_addr + total_size > USER_STACK_TOP) {
        return 0;
    }
    
    return start_addr;
}

/*
 * 解除映射
 *
 * @param as 进程地址空间
 * @param addr 虚拟地址
 *
 * @return VMM_OK 成功，否则错误码
 */
vmm_result_t as_unmap(as_t *as, uintptr_t addr) {
    if (as == NULL) {
        return VMM_INVALID_ARGUMENT;
    }

    // 查找对应的vma
    vm_area_t *vma = vma_find(as, addr);
    if (vma == NULL) {
        return VMM_INVALID_ADDRESS;
    }

    // 读取vma的 start 和 end 用于tlb刷新
    uintptr_t start, end;
    vma_range(vma, &start, &end);

    // 释放页表页
    page_table_blocks_struct *ptb = &vma->page_table_blocks;
    mmu_remove_map(ptb);
    
    // 释放线性地址
    if (vma->linear_addr != 0) {
        kheap_free((void *)vma->linear_addr);
    }
    
    // 根据映射大小选择TLB刷新策略
    uint64_t page_count = (end - start) / PAGE_SIZE;
    if (page_count > TLB_FLUSH_THRESHOLD_PAGES) {
        // 大范围，刷新整个TLB
        mmu_invalidate_all();
    } else {
        // 小范围，逐页刷新
        for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
            mmu_invalidate(va);
        }
    }
    
    // 从地址空间中移除vma
    return vma_remove(as, vma);
}