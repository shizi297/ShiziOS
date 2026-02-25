/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <stdbool.h>
#include <io.h>
#include <time.h>           
#include <processor.h>    
#include <smp.h>   
#include <ioapic.h>

#define PIT_COUNTER0        0x40
#define PIT_CONTROL         0x43

#define PIT_SEL0            0x00
#define PIT_RW_LSB_MSB      0x30
#define PIT_MODE0           0x00        // 模式0，单次
#define PIT_MODE2           0x04        // 模式2，周期中断（rate generator）
#define PIT_BINARY          0x00

#define PIT_CTRL_ONESHOT    (PIT_SEL0 | PIT_RW_LSB_MSB | PIT_MODE0 | PIT_BINARY)  
#define PIT_CTRL_PERIODIC   (PIT_SEL0 | PIT_RW_LSB_MSB | PIT_MODE2 | PIT_BINARY)  

#define PIT_GSI 2

// 停止时钟事件
static void pit_shutdown(void) {
    // 屏蔽 IRQ_PIT 中断
    ioapic_mask_gsi(PIT_GSI);
}

// 设置为单次模式
static void pit_set_oneshot(void) {
    outb(PIT_CTRL_ONESHOT, PIT_CONTROL);

    // 使能 IRQ_PIT 中断
    ioapic_unmask_gsi(PIT_GSI);
}

// 设置为周期模式
static void pit_set_periodic(void) {
    outb(PIT_CTRL_PERIODIC, PIT_CONTROL);
    
    // 使能 IRQ_PIT 中断
    ioapic_unmask_gsi(PIT_GSI);
}

/**
 * 设置下一次中断的计数值
 * 
 * @param value 设备值
 */
static void pit_set_value(uint64_t value) {
    uint16_t cnt = (uint16_t)value;
    outb(cnt & 0xFF, PIT_COUNTER0);
    outb((cnt >> 8) & 0xFF, PIT_COUNTER0);
}

// 中断处理
static void pit_irq(struct pt_regs *regs) {
    void (*handler)(void);

    // 获取 PIT 设备自己的事件处理函数
    get_event_handler("pit", &handler);
    if (handler) {
        handler();
    }
}

// 读取当前计数值
static uint64_t pit_read(void) {
    uint16_t cnt;

    // 锁存计数器0
    outb(0x00, PIT_CONTROL);
    cnt = inb(PIT_COUNTER0);
    cnt |= (inb(PIT_COUNTER0) << 8);

    // 转换为递增
    return 65536 - cnt;
}

/**
 * PIT初始化
 * 
 * @return 成功：true
 * @return 失败：false
 */
bool pit_init(void) {
    // 确保 PIT 归零
    outb(PIT_CTRL_ONESHOT, PIT_CONTROL);   // 设置为模式0
    outb(0x01, PIT_COUNTER0);               // 写入 1
    outb(0x00, PIT_COUNTER0);

    // 轮询等待计数归零
    while (1) {
        outb(0x00, PIT_CONTROL);            // 锁存当前值
        uint8_t low = inb(PIT_COUNTER0);
        uint8_t high = inb(PIT_COUNTER0);
        if ((low | high) == 0)
            break;
    }

    // 调用中断控制器注册函数，将 pit_irq 注册到 IRQ_PIT
    smp_irq_register_handler(IRQ_PIT, (uint64_t)pit_irq);

    // 注册时钟事件设备到框架
    clockevent_register(
        "pit",
        pit_shutdown,
        pit_set_oneshot,
        pit_set_periodic,
        pit_set_value,
        1193182                 
    );

    // 注册时钟源设备
    clocksource_register(
        "pit",
        pit_read,
        1193182
    );

    // 禁用pic
    outb(0xFF, 0xA1); 
    outb(0xFF, 0x21); 

    // 注册到ioapic
    bool is_success = ioapic_register_gsi(PIT_GSI, IRQ_PIT, get_apic_id(), IOAPIC_POLARITY);
    if (!is_success) return false;

    return true;
}