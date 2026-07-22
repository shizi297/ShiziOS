/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <vfs.h>
#include <shizi/types.h>

struct device;
typedef struct drivers_minor_devt drivers_minor_devt;

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

// 分配一个新的主设备号，返回一个次设备号分配器
drivers_minor_devt *drivers_major_alloc(void);

// 初始化次设备号分配器
void drivers_minor_allocator_init(drivers_minor_devt *handle, bool is_dynamic);

// 分配一个次设备号
int drivers_minor_alloc(drivers_minor_devt *handle, dev_t *dev);

// 释放一个次设备号
void drivers_minor_free(drivers_minor_devt *handle, dev_t dev);

// 释放主设备号和次设备号分配器
void drivers_major_free(drivers_minor_devt *handle);

// 注册 fops 到主设备号
void drivers_register_fops(drivers_minor_devt *minor_handle, struct file_operations *fops);

// 注销主设备号的 fops
void drivers_unregister_fops(drivers_minor_devt *minor_handle);