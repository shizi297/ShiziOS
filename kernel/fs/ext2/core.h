/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <asm/mm_addr.h>

// 超级块魔数
#define EXT2_SUPER_MAGIC 0xEF53

// 保留 inode 号
#define EXT2_ROOT_INO 2
#define EXT2_NAME_LEN UINT8_MAX
#define EXT2_SB_OFFSET 1024
#define EXT2_BLOCK_SIZE_MIN 1024
#define EXT2_BLOCK_SIZE_MAX PAGE_SIZE 

#define EXT2_S_IFMT  ((uint16_t)GENMASK(15, 12))   // 文件类型掩码
#define EXT2_S_PERM  ((uint16_t)GENMASK(11, 0))    // 权限位掩码

// 超级块状态
typedef enum : uint16_t {
    EXT2_VALID_FS = 1,          // 文件系统正常，未发生错误
    EXT2_ERROR_FS = 2,          // 文件系统发生过错误，需要检查
} ext2_state_t;

// 错误处理行为
typedef enum : uint16_t {
    EXT2_ERRORS_CONTINUE = 1,   // 忽略错误，继续运行
    EXT2_ERRORS_RO       = 2,   // 重新挂载为只读
    EXT2_ERRORS_PANIC    = 3,   // 触发内核 panic
} ext2_errors_t;

// 创建操作系统
typedef enum : uint32_t {
    EXT2_OS_LINUX   = 0,        // Linux
    EXT2_OS_HURD    = 1,        // GNU Hurd
    EXT2_OS_MASIX   = 2,        // Masix
    EXT2_OS_FREEBSD = 3,        // FreeBSD
    EXT2_OS_LITES   = 4,        // Lites
} ext2_os_t;

// 修订版本
typedef enum : uint32_t {
    EXT2_GOOD_OLD_REV = 0,      // 原始 ext2 版本
    EXT2_DYNAMIC_REV  = 1,      // 支持动态 inode 大小等新特性
} ext2_rev_t;

// 兼容特性位
typedef enum : uint32_t {
    EXT2_FEATURE_COMPAT_DIR_PREALLOC  = 1ULL << 0, // 目录预分配
    EXT2_FEATURE_COMPAT_IMAGIC_INODES = 1ULL << 1, // 特殊魔数 inode
    EXT3_FEATURE_COMPAT_HAS_JOURNAL   = 1ULL << 2, // 存在日志（ext3）
    EXT2_FEATURE_COMPAT_EXT_ATTR      = 1ULL << 3, // 扩展属性
    EXT2_FEATURE_COMPAT_RESIZE_INO    = 1ULL << 4, // 预留 inode 用于扩容
    EXT2_FEATURE_COMPAT_DIR_INDEX     = 1ULL << 5, // 目录索引（HTree）
} ext2_feature_compat_t;

// 不兼容特性位
typedef enum : uint32_t {
    EXT2_FEATURE_INCOMPAT_COMPRESSION = 1ULL << 0, // 压缩支持
    EXT2_FEATURE_INCOMPAT_FILETYPE    = 1ULL << 1, // 目录项包含文件类型
    EXT3_FEATURE_INCOMPAT_RECOVER     = 1ULL << 2, // 日志需要恢复
    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV = 1ULL << 3, // 日志设备
    EXT2_FEATURE_INCOMPAT_META_BG     = 1ULL << 4, // 元块组
} ext2_feature_incompat_t;

// 只读兼容特性位
typedef enum : uint32_t {
    EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER = 1ULL << 0, // 稀疏超级块
    EXT2_FEATURE_RO_COMPAT_LARGE_FILE   = 1ULL << 1, // 支持大文件（>2GB）
    EXT2_FEATURE_RO_COMPAT_BTREE_DIR    = 1ULL << 2, // B-tree 目录（HTree）
} ext2_feature_ro_compat_t;

// 压缩算法位图
typedef enum : uint32_t {
    EXT2_LZV1_ALG   = 1ULL << 0, // LZV1 压缩
    EXT2_LZRW3A_ALG = 1ULL << 1, // LZRW3A 压缩
    EXT2_GZIP_ALG   = 1ULL << 2, // GZIP 压缩
    EXT2_BZIP2_ALG  = 1ULL << 3, // BZIP2 压缩
    EXT2_LZO_ALG    = 1ULL << 4, // LZO 压缩
} ext2_algo_t;

