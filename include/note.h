/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// ShiziOS ABI 标识字符串
#define NOTE_SHIZIOS_NAME "ShiziOS"

#define NOTE_SHIZIOS_TYPE 1

// 用于标识 ShiziOS 可执行文件
struct note_shizios {
    uint32_t namesz;                         // sizeof(NOTE_SHIZIOS_NAME) 
    uint32_t descsz;                         // 当前为 0，无描述数据
    uint32_t type;                           // NOTE_SHIZIOS_TYPE
    char name[sizeof(NOTE_SHIZIOS_NAME)];    // "ShiziOS\0"
} __attribute__((packed, aligned(4)));