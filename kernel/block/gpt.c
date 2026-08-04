/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <stdint.h>
#include <stdbool.h>
#include <shizi/types.h>
#include <kio.h>
#include <crc32.h>
#include <dynarr.h>

/**
 * EFI 系统分区 (ESP)
 * 
 * UUID 字符串: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
 */
#define GUID_EFI_SYSTEM_PARTITION \
    ((uint8_t[16]){0xC1,0x2A,0x73,0x28,0xF8,0x1F,0x11,0xD2,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B})

/**
 * Linux 通用数据分区
 * 
 * UUID 字符串: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
 * 用于: Linux 文件系统分区
 */
#define GUID_LINUX_DATA_PARTITION \
    ((uint8_t[16]){0x0F,0xC6,0x3D,0xAF,0x84,0x83,0x47,0x72,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4})

/**
 * ShiziOS 系统分区 
 * 
 * UUID 字符串: 22f58447-2882-5023-ae1d-7d8750dae7fd
 * 用途: 标记 ShiziOS 系统根目录 (/system)
 * 该 GUID 应与 SHIZIOS_ATTR_MANAGED 属性位配合使用
 */
#define GUID_SHIZIOS_SYSTEM_PARTITION \
    ((uint8_t[16]){0x22,0xF5,0x84,0x47,0x28,0x82,0x50,0x23,0xAE,0x1D,0x7D,0x87,0x50,0xDA,0xE7,0xFD})

/**
 * ShiziOS 用户数据分区
 * 
 * UUID 字符串: 2eef4580-8b6d-53ff-9e55-bb56bc023ffb
 * 用途: 标记 ShiziOS 用户目录 (/<user>/home)
 * 该 GUID 应与 SHIZIOS_ATTR_MANAGED 属性位配合使用
 */
#define GUID_SHIZIOS_HOME_PARTITION \
    ((uint8_t[16]){0x2E,0xEF,0x45,0x80,0x8B,0x6D,0x53,0xFF,0x9E,0x55,0xBB,0x56,0xBC,0x02,0x3F,0xFB})

