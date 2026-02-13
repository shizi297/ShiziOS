/* SPDX-License-Identifier: Apache-2.0 */
/* SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297> */

#ifndef SHIZI_STRING_H
#define SHIZI_STRING_H

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

#endif // SHIZI_STRING_H