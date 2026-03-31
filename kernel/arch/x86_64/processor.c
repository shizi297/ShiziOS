/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include "processor.h"
#include "fault.h"

extern uint64_t irq_entry_table[256];

extern void ret_from_kernel_thread(void);

static gdte gdt_temp[GDT_ENTRY_COUNT];
static struct idt_gate idt_temp[IDT_ENTRY_COUNT];
static struct tss tss_temp = {0};

uint64_t irq_table[256] = {
    [EXC_DE] = (uint64_t)exc_de,
    [EXC_DB] = (uint64_t)exc_db,
    [EXC_NMI] = (uint64_t)exc_nmi,
    [EXC_BP] = (uint64_t)exc_bp,
    [EXC_OF] = (uint64_t)exc_of,
    [EXC_BR] = (uint64_t)exc_br,
    [EXC_UD] = (uint64_t)exc_ud,
    [EXC_NM] = (uint64_t)exc_nm,
    [EXC_DF] = (uint64_t)exc_df,
    [EXC_CSO] = (uint64_t)exc_cso,
    [EXC_TS] = (uint64_t)exc_ts,
    [EXC_NP] = (uint64_t)exc_np,
    [EXC_SS] = (uint64_t)exc_ss,
    [EXC_GP] = (uint64_t)exc_gp,
    [EXC_PF] = (uint64_t)exc_pf,
    [EXC_SPUR] = 0, // 伪中断不需要处理函数
    [EXC_MF] = (uint64_t)exc_mf,
    [EXC_AC] = (uint64_t)exc_ac,
    [EXC_MC] = (uint64_t)exc_mc,
    [EXC_XM] = (uint64_t)exc_xm,
    [EXC_VE] = (uint64_t)exc_ve,
};

/*
 * GDT模版初始化
 * 后续被复制到per_cpu中
 */
static void gdt_temp_init(void) {
    gdt_temp[GDT_NULL_INDEX] = GDT_NULL_DESC;
    gdt_temp[GDT_KERNEL_CODE_INDEX] = GDT_KERNEL_CODE_DESC;
    gdt_temp[GDT_KERNEL_DATA_INDEX] = GDT_KERNEL_DATA_DESC;
    gdt_temp[GDT_USER_CODE_INDEX] = GDT_USER_CODE_DESC;
    gdt_temp[GDT_USER_DATA_INDEX] = GDT_USER_DATA_DESC;
    // 不初始化 TSS 描述符，在per_cpu结构中设置
}

/*
 * idt模版初始化
 * 后续被复制到per_cpu中
 */
static void idt_temp_init(void) {
    for (int i = 0; i < 256; i++) {
        // 获取中断处理函数地址
        uint64_t handler_addr = (uint64_t)irq_entry_table[i];
        
        uint8_t gate_type;
        uint8_t dpl;
        
        if (i <= EXC_MAX_VEC) {
            // 获取异常的门类型与态
            gate_type = ((FAULT_GATE_TYPE_BITMAP >> i) & 1) ? 
                       IDT_TRAP_GATE : IDT_INTERRUPT_GATE;
            dpl = ((FAULT_DPL_BITMAP >> i) & 1) ? 
                 IDT_DPL_USER : IDT_DPL_KERNEL;
        } else {
            /*
             * 因为还没有中断设置
             * 所以先用默认配置（中断门、内核态）
             */
            gate_type = IDT_INTERRUPT_GATE;
            dpl = IDT_DPL_KERNEL;
        }
        
        // 使用内核代码段选择子，ist固定为0（使用当前栈）
        idt_temp[i] = IDT_MAKE_GATE(
            handler_addr,
            GDT_KERNEL_CODE_SELECTOR,
            0,          // ist=0，使用当前栈
            gate_type,
            dpl
        );
    }
}

/*
 * tss模版初始化
 * 后续被复制到per_cpu结构体中
 */
static void tss_temp_init(void) {
    // 禁用io位图
    tss_temp.io_map_base = sizeof(struct tss);
}

// 获取gdt模版的虚拟地址
uint64_t *get_gdt_temp(void) {
    return gdt_temp;
}

// 获取idt模版的虚拟地址
struct idt_gate* get_idt_temp(void) {
    return idt_temp;
}

// 获取tss模版的虚拟地址
struct tss* get_tss_temp(void) {
    return &tss_temp;
}

// 早期任务切换
void processor_boot_switch(struct thread_struct *thread) {
    __asm__ volatile(
        "movq %0, %%rsp\n\t"
        "movq %1, %%rbx\n\t"
        "movq %2, %%rbp\n\t"
        "ret\n"
        : : "r"(thread->rsp), "r"(thread->rbx), "r"(thread->rbp)
        : "memory"
    );
}

/**
 * 任务切换
 * 
 * @param prev 当前任务的 thread_struct 指针
 * @param next 下一个任务的 thread_struct 指针
 * 
 * 此函数不会保存/恢复fpu状态
 * fpu由上层调用者负责
 */
