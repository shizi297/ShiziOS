/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

// 设备号
typedef uint32_t dev_t; 

// 块计数类型
typedef int64_t blkcnt_t;

// 块大小
typedef int64_t blksize_t;

// 驱动框架初始化
void drivers_init(void);

// 生成一个新的匿名对象 ID
int drivers_get_anon_id(dev_t *dev);

// 释放一个匿名对象 ID
void drivers_free_anon_id(dev_t dev);