/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <fs/vfs.h>
#include <block.h>
#include <bitmap.h>
#include <time.h>
#include <mutex.h>
#include <dynarr.h>
#include <minmax.h>
#include <initcall.h>
#include <time.h>
#include <heap.h>

#define EXT2_PRINT(fmt, ...) \
    printk("[EXT2] " fmt, ##__VA_ARGS__)

static struct file_operations ext2_file_operations;
static struct inode_operations ext2_inode_operations;
static struct dentry_operations ext2_dentry_ops;

struct ext2_sb_info {
    uint32_t block_size;        // 文件系统块大小
    uint32_t blocks_per_group;  // 每组的块数
    uint32_t inodes_per_group;  // 每组的 inode 数
    uint32_t first_data_block;  // 第一个数据块的块号
    uint32_t group_desc_block;  // 组描述符表起始块号
    uint32_t group_desc_count;  // 块组描述符的总个数

    // 需要被锁保护的部分
    struct {
        mutex_t lock;          
        struct ext2_super_block super_block; // 磁盘超级块的缓存
        struct ext2_group_desc *group_desc_table; // 块组描述符表的缓存
    } stat;
};

struct ext2_inode_info {
    uint32_t i_disk_ino;      // 磁盘上 inode 的编号 (与 inode->ino 相同)
    uint32_t i_disk_block;    // inode 所在块的块号 (用于快速定位)
    uint32_t i_disk_offset;   // inode 在块内的字节偏移 (用于快速定位)
    mutex_t lock;             // 保护写路径的并发操作
};

/**
 * 将 VFS mode_t 中的文件类型转换为 ext2 目录项类型
 *
 * @param mode VFS 文件模式
 * @return ext2_file_type_t
 */
static inline ku8 ext2_mode_to_file_type(mode_t mode) {
    ext2_file_type_t type;

    switch (mode & S_IFMT) {
        case S_IFREG:
            type = EXT2_FT_REG_FILE;
            break;
        case S_IFDIR:
            type = EXT2_FT_DIR;
            break;
        case S_IFLNK:
            type = EXT2_FT_SYMLINK;
            break;
        case S_IFCHR:
            type = EXT2_FT_CHRDEV;
            break;
        case S_IFBLK:
            type = EXT2_FT_BLKDEV;
            break;
        case S_IFIFO:
            type = EXT2_FT_FIFO;
            break;
        case S_IFSOCK:
            type = EXT2_FT_SOCK;
            break;
        default:
            return (ku8)K_ERR(-EINVAL);
    }

    return (ku8)K_OK(type);
}

/**
 * 将 VFS mode_t 转换为 ext2 磁盘 inode 的 i_mode 字段
 *
 * @param mode VFS 文件模式，类型位必须有效
 * @return ext2_inode_mode_t
 */
static inline ku16 ext2_make_i_mode(mode_t mode) {
    ext2_inode_mode_t type;

    switch (mode & S_IFMT) {
        case S_IFREG:
            type = EXT2_S_IFREG;
            break;
        case S_IFDIR:
            type = EXT2_S_IFDIR;
            break;
        case S_IFLNK:
            type = EXT2_S_IFLNK;
            break;
        case S_IFCHR:
            type = EXT2_S_IFCHR;
            break;
        case S_IFBLK:
            type = EXT2_S_IFBLK;
            break;
        case S_IFIFO:
            type = EXT2_S_IFIFO;
            break;
        case S_IFSOCK:
            type = EXT2_S_IFSOCK;
            break;
        default:
            return (ku16)K_ERR(-EINVAL);
    }

    uint16_t perm = (mode & EXT2_S_PERM);
    if (S_ISLNK(mode))
        perm = 0777;

    return (ku16)K_OK(type | perm);
}

/**
 * 将 ext2 磁盘 inode 的 i_mode 转换为 VFS mode_t
 *
 * @param raw_mode ext2 的 i_mode 字段值
 * @return 对应的 VFS mode_t
 */
static inline mode_t ext2_raw_mode_to_vfs(uint16_t raw_mode) {
    mode_t type;

    switch (raw_mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG:
            type = S_IFREG;
            break;
        case EXT2_S_IFDIR:
            type = S_IFDIR;
            break;
        case EXT2_S_IFLNK:
            type = S_IFLNK;
            break;
        case EXT2_S_IFCHR:
            type = S_IFCHR;
            break;
        case EXT2_S_IFBLK:
            type = S_IFBLK;
            break;
        case EXT2_S_IFIFO:
            type = S_IFIFO;
            break;
        case EXT2_S_IFSOCK:
            type = S_IFSOCK;
            break;
        default:
            type = 0;
            break;
    }

    return type | (raw_mode & EXT2_S_PERM);
}

/**
 * 将文件系统块号转换为扇区号
 *
 * @param sb 超级块指针
 * @param block 文件系统块号
 */
static inline uint64_t ext2_block_to_sector(struct super_block *sb, uint32_t block) {
    return (uint64_t)block * (sb->block_size / 512);
}

/**
 * 读取一个扇区
 *
 * @param sb 超级块指针
 * @param sector 扇区号
 * @param buf 缓冲区，至少 512 字节
 */
static int ext2_read_sector(struct super_block *sb, uint64_t sector, void *buf) {
    return block_read_sectors(sb->dev, sector, buf, 1);
}

/**
 * 写入一个扇区
 *
 * @param sb 超级块指针
 * @param sector 扇区号
 * @param buf 缓冲区，至少 512 字节
 */
static int ext2_write_sector(struct super_block *sb, uint64_t sector, const void *buf) {
    return block_write_sectors(sb->dev, sector, buf, 1);
}

/**
 * 从磁盘读取 inode 到 raw 结构体
 *
 * @param sb 超级块指针
 * @param ei 已定位磁盘信息的 inode 私有指针
 * @param raw 输出参数，指向调用者提供的磁盘 inode 结构体
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 *
 * 需要 i_disk_block 和 i_disk_offset 正确初始化
 */
static int ext2_inode_read(
    struct super_block *sb,
    struct ext2_inode_info *ei,
    struct ext2_inode *raw,
    uint8_t *buf
) {
    uint32_t block_size = sb->block_size;
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint64_t sector = ext2_block_to_sector(sb, ei->i_disk_block);

    if (block_read_sectors(sb->dev, sector, buf, sectors_per_block) < 0)
        return -EIO;

    memcpy(raw, buf + ei->i_disk_offset, sizeof(*raw));
    return 0;
}

/**
 * 将 raw 结构体写入磁盘 inode
 *
 * @param sb 超级块指针
 * @param sbi ext2 文件系统私有数据
 * @param ino inode 号
 * @param raw 指向待写入的磁盘 inode 结构体
 * @param ei 输出参数，用于记录定位信息
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_inode_write(
    struct super_block *sb,
    struct ext2_sb_info *sbi,
    ino_t ino,
    struct ext2_inode *raw,
    struct ext2_inode_info *ei,
    uint8_t *buf
) {
    uint32_t block_size = sbi->block_size;
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint32_t block_group = (ino - 1) / sbi->inodes_per_group;
    uint32_t local_index = (ino - 1) % sbi->inodes_per_group;
    uint32_t inode_table_block = sbi->stat.group_desc_table[block_group].bg_inode_table;
    uint32_t inode_block = inode_table_block +
        (local_index * sizeof(struct ext2_inode)) / block_size;
    uint32_t inode_offset = (local_index * sizeof(struct ext2_inode)) % block_size;
    uint64_t sector = ext2_block_to_sector(sb, inode_block);

    if (block_read_sectors(sb->dev, sector, buf, sectors_per_block) < 0)
        return -EIO;

    memcpy(buf + inode_offset, raw, sizeof(*raw));

    if (block_write_sectors(sb->dev, sector, buf, sectors_per_block) < 0)
        return -EIO;

    ei->i_disk_ino = ino;
    ei->i_disk_block = inode_block;
    ei->i_disk_offset = inode_offset;
    return 0;
}

/**
 * 计算逻辑块号在间接块中的深度和各级索引
 *
 * @param lb 逻辑块号
 * @param entries_per_block 每个间接块可容纳的条目数
 * @param depth_out 输出深度
 * @param levels_out 输出各级索引，levels[1..depth] 有效，levels[0] 未使用
 */
static void ext2_bmap_levels(
    uint32_t lb,
    uint32_t entries_per_block,
    int *depth_out,
    uint32_t *levels_out
) {
    uint32_t e = entries_per_block;

    // 直接块
    if (lb < 12) {
        *depth_out = 0;
        levels_out[0] = lb;
        return;
    }

    uint32_t idx = lb - 12;

    if (idx < e) {
        // 一级间接块
        *depth_out = 1;
        levels_out[1] = idx;
    } else if (idx < e + e * e) {
        // 二级间接块
        *depth_out = 2;
        idx -= e;
        levels_out[2] = idx / e;
        levels_out[1] = idx % e;
    } else {
        // 三级间接块
        *depth_out = 3;
        idx -= e + e * e;
        levels_out[3] = idx / (e * e);
        uint32_t rem = idx % (e * e);
        levels_out[2] = rem / e;
        levels_out[1] = rem % e;
    }
}

/**
 * 从块位图中分配一个空闲块号
 *
 * @param sb 超级块指针
 * @param sbi ext2 文件系统私有数据
 * @param bitmap_buf 调用者提供的缓冲区，至少 block_size 字节
 *
 * @return 新分配的块号
 */
static uint32_t ext2_alloc_block(
    struct super_block *sb,
    struct ext2_sb_info *sbi,
    uint8_t *bitmap_buf
) {
    uint32_t group_count = sbi->group_desc_count;
    uint32_t block_size = sbi->block_size;
    uint32_t total_bits = block_size * 8;

    for (uint32_t block_group = 0; block_group < group_count; block_group++) {
        struct ext2_group_desc *desc = &sbi->stat.group_desc_table[block_group];
        if (desc->bg_free_blocks_count == 0)
            continue;

        uint32_t bitmap_block = desc->bg_block_bitmap;

        mutex_lock(&sbi->stat.lock);

        // 读取位图块
        if (ext2_read_sector(
            sb,
            ext2_block_to_sector(sb, bitmap_block),
            bitmap_buf
            ) < 0
        ) {
            mutex_unlock(&sbi->stat.lock);
            continue;
        }

        // 查找空闲位
        uint32_t local_index = bitmap_find(bitmap_buf, total_bits, 0, false);
        if (local_index >= total_bits) {
            mutex_unlock(&sbi->stat.lock);
            continue;
        }

        // 标记占用
        bitmap_set(bitmap_buf, local_index);

        // 更新空闲块计数
        desc->bg_free_blocks_count--;
        sbi->stat.super_block.s_free_blocks_count--;

        // 写回位图块
        if (ext2_write_sector(
            sb,
            ext2_block_to_sector(sb, bitmap_block),
            bitmap_buf
            ) < 0
        ) {
            EXT2_PRINT(
                "alloc block %u failed, bitmap write error\n",
                sbi->first_data_block + block_group * sbi->blocks_per_group + local_index
            );
        }

        mutex_unlock(&sbi->stat.lock);

        return sbi->first_data_block + block_group * sbi->blocks_per_group + local_index;
    }

    return 0;
}

/**
 * 释放单个数据块
 *
 * @param sb 超级块指针
 * @param block 要释放的块号
 * @param sbi ext2 文件系统私有数据
 * @param bitmap_buf 调用者提供的缓冲区，至少 block_size 字节
 */
static void ext2_free_block(
    struct super_block *sb,
    uint32_t block,
    struct ext2_sb_info *sbi,
    uint8_t *bitmap_buf
) {
    if (!block) return;

    uint32_t block_group = (block - sbi->first_data_block) / sbi->blocks_per_group;
    uint32_t local_index  = (block - sbi->first_data_block) % sbi->blocks_per_group;
    struct ext2_group_desc *desc = &sbi->stat.group_desc_table[block_group];
    uint32_t bitmap_block = desc->bg_block_bitmap;

    mutex_lock(&sbi->stat.lock);

    // 读取位图块
    if (ext2_read_sector(
        sb,
        ext2_block_to_sector(sb, bitmap_block),
        bitmap_buf
        ) < 0
    ) {
        mutex_unlock(&sbi->stat.lock);
        EXT2_PRINT("free block %u failed, bitmap read error\n", block);
        return;
    }

    // 清除对应位
    bitmap_clear(bitmap_buf, local_index);

    // 更新空闲块计数
    desc->bg_free_blocks_count++;
    sbi->stat.super_block.s_free_blocks_count++;

    // 写回位图块
    if (ext2_write_sector(
        sb,
        ext2_block_to_sector(sb, bitmap_block),
        bitmap_buf
        ) < 0
    ) EXT2_PRINT("free block %u failed, bitmap write error\n", block);

    mutex_unlock(&sbi->stat.lock);
}

/**
 * 将文件逻辑块号映射为磁盘物理块号
 *
 * @param inode VFS inode 指针
 * @param lb 逻辑块号
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 * @param allocate 是否在未映射时分配新块
 *
 * @return 物理块号
 */
static uint32_t ext2_bmap(
    struct inode *inode,
    uint32_t lb,
    uint32_t *buf,
    bool allocate
) {
    struct ext2_sb_info *sbi = inode->sb->private;
    struct ext2_inode_info *ei = inode->private;
    uint32_t block_size = sbi->block_size;
    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint64_t inode_sector = ext2_block_to_sector(inode->sb, ei->i_disk_block);
    uint32_t inode_offset = ei->i_disk_offset;
    struct ext2_inode raw;
    uint32_t phys = 0;

    // 读取 inode 所在磁盘块
    if (block_read_sectors(
        inode->sb->dev,
        inode_sector,
        buf,
        sectors_per_block
        ) < 0
    ) {
        EXT2_PRINT("bmap: failed to read inode block\n");
        return 0;
    }
    memcpy(&raw, (uint8_t *)buf + inode_offset, sizeof(raw));

    int depth;
    uint32_t levels[4];
    ext2_bmap_levels(lb, entries_per_block, &depth, levels);

    // 直接块：直接从 i_block 数组获取，若不存在且允许分配则分配新块
    if (depth == 0) {
        phys = raw.i_block[levels[0]];
        if (!phys && allocate) {
            phys = ext2_alloc_block(
                inode->sb,
                sbi,
                (uint8_t *)buf
            );
            if (phys) {
                mutex_lock(&ei->lock);
                raw.i_block[levels[0]] = phys;
                memcpy(
                    (uint8_t *)buf + inode_offset,
                    &raw,
                    sizeof(raw)
                );
                block_write_sectors(
                    inode->sb->dev,
                    inode_sector,
                    buf,
                    sectors_per_block
                );
                mutex_unlock(&ei->lock);
            }
        }
        return phys;
    }

    // 间接块路径：获取起始间接块号，若不存在则分配
    uint32_t indirect_block = raw.i_block[12 + (depth - 1)];
    if (!indirect_block) {
        if (!allocate)
            return 0;

        // 分配根间接块
        indirect_block = ext2_alloc_block(
            inode->sb,
            sbi,
            (uint8_t *)buf
        );
        if (!indirect_block)
            return 0;

        // 更新 inode 指针并写回
        mutex_lock(&ei->lock);
        raw.i_block[12 + (depth - 1)] = indirect_block;
        memcpy(
            (uint8_t *)buf + inode_offset,
            &raw,
            sizeof(raw)
        );
        block_write_sectors(
            inode->sb->dev,
            inode_sector,
            buf,
            sectors_per_block
        );
        mutex_unlock(&ei->lock);

        // 清零新间接块
        memset(buf, 0, block_size);
        block_write_sectors(
            inode->sb->dev,
            ext2_block_to_sector(inode->sb, indirect_block),
            buf,
            sectors_per_block
        );
    }

    // 逐层向下读取间接块，直到最后一级之前
    for (int d = depth; d > 1; d--) {
        uint32_t target = levels[d];
        if (block_read_sectors(
            inode->sb->dev,
            ext2_block_to_sector(inode->sb, indirect_block),
            buf,
            sectors_per_block
            ) < 0
        ) return 0;

        uint32_t next_block = buf[target];
        if (!next_block) {
            if (!allocate)
                return 0;

            // 分配中间间接块
            next_block = ext2_alloc_block(
                inode->sb,
                sbi,
                (uint8_t *)buf
            );
            if (!next_block)
                return 0;

            // 更新上层间接块条目并写回
            mutex_lock(&ei->lock);
            buf[target] = next_block;
            block_write_sectors(
                inode->sb->dev,
                ext2_block_to_sector(inode->sb, indirect_block),
                buf,
                sectors_per_block
            );
            mutex_unlock(&ei->lock);

            // 清零新间接块
            memset(buf, 0, block_size);
            block_write_sectors(
                inode->sb->dev,
                ext2_block_to_sector(inode->sb, next_block),
                buf,
                sectors_per_block
            );
        }
        indirect_block = next_block;
    }

    // 最后一级间接块，获取或分配目标数据块
    uint32_t target = levels[1];
    if (block_read_sectors(
        inode->sb->dev,
        ext2_block_to_sector(inode->sb, indirect_block),
        buf,
        sectors_per_block
        ) < 0
    ) return 0;

    phys = buf[target];
    if (!phys && allocate) {
        phys = ext2_alloc_block(
            inode->sb,
            sbi,
            (uint8_t *)buf
        );
        if (phys) {
            mutex_lock(&ei->lock);
            buf[target] = phys;
            block_write_sectors(
                inode->sb->dev,
                ext2_block_to_sector(inode->sb, indirect_block),
                buf,
                sectors_per_block
            );
            mutex_unlock(&ei->lock);
        }
    }

    return phys;
}

/**
 * 间接块树遍历器
 *
 * @param sb 超级块指针
 * @param sbi ext2 文件系统私有数据
 * @param indirect_block 根间接块号
 * @param depth 间接块深度
 * @param start 起始索引
 * @param indirect_buf 外部缓冲区，为 NULL 时降级为逐扇区读取
 * @param bitmap_buf 用于位图操作的缓冲区
 */
static bool ext2_indirect_walk(
    struct super_block *sb,
    struct ext2_sb_info *sbi,
    uint32_t indirect_block,
    int depth,
    uint32_t start,
    uint32_t *indirect_buf,
    uint8_t *bitmap_buf
) {
    uint32_t block_size = sbi->block_size;
    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint8_t sector_buf[512];
    bool has_buf = (indirect_buf != NULL);

    struct {
        uint32_t block;
        uint32_t index;
        uint32_t start;
        int depth;
        bool all_freed;
        bool block_loaded;
    } stack[depth];
    int top = 0;

    // 压入根间接块
    stack[top].block = indirect_block;
    stack[top].index = start;
    stack[top].start = start;
    stack[top].depth = depth;
    stack[top].all_freed = true;
    stack[top].block_loaded = false;
    top++;

    while (top > 0) {
        uint32_t current_block = stack[top - 1].block;
        uint32_t current_index = stack[top - 1].index;
        uint32_t current_start = stack[top - 1].start;
        int current_depth = stack[top - 1].depth;
        bool current_all_freed = stack[top - 1].all_freed;

        // 当前块遍历完成，根据空闲状态决定释放、写回或继续
        if (current_index >= entries_per_block) {
            if (current_all_freed && current_depth == depth) {
                // 根层级全部空闲，释放根间接块并返回 true
                ext2_free_block(sb, current_block, sbi, bitmap_buf);
                top--;
                if (top > 0)
                    stack[top - 1].all_freed = true;
                else
                    return true;
            } else if (current_all_freed && current_depth != depth) {
                // 非根层级全部空闲，释放该间接块
                ext2_free_block(sb, current_block, sbi, bitmap_buf);
                top--;
                if (top > 0)
                    stack[top - 1].all_freed = true;
            } else {
                // 仍有保留条目，写回修改后的间接块
                if (has_buf && current_all_freed) {
                    if (block_write_sectors(
                        sb->dev,
                        ext2_block_to_sector(sb, current_block),
                        indirect_buf,
                        sectors_per_block
                        ) < 0
                    ) EXT2_PRINT("indirect walk write failed at block %u\n", current_block);
                }
                top--;
                if (top > 0)
                    stack[top - 1].all_freed = false;
            }
            continue;
        }

        // 有缓冲区且首次处理此块时，一次性读取整个间接块
        if (has_buf && !stack[top - 1].block_loaded) {
            if (block_read_sectors(
                sb->dev,
                ext2_block_to_sector(sb, current_block),
                indirect_buf,
                sectors_per_block
                ) < 0
            ) {
                EXT2_PRINT("indirect walk read failed at block %u\n", current_block);
                top--;
                continue;
            }
            stack[top - 1].block_loaded = true;
        }

        // 获取当前条目值：有缓冲直接取，无缓冲逐扇区读取
        uint32_t entry;
        uint32_t sec_off = (current_index * sizeof(uint32_t)) / 512;
        uint32_t byte_off = (current_index * sizeof(uint32_t)) % 512;

        if (has_buf) {
            entry = indirect_buf[current_index];
        } else {
            if (ext2_read_sector(
                sb,
                ext2_block_to_sector(sb, current_block) + sec_off,
                sector_buf
                ) < 0
            ) {
                stack[top - 1].index++;
                continue;
            }
            entry = *(uint32_t *)(sector_buf + byte_off);
        }

        stack[top - 1].index++;

        if (current_index < current_start) {
            if (entry)
                stack[top - 1].all_freed = false;
            continue;
        }

        if (!entry) continue;

        if (current_depth == 1) {
            // 条目是数据块，释放
            ext2_free_block(sb, entry, sbi, bitmap_buf);
            if (has_buf) {
                indirect_buf[current_index] = 0;
            } else {
                // 无缓冲区时清零条目并立即写回该扇区
                *(uint32_t *)(sector_buf + byte_off) = 0;
                if (ext2_write_sector(
                    sb,
                    ext2_block_to_sector(sb, current_block) + sec_off,
                    sector_buf
                    ) < 0
                ) EXT2_PRINT("indirect walk write failed at entry %u\n", current_index);
            }
        } else {
            // 条目是下一级间接块，压入新帧
            stack[top].block = entry;
            stack[top].index = 0;
            stack[top].start = 0;
            stack[top].depth = current_depth - 1;
            stack[top].all_freed = true;
            stack[top].block_loaded = false;
            top++;
        }
    }

    return false;
}

/**
 * 释放间接块树
 *
 * @param sb 超级块指针
 * @param indirect_block 间接块号
 * @param depth 间接块深度
 * @param start 起始索引
 * @param sbi ext2 文件系统私有数据
 * @param indirect_buf 用于读取间接块内容的缓冲区，可为 NULL
 * @param bitmap_buf 用于位图操作的缓冲区
 */
static bool ext2_free_indirect(
    struct super_block *sb,
    uint32_t indirect_block,
    int depth,
    uint32_t start,
    struct ext2_sb_info *sbi,
    uint32_t *indirect_buf,
    uint8_t *bitmap_buf
) {
    return ext2_indirect_walk(
        sb,
        sbi,
        indirect_block,
        depth,
        start,
        indirect_buf,
        bitmap_buf
    );
}

/**
 * 将数据写入文件
 *
 * @param inode VFS inode 指针
 * @param offset 写入偏移（字节）
 * @param data 数据缓冲区
 * @param len 数据长度（字节）
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_write_data(
    struct inode *inode,
    off_t offset,
    const void *data,
    size_t len,
    uint32_t *buf
) {
    struct ext2_sb_info *sbi = inode->sb->private;
    uint32_t block_size = sbi->block_size;
    uint32_t sectors_per_block = (block_size + 511) / 512;
    const uint8_t *src = data;
    size_t remaining = len;

    while (remaining > 0) {
        uint32_t lb = offset / block_size;
        uint32_t block_off = offset % block_size;
        uint32_t chunk = block_size - block_off;
        if (chunk > remaining)
            chunk = remaining;

        // 获取或分配物理块
        uint32_t phys = ext2_bmap(inode, lb, buf, true);
        if (!phys)
            return -ENOSPC;

        if (block_read_sectors(
            inode->sb->dev,
            ext2_block_to_sector(inode->sb, phys),
            buf,
            sectors_per_block
            ) < 0
        ) return -EIO;

        memcpy((uint8_t *)buf + block_off, src, chunk);

        if (block_write_sectors(
            inode->sb->dev,
            ext2_block_to_sector(inode->sb, phys),
            buf,
            sectors_per_block
            ) < 0
        ) return -EIO;

        src += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    // 更新文件大小和块计数
    if (offset > inode->size) {
        inode->size = offset;
        inode->blocks = (offset + 511) / 512;
    }

    return 0;
}

/**
 * 从文件读取数据
 *
 * @param inode VFS inode 指针
 * @param offset 读取偏移（字节）
 * @param data 数据缓冲区
 * @param len 数据长度（字节）
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_read_data(
    struct inode *inode,
    off_t offset,
    void *data,
    size_t len,
    uint32_t *buf
) {
    struct ext2_sb_info *sbi = inode->sb->private;
    uint32_t block_size = sbi->block_size;
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint8_t *dst = data;
    size_t remaining = len;

    while (remaining > 0) {
        uint32_t lb = offset / block_size;
        uint32_t block_off = offset % block_size;
        uint32_t chunk = block_size - block_off;
        if (chunk > remaining)
            chunk = remaining;

        // 获取物理块号
        uint32_t phys = ext2_bmap(inode, lb, buf, false);

        if (phys) {
            // 读取物理块
            if (block_read_sectors(
                inode->sb->dev,
                ext2_block_to_sector(inode->sb, phys),
                buf,
                sectors_per_block
                ) < 0
            ) return -EIO;

            memcpy(dst, (uint8_t *)buf + block_off, chunk);
        } else {
            // 空洞：填充零
            memset(dst, 0, chunk);
        }

        dst += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    return 0;
}

/**
 * 从 inode 位图中分配一个空闲 inode 号
 *
 * @param sb 超级块指针
 * @param sbi ext2 文件系统私有数据
 * @param bitmap_buf 调用者提供的缓冲区，至少 block_size 字节
 *
 * @return 新分配的 inode 号
 */
static ino_t ext2_ino_alloc(
    struct super_block *sb,
    struct ext2_sb_info *sbi,
    uint8_t *bitmap_buf
) {
    uint32_t group_count = sbi->group_desc_count;
    uint32_t block_size = sbi->block_size;
    uint32_t total_bits = block_size * 8;

    for (uint32_t block_group = 0; block_group < group_count; block_group++) {
        struct ext2_group_desc *desc = &sbi->stat.group_desc_table[block_group];
        if (desc->bg_free_inodes_count == 0)
            continue;

        uint32_t bitmap_block = desc->bg_inode_bitmap;

        mutex_lock(&sbi->stat.lock);

        // 读取位图块
        if (ext2_read_sector(
            sb,
            ext2_block_to_sector(sb, bitmap_block),
            bitmap_buf
            ) < 0
        ) {
            mutex_unlock(&sbi->stat.lock);
            continue;
        }

        // 查找空闲位
        uint32_t local_index = bitmap_find(bitmap_buf, total_bits, 0, false);
        if (local_index >= total_bits) {
            mutex_unlock(&sbi->stat.lock);
            continue;
        }

        // 标记占用
        bitmap_set(bitmap_buf, local_index);

        // 更新内存中的空闲 inode 计数
        desc->bg_free_inodes_count--;
        sbi->stat.super_block.s_free_inodes_count--;

        // 写回位图
        if (ext2_write_sector(
            sb,
            ext2_block_to_sector(sb, bitmap_block),
            bitmap_buf
            ) < 0
        ) {
            EXT2_PRINT(
                "ino alloc %u failed, bitmap write error\n",
                block_group * sbi->inodes_per_group + local_index + 1
            );
        }

        mutex_unlock(&sbi->stat.lock);

        return block_group * sbi->inodes_per_group + local_index + 1;
    }

    return 0;
}

/**
 * 释放一个已分配的 inode 号
 *
 * @param sb 超级块指针
 * @param sbi ext2 文件系统私有数据
 * @param ino 要释放的 inode 号
 * @param bitmap_buf 调用者提供的缓冲区，至少 block_size 字节
 */
static void ext2_ino_free(
    struct super_block *sb,
    struct ext2_sb_info *sbi,
    ino_t ino,
    uint8_t *bitmap_buf
) {
    uint32_t block_group = (ino - 1) / sbi->inodes_per_group;
    uint32_t local_index = (ino - 1) % sbi->inodes_per_group;
    struct ext2_group_desc *desc = &sbi->stat.group_desc_table[block_group];
    uint32_t bitmap_block = desc->bg_inode_bitmap;

    mutex_lock(&sbi->stat.lock);

    // 读取位图块
    if (ext2_read_sector(
        sb,
        ext2_block_to_sector(sb, bitmap_block),
        bitmap_buf
        ) < 0
    ) {
        mutex_unlock(&sbi->stat.lock);
        EXT2_PRINT("ino free %lu failed, bitmap read error\n", ino);
        return;
    }

    // 清除对应位
    bitmap_clear(bitmap_buf, local_index);

    // 更新内存中的空闲 inode 计数
    desc->bg_free_inodes_count++;
    sbi->stat.super_block.s_free_inodes_count++;

    // 写回位图
    if (ext2_write_sector(
        sb,
        ext2_block_to_sector(sb, bitmap_block),
        bitmap_buf
        ) < 0
    ) EXT2_PRINT("ino free %lu failed, bitmap write error\n", ino);

    mutex_unlock(&sbi->stat.lock);
}

/**
 * 将超级块和组描述符表写回磁盘
 *
 * @param sb 超级块指针
 * @param sbi ext2 文件系统私有数据
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static void ext2_sync_metadata(
    struct super_block *sb,
    struct ext2_sb_info *sbi,
    uint8_t *buf
) {
    uint32_t block_size = sbi->block_size;
    uint32_t group_count = sbi->group_desc_count;
    uint32_t sectors_per_block = (block_size + 511) / 512;

    // 写回超级块
    memcpy(
        buf,
        &sbi->stat.super_block,
        sizeof(sbi->stat.super_block)
    );
    uint32_t sb_sectors = (sizeof(sbi->stat.super_block) + 511) / 512;
    if (block_write_sectors(
        sb->dev,
        ext2_block_to_sector(sb, sbi->first_data_block),
        buf,
        sb_sectors
        ) < 0
    ) EXT2_PRINT("superblock write failed\n");

    // 写回组描述符表
    uint32_t gd_per_block = block_size / sizeof(struct ext2_group_desc);
    uint32_t gd_blocks = (group_count + gd_per_block - 1) / gd_per_block;
    uint64_t gd_sector = ext2_block_to_sector(sb, sbi->group_desc_block);
    if (block_write_sectors(
        sb->dev,
        gd_sector,
        sbi->stat.group_desc_table,
        gd_blocks * sectors_per_block
        ) < 0
    ) EXT2_PRINT("group descriptor write failed\n");
}

static struct inode *ext2_alloc_inode(struct super_block *sb) {
    struct ext2_inode_info *ei = kheap_alloc(sizeof(*ei));
    if (!ei) return NULL;

    struct inode *inode = kheap_alloc(sizeof(*inode));
    if (!inode) {
        kheap_free(ei);
        return NULL;
    }

    ei->i_disk_ino = 0;
    ei->i_disk_block = 0;
    ei->i_disk_offset = 0;
    mutex_init(&ei->lock);

    inode->private = ei;
    return inode;
}

static void ext2_destroy_inode(struct inode *inode) {
    kheap_free(inode->private);
    kheap_free(inode);
}

static void ext2_evict_inode(struct inode *inode) {
    if (!inode->ino) return;

    struct ext2_sb_info *sbi = inode->sb->private;
    struct ext2_inode_info *ei = inode->private;
    uint32_t block_size = sbi->block_size;
    struct ext2_inode raw;
    uint8_t *buf;
    uint32_t *indirect_buf;

    // 分配工作缓冲区，失败则直接返回，磁盘资源尚未触及无需回滚
    buf = kheap_alloc(block_size);
    if (!buf)
        return;

    // 读取磁盘 inode
    if (ext2_inode_read(inode->sb, ei, &raw, buf) < 0) {
        kheap_free(buf);
        return;
    }

    // 释放直接块
    for (int i = 0; i < 12; i++) {
        if (raw.i_block[i]) {
            ext2_free_block(inode->sb, raw.i_block[i], sbi, buf);
            raw.i_block[i] = 0;
        }
    }

    // 分配间接块缓冲区，失败时跳过间接块释放
    indirect_buf = kheap_alloc(block_size * 3);

    if (raw.i_block[12] && indirect_buf)
        ext2_free_indirect(inode->sb, raw.i_block[12], 1, 0, sbi, indirect_buf, buf);
    if (raw.i_block[13] && indirect_buf)
        ext2_free_indirect(inode->sb, raw.i_block[13], 2, 0, sbi, indirect_buf, buf);
    if (raw.i_block[14] && indirect_buf)
        ext2_free_indirect(inode->sb, raw.i_block[14], 3, 0, sbi, indirect_buf, buf);

    kheap_free(indirect_buf);

    // 将清零后的 inode 写回磁盘，成功后才释放 inode 号
    if (ext2_inode_write(inode->sb, sbi, inode->ino, &raw, ei, buf) == 0) {
        ext2_ino_free(inode->sb, sbi, inode->ino, buf);
        ext2_sync_metadata(inode->sb, sbi, buf);
    }

    kheap_free(buf);
}

static void ext2_put_super(struct super_block *sb) {
    struct ext2_sb_info *sbi = sb->private;
    kheap_free(sbi->stat.group_desc_table);
    kheap_free(sbi);
    kheap_free(sb);
}

static int ext2_sync_fs(struct super_block *sb, bool wait) {
    (void)wait;
    // TODO: 解析 wait 调用 block 使用异步提交

    return block_flush(sb->dev);
}

static int ext2_statfs(struct dentry *dentry, struct statfs *buf) {
    struct super_block *sb = dentry->inode->sb;
    struct ext2_sb_info *sbi = sb->private;

    buf->f_bsize   = sbi->block_size;
    buf->f_blocks  = sbi->stat.super_block.s_blocks_count;
    buf->f_bfree   = sbi->stat.super_block.s_free_blocks_count;
    buf->f_bavail  = buf->f_bfree - sbi->stat.super_block.s_r_blocks_count;
    buf->f_files   = sbi->stat.super_block.s_inodes_count;
    buf->f_ffree   = sbi->stat.super_block.s_free_inodes_count;
    buf->f_namemax = UINT8_MAX;
    return 0;
}

static struct super_operations ext2_super_operations = {
    .alloc_inode   = ext2_alloc_inode,
    .destroy_inode = ext2_destroy_inode,
    .evict_inode   = ext2_evict_inode,
    .put_super     = ext2_put_super,
    .sync_fs       = ext2_sync_fs,
    .statfs        = ext2_statfs,
};

/**
 * 从磁盘读取并构造 VFS inode
 *
 * @param sb 超级块指针
 * @param ino inode 号
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 *
 * @return inode 指针(已增加引用计数)
 */
static struct inode *ext2_iget(
    struct super_block *sb,
    ino_t ino,
    uint8_t *buf
) {
    struct ext2_sb_info *sbi = sb->private;
    struct ext2_inode_info *ei;
    struct inode *inode;
    struct ext2_inode raw;
    uint32_t block_group, local_index;
    uint32_t inode_table_block, inode_block, inode_offset;

    // 优先从 icache 查找
    inode = vfs_icache_find(sb, ino);
    if (inode) return inode;

    // 未命中，分配新壳子
    inode = ext2_alloc_inode(sb);
    if (!inode) return NULL;

    vfs_iget(inode);
    ei = inode->private;

    // 计算磁盘位置
    block_group = (ino - 1) / sbi->inodes_per_group;
    local_index = (ino - 1) % sbi->inodes_per_group;
    inode_table_block = sbi->stat.group_desc_table[block_group].bg_inode_table;
    inode_block = inode_table_block + (local_index * sizeof(struct ext2_inode)) / sbi->block_size;
    inode_offset = (local_index * sizeof(struct ext2_inode)) % sbi->block_size;

    // 记录磁盘位置，便于后续写回
    ei->i_disk_ino = ino;
    ei->i_disk_block = inode_block;
    ei->i_disk_offset = inode_offset;

    // 读取磁盘 inode
    if (ext2_inode_read(sb, ei, &raw, buf) < 0) {
        ext2_destroy_inode(inode);
        return NULL;
    }

    inode->ino = ino;
    inode->mode = ext2_raw_mode_to_vfs(raw.i_mode);
    inode->uid = raw.i_uid;
    inode->gid = raw.i_gid;
    inode->size = raw.i_size;
    inode->atime.tv_sec = raw.i_atime;
    inode->atime.tv_nsec = 0;
    inode->mtime.tv_sec = raw.i_mtime;
    inode->mtime.tv_nsec = 0;
    inode->ctime.tv_sec = raw.i_ctime;
    inode->ctime.tv_nsec = 0;
    inode->nlink = raw.i_links_count;
    inode->blocks = raw.i_blocks;

    // 根据文件类型设置操作表
    inode->ops = &ext2_inode_operations;
    if (S_ISREG(inode->mode))
        inode->fop = &ext2_file_operations;
    else if (S_ISDIR(inode->mode))
        inode->fop = NULL;
    else if (S_ISLNK(inode->mode))
        inode->fop = NULL;
    else
        inode->fop = &dev_fops;

    // 插入 icache
    vfs_icache_add(inode);
    return inode;
}

/**
 * 截断文件
 *
 * @param inode VFS inode 指针
 * @param new_size 新大小（字节）
 * @param raw 调用者已读取的磁盘 inode 结构体，被修改为截断后的
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 *
 * 调用者负责在截断后写回磁盘
 */
static void ext2_truncate(
    struct inode *inode,
    uint64_t new_size,
    struct ext2_inode *raw,
    uint8_t *buf
) {
    struct ext2_sb_info *sbi = inode->sb->private;
    uint32_t block_size = sbi->block_size;
    uint64_t old_blocks = (inode->size + block_size - 1) / block_size;
    uint64_t new_blocks = (new_size + block_size - 1) / block_size;

    if (new_blocks >= old_blocks)
        return;

    for (uint64_t lb = new_blocks; lb < old_blocks; lb++) {
        uint32_t phys = ext2_bmap(inode, lb, (uint32_t *)buf, false);
        if (phys) {
            ext2_free_block(inode->sb, phys, sbi, buf);
            if (lb < 12)
                raw->i_block[lb] = 0;
        }
    }

    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    uint32_t *indirect_buf = kheap_alloc(block_size * 3);

    uint64_t threshold_base = 12;

    for (int level = 1; level <= 3; level++) {
        uint64_t layer_capacity = 1;
        for (int i = 0; i < level; i++)
            layer_capacity *= entries_per_block;

        threshold_base += layer_capacity;

        uint32_t *block_ptr = &raw->i_block[11 + level];
        if (!*block_ptr)
            continue;

        uint64_t full_threshold = threshold_base - layer_capacity;
        uint64_t partial_threshold = threshold_base;

        if (new_blocks <= full_threshold) {
            ext2_free_indirect(
                inode->sb,
                *block_ptr,
                level,
                0,
                sbi,
                indirect_buf,
                buf
            );
            *block_ptr = 0;
        } else if (new_blocks < partial_threshold) {
            uint32_t start;
            if (level == 1) {
                start = (uint32_t)(new_blocks - full_threshold);
            } else {
                start = (uint32_t)(
                    (new_blocks - full_threshold + (layer_capacity / entries_per_block) - 1) /
                    (layer_capacity / entries_per_block)
                );
            }

            bool freed = ext2_free_indirect(
                inode->sb,
                *block_ptr,
                level,
                start,
                sbi,
                indirect_buf,
                buf
            );
            if (freed)
                *block_ptr = 0;
        }
    }

    kheap_free(indirect_buf);
}

/**
 * 重建目录
 *
 * @param dir 目录 inode
 * @param entries 条目 dynarr，元素大小为 sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_rebuild_directory(
    struct inode *dir,
    dynarr_t *entries,
    uint32_t *buf
) {
    struct ext2_sb_info *sbi = dir->sb->private;
    uint32_t block_size = sbi->block_size;
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint32_t count = dynarr_count(entries);

    uint32_t *entry_sizes = kheap_alloc(count * sizeof(uint32_t));
    if (!entry_sizes)
        return -ENOMEM;

    uint32_t total_size = 0;
    for (uint32_t i = 0; i < count; i++) {
        struct ext2_dir_entry *de = dynarr_get(entries, i);
        uint32_t size = sizeof(struct ext2_dir_entry) + de->name_len;
        size = (size + 3) & ~3;
        entry_sizes[i] = size;
        total_size += size;
    }

    uint32_t total_blocks = (total_size + block_size - 1) / block_size;

    // 读取磁盘 inode
    struct ext2_inode_info *ei = dir->private;
    struct ext2_inode raw;

    if (ext2_inode_read(dir->sb, ei, &raw, (uint8_t *)buf) < 0) {
        kheap_free(entry_sizes);
        return -EIO;
    }

    // 释放不再需要的旧块
    uint32_t old_total_blocks = (dir->size + block_size - 1) / block_size;
    for (uint32_t lb = total_blocks; lb < old_total_blocks; lb++) {
        uint32_t phys = ext2_bmap(dir, lb, buf, false);
        if (phys) {
            ext2_free_block(dir->sb, phys, sbi, (uint8_t *)buf);
            if (lb < 12)
                raw.i_block[lb] = 0;
        }
    }

    // 分配需要的新块
    for (uint32_t lb = old_total_blocks; lb < total_blocks; lb++) {
        uint32_t phys = ext2_alloc_block(dir->sb, sbi, (uint8_t *)buf);
        if (!phys) {
            kheap_free(entry_sizes);
            return -ENOSPC;
        }
        if (lb < 12)
            raw.i_block[lb] = phys;
    }

    // 逐块构建并写回目录内容
    uint32_t entry_index = 0;

    for (uint32_t lb = 0; lb < total_blocks; lb++) {
        memset(buf, 0, block_size);

        uint32_t offset_in_block = 0;
        while (
            entry_index < count &&
            offset_in_block + entry_sizes[entry_index] <= block_size
        ) {
            struct ext2_dir_entry *de = dynarr_get(entries, entry_index);
            struct ext2_dir_entry *dst = (struct ext2_dir_entry *)(
                (uint8_t *)buf + offset_in_block
            );
            memcpy(
                dst,
                de,
                sizeof(struct ext2_dir_entry) + de->name_len
            );
            dst->rec_len = entry_sizes[entry_index];
            offset_in_block += entry_sizes[entry_index];
            entry_index++;
        }

        // 最后一个条目扩展到块尾
        if (offset_in_block > 0 && offset_in_block < block_size) {
            struct ext2_dir_entry *last_de = (struct ext2_dir_entry *)(
                (uint8_t *)buf +
                offset_in_block -
                entry_sizes[entry_index - 1]
            );
            last_de->rec_len = block_size - (
                offset_in_block - entry_sizes[entry_index - 1]
            );
        }

        uint32_t phys = ext2_bmap(dir, lb, buf, false);
        if (!phys) {
            kheap_free(entry_sizes);
            return -ENOSPC;
        }

        if (block_write_sectors(
            dir->sb->dev,
            ext2_block_to_sector(dir->sb, phys),
            buf,
            sectors_per_block
            ) < 0
        ) {
            kheap_free(entry_sizes);
            return -EIO;
        }
    }

    kheap_free(entry_sizes);

    // 同步内存中的目录大小和块计数
    dir->size = total_size;
    dir->blocks = (total_size + 511) / 512;

    // 将更新后的 i_size、i_blocks 以及 i_block 数组写回磁盘
    raw.i_size = total_size;
    raw.i_blocks = dir->blocks;
    ext2_inode_write(dir->sb, sbi, dir->ino, &raw, ei, (uint8_t *)buf);

    return 0;
}

/**
 * 收集目录中的所有有效条目
 *
 * @param dir 目录 inode
 * @param entries 输出参数，元素大小为 sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_dir_collect(
    struct inode *dir,
    dynarr_t *entries,
    uint32_t *buf
) {
    struct ext2_sb_info *sbi = dir->sb->private;
    uint32_t block_size = sbi->block_size;
    uint32_t total_blocks = (dir->size + block_size - 1) / block_size;

    for (uint32_t lb = 0; lb < total_blocks; lb++) {
        uint32_t phys = ext2_bmap(dir, lb, buf, false);
        if (!phys)
            continue;

        // 读取目录数据块
        if (block_read_sectors(
            dir->sb->dev,
            ext2_block_to_sector(dir->sb, phys),
            buf,
            (block_size + 511) / 512
            ) < 0
        ) continue;

        uint32_t off = 0;
        while (off + sizeof(struct ext2_dir_entry) <= block_size) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)((uint8_t *)buf + off);
            if (de->rec_len < sizeof(struct ext2_dir_entry))
                break;
            if (off + de->rec_len > block_size)
                break;

            if (de->inode) {
                if (!dynarr_append(entries, de))
                    return -ENOMEM;
            }
            off += de->rec_len;
        }
    }

    return 0;
}

/**
 * 在目录中添加条目并写回磁盘
 *
 * @param dir 目录 inode
 * @param ino 新条目的 inode 号
 * @param name 新条目的名称
 * @param name_len 名称长度
 * @param type 新条目的文件类型
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_dir_collect_and_add(
    struct inode *dir,
    ino_t ino,
    const char *name,
    uint32_t name_len,
    ext2_file_type_t type,
    uint32_t *buf
) {
    int err;

    dynarr_t *entries = dynarr_create(
        sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN,
        0
    );
    if (!entries)
        return -ENOMEM;

    err = ext2_dir_collect(dir, entries, buf);
    if (err) {
        dynarr_destroy(entries);
        return err;
    }

    // 构建新条目并追加
    struct ext2_dir_entry new_entry;
    new_entry.inode = ino;
    new_entry.name_len = name_len;
    new_entry.file_type = type;
    memcpy(new_entry.name, name, name_len);

    if (!dynarr_append(entries, &new_entry)) {
        dynarr_destroy(entries);
        return -ENOMEM;
    }

    err = ext2_rebuild_directory(dir, entries, buf);
    dynarr_destroy(entries);
    return err;
}

/**
 * 从目录中删除条目并写回磁盘
 *
 * @param dir 目录 inode
 * @param name 要删除的条目名称
 * @param name_len 名称长度
 * @param buf 调用者提供的缓冲区，至少 block_size 字节
 */
static int ext2_dir_collect_and_remove(
    struct inode *dir,
    const char *name,
    uint32_t name_len,
    uint32_t *buf
) {
    int err;
    bool found = false;

    dynarr_t *entries = dynarr_create(
        sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN,
        0
    );
    if (!entries)
        return -ENOMEM;

    err = ext2_dir_collect(dir, entries, buf);
    if (err) {
        dynarr_destroy(entries);
        return err;
    }

    // 查找目标条目并删除
    uint32_t count = dynarr_count(entries);
    for (uint32_t i = 0; i < count; i++) {
        struct ext2_dir_entry *de = dynarr_get(entries, i);
        if (!de) continue;

        if (de->name_len == name_len &&
            !memcmp(de->name, name, name_len)) {
            found = true;

            for (uint32_t j = i; j < count - 1; j++) {
                struct ext2_dir_entry *next = dynarr_get(entries, j + 1);
                struct ext2_dir_entry *cur = dynarr_get(entries, j);
                if (cur && next)
                    memcpy(cur, next, sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN);
            }

            dynarr_pop(entries, NULL);
            break;
        }
    }

    if (!found) {
        dynarr_destroy(entries);
        return -ENOENT;
    }

    err = ext2_rebuild_directory(dir, entries, buf);
    dynarr_destroy(entries);
    return err;
}

static kptr ext2_lookup(
    struct inode *dir,
    struct dentry *dentry
) {
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    uint32_t block_size = sbi->block_size;
    uint32_t sectors_per_block = (block_size + 511) / 512;
    uint32_t total_blocks = (dir->size + block_size - 1) / block_size;
    uint32_t *buf;
    struct inode *child = NULL;

    // 分配工作缓冲区
    buf = kheap_alloc(block_size);
    if (!buf) return (kptr)K_PTR(dentry);

    // 逐逻辑块遍历目录文件
    for (uint32_t lb = 0; lb < total_blocks; lb++) {
        // 将逻辑块号映射为物理块号
        uint32_t phys = ext2_bmap(dir, lb, buf, false);
        if (!phys) continue;

        // 读取物理块，扫描其中的 ext2_dir_entry 链表
        if (block_read_sectors(
            sb->dev,
            ext2_block_to_sector(sb, phys),
            buf,
            sectors_per_block
            ) < 0
        ) continue;

        uint32_t off = 0;
        while (off + sizeof(struct ext2_dir_entry) <= block_size) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)((uint8_t *)buf + off);

            // 校验 rec_len 合法性，防止损坏的目录导致越界
            if (de->rec_len < sizeof(struct ext2_dir_entry)) break;
            if (off + de->rec_len > block_size) break;

            // 跳过已删除条目，匹配名称
            if (
                de->inode &&
                de->name_len == dentry->name.len &&
                !memcmp(de->name, dentry->name.name, dentry->name.len)
            ) {
                child = ext2_iget(sb, de->inode, (uint8_t *)buf);
                if (!child)
                    EXT2_PRINT("lookup failed to get inode %u\n", de->inode);
                goto out;
            }
            off += de->rec_len;
        }
    }

