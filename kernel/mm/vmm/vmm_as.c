/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "vmm_as.h"
#include <heap.h>
#include <mm/bootmem/linear_map.h>
#include <list.h>
#include <libtree.h>
#include <shizi/string.h>

// 字段信息表，用于通用读写
static const struct {
    size_t offset;
    size_t size;
    bool writable;
} vma_field_info[] = {
    [VMA_FIELD_START]  = { offsetof(vm_area_t, start),       sizeof(uintptr_t), false },
    [VMA_FIELD_END]    = { offsetof(vm_area_t, end),         sizeof(uintptr_t), false },
    [VMA_FIELD_LINEAR] = { offsetof(vm_area_t, linear_addr), sizeof(uintptr_t), true  },
    [VMA_FIELD_PROT]   = { offsetof(vm_area_t, prot),        sizeof(vm_prot_t), false },
    [VMA_FIELD_FLAGS]  = { offsetof(vm_area_t, flags),       sizeof(uint8_t),   false },
};

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

/*
 * 读取VMA指定字段的值
 *
 * @param vma VMA指针
 * @param field 要读取的字段
 * @param out_data 输出缓冲区
 *
 * @return VMM_OK 成功
 * @return VMM_INVALID_ARGUMENT 参数无效
 */
vmm_result_t vma_read(const vm_area_t *vma, vma_field_t field, void *out_data) {
    if (!vma || !out_data || field >= sizeof(vma_field_info)/sizeof(vma_field_info[0]))
        return VMM_INVALID_ARGUMENT;

    const typeof(vma_field_info[0]) *info = &vma_field_info[field];
    memcpy(out_data, (const uint8_t*)vma + info->offset, info->size);
    return VMM_OK;
}

/*
 * 写入VMA指定字段的值
 *
 * @param vma VMA指针
 * @param field 要写入的字段
 * @param data 待写入数据
 *
 * @return VMM_OK 成功
 * @return VMM_INVALID_ARGUMENT 参数无效或字段不可写
 */
vmm_result_t vma_write(vm_area_t *vma, vma_field_t field, const void *data) {
    if (!vma || !data || field >= sizeof(vma_field_info)/sizeof(vma_field_info[0]))
        return VMM_INVALID_ARGUMENT;

    const typeof(vma_field_info[0]) *info = &vma_field_info[field];
    if (!info->writable)
        return VMM_INVALID_ARGUMENT;

    memcpy((uint8_t*)vma + info->offset, data, info->size);
    return VMM_OK;
}

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

    spinlock_init(&vaddr->lock);
    vaddr->pgd = pgd;
    INIT_LIST_HEAD(&vaddr->vma_list);
    rbtree_init(&vaddr->vma_tree, vma_rb_cmp, 0);

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
vmm_result_t vma_add_nolock(as_t *as, uintptr_t start, uintptr_t end,
                            vm_prot_t prot, uint8_t flags) {
    // 参数检查
    if (as == NULL) {
        return VMM_INVALID_ARGUMENT;
    }
    
    // 地址有效性检查
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
 * 添加vma
 *
 * @param as 进程地址空间的虚拟地址
 * @param start 这段区域的起始虚拟地址
 * @param end 这段区域的结束虚拟地址
 * @param prot 权限标志
 * @param flags 映射标志
 */
vmm_result_t vma_add(as_t *as, uintptr_t start, uintptr_t end,
                     vm_prot_t prot, uint8_t flags) {
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
        return NULL;
    }

    vm_area_t *candidate = vma_rb_find_le(&as->vma_tree, addr);
    if (candidate && addr < candidate->end) {
        return candidate;
    }
    return NULL;
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
        return NULL;
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

    // 遍历链表，计算VMA数量
    uint64_t count = 0;
    vm_area_t *pos;
    list_for_each_entry(pos, &as->vma_list, list_node) {
        count++;
    }

    // 创建结构体用于返回
    uint64_t size = sizeof(vma_result_t) + count * sizeof(vma_data_t);
    vma_result_t *vma_result = kheap_alloc(size);
    if (vma_result == NULL) {
        return VMM_OUT_OF_MEMORY;
    }

    vma_result->addr_count = count;
    uint64_t i = 0;
    list_for_each_entry(pos, &as->vma_list, list_node) {
        vma_result->vma_data[i].start_addr = pos->start;
        vma_result->vma_data[i].end_addr = pos->end;
        vma_result->vma_data[i].linear = pos->linear_addr;
        vma_result->vma_data[i].prot = pos->prot;
        vma_result->vma_data[i].flags = pos->flags;
        i++;
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

    // 不加锁，由调用者负责
    struct rbtree_node *last_node = rbtree_last(&as->vma_tree);
    return last_node ? container_of(last_node, vm_area_t, rb_node) : NULL;
}