/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>

// 提取128位值的高64位和低64位
#define HI(x) ((uint64_t)((x) >> 64))
#define LO(x) ((uint64_t)(x))

// 除零陷阱
#define DIV0_TRAP() __builtin_trap()

/**
 * 无符号128位除法
 *
 * @param a 被除数
 * @param b 除数
 *
 * @return 商
 */
__uint128_t __udivti3(__uint128_t a, __uint128_t b) {
    uint64_t b_hi = HI(b);
    uint64_t b_lo = LO(b);
    if (b_hi != 0) __builtin_trap();
    if (b_lo == 0) DIV0_TRAP();

    uint64_t a_hi = HI(a);
    uint64_t a_lo = LO(a);
    uint64_t q, r;

    __asm__ volatile (
        "divq %4"
        : "=a"(q), "=d"(r)
        : "a"(a_lo), "d"(a_hi), "rm"(b_lo)
        : "cc"
    );

    return (__uint128_t)q;
}

/**
 * 无符号128位取模
 *
 * @param a 被除数
 * @param b 除数
 *
 * @return 余数
 */
__uint128_t __umodti3(__uint128_t a, __uint128_t b) {
    uint64_t b_hi = HI(b);
    uint64_t b_lo = LO(b);

    if (b_hi != 0)
        __builtin_trap();
    if (b_lo == 0)
        DIV0_TRAP();

    uint64_t a_hi = HI(a);
    uint64_t a_lo = LO(a);
    uint64_t r;

    __asm__ volatile (
        "divq %3"
        : "=a"(a_lo), "=d"(r)
        : "a"(a_lo), "d"(a_hi), "rm"(b_lo)
        : "cc"
    );

    return (__uint128_t)r;
}

/**
 * 有符号128位除法
 *
 * @param a 被除数
 * @param b 除数
 *
 * @return 商（向零取整）
 */
__int128_t __divti3(__int128_t a, __int128_t b) {
    int sign = 1;
    __uint128_t ua = (__uint128_t)a;
    __uint128_t ub = (__uint128_t)b;

    if (a < 0) {
        ua = -ua;
        sign = -sign;
    }
    if (b < 0) {
        ub = -ub;
        sign = -sign;
    }

    __uint128_t uq = __udivti3(ua, ub);
    __int128_t q = (__int128_t)uq;
    return (sign < 0) ? -q : q;
}

/**
 * 有符号128位取模
 *
 * @param a 被除数
 * @param b 除数
 *
 * @return 余数（符号与被除数相同）
 */
__int128_t __modti3(__int128_t a, __int128_t b) {
    int sign = 1;
    __uint128_t ua = (__uint128_t)a;
    __uint128_t ub = (__uint128_t)b;

    if (a < 0) {
        ua = -ua;
        sign = -sign;
    }
    if (b < 0) {
        ub = -ub;
    }

    __uint128_t ur = __umodti3(ua, ub);
    __int128_t r = (__int128_t)ur;
    return (sign < 0) ? -r : r;
}