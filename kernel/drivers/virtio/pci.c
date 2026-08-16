/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <drivers/pci.h>
#include <shizi/types.h>
#include <klibc.h>
#include <heap.h>
#include <initcall.h>
#include <drivers/drivers.h>
#include <bootboot.h>

#define VIRTIO_PCI_CAP_ID 0x09

// PCI 厂商 ID
#define PCI_VENDOR_ID_VIRTIO 0x1AF4  

#define VIRTIO_PCI_DEVICE_ID_START 0x1040

// VirtIO PCI 配置类型
typedef enum : uint8_t {
    VIRTIO_PCI_CFG_COMMON  = 1,  // 通用配置区域
    VIRTIO_PCI_CFG_NOTIFY  = 2,  // 通知区域
    VIRTIO_PCI_CFG_ISR     = 3,  // 中断状态区域
    VIRTIO_PCI_CFG_DEVICE  = 4,  // 设备专属配置区域
    VIRTIO_PCI_CFG_PCI     = 5,  // PCI 配置区域
} virtio_pci_cfg_type_t;

// VirtIO PCI 能力
struct virtio_pci_cap {
    uint8_t cap_vndr;       // 固定为 0x09
    uint8_t cap_next;       // 下一个能力在配置空间中的偏移（0 表示链表结束）
    uint8_t cap_len;        // 能力长度

    virtio_pci_cfg_type_t cfg_type;       

    uint8_t bar;            // BAR 编号
    uint8_t id;             // 用于区分同一类型的多个能力
    uint8_t padding[2];     // 填充到 32 位边界
    uint32_t offset;        // BAR 内偏移（小端）
    uint32_t length;        // 结构体长度（小端）
} __attribute__((packed));

// 带 64 位的 VirtIO PCI 能力
struct virtio_pci_cap64 {
    struct virtio_pci_cap cap;  
    uint32_t offset_hi;         // 偏移的高 32 位
    uint32_t length_hi;         // 长度的高 32 位
} __attribute__((packed));

struct virtio_pci_notify_cap {
    struct virtio_pci_cap cap;
    uint32_t notify_off_multiplier;
};

// VirtIO PCI 通用配置结构体
struct virtio_pci_common {
    // 关于整个设备的字段
    volatile uint32_t device_feature_select;    // 选择 device_feature 的 32 位段
    volatile uint32_t device_feature;           // 设备提供的能力位（只读）
    volatile uint32_t driver_feature_select;    // 选择 driver_feature 的 32 位段
    volatile uint32_t driver_feature;           // 驱动接受的能力位（读/写）
    volatile uint16_t config_msix_vector;       // 配置变更的 MSI-X 向量
    volatile uint16_t num_queues;               // 设备支持的最大队列数（只读）
    volatile virtio_status_t device_status;     // 设备状态（读/写，写 0 复位）
    volatile uint8_t config_generation;         // 配置空间原子性保护（只读）

    // 关于特定 virtioqueue 的字段（需先写 queue_select）
    volatile uint16_t queue_select;             // 选择当前操作的队列索引
    volatile uint16_t queue_size;               // 队列大小（读/写，0 表示不可用）
    volatile uint16_t queue_msix_vector;        // 该队列的 MSI-X 向量
    volatile uint16_t queue_enable;             // 队列使能（1 启用，0 禁用）
    volatile uint16_t queue_notify_off;         // 通知结构偏移量（只读）
    volatile uint64_t queue_desc;               // Descriptor 区的物理地址
    volatile uint64_t queue_driver;             // available ring 的物理地址
    volatile uint64_t queue_device;             // used ring 的物理地址
    volatile uint16_t queue_notify_data;        // 驱动通知时使用的数据（若 VIRTIO_F_NOTIF_CONFIG_DATA 已协商）
    volatile uint16_t queue_reset;              // 队列复位（若 VIRTIO_F_RING_RESET 已协商）
} __attribute__((packed));

// Notify 区域结构体
struct virtio_pci_notify {
    union {
        struct {
            volatile uint16_t notify16;   
            volatile uint16_t reserve;
        };  // 未开启 VIRTIO_F_NOTIFICATION_DATA 时使用

        volatile uint32_t notify32; // 开启 VIRTIO_F_NOTIFICATION_DATA 时使用
    };
} __attribute__((packed));

struct virtio_pci_isr {
    volatile uint8_t status;
};

struct virtio_pci_device {
    volatile word_t val;
} __attribute__((packed));

// 传输层私有数据
struct virtio_transport_priv {
    struct virtio_pci_common *common;        
    struct virtio_pci_notify **notify;    
    uint32_t num_notify;  
    struct virtio_pci_isr *isr;      
    struct virtio_pci_device *device;       

