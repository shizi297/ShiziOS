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

typedef int64_t ssize_t;

// 设备号
typedef uint32_t dev_t; 