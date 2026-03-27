/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <io.h>
#include <arch_processor.h>
#include <spinlock.h>
#include <stdarg.h>

#define SERIAL_PORT 0x3F8

#ifdef SERIAL_FILE_INIT
spinlock_t serial_lock = SPIN_LOCK_INIT;
#else
extern spinlock_t serial_lock;
#endif

static inline void serial_putchar_nolock(char c) {
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
    outb(SERIAL_PORT, c);
}

static inline void serial_puts_nolock(const char* str) {
    while (*str) {
        if (*str == '\n') {
            serial_putchar_nolock('\r');
            serial_putchar_nolock('\n');
        } else {
            serial_putchar_nolock(*str);
        }
        str++;
    }
}

static inline void serial_put_hex_nolock(uint64_t value) {
    const char* digits = "0123456789ABCDEF";
    serial_puts_nolock("0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        serial_putchar_nolock(digits[nibble]);
    }
}

static inline void serial_put_dec_nolock(uint64_t value) {
    char buffer[32];
    char* p = buffer + 31;
    *p = '\0';
    
    if (value == 0) {
        serial_putchar_nolock('0');
        return;
    }
    
    while (value > 0) {
        *--p = '0' + (value % 10);
        value /= 10;
    }
    serial_puts_nolock(p);
}

static inline void serial_put_dec_signed_nolock(int64_t value) {
    if (value < 0) {
        serial_putchar_nolock('-');
        value = -value;
    }
    serial_put_dec_nolock((uint64_t)value);
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
    uint64_t flags;
    spin_lock_irqsave(&serial_lock, &flags);
    serial_putchar_nolock(c);
    spin_unlock_irqrestore(&serial_lock, flags);
}

static inline void serial_puts(const char* str) {
    uint64_t flags;
    spin_lock_irqsave(&serial_lock, &flags);
    serial_puts_nolock(str);
    spin_unlock_irqrestore(&serial_lock, flags);
}

static inline void serial_put_hex(uint64_t value) {
    uint64_t flags;
    spin_lock_irqsave(&serial_lock, &flags);
    serial_put_hex_nolock(value);
    spin_unlock_irqrestore(&serial_lock, flags);
}

static inline void serial_put_dec(uint64_t value) {
    uint64_t flags;
    spin_lock_irqsave(&serial_lock, &flags);
    serial_put_dec_nolock(value);
    spin_unlock_irqrestore(&serial_lock, flags);
}

__attribute__((noreturn))
static inline void panic(const char* msg) {
    // 关闭中断，避免打断
    irq_off();

    serial_puts_nolock(msg);
    
    // 死循环
    while (1) {
        cpu_halt();
    }
}

static inline void vprintk(const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int long_mod = 0;      // 0=int, 1=long, 2=long long
            int alt = 0;           // 是否加0x前缀
            
            // 解析标志
            while (*fmt == '#') {
                alt = 1;
                fmt++;
            }
            // 解析长度修饰符
            while (*fmt == 'l') {
                long_mod++;
                fmt++;
            }
            
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int64_t val;
                    if (long_mod == 1)
                        val = va_arg(args, long);
                    else if (long_mod == 2)
                        val = va_arg(args, long long);
                    else
                        val = va_arg(args, int);
                    serial_put_dec_signed_nolock(val);
                    break;
                }
                case 'u': {
                    uint64_t val;
                    if (long_mod == 1)
                        val = va_arg(args, unsigned long);
                    else if (long_mod == 2)
                        val = va_arg(args, unsigned long long);
                    else
                        val = va_arg(args, unsigned int);
                    serial_put_dec_nolock(val);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t val;
                    if (long_mod == 1)
                        val = va_arg(args, unsigned long);
                    else if (long_mod == 2)
                        val = va_arg(args, unsigned long long);
                    else
                        val = va_arg(args, unsigned int);
                    if (alt)
                        serial_puts_nolock("0x");
                    serial_put_hex_nolock(val);
                    break;
                }
                case 'p': {
                    uint64_t val = (uint64_t)va_arg(args, void*);
                    serial_puts_nolock("0x");
                    serial_put_hex_nolock(val);
                    break;
                }
                case 'c':
                    serial_putchar_nolock((char)va_arg(args, int));
                    break;
                case 's':
                    serial_puts_nolock(va_arg(args, char *));
                    break;
                default:
                    serial_putchar_nolock('%');
                    serial_putchar_nolock(*fmt);
                    break;
            }
        } else {
            serial_putchar_nolock(*fmt);
        }
        fmt++;
    }
}

static inline void printk_nolock(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

static inline void printk(const char *fmt, ...) {
    uint64_t flags;
    spin_lock_irqsave(&serial_lock, &flags);
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    spin_unlock_irqrestore(&serial_lock, flags);
}

__attribute__((noreturn))
static inline void printp(const char *fmt, ...) {
    irq_off();                          
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);                
    va_end(args);
    while (1) cpu_halt();               
}