__attribute__((naked, noinline))
void switch_to(struct thread_struct *prev, struct thread_struct *next) {
    __asm__ volatile (
        // 保存 prev 的寄存器到 prev->thread
        "movq %%rbx,  %c[thr_rbx](%%rdi)\n\t"
        "movq %%rbp,  %c[thr_rbp](%%rdi)\n\t"
        "movq %%r12,  %c[thr_r12](%%rdi)\n\t"
        "movq %%r13,  %c[thr_r13](%%rdi)\n\t"
        "movq %%r14,  %c[thr_r14](%%rdi)\n\t"
        "movq %%r15,  %c[thr_r15](%%rdi)\n\t"
        "movq %%rsp,  %c[thr_rsp](%%rdi)\n\t"

        // 保存 fs_base
        "rdfsbase %%rax\n\t"
        "movq %%rax,  %c[thr_fs](%%rdi)\n\t"

        // 保存 cr3
        "movq %%cr3, %%rax\n\t"
        "movq %%rax,  %c[thr_cr3](%%rdi)\n\t"

        // 加载 next 的寄存器
        "movq %c[thr_rsp](%%rsi), %%rsp\n\t"
        "movq %c[thr_rbx](%%rsi), %%rbx\n\t"
        "movq %c[thr_rbp](%%rsi), %%rbp\n\t"
        "movq %c[thr_r12](%%rsi), %%r12\n\t"
        "movq %c[thr_r13](%%rsi), %%r13\n\t"
        "movq %c[thr_r14](%%rsi), %%r14\n\t"
        "movq %c[thr_r15](%%rsi), %%r15\n\t"

        // 恢复 fs_base
        "movq %c[thr_fs](%%rsi), %%rax\n\t"
        "wrfsbase %%rax\n\t"

        // 加载页表
        "movq %c[thr_cr3](%%rsi), %%rax\n\t"
        "movq %%rax, %%cr3\n\t"

        // 跳转到新任务（栈顶已有返回地址）
        "ret\n"
        :
        : [thr_cr3]  "i" (THR_CR3),
          [thr_rsp]  "i" (THR_RSP),
          [thr_fs]   "i" (THR_FS),
          [thr_rbx]  "i" (THR_RBX),
          [thr_rbp]  "i" (THR_RBP),
          [thr_r12]  "i" (THR_R12),
          [thr_r13]  "i" (THR_R13),
          [thr_r14]  "i" (THR_R14),
          [thr_r15]  "i" (THR_R15)
        : "rax", "memory"
    );
}

/**
 * 保存fpu信息
 * 
 * @param state fpu信息结构体
 */
void fpu_save(struct thread_struct *thread) {
    struct fpu_state *state = &thread->fpu_state;
    uint32_t lmask = 0xffffffff;
    uint32_t hmask = 0xffffffff;
    __asm__ volatile (
        "xsave %0"
        : "+m" (*(char *)state->xsaves)
        : "a" (lmask), "d" (hmask)
        : "memory"
    );
}

/**
 * 恢复fpu状态
 * 
 * @param state fpu信息结构体
 */
void fpu_restore(struct thread_struct *thread) {
    struct fpu_state *state = &thread->fpu_state;
    uint32_t lmask = 0xffffffff;
    uint32_t hmask = 0xffffffff;
    __asm__ volatile (
        "xrstor %0"
        : : "m" (*(char *)state->xsaves), "a" (lmask), "d" (hmask)
        : "memory"
    );
}

// 为任务分配thread_struct结构体
struct thread_struct *thread_struct_create(void) {
    struct thread_struct *thread = (struct thread_struct *)kheap_alloc(sizeof(struct thread_struct));
    if (!thread) goto fail;
    void *fpu = kheap_alloc(xsaves_size);
    if (!fpu) goto fail;

    thread->fpu_state.size = xsaves_size;
    thread->fpu_state.xsaves = fpu;

    return thread;

    fail:
        if (fpu) kheap_free(fpu);
        if (thread) kheap_free(thread);
        return NULL;
}

// 销毁thread_struct结构体
void thread_struct_destroy(struct thread_struct *thread) {
    kheap_free(thread->fpu_state.xsaves);
    kheap_free(thread);
}

// 设置任务thread为内核线程并初始化
void thread_struct_to_kernel_init(
    struct thread_struct *thread,
    void *stack_top, 
    void *pgd,
    void (*func)(void *), 
    void *arg
) {
    thread->cr3 = (uint64_t)pgd;
    thread->rsp = (uint64_t)stack_top;
    thread->rbp = (uint64_t)arg;
    thread->rbx = (uint64_t)func;
}

// 初始化所有模版
void processor_init(void) {
    gdt_temp_init();
    idt_temp_init();
    tss_temp_init();
}