out:
    // 释放缓冲区，设置查找结果
    kheap_free(buf);

    if (child) {
        spin_lock(&dentry->lock);
        dentry->inode = child;
        spin_unlock(&dentry->lock);
    }

    return (kptr)K_PTR(dentry);
}

static int ext2_create(struct inode *dir, struct dentry *dentry, mode_t mode) {
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    struct ext2_inode_info *ei;
    struct ext2_inode_info *ei_dir = dir->private;
    struct inode *inode;
    struct ext2_inode raw;
    struct timespec now;
    uid_t uid;
    gid_t gid;
    ino_t new_ino;
    uint8_t *buf;
    int err;

    task_get_current_ugid(&uid, &gid);
    time_get(&now);

    // 分配 VFS inode 壳子
    inode = ext2_alloc_inode(sb);
    if (!inode) return -ENOMEM;

    // 增加引用计数
    vfs_iget(inode);
    ei = inode->private;

    // 分配工作缓冲区
    buf = kheap_alloc(sbi->block_size);
    if (!buf) {
        err = -ENOMEM;
        goto err_buf;
    }

    // 从 inode 位图分配新 inode 号
    new_ino = ext2_ino_alloc(sb, sbi, buf);
    if (!new_ino) {
        err = -ENOSPC;
        goto err_bitmap;
    }

    // 初始化磁盘 inode 结构
    memset(&raw, 0, sizeof(raw));

    ku16 imode_res = ext2_make_i_mode(mode | S_IFREG);
    K_ERR_LABEL_AND_SAVE(imode_res, err_bitmap, err);
    raw.i_mode = (ext2_inode_mode_t)imode_res.val;
    raw.i_uid = uid;
    raw.i_gid = gid;
    raw.i_links_count = 1;
    raw.i_atime = now.tv_sec;
    raw.i_ctime = now.tv_sec;
    raw.i_mtime = now.tv_sec;

    // 写回磁盘 inode
    err = ext2_inode_write(sb, sbi, new_ino, &raw, ei, buf);
    if (err) goto err_bitmap;

    // 填充 VFS inode 字段
    inode->ino = new_ino;
    inode->mode = ext2_raw_mode_to_vfs(raw.i_mode);
    inode->uid = raw.i_uid;
    inode->gid = raw.i_gid;
    inode->size = 0;
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->nlink = 1;
    inode->blocks = 0;
    inode->ops = &ext2_inode_operations;
    inode->fop = &ext2_file_operations;
    inode->sb = sb;

    // 插入 icache
    vfs_icache_add(inode);

    // 锁定父目录 inode，保证目录修改操作的原子性
    mutex_lock(&ei_dir->lock);

    // 在父目录中添加目录项
    err = ext2_dir_collect_and_add(
        dir,
        new_ino,
        dentry->name.name,
        dentry->name.len,
        EXT2_FT_REG_FILE,
        (uint32_t *)buf
    );
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto err_icache;
    }

    // 写回超级块和组描述符
    ext2_sync_metadata(sb, sbi, buf);

    mutex_unlock(&ei_dir->lock);

    spin_lock(&dentry->lock);
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    spin_unlock(&dentry->lock);
    kheap_free(buf);
    return 0;

