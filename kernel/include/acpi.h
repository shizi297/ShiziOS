/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * acpi初始化
 * 
 * @return 成功： true 
 * @return 失败： false
 */
bool acpi_init(void);

/**
 * 加载ACPI命名空间
 * 
 * @return 成功： true 
 * @return 失败： false
 */
bool acpi_namespace_load(void);

/**
 * 初始化ACPI命名空间
 * 
 * @return 成功： true 
 * @return 失败： false
 */
bool acpi_namespace_init(void);