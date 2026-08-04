/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <stdint.h>
#include <stddef.h>
#include <block.h>
#include <heap.h>
#include <kio.h>
#include <list.h>
#include <klibc.h>
#include <vfs.h>
#include <dynarr.h>
#include <shizi/types.h>

#define BLOCK_DEVICE_PATH_MAX 32

#define BLOCK_PRINT(fmt, ...) \
    printk("[BLOCK] " fmt, ##__VA_ARGS__)

#define BLOCK_TYPE_INFO(name) \
    printk("[BLOCK_INFO] register block type : [\"name\" = \"%s\"\n", name)

/**
 * 块设备协议类型
 * 每种底层协议对应一个实例
 */
struct block_type {
    const char *name;        // 协议名
    uint32_t counter;        // 设备编号计数器，用于生成 dev 节点名字
    drivers_minor_devt *major_handle;   // 主设备号句柄
};

typedef struct {
    void *priv;
    struct device *dev;
    uint64_t sector_count;  // 设备总扇区数
    uint64_t start_sector;  // 分区起始扇区数，整盘为 0
    dev_t devt;
    struct block_type *type;
    char path[BLOCK_DEVICE_PATH_MAX];
    struct list_head node;
} block_dev_info;

static struct list_head block_devt_to_info;

static int block_open(struct inode *inode, struct file *file) {
    dev_t rdev = vfs_inode_get_rdev(inode);
    block_dev_info *info;
    struct list_head *pos;

    list_for_each(pos, &block_devt_to_info) {
        info = list_entry(pos, block_dev_info, node);
        if (info->devt == rdev) {
            vfs_file_set_private(file, info);
            return 0;
        }
    }

    return -ENODEV;
}

static int block_release(struct inode *inode, struct file *file) {
    (void)inode;
    (void)file;
    return 0;
}

static ssize_t block_read(
    struct file *file,
    char *buf,
    size_t count,
    off_t *pos
) {
    block_dev_info *info = vfs_file_get_private(file);
    struct block_hdr *hdr = info->priv;
    uint64_t sector, sector_count;
    int ret;

    // 要求 512 字节对齐
    if ((*pos & 511) || (count & 511))
        return -EINVAL;

    sector = *pos >> 9;
    sector_count = count >> 9;

    ret = hdr->read(info->priv, info->dev, sector + info->start_sector, buf, sector_count);
    if (ret == 0)
        *pos += count;
    return ret == 0 ? count : ret;
}

static ssize_t block_write(
    struct file *file,
    const char *buf,
    size_t count,
    off_t *pos
) {
    block_dev_info *info = vfs_file_get_private(file);
    struct block_hdr *hdr = info->priv;
    uint64_t sector, sector_count;
    int ret;

    if ((*pos & 511) || (count & 511))
        return -EINVAL;

    sector = *pos >> 9;
    sector_count = count >> 9;

    ret = hdr->write(info->priv, info->dev, sector + info->start_sector, buf, sector_count);
    if (ret == 0)
        *pos += count;
    return ret == 0 ? count : ret;
}

static off_t block_llseek(struct file *file, off_t offset, seek_whence_t whence) {
    block_dev_info *info = vfs_file_get_private(file);
    off_t size = info->sector_count * 512;
    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_END:
            new_pos = size + offset;
            break;
        default:
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > size)
        return -EINVAL;

    return new_pos;
}

static int block_fsync(struct file *file, bool meta) {
    (void)meta;

    block_dev_info *info = vfs_file_get_private(file);
    struct block_hdr *hdr = info->priv;

    return hdr->flush(info->priv, info->dev);
}

// 文件操作表
static struct file_operations block_file_operations = {
    .open    = block_open,
    .release = block_release,
    .read    = block_read,
    .write   = block_write,
    .llseek  = block_llseek,
    .fsync   = block_fsync,
};

/**
 * 注册设备到块系统
 * 
 * @param name 创建的设备节点名称
 * 
 * @return 句柄
 */
struct block_type *block_register_type(const char *name) {
    struct block_type *type;

    type = kheap_alloc(sizeof(struct block_type));
    if (!type)
        return NULL;

    type->name = name;
    type->counter = 0;

    type->major_handle = drivers_major_alloc();
    if (!type->major_handle) {
        kheap_free(type);
        return NULL;
    }

    // 注册统一的 VFS 函数到 fops
    drivers_register_fops(type->major_handle, &block_file_operations);

    BLOCK_TYPE_INFO(type->name);

    return type;
}

/*
 * 添加设备到块系统
 *
 * @param type 块设备类型句柄
 * @param dev 设备指针
 * @param priv 块设备私有数据
 * @param sector_count 设备总扇区数
 */