err_icache:
    vfs_iput(inode);
err_bitmap:
    ext2_ino_free(sb, sbi, new_ino, buf);
    ext2_sync_metadata(sb, sbi, buf);
    kheap_free(buf);
err_buf:
    ext2_destroy_inode(inode);
    return err;
}

static int ext2_symlink(struct inode *dir, struct dentry *dentry, const char *target) {
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    struct ext2_inode_info *ei;
    struct ext2_inode_info *ei_dir = dir->private;
    struct inode *inode;
    struct ext2_inode raw;
    struct timespec now;
    uid_t uid;
    gid_t gid;
    ino_t new_ino;
    uint8_t *buf;
    int err;
    size_t target_len = strlen(target);

    // 目标路径长度上限
    if (target_len >= PATH_MAX)
        return -ENAMETOOLONG;

    task_get_current_ugid(&uid, &gid);
    time_get(&now);

    // 分配 VFS inode 壳子
    inode = ext2_alloc_inode(sb);
    if (!inode) return -ENOMEM;

    vfs_iget(inode);
    ei = inode->private;

    // 分配工作缓冲区
    buf = kheap_alloc(sbi->block_size);
    if (!buf) {
        err = -ENOMEM;
        goto err_buf;
    }

    // 从 inode 位图分配新 inode 号
    new_ino = ext2_ino_alloc(sb, sbi, buf);
    if (!new_ino) {
        err = -ENOSPC;
        goto err_bitmap;
    }

    // 初始化磁盘 inode 结构
    memset(&raw, 0, sizeof(raw));
    ku16 imode_res = ext2_make_i_mode(S_IFLNK | 0777);
    K_ERR_LABEL_AND_SAVE(imode_res, err_bitmap, err);   
    
    raw.i_mode = (ext2_inode_mode_t)imode_res.val;
    raw.i_uid = uid;
    raw.i_gid = gid;
    raw.i_links_count = 1;
    raw.i_atime = now.tv_sec;
    raw.i_ctime = now.tv_sec;
    raw.i_mtime = now.tv_sec;

    if (target_len < 60) {
        // 快速符号链接：目标路径直接存储在 i_block 中
        memcpy(raw.i_block, target, target_len);
        raw.i_size = target_len;
        raw.i_blocks = 0;
    } else {
        // 普通符号链接：分配数据块并写入目标路径
        raw.i_size = 0;
        raw.i_blocks = 0;
    }

    // 写回磁盘 inode
    err = ext2_inode_write(sb, sbi, new_ino, &raw, ei, buf);
    if (err) goto err_bitmap;

    // 填充 VFS inode 字段
    inode->ino = new_ino;
    inode->mode = ext2_raw_mode_to_vfs(raw.i_mode);
    inode->uid = raw.i_uid;
    inode->gid = raw.i_gid;
    inode->size = raw.i_size;
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->nlink = 1;
    inode->blocks = raw.i_blocks;
    inode->ops = &ext2_inode_operations;
    inode->fop = NULL;
    inode->sb = sb;

    // 慢速路径：写入目标路径到数据块，并写回 i_size 和 i_blocks
    if (target_len >= 60) {
        err = ext2_write_data(
            inode,
            0,
            target,
            target_len,
            (uint32_t *)buf
        );
        if (err) goto err_icache;

        inode->size = target_len;
        inode->blocks = (target_len + 511) / 512;

        // 将更新后的 i_size 和 i_blocks 写回磁盘
        err = ext2_inode_read(sb, ei, &raw, buf);
        if (err) goto err_icache;

        raw.i_size = inode->size;
        raw.i_blocks = inode->blocks;

        err = ext2_inode_write(sb, sbi, new_ino, &raw, ei, buf);
        if (err) goto err_icache;
    }

    // 插入 icache
    vfs_icache_add(inode);

    // 锁定父目录 inode，保证目录修改操作的原子性
    mutex_lock(&ei_dir->lock);

    // 在父目录中添加目录项
    err = ext2_dir_collect_and_add(
        dir,
        new_ino,
        dentry->name.name,
        dentry->name.len,
        EXT2_FT_SYMLINK,
        (uint32_t *)buf
    );
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto err_icache;
    }

    // 写回超级块和组描述符
    ext2_sync_metadata(sb, sbi, buf);

    mutex_unlock(&ei_dir->lock);

    spin_lock(&dentry->lock);
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    spin_unlock(&dentry->lock);
    kheap_free(buf);
    return 0;

