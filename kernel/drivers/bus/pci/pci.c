/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <heap.h>
#include <kio.h>
#include <acpi.h>
#include <initcall.h>
#include <drivers/base/drivers.h>
#include <klibc.h>
#include <asm/msi.h>
#include <drivers/pci.h>

// 每个总线 8MB (256 devices * 8 functions * 4KB)
#define PCI_ECAM_BUS_SIZE  (8ULL * 1024 * 1024)   

// 表示该驱动可以匹配任意厂商或任意设备 ID
#define PCI_ANY_ID 0xFFFF

// ID 无效
#define PCI_VENDOR_ID_INVALID   0x0000

// PCI 配置空间标准寄存器偏移
#define PCI_VENDOR_ID_OFFSET    0x00    // 厂商 ID
#define PCI_DEVICE_ID_OFFSET    0x02    // 设备 ID
/**
 * Header Type 寄存器
 * bit7=1 表示多功能设备
 * 低 7 位表示设备类型（0=普通，1=PCI-PCI 桥，2=CardBus 桥）
 */
#define PCI_HEADER_TYPE_OFFSET  0x0E    
#define PCI_CAP_PTR_OFFSET      0x34    // Capability 指针，指向第一个 Capability 的偏移
#define PCI_BAR0_OFFSET         0x10    // 第一个 BAR 的偏移，BAR0~BAR5 依次递增 4 字节

// BAR 索引到偏移的转换
#define PCI_BAR_OFFSET(i)       (0x10 + (i) * 4)

// 每个总线偏移占 20 位（12位功能内偏移 + 3位功能索引 + 5位设备索引）
#define PCI_ECAM_BUS_SHIFT      20

// 每个设备偏移占 15 位（12位功能内偏移 + 3位功能索引）
#define PCI_ECAM_DEV_SHIFT      15

// 每个功能偏移占 12 位（4KB 配置空间）
#define PCI_ECAM_FUNC_SHIFT     12

// BAR 低几位为标志位，地址需要对齐
#define PCI_BAR_IO_MASK         (~0x3ULL)      // I/O BAR 屏蔽低 2 位（bit0=1 表示 I/O，bit1 保留）
#define PCI_BAR_MEM_MASK        (~0xFULL)      // MMIO BAR 屏蔽低 4 位（bit0=0 表示 MMIO，bit1-2 表示类型，bit3 表示预取）
#define PCI_BAR_MEM_LOW_MASK  0xFULL   // MMIO BAR 低 4 位标志位掩码

// BAR 类型编码（位于 bit1和2，仅对 MMIO 有效）
#define PCI_BAR_TYPE_SHIFT      1
#define PCI_BAR_TYPE_MASK       0x3
#define PCI_BAR_TYPE_32BIT      0x00    // 32 位 MMIO
#define PCI_BAR_TYPE_64BIT      0x02    // 64 位 MMIO（占用连续两个 BAR 寄存器）
#define PCI_BAR_TYPE_RESERVED1  0x01    // 保留
#define PCI_BAR_TYPE_RESERVED3  0x03    // 保留

// 预取标志（仅 MMIO）
#define PCI_BAR_PREFETCHABLE    (1 << 3)    // 等于 1 表示可预取

// Header Type 寄存器位定义
#define PCI_HEADER_TYPE_MULTIFUNC   (1 << 7)    // 等于 1 表示多功能设备
#define PCI_HEADER_TYPE_MASK        0x7F        // 低 7 位表示设备类型
#define PCI_HEADER_TYPE_NORMAL      0x00        // 普通设备
#define PCI_HEADER_TYPE_BRIDGE      0x01        // PCI-to-PCI 桥
#define PCI_HEADER_TYPE_CARDBUS     0x02        // CardBus 桥

// Capability 链表头部
#define PCI_CAP_ID_OFFSET       0       // Capability ID
#define PCI_CAP_NEXT_OFFSET     1       // 下一个 Capability 偏移，0 表示结束

// MSI-X Capability ID
#define PCI_CAP_ID_MSIX         0x11

// MSI-X Capability 内部偏移
#define PCI_MSIX_CTRL_OFFSET    2       // Message Control
#define PCI_MSIX_TABLE_OFFSET   4       // Table Offset/BIR
#define PCI_MSIX_PBA_OFFSET     8       // PBA Offset/BIR

// Message Control 寄存器位定义
#define PCI_MSIX_CTRL_TABLE_SIZE_MASK   0x07FF  // 低 11 位表示表大小（N-1）
#define PCI_MSIX_CTRL_ENABLE_BIT        (1 << 15)   // Enable 位
#define PCI_MSIX_CTRL_FUNC_MASK_BIT     (1 << 14)   // Function Mask 位

// Table / PBA 寄存器中 BIR 字段
#define PCI_MSIX_BIR_MASK       0x7
#define PCI_MSIX_BIR_SHIFT      0

// 桥配置空间偏移
#define PCI_BRIDGE_PRIMARY_BUS_OFFSET   0x18    // 主总线号
#define PCI_BRIDGE_SECONDARY_BUS_OFFSET 0x19    // 次总线号
#define PCI_BRIDGE_SUBORDINATE_BUS_OFFSET 0x1A  // 从属总线号

// 设备枚举限制
#define PCI_MAX_DEVICES         32      // 每个总线最多 32 个设备
#define PCI_MAX_FUNCTIONS       8       // 每个设备最多 8 个功能
#define PCI_MAX_BARS            6       // 最多 6 个 BAR

// 从 Message Control 寄存器值计算 MSI-X 表条目数（低 11 位 + 1）
#define PCI_MSIX_TABLE_ENTRIES(ctrl)   (((ctrl) & PCI_MSIX_CTRL_TABLE_SIZE_MASK) + 1)

// 从 Message Control 寄存器值计算 MSI-X 表总大小（字节），每个条目 16 字节
#define PCI_MSIX_TABLE_SIZE(ctrl)      (PCI_MSIX_TABLE_ENTRIES(ctrl) * 16)

