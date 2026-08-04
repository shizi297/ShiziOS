/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdbool.h>
#include <kio.h>
#include <heap.h>
#include <fs/vfs.h>
#include <initcall.h>
#include <klibc.h>
#include <minmax.h>

#define MAX_SYMLINKS 8

#define VFS_HASH_MIN 256
#define VFS_HASH_MAX (1 << 18)

// POSIX 默认 umask
#define VFS_DEFAULT_UMASK 022

#define VFS_PRINT(fmt, ...) \
    printk("[VFS] " fmt, ##__VA_ARGS__)

#define VFS_PANIC(fmt, ...) \
    printp("[VFS] " fmt, ##__VA_ARGS__)

#define VFS_WARN(fmt, ...) \
    printk("[VFS] WARNING : " fmt, ##__VA_ARGS__)

#define VFS_INFO_PRINT(name) \
    VFS_PRINT("register filesystem : [\"name\" = \"%s\"]\n", name)

// 文件系统锁链表头，用于管理已注册的文件系统
static struct vfs_lock_list fs_list_head = {0};

// dentry LRU 链表头
static struct vfs_lock_list dentry_lru_list = {0};   

static struct vfs_lock_list inode_lru_list = {0};

// 缓存的哈希表
static struct vfs_hash_table inode_cache = {0};   
static struct vfs_hash_table dentry_cache = {0};

// 用于挂载文件系统
static struct {
    struct vfsmount *root;  // 根挂载点
    atomic_uint count;  // 挂载的文件系统数量
    mutex_t lock;   // 保护挂载树修改的互斥锁
} mount_tree = {0};

static struct path *vfs_root = NULL;   // 根路径对象

static uint64_t vfs_get_hash_size(uint64_t obj_size, uint64_t scale);
static inline void vfs_dget(struct dentry *dentry);

/**
 * dentry 哈希
 *
 * @param key 指向 dentry_key 的指针
 *
 * @return 32 位哈希值
 */
static uint32_t vfs_dentry_hash_fn(const void *key) {
    const struct dentry_key *dk = key;
    uint32_t h;

    h = (uint32_t)(uintptr_t)dk->parent ^ dk->name.hash;
    h *= 0x9e370001u;
    return h;
}

/**
 * dentry 比较
 *
 * @param k1 指向 dentry_key 的指针
 * @param k2 指向 dentry_key 的指针
 */
static bool vfs_dentry_eq_fn(const void *k1, const void *k2) {
    const struct dentry_key *a = k1;
    const struct dentry_key *b = k2;

    if (a->parent != b->parent)
        return false;

    if (a->name.len != b->name.len)
        return false;

    if (a->name.hash != b->name.hash)
        return false;

    return !memcmp(a->name.name, b->name.name, a->name.len);
}

/**
 * 从 hlist_node 提取 dentry_key 指针
 *
 * @param node 哈希链表节点
 *
 * @return dentry_key 指针
 */
static const void *vfs_dentry_get_key(const struct hlist_node *node) {
    const struct dentry *d;

    d = hlist_entry(node, struct dentry, hash_node);
    return &d->key;
}

/**
 * inode 哈希
 *
 * @param key 指向 inode_key 的指针
 *
 * @return 32 位哈希值
 */
static uint32_t vfs_inode_hash_fn(const void *key) {
    const struct inode_key *ik = key;
    uint32_t h;

    h = (uint32_t)ik->sb->dev;
    h += (uint32_t)ik->ino;
    h *= 0x9e370001u;
    return h;
}

/**
 * inode 比较
 *
 * @param k1 指向 inode_key 的指针
 * @param k2 指向 inode_key 的指针
 */
static bool vfs_inode_eq_fn(const void *k1, const void *k2) {
    const struct inode_key *a = k1;
    const struct inode_key *b = k2;

    return (a->sb->dev == b->sb->dev) && (a->ino == b->ino);
}

/**
 * 从 hlist_node 提取 inode_key 指针
 *
 * @param node 哈希链表节点
 *
 * @return inode_key 指针
 */
static const void *vfs_inode_get_key(const struct hlist_node *node) {
    const struct inode *i;

    i = hlist_entry(node, struct inode, hash);
    return &i->key;
}

// 初始化 dentry 缓存哈希表
static int vfs_dinit(void) {
    struct vfs_hash_table *cache = &dentry_cache;
    size_t bucket_count;
    struct hlist_head *buckets;

    bucket_count = vfs_get_hash_size(sizeof(struct dentry), 8);
    buckets = kheap_alloc(bucket_count * sizeof(struct hlist_head));
    if (!buckets)
        return -ENOMEM;

    hash_init(
        &cache->hash, buckets, bucket_count,
        vfs_dentry_hash_fn, vfs_dentry_eq_fn
    );

    rwlock_init(&cache->lock);

    return 0;
}

// 初始化 inode 缓存哈希表
static int vfs_iinit(void) {
    struct vfs_hash_table *cache = &inode_cache;
    size_t bucket_count;
    struct hlist_head *buckets;

    bucket_count = vfs_get_hash_size(sizeof(struct inode), 8);
    buckets = kheap_alloc(bucket_count * sizeof(struct hlist_head));
    if (!buckets)
        return -ENOMEM;

    hash_init(
        &cache->hash, buckets, bucket_count,
        vfs_inode_hash_fn, vfs_inode_eq_fn
    );

    rwlock_init(&cache->lock);

    return 0;
}

// 获取vfs哈希表的大小
static uint64_t vfs_get_hash_size(uint64_t obj_size, uint64_t scale) {
    uint64_t total_pages = kheap_max_page();
    uint64_t objs_per_page = PAGE_SIZE / obj_size;
    uint64_t max_objs = total_pages * objs_per_page;
    uint64_t buckets = max_objs / scale;

    buckets = clamp(buckets, (uint64_t)VFS_HASH_MIN, (uint64_t)VFS_HASH_MAX);

    // 向上取整到 2 的幂
    if (buckets == 0) {
        buckets = 1;
    } else {
        buckets--;
        buckets |= buckets >> 1;
        buckets |= buckets >> 2;
        buckets |= buckets >> 4;
        buckets |= buckets >> 8;
        buckets |= buckets >> 16;
        buckets |= buckets >> 32;
        buckets++;
    }

    return buckets;
}

// 获取超级块引用
static inline void vfs_sb_get(struct super_block *sb) {
    atomic_fetch_add(&sb->count, 1);
}

// 释放超级块引用
static inline void vfs_sb_put(struct super_block *sb) {
    atomic_fetch_sub(&sb->count, 1);
}

// 增加 vfsmount 引用计数
static inline void vfs_mntget(struct vfsmount *mnt) {
    atomic_fetch_add(&mnt->count, 1);
}

// 减少 vfsmount 引用计数
static inline void vfs_mntput(struct vfsmount *mnt) {
    if (atomic_fetch_sub(&mnt->count, 1) == 1)
        kheap_free(mnt);
}

// 在 dentry 哈希表中查找 dentry
static struct dentry *vfs_dlookup(struct dentry *parent, const char *name, size_t len) {
    return vfs_dcache_find(parent, name, len);
}

// 分配并初始化 dentry
static struct dentry *vfs_dalloc(struct dentry *parent, const char *name) {
    struct dentry *dentry;

    // 先检查缓存中是否已有同名 dentry 避免重复创建
    dentry = vfs_dcache_find(parent, name, strlen(name));
    if (dentry)
        return dentry;

    // 缓存未命中，分配新的 dentry
    dentry = kheap_alloc(sizeof(struct dentry));
    if (!dentry)
        return NULL;

    // 设置父目录项并增加引用计数
    if (parent) {
        dentry->parent = parent;
        vfs_dget(parent);
    } else {
        dentry->parent = NULL;
    }

    // 复制名称并计算哈希值
    dentry->name.name = strdup(name);
    dentry->name.len = strlen(name);
    dentry->name.hash = vfs_full_name_hash(name, dentry->name.len);

    dentry->inode = NULL;
    dentry->ops = NULL;
    INIT_HLIST_NODE(&dentry->hash_node);
    INIT_LIST_HEAD(&dentry->lru);
    INIT_LIST_HEAD(&dentry->child);
    INIT_LIST_HEAD(&dentry->subdirs);
    atomic_init(&dentry->count, 1);
    dentry->flags = DCACHE_NONE;
    spinlock_init(&dentry->lock);
    dentry->key.parent = parent;
    dentry->key.name = dentry->name;

    if (parent) {
        // 将新 dentry 添加到父目录的子链表中
        spin_lock(&parent->lock);
        list_add_tail(&dentry->child, &parent->subdirs);
        spin_unlock(&parent->lock);
    }

    return dentry;
}

// 读取符号链接目标
static ssize_t vfs_readlink(struct dentry *dentry, char *buf, size_t bufsiz) {
    struct inode *inode;

    spin_lock(&dentry->lock);
    inode = dentry->inode;
    spin_unlock(&dentry->lock);

    if (!inode || !S_ISLNK(inode->mode))
        return -EINVAL;

    if (!inode->ops->readlink)
        return -ENOSYS;

    return inode->ops->readlink(inode, buf, bufsiz);
}

// 将 dentry 加入全局 LRU 链表尾部
static void vfs_dlru_add(struct dentry *dentry) {
    spin_lock(&dentry_lru_list.lock);

    if (list_empty(&dentry->lru))
        list_add_tail(&dentry->lru, &dentry_lru_list.list);

    spin_unlock(&dentry_lru_list.lock);
}

// 将 dentry 从全局 LRU 链表中删除
static void vfs_dlru_del(struct dentry *dentry) {
    spin_lock(&dentry_lru_list.lock);

    if (!list_empty(&dentry->lru))
        list_del_init(&dentry->lru);

    spin_unlock(&dentry_lru_list.lock);
}

/**
 * 将 inode 加入全局 LRU 链表尾部
 *
 * @param inode 要加入 LRU 的 inode
 */
static void vfs_ilru_add(struct inode *inode) {
    spin_lock(&inode_lru_list.lock);
    if (list_empty(&inode->lru))
        list_add_tail(&inode->lru, &inode_lru_list.list);
    spin_unlock(&inode_lru_list.lock);
}

/**
 * 将 inode 从全局 LRU 链表中删除
 *
 * @param inode 要从 LRU 中移除的 inode
 */
static void vfs_ilru_del(struct inode *inode) {
    spin_lock(&inode_lru_list.lock);
    if (!list_empty(&inode->lru))
        list_del_init(&inode->lru);
    spin_unlock(&inode_lru_list.lock);
}

// 增加 dentry 引用计数
static inline void vfs_dget(struct dentry *dentry) {
    uint64_t old = atomic_fetch_add(&dentry->count, 1);
    if (!old) {
        vfs_dlru_del(dentry);
    }
}

// 减少 dentry 引用计数
static inline void vfs_dput(struct dentry *dentry) {
    if (atomic_fetch_sub(&dentry->count, 1) == 1) 
        vfs_dlru_add(dentry);
}

/**
 * 检查文件是否已存在
 *
 * @param parent_dentry 父目录的 dentry
 * @param name 文件名
 * @param name_len 文件名长度
 *
 * 调用前需要父目录的 inode 锁
 */
static inline bool vfs_file_exists(
    struct dentry *parent_dentry,
    const char *name,
    size_t name_len
) {
    struct dentry *exist = vfs_dcache_find(parent_dentry, name, name_len);
    if (exist) {
        bool has_inode;
        spin_lock(&exist->lock);
        has_inode = (exist->inode != NULL);
        spin_unlock(&exist->lock);
        vfs_dput(exist);
        return has_inode;
    }

    // 缓存未命中，穿透到文件系统确认
    exist = vfs_dalloc(parent_dentry, name);
    if (!exist)
        return false;

    exist = parent_dentry->inode->ops->lookup(parent_dentry->inode, exist);
    if (!exist || IS_ERR(exist))
        return false;

    bool has_inode;
    spin_lock(&exist->lock);
    has_inode = (exist->inode != NULL);
    spin_unlock(&exist->lock);
    vfs_dput(exist);
    return has_inode;
}

// 根据名称查找已注册的文件系统类型
static struct file_system_type *vfs_find_filesystem(const char *name) {
    struct file_system_type *fs;
    spin_lock(&fs_list_head.lock);
    list_for_each_entry(fs, &fs_list_head.list, list) {
        if (!strcmp(fs->name, name)) {
            spin_unlock(&fs_list_head.lock);
            return fs;
        }
    }

    spin_unlock(&fs_list_head.lock);
    return NULL;
}

// 获取当前任务的文件系统上下文
static void vfs_get_current_fs(struct path **root, struct path **pwd) {
    struct path *tmp_root = NULL;
    struct path *tmp_pwd = NULL;
    task_get_current_fs (&tmp_root, &tmp_pwd);
    
    if (root) {
        *root = kheap_alloc(sizeof(struct path));
        if (*root) {
            memcpy(*root, tmp_root, sizeof(struct path));
            vfs_path_get(*root);
        }
    }
    
    if (pwd) {
        *pwd = kheap_alloc(sizeof(struct path));
        if (*pwd) {
            memcpy(*pwd, tmp_pwd, sizeof(struct path));
            vfs_path_get(*pwd);
        }
    }
}

// 归还当前任务的文件系统上下文
static void vfs_put_current_fs(struct path **root, struct path **pwd) {
    if (root && *root) {
        vfs_path_put(*root);
        kheap_free(*root);
        *root = NULL;
    }

    if (pwd && *pwd) {
        vfs_path_put(*pwd);
        kheap_free(*pwd);
        *pwd = NULL;
    }
}

// 将 vfsmount 挂载到指定挂载点
static void vfs_attach_mount(struct vfsmount *mnt, struct path *mountpoint) {
    mutex_lock(&mount_tree.lock);
    list_add_tail(&mnt->list, &mountpoint->mnt->mnt_mounts);
    atomic_fetch_add(&mount_tree.count, 1);
    mutex_unlock(&mount_tree.lock);

    spin_lock(&mountpoint->dentry->lock);
    mountpoint->dentry->flags |= DCACHE_MOUNTED;
    spin_unlock(&mountpoint->dentry->lock);
}

// 从挂载树中摘除 vfsmount
static void vfs_detach_mount(struct vfsmount *mnt) {
    mutex_lock(&mount_tree.lock);
    list_del(&mnt->list);
    atomic_fetch_sub(&mount_tree.count, 1);
    mutex_unlock(&mount_tree.lock);

    spin_lock(&mnt->mountpoint->lock);
    mnt->mountpoint->flags &= ~DCACHE_MOUNTED;
    spin_unlock(&mnt->mountpoint->lock);
}

// 根据挂载点 dentry 查找对应的 vfsmount，返回时已增加引用计数
static struct vfsmount *vfs_find_mount(struct dentry *dentry) {
    struct vfsmount *found = NULL;

    mutex_lock(&mount_tree.lock);

    // 获取当前挂载点总数
    uint32_t total = atomic_load(&mount_tree.count);

    // 分配栈数组,使用挂载点总数作为上限，防止溢出
    struct vfsmount **stack = kheap_alloc(total * sizeof(struct vfsmount *));
    if (!stack) {
        mutex_unlock(&mount_tree.lock);
        return NULL;
    }

    int top = 0;

    // 从根挂载点开始遍历
    if (mount_tree.root)
        stack[top++] = mount_tree.root;

    while (top > 0) {
        struct vfsmount *cur = stack[--top];

        // 检查当前挂载点的挂载点 dentry 是否匹配
        if (cur->mountpoint == dentry) {
            found = cur;
            // 在释放锁之前增加引用计数，防止并发卸载导致悬挂指针
            vfs_mntget(found);
            break;
        }

        // 遍历当前挂载点的所有子挂载点
        struct vfsmount *child;
        list_for_each_entry(child, &cur->mnt_mounts, list) {
            stack[top++] = child;
        }
    }

    kheap_free(stack);

    mutex_unlock(&mount_tree.lock);

    return found;
}

// 获取下一个目录或文件的名称
static struct path *vfs_path_prs_next(vfs_path_prs_t *prs, const struct path *pwd) {
    if (!prs || !prs->path) 
        return ERR_PTR(-EINVAL);

    // curr_path 为空，进行初始化
    if (!prs->curr_path) {
        struct path *new_cur = kheap_alloc(sizeof(struct path));
        if (!new_cur) return ERR_PTR(-ENOMEM);

        if (prs->path[0] == '/') {
            // 是根目录，使用vfs_root变量
            new_cur->mnt = vfs_root->mnt;
            new_cur->dentry = vfs_root->dentry;
            vfs_path_get(new_cur);
        } else {
            // 是相对于用户的路径，设置为当前任务的工作目录
            new_cur->mnt = pwd->mnt;
            new_cur->dentry = pwd->dentry;
            vfs_path_get(new_cur);
        }
        prs->curr_path = new_cur;
    }

    // 跳过路径分隔符
    while (prs->path[prs->offset] == '/')
        prs->offset++;

    // 如果已经到达字符串末尾，返回 NULL
    if (prs->path[prs->offset] == '\0')
        return NULL;

    // 保存当前起始位置
    const char *start = prs->path + prs->offset;

    // 找到这个字符的末尾
    while (prs->path[prs->offset] != '/' && prs->path[prs->offset] != '\0') 
        prs->offset++;

    // 计算长度用于传给目录项查找函数
    size_t len = (prs->path + prs->offset) - start;

    struct dentry *parent_dentry = prs->curr_path->dentry;

    // 获取当前找的 dentry
    struct dentry *dentry = vfs_dlookup(parent_dentry, start, len);

    if (!dentry) {
        // 缓存未命中，创建一个新的 dentry
        char *name = kheap_alloc(len + 1);
        if (!name)
            return ERR_PTR(-ENOMEM);

        // 复制目录名到临时缓冲区
        memcpy(name, start, len);
        name[len] = '\0';

        // 分配一个新的 dentry
        dentry = vfs_dalloc(parent_dentry, name);
        if (!dentry) {
            kheap_free(name);
            return ERR_PTR(-ENOMEM);
        }

        // 创建inode并关联到dentry
        struct dentry *found = parent_dentry->inode->ops->lookup(parent_dentry->inode, dentry);
        if (found && found != dentry) {
            // 查找成功，释放新创建的 dentry 并使用找到的 dentry
            vfs_dput(dentry);
            dentry = found;
        } else if (IS_ERR(found)) {
            // 查找失败，释放资源并返回错误
            vfs_dput(dentry);
            kheap_free(name);
            return ERR_CAST(found);
        }

        kheap_free(name);
    }

    // 更新句柄中的当前路径
    struct path *new_path_ptr = kheap_alloc(sizeof(struct path));
    if (!new_path_ptr)
        return ERR_PTR(-ENOMEM);
    
    // 设置新的mnt，并增加它的引用
    new_path_ptr->mnt = prs->curr_path->mnt;
    vfs_mntget(new_path_ptr->mnt);
    
    // 设置新的dentry（查找时，已添加引用，这里不做重复添加）
    new_path_ptr->dentry = dentry;
    
    // 释放旧路径的引用
    vfs_path_put(prs->curr_path);
    
    // 释放临时分配的 path
    kheap_free(prs->curr_path);
        
    prs->curr_path = new_path_ptr;

    return prs->curr_path;
}

/**
 * 查找路径
 *
 * @param path 路径字符串
 * @param flags 查找标志
 * @param pwd 当前工作目录
 *
 * @return 路径的path对象
 */
static struct path *vfs_path_lookup(
    const char *path,
    vfs_lookup_flags_t flags,
    const struct path *pwd
) {
    vfs_path_prs_t stack[MAX_SYMLINKS];
    int depth = 1;
    int err = 0;
    struct path *res = NULL;

    // 初始化最外层的解析器，统一使用动态分配的路径副本
    stack[0].path = strdup(path);
    if (!stack[0].path) return ERR_PTR(-ENOMEM);
    stack[0].offset = 0;
    stack[0].curr_path = NULL;

    // 解析每一个组件
    while (1) {
        struct path *p = vfs_path_prs_next(&stack[depth-1], pwd);
        if (IS_ERR(p)) {
            err = PTR_ERR(p);
            break;
        }

        // 所有组件解析完成
        if (p == NULL) {
            if (depth == 1) {
                // 没有符号链接，直接检查结果并返回
                bool has_inode;
                spin_lock(&stack[0].curr_path->dentry->lock);
                has_inode = (stack[0].curr_path->dentry->inode != NULL);
                spin_unlock(&stack[0].curr_path->dentry->lock);
                if (!has_inode) {
                    err = -ENOENT;
                    break;
                }

                if (
                    (flags & LOOKUP_DIRECTORY) &&
                    !S_ISDIR(stack[0].curr_path->dentry->inode->mode)
                ) {
                    err = -ENOTDIR;
                    break;
                }

                // 解析成功，转移结果所有权
                res = stack[0].curr_path;
                stack[0].curr_path = NULL;

                // 释放主解析器的路径副本
                kheap_free((void *)stack[0].path);
                stack[0].path = NULL;
                return res;
            } else {
                /*
                 * 符号链接目标路径解析完毕
                 * 需要返回上一层并接续剩余路径
                 */
                struct path *target_path = stack[depth-1].curr_path;
                stack[depth-1].curr_path = NULL;   // 转移所有权

                // 清理当前层（符号链接的解析器）
                kheap_free((void *)stack[depth-1].path);
                stack[depth-1].path = NULL;

                // 释放当前层的 struct path 结构体本身（target_path 已取出）
                if (stack[depth-1].curr_path)
                    kheap_free(stack[depth-1].curr_path);

                // 弹栈，回退到上一层
                depth--;

                // 将目标路径的终点交给上一层解析器
                if (stack[depth-1].curr_path) {
                    vfs_path_put(stack[depth-1].curr_path);
                    kheap_free(stack[depth-1].curr_path);
                }
                stack[depth-1].curr_path = target_path;

                // 继续解析剩余路径
                continue;
            }
        }

        bool is_mounted;
        spin_lock(&p->dentry->lock);
        is_mounted = (p->dentry->flags & DCACHE_MOUNTED);
        spin_unlock(&p->dentry->lock);
        if (is_mounted) {
            // 当前是挂载点
            struct vfsmount *mnt = vfs_find_mount(p->dentry);
            if (!mnt) {
                err = -ENOENT;
                break;
            }

            struct path *new_cur = kheap_alloc(sizeof(struct path));
            if (!new_cur) {
                err = -ENOMEM;
                break;
            }
            
            /*
             * 切换到挂载点根
             * 引用已在 vfs_find_mount 函数增加过
             * 这里不增加
             */
            new_cur->mnt = mnt;
            vfs_dget(mnt->root);
            new_cur->dentry = mnt->root;

            vfs_path_put(stack[depth-1].curr_path);
            kheap_free(stack[depth-1].curr_path);
            stack[depth-1].curr_path = new_cur;
            continue;
        }

        // 检查是否是符号链接
        if (S_ISLNK(p->dentry->inode->mode) && (flags & LOOKUP_FOLLOW)) {
            if (depth >= MAX_SYMLINKS) {
                // 符号链接过多，可能存在循环，返回错误
                err = -ELOOP;
                break;
            }

            // 保存当前层状态（增加引用，因为将作为上一层被共享）
            vfs_path_get(stack[depth-1].curr_path);

            // 读取符号链接的目标路径
            char target[PATH_MAX];
            ssize_t ret = vfs_readlink(p->dentry, target, sizeof(target) - 1);
            if (ret < 0) {
                vfs_path_put(stack[depth-1].curr_path); // 回滚刚增加的引用
                err = ret;
                break;
            }
            target[ret] = '\0';

            // 创建新的一层解析器
            depth++;
            memset(&stack[depth-1], 0, sizeof(vfs_path_prs_t));

            // 复制目标路径字符串，保证每一层独立
            stack[depth-1].path = strdup(target);
            if (!stack[depth-1].path) {
                depth--;
                vfs_path_put(stack[depth-1].curr_path);
                err = -ENOMEM;
                break;
            }

            // 设置新解析器的起始目录
            const struct path *base = (target[0] == '/') ? vfs_root : stack[depth-2].curr_path;
            stack[depth-1].curr_path = kheap_alloc(sizeof(struct path));
            if (!stack[depth-1].curr_path) {
                depth--;
                kheap_free((void *)stack[depth-1].path);
                vfs_path_put(stack[depth-1].curr_path);
                err = -ENOMEM;
                break;
            }
            stack[depth-1].curr_path->mnt = base->mnt;
            stack[depth-1].curr_path->dentry = base->dentry;
            vfs_path_get(stack[depth-1].curr_path);

            // 初始化新解析器后直接开始下一轮循环，它会自动被当做当前解析器
            continue;
        }

        // 普通组件，不需要处理，直接继续解析下一个组件
    }

    // 清理所有栈帧的资源
    for (int i = 0; i < depth; i++) {
        if (stack[i].curr_path) {
            vfs_path_put(stack[i].curr_path);
            kheap_free(stack[i].curr_path);
        }
        if (stack[i].path) {
            kheap_free((void *)stack[i].path);
        }
    }

    return ERR_PTR(err);
}

/**
 * 根据路径找到父目录的信息
 *
 * @param path 要解析的路径字符串
 * @param pwd 当前工作目录
 * @param name 指向原始 path 中最后一个组件的起始字符
 * @param len 最后一个组件的长度
 *
 * @return 父目录的 path 指针
 */
static struct path *vfs_path_parent(
    const char *path,
    const struct path *pwd,
    const char **name,
    size_t *len
) {
    const char *last_slash;
    const char *end;
    char *parent_buf;
    struct path *parent;
    size_t parent_len;

    if (!path || !name || !len)
        return ERR_PTR(-EINVAL);

    // 跳过末尾的斜杠，定位到实际路径的最后一个非斜杠字符
    end = path + strlen(path);
    while (end > path && end[-1] == '/')
        end--;
    if (end == path)                // 路径全部由斜杠组成，无效
        return ERR_PTR(-ENOENT);

    // 在有效范围内查找最后一个 '/' 字符
    last_slash = memrchr(path, '/', end - path);
    if (last_slash) {
        // 有斜杠：父目录为前缀部分，最后一个组件为斜杠后的部分
        parent_len = last_slash - path;
        if (parent_len == 0) {
            // 父目录是根目录
            parent = vfs_path_lookup("/", LOOKUP_DIRECTORY, pwd);
        } else {
            // 分配父目录字符串
            parent_buf = kheap_alloc(parent_len + 1);
            if (!parent_buf)
                return ERR_PTR(-ENOMEM);

            memcpy(parent_buf, path, parent_len);
            parent_buf[parent_len] = '\0';
            parent = vfs_path_lookup(parent_buf, LOOKUP_DIRECTORY, pwd);
            kheap_free(parent_buf);
        }
        
        // 输出最后一个组件的名称和长度（跳过斜杠）
        *name = last_slash + 1;
        *len = end - (last_slash + 1);
    } else {
        // 没有斜杠：整个路径就是最后一个组件，父目录为当前目录
        parent = vfs_path_lookup(".", LOOKUP_DIRECTORY, pwd);
        *name = path;
        *len = end - path;
    }

    if (IS_ERR(parent))
        return parent;

    // 确保最后一个组件非空
    if (*len == 0) {
        vfs_path_put(parent);
        return ERR_PTR(-ENOENT);
    }

    return parent;
}

// 挂载根文件系统
static int vfs_root_mount(void) {
    struct file_system_type *tmpfs;
    struct dentry *root_dentry;
    struct vfsmount *mnt;
    struct path *root_path;
    kresult_t res;

    // 查找 tmpfs 文件系统类型
    tmpfs = vfs_find_filesystem("tmpfs");
    if (!tmpfs)
        return -ENODEV;

    // 调用 tmpfs 的 mount 回调，获取根 dentry
    res = tmpfs->mount(tmpfs, MS_NOUSER, NULL, NULL);
    if (res.err)
        return res.err;

    root_dentry = res.ptr;

    // 分配并初始化根挂载点
    mnt = kheap_alloc(sizeof(struct vfsmount));
    if (!mnt) {
        vfs_dput(root_dentry);
        return -ENOMEM;
    }

    mnt->sb = root_dentry->inode->sb;
    mnt->root = root_dentry;
    mnt->mountpoint = NULL;
    mnt->parent = NULL;
    mnt->flags = MS_NONE;
    atomic_init(&mnt->count, 1);
    INIT_LIST_HEAD(&mnt->mnt_mounts);

    // 设置挂载树根
    mount_tree.root = mnt;

    // 设置全局根路径对象 vfs_root
    root_path = kheap_alloc(sizeof(struct path));
    if (!root_path) {
        vfs_dput(root_dentry);
        kheap_free(mnt);
        return -ENOMEM;
    }

    root_path->mnt = mnt;
    root_path->dentry = root_dentry;

    // 增加引用计数并设置全局变量
    vfs_path_get(root_path);
    vfs_root = root_path;

    return 0;
}

// 挂载 /dev 用于设备文件
static int vfs_dev_mount(void) {
    struct path *dev_path;
    struct dentry *root_dentry;
    struct vfsmount *mnt;
    struct file_system_type *tmpfs;
    kresult_t res;
    int err;

    // 创建 /dev 目录
    err = vfs_mkdir("/dev", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, vfs_root);
    if (err)
        return err;

    // 获取 /dev 的 path（已增加引用）
    dev_path = vfs_path_lookup("/dev", LOOKUP_FOLLOW, vfs_root);
    if (IS_ERR(dev_path))
        return PTR_ERR(dev_path);

    // 查找 tmpfs 文件系统类型
    tmpfs = vfs_find_filesystem("tmpfs");
    if (!tmpfs) {
        vfs_path_put(dev_path);
        return -ENODEV;
    }

    // 调用 tmpfs 的 mount 回调，创建根 dentry
    res = tmpfs->mount(tmpfs, MS_NOUSER, NULL, NULL);
    if (res.err) {
        vfs_path_put(dev_path);
        return res.err;
    }

    root_dentry = res.ptr;

    // 分配 vfsmount
    mnt = kheap_alloc(sizeof(struct vfsmount));
    if (!mnt) {
        vfs_dput(root_dentry);
        vfs_path_put(dev_path);
        return -ENOMEM;
    }

    mnt->sb = root_dentry->inode->sb;
    mnt->root = root_dentry;
    mnt->mountpoint = dev_path->dentry;
    mnt->parent = dev_path->mnt;
    mnt->flags = MS_NOUSER;
    atomic_init(&mnt->count, 1);
    INIT_LIST_HEAD(&mnt->mnt_mounts);

    // 增加引用
    vfs_mntget(mnt->parent);
    vfs_dget(mnt->mountpoint);
    vfs_sb_get(mnt->sb);

    // 将新挂载点加入挂载树
    vfs_attach_mount(mnt, dev_path);

    vfs_path_put(dev_path);
    return 0;
}

/**
 * 检查 inode 权限
 *
 * @param inode 索引节点
 * @param mask 访问掩码
 */
static int vfs_inode_permission(struct inode *inode, int mask) {
    uid_t uid;
    gid_t gid;
    task_get_current_ugid(&uid, &gid);

    // uid 为 0 的进程拥有root权限，直接放行
    if (!uid)
        return 0;

    mode_t mode = inode->mode;

    if (uid == inode->uid) {
        // 文件属主，使用用户权限位
        if ((mask & MAY_READ) && !(mode & S_IRUSR))
            return -EACCES;
        if ((mask & MAY_WRITE) && !(mode & S_IWUSR))
            return -EACCES;
        if ((mask & MAY_EXEC) && !(mode & S_IXUSR))
            return -EACCES;
    } else if (gid == inode->gid) {
        // 属组成员，使用组权限位
        if ((mask & MAY_READ) && !(mode & S_IRGRP))
            return -EACCES;
        if ((mask & MAY_WRITE) && !(mode & S_IWGRP))
            return -EACCES;
        if ((mask & MAY_EXEC) && !(mode & S_IXGRP))
            return -EACCES;
    } else {
        // 其他人，使用其他用户权限位
        if ((mask & MAY_READ) && !(mode & S_IROTH))
            return -EACCES;
        if ((mask & MAY_WRITE) && !(mode & S_IWOTH))
            return -EACCES;
        if ((mask & MAY_EXEC) && !(mode & S_IXOTH))
            return -EACCES;
    }

    return 0;
}

/**
 * 创建常规文件
 *
 * @param dir 父目录 inode
 * @param dentry 新文件目录项
 * @param mode 文件权限
 */
static int vfs_create(struct inode *dir, struct dentry *dentry, mode_t mode) {
    int err;

    err = vfs_inode_permission(dir, MAY_WRITE | MAY_EXEC);
    if (err)
        return err;

    if (!dir->ops->create)
        return -EINVAL;

    return dir->ops->create(dir, dentry, mode);
}

/**
 * 默认 llseek 实现
 *
 * @param file 打开的文件
 * @param offset 偏移量
 * @param whence 参照点
 *
 * @return 新文件偏移
 */
static off_t vfs_default_llseek(struct file *file, off_t offset, seek_whence_t whence) {
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

// 默认文件操作结构体, 用于文件系统没有提供特定实现时的占位符
static struct file_operations default_file_operations = {
    .open    = NULL,
    .release = NULL,
    .read    = NULL,
    .write   = NULL,
    .llseek  = vfs_default_llseek,
};

// 将 inode 添加到 inode 缓存哈希表
void vfs_icache_add(struct inode *inode) {
    rwlock_write_lock(&inode_cache.lock);
    hash_add(&inode_cache.hash, &inode->hash, &inode->key);
    rwlock_write_unlock(&inode_cache.lock);
}

/**
 * 对已知的 inode 增加引用计数
 *
 * @param inode 要增加引用的 inode
 */
void vfs_iget(struct inode *inode) {
    if (!inode)
        return;

    if (atomic_fetch_add(&inode->count, 1) == 0)
        vfs_ilru_del(inode);
}

/**
 * 增加 inode 引用
 *
 * @param inode 要释放引用的 inode
 */
void vfs_iput(struct inode *inode) {
    if (!inode)
        return;

    if (atomic_fetch_sub(&inode->count, 1) != 1)
        return;

    vfs_ilru_add(inode);
}

/**
 * 在 inode 缓存哈希表中查找 inode
 *
 * @param sb 超级块
 * @param ino inode 号
 *
 * @return 找到的 inode（引用计数已增加）
 */
struct inode *vfs_icache_find(struct super_block *sb, ino_t ino) {
    struct inode_key key = { .sb = sb, .ino = ino };
    struct hlist_node *node;
    struct inode *inode = NULL;

    rwlock_read_lock(&inode_cache.lock);

    node = hash_lookup(&inode_cache.hash, &key, vfs_inode_get_key);
    if (node) {
        inode = hlist_entry(node, struct inode, hash);
        if (atomic_fetch_add(&inode->count, 1) == 0)
            vfs_ilru_del(inode);
    }

    rwlock_read_unlock(&inode_cache.lock);
    return inode;
}

// 将 dentry 添加到 dentry 缓存哈希表
void vfs_dcache_add(struct dentry *dentry) {
    rwlock_write_lock(&dentry_cache.lock);
    hash_add(&dentry_cache.hash, &dentry->hash_node, &dentry->key);
    rwlock_write_unlock(&dentry_cache.lock);
}

/**
 * 在 dentry 缓存哈希表中查找 dentry
 *
 * @param parent 父 dentry
 * @param name 组件名
 * @param len 名称长度
 *
 * @return 找到的 dentry（引用计数已增加）
 */
struct dentry *vfs_dcache_find(struct dentry *parent, const char *name, size_t len) {
    struct dentry_key key;
    struct hlist_node *node;
    struct dentry *dentry;

    key.parent = parent;
    key.name.name = name;
    key.name.len = len;
    key.name.hash = vfs_full_name_hash(name, len);

    rwlock_read_lock(&dentry_cache.lock);

    node = hash_lookup(&dentry_cache.hash, &key, vfs_dentry_get_key);
    if (node) {
        dentry = hlist_entry(node, struct dentry, hash_node);
        vfs_dget(dentry);
        rwlock_read_unlock(&dentry_cache.lock);
        return dentry;
    }

    rwlock_read_unlock(&dentry_cache.lock);
    return NULL;
}

/**
 * 执行打开文件
 *
 * @param ps 已构造好的路径对象（调用者持有引用）
 * @param flags 打开标志
 * @param perm_mask 权限掩码
 *
 * @return file 指针
 */
static struct file *vfs_do_open(struct path *ps, open_flags_t flags, int perm_mask) {
    struct file *file = NULL;
    int err = 0;
    struct inode *inode = ps->dentry->inode;

    // 检查权限
    err = vfs_inode_permission(inode, perm_mask);
    if (err)
        goto out;

    // O_DIRECTORY 支持
    if ((flags & O_DIRECTORY) && !S_ISDIR(inode->mode)) {
        err = -ENOTDIR;
        goto out;
    }

    // 如果要求写操作或截断，且文件系统以只读挂载，则拒绝
    if ((perm_mask & MAY_WRITE) || (flags & O_TRUNC)) {
        if (ps->mnt->flags & MS_RDONLY) {
            err = -EROFS;
            goto out;
        }
    }

    // 分配文件描述符
    file = kheap_alloc(sizeof(struct file));
    if (!file) {
        err = -ENOMEM;
        goto out;
    }

    file->path = *ps;
    vfs_path_get(&file->path);
    file->pos = 0;
    file->mode = perm_mask;
    file->flags = flags;
    atomic_init(&file->count, 1);
    spinlock_init(&file->lock);
    file->ops = inode->fop ? inode->fop : &default_file_operations;

    // 调用文件系统特定的 open 操作
    if (file->ops->open) {
        err = file->ops->open(inode, file);
        if (err)
            goto out_free_file;
    }

    // 如果指定了 O_TRUNC 且有写权限且是常规文件，执行截断
    if ((flags & O_TRUNC) && (perm_mask & MAY_WRITE) && S_ISREG(inode->mode)) {
        err = vfs_truncate(file, 0);
        if (err)
            goto out_free_file;
    }

    return file;

out_free_file:
    vfs_path_put(&file->path);
    kheap_free(file);
    file = NULL;
out:
    vfs_path_put(ps);
    return ERR_PTR(err);
}

/**
 * 默认设备 open 函数
 *
 * @param inode 设备节点 inode
 * @param file  文件结构体
 */
static int vfs_dev_open(struct inode *inode, struct file *file) {
    struct file_operations *real_fops;

    // 根据设备号和文件类型查找已注册的驱动操作表
    real_fops = drivers_dev_find(inode->rdev, inode->mode);
    if (!real_fops)
        return -ENODEV;

    // 替换文件操作表为真正的驱动操作表
    file->ops = real_fops;

    // 如果驱动有自己的 open 方法，则调用它
    if (file->ops->open)
        return file->ops->open(inode, file);

    return 0;
}

// 默认设备 llseek 函数
static off_t vfs_dev_llseek(struct file *file, off_t offset, seek_whence_t whence) {
    return -ENODEV;
}

// 默认设备 read 函数
static ssize_t vfs_dev_read(struct file *file, char *buf, size_t count, off_t *pos) {
    return -ENODEV;
}

// 默认设备 write 函数
static ssize_t vfs_dev_write(struct file *file, const char *buf, size_t count, off_t *pos) {
    return -ENODEV;
}

// 默认设备 release 函数
static int vfs_dev_release(struct inode *inode, struct file *file) {
    return -ENODEV;
}

// 默认设备操作表，用于设备节点 inode->ops 的初始值
struct file_operations dev_fops = {
    .open    = vfs_dev_open,
    .read    = vfs_dev_read,
    .write   = vfs_dev_write,
    .llseek  = vfs_dev_llseek,
    .release = vfs_dev_release,
};

// 注册文件系统
int vfs_register_filesystem(struct file_system_type *fs) {
    if (!fs || !fs->name) return -EINVAL;

    uint64_t flags;
    struct file_system_type *p;
    int err = 0;

    spin_lock_irqsave(&fs_list_head.lock, &flags);

    // 检查是否已存在同名文件系统
    list_for_each_entry(p, &fs_list_head.list, list) {
        if (!strcmp(p->name, fs->name)) {
            err = -EEXIST;
            goto out;
        }
    }

    list_add_tail(&fs->list, &fs_list_head.list);
    VFS_INFO_PRINT(fs->name);

out:
    spin_unlock_irqrestore(&fs_list_head.lock, flags);
    return err;
}

// 计算字符串的哈希值
uint32_t vfs_full_name_hash(const char *name, unsigned int len) {
    uint32_t hash = 0;
    while (len--) 
        hash = (hash << 5) + hash + *name++;

    return hash;
}

// 增加路径引用
void vfs_path_get(struct path *path) {
    vfs_mntget(path->mnt);
    vfs_dget(path->dentry);
}

// 减少路径引用
void vfs_path_put(struct path *path) {
    vfs_dput(path->dentry);
    vfs_mntput(path->mnt);
}

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
kresult_t vfs_mount(
    const char *dev_name,
    const char *dir_name,
    const char *type,
    mount_flags_t flags,
    void *data,
    bool from_kernel
) {
    struct path *mountpoint = NULL;
    struct vfsmount *mnt = NULL;
    struct dentry *root_dentry = NULL;
    struct file_system_type *fs = NULL;
    struct path *root_path = NULL;
    struct path *pwd_path = NULL;
    kresult_t result;
    int err;

    // 校验 dir_name 非空
    if (!dir_name)
        return (kresult_t){ .err = -EINVAL, .ptr = NULL };

    // 用户调用时不能指定 MS_NOUSER 标志
    if (!from_kernel && (flags & MS_NOUSER))
        return (kresult_t){ .err = -EINVAL, .ptr = NULL };

    // 用户不能挂载 devtmpfs
    if (!from_kernel && type && !strcmp(type, "devtmpfs"))
        return (kresult_t){ .err = -EPERM, .ptr = NULL };

    // 获取当前任务的文件系统上下文
    vfs_get_current_fs(&root_path, &pwd_path);
    if (!root_path || !pwd_path) {
        vfs_put_current_fs(&root_path, &pwd_path);
        return (kresult_t){ .err = -ENOMEM, .ptr = NULL };
    }

    // 解析挂载点路径
    mountpoint = vfs_path_lookup(dir_name, LOOKUP_DIRECTORY, pwd_path);
    if (IS_ERR(mountpoint)) {
        err = PTR_ERR(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return (kresult_t){ .err = err, .ptr = NULL };
    }

    // 普通挂载：需要文件系统类型
    if (!type) {
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return (kresult_t){ .err = -EINVAL, .ptr = NULL };
    }

    // 查找文件系统类型
    uint64_t irqflags;
    spin_lock_irqsave(&fs_list_head.lock, &irqflags);

    list_for_each_entry(fs, &fs_list_head.list, list)
        if (!strcmp(fs->name, type)) break;

    spin_unlock_irqrestore(&fs_list_head.lock, irqflags);
    if (!fs) {
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return (kresult_t){ .err = -ENODEV, .ptr = NULL };
    }

    // 调用文件系统的 mount 回调，获取根 dentry
    result = fs->mount(fs, flags, dev_name, data);
    if (result.err) {
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return result;
    }
    root_dentry = result.ptr;

    // 分配 vfsmount 结构体
    mnt = kheap_alloc(sizeof(struct vfsmount));
    if (!mnt) {
        vfs_dput(root_dentry);
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return (kresult_t){ .err = -ENOMEM, .ptr = NULL };
    }

    // 初始化 mnt 字段
    mnt->sb = root_dentry->inode->sb;
    mnt->root = root_dentry;
    mnt->mountpoint = mountpoint->dentry;
    mnt->parent = mountpoint->mnt;
    mnt->flags = flags;
    atomic_init(&mnt->count, 1);
    INIT_LIST_HEAD(&mnt->mnt_mounts);

    // 增加引用计数
    vfs_mntget(mnt->parent);
    vfs_dget(mnt->mountpoint);
    vfs_sb_get(mnt->sb);

    // 将新挂载点加入挂载树
    vfs_attach_mount(mnt, mountpoint);

    vfs_path_put(mountpoint);
    vfs_put_current_fs(&root_path, &pwd_path);
    return (kresult_t){ .err = 0, .ptr = mnt };
}

/**
 * 卸载文件系统
 *
 * @param dir_name 挂载点路径
 * @param flags 标志位
 * @param from_kernel 调用者是否为内核
 */
int vfs_umount(const char *dir_name, int flags, bool from_kernel) {
    struct path *mountpoint = NULL;
    struct vfsmount *mnt = NULL;
    struct super_block *sb = NULL;
    struct path *root_path = NULL;
    struct path *pwd_path = NULL;
    int err = 0;

    if (!dir_name || flags)
        return -EINVAL;

    // 获取当前任务的文件系统上下文
    vfs_get_current_fs(&root_path, &pwd_path);
    if (!root_path || !pwd_path) {
        vfs_put_current_fs(&root_path, &pwd_path);
        return -ENOMEM;
    }

    // 解析挂载点路径
    mountpoint = vfs_path_lookup(dir_name, LOOKUP_DIRECTORY, pwd_path);
    if (IS_ERR(mountpoint)) {
        err = PTR_ERR(mountpoint);
        goto out_put_fs;
    }

    mnt = mountpoint->mnt;
    sb = mnt->sb;

    // 如果挂载点标记为禁止用户卸载，且本次调用来自用户态，则拒绝
    if (!from_kernel && (mnt->flags & MS_NOUSER)) {
        err = -EPERM;
        goto out_put_mountpoint;
    }

    mutex_lock(&mount_tree.lock);

    // 检查是否有子挂载
    if (!list_empty(&mnt->mnt_mounts)) {
        err = -EBUSY;
        goto out_unlock;
    }

    // 从父挂载点的子链表中移除
    list_del(&mnt->list);

    // 清除挂载点 dentry 的标志
    spin_lock(&mountpoint->dentry->lock);
    mountpoint->dentry->flags &= ~DCACHE_MOUNTED;
    spin_unlock(&mountpoint->dentry->lock);

    mutex_unlock(&mount_tree.lock);

    // 减少超级块引用计数，若归零则销毁
    if (atomic_fetch_sub(&sb->count, 1) == 1) {
        if (sb->type->kill_sb)
            sb->type->kill_sb(sb);
    }

    // 减少 vfsmount 引用计数并释放
    vfs_mntput(mnt);

    // 释放传入的 path 引用
    vfs_path_put(mountpoint);

    vfs_put_current_fs(&root_path, &pwd_path);
    return 0;

out_unlock:
    mutex_unlock(&mount_tree.lock);
out_put_mountpoint:
    vfs_path_put(mountpoint);
out_put_fs:
    vfs_put_current_fs(&root_path, &pwd_path);
    return err;
}

// 获取根目录path
struct path *vfs_get_root_path(void) {
    vfs_path_get(vfs_root);
    return vfs_root;
}

// 获取 inode 的 rdev 字段
dev_t vfs_inode_get_rdev(struct inode *inode) {
    return inode->rdev;
}

// 获取 file 结构体的私有数据
void *vfs_file_get_private(struct file *file) {
    void *priv;
    spin_lock(&file->lock);
    priv = file->private_data;
    spin_unlock(&file->lock);
    return priv;
}

// 设置 file 结构体的私有数据
void vfs_file_set_private(struct file *file, void *priv) {
    spin_lock(&file->lock);
    file->private_data = priv;
    spin_unlock(&file->lock);
}

// 初始化vfs
bool vfs_init(void) {
    // 初始化用于缓存的哈希表
    if (vfs_dinit()) return false;
    if (vfs_iinit()) return false;

    // 初始化全局链表和锁
    INIT_LIST_HEAD(&dentry_lru_list.list);
    spinlock_init(&dentry_lru_list.lock);
    spinlock_init(&fs_list_head.lock);
    INIT_LIST_HEAD(&fs_list_head.list);

    // 初始化挂载树
    mutex_init(&mount_tree.lock);
    atomic_init(&mount_tree.count, 0);

    // 遍历所有文件系统类型并注册
    initcall(fs, 0);

    // 挂载根文件系统
    if (vfs_root_mount()) {
        VFS_WARN("root mount failed\n");
        return false;
    }

    // 挂载设备节点
    if (vfs_dev_mount()) {
        VFS_WARN("dev mount failed\n");
        return false;
    }

    VFS_PRINT("vfs init success\n");

    return true;
}

/**
 * 调整文件偏移
 *
 * @param file 打开的文件
 * @param offset 偏移量
 * @param whence 参照点
 *
 * @return 新的文件偏移
 */
off_t vfs_lseek(struct file *file, off_t offset, seek_whence_t whence) {
    off_t new_pos;

    // 把 SEEK_CUR 转换成 SEEK_SET
    if (whence == SEEK_CUR) {
        offset = file->pos + offset;
        whence = SEEK_SET;
    }

    if (!file->ops->llseek)
        return -ESPIPE;

    new_pos = file->ops->llseek(file, offset, whence);
    if (new_pos < 0)
        return new_pos;

    spin_lock(&file->lock);
    file->pos = new_pos;
    spin_unlock(&file->lock);

    return new_pos;
}

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
ssize_t vfs_read(struct file *file, char *buf, size_t count, off_t *pos) {
    off_t offset;
    ssize_t ret;

    if (!(file->mode & MAY_READ))
        return -EBADF;

    if (!file->ops->read)
        return -EINVAL;

    // 确定读取偏移：外部传入优先，否则用文件内部偏移
    if (pos) {
        offset = *pos;
    } else {
        offset = file->pos;
    }

    struct inode *inode = file->path.dentry->inode;
    mutex_lock(&inode->lock);
    ret = file->ops->read(file, buf, count, &offset);
    mutex_unlock(&inode->lock);
    if (ret > 0) {
        if (pos) {
            *pos = offset;
        } else {
            // 如果成功读取，更新 file 内部偏移
            spin_lock(&file->lock);
            file->pos = offset;
            spin_unlock(&file->lock);
        }
    }

    return ret;
}

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
ssize_t vfs_write(struct file *file, const char *buf, size_t count, off_t *pos) {
    off_t offset;
    ssize_t ret;

    if (!(file->mode & MAY_WRITE))
        return -EBADF;

    if (!file->ops->write)
        return -EINVAL;

    // 确定写入偏移：外部传入优先，否则用文件内部偏移
    if (pos) {
        offset = *pos;
    } else {
        offset = file->pos;
    }

    struct inode *inode = file->path.dentry->inode;
    mutex_lock(&inode->lock);
    if (file->flags & O_APPEND)
        offset = inode->size;
    ret = file->ops->write(file, buf, count, &offset);
    mutex_unlock(&inode->lock);
    if (ret > 0) {
        if (pos) {
            *pos = offset;
        } else {
            // 如果成功写入，更新 file 内部偏移
            spin_lock(&file->lock);
            file->pos = offset;
            spin_unlock(&file->lock);
        }
    }

    return ret;
}

/**
 * 截断文件
 *
 * @param file 打开的文件
 * @param length 目标长度
 */
int vfs_truncate(struct file *file, off_t length) {
    struct inode *inode = file->path.dentry->inode;
    struct iattr attr = { .ia_valid = ATTR_SIZE, .ia_size = length };

    if (!inode->ops->setattr)
        return -EINVAL;

    mutex_lock(&inode->lock);
    int ret = inode->ops->setattr(file->path.dentry, &attr);
    mutex_unlock(&inode->lock);
    return ret;
}

/**
 * 关闭文件
 *
 * @param file 要关闭的文件
 */
void vfs_close(struct file *file) {
    if (file->ops->release)
        file->ops->release(file->path.dentry->inode, file);

    // 引用计数归零时释放 file 结构体
    if (atomic_fetch_sub(&file->count, 1) == 1) {
        vfs_path_put(&file->path);
        kheap_free(file);
    }
}

/**
 * 创建目录
 *
 * @param path 路径字符串
 * @param mode 目录权限
 * @param pwd 当前工作目录
 */
int vfs_mkdir(const char *path, mode_t mode, const struct path *pwd) {
    struct path *parent_path = NULL;
    struct dentry *dentry = NULL;
    const char *name;
    size_t namelen;
    char *name_copy;
    int err;

    // 解析父目录路径
    parent_path = vfs_path_parent(path, pwd, &name, &namelen);
    if (IS_ERR(parent_path))
        return PTR_ERR(parent_path);

    // 权限检查
    err = vfs_inode_permission(parent_path->dentry->inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out;

    // 应用 umask 屏蔽权限位
    mode = mode & ~VFS_DEFAULT_UMASK;

    // 复制组件名（因为 vfs_dalloc 需要以 '\0' 结尾）
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out;
    }

    // 分配临时 dentry
    dentry = vfs_dalloc(parent_path->dentry, name_copy);
    if (!dentry) {
        kheap_free(name_copy);
        err = -ENOMEM;
        goto out;
    }

    // 获取父目录 inode 锁
    mutex_lock(&parent_path->dentry->inode->lock);

    // 检查是否已被并发创建
    if (vfs_file_exists(parent_path->dentry, name, namelen)) {
        vfs_dput(dentry);
        mutex_unlock(&parent_path->dentry->inode->lock);
        kheap_free(name_copy);
        err = -EEXIST;
        goto out;
    }

    // 调用对应文件系统的 mkdir
    err = parent_path->dentry->inode->ops->mkdir(parent_path->dentry->inode, dentry, mode);
    if (err == 0)
        vfs_dcache_add(dentry);
    mutex_unlock(&parent_path->dentry->inode->lock);

    vfs_dput(dentry);
    kheap_free(name_copy);
out:
    vfs_path_put(parent_path);
    return err;
}

/**
 * 删除空目录
 *
 * @param path 路径字符串
 * @param pwd 当前工作目录
 */
int vfs_rmdir(const char *path, const struct path *pwd) {
    struct path *target = NULL;
    struct inode *dir_inode, *child_inode;
    int err;

    // 解析目标路径
    target = vfs_path_lookup(path, LOOKUP_DIRECTORY, pwd);
    if (IS_ERR(target))
        return PTR_ERR(target);

    child_inode = target->dentry->inode;

    // 确保目标是目录
    if (!S_ISDIR(child_inode->mode)) {
        err = -ENOTDIR;
        goto out;
    }

    // 权限检查（需要父目录的写和执行权限）
    dir_inode = target->dentry->parent->inode;
    err = vfs_inode_permission(dir_inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out;

    if (dir_inode->ino < child_inode->ino) {
        mutex_lock(&dir_inode->lock);
        mutex_lock(&child_inode->lock);
    } else if (dir_inode->ino > child_inode->ino) {
        mutex_lock(&child_inode->lock);
        mutex_lock(&dir_inode->lock);
    } else {
        mutex_lock(&dir_inode->lock);
    }

    // 调用对应文件系统的 rmdir
    err = dir_inode->ops->rmdir(dir_inode, target->dentry);

    if (dir_inode->ino < child_inode->ino) {
        mutex_unlock(&child_inode->lock);
        mutex_unlock(&dir_inode->lock);
    } else if (dir_inode->ino > child_inode->ino) {
        mutex_unlock(&dir_inode->lock);
        mutex_unlock(&child_inode->lock);
    } else {
        mutex_unlock(&dir_inode->lock);
    }

out:
    vfs_path_put(target);
    return err;
}

/**
 * 删除文件
 *
 * @param path 路径字符串
 * @param pwd 当前工作目录
 */
int vfs_unlink(const char *path, const struct path *pwd) {
    struct path *parent_path = NULL;
    struct dentry *dentry = NULL;
    const char *name;
    size_t namelen;
    char *name_copy;
    int err;

    // 解析父目录路径和最后一个组件名
    parent_path = vfs_path_parent(path, pwd, &name, &namelen);
    if (IS_ERR(parent_path))
        return PTR_ERR(parent_path);

    // 权限检查（需要父目录的写和执行权限）
    err = vfs_inode_permission(parent_path->dentry->inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out_parent;

    // 复制组件名（因为 vfs_dalloc 需要以 '\0' 结尾）
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out_parent;
    }

    // 分配临时 dentry（负缓存）
    dentry = vfs_dalloc(parent_path->dentry, name_copy);
    kheap_free(name_copy);
    if (!dentry) {
        err = -ENOMEM;
        goto out_parent;
    }

    // 调用对应文件系统的 unlink
    mutex_lock(&parent_path->dentry->inode->lock);
    err = parent_path->dentry->inode->ops->unlink(parent_path->dentry->inode, dentry);
    mutex_unlock(&parent_path->dentry->inode->lock);
    vfs_dput(dentry);

out_parent:
    vfs_path_put(parent_path);
    return err;
}

/**
 * 创建符号链接
 *
 * @param target 链接目标字符串
 * @param linkpath 链接路径
 * @param pwd 当前工作目录
 */
int vfs_symlink(const char *target, const char *linkpath, const struct path *pwd) {
    struct path *parent_path = NULL;
    struct dentry *dentry = NULL;
    const char *name;
    size_t namelen;
    char *name_copy;
    int err;

    // 解析链接的父目录路径
    parent_path = vfs_path_parent(linkpath, pwd, &name, &namelen);
    if (IS_ERR(parent_path))
        return PTR_ERR(parent_path);

    // 权限检查
    err = vfs_inode_permission(parent_path->dentry->inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out;

    // 应用 umask 屏蔽权限位（符号链接默认权限为 0777）
    mode_t mode = 0777 & ~VFS_DEFAULT_UMASK;

    // 复制组件名
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out;
    }

    // 分配临时 dentry
    dentry = vfs_dalloc(parent_path->dentry, name_copy);
    if (!dentry) {
        kheap_free(name_copy);
        err = -ENOMEM;
        goto out;
    }

    // 获取父目录 inode 锁
    mutex_lock(&parent_path->dentry->inode->lock);

    // 检查是否已被并发创建
    if (vfs_file_exists(parent_path->dentry, name, namelen)) {
        vfs_dput(dentry);
        mutex_unlock(&parent_path->dentry->inode->lock);
        kheap_free(name_copy);
        err = -EEXIST;
        goto out;
    }

    // 调用对应文件系统的 symlink
    err = parent_path->dentry->inode->ops->symlink(parent_path->dentry->inode, dentry, target);
    if (err == 0)
        vfs_dcache_add(dentry);
    mutex_unlock(&parent_path->dentry->inode->lock);

    vfs_dput(dentry);
    kheap_free(name_copy);
out:
    vfs_path_put(parent_path);
    return err;
}

/**
 * 创建硬链接
 *
 * @param oldpath 现有文件路径
 * @param newpath 新链接路径
 * @param pwd 当前工作目录
 */
int vfs_link(const char *oldpath, const char *newpath, const struct path *pwd) {
    struct path *old_path = NULL, *new_parent_path = NULL;
    struct dentry *new_dentry = NULL;
    const char *name;
    size_t namelen;
    char *name_copy;
    int err;

    // 解析现有文件路径
    old_path = vfs_path_lookup(oldpath, LOOKUP_FOLLOW, pwd);
    if (IS_ERR(old_path))
        return PTR_ERR(old_path);

    // 确保不是目录（硬链接不允许对目录）
    if (S_ISDIR(old_path->dentry->inode->mode)) {
        err = -EPERM;
        goto out_old;
    }

    // 解析新链接的父目录路径
    new_parent_path = vfs_path_parent(newpath, pwd, &name, &namelen);
    if (IS_ERR(new_parent_path)) {
        err = PTR_ERR(new_parent_path);
        goto out_old;
    }

    // 权限检查（需要父目录的写和执行权限）
    err = vfs_inode_permission(new_parent_path->dentry->inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out_new_parent;

    // 复制组件名
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out_new_parent;
    }

    // 分配临时 dentry
    new_dentry = vfs_dalloc(new_parent_path->dentry, name_copy);
    if (!new_dentry) {
        kheap_free(name_copy);
        err = -ENOMEM;
        goto out_new_parent;
    }

    // 获取目标目录 inode 锁
    mutex_lock(&new_parent_path->dentry->inode->lock);

    // 检查目标是否已被并发创建
    if (vfs_file_exists(new_parent_path->dentry, name, namelen)) {
        vfs_dput(new_dentry);
        mutex_unlock(&new_parent_path->dentry->inode->lock);
        kheap_free(name_copy);
        err = -EEXIST;
        goto out_new_parent;
    }

    // 调用对应文件系统的 link
    err = new_parent_path->dentry->inode->ops->link(old_path->dentry, new_parent_path->dentry->inode, new_dentry);
    if (err == 0)
        vfs_dcache_add(new_dentry);
    mutex_unlock(&new_parent_path->dentry->inode->lock);

    vfs_dput(new_dentry);
    kheap_free(name_copy);
out_new_parent:
    vfs_path_put(new_parent_path);
out_old:
    vfs_path_put(old_path);
    return err;
}

/**
 * 重命名或移动文件/目录
 *
 * @param oldpath 源路径
 * @param newpath 目标路径
 */
int vfs_rename(const char *oldpath, const char *newpath, const struct path *pwd) {
    struct path *old_path = NULL, *new_path = NULL;
    struct path *new_parent_path = NULL;
    struct dentry *new_dentry = NULL;
    const char *name;
    size_t namelen;
    char *name_copy;
    int err;

    // 解析源路径
    old_path = vfs_path_lookup(oldpath, LOOKUP_FOLLOW, pwd);
    if (IS_ERR(old_path))
        return PTR_ERR(old_path);

    // 解析目标路径（可能已存在）
    new_path = vfs_path_lookup(newpath, LOOKUP_FOLLOW, pwd);
    if (IS_ERR(new_path)) {
        if (PTR_ERR(new_path) != -ENOENT) {
            err = PTR_ERR(new_path);
            goto out_old;
        }
        
        // 目标路径不存在，需要创建负缓存 dentry
        new_parent_path = vfs_path_parent(newpath, pwd, &name, &namelen);
        if (IS_ERR(new_parent_path)) {
            err = PTR_ERR(new_parent_path);
            goto out_old;
        }

        // 权限检查（父目录写和执行权限）
        err = vfs_inode_permission(new_parent_path->dentry->inode, MAY_WRITE | MAY_EXEC);
        if (err) {
            vfs_path_put(new_parent_path);
            goto out_old;
        }

        name_copy = strndup(name, namelen);
        if (!name_copy) {
            vfs_path_put(new_parent_path);
            err = -ENOMEM;
            goto out_old;
        }

        new_dentry = vfs_dalloc(new_parent_path->dentry, name_copy);
        kheap_free(name_copy);
        if (!new_dentry) {
            vfs_path_put(new_parent_path);
            err = -ENOMEM;
            goto out_old;
        }

        // 构造一个临时 path 结构体代表该负缓存 dentry
        new_path = kheap_alloc(sizeof(struct path));
        if (!new_path) {
            vfs_dput(new_dentry);
            vfs_path_put(new_parent_path);
            err = -ENOMEM;
            goto out_old;
        }

        new_path->mnt = new_parent_path->mnt;
        vfs_mntget(new_path->mnt);
        new_path->dentry = new_dentry;
        vfs_path_put(new_parent_path);
    }

    // 权限检查（需要源和目标父目录的写和执行权限）
    err = vfs_inode_permission(old_path->dentry->parent->inode, MAY_WRITE | MAY_EXEC);
    if (!err)
        err = vfs_inode_permission(new_path->dentry->parent->inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out_new;

    // 调用对应文件系统的 rename
    struct inode *old_dir_inode = old_path->dentry->parent->inode;
    struct inode *new_dir_inode = new_path->dentry->parent->inode;
    if (old_dir_inode->ino < new_dir_inode->ino) {
        mutex_lock(&old_dir_inode->lock);
        mutex_lock(&new_dir_inode->lock);
    } else if (old_dir_inode->ino > new_dir_inode->ino) {
        mutex_lock(&new_dir_inode->lock);
        mutex_lock(&old_dir_inode->lock);
    } else {
        mutex_lock(&old_dir_inode->lock);
    }
    err = old_dir_inode->ops->rename(old_dir_inode, old_path->dentry, new_dir_inode, new_path->dentry);
    if (old_dir_inode->ino < new_dir_inode->ino) {
        mutex_unlock(&new_dir_inode->lock);
        mutex_unlock(&old_dir_inode->lock);
    } else if (old_dir_inode->ino > new_dir_inode->ino) {
        mutex_unlock(&old_dir_inode->lock);
        mutex_unlock(&new_dir_inode->lock);
    } else {
        mutex_unlock(&old_dir_inode->lock);
    }

out_new:
    vfs_path_put(new_path);
out_old:
    vfs_path_put(old_path);
    return err;
}

/**
 * 获取文件属性
 *
 * @param path 路径字符串
 * @param stat 输出状态结构体
 * @param pwd 当前工作目录
 */
int vfs_getattr(const char *path, struct kstat *stat, const struct path *pwd) {
    struct path *ps = NULL;
    int err;

    // 解析路径
    ps = vfs_path_lookup(path, LOOKUP_FOLLOW, pwd);
    if (IS_ERR(ps))
        return PTR_ERR(ps);

    // 调用对应文件系统的 getattr
    err = ps->dentry->inode->ops->getattr(ps, stat);

    vfs_path_put(ps);
    return err;
}

/**
 * 设置文件属性
 *
 * @param path 路径字符串
 * @param attr 属性结构体
 * @param pwd 当前工作目录
 */
int vfs_setattr(const char *path, struct iattr *attr, const struct path *pwd) {
    struct path *ps = NULL;
    int err;

    // 解析路径
    ps = vfs_path_lookup(path, LOOKUP_FOLLOW, pwd);
    if (IS_ERR(ps))
        return PTR_ERR(ps);

    // 权限检查（修改属性通常需要写权限或自己是属主）
    err = vfs_inode_permission(ps->dentry->inode, MAY_WRITE);
    if (err)
        goto out;

    // 调用底层 setattr
    mutex_lock(&ps->dentry->inode->lock);
    err = ps->dentry->inode->ops->setattr(ps->dentry, attr);
    mutex_unlock(&ps->dentry->inode->lock);

out:
    vfs_path_put(ps);
    return err;
}

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
struct file *vfs_open(const char *path, open_flags_t flags, mode_t mode, const struct path *pwd) {
    struct path *ps = NULL;
    struct dentry *dentry = NULL;
    struct file *file = NULL;
    int err = 0;
    int perm_mask;

    // 根据 O_NOFOLLOW 决定是否跟随符号链接
    vfs_lookup_flags_t lookup_flags = (flags & O_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;

    // 首先尝试直接查找路径
    ps = vfs_path_lookup(path, lookup_flags, pwd);
    if (!IS_ERR(ps)) {
        // 路径已存在，如果指定了 O_EXCL，返回 -EEXIST
        if (flags & O_EXCL) {
            vfs_path_put(ps);
            return ERR_PTR(-EEXIST);
        }
        // 计算权限掩码并直接打开
        struct inode *inode = ps->dentry->inode;
        int acc = flags & O_RDWR;
        perm_mask = 0;
        if (acc != O_WRONLY) perm_mask |= MAY_READ;
        if (acc != O_RDONLY) perm_mask |= MAY_WRITE;
        return vfs_do_open(ps, flags, perm_mask);
    }

    // 路径不存在：如果不是 O_CREAT，直接返回错误
    if (PTR_ERR(ps) != -ENOENT || !(flags & O_CREAT))
        return ERR_CAST(ps);

    // O_CREAT 且文件不存在，准备创建
    char *path_copy = strdup(path);
    if (!path_copy) {
        err = -ENOMEM;
        goto out_err;
    }

    char *last_slash = strrchr(path_copy, '/');
    char *filename;
    const char *parent_path_str;

    if (last_slash) {
        *last_slash = '\0';
        parent_path_str = path_copy;
        filename = last_slash + 1;
    } else {
        parent_path_str = ".";
        filename = path_copy;
    }

    // 解析父目录
    struct path *parent_ps = vfs_path_lookup(parent_path_str, lookup_flags, pwd);
    if (IS_ERR(parent_ps)) {
        err = PTR_ERR(parent_ps);
        goto err_free_copy;
    }

    // 分配一个临时 dentry
    dentry = vfs_dalloc(parent_ps->dentry, filename);
    if (!dentry) {
        err = -ENOMEM;
        goto err_put_parent;
    }

    // 计算权限掩码
    int acc = flags & O_RDWR;
    perm_mask = 0;
    if (acc != O_WRONLY) perm_mask |= MAY_READ;
    if (acc != O_RDONLY) perm_mask |= MAY_WRITE;

    // 获取父目录 inode 锁
    mutex_lock(&parent_ps->dentry->inode->lock);

    // 重新检查目标是否已被并发创建
    if (vfs_file_exists(parent_ps->dentry, filename, strlen(filename))) {
        // 文件已存在
        vfs_dput(dentry);
        mutex_unlock(&parent_ps->dentry->inode->lock);

        if (flags & O_EXCL) {
            err = -EEXIST;
            goto err_put_parent;
        }

        // 复用已存在的 dentry 构造 path 并打开
        dentry = vfs_dalloc(parent_ps->dentry, filename);
        if (!dentry) {
            err = -ENOMEM;
            goto err_put_parent;
        }

        ps = kheap_alloc(sizeof(struct path));
        if (!ps) {
            vfs_dput(dentry);
            err = -ENOMEM;
            goto err_put_parent;
        }
        ps->mnt = parent_ps->mnt;
        vfs_mntget(ps->mnt);
        ps->dentry = dentry;

        vfs_path_put(parent_ps);
        kheap_free(path_copy);
        return vfs_do_open(ps, flags, perm_mask);
    }

    // 目标确实不存在，创建新文件
    mode_t effective_mode = mode & ~VFS_DEFAULT_UMASK;
    err = vfs_create(parent_ps->dentry->inode, dentry, effective_mode);
    if (err == 0)
        vfs_dcache_add(dentry);
    mutex_unlock(&parent_ps->dentry->inode->lock);

    if (err)
        goto err_free_dentry;

    // 创建成功，构造 path 对象
    ps = kheap_alloc(sizeof(struct path));
    if (!ps) {
        err = -ENOMEM;
        goto err_free_dentry;
    }

    ps->mnt = parent_ps->mnt;
    vfs_mntget(ps->mnt);
    ps->dentry = dentry;

    vfs_path_put(parent_ps);
    kheap_free(path_copy);

    return vfs_do_open(ps, flags, perm_mask);

err_free_dentry:
    vfs_dput(dentry);
err_put_parent:
    vfs_path_put(parent_ps);
err_free_copy:
    kheap_free(path_copy);
out_err:
    return ERR_PTR(err);
}

/**
 * 创建设备节点
 *
 * @param path 路径字符串
 * @param mode 文件类型和权限（必须包含 S_IFCHR 或 S_IFBLK）
 * @param dev 设备号
 * @param pwd 当前工作目录
 */
int vfs_mknod(const char *path, mode_t mode, dev_t dev, const struct path *pwd) {
    struct path *parent_path = NULL;
    struct dentry *dentry = NULL;
    const char *name;
    size_t namelen;
    char *name_copy;
    int err;

    if (!path || !pwd)
        return -EINVAL;

    // 模式必须包含设备类型
    if (!S_ISCHR(mode) && !S_ISBLK(mode))
        return -EINVAL;

    // 解析父目录路径和最后一个组件名
    parent_path = vfs_path_parent(path, pwd, &name, &namelen);
    if (IS_ERR(parent_path))
        return PTR_ERR(parent_path);

    // 权限检查
    err = vfs_inode_permission(parent_path->dentry->inode, MAY_WRITE | MAY_EXEC);
    if (err)
        goto out_parent;

    // 只有 root 用户可以创建设备节点
    uid_t uid;
    gid_t gid;
    task_get_current_ugid(&uid, &gid);
    if (uid != 0) {
        err = -EPERM;
        goto out_parent;
    }

    // 应用 umask 屏蔽权限位
    mode = mode & ~VFS_DEFAULT_UMASK;

    // 复制组件名（因为 vfs_dalloc 需要以 '\0' 结尾）
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out_parent;
    }

    // 分配临时 dentry
    dentry = vfs_dalloc(parent_path->dentry, name_copy);
    if (!dentry) {
        kheap_free(name_copy);
        err = -ENOMEM;
        goto out_parent;
    }

    // 获取父目录 inode 锁
    mutex_lock(&parent_path->dentry->inode->lock);

    // 检查是否已被并发创建
    if (vfs_file_exists(parent_path->dentry, name, namelen)) {
        vfs_dput(dentry);
        mutex_unlock(&parent_path->dentry->inode->lock);
        kheap_free(name_copy);
        err = -EEXIST;
        goto out_parent;
    }

    // 调用文件系统的 mknod 回调
    err = parent_path->dentry->inode->ops->mknod(parent_path->dentry->inode, dentry, mode, dev);
    if (err == 0)
        vfs_dcache_add(dentry);
    mutex_unlock(&parent_path->dentry->inode->lock);
    if (err) {
        vfs_dput(dentry);
        kheap_free(name_copy);
        goto out_parent;
    }

    vfs_dput(dentry);
    kheap_free(name_copy);
    vfs_path_put(parent_path);
    return 0;

out_parent:
    vfs_path_put(parent_path);
    return err;
}