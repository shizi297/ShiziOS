/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <shizi/types.h>
#include <drivers/drivers.h>

// 通用特性
#define VIRTIO_F_NOTIFY_ON_EMPTY    24   // Legacy 专用，队列非空变空时通知
#define VIRTIO_F_RESERVED_25        25   // 保留
#define VIRTIO_F_RESERVED_26        26   // 保留
#define VIRTIO_F_ANY_LAYOUT         27   // Legacy 专用，接受任意描述符布局
#define VIRTIO_F_INDIRECT_DESC      28   // 支持间接描述符表
#define VIRTIO_F_EVENT_IDX          29   // 支持抑制通知
#define VIRTIO_F_UNUSED_30          30   // 未使用，禁止协商
#define VIRTIO_F_RESERVED_31        31   // 保留
#define VIRTIO_F_VERSION_1          32   // 符合 1.0 及以上规范，区分 legacy
#define VIRTIO_F_ACCESS_PLATFORM    33   // 设备可能受平台限制，驱动必须接受但是不一定要使用
#define VIRTIO_F_RING_PACKED        34   // 使用 Packed Ring 队列布局
#define VIRTIO_F_IN_ORDER           35   // 设备按驱动提交顺序使用缓冲区
#define VIRTIO_F_ORDER_PLATFORM     36   // 需要平台强内存顺序屏障
#define VIRTIO_F_SR_IOV             37   // 支持单根虚拟化
#define VIRTIO_F_NOTIFICATION_DATA  38   // 驱动通知时携带额外数据
#define VIRTIO_F_NOTIF_CONFIG_DATA  39   // 设备提供 per_vq 通知数据
#define VIRTIO_F_RING_RESET         40   // 支持单独重置 virtqueue
#define VIRTIO_F_START              VIRTIO_F_NOTIFY_ON_EMPTY
#define VIRTIO_F_END                VIRTIO_F_RING_RESET
#define VIRTIO_FEATURES_BITS        VIRTIO_F_END - VIRTIO_F_START + 1

#define VIRTIO_DESC_SIZE      sizeof(struct virtio_desc)
#define VIRTIO_AVAIL_HEADER   offsetof(struct virtio_avail, ring)
#define VIRTIO_AVAIL_ENTRY    sizeof(uint16_t)

#define VIRTIO_DEVICE_NAME_LEN 7

// 驱动要求设备必须具备的能力
#define VIRTIO_REQUIRED_CAPS ((1ULL << VIRTIO_F_VERSION_1))

// 驱动明确禁止启用的特性
#define VIRTIO_FORBIDDEN_FEATURES \
    ((1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) | \
     (1ULL << VIRTIO_F_RESERVED_25)  | \
     (1ULL << VIRTIO_F_RESERVED_26)  | \
     (1ULL << VIRTIO_F_ANY_LAYOUT)   | \
     (1ULL << VIRTIO_F_UNUSED_30)    | \
     (1ULL << VIRTIO_F_RESERVED_31)  | \
     (1ULL << VIRTIO_F_SR_IOV))

struct virtioqueue;

// VirtIO Split Ring 描述符
struct virtio_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// VirtIO Split Ring Available Ring
struct virtio_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];   // 元素数量 = vq->size
} __attribute__((packed));

// VirtIO Split Ring Used Ring
struct virtio_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtio_used {
    uint16_t flags;
    uint16_t idx;
    struct virtio_used_elem ring[];   // 元素数量 = vq->size
} __attribute__((packed));

typedef enum {
    VIRTIO_STATUS_ACKNOWLEDGE = 1,    // 已识别该设备为 virtio 设备
    VIRTIO_STATUS_DRIVER = 2,    // 驱动已加载，知道如何处理此设备
    VIRTIO_STATUS_FEATURES_OK = 8,    // 特性协商完成
    VIRTIO_STATUS_DRIVER_OK = 4,    // 驱动完全就绪，设备可开始工作
    VIRTIO_STATUS_DEVICE_NEEDS_RESET = 64,   // 设备发生致命错误，需要复位
    VIRTIO_STATUS_FAILED = 128,  // 驱动初始化失败，设备不可用
} virtio_status_t;

struct virtioqueue {
    void *ring; // 描述符环虚拟地址
    uint64_t ring_phys; // 描述符环物理地址
    uint64_t size;  // 队列大小
    void (*done_callback)(struct virtioqueue *vq);    // 处理完成后调用

    uint16_t queue_index;   // 队列索引，由传输层在 set_vq 时填充

    uint64_t desc_phys;    // Descriptor Table 基址
    uint64_t avail_phys;   // Available Ring 基址
    uint64_t used_phys;    // Used Ring 基址

    // Split Ring 专用
    struct {
        uint16_t free_desc; // 下一个空闲描述符
        uint16_t last_used_idx; // 最后处理的 used 条目
        struct vring_avail *avail;     // 请求环指针
        struct vring_used *used;       // 完成环指针
    } split;
};

struct virtio_bus_priv {
    struct virtio_dev_ops *ops;   
};

struct virtio_transport_priv;

struct virtio_dev_priv {
    uint64_t features;  // 协商后的特性位图
    struct device *pdev;    // 实际的物理设备
    struct virtioqueue **vqs; // 队列指针数组
    uint32_t num_vqs;    // 队列数量
    uint16_t type;  // 设备类型
    struct virtio_transport_priv *transport_priv;    // 传输层私有数据
};

struct virtio_dev_ops {
    bool (*init)(struct device *dev);    // 初始化设备
    void (*destroy)(struct device *dev);    // 销毁设备，释放资源
    virtio_status_t (*get_status)(struct device *dev);  // 获取设备状态
    void (*set_status)(struct device *dev, virtio_status_t status); // 设置设备状态
    void (*read_device_config)(
        struct device *dev, 
        uint32_t offset, 
        word_t *buf, 
        size_t len
    ); // 从设备配置空间读取数据
    void (*write_device_config)(
        struct device *dev, 
        uint32_t offset, 
        const word_t *buf, 
        size_t len
    );  // 向设备配置空间写入数据
    bool (*set_vq)(
        struct device *dev, 
        uint32_t index, 
        struct virtioqueue *vq,
        uint32_t logical_id, 
        uint32_t vector    
    );    // 设置 virtqueue
    void (*notify)(struct device *dev, struct virtioqueue *vq, int data);   // 通知设备 virtqueue 中有新的请求
};

struct virtio_device_id{
    uint32_t type;
};

extern struct bus virtio_bus_type;

int virtio_probe(struct device *dev, struct virtio_dev_ops *ops, struct device *parent);
void virtio_remove(struct device *dev);