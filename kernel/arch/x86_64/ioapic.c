/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */
 
#include <stdint.h>
#include <acpi.h>
#include <serial.h>
#include <mm_addr.h>
#include <heap.h>
#include <ioapic.h>

#define IOAPIC_PRINT(fmt, ...) \
    printk("[IOAPIC] " fmt, ##__VA_ARGS__)

// 寄存器偏移
#define IOAPIC_IOREGSEL 0x00    // 索引寄存器
#define IOAPIC_IOWIN    0x10    // 数据寄存器

// 内部寄存器
#define IOAPIC_VER 0x01 // 版本寄存器的索引

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

struct {
    uintptr_t base;
    uint32_t start_gsi;
    uint32_t gsi_count;
} static ioapic_info = {0};

/**
 * 向ioapic的内部寄存器写入32位值
 * 
 * @param reg 寄存器索引
 * @param val 要写入的值
 */
static inline void ioapic_write(uint32_t reg, uint32_t val) {
    volatile uint32_t *io_sel = (volatile uint32_t *)ioapic_info.base;      // IOREGSEL 在偏移 0
    volatile uint32_t *io_win = (volatile uint32_t *)(ioapic_info.base + 0x10); // IOWIN 在偏移 0x10
    *io_sel = reg;
    *io_win = val;
}

/**
 * 从ioapic的内部寄存器读取32位值
 * 
 * @parma reg 寄存器索引
 * 
 * @return 读到的值
 */
static inline uint32_t ioapic_read(uint32_t reg) {
    volatile uint32_t *io_sel = (volatile uint32_t *)ioapic_info.base;
    volatile uint32_t *io_win = (volatile uint32_t *)(ioapic_info.base + 0x10);
    *io_sel = reg;
    return *io_win;
}

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

    // 映射虚拟地址
    ioapic_info.base = (uintptr_t)vheap_map_mmio(acpi_ioapic_info.base, PAGE_SIZE);
    if (!ioapic_info.base) return false;

    ioapic_info.start_gsi = acpi_ioapic_info.start_gsi;

    // 通过索引窗口读取版本寄存器
    uint32_t reg_val = ioapic_read(IOAPIC_VER);
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

    // 计算重定向表项的索引
    uint32_t index_low = IOAPIC_REDIR_TABLE + 2 * (gsi - ioapic_info.start_gsi);
    uint32_t index_high = index_low + 1;

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

    ioapic_write(index_low, low);
    ioapic_write(index_high, high);

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

    uint32_t index_low = IOAPIC_REDIR_TABLE + 2 * (gsi - ioapic_info.start_gsi);
    uint32_t val = ioapic_read(index_low);
    val |= (1 << IOAPIC_MASK_BIT);
    ioapic_write(index_low, val);
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

    uint32_t index_low = IOAPIC_REDIR_TABLE + 2 * (gsi - ioapic_info.start_gsi);
    uint32_t val = ioapic_read(index_low);
    val &= ~(1 << IOAPIC_MASK_BIT);
    ioapic_write(index_low, val);
}