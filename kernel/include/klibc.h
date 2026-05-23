/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <string.h>
#include <errno.h>
#include <heap.h>

#define EPROBE_DEFER    -517    // 设备依赖未就绪，需要推迟探测

// 判断值是否为错误码
#define IS_ERR_VALUE(x) ((unsigned long)(x) >= (unsigned long)-4095)

// 将错误码转换为指针
#define ERR_PTR(error)   ((void *)(long)(error))

// 从错误指针提取错误码
#define PTR_ERR(ptr)     ((long)(ptr))

// 将错误码转为指针
#define ERR_CAST(x) ((void *)(long)(x))

// 判断指针是否为错误指针
#define IS_ERR(ptr)      IS_ERR_VALUE((unsigned long)(ptr))

// 判断指针是否为NULL或错误指针
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))

static inline char *strdup(const char *s) {
    size_t l = strlen(s);
    char *d = kheap_alloc(l + 1);
    if (!d) return NULL;
    return memcpy(d, s, l + 1);
}

static inline char *strndup(const char *s, size_t n) {
    size_t l = strnlen(s, n);
    char *d = kheap_alloc(l + 1);
    if (!d) return NULL;
    memcpy(d, s, l);
    d[l] = '\0';
    return d;
}