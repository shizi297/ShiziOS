/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *buffer;   // 缓冲区指针
    size_t size;    // 缓冲区大小
    size_t pos;     // 当前写入位置
    size_t count;   // 已写入字符数（不含 '\0'）
} PFILE;

// 回调类型，用于函数进行输出文字
typedef void (*kio_output_t)(PFILE *stream);

/*
 * 注册输出后端
 *
 * @param backend 普通输出后端
 * @param panic_backend panic 输出后端
 */
void kio_register_backend(kio_output_t backend, kio_output_t panic_backend);

/**
 * 格式化字符串并通过 PFILE 流输出
 *
 * @param stream 输出流
 * @param fmt 格式控制字符串
 * @param args 可变参数列表
 *
 * @return 输出的字符总数（不含 '\0'）
 */
int vfprintk(PFILE *stream, const char *fmt, va_list args);

void vprintk(const char *fmt, va_list args);

__attribute__((noreturn))
void vprintp(const char *fmt, va_list args);

int vsnprintk(char *buf, size_t size, const char *fmt, va_list args);

int snprintk(char *buf, size_t size, const char *fmt, ...);

void printk(const char *fmt, ...);

__attribute__((noreturn))
void printp(const char *fmt, ...);
