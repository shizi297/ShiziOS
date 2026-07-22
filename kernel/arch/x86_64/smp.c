/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <asm/smp.h>
#include <asm/platform_dev.h>
#include <processor.h>
#include <bootboot.h>
#include <heap.h>
#include <stdint.h>
#include <config.h>
#include <kio.h>
#include <acpi.h>
#include <pit.h>
#include <ioapic.h>
#include <tsc.h>
#include <apic.h>
#include <spinlock.h>
#include <task.h>
#include <desc.h>
#include <time.h>
#include <drivers.h>
#include <stdatomic.h>
#include <shizi/types.h>
#include <klibc.h>

#define SMP_PRINT(fmt, ...) \
    printk("[SMP] " fmt, ##__VA_ARGS__)

#define SMP_PANIC(fmt, ...) \
    printp("[SMP] ERROR: " fmt, ##__VA_ARGS__)

gdte *gdt_ptr = NULL;
struct idt_gate *idt_ptr = NULL;
struct tss *tss_ptr = NULL;

struct logicalid_to_apicid_struct *logicalid_to_apicid_struct_ptr = 0;

per_cpu *per_cpu_ptr = NULL;

atomic_bool bp_init = false;

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
        uint64_t stack_top = (uint64_t)stack + (current_tss + 1) * KERNEL_START_SIZE - 128;

        // 设置内核栈
        tss_ptr[current_tss].rsp0 = stack_top;
    }
}

// 获取逻辑cpuid的apicid
_arch uint32_t smp_get_apicid(uint64_t logical_id) {
    return logicalid_to_apicid_struct_ptr->logicalid_to_apicid_arr[logical_id];
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
        
        if (!gdt_ptr || !idt_ptr || !tss_ptr) SMP_PANIC("memory allocation failed\n");
    }

    // 给每个cpu分配内核栈
    void *kernel_stack = kheap_alloc(KERNEL_START_SIZE * max_cpu_count);
    
    if (!kernel_stack) SMP_PANIC("memory allocation failed\n");

    tss_init(tss_temp_addr, tss_ptr, kernel_stack, max_cpu_count);
    idt_init(idt_temp_addr, idt_ptr, max_cpu_count);
    gdt_init(gdt_temp_addr, gdt_ptr, tss_ptr, max_cpu_count);

    // 分配per_cpu
    per_cpu_ptr = kheap_alloc(sizeof(per_cpu) * max_cpu_count);
    if (!per_cpu_ptr) SMP_PANIC("memory allocation failed\n");

    // 分配逻辑cpuid映射apicid结构体
    uint16_t logicalid_to_apicid_struct_size = sizeof(uint16_t) + (sizeof(uint16_t) * max_cpu_count);
    logicalid_to_apicid_struct_ptr = kheap_alloc(logicalid_to_apicid_struct_size);
    if (!logicalid_to_apicid_struct_ptr) SMP_PANIC("memory allocation failed\n");

    logicalid_to_apicid_struct_ptr->count = max_cpu_count;

    SMP_PRINT("smp data init succeed\n");
}

// smp 初始化入口，负责切换栈后跳转真正的执行函数
__attribute__((naked, noreturn))
void smp_init(uint32_t logical_id, uint32_t apic_id) {
    __asm__ volatile (
        "movq    tss_ptr(%%rip), %%rax\n"
        "movl    %%edi, %%ecx\n"
        "imulq   $104, %%rcx, %%rcx\n"
        "movq    4(%%rax,%%rcx), %%rsp\n"
        "xorq    %%rbp, %%rbp\n"
        "jmp     smp_init_raw\n"
        :
        :
        : "rax", "rcx", "memory"
    );
}

/*
 * 初始化所有核心
 *
 * @param logical_id 当前cpu的逻辑cpuid
 * @param apic_id 当前cpu的apicid
 */
