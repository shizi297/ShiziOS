/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <vfs.h>
#include <shizi/types.h>

// 驱动框架初始化
bool drivers_init(void);

// 生成一个新的匿名对象 ID
int drivers_get_anon_id(dev_t *dev);

// 释放一个匿名对象 ID
void drivers_free_anon_id(dev_t dev);

// 查找对应驱动的文件操作表
struct file_operations *drivers_dev_find(dev_t dev, mode_t mode);