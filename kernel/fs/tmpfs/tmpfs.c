/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <fs/vfs.h>
#include <heap.h>
#include <time.h>
#include <dynarr.h>
#include <bitmap.h>
#include <initcall.h>
#include <klibc.h>
#include <minmax.h>

#define TMPFS_MAGIC 0xad661a3b

static struct file_operations tmpfs_file_operations;
static struct inode_operations tmpfs_inode_operations;
static struct dentry_operations tmpfs_dentry_ops;

// 超极块私有数据
struct tmpfs_sb_info {
    dynarr_t *inode_bitmap;     // 用于分配 inode 号
    ino_t max_inodes;        // 最大允许的 inode 数量，0 表示无限制
    atomic_uint inode_count;    // 当前已分配的 inode 数量
    spinlock_t inode_lock;      // 保护位图操作的锁
    uint64_t max_pages; // 能用的最大页，0 表示无限制
    atomic_uint_least64_t used_pages;   // 当前已分配页数
    spinlock_t used_lock;   // 保护 used_pages 检查更新
};

// inode 私有数据
struct tmpfs_inode {
    dynarr_t *pages;    // 文件数据页指针数组
    char *symlink_target;   // 符号链接目标字符串
    uint64_t data_size; // 文件实际数据大小
    struct list_head dir_list;  // 目录项链表头（仅目录使用）
    spinlock_t dir_lock;
};

// 目录项
struct tmpfs_dir_entry {
    char *name;
    uint32_t name_len;
    ino_t ino;
    struct list_head list;
};

// 检查并预占新页配额
static bool tmpfs_alloc_pages(struct tmpfs_sb_info *sbi, uint64_t count) {
    if (!sbi->max_pages)
        return true;

    spin_lock(&sbi->used_lock);
    uint64_t used = atomic_load(&sbi->used_pages);

    if (used + count > sbi->max_pages) {
        spin_unlock(&sbi->used_lock);
        
        return false;
    }

    atomic_fetch_add(&sbi->used_pages, count);
    spin_unlock(&sbi->used_lock);
    return true;
}

// 归还页面使用计数
static void tmpfs_free_pages(struct tmpfs_sb_info *sbi, uint64_t count) {
    if (count)
        atomic_fetch_sub(&sbi->used_pages, count);
}

// 初始化 inode 的分配器
static bool tmpfs_ino_bitmap_init(struct tmpfs_sb_info *sbi) {
    sbi->inode_bitmap = dynarr_bitmap_create(sbi->max_inodes);
    if (!sbi->inode_bitmap)
        return false;

    // 预置第 0 个元素，保留 inode 0 不用
    uint32_t dummy;
    if (!dynarr_bitmap_alloc(sbi->inode_bitmap, 1, &dummy))
        return false;  

    atomic_init(&sbi->inode_count, 0);
    spinlock_init(&sbi->inode_lock);

    return true;
}

// 分配 inode 号
static bool tmpfs_alloc_ino(struct tmpfs_sb_info *sbi, ino_t *out_ino) {
    spin_lock(&sbi->inode_lock);

    uint32_t found;
    if (!dynarr_bitmap_alloc(sbi->inode_bitmap, 1, &found)) {
        spin_unlock(&sbi->inode_lock);

        return false;
    }

    atomic_fetch_add(&sbi->inode_count, 1);
    
    spin_unlock(&sbi->inode_lock);

    *out_ino = (ino_t)found;
    return true;
}

// 释放 inode 号
static void tmpfs_free_ino(struct tmpfs_sb_info *sbi, ino_t ino) {
    if (!ino)
        return;

    spin_lock(&sbi->inode_lock);
    
    dynarr_bitmap_free(sbi->inode_bitmap, (uint32_t)ino);

    atomic_fetch_sub(&sbi->inode_count, 1);

    spin_unlock(&sbi->inode_lock);
}

// 销毁 inode 号位图分配器
static void tmpfs_ino_bitmap_destroy(struct tmpfs_sb_info *sbi) {
    dynarr_destroy(sbi->inode_bitmap);
}

/**
 * 初始化 inode 字段
 *
 * @param inode 已分配的 inode
 * @param sb 所属超级块
 * @param mode 文件模式
 * 
 * 调用者需要自己设置ops和fop
 */
static int tmpfs_inode_init(
    struct inode *inode,
    struct super_block *sb,
    mode_t mode
) {
    struct timespec now;
    struct tmpfs_sb_info *sbi = sb->private;
    uid_t uid;
    gid_t gid;
    ino_t new_ino;

    // 获取当前任务的用户和组
    task_get_current_ugid(&uid, &gid);

    // 分配 inode 号
    if (!tmpfs_alloc_ino(sbi, &new_ino))
        return -ENOSPC;

    time_get(&now);

    inode->ino = new_ino;
    inode->rdev = 0;
    inode->mode = mode;
    inode->uid = uid;
    inode->gid = gid;
    inode->size = 0;
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->blocks = 0;
    inode->ops = NULL;
    inode->fop = NULL;
    inode->sb = sb;

    spinlock_init(&inode->lock);
    atomic_init(&inode->count, 1);
    inode->nlink = 0;
    INIT_LIST_HEAD(&inode->lru);
    INIT_HLIST_NODE(&inode->hash);
    INIT_LIST_HEAD(&inode->sb_list);

    inode->key.sb = sb;
    inode->key.ino = new_ino;

    // 插入 inode 哈希表
    vfs_icache_add(inode);

    return 0;
}

// 分配 struct inode 及其私有数据
static struct inode *tmpfs_alloc_inode(struct super_block *sb) {
    struct inode *inode = kheap_alloc(sizeof(struct inode));
    if (!inode)
        return NULL;

    struct tmpfs_inode *ti = kheap_alloc(sizeof(struct tmpfs_inode));
    if (!ti) {
        kheap_free(inode);
        return NULL;
    }

    ti->pages = NULL;
    ti->symlink_target = NULL;
    ti->data_size = 0;
    INIT_LIST_HEAD(&ti->dir_list);
    spinlock_init(&ti->dir_lock);    
    inode->private = ti;

    return inode;
}

// 销毁 inode 结构体
static void tmpfs_destroy_inode(struct inode *inode) {
    kheap_free(inode->private);  
    kheap_free(inode);
}

