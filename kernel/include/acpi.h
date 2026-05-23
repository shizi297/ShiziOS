/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

typedef enum {
    ACPI_TABLE_RSDP,
    ACPI_TABLE_RSDT,
    ACPI_TABLE_XSDT,
    ACPI_TABLE_MADT,
    ACPI_TABLE_FADT,
    ACPI_TABLE_FACS,
    ACPI_TABLE_MCFG,
    ACPI_TABLE_HPET,
    ACPI_TABLE_SRAT,
    ACPI_TABLE_SLIT,
    ACPI_TABLE_DSDT,
    ACPI_TABLE_SSDT,
    ACPI_TABLE_PSDT,
    ACPI_TABLE_ECDT,
    ACPI_TABLE_RHCT,
    ACPI_TABLE_COUNT,
}acpi_table_type;

typedef struct {
    uint16_t size;  // 表固定头部的大小
    char name[16];   // ACPI表签名
} acpi_table_info;

static const acpi_table_info acpi_table_headers[ACPI_TABLE_COUNT] = {
    [ACPI_TABLE_RSDP] = { .size = sizeof(struct acpi_rsdp), .name = ACPI_RSDP_SIGNATURE },
    [ACPI_TABLE_RSDT] = { .size = sizeof(struct acpi_rsdt), .name = ACPI_RSDT_SIGNATURE },
    [ACPI_TABLE_XSDT] = { .size = sizeof(struct acpi_xsdt), .name = ACPI_XSDT_SIGNATURE },
    [ACPI_TABLE_MADT] = { .size = sizeof(struct acpi_madt), .name = ACPI_MADT_SIGNATURE },
    [ACPI_TABLE_FADT] = { .size = sizeof(struct acpi_fadt), .name = ACPI_FADT_SIGNATURE },
    [ACPI_TABLE_FACS] = { .size = sizeof(struct acpi_facs), .name = ACPI_FACS_SIGNATURE },
    [ACPI_TABLE_MCFG] = { .size = sizeof(struct acpi_mcfg), .name = ACPI_MCFG_SIGNATURE },
    [ACPI_TABLE_HPET] = { .size = sizeof(struct acpi_hpet), .name = ACPI_HPET_SIGNATURE },
    [ACPI_TABLE_SRAT] = { .size = sizeof(struct acpi_srat), .name = ACPI_SRAT_SIGNATURE },
    [ACPI_TABLE_SLIT] = { .size = sizeof(struct acpi_slit), .name = ACPI_SLIT_SIGNATURE },
    [ACPI_TABLE_DSDT] = { .size = sizeof(struct acpi_dsdt), .name = ACPI_DSDT_SIGNATURE },
    [ACPI_TABLE_SSDT] = { .size = sizeof(struct acpi_ssdt), .name = ACPI_SSDT_SIGNATURE },
    [ACPI_TABLE_PSDT] = { .size = sizeof(struct acpi_dsdt), .name = ACPI_PSDT_SIGNATURE },  // PSDT表与DSDT表结构相同
    [ACPI_TABLE_ECDT] = { .size = sizeof(struct acpi_ecdt), .name = ACPI_ECDT_SIGNATURE },
    [ACPI_TABLE_RHCT] = { .size = sizeof(struct acpi_rhct), .name = ACPI_RHCT_SIGNATURE },
};

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
 * 遍历指定类型的ACPI表中的所有子表
 *
 * @param type 要遍历的ACPI表类型
 * @param callback 每个子表的回调函数
 * @param context 传递给回调的上下文指针
 * 
 * @return 成功： true
 * @return 失败： false
 */
bool acpi_foreach_subtable(
    acpi_table_type type,
    uacpi_subtable_iteration_callback callback,
    void *context
);