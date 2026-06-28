/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct virtio_device_type;

struct scatterlist {
    uint64_t addr;   // 物理地址
    uint32_t length; // 长度
};

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

/**
 * VIRTIO ISR 处理入口
 *
 * @param name ISR 函数名
 * @param handle 设备类函数句柄
 * @param data_var 保存 caller_data 的变量名
 * @param ... 处理单个请求的代码块（可使用 data_var 访问 caller_data）
 * 
 * 对于 ... 部分需要使用 () 进行包裹逻辑
 */
#define VIRTIO_DRIVER_ISR_ENTRY(name, handle, data_var, ...) \
    void name(void) { \
        uint64_t __isr_index = 0; \
        void *data_var; \
        while ((data_var = virtioqueue_process_isr(handle, &__isr_index)) != NULL) { \
            __VA_ARGS__ \
        } \
    }