    uintptr_t notify_base;                  
    uint32_t notify_multiplier;          
    struct virtio_pci_notify **notify_by_vqn; 
};

// 单个 VirtIO PCI 能力条目
struct virtio_pci_cap_entry {
    virtio_pci_cfg_type_t cfg_type; 
    uint8_t id;          // 区分同类型的多个能力
    uint8_t bar;         // BAR 编号
    uint64_t offset;     // BAR 内偏移
    uint64_t phys_addr;  // 物理地址
    void *virt_addr;     // 映射后的虚拟地址
    uint8_t cap_len;
};

// 解析到的所有能力
struct virtio_pci_caps {
    uint32_t count;   // 能力条目数量
    struct virtio_pci_cap_entry caps[];    // 保存所有能力条目的信息
};

// 传输层内部私有数据结构
struct virtio_pci_dev_priv {
    struct virtio_pci_caps *caps;
};

static struct device *virtio_pci_dev;
static struct virtio_dev_ops ops; 
static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

static int virtio_pci_probe(struct device *dev) {
    return virtio_probe(dev, &ops, virtio_pci_dev);
}

void virtio_pci_remove(struct device *dev) {
    virtio_remove(dev, virtio_pci_dev);
}

static const struct pci_device_id virtio_pci_id_table[] = {
    { PCI_VENDOR_ID_VIRTIO, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
    { 0, 0, 0, 0, 0, 0 }
};

static struct driver virtio_pci_driver = {
    .name = "virtio-pci",
    .bus = &pci_bus_type,
    .probe = virtio_pci_probe,
    .remove = virtio_pci_remove,
    .id_table = virtio_pci_id_table,
    .node = {0},
};

/**
 * 解析 VirtIO PCI 能力列表
 *
 * @param dev 物理 PCI 设备
 * 
 * @return virtio_pci_caps 指针
 */
static struct virtio_pci_caps *virtio_pci_parse_caps(struct device *dev) {
    uint32_t count = 0;
    uint8_t off = 0;

    // 第一次遍历：统计数量
    while (1) {
        off = pci_find_capability(dev, VIRTIO_PCI_CAP_ID, off);
        if (off == 0)
            break;

        count++;
        off = pci_config_read_byte(dev, off + offsetof(struct virtio_pci_cap, cap_next));
        if (off == 0)
            break;
    }

    if (count == 0)
        return NULL;

    // 分配柔性数组
    size_t caps_size = 
        sizeof(struct virtio_pci_caps) +
        count * sizeof(struct virtio_pci_cap_entry);

    struct virtio_pci_caps *caps = kheap_alloc(caps_size);
    if (!caps)
        return NULL;
        
    memset(caps, 0, caps_size);
    caps->count = count;

    // 第二次遍历：填充数组并映射 MMIO
    off = 0;
    uint32_t idx = 0;
    while (1) {
        off = pci_find_capability(dev, VIRTIO_PCI_CAP_ID, off);
        if (off == 0)
            break;

        struct virtio_pci_cap_entry *entry = &caps->caps[idx];

        uint8_t cap_len = pci_config_read_byte(
            dev, off + offsetof(struct virtio_pci_cap, cap_len)
        );

        entry->cap_len = cap_len;
        entry->cfg_type = (virtio_pci_cfg_type_t)pci_config_read_byte(
            dev, off + offsetof(struct virtio_pci_cap, cfg_type)
        );

        entry->bar = pci_config_read_byte(
            dev, off + offsetof(struct virtio_pci_cap, bar)
        );

        entry->id = pci_config_read_byte(
            dev, off + offsetof(struct virtio_pci_cap, id)
        );

        uint64_t offset = pci_config_read_dword(
            dev, off + offsetof(struct virtio_pci_cap, offset)
        );

        uint64_t length = pci_config_read_dword(
            dev, off + offsetof(struct virtio_pci_cap, length)
        );

        // 如果是 64 位能力，读取高 32 位
        if (cap_len >= sizeof(struct virtio_pci_cap64)) {
            uint32_t offset_hi = pci_config_read_dword(
                dev, off + offsetof(struct virtio_pci_cap64, offset_hi)
            );
            offset |= (uint64_t)offset_hi << 32;

            uint32_t length_hi = pci_config_read_dword(
                dev, off + offsetof(struct virtio_pci_cap64, length_hi)
            );
            length |= (uint64_t)length_hi << 32;
        }

        entry->offset = offset;

        // 计算物理地址
        uint64_t bar_phys = dev->res[entry->bar].start;
        entry->phys_addr = bar_phys + offset;

        // 长度为 0 的能力不需要映射 MMIO，直接跳过
        if (length == 0) {
            off = pci_config_read_byte(
                dev, off + offsetof(struct virtio_pci_cap, cap_next)
            );
            if (off == 0)
                break;
            continue;
        }

        // 映射 MMIO
        entry->virt_addr = vheap_map_mmio(entry->phys_addr, length);
        if (!entry->virt_addr) {
            // 回滚已映射的条目
            for (uint32_t j = 0; j < idx; j++) {
                if (caps->caps[j].virt_addr)
                    vheap_unmap_mmio(caps->caps[j].virt_addr);
            }
            kheap_free(caps);
            return NULL;
        }

        // 读取 cap_next 以继续遍历
        off = pci_config_read_byte(
            dev, off + offsetof(struct virtio_pci_cap, cap_next)
        );

        if (off == 0)
            break;

        idx++;
    }

    return caps;
}

static struct virtio_dev_priv *virtio_pci_ops_init(struct device *dev) {
    struct virtio_dev_priv *priv = NULL;
    struct virtio_pci_caps *caps = NULL;
    struct virtio_pci_common *common = NULL;
    struct virtio_pci_notify **notify_array = NULL;
    uint32_t num_notify = 0;
    struct virtio_pci_isr *isr = NULL;
    struct virtio_pci_device *device_cfg = NULL;
    struct virtio_transport_priv *ctx = NULL;
    uintptr_t notify_base = 0;
    uint32_t notify_multiplier = 0;
    bool found_mmio_notify = false;

    // 分配设备私有数据
    priv = kheap_alloc(sizeof(*priv));
    if (!priv)
        goto out;
    memset(priv, 0, sizeof(*priv));

    priv->pdev = dev;

    // 读取 VirtIO 设备类型
    priv->type = pci_get_device_id(dev) - VIRTIO_PCI_DEVICE_ID_START;

    // 解析能力列表
    caps = virtio_pci_parse_caps(dev);
    if (!caps)
        goto free_priv;

    // 第一次遍历：统计 Notify 条目数量
    for (uint32_t i = 0; i < caps->count; i++) {
        if (caps->caps[i].cfg_type == VIRTIO_PCI_CFG_NOTIFY)
            num_notify++;
    }

    // 分配 Notify 数组
    if (num_notify > 0) {
        notify_array = kheap_alloc(num_notify * sizeof(struct virtio_pci_notify *));
        if (!notify_array)
            goto unmap_mmio;
        memset(notify_array, 0, num_notify * sizeof(struct virtio_pci_notify *));
    }

    // 第二次遍历：填充各配置区域，并记录主通知区域信息
    for (uint32_t i = 0; i < caps->count; i++) {
        struct virtio_pci_cap_entry *entry = &caps->caps[i];

        switch (entry->cfg_type) {
            case VIRTIO_PCI_CFG_COMMON:
                if (entry->id == 0)
                    common = (struct virtio_pci_common *)entry->virt_addr;
                break;
            case VIRTIO_PCI_CFG_NOTIFY:
                if (entry->id < num_notify)
                    notify_array[entry->id] = entry->virt_addr;

                // 选择第一个 MMIO BAR 的 Notify 能力作为主通知区域
                if (!found_mmio_notify && (dev->res[entry->bar].flags & IORESOURCE_MEM)) {
                    // 获取乘数
                    struct virtio_pci_notify_cap *notify_cap = 
                        (struct virtio_pci_notify_cap *)entry->virt_addr;

                    notify_base = (uintptr_t)entry->virt_addr;
                    notify_multiplier = notify_cap->notify_off_multiplier;
                    found_mmio_notify = true;
                }
                break;
            case VIRTIO_PCI_CFG_ISR:
                if (entry->id == 0)
                    isr = entry->virt_addr;
                break;
            case VIRTIO_PCI_CFG_DEVICE:
                if (entry->id == 0)
                    device_cfg = entry->virt_addr;
                break;
            default:
                break;
        }
    }

    // 必须找到 Common 配置区域和至少一个 Notify 区域
    if (!common || !notify_array)
        goto free_notify;

    // 必须找到 MMIO BAR 的 Notify 能力，否则无法继续
    if (!found_mmio_notify)
        goto free_notify;

    // 读取设备特性位图
    common->device_feature_select = 0;
    uint32_t dev_lo = common->device_feature;
    common->device_feature_select = 1;
    uint32_t dev_hi = common->device_feature;
    uint64_t dev_features = ((uint64_t)dev_hi << 32) | dev_lo;

    // 检查必须支持的能力
    if ((dev_features & VIRTIO_REQUIRED_CAPS) != VIRTIO_REQUIRED_CAPS)
        goto free_notify;

    // 计算驱动特性
    uint64_t driver_features = 
        VIRTIO_REQUIRED_CAPS | (dev_features & ~VIRTIO_FORBIDDEN_FEATURES);

    // 写回硬件
    common->driver_feature_select = 0;
    common->driver_feature = (uint32_t)driver_features;
    common->driver_feature_select = 1;
    common->driver_feature = (uint32_t)(driver_features >> 32);

    // 保存协商结果
    priv->features = driver_features;

    // 构建传输层上下文
    ctx = kheap_alloc(sizeof(*ctx));
    if (!ctx)
        goto free_notify;

    ctx->common = common;
    ctx->notify = notify_array;
    ctx->num_notify = num_notify;
    ctx->isr = isr;
    ctx->device = device_cfg;
    ctx->notify_base = notify_base;
    ctx->notify_multiplier = notify_multiplier;

    // 分配按 vqn 索引的通知地址数组，大小为 CPU 核心数
    ctx->notify_by_vqn = kheap_alloc(bootboot->numcores * sizeof(struct virtio_pci_notify *));
    if (!ctx->notify_by_vqn)
        goto free_ctx;
    memset(ctx->notify_by_vqn, 0, bootboot->numcores * sizeof(struct virtio_pci_notify *));

    priv->transport_priv = ctx;

    // 清理能力列表
    kheap_free(caps);
    return priv;

free_ctx:
    kheap_free(ctx);
free_notify:
    kheap_free(notify_array);
unmap_mmio:
    if (caps) {
        for (uint32_t i = 0; i < caps->count; i++) {
            if (caps->caps[i].virt_addr)
                vheap_unmap_mmio(caps->caps[i].virt_addr);
        }
        kheap_free(caps);
    }
free_priv:
    kheap_free(priv);
out:
    return NULL;
}

static void virtio_pci_ops_destroy(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    if (!priv || !priv->transport_priv)
        return;

    struct virtio_transport_priv *ctx = priv->transport_priv;

    // 释放 Notify 数组
    if (ctx->notify) {
        for (uint32_t i = 0; i < ctx->num_notify; i++) {
            if (ctx->notify[i])
                vheap_unmap_mmio((void *)ctx->notify[i]);
        }
        kheap_free(ctx->notify);
    }

    // 释放按 vqn 索引的通知地址数组
    if (ctx->notify_by_vqn) {
        kheap_free(ctx->notify_by_vqn);
        ctx->notify_by_vqn = NULL;
    }

    // 释放 MMIO 映射
    if (ctx->common)
        vheap_unmap_mmio((void *)ctx->common);
    if (ctx->isr)
        vheap_unmap_mmio((void *)ctx->isr);
    if (ctx->device)
        vheap_unmap_mmio((void *)ctx->device);

    kheap_free(ctx);
    priv->transport_priv = NULL;
}

static virtio_status_t virtio_pci_ops_get_status(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;
    return ctx->common->device_status;
}

static void virtio_pci_ops_set_status(struct device *dev, virtio_status_t status) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;
    ctx->common->device_status = status;
}

