/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <spinlock.h>
#include <shizi/types.h>
#include <vfs.h>
#include <list.h>
#include <hash.h>
#include <time.h>
#include <vfs.h>
#include <mutex.h>
#include <drivers.h>
#include <rcu.h>

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)

// 路径查找标志
typedef enum lookup_flags {
    LOOKUP_FOLLOW    = 1 << 0,   // 跟随符号链接
    LOOKUP_DIRECTORY = 1 << 1,   // 要求最后组件是目录
} vfs_lookup_flags_t;

// 文件偏移
typedef enum seek_whence {
    SEEK_SET = 0,   // 从文件开头偏移
    SEEK_CUR = 1,   // 从当前位置偏移
    SEEK_END = 2,   // 从文件末尾偏移
} seek_whence_t;

// 目录项标志
typedef enum dentry_flags {
    DCACHE_NONE     = 0,
    DCACHE_MOUNTED  = 1 << 0,   // 挂载点
    DCACHE_NEGATIVE = 1 << 1,   // 负缓存（d_inode == NULL）
} dentry_flags_t;

// 用于 setattr 的属性有效标志
typedef enum {
    ATTR_MODE  = 1 << 0,
    ATTR_UID   = 1 << 1,
    ATTR_GID   = 1 << 2,
    ATTR_SIZE  = 1 << 3,
    ATTR_ATIME = 1 << 4,
    ATTR_MTIME = 1 << 5,
} iattr_flag_t;

// 权限掩码
typedef enum {
    MAY_READ  = 1 << 0,
    MAY_WRITE = 1 << 1,
    MAY_EXEC  = 1 << 2,
} perm_mask_t;

// 用于快速比较字符串
struct qstr {
    const char *name;
    uint32_t len;
    uint32_t hash;
};

// 打开的文件描述符
struct file {
    struct path path;   // 包含 dentry 和 vfsmount
    off_t pos; // 当前偏移
    uint32_t mode;  // 打开模式
    open_flags_t flags; // 打开标志
    atomic_uint count;  // 引用计数
    struct file_operations *ops;    // 文件操作表
    spinlock_t lock;    // 保护 pos, flags
};

// 用于 setattr 的属性结构体
struct iattr {
    uint32_t ia_valid;  
    mode_t ia_mode;
    uid_t ia_uid;
    gid_t ia_gid;
    off_t ia_size;
    struct timespec ia_atime;
    struct timespec ia_mtime;
};

// 用于 dentry 哈希缓存的键
struct dentry_key {
    struct dentry *parent;
    struct qstr name;
};

// 用于 inode 哈希缓存的键
struct inode_key {
    struct super_block *sb;
    ino_t ino;
};

// 文件系统类型，用于注册文件系统
struct file_system_type {
    const char *name;   
    struct dentry *(*mount)(
        struct file_system_type *fst, 
        mount_flags_t flags,
        const char *dev_name,
        void *data
    );
    void (*kill_sb)(struct super_block *sb);    // 卸载文件系统时调用，释放超级块资源
    struct list_head list;   // 用于挂入 fs_list_head 链表的节点
};

// 超级块结构体，表示一个挂载的文件系统实例
struct super_block {
    dev_t dev;  // 设备号
    uint32_t block_size;  // 块大小
    uint8_t block_size_bits;  // 块大小的位数
    uint32_t magic;  // 文件系统魔数
    struct super_operations *ops;  // 超级块操作表
    struct dentry *root;  // 挂载的根目录
    struct list_head inodes;  // 已加载的 inode 列表
    atomic_uint count;  // 引用计数
    spinlock_t lock;    // 保护 inodes 链表和 root 指针
    mutex_t umount_lock;    // 保护卸载过程
    struct list_head sb_list;  // 超级块链表
    void *private;  // 文件系统私有数据
    struct file_system_type *type; // 所属文件系统类型
};

// 索引节点
struct inode {
    uint64_t ino;  // 索引节点号
    mode_t mode;  // 文件模式
    uid_t uid;    // 属主用户 ID
    gid_t gid;    // 属主组 ID
    off_t size;  // 文件大小
    struct timespec atime;  // 最后访问时间
    struct timespec mtime;  // 最后修改时间
    struct timespec ctime;  // 最后状态改变时间
    blkcnt_t blocks;  // 占用的块数
    struct inode_operations *ops;  // 索引节点操作表
    struct file_operations *fop;    // 文件操作表
    struct super_block *sb; // 所属超级块

    /*
     * 保护 mode, uid, gid, size, 
     * atime, mtime, ctime, blocks, 
     * nlink, lru, sb_list, private  
     */
    spinlock_t lock;    
    atomic_uint count;  // 引用计数
    nlink_t nlink; // 硬链接数
    struct list_head lru;   // LRU 链表节点（用于回收）
    struct hlist_node hash; // 哈希表节点（用于 inode 缓存）
    struct list_head sb_list;   // 用于挂入 super_block->inodes 链表的节点
    struct rcu_head rcu;    // 用于保护结构体实例占用的内存
    void *private;  // 文件系统私有数据
    struct inode_key key;   // 用于哈希缓存的键
};

