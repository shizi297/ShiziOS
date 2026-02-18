/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <fault.h>

/*
 * GDT 条目数量
 * 包括:
 * 1. NULL 描述符
 * 2. 内核代码段
 * 3. 内核数据段
 * 4. 用户代码段
 * 5. 用户数据段
 * 6. TSS 描述符低 64 位
 * 7. TSS 描述符高 64 位
 */
#define GDT_ENTRY_COUNT 7

typedef uint64_t gdte;

// GDT 条目索引
#define GDT_NULL_INDEX 0
#define GDT_KERNEL_CODE_INDEX 1
#define GDT_KERNEL_DATA_INDEX 2
#define GDT_USER_CODE_INDEX 3
#define GDT_USER_DATA_INDEX 4
#define GDT_TSS_LOW_INDEX 5
#define GDT_TSS_HIGH_INDEX 6

// 段选择子：索引 << 3 | RPL
#define GDT_NULL_SELECTOR (GDT_NULL_INDEX << 3)
#define GDT_KERNEL_CODE_SELECTOR (GDT_KERNEL_CODE_INDEX << 3)
#define GDT_KERNEL_DATA_SELECTOR (GDT_KERNEL_DATA_INDEX << 3)

// 用户选择子需要 RPL = 3
#define GDT_USER_CODE_SELECTOR ((GDT_USER_CODE_INDEX << 3) | 3)
#define GDT_USER_DATA_SELECTOR ((GDT_USER_DATA_INDEX << 3) | 3)
#define GDT_TSS_SELECTOR (GDT_TSS_LOW_INDEX << 3)

/*
 * 64 位模式固定段描述符值
 * 内核代码段: P=1, DPL=0, S=1, Type=1010(执行/读), G=1, L=1, D=0
 * 内核数据段: P=1, DPL=0, S=1, Type=0010(读/写), G=1
 * 用户代码段: P=1, DPL=3, S=1, Type=1010(执行/读), G=1, L=1, D=0
 * 用户数据段: P=1, DPL=3, S=1, Type=0010(读/写), G=1
 * NULL 描述符必须为 0
 */
#define GDT_KERNEL_CODE_DESC 0x00209A0000000000
#define GDT_KERNEL_DATA_DESC 0x0000920000000000
#define GDT_USER_CODE_DESC 0x0020FA0000000000
#define GDT_USER_DATA_DESC 0x0000F20000000000
#define GDT_NULL_DESC 0x0000000000000000

/*
 * GDT TSS 段构建
 * TSS 描述符类型: 0x89 = P=1, DPL=00, Type=1001 (64 位 TSS 可用)
 * limit 必须至少为 0x67 (TSS 段最小尺寸)
 */
#define GDT_SET_LOW_TSS(base, limit) \
    ((((uint64_t)(base) & 0xFFFFFF) << 16) | \
     ((uint64_t)(limit) & 0xFFFF) | \
     (((uint64_t)((limit) >> 16) & 0xF) << 48) | \
     ((uint64_t)0x89 << 40) | \
     (((uint64_t)((base) >> 24) & 0xFF) << 56))

#define GDT_SET_HIGH_TSS(base) ((uint64_t)(base) >> 32)

/*
 * IDT 条目结构体
 * 64位门描述符为16字节，由两个uint64_t组成
 */
struct idt_gate {
    uint64_t low;
    uint64_t high;
} __attribute__((packed, aligned(16)));

extern uint64_t irq_table[256];

#define IDT_ENTRY_COUNT 256

// 门描述符类型：中断门 (0xE) 自动清除 IF，陷阱门 (0xF) 不改变 IF
#define IDT_INTERRUPT_GATE 0xE  // 用于硬件中断
#define IDT_TRAP_GATE 0xF       // 用于异常处理

// 门描述符属性位
#define IDT_PRESENT (1 << 7)    // 描述符存在位
#define IDT_DPL_KERNEL (0 << 5) // 内核级门，只能由内核调用
#define IDT_DPL_USER (3 << 5)   // 用户级门，允许用户程序调用