err_icache:
    vfs_iput(inode);
err_bitmap:
    ext2_ino_free(sb, sbi, new_ino, buf);
    ext2_sync_metadata(sb, sbi, buf);
    kheap_free(buf);
err_buf:
    ext2_destroy_inode(inode);
    return err;
}

static int ext2_link(
    struct dentry *old_dentry,
    struct inode *dir,
    struct dentry *new_dentry
) {
    struct inode *inode = old_dentry->inode;
    struct super_block *sb = inode->sb;
    struct ext2_sb_info *sbi = sb->private;
    uint32_t block_size = sbi->block_size;
    uint8_t *buf;
    ext2_file_type_t type;
    struct timespec now;
    int err;

    // 不允许对目录创建硬链接
    if (S_ISDIR(inode->mode))
        return -EPERM;

    // 根据源 inode 的文件类型确定目录项类型
    ku8 type_res = ext2_mode_to_file_type(inode->mode);
    K_ERR_RETURN(type_res);

    type = (ext2_file_type_t)type_res.val;

    time_get(&now);

    buf = kheap_alloc(block_size);
    if (!buf) return -ENOMEM;

    // 锁定目标目录 inode，保证目录修改操作的原子性
    struct ext2_inode_info *ei_dir = dir->private;
    mutex_lock(&ei_dir->lock);

    // 在目标目录中添加目录项并写回
    err = ext2_dir_collect_and_add(
        dir,
        inode->ino,
        new_dentry->name.name,
        new_dentry->name.len,
        type,
        (uint32_t *)buf
    );
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto out;
    }

    // 更新磁盘 inode 的硬链接计数和时间戳
    struct ext2_inode_info *ei = inode->private;
    struct ext2_inode raw;

    err = ext2_inode_read(sb, ei, &raw, buf);
    if (err) {
        // 回滚已添加的目录项
        ext2_dir_collect_and_remove(
            dir,
            new_dentry->name.name,
            new_dentry->name.len,
            (uint32_t *)buf
        );
        mutex_unlock(&ei_dir->lock);
        goto out;
    }

    raw.i_links_count++;
    raw.i_ctime = now.tv_sec;

    err = ext2_inode_write(sb, sbi, inode->ino, &raw, ei, buf);
    if (err) {
        // 回滚已添加的目录项
        ext2_dir_collect_and_remove(
            dir,
            new_dentry->name.name,
            new_dentry->name.len,
            (uint32_t *)buf
        );
        mutex_unlock(&ei_dir->lock);
        goto out;
    }

    mutex_unlock(&ei_dir->lock);

    // 更新内存中的硬链接计数和时间戳
    inode->nlink++;
    inode->ctime = now;

    // 增加引用并关联新 dentry
    vfs_iget(inode);
    spin_lock(&new_dentry->lock);
    new_dentry->inode = inode;
    new_dentry->flags &= ~DCACHE_NEGATIVE;
    spin_unlock(&new_dentry->lock);

    ext2_sync_metadata(sb, sbi, buf);
    kheap_free(buf);
    return 0;