static void virtio_pci_ops_read_device_config(
    struct device *dev, 
    uint32_t offset, 
    void *buf, 
    size_t len
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;
    volatile word_t *val = (volatile word_t *)(&ctx->device->val.u8 + offset);
    word_t *_buf = (word_t *)buf;

    switch (len) {
        case 1:
            _buf->u8 = val->u8;
            break;
        case 2:
            _buf->u16 = val->u16;
            break;
        case 4:
            _buf->u32 = val->u32;
            break;
        case 8:
            _buf->u64 = val->u64;
            break;
        default:
            break;
    }
}

static void virtio_pci_ops_write_device_config(
    struct device *dev,
    uint32_t offset,
    const void *buf,
    size_t len
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;
    volatile word_t *val = (volatile word_t *)(&ctx->device->val.u8 + offset);
    const word_t *_buf = (const word_t *)buf;

    switch (len) {
        case 1:
            val->u8 = _buf->u8;
            break;
        case 2:
            val->u16 = _buf->u16;
            break;
        case 4:
            val->u32 = _buf->u32;
            break;
        case 8:
            val->u64 = _buf->u64;
            break;
        default:
            break;
    }
}

static void virtio_pci_ops_write_features(struct device *dev, uint64_t features) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;

    ctx->common->driver_feature_select = 0;
    ctx->common->driver_feature = (uint32_t)features;
    ctx->common->driver_feature_select = 1;
    ctx->common->driver_feature = (uint32_t)(features >> 32);
}

