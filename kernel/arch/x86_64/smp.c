/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <smp.h>
#include <processor.h>
#include <bootboot.h>
#include <heap.h>
#include <stdint.h>
#include <config.h>
#include <serial.h>
#include <acpi.h>
#include <pit.h>
#include <ioapic.h>

#define SMP_PRINT(str) \
    serial_puts("[SMP] " str)

#define SMP_PANIC(str) \
    serial_puts("[SMP] PANIC: " str); 

gdte *gdt_ptr = NULL;
struct idt_gate *idt_ptr = NULL;
struct tss *tss_ptr = NULL;

struct logicalid_to_apicid_struct *logicalid_to_apicid_struct_ptr = 0;

per_cpu *per_cpu_ptr = NULL;

/**
 * 存放逻辑cpuid对应的apicid
 * 
 * logicalid_to_apic_arr的数组大小是最大的逻辑cpuid
 * 对应的位置存放apicid
 */
struct logicalid_to_apicid_struct {
    uint16_t count;
    uint16_t logicalid_to_apicid_arr[];
};

/*
 * 初始化gdt
 *
 * @param gdt_temp_addr 指向gdt模版的指针
 * @param gdt_ptr 要初始化的gdt数组
 * @param tss_ptr 要填充的tss数组
 * @param cpu_count cpu逻辑核心数量
 */
static inline void gdt_init(
    gdte *gdt_temp_addr, 
    gdte *gdt_ptr, 
    struct tss *tss_ptr, 
    uint16_t cpu_count
) {
    // 复制gdt模版到要初始化gdt并填充tss段
    for(int current_gdt = 0;current_gdt < cpu_count;current_gdt++) {
        for (int current_gdte = 0;current_gdte < GDT_ENTRY_COUNT - 2;current_gdte++) {
            gdt_ptr[current_gdt * GDT_ENTRY_COUNT+ current_gdte] = gdt_temp_addr[current_gdte]; 
        }

        // 计算tss基地址和界限
        uint64_t tss_base = (uint64_t)&tss_ptr[current_gdt];
        uint64_t tss_limit = sizeof(struct tss) - 1;

        // 填充tss段
        gdt_ptr[current_gdt * GDT_ENTRY_COUNT + GDT_TSS_LOW_INDEX] = GDT_SET_LOW_TSS(tss_base, tss_limit);
        gdt_ptr[current_gdt * GDT_ENTRY_COUNT + GDT_TSS_HIGH_INDEX] = GDT_SET_HIGH_TSS(tss_base);
    }
}

/*
 * 初始化idt
 *
 * @param idt_temp_addr 指向idt模版的指针
 * @param idt_ptr 要初始化的idt数组
 * @param cpu_count cpu逻辑核心数量
 */
static inline void idt_init(
    struct idt_gate *idt_temp_addr,
    struct idt_gate *idt_ptr,
    uint16_t cpu_count
) {
    // 复制模版idt到所有要初始化的idt
    for (int current_idt = 0;current_idt < cpu_count;current_idt++) {
        for (int current_idte = 0;current_idte < IDT_ENTRY_COUNT;current_idte++) {
            idt_ptr[current_idt * IDT_ENTRY_COUNT + current_idte] = idt_temp_addr[current_idte];
        }
    }
}

/*
 * 初始化tss
 *
 * @param tss_temp_addr 指向tss模版的指针
 * @param tss_ptr 要初始化的tss数组
 * @param 内核栈数组
 * @param cpu_count cpu逻辑核心数量
 */
static inline void tss_init(
    struct tss *tss_temp_addr,
    struct tss *tss_ptr,
    void *stack,
    uint16_t cpu_count
) {
    // 复制模版tss到需要初始化的tss
    for (int current_tss = 0;current_tss < cpu_count;current_tss++) {
        tss_ptr[current_tss] = tss_temp_addr[0];

        // 计算栈顶
        uint64_t stack_top = (uint64_t)stack + (current_tss + 1) * KERNEL_START_SIZE;

        // 设置内核栈
        tss_ptr[current_tss].rsp0 = stack_top;
    }
}

/*
 * 多核数据结构初始化
 * 负责给所有核心cpu提供基础数据结构
 * 
 * @param gdt_temp_addr 指向gdt模版的指针
 * @param idt_temp_addr 指向idt模版的指针
 * @param tss_temp_addr 指向tss模版的指针
 */