// inode 行为控制标志
typedef enum : uint32_t {
    EXT2_SECRM_FL         = 1ULL << 0,  // 安全删除（覆盖数据）
    EXT2_UNRM_FL          = 1ULL << 1,  // 可恢复删除
    EXT2_COMPR_FL         = 1ULL << 2,  // 压缩文件
    EXT2_SYNC_FL          = 1ULL << 3,  // 同步写入
    EXT2_IMMUTABLE_FL     = 1ULL << 4,  // 不可变文件
    EXT2_APPEND_FL        = 1ULL << 5,  // 仅追加模式
    EXT2_NODUMP_FL        = 1ULL << 6,  // 不 dump
    EXT2_NOATIME_FL       = 1ULL << 7,  // 不更新访问时间
    EXT2_DIRTY_FL         = 1ULL << 8,  // 脏文件
    EXT2_COMPRBLK_FL      = 1ULL << 9,  // 压缩块
    EXT2_NOCOMPR_FL       = 1ULL << 10, // 禁止压缩
    EXT2_ECOMPR_FL        = 1ULL << 11, // 错误压缩
    EXT2_INDEX_FL         = 1ULL << 12, // 目录索引
    EXT2_IMAGIC_FL        = 1ULL << 13, // 魔数 inode
    EXT3_JOURNAL_DATA_FL  = 1ULL << 14, // 日志数据（ext3）
    EXT2_RESERVED_FL      = 1ULL << 31, // 保留，不得使用
} ext2_inode_flags_t;

// 文件类型（i_mode 高四位）
typedef enum : uint16_t {
    EXT2_S_IFSOCK = 0xC000, // socket
    EXT2_S_IFLNK  = 0xA000, // 符号链接
    EXT2_S_IFREG  = 0x8000, // 普通文件
    EXT2_S_IFBLK  = 0x6000, // 块设备
    EXT2_S_IFDIR  = 0x4000, // 目录
    EXT2_S_IFCHR  = 0x2000, // 字符设备
    EXT2_S_IFIFO  = 0x1000, // FIFO
} ext2_inode_mode_t;

// 目录项文件类型
typedef enum : uint8_t {
    EXT2_FT_UNKNOWN  = 0, // 未知类型
    EXT2_FT_REG_FILE = 1, // 普通文件
    EXT2_FT_DIR      = 2, // 目录
    EXT2_FT_CHRDEV   = 3, // 字符设备
    EXT2_FT_BLKDEV   = 4, // 块设备
    EXT2_FT_FIFO     = 5, // FIFO
    EXT2_FT_SOCK     = 6, // socket
    EXT2_FT_SYMLINK  = 7, // 符号链接
} ext2_file_type_t;

// 索引目录哈希版本
typedef enum : uint8_t {
    DX_HASH_LEGACY    = 0, // 传统哈希
    DX_HASH_HALF_MD4  = 1, // Half-MD4 哈希
    DX_HASH_TEA       = 2, // TEA 哈希
} ext2_dx_hash_version_t;