static bool virtio_pci_ops_set_vq(
    struct device *dev,
    uint32_t index,
    struct virtioqueue *vq,
    uint32_t logical_id,
    uint32_t vector
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;

    if (index >= ctx->common->num_queues)
        return false;

    // 选择队列
    ctx->common->queue_select = index;

    if (vq->size > ctx->common->queue_size)
        return false;

    // 分配 MSI‑X 向量
    int msix = pci_msix_alloc_vector(priv->pdev, logical_id, vector);
    if (msix < 0)
        return false;

    ctx->common->queue_msix_vector = msix;

    // 设置队列物理地址
    ctx->common->queue_desc = vq->desc_phys;
    ctx->common->queue_driver = vq->avail_phys;
    ctx->common->queue_device = vq->used_phys;

    // 启用队列
    ctx->common->queue_enable = 1;

    if (!ctx->common->queue_enable)
        goto err_free_msix;

    vq->queue_index = index;
    vq->queue_notify_data = ctx->common->queue_notify_data;
    vq->notify_default_idx = ctx->common->queue_notify_off;

    // 读取当前队列的通知偏移量并计算通知地址
    uint16_t off = ctx->common->queue_notify_off;
    uintptr_t addr = ctx->notify_base + off * ctx->notify_multiplier;
    ctx->notify_by_vqn[index] = (struct virtio_pci_notify *)addr;

    return true;

err_free_msix:
    pci_msix_free_vector(priv->pdev, msix);
    return false;
}

