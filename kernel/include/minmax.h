/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#ifdef __CHECKER__
    #define min(a, b) ((a) < (b) ? (a) : (b))
    #define max(a, b) ((a) > (b) ? (a) : (b))
    #define min3(a, b, c) min(min((a), (b)), (c))
    #define max3(a, b, c) max(max((a), (b)), (c))
    #define clamp(val, lo, hi) max((lo), min((val), (hi)))
#else
    #define MINMAX_MIN_TYPE(name, type) \
    static inline type min_##name(type a, type b) { \
        return a < b ? a : b; \
    }

    MINMAX_MIN_TYPE(schar, signed char)
    MINMAX_MIN_TYPE(uchar, unsigned char)
    MINMAX_MIN_TYPE(short, short)
    MINMAX_MIN_TYPE(ushort, unsigned short)
    MINMAX_MIN_TYPE(int, int)
    MINMAX_MIN_TYPE(uint, unsigned int)
    MINMAX_MIN_TYPE(long, long)
    MINMAX_MIN_TYPE(ulong, unsigned long)
    MINMAX_MIN_TYPE(llong, long long)
    MINMAX_MIN_TYPE(ullong, unsigned long long)

    #define min(a, b) _Generic(__typeof__(a), \
        signed char:   min_schar, \
        unsigned char: min_uchar, \
        short:         min_short, \
        unsigned short: min_ushort, \
        int:           min_int, \
        unsigned int:  min_uint, \
        long:          min_long, \
        unsigned long: min_ulong, \
        long long:     min_llong, \
        unsigned long long: min_ullong \
    )((a), (b))

    #define MINMAX_MAX_TYPE(name, type) \
    static inline type max_##name(type a, type b) { \
        return a > b ? a : b; \
    }

    MINMAX_MAX_TYPE(schar, signed char)
    MINMAX_MAX_TYPE(uchar, unsigned char)
    MINMAX_MAX_TYPE(short, short)
    MINMAX_MAX_TYPE(ushort, unsigned short)
    MINMAX_MAX_TYPE(int, int)
    MINMAX_MAX_TYPE(uint, unsigned int)
    MINMAX_MAX_TYPE(long, long)
    MINMAX_MAX_TYPE(ulong, unsigned long)
    MINMAX_MAX_TYPE(llong, long long)
    MINMAX_MAX_TYPE(ullong, unsigned long long)

    #define max(a, b) _Generic(__typeof__(a), \
        signed char:   max_schar, \
        unsigned char: max_uchar, \
        short:         max_short, \
        unsigned short: max_ushort, \
        int:           max_int, \
        unsigned int:  max_uint, \
        long:          max_long, \
        unsigned long: max_ulong, \
        long long:     max_llong, \
        unsigned long long: max_ullong \
    )((a), (b))

    #define MINMAX_MIN3_TYPE(name, type) \
    static inline type min3_##name(type a, type b, type c) { \
        return min_##name(min_##name(a, b), c); \
    }

    MINMAX_MIN3_TYPE(schar, signed char)
    MINMAX_MIN3_TYPE(uchar, unsigned char)
    MINMAX_MIN3_TYPE(short, short)
    MINMAX_MIN3_TYPE(ushort, unsigned short)
    MINMAX_MIN3_TYPE(int, int)
    MINMAX_MIN3_TYPE(uint, unsigned int)
    MINMAX_MIN3_TYPE(long, long)
    MINMAX_MIN3_TYPE(ulong, unsigned long)
    MINMAX_MIN3_TYPE(llong, long long)
    MINMAX_MIN3_TYPE(ullong, unsigned long long)

    #define min3(a, b, c) _Generic(__typeof__(a), \
        signed char:   min3_schar, \
        unsigned char: min3_uchar, \
        short:         min3_short, \
        unsigned short: min3_ushort, \
        int:           min3_int, \
        unsigned int:  min3_uint, \
        long:          min3_long, \
        unsigned long: min3_ulong, \
        long long:     min3_llong, \
        unsigned long long: min3_ullong \
    )((a), (b), (c))

    #define MINMAX_MAX3_TYPE(name, type) \
    static inline type max3_##name(type a, type b, type c) { \
        return max_##name(max_##name(a, b), c); \
    }

    MINMAX_MAX3_TYPE(schar, signed char)
    MINMAX_MAX3_TYPE(uchar, unsigned char)
    MINMAX_MAX3_TYPE(short, short)
    MINMAX_MAX3_TYPE(ushort, unsigned short)
    MINMAX_MAX3_TYPE(int, int)
    MINMAX_MAX3_TYPE(uint, unsigned int)
    MINMAX_MAX3_TYPE(long, long)
    MINMAX_MAX3_TYPE(ulong, unsigned long)
    MINMAX_MAX3_TYPE(llong, long long)
    MINMAX_MAX3_TYPE(ullong, unsigned long long)

    #define max3(a, b, c) _Generic(__typeof__(a), \
        signed char:   max3_schar, \
        unsigned char: max3_uchar, \
        short:         max3_short, \
        unsigned short: max3_ushort, \
        int:           max3_int, \
        unsigned int:  max3_uint, \
        long:          max3_long, \
        unsigned long: max3_ulong, \
        long long:     max3_llong, \
        unsigned long long: max3_ullong \
    )((a), (b), (c))

    #define MINMAX_CLAMP_TYPE(name, type) \
    static inline type clamp_##name(type val, type lo, type hi) { \
        return max_##name(lo, min_##name(val, hi)); \
    }

    MINMAX_CLAMP_TYPE(schar, signed char)
    MINMAX_CLAMP_TYPE(uchar, unsigned char)
    MINMAX_CLAMP_TYPE(short, short)
    MINMAX_CLAMP_TYPE(ushort, unsigned short)
    MINMAX_CLAMP_TYPE(int, int)
    MINMAX_CLAMP_TYPE(uint, unsigned int)
    MINMAX_CLAMP_TYPE(long, long)
    MINMAX_CLAMP_TYPE(ulong, unsigned long)
    MINMAX_CLAMP_TYPE(llong, long long)
    MINMAX_CLAMP_TYPE(ullong, unsigned long long)

    #define clamp(val, lo, hi) _Generic(__typeof__(val), \
        signed char:   clamp_schar, \
        unsigned char: clamp_uchar, \
        short:         clamp_short, \
        unsigned short: clamp_ushort, \
        int:           clamp_int, \
        unsigned int:  clamp_uint, \
        long:          clamp_long, \
        unsigned long: clamp_ulong, \
        long long:     clamp_llong, \
        unsigned long long: clamp_ullong \
    )((val), (lo), (hi))
#endif