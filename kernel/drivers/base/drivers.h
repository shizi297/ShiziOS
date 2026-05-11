/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <rcu.h>
#include <spinlock.h>
#include <hash.h>
#include <shizi/types.h>

// 设备节点
struct device {
    struct bus *bus;    // 所属的总线

    const char *name;

    // 设备资源数组，由总线驱动填充
    struct resource *res;
    int num_res;    // 数组元素数量

    // 总线私有资源
    void *priv; 

    /*
     * 设备号
     * 用于创建设备节点
     * 由总线驱动或设备驱动分配
     * 在 probe 中填充
     */
    dev_t devt;

    // 当前绑定的驱动
    struct driver __rcu *driver;

    // 父设备
    struct device *parent;

    // 子设备链表头
    struct list_head children;

    // 用于链入父设备 children 链表的节点
    struct list_head sibling;

    // 链表节点，挂入 bus->devices
    struct list_head node;

    // 哈希节点，挂入 bus->dev_hash 的桶
    struct hlist_node hash_node;

    struct rcu_head rcu;

    bool registered;    // 是否已注册
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

    struct rcu_head rcu;        
};

// 总线节点
struct bus {
    const char *name;  

    // 根据 device 和 driver 中的硬件 ID 判断是否匹配
    bool (*match)(struct device *dev, struct driver *drv);

    // 设备链表头
    struct list_head devices;

    // 驱动链表头，使用 RCU 保护。
    struct list_head drivers;

    // 设备名称哈希表，用于快速查找
    struct hash_table dev_hash; 

    // 用于保护总线节点内字段的写操作
    spinlock_t lock;

    // 用于将总线挂入根节点的链表节点
    struct list_head node;
};