/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <klibc.h>  
#include <bitmap.h> 

// 负载因子
#define DYNARR_LOAD_NUMERATOR 9
#define DYNARR_LOAD_DENOMINATOR 10

// 根据容量计算扩容阈值
#define DYNARR_MAX_COUNT(capacity) \
    (((capacity) * DYNARR_LOAD_NUMERATOR) / DYNARR_LOAD_DENOMINATOR)

#define DYNARR_MALLOC(size) kheap_alloc(size)
#define DYNARR_FREE(ptr)    kheap_free(ptr)

typedef struct dynarr_struct {
    void *arr;               // 数据缓冲区
    uint64_t element_size;   // 每个元素的字节数
    uint64_t current_count;  // 当前元素个数
    uint64_t max_count;      // 扩容阈值
    uint64_t dynarr_count;   // 当前总容量（元素个数）
    uint64_t max_limit;      // 最大容量限制，0 表示无限制
} dynarr_t;

/**
 * 创建动态数组
 *
 * @param element_size 每个元素的大小（字节）
 * @param max_capacity 最大容量限制（元素个数），0 表示无限制
 *
 * @return dynarr_t 指针
 */
static inline dynarr_t *dynarr_create(
    uint64_t element_size, uint64_t max_capacity
) {
    if (element_size == 0) return NULL;

    uint64_t init_capacity = 16;  // 固定初始容量为16

    // 如果最大容量小于16且不为0，则初始容量不能超过最大容量
    if (max_capacity != 0 && init_capacity > max_capacity) {
        init_capacity = max_capacity;
    }

    dynarr_t *d = (dynarr_t *)DYNARR_MALLOC(sizeof(dynarr_t));
    if (!d) return NULL;

    d->arr = DYNARR_MALLOC(init_capacity * element_size);
    if (!d->arr) {
        DYNARR_FREE(d);
        return NULL;
    }

    d->element_size = element_size;
    d->dynarr_count = init_capacity;
    d->max_count = DYNARR_MAX_COUNT(init_capacity);
    d->current_count = 0;
    d->max_limit = max_capacity;

    return d;
}

/**
 * 销毁动态数组
 *
 * @param d 动态数组指针
 */
static inline void dynarr_destroy(dynarr_t *d) {
    if (!d) return;
    if (d->arr)
        DYNARR_FREE(d->arr);
    DYNARR_FREE(d);
}

/**
 * 将容量翻倍
 *
 * @param d 动态数组指针
 */
static inline bool dynarr_grow(dynarr_t *d) {
    uint64_t new_capacity = d->dynarr_count * 2;
    // 检查是否超过最大限制
    if (d->max_limit != 0 && new_capacity > d->max_limit) {
        new_capacity = d->max_limit;
        if (new_capacity <= d->dynarr_count) {
            return false;  // 无法再扩容
        }
    }

    void *new_arr = DYNARR_MALLOC(new_capacity * d->element_size);
    if (!new_arr) return false;

    // 拷贝旧数据
    for (uint64_t i = 0; i < d->current_count; i++) {
        void *src = (char *)d->arr + i * d->element_size;
        void *dst = (char *)new_arr + i * d->element_size;
        memcpy(dst, src, d->element_size);
    }

    DYNARR_FREE(d->arr);
    d->arr = new_arr;
    d->dynarr_count = new_capacity;
    d->max_count = DYNARR_MAX_COUNT(new_capacity);
    return true;
}

/**
 * 追加一个元素
 *
 * @param d 动态数组指针
 * @param element 指向要追加的元素的指针
 */
static inline bool dynarr_append(dynarr_t *d, const void *element) {
    if (!d || !element) return false;

    if (d->current_count >= d->max_count) {
        if (!dynarr_grow(d)) return false;
    }

    void *dst = (char *)d->arr + d->current_count * d->element_size;
    memcpy(dst, element, d->element_size);

    d->current_count++;
    return true;
}

/**
 * 获取指定索引元素的指针
 *
 * @param d 动态数组指针
 * @param index 索引（0-based）
 *
 * @return 元素指针
 */
