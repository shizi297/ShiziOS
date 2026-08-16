/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "files.h"
#include <heap.h>
#include <klibc.h>
#include <errno.h>
#include <vfs.h>

#define MAX_FD 1024

/*
 * 文件描述符表
 * 每个任务拥有一张独立或共享的表，
 * 用于将整数 fd 映射到内核文件对象
 * fd_table 中存储 struct file *，NULL 表示该槽位空闲
 */
struct files_descriptor {
    __ktype(struct file *) dynarr_t *fd_table;
    spinlock_t lock;
    atomic_t refcount;
};

/*
 * 分配一个新的文件描述符表
 *
 * @return 文件描述符表指针
 */
struct files_descriptor *task_files_alloc(void) {
    struct files_descriptor *fdt = kheap_alloc(sizeof(*fdt));
    if (!fdt) return NULL;

    fdt->fd_table = dynarr_create(sizeof(struct file *), MAX_FD);
    if (!fdt->fd_table) {
        kheap_free(fdt);
        return NULL;
    }

    spinlock_init(&fdt->lock);
    atomic_init(&fdt->refcount, 1);

    return fdt;
}

/*
 * 增加文件描述符表的引用计数
 *
 * @param fdt 文件描述符表
 */
void task_files_get(struct files_descriptor *fdt) {
    if (!fdt) return;
    atomic_fetch_add(&fdt->refcount, 1);
}

/*
 * 减少文件描述符表的引用计数，归零时释放
 *
 * @param fdt 文件描述符表
 */
void task_files_put(struct files_descriptor *fdt) {
    if (!fdt) return;

    int new_ref = atomic_fetch_sub(&fdt->refcount, 1) - 1;
    if (new_ref == 0) {
        // 关闭所有仍打开的文件
        uint64_t count = dynarr_count(fdt->fd_table);
        for (uint64_t i = 0; i < count; i++) {
            struct file *file = (struct file *)dynarr_get(fdt->fd_table, i);
            if (file) {
                vfs_close(file);
            }
        }
        dynarr_destroy(fdt->fd_table);
        kheap_free(fdt);
    }
}

/*
 * 分配一个空闲的文件描述符编号
 *
 * @param fdt 文件描述符表
 * @param out_fd 输出分配到的 fd
 */
int task_files_alloc_fd(struct files_descriptor *fdt, uint32_t *out_fd) {
    if (!fdt || !out_fd) return -EINVAL;

    spin_lock(&fdt->lock);

    uint64_t count = dynarr_count(fdt->fd_table);
    uint64_t i;

    // 遍历查找第一个 NULL 槽位
    for (i = 0; i < count; i++) {
        struct file *file = (struct file *)dynarr_get(fdt->fd_table, i);
        if (file == NULL) {
            *out_fd = (uint32_t)i;
            spin_unlock(&fdt->lock);
            return 0;
        }
    }

    // 没有空闲槽位，检查是否已达到最大限制
    if (count >= MAX_FD) {
        spin_unlock(&fdt->lock);
        return -EMFILE;
    }

    // 在末尾追加一个 NULL
    if (!dynarr_append(fdt->fd_table, NULL)) {
        spin_unlock(&fdt->lock);
        return -EMFILE;
    }

    *out_fd = (uint32_t)count;
    spin_unlock(&fdt->lock);
    return 0;
}

/*
 * 将文件对象安装到指定的文件描述符编号
 *
 * @param fdt 文件描述符表
 * @param fd 文件描述符编号
 * @param file 文件对象指针
 */
int task_files_install(struct files_descriptor *fdt, int fd, struct file *file) {
    if (!fdt || fd < 0 || fd >= MAX_FD || !file) return -EINVAL;

    spin_lock(&fdt->lock);

    // 检查目标 fd 是否已被占用
    struct file *existing = (struct file *)dynarr_get(fdt->fd_table, (uint64_t)fd);
    if (existing != NULL) {
        spin_unlock(&fdt->lock);
        return -EBUSY;
    }

    // 增加文件对象引用计数
    vfs_file_get(file);

    // 安装文件指针
    if (!dynarr_set(fdt->fd_table, (uint64_t)fd, file)) {
        vfs_file_put(file);
        spin_unlock(&fdt->lock);
        return -ENOMEM;
    }

    spin_unlock(&fdt->lock);
    return 0;
}