// 释放 inode
static void tmpfs_evict_inode(struct inode *inode) {
    struct tmpfs_inode *ti = inode->private;
    struct tmpfs_sb_info *sbi = inode->sb->private;
    uint64_t page_count = 0;

    // 释放文件数据页
    if (ti->pages) {
        page_count = dynarr_count(ti->pages);
        for (uint64_t i = 0; i < page_count; i++) {
            void *page = dynarr_get(ti->pages, i);
            if (page)
                kheap_free(page);
        }

        dynarr_destroy(ti->pages);
        ti->pages = NULL;
    }

    // 释放目录项（仅目录）
    if (S_ISDIR(inode->mode)) {
        struct tmpfs_dir_entry *de, *tmp;
        spin_lock(&ti->dir_lock);

        list_for_each_entry_safe(de, tmp, &ti->dir_list, list) {
            list_del(&de->list);
            kheap_free(de->name);
            kheap_free(de);
        }

        spin_unlock(&ti->dir_lock);
    }

    // 释放符号链接的字符串
    if (ti->symlink_target) kheap_free(ti->symlink_target);

    // 归还在超级块中占用的页数
    tmpfs_free_pages(sbi, page_count);

    // 回收 inode 号
    tmpfs_free_ino(sbi, inode->ino);
}

// 释放超级块及其资源
static void tmpfs_put_super(struct super_block *sb) {
    struct tmpfs_sb_info *sbi = sb->private;

    // 销毁 inode 位图分配器
    tmpfs_ino_bitmap_destroy(sbi);

    // 归还设备号
    drivers_free_anon_id(sb->dev);

    // 释放超级块私有数据和超级块
    kheap_free(sbi);
    kheap_free(sb);
}

// tmpfs 没有存储，不需要同步，直接返回成功
static int tmpfs_sync_fs(struct super_block *sb, bool wait) {
    return 0;
}

// 返回文件系统统计信息
static int tmpfs_statfs(struct dentry *dentry, struct statfs *buf) {
    struct super_block *sb = dentry->inode->sb;
    struct tmpfs_sb_info *sbi = sb->private;
    uint64_t used = atomic_load(&sbi->used_pages);

    buf->f_bsize = PAGE_SIZE;
    buf->f_blocks = sbi->max_pages;
    buf->f_bfree = sbi->max_pages - used;
    buf->f_bavail = buf->f_bfree;
    buf->f_files = 0;   // inode 总数无限制
    buf->f_ffree = 0;   // 空闲 inode 数无限制
    buf->f_namemax = 255;

    return 0;
}

// 超极块操作表
static struct super_operations tmpfs_super_operations = {
    .alloc_inode = tmpfs_alloc_inode,
    .destroy_inode = tmpfs_destroy_inode,
    .evict_inode = tmpfs_evict_inode,
    .put_super = tmpfs_put_super,
    .sync_fs = tmpfs_sync_fs,
    .statfs = tmpfs_statfs,
};

/**
 * 根据 inode 号从超级块的已加载 inode 链表中获取 inode
 *
 * @param sb 超级块
 * @param ino inode 号
 *
 * @return inode 指针（引用计数已加）
 */
static struct inode *tmpfs_iget(struct super_block *sb, ino_t ino) {
    struct inode *inode;

    // 优先从 inode 哈希表查找
    inode = vfs_icache_find(sb, ino);
    if (inode)
        return inode;

    // 哈希表未命中，回退到链表遍历
    spin_lock(&sb->lock);

    list_for_each_entry(inode, &sb->inodes, sb_list) {
        if (inode->ino == ino) {
            atomic_fetch_add(&inode->count, 1);

            spin_unlock(&sb->lock);

            return inode;
        }
    }

    spin_unlock(&sb->lock);
    return ERR_PTR(-ENOENT);
}

// 在目录链表中按名称查找条目(调用方需要有私有 inode 的 dir 锁)
static struct tmpfs_dir_entry *tmpfs_dir_lookup(
    struct tmpfs_inode *ti,
    const char *name,
    uint32_t len
) {
    struct tmpfs_dir_entry *de;

    // 遍历目录链表，匹配名称长度和内容
    list_for_each_entry(de, &ti->dir_list, list) {
        if (de->name_len == len && !memcmp(de->name, name, len))
            return de;
    }
    return NULL;
}

// 在目录链表尾部添加条目(调用方需要有私有 inode 的 dir 锁)
static int tmpfs_dir_add(
    struct tmpfs_inode *ti,
    const char *name,
    uint32_t len,
    ino_t ino
) {
    struct tmpfs_dir_entry *de;

    // 分配目录项结构体
    de = kheap_alloc(sizeof(*de));
    if (!de) return -ENOMEM;

    // 分配名字字符串
    de->name = kheap_alloc(len + 1);
    if (!de->name) {
        kheap_free(de);
        return -ENOMEM;
    }

    // 复制名字，初始化字段，挂入链表尾部
    memcpy(de->name, name, len);
    de->name[len] = '\0';
    de->name_len = len;
    de->ino = ino;
    INIT_LIST_HEAD(&de->list);
    list_add_tail(&de->list, &ti->dir_list);

    return 0;
}

// 从目录链表中删除条目并释放其占用的内存(调用方需要有私有 inode 的 dir 锁)
static void tmpfs_dir_remove(struct tmpfs_dir_entry *de) {
    list_del(&de->list);
    kheap_free(de->name);
    kheap_free(de);
}

/**
 * 在目录中按名称查找目录项
 *
 * @param dir 父目录 inode
 * @param dentry 待查找的 dentry（name 已填充）
 *
 * @return 传入的 dentry
 */
static struct dentry *tmpfs_lookup(
    struct inode *dir,
    struct dentry *dentry
) {
    struct tmpfs_inode *tdir = dir->private;
    struct super_block *sb = dir->sb;
    struct tmpfs_dir_entry *de;
    struct inode *inode = NULL;

    spin_lock(&tdir->dir_lock);

    // 调用辅助函数在目录链表中查找匹配项
    de = tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len);
    if (de)
        inode = tmpfs_iget(sb, de->ino);

    spin_unlock(&tdir->dir_lock);

    // 命中则关联 inode，清除可能的负缓存标志
    if (inode) {
        dentry->inode = inode;
        dentry->flags &= ~DCACHE_NEGATIVE;
    }

    return dentry;
}

/**
 * 在目录中创建普通文件
 *
 * @param dir 父目录 inode
 * @param dentry 新文件的 dentry（name已填充）
 * @param mode 文件权限
 */
