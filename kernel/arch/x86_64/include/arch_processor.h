/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

struct pt_regs;
struct thread_struct;
typedef uint64_t gdte;
struct tss;
struct idt_gate;

/**
 * 设置当前CPU的栈指针
 * 
 * @param new_stack_top  新栈顶地址
 *
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

// 获取gdt模版的虚拟地址
uint64_t *get_gdt_temp(void);

// 获取idt模版的虚拟地址
struct idt_gate* get_idt_temp(void);

// 获取tss模版的虚拟地址
struct tss* get_tss_temp(void);

// 初始化所有模版
void processor_init(void);