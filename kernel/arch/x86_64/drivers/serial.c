/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <io.h>

#define SERIAL_PORT 0x3F8

void init_serial(void) {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}

void serial_putchar(char c) {
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);
    outb(SERIAL_PORT, c);
}

void serial_puts(const char* str) {
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
            serial_putchar('\n');
        } else {
            serial_putchar(*str);
        }
        str++;
    }
}

void serial_put_hex(uint64_t value) {
    const char* digits = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        serial_putchar(digits[nibble]);
    }
}

void serial_put_dec(uint64_t value) {
    char buffer[32];
    char* p = buffer + 31;
    *p = '\0';
    
    if (value == 0) {
        serial_putchar('0');
        return;
    }
    
    while (value > 0) {
        *--p = '0' + (value % 10);
        value /= 10;
    }
    serial_puts(p);
}

void panic(const char* msg) {
    // 关闭中断，避免打断
    __asm__ __volatile__("cli");

    serial_puts(msg);
    
    // 死循环
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
