/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <serial.h>
#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#define ACPI_PRINT(str) \
    serial_puts("[ACPI] " str "\n")

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
bool acpi_init(void) {
    // 传0表示使用默认选项
    uacpi_status status = uacpi_initialize(0);
    if (status != UACPI_STATUS_OK) {
        return false;
    }

    ACPI_PRINT("ACPI init succeed\n");

    return true;
}

/**
 * 加载ACPI命名空间
 * 
 * @return 成功： true 
 * @return 失败： false
 */
bool acpi_namespace_load(void) {
    uacpi_status status = uacpi_namespace_load();
    if (status != UACPI_STATUS_OK) {
        return false;
    }

    ACPI_PRINT("ACPI namespace load succeed\n");

    return true;
}

/**
 * 初始化ACPI命名空间
 * 
 * @return 成功： true 
 * @return 失败： false
 */
bool acpi_namespace_init(void) {
    uacpi_status status = uacpi_namespace_initialize();
    if (status != UACPI_STATUS_OK) {
        return false;
    }

    ACPI_PRINT("ACPI namespace init succeed\n");

    return true;
}

/**
 * 遍历指定类型的ACPI表中的所有子表
 *
 * @param type 要遍历的ACPI表类型
 * @param callback 每个子表
 * @param context 传递给回调的上下文指针
 * 
 * @return 成功： true
 * @return 失败： false
 */
bool acpi_foreach_subtable(
    acpi_table_type type,
    uacpi_subtable_iteration_callback callback,
    void *context
) {
    uacpi_table table = {0};
    uacpi_status status;
    bool result = false;
    const acpi_table_info *info = NULL;

    // 有标准子表的表,直接通过通用接口查找表头并遍历子表
    if (
        type == ACPI_TABLE_MADT ||
        type == ACPI_TABLE_SRAT ||
        type == ACPI_TABLE_RHCT
    ) {
        info = &acpi_table_headers[type];
        status = uacpi_table_find_by_signature((const uacpi_char *)info->name, &table);
        if (status != UACPI_STATUS_OK)
            goto DONE;

        status = uacpi_for_each_subtable(table.hdr, info->size, callback, context);
        if (status != UACPI_STATUS_OK)
            goto DONE;

        result = true;
        goto DONE;
    } else if (
        type == ACPI_TABLE_ECDT ||
        type == ACPI_TABLE_MCFG ||
        type == ACPI_TABLE_HPET
    ) {
        // 通过签名查找
        info = &acpi_table_headers[type];
        status = uacpi_table_find_by_signature((const uacpi_char *)info->name, &table);
        if (status != UACPI_STATUS_OK)
            goto DONE;
        uacpi_iteration_decision dec = callback(context, (struct acpi_entry_hdr *)table.hdr);
        if (dec == UACPI_ITERATION_DECISION_CONTINUE || dec == UACPI_ITERATION_DECISION_BREAK) {
            result = true;
        }
        goto DONE;
    } else {
        // 对于其他表，调用专用接口
        switch (type) {
            case ACPI_TABLE_FADT: {
                struct acpi_fadt *fadt = NULL;
                status = uacpi_table_fadt(&fadt);
                if (status != UACPI_STATUS_OK)
                    goto DONE;
                // 调用回调，传递整个表头
                uacpi_iteration_decision dec = callback(context, (struct acpi_entry_hdr *)fadt);
                if (dec == UACPI_ITERATION_DECISION_CONTINUE || dec == UACPI_ITERATION_DECISION_BREAK) {
                    result = true;
                }
                goto DONE;
            }

            default:
                // 不支持的表类型
                goto DONE;
        }
    }

DONE:
    if (table.ptr != NULL)
        uacpi_table_unref(&table);
    return result;
}