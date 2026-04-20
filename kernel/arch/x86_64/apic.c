/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <msr.h>
#include <processor.h>
#include <time.h>
#include <bootboot.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <heap.h>

// 广播IPI的目标范围
typedef enum {
    NO_ONESELF = 0x2,  // 所有核心，包括自身 
    ONESELF = 0x3,  // 所有核心，排除自身 
} apic_scope;

// 为当前 CPU 启用 x2APIC 模式
static void set_apic_x2apic(void) {
    uint64_t msr_val = msr_read(MSR_IA32_APIC_BASE);

    // 已经处于 x2APIC 模式，无需操作
    if ((msr_val & (APIC_BASE_MSR_X2APIC | APIC_BASE_MSR_ENABLE))
         == (APIC_BASE_MSR_X2APIC | APIC_BASE_MSR_ENABLE))
        return;

    msr_val |= APIC_BASE_MSR_X2APIC | APIC_BASE_MSR_ENABLE;
    msr_write(MSR_IA32_APIC_BASE, msr_val);
}

/**
 * 配置本地向量表 (LVT) 定时器条目
 *
 * @param vector 中断向量号
 * @param mode 触发模式 
 * @param mask 屏蔽位 (1=屏蔽，0=启用)
 */
static void apic_set_lvt_timer(uint32_t vector, uint32_t mode, uint32_t mask) {
    uint64_t val = ((uint64_t)vector & 0xFF) | (((uint64_t)mode & 0x7) << 17) | (((uint64_t)mask & 0x1) << 16);
    msr_write(X2APIC_MSR_LVT_TIMER, val);
}

// TSC deadline 模式相关
static void apic_tsc_deadline_set_value(uint64_t value) {
    uint64_t now = rdtsc();
    msr_write(MSR_IA32_TSC_DEADLINE, now + value);
}

static void apic_tsc_deadline_set_oneshot(void) {
    apic_set_lvt_timer(IRQ_APIC, 2, 0);
}

static void apic_tsc_deadline_shutdown(void) {
    apic_set_lvt_timer(IRQ_APIC, 2, 1);
}

// 传统 APIC 定时器模式
#define APIC_BUS_FREQ 100000000  // 总线频率,100MHZ

static void apic_legacy_set_value(uint64_t cycles) {
    msr_write(X2APIC_MSR_TIMER_INITCNT, cycles);
}

static void apic_legacy_set_oneshot(void) {
    apic_set_lvt_timer(IRQ_APIC, 0, 0);
}

static void apic_legacy_set_periodic(void) {
    apic_set_lvt_timer(IRQ_APIC, 1, 0);
}

static void apic_legacy_shutdown(void) {
    apic_set_lvt_timer(IRQ_APIC, 0, 1);
}

/**
 * 设置svr寄存器
 *
 * @param value 伪中断向量号
 */
static void apic_set_svr(uint8_t value) {
    uint64_t svr =  msr_read(X2APIC_MSR_SVR);
    svr = (svr & ~0xFF) | ((1 << 8)) | value;
    msr_write(X2APIC_MSR_SVR, svr);
}

// 读取错误状态寄存器
static uint32_t apic_read_esr(void) {
    msr_write(X2APIC_MSR_ESR, 0);
    return (uint32_t)msr_read(X2APIC_MSR_ESR);
}

/**
 * 设置任务优先级 (TPR)
 *
 * @param priority 要设置的优先级值
 */
static void apic_set_tpr(uint8_t priority) {
    msr_write(X2APIC_MSR_TPR, priority);
}

/**
 * 单播IPI发送接口
 * 处理单播IPI请求的队列管理和发送
 *
 * @param apic_id 目标CPU的apic_id
 * @param vector 中断向量号
 */
void apic_send_ipi(uint32_t apic_id, uint32_t vector) {
    uint64_t icr_val = ((uint64_t)apic_id << 32) | vector;
    msr_write(X2APIC_MSR_ICR, icr_val);
}

/**
 * 广播IPI发送接口
 * 向所有CPU核心（除自身）发送IPI
 *
 * @param vector 中断向量号
 */
void apic_send_ipi_all(uint32_t vector) {
    uint64_t icr_val = ((uint64_t)ONESELF << 18) | (vector & 0xFF);
    msr_write(X2APIC_MSR_ICR, icr_val);
}

// 用于通知apic当前中断已完成，需要在所有中断处理后面添加
void apic_eoi(void) {
    msr_write(X2APIC_MSR_EOI, 0);
}

// 获取当前CPU的APIC ID
uint32_t apic_get_id(void) {
    return (uint32_t)msr_read(X2APIC_MSR_APIC_ID);
}

// 早期初始化，用于启动x2apic模式，让系统可以使用一些东西
void apic_boot_init(void) {
    // 设置为x2apic模式
    set_apic_x2apic();

    // 禁用PIC
    outb(0xFF, 0xA1); 
    outb(0xFF, 0x21); 
}

// per-cpu 句柄数组
static clockevent_handle_t *apic_ce_percpu = NULL;

// 定时器中断处理函数
static void apic_timer_irq(struct pt_regs *regs) {
    uint32_t logical_id = get_logical_id();
    if (apic_ce_percpu && apic_ce_percpu[logical_id]) {
        clockevent_handle_irq(apic_ce_percpu[logical_id]);
    }
}

// 将当前cpu的apic定时器注册到时钟事件框架
static bool apic_clockevent_register_legacy(void) {
    clockevent_register(
        "apic",
        apic_legacy_shutdown,
        apic_legacy_set_oneshot,
        apic_legacy_set_periodic,
        apic_legacy_set_value,
        APIC_BUS_FREQ
    );
    return true;
}

// 初始化apic
bool apic_init(void) {
    // 注册当前cpu的apic定时器到时钟事件框架
    if(!apic_clockevent_register_legacy()) {
        return false;
    }

    const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

    // 如果是bp，分配 per_cpu 句柄数组
    if (apic_get_id() == bootboot->bspid) {
        uint16_t max_cpu = bootboot->numcores;
        apic_ce_percpu = kheap_alloc(sizeof(clockevent_handle_t) * max_cpu);
        if (!apic_ce_percpu) return false;
        for (int i = 0; i < max_cpu; i++) {
            apic_ce_percpu[i] = NULL;
        }
    }

    // 获取当前cpu的句柄并保存
    uint32_t logical_id = get_logical_id();
    apic_ce_percpu[logical_id] = clockevent_get("apic");

    // 如果是bp，注册中断处理函数
    if (apic_get_id() == bootboot->bspid) {
        smp_irq_register_handler(IRQ_APIC, (uint64_t)apic_timer_irq);
    }
    
    // 设置分频为 1（不分频）
    msr_write(X2APIC_MSR_TIMER_DIV, 0xB);  

    return true;
}