out:
    kheap_free(buf);
    return err;
}

static int ext2_unlink(
    struct inode *dir,
    struct dentry *dentry
) {
    struct inode *inode = dentry->inode;
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    uint32_t block_size = sbi->block_size;
    uint8_t *buf;
    struct timespec now;
    int err;

    time_get(&now);

    buf = kheap_alloc(block_size);
    if (!buf) return -ENOMEM;

    // 锁定父目录 inode，保证目录修改操作的原子性
    struct ext2_inode_info *ei_dir = dir->private;
    mutex_lock(&ei_dir->lock);

    // 从父目录中删除目录项并写回
    err = ext2_dir_collect_and_remove(
        dir,
        dentry->name.name,
        dentry->name.len,
        (uint32_t *)buf
    );
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto out;
    }

    // 更新磁盘 inode 的硬链接计数和时间戳
    struct ext2_inode_info *ei = inode->private;
    struct ext2_inode raw;

    err = ext2_inode_read(sb, ei, &raw, buf);
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto out;
    }

    raw.i_links_count--;
    raw.i_ctime = now.tv_sec;

    err = ext2_inode_write(sb, sbi, inode->ino, &raw, ei, buf);
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto out;
    }

    mutex_unlock(&ei_dir->lock);

    // 更新内存中的硬链接计数和时间戳
    inode->nlink--;
    inode->ctime = now;

    // 解除 dentry 关联
    spin_lock(&dentry->lock);
    dentry->inode = NULL;
    spin_unlock(&dentry->lock);

    ext2_sync_metadata(sb, sbi, buf);

out:
    kheap_free(buf);
    return err;
}

