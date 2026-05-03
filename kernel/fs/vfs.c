/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdbool.h>
#include <asm/serial.h>
#include <heap.h>
#include <fs/vfs.h>
#include <initcall.h>
#include <klibc.h>

#define MAX_SYMLINKS 8

#define VFS_HASH_MIN 256
#define VFS_HASH_MAX (1 << 18)

#define VFS_PRINT(fmt, ...) \
    printk("[VFS] " fmt, ##__VA_ARGS__)

// 文件系统锁链表头，用于管理已注册的文件系统
static struct vfs_lock_list fs_list_head = {0};

// dentry LRU 链表头
static struct vfs_lock_list dentry_lru_list = {0};   

// 缓存的哈希表
static struct vfs_hash_table inode_cache = {0};   
static struct vfs_hash_table dentry_cache = {0};

// 用于挂载文件系统
static struct {
    struct vfsmount * __rcu root;  // 根挂载点
    atomic_uint count;  // 挂载的文件系统数量
    mutex_t lock;   // 保护挂载树修改的互斥锁
} mount_tree = {0};

static struct path * __rcu vfs_root = NULL;   // 根路径对象

static uint64_t vfs_get_hash_size(uint64_t obj_size, uint64_t scale);
static inline void vfs_dget(struct dentry *dentry);
static inline struct dentry *vfs_dget_rcu(struct dentry *dentry);

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

    spinlock_init(&cache->lock);

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

    spinlock_init(&cache->lock);

    return 0;
}

// 获取vfs哈希表的大小
static uint64_t vfs_get_hash_size(uint64_t obj_size, uint64_t scale) {
    uint64_t total_pages = kheap_max_page();
    uint64_t objs_per_page = PAGE_SIZE / obj_size;
    uint64_t max_objs = total_pages * objs_per_page;
    uint64_t buckets = max_objs / scale;

    if (buckets < VFS_HASH_MIN)
        buckets = VFS_HASH_MIN;
    if (buckets > VFS_HASH_MAX)
        buckets = VFS_HASH_MAX;

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
    struct dentry *dentry = kheap_alloc(sizeof(struct dentry));
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
    INIT_LIST_HEAD(&dentry->rcu.node);
    dentry->key.parent = parent;
    dentry->key.name = dentry->name;

    if (parent) {
        // 将新 dentry 添加到父目录的子链表中
        spin_lock(&parent->lock);
        list_add_tail(&dentry->child, &parent->subdirs);
        spin_unlock(&parent->lock);
    }

    // 插入 dentry 哈希表
    vfs_dcache_add(dentry);

    return dentry;
}

