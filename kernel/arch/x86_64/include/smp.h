/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <processor.h>

struct sched_class;
typedef struct task_struct task_struct;

typedef struct {
    uint64_t (*timestamp)(void);    // 时间戳获取
    task_struct *current;

    // 当前cpuid
    uint16_t logical_id;

    // 调度器头节点
    void *sched;

} __attribute__((aligned(64))) per_cpu;

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
);

// 获取cpu核心的逻辑id
uint32_t get_logical_id(void);

// 设置调度器头节点
void smp_set_sched(void *sched);

// 获取当前cpu的调度器头节点
void *smp_get_sched(void);

/*
 * 初始化所有核心
 *
 * @param logical_id 当前cpu的逻辑cpuid
 * @param apic_id 当前cpu的apicid
 */
void smp_init(uint32_t logical_id, uint32_t apic_id);

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
bool smp_irq_register_handler(uint8_t vector, uint64_t handler_addr);

/**
 * 注销中断处理函数
 * 
 * @param vector 中断向量号
 */
void smp_irq_unregister_handler(uint8_t vector);