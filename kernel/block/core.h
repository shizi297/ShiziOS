/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <dynarr.h>
#include <drivers.h>
#include <block.h>

/**
 * 块设备分区信息结构体
 * 存储从 GPT 解析出的单个分区的完整参数
 * 用于后续创建设备节点或传递给用户空间
 */
struct block_partition_info {
    uint64_t start_lba;          // 分区起始 LBA（相对于整个磁盘）
    uint64_t end_lba;            // 分区结束 LBA（包含）
    uint64_t size;               // 分区总扇区数（end_lba - start_lba + 1）
    uint8_t type_guid[16];      // 分区类型 GUID
    uint8_t part_guid[16];      // 分区唯一 GUID
    uint64_t attributes;         // 分区属性
    uint16_t name[36];           // 分区名, UTF‑16LE 原始编码
};

/**
 * 解析 GPT 分区表
 *
 * @param dev 设备号
 * @param partitions 输出参数，存储解析出的分区信息（struct block_partition_info）
 */
bool gpt_parse(dev_t dev, dynarr_t *partitions);