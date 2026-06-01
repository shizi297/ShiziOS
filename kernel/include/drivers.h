/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <vfs.h>
#include <shizi/types.h>

struct device;

// 驱动框架数据初始化
bool drivers_data_init(void);

// 驱动框架初始化
bool drivers_init(void);

// 用于内核模块的 probe 处理入口
void drivers_probe_process_kmodule(void);

// 生成一个新的匿名对象 ID
int drivers_get_anon_id(dev_t *dev);

// 释放一个匿名对象 ID
void drivers_free_anon_id(dev_t dev);

// 查找对应驱动的文件操作表
struct file_operations *drivers_dev_find(dev_t dev, mode_t mode);