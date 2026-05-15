/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <drivers.h>
#include <stdatomic.h>
#include <asm/serial.h>
#include <errno.h>
#include <bitmap.h>
#include <dynarr.h>
#include <spinlock.h>
#include <asm/serial.h>
#include <klibc.h>
#include <heap.h>
#include <drivers/base/drivers.h>

#define DRIVERS_PRINT(fmt, ...) \
    printk("[DRIVERS] " fmt, ##__VA_ARGS__)

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

// 设备号分配器状态
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

// 设备树根节点，用于挂载bus
static LIST_HEAD(drivers_list);
static spinlock_t drivers_lock = SPIN_LOCK_INIT;    // 保护设备树的锁

// fops 表，按主设备号索引，存储 struct file_operations *
static dynarr_t *fops_table = NULL;

// 探测设备
static bool drivers_probe_device(struct device *dev, struct driver *drv) {
    if (!dev || !drv || !drv->probe)
        return false;

    if (drv->probe(dev) == 0)
        return true;

    return false;
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

// 驱动框架初始化
bool drivers_init(void) {
    if (!drivers_anon_init()) 
        return false;

    if (!drivers_major_init())
        return false;

    // 创建 fops 表
    fops_table = dynarr_create(sizeof(struct file_operations *), DEV_MAX_MAJOR + 1);
    if (!fops_table)
        return false;

    DRIVERS_PRINT("drivers init success\n");
    return true;
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
drivers_minor_devt *drivers_major_alloc(void) {
    spin_lock(&major_state.lock);   // 保护全局主设备号位图

    uint32_t major;
    if (!dynarr_bitmap_alloc(major_state.bitmap, 1, &major)) {
        spin_unlock(&major_state.lock);
        return ERR_PTR(-ENOSPC);    // 无空闲主设备号
    }

    spin_unlock(&major_state.lock);

    drivers_minor_devt *handle = kheap_alloc(sizeof(drivers_minor_devt));
    if (!handle) {
        // 分配结构体失败，归还已占用的主设备号
        spin_lock(&major_state.lock);
        dynarr_bitmap_free(major_state.bitmap, major);
        spin_unlock(&major_state.lock);

        return ERR_PTR(-ENOMEM);
    }

    handle->major = major;
    return handle;
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
int drivers_register_fops(unsigned int major, struct file_operations *fops) {
    if (!fops_table || major > DEV_MAX_MAJOR)
        return -EINVAL;

    dynarr_set(fops_table, major, &fops);
    return 0;
}

// 注销主设备号的 fops
void drivers_unregister_fops(unsigned int major) {
    if (!fops_table || major > DEV_MAX_MAJOR)
        return;

    struct file_operations *null = NULL;
    dynarr_set(fops_table, major, &null);
}

// 初始化总线
void drivers_bus_init(
    struct bus *bus, 
    const char *name,
    bool (*match)(struct device *, struct driver *)
) {
    bus->name = name;
    bus->match = match;

    INIT_LIST_HEAD(&bus->devices);
    INIT_LIST_HEAD(&bus->drivers);
    INIT_LIST_HEAD(&bus->node);

    spinlock_init(&bus->lock);
}

// 添加一个总线
bool drivers_add_bus(struct bus *bus) {
    spin_lock(&drivers_lock);
    list_add(&bus->node, &drivers_list);
    spin_unlock(&drivers_lock);
    return true;
}

// 移除一个总线,调用者需要确保总线上的资源都被释放
void drivers_remove_bus(struct bus *bus) {
    spin_lock(&drivers_lock);
    list_del(&bus->node);
    spin_unlock(&drivers_lock);
}

// 初始化设备节点
void drivers_device_init(
    struct device *dev, 
    struct bus *bus, 
    const char *name, 
    struct resource *res, 
    int num_res,
    void *priv,
    dev_t devt,
    struct device *parent
) {
    dev->bus = bus;
    dev->name = name;
    dev->res = res;
    dev->num_res = num_res;
    dev->priv = priv;
    dev->devt = devt;
    dev->driver = NULL;
    dev->parent = parent;
    INIT_LIST_HEAD(&dev->children);
    INIT_LIST_HEAD(&dev->sibling);
    INIT_LIST_HEAD(&dev->node);
    dev->registered = false;

    // 匹配自己的驱动
    if (bus) {
        spin_lock(&bus->lock);

        struct driver *drv = NULL;
        list_for_each_entry(drv, &bus->drivers, node) {
            if (bus->match(dev, drv) && drivers_probe_device(dev, drv)) {
                dev->driver = drv;
                break;
            }
        }

        spin_unlock(&bus->lock);
    }
}

// 添加设备节点
bool drivers_add_device(struct device *dev) {
    struct bus *bus = dev->bus;
    if (!bus)
        return false;

    spin_lock(&bus->lock);
    list_add(&dev->node, &bus->devices);
    spin_unlock(&bus->lock);

    dev->registered = true;

    return true;
}

// 移除设备节点
void drivers_remove_device(struct device *dev) {
    struct bus *bus = dev->bus;
    if (!bus)
        return;

    spin_lock(&bus->lock);
    list_del(&dev->node);
    spin_unlock(&bus->lock);

    dev->registered = false;
}

// 初始化驱动节点
void drivers_driver_init(
    struct driver *drv,
    const char *name,
    struct bus *bus,
    int (*probe)(struct device *dev),
    void (*remove)(struct device *dev),
    const void *id_table
) {
    drv->name = name;
    drv->bus = bus;
    drv->probe = probe;
    drv->remove = remove;
    drv->id_table = id_table;

    spin_lock(&bus->lock);
    list_add(&drv->node, &bus->drivers);
    spin_unlock(&bus->lock);
}

// 添加驱动节点
bool drivers_add_driver(struct driver *drv) {
    struct bus *bus = drv->bus;
    if (!bus)
        return false;

    // 遍历总线上的设备，尝试匹配
    spin_lock(&bus->lock);

    struct device *dev = NULL;
    list_for_each_entry(dev, &bus->devices, node) {
        if (!dev->driver && bus->match(dev, drv) && drivers_probe_device(dev, drv)) 
            dev->driver = drv;
    }

    spin_unlock(&bus->lock);

    return true;
}

// 移除驱动节点
void drivers_remove_driver(struct driver *drv) {
    struct bus *bus = drv->bus;
    if (!bus)
        return;

    // 遍历总线上的设备，解绑该驱动
    spin_lock(&bus->lock);

    struct device *dev = NULL;
    struct device *tmp;
    list_for_each_entry_safe(dev, tmp, &bus->devices, node) {
        if (dev->driver == drv) {
            if (drv->remove)
                drv->remove(dev);

            dev->driver = NULL;
        }
    }

    list_del(&drv->node);

    spin_unlock(&bus->lock);
}