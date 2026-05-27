/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <asm/io.h>
#include <spinlock.h>

#define SERIAL_PORT 0x3F8

#ifdef SERIAL_FILE_INIT
spinlock_t serial_lock = SPIN_LOCK_INIT;
#else
extern spinlock_t serial_lock;
#endif

static inline void serial_putchar_nolock(char c) {
    if (c == '\n')
        serial_putchar_nolock('\r');
        
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
    outb(SERIAL_PORT, c);
}

static inline void serial_puts_nolock(const char *str) {
    while (*str)
        serial_putchar_nolock(*str++);
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