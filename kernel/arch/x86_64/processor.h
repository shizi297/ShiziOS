/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include "desc.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <fault.h>
#include <arch_processor.h>
#include <heap.h>
#include <msr.h>

extern uint32_t xsaves_size;

// CR4寄存器位掩码定义
#define CR4_VME          (1ULL <<  0)  // 虚拟8086模式扩展
#define CR4_PVI          (1ULL <<  1)  // 保护模式虚拟中断
#define CR4_TSD          (1ULL <<  2)  // 时间戳禁用
#define CR4_DE           (1ULL <<  3)  // 调试扩展
#define CR4_PSE          (1ULL <<  4)  // 页大小扩展（4MB页）
#define CR4_PAE          (1ULL <<  5)  // 物理地址扩展
#define CR4_MCE          (1ULL <<  6)  // 机器检查异常使能
#define CR4_PGE          (1ULL <<  7)  // 页全局使能
#define CR4_PCE          (1ULL <<  8)  // 性能监控计数器使能
#define CR4_OSFXSR       (1ULL <<  9)  // 支持FXSAVE/FXRSTOR
#define CR4_OSXMMEXCPT   (1ULL << 10)  // 支持SIMD浮点异常
#define CR4_UMIP         (1ULL << 11)  // 用户模式指令阻止
#define CR4_LA57         (1ULL << 12)  // 5级分页使能
#define CR4_VMXE         (1ULL << 13)  // VMX使能
#define CR4_SMXE         (1ULL << 14)  // SMX使能
#define CR4_FSGSBASE     (1ULL << 16)  // FS/GS基址快速访问指令使能
#define CR4_PCIDE        (1ULL << 17)  // 进程上下文标识符使能
#define CR4_OSXSAVE      (1ULL << 18)  // 操作系统支持XSAVE/XRSTOR
#define CR4_SMEP         (1ULL << 20)  // 内核模式执行保护
#define CR4_SMAP         (1ULL << 21)  // 内核模式访问保护
#define CR4_PKE          (1ULL << 22)  // 页密钥使能
#define CR4_CET          (1ULL << 23)  // 控制流强制技术使能
#define CR4_PKS          (1ULL << 24)  // 页密钥存储使能
#define CR4_UINTR        (1ULL << 25)  // 用户中断使能

// CR4配置
#define CR4_CONFIG (CR4_MCE | CR4_PAE | CR4_PSE | CR4_PGE | CR4_OSFXSR | \
                   CR4_OSXMMEXCPT | CR4_OSXSAVE | CR4_FSGSBASE | \
                   CR4_SMEP | CR4_SMAP)

// 存储中断/异常/系统调用/信号处理时的信息
struct pt_regs {
    /*
     * 调用者保存寄存器
     * 包括系统调用参数寄存器
     * 
     * rcx和r11在系统调用时有特殊用途
     * syscall指令将rip保存到rcx
     * syscall指令将rflags保存到r11
     * 因此它们在系统调用时同时作为：
     * 通用寄存器和保存关键控制寄存器值
     */
    uint64_t rdi;   // 系统调用第1个参数 
    uint64_t rsi;   // 系统调用第2个参数 
    uint64_t rdx;   // 系统调用第3个参数 
    uint64_t rcx;   // 系统调用时保存用户rip，也作为通用寄存器rcx 
    uint64_t rax;   // 系统调用号/返回值 
    uint64_t r8;    // 系统调用第5个参数 
    uint64_t r9;    // 系统调用第6个参数 
    uint64_t r10;   // 系统调用第4个参数 
    uint64_t r11;   // 系统调用时保存用户rflags，也作为通用寄存器r11

    /*
     * 保存这些通用寄存器
     * 因为有的用户程序会使用他们
     */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t vector;     // 中断/异常向量号
    uint64_t error_code; // 错误码（如果有），否则为0

