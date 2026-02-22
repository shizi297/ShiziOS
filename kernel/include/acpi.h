/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct{
    uintptr_t base; // ioapic物理基址
    uint32_t start_gsi; // gsi起始编号
}acpi_ioapic_info_struct;

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

/**
 * 获取ioapic信息
 * 
 * @param ioapic_info 数据存放的位置的指针
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool acpi_get_ioapic_info(acpi_ioapic_info_struct *acpi_ioapic_info);