// 32 位 BAR 写全 1 后读回的掩码若为此值，表示 BAR 无效
#define PCI_BAR_MASK_32BIT_ALL  0xFFFFFFFFU

// 64 位 BAR 写全 1 后读回的掩码若为此值，表示 BAR 无效
#define PCI_BAR_MASK_64BIT_ALL  0xFFFFFFFFFFFFFFFFULL

// 用于从读取的 32 位数据中提取指定字节的值
#define PCI_OFFSET_BYTE_SHIFT(offset)  (((offset) & 3) * 8)

// 用于从读取的 32 位数据中提取 16 位字的值（要求 offset 按 2 字节对齐）
#define PCI_OFFSET_WORD_SHIFT(offset)  (((offset) & 2) ? 16 : 0)

// 对齐到4字节，因为 ECAM 按 4 字节访问
#define PCI_ECAM_OFFSET_ALIGN_MASK  (~3)

// PCI 设备名称最小长度（格式 "SSSS:BB:DD.F" 共 12 字符 + 结束符）
#define PCI_DEVICE_NAME_MIN_LEN 13

// PCI 设备名称缓冲区长度
#define PCI_DEVICE_NAME_LEN 16

// 扫描栈深度
#define PCI_SCAN_STACK_DEPTH    32

// Capability 链表遍历最大迭代次数
#define PCI_MAX_CAP_LOOP        256

