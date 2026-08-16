/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <string.h>
#include <errno.h>
#include <heap.h>

#define EPROBE_DEFER    -517    // 设备依赖未就绪，需要推迟探测

// 构造成功返回值
#define K_OK(v)   { .err = 0, .val = v }

// 构造成功返回指针
#define K_PTR(p)  { .err = 0, .ptr = p }

// 构造错误返回
#define K_ERR(e)    { .err = (e) }

// 如果出错，直接返回 err
#define K_ERR_RETURN(res) K_ERR_RETURN_IF_NOT(res, 0)

// 如果出错，且错误码不等于 err_code，返回错误码
#define K_ERR_RETURN_IF_NOT(res, err_code) \
    do { \
        if ((res).err && (res).err != (err_code)) \
            return (res).err; \
    } while(0)

// 如果出错，且错误码等于 err_code，返回错误码
#define K_ERR_RETURN_IF_OK(res, err_code) \
    do { \
        if ((res).err && (res).err == (err_code)) \
            return (res).err; \
    } while(0)

// 如果出错，返回原类型
#define K_ERR_RETURN_SELF(res) K_ERR_RETURN_SELF_IF_NOT(res, 0)

// 如果出错，且错误码不等于 err_code，返回原类型
#define K_ERR_RETURN_SELF_IF_NOT(res, err_code) \
    do { \
        if ((res).err && (res).err != (err_code)) \
            return res; \
    } while(0)

// 如果出错，且错误码等于 err_code，返回原类型
#define K_ERR_RETURN_SELF_IF_OK(res, err_code) \
    do { \
        if ((res).err && (res).err == (err_code)) \
            return res; \
    } while(0)

// 如果出错，用指定的类型重新构造错误值并返回
#define K_ERR_RETURN_SELF_TYPE(res, type) K_ERR_RETURN_SELF_TYPE_IF_NOT(res, type, 0)

// 如果出错，且错误码不等于 err_code，用指定类型重新构造错误值并返回
#define K_ERR_RETURN_SELF_TYPE_IF_NOT(res, type, err_code) \
    do { \
        if ((res).err && (res).err != (err_code)) \
            return (type){ .err = (res).err }; \
    } while(0)

// 如果出错，且错误码等于 err_code，用指定类型重新构造错误值并返回
#define K_ERR_RETURN_SELF_TYPE_IF_OK(res, type, err_code) \
    do { \
        if ((res).err && (res).err == (err_code)) \
            return (type){ .err = (res).err }; \
    } while(0)

// 如果错误，跳转到标签
#define K_ERR_LABEL(res, label) K_ERR_LABEL_IF_NOT(res, 0, label)

// 如果错误，且错误码不等于 err_code，跳转到标签
#define K_ERR_LABEL_IF_NOT(res, err_code, label) \
    do { \
        if ((res).err && (res).err != (err_code)) \
            goto label; \
    } while(0)

// 如果错误，且错误码等于 err_code，跳转到标签
#define K_ERR_LABEL_IF_OK(res, err_code, label) \
    do { \
        if ((res).err && (res).err == (err_code)) \
            goto label; \
    } while(0)

// 如果错误，设置 err 变量，并跳转到标签
#define K_ERR_LABEL_AND_SAVE(res, label, err_name) K_ERR_LABEL_AND_SAVE_IF_NOT(res, 0, label, err_name)

// 如果错误，且错误码不等于 err_code，设置 err 变量并跳转到标签
#define K_ERR_LABEL_AND_SAVE_IF_NOT(res, err_code, label, err_name) \
    do { \
        if ((res).err && (res).err != (err_code)) { \
            err_name = (res).err; \
            goto label; \
        } \
    } while(0)

// 如果错误，且错误码等于 err_code，设置 err 变量并跳转到标签
#define K_ERR_LABEL_AND_SAVE_IF_OK(res, err_code, label, err_name) \
    do { \
        if ((res).err && (res).err == (err_code)) { \
            err_name = (res).err; \
            goto label; \
        } \
    } while(0)

// 如果出错，直接 break
#define K_ERR_BREAK(res) K_ERR_BREAK_IF_NOT(res, 0)

// 如果出错，且错误码不等于 err_code，跳出循环
#define K_ERR_BREAK_IF_NOT(res, err_code) \
    { \
        if ((res).err && (res).err != (err_code)) \
            break; \
    }

// 如果出错，且错误码等于 err_code，跳出循环
#define K_ERR_BREAK_IF_OK(res, err_code) \
    { \
        if ((res).err && (res).err == (err_code)) \
            break; \
    }

// 如果出错，保存错误码并 break
#define K_ERR_BREAK_AND_SAVE(res, err_name) K_ERR_BREAK_AND_SAVE_IF_NOT(res, 0, err_name)

// 如果出错，且错误码不等于 err_code，保存错误码并跳出循环
#define K_ERR_BREAK_AND_SAVE_IF_NOT(res, err_code, err_name) \
    { \
        if ((res).err && (res).err != (err_code)) { \
            err_name = (res).err; \
            break; \
        } \
    }

// 如果出错，且错误码等于 err_code，保存错误码并跳出循环
#define K_ERR_BREAK_AND_SAVE_IF_OK(res, err_code, err_name) \
    { \
        if ((res).err && (res).err == (err_code)) { \
            err_name = (res).err; \
            break; \
        } \
    }

// 如果出错，返回指定的值
#define K_ERR_RESULT(res, result) K_ERR_RESULT_IF_NOT(res, 0, result)

// 如果出错，且错误码不等于 err_code，返回指定的值
#define K_ERR_RESULT_IF_NOT(res, err_code, result) \
    do { \
        if ((res).err && (res).err != (err_code)) \
            return (result); \
    } while(0)

// 如果出错，且错误码等于 err_code，返回指定的值
#define K_ERR_RESULT_IF_OK(res, err_code, result) \
    do { \
        if ((res).err && (res).err == (err_code)) \
            return (result); \
    } while(0)

#define CONCAT(a, b) a##b

#define BITS_PER_BYTE           8
#define BITS_PER_LONG           (sizeof(unsigned long) * BITS_PER_BYTE)
#define BITS_PER_LONG_LONG      (sizeof(unsigned long long) * BITS_PER_BYTE)

#define BIT(n)                  (1UL << (n))
#define BIT_ULL(n)              (1ULL << (n))

#define BIT_MASK(n)             BIT(n)
#define BIT_WORD(n)             ((n) / BITS_PER_LONG)

#define GENMASK(h, l) \
    (((~0UL) - BIT(l) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#define GENMASK_ULL(h, l) \
    (((~0ULL) - BIT_ULL(l) + 1) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))

#define container_of(ptr, type, member) __extension__ ({ \
	const __typeof__(((type *)0)->member) *__pmember = (ptr); \
	(type *)((char *)__pmember - offsetof(type, member)); })

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