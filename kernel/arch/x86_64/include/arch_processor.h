/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

struct pt_regs;
struct thread_struct;
typedef uint64_t gdte;
struct tss;
struct idt_gate;

/**
 * 设置当前CPU的栈指针
 * 
 * @param new_stack_top  新栈顶地址
 */
#define processor_set_stack(new_stack_top) \
    do { \
        __asm__ volatile( \
            "movq %0, %%rsp\n" \
            "xorq %%rbp, %%rbp\n" \
            : \
            : "r"((uint64_t)(new_stack_top)) \
            : "memory" \
        ); \
    } while (0)

// CPU暂停,用于优化等待循环，防止过度占用执行资源
static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

// 禁止中断
static inline void irq_off(void) {
    __asm__ volatile("cli" ::: "memory");
}

// 开启中断
static inline void irq_on(void) {
    __asm__ volatile("sti" ::: "memory");
}

// 获取gdt模版的虚拟地址
uint64_t *get_gdt_temp(void);

// 获取idt模版的虚拟地址
struct idt_gate* get_idt_temp(void);

// 获取tss模版的虚拟地址
struct tss* get_tss_temp(void);

/**
 * 任务切换
 * 
 * @param prev 当前任务的 thread_struct 指针
 * @param next 下一个任务的 thread_struct 指针
 * 
 * 此函数不会保存/恢复fpu状态
 * fpu由上层调用者负责
 */
void switch_to(struct thread_struct *prev, struct thread_struct *next);

/**
 * 保存fpu信息
 * 
 * @param state fpu信息结构体
 */
void fpu_save(struct thread_struct *thread);

/**
 * 恢复fpu状态
 * 
 * @param state fpu信息结构体
 */
void fpu_restore(struct thread_struct *thread);

// 为任务分配thread_struct结构体
struct thread_struct *thread_struct_init(void);

// 初始化所有模版
void processor_init(void);