static int tmpfs_create(
    struct inode *dir,
    struct dentry *dentry,
    mode_t mode
) {
    struct inode *inode;
    struct tmpfs_inode *tdir = dir->private;
    int err;

    // 分配并初始化新 inode，强制设置文件类型为普通文件
    inode = tmpfs_alloc_inode(dir->sb);
    if (!inode)
        return -ENOMEM;

    err = tmpfs_inode_init(inode, dir->sb, (mode & ~S_IFMT) | S_IFREG);
    if (err) {
        tmpfs_destroy_inode(inode);
        return err;
    }

    // 设置文件操作表及初始硬链接数
    inode->fop = &tmpfs_file_operations;
    inode->nlink = 1;

    // 设置操作表
    inode->ops = &tmpfs_inode_operations;

    // 加入超级块的 inode 链表
    spin_lock(&dir->sb->lock);
    list_add_tail(&inode->sb_list, &dir->sb->inodes);
    spin_unlock(&dir->sb->lock);

    // 在父目录的目录项链表中添加新条目
    spin_lock(&tdir->dir_lock);

    if (tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len)) {
        spin_unlock(&tdir->dir_lock);

        err = -EEXIST;
        goto fail;
    }

    err = tmpfs_dir_add(tdir, dentry->name.name, dentry->name.len, inode->ino);

    spin_unlock(&tdir->dir_lock);
    if (err)
        goto fail;

    // 将新创建的 inode 关联到 dentry，清除负缓存标志
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    return 0;

fail:
    spin_lock(&dir->sb->lock);
    list_del(&inode->sb_list);
    spin_unlock(&dir->sb->lock);
    tmpfs_evict_inode(inode);
    tmpfs_destroy_inode(inode);
    return err;
}

/**
 * 创建软链接
 *
 * @param dir 父目录 inode
 * @param dentry 新软链接的 dentry（name 已设置）
 * @param target 软链接指向的目标路径字符串
 */
static int tmpfs_symlink(
    struct inode *dir,
    struct dentry *dentry,
    const char *target
) {
    struct inode *inode;
    struct tmpfs_inode *tdir = dir->private;
    struct tmpfs_inode *ti;
    int err;
    size_t target_len;

    // 分配并初始化新 inode
    inode = tmpfs_alloc_inode(dir->sb);
    if (!inode)
        return -ENOMEM;

    // 软链接的权限固定为 0777，类型位设为 S_IFLNK
    err = tmpfs_inode_init(inode, dir->sb, S_IFLNK | 0777);
    if (err) {
        tmpfs_destroy_inode(inode);
        return err;
    }

    // 设置操作表及初始硬链接数
    inode->ops = &tmpfs_inode_operations;
    inode->nlink = 1;

    ti = inode->private;
    target_len = strlen(target);

    // 限制符号链接长度，避免过大
    if (target_len >= PATH_MAX) {
        tmpfs_destroy_inode(inode);
        return -ENAMETOOLONG;
    }

    // 分配缓冲区存储目标路径
    ti->symlink_target = kheap_alloc(target_len + 1);
    if (!ti->symlink_target) {
        tmpfs_destroy_inode(inode);
        return -ENOMEM;
    }

    // 复制目标路径（包括结尾 '\0'）
    memcpy(ti->symlink_target, target, target_len + 1);

    ti->data_size = target_len;
    inode->size = target_len;

    // 更新块计数，用于 getattr
    inode->blocks = (target_len + 511) >> 9;

    // 加入超级块 inode 链表
    spin_lock(&dir->sb->lock);
    list_add_tail(&inode->sb_list, &dir->sb->inodes);
    spin_unlock(&dir->sb->lock);

    // 在父目录的目录项链表中添加新条目
    spin_lock(&tdir->dir_lock);

    if (tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len)) {
        spin_unlock(&tdir->dir_lock);

        err = -EEXIST;
        goto fail;
    }

    err = tmpfs_dir_add(tdir, dentry->name.name, dentry->name.len, inode->ino);
    spin_unlock(&tdir->dir_lock);

    if (err)
        goto fail;

    // 将新创建的 inode 关联到 dentry，清除负缓存标志
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    return 0;

fail:
    spin_lock(&dir->sb->lock);
    list_del(&inode->sb_list);
    spin_unlock(&dir->sb->lock);
    tmpfs_evict_inode(inode);
    tmpfs_destroy_inode(inode);
    return err;
}

/**
 * 创建硬链接
 *
 * @param old_dentry 已存在文件的 dentry
 * @param dir 目标目录 inode
 * @param new_dentry 新硬链接的 dentry（name 已设置）
 */
static int tmpfs_link(
    struct dentry *old_dentry,
    struct inode *dir,
    struct dentry *new_dentry
) {
    struct inode *inode = old_dentry->inode;
    struct tmpfs_inode *tdir = dir->private;
    int err;

    spin_lock(&tdir->dir_lock);
    err = tmpfs_dir_add(tdir, new_dentry->name.name, new_dentry->name.len, inode->ino);
    spin_unlock(&tdir->dir_lock);
    if (err)
        return err;

    spin_lock(&inode->lock);
    inode->nlink++;
    spin_unlock(&inode->lock);

    new_dentry->inode = inode;
    atomic_fetch_add(&inode->count, 1);
    new_dentry->flags &= ~DCACHE_NEGATIVE;
    return 0;
}

/**
 * 删除文件
 *
 * @param dir 父目录 inode
 * @param dentry 待删除文件的 dentry
 */
static int tmpfs_unlink(
    struct inode *dir,
    struct dentry *dentry
) {
    struct tmpfs_inode *tdir = dir->private;
    struct tmpfs_dir_entry *de;

    spin_lock(&tdir->dir_lock);

    de = tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len);
    if (!de) {
        spin_unlock(&tdir->dir_lock);

        return -ENOENT;
    }

    tmpfs_dir_remove(de);
    spin_unlock(&tdir->dir_lock);

    spin_lock(&dentry->inode->lock);
    dentry->inode->nlink--;
    spin_unlock(&dentry->inode->lock);

    dentry->inode = NULL;
    return 0;
}

/**
 * 创建目录
 *
 * @param dir 父目录 inode
 * @param dentry 新目录的 dentry（name 已设置）
 * @param mode 目录权限
 */
