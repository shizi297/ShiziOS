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
 * 处理缺页异常
 *
 * @param as 进程地址空间
 * @param fault_addr 触发缺页的虚拟地址
 * @param access_flags 访问类型
 */
int vmm_handle_fault(as_t *as, uintptr_t fault_addr, uint32_t access_flags) {
    if (!as) return -EINVAL;

    uintptr_t fault_page = fault_addr & ~(PAGE_SIZE - 1);

    // 查找 VMA
    as_lock(as);
    vm_area_t *vma = vma_find(as, fault_addr);
    if (!vma) {
        as_unlock(as);
        return -EFAULT;
    }

    // 检查权限
    if (!(vma->prot & access_flags)) {
        as_unlock(as);
        return -EACCES;
    }

    // 拷贝必要信息后释放锁，因为 vma_read_page 可能睡眠
    uintptr_t start = vma->start;
    uintptr_t end = vma->end;
    vm_prot_t prot = vma->prot;
    uint8_t flags = vma->flags;
    as_unlock(as);

    // 分配物理页
    void *linear = kheap_alloc(PAGE_SIZE);
    if (!linear) return -ENOMEM;

    // 填充页面内容
    int ret = vma_read_page(vma, fault_addr, linear);
    if (ret < 0) {
        kheap_free(linear);
        return ret;
    }

    // 重新持锁，确认 VMA 仍然有效
    as_lock(as);
    vma = vma_find(as, fault_addr);
    if (!vma || vma->start != start || vma->end != end) {
        as_unlock(as);
        kheap_free(linear);
        return -EAGAIN;
    }

    // 建立页表映射
    uintptr_t phys_addr = LINEAR_TO_PHYS((uintptr_t)linear);
    page_table_blocks_struct ptb;
    ret = mmu_add_map(
        as_get_pgd(as),
        fault_page,
        phys_addr,
        1,
        prot,
        flags,
        &ptb
    );
    if (ret < 0) {
        as_unlock(as);
        kheap_free(linear);
        return ret;
    }

    // 记录映射信息
    vma_set_map(vma, (uintptr_t)linear, &ptb);
    as_unlock(as);

    // 刷新 TLB
    mmu_invalidate(fault_page);

    return 0;
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

/*
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
        vm_prot_t prot = vma->prot;
        uint8_t flags = vma->flags;

        // 在新地址空间添加vma
        int ret = vma_add(new_as, start, end, prot, flags);
        if (ret < 0) goto fail;

        // 找到刚添加的vma
        vm_area_t *new_vma = vma_find(new_as, start);
        if (!new_vma) goto fail;

        if (vma->offset_or_anon < 0) {
            // 匿名映射：复制 anon_vma 并增加引用计数
            new_vma->offset_or_anon = -1;
            new_vma->anon_vma = vma->anon_vma;
            if (vma->anon_vma) {
                vma->anon_vma->refcount++;
            }
            new_vma->file = NULL;
            new_vma->file_path = NULL;
        } else {
            // 文件映射：重新打开文件，获得独立的 file 对象
            if (vma->file_path) {
                kptr res = vfs_open(vma->file_path, O_RDONLY, 0, NULL);
                if (K_IS_ERR(res)) {
                    goto fail;
                }
                struct file *new_file = (struct file *)res.ptr;
                new_vma->offset_or_anon = vma->offset_or_anon;
                new_vma->file = new_file;
                new_vma->file_path = strdup(vma->file_path);
                new_vma->anon_vma = NULL;
            } else {
                goto fail;
            }
        }

        // 清空物理映射相关字段（物理页由缺页处理分配）
        new_vma->linear_addr = 0;
        memset(&new_vma->page_table_blocks, 0, sizeof(page_table_blocks_struct));
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
 * @param flags 映射标志（当前必须传 0）
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

    // 自动分配地址
    if (addr == 0) {
        addr = as_alloc_addr(as, size);
        if (addr == 0) {
            as_unlock(as);
            return (kuptr)K_ERR(-ENOMEM);
        }
    } else {
        if (addr < USER_START || addr + size > USER_STACK_TOP) {
            as_unlock(as);
            return (kuptr)K_ERR(-EFAULT);
        }
        if ((addr & (PAGE_SIZE - 1)) != 0) {
            as_unlock(as);
            return (kuptr)K_ERR(-EINVAL);
        }
    }

    // 添加 VMA
    int ret = vma_add(as, addr, addr + size, prot, flags);
    if (ret < 0) {
        as_unlock(as);
        return (kuptr)K_ERR(ret);
    }

    vm_area_t *vma = vma_find(as, addr);
    if (vma == NULL) {
        as_unlock(as);
        return (kuptr)K_ERR(-EIO);
    }

    // 设置为匿名映射
    vma_set_anon(vma, anon_vma);

    if (alloc) {
        // 预分配物理内存
        uintptr_t alloc_addr = (uintptr_t)kheap_alloc(size);
        if (alloc_addr == 0) {
            vma_remove(as, vma);
            as_unlock(as);
            return (kuptr)K_ERR(-ENOMEM);
        }

        // 建立映射
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
 * @param path 文件路径
 * @param offset 文件内偏移（必须页对齐）
 * @param pwd 当前工作目录
 *
 * @return 映射的虚拟地址
 */
kuptr vmm_map_file(
    as_t *as,
    uintptr_t addr,
    uint64_t size,
    vm_prot_t prot,
    uint8_t flags,
    const char *path,
    uint64_t offset,
    const struct path *pwd
) {
    if (as == NULL || path == NULL || size == 0) {
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

    // 打开文件，生命周期由 VMM 管理
    kptr file_res = vfs_open(path, O_RDONLY, 0, pwd);
    if (K_IS_ERR(file_res)) {
        return (kuptr)K_ERR(file_res.err);
    }
    struct file *file = (struct file *)file_res.ptr;

    uint64_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    as_lock(as);

    // 自动分配地址
    if (addr == 0) {
        addr = as_alloc_addr(as, aligned_size);
        if (addr == 0) {
            as_unlock(as);
            vfs_close(file);
            return (kuptr)K_ERR(-ENOMEM);
        }
    }

    // 添加 VMA
    int ret = vma_add(as, addr, addr + aligned_size, prot, flags);
    if (ret < 0) {
        as_unlock(as);
        vfs_close(file);
        return (kuptr)K_ERR(ret);
    }

    vm_area_t *vma = vma_find(as, addr);
    if (vma == NULL) {
        as_unlock(as);
        vfs_close(file);
        return (kuptr)K_ERR(-EIO);
    }

    // 设置为文件映射
    vma_set_file(vma, file, offset, path);

    as_unlock(as);
    return (kuptr)K_OK(addr);
}