/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "vmm_as.h"
#include "vmm_map.h"
#include "vmm_types.h"
#include <heap.h>
#include <mm/linear_map.h>
#include <mm/vmm_mmu.h>
#include <stdint.h>
#include <stddef.h>
#include <kio.h>
#include <klibc.h>
#include <errno.h>
#include <vfs.h>

#define VMM_PANIC(fmt, ...) \
    printp("[VMM] ERROR : " fmt, ##__VA_ARGS__)

static uintptr_t mmio_addr = MMIO_MAP;
static spinlock_t mmio_map_lock = SPIN_LOCK_INIT;
static as_t *kernel_as = NULL;

// 初始化
void vmm_init(void) {
    mmu_init();
    uintptr_t kernel_pgd_phys = mmu_get_kernel_pgd();
    kernel_as = as_create(kernel_pgd_phys);
    if (!kernel_as) VMM_PANIC("Failed to create kernel address space\n");
}

// 获取内核地址空间
as_t *vmm_get_kernel_as(void) {
    return kernel_as;
}

// 获取内核的页全局目录
uintptr_t vmm_get_kernel_pgd(void) {
    return mmu_get_kernel_pgd();
}

/*
 * 创建一个新的进程地址空间（自动分配页全局目录）
 *
 * @return 进程地址空间的虚拟地址
 */
as_t *vmm_create_as(void) {
    uintptr_t pgd_phys = 0;
    int ret = mmu_add_pgd(&pgd_phys);
    if (ret < 0 || pgd_phys == 0) {
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
 * 解除映射
 *
 * @param as 进程地址空间
 * @param addr 虚拟地址
 */
int vmm_unmap(as_t *as, uintptr_t addr) {
    if (as == NULL) {
        return -EINVAL;
    }
    as_lock(as);
    int ret = as_unmap(as, addr);
    as_unlock(as);
    return ret;
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
    mmu_set_pgd(as_get_pgd(as));
}

/**
 * 映射mmio地址
 *
 * @param phy_addr 物理地址
 * @param page_count 大小
 *
 * @return 映射的虚拟内存
 */
uintptr_t vmm_map_mmio(uint64_t phy_addr, uint64_t page_count) {
    spin_lock(&mmio_map_lock);

    vm_prot_t mmio_prot = VM_READ | VM_WRITE | VM_UC;
    uintptr_t current_mmio = mmio_addr;
     
    page_table_blocks_struct *ptb = kheap_alloc(PAGE_SIZE);
    if (!ptb) goto fail;

    int ret = mmu_add_map(
        mmu_get_kernel_pgd(),
        mmio_addr,
        (uintptr_t)phy_addr,
        page_count,
        mmio_prot,
        0,
        ptb
    );

    if (ret < 0) goto fail;

    // 设置下一次映射的虚拟起始地址
    mmio_addr += page_count * PAGE_SIZE;

    spin_unlock(&mmio_map_lock);

    return current_mmio;

    fail:
        spin_unlock(&mmio_map_lock);
        return 0;
}

// 增加as的引用计数
void vmm_as_add_ref(as_t *as) {
    as_add_ref(as);
}

/*
 * 销毁进程地址空间
 *
 * @param as 进程地址空间的虚拟地址
 */
void vmm_destroy_as(as_t *as) {
    if (as == NULL) {
        return;
    }
    as_lock(as);
    as_destroy(as);
    as_unlock(as);
}

/**
 * 复制地址空间
 * 自动分配新的页表页与物理内存
 *
 * @param as 进程地址空间的虚拟地址
 *
 * @return 进程地址空间的虚拟地址
 */
as_t *vmm_copy_as(as_t *as) {
    if (!as) return NULL;

    // 创建新地址空间
    as_t *new_as = vmm_create_as();
    if (!new_as) return NULL;

    as_lock(as);
    as_lock(new_as);

    // 遍历原地址空间的所有vma
    vm_area_t *vma;
    list_for_each_entry(vma, &as->vma_list, list_node) {
        uintptr_t start, end;
        vma_range(vma, &start, &end);
        uint64_t size = end - start;
        vm_prot_t prot = vma->prot;
        uint8_t flags = vma->flags;
        uintptr_t linear = vma->linear_addr;

        // 在新地址空间添加vma
        int ret = vma_add(new_as, start, end, prot, flags);
        if (ret < 0) goto fail;

        // 如果原vma已分配物理内存，则复制
        if (linear != 0) {
            void *new_linear = kheap_alloc(size);
            if (!new_linear) goto fail;
            memcpy(new_linear, (void*)linear, size);
            uintptr_t paddr = LINEAR_TO_PHYS((uintptr_t)new_linear);
            page_table_blocks_struct ptb;
            ret = mmu_add_map(as_get_pgd(new_as), start, paddr, size / PAGE_SIZE, prot, flags, &ptb);
            if (ret < 0) {
                kheap_free(new_linear);
                goto fail;
            }
            // 找到刚添加的vma
            vm_area_t *new_vma = vma_find(new_as, start);
            if (!new_vma) {
                kheap_free(new_linear);
                goto fail;
            }
            // 设置线性地址和页表块
            vma_set_map(new_vma, (uintptr_t)new_linear, &ptb);
        }
    }

    as_unlock(new_as);
    as_unlock(as);
    return new_as;

fail:
    as_unlock(new_as);
    as_unlock(as);
    vmm_destroy_as(new_as);
    return NULL;
}

/*
 * 映射匿名内存区域
 *
 * @param as 进程地址空间
 * @param addr 要映射的虚拟地址，如果为0则自动分配
 * @param page 映射页数
 * @param prot 映射权限
 * @param flags 映射标志
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
) {
    if (as == NULL || page == 0) {
        return (kuptr)K_ERR(-EINVAL);
    }
    
    uint64_t size = page * PAGE_SIZE;
    
    as_lock(as);
    
    // 如果addr为0，自动分配地址
    if (addr == 0) {
        addr = as_alloc_addr(as, size);
        if (addr == 0) {
            as_unlock(as);
            return (kuptr)K_ERR(-ENOMEM);
        }
        // 自动分配的地址已经满足所有条件，跳过后续检查
    } else {
        // 手动指定地址，需要检查一些参数
        if (addr < USER_START || addr + size > USER_STACK_TOP) {
            as_unlock(as);
            return (kuptr)K_ERR(-EFAULT);
        }
        
        // 检查地址是否页对齐
        if ((addr & (PAGE_SIZE - 1)) != 0) {
            as_unlock(as);
            return (kuptr)K_ERR(-EINVAL);
        }
    }
    
    // 添加vma
    int ret = vma_add(as, addr, addr + size, prot, flags);
    if (ret < 0) {
        as_unlock(as);
        return (kuptr)K_ERR(ret);
    }
    
    // 查找刚添加的vma
    vm_area_t *vma = vma_find(as, addr);
    if (vma == NULL) {
        as_unlock(as);
        return (kuptr)K_ERR(-EIO);
    }
    
    // 设置为匿名映射
    vma->offset_or_anon = -1;
    vma->anon_vma = anon_vma;
    
    if (alloc) {
        // 预分配物理内存
        uintptr_t alloc_addr = (uintptr_t)kheap_alloc(size);
        if (alloc_addr == 0) {
            // 分配失败，移除vma
            vma_remove(as, vma);
            as_unlock(as);
            return (kuptr)K_ERR(-ENOMEM);
        }
        
        // 建立映射，传入page_table_blocks指针
        uintptr_t paddr_start = LINEAR_TO_PHYS(alloc_addr);
        page_table_blocks_struct ptb;
        ret = mmu_add_map(as_get_pgd(as), addr, paddr_start, page, prot, flags, &ptb);
        if (ret < 0) {
            kheap_free((void *)alloc_addr);
            vma_remove(as, vma);
            as_unlock(as);
            return (kuptr)K_ERR(ret);
        }
        vma_set_map(vma, alloc_addr, &ptb);
    }
    
    as_unlock(as);
    
    return (kuptr)K_OK(addr);
}

/*
 * 映射文件到进程地址空间
 *
 * @param as 进程地址空间
 * @param addr 建议的虚拟地址（0 表示自动分配）
 * @param size 映射大小（字节）
 * @param prot 内存权限
 * @param flags 映射标志（当前必须传 0）
 * @param file 已打开的 file 结构体指针（由 VFS 层提供）
 * @param offset 文件内偏移（必须页对齐）
 *
 * @return 映射的虚拟地址
 */
kuptr vmm_map_file(
    as_t *as,
    uintptr_t addr,
    uint64_t size,
    vm_prot_t prot,
    uint8_t flags,
    struct file *file,
    uint64_t offset
) {
    if (as == NULL || file == NULL || size == 0) {
        return (kuptr)K_ERR(-EINVAL);
    }
    if ((offset & (PAGE_SIZE - 1)) != 0) {
        return (kuptr)K_ERR(-EINVAL);
    }
    if (addr != 0) {
        if ((addr & (PAGE_SIZE - 1)) != 0) {
            return (kuptr)K_ERR(-EINVAL);
        }
        if (addr < USER_START || addr + size > USER_STACK_TOP) {
            return (kuptr)K_ERR(-EFAULT);
        }
    }

    uint64_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    as_lock(as);

    if (addr == 0) {
        addr = as_alloc_addr(as, aligned_size);
        if (addr == 0) {
            as_unlock(as);
            return (kuptr)K_ERR(-ENOMEM);
        }
    }

    int ret = vma_add(as, addr, addr + aligned_size, prot, flags);
    if (ret < 0) {
        as_unlock(as);
        return (kuptr)K_ERR(ret);
    }

    vm_area_t *vma = vma_find(as, addr);
    if (vma == NULL) {
        as_unlock(as);
        return (kuptr)K_ERR(-EIO);
    }

    // 设置为文件映射
    vma->offset_or_anon = (int64_t)offset;
    vma->file = file;

    as_unlock(as);
    return (kuptr)K_OK(addr);
}