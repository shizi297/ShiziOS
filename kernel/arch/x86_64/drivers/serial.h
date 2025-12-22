/* SPDX-License-Identifier: Apache-2.0 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <io.h>

#define SERIAL_PORT 0x3F8

void init_serial(void);
void serial_putchar(char c);
void serial_puts(const char* str);
void serial_put_hex(uint64_t value);
void serial_put_dec(uint64_t value);
void panic(const char* msg);

#endif