// 目录项结构体，表示文件系统中的一个目录项
struct dentry {
    struct dentry *parent;  // 父目录项指针
    struct qstr name;   
    struct inode * __rcu inode;    // 目录项对应的 inode
    struct dentry_operations *ops;  // 目录项操作表
    struct hlist_node hash_node; // 哈希表节点（用于dentry 缓存）
    struct list_head lru;   // LRU 链表节点
    atomic_uint count;  // 引用计数
    dentry_flags_t flags; // 状态标志
    spinlock_t lock;    // 保护 parent, inode, flags
    struct list_head child;   // 父目录的子链表节点
    struct list_head subdirs; // 子目录项链表头（仅目录使用）
    struct rcu_head rcu;    // 确保结构体实例占用的内存安全释放
    struct dentry_key key;    // 用于哈希缓存的键
};

// 挂载点结构体，表示一个挂载的文件系统实例
struct vfsmount {
    struct super_block *sb; // 挂载的超级块
    struct dentry *root;    // 被挂载文件系统的根 dentry
    struct dentry *mountpoint;  // 挂载点目录的 dentry（父文件系统）
    struct vfsmount *parent;    // 父挂载点
    mount_flags_t flags; // 挂载标志
    struct list_head list;  // 用于链接到父挂载点的子挂载链表
    struct list_head mnt_mounts; // 子挂载点链表头
    atomic_uint count;  // 引用计数
};

// 超级块操作表
struct super_operations {
    struct inode *(*alloc_inode)(struct super_block *sb); // 分配一个新的 inode
    void (*destroy_inode)(struct inode *inode);  // 销毁一个 inode
    void (*evict_inode)(struct inode *inode);  // 回收一个 inode（当引用计数为0时调用）
    void (*put_super)(struct super_block *sb);    // 卸载文件系统时调用，释放超级块资源
    int (*sync_fs)(struct super_block *sb, bool wait); // 同步文件系统数据到磁盘
    int (*statfs)(struct dentry *dentry, struct statfs *buf);    // 获取文件系统统计信息
};

// 索引节点操作表
struct inode_operations {
    struct dentry *(*lookup)(struct inode *dir, struct dentry *dentry);    // 查找目录项
    int (*create)(struct inode *dir, struct dentry *dentry, mode_t mode);    // 创建新文件
    int (*symlink)(struct inode *dir, struct dentry *dentry, const char *target);   // 创建软链接
    int (*link)(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry); // 创建硬链接
    int (*unlink)(struct inode *dir, struct dentry *dentry);    // 删除文件
    int (*mkdir)(struct inode *dir, struct dentry *dentry, mode_t mode);    // 创建目录
    int (*rmdir)(struct inode *dir, struct dentry *dentry);    // 删除目录
    int (*rename)(
        struct inode *old_dir, 
        struct dentry *old_dentry, 
        struct inode *new_dir, 
        struct dentry *new_dentry
    );    // 重命名文件
    int (*setattr)(struct dentry *dentry, struct iattr *attr);    // 设置属性
    int (*getattr)(struct path *path, struct kstat *stat);    // 获取属性
    ssize_t (*readlink)(struct inode *inode, char *buf, size_t bufsiz); // 读取符号链接目标
};

// 文件操作表
struct file_operations {
    int (*open)(struct inode *inode, struct file *file); // 打开文件
    int (*release)(struct inode *inode, struct file *file);  // 关闭文件
    ssize_t (*read)(struct file *file, char *buf, size_t count, off_t *pos);   // 读取文件数据
    ssize_t (*write)(struct file *file, const char *buf, size_t count, off_t *pos);    // 写入文件数据
    off_t (*llseek)(struct file *file, off_t offset, seek_whence_t whence);   // 调整文件偏移
};

// 目录项操作表
struct dentry_operations {
    int (*compare)(const struct dentry *dentry, uint32_t len, const char *str);    // 比较目录项名称
    void (*release)(struct dentry *dentry); // 释放目录项资源
};

// 用于vfs的锁链表
struct vfs_lock_list {
    struct list_head list;
    spinlock_t lock;
};

// 用于vfs的哈希表
struct vfs_hash_table {
    struct hash_table hash; // 哈希表
    spinlock_t lock;    // 保护哈希表的锁
};

// 用于解析路径字符串的句柄，记录解析状态
typedef struct {
    const char *path;   // 需要解析的字符串
    struct path *curr_path;    // 当前解析到的路径
    size_t offset;   // 当前解析位置
} vfs_path_prs_t;

// 注册文件系统
int vfs_register_filesystem(struct file_system_type *fs);

// 计算字符串的哈希值
uint32_t vfs_full_name_hash(const char *name, unsigned int len);