/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

/**
 * VIRTIO ISR 处理入口
 *
 * @param name ISR 函数名
 * @param handle 设备类函数句柄
 * @param data_var 保存 caller_data 的变量名
 * @param body 处理单个请求的代码块（可使用 data_var 访问 caller_data）
 * 
 * 对于 body 部分需要使用 {} 进行包裹逻辑
 */
#define VIRTIO_DRIVER_ISR_ENTRY(name, handle, data_var, body) \
    void name(void) { \
        uint64_t __isr_index = 0; \
        void *data_var; \
        while ((data_var = virtioqueue_process_isr(handle, &__isr_index)) != NULL) { \
            body \
        } \
    }

struct virtio_device_type;

struct scatterlist {
    uint64_t addr;   // 物理地址
    uint32_t length; // 长度
};

struct virtio_device_id{
    uint32_t type;
};

extern struct bus virtio_bus_type;

/**
 * 设置驱动私有数据
 * 
 * @param dev 虚拟设备
 * @param data 数据指针
 */
void virtio_set_drvdata(struct device *dev, void *data);

/**
 * 获取设备私有数据
 * 
 * @param dev 虚拟设备
 * 
 * @return 数据指针
 */
void *virtio_get_drvdata(struct device *dev);

/**
 * 写入特性位
 * 
 * @param dev 虚拟设备
 * @param features 要写入的特性
 */
void virtio_write_features(struct device *dev, uint64_t features);

/**
 * 获取所有支持的特性
 * 
 * @param dev 虚拟设备
 */
uint64_t virtio_get_features(struct device *dev);

/**
 * 从设备配置空间读取数据
 *
 * @param dev 虚拟设备
 * @param offset 配置空间偏移
 * @param buf 接收缓冲区
 * @param len 读取长度
 */
void virtio_read_device_config(struct device *dev, uint32_t offset, void *buf, size_t len);

/**
 * 向设备配置空间写入数据
 *
 * @param dev 虚拟设备
 * @param offset 配置空间偏移
 * @param buf 数据缓冲区
 * @param len 写入长度
 */
void virtio_write_device_config(struct device *dev, uint32_t offset, const void *buf, size_t len);

/*
 * 为一类设备申请句柄
 *
 * @param name 设备类型名
 * 
 * @return 句柄指针
 */
struct virtio_device_type *virtio_register_type(const char *name);

/*
 * 将设备注册到句柄下
 *
 * @param type 句柄
 * @param dev 虚拟设备
 */
bool virtio_register_device(struct virtio_device_type *type, struct device *dev);

/*
 * 从句柄注销设备
 *
 * @param type 句柄
 * @param dev 虚拟设备
 */
void virtio_unregister_device(struct virtio_device_type *type, struct device *dev);

/*
 * 释放句柄
 *
 * @param type 句柄
 */
void virtio_unregister_type(struct virtio_device_type *type);

/**
 * 创建并激活所有队列
 *
 * @param dev 虚拟设备
 * @param queue_size 每个队列的大小（0 表示使用默认值）
 * @param vector 中断向量号
 * @param max_queues 硬件支持的最大队列数
 * @param num_queues_out 输出参数，实际创建的队列数
 */
int virtioqueue_set_all(
    struct device *dev,
    uint64_t queue_size,
    uint32_t vector,
    uint32_t max_queues,
    int *num_queues_out
);

/**
 * 向指定队列提交请求
 *
 * @param dev 虚拟设备
 * @param queue_index 队列索引
 * @param sg 描述数据缓冲区的地址和长度
 * @param out 输出（设备读取）缓冲区数量
 * @param in 输入（设备写入）缓冲区数量
 * @param caller_data 驱动上下文，完成时通过 process_isr 返回
 */
int virtioqueue_add_buf(
    struct device *dev,
    uint32_t queue_index,
    struct scatterlist *sg,
    uint32_t out,
    uint32_t in,
    void *req
);

/**
 * 通知指定队列有新请求
 *
 * @param dev 虚拟设备
 * @param queue_index 队列索引
 */
void virtioqueue_kick(struct device *dev, uint32_t queue_index);

/**
 * 回收所有队列的已完成请求
 * 
 * @param handle 设备类型句柄
 * @param index 存放当前查找进度
 *
 * @return 下一个 caller_data，无完成项时返回 NULL
 */
void *virtioqueue_process_isr(struct virtio_device_type *handle, uint64_t *index);