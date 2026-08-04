/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <drivers.h>

struct block_type;

/**
 * 块设备驱动公共头部
 * 所有块设备驱动的私有数据必须以此结构体开头
 */
struct block_hdr {
    /**
     * 从设备读取扇区
     *
     * @param priv 驱动私有数据
     * @param dev 设备指针
     * @param sector 起始扇区号（512 字节）
     * @param buf 数据缓冲区
     * @param sector_count 扇区数量
     */
    int (*read)(
        void *priv,
        struct device *dev,
        uint64_t sector,
        void *buf,
        size_t sector_count
    );

    /**
     * 向设备写入扇区
     *
     * @param priv 驱动私有数据
     * @param dev 设备指针
     * @param sector 起始扇区号（512 字节）
     * @param buf 数据缓冲区
     * @param sector_count 扇区数量
     */
    int (*write)(
        void *priv,
        struct device *dev,
        uint64_t sector,
        const void *buf,
        size_t sector_count
    );

    /**
     * 刷新设备缓存
     *
     * @param priv 驱动私有数据
     * @param dev 设备指针
     */
    int (*flush)(void *priv, struct device *dev);
};

/**
 * 注册设备到块系统
 * 
 * @param name 创建的设备节点名称
 * 
 * @return 句柄
 */
struct block_type *block_register_type(const char *name);

/*
 * 添加设备到块系统
 *
 * @param type 块设备类型句柄
 * @param dev 设备指针
 * @param priv 块设备私有数据
 * @param sector_count 设备总扇区数
 */
int block_add_device(
    struct block_type *type,
    struct device *dev,
    void *priv,
    uint64_t sector_count
);

/*
 * 从块设备层移除设备
 *
 * @param dev 设备指针
 */
int block_remove_device(struct device *dev);

bool block_init(void);

/**
 * 从设备读取扇区
 *
 * @param dev 设备号
 * @param sector 起始扇区号
 * @param buf 数据缓冲区
 * @param count 扇区数量
 */
int block_read_sectors(dev_t dev, uint64_t sector, void *buf, size_t count);

/**
 * 向设备写入扇区
 *
 * @param dev 设备号
 * @param sector 起始扇区号
 * @param buf 数据缓冲区
 * @param count 扇区数量
 */
int block_write_sectors(dev_t dev, uint64_t sector, const void *buf, size_t count);

/**
 * 刷新设备缓存
 *
 * @param dev 设备号
 */
int block_flush(dev_t dev);