#define PCI_PRINT(fmt, ...) \
    printk("[PCI] " fmt, ##__VA_ARGS__)

// PCI 总线私有数据
struct pci_bus_priv {
    struct pci_ecam_region *regions;
    int num_regions;
};

// PCI 设备私有结构体
struct pci_dev_priv {
    uint16_t segment;   // // PCI 段组号
    uint8_t bus;    // 总线号
    uint8_t dev;    // 设备号
    uint8_t func;   // 功能号
    uint16_t vendor_id; // 厂商 ID，0xFFFF 表示设备不存在
    uint16_t device_id; // 设备 ID
    uint8_t header_type;    // 配置空间头类型

    // BAR 资源
    struct resource bars[6];
    int num_bars;

    // MSI-X 能力
    uint8_t msix_cap_offset;    // MSI‑X Capability 在配置空间中的偏移（字节）
    uint16_t msix_control;  // Message Control 寄存器，低 11 位为表大小
    uint8_t msix_table_bir; // 指向 MSI‑X 表的 BAR 索引
    uint32_t msix_table_offset; // MSI‑X 表在对应 BAR 中的偏移
    uint8_t msix_pba_bir;   // 指向 MSI‑X PBA 的 BAR 索引
    uint32_t msix_pba_offset;   // PBA 在对应 BAR 中的偏移
    void *msix_table_virt;  // MSI‑X 表映射到内核的虚拟地址  
    size_t msix_table_size; // MSI‑X 表的总大小（字节）
};

// 扫描栈条目，用于记录待扫描的总线及对应的父设备
struct scan_entry {
    uint16_t segment;
    uint8_t bus;
    struct device *parent_dev;
};

// ECAM 区域信息
struct pci_ecam_region {
    uint64_t base_addr; // ECAM 区域的物理基地址
    void *base_virt;    // 映射后的内核虚拟基地址
    uint16_t segment;   // PCI 段组号
    uint8_t start_bus;  // 该区域覆盖的起始总线号
    uint8_t end_bus;    // 该区域覆盖的结束总线号
};

// 用于 PCI 设备的 ID 描述
struct pci_device_id {
    uint16_t vendor;      // 厂商 ID
    uint16_t device;      // 设备 ID
    uint16_t subvendor;   // 子系统厂商 ID
    uint16_t subdevice;   // 子系统设备 ID
    uint32_t class;       // 设备类别
    uint32_t class_mask;  // 类别掩码
    uintptr_t driver_data; // 驱动私有数据
};

static struct bus pci_bus_type;

// 保护 ecam 的访问
static spinlock_t ecam_lock = SPIN_LOCK_INIT;

/**
 * 根据段组和总线号查找对应的 ECAM 区域
 * 
 * @param priv PCI 总线私有数据
 * @param segment PCI 段组号
 * @param bus 总线号
 *
 * @return 找到的 ECAM 区域指针，失败返回NULL
 */
static inline struct pci_ecam_region *pci_ecam_find_region(
    struct pci_bus_priv *priv, 
    uint16_t segment, 
    uint8_t bus
) {
    for (int i = 0; i < priv->num_regions; i++) {
        struct pci_ecam_region *reg = &priv->regions[i];
        if (
            reg->segment == segment && 
            bus >= reg->start_bus && 
            bus <= reg->end_bus
        ) return reg;
    }
    
    return NULL;
}

/**
 * 从 PCI 配置空间读取数据
 * 
 * @param priv PCI 总线私有数据
 * @param segment PCI 段组号
 * @param bus 总线号
 * @param dev 设备号
 * @param func 功能号
 * @param offset 配置空间偏移
 * @param size 读取字节数
 * @param val 输出缓冲区（至少 size 字节）
 */
static bool pci_ecam_read(
    struct pci_bus_priv *priv,
    uint16_t segment,
    uint8_t bus,
    uint8_t dev,
    uint8_t func,
    uint8_t offset,
    int size,
    uint32_t *val
) {
    struct pci_ecam_region *reg;

    spin_lock(&ecam_lock);

    reg = pci_ecam_find_region(priv, segment, bus);
    if (!reg) {
        PCI_PRINT("No ECAM region for segment %u, bus %u\n", segment, bus);
        spin_unlock(&ecam_lock);
        return false;
    }

    uintptr_t addr = 
        (uintptr_t)reg->base_virt + (
            ((bus - reg->start_bus) << PCI_ECAM_BUS_SHIFT) |
            (dev << PCI_ECAM_DEV_SHIFT) |
            (func << PCI_ECAM_FUNC_SHIFT) |
            (offset & PCI_ECAM_OFFSET_ALIGN_MASK)
        );

    uint32_t data = *(volatile uint32_t *)addr;

    switch (size) {
        case 1:
            *val = (data >> PCI_OFFSET_BYTE_SHIFT(offset)) & 0xFF;
            break;
        case 2:
            if (offset & 1) {
                PCI_PRINT("Unaligned 16-bit read at offset %u\n", offset);
                spin_unlock(&ecam_lock);
                return false;
            }
            *val = (data >> PCI_OFFSET_WORD_SHIFT(offset)) & 0xFFFF;
            break;
        case 4:
            if (offset & 3) {
                PCI_PRINT("Unaligned 32-bit read at offset %u\n", offset);
                spin_unlock(&ecam_lock);
                return false;
            }
            *val = data;
            break;
        default:
            spin_unlock(&ecam_lock);
            return false;
    }
    
    spin_unlock(&ecam_lock);
    return true;
}

/**
 * 向 PCI 配置空间写入数据
 * 
 * @param priv PCI 总线私有数据
 * @param segment PCI 段组号
 * @param bus 总线号
 * @param dev 设备号
 * @param func 功能号
 * @param offset 配置空间偏移
 * @param size 写入字节数
 * @param val 要写入的值
 */
static void pci_ecam_write(
    struct pci_bus_priv *priv,
    uint16_t segment,
    uint8_t bus,
    uint8_t dev,
    uint8_t func,
    uint8_t offset,
    int size,
    uint32_t val
) {
    struct pci_ecam_region *reg;

    spin_lock(&ecam_lock);

    reg = pci_ecam_find_region(priv, segment, bus);
    if (!reg) {
        PCI_PRINT("No ECAM region for segment %u, bus %u\n", segment, bus);
        spin_unlock(&ecam_lock);
        return;
    }

    uintptr_t addr = 
        (uintptr_t)reg->base_virt + (
            ((bus - reg->start_bus) << PCI_ECAM_BUS_SHIFT) |
            (dev << PCI_ECAM_DEV_SHIFT) |
            (func << PCI_ECAM_FUNC_SHIFT) |
            (offset & PCI_ECAM_OFFSET_ALIGN_MASK)
        );

    uint32_t old = *(volatile uint32_t *)addr;
    uint32_t new;

    switch (size) {
        case 1: {
            uint32_t shift = PCI_OFFSET_BYTE_SHIFT(offset);
            new = (old & ~(0xFFU << shift)) | ((val & 0xFF) << shift);
            break;
        }
        case 2: {
            if (offset & 1) {
                PCI_PRINT("Unaligned 16-bit write at offset %u\n", offset);
                spin_unlock(&ecam_lock);
                return;
            }
            uint32_t shift = PCI_OFFSET_WORD_SHIFT(offset);
            new = (old & ~(0xFFFFU << shift)) | ((val & 0xFFFF) << shift);
            break;
        }
        case 4: {
            if (offset & 3) {
                PCI_PRINT("Unaligned 32-bit write at offset %u\n", offset);
                spin_unlock(&ecam_lock);
                return;
            }
            new = val;
            break;
        }
        default:
            spin_unlock(&ecam_lock);
            return;
    }

    *(volatile uint32_t *)addr = new;

    spin_unlock(&ecam_lock);
}

// 获取 ECAM 区域数组
static struct pci_ecam_region *pci_get_ecam_info(int *num) {
    uacpi_table table;
    uacpi_status status;
    struct acpi_mcfg *mcfg;
    struct pci_ecam_region *regions = NULL;
    size_t entry_count, i;
    int valid_count = 0, idx = 0;

    // 查找 MCFG 表
    status = uacpi_table_find_by_signature("MCFG", &table);
    if (status != UACPI_STATUS_OK) {
        PCI_PRINT("Failed to find MCFG table: %s\n", uacpi_status_to_string(status));
        return NULL;
    }

    mcfg = (struct acpi_mcfg *)table.ptr;

    // 计算 entries 数量
    entry_count = (mcfg->hdr.length - sizeof(*mcfg)) / sizeof(mcfg->entries[0]);
    if (entry_count == 0) {
        PCI_PRINT("No ECAM entries in MCFG\n");
        goto out;
    }

    // 统计有效条目数
    for (i = 0; i < entry_count; i++) {
        struct acpi_mcfg_allocation *entry = &mcfg->entries[i];
        if (!entry->address)
            continue;

        if (entry->start_bus > entry->end_bus)
            continue;

        valid_count++;
    }

    if (!valid_count) {
        PCI_PRINT("No valid ECAM entries after filtering\n");
        goto out;
    }

    // 分配存储数组
    regions = kheap_alloc(sizeof(struct pci_ecam_region) * valid_count);
    if (!regions) {
        PCI_PRINT("Failed to allocate ECAM region array\n");
        goto out;
    }

    // 填充数组
    for (i = 0; i < entry_count; i++) {
        struct acpi_mcfg_allocation *entry = &mcfg->entries[i];
        if (!entry->address)
            continue;

        if (entry->start_bus > entry->end_bus)
            continue;

        regions[idx].base_addr = entry->address;
        regions[idx].segment   = entry->segment;
        regions[idx].start_bus = entry->start_bus;
        regions[idx].end_bus   = entry->end_bus;
        idx++;
    }

    *num = valid_count;

out:
    uacpi_table_unref(&table);
    return regions;
}

/** 
 * 检查 PCI 设备是否存在
 *
 * @param priv PCI 总线私有数据
 * @param segment 段组号
 * @param bus 总线号
 * @param dev 设备号
 * @param func 功能号
 * @return true 存在，false 不存在
 */
static bool pci_device_exists(
    struct pci_bus_priv *priv,
    uint16_t segment,
    uint8_t bus,
    uint8_t dev,
    uint8_t func
) {
    uint32_t val;
    // 读取 vendor ID
    if (!pci_ecam_read(priv, segment, bus, dev, func, PCI_VENDOR_ID_OFFSET, 2, &val))
        return false;

    uint16_t vendor = val & 0xFFFF;

    // vendor ID 为 0xFFFF 或 0x0000 表示设备不存在
    if (vendor == PCI_ANY_ID || vendor == PCI_VENDOR_ID_INVALID)
        return false;

    return true;
}

/**
 * 分配并初始化 PCI 设备私有结构体，读取基本配置信息
 *
 * @param priv PCI 总线私有数据
 * @param segment 段组号
 * @param bus 总线号
 * @param dev 设备号
 * @param func 功能号
 * @return 返回 pci_dev_priv 指针，失败返回 NULL
 */
static struct pci_dev_priv *pci_alloc_device_priv(
    struct pci_bus_priv *priv,
    uint16_t segment,
    uint8_t bus,
    uint8_t dev,
    uint8_t func
) {
    struct pci_dev_priv *pdev;
    uint32_t val;

    if (!pci_device_exists(priv, segment, bus, dev, func))
        return NULL;

    pdev = kheap_alloc(sizeof(*pdev));
    if (!pdev) {
        PCI_PRINT("Out of memory for PCI private data\n");
        return NULL;
    }
    memset(pdev, 0, sizeof(*pdev));

    pdev->segment = segment;
    pdev->bus = bus;
    pdev->dev = dev;
    pdev->func = func;

    // 读取 vendor ID
    if (!pci_ecam_read(priv, segment, bus, dev, func, PCI_VENDOR_ID_OFFSET, 2, &val))
        goto fail;

    pdev->vendor_id = val & 0xFFFF;

    // 读取 device ID
    if (!pci_ecam_read(priv, segment, bus, dev, func, PCI_DEVICE_ID_OFFSET, 2, &val))
        goto fail;

    pdev->device_id = val & 0xFFFF;

    // 读取 header type
    if (!pci_ecam_read(priv, segment, bus, dev, func, PCI_HEADER_TYPE_OFFSET, 1, &val))
        goto fail;

    pdev->header_type = val & 0xFF;

    return pdev;

fail:
    kheap_free(pdev);
    return NULL;
}

/**
 * 生成 PCI 设备名称，格式 "SSSS:BB:DD.F"
 *
 * @param buf 输出缓冲区（至少 13 字节）
 * @param size 缓冲区大小
 * @param segment 段组号
 * @param bus 总线号
 * @param dev 设备号
 * @param func 功能号
 */
static void pci_format_device_name(
    char *buf,
    size_t size,
    uint16_t segment,
    uint8_t bus,
    uint8_t dev,
    uint8_t func
) {
    const char hex[] = "0123456789ABCDEF";
    if (size < PCI_DEVICE_NAME_MIN_LEN)
        return;

    buf[0] = hex[(segment >> 12) & 0xF];
    buf[1] = hex[(segment >> 8) & 0xF];
    buf[2] = hex[(segment >> 4) & 0xF];
    buf[3] = hex[segment & 0xF];
    buf[4] = ':';
    buf[5] = hex[(bus >> 4) & 0xF];
    buf[6] = hex[bus & 0xF];
    buf[7] = ':';
    buf[8] = hex[(dev >> 4) & 0xF];
    buf[9] = hex[dev & 0xF];
    buf[10] = '.';
    buf[11] = hex[func & 0xF];
    buf[12] = '\0';
}

/**
 * 读取 PCI BAR 的原始单个寄存器值
 *
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 * @param offset 配置空间偏移（字节）
 * @param val 输出值
 */
static bool pci_bar_read_raw(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev,
    uint8_t offset,
    uint32_t *val
) {
    return pci_ecam_read(
        priv,
        pdev->segment, pdev->bus,
        pdev->dev, pdev->func,
        offset, 4, val
    );
}

/**
 * 获取寄存器掩码
 *
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 * @param offset 配置空间偏移（字节）
 * @param orig_val 原始 BAR 值
 * @param mask 输出掩码
 */
static bool pci_get_bar_mask(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev,
    uint8_t offset,
    uint32_t orig_val,
    uint32_t *mask
) {
    // 向 BAR 写入全 1，设备会将可写地址位设为 1，固定标志位保持原样
    pci_ecam_write(
        priv,
        pdev->segment, pdev->bus,
        pdev->dev, pdev->func,
        offset, 4, PCI_BAR_MASK_32BIT_ALL
    );

    // 读回掩码，低 N 位为 0（表示固定位），高 M 位为 1（表示可写地址范围）

    if (!pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            offset, 4, mask
        )) {

        // 读取失败，恢复原值避免设备状态被破坏
        pci_ecam_write(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            offset, 4, orig_val
        );
        return false;
    }

    // 恢复 BAR 原始值，不影响设备配置
    pci_ecam_write(
        priv,
        pdev->segment, pdev->bus,
        pdev->dev, pdev->func,
        offset, 4, orig_val
    );

    return true;
}

