/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <klibc.h>

typedef uint8_t bitmap_t;

// 每个存储单元包含的位数
#define BITS_PER_UNIT (sizeof(bitmap_t) * 8)

// 计算存储bits个位所需的字节数
#define BITMAP_BYTES(bits)  (((bits) + 7) / 8)

/**
 * 初始化一个位图变量
 * 
 * @param name 变量名
 * @param bits 位图的总位数
 */
#define INIT_BITMAP(name, bits) \
    bitmap_t name[BITMAP_BYTES(bits)] __attribute__((aligned(sizeof(uint64_t))))

/**
 * 设置指定位置为1
 * 
 * @param map 位图基指针
 * @param bit 要设置的位索引
 */
static inline void bitmap_set(bitmap_t *map, uint32_t bit) {
    map[bit / BITS_PER_UNIT] |= (1U << (bit % BITS_PER_UNIT));
}

/**
 * 清除指定位置（设置为0）
 * 
 * @param map 位图指针
 * @param bit 要清除的位索引
 */
static inline void bitmap_clear(bitmap_t *map, uint32_t bit) {
    map[bit / BITS_PER_UNIT] &= ~(1U << (bit % BITS_PER_UNIT));
}

/**
 * 查看指定位置是否为1
 * 
 * @param map 位图基指针
 * @param bit 要测试的位索引
 * 
 * @return 为1 ：true
 * @return 不为1 ：false
 */
static inline bool bitmap_check(const bitmap_t *map, uint32_t bit) {
    return (map[bit / BITS_PER_UNIT] >> (bit % BITS_PER_UNIT)) & 1U;
}

/**
 * 将整个位图清零
 * 
 * @param map 位图基指针
 * @param bits 位图的总位数
 */
static inline void bitmap_zero(bitmap_t *map, uint32_t bits) {
    memset(map, 0, BITMAP_BYTES(bits));
}

/**
 * 将整个位图全部置1
 * 
 * @param map 位图指针
 * @param bits 位图的总位数
 */
static inline void bitmap_fill(bitmap_t *map, uint32_t bits) {
    memset(map, 0xFF, BITMAP_BYTES(bits));
    uint32_t remainder = bits % BITS_PER_UNIT;
    if (remainder) {
        map[BITMAP_BYTES(bits) - 1] &= (1U << remainder) - 1;
    }
}

/**
 * 将一个位图复制到另一个位图
 * 
 * @param dst 目标位图指针
 * @param src 源位图指针
 * @param bits 要复制的总位数
 */
static inline void bitmap_copy(bitmap_t *dst, const bitmap_t *src, uint32_t bits) {
    memcpy(dst, src, BITMAP_BYTES(bits));
}

/**
 * 设置一段连续位为1
 * 
 * @param map   位图指针
 * @param start 起始位索引
 * @param count 连续位数
 */
static inline void bitmap_set_range(bitmap_t *map, uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set(map, start + i);
    }
}

/**
 * 清除一段连续位（置0）
 * 
 * @param map   位图指针
 * @param start 起始位索引
 * @param count 连续位数
 */
static inline void bitmap_clear_range(bitmap_t *map, uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        bitmap_clear(map, start + i);
    }
}

// 用于字扫描的字类型
typedef uint64_t bitmap_word_t;
#define BITS_PER_WORD (sizeof(bitmap_word_t) * 8)

// 将位图指针转换为字指针
static inline const bitmap_word_t *bitmap_to_words(const bitmap_t *map) {
    return (const bitmap_word_t *)map;
}

/**
 * 在位图中从指定起始位开始查找第一个值为 find_value 的位
 * 
 * @param map        位图基指针
 * @param bits       位图的总位数
 * @param start      起始查找位索引（包含）
 * @param find_value 要查找的值
 * 
 * @return 找到：位索引
 * @return 没找到 ：bits
 */
static inline uint32_t bitmap_find(const bitmap_t *map, uint32_t bits, uint32_t start, bool find_value) {
    if (start >= bits) return bits;

    for (uint32_t i = start; i < bits; i++) {
        if (bitmap_check(map, i) == find_value)
            return i;
    }
    return bits;
}