// 中断号
#define IRQ_APIC    33
#define IRQ_PIT     34
#define IRQ_RES1    35
#define IRQ_RES2    36
#define IRQ_RES3    37
#define IRQ_RES4    38
#define IRQ_RES5    39
#define IRQ_TLB_REFRESH 40
#define IRQ_RES6    41
#define IRQ_RES7    42
#define IRQ_RES8    43
#define IRQ_RES9    44
#define IRQ_RES10   45
#define IRQ_RES11   46
#define IRQ_RES12   47

/*
 * IDT 门描述符构建宏
 * 64 位门描述符为 16 字节，由两个 uint64_t 组成
 * ist: 0-7，选择 TSS 中的 IST 栈指针，0 表示使用当前栈
 * 返回 struct idt_gate 结构体
 */
#define IDT_MAKE_GATE(offset, selector, ist, type, dpl) \
    (struct idt_gate) { \
        .low = (((uint64_t)(offset) & 0xFFFF) << 0) | \
               (((uint64_t)(selector) & 0xFFFF) << 16) | \
               (((uint64_t)(ist) & 0x7) << 32) | \
               (((uint64_t)(type) & 0xF) << 40) | \
               (((uint64_t)(dpl) & 0x3) << 45) | \
               ((uint64_t)IDT_PRESENT << 47) | \
               (((uint64_t)((uint64_t)(offset) >> 16) & 0xFFFF) << 48), \
        .high = (uint64_t)((uint64_t)(offset) >> 32) \
    }

/*
 * TSS 结构
 * 仅用于栈切换和 I/O 权限
 */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;   // Ring 0 栈指针，任务切换时必须更新
    uint64_t rsp1;   // 保留，设为 0
    uint64_t rsp2;   // 保留，设为 0
    uint64_t reserved1;
    // 中断栈表指针
    uint64_t ist1;   // 通常用于双重错误 (#DF)
    uint64_t ist2;   // 通常用于 NMI
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;  // I/O 权限位图基址，设为禁用
}__attribute__((packed));

// 模型特定寄存器 (MSR) 地址
#define MSR_EFER 0xC0000080            // 扩展功能使能寄存器
#define MSR_STAR 0xC0000081            // 系统调用配置：内核/用户 CS
#define MSR_LSTAR 0xC0000082           // 64 位系统调用入口点
#define MSR_CSTAR 0xC0000083           // 兼容模式系统调用入口点
#define MSR_SFMASK 0xC0000084          // 系统调用标志屏蔽
#define MSR_FS_BASE 0xC0000100         // FS 段基址
#define MSR_GS_BASE 0xC0000101         // GS 段基址
#define MSR_KERNEL_GS_BASE 0xC0000102  // 内核 GS 基址 (swapgs 使用)
#define MSR_IA32_APIC_BASE         0x1B
#define MSR_IA32_TSC_DEADLINE      0x6E0  
#define APIC_BASE_MSR_ENABLE       (1ULL << 11)
#define APIC_BASE_MSR_X2APIC       (1ULL << 10)
#define APIC_TSC_DEADLINE   2