/**
 * 解析单个 PCI BAR，返回基址和大小以及资源标志
 *
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 * @param bar_idx BAR 索引
 * @param is_64bit 是否为 64 位 BAR
 * @param base 输出基址（64 位）
 * @param size 输出大小（字节）
 * @param flags 输出资源标志
 */
static bool pci_get_bar_info(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev,
    int bar_idx,
    bool is_64bit,
    uint64_t *base,
    uint64_t *size,
    uint32_t *flags
) {
    uint8_t offset_low = PCI_BAR_OFFSET(bar_idx);
    uint32_t bar_low, bar_high = 0;
    uint32_t mask_low, mask_high = 0;
    uint64_t bar_mask;

    // 读取低 32 位原始值
    if (!pci_bar_read_raw(priv, pdev, offset_low, &bar_low))
        return false;

    // 判断类型：bit0 = 1 为 I/O 端口，0 为 MMIO
    bool is_io = (bar_low & 1);
    *flags = is_io ? IORESOURCE_IO : IORESOURCE_MEM;

    // 获取低 32 位掩码（通过写全 1 读回）
    if (!pci_get_bar_mask(priv, pdev, offset_low, bar_low, &mask_low))
        return false;

    if (is_64bit) {
        // 64 位 MMIO BAR，需要读取下一个连续的 32 位寄存器
        if (bar_idx + 1 >= PCI_MAX_BARS) {
            PCI_PRINT("64-bit BAR %d needs next register, out of range\n", bar_idx);
            return false;
        }

        uint8_t offset_high = offset_low + 4;
        if (!pci_bar_read_raw(priv, pdev, offset_high, &bar_high))
            return false;

        // 获取高 32 位掩码
        if (!pci_get_bar_mask(priv, pdev, offset_high, bar_high, &mask_high))
            return false;

        // 组合 64 位掩码
        bar_mask = ((uint64_t)mask_high << 32) | mask_low;

        /*
         * 64 位 MMIO BAR 的低 4 位是硬编码的标志位
         * 不是地址掩码
         * 强制置 1 以保证 size 正确对齐到 16 字节 
         */
        bar_mask |= PCI_BAR_MEM_LOW_MASK;

        // 掩码为全 1 表示 BAR 无效
        if (bar_mask == PCI_BAR_MASK_64BIT_ALL)
            *size = 0;
        else
            *size = ~bar_mask + 1;      // 地址掩码转大小

        if (*size == 0)
            return false;

        /*
         * 计算 PCI 设备 64 位 MMIO BAR 的物理基址
         * 高 32 位左移
         * 低 32 位去掉低 4 位标志（MMIO 需要 16 字节对齐）
         */
        *base = ((uint64_t)bar_high << 32) | (bar_low & PCI_BAR_MEM_MASK);
    } else {
        // 32 位 BAR
        bar_mask = mask_low;

        // 掩码为 0 或全 1 表示 BAR 无效
        if (bar_mask == 0 || bar_mask == PCI_BAR_MASK_32BIT_ALL)
            *size = 0;
        else
            *size = ~bar_mask + 1;

        if (*size == 0)
            return false;

        /*
         * 去除标志位
         * 对于 I/O BAR 去掉低 2 位
         * 对于 MMIO BAR 去掉低 4 位标志
         */
        if (is_io)
            *base = bar_low & PCI_BAR_IO_MASK;
        else
            *base = bar_low & PCI_BAR_MEM_MASK;
    }

    // 对于 MMIO BAR，检查可预取标志，并设置资源标志
    if (!is_io && (bar_low & PCI_BAR_PREFETCHABLE))
        *flags |= IORESOURCE_PREFETCHABLE;

    return true;
}

