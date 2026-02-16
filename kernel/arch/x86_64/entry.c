/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <fault.h>
#include <processor.h>

// 系统调用入口
void syscall_entry(void) {
    // TODO
}

/**
 * 中断/异常/信号处理入口
 *
 * @param regs 发生时的寄存器状态
 */
void irq_entry(struct pt_regs *regs) {


    // 告诉处理器我们已经处理完了
    processor_eoi();
}