/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

#define IO_WAIT_PORT 0x80

static inline void io_barrier(void) {
    asm volatile ("" : : : "memory");
}

static inline void io_wait(void) {
    asm volatile ("outb %%al, $0x80" : : "a"(0));
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
    io_barrier();
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    io_barrier();
    return ret;
}

static inline void outw(uint16_t port, uint16_t value) {
    asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
    io_barrier();
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    io_barrier();
    return ret;
}

static inline void outl(uint16_t port, uint32_t value) {
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
    io_barrier();
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    io_barrier();
    return ret;
}

static inline void insb(uint16_t port, void* buffer, uint32_t count) {
    asm volatile ("cld; rep insb" : "+D"(buffer), "+c"(count) : "d"(port) : "memory");
    io_barrier();
}

static inline void insw(uint16_t port, void* buffer, uint32_t count) {
    asm volatile ("cld; rep insw" : "+D"(buffer), "+c"(count) : "d"(port) : "memory");
    io_barrier();
}

static inline void insl(uint16_t port, void* buffer, uint32_t count) {
    asm volatile ("cld; rep insl" : "+D"(buffer), "+c"(count) : "d"(port) : "memory");
    io_barrier();
}

static inline void outsb(uint16_t port, const void* buffer, uint32_t count) {
    asm volatile ("cld; rep outsb" : "+S"(buffer), "+c"(count) : "d"(port) : "memory");
    io_barrier();
}

static inline void outsw(uint16_t port, const void* buffer, uint32_t count) {
    asm volatile ("cld; rep outsw" : "+S"(buffer), "+c"(count) : "d"(port) : "memory");
    io_barrier();
}

static inline void outsl(uint16_t port, const void* buffer, uint32_t count) {
    asm volatile ("cld; rep outsl" : "+S"(buffer), "+c"(count) : "d"(port) : "memory");
    io_barrier();
}

static inline void rdmsr(uint32_t msr, uint32_t* lo, uint32_t* hi) {
    asm volatile ("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
    io_barrier();
}

static inline void wrmsr(uint32_t msr, uint32_t lo, uint32_t hi) {
    asm volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
    io_barrier();
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp(void) {
    uint32_t lo, hi;
    asm volatile ("rdtscp" : "=a"(lo), "=d"(hi) : : "rcx");
    return ((uint64_t)hi << 32) | lo;
}

#endif
