/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <drivers.h>
#include <stdatomic.h>
#include <kio.h>
#include <bitmap.h>
#include <dynarr.h>
#include <spinlock.h>
#include <klibc.h>
#include <heap.h>
#include <initcall.h>
#include <task.h>
#include <drivers/drivers.h>

#define DRIVERS_PRINT(fmt, ...) \
    printk("[DRIVERS] " fmt, ##__VA_ARGS__)

#define DRIVERS_WARN(fmt, ...) \
    printk("[DRIVERS] WARN: " fmt, ##__VA_ARGS__)

#define BUS_INFO_PRINT(name) \
    DRIVERS_PRINT("register bus : [\"name\" = \"%s\"]\n", name)

#define DRIVER_INFO_PRINT(name, bus) \
    DRIVERS_PRINT("register driver : [\"name\" = \"%s\", \"bus\" = \"%s\"]\n", name, bus)

#define DEVICE_INFO_PRINT(_name, _bus, _parent) \
    DRIVERS_PRINT("add device : [\"name\" = \"%s\", \"bus\" = \"%s\", \"parent\" = \"%s\"]\n", \
                  _name, _bus, (_parent) ? (_parent)->name : "null")

// 次设备号分配的最大值
#define DEVT_BIT_WIDTH 20
#define DEVT_MAX_COUNT ((1ULL << DEVT_BIT_WIDTH) - 1) 

// 用于匿名设备的主设备号
#define ANON_DEV 0

// 主设备号
#define DEV_MAIN_BIT_WIDTH (32 - DEVT_BIT_WIDTH)
#define DEV_MAX_MAJOR ((1ULL << DEV_MAIN_BIT_WIDTH) - 1)

// 设备号打包
#define MKDEV(ma, mi) (((ma) << DEVT_BIT_WIDTH) | (mi))

// 设备匹配最大重试次数
#define MAX_RETRY 10   

/*
 * 设备号分配器句柄
 * 用于管理一个主设备号下的次设备号分配
 */
typedef struct drivers_minor_devt {
    unsigned int major; // 主设备号
    bool dynamic;   // true: 动态, false: 静态
    spinlock_t lock;    // 保护次设备号分配和释放
    union {
        dynarr_t *bitmap;   // dynamic == true 时使用
        uint64_t counter;   // dynamic == false 时使用
    };
} drivers_minor_devt;

// probe 处理节点
struct probe_process_node {
    struct device *dev;  // 待处理设备
    struct driver *drv; // 待处理驱动
    struct list_head node;  // 挂入 probe_process_list
    uint16_t retry_count; // 尝试次数，超过 MAX_RETRY 后放弃匹配
}; 

// 匿名设备号分配器状态
static struct {
    dynarr_t *bitmap;
    spinlock_t lock;
} anon_state = {
    .bitmap = NULL,
    .lock = SPIN_LOCK_INIT
};

// 主设备号分配器
static struct {
    dynarr_t *bitmap;
    spinlock_t lock;
} major_state = {
    .bitmap = NULL,
    .lock = SPIN_LOCK_INIT
};

// 用于处理驱动的 probe 调用
static struct {
    struct list_head curr; // 当前正在处理的节点
    struct list_head wait; // 等待加入 curr 的节点 
    spinlock_t curr_trylock;    // 确保处理 curr 的 worker 项只有一个
    spinlock_t wait_lock;    // 保护 wait 链表
} probe_process_list = {0};

// 设备树根节点，用于挂载bus
static struct { 
    struct list_head root;
    spinlock_t lock;    
} device_tree = {0};

static struct {
    struct list_head head;   
    spinlock_t lock;        
} bus_list = {0};

// 待处理设备链表
static struct {
    struct list_head head;
    spinlock_t lock;
} devices_unmatched_list = {0};

// fops 表，按主设备号索引，存储 struct file_operations *
static dynarr_t *fops_table = NULL;

// 设备资源释放
static void device_release(struct device *dev) {
    spin_lock(&devices_unmatched_list.lock);
    if (!list_empty(&dev->unmatched_node))
        list_del_init(&dev->unmatched_node);    // 从未匹配设备列表中移除

    spin_unlock(&devices_unmatched_list.lock);

    // 调用总线提供的释放回调，释放私有资源
    if (dev->bus && dev->bus->free_device)
        dev->bus->free_device(dev);

    // 释放设备自身内存
    kheap_free(dev);
}

// 探测设备处理
static void drivers_probe_process(void *arg) {
    (void)arg;

    // 尝试获取处理权，确保只有一个实例在运行
    if (!spin_trylock(&probe_process_list.curr_trylock))
        return;

    bool has_new = true;

    /*
     * 循环直到 wait 队列为空且没有新节点加入 
     * 全程持有 curr_trylock
     * 防止多 worker 同时处理 curr 队列
     */
    while (has_new) {
        // 将 wait 队列中的所有节点移动到 curr 队列
        spin_lock(&probe_process_list.wait_lock);
        list_splice_tail_init(&probe_process_list.wait, &probe_process_list.curr);
        spin_unlock(&probe_process_list.wait_lock);

        // 处理 curr 队列中的每个节点
        while (!list_empty(&probe_process_list.curr)) {
            struct probe_process_node *node = list_first_entry(
                &probe_process_list.curr,
                struct probe_process_node,
                node
            );

            list_del_init(&node->node);

            struct driver *drv = node->drv;
            struct device *dev = node->dev;

            // 调用驱动 probe 函数
            int ret = drv->probe(dev);
            if (ret == -EPROBE_DEFER && node->retry_count < MAX_RETRY) {
                node->retry_count++;
                spin_lock(&probe_process_list.wait_lock);
                list_add_tail(&node->node, &probe_process_list.wait);
                spin_unlock(&probe_process_list.wait_lock);

                // 继续持有设备的引用，等待下次重试
                continue;
            }

            // 释放节点持有的设备引用
            device_ref_put(dev);

            // 销毁节点
            kheap_free(node);
        }

        // 检查 wait 队列是否又有了新节点（在 probe 过程中被添加）
        spin_lock(&probe_process_list.wait_lock);
        has_new = !list_empty(&probe_process_list.wait);
        spin_unlock(&probe_process_list.wait_lock);
    }

    spin_unlock(&probe_process_list.curr_trylock);
}

// 初始化匿名设备号位图
static bool drivers_anon_init(void) {
    anon_state.bitmap = dynarr_bitmap_create(DEVT_MAX_COUNT);
    if (!anon_state.bitmap)
        return false;

    // 保留 0 号设备号
    uint32_t dummy;
    dynarr_bitmap_alloc(anon_state.bitmap, 1, &dummy);
    return true;
}

// 初始化主设备号分配器
static bool drivers_major_init(void) {
    major_state.bitmap = dynarr_bitmap_create(1 << DEV_MAIN_BIT_WIDTH);
    if (!major_state.bitmap)
        return false;

    return true;
}

// 驱动框架数据初始化
bool drivers_data_init(void) {
    // 初始化全局总线链表
    INIT_LIST_HEAD(&bus_list.head);
    spinlock_init(&bus_list.lock);

    // 初始化未匹配设备列表
    INIT_LIST_HEAD(&devices_unmatched_list.head);
    spinlock_init(&devices_unmatched_list.lock);

    // 初始化 probe 处理队列
    INIT_LIST_HEAD(&probe_process_list.curr);
    INIT_LIST_HEAD(&probe_process_list.wait);
    spinlock_init(&probe_process_list.curr_trylock);
    spinlock_init(&probe_process_list.wait_lock);

    // 初始化设备树根节点锁和链表
    INIT_LIST_HEAD(&device_tree.root);
    spinlock_init(&device_tree.lock);

    // 初始化匿名设备号分配器
    anon_state.bitmap = dynarr_bitmap_create(DEVT_MAX_COUNT);
    if (!anon_state.bitmap) {
        DRIVERS_WARN("anon_state bitmap create failed");
        return false;
    }

    // 保留 0 号设备号
    uint32_t dummy;
    dynarr_bitmap_alloc(anon_state.bitmap, 1, &dummy);
    spinlock_init(&anon_state.lock);

    // 初始化主设备号分配器
    major_state.bitmap = dynarr_bitmap_create(1 << DEV_MAIN_BIT_WIDTH);
    if (!major_state.bitmap) {
        DRIVERS_WARN("major_state bitmap create failed");
        return false;
    }
    spinlock_init(&major_state.lock);

    // 创建 fops 表
    fops_table = dynarr_create(sizeof(struct file_operations *), DEV_MAX_MAJOR + 1);
    if (!fops_table) {
        DRIVERS_WARN("fops_table create failed");
        return false;
    }

    DRIVERS_PRINT("drivers data init success\n");
    return true;
}

// 驱动框架初始化
bool drivers_init(void) {
    initcall(drivers, 0);

    // 处理驱动的 probe
    drivers_probe_process(NULL);

    DRIVERS_PRINT("drivers init success\n");
    return true;
}

// 用于内核模块的 probe 处理入口
void drivers_probe_process_kmodule(void) {
    task_submit_work(drivers_probe_process, NULL);
}

// 生成一个新的匿名对象 ID
int drivers_get_anon_id(dev_t *dev) {
    if (!anon_state.bitmap)
        return -ENODEV;

    spin_lock(&anon_state.lock);

    uint32_t idx;
    if (!dynarr_bitmap_alloc(anon_state.bitmap, 1, &idx)) {
        spin_unlock(&anon_state.lock);
        return -ENOSPC;
    }

    spin_unlock(&anon_state.lock);

    *dev = MKDEV(ANON_DEV, idx);
    return 0;
}

// 释放一个匿名对象 ID
void drivers_free_anon_id(dev_t dev) {
    uint32_t mi = dev & DEVT_MAX_COUNT;
    if (!mi) return;

    spin_lock(&anon_state.lock);
    dynarr_bitmap_free(anon_state.bitmap, mi);
    spin_unlock(&anon_state.lock);
}

// 查找对应驱动的文件操作表
struct file_operations *drivers_dev_find(dev_t dev, mode_t mode) {
    unsigned int major = dev >> DEVT_BIT_WIDTH;   // 提取主设备号
    if (!fops_table || major > DEV_MAX_MAJOR)
        return NULL;

    struct file_operations **fops_ptr = dynarr_get(fops_table, major);
    if (fops_ptr)
        return *fops_ptr;
        
    return NULL;
}

// 分配一个新的主设备号，返回一个次设备号分配器
kptr drivers_major_alloc(void) {
    spin_lock(&major_state.lock);   // 保护全局主设备号位图

    uint32_t major;
    if (!dynarr_bitmap_alloc(major_state.bitmap, 1, &major)) {
        spin_unlock(&major_state.lock);
        return (kptr)K_ERR(-ENOSPC);    // 无空闲主设备号
    }

    spin_unlock(&major_state.lock);

    drivers_minor_devt *handle = kheap_alloc(sizeof(drivers_minor_devt));
    if (!handle) {
        // 分配结构体失败，归还已占用的主设备号
        spin_lock(&major_state.lock);
        dynarr_bitmap_free(major_state.bitmap, major);
        spin_unlock(&major_state.lock);

        return (kptr)K_ERR(-ENOMEM);
    }

    handle->major = major;
    return (kptr)K_PTR(handle);
}

// 初始化次设备号分配器
void drivers_minor_allocator_init(drivers_minor_devt *handle, bool is_dynamic) {
    if (!handle)
        return;

    handle->dynamic = is_dynamic;
    spinlock_init(&handle->lock);   // 保护次设备号分配/释放

    if (is_dynamic) {
        handle->bitmap = dynarr_bitmap_create(DEVT_MAX_COUNT);

        // 位图创建失败时降级为静态，避免句柄完全不可用
        if (!handle->bitmap) {
            handle->dynamic = false;
        } else {
            // 位图创建成功，0 号保留
            uint32_t dummy;
            dynarr_bitmap_alloc(handle->bitmap, 1, &dummy);
        }
    } else {
        handle->counter = 1;    // 静态计数器起始为 1，0 号保留
    }
}

// 分配一个次设备号
int drivers_minor_alloc(drivers_minor_devt *handle, dev_t *dev) {
    if (!handle || !dev)
        return -EINVAL;

    spin_lock(&handle->lock);       

    uint32_t mi = 0;
    if (handle->dynamic) {
        // 动态模式：从位图中分配一个空闲位作为次设备号
        if (!dynarr_bitmap_alloc(handle->bitmap, 1, &mi)) {
            spin_unlock(&handle->lock);
            return -ENOSPC;
        }
    } else {
        // 静态模式：递增计数器
        mi = handle->counter++;

        if (mi > DEVT_MAX_COUNT) {
            spin_unlock(&handle->lock);

            return -ENOSPC;
        }
    }

    spin_unlock(&handle->lock);

    *dev = MKDEV(handle->major, mi);
    return 0;
}

// 释放一个次设备号
void drivers_minor_free(drivers_minor_devt *handle, dev_t dev) {
    if (!handle)
        return;

    uint32_t mi = dev & DEVT_MAX_COUNT;
    if (!mi)     // 次设备号 0 保留，不释放
        return;

    spin_lock(&handle->lock);

    // 动态模式：将对应位清零
    if (handle->dynamic)
        dynarr_bitmap_free(handle->bitmap, mi);

    // 静态模式下计数器不回收，不执行任何操作

    spin_unlock(&handle->lock);
}

// 释放主设备号和次设备号分配器
void drivers_major_free(drivers_minor_devt *handle) {
    if (!handle)
        return;

    if (handle->dynamic && handle->bitmap)
        dynarr_bitmap_destroy(handle->bitmap);

    spin_lock(&major_state.lock);
    dynarr_bitmap_free(major_state.bitmap, handle->major);
    spin_unlock(&major_state.lock);

    kheap_free(handle);
}

// 注册 fops 到主设备号
void drivers_register_fops(drivers_minor_devt *minor_handle, struct file_operations *fops) {
    dynarr_set(fops_table, minor_handle->major, &fops);
}

// 注销主设备号的 fops
void drivers_unregister_fops(drivers_minor_devt *minor_handle) {
    struct file_operations *null = NULL;
    dynarr_set(fops_table, minor_handle->major, &null);
}

// 增加设备引用计数
void device_ref_get(struct device *dev) {
    atomic_fetch_add(&dev->refcnt, 1);
}

// 减少设备引用计数，归零时释放设备
void device_ref_put(struct device *dev) {
    if (atomic_fetch_sub(&dev->refcnt, 1) == 1) {
        // 引用计数从 1 变为 0，释放设备
        device_release(dev);
    }
}

// 添加一个总线
bool drivers_add_bus(struct bus *bus) {
    if (!bus)
        return false;

    spin_lock(&bus_list.lock);
    list_add_tail(&bus->node, &bus_list.head);
    spin_unlock(&bus_list.lock);

    BUS_INFO_PRINT(bus->name);

    return true;
}

// 移除一个总线,调用者需要确保总线上的资源都被释放
void drivers_remove_bus(struct bus *bus) {
    if (!bus)
        return;

    spin_lock(&bus_list.lock);
    list_del_init(&bus->node);
    spin_unlock(&bus_list.lock);
}

// 添加设备节点
bool drivers_add_device(struct device *dev) {
    if (!dev)
        return false;

    struct bus *bus = dev->bus;
    if (!bus)
        return false;

    // 挂载到父设备
    if (dev->parent) {
        spin_lock(&device_tree.lock);
        list_add_tail(&dev->sibling, &dev->parent->children);
        spin_unlock(&device_tree.lock);
    }

    // 挂载到总线的设备链表
    spin_lock(&bus->lock);
    list_add_tail(&dev->node, &bus->devices);
    spin_unlock(&bus->lock);

    // 匹配驱动
    bool matched = false;
    spin_lock(&bus->lock);

    struct driver *drv;
    list_for_each_entry(drv, &bus->drivers, node) {
        if (bus->match(dev, drv)) {
            matched = true;

            // 创建 probe 节点
            struct probe_process_node *node = kheap_alloc(sizeof(*node));
            if (!node) {
                // 回滚：从总线链表移除设备
                list_del_init(&dev->node);
                spin_unlock(&bus->lock);

                // 从父设备链表移除设备
                if (dev->parent) {
                    spin_lock(&device_tree.lock);
                    list_del_init(&dev->sibling);
                    spin_unlock(&device_tree.lock);
                }

                return false;
            }

            node->dev = dev;
            node->drv = drv;
            node->retry_count = 0;
            INIT_LIST_HEAD(&node->node);

            // 增加设备引用(被驱动框架引用)
            device_ref_get(dev);

            spin_lock(&probe_process_list.wait_lock);
            list_add_tail(&node->node, &probe_process_list.wait);
            spin_unlock(&probe_process_list.wait_lock);

            break;
        }
    }

    spin_unlock(&bus->lock);

    // 没有匹配到驱动，加入未匹配设备列表
    if (!matched) {
        spin_lock(&devices_unmatched_list.lock);
        list_add_tail(&dev->unmatched_node, &devices_unmatched_list.head);
        spin_unlock(&devices_unmatched_list.lock);
    }

    DEVICE_INFO_PRINT(dev->name, dev->bus->name, dev->parent);

    return true;
}

// 移除设备节点
bool drivers_remove_device(struct device *dev) {
    if (!dev)
        return false;

    // 断开设备与父设备的连接（从父设备 children 链表摘除）
    spin_lock(&device_tree.lock);
    
    if (dev->parent)
        list_del_init(&dev->sibling);

    spin_unlock(&device_tree.lock);

    // 断开设备与总线的连接（从 bus->devices 链表摘除）
    if (dev->bus) {
        spin_lock(&dev->bus->lock);
        list_del_init(&dev->node);
        spin_unlock(&dev->bus->lock);
    }

    // 后序遍历子树并释放
    struct device *curr = dev;
    while (curr) {
        // 一直取第一个子节点，直到叶子
        while (!list_empty(&curr->children)) 
            curr = list_first_entry(&curr->children, struct device, sibling);

        // 此时 curr 是叶子节点（无子节点）
        struct device *parent = curr->parent;

        // 从父链表中摘除当前节点，防止访问
        if (parent)
            list_del_init(&curr->sibling);

        // 解绑驱动
        if (curr->driver) {
            if (curr->driver->remove)
                curr->driver->remove(curr);
                
            curr->driver = NULL;    // 表示当前设备节点无用
        }

        // 从总线链表中摘除当前节点（如果尚未摘除）
        if (curr->bus) {
            spin_lock(&curr->bus->lock);
            list_del_init(&curr->node);
            spin_unlock(&curr->bus->lock);
        }

        // 释放设备自身的引用
        device_ref_put(curr);

        // 回溯
        if (!parent) {
            // 根节点已释放，结束
            break;
        }

        // 检查父节点是否还有其他子节点
        if (!list_empty(&parent->children)) {
            // 有兄弟：切换到下一个兄弟子树
            curr = list_first_entry(&parent->children, struct device, sibling);
        } else {
            // 无兄弟：父节点变为新叶子
            curr = parent;
        }
    }

    return true;
}

// 添加驱动节点
bool drivers_add_driver(struct driver *drv) {
    if (!drv || !drv->bus)
        return false;

    struct bus *bus = drv->bus;

    // 将驱动加入总线驱动链表
    spin_lock(&bus->lock);
    list_add_tail(&drv->node, &bus->drivers);
    spin_unlock(&bus->lock);

    // 遍历未匹配设备列表，尝试匹配
    spin_lock(&devices_unmatched_list.lock);
    struct device *dev, *tmp;
    list_for_each_entry_safe(dev, tmp, &devices_unmatched_list.head, unmatched_node) {
        if (bus->match(dev, drv)) {
            // 从未匹配链表中移除
            list_del_init(&dev->unmatched_node);
            spin_unlock(&devices_unmatched_list.lock);

            // 创建 probe 节点
            struct probe_process_node *node = kheap_alloc(sizeof(*node));
            if (!node) {
                // 内存分配失败，将设备放回未匹配链表，并继续尝试其他设备
                spin_lock(&devices_unmatched_list.lock);
                list_add_tail(&dev->unmatched_node, &devices_unmatched_list.head);
                
                // 不释放锁，继续遍历
                continue;
            }

            node->dev = dev;
            node->drv = drv;
            node->retry_count = 0;
            INIT_LIST_HEAD(&node->node);

            // 增加设备引用
            device_ref_get(dev);

            spin_lock(&probe_process_list.wait_lock);
            list_add_tail(&node->node, &probe_process_list.wait);
            spin_unlock(&probe_process_list.wait_lock);

            // 重新获取锁，继续遍历
            spin_lock(&devices_unmatched_list.lock);
        }
    }

    spin_unlock(&devices_unmatched_list.lock);

    DRIVER_INFO_PRINT(drv->name, drv->bus->name);

    return true;
}

// 移除驱动节点
bool drivers_remove_driver(struct driver *drv) {
    if (!drv)
        return false;

    // 检查是否有设备绑定该驱动
    struct device *dev;
    int found = 0;

    // 遍历总线设备链表，检查是否有设备绑定该驱动
    spin_lock(&drv->bus->lock);
    list_for_each_entry(dev, &drv->bus->devices, node) {
        if (dev->driver == drv) {
            found = 1;
            break;
        }
    }
    spin_unlock(&drv->bus->lock);

    if (found)
        return false;  // 仍有设备绑定该驱动，拒绝移除

    // 从总线驱动链表中删除
    spin_lock(&drv->bus->lock);
    list_del_init(&drv->node);
    spin_unlock(&drv->bus->lock);

    return true;
}