struct ext2_super_block {
    uint32_t s_inodes_count;          // 总 inode 数
    uint32_t s_blocks_count;          // 总块数
    uint32_t s_r_blocks_count;        // 超级用户保留块数
    uint32_t s_free_blocks_count;     // 空闲块数
    uint32_t s_free_inodes_count;     // 空闲 inode 数
    uint32_t s_first_data_block;      // 第一个数据块号
    uint32_t s_log_block_size;        // 块大小 = 1024 << s_log_block_size
    uint32_t s_log_frag_size;         // 碎片大小
    uint32_t s_blocks_per_group;      // 每组块数
    uint32_t s_frags_per_group;       // 每组碎片数
    uint32_t s_inodes_per_group;      // 每组 inode 数
    uint32_t s_mtime;                 // 上次挂载时间
    uint32_t s_wtime;                 // 上次写入时间
    uint16_t s_mnt_count;             // 挂载计数
    uint16_t s_max_mnt_count;         // 最大挂载次数
    uint16_t s_magic;                 // 魔数 0xEF53
    ext2_state_t s_state;             // 文件系统状态
    ext2_errors_t s_errors;           // 错误处理方式
    uint16_t s_minor_rev_level;       // 次版本号
    uint32_t s_lastcheck;             // 上次检查时间
    uint32_t s_checkinterval;         // 检查间隔（秒）
    ext2_os_t s_creator_os;           // 创建操作系统
    ext2_rev_t s_rev_level;           // 主修订版本
    uint16_t s_def_resuid;            // 保留块默认用户 ID
    uint16_t s_def_resgid;            // 保留块默认组 ID
    uint32_t s_first_ino;             // 第一个非保留 inode
    uint16_t s_inode_size;            // inode 结构大小
    uint16_t s_block_group_nr;        // 本超级块所在组号
    ext2_feature_compat_t s_feature_compat;       // 兼容特性掩码
    ext2_feature_incompat_t s_feature_incompat;   // 不兼容特性掩码
    ext2_feature_ro_compat_t s_feature_ro_compat; // 只读兼容特性掩码
    uint8_t s_uuid[16];               // 卷 UUID
    uint8_t s_volume_name[16];        // 卷名
    uint8_t s_last_mounted[64];       // 上次挂载点
    ext2_algo_t s_algo_bitmap;        // 压缩算法位图
    uint8_t s_prealloc_blocks;        // 预分配块数（普通文件）
    uint8_t s_prealloc_dir_blocks;    // 预分配块数（目录）
    uint16_t s_padding1;              // 对齐填充
    uint8_t s_journal_uuid[16];       // 日志超级块 UUID
    uint32_t s_journal_inum;          // 日志文件 inode 号
    uint32_t s_journal_dev;           // 日志设备号
    uint32_t s_last_orphan;           // 孤儿 inode 列表头
    uint32_t s_hash_seed[4];          // 目录索引哈希种子
    uint8_t s_def_hash_version;       // 默认哈希版本
    uint8_t s_padding2[3];            // 保留
    uint32_t s_default_mount_options; // 默认挂载选项
    uint32_t s_first_meta_bg;         // 首个元块组号
    uint8_t s_reserved[760];          // 保留
} __attribute__((packed));

// 块组描述符
struct ext2_group_desc {
    uint32_t bg_block_bitmap;         // 块位图所在块号
    uint32_t bg_inode_bitmap;         // inode 位图所在块号
    uint32_t bg_inode_table;          // inode 表起始块号
    uint16_t bg_free_blocks_count;    // 本组空闲块数
    uint16_t bg_free_inodes_count;    // 本组空闲 inode 数
    uint16_t bg_used_dirs_count;      // 本组目录 inode 数
    uint16_t bg_pad;                  // 填充
    uint8_t bg_reserved[12];          // 保留
} __attribute__((packed));

struct ext2_inode {
    uint16_t i_mode;                  // 文件类型和权限
    uint16_t i_uid;                   // 用户 ID（低 16 位）
    uint32_t i_size;                  // 文件大小（低 32 位）
    uint32_t i_atime;                 // 访问时间
    uint32_t i_ctime;                 // 创建时间
    uint32_t i_mtime;                 // 修改时间
    uint32_t i_dtime;                 // 删除时间
    uint16_t i_gid;                   // 组 ID（低 16 位）
    uint16_t i_links_count;           // 硬链接计数
    uint32_t i_blocks;                // 占用的 512 字节扇区数
    ext2_inode_flags_t i_flags;       // 行为控制标志
    uint32_t i_osd1;                  // 操作系统相关 1
    uint32_t i_block[15];             // 数据块指针
    uint32_t i_generation;            // NFS 版本
    uint32_t i_file_acl;              // 扩展属性块号
    uint32_t i_dir_acl;               // 目录扩展属性或高 32 位大小
    uint32_t i_faddr;                 // 碎片地址
    uint8_t osd2[12];                 // 操作系统相关 2
} __attribute__((packed));

// 目录项
struct ext2_dir_entry {
    uint32_t inode;                   // inode 号（0 表示空闲）
    uint16_t rec_len;                 // 本记录长度
    uint8_t name_len;                 // 名字长度
    ext2_file_type_t file_type;       // 文件类型
    char name[];                      // 名字
} __attribute__((packed));