/*
 * 通过文件描述符编号获取文件对象
 *
 * @param fdt 文件描述符表
 * @param fd 文件描述符编号
 *
 * @return 文件对象指针（已添加引用）
 */
struct file *task_files_open(struct files_descriptor *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= MAX_FD) return NULL;

    spin_lock(&fdt->lock);

    uint64_t count = dynarr_count(fdt->fd_table);
    if ((uint64_t)fd >= count) {
        spin_unlock(&fdt->lock);
        return NULL;
    }

    struct file *file = (struct file *)dynarr_get(fdt->fd_table, (uint64_t)fd);
    if (file) 
        vfs_file_get(file);   
    
    spin_unlock(&fdt->lock);
    return file;
}

/*
 * 关闭文件描述符
 *
 * @param fdt 文件描述符表
 * @param fd 文件描述符编号
 */
int task_files_close(struct files_descriptor *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= MAX_FD) return -EINVAL;

    spin_lock(&fdt->lock);

    uint64_t count = dynarr_count(fdt->fd_table);
    if ((uint64_t)fd >= count) {
        spin_unlock(&fdt->lock);
        return -EBADF;
    }

    struct file *file = (struct file *)dynarr_get(fdt->fd_table, (uint64_t)fd);
    if (!file) {
        spin_unlock(&fdt->lock);
        return -EBADF;
    }

    // 清空槽位
    dynarr_set(fdt->fd_table, (uint64_t)fd, NULL);

    spin_unlock(&fdt->lock);

    // 释放文件对象（不在锁内，因为 vfs_close 可能睡眠）
    vfs_close(file);

    return 0;
}

/*
 * 从源文件描述符表中复制指定 fd 列表到目标表
 *
 * @param dst 目标文件描述符表
 * @param src 源文件描述符表
 * @param fd_list 要复制的 fd 编号数组
 * @param fd_count 数组长度
 */
int task_files_copy_list(
    struct files_descriptor *dst,
    struct files_descriptor *src,
    int *fd_list,
    uint32_t fd_count
) {
    if (!dst || !src || (!fd_list && fd_count > 0)) return -EINVAL;

    if (fd_count == 0) return 0;

    uint32_t copied_count = 0;
    int err = 0;

    spin_lock(&src->lock);
    spin_lock(&dst->lock);

    for (uint32_t i = 0; i < fd_count; i++) {
        int fd = fd_list[i];
        if (fd < 0 || fd >= MAX_FD) {
            err = -EINVAL;
            break;
        }

        // 检查源表 fd 是否有效
        uint64_t src_count = dynarr_count(src->fd_table);
        if ((uint64_t)fd >= src_count) {
            err = -EBADF;
            break;
        }

        struct file *file = (struct file *)dynarr_get(src->fd_table, (uint64_t)fd);
        if (!file) {
            err = -EBADF;
            break;
        }

        // 检查目标表该 fd 是否为空（同时起到去重和冲突检测的作用）
        if (dynarr_get(dst->fd_table, (uint64_t)fd) != NULL) {
            err = -EBUSY;
            break;
        }

        // 增加文件对象引用计数
        vfs_file_get(file);

        // 安装文件指针
        if (!dynarr_set(dst->fd_table, (uint64_t)fd, file)) {
            vfs_file_put(file);
            err = -ENOMEM;
            break;
        }

        copied_count++;
    }

    spin_unlock(&dst->lock);
    spin_unlock(&src->lock);

    if (err) {
        // 逆序回滚：只回滚已复制的 fd
        for (uint32_t j = copied_count; j > 0; j--) {
            int fd = fd_list[j - 1];

            spin_lock(&dst->lock);
            if ((uint64_t)fd < dynarr_count(dst->fd_table)) {
                dynarr_set(dst->fd_table, (uint64_t)fd, NULL);
            }
            spin_unlock(&dst->lock);

            spin_lock(&src->lock);
            struct file *file = NULL;
            if ((uint64_t)fd < dynarr_count(src->fd_table)) {
                file = (struct file *)dynarr_get(src->fd_table, (uint64_t)fd);
            }
            spin_unlock(&src->lock);

            if (file) {
                vfs_close(file);
            }
        }
        return err;
    }

    return 0;
}