static int tmpfs_mkdir(
    struct inode *dir,
    struct dentry *dentry,
    mode_t mode
) {
    struct inode *inode;
    struct tmpfs_inode *tdir = dir->private;
    struct tmpfs_inode *ti;
    int err;

    // 分配并初始化新 inode，强制设置文件类型为目录
    inode = tmpfs_alloc_inode(dir->sb);
    if (!inode)
        return -ENOMEM;

    err = tmpfs_inode_init(inode, dir->sb, (mode & ~S_IFMT) | S_IFDIR);
    if (err) {
        tmpfs_destroy_inode(inode);
        return err;
    }

    // 设置目录操作表及初始硬链接数
    inode->ops = &tmpfs_inode_operations;
    inode->nlink = 2;

    // 加入超级块的 inode 链表
    spin_lock(&dir->sb->lock);
    list_add_tail(&inode->sb_list, &dir->sb->inodes);
    spin_unlock(&dir->sb->lock);

    // 在父目录中添加新条目
    spin_lock(&tdir->dir_lock);

    if (tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len)) {
        spin_unlock(&tdir->dir_lock);

        err = -EEXIST;
        goto fail;
    }

    err = tmpfs_dir_add(tdir, dentry->name.name, dentry->name.len, inode->ino);

    spin_unlock(&tdir->dir_lock);

    if (err)
        goto fail;

    ti = inode->private;

    // 添加 "." 条目，指向自己
    spin_lock(&ti->dir_lock);

    err = tmpfs_dir_add(ti, ".", 1, inode->ino);
    if (err) {
        spin_unlock(&ti->dir_lock);

        goto rollback_parent;
    }

    // 添加 ".." 条目，指向父目录
    err = tmpfs_dir_add(ti, "..", 2, dir->ino);
    if (err) {
        struct tmpfs_dir_entry *dot = tmpfs_dir_lookup(ti, ".", 1);
        if (dot)
            tmpfs_dir_remove(dot);
        spin_unlock(&ti->dir_lock);
        goto rollback_parent;
    }

    spin_unlock(&ti->dir_lock);

    // 增加父目录的硬链接引用
    spin_lock(&dir->lock);
    dir->nlink++;
    spin_unlock(&dir->lock);

    // 将新创建的 inode 关联到 dentry，清除负缓存标志
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    return 0;

rollback_parent:
    spin_lock(&tdir->dir_lock);

    {
        struct tmpfs_dir_entry *parent_de = tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len);
        if (parent_de)
            tmpfs_dir_remove(parent_de);
    }

    spin_unlock(&tdir->dir_lock);
fail:
    spin_lock(&dir->sb->lock);
    list_del(&inode->sb_list);
    spin_unlock(&dir->sb->lock);
    tmpfs_evict_inode(inode);
    tmpfs_destroy_inode(inode);
    return err;
}

/**
 * 删除空目录
 *
 * @param dir 父目录 inode
 * @param dentry 待删除目录的 dentry
 */