    uint64_t rip;   // 异常发生时的指令地址 
    uint64_t cs;    // 代码段选择子，区分用户态和内核态 
    uint64_t rflags;    // 处理器状态标志 
    uint64_t rsp;   // 异常发生时的栈指针（仅来自用户态时有效）
    uint64_t ss;    // 栈段选择子（仅来自用户态时有效）
} __attribute__((packed, aligned(8)));

// fpu信息
struct fpu_state {
    void *xsaves;
    size_t size;
};

// 任务切换时保存的信息
struct thread_struct {
    uint64_t cr3;           // 页表基址
    uint64_t rsp;           // 内核栈指针
    uint64_t fs_base;       // 用户态 TLS

    // 被调用者保存的寄存器
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    struct fpu_state fpu_state; 
};

#define THR_CR3   offsetof(struct thread_struct, cr3)
#define THR_RSP   offsetof(struct thread_struct, rsp)
#define THR_FS    offsetof(struct thread_struct, fs_base)
#define THR_RBX   offsetof(struct thread_struct, rbx)
#define THR_RBP   offsetof(struct thread_struct, rbp)
#define THR_R12   offsetof(struct thread_struct, r12)
#define THR_R13   offsetof(struct thread_struct, r13)
#define THR_R14   offsetof(struct thread_struct, r14)
#define THR_R15   offsetof(struct thread_struct, r15)

// 设置gs寄存器
static inline void set_gs_base(uint64_t base) {
    asm volatile("wrgsbase %0" : : "r"(base) : "memory");
}

/**
 * 读取gs寄存器
 *
 * @return gs寄存器的值
 */
static inline void *get_gs_base(void) {
    uint64_t base;
    asm volatile("rdgsbase %0" : "=r"(base) : : "memory");
    return (void *)base;
}

/**
 * 检测cpu是否支持fsgsbase
 * 
 * @return 支持：true
 * @return 不支持：false 
 */
static inline bool cpuid_fsgsbase(void) {
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));
    
    return (ebx & (1 << 0)) != 0;
}

// 获取xsaves指令需要的最大内存大小
static inline uint32_t cpuid_xsaves_size(void) {
    uint32_t ebx;
    asm volatile("cpuid" : "=b"(ebx) : "a"(0x0D), "c"(0) : "edx", "memory");
    return ebx;
}

/**
 * 初始化xsave
 *
 * @return 成功: true
 * @return 不支持 : false
 */
static inline bool processor_xsave_init(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t dummy;

    // 检测是否支持 XSAVE
    asm volatile("cpuid" : "=a"(eax), "=b"(dummy), "=c"(ecx), "=d"(dummy) : "a"(1), "c"(0) : "memory");
    if (!(ecx & (1 << 26))) {
        xsaves_size = 0;
        return false;
    }

    // 获取XCR0支持的掩码
    asm volatile("cpuid" : "=a"(eax), "=b"(dummy), "=c"(dummy), "=d"(edx) : "a"(0xD), "c"(0) : "memory");
    uint64_t xcr0_mask = ((uint64_t)edx << 32) | eax;

    if (xcr0_mask) {
        asm volatile("xsetbv" : : "a"(xcr0_mask), "d"(xcr0_mask >> 32), "c"(0) : "memory");
    }

    // 获取保存占用大小
    asm volatile("cpuid" : "=b"(xsaves_size) : "a"(0xD), "c"(0) : "edx", "memory");

    return true;
}

// 写入CR4(使用CR4_CONFIG)
static inline void set_cr4(void) {
    uint64_t current_cr4;
    uint64_t new_cr4;
    uint64_t config;
    
    // 读取当前CR4值
    __asm__ volatile("mov %%cr4, %0" : "=r"(current_cr4));
    
    config = (uint64_t)CR4_CONFIG;
    
    // 只设置CR4_CONFIG中定义的位，其他位保持不变
    new_cr4 = current_cr4 | config;
    
    // 写回CR4
    __asm__ volatile("mov %0, %%cr4" : : "r"(new_cr4));
}

// 读取tsc设备值
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile ("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

// 内存屏障
static inline void barrier(void) {
    __asm__ volatile ("" ::: "memory");
}