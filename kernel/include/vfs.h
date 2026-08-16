/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
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

// 块计数类型
typedef int64_t blkcnt_t;

// 块大小
typedef int64_t blksize_t;

struct inode;
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
    MS_NOUSER   = 1 << 1,  // 禁止用户卸载（仅内核内部可使用）
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
    O_DIRECTORY = 0x10000,   // 要求打开的文件必须是目录
    O_NOFOLLOW = 0x20000,    // 不跟随符号链接
    O_CLOEXEC  = 0x80000,    // 执行时关闭文件描述符
} open_flags_t;

// 文件偏移
typedef enum seek_whence {
    SEEK_SET = 0,   // 从文件开头偏移
    SEEK_CUR = 1,   // 从当前位置偏移
    SEEK_END = 2,   // 从文件末尾偏移
} seek_whence_t;

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

// 路径结构体，表示一个文件系统路径
struct path {
    struct vfsmount *mnt;
    struct dentry *dentry;
};

// 文件操作表
struct file_operations {
    int (*open)(struct inode *inode, struct file *file); // 打开文件
    int (*release)(struct inode *inode, struct file *file);  // 关闭文件

    /**
     * 读取文件数据
     *
     * @param file 打开的文件
     * @param buf 用户缓冲区
     * @param count 请求读取的字节数
     * @param pos 偏移指针，NULL 表示使用 file 内部偏移
     *
     * @return 成功返回实际读取字节数，失败返回错误码
     */
    ssize_t (*read)(struct file *file, char *buf, size_t count, off_t *pos);  

    /**
     * 写入文件数据
     *
     * @param file 打开的文件
     * @param buf 数据缓冲区
     * @param count 请求写入的字节数
     * @param pos 偏移指针，NULL 表示使用 file 内部偏移
     *
     * @return 实际写入字节数
     */
    ssize_t (*write)(struct file *file, const char *buf, size_t count, off_t *pos);    

    /**
     * 调整文件偏移
     *
     * @param file 打开的文件
     * @param offset 偏移量
     * @param whence 参照点
     *
     * @return 新的文件偏移
     */
    off_t (*llseek)(struct file *file, off_t offset, seek_whence_t whence);  

    /**
     * 同步文件数据到持久存储
     *
     * @param file 文件结构体
     * @param meta true 表示是否同步源数据
     */
    int (*fsync)(struct file *file, bool meta); 
};

/*
 * 增加文件对象的引用计数
 *
 * @param file 文件对象指针
 */
void vfs_file_get(struct file *file);

// 增加路径引用
void vfs_path_get(struct path *path);

// 减少路径引用
void vfs_path_put(struct path *path);

/**
 * 挂载一个文件系统
 *
 * @param dev_name 设备名称
 * @param dir_name 挂载点路径
 * @param type 文件系统类型名称
 * @param flags 挂载标志
 * @param data 文件系统特定数据
 * @param from_kernel 调用者是否为内核
 *
 * @return .ptr 指向挂载点的 vfsmount
 */
kptr vfs_mount(
    const char *dev_name,
    const char *dir_name,
    const char *type,
    mount_flags_t flags,
    void *data,
    bool from_kernel
);

/**
 * 卸载文件系统
 *
 * @param dir_name 挂载点路径
 * @param flags 标志位
 * @param from_kernel 调用者是否为内核
 */
int vfs_umount(const char *dir_name, int flags, bool from_kernel);

// 获取根目录path
struct path *vfs_get_root_path(void);

// 获取 inode 的 rdev 字段
dev_t vfs_inode_get_rdev(struct inode *inode);

// 获取 file 结构体的私有数据
void *vfs_file_get_private(struct file *file);

// 设置 file 结构体的私有数据
void vfs_file_set_private(struct file *file, void *priv);

// 初始化vfs
bool vfs_init(void);

/**
 * 调整文件偏移
 *
 * @param file 打开的文件
 * @param offset 偏移量
 * @param whence 参照点
 *
 * @return 新的文件偏移
 */
off_t vfs_lseek(struct file *file, off_t offset, seek_whence_t whence);

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
 * 截断文件
 *
 * @param file 打开的文件
 * @param length 目标长度
 */
int vfs_truncate(struct file *file, off_t length);

/**
 * 关闭文件
 *
 * @param file 要关闭的文件
 */
void vfs_close(struct file *file);

/**
 * 创建目录
 *
 * @param path 路径字符串
 * @param mode 目录权限
 * @param pwd 当前工作目录
 */
int vfs_mkdir(const char *path, mode_t mode, const struct path *pwd);

/**
 * 删除空目录
 *
 * @param path 路径字符串
 * @param pwd 当前工作目录
 */
int vfs_rmdir(const char *path, const struct path *pwd);

/**
 * 删除文件
 *
 * @param path 路径字符串
 * @param pwd 当前工作目录
 */
int vfs_unlink(const char *path, const struct path *pwd);

/**
 * 创建符号链接
 *
 * @param target 链接目标字符串
 * @param linkpath 链接路径
 * @param pwd 当前工作目录
 */
int vfs_symlink(const char *target, const char *linkpath, const struct path *pwd);

/**
 * 创建硬链接
 *
 * @param oldpath 现有文件路径
 * @param newpath 新链接路径
 * @param pwd 当前工作目录
 */
int vfs_link(const char *oldpath, const char *newpath, const struct path *pwd);

/**
 * 重命名或移动文件/目录
 *
 * @param oldpath 源路径
 * @param newpath 目标路径
 */
int vfs_rename(const char *oldpath, const char *newpath, const struct path *pwd);

/**
 * 获取文件属性
 *
 * @param path 路径字符串
 * @param stat 输出状态结构体
 * @param pwd 当前工作目录
 */
int vfs_getattr(const char *path, struct kstat *stat, const struct path *pwd);

/**
 * 设置文件属性
 *
 * @param path 路径字符串
 * @param attr 属性结构体
 * @param pwd 当前工作目录
 */
int vfs_setattr(const char *path, struct iattr *attr, const struct path *pwd);

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
kptr vfs_open(const char *path, open_flags_t flags, mode_t mode, const struct path *pwd);

/**
 * 创建设备节点
 *
 * @param path 路径字符串
 * @param mode 文件类型和权限（必须包含 S_IFCHR 或 S_IFBLK）
 * @param dev 设备号
 * @param pwd 当前工作目录
 */
int vfs_mknod(const char *path, mode_t mode, dev_t dev, const struct path *pwd);