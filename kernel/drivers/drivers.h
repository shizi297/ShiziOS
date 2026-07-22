/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <spinlock.h>
#include <list.h>
#include <shizi/types.h>
#include <drivers.h>

// 资源类型
enum resource_flags {
    IORESOURCE_MEM = 1ULL << 0,   // 内存区域（MMIO）
    IORESOURCE_IO  = 1ULL << 1,   // I/O 端口
    IORESOURCE_IRQ = 1ULL << 2,   // 中断号
    IORESOURCE_DMA = 1ULL << 3,   // DMA 通道
    IORESOURCE_PREFETCHABLE = 1ULL << 4,    // 可预取
};

// 用于设备资源描述
struct resource {
    uintptr_t start;            // 资源起始地址
    uintptr_t end;              // 资源结束地址
    const char *name;           // 资源名称
    enum resource_flags flags;  // 资源类型
};

// 设备节点
struct device {
    struct bus *bus;    // 所属的总线

    const char *name;

    // 设备资源数组，由总线驱动填充
    struct resource *res;
    int num_res;    // 数组元素数量

    // 驱动私有资源
    void *driver_data; 

    /*
     * 设备号
     * 用于创建设备节点
     * 由总线驱动或设备驱动分配
     * 在 probe 中填充
     */
    dev_t devt;

    // 当前绑定的驱动
    struct driver *driver;

    // 父设备
    struct device *parent;

    // 子设备链表头
    struct list_head children;

    // 用于链入父设备 children 链表的节点
    struct list_head sibling;

    // 链表节点，挂入 bus->devices
    struct list_head node;

    // 用于挂入待处理设备链表
    struct list_head unmatched_node;  

    // 引用计数，为 0 才能真正释放
    atomic_int refcnt;
};

// 驱动节点
struct driver {
    const char *name;               

    struct bus *bus;    // 所属总线

    // 用于设备匹配成功后调用，用于初始化相关的东西
    int (*probe)(struct device *dev);

    // 用于设备移除时调用
    void (*remove)(struct device *dev);

    /*
     * ID 表
     * 表示该驱动支持的硬件标识列表
     * 格式由总线解释
     */
    const void *id_table;

    // 链表节点，挂入 bus->drivers
    struct list_head node;
};

// 总线节点
struct bus {
    const char *name;  

    // 根据 device 和 driver 中的硬件 ID 判断是否匹配
    bool (*match)(struct device *dev, struct driver *drv);

    // 设备链表头
    struct list_head devices;

    // 驱动链表头
    struct list_head drivers;

    // 用于保护总线节点内字段的写操作
    spinlock_t lock;

    // 用于将总线挂入根节点的链表节点
    struct list_head node;
    
   // 总线私有数据
    void *priv;

    // 释放设备的驱动私有数据
    void (*free_device)(struct device *dev);
};

// 增加设备引用计数
void device_ref_get(struct device *dev);

// 减少设备引用计数，归零时释放设备
void device_ref_put(struct device *dev); 

// 添加一个总线
bool drivers_add_bus(struct bus *bus);

// 移除一个总线,调用者需要确保总线上的资源都被释放
void drivers_remove_bus(struct bus *bus);

// 添加设备节点
bool drivers_add_device(struct device *dev);

// 移除设备节点
bool drivers_remove_device(struct device *dev);

// 添加驱动节点
bool drivers_add_driver(struct driver *drv);

// 移除驱动节点
bool drivers_remove_driver(struct driver *drv);

