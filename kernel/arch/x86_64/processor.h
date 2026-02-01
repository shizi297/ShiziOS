/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>

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

#define IDT_ENTRY_COUNT 256

// 门描述符类型：中断门 (0xE) 自动清除 IF，陷阱门 (0xF) 不改变 IF
#define IDT_INTERRUPT_GATE 0xE  // 用于硬件中断
#define IDT_TRAP_GATE 0xF       // 用于异常处理

// 门描述符属性位
#define IDT_PRESENT (1 << 7)    // 描述符存在位
#define IDT_DPL_KERNEL (0 << 5) // 内核级门，只能由内核调用
#define IDT_DPL_USER (3 << 5)   // 用户级门，允许用户程序调用

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
               (((uint64_t)((offset) >> 16) & 0xFFFF) << 48), \
        .high = (uint64_t)((offset) >> 32) \
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
    // 中断栈表指针：为关键异常提供独立栈
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
#define CR4_VME          0x00000001  // 虚拟8086模式扩展
#define CR4_PVI          0x00000002  // 保护模式虚拟中断
#define CR4_TSD          0x00000004  // 时间戳禁用
#define CR4_DE           0x00000008  // 调试扩展
#define CR4_PSE          0x00000010  // 页大小扩展（4MB页）
#define CR4_PAE          0x00000020  // 物理地址扩展
#define CR4_MCE          0x00000040  // 机器检查异常使能
#define CR4_PGE          0x00000080  // 页全局使能
#define CR4_PCE          0x00000100  // 性能监控计数器使能
#define CR4_OSFXSR       0x00000200  // 支持FXSAVE/FXRSTOR
#define CR4_OSXMMEXCPT   0x00000400  // 支持SIMD浮点异常
#define CR4_UMIP         0x00000800  // 用户模式指令阻止
#define CR4_LA57         0x00001000  // 5级分页使能
#define CR4_VMXE         0x00002000  // VMX使能
#define CR4_SMXE         0x00004000  // SMX使能
#define CR4_FSGSBASE     0x00008000  // FS/GS基址快速访问指令使能
#define CR4_PCIDE        0x00010000  // 进程上下文标识符使能
#define CR4_OSXSAVE      0x00020000  // 操作系统支持XSAVE/XRSTOR
#define CR4_SMEP         0x00040000  // 内核模式执行保护
#define CR4_SMAP         0x00080000  // 内核模式访问保护
#define CR4_PKE          0x00100000  // 页密钥使能
#define CR4_CET          0x00200000  // 控制流强制技术使能
#define CR4_PKS          0x00400000  // 页密钥存储使能
#define CR4_UINTR        0x00800000  // 用户中断使能

// CR4配置
#define CR4_CONFIG (CR4_MCE | CR4_PAE | CR4_PSE | CR4_PGE | CR4_OSFXSR | \
                   CR4_OSXMMEXCPT | CR4_OSXSAVE | CR4_FSGSBASE | \
                   CR4_SMEP | CR4_SMAP)

// 存储中断/异常/系统调用/信号处理时的信息
struct pt_regs {
    /*
     * 中断或异常时
     * cpu会自动压入这些
     */
    uint64_t ss;    // 栈段选择子
    uint64_t rsp;   // 异常发生时的栈指针 
    uint64_t rflags;    // 处理器状态标志 
    uint64_t cs;    // 代码段选择子，区分用户态和内核态 
    uint64_t rip;   // 异常发生时的指令地址 
    
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
     * 存储系统调用号/异常错误码
     * 系统调用：保存原始的系统调用号
     * 异常：保存错误码（如果有）
     */
    uint64_t orig_ax;
    
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
    
    /*
     * 线程本地存储（TLS）指针
     * x86-64使用MSR_FS_BASE寄存器保存用户态TLS基址
     */
    uint64_t fs_base;
} __attribute__((packed, aligned(8)));

// 任务切换时保存的信息
struct thread_struct {
    uint64_t rsp;   // 内核栈指针
    uint64_t rip;   // 返回地址/指令指针
    uint64_t cr3;   // 页表基址

    uint64_t fs_base;   // 用户态tls
};

// CPU暂停,用于优化等待循环，防止过度占用执行资源
static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

// 设置gs寄存器
static inline void set_gs_base(uint64_t base) {
    uint32_t low = base & 0xFFFFFFFF;
    uint32_t high = base >> 32;
    __asm__ volatile(
        "wrmsr\n"
        : 
        : "c" (MSR_GS_BASE), "a" (low), "d" (high)
        : "memory"
    );
}

// 用来表示是否设置CR4_FSGSBASE
typedef enum {
    NO_FAGSBASE = 0,
    FSGSBASE = 1,
} fsgsbase_set;

/*
 * 写入CR4(使用CR4_CONFIG)
 *
 * @param fsgsbase 是否设置fsgsbase位
 */ 
static inline void set_cr4(fsgsbase_set fsgsbase) {
    uint64_t current_cr4;
    uint64_t new_cr4;
    uint64_t config;
    
    // 读取当前CR4值
    __asm__ volatile("mov %%cr4, %0" : "=r"(current_cr4));
    
    // 根据参数调整配置
    if (fsgsbase == NO_FAGSBASE) {
        config = (uint64_t)CR4_CONFIG & ~CR4_FSGSBASE;
    } else {
        config = (uint64_t)CR4_CONFIG;
    }
    
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

// 获取gdt模版的虚拟地址
uint64_t *get_gdt_temp(void);

// 获取idt模版的虚拟地址
struct idt_gate* get_idt_temp(void);

// 获取tss模版的虚拟地址
struct tss* get_tss_temp(void);

// 初始化所有模版
void processor_init(void);

#endif // PROCESSOR_H