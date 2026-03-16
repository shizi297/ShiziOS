/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "fault.h"
#include <serial.h>
#include <processor.h>

static void print_regs(const struct pt_regs *regs) {
    serial_puts("rdi=");   serial_put_hex(regs->rdi);
    serial_puts(" rsi=");  serial_put_hex(regs->rsi);
    serial_puts(" rdx=");  serial_put_hex(regs->rdx);
    serial_puts(" rcx=");  serial_put_hex(regs->rcx);
    serial_puts(" rax=");  serial_put_hex(regs->rax);
    serial_puts(" r8=");   serial_put_hex(regs->r8);
    serial_puts(" r9=");   serial_put_hex(regs->r9);
    serial_puts(" r10=");  serial_put_hex(regs->r10);
    serial_puts(" r11=");  serial_put_hex(regs->r11);
    serial_puts("\n");

    serial_puts("rbx=");   serial_put_hex(regs->rbx);
    serial_puts(" rbp=");  serial_put_hex(regs->rbp);
    serial_puts(" r12=");  serial_put_hex(regs->r12);
    serial_puts(" r13=");  serial_put_hex(regs->r13);
    serial_puts(" r14=");  serial_put_hex(regs->r14);
    serial_puts(" r15=");  serial_put_hex(regs->r15);
    serial_puts("\n");

    serial_puts("vector=");     serial_put_dec(regs->vector);
    serial_puts(" error_code="); serial_put_hex(regs->error_code);
    serial_puts("\n");

    serial_puts("rip=");    serial_put_hex(regs->rip);
    serial_puts(" cs=");    serial_put_hex(regs->cs);
    serial_puts(" rflags="); serial_put_hex(regs->rflags);
    serial_puts(" rsp=");   serial_put_hex(regs->rsp);
    serial_puts(" ss=");    serial_put_hex(regs->ss);
    serial_puts("\n");
}

void exc_de(struct pt_regs *regs) {
    serial_puts("Divide Error\n");
    print_regs(regs);
    panic("Divide Error");
}

void exc_db(struct pt_regs *regs) {
    serial_puts("Debug\n");
    print_regs(regs);
    panic("Debug");
}

void exc_nmi(struct pt_regs *regs) {
    serial_puts("Non-Maskable Interrupt\n");
    print_regs(regs);
    panic("Non-Maskable Interrupt");
}

void exc_bp(struct pt_regs *regs) {
    serial_puts("Breakpoint\n");
    print_regs(regs);
    panic("Breakpoint");
}

void exc_of(struct pt_regs *regs) {
    serial_puts("Overflow\n");
    print_regs(regs);
    panic("Overflow");
}

void exc_br(struct pt_regs *regs) {
    serial_puts("Bound Range Exceeded\n");
    print_regs(regs);
    panic("Bound Range Exceeded");
}

void exc_ud(struct pt_regs *regs) {
    serial_puts("Invalid Opcode\n");
    print_regs(regs);
    panic("Invalid Opcode");
}

void exc_nm(struct pt_regs *regs) {
    serial_puts("Device Not Available\n");
    print_regs(regs);
    panic("Device Not Available");
}

void exc_df(struct pt_regs *regs) {
    serial_puts("Double Fault\n");
    print_regs(regs);
    panic("Double Fault");
}

void exc_cso(struct pt_regs *regs) {
    serial_puts("Coprocessor Segment Overrun\n");
    print_regs(regs);
    panic("Coprocessor Segment Overrun");
}

void exc_ts(struct pt_regs *regs) {
    serial_puts("Invalid TSS\n");
    print_regs(regs);
    panic("Invalid TSS");
}

void exc_np(struct pt_regs *regs) {
    serial_puts("Segment Not Present\n");
    print_regs(regs);
    panic("Segment Not Present");
}

void exc_ss(struct pt_regs *regs) {
    serial_puts("Stack-Segment Fault\n");
    print_regs(regs);
    panic("Stack-Segment Fault");
}

void exc_gp(struct pt_regs *regs) {
    serial_puts("General Protection Fault\n");
    print_regs(regs);
    panic("General Protection Fault");
}

void exc_pf(struct pt_regs *regs) {
    serial_puts("Page Fault\n");
    print_regs(regs);
    panic("Page Fault");
}

void exc_mf(struct pt_regs *regs) {
    serial_puts("x87 Floating-Point Exception\n");
    print_regs(regs);
    panic("x87 Floating-Point Exception");
}

void exc_ac(struct pt_regs *regs) {
    serial_puts("Alignment Check\n");
    print_regs(regs);
    panic("Alignment Check");
}

void exc_mc(struct pt_regs *regs) {
    serial_puts("Machine Check\n");
    print_regs(regs);
    panic("Machine Check");
}

void exc_xm(struct pt_regs *regs) {
    serial_puts("SIMD Floating-Point Exception\n");
    print_regs(regs);
    panic("SIMD Floating-Point Exception");
}

void exc_ve(struct pt_regs *regs) {
    serial_puts("Virtualization Exception\n");
    print_regs(regs);
    panic("Virtualization Exception");
}

void __stack_chk_fail(void) {
    serial_puts("\n[PANIC] Stack smashing detected\n");

    uint64_t *rbp;
    asm volatile("mov %%rbp, %0" : "=r"(rbp));

    for (int i = 0; i < 8 && rbp; i++) {
        uint64_t ret_addr = *(rbp + 1);
        serial_puts("  #");
        serial_put_dec(i);
        serial_puts(" at ");
        serial_put_hex(ret_addr);
        serial_puts("\n");

        rbp = (uint64_t*)*rbp;
    }

    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    serial_puts("RSP: ");
    serial_put_hex(rsp);
    serial_puts("\n");

    panic("Stack smashing detected");
}