// x2APIC MSR 地址 
#define X2APIC_MSR_BASE           0x800
#define X2APIC_MSR_APIC_ID        (X2APIC_MSR_BASE + 0x02)
#define X2APIC_MSR_VERSION        (X2APIC_MSR_BASE + 0x03)
#define X2APIC_MSR_TPR            (X2APIC_MSR_BASE + 0x08)
#define X2APIC_MSR_EOI            (X2APIC_MSR_BASE + 0x0B)
#define X2APIC_MSR_LDR            (X2APIC_MSR_BASE + 0x0D)
#define X2APIC_MSR_SVR            (X2APIC_MSR_BASE + 0x0F)
#define X2APIC_MSR_ISR_BASE       (X2APIC_MSR_BASE + 0x10)
#define X2APIC_MSR_TMR_BASE       (X2APIC_MSR_BASE + 0x18)
#define X2APIC_MSR_IRR_BASE       (X2APIC_MSR_BASE + 0x20)
#define X2APIC_MSR_ESR            (X2APIC_MSR_BASE + 0x28)
#define X2APIC_MSR_LVT_CMCI       (X2APIC_MSR_BASE + 0x2F)
#define X2APIC_MSR_ICR            (X2APIC_MSR_BASE + 0x30)
#define X2APIC_MSR_LVT_TIMER      (X2APIC_MSR_BASE + 0x32)
#define X2APIC_MSR_LVT_THERMAL    (X2APIC_MSR_BASE + 0x33)
#define X2APIC_MSR_LVT_PMI        (X2APIC_MSR_BASE + 0x34)
#define X2APIC_MSR_LVT_LINT0      (X2APIC_MSR_BASE + 0x35)
#define X2APIC_MSR_LVT_LINT1      (X2APIC_MSR_BASE + 0x36)
#define X2APIC_MSR_LVT_ERROR      (X2APIC_MSR_BASE + 0x37)
#define X2APIC_MSR_TIMER_INITCNT  (X2APIC_MSR_BASE + 0x38)
#define X2APIC_MSR_TIMER_CURRCNT  (X2APIC_MSR_BASE + 0x39)
#define X2APIC_MSR_TIMER_DIV      (X2APIC_MSR_BASE + 0x3E)
#define X2APIC_MSR_SELF_IPI       (X2APIC_MSR_BASE + 0x3F)

// EFER 寄存器位定义
#define EFER_SCE (1 << 0)   // 系统调用扩展使能
#define EFER_LME (1 << 8)   // 长模式使能
#define EFER_LMA (1 << 10)  // 长模式激活 (只读)
#define EFER_NXE (1 << 11)  // 禁止执行位使能

/*
 * 用于 MSR_STAR 配置的系统调用段选择子
 * STAR[47:32] = 返回时的 CS/SS (用户模式)
 * STAR[31:16] = 入口时的 CS/SS (内核模式)
 */
#define STAR_KERNEL_CS GDT_KERNEL_CODE_SELECTOR
#define STAR_USER_CS (GDT_USER_CODE_SELECTOR & ~3)

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
     * 保存这些通用寄存器
     * 因为有的用户程序会使用他们
     */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;

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
    uint64_t r11;   // 系统调用时保存用户rflags，也作为通用寄存器r11
    uint64_t r10;   // 系统调用第4个参数 
    uint64_t r9;    // 系统调用第6个参数 
    uint64_t r8;    // 系统调用第5个参数 
    uint64_t rax;   // 系统调用号/返回值 
    uint64_t rcx;   // 系统调用时保存用户rip，也作为通用寄存器rcx 
    uint64_t rdx;   // 系统调用第3个参数 
    uint64_t rsi;   // 系统调用第2个参数 
    uint64_t rdi;   // 系统调用第1个参数 

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
    uint64_t rsp;   // 内核栈指针
    uint64_t rip;   // 返回地址/指令指针
    uint64_t cr3;   // 页表基址

    uint64_t fs_base;   // 用户态tls

    struct fpu_state fpu_state;
};

// CPU暂停,用于优化等待循环，防止过度占用执行资源
static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

// 读取指定的 MSR 寄存器
static inline uint64_t msr_read(uint32_t reg) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(reg));
    return ((uint64_t)high << 32) | low;
}

// 写入指定的 MSR 寄存器
static inline void msr_write(uint32_t reg, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    asm volatile ("wrmsr" :: "a"(low), "d"(high), "c"(reg));
}

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
static bool cpuid_fsgsbase(void) {
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));
    
    return (ebx & (1 << 0)) != 0;
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

// 获取当前CPU的APIC ID
static inline uint32_t get_apic_id(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1));
    return ebx >> 24;
}
 