#define GPT_PRINT(fmt, ...) \
    printk("[GPT] " fmt, ##__VA_ARGS__)
    
typedef enum : uint64_t {
    // 规范定义
    SHIZIOS_ATTR_REQUIRED_SYSTEM   = 1ULL << 0,   // 系统必需分区
    SHIZIOS_ATTR_NO_BLOCK_IO       = 1ULL << 1,   // 固件不提供 Block I/O
    SHIZIOS_ATTR_LEGACY_MBR        = 1ULL << 2,   // 混合 MBR 标识

    // ShiziOS 自定义(bit 16-47)
    SHIZIOS_ATTR_MANAGED           = 1ULL << 16,  // ShiziOS 托管分区
} gpt_part_attr;

typedef struct {
    uint8_t signature[8];           // 固定签名，ASCII 字符串 "EFI PART"，用于识别 GPT 头
    uint32_t revision;               // GPT 头版本号，固定为 0x00010000
    uint32_t header_size;            // GPT 头的大小（字节）
    uint32_t header_crc32;           // GPT 头自身的 CRC32 校验值
    uint32_t reserved;               // 保留字段，必须为 0
    uint64_t current_lba;            // 此 GPT 头所在的 LBA
    uint64_t backup_lba;             // 备份 GPT 头所在的 LBA
    uint64_t first_usable_lba;       // 分区可用的起始 LBA
    uint64_t last_usable_lba;        // 分区可用的结束 LBA
    uint8_t disk_guid[16];          // 整个磁盘的全局唯一标识符
    uint64_t partition_entry_lba;    // 分区条目数组的起始 LBA
    uint32_t partition_entry_count;  // 分区条目数组中的条目数量
    uint32_t partition_entry_size;   // 每个分区条目的大小（字节，规范要求为 128）
    uint32_t partition_table_crc32;  // 整个分区条目数组的 CRC32 校验值
} __attribute__((packed)) gpt_header_t;

typedef struct {
    uint8_t partition_type_guid[16]; // 分区类型 GUID
    uint8_t unique_guid[16];         // 该分区的唯一 GUID
    uint64_t starting_lba;            // 分区的起始 LBA（绝对扇区号）
    uint64_t ending_lba;              // 分区的结束 LBA（包含此扇区）
    gpt_part_attr attributes;             // 分区属性标志
    uint16_t partition_name[36];      // 分区名称，UTF‑16LE 编码，无 null 终止符
} __attribute__((packed)) gpt_entry_t;

/**
 * 检查是否存在保护性 MBR
 *
 * @param dev 设备号
 */
static bool gpt_check_protective_mbr(dev_t dev) {
    uint8_t mbr[512];
    int ret;

    // 读取 LBA 0
    ret = block_read_sectors(dev, 0, mbr, 1);
    if (ret < 0) {
        return false;
    }

    // 检查 MBR 签名
    uint16_t signature = *(uint16_t *)(mbr + 0x1FE);
    if (signature != 0xAA55) {
        // 不是有效 MBR，自然也不是保护性 MBR
        return false;
    }

    // 保护性 MBR 的分区类型为 0xEE
    if (mbr[0x1BE + 4] == 0xEE) {
        return true;
    }

    return false;
}

/**
 * 读取并验证 GPT 头部
 *
 * @param dev 设备号
 * @param header 输出参数，存储验证通过后的 GPT 头部数据
 */
bool gpt_check_header(dev_t dev, gpt_header_t *header) {
    uint8_t buf[512];
    int ret;
    uint32_t crc_calc;
    uint32_t stored_crc;

    // 读取 LBA 1
    ret = block_read_sectors(dev, 1, buf, 1);
    if (ret < 0)
        return false;

    // 用于后续 crc32 计算
    memcpy(header, buf, sizeof(gpt_header_t));

    if (memcmp(header->signature, "EFI PART", 8) != 0)
        return false;

    if (header->revision != 0x00010000)
        return false;

    // 头部大小至少能容纳标准头部，且不超过单个扇区
    if (header->header_size < sizeof(gpt_header_t) || header->header_size > sizeof(buf))
        return false;

    if (header->current_lba != 1)
        return false;

    if (header->partition_entry_lba <= 1)
        return false;

    if (header->partition_entry_count == 0)
        return false;

    // 分区条目大小至少能容纳标准分区条目
    if (header->partition_entry_size < sizeof(gpt_entry_t))
        return false;

    // 验证头部 CRC32
    stored_crc = header->header_crc32;
    memset(buf + 16, 0, 4);                 // 将 buf 中的 CRC 字段置零
    crc_calc = crc32(0, buf, header->header_size);

    if (crc_calc != stored_crc)
        return false;

    return true;
}

/**
 * 读取分区表并验证其 CRC32
 *
 * @param dev 设备号
 * @param header 已验证的 GPT 头部
 * @param raw_data 输出参数，用于存储原始分区表字节的 dynarr
 */
static bool gpt_check_partition_table(
    dev_t dev,
    const gpt_header_t *header,
    dynarr_t *raw_data
) {
    uint64_t total_size;
    uint64_t sectors;
    uint8_t sector_buf[512];
    int ret;

    // 计算分区表总大小（字节）
    total_size = (uint64_t)header->partition_entry_count * header->partition_entry_size;
    if (total_size == 0)
        return false;

    // 计算需要读取的扇区数
    sectors = (total_size + 511) / 512;

    // 逐扇区读取，将原始字节追加到 raw_data
    for (uint64_t i = 0; i < sectors; i++) {
        ret = block_read_sectors(dev, header->partition_entry_lba + i, sector_buf, 1);
        if (ret < 0)
            return false;

        // 将当前扇区的每个字节追加到 dynarr
        for (size_t j = 0; j < 512; j++) {
            if (!dynarr_append(raw_data, &sector_buf[j]))
                return false;
        }
    }

    // 计算 CRC32（仅计算实际大小的部分，忽略最后一个扇区的多余字节）
    uint32_t crc_calc = crc32(0, raw_data->arr, total_size);
    return crc_calc == header->partition_table_crc32;
}

/**
 * 从已验证的原始分区表中提取有效分区信息
 *
 * @param header 已验证的 GPT 头部
 * @param raw_data 包含原始分区表字节的 dynarr
 * @param partitions 输出参数，用于存储结构化分区信息的 dynarr
 */
static bool gpt_extract_partitions(
    const gpt_header_t *header,
    const dynarr_t *raw_data,
    dynarr_t *partitions
) {
    uint64_t count = header->partition_entry_count;
    uint64_t entry_size = header->partition_entry_size;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t offset = i * entry_size;
        const gpt_entry_t *entry;

        entry = (const gpt_entry_t *)((const uint8_t *)raw_data->arr + offset);

        // 未使用的分区条目（starting_lba == 0）
        if (entry->starting_lba == 0)
            continue;

        // 结束 LBA 不应小于起始 LBA
        if (entry->ending_lba < entry->starting_lba)
            continue;

        struct block_partition_info info;

        info.start_lba = entry->starting_lba;
        info.end_lba = entry->ending_lba;
        info.size = entry->ending_lba - entry->starting_lba + 1;

        memcpy(info.type_guid, entry->partition_type_guid, 16);
        memcpy(info.part_guid, entry->unique_guid, 16);

        info.attributes = entry->attributes;

        memcpy(info.name, entry->partition_name, sizeof(info.name));

        // 追加到 partitions dynarr
        if (!dynarr_append(partitions, &info))
            return false;
    }

    return true;
}

/**
 * 解析 GPT 分区表
 *
 * @param dev 设备号
 * @param partitions 输出参数，存储解析出的分区信息（struct block_partition_info）
 */
bool gpt_parse(dev_t dev, dynarr_t *partitions) {
    gpt_header_t header;
    dynarr_t *raw_data = NULL;
    bool ret = false;

    if (gpt_check_protective_mbr(dev))
        GPT_PRINT("Protective MBR detected\n");

    if (!gpt_check_header(dev, &header))
        goto out;

    raw_data = dynarr_create(sizeof(uint8_t), 0);
    if (!raw_data)
        goto out;

    if (!gpt_check_partition_table(dev, &header, raw_data))
        goto out_free;

    if (!gpt_extract_partitions(&header, raw_data, partitions))
        goto out_free;

    ret = true;

out_free:
    dynarr_destroy(raw_data);
out:
    return ret;
}