static inline void *dynarr_get(dynarr_t *d, uint64_t index) {
    if (!d || index >= d->current_count) return NULL;
    return (char *)d->arr + index * d->element_size;
}

/**
 * 移除最后一个元素
 *
 * @param d 动态数组指针
 * @param out_element 指向存放被移除元素的空间(可以为null)
 */
static inline bool dynarr_pop(dynarr_t *d, void *out_element) {
    if (!d || !d->current_count) return false;

    d->current_count--;

    if (out_element) {
        void *src = (char *)d->arr + d->current_count * d->element_size;
        memcpy(out_element, src, d->element_size);
    }

    return true;
}

// 将容量扩展到至少 min_capacity，新增区域清零
static inline bool dynarr_expand_to(dynarr_t *d, uint64_t min_capacity) {
    if (min_capacity <= d->dynarr_count) return true;

    uint64_t new_capacity = d->dynarr_count;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
        // 检查是否超过最大限制
        if (d->max_limit != 0 && new_capacity > d->max_limit) {
            new_capacity = d->max_limit;
            if (new_capacity < min_capacity) {
                return false;  // 无法满足要求
            }
            break;
        }
    }

    void *new_arr = DYNARR_MALLOC(new_capacity * d->element_size);
    if (!new_arr) return false;

    // 拷贝旧数据
    for (uint64_t i = 0; i < d->current_count; i++) {
        void *src = (char *)d->arr + i * d->element_size;
        void *dst = (char *)new_arr + i * d->element_size;
        memcpy(dst, src, d->element_size);
    }

    // 清零新增区域（从 old_count 到 new_capacity）
    uint64_t old_count = d->current_count;
    size_t clear_size = (new_capacity - old_count) * d->element_size;
    memset((char *)new_arr + old_count * d->element_size, 0, clear_size);

    DYNARR_FREE(d->arr);
    d->arr = new_arr;
    d->dynarr_count = new_capacity;
    d->max_count = DYNARR_MAX_COUNT(new_capacity);
    return true;
}

/**
 * 设置指定索引的元素值
 *
 * @param d 动态数组指针
 * @param index 索引
 * @param element 指向新元素的指针
 */
static inline bool dynarr_set(dynarr_t *d, uint64_t index, const void *element) {
    if (!d || !element) return false;

    if (index >= d->current_count) {
        // 需要扩容
        if (!dynarr_expand_to(d, index + 1)) return false;
        d->current_count = index + 1;
    }

    void *dst = (char *)d->arr + index * d->element_size;
    memcpy(dst, element, d->element_size);
    return true;
}

/**
 * 预留容量（手动扩容）
 *
 * @param d 动态数组指针
 * @param new_capacity 新容量（元素个数），必须大于当前容量
 */
static inline bool dynarr_reserve(dynarr_t *d, uint64_t new_capacity) {
    if (!d || new_capacity <= d->dynarr_count) return false;
    
    // 检查是否超过最大限制
    if (d->max_limit != 0 && new_capacity > d->max_limit) {
        return false;
    }

    void *new_arr = DYNARR_MALLOC(new_capacity * d->element_size);
    if (!new_arr) return false;

    // 拷贝旧数据
    for (uint64_t i = 0; i < d->current_count; i++) {
        void *src = (char *)d->arr + i * d->element_size;
        void *dst = (char *)new_arr + i * d->element_size;
        memcpy(dst, src, d->element_size);
    }

    DYNARR_FREE(d->arr);
    d->arr = new_arr;
    d->dynarr_count = new_capacity;
    d->max_count = DYNARR_MAX_COUNT(new_capacity);
    return true;
}

/**
 * 获取当前元素个数
 *
 * @param d 动态数组指针
 *
 * @return 元素个数
 */
static inline uint64_t dynarr_count(dynarr_t *d) {
    return d ? d->current_count : 0;
}

/**
 * 获取当前容量
 *
 * @param d 动态数组指针
 *
 * @return 容量
 */
