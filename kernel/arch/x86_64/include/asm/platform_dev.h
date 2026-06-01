/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <drivers.h>

extern struct bus platform_bus_type;

// 初始化
bool platform_dev_init(void);

// 获取段组号
uint16_t platform_get_segment(struct device *dev);