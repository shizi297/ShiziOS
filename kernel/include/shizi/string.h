/* SPDX-License-Identifier: Apache-2.0 */
/* SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297> */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>   

/**
 * 判断两个字符串是否相等
 *
 * @param a 第1个字符串
 * @param b 第2个字符串
 *
 * @return 相等：true
 * @return 不相等：false
 */
static inline bool strcmp(const char *a, const char *b) {
    if (!a || !b) return false; // 字符串指针为NULL，直接返回不相等

    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return (*a == *b);
}

/**
 * 比较两块内存是否相等
 *
 * @param a 内存块1
 * @param b 内存块2
 * @param n 要比较的字节数
 *
 * @return 相等：true
 * @return 不相等：false
 */
static inline bool memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (uint8_t *)a, *pb = (uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return false;
    }
    return true;
}