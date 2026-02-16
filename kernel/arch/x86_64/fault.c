/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "fault.h"
#include <serial.h>

void exc_de(void) {
    panic("Divide Error");
}

void exc_db(void) {
    panic("Debug");
}

void exc_nmi(void) {
    panic("Non-Maskable Interrupt");
}

void exc_bp(void) {
    panic("Breakpoint");
}

void exc_of(void) {
    panic("Overflow");
}

void exc_br(void) {
    panic("Bound Range Exceeded");
}

void exc_ud(void) {
    panic("Invalid Opcode");
}

void exc_nm(void) {
    panic("Device Not Available");
}

void exc_df(void) {
    panic("Double Fault");
}

void exc_cso(void) {
    panic("Coprocessor Segment Overrun");
}

void exc_ts(void) {
    panic("Invalid TSS");
}

void exc_np(void) {
    panic("Segment Not Present");
}

void exc_ss(void) {
    panic("Stack-Segment Fault");
}

void exc_gp(void) {
    panic("General Protection Fault");
}

void exc_pf(void) {
    panic("Page Fault");
}

void exc_mf(void) {
    panic("x87 Floating-Point Exception");
}

void exc_ac(void) {
    panic("Alignment Check");
}

void exc_mc(void) {
    panic("Machine Check");
}

void exc_xm(void) {
    panic("SIMD Floating-Point Exception");
}

void exc_ve(void) {
    panic("Virtualization Exception");
}

