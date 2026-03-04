/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

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
bool strcmp(const char *a, const char *b);

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
bool memcmp(const void *a, const void *b, size_t n);

/**
 * 内存复制
 *
 * @param dest 目标内存起始地址
 * @param src  源内存起始地址
 * @param n    要拷贝的字节数
 */
void memcpy(void *dest, const void *src, size_t n);

/**
 * 内存填充
 *
 * @param dest 目标内存起始地址
 * @param c    填充值
 * @param n    要填充的字节数
 */
void memset(void *dest, uint8_t c, size_t n);