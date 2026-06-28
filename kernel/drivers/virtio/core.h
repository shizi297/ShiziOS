/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <shizi/types.h>
#include <drivers/drivers.h>
#include <drivers/virtio.h>
#include <stdatomic.h>

#define VIRTIO_PACK_NOTIFICATION_DATA(vqn, off, wrap, flag) \
    (((uint64_t)((flag) & 1) << 32) | \
     ((uint64_t)((uint32_t)(vqn) & 0xFFFF) | \
      ((uint32_t)((off) & 0x7FFF) << 16) | \
      ((uint32_t)((wrap) & 1) << 31)))

#define VIRTIO_UNPACK_NOTIFICATION_DATA(val, hw_data, flag) \
    do { \
        (hw_data) = (uint32_t)((val) & 0xFFFFFFFFULL); \
        (flag) = (uint8_t)(((val) >> 32) & 1ULL); \
    } while(0)

#define VIRTIO_UNPACK_NOTIFICATION_DATA32(hw_data, vqn, off, wrap) \
    do { \
        (vqn) = (uint16_t)((hw_data) & 0xFFFF); \
        (off) = (uint16_t)(((hw_data) >> 16) & 0x7FFF); \
        (wrap) = (uint8_t)(((hw_data) >> 31) & 1); \
    } while(0)

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
#define VIRTIO_F_RING_RESET         40   // 支持单独重置 virtioqueue
#define VIRTIO_F_START              VIRTIO_F_NOTIFY_ON_EMPTY
#define VIRTIO_F_END                VIRTIO_F_RING_RESET
#define VIRTIO_FEATURES_BITS        VIRTIO_F_END - VIRTIO_F_START + 1

#define VIRTIO_DESC_SIZE      sizeof(struct virtio_desc)
#define VIRTIO_AVAIL_HEADER   offsetof(struct virtio_avail, ring)
#define VIRTIO_AVAIL_ENTRY    sizeof(uint16_t)
#define VIRTIO_USED_HEADER  offsetof(struct virtio_used, ring)
#define VIRTIO_USED_ENTRY   sizeof(struct virtio_used_elem)

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
     (1ULL << VIRTIO_F_ORDER_PLATFORM) | \
     (1ULL << VIRTIO_F_SR_IOV))

struct virtioqueue;

// 描述符
struct virtio_desc {
    uint64_t addr;
    uint32_t len;
    union {
        struct {
            uint16_t flags;  
            uint16_t next;   // 链式描述符的下一个索引
        } split;
        
        struct {
            uint16_t id;     // 缓冲区 ID
            uint16_t flags; 
        } packed;
    };
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
    uint16_t queue_index;       // 队列索引，由传输层填充
    uint16_t queue_notify_data; // 由传输层填充，仅在 VIRTIO_F_NOTIF_CONFIG_DATA 协商后有效
    uint16_t notify_default_idx;    // 默认索引，由传输层填充

    void *ring;                 // 描述符环虚拟地址
    uint64_t ring_phys;         // 描述符环物理地址
    uint64_t size;              // 队列大小

    uint64_t desc_phys;         // Descriptor Table 基址
    uint64_t avail_phys;        // Available Ring 基址
    uint64_t used_phys;         // Used Ring 基址

    uint16_t last_used_idx;     // 最后处理的 used 条目
    struct virtio_desc *desc;   // 描述符表虚拟地址

    // Split Ring 专用
    struct {
        uint16_t free_desc;         // 下一个空闲描述符
        struct virtio_avail *avail;  // 请求环指针
        struct virtio_used *used;    // 完成环指针
    } split;

    // Packed Ring 专用
    struct {
        uint16_t next_avail_idx;    // 下一次分配的起始位置
        uint8_t avail_wrap_count;  // 驱动 wrap 计数器 
        uint8_t used_wrap_count;   // 设备 wrap 计数器 
    } packed;
};

struct virtio_bus_priv {
    struct virtio_dev_ops *ops;   
};

struct virtio_transport_priv;

// 根据特性位填充的表
struct virtio_feature_ops {
    /**
     * 创建间接描述符表
     *
     * @param vq 队列指针
     * @param dev 虚拟设备
     * @param count 间接描述符表中条目的数量
     * @param desc_idx 输出参数，返回主描述符索引（指向间接表）
     */
    bool (*create_indirect)(
        struct virtioqueue *vq,
        struct device *dev,
        uint32_t count,
        uint16_t *desc_idx
    );

    /**
     * 检查是否需要发送通知
     *
     * @param vq 队列指针
     * @param dev 设备指针
     * 
     * @return true 需要通知，false 抑制通知
     */
    bool (*check_notify)(struct virtioqueue *vq, struct device *dev);

    /**
     * 构造通知数据
     *
     * @param vq 队列指针
     * @param dev 设备指针
     * 
     * @return 64 位打包值
     */
    uint64_t (*get_notify_data)(struct virtioqueue *vq, struct device *dev);

