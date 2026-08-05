/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// 用于标记在公开头文件中只能被架构内部使用的东西
#define _arch 

// 属主用户ID和组ID类型
typedef unsigned int uid_t;
typedef unsigned int gid_t;

typedef struct {
    int err;
    uint64_t val;
} ku64;

typedef struct {
    int err;
    uint32_t val;
} ku32;

typedef struct {
    int err;
    uint16_t val;
} ku16;

typedef struct {
    int err;
    uint8_t val;
} ku8;

typedef struct {
    int err;
    void *ptr;
} kptr;

typedef struct {
    int err;
    uintptr_t val;
} kuptr;

typedef int64_t ssize_t;

// 设备号
typedef uint32_t dev_t; 

// 通用位宽类型
typedef union {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
} word_t;