static int ext2_mkdir(struct inode *dir, struct dentry *dentry, mode_t mode) {
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    struct ext2_inode_info *ei;
    struct ext2_inode_info *ei_dir = dir->private;
    struct inode *inode;
    struct ext2_inode raw;
    struct timespec now;
    uid_t uid;
    gid_t gid;
    ino_t new_ino;
    uint8_t *buf;
    int err;

    task_get_current_ugid(&uid, &gid);
    time_get(&now);

    // 分配 VFS inode 壳子
    inode = ext2_alloc_inode(sb);
    if (!inode) return -ENOMEM;

    vfs_iget(inode);
    ei = inode->private;

    // 分配工作缓冲区
    buf = kheap_alloc(sbi->block_size);
    if (!buf) {
        err = -ENOMEM;
        goto err_buf;
    }

    // 从 inode 位图分配新 inode 号
    new_ino = ext2_ino_alloc(sb, sbi, buf);
    if (!new_ino) {
        err = -ENOSPC;
        goto err_bitmap;
    }

    // 初始化磁盘 inode 结构
    memset(&raw, 0, sizeof(raw));
    ku16 imode_res = ext2_make_i_mode(mode | S_IFDIR);
    K_ERR_LABEL_AND_SAVE(imode_res, err_bitmap, err);

    raw.i_mode = (ext2_inode_mode_t)imode_res.val;
    raw.i_uid = uid;
    raw.i_gid = gid;
    raw.i_links_count = 2;
    raw.i_atime = now.tv_sec;
    raw.i_ctime = now.tv_sec;
    raw.i_mtime = now.tv_sec;

    uint32_t block_size = sbi->block_size;
    raw.i_size = block_size;
    raw.i_blocks = block_size / 512;

    // 写回磁盘 inode
    err = ext2_inode_write(sb, sbi, new_ino, &raw, ei, buf);
    if (err) goto err_bitmap;

    // 填充 VFS inode 字段
    inode->ino = new_ino;
    inode->mode = ext2_raw_mode_to_vfs(raw.i_mode);
    inode->uid = raw.i_uid;
    inode->gid = raw.i_gid;
    inode->size = block_size;
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->nlink = 2;
    inode->blocks = raw.i_blocks;
    inode->ops = &ext2_inode_operations;
    inode->fop = NULL;
    inode->sb = sb;

    // 构建新目录的 "." 和 ".." 条目
    struct ext2_dir_entry dot_entries[2];

    dot_entries[0].inode = new_ino;
    dot_entries[0].name_len = 1;
    dot_entries[0].file_type = EXT2_FT_DIR;
    dot_entries[0].name[0] = '.';

    dot_entries[1].inode = dir->ino;
    dot_entries[1].name_len = 2;
    dot_entries[1].file_type = EXT2_FT_DIR;
    dot_entries[1].name[0] = '.';
    dot_entries[1].name[1] = '.';

    // 用重建函数一次性写入新目录
    dynarr_t *dot_dynarr = dynarr_create(
        sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN,
        0
    );
    if (!dot_dynarr) {
        err = -ENOMEM;
        goto err_icache;
    }

    if (!dynarr_append(dot_dynarr, &dot_entries[0]) ||
        !dynarr_append(dot_dynarr, &dot_entries[1])) {
        dynarr_destroy(dot_dynarr);
        err = -ENOMEM;
        goto err_icache;
    }

    err = ext2_rebuild_directory(inode, dot_dynarr, (uint32_t *)buf);
    dynarr_destroy(dot_dynarr);

    if (err) goto err_icache;

    // 插入 icache
    vfs_icache_add(inode);

    // 锁定父目录 inode，保证目录修改操作的原子性
    mutex_lock(&ei_dir->lock);

    // 在父目录中添加目录项并写回
    err = ext2_dir_collect_and_add(
        dir,
        new_ino,
        dentry->name.name,
        dentry->name.len,
        EXT2_FT_DIR,
        (uint32_t *)buf
    );
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto err_icache;
    }

    // 增加父目录的硬链接计数，并更新父目录的时间戳
    dir->nlink++;

    // 更新父目录磁盘 inode 的硬链接计数和时间戳
    struct ext2_inode parent_raw;
    if (ext2_inode_read(sb, ei_dir, &parent_raw, buf) == 0) {
        parent_raw.i_links_count++;
        parent_raw.i_mtime = now.tv_sec;
        parent_raw.i_ctime = now.tv_sec;
        ext2_inode_write(sb, sbi, dir->ino, &parent_raw, ei_dir, buf);
    }

    // 同步父目录内存时间戳
    dir->mtime = now;
    dir->ctime = now;

    // 写回超级块和组描述符
    ext2_sync_metadata(sb, sbi, buf);

    mutex_unlock(&ei_dir->lock);

    spin_lock(&dentry->lock);
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    spin_unlock(&dentry->lock);
    kheap_free(buf);
    return 0;

err_icache:
    vfs_iput(inode);
err_bitmap:
    ext2_ino_free(sb, sbi, new_ino, buf);
    ext2_sync_metadata(sb, sbi, buf);
    kheap_free(buf);
err_buf:
    ext2_destroy_inode(inode);
    return err;
}

static int ext2_rmdir(
    struct inode *dir,
    struct dentry *dentry
) {
    struct inode *inode = dentry->inode;
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    uint32_t block_size = sbi->block_size;
    uint8_t *buf = NULL;
    bool is_empty = true;
    int err = 0;

    // 获取目录 inode 的锁，按 inode 号排序避免死锁
    struct ext2_inode_info *ei_inode = inode->private;
    struct ext2_inode_info *ei_dir = dir->private;

    if (inode->ino < dir->ino) {
        mutex_lock(&ei_inode->lock);
        mutex_lock(&ei_dir->lock);
    } else if (inode->ino > dir->ino) {
        mutex_lock(&ei_dir->lock);
        mutex_lock(&ei_inode->lock);
    } else {
        mutex_lock(&ei_dir->lock);
    }

    // 检查目标目录是否为空
    uint32_t total_blocks = (inode->size + block_size - 1) / block_size;
    uint32_t *dir_buf = kheap_alloc(block_size);
    if (!dir_buf) {
        err = -ENOMEM;
        goto out;
    }

    for (uint32_t lb = 0; lb < total_blocks && is_empty; lb++) {
        uint32_t phys = ext2_bmap(inode, lb, dir_buf, false);
        if (!phys) continue;

        if (block_read_sectors(
            sb->dev,
            ext2_block_to_sector(sb, phys),
            dir_buf,
            (block_size + 511) / 512
            ) < 0
        ) {
            is_empty = false;
            break;
        }

        uint32_t off = 0;
        while (off + sizeof(struct ext2_dir_entry) <= block_size) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)((uint8_t *)dir_buf + off);
            if (de->rec_len < sizeof(struct ext2_dir_entry)) break;
            if (off + de->rec_len > block_size) break;

            if (de->inode &&
                !(de->name_len == 1 && de->name[0] == '.') &&
                !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')
            ) {
                is_empty = false;
                break;
            }
            off += de->rec_len;
        }
    }

    kheap_free(dir_buf);

    if (!is_empty) {
        err = -ENOTEMPTY;
        goto out;
    }

    buf = kheap_alloc(block_size);
    if (!buf) {
        err = -ENOMEM;
        goto out;
    }

    // 从父目录中删除目录项并写回
    err = ext2_dir_collect_and_remove(
        dir,
        dentry->name.name,
        dentry->name.len,
        (uint32_t *)buf
    );
    if (err)
        goto out;

    // 更新目标目录磁盘 inode 的硬链接计数
    struct ext2_inode_info *ei = inode->private;
    struct ext2_inode raw;

    err = ext2_inode_read(sb, ei, &raw, buf);
    if (err)
        goto out;

    raw.i_links_count--;

    err = ext2_inode_write(sb, sbi, inode->ino, &raw, ei, buf);
    if (err)
        goto out;

    inode->nlink--;

    // 更新父目录磁盘 inode 的硬链接计数
    struct ext2_inode parent_raw;

    err = ext2_inode_read(sb, ei_dir, &parent_raw, buf);
    if (err)
        goto out;

    parent_raw.i_links_count--;

    err = ext2_inode_write(sb, sbi, dir->ino, &parent_raw, ei_dir, buf);
    if (err)
        goto out;

    dir->nlink--;

    spin_lock(&dentry->lock);
    dentry->inode = NULL;
    spin_unlock(&dentry->lock);
    ext2_sync_metadata(sb, sbi, buf);

out:
    // 统一释放锁：确保两把锁都被正确释放
    mutex_unlock(&ei_inode->lock);
    if (inode != dir)
        mutex_unlock(&ei_dir->lock);

    kheap_free(buf);
    return err;
}

static int ext2_rename(
    struct inode *old_dir,
    struct dentry *old_dentry,
    struct inode *new_dir,
    struct dentry *new_dentry
) {
    struct inode *old_inode = old_dentry->inode;
    struct inode *new_inode = new_dentry->inode;
    struct super_block *sb = old_dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    uint32_t block_size = sbi->block_size;
    uint8_t *buf;
    struct timespec now;
    ext2_file_type_t new_type;
    dynarr_t *entries = NULL;
    int err;

    if (!old_inode)
        return -ENOENT;

    // 源和目标相同，无需操作
    if (old_inode == new_inode)
        return 0;

    // 类型冲突检查
    if (new_inode && S_ISDIR(new_inode->mode) && !S_ISDIR(old_inode->mode))
        return -EISDIR;

    if (new_inode && !S_ISDIR(new_inode->mode) && S_ISDIR(old_inode->mode))
        return -ENOTDIR;

    time_get(&now);

    // 获取目录 inode 的锁, 按 inode 号排序避免死锁
    struct ext2_inode_info *ei_old_dir = old_dir->private;
    struct ext2_inode_info *ei_new_dir = new_dir->private;

    if (old_dir == new_dir) {
        mutex_lock(&ei_old_dir->lock);
    } else if (old_dir->ino < new_dir->ino) {
        mutex_lock(&ei_old_dir->lock);
        mutex_lock(&ei_new_dir->lock);
    } else {
        mutex_lock(&ei_new_dir->lock);
        mutex_lock(&ei_old_dir->lock);
    }

    // 如果目标是已存在的非空目录，拒绝覆盖
    if (new_inode && S_ISDIR(new_inode->mode) && new_inode != old_inode) {
        uint32_t total_blocks = (new_inode->size + block_size - 1) / block_size;
        uint32_t *check_buf = kheap_alloc(block_size);
        if (!check_buf) {
            err = -ENOMEM;
            goto out_unlock;
        }

        bool is_empty = true;
        for (uint32_t lb = 0; lb < total_blocks && is_empty; lb++) {
            uint32_t phys = ext2_bmap(new_inode, lb, check_buf, false);
            if (!phys) continue;

            if (block_read_sectors(
                sb->dev,
                ext2_block_to_sector(sb, phys),
                check_buf,
                (block_size + 511) / 512
                ) < 0
            ) {
                is_empty = false;
                break;
            }

            uint32_t off = 0;
            while (off + sizeof(struct ext2_dir_entry) <= block_size) {
                struct ext2_dir_entry *de = (struct ext2_dir_entry *)((uint8_t *)check_buf + off);
                if (de->rec_len < sizeof(struct ext2_dir_entry)) break;
                if (off + de->rec_len > block_size) break;

                if (de->inode &&
                    !(de->name_len == 1 && de->name[0] == '.') &&
                    !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')
                ) {
                    is_empty = false;
                    break;
                }
                off += de->rec_len;
            }
        }

        kheap_free(check_buf);

        if (!is_empty) {
            err = -ENOTEMPTY;
            goto out_unlock;
        }
    }

    // 祖先检查：如果源是目录，目标不能是源的后代
    if (S_ISDIR(old_inode->mode) && old_dir != new_dir) {
        struct dentry *ancestor = new_dentry->parent;
        while (ancestor) {
            if (ancestor->inode == old_inode) {
                err = -EINVAL;
                goto out_unlock;
            }
            if (ancestor->inode == sb->root->inode)
                break;
            ancestor = ancestor->parent;
        }
    }

    buf = kheap_alloc(block_size);
    if (!buf) {
        err = -ENOMEM;
        goto out_unlock;
    }

    // 确定新条目的文件类型
    ku8 type_res = ext2_mode_to_file_type(old_inode->mode);
    K_ERR_LABEL_AND_SAVE(type_res, out_free_buf, err);
    new_type = (ext2_file_type_t)type_res.val;

    // 如果目标已存在，先删除目标条目
    if (new_inode) {
        // 删除目标条目
        err = ext2_dir_collect_and_remove(
            new_dir,
            new_dentry->name.name,
            new_dentry->name.len,
            (uint32_t *)buf
        );
        if (err)
            goto out_free_buf;

        // 更新目标磁盘 inode 的链接计数和时间戳
        struct ext2_inode_info *ei_new = new_inode->private;
        struct ext2_inode new_raw;

        err = ext2_inode_read(sb, ei_new, &new_raw, buf);
        if (err)
            goto out_free_buf;

        new_raw.i_links_count--;
        new_raw.i_ctime = now.tv_sec;

        err = ext2_inode_write(sb, sbi, new_inode->ino, &new_raw, ei_new, buf);
        if (err)
            goto out_free_buf;

        // 更新内存中的链接计数和时间戳
        new_inode->nlink--;
        new_inode->ctime = now;

        // 如果目标是目录，减少父目录的链接计数
        if (S_ISDIR(new_inode->mode)) {
            new_dir->nlink--;
        }
    }

    if (old_dir == new_dir) {
        // 同一目录内重命名：收集条目、修改名字、重建
        entries = dynarr_create(
            sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN,
            0
        );
        if (!entries) {
            err = -ENOMEM;
            goto out_free_buf;
        }

        err = ext2_dir_collect(old_dir, entries, (uint32_t *)buf);
        if (err)
            goto out_free_entries;

        // 查找旧名字条目并修改
        bool found = false;
        uint32_t count = dynarr_count(entries);
        for (uint32_t i = 0; i < count; i++) {
            struct ext2_dir_entry *de = dynarr_get(entries, i);
            if (!de) continue;

            if (de->name_len == old_dentry->name.len &&
                !memcmp(de->name, old_dentry->name.name, old_dentry->name.len)) {
                // 修改名字
                de->name_len = new_dentry->name.len;
                memcpy(de->name, new_dentry->name.name, new_dentry->name.len);
                found = true;
                break;
            }
        }

        if (!found) {
            err = -ENOENT;
            goto out_free_entries;
        }

        err = ext2_rebuild_directory(old_dir, entries, (uint32_t *)buf);
        dynarr_destroy(entries);
        entries = NULL;   // 防止后续重复释放
    } else {
        // 跨目录移动：先在目标目录添加新条目
        err = ext2_dir_collect_and_add(
            new_dir,
            old_inode->ino,
            new_dentry->name.name,
            new_dentry->name.len,
            new_type,
            (uint32_t *)buf
        );
        if (err)
            goto out_free_buf;

        // 再从源目录删除旧条目
        err = ext2_dir_collect_and_remove(
            old_dir,
            old_dentry->name.name,
            old_dentry->name.len,
            (uint32_t *)buf
        );
        if (err) {
            // 回滚：删除刚添加的目标条目
            ext2_dir_collect_and_remove(
                new_dir,
                new_dentry->name.name,
                new_dentry->name.len,
                (uint32_t *)buf
            );
            goto out_free_buf;
        }

        // 如果移动的是目录，更新 ".." 条目和父目录链接计数
        if (S_ISDIR(old_inode->mode)) {
            // 更新源目录的 ".." 条目指向新父目录
            dynarr_t *src_entries = dynarr_create(
                sizeof(struct ext2_dir_entry) + EXT2_NAME_LEN,
                0
            );
            if (src_entries) {
                err = ext2_dir_collect(old_inode, src_entries, (uint32_t *)buf);
                if (!err) {
                    uint32_t src_count = dynarr_count(src_entries);
                    for (uint32_t i = 0; i < src_count; i++) {
                        struct ext2_dir_entry *de = dynarr_get(src_entries, i);
                        if (de && de->name_len == 2 &&
                            de->name[0] == '.' && de->name[1] == '.') {
                            de->inode = new_dir->ino;
                            break;
                        }
                    }
                    ext2_rebuild_directory(old_inode, src_entries, (uint32_t *)buf);
                }
                dynarr_destroy(src_entries);
            }

            // 更新父目录链接计数
            old_dir->nlink--;
            new_dir->nlink++;
        }
    }

    // 更新源 inode 的 i_ctime
    struct ext2_inode_info *ei_old = old_inode->private;
    struct ext2_inode old_raw;

    err = ext2_inode_read(sb, ei_old, &old_raw, buf);
    if (err)
        goto out_free_buf;

    old_raw.i_ctime = now.tv_sec;

    err = ext2_inode_write(sb, sbi, old_inode->ino, &old_raw, ei_old, buf);
    if (err)
        goto out_free_buf;

    old_inode->ctime = now;

    // 源父目录
    struct ext2_inode old_dir_raw;

    err = ext2_inode_read(sb, ei_old_dir, &old_dir_raw, buf);
    if (err)
        goto out_free_buf;

    old_dir_raw.i_mtime = now.tv_sec;
    old_dir_raw.i_ctime = now.tv_sec;

    err = ext2_inode_write(sb, sbi, old_dir->ino, &old_dir_raw, ei_old_dir, buf);
    if (err)
        goto out_free_buf;

    old_dir->mtime = now;
    old_dir->ctime = now;

    // 目标父目录
    if (old_dir != new_dir) {
        struct ext2_inode new_dir_raw;

        err = ext2_inode_read(sb, ei_new_dir, &new_dir_raw, buf);
        if (err)
            goto out_free_buf;

        new_dir_raw.i_mtime = now.tv_sec;
        new_dir_raw.i_ctime = now.tv_sec;

        err = ext2_inode_write(sb, sbi, new_dir->ino, &new_dir_raw, ei_new_dir, buf);
        if (err)
            goto out_free_buf;

        new_dir->mtime = now;
        new_dir->ctime = now;
    }

    // 更新 dentry 关联
    {
        struct dentry *first = old_dentry, *second = new_dentry;
        if ((uintptr_t)first > (uintptr_t)second) {
            first = new_dentry;
            second = old_dentry;
        }
        spin_lock(&first->lock);
        spin_lock(&second->lock);
        old_dentry->inode = NULL;
        new_dentry->inode = old_inode;
        new_dentry->flags &= ~DCACHE_NEGATIVE;
        spin_unlock(&second->lock);
        spin_unlock(&first->lock);
    }

    ext2_sync_metadata(sb, sbi, buf);
    kheap_free(buf);
    err = 0;

out_free_entries:
    dynarr_destroy(entries);
out_free_buf:
    kheap_free(buf);
out_unlock:
    if (old_dir == new_dir) {
        mutex_unlock(&ei_old_dir->lock);
    } else if (old_dir->ino < new_dir->ino) {
        mutex_unlock(&ei_new_dir->lock);
        mutex_unlock(&ei_old_dir->lock);
    } else {
        mutex_unlock(&ei_old_dir->lock);
        mutex_unlock(&ei_new_dir->lock);
    }
    return err;
}