__attribute__((noreturn, noinline, used))
static void smp_init_raw(uint32_t logical_id, uint32_t apic_id) {
    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    logicalid_to_apicid_struct_ptr->logicalid_to_apicid_arr[logical_id] = apic_id;

    // 计算索引
    uint64_t gdt_index = logical_id * GDT_ENTRY_COUNT;
    uint64_t idt_index = logical_id * IDT_ENTRY_COUNT;

    // 强制读取指针值
    uintptr_t gdt_base = (uintptr_t)gdt_ptr;
    uintptr_t idt_base = (uintptr_t)idt_ptr;

    // 加载GDT
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr = {
        .limit = GDT_ENTRY_COUNT * sizeof(gdte) - 1,
        .base = gdt_base + gdt_index * sizeof(gdte)
    };
    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    // 刷新数据段寄存器为内核数据段选择子
    __asm__ volatile(
        "movw %0, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "i"(GDT_KERNEL_DATA_SELECTOR)  
        : "ax", "memory"
    );

    // 刷新CS为内核代码段选择子
    __asm__ volatile(
        "pushq %0\n\t"
        "pushq $1f\n\t"
        "lretq\n"
        "1:\n\t"
        :
        : "i"((uint64_t)GDT_KERNEL_CODE_SELECTOR)  
        : "memory"
    );

    // 加载IDT
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = {
        .limit = IDT_ENTRY_COUNT * sizeof(struct idt_gate) - 1,
        .base = idt_base + idt_index * sizeof(struct idt_gate)
    };
    __asm__ volatile("lidt %0" : : "m"(idtr));

    // 加载tss
    uint16_t tss_selector = (GDT_TSS_LOW_INDEX * 8);
    __asm__ volatile("ltr %w0" : : "r"(tss_selector) : "memory");

    // 设置per_cpu的逻辑cpuid
    per_cpu_ptr[logical_id].logical_id = logical_id;

    // 初始化canary
    per_cpu_ptr[logical_id].cancry = 0x28;

    INIT_LIST_HEAD(&per_cpu_ptr[logical_id].migration);

    // 设置当前cpu的gs到per_cpu
    set_gs_base((uint64_t)&per_cpu_ptr[logical_id]);

    // 开启中断
    irq_on();

    if (!tsc_init()) SMP_PANIC("tsc init failed\n");
    if (!apic_init()) SMP_PANIC("apic init failed\n");

    // 如果是bp，执行特定初始化
    if (logical_id == bootboot->bspid) {
        if (!acpi_init()) SMP_PANIC("acpi init failed\n");
        if (!ioapic_init()) SMP_PANIC("ioacpi init failed\n");
        if (!pit_init()) SMP_PANIC("pit init failed\n");
        if (!vfs_init()) SMP_PANIC("vfs init failed");
        if (!task_data_init()) SMP_PANIC("task init failed\n");

        // 通知ap继续执行
        atomic_store_explicit(&bp_init, true, memory_order_relaxed);
    } else {
        // 等待bp完成初始化
        while (!atomic_load_explicit(&bp_init, memory_order_acquire)) {
            cpu_pause();  
        }
    }

    // 初始化时间数据结构
    if (!time_data_init()) SMP_PANIC("time data init failed\n");

    // 写入per_cpu用于获取时间戳
    uint64_t (*ts)(void) = &clocksource_default_read;
    smp_set_timestamp(ts);

    if (!task_init()) SMP_PANIC("task init failed\n");

    static atomic_bool bp_post_init = false;

    // 如果是bp，执行特定初始化
    if (logical_id == bootboot->bspid) {
        if (!acpi_namespace_load()) SMP_PANIC("acpi namespace load failed\n");
        if (!acpi_namespace_init()) SMP_PANIC("acpi namespace init failed\n");
        if (!platform_dev_init()) SMP_PANIC("acpi init failed\n");
        if (!drivers_init()) SMP_PANIC("drivers init failed");

        // 通知ap继续执行
        atomic_store_explicit(&bp_post_init, true, memory_order_relaxed);
    } else {
        // 等待bp完成初始化
        while (!atomic_load_explicit(&bp_post_init, memory_order_acquire)) {
            cpu_pause();  
        }
    }

    SMP_PRINT("smp init succeed\n");

    task_run();

    SMP_PANIC("system error\n");

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
    // vector是uint8_t，不会超过255，不需要检查
    if (vector < 32) { 
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
    // vector是uint8_t，不会超过255，不需要检查
    if (vector < 32) { 
        // 无效的中断向量
        return;
    }

    // 恢复默认值
    irq_table[vector] = 0;
}

/**
 * 分配中断向量号
 * 
 * @param handler_addr 中断处理函数地址
 * 
 * @return 向量号
 */
kresult_t smp_irq_alloc_handler(uint64_t handler_addr) {
    if (handler_addr == 0)
        return (kresult_t){.err = -EINVAL, .val = 0};

    for (int vector = 32; vector < 256; vector++) {
        if (irq_table[vector] == 0) {
            irq_table[vector] = handler_addr;
            return (kresult_t){.err = 0, .val = vector};
        }
    }

    return (kresult_t){.err = -ENOSPC, .val = 0};
}

// 获取当前cpu的内核tls
per_cpu *smp_get_kernel_tls(void) {
    return get_gs_base();
}

// 向目标cpu发送中断
void smp_send_irq(uint64_t logical_id, uint8_t vector) {
    uint32_t apicid = smp_get_apicid(logical_id);
    apic_send_ipi(apicid, vector);
}

// 更新架构相关的状态，用于切换上下文前
void smp_arch_update_state(struct thread_struct *thread) {
    tss_ptr[get_logical_id()].rsp0 = (uint64_t)thread->rsp;
}