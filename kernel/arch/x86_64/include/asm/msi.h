/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <asm/smp.h>
#include <minmax.h>
#include <bootboot.h>
#include <apic.h>

// 用于构建 msi 消息
struct _msi_msg {
    uint64_t addr;
    uint32_t data;
} __attribute__((packed));

// 构建 msi 的 addr 字段
static inline uint64_t _msi_create_addr(uint32_t apic_id) {
    return apic_get_base() | ((uint64_t)apic_id << 12); 
}

// 构建 msi 的 data 字段
static inline uint32_t _msi_create_data(uint32_t vector) {
    return 
        (vector & 0xFF) |   // 向量号
        (0 << 8) |  // 传送模式：固定
        (0 << 11) | // 目标模式：物理
        (1 << 15);  // 触发模式：边沿
}

// 获取 msi 支持的最大cpu数量
static inline uint32_t msi_max_cpu(void) {
    static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
    return min((uint32_t)bootboot->numcores, (uint32_t)UINT8_MAX);
}

/**
 * 构建 msi 消息
 * 
 * @param cpuid 目标 CPU 的逻辑 ID
 * @param vector 向量号
 * @param msg 指向要构建的 MSI 消息的指针
 */
static inline bool msi_create_msg(uint32_t cpuid, uint32_t vector, struct _msi_msg *msg) {
    if (!msg) 
        return false; // 消息指针为空

    if (vector > 255) 
        return false; // 向量号必须在0-255范围内
    
    // 获取目标 CPU 的 APIC ID
    uint32_t apicid = smp_get_apic_id(cpuid);

    if (apicid >= msi_max_cpu()) 
        return false; // 超出支持的CPU数量

    // 构建MSI消息地址和数据
    msg->addr = _msi_create_addr(apicid);
    msg->data = _msi_create_data(vector);

    return true;
}