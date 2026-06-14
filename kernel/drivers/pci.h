/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// 表示该驱动可以匹配任意厂商或任意设备 ID
#define PCI_ANY_ID 0xFFFF

// 用于 PCI 设备的 ID 描述
struct pci_device_id {
    uint16_t vendor;      // 厂商 ID
    uint16_t device;      // 设备 ID
    uint16_t subvendor;   // 子系统厂商 ID
    uint16_t subdevice;   // 子系统设备 ID
    uint32_t class;       // 设备类别
    uint32_t class_mask;  // 类别掩码
};

extern struct bus pci_bus_type;

/**
 * 获取 PCI 设备的厂商 ID
 * 
 * @param dev pci设备
 */
uint16_t pci_get_vendor_id(struct device *dev);

/**
 * 获取 PCI 设备的设备 ID
 * 
 * @param dev pci设备
 */
uint16_t pci_get_device_id(struct device *dev);

/*
 * 查找指定 Capability ID 在 PCI 配置空间中的偏移
 *
 * @param dev PCI 设备结构体
 * @param cap_id 要查找的 Capability ID
 * @param start 起始查找偏移（0 表示从链表头开始）
 *
 * @return 找到时返回 Capability 在配置空间中的偏移（字节），未找到返回 0
 */
uint8_t pci_find_capability(struct device *dev, uint8_t cap_id, uint8_t start);

/**
 * 从 MSI‑X 向量池中分配一个空闲向量
 *
 * @param dev PCI 设备
 * @param logical_id 目标 CPU 的逻辑 ID
 * @param vector 中断向量号
 *
 * @return 向量索引，失败返回 -1
 */
int pci_msix_alloc_vector(
    struct device *dev,
    uint32_t logical_id,
    uint32_t vector
);

/**
 * 释放一个已分配的 MSI‑X 向量
 *
 * @param dev PCI 设备
 * @param index 向量索引
 */
void pci_msix_free_vector(struct device *dev, int index);

/*
 * 从 PCI 配置空间读取 8 位字节
 *
 * @param dev PCI 设备结构体
 * @param offset 配置空间偏移
 *
 * @return 读取到的 8 位值，失败返回 0xFF
 */
uint8_t pci_config_read_byte(struct device *dev, uint16_t offset);

/*
 * 从 PCI 配置空间读取 16 位字
 *
 * @param dev PCI 设备结构体
 * @param offset 配置空间偏移
 *
 * @return 读取到的 16 位值，失败返回 0xFFFF
 */
uint16_t pci_config_read_word(struct device *dev, uint16_t offset);

/*
 * 从 PCI 配置空间读取 32 位双字
 *
 * @param dev PCI 设备结构体
 * @param offset 配置空间偏移
 *
 * @return 读取到的 32 位值，失败返回 0xFFFFFFFF
 */
uint32_t pci_config_read_dword(struct device *dev, uint16_t offset);