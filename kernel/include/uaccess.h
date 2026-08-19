/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <klibc.h>
#include <asm/mm_addr.h>
#include <asm/uaccess.h>

/*
 * 验证用户空间地址是否可访问
 *
 * @param addr 用户空间地址
 * @param size 要访问的大小
 */
#define uaccess_uaddr_ok(addr, size) \
    ((uintptr_t)(addr) <= USER_END && \
     (uintptr_t)(size) <= USER_END + 1 - (uintptr_t)(addr))

/*
 * 从用户空间读取一个整数值
 *
 * @param n 位宽
 * @param ... 参数
 */
#define UACCESS_GET_USER_FUNC(n, ...) uaccess_get_user_raw_##n(__VA_ARGS__)

/*
 * 向用户空间写入一个整数值
 *
 * @param n 位宽
 * @param ... 参数
 */
#define UACCESS_PUT_USER_FUNC(n, ...) uaccess_put_user_raw_##n(__VA_ARGS__)

/*
 * 从用户空间读取一个整数值
 *
 * @param x 目标变量
 * @param ptr 用户空间指针
 */
#define uaccess_get_user(x, ptr) \
    _Generic((x), \
        signed char:           UACCESS_GET_USER_FUNC(8, &(x), (ptr)), \
        unsigned char:         UACCESS_GET_USER_FUNC(8, &(x), (ptr)), \
        char:                  UACCESS_GET_USER_FUNC(8, &(x), (ptr)), \
        short:                 UACCESS_GET_USER_FUNC(16, &(x), (ptr)), \
        unsigned short:        UACCESS_GET_USER_FUNC(16, &(x), (ptr)), \
        int:                   UACCESS_GET_USER_FUNC(32, &(x), (ptr)), \
        unsigned int:          UACCESS_GET_USER_FUNC(32, &(x), (ptr)), \
        long:                  UACCESS_GET_USER_FUNC(64, &(x), (ptr)), \
        unsigned long:         UACCESS_GET_USER_FUNC(64, &(x), (ptr)), \
        long long:             UACCESS_GET_USER_FUNC(64, &(x), (ptr)), \
        unsigned long long:    UACCESS_GET_USER_FUNC(64, &(x), (ptr)) \
    )

/*
 * 向用户空间写入一个整数值
 *
 * @param x 要写入的值
 * @param ptr 用户空间指针
 */
#define uaccess_put_user(x, ptr) \
    _Generic((x), \
        signed char:           UACCESS_PUT_USER_FUNC(8, (ptr), (uint8_t)(x)), \
        unsigned char:         UACCESS_PUT_USER_FUNC(8, (ptr), (uint8_t)(x)), \
        char:                  UACCESS_PUT_USER_FUNC(8, (ptr), (uint8_t)(x)), \
        short:                 UACCESS_PUT_USER_FUNC(16, (ptr), (uint16_t)(x)), \
        unsigned short:        UACCESS_PUT_USER_FUNC(16, (ptr), (uint16_t)(x)), \
        int:                   UACCESS_PUT_USER_FUNC(32, (ptr), (uint32_t)(x)), \
        unsigned int:          UACCESS_PUT_USER_FUNC(32, (ptr), (uint32_t)(x)), \
        long:                  UACCESS_PUT_USER_FUNC(64, (ptr), (uint64_t)(x)), \
        unsigned long:         UACCESS_PUT_USER_FUNC(64, (ptr), (uint64_t)(x)), \
        long long:             UACCESS_PUT_USER_FUNC(64, (ptr), (uint64_t)(x)), \
        unsigned long long:    UACCESS_PUT_USER_FUNC(64, (ptr), (uint64_t)(x)) \
    )

// 实例化各宽度 RAW 函数
UACCESS_GET_USER_RAW_FUNC(8, uint8_t)
UACCESS_GET_USER_RAW_FUNC(16, uint16_t)
UACCESS_GET_USER_RAW_FUNC(32, uint32_t)
UACCESS_GET_USER_RAW_FUNC(64, uint64_t)

UACCESS_PUT_USER_RAW_FUNC(8, uint8_t)
UACCESS_PUT_USER_RAW_FUNC(16, uint16_t)
UACCESS_PUT_USER_RAW_FUNC(32, uint32_t)
UACCESS_PUT_USER_RAW_FUNC(64, uint64_t)

/*
 * 从用户空间复制数据到内核空间
 *
 * @param to 内核空间目标地址
 * @param from 用户空间源地址
 * @param n 复制字节数
 */
int uaccess_copy_from_user(void *to, const void __uptr *from, size_t n);

/*
 * 从内核空间复制数据到用户空间
 *
 * @param to 用户空间目标地址
 * @param from 内核空间源地址
 * @param n 复制字节数
 */
int uaccess_copy_to_user(void __uptr *to, const void *from, size_t n);

/*
 * 从用户空间复制字符串到内核空间
 *
 * @param dst 内核空间目标地址
 * @param src 用户空间源地址
 * @param max 最大复制字节数
 *
 * @return 字符串长度（不含 '\0'）
 */
int uaccess_strncpy_from_user(char *dst, const char __uptr *src, size_t max);

/*
 * 获取用户空间字符串长度
 *
 * @param s 用户空间字符串地址
 * @param max 最大检查长度
 *
 * @return 字符串长度（不含 '\0'）
 */
int uaccess_strnlen_user(const char __uptr *s, size_t max);