// 为当前 CPU 启用 x2APIC 模式
static inline void set_apic_x2apic(void) {
    uint64_t msr_val = msr_read(MSR_IA32_APIC_BASE);

    // 已经处于 x2APIC 模式，无需操作
    if ((msr_val & (APIC_BASE_MSR_X2APIC | APIC_BASE_MSR_ENABLE))
         == (APIC_BASE_MSR_X2APIC | APIC_BASE_MSR_ENABLE))
        return;

    msr_val |= APIC_BASE_MSR_X2APIC | APIC_BASE_MSR_ENABLE;
    msr_write(MSR_IA32_APIC_BASE, msr_val);
}

/**
 * 发送 EOI，通知 APIC 中断处理已完成
 * 在每个中断处理程序结束时调用
 */ 
static inline void processor_eoi(void) {
    msr_write(X2APIC_MSR_EOI, 0);
}

/**
 * 设置任务优先级 (TPR)
 *
 * @param priority 要设置的优先级值
 */
static inline void apic_set_tpr(uint8_t priority) {
    msr_write(X2APIC_MSR_TPR, priority);
}

/**
 * 配置本地向量表 (LVT) 定时器条目
 *
 * @param vector 中断向量号
 * @param mode 触发模式 
 * @param mask 屏蔽位 (1=屏蔽，0=启用)
 */
static inline void apic_set_lvt_timer(uint32_t vector, uint32_t mode, uint32_t mask) {
    uint64_t val = ((uint64_t)vector & 0xFF) | (((uint64_t)mode & 0x7) << 8) | (((uint64_t)mask & 0x1) << 16);
    msr_write(X2APIC_MSR_LVT_TIMER, val);
}

/**
 * 设置 TSC DEADLINE的触发时间
 *
 * @param tsc_value TSC截止值
 */
static inline void apic_set_tsc_deadline(uint64_t tsc_value) {
    msr_write(MSR_IA32_TSC_DEADLINE, tsc_value);
}

// 读取错误状态寄存器
static inline uint32_t apic_read_esr(void) {
    msr_write(X2APIC_MSR_ESR, 0);
    return (uint32_t)msr_read(X2APIC_MSR_ESR);
}

/**
 * 发送处理器间中断 (IPI)。
 *
 * @param apic_id 目标CPU的APIC ID
 * @param vector 中断向量号
 */
static inline void processor_send_ipi(uint32_t apic_id, uint32_t vector) {
    uint64_t icr_val = ((uint64_t)apic_id << 32) | vector;
    msr_write(X2APIC_MSR_ICR, icr_val);
}

// 广播IPI的目标范围
typedef enum {
    NO_ONESELF = 0x2,  // 所有核心，包括自身 
    ONESELF = 0x3,  // 所有核心，排除自身 
} apic_scope;

/**
 * 向指定范围的核心广播IPI
 * 
 * @param vector 中断向量号
 * @param dest 广播目标范围 
 */
static inline void processor_send_ipi_all(uint32_t vector, apic_scope scope) {
    uint64_t icr_val = ((uint64_t)scope << 18) | (vector & 0xFF);
    msr_write(X2APIC_MSR_ICR, icr_val);
}

/**
 * 获取当前cpu标志值
 * 
 * @return 当前cpu标志值
 */
static inline uint64_t get_cpu_flags(void) {
    uint64_t flags;
    __asm__ volatile(
        "pushfq\n\t"        
        "pop %0"            
        : "=r" (flags)     
        :
        : "memory"          
    );
    return flags;
}

#include <stdint.h>

/**
 * 设置CPU标志寄存器
 * 
 * @param flags 要设置的标志值
 */
static inline void write_cpu_flags(uint64_t flags) {
    __asm__ volatile(
        "push %0\n\t"       
        "popfq"            
        :
        : "r" (flags)       
        : "memory", "cc"    
    );
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

// 初始化所有模版
void processor_init(void);

#endif // PROCESSOR_H