static int tmpfs_rmdir(
    struct inode *dir,
    struct dentry *dentry
) {
    struct inode *child = dentry->inode;
    struct tmpfs_inode *ti_child = child->private;
    struct tmpfs_inode *tdir = dir->private;
    struct tmpfs_dir_entry *de;
    struct tmpfs_dir_entry *parent_de;
    bool empty = true;

    // 检查目标目录是否为空（忽略 . 和 ..）
    spin_lock(&ti_child->dir_lock);

    list_for_each_entry(de, &ti_child->dir_list, list) {
        if (de->name_len == 1 && de->name[0] == '.')
            continue;
        if (de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')
            continue;
        empty = false;
        break;
    }

    spin_unlock(&ti_child->dir_lock);

    if (!empty)
        return -ENOTEMPTY;

    // 在父目录中查找并移除目标目录的条目
    spin_lock(&tdir->dir_lock);
    parent_de = tmpfs_dir_lookup(tdir, dentry->name.name, dentry->name.len);
    if (!parent_de) {
        spin_unlock(&tdir->dir_lock);

        return -ENOENT;
    }
    
    tmpfs_dir_remove(parent_de);

    spin_unlock(&tdir->dir_lock);

    // 减少目标目录硬链接引用
    spin_lock(&child->lock);
    child->nlink--;
    spin_unlock(&child->lock);

    // 减少父目录的硬链接引用（目标目录的 ".." 不再指向它）
    spin_lock(&dir->lock);
    dir->nlink--;
    spin_unlock(&dir->lock);

    // 解除 dentry 与 inode 的关联
    dentry->inode = NULL;
    return 0;
}

/**
 * 重命名或移动文件/目录
 *
 * @param old_dir 源目录 inode
 * @param old_dentry 源目录项
 * @param new_dir 目标目录 inode
 * @param new_dentry 目标目录项
 */
static int tmpfs_rename(
    struct inode *old_dir,
    struct dentry *old_dentry,
    struct inode *new_dir,
    struct dentry *new_dentry
) {
    struct inode *old_inode = old_dentry->inode;
    struct inode *new_inode = new_dentry->inode;
    struct tmpfs_inode *ti_old_dir = old_dir->private;
    struct tmpfs_inode *ti_new_dir = new_dir->private;
    struct tmpfs_dir_entry *old_de, *new_de;
    bool same_dir = (old_dir == new_dir);
    int err = 0;

    if (!old_inode)
        return -ENOENT;

    // 若目标存在且是目录，但源不是目录，不能覆盖
    if (new_inode && S_ISDIR(new_inode->mode) && !S_ISDIR(old_inode->mode))
        return -EISDIR;

    // 若目标是目录且非空，不能覆盖
    if (new_inode && S_ISDIR(new_inode->mode) && new_inode != old_inode) {
        struct tmpfs_inode *ti_new = new_inode->private;

        bool empty = true;
        spin_lock(&ti_new->dir_lock);

        list_for_each_entry(new_de, &ti_new->dir_list, list) {
            if (new_de->name_len == 1 && new_de->name[0] == '.')
                continue;
            if (new_de->name_len == 2 && new_de->name[0] == '.' && new_de->name[1] == '.')
                continue;
            empty = false;
            break;
        }

        spin_unlock(&ti_new->dir_lock);

        if (!empty)
            return -ENOTEMPTY;
    }

    // 目标不能是源的后代
    if (S_ISDIR(old_inode->mode) && !same_dir) {
        struct dentry *p = new_dentry->parent;
        while (p) {
            if (p->inode == old_inode)
                return -EINVAL;

            p = p->parent;
        }
    }

    if (same_dir) {
        // 对于同一个目录，只需要获取一次锁
        spin_lock(&ti_old_dir->dir_lock);
    } else {
        // 按内存地址获取锁，防止死锁
        if ((uintptr_t)ti_old_dir < (uintptr_t)ti_new_dir) {
            spin_lock(&ti_old_dir->dir_lock);
            spin_lock(&ti_new_dir->dir_lock);
        } else {
            spin_lock(&ti_new_dir->dir_lock);
            spin_lock(&ti_old_dir->dir_lock);
        }
    }

    // 在源目录中查找旧条目
    old_de = tmpfs_dir_lookup(ti_old_dir, old_dentry->name.name, old_dentry->name.len);
    if (!old_de) {
        err = -ENOENT;
        goto out;
    }

    // 如果目标已存在，删除目标条目
    new_de = NULL;
    if (new_inode) {
        new_de = tmpfs_dir_lookup(ti_new_dir, new_dentry->name.name, new_dentry->name.len);
        if (new_de) {
            // 如果是同一个条目，直接成功
            if (same_dir && old_de == new_de) {
                err = 0;
                goto out;
            }

            tmpfs_dir_remove(new_de);

            spin_lock(&new_inode->lock);
            new_inode->nlink--;
            spin_unlock(&new_inode->lock);
        }
    }

    // 执行移动/重命名
    if (same_dir) {
        // 对于同目录，先分配新名字，避免悬空指针
        char *new_name = kheap_alloc(new_dentry->name.len + 1);
        if (!new_name) {
            err = -ENOMEM;
            goto out;
        }

        memcpy(new_name, new_dentry->name.name, new_dentry->name.len);
        new_name[new_dentry->name.len] = '\0';

        kheap_free(old_de->name);
        old_de->name = new_name;
        old_de->name_len = new_dentry->name.len;
    } else {
        // 对于跨目录，在目标添加目录，在源目录删除
        err = tmpfs_dir_add(ti_new_dir, new_dentry->name.name, new_dentry->name.len, old_inode->ino);
        if (err)
            goto out;

        tmpfs_dir_remove(old_de);

        // 如果是目录，更新 .. 条目，并调整 nlink
        if (S_ISDIR(old_inode->mode)) {
            struct tmpfs_inode *ti_old = old_inode->private;

            spin_lock(&ti_old->dir_lock);

            struct tmpfs_dir_entry *dotdot = tmpfs_dir_lookup(ti_old, "..", 2);
            if (dotdot)
                dotdot->ino = new_dir->ino;

            spin_unlock(&ti_old->dir_lock);

            spin_lock(&old_dir->lock);
            old_dir->nlink--;
            spin_unlock(&old_dir->lock);

            spin_lock(&new_dir->lock);
            new_dir->nlink++;
            spin_unlock(&new_dir->lock);
        }
    }

    // 更新 dentry 关联
    old_dentry->inode = NULL;
    new_dentry->inode = old_inode;
    new_dentry->flags &= ~DCACHE_NEGATIVE;

out:
    if (same_dir) {

        spin_unlock(&ti_old_dir->dir_lock);
    } else {
        spin_unlock(&ti_old_dir->dir_lock);
        spin_unlock(&ti_new_dir->dir_lock);
    }

    return err;
}

/**
 * 获取文件属性
 *
 * @param path 文件路径
 * @param stat 返回文件状态信息
 */
static int tmpfs_getattr(
    struct path *path,
    struct kstat *stat
) {
    struct inode *inode = path->dentry->inode;
    struct super_block *sb = inode->sb;

    stat->st_dev = sb->dev;
    stat->st_ino = inode->ino;
    stat->st_mode = inode->mode;
    stat->st_nlink = inode->nlink;
    stat->st_uid = inode->uid;
    stat->st_gid = inode->gid;
    stat->st_rdev = inode->rdev;;
    stat->st_size = inode->size;
    stat->st_atim = inode->atime;
    stat->st_mtim = inode->mtime;
    stat->st_ctim = inode->ctime;
    stat->st_blksize = sb->block_size;
    stat->st_blocks = inode->blocks;

    return 0;
}

/**
 * 设置文件属性
 *
 * @param dentry 目标文件目录项
 * @param attr 要设置的属性
 */
static int tmpfs_setattr(
    struct dentry *dentry,
    struct iattr *attr
) {
    struct inode *inode = dentry->inode;
    struct tmpfs_inode *ti = inode->private;
    struct tmpfs_sb_info *sbi = inode->sb->private;

    spin_lock(&inode->lock);

    // 处理文件大小变化
    if (attr->ia_valid & ATTR_SIZE) {
        uint64_t new_size = attr->ia_size;
        uint64_t old_pages = (inode->size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t new_pages = (new_size + PAGE_SIZE - 1) / PAGE_SIZE;

        if (new_size < inode->size) {
            // 截断，释放超出新尾部的页面
            if (ti->pages && new_pages < dynarr_count(ti->pages)) {
                for (uint64_t i = new_pages; i < dynarr_count(ti->pages); i++) {
                    void *page = dynarr_get(ti->pages, i);
                    if (page) {
                        kheap_free(page);
                        dynarr_set(ti->pages, i, NULL);
                    }
                }
            }

            // 归还已释放页面的使用计数
            tmpfs_free_pages(sbi, old_pages - new_pages);
        } else if (new_size > inode->size && new_pages > old_pages) {
            // 先检查容量配额
            if (!tmpfs_alloc_pages(sbi, new_pages - old_pages)) {
                spin_unlock(&inode->lock);

                return -ENOSPC;
            }

            // 为空洞或新增部分分配并清零页面
            if (!ti->pages) {
                ti->pages = dynarr_create(sizeof(void *), 0);
                if (!ti->pages) {
                    tmpfs_free_pages(sbi, new_pages - old_pages);

                    spin_unlock(&inode->lock);

                    return -ENOMEM;
                }
            }

            for (uint64_t i = old_pages; i < new_pages; i++) {
                void *page = kheap_alloc(PAGE_SIZE);
                if (!page) {
                    // 释放已分配的新页
                    for (uint64_t j = old_pages; j < i; j++) {
                        void *p = dynarr_get(ti->pages, j);
                        if (p) kheap_free(p);
                    }

                    dynarr_destroy(ti->pages);
                    ti->pages = NULL;
                    tmpfs_free_pages(sbi, new_pages - old_pages);

                    spin_unlock(&inode->lock);

                    return -ENOMEM;
                }

                memset(page, 0, PAGE_SIZE);
                if (!dynarr_append(ti->pages, &page)) {
                    kheap_free(page);

                    // 释放之前分配的页
                    for (uint64_t j = old_pages; j < i; j++) {
                        void *p = dynarr_get(ti->pages, j);
                        if (p) kheap_free(p);
                    }

                    dynarr_destroy(ti->pages);
                    ti->pages = NULL;
                    tmpfs_free_pages(sbi, new_pages - old_pages);

                    spin_unlock(&inode->lock);

                    return -ENOMEM;
                }
            }
        }

        // 更新文件和私有数据中的大小字段
        inode->size = new_size;
        ti->data_size = new_size;
        inode->blocks = (new_size + 511) >> 9;
    }

    if (attr->ia_valid & ATTR_MODE)
        inode->mode = (inode->mode & S_IFMT) | (attr->ia_mode & ~S_IFMT);

    if (attr->ia_valid & ATTR_UID)
        inode->uid = attr->ia_uid;

    if (attr->ia_valid & ATTR_GID)
        inode->gid = attr->ia_gid;

    if (attr->ia_valid & ATTR_ATIME)
        inode->atime = attr->ia_atime;

    if (attr->ia_valid & ATTR_MTIME)
        inode->mtime = attr->ia_mtime;

    // 更新修改时间
    time_get(&inode->ctime);
    spin_unlock(&inode->lock);
    return 0;
}

/**
 * 读取符号链接指向的目标路径
 *
 * @param inode 符号链接 inode
 * @param buf 缓冲区，用于接收目标路径
 * @param bufsiz 缓冲区大小
 *
 * @return 实际写入的字节数
 */
static ssize_t tmpfs_readlink(
    struct inode *inode,
    char *buf,
    size_t bufsiz
) {
    struct tmpfs_inode *ti = inode->private;
    size_t copy_len;

    // 符号链接还没有写入数据
    if (!ti->symlink_target)
        return -EINVAL;

    // 目标路径长度
    copy_len = ti->data_size;
    copy_len = min(copy_len, bufsiz);

    // 复制目标路径到用户缓冲区
    memcpy(buf, ti->symlink_target, copy_len);
    return copy_len;
}

/**
 * 创建设备节点
 *
 * @param dir 父目录 inode
 * @param dentry 新设备节点的 dentry（name已填充）
 * @param mode 文件类型和权限
 * @param dev 设备号
 */
static int tmpfs_mknod(struct inode *dir, struct dentry *dentry, mode_t mode, dev_t dev) {
    struct inode *inode;
    struct tmpfs_inode *ti;
    int err;

    // 分配并初始化新 inode
    inode = tmpfs_alloc_inode(dir->sb);
    if (!inode)
        return -ENOMEM;

    err = tmpfs_inode_init(inode, dir->sb, mode);
    if (err) {
        tmpfs_destroy_inode(inode);
        return err;
    }

    // 设置设备号
    inode->rdev = dev;

    // 设备节点使用默认设备操作表（后面会动态绑定真正驱动）
    inode->fop = &dev_fops;

    // 加入超级块的 inode 链表
    spin_lock(&dir->sb->lock);
    list_add_tail(&inode->sb_list, &dir->sb->inodes);
    spin_unlock(&dir->sb->lock);

    // 在父目录的目录项链表中添加新条目
    spin_lock(&((struct tmpfs_inode *)dir->private)->dir_lock);
    err = tmpfs_dir_add(dir->private, dentry->name.name, dentry->name.len, inode->ino);

    spin_unlock(&((struct tmpfs_inode *)dir->private)->dir_lock);

    if (err) {
        spin_lock(&dir->sb->lock);
        list_del(&inode->sb_list);
        spin_unlock(&dir->sb->lock);
        tmpfs_evict_inode(inode);
        tmpfs_destroy_inode(inode);
        return err;
    }

    // 将新创建的 inode 关联到 dentry，清除负缓存标志
    dentry->inode = inode;
    dentry->flags &= ~DCACHE_NEGATIVE;
    return 0;
}

static struct inode_operations tmpfs_inode_operations = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .symlink = tmpfs_symlink,
    .link = tmpfs_link,
    .unlink = tmpfs_unlink,
    .mkdir = tmpfs_mkdir,
    .rmdir = tmpfs_rmdir,
    .rename = tmpfs_rename,
    .setattr = tmpfs_setattr,
    .getattr = tmpfs_getattr,
    .readlink = tmpfs_readlink,
    .mknod = tmpfs_mknod,
};

/**
 * 解析挂载参数中的 size 选项
 *
 * @param data 挂载参数字符串，格式形如 "size=16M"，可为 NULL
 *
 * @return 最大页数，0 表示无限制
 */
static uint64_t tmpfs_parse_size(const char *data) {
    const char *p;
    uint64_t size_bytes = 0;
    uint64_t pages;

    if (!data)
        return 0;

    // 查找 "size=" 子串
    p = strstr(data, "size=");
    if (!p)
        return 0;

    p += 5; // 跳过 "size="

    // 解析数字部分
    while (*p >= '0' && *p <= '9') {
        size_bytes = size_bytes * 10 + (*p - '0');
        p++;
    }

    // 处理后缀
    switch (*p) {
        case 'G':
        case 'g':
            size_bytes *= 1024;
            // fall through
        case 'M':
        case 'm':
            size_bytes *= 1024;
            // fall through
        case 'K':
        case 'k':
            size_bytes *= 1024;
            break;
        default:
            break;
    }

    if (size_bytes == 0)
        return 0;

    // 转换为页数（向上取整）
    pages = (size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    return pages;
}

// 挂载 tmpfs
static struct dentry *tmpfs_mount(
    struct file_system_type *fst,
    mount_flags_t flags,
    const char *dev_name,
    void *data
) {
    struct super_block *sb;
    struct tmpfs_sb_info *sbi;
    struct inode *root_inode;
    struct dentry *root_dentry;
    int err;

    // 分配并初始化超级块私有数据
    sbi = kheap_alloc(sizeof(*sbi));
    if (!sbi)
        return ERR_PTR(-ENOMEM);

    if (!tmpfs_ino_bitmap_init(sbi)) {
        err = -ENOMEM;
        goto out_free_sbi;
    }

    sbi->max_inodes = 0;
    atomic_init(&sbi->inode_count, 0);
    spinlock_init(&sbi->inode_lock);

    sbi->max_pages = tmpfs_parse_size(data);
    atomic_init(&sbi->used_pages, 0);
    spinlock_init(&sbi->used_lock);

    // 分配超级块
    sb = kheap_alloc(sizeof(*sb));
    if (!sb) {
        err = -ENOMEM;
        goto out_free_bitmap;
    }

    // 获取设备号
    if (drivers_get_anon_id(&sb->dev)) {
        err = -ENOSPC;
        goto out_free_sb;
    }

    sb->block_size = PAGE_SIZE;
    sb->block_size_bits = PAGE_SHIFT;
    sb->magic = TMPFS_MAGIC;
    sb->ops = &tmpfs_super_operations;
    sb->root = NULL;
    INIT_LIST_HEAD(&sb->inodes);
    atomic_init(&sb->count, 1);
    spinlock_init(&sb->lock);
    mutex_init(&sb->umount_lock);
    INIT_LIST_HEAD(&sb->sb_list);
    sb->type = fst;
    sb->private = sbi;

    // 创建根 inode
    root_inode = tmpfs_alloc_inode(sb);
    if (!root_inode) {
        err = -ENOMEM;
        goto out_free_dev;
    }

    err = tmpfs_inode_init(root_inode, sb, S_IFDIR | 0755);
    if (err) {
        tmpfs_destroy_inode(root_inode);
        goto out_free_dev;
    }

    // 设置目录操作表及硬链接数
    root_inode->ops = &tmpfs_inode_operations;
    root_inode->nlink = 2;  // 根目录的 . 和 .. 都指向自己

    // 加入超级块 inode 链表
    spin_lock(&sb->lock);
    list_add_tail(&root_inode->sb_list, &sb->inodes);
    spin_unlock(&sb->lock);

    // 在根目录中添加 . 和 .. 条目，都指向自己
    struct tmpfs_inode *root_ti = root_inode->private;

    spin_lock(&root_ti->dir_lock);

    err = tmpfs_dir_add(root_ti, ".", 1, root_inode->ino);
    if (err) {
        spin_unlock(&root_ti->dir_lock);

        goto out_evict_root;
    }

    err = tmpfs_dir_add(root_ti, "..", 2, root_inode->ino);
    if (err) {
        struct tmpfs_dir_entry *dot;
        dot = tmpfs_dir_lookup(root_ti, ".", 1);
        if (dot)
            tmpfs_dir_remove(dot);

        spin_unlock(&root_ti->dir_lock);

        goto out_evict_root;
    }

    spin_unlock(&root_ti->dir_lock);

    // 创建根 dentry
    root_dentry = kheap_alloc(sizeof(*root_dentry));
    if (!root_dentry) {
        err = -ENOMEM;
        goto out_evict_root;
    }

    root_dentry->parent = NULL;

    root_dentry->name.name = strdup("/");
    if (!root_dentry->name.name) {
        err = -ENOMEM;
        goto out_free_dentry;
    }

    root_dentry->name.len = 1;
    root_dentry->name.hash = vfs_full_name_hash("/", 1);

    root_dentry->inode = root_inode;
    root_dentry->ops = &tmpfs_dentry_ops;

    INIT_HLIST_NODE(&root_dentry->hash_node);
    INIT_LIST_HEAD(&root_dentry->lru);
    atomic_init(&root_dentry->count, 1);
    root_dentry->flags = DCACHE_NONE;
    spinlock_init(&root_dentry->lock);
    INIT_LIST_HEAD(&root_dentry->child);
    INIT_LIST_HEAD(&root_dentry->subdirs);

    root_dentry->key.parent = NULL;
    root_dentry->key.name = root_dentry->name;

    // 填充超级块根指针，返回根 dentry
    sb->root = root_dentry;
    return root_dentry;

out_free_dentry:
    kheap_free(root_dentry);
out_evict_root:
    spin_lock(&sb->lock);
    list_del(&root_inode->sb_list);
    spin_unlock(&sb->lock);
    tmpfs_evict_inode(root_inode);
    tmpfs_destroy_inode(root_inode);
out_free_dev:
    drivers_free_anon_id(sb->dev);
out_free_sb:
    kheap_free(sb);
out_free_bitmap:
    tmpfs_ino_bitmap_destroy(sbi);
out_free_sbi:
    kheap_free(sbi);
    return ERR_PTR(err);
}

// 卸载文件系统时释放所有资源
static void tmpfs_kill_sb(struct super_block *sb) {
    struct inode *inode, *tmp;

    // 释放超级块中所有已分配的 inode
    spin_lock(&sb->lock);
    list_for_each_entry_safe(inode, tmp, &sb->inodes, sb_list) {
        list_del_init(&inode->sb_list);
        spin_unlock(&sb->lock);

        // 确保在驱逐过程中不会被意外释放
        atomic_fetch_add(&inode->count, 1);
        tmpfs_evict_inode(inode);
        tmpfs_destroy_inode(inode);

        spin_lock(&sb->lock);
    }

    spin_unlock(&sb->lock);

    // 释放超级块及其私有数据
    tmpfs_put_super(sb);
}

void tmpfs_init(void) {
    // 注册 tmpfs 文件系统类型
    static struct file_system_type tmpfs_type = {
        .name = "tmpfs",
        .mount = tmpfs_mount,
        .kill_sb = tmpfs_kill_sb,
    };

    vfs_register_filesystem(&tmpfs_type);
}

static int tmpfs_open(struct inode *inode, struct file *file) {
    return 0;
}

static int tmpfs_release(struct inode *inode, struct file *file) {
    return 0;
}

static ssize_t tmpfs_read(
    struct file *file,
    char *buf,
    size_t count,
    off_t *pos
) {
    struct inode *inode = file->path.dentry->inode;
    struct tmpfs_inode *ti = inode->private;
    off_t offset;
    size_t remaining, done = 0;

    // 确定起始读取偏移
    offset = pos ? *pos : file->pos;

    // 实际可读字节不能超过文件大小
    if (offset >= (off_t)ti->data_size)
        return 0;

    if (offset + (off_t)count > (off_t)ti->data_size)
        count = ti->data_size - offset;

    spin_lock(&inode->lock);

    remaining = count;
    while (remaining > 0) {
        uint64_t page_idx = offset / PAGE_SIZE;
        uint64_t page_off = offset % PAGE_SIZE;
        void *page;

        // 如果页数组未分配或索引越界，剩余部分填充零
        if (!ti->pages || page_idx >= dynarr_count(ti->pages)) {
            size_t chunk = PAGE_SIZE - page_off;
            chunk = min(chunk, remaining);

            memset(buf + done, 0, chunk);
            done += chunk;
            offset += chunk;
            remaining -= chunk;
            continue;
        }

        page = dynarr_get(ti->pages, page_idx);

        // 对应页不存在，填充零
        if (!page) {
            size_t chunk = PAGE_SIZE - page_off;
            chunk = min(chunk, remaining);

            memset(buf + done, 0, chunk);
            done += chunk;
            offset += chunk;
            remaining -= chunk;
            continue;
        }

        // 拷贝页内数据
        size_t chunk = PAGE_SIZE - page_off;
        chunk = min(chunk, remaining);

        memcpy(buf + done, (char *)page + page_off, chunk);
        done += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    spin_unlock(&inode->lock);

    // 更新偏移
    if (pos) {
        *pos = offset;
    } else {
        spin_lock(&file->lock);
        file->pos = offset;
        spin_unlock(&file->lock);
    }

    // 更新访问时间
    time_get(&inode->atime);

    return done;
}

static ssize_t tmpfs_write(
    struct file *file,
    const char *buf,
    size_t count,
    off_t *pos
) {
    struct inode *inode = file->path.dentry->inode;
    struct tmpfs_inode *ti = inode->private;
    struct tmpfs_sb_info *sbi = inode->sb->private;
    off_t offset;
    uint64_t end, new_pages, old_pages;
    size_t remaining, done = 0;
    int err = 0;

    // 确定起始写入偏移
    offset = pos ? *pos : file->pos;
    end = (uint64_t)offset + count;

    // 计算写入前后所需的页数
    old_pages = (ti->data_size + PAGE_SIZE - 1) / PAGE_SIZE;
    new_pages = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    spin_lock(&inode->lock);

    // 若写入需要额外的页，先尝试预占容量配额
    if (new_pages > old_pages) {
        if (!tmpfs_alloc_pages(sbi, new_pages - old_pages)) {
            spin_unlock(&inode->lock);
            return -ENOSPC;
        }
    }

    // 确保页数组存在
    if (!ti->pages) {
        ti->pages = dynarr_create(sizeof(void *), 0);
        if (!ti->pages) {
            err = -ENOMEM;
            goto out;
        }
    }

    // 记录当前页数组长度，用于失败时回滚
    uint64_t old_dynarr_count = dynarr_count(ti->pages);

    // 为写入范围分配缺失的新页面
    for (uint64_t page_idx = old_pages; page_idx < new_pages; page_idx++) {
        void *page = kheap_alloc(PAGE_SIZE);
        if (!page) {
            err = -ENOMEM;
            goto out;
        }

        memset(page, 0, PAGE_SIZE);
        if (!dynarr_append(ti->pages, &page)) {
            kheap_free(page);
            err = -ENOMEM;
            goto out;
        }
    }

    // 逐页将用户数据拷贝到对应页面中
    remaining = count;
    while (remaining > 0) {
        uint64_t page_idx = offset / PAGE_SIZE;
        uint64_t page_off = offset % PAGE_SIZE;
        void *page;

        page = dynarr_get(ti->pages, page_idx);

        size_t chunk = PAGE_SIZE - page_off;
        chunk = min(chunk, remaining);

        memcpy((char *)page + page_off, buf + done, chunk);
        done += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    // 写入完成后扩展文件大小
    if (end > ti->data_size) {
        ti->data_size = end;
        inode->size = end;
        inode->blocks = (end + 511) >> 9;
    }

out:
    // 若中间步骤失败，回滚已分配的页面，并退还容量配额
    if (err) {
        void *page_ptr;
        while (dynarr_count(ti->pages) > old_dynarr_count) {
            if (dynarr_pop(ti->pages, &page_ptr) && page_ptr)
                kheap_free(page_ptr);
        }

        // 若数组回退到初始的空状态，那么就销毁它
        if (old_dynarr_count == 0) {
            dynarr_destroy(ti->pages);
            ti->pages = NULL;
        }

        if (new_pages > old_pages)
            tmpfs_free_pages(sbi, new_pages - old_pages);
    }

    spin_unlock(&inode->lock);

    if (err)
        return err;

    // 更新文件偏移
    if (pos)
        *pos = offset;
    else {
        spin_lock(&file->lock);
        file->pos = offset;
        spin_unlock(&file->lock);
    }

    // 更新修改时间
    time_get(&inode->mtime);

    return done;
}

static off_t tmpfs_llseek(
    struct file *file,
    off_t offset,
    seek_whence_t whence
) {
    struct inode *inode = file->path.dentry->inode;
    off_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
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

static int tmpfs_fsync(struct file *file, bool meta) {
    (void)file;
    (void)meta;
    return 0;  // 内存文件系统，不需要落盘
}

// 文件操作表
static struct file_operations tmpfs_file_operations = {
    .open    = tmpfs_open,
    .release = tmpfs_release,
    .read    = tmpfs_read,
    .write   = tmpfs_write,
    .llseek  = tmpfs_llseek,
    .fsync   = tmpfs_fsync,
};

/**
 * 比较 dentry 名称
 *
 * @param dentry 目录项
 * @param len 待比较字符串长度
 * @param str 待比较字符串
 */
static int tmpfs_dentry_compare(
    const struct dentry *dentry,
    uint32_t len,
    const char *str
) {
    uint32_t hash;

    // 长度不等，直接返回差值
    if (dentry->name.len != len)
        return (int)(dentry->name.len - len);

    // 哈希值不等，不匹配
    hash = vfs_full_name_hash(str, len);
    if (dentry->name.hash != hash)
        return 1;

    // 长度和哈希都匹配，用 memcmp 做最终确认
    return memcmp(dentry->name.name, str, len);
}

// dentry 释放回调，tmpfs 没有额外资源
static void tmpfs_dentry_release(struct dentry *dentry) {
    return;
}

// dentry 操作表
static struct dentry_operations tmpfs_dentry_ops = {
    .compare = tmpfs_dentry_compare,
    .release = tmpfs_dentry_release,
};

INITCALL(fs, 0, tmpfs_init);