static int ext2_getattr(struct path *path, struct kstat *stat) {
    struct inode *inode = path->dentry->inode;
    struct super_block *sb = inode->sb;
    struct ext2_inode_info *ei = inode->private;
    struct ext2_sb_info *sbi = sb->private;

    stat->st_dev     = sb->dev;
    stat->st_ino     = ei->i_disk_ino;
    stat->st_mode    = inode->mode;
    stat->st_nlink   = inode->nlink;
    stat->st_uid     = inode->uid;
    stat->st_gid     = inode->gid;
    stat->st_rdev    = inode->rdev;
    stat->st_size    = inode->size;
    stat->st_atim    = inode->atime;
    stat->st_mtim    = inode->mtime;
    stat->st_ctim    = inode->ctime;
    stat->st_blksize = sbi->block_size;
    stat->st_blocks  = inode->blocks;
    return 0;
}

static int ext2_setattr(struct dentry *dentry, struct iattr *attr) {
    struct inode *inode = dentry->inode;
    struct super_block *sb = inode->sb;
    struct ext2_inode_info *ei = inode->private;
    struct ext2_sb_info *sbi = sb->private;
    struct ext2_inode raw;
    struct timespec now;
    uint8_t *buf;
    int err;

    time_get(&now);

    buf = kheap_alloc(sbi->block_size);
    if (!buf)
        return -ENOMEM;

    err = ext2_inode_read(sb, ei, &raw, buf);
    if (err) {
        kheap_free(buf);
        return err;
    }

    mutex_lock(&ei->lock);

    if (attr->ia_valid & ATTR_MODE)
        raw.i_mode = (raw.i_mode & EXT2_S_IFMT) | (attr->ia_mode & EXT2_S_PERM);
    if (attr->ia_valid & ATTR_UID)
        raw.i_uid = attr->ia_uid;
    if (attr->ia_valid & ATTR_GID)
        raw.i_gid = attr->ia_gid;
    if (attr->ia_valid & ATTR_SIZE) {
        raw.i_size = attr->ia_size;
        raw.i_mtime = now.tv_sec;
        ext2_truncate(inode, attr->ia_size, &raw, buf);
        raw.i_blocks = (attr->ia_size + 511) / 512;
    }
    if (attr->ia_valid & ATTR_ATIME)
        raw.i_atime = attr->ia_atime.tv_sec;
    if (attr->ia_valid & ATTR_MTIME)
        raw.i_mtime = attr->ia_mtime.tv_sec;
    raw.i_ctime = now.tv_sec;

    err = ext2_inode_write(sb, sbi, inode->ino, &raw, ei, buf);
    if (err) {
        mutex_unlock(&ei->lock);
        kheap_free(buf);
        return err;
    }

    inode->mode = ext2_raw_mode_to_vfs(raw.i_mode);
    inode->uid   = raw.i_uid;
    inode->gid   = raw.i_gid;
    inode->size  = raw.i_size;
    inode->atime.tv_sec  = raw.i_atime;
    inode->atime.tv_nsec = 0;
    inode->mtime.tv_sec  = raw.i_mtime;
    inode->mtime.tv_nsec = 0;
    inode->ctime = now;

    mutex_unlock(&ei->lock);
    kheap_free(buf);
    return 0;
}

static ssize_t ext2_readlink(
    struct inode *inode,
    char *buf,
    size_t bufsiz
) {
    struct ext2_sb_info *sbi = inode->sb->private;
    struct ext2_inode_info *ei = inode->private;
    uint32_t block_size = sbi->block_size;
    struct ext2_inode raw;
    uint8_t *io_buf;
    ssize_t ret;

    // 分配工作缓冲区
    io_buf = kheap_alloc(block_size);
    if (!io_buf)
        return -ENOMEM;

    // 读取磁盘 inode
    if (ext2_inode_read(inode->sb, ei, &raw, io_buf) < 0) {
        kheap_free(io_buf);
        return -EIO;
    }

    // 快速符号链接：目标路径直接存储在 i_block 中
    if (raw.i_blocks == 0 && raw.i_size < 60) {
        size_t copy_len = raw.i_size;
        if (copy_len > bufsiz)
            copy_len = bufsiz;
        memcpy(buf, raw.i_block, copy_len);
        ret = copy_len;
    } else {
        // 普通符号链接
        ret = ext2_read_data(
            inode,
            0,
            buf,
            bufsiz < inode->size ? bufsiz : inode->size,
            (uint32_t *)io_buf
        );
    }

    kheap_free(io_buf);
    return ret;
}

static int ext2_mknod(struct inode *dir, struct dentry *dentry, mode_t mode, dev_t dev) {
    struct super_block *sb = dir->sb;
    struct ext2_sb_info *sbi = sb->private;
    struct ext2_inode_info *ei;
    struct ext2_inode_info *ei_dir = dir->private;
    struct inode *inode;
    struct ext2_inode raw;
    struct timespec now;
    uid_t uid;
    gid_t gid;
    ino_t new_ino;
    uint8_t *buf;
    ext2_file_type_t type;
    int err;

    // 根据文件类型确定目录项类型
    ku8 type_res = ext2_mode_to_file_type(mode);
    K_ERR_RETURN(type_res);
    type = (ext2_file_type_t)type_res.val;

    task_get_current_ugid(&uid, &gid);
    time_get(&now);

    // 分配 VFS inode 壳子
    inode = ext2_alloc_inode(sb);
    if (!inode) return -ENOMEM;

    vfs_iget(inode);
    ei = inode->private;

    // 分配工作缓冲区
    buf = kheap_alloc(sbi->block_size);
    if (!buf) {
        err = -ENOMEM;
        goto err_buf;
    }

    // 从 inode 位图分配新 inode 号
    new_ino = ext2_ino_alloc(sb, sbi, buf);
    if (!new_ino) {
        err = -ENOSPC;
        goto err_bitmap;
    }

    // 初始化磁盘 inode 结构
    memset(&raw, 0, sizeof(raw));
    ku16 imode_res = ext2_make_i_mode(mode);
    K_ERR_LABEL_AND_SAVE(imode_res, err_bitmap, err);
    raw.i_mode = (ext2_inode_mode_t)imode_res.val;
    raw.i_uid = uid;
    raw.i_gid = gid;
    raw.i_links_count = 1;
    raw.i_atime = now.tv_sec;
    raw.i_ctime = now.tv_sec;
    raw.i_mtime = now.tv_sec;

    // 存储设备号
    if (S_ISCHR(mode))
        raw.i_block[0] = dev;
    else if (S_ISBLK(mode))
        raw.i_block[1] = dev;

    // 写回磁盘 inode
    err = ext2_inode_write(sb, sbi, new_ino, &raw, ei, buf);
    if (err) goto err_bitmap;

    // 填充 VFS inode 字段
    inode->ino = new_ino;
    inode->mode = ext2_raw_mode_to_vfs(raw.i_mode);
    inode->uid = raw.i_uid;
    inode->gid = raw.i_gid;
    inode->size = 0;
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->nlink = 1;
    inode->blocks = 0;
    inode->rdev = dev;
    inode->ops = &ext2_inode_operations;
    inode->fop = &dev_fops;
    inode->sb = sb;

    // 插入 icache
    vfs_icache_add(inode);

    // 锁定父目录 inode，保证目录修改操作的原子性
    mutex_lock(&ei_dir->lock);

    // 在父目录中添加目录项
    err = ext2_dir_collect_and_add(
        dir,
        new_ino,
        dentry->name.name,
        dentry->name.len,
        type,
        (uint32_t *)buf
    );
    if (err) {
        mutex_unlock(&ei_dir->lock);
        goto err_icache;
    }

    // 更新父目录磁盘 inode 的时间戳
    struct ext2_inode parent_raw;
    if (ext2_inode_read(sb, ei_dir, &parent_raw, buf) == 0) {
        parent_raw.i_mtime = now.tv_sec;
        parent_raw.i_ctime = now.tv_sec;
        ext2_inode_write(sb, sbi, dir->ino, &parent_raw, ei_dir, buf);
    }

    // 同步父目录内存时间戳
    dir->mtime = now;
    dir->ctime = now;

    // 写回超级块和组描述符
    ext2_sync_metadata(sb, sbi, buf);

    mutex_unlock(&ei_dir->lock);

    spin_lock(&dentry->lock);
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    spin_unlock(&dentry->lock);
    kheap_free(buf);
    return 0;

err_icache:
    vfs_iput(inode);
err_bitmap:
    ext2_ino_free(sb, sbi, new_ino, buf);
    ext2_sync_metadata(sb, sbi, buf);
    kheap_free(buf);
err_buf:
    ext2_destroy_inode(inode);
    return err;
}