// 读取符号链接目标
static ssize_t vfs_readlink(struct dentry *dentry, char *buf, size_t bufsiz) {
    if (!dentry->inode || !S_ISLNK(dentry->inode->mode))
        return -EINVAL;

    if (!dentry->inode->ops->readlink)
        return -ENOSYS;

    return dentry->inode->ops->readlink(dentry->inode, buf, bufsiz);
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

// 增加 dentry 引用计数
static inline void vfs_dget(struct dentry *dentry) {
    uint64_t old = atomic_fetch_add(&dentry->count, 1);
    if (!old) {
        vfs_dlru_del(dentry);
    }
}

// 增加 dentry 引用计数(rcu 版本,调用者需要持有 rcu 读锁)
static inline struct dentry *vfs_dget_rcu(struct dentry *dentry) {
    if (atomic_fetch_add(&dentry->count, 1) == 0) {
        atomic_fetch_sub(&dentry->count, 1);
        return NULL;
    }

    return dentry;
}

// 减少 dentry 引用计数
static inline void vfs_dput(struct dentry *dentry) {
    if (atomic_fetch_sub(&dentry->count, 1) == 1) 
        vfs_dlru_add(dentry);
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

// 分配解析字符串的句柄
static vfs_path_prs_t *vfs_path_prs_alloc(const char *path) {
    if (!path) return ERR_PTR(-EINVAL);

    vfs_path_prs_t *prs = kheap_alloc(sizeof(vfs_path_prs_t));
    if (!prs) return ERR_PTR(-ENOMEM);

    // 复制路径字符串，确保每个 path 的引用者只有一个
    prs->path = strdup(path);
    if (!prs->path) {
        kheap_free(prs);
        return ERR_PTR(-ENOMEM);
    }

    // 从路径开头开始解析
    prs->offset = 0;

    prs->curr_path = NULL;

    return prs;
}

// 释放解析句柄
static void vfs_path_prs_free(vfs_path_prs_t *prs) {
    if (!prs) return;

    // 释放路径字符串
    kheap_free((void *)prs->path);
    
    // 释放当前路径对象
    if (prs->curr_path) {
        vfs_path_put(prs->curr_path);
        kheap_free(prs->curr_path);
    }
    
    kheap_free(prs);
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
    vfs_path_prs_t *cur;
    vfs_path_prs_t *stack[MAX_SYMLINKS];
    int depth = 0;
    int err = 0;
    struct path *res = NULL;

    // 获取句柄用于解析路径字符串
    cur = vfs_path_prs_alloc(path);
    if (IS_ERR(cur))
        return ERR_CAST(cur);

    // 解析每一个组件
    while (1) {
        struct path *p = vfs_path_prs_next(cur, pwd);
        if (IS_ERR(p)) {
            // 解析错误
            err = PTR_ERR(p);
            break;
        }

        // 所有组件解析完成
        if (p == NULL) {
            if (!depth) {
                // 是负缓存，返回没有找到文件或目录
                if (!cur->curr_path->dentry->inode) {
                    err = -ENOENT;
                    break;
                }

                // 没有符号链接，直接返回结果
                if (
                    (flags & LOOKUP_DIRECTORY) &&
                    !S_ISDIR(cur->curr_path->dentry->inode->mode)
                ) {
                    // 不是目录但要求是目录，返回错误
                    err = -ENOTDIR;
                    break;
                }

                // 解析成功，返回结果
                res = cur->curr_path;
                cur->curr_path = NULL;
                vfs_path_prs_free(cur);
                return res;
            } else {
                /*
                 * 我们已经完成了符号链接的展开
                 * 此时 cur->curr_path 就是最终路径
                 * 直接返回结果
                 * 并清理栈中所有残留的解析器 
                 */
                res = cur->curr_path;
                cur->curr_path = NULL;
                vfs_path_prs_free(cur);

                while (depth > 0)
                    vfs_path_prs_free(stack[--depth]);

                return res;
            }
        }

        if (p->dentry->flags & DCACHE_MOUNTED) {
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

            vfs_path_put(cur->curr_path);
            kheap_free(cur->curr_path);
            cur->curr_path = new_cur;
            continue;
        }

        // 检查是否是符号链接
        if (S_ISLNK(p->dentry->inode->mode) && (flags & LOOKUP_FOLLOW)) {
            if (depth >= MAX_SYMLINKS) {
                // 符号链接过多，可能存在循环，返回错误
                err = -ELOOP;
                break;
            }

            stack[depth++] = cur;

            // 读取符号链接的目标路径
            char target[PATH_MAX];
            ssize_t ret = vfs_readlink(p->dentry, target, sizeof(target) - 1);
            if (ret < 0) {
                err = ret;
                break;
            }

            target[ret] = '\0';

            // 复制目标路径字符串，确保每个解析句柄有独立的字符串
            char *dup = strdup(target);
            if (!dup) {
                err = -ENOMEM;
                break;
            }

            // 为新的路径解析分配一个句柄
            vfs_path_prs_t *new_prs = vfs_path_prs_alloc(dup);
            if (IS_ERR(new_prs)) {
                kheap_free(dup);
                err = PTR_ERR(new_prs);
                break;
            }

            const struct path *base = (target[0] == '/') ? vfs_root : cur->curr_path;

            // 初始化新句柄并解析第一个组件
            struct path *first = vfs_path_prs_next(new_prs, base);
            if (IS_ERR(first)) {
                // 解析符号链接目标失败，清理资源并返回错误
                vfs_path_prs_free(new_prs);
                err = PTR_ERR(first);
                break;
            }

            // 解析成功，切换到新的解析句柄继续处理剩余组件
            cur = new_prs;
            continue;
        }

        // 普通组件，不需要处理，直接继续解析下一个组件
    }

    // 清理资源
    if (cur)
        vfs_path_prs_free(cur);

    // 释放所有符号链接解析句柄
    while (depth > 0)
        vfs_path_prs_free(stack[--depth]);

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

    // 查找 tmpfs 文件系统类型
    tmpfs = vfs_find_filesystem("tmpfs");
    if (!tmpfs)
        return -ENODEV;

    // 调用 tmpfs 的 mount 回调，获取根 dentry
    root_dentry = tmpfs->mount(tmpfs, MS_NONE, NULL, NULL);
    if (IS_ERR(root_dentry))
        return PTR_ERR(root_dentry);

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

    // 根据 whence 计算新的文件偏移
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

    // 不允许负偏移
    if (new_pos < 0)
        return -EINVAL;

    spin_lock(&file->lock);
    file->pos = new_pos;
    spin_unlock(&file->lock);

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
    spin_lock(&inode_cache.lock);
    hash_add(&inode_cache.hash, &inode->hash, &inode->key);
    spin_unlock(&inode_cache.lock);
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
    struct inode *inode;

    rcu_read_lock();

    node = hash_lookup_rcu(&inode_cache.hash, &key, vfs_inode_get_key);
    if (node) {
        inode = hlist_entry(node, struct inode, hash);
        atomic_fetch_add(&inode->count, 1);
        rcu_read_unlock();
        return inode;
    }

    rcu_read_unlock();
    return NULL;
}

// 将 dentry 添加到 dentry 缓存哈希表
void vfs_dcache_add(struct dentry *dentry) {
    spin_lock(&dentry_cache.lock);
    hash_add(&dentry_cache.hash, &dentry->hash_node, &dentry->key);
    spin_unlock(&dentry_cache.lock);
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

    rcu_read_lock();

    node = hash_lookup_rcu(&dentry_cache.hash, &key, vfs_dentry_get_key);
    if (node) {
        dentry = hlist_entry(node, struct dentry, hash_node);
        if (vfs_dget_rcu(dentry)) {
            rcu_read_unlock();
            return dentry;
        }
    }
    
    rcu_read_unlock();
    return NULL;
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
) {
    struct path *mountpoint = NULL;
    struct vfsmount *mnt = NULL;
    struct dentry *root_dentry = NULL;
    struct file_system_type *fs = NULL;
    struct path *root_path = NULL;
    struct path *pwd_path = NULL;
    int err;

    // 校验 dir_name 非空
    if (!dir_name) return ERR_PTR(-EINVAL);

    // 获取当前任务的文件系统上下文
    vfs_get_current_fs(&root_path, &pwd_path);
    if (!root_path || !pwd_path) {
        vfs_put_current_fs(&root_path, &pwd_path);
        return ERR_PTR(-ENOMEM);
    }

    // 解析挂载点路径
    mountpoint = vfs_path_lookup(dir_name, LOOKUP_DIRECTORY, pwd_path);
    if (IS_ERR(mountpoint)) {
        err = PTR_ERR(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return ERR_PTR(err);
    }

    // 普通挂载：需要文件系统类型
    if (!type) {
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return ERR_PTR(-EINVAL);
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
        return ERR_PTR(-ENODEV);
    }

    // 调用文件系统的 mount 回调，获取根 dentry
    root_dentry = fs->mount(fs, flags, dev_name, data);
    if (IS_ERR(root_dentry)) {
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return ERR_CAST(root_dentry);
    }

    // 分配 vfsmount 结构体
    mnt = kheap_alloc(sizeof(struct vfsmount));
    if (!mnt) {
        vfs_dput(root_dentry);
        vfs_path_put(mountpoint);
        vfs_put_current_fs(&root_path, &pwd_path);
        return ERR_PTR(-ENOMEM);
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
    return mnt;
}

// 获取根目录path
struct path *vfs_get_root_path(void) {
    vfs_path_get(vfs_root);
    return vfs_root;
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
    if (vfs_root_mount()) return false;

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
    struct inode *inode = file->path.dentry->inode;
    off_t new_pos;

    // 根据参照点计算新位置
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

    // 偏移不能为负
    if (new_pos < 0)
        return -EINVAL;

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

    ret = file->ops->read(file, buf, count, &offset);
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

    ret = file->ops->write(file, buf, count, &offset);
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

    return inode->ops->setattr(file->path.dentry, &attr);
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

    // 复制组件名（因为 vfs_dalloc 需要以 '\0' 结尾）
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out;
    }

    // 分配临时 dentry
    dentry = vfs_dalloc(parent_path->dentry, name_copy);
    kheap_free(name_copy);
    if (!dentry) {
        err = -ENOMEM;
        goto out;
    }

    // 调用对应文件系统的 mkdir
    err = parent_path->dentry->inode->ops->mkdir(parent_path->dentry->inode, dentry, mode);

    vfs_dput(dentry);
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

    // 调用对应文件系统的 rmdir
    err = dir_inode->ops->rmdir(dir_inode, target->dentry);

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
    err = parent_path->dentry->inode->ops->unlink(parent_path->dentry->inode, dentry);
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

    // 复制组件名
    name_copy = strndup(name, namelen);
    if (!name_copy) {
        err = -ENOMEM;
        goto out;
    }

    // 分配临时 dentry
    dentry = vfs_dalloc(parent_path->dentry, name_copy);
    kheap_free(name_copy);
    if (!dentry) {
        err = -ENOMEM;
        goto out;
    }

    // 调用对应文件系统的 symlink
    err = parent_path->dentry->inode->ops->symlink(parent_path->dentry->inode, dentry, target);

    vfs_dput(dentry);
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

    // 确保不是目录（硬链接通常不允许对目录）
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
    kheap_free(name_copy);
    if (!new_dentry) {
        err = -ENOMEM;
        goto out_new_parent;
    }

    // 调用对应文件系统的 link
    err = new_parent_path->dentry->inode->ops->link(old_path->dentry, new_parent_path->dentry->inode, new_dentry);

    vfs_dput(new_dentry);
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
    err = old_path->dentry->parent->inode->ops->rename(
        old_path->dentry->parent->inode, old_path->dentry,
        new_path->dentry->parent->inode, new_path->dentry);

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
    err = ps->dentry->inode->ops->setattr(ps->dentry, attr);

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

    // 首先尝试直接查找路径
    ps = vfs_path_lookup(path, LOOKUP_FOLLOW, pwd);
    if (IS_ERR(ps)) {
        // 如果错误不是 ENOENT 或者没有指定 O_CREAT，则直接返回错误
        if (PTR_ERR(ps) != -ENOENT || !(flags & O_CREAT))
            return ERR_CAST(ps);

        // O_CREAT 且文件不存在，准备创建
        char *path_copy = strdup(path);
        if (!path_copy) {
            err = -ENOMEM;
            goto out_err_ps;
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
        struct path *parent_ps = vfs_path_lookup(parent_path_str, LOOKUP_FOLLOW, pwd);
        if (IS_ERR(parent_ps)) {
            err = PTR_ERR(parent_ps);
            kheap_free(path_copy);
            goto out_err_ps;
        }

        // 分配一个临时 dentry
        dentry = vfs_dalloc(parent_ps->dentry, filename);
        if (!dentry) {
            err = -ENOMEM;
            vfs_path_put(parent_ps);
            kheap_free(path_copy);
            goto out_err_ps;
        }

        // 在父目录中创建新文件
        err = vfs_create(parent_ps->dentry->inode, dentry, mode);
        vfs_path_put(parent_ps);
        kheap_free(path_copy);
        if (err) {
            vfs_dput(dentry);
            goto out_err_ps;
        }

        // 创建成功，构造一个 path 对象代表这个新文件
        ps = kheap_alloc(sizeof(struct path));
        if (!ps) {
            err = -ENOMEM;
            vfs_dput(dentry);
            goto out_err_ps;
        }

        // 新文件与父目录在同一挂载点
        ps->mnt = parent_ps->mnt;
        vfs_mntget(ps->mnt);
        ps->dentry = dentry;
    }

    struct inode *inode = ps->dentry->inode;

    // 根据打开标志计算权限掩码
    int acc = flags & O_RDWR;
    perm_mask = 0;
    if (acc != O_WRONLY)
        perm_mask |= MAY_READ;

    if (acc != O_RDONLY)
        perm_mask |= MAY_WRITE;

    // 检查权限
    err = vfs_inode_permission(inode, perm_mask);
    if (err)
        goto out_free_ps;

    // 如果要求写操作或截断，且文件系统以只读挂载，则拒绝
    if ((perm_mask & MAY_WRITE) || (flags & O_TRUNC)) {
        if (ps->mnt->flags & MS_RDONLY) {
            err = -EROFS;
            goto out_free_ps;
        }
    }

    // 分配文件描述符
    file = kheap_alloc(sizeof(struct file));
    if (!file) {
        err = -ENOMEM;
        goto out_free_ps;
    }

    file->path = *ps;
    vfs_path_get(&file->path);  // 增加对路径的引用，文件结构体持有引用
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

    // 成功，释放临时查询的路径对象
    vfs_path_put(ps);
    return file;

out_free_file:
    vfs_path_put(&file->path);
    kheap_free(file);
out_free_ps:
    vfs_path_put(ps);
out_err_ps:
    return ERR_PTR(err);
}

