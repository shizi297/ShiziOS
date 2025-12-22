/* SPDX-License-Identifier: Apache-2.0 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

#define IO_WAIT_PORT 0x80

void io_barrier(void);
void io_wait(void);
void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);
void insb(uint16_t port, void* buffer, uint32_t count);
void insw(uint16_t port, void* buffer, uint32_t count);
void insl(uint16_t port, void* buffer, uint32_t count);
void outsb(uint16_t port, const void* buffer, uint32_t count);
void outsw(uint16_t port, const void* buffer, uint32_t count);
void outsl(uint16_t port, const void* buffer, uint32_t count);
void rdmsr(uint32_t msr, uint32_t* lo, uint32_t* hi);
void wrmsr(uint32_t msr, uint32_t lo, uint32_t hi);
uint64_t rdtsc(void);
uint64_t rdtscp(void);

#endif