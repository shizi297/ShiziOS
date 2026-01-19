/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stdint.h>

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
#define GDT_USER_DATA_DESC 0x0000F2000000000000
#define GDT_NULL_DESC 0x0000000000000000

/*
 * TSS 描述符构建宏
 * TSS 描述符类型: 0x89 = P=1, DPL=00, Type=1001 (64 位 TSS 可用)
 * limit 必须至少为 0x67 (TSS 最小尺寸)
 */
#define GDT_SET_LOW_TSS(base, limit) \
    ((((uint64_t)(base) & 0xFFFFFF) << 16) | \
     ((uint64_t)(limit) & 0xFFFF) | \
     (((uint64_t)((limit) >> 16) & 0xF) << 48) | \
     ((uint64_t)0x89 << 40) | \
     (((uint64_t)((base) >> 24) & 0xFF) << 56))

#define GDT_SET_HIGH_TSS(base) ((uint64_t)(base) >> 32)

/*
 * IDT 配置
 * 256 个中断向量，其中 0-31 为处理器异常
 */
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
 */
#define IDT_MAKE_GATE(offset, selector, ist, type, dpl) \
    (uint64_t)((((uint64_t)(offset) & 0xFFFF) << 0) | \
               (((uint64_t)(selector) & 0xFFFF) << 16) | \
               (((uint64_t)(ist) & 0x7) << 32) | \
               (((uint64_t)(type) & 0xF) << 40) | \
               (((uint64_t)(dpl) & 0x3) << 45) | \
               (((uint64_t)IDT_PRESENT) << 47) | \
               (((uint64_t)((offset) >> 16) & 0xFFFF) << 48)), \
    (uint64_t)((uint64_t)((offset) >> 32) & 0xFFFFFFFF)

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
    uint16_t io_map_base;  // I/O 权限位图基址，设为 sizeof(tss) 禁用
};

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

#endif // PROCESSOR_H