/* SPDX-License-Identifier: Apache-2.0 */

#include "fault.h"

.altmacro

.extern irq_entry

/*
 * 为每个中断向量生成独立入口点
 *
 * 所有入口点确保统一栈布局：
 *   无错误码异常：压入0占位，再压入向量号
 *   有错误码异常：只压入向量号（错误码已由硬件压入）
 *   外部中断：统一按无错误码处理
 *
 * 进入irq_entry时栈布局（从低地址向高地址）：
 *   [rsp]     错误码（或0占位）
 *   [rsp+8]   向量号
 *   [rsp+16]  硬件压入的RIP
 *   [rsp+24]  硬件压入的CS
 *   [rsp+32]  硬件压入的RFLAGS
 *   [rsp+40]  硬件压入的RSP（若来自用户态）
 *   [rsp+48]  硬件压入的SS（若来自用户态）
 *
 * 这样irq_entry可以通过固定偏移访问向量号和错误码。
 */

.macro IRQ_ENTRY vector, has_error
    .globl irq_entry_\vector
    .type irq_entry_\vector, @function
    .align 8
irq_entry_\vector:
    .if \has_error == 0
        pushq $0
    .endif
    pushq $\vector
    jmp irq_entry
.endm

/* 地址表条目 */
.macro TABLE_ENTRY vector
    .quad irq_entry_\vector
.endm

.section .text.irq_entries, "ax"
.align 8

/* 异常向量 0-31 */
.set vector, 0
.rept 32
    .if (EXC_HAS_ERRCODE >> vector) & 1
        IRQ_ENTRY %vector, 1
    .else
        IRQ_ENTRY %vector, 0
    .endif
    .set vector, vector + 1
.endr

/* 外部中断向量 32-255 */
.set vector, 32
.rept 224
    IRQ_ENTRY %vector, 0
    .set vector, vector + 1
.endr

/*
 * 中断处理函数地址表
 * 用于C代码初始化IDT时获取每个中断向量的处理函数地址
 *
 * 此表包含256个条目，每个条目是8字节的函数指针
 * 对应irq_entry_0到irq_entry_255
 *
 * 其他文件可以通过extern uint64_t irq_entry_table[256]访问此表
 */
.section .rodata
.align 8
.globl irq_entry_table
.type irq_entry_table, @object
irq_entry_table:
.set vector, 0
.rept 256
    TABLE_ENTRY %vector
    .set vector, vector + 1
.endr
.size irq_entry_table, . - irq_entry_table