/**
 * 解析 PCI 设备的 BAR，将结果存入 dev->res 和 pdev->bars
 *
 * @param dev 设备结构体
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 */
static void pci_parse_bars(
    struct device *dev,
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev
) {
    int bar_count = 0;

    for (int i = 0; i < PCI_MAX_BARS; ) {
        uint32_t bar_low;
        uint8_t offset = PCI_BAR_OFFSET(i);

        // 读取 BAR 低 32 位原始值
        if (!pci_bar_read_raw(priv, pdev, offset, &bar_low)) {
            i++;
            continue;
        }

        // 判断 BAR 类型
        bool is_io = (bar_low & 1);
        uint8_t bar_type = (bar_low >> PCI_BAR_TYPE_SHIFT) & PCI_BAR_TYPE_MASK;

        /*
         * bit0: 0=MMIO, 1=I/O
         * 对于 MMIO
         * bit1-2: 0=32位, 2=64位
         * 1和3是保留值
         * 遇到保留类型跳过，避免错误解析
         */
        if (!is_io && (
                bar_type == PCI_BAR_TYPE_RESERVED1 ||
                bar_type == PCI_BAR_TYPE_RESERVED3
            )
        ) {
            i++;
            continue;
        }

        bool is_64bit = (!is_io && bar_type == PCI_BAR_TYPE_64BIT);
        uint64_t base, size;
        uint32_t flags;

        // 解析当前 BAR
        if (pci_get_bar_info(priv, pdev, i, is_64bit, &base, &size, &flags)) {
            if (bar_count < PCI_MAX_BARS) {
                struct resource *res = &pdev->bars[bar_count];
                res->start = base;
                res->end = base + size - 1;
                res->flags = flags;
                bar_count++;
            }

            // 64 位 BAR 占用两个索引，跳过下一个
            i += is_64bit ? 2 : 1;
        } else {
            i++;
        }
    }

    pdev->num_bars = bar_count;
    dev->res = pdev->bars;
    dev->num_res = bar_count;
}

/**
 * 在 PCI 配置空间的 Capability 链表中查找指定 ID 的能力
 * 
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 * @param cap_id 要查找的 Capability ID
 * 
 * @return 找到时返回 Capability 在配置空间中的偏移（字节），未找到返回 0
 */
static uint8_t _pci_find_capability(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev,
    uint8_t cap_id
) {
    uint32_t val;
    uint8_t cap_ptr;
    int loop = PCI_MAX_CAP_LOOP;

    // 读取 Capability 指针寄存器
    if (!pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            PCI_CAP_PTR_OFFSET, 1, &val
        )
    ) return 0;

    cap_ptr = val & 0xFF;
    if (cap_ptr == 0)
        return 0;

    // 遍历 Capability 链表
    while (cap_ptr != 0 && loop-- > 0) {
        uint8_t id;

        // 读取 Capability ID
        if (!pci_ecam_read(
                priv,
                pdev->segment, pdev->bus,
                pdev->dev, pdev->func,
                cap_ptr, 1, &val
            )
        ) break;

        id = val & 0xFF;

        if (id == cap_id)
            return cap_ptr;   // 找到目标 Capability

        // 读取下一个 Capability 的偏移
        if (!pci_ecam_read(
                priv,
                pdev->segment, pdev->bus,
                pdev->dev, pdev->func,
                cap_ptr + PCI_CAP_NEXT_OFFSET, 1, &val
            )
        ) break;
        cap_ptr = val & 0xFF;
    }

    return 0;
}

