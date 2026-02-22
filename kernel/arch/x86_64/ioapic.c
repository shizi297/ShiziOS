/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */
 
#include <stdint.h>
#include <acpi.h>
#include <serial.h>
#include <mm_addr.h>
#include <ioapic.h>

#define IOAPIC_VER 0x01 // 版本寄存器的偏移

#define IOAPIC_INTR_START_BIT 16    // ioapi重定向表索引起始位 
#define IOAPIC_MAX_INTR(reg_val) \
    ((reg_val >> IOAPIC_INTR_START_BIT) & 0xFF)  // 获取最大重定向表索引
    
#define IOAPIC_REDIR_TABLE 0x10 // 重定向表的起始偏移
#define IOAPIC_REG_BYTE 4   // 单个寄存器的字节数
#define IOAPIC_ENTRY_REGS 2 // 每个条目占用的寄存器
#define IOAPIC_ENTRY_BYTE (IOAPIC_REG_BYTE * IOAPIC_ENTRY_REGS) // 单个条目要用的字节数

#define IOAPIC_MASK_BIT 16
#define IOAPIC_POLARITY_BIT 13
#define IOAPIC_TRIG_BIT 15

#define IOAPIC_PRINT(str) \
    serial_puts("[IOAPIC] " str );

struct {
    uintptr_t base;
    uint32_t start_gsi;
    uint32_t gsi_count;
} static ioapic_info = {0};

/**
 * 初始化ioapic
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool ioapic_init(void) {
    acpi_ioapic_info_struct acpi_ioapic_info = {0};

    bool is_success = acpi_get_ioapic_info(&acpi_ioapic_info);
    if (!is_success) return false;

    // 转为虚拟地址
    ioapic_info.base = (uintptr_t)PHYS_TO_LINEAR(acpi_ioapic_info.base);
    ioapic_info.start_gsi = acpi_ioapic_info.start_gsi;

    volatile uint32_t *reg_ptr = (volatile uint32_t *)(ioapic_info.base + IOAPIC_VER);
    uint32_t reg_val = *reg_ptr;

    uint32_t max_intr = IOAPIC_MAX_INTR(reg_val);
    ioapic_info.gsi_count = max_intr + 1;

    IOAPIC_PRINT("ioapic init success\n");
    return true;
}

/**
 * 注册指定的gsi的中断
 * 
 * @param gsi 全局中断号
 * @param vector 中断向量号
 * @param dest 目标cpu的apicid
 * @param flags 设置标志
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool ioapic_register_gsi(
    uint32_t gsi, 
    uint8_t vector, 
    uint32_t dest, 
    ioapic_flags_t flags
) {
    if (
        gsi >= ioapic_info.start_gsi + ioapic_info.gsi_count ||
        gsi < ioapic_info.start_gsi
    ) return false;

    // 重定向表的地址
    volatile uint32_t *low_addr = 
        (volatile uint32_t*)(
            ioapic_info.base +
            IOAPIC_REDIR_TABLE + 
            IOAPIC_ENTRY_BYTE *
            (gsi - ioapic_info.start_gsi)
        );
    
    volatile uint32_t *high_addr = (volatile uint32_t *)(low_addr + 1);

    /*
     * 构建低32位的值
     * 用于写入第一个寄存器
     * 
     * 传递模式与目标模式固定为0
     */
    uint32_t low = vector & 0xFF;
    if (flags & IOAPIC_MASK) low |= (1 << IOAPIC_MASK_BIT);
    if (flags & IOAPIC_POLARITY) low |= (1 << IOAPIC_POLARITY_BIT);
    if (flags & IOAPIC_TRIG) low |= (1 << IOAPIC_TRIG_BIT);

    uint32_t high = dest;

    *low_addr = low;
    *high_addr = high;

    return true;
}

/**
 * 屏蔽指定gsi的中断
 * 
 * @return gsi 全局中断号
 */
void ioapic_mask_gsi(uint32_t gsi) {
    if (
        gsi >= ioapic_info.start_gsi + ioapic_info.gsi_count ||
        gsi < ioapic_info.start_gsi
    ) return;

    volatile uint32_t *low = 
        (volatile uint32_t*)(
            ioapic_info.base +
            IOAPIC_REDIR_TABLE + 
            IOAPIC_ENTRY_BYTE *
            (gsi - ioapic_info.start_gsi)
        );
    uint32_t val = *low;
    val |= (1 << IOAPIC_MASK_BIT);
    *low = val;
}

/**
 * 取消屏蔽gsi的中断
 * 
 * @return gsi 全局中断号
 */
void ioapic_unmask_gsi(uint32_t gsi) {
    if (
        gsi >= ioapic_info.start_gsi + ioapic_info.gsi_count ||
        gsi < ioapic_info.start_gsi
    ) return;

        volatile uint32_t *low = 
        (volatile uint32_t*)(
            ioapic_info.base +
            IOAPIC_REDIR_TABLE + 
            IOAPIC_ENTRY_BYTE *
            (gsi - ioapic_info.start_gsi)
        );
    uint32_t val = *low;
    val &= ~(1 << IOAPIC_MASK_BIT);
    *low = val;
}