static struct inode_operations ext2_inode_operations = {
    .lookup   = ext2_lookup,
    .create   = ext2_create,
    .symlink  = ext2_symlink,
    .link     = ext2_link,
    .unlink   = ext2_unlink,
    .mkdir    = ext2_mkdir,
    .rmdir    = ext2_rmdir,
    .rename   = ext2_rename,
    .getattr  = ext2_getattr,
    .setattr  = ext2_setattr,
    .readlink = ext2_readlink,
    .mknod    = ext2_mknod,
};

static int ext2_open(struct inode *inode, struct file *file) {
    return 0;
}

static int ext2_release(struct inode *inode, struct file *file) {
    vfs_iput(inode);
    return 0;
}

static ssize_t ext2_read(struct file *file, char *buf, size_t count, off_t *pos) {
    struct inode *inode = file->path.dentry->inode;
    struct ext2_sb_info *sbi = inode->sb->private;
    uint32_t block_size = sbi->block_size;
    off_t offset;
    uint32_t *work_buf;
    ssize_t ret;

    // 确定读取偏移
    offset = pos ? *pos : file->pos;

    // 不能超过文件大小
    if (offset >= (off_t)inode->size)
        return 0;

    if (offset + (off_t)count > (off_t)inode->size)
        count = inode->size - offset;

    // 分配工作缓冲区
    work_buf = kheap_alloc(block_size);
    if (!work_buf)
        return -ENOMEM;

    // 调用 ext2_read_data 读取数据
    ret = ext2_read_data(inode, offset, buf, count, work_buf);

    kheap_free(work_buf);

    if (ret < 0)
        return ret;

    // 更新偏移
    offset += ret;
    if (pos)
        *pos = offset;
    else
        file->pos = offset;

    return ret;
}

static ssize_t ext2_write(struct file *file, const char *buf, size_t count, off_t *pos) {
    struct inode *inode = file->path.dentry->inode;
    struct ext2_sb_info *sbi = inode->sb->private;
    struct ext2_inode_info *ei = inode->private;
    uint32_t block_size = sbi->block_size;
    off_t offset;
    uint32_t *work_buf;
    ssize_t ret;

    // 确定写入偏移
    offset = pos ? *pos : file->pos;

    // 分配工作缓冲区
    work_buf = kheap_alloc(block_size);
    if (!work_buf)
        return -ENOMEM;

    ret = ext2_write_data(inode, offset, buf, count, work_buf);
    if (ret < 0) {
        kheap_free(work_buf);
        return ret;
    }

    // 更新偏移
    offset += ret;
    if (pos)
        *pos = offset;
    else
        file->pos = offset;

    // 更新时间戳、i_size 和 i_blocks
    struct timespec now;
    time_get(&now);

    struct ext2_inode raw;
    if (ext2_inode_read(inode->sb, ei, &raw, (uint8_t *)work_buf) == 0) {
        raw.i_size = inode->size;
        raw.i_blocks = inode->blocks;
        raw.i_mtime = now.tv_sec;
        raw.i_ctime = now.tv_sec;
        if (ext2_inode_write(inode->sb, sbi, inode->ino, &raw, ei, (uint8_t *)work_buf) == 0) {
            inode->mtime = now;
            inode->ctime = now;
        }
    }

    kheap_free(work_buf);
    return ret;
}

static off_t ext2_llseek(struct file *file, off_t offset, seek_whence_t whence) {
    struct inode *inode = file->path.dentry->inode;
    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = file->pos + offset;
            break;
        case SEEK_END:
            new_pos = inode->size + offset;
            break;
        default:
            return -EINVAL;
    }

    if (new_pos < 0)
        return -EINVAL;

    return new_pos;
}

static int ext2_fsync(struct file *file, bool meta) {
    return block_flush(file->path.dentry->inode->sb->dev);
}

static struct file_operations ext2_file_operations = {
    .open    = ext2_open,
    .release = ext2_release,
    .read    = ext2_read,
    .write   = ext2_write,
    .llseek  = ext2_llseek,
    .fsync   = ext2_fsync,
};

static int ext2_dentry_compare(
    const struct dentry *dentry,
    uint32_t len,
    const char *str
) {
    // 长度不等，直接返回差值
    if (dentry->name.len != len)
        return (int)(dentry->name.len - len);

    // 哈希值不等，不匹配
    uint32_t hash = vfs_full_name_hash(str, len);
    if (dentry->name.hash != hash)
        return 1;

    // 长度和哈希都匹配，用 memcmp 做最终确认
    return memcmp(dentry->name.name, str, len);
}

static void ext2_dentry_release(struct dentry *dentry) {
    return;
}

static struct dentry_operations ext2_dentry_ops = {
    .compare = ext2_dentry_compare,
    .release = ext2_dentry_release,
};

static kptr ext2_mount(
    struct file_system_type *fst,
    mount_flags_t flags,
    const char *dev_name,
    void *data
) {
    struct super_block *sb = NULL;
    struct ext2_sb_info *sbi = NULL;
    struct ext2_super_block *esb;
    struct inode *root_inode = NULL;
    struct dentry *root_dentry = NULL;
    dev_t dev;
    uint8_t *buf = NULL;
    uint32_t block_size;
    uint32_t group_count;
    uint32_t gd_blocks;
    struct file *dev_file = NULL;
    int err = 0;

    // 拒绝无设备名的挂载
    if (!dev_name || !dev_name[0])
        return (kptr)K_ERR(-EINVAL);

    // 获取当前任务的文件系统上下文，用于解析设备路径
    struct path *root, *pwd;
    task_get_current_fs(&root, &pwd);
    if (!root || !pwd)
        return (kptr)K_ERR(-ENOMEM);

    // 打开设备文件，获取设备号
    kptr dev_res = vfs_open(dev_name, O_RDONLY, 0, pwd);
    K_ERR_LABEL_AND_SAVE(dev_res, out_put_fs, err);
    dev_file = dev_res.ptr;
    dev = vfs_inode_get_rdev(dev_file->path.dentry->inode);

    // 设备路径解析完成，释放文件系统上下文
    vfs_path_put(root);
    vfs_path_put(pwd);

    if (!dev) {
        err = -ENODEV;
        goto out_close_dev;
    }

    // 分配缓冲区并读取超级块（超级块固定位于字节偏移 EXT2_SB_OFFSET 处）
    buf = kheap_alloc(EXT2_SB_OFFSET);
    if (!buf) {
        err = -ENOMEM;
        goto out_close_dev;
    }

    if (block_read_sectors(
        dev,
        EXT2_SB_OFFSET / 512,
        buf,
        (sizeof(struct ext2_super_block) + 511) / 512
        ) < 0
    ) {
        err = -EIO;
        goto out_free_initial_buf;
    }

    esb = (struct ext2_super_block *)buf;

    // 校验魔数
    if (esb->s_magic != EXT2_SUPER_MAGIC) {
        err = -EINVAL;
        goto out_free_initial_buf;
    }

    // 计算块大小并校验
    block_size = 1024 << esb->s_log_block_size;
    if (block_size < EXT2_BLOCK_SIZE_MIN || block_size > EXT2_BLOCK_SIZE_MAX) {
        err = -EINVAL;
        goto out_free_initial_buf;
    }

    // 检查碎片配置
    if (esb->s_log_frag_size != esb->s_log_block_size) {
        err = -EINVAL;
        goto out_free_initial_buf;
    }

    // 检查不兼容特性（只允许 FILETYPE 和 META_BG）
    uint32_t incompat = esb->s_feature_incompat;
    uint32_t supported = EXT2_FEATURE_INCOMPAT_FILETYPE | EXT2_FEATURE_INCOMPAT_META_BG;
    if (incompat & ~supported) {
        err = -EINVAL;
        goto out_free_initial_buf;
    }

    // 如果文件系统含有日志，拒绝读写挂载
    if (esb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL)
        flags |= MS_RDONLY;

    // 分配超级块私有数据并缓存超级块
    sbi = kheap_alloc(sizeof(*sbi));
    if (!sbi) {
        err = -ENOMEM;
        goto out_free_initial_buf;
    }

    sbi->block_size = block_size;
    sbi->blocks_per_group = esb->s_blocks_per_group;
    sbi->inodes_per_group = esb->s_inodes_per_group;
    sbi->first_data_block = esb->s_first_data_block;
    sbi->group_desc_count = (esb->s_blocks_count + esb->s_blocks_per_group - 1) / esb->s_blocks_per_group;
    sbi->group_desc_block = sbi->first_data_block + 1;
    memcpy(&sbi->stat.super_block, esb, sizeof(*esb));
    mutex_init(&sbi->stat.lock);

    // 释放初始缓冲区，按块大小重新分配工作缓冲区
    kheap_free(buf);
    buf = kheap_alloc(block_size);
    if (!buf) {
        err = -ENOMEM;
        goto out_free_sbi;
    }

    // 分配 VFS 超级块并设置块大小
    sb = kheap_alloc(sizeof(*sb));
    if (!sb) {
        err = -ENOMEM;
        goto out_free_new_buf;
    }
    sb->dev = dev;
    sb->block_size = block_size;
    sb->block_size_bits = esb->s_log_block_size + 10;
    sb->magic = EXT2_SUPER_MAGIC;
    sb->ops = &ext2_super_operations;
    sb->private = sbi;
    atomic_init(&sb->count, 1);
    spinlock_init(&sb->lock);
    mutex_init(&sb->umount_lock);
    INIT_LIST_HEAD(&sb->inodes);
    INIT_LIST_HEAD(&sb->sb_list);
    sb->type = fst;

    // 读取块组描述符表
    group_count = sbi->group_desc_count;
    gd_blocks = (group_count * sizeof(struct ext2_group_desc) + block_size - 1) / block_size;
    sbi->stat.group_desc_table = kheap_alloc(gd_blocks * block_size);
    if (!sbi->stat.group_desc_table) {
        err = -ENOMEM;
        goto out_free_sb;
    }

    uint64_t gd_sector = ext2_block_to_sector(sb, sbi->group_desc_block);
    uint32_t sectors_per_block = (block_size + 511) / 512;
    if (block_read_sectors(
        dev,
        gd_sector,
        sbi->stat.group_desc_table,
        gd_blocks * sectors_per_block
        ) < 0
    ) {
        err = -EIO;
        goto out_free_gd;
    }

    // 读取根 inode
    root_inode = ext2_iget(sb, EXT2_ROOT_INO, buf);
    if (!root_inode) {
        err = -EIO;
        goto out_free_gd;
    }

    // 构造根 dentry
    root_dentry = kheap_alloc(sizeof(*root_dentry));
    if (!root_dentry) {
        err = -ENOMEM;
        goto out_iput_root;
    }

    root_dentry->parent = NULL;
    root_dentry->name.name = strdup("/");
    if (!root_dentry->name.name) {
        err = -ENOMEM;
        goto out_free_root;
    }
    root_dentry->name.len = 1;
    root_dentry->name.hash = vfs_full_name_hash("/", 1);
    root_dentry->inode = root_inode;
    root_dentry->ops = &ext2_dentry_ops;
    INIT_HLIST_NODE(&root_dentry->hash_node);
    INIT_LIST_HEAD(&root_dentry->lru);
    atomic_init(&root_dentry->count, 1);
    root_dentry->flags = DCACHE_NONE;
    spinlock_init(&root_dentry->lock);
    INIT_LIST_HEAD(&root_dentry->child);
    INIT_LIST_HEAD(&root_dentry->subdirs);
    root_dentry->key.parent = NULL;
    root_dentry->key.name = root_dentry->name;

    sb->root = root_dentry;

    // 挂载成功，释放临时缓冲区，返回根 dentry
    kheap_free(buf);
    vfs_close(dev_file);
    return (kptr)K_PTR(root_dentry);

out_free_root:
    kheap_free(root_dentry);
out_iput_root:
    vfs_iput(root_inode);
out_free_gd:
    kheap_free(sbi->stat.group_desc_table);
out_free_sb:
    kheap_free(sb);
out_free_new_buf:
    kheap_free(buf);
    goto out_close_dev;
out_free_sbi:
    kheap_free(sbi);
out_free_initial_buf:
    kheap_free(buf);
out_close_dev:
    if (dev_file)
        vfs_close(dev_file);
out_put_fs:
    vfs_path_put(root);
    vfs_path_put(pwd);
    return (kptr)K_ERR(err);
}

static void ext2_kill_sb(struct super_block *sb) {
    ext2_put_super(sb);
}

static void ext2_init(void) {
    static struct file_system_type ext2_type = {
        .name    = "ext2",
        .mount   = ext2_mount,
        .kill_sb = ext2_kill_sb,
    };

    vfs_register_filesystem(&ext2_type);
}

INITCALL(fs, 0, ext2_init);