static inline uint64_t dynarr_capacity(dynarr_t *d) {
    return d ? d->dynarr_count : 0;
}

/*
 * 创建基于 dynarr 的位图
 *
 * @param max_bits 最大位数（0 表示无限制）
 *
 * @return dynarr_t 指针
 */
static inline dynarr_t *dynarr_bitmap_create(uint32_t max_bits) {
    // 初始分配 16 字节，可管理 128 位
    size_t init_bytes = 16;
    size_t max_capacity;

    // 若最大位数小于 128，按需减小初始字节数
    if (max_bits && max_bits < 128)
        init_bytes = (max_bits + 7) / 8;

    // 计算最大容量（字节数），0 表示无限制
    max_capacity = !max_bits ? 0 : (max_bits + 7) / 8;

    dynarr_t *d = dynarr_create(sizeof(bitmap_t), max_capacity);
    if (!d)
        return NULL;

    // 预分配初始空间，失败则销毁
    if (init_bytes > 0) {
        if (!dynarr_expand_to(d, init_bytes)) {
            dynarr_destroy(d);
            return NULL;
        }
    }
    
    return d;
}

/*
 * 在位图中分配一个空闲位
 *
 * @param bitmap 位图指针
 * @param hint 搜索起点，0 表示从头开始
 * @param out_bit 写入位索引的指针
 */
static inline bool dynarr_bitmap_alloc(dynarr_t *bitmap, uint32_t hint, uint32_t *out_bit) {
    if (!bitmap)
        return false;

    // 在位图当前容量内查找空闲位
    uint32_t total_bits = dynarr_capacity(bitmap) * BITS_PER_UNIT;
    uint32_t found = bitmap_find(bitmap->arr, total_bits, hint, 0);

    if (found < total_bits) {
        bitmap_set(bitmap->arr, found);
        if (out_bit)
            *out_bit = found;

        return true;
    }

    // 已达到最大容量上限且无空闲位，无法继续分配
    if (bitmap->max_limit != 0 && dynarr_capacity(bitmap) >= bitmap->max_limit)
        return false;

    // 容量翻倍，至少增加 16 字节，且不超过 max_limit
    uint64_t new_capacity = dynarr_capacity(bitmap) * 2;
    if (new_capacity < 16)
        new_capacity = 16;

    if (bitmap->max_limit != 0 && new_capacity > bitmap->max_limit)
        new_capacity = bitmap->max_limit;

    if (!dynarr_expand_to(bitmap, new_capacity))
        return false;

    // 扩容后重新查找空闲位
    total_bits = dynarr_capacity(bitmap) * BITS_PER_UNIT;
    found = bitmap_find(bitmap->arr, total_bits, hint, 0);
    if (found < total_bits) {
        bitmap_set(bitmap->arr, found);
        if (out_bit)
            *out_bit = found;

        return true;
    }

    // 仍无空闲位则返回失败
    return false;
}

/*
 * 释放已分配的位
 *
 * @param bitmap 位图指针
 * @param bit 位索引
 */
static inline void dynarr_bitmap_free(dynarr_t *bitmap, uint32_t bit) {
    if (!bitmap)
        return;

    // 检查位索引是否在有效范围内，避免越界清除
    if (bit < dynarr_capacity(bitmap) * BITS_PER_UNIT)
        bitmap_clear(bitmap->arr, bit);
}

/*
 * 检查位是否已分配
 *
 * @param bitmap 位图指针
 * @param bit 位索引
 */
static inline bool dynarr_bitmap_test(dynarr_t *bitmap, uint32_t bit) {
    if (!bitmap)
        return false;

    uint32_t total_bits = dynarr_capacity(bitmap) * BITS_PER_UNIT;

    // 超出当前容量的位视为未分配
    if (bit >= total_bits)
        return false;

    return bitmap_check(bitmap->arr, bit);
}

/*
 * 销毁位图
 *
 * @param bitmap 位图指针
 */
static inline void dynarr_bitmap_destroy(dynarr_t *bitmap) {
    dynarr_destroy(bitmap);
}