/**
 * 从指定的 Capability 偏移处读取 MSI‑X 信息，并填充到 pdev 中
 * 
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 * @param cap_offset MSI‑X Capability 在配置空间中的偏移
 */
static bool pci_read_msix_info(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev,
    uint8_t cap_offset
) {
    uint32_t val;

    // 读取 Message Control 寄存器
    if (!pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            cap_offset + PCI_MSIX_CTRL_OFFSET, 2, &val
        )
    ) return false;

    pdev->msix_control = val & 0xFFFF;

    // 读取 Table Offset 和 BIR
    if (!pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            cap_offset + PCI_MSIX_TABLE_OFFSET, 4, &val
        )
    ) return false;

    pdev->msix_table_bir = val & 0x7;           // BIR
    pdev->msix_table_offset = val & ~0x7;       // 表在 BAR 内的偏移（字节）

    // 读取 PBA Offset 和 BIR
    if (!pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            cap_offset + PCI_MSIX_PBA_OFFSET, 4, &val
        )
    ) return false;

    pdev->msix_pba_bir = val & 0x7;
    pdev->msix_pba_offset = val & ~0x7;

    // 计算表大小：条目数 = (msix_control 的低 11 位) + 1，每个条目 16 字节
    size_t table_entries = (pdev->msix_control & PCI_MSIX_CTRL_TABLE_SIZE_MASK) + 1;
    pdev->msix_table_size = table_entries * 16;

    return true;
}

/**
 * 解析 PCI 设备的 MSI-X 能力，将信息缓存到 pdev 中
 *
 * @param priv PCI 总线私有数据
 * @param pdev 设备私有结构体
 */
static void pci_parse_msix_capability(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev
) {
    uint8_t cap_ptr;

    // 查找 MSI-X Capability
    cap_ptr = _pci_find_capability(priv, pdev, PCI_CAP_ID_MSIX);
    if (cap_ptr == 0)
        return;

    pdev->msix_cap_offset = cap_ptr;

    // 读取 MSI-X 详细信息
    if (!pci_read_msix_info(priv, pdev, cap_ptr))
        return;
}

/**
 * 创建 struct device 并注册到驱动框架
 *
 * @param priv PCI 总线私有数据
 * @param pdev 已填充的 PCI 私有结构体
 * @param parent 父设备
 */
static struct device *pci_create_device(
    struct pci_bus_priv *priv,
    struct pci_dev_priv *pdev,
    struct device *parent
) {
    struct device *dev;
    char name[PCI_DEVICE_NAME_LEN];

    dev = kheap_alloc(sizeof(*dev));
    if (!dev) {
        PCI_PRINT("Failed to allocate device\n");
        kheap_free(pdev);
        return NULL;
    }
    
    memset(dev, 0, sizeof(*dev));

    dev->bus = &pci_bus_type;
    dev->parent = parent;
    pci_format_device_name(
        name, sizeof(name),
        pdev->segment, pdev->bus, 
        pdev->dev, pdev->func
    );

    dev->name = strdup(name);

    if (!dev->name) {
        PCI_PRINT("Failed to strdup device name\n");
        kheap_free(dev);
        kheap_free(pdev);
        return NULL;
    }

    dev->driver_data = pdev;
    atomic_init(&dev->refcnt, 1);
    INIT_LIST_HEAD(&dev->children);
    INIT_LIST_HEAD(&dev->sibling);
    INIT_LIST_HEAD(&dev->node);
    INIT_LIST_HEAD(&dev->unmatched_node);

    pci_parse_bars(dev, priv, pdev);
    pci_parse_msix_capability(priv, pdev);

    if (!drivers_add_device(dev)) {
        PCI_PRINT("Failed to add device %s\n", name);
        kheap_free((void *)dev->name);
        kheap_free(dev);
        kheap_free(pdev);
        return NULL;
    }

    PCI_PRINT(
        "Found device %s: vendor %04x, device %04x, header %02x, bars=%d\n",
        name, pdev->vendor_id, pdev->device_id, pdev->header_type, pdev->num_bars
    );

    return dev;
}

/**
 * 处理桥设备，读取 secondary bus 并压入扫描栈
 *
 * @param priv PCI 总线私有数据
 * @param bridge_dev 桥设备对应的 struct device
 * @param pdev 桥设备的 PCI 私有数据
 * @param stack 扫描栈
 * @param top 栈顶指针（入栈时增加）
 */
