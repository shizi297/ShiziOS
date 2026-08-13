/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <fault.h>
#include <kio.h>
#include <processor.h>
#include <heap.h>
#include <task.h>
#include <signal.h>
#include <bootboot.h>
#include <asm/smp.h>

enum pf_flags {
    PF_PROT  = 1 << 0,
    PF_WRITE = 1 << 1,
    PF_USER  = 1 << 2,
    PF_INSTR = 1 << 4,
};

static void print_regs(const struct pt_regs *regs) {
    printk(
        "rdi=%#lx\n"
        "rsi=%#lx\n"
        "rdx=%#lx\n"
        "rcx=%#lx\n"
        "rax=%#lx\n"
        "r8=%#lx\n"
        "r9=%#lx\n"
        "r10=%#lx\n"
        "r11=%#lx\n"
        "rbx=%#lx\n"
        "rbp=%#lx\n"
        "r12=%#lx\n"
        "r13=%#lx\n"
        "r14=%#lx\n"
        "r15=%#lx\n"
        "vector=%lu\n"
        "error_code=%#lx\n"
        "rip=%#lx\n"
        "cs=%#lx\n"
        "rflags=%#lx\n"
        "rsp=%#lx\n"
        "ss=%#lx\n",
        regs->rdi,
        regs->rsi,
        regs->rdx,
        regs->rcx,
        regs->rax,
        regs->r8,
        regs->r9,
        regs->r10,
        regs->r11,
        regs->rbx,
        regs->rbp,
        regs->r12,
        regs->r13,
        regs->r14,
        regs->r15,
        regs->vector,
        regs->error_code,
        regs->rip,
        regs->cs,
        regs->rflags,
        regs->rsp,
        regs->ss
    );
}

void exc_de(struct pt_regs *regs) {
    printk("Divide Error\n");
    print_regs(regs);
    printp("Divide Error\n");
}

void exc_db(struct pt_regs *regs) {
    printk("Debug\n");
    print_regs(regs);
    printp("Debug\n");
}

void exc_nmi(struct pt_regs *regs) {
    printk("Non-Maskable Interrupt\n");
    print_regs(regs);
    printp("Non-Maskable Interrupt\n");
}

void exc_bp(struct pt_regs *regs) {
    printk("Breakpoint\n");
    print_regs(regs);
    printp("Breakpoint\n");
}

void exc_of(struct pt_regs *regs) {
    printk("Overflow\n");
    print_regs(regs);
    printp("Overflow\n");
}

void exc_br(struct pt_regs *regs) {
    printk("Bound Range Exceeded\n");
    print_regs(regs);
    printp("Bound Range Exceeded\n");
}

void exc_ud(struct pt_regs *regs) {
    printk("Invalid Opcode\n");
    print_regs(regs);
    printp("Invalid Opcode\n");
}

void exc_nm(struct pt_regs *regs) {
    printk("Device Not Available\n");
    print_regs(regs);
    printp("Device Not Available\n");
}

void exc_df(struct pt_regs *regs) {
    printk("Double Fault\n");
    print_regs(regs);
    printp("Double Fault\n");
}

void exc_cso(struct pt_regs *regs) {
    printk("Coprocessor Segment Overrun\n");
    print_regs(regs);
    printp("Coprocessor Segment Overrun\n");
}

void exc_ts(struct pt_regs *regs) {
    printk("Invalid TSS\n");
    print_regs(regs);
    printp("Invalid TSS\n");
}

void exc_np(struct pt_regs *regs) {
    printk("Segment Not Present\n");
    print_regs(regs);
    printp("Segment Not Present\n");
}

void exc_ss(struct pt_regs *regs) {
    printk("Stack-Segment Fault\n");
    print_regs(regs);
    printp("Stack-Segment Fault\n");
}

void exc_gp(struct pt_regs *regs) {
    printk("General Protection Fault\n");
    print_regs(regs);
    printp("General Protection Fault\n");
}

void exc_pf(struct pt_regs *regs) {
    uintptr_t fault_addr = processor_read_cr2();
    uint64_t error_code = regs->error_code;

    // 内核态缺页：无法恢复，打印信息后挂起
    if (!(error_code & PF_USER)) {
        printk("Page Fault\n");
        print_regs(regs);
        printp("Page Fault\n");
    }

    // 转换硬件错误码为 VMM 访问类型
    uint32_t access_flags = 0;
    if (error_code & PF_WRITE) {
        access_flags |= VM_WRITE;
    } else {
        access_flags |= VM_READ;
    }
    if (error_code & PF_INSTR) {
        access_flags |= VM_EXEC;
    }

    as_t *as = smp_get_as();
    int ret = vheap_handle_fault(as, fault_addr, access_flags);

    // 修复失败，发送 SIGSEGV 终止进程
    if (ret < 0) {
        task_struct *curr = smp_get_task_current();
        if (curr) {
            task_send_signal(curr, SIGSEGV);
        } else {
            printp("Page fault handling error\n");
        }
    }
}

void exc_mf(struct pt_regs *regs) {
    printk("x87 Floating-Point Exception\n");
    print_regs(regs);
    printp("x87 Floating-Point Exception\n");
}

void exc_ac(struct pt_regs *regs) {
    printk("Alignment Check\n");
    print_regs(regs);
    printp("Alignment Check\n");
}

void exc_mc(struct pt_regs *regs) {
    printk("Machine Check\n");
    print_regs(regs);
    printp("Machine Check\n");
}

void exc_xm(struct pt_regs *regs) {
    printk("SIMD Floating-Point Exception\n");
    print_regs(regs);
    printp("SIMD Floating-Point Exception\n");
}

void exc_ve(struct pt_regs *regs) {
    printk("Virtualization Exception\n");
    print_regs(regs);
    printp("Virtualization Exception\n");
}

void __stack_chk_fail(void) {
    printk("\n[PANIC] Stack smashing detected\n");

    uint64_t *rbp;
    asm volatile("mov %%rbp, %0" : "=r"(rbp));

    for (int i = 0; i < 8 && rbp; i++) {
        uint64_t ret_addr = *(rbp + 1);
        printk("  #%d at %#lx\n", i, ret_addr);

        rbp = (uint64_t*)*rbp;
    }

    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    printk("RSP: %#lx\n", rsp);

    printp("Stack smashing detected\n");
}