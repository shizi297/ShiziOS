/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// 模型特定寄存器 (MSR) 地址
#define MSR_EFER 0xC0000080            // 扩展功能使能寄存器
#define MSR_STAR 0xC0000081            // 系统调用配置：内核/用户 CS
#define MSR_LSTAR 0xC0000082           // 64 位系统调用入口点
#define MSR_CSTAR 0xC0000083           // 兼容模式系统调用入口点
#define MSR_SFMASK 0xC0000084          // 系统调用标志屏蔽
#define MSR_FS_BASE 0xC0000100         // FS 段基址
#define MSR_GS_BASE 0xC0000101         // GS 段基址
#define MSR_KERNEL_GS_BASE 0xC0000102  // 内核 GS 基址 (swapgs 使用)
#define MSR_IA32_APIC_BASE         0x1B
#define MSR_IA32_TSC_DEADLINE      0x6E0  
#define APIC_BASE_MSR_ENABLE       (1ULL << 11)
#define APIC_BASE_MSR_X2APIC       (1ULL << 10)
#define APIC_TSC_DEADLINE   2

// x2APIC MSR 地址 
#define X2APIC_MSR_BASE           0x800
#define X2APIC_MSR_APIC_ID        (X2APIC_MSR_BASE + 0x02)
#define X2APIC_MSR_VERSION        (X2APIC_MSR_BASE + 0x03)
#define X2APIC_MSR_TPR            (X2APIC_MSR_BASE + 0x08)
#define X2APIC_MSR_EOI            (X2APIC_MSR_BASE + 0x0B)
#define X2APIC_MSR_LDR            (X2APIC_MSR_BASE + 0x0D)
#define X2APIC_MSR_SVR            (X2APIC_MSR_BASE + 0x0F)
#define X2APIC_MSR_ISR_BASE       (X2APIC_MSR_BASE + 0x10)
#define X2APIC_MSR_TMR_BASE       (X2APIC_MSR_BASE + 0x18)
#define X2APIC_MSR_IRR_BASE       (X2APIC_MSR_BASE + 0x20)
#define X2APIC_MSR_ESR            (X2APIC_MSR_BASE + 0x28)
#define X2APIC_MSR_LVT_CMCI       (X2APIC_MSR_BASE + 0x2F)
#define X2APIC_MSR_ICR            (X2APIC_MSR_BASE + 0x30)
#define X2APIC_MSR_LVT_TIMER      (X2APIC_MSR_BASE + 0x32)
#define X2APIC_MSR_LVT_THERMAL    (X2APIC_MSR_BASE + 0x33)
#define X2APIC_MSR_LVT_PMI        (X2APIC_MSR_BASE + 0x34)
#define X2APIC_MSR_LVT_LINT0      (X2APIC_MSR_BASE + 0x35)
#define X2APIC_MSR_LVT_LINT1      (X2APIC_MSR_BASE + 0x36)
#define X2APIC_MSR_LVT_ERROR      (X2APIC_MSR_BASE + 0x37)
#define X2APIC_MSR_TIMER_INITCNT  (X2APIC_MSR_BASE + 0x38)
#define X2APIC_MSR_TIMER_CURRCNT  (X2APIC_MSR_BASE + 0x39)
#define X2APIC_MSR_TIMER_DIV      (X2APIC_MSR_BASE + 0x3E)
#define X2APIC_MSR_SELF_IPI       (X2APIC_MSR_BASE + 0x3F)

// EFER 寄存器位定义
#define EFER_SCE (1 << 0)   // 系统调用扩展使能
#define EFER_LME (1 << 8)   // 长模式使能
#define EFER_LMA (1 << 10)  // 长模式激活 (只读)
#define EFER_NXE (1 << 11)  // 禁止执行位使能

// 读取指定的 MSR 寄存器
static inline uint64_t msr_read(uint32_t reg) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(reg));
    return ((uint64_t)high << 32) | low;
}

// 写入指定的 MSR 寄存器
static inline void msr_write(uint32_t reg, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    asm volatile ("wrmsr" :: "a"(low), "d"(high), "c"(reg));
}