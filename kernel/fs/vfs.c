/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stddef.h>
#include <list.h>
#include <stdatomic.h>
#include <spinlock.h>
#include <mutex.h>
#include <time.h>
#include <shizi/types.h>
#include <stdbool.h>

// 设备号, 目前现在这里定义，在以后移到其他模块
typedef uint64_t dev_t; 

// 块计数类型,后面移动到其他模块
typedef int64_t blkcnt_t;

// 块大小,后面移动到其他模块
typedef int64_t blksize_t;

// 文件的偏移量
typedef int64_t off_t;

// inode 号类型
typedef uint64_t ino_t;

// 硬链接计数类型
typedef uint64_t nlink_t;

// 文件模式类型（枚举）
typedef enum mode_t : unsigned int {
    // 文件类型
    S_IFMT   = 0170000,   // 类型掩码
    S_IFSOCK = 0140000,   // socket
    S_IFLNK  = 0120000,   // 符号链接
    S_IFREG  = 0100000,   // 普通文件
    S_IFBLK  = 0060000,   // 块设备
    S_IFDIR  = 0040000,   // 目录
    S_IFCHR  = 0020000,   // 字符设备
    S_IFIFO  = 0010000,   // FIFO

    // setuid/setgid/sticky
    S_ISUID  = 0004000,  
    S_ISGID  = 0002000,   
    S_ISVTX  = 0001000,   

    // 属主权限
    S_IRWXU  = 00700,     // 属主读/写/执行
    S_IRUSR  = 00400,     // 属主读
    S_IWUSR  = 00200,     // 属主写
    S_IXUSR  = 00100,     // 属主执行

    // 属组权限
    S_IRWXG  = 00070,     // 属组读/写/执行
    S_IRGRP  = 00040,     // 属组读
    S_IWGRP  = 00020,     // 属组写
    S_IXGRP  = 00010,     // 属组执行

    // 其他用户权限
    S_IRWXO  = 00007,     // 其他读/写/执行
    S_IROTH  = 00004,     // 其他读
    S_IWOTH  = 00002,     // 其他写
    S_IXOTH  = 00001,     // 其他执行
} mode_t;

// 用于挂载时指定标志 
typedef enum mount_flags {
    MS_NONE     = 0,       // 无特殊标志
    MS_RDONLY   = 1 << 0,   // 只读挂载
    MS_NOSUID   = 1 << 1,   // 忽略 setuid/setgid 位
    MS_NOEXEC   = 1 << 2,   // 不允许执行程序
    MS_NODEV    = 1 << 3,   // 禁止访问设备文件
    MS_SYNC     = 1 << 4,   // 同步 I/O
    MS_NOATIME  = 1 << 5,   // 不更新访问时间
    MS_REMOUNT  = 1 << 6,   // 重新挂载（修改标志）
    MS_BIND     = 1 << 7,   // 绑定挂载（将目录挂载到另一处）
    MS_REC      = 1 << 8,   // 递归挂载（用于绑定）
} mount_flags_t;

// 打开文件标志
typedef enum open_flags {
    O_RDONLY   = 0,
    O_WRONLY   = 1,
    O_RDWR     = 2,
    O_CREAT    = 0x40,
    O_EXCL     = 0x80,
    O_TRUNC    = 0x200,
    O_APPEND   = 0x400,
    O_NONBLOCK = 0x800,
    O_SYNC     = 0x1000,
    O_CLOEXEC  = 0x80000,
} open_flags_t;

// 路径查找标志
typedef enum lookup_flags {
    LOOKUP_FOLLOW    = 1,   // 跟随符号链接
    LOOKUP_DIRECTORY = 2,   // 要求最后组件是目录
} lookup_flags_t;

// 文件偏移 whence
typedef enum seek_whence {
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2,
} seek_whence_t;

// 用于vfs的锁链表
struct vfs_lock_list {
    struct list_head list;
    void *lock;
};

// 文件系统锁链表头，用于管理已注册的文件系统
static struct vfs_lock_list fs_list_head;

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
};

// 目录项结构体，表示文件系统中的一个目录项
struct dentry {
    struct dentry *parent;  // 父目录项指针
    struct qstr name;   
    struct inode *inode;    // 目录项对应的 inode
    struct dentry_operations *ops;  // 目录项操作表
    struct hlist_node hash_node; // 哈希表节点（用于dentry 缓存）
    struct list_head lru;   // LRU 链表节点
    atomic_uint count;  // 引用计数
    uint32_t flags; // 状态标志
    spinlock_t lock;    // 保护 parent, inode, flags
    struct rcu_head rcu;    // 确保结构体实例占用的内存安全释放
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

// 挂载点结构体，表示一个挂载的文件系统实例
struct vfsmount {
    struct super_block *sb; // 挂载的超级块
    struct dentry *root;    // 被挂载文件系统的根 dentry
    struct dentry *mountpoint;  // 挂载点目录的 dentry（父文件系统）
    struct vfsmount *parent;    // 父挂载点
    uint32_t flags; // 挂载标志
    struct list_head list;  // 挂载树链表节点
    atomic_uint count;  // 引用计数
};

// 路径结构体，表示一个文件系统路径
struct path {
    struct vfsmount *mnt;
    struct dentry *dentry;
};

// 用于快速比较字符串
struct qstr {
    const char *name;
    uint32_t len;
    uint32_t hash;
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
    struct dentry *(*lookup)(struct inode *dir, struct dentry *dentry, lookup_flags_t flags);    // 查找目录项
    int (*create)(struct inode *dir, struct dentry *dentry, mode_t mode);    // 创建新文件
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
    int (*d_compare)(const struct dentry *dentry, uint32_t len, const char *str);    // 比较目录项名称
    void (*d_release)(struct dentry *dentry); // 释放目录项资源
};