    /**
     * 获取指定数量的描述符槽位
     *
     * @param vq 队列指针
     * @param dev 虚拟设备
     * @param count 需要分配的连续槽位数量
     * @param desc_idx 输出参数，返回链表头索引
     * @param last_idx 输出参数，返回链表尾索引
     */
    bool (*alloc_desc)(
        struct virtioqueue *vq,
        struct device *dev,
        uint32_t count,
        uint16_t *desc_idx,
        uint16_t *last_idx
    );

    /**
     * 有顺序的分配实现
     *
     * @param vq 队列指针
     * @param count 需要分配的连续槽位数量
     * @param desc_idx 输出参数，返回链表头索引
     * @param last_idx 输出参数，返回链表尾索引
     */
    bool (*alloc_inorder)(
        struct virtioqueue *vq,
        uint32_t count,
        uint16_t *desc_idx,
        uint16_t *last_idx
    );

    /**
     * 乱序的分配实现
     *
     * @param vq 队列指针
     * @param count 需要分配的连续槽位数量
     * @param desc_idx 输出参数，返回链表头索引
     * @param last_idx 输出参数，返回链表尾索引
     */
    bool (*alloc_noorder)(
        struct virtioqueue *vq,
        uint32_t count,
        uint16_t *desc_idx,
        uint16_t *last_idx
    );

    /**
     * 填充分配的描述符
     *
     * @param vq 队列指针
     * @param desc_idx 描述符链头索引
     * @param sg 描述 I/O 请求的物理缓冲区列表
     * @param out 前 out 个 sg 条目是 Device-readable（驱动写，设备读）
     * @param in 后 in 个 sg 条目是 Device-writable（设备写，驱动读）
     */
    void (*fill_desc)(
        struct virtioqueue *vq,
        uint16_t desc_idx,
        struct scatterlist *sg,
        uint32_t out,
        uint32_t in
    );

    /**
     * 将请求上下文保存到描述符的私有数据区
     *
     * @param vq 队列指针
     * @param desc_idx 描述符链头索引
     * @param last_idx 最后一个数据描述符索引
     * @param req 驱动上下文指针，回收时原样返回
     */
    void (*set_req)(
        struct virtioqueue *vq,
        uint16_t desc_idx,
        uint16_t last_idx,
        void *req
    );

    /**
     * 回收请求项
     *
     * @param vq 队列指针
     * 
     * @return caller_data
     */
    void *(*recycle)(struct virtioqueue *vq);

    /**
     * 队列初始化
     *
     * @param dev 虚拟设备
     * @param vq 队列指针
     * @param size 队列大小（描述符数量），0 表示使用默认值
     */
    bool (*init)(struct device *dev, struct virtioqueue *vq, uint64_t size);

    /**
     * 创建间接描述符表的原始实现
     *
     * @param vq 队列指针
     * @param dev 虚拟设备
     * @param count 间接描述符表中条目的数量
     * @param desc_idx 输出参数，返回主描述符索引（指向间接表）
     */
    bool (*create_indirect_raw)(
        struct virtioqueue *vq,
        struct device *dev,
        uint32_t count,
        uint16_t *desc_idx
    );

    /**
     * 检查通知的原始实现
     *
     * @param vq 队列指针
     * 
     * @return true 需要通知，false 抑制通知
     */
    bool (*check_notify_raw)(struct virtioqueue *vq);

    /**
     * 构造通知数据的底层实现
     *
     * @param vq 队列指针
     * @param dev 设备指针
     * 
     * @return 64 位打包值
     */
    uint64_t (*get_notify_data_raw)(struct virtioqueue *vq, struct device *dev);

    /**
     * 获取 vqn
     * 
     * @param vq 队列指针
     * 
     * @return vqn 
     */
    uint16_t (*get_notify_vqn)(struct virtioqueue *vq);
};

struct virtio_dev_priv {
    uint64_t features;  // 协商后的特性位图
    struct device *pdev;    // 实际的物理设备
    struct virtioqueue **vqs; // 队列指针数组
    uint32_t num_vqs;    // 队列数量
    uint16_t type;  // 设备类型
    struct virtio_feature_ops feature_ops;
    struct virtio_transport_priv *transport_priv;    // 传输层私有数据
};

struct virtio_dev_ops {
    struct virtio_dev_priv *(*init)(struct device *dev);    // 初始化设备
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
    );    // 设置 virtioqueue
    void (*notify)(struct virtioqueue *vq, struct device *dev, uint64_t data);   // 通知设备 virtioqueue 中有新的请求
};

struct virtio_device_id{
    uint32_t type;
};

extern struct bus virtio_bus_type;

int virtio_probe(struct device *dev, struct virtio_dev_ops *ops, struct device *parent);
void virtio_remove(struct device *phys_dev, struct device *virtio_root);