/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "fault.h"
#include <serial.h>
#include <processor.h>

void exc_de(struct pt_regs *regs) {
    panic("Divide Error");
}

void exc_db(struct pt_regs *regs) {
    panic("Debug");
}

void exc_nmi(struct pt_regs *regs) {
    panic("Non-Maskable Interrupt");
}

void exc_bp(struct pt_regs *regs) {
    panic("Breakpoint");
}

void exc_of(struct pt_regs *regs) {
    panic("Overflow");
}

void exc_br(struct pt_regs *regs) {
    panic("Bound Range Exceeded");
}

void exc_ud(struct pt_regs *regs) {
    panic("Invalid Opcode");
}

void exc_nm(struct pt_regs *regs) {
    panic("Device Not Available");
}

void exc_df(struct pt_regs *regs) {
    panic("Double Fault");
}

void exc_cso(struct pt_regs *regs) {
    panic("Coprocessor Segment Overrun");
}

void exc_ts(struct pt_regs *regs) {
    panic("Invalid TSS");
}

void exc_np(struct pt_regs *regs) {
    panic("Segment Not Present");
}

void exc_ss(struct pt_regs *regs) {
    panic("Stack-Segment Fault");
}

void exc_gp(struct pt_regs *regs) {
    panic("General Protection Fault");
}

void exc_pf(struct pt_regs *regs) {
    panic("Page Fault");
}

void exc_mf(struct pt_regs *regs) {
    panic("x87 Floating-Point Exception");
}

void exc_ac(struct pt_regs *regs) {
    panic("Alignment Check");
}

void exc_mc(struct pt_regs *regs) {
    panic("Machine Check");
}

void exc_xm(struct pt_regs *regs) {
    panic("SIMD Floating-Point Exception");
}

void exc_ve(struct pt_regs *regs) {
    panic("Virtualization Exception");
}