int block_add_device(
    struct block_type *type,
    struct device *dev,
    void *priv,
    uint64_t sector_count
) {
    dev_t devt;
    int ret;
    block_dev_info *info;
    struct path *root;

    // 分配次设备号
    ret = drivers_minor_alloc(type->major_handle, &devt);
    if (ret < 0)
        return ret;

    // 分配并填充设备信息结构体
    info = kheap_alloc(sizeof(block_dev_info));
    if (!info) {
        ret = -ENOMEM;
        goto err_minor;
    }
    info->priv = priv;
    info->dev = dev;
    info->sector_count = sector_count;
    info->start_sector = 0; // 表示整盘
    info->devt = devt;
    info->type = type;
    INIT_LIST_HEAD(&info->node);

    // 挂入链表
    list_add_tail(&info->node, &block_devt_to_info);

    // 创建设备节点，路径格式为 /dev/nameX
    snprintk(info->path, sizeof(info->path), "/dev/%s%u", type->name, type->counter);
    root = vfs_get_root_path();
    ret = vfs_mknod(info->path, S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, devt, root);
    vfs_path_put(root);

    if (ret < 0)
        goto err_list;

    type->counter++;

    // GPT 解析与分区节点创建
    dynarr_t *partitions = dynarr_create(sizeof(struct block_partition_info), 0);
    if (partitions) {
        if (gpt_parse(devt, partitions)) {
            uint64_t count = dynarr_count(partitions);
            for (uint64_t i = 0; i < count; i++) {
                struct block_partition_info *pinfo = dynarr_get(partitions, i);
                if (!pinfo)
                    continue;

                // 分配次设备号
                dev_t part_devt;
                int ret = drivers_minor_alloc(type->major_handle, &part_devt);
                if (ret < 0)
                    continue; // 跳过该分区，继续处理下一个

                // 分配并填充分区 block_dev_info
                block_dev_info *part_info = kheap_alloc(sizeof(block_dev_info));
                if (!part_info) {
                    drivers_minor_free(type->major_handle, part_devt);
                    continue;
                }

                part_info->priv = priv;
                part_info->dev = dev;
                part_info->sector_count = pinfo->size;
                part_info->start_sector = pinfo->start_lba;
                part_info->devt = part_devt;
                part_info->type = type;
                INIT_LIST_HEAD(&part_info->node);

                // 挂入全局链表
                list_add_tail(&part_info->node, &block_devt_to_info);

                // 生成分区设备节点路径：/dev/nameXpN
                snprintk(
                    part_info->path, sizeof(part_info->path),
                    "/dev/%s%up%u", type->name, type->counter, (unsigned int)(i + 1)
                );

                struct path *root = vfs_get_root_path();
                ret = vfs_mknod(
                    part_info->path,
                    S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
                    part_devt, root
                );

                vfs_path_put(root);

                if (ret < 0) {
                    // 创建节点失败，回滚
                    list_del(&part_info->node);
                    kheap_free(part_info);
                    drivers_minor_free(type->major_handle, part_devt);
                    
                    // 继续处理下一个分区
                }
            }
        }
        dynarr_destroy(partitions);
    }

    return 0;

err_list:
    list_del(&info->node);
    kheap_free(info);
err_minor:
    drivers_minor_free(type->major_handle, devt);
    return ret;
}

/*
 * 从块设备层移除设备
 *
 * @param dev 设备指针
 */
int block_remove_device(struct device *dev) {
    block_dev_info *info;
    struct list_head *pos, *tmp;
    struct path *root;
    int ret = -ENODEV;

    list_for_each_safe(pos, tmp, &block_devt_to_info) {
        info = list_entry(pos, block_dev_info, node);
        if (info->dev == dev) {
            // 从链表中删除
            list_del(&info->node);

            // 删除设备节点
            root = vfs_get_root_path();
            vfs_unlink(info->path, root);
            vfs_path_put(root);

            // 归还次设备号
            drivers_minor_free(info->type->major_handle, info->devt);

            // 释放 info
            kheap_free(info);
            ret = 0;

            // 继续删除其他匹配项
        }
    }

    return ret;
}

bool block_init(void) {
    INIT_LIST_HEAD(&block_devt_to_info);

    BLOCK_PRINT("block init success");
    return true;
}

/*
 * 从设备读取扇区
 *
 * @param dev 设备号
 * @param sector 起始扇区号
 * @param buf 数据缓冲区
 * @param count 扇区数量
 */
int block_read_sectors(dev_t dev, uint64_t sector, void *buf, size_t count) {
    block_dev_info *info;
    struct list_head *pos;
    struct block_hdr *hdr;

    list_for_each(pos, &block_devt_to_info) {
        info = list_entry(pos, block_dev_info, node);
        if (info->devt == dev) {
            hdr = info->priv;
            return hdr->read(info->priv, info->dev, sector + info->start_sector, buf, count);
        }
    }

    return -ENODEV;
}

/*
 * 向设备写入扇区
 *
 * @param dev 设备号
 * @param sector 起始扇区号
 * @param buf 数据缓冲区
 * @param count 扇区数量
 */
int block_write_sectors(dev_t dev, uint64_t sector, const void *buf, size_t count) {
    block_dev_info *info;
    struct list_head *pos;
    struct block_hdr *hdr;

    list_for_each(pos, &block_devt_to_info) {
        info = list_entry(pos, block_dev_info, node);
        if (info->devt == dev) {
            hdr = info->priv;
            return hdr->write(info->priv, info->dev, sector + info->start_sector, buf, count);
        }
    }

    return -ENODEV;
}

/*
 * 刷新设备缓存
 *
 * @param dev 设备号
 */
int block_flush(dev_t dev) {
    block_dev_info *info;
    struct list_head *pos;
    struct block_hdr *hdr;

    list_for_each(pos, &block_devt_to_info) {
        info = list_entry(pos, block_dev_info, node);
        if (info->devt == dev) {
            hdr = info->priv;
            return hdr->flush(info->priv, info->dev);
        }
    }

    return -ENODEV;
}