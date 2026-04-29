/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <drivers.h>
#include <shizi/types.h>
#include <time.h>

// 文件的偏移量
typedef int64_t off_t;

// inode 号类型
typedef uint64_t ino_t;

// 硬链接计数类型
typedef uint64_t nlink_t;

// 打开的文件描述符
struct file;

// 文件模式类型
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
    MS_RDONLY   = 1 << 0,  // 只读挂载
} mount_flags_t;

// 打开文件标志
typedef enum open_flags {
    O_RDONLY   = 0,          // 只读打开
    O_WRONLY   = 1,          // 只写打开
    O_RDWR     = 2,          // 读写打开
    O_CREAT    = 0x40,       // 若文件不存在则创建
    O_EXCL     = 0x80,       // 与 O_CREAT 一起使用，文件已存在则失败
    O_TRUNC    = 0x200,      // 若文件存在且可写，则截断长度为 0
    O_APPEND   = 0x400,      // 每次写操作追加到文件末尾
    O_NONBLOCK = 0x800,      // 非阻塞模式
    O_SYNC     = 0x1000,     // 同步 I/O（写操作等待数据落盘）
    O_CLOEXEC  = 0x80000,    // 执行时关闭文件描述符
} open_flags_t;

// 路径结构体，表示一个文件系统路径
struct path {
    struct vfsmount *mnt;
    struct dentry *dentry;
};

// vfs 文件状态信息
struct kstat {
    dev_t st_dev;           // 文件所在设备号
    ino_t st_ino;           // inode 号
    mode_t st_mode;         // 文件类型和权限
    nlink_t st_nlink;       // 硬链接数
    uid_t st_uid;           // 属主用户 ID
    gid_t st_gid;           // 属主组 ID
    dev_t st_rdev;          // 特殊设备文件的设备号（tmpfs 为 0）
    off_t st_size;          // 文件逻辑大小（字节）
    struct timespec st_atim; // 最后访问时间
    struct timespec st_mtim; // 最后修改时间
    struct timespec st_ctim; // 最后状态改变时间
    blksize_t st_blksize;   // 文件系统块大小
    blkcnt_t st_blocks;     // 已分配的 512 字节块数
};

// vfs 文件系统统计信息
struct statfs {
    uint64_t f_bsize;   // 文件系统块大小
    uint64_t f_blocks;  // 总块数
    uint64_t f_bfree;   // 空闲块数
    uint64_t f_bavail;  // 非特权用户可用块数
    uint64_t f_files;   // 总文件节点数
    uint64_t f_ffree;   // 空闲文件节点数
    uint64_t f_namemax; // 最大文件名长度
};

/**
 * 挂载一个文件系统
 * 
 * @param dev_name 设备名称
 * @param dir_name 挂载点路径
 * @param type 文件系统类型名称
 * @param flags 挂载标志
 * @param data 文件系统特定数据
 * 
 * @return 挂载点的 vfsmount 指针
 */
struct vfsmount *vfs_mount(
    const char *dev_name,
    const char *dir_name,
    const char *type,
    mount_flags_t flags,
    void *data
);

// 获取根目录path
struct path *vfs_get_root_path(void);

// 初始化vfs
bool vfs_init(void);

/**
 * 打开文件
 *
 * @param path 路径字符串
 * @param flags 打开标志
 * @param mode 创建时的权限
 * @param pwd 当前工作目录
 *
 * @return file 指针
 */
struct file *vfs_open(
    const char *path,
    open_flags_t flags,
    mode_t mode,
    const struct path *pwd
);

/**
 * 读取文件
 *
 * @param file 打开的文件
 * @param buf 用户缓冲区
 * @param count 读取字节数
 * @param pos 偏移指针，NULL 表示使用 file 内部偏移
 *
 * @return 实际读取字节数
 */
ssize_t vfs_read(struct file *file, char *buf, size_t count, off_t *pos);

/**
 * 写入文件
 *
 * @param file 打开的文件
 * @param buf 数据缓冲区
 * @param count 写入字节数
 * @param pos 偏移指针，NULL 表示使用 file 内部偏移
 *
 * @return 实际写入字节数
 */
ssize_t vfs_write(struct file *file, const char *buf, size_t count, off_t *pos);

/**
 * 关闭文件
 *
 * @param file 要关闭的文件
 */
void vfs_close(struct file *file);