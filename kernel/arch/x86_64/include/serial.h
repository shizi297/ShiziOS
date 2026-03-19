/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <io.h>
#include <arch_processor.h>
#include <spinlock.h>

#define SERIAL_PORT 0x3F8

#ifdef SERIAL_FILE_INIT
spinlock_t serial_lock = SPIN_LOCK_INIT;
#else
extern spinlock_t serial_lock;
#endif

static inline void __serial_putchar(char c) {
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
    outb(SERIAL_PORT, c);
}

static inline void __serial_puts(const char* str) {
    while (*str) {
        if (*str == '\n') {
            __serial_putchar('\r');
            __serial_putchar('\n');
        } else {
            __serial_putchar(*str);
        }
        str++;
    }
}

static inline void init_serial(void) {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);

    spinlock_init(&serial_lock);
}

static inline void serial_putchar(char c) {
    spin_lock(&serial_lock);
    __serial_putchar(c);
    spin_unlock(&serial_lock);
}

static inline void serial_puts(const char* str) {
    spin_lock(&serial_lock);
    __serial_puts(str);
    spin_unlock(&serial_lock);
}

static inline void serial_put_hex(uint64_t value) {
    spin_lock(&serial_lock);
    const char* digits = "0123456789ABCDEF";
    __serial_puts("0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        __serial_putchar(digits[nibble]);
    }
    spin_unlock(&serial_lock);
}

static inline void serial_put_dec(uint64_t value) {
    spin_lock(&serial_lock);
    char buffer[32];
    char* p = buffer + 31;
    *p = '\0';
    
    if (value == 0) {
        __serial_putchar('0');
        spin_unlock(&serial_lock);
        return;
    }
    
    while (value > 0) {
        *--p = '0' + (value % 10);
        value /= 10;
    }
    __serial_puts(p);
    spin_unlock(&serial_lock);
}

__attribute__((noreturn))
static inline void panic(const char* msg) {
    // 关闭中断，避免打断
    irq_off();

    __serial_puts(msg);
    
    // 死循环
    while (1) {
        cpu_halt();
    }
}