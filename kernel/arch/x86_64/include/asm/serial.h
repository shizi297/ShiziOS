/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stddef.h>
#include <asm/io.h>
#include <spinlock.h>
#include <kio.h>

#define SERIAL_PORT 0x3F8

#define LSR_THR_EMPTY 0x20

#define serial_smp_init() \
    do { \
        static spinlock_t lock = SPIN_LOCK_INIT; \
        static bool done = false; \
        spin_lock(&lock); \
        if (!done) { \
            serial_init(); \
            done = true; \
        } \
        spin_unlock(&lock); \
    } while (0)

#ifdef SERIAL_FILE_INIT
spinlock_t serial_lock = SPIN_LOCK_INIT;
#else
extern spinlock_t serial_lock;
#endif

static inline void serial_putchar(char c) {
    while ((inb(SERIAL_PORT + 5) & LSR_THR_EMPTY) == 0)
        cpu_pause();
    outb(SERIAL_PORT, c);
}

static inline void serial_backend(PFILE *stream) {
    size_t count = stream->count;
    const char *buf = stream->buffer;
    uint64_t irqflags;

    spin_lock_irqsave(&serial_lock, &irqflags);

    for (size_t i = 0; i < count; i++) {
        char c = buf[i];
        if (c == '\n') {
            serial_putchar('\r');
            serial_putchar('\n');
        } else {
            serial_putchar(c);
        }
    }

    spin_unlock_irqrestore(&serial_lock, irqflags);
}

static inline void serial_panic_backend(PFILE *stream) {
    size_t count = stream->count;
    const char *buf = stream->buffer;

    for (size_t i = 0; i < count; i++) {
        char c = buf[i];
        if (c == '\n') {
            serial_putchar('\r');
            serial_putchar('\n');
        } else {
            serial_putchar(c);
        }
    }
}

static inline void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);

    kio_register_backend(serial_backend, serial_panic_backend);
}