static void pci_handle_bridge(
    struct pci_bus_priv *priv,
    struct device *bridge_dev,
    struct pci_dev_priv *pdev,
    struct scan_entry *stack,
    int *top
) {
    uint32_t val;
    uint8_t secondary_bus;

    // 对于 header type 0x01 或 0x02，只处理桥设备
    if (
        (pdev->header_type & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE 
        && (pdev->header_type & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_CARDBUS
    ) return;

    // 读取 secondary bus 寄存器
    if (!pci_ecam_read(
        priv, pdev->segment, pdev->bus, 
        pdev->dev, pdev->func,
        PCI_BRIDGE_SECONDARY_BUS_OFFSET, 1, &val
    )) return;

    secondary_bus = val & 0xFF;

    // 去重：避免重复扫描同一总线
    for (int i = 0; i < *top; i++) {
        if (stack[i].segment == pdev->segment && stack[i].bus == secondary_bus)
            return;
    }

    if (*top >= PCI_SCAN_STACK_DEPTH) {
        PCI_PRINT("Stack full, cannot scan secondary bus %u\n", secondary_bus);
        return;
    }

    stack[*top].segment = pdev->segment;
    stack[*top].bus = secondary_bus;
    stack[*top].parent_dev = bridge_dev;
    (*top)++;

    PCI_PRINT("Bridge: secondary bus %u\n", secondary_bus);
}

/*
 * 扫描一条 PCI 总线上的所有设备
 *
 * @param priv PCI 总线私有数据
 * @param segment 段组号
 * @param bus 总线号
 * @param parent 该总线的父设备（桥设备或 NULL）
 * @param stack 扫描栈（用于发现下游总线）
 * @param top 栈顶指针
 */
static void pci_scan_bus(
    struct pci_bus_priv *priv,
    uint16_t segment,
    uint8_t bus,
    struct device *parent,
    struct scan_entry *stack,
    int *top
) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEVICES; dev++) {
        bool multi_func = false;
        uint32_t val;

        // 尝试读取 function 0 的 Header Type，判断是否为多功能设备
        if (pci_ecam_read(
                priv,
                segment, bus,
                dev, 0,
                PCI_HEADER_TYPE_OFFSET, 1, &val
            )
        ) {
            uint8_t header_type = val & 0xFF;
            multi_func = (header_type & PCI_HEADER_TYPE_MULTIFUNC) != 0;
        } else {
            // function 0 不存在，扫描所有功能
            multi_func = true;
        }

        uint8_t max_func = multi_func ? PCI_MAX_FUNCTIONS - 1 : 0;

        for (uint8_t func = 0; func <= max_func; func++) {
            if (!pci_device_exists(priv, segment, bus, dev, func))
                continue;

            struct pci_dev_priv *pdev = pci_alloc_device_priv(priv, segment, bus, dev, func);
            if (!pdev)
                continue;

            struct device *device = pci_create_device(priv, pdev, parent);
            if (!device)
                continue;

            pci_handle_bridge(priv, device, pdev, stack, top);
        }
    }
}

// PCI 设备枚举
static void pci_enumerate_devices(void) {
    struct pci_bus_priv *priv = pci_bus_type.priv;
    struct scan_entry stack[PCI_SCAN_STACK_DEPTH];
    int top = 0;

    if (!priv)
        return;

    // 初始压栈：每个 ECAM 区域的起始总线（去重）
    for (int i = 0; i < priv->num_regions; i++) {
        struct pci_ecam_region *reg = &priv->regions[i];
        bool dup = false;
        for (int j = 0; j < top; j++) {
            if (stack[j].segment == reg->segment && stack[j].bus == reg->start_bus) {
                dup = true;
                break;
            }
        }

        if (!dup && top < PCI_SCAN_STACK_DEPTH) {
            stack[top].segment = reg->segment;
            stack[top].bus = reg->start_bus;
            stack[top].parent_dev = NULL;
            top++;
        }
    }

    // 循环处理栈中的总线
    while (top > 0) {
        top--;
        struct scan_entry entry = stack[top];
        pci_scan_bus(priv, entry.segment, entry.bus, entry.parent_dev, stack, &top);
    }
}

/*
 * PCI 总线 match 函数，用于匹配设备与驱动
 *
 * @param dev 设备结构体
 * @param drv 驱动结构体
 */
static bool pci_match(struct device *dev, struct driver *drv) {
    struct pci_dev_priv *pdev = dev->driver_data;
    const struct pci_device_id *id_table = drv->id_table;
    if (!pdev || !id_table)
        return false;

    for (const struct pci_device_id *id = id_table; id->vendor != 0 || id->device != 0; id++) {
        if ((id->vendor == PCI_ANY_ID || id->vendor == pdev->vendor_id) &&
            (id->device == PCI_ANY_ID || id->device == pdev->device_id)) {
            return true;
        }
    }

    return false;
}

/*
 * PCI 总线 free_device 回调，释放设备私有数据
 *
 * @param dev 设备结构体
 */
static void pci_free_device(struct device *dev) {
    struct pci_dev_priv *pdev = dev->driver_data;
    if (!pdev)
        return;

    vheap_unmap_mmio(dev->driver_data);
    kheap_free(pdev);
    dev->driver_data = NULL;
}

// PCI 总线初始化
static void pci_init(void) {
    int num_regions;
    struct pci_ecam_region *regions = NULL;
    struct pci_bus_priv *priv = NULL;
    
    INIT_LIST_HEAD(&pci_bus_type.devices);
    INIT_LIST_HEAD(&pci_bus_type.drivers);
    INIT_LIST_HEAD(&pci_bus_type.node);
    
    regions = pci_get_ecam_info(&num_regions);
    if (!regions) {
        PCI_PRINT("No ECAM regions, PCI disabled\n");
        return;
    }

    // 映射每个 ECAM 区域
    for (int i = 0; i < num_regions; i++) {
        struct pci_ecam_region *reg = &regions[i];
        uint64_t size = (reg->end_bus - reg->start_bus + 1) * PCI_ECAM_BUS_SIZE;
        void *virt = vheap_map_mmio(reg->base_addr, size);
        if (!virt) {
            PCI_PRINT("Failed to map ECAM region [%d], PCI disabled\n", i);
            goto out_free_regions;
        }
        
        reg->base_virt = virt;
    }

    // 缓存到总线私有数据
    priv = kheap_alloc(sizeof(*priv));
    if (!priv) {
        PCI_PRINT("Failed to allocate PCI private data\n");
        goto out_free_regions;
    }

    priv->regions = regions;
    priv->num_regions = num_regions;
    pci_bus_type.priv = priv;

    // 注册 PCI 总线
    if (!drivers_add_bus(&pci_bus_type)) {
        PCI_PRINT("Failed to register PCI bus\n");
        goto out_free_priv;
    }

    // 枚举所有 PCI 设备
    pci_enumerate_devices();
    return;

out_free_priv:
    kheap_free(priv);
out_free_regions:
    kheap_free(regions);
}

