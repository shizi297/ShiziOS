/* SPDX-License-Identifier: Apache-2.0 */

#include "processor.h"
#include "fault.h"

extern uint64_t irq_entry_table[256];

static uint64_t gdt_temp[GDT_ENTRY_COUNT];
static struct idt_gate idt_temp[IDT_ENTRY_COUNT];
static struct tss tss_temp = {0};

/*
 * GDT模版初始化
 * 后续被复制到per_cpu结构体中
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
 * 后续被复制到per_cpu结构体中
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

// 初始化所有模版
void processor_init(void) {
    gdt_temp_init();
    idt_temp_init();
    tss_temp_init();
}