void smp_data_init(
    gdte *gdt_temp_addr, 
    struct idt_gate *idt_temp_addr, 
    struct tss *tss_temp_addr
) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    // 获取cpu最大逻辑核心数
    uint16_t max_cpu_count = bootboot->numcores;

    {
        uint32_t gdt_size = sizeof(gdte) * GDT_ENTRY_COUNT * max_cpu_count;
        uint32_t idt_size = sizeof(struct idt_gate) * IDT_ENTRY_COUNT * max_cpu_count;
        uint32_t tss_size = sizeof(struct tss) * max_cpu_count;

        gdt_ptr = kheap_alloc(gdt_size);
        idt_ptr = kheap_alloc(idt_size);
        tss_ptr = kheap_alloc(tss_size);
        
        if (!gdt_ptr || !idt_ptr || !tss_ptr) SMP_PANIC("memory allocation failed");
    }
    // 给每个cpu分配内核栈
    void *kernel_stack = kheap_alloc(KERNEL_START_SIZE * max_cpu_count);
    
    if (!kernel_stack) SMP_PANIC("memory allocation failed");

    tss_init(tss_temp_addr, tss_ptr, kernel_stack, max_cpu_count);
    idt_init(idt_temp_addr, idt_ptr, max_cpu_count);
    gdt_init(gdt_temp_addr, gdt_ptr, tss_ptr, max_cpu_count);

    // 分配per_cpu
    per_cpu_ptr = kheap_alloc(sizeof(per_cpu) * max_cpu_count);
    if (!per_cpu_ptr) SMP_PANIC("memory allocation failed");

    // 分配逻辑cpuid映射apicid结构体
    uint16_t logicalid_to_apicid_struct_size = sizeof(uint16_t) + (sizeof(uint16_t) * max_cpu_count);
    logicalid_to_apicid_struct_ptr = kheap_alloc(logicalid_to_apicid_struct_size);
    if (!logicalid_to_apicid_struct_ptr) SMP_PANIC("memory allocation failed");

    logicalid_to_apicid_struct_ptr->count = max_cpu_count;

    SMP_PRINT("smp data init succeed\n");
}

// 获取cpu核心的逻辑id
uint32_t get_logical_id(void) {
    per_cpu *per_cpu_ptr = get_gs_base();
    return per_cpu_ptr->logical_id;
}

/*
 * 初始化所有核心
 *
 * @param logical_id 当前cpu的逻辑cpuid
 * @param apic_id 当前cpu的apicid
 */
__attribute__((noreturn))
void smp_init(uint32_t logical_id, uint32_t apic_id) {
    // 设置当前cpu的栈
    uint64_t new_stack_top = tss_ptr[logical_id].rsp0;

    __asm__ volatile(
        "movq %0, %%rsp\n"
        "xorq %%rbp, %%rbp\n"
        :
        : "r"(new_stack_top), "D"(logical_id)
        : "memory"
    );

    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    logicalid_to_apicid_struct_ptr->logicalid_to_apicid_arr[logical_id] = apic_id;

    // 计算当前CPU在数组中的偏移
    uint64_t gdt_offset = logical_id * GDT_ENTRY_COUNT;
    uint64_t idt_offset = logical_id * IDT_ENTRY_COUNT;

    // 加载GDT
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr = {
        .limit = GDT_ENTRY_COUNT * sizeof(gdte) - 1,
        .base = (uint64_t)&gdt_ptr[gdt_offset]
    };
    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    // 加载IDT
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = {
        .limit = IDT_ENTRY_COUNT * sizeof(struct idt_gate) - 1,
        .base = (uint64_t)&idt_ptr[idt_offset]
    };
    __asm__ volatile("lidt %0" : : "m"(idtr));

    // 加载tss
    uint16_t tss_selector = (GDT_TSS_LOW_INDEX * 8);
    __asm__ volatile("ltr %w0" : : "r"(tss_selector));

    // 设置per_cpu的逻辑cpuid
    per_cpu_ptr[logical_id].logical_id = logical_id;

    // 设置当前cpu的gs到per_cpu
    set_gs_base((uint64_t)&per_cpu_ptr[logical_id]);

    // 如果是bp，执行特定初始化
    if (logical_id == bootboot->bspid) {
        if (!acpi_init()) SMP_PANIC("acpi init failed\n");
        if (!ioapic_init()) SMP_PANIC("ioacpi init failed\n");

        pit_init();
    }

    SMP_PRINT("smp init succeed\n");

    while (1) {
        cpu_pause();
    }
}

/**
 * 注册中断处理函数
 * 
 * @param vector 中断向量号
 * @param handler_addr 处理函数地址
 * 
 * @return 注册成功：true
 * @return 注册失败：false
 * 
 * 使用中断门，DPL=0，IST=0
 */
bool smp_irq_register_handler(uint8_t vector, uint64_t handler_addr) {
    if (vector >= 256 || vector < 32) { 
        // 无效的中断向量
        return false;
    }

    if (handler_addr == 0) {
        // 无效的处理函数地址
        return false;
    }
    
    // 更新数组，让中断处理函数知道位置
    irq_table[vector] = handler_addr;
    
    return true;
}

/**
 * 注销中断处理函数
 * 
 * @param vector 中断向量号
 */
void smp_irq_unregister_handler(uint8_t vector) {
    if (vector >= 256 || vector < 32) { 
        // 无效的中断向量
        return;
    }

    // 恢复默认值
    irq_table[vector] = 0;
}