static struct bus pci_bus_type = {
    .name = "pci",
    .match = pci_match,
    .devices = {0},
    .drivers = {0},
    .lock = SPIN_LOCK_INIT,
    .node = {0},
    .priv = NULL,
    .free_device = pci_free_device,
};

INITCALL(drivers, 0, pci_init);

/**
 * 获取 PCI 设备的厂商 ID
 * 
 * @param dev pci设备
 */
uint16_t pci_get_vendor_id(struct device *dev) {
    struct pci_dev_priv *pdev;

    if (!dev || !dev->driver_data)
        return 0xFFFF;

    pdev = dev->driver_data;
    return pdev->vendor_id;
}

/**
 * 获取 PCI 设备的设备 ID
 * 
 * @param dev pci设备
 */
uint16_t pci_get_device_id(struct device *dev) {
    struct pci_dev_priv *pdev;

    if (!dev || !dev->driver_data)
        return 0xFFFF;

    pdev = dev->driver_data;
    return pdev->device_id;
}

/*
 * 查找指定 Capability ID 在 PCI 配置空间中的偏移
 *
 * @param dev PCI 设备结构体
 * @param cap_id 要查找的 Capability ID
 *
 * @return 找到时返回 Capability 在配置空间中的偏移（字节），未找到返回 0
 */
uint8_t pci_find_capability(struct device *dev, uint8_t cap_id) {
    struct pci_dev_priv *pdev;
    struct pci_bus_priv *priv;

    if (!dev || !dev->driver_data)
        return 0;

    pdev = dev->driver_data;
    if (!dev->bus || !dev->bus->priv)
        return 0;

    if (cap_id == PCI_CAP_ID_MSIX) {
        return pdev->msix_cap_offset;
    }

    priv = dev->bus->priv;
    return _pci_find_capability(priv, pdev, cap_id);
}

/*
 * 启用 MSI‑X 中断
 *
 * @param dev PCI 设备
 * @param vectors 要启用的中断向量数
 * @param logical_id 目标 CPU 的逻辑 ID
 * @param vector_start 起始中断向量号
 */
int pci_msix_enable(
    struct device *dev, 
    int vectors, 
    uint32_t logical_id, 
    uint8_t vector_start
) {
    struct pci_dev_priv *pdev;
    struct pci_bus_priv *priv;
    struct resource *bar;
    uint64_t table_phys;
    void *table_virt;
    int i, max_vectors;
    uint32_t ctrl;
    struct _msi_msg msg;
    uintptr_t entry_addr;

    if (!dev || !dev->driver_data)
        return -ENODEV;

    pdev = dev->driver_data;
    priv = dev->bus->priv;

    if (!pdev->msix_cap_offset)
        return -ENODEV;

    // 检查请求的向量数是否超过表容量
    max_vectors = (pdev->msix_control & PCI_MSIX_CTRL_TABLE_SIZE_MASK) + 1;
    if (vectors <= 0 || vectors > max_vectors)
        return -EINVAL;

    // 获取 MSI‑X 表所在的 BAR
    if (pdev->msix_table_bir >= PCI_MAX_BARS)
        return -EINVAL;

    bar = &dev->res[pdev->msix_table_bir];
    if (bar->start == 0 || bar->end < bar->start)
        return -ENXIO;

    // 计算 MSI‑X 表物理地址
    table_phys = bar->start + pdev->msix_table_offset;
    if (pdev->msix_table_size == 0)
        return -ENXIO;

    // 映射 MSI‑X 表到内核虚拟地址
    table_virt = vheap_map_mmio(table_phys, pdev->msix_table_size);
    if (!table_virt)
        return -ENOMEM;

    pdev->msix_table_virt = table_virt;

    // 对每个向量配置 MSI‑X 表项
    for (i = 0; i < vectors; i++) {
        if (!msi_create_msg(logical_id, vector_start + i, &msg)) {
            pci_msix_disable(dev);
            return -EIO;
        }

        entry_addr = (uintptr_t)table_virt + i * 16;
        *(volatile uint64_t *)entry_addr = msg.addr;
        *(volatile uint32_t *)(entry_addr + 8) = msg.data;
    }

    // 读取 Message Control 寄存器，设置 Enable 位
    if (!pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            pdev->msix_cap_offset + PCI_MSIX_CTRL_OFFSET, 2, &ctrl
        )
    ) {
        pci_msix_disable(dev);
        return -EIO;
    }

    ctrl |= PCI_MSIX_CTRL_ENABLE_BIT;
    pci_ecam_write(
        priv,
        pdev->segment, pdev->bus,
        pdev->dev, pdev->func,
        pdev->msix_cap_offset + PCI_MSIX_CTRL_OFFSET, 2, ctrl
    );

    return 0;
}

/*
 * 禁用 MSI‑X 中断
 *
 * @param dev PCI 设备
 */
void pci_msix_disable(struct device *dev) {
    struct pci_dev_priv *pdev;
    struct pci_bus_priv *priv;
    uint32_t ctrl;

    if (!dev || !dev->driver_data)
        return;

    pdev = dev->driver_data;
    priv = dev->bus->priv;

    if (!pdev->msix_cap_offset)
        return;

    // 清除 Message Control 的 Enable 位
    if (pci_ecam_read(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            pdev->msix_cap_offset + PCI_MSIX_CTRL_OFFSET, 2, &ctrl
        )
    ) {
        ctrl &= ~PCI_MSIX_CTRL_ENABLE_BIT;
        pci_ecam_write(
            priv,
            pdev->segment, pdev->bus,
            pdev->dev, pdev->func,
            pdev->msix_cap_offset + PCI_MSIX_CTRL_OFFSET, 2, ctrl
        );
    }

    // 释放 MSI‑X 表映射
    if (pdev->msix_table_virt) {
        vheap_unmap_mmio(pdev->msix_table_virt);
        pdev->msix_table_virt = NULL;
    }
}