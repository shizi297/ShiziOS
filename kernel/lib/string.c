/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <shizi/string.h>

bool strcmp(const char *a, const char *b) {
    if (!a || !b) return false; // 字符串指针为NULL，直接返回不相等

    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return (*a == *b);
}

bool memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;

    // 将pa地址对齐到 8 字节
    while (n && ((uintptr_t)pa & 7)) {
        if (*pa++ != *pb++) return false;
        n--;
    }

    // 比较后面的64位块
    const uint64_t *pa64 = (const uint64_t *)pa;
    const uint64_t *pb64 = (const uint64_t *)pb;
    while (n >= 8) {
        if (*pa64++ != *pb64++) return false;
        n -= 8;
    }

    // 尾部剩余字节
    pa = (const uint8_t *)pa64;
    pb = (const uint8_t *)pb64;
    while (n--) {
        if (*pa++ != *pb++) return false;
    }

    return true;
}

void memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    // 将dest对齐到 8 字节
    while (n && ((uintptr_t)d & 7)) {
        *d++ = *s++;
        n--;
    }

    // 批量复制64位块
    uint64_t *d64 = (uint64_t *)d;
    const uint64_t *s64 = (const uint64_t *)s;
    while (n >= 8) {
        *d64++ = *s64++;
        n -= 8;
    }

    // 尾部剩余字节
    d = (uint8_t *)d64;
    s = (const uint8_t *)s64;
    while (n--) {
        *d++ = *s++;
    }
}

void memset(void *dest, uint8_t c, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    uint8_t value = (uint8_t)c;

    // 将dest对齐到8字节
    while (n && ((uintptr_t)d & 7)) {
        *d++ = value;
        n--;
    }

    // 8 个字节均设为 value
    uint64_t val = value;
    val |= val << 8;
    val |= val << 16;
    val |= val << 32; // 64 位下覆盖所有字节

    // 批量填充64位块
    uint64_t *d64 = (uint64_t *)d;
    while (n >= 8) {
        *d64++ = val;
        n -= 8;
    }

    // 尾部剩余字节
    d = (uint8_t *)d64;
    while (n--) {
        *d++ = value;
    }
}