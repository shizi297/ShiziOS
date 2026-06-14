/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <klibc.h>

static uint8_t virtio_device_counter = 0;

static bool virtio_match(struct device *dev, struct driver *drv) {
    struct virtio_dev_priv *priv = dev->driver_data;
    const struct virtio_device_id *id_table = drv->id_table;

    if (!priv || !id_table)
        return false;

    for (const struct virtio_device_id *id = id_table; id->type != 0; id++) {
        if (id->type == priv->type)
            return true;
    }

    return false;
}

static void virtio_free_device(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    if (!priv)
        return;

    // 释放设备私有数据结构
    kheap_free(priv);
    dev->driver_data = NULL;

    // 释放设备名
    kheap_free((void *)dev->name);
}

struct bus virtio_bus_type = {
    .name = "virtio",
    .match = virtio_match,
    .devices = {0},
    .drivers = {0},
    .lock = SPIN_LOCK_INIT,
    .node = {0},
    .priv = NULL,
    .free_device = virtio_free_device,
};

static void virtio_format_device_name(char *buf, uint16_t type, uint8_t count) {
    const char dec[] = "0123456789";

    // type 的高位和低位
    buf[0] = dec[(type / 10) % 10];
    buf[1] = dec[type % 10];
    buf[2] = ':';

    // count 的百位、十位、个位
    buf[3] = dec[(count / 100) % 10];
    buf[4] = dec[(count / 10) % 10];
    buf[5] = dec[count % 10];
    buf[6] = '\0';
}

int virtio_probe(struct device *dev, struct virtio_dev_ops *ops, struct device *parent) {
    int ret = -ENODEV;
    struct virtio_dev_priv *priv;
    struct device *virtio_dev = NULL;
    char *name = NULL;

    if (!ops->init(dev))
        goto err_ret;

    // 取出 init 填充好的设备私有数据
    priv = dev->driver_data;
    if (!priv)
        goto err_destroy;

    // 后面失败均为内存不足
    ret = -ENOMEM;

    // 创建虚拟设备
    virtio_dev = kheap_alloc(sizeof(*virtio_dev));
    if (!virtio_dev)
        goto err_destroy;

    // 构造设备名
    name = kheap_alloc(VIRTIO_DEVICE_NAME_LEN);
    if (!name)
        goto err_free_device;

    memset(virtio_dev, 0, sizeof(*virtio_dev));
    virtio_format_device_name(name, priv->type, virtio_device_counter++);
    virtio_dev->name = name;
    virtio_dev->bus = &virtio_bus_type;
    virtio_dev->parent = parent;
    virtio_dev->driver_data = priv;

    atomic_init(&virtio_dev->refcnt, 0);
    INIT_LIST_HEAD(&virtio_dev->children);
    INIT_LIST_HEAD(&virtio_dev->sibling);
    INIT_LIST_HEAD(&virtio_dev->node);
    INIT_LIST_HEAD(&virtio_dev->unmatched_node);

    // 注册到 VirtIO 总线
    drivers_add_device(virtio_dev);
    return 0;

err_free_device:
    kheap_free(virtio_dev);
err_destroy:
    ops->destroy(dev);
err_ret:
    return ret;
}

void virtio_remove(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_dev_ops *ops = dev->bus->priv;

    if (priv && ops && ops->destroy)
        ops->destroy(priv->pdev);

    // 释放设备名
    kheap_free((void *)dev->name);

    // 释放设备本身
    kheap_free(dev);
}