static void virtio_pci_ops_notify(struct virtioqueue *vq, struct device *dev, uint64_t data) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_transport_priv *ctx = priv->transport_priv;
    uint32_t hw_data;
    uint8_t flag;
    uint16_t vqn;
    uint16_t off;
    uint8_t wrap;

    VIRTIO_UNPACK_NOTIFICATION_DATA(data, hw_data, flag);
    VIRTIO_UNPACK_NOTIFICATION_DATA32(hw_data, vqn, off, wrap);

    // vqn 不能超过 CPU 核心数
    if (vq->queue_index >= bootboot->numcores)
        return;

    struct virtio_pci_notify *notify = ctx->notify_by_vqn[vq->queue_index];
    if (!notify)
        return;

    if (flag == 0)
        notify->notify16 = vqn;
    else
        notify->notify32 = hw_data;
}

static struct virtio_dev_ops ops = {
    .init = virtio_pci_ops_init,
    .destroy = virtio_pci_ops_destroy,
    .get_status = virtio_pci_ops_get_status,
    .set_status = virtio_pci_ops_set_status,
    .read_device_config = virtio_pci_ops_read_device_config,
    .write_device_config = virtio_pci_ops_write_device_config,
    .write_features = virtio_pci_ops_write_features,
    .set_vq = virtio_pci_ops_set_vq,
    .notify = virtio_pci_ops_notify,
};

// 初始化
void virtio_pci_init(void) {
    INIT_LIST_HEAD(&virtio_bus_type.devices);
    INIT_LIST_HEAD(&virtio_bus_type.drivers);
    INIT_LIST_HEAD(&virtio_bus_type.node);

    // 分配并初始化 VirtIO 总线私有数据
    struct virtio_bus_priv *bus_priv = kheap_alloc(sizeof(*bus_priv));
    if (!bus_priv)
        return;

    bus_priv->ops = &ops;
    virtio_bus_type.priv = bus_priv;

    // 注册 VirtIO 总线(用于后续虚拟节点挂载)
    if (!drivers_add_bus(&virtio_bus_type)) {
        kheap_free(bus_priv);
        virtio_bus_type.priv = NULL;
        return;
    }

    // 创建全局控制器设备
    struct device *dev = kheap_alloc(sizeof(*dev));
    if (!dev)
        return;

    memset(dev, 0, sizeof(*dev));

    dev->name = "virtio-pci";
    dev->bus = &virtio_bus_type;

    atomic_init(&dev->refcnt, 0);
    INIT_LIST_HEAD(&dev->children);
    INIT_LIST_HEAD(&dev->sibling);
    INIT_LIST_HEAD(&dev->node);
    INIT_LIST_HEAD(&dev->unmatched_node);

    // 保存指针放到全局变量
    virtio_pci_dev = dev;

    // 此时全局指针指向当前 dev，增加引用
    device_ref_get(virtio_pci_dev);

    // 注册到驱动框架
    drivers_add_device(virtio_pci_dev);

    // 注册 virtio-pci PCI 驱动
    drivers_add_driver(&virtio_pci_driver);
}

INITCALL(drivers, 0, virtio_pci_init);