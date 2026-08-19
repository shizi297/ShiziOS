/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <klibc.h>
#include <asm/mm_addr.h>
#include <asm/extable.h>

/*
 * 生成从用户空间读取一个整数值的内联函数
 *
 * @param n 位宽编号
 * @param type 数据类型
 */
#define UACCESS_GET_USER_RAW_FUNC(n, type) \
static inline int uaccess_get_user_raw_##n(void *out, const void __uptr *ptr) \
{ \
    int ret = 0; \
    type val; \
    type *__out = (type *)out; \
    __asm__ __volatile__( \
        "1: mov (%2), %1\n" \
        "2:\n" \
        EXTABLE_ADD(1b, 3f) \
        ".pushsection .fixup,\"ax\"\n" \
        "3: mov %3, %0\n" \
        "   xor %1, %1\n" \
        "   jmp 2b\n" \
        ".popsection\n" \
        : "=r"(ret), "=r"(val) \
        : "r"(ptr), "i"(-EFAULT) \
        : "memory" \
    ); \
    *__out = val; \
    return ret; \
}

/*
 * 生成向用户空间写入一个整数值的内联函数
 *
 * @param n 位宽编号
 * @param type 数据类型
 */
#define UACCESS_PUT_USER_RAW_FUNC(n, type) \
static inline int uaccess_put_user_raw_##n(void __uptr *ptr, type val) \
{ \
    int ret = 0; \
    type tmp = val; \
    __asm__ __volatile__( \
        "1: mov %1, (%2)\n" \
        "2:\n" \
        EXTABLE_ADD(1b, 3f) \
        ".pushsection .fixup,\"ax\"\n" \
        "3: mov %3, %0\n" \
        "   jmp 2b\n" \
        ".popsection\n" \
        : "=r"(ret) \
        : "r"(tmp), "r"(ptr), "i"(-EFAULT) \
        : "memory" \
    ); \
    return ret; \
}

/*
 * 从用户空间复制数据到内核空间
 *
 * @param to 内核空间目标地址
 * @param from 用户空间源地址
 * @param n 复制字节数
 */
__ktype(int)
#define UACCESS_COPY_FROM_USER(to, from, n) ({ \
    int __ret = 0; \
    __asm__ __volatile__( \
        "1: rep movsb\n" \
        "2:\n" \
        EXTABLE_ADD(1b, 3f) \
        ".pushsection .fixup,\"ax\"\n" \
        "3: mov %4, %0\n" \
        "   jmp 2b\n" \
        ".popsection\n" \
        : "=r"(__ret) \
        : "D"(to), "S"(from), "c"(n), "i"(-EFAULT) \
        : "memory" \
    ); \
    __ret; \
})

/*
 * 从内核空间复制数据到用户空间
 *
 * @param to 用户空间目标地址
 * @param from 内核空间源地址
 * @param n 复制字节数
 */
__ktype(int)
#define UACCESS_COPY_TO_USER(to, from, n) ({ \
    int __ret = 0; \
    __asm__ __volatile__( \
        "1: rep movsb\n" \
        "2:\n" \
        EXTABLE_ADD(1b, 3f) \
        ".pushsection .fixup,\"ax\"\n" \
        "3: mov %4, %0\n" \
        "   jmp 2b\n" \
        ".popsection\n" \
        : "=r"(__ret) \
        : "D"(to), "S"(from), "c"(n), "i"(-EFAULT) \
        : "memory" \
    ); \
    __ret; \
})