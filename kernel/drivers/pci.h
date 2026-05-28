/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

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
 *
 * @return 找到时返回 Capability 在配置空间中的偏移（字节），未找到返回 0
 */
uint8_t pci_find_capability(struct device *dev, uint8_t cap_id);

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
);

/*
 * 禁用 MSI‑X 中断
 *
 * @param dev PCI 设备
 */
void pci_msix_disable(struct device *dev);