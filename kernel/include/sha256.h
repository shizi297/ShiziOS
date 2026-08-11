/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

struct sha256_ctx;

/**
 * 向 SHA-256 迭代器添加数据
 *
 * @param ctx 上一次返回的上下文指针，或 NULL 表示首次调用
 * @param data 数据缓冲区
 * @param len 数据长度（字节）
 *
 * @return 当前上下文指针
 */
struct sha256_ctx *sha256_iter_next(
    struct sha256_ctx *ctx,
    const void *data,
    size_t len
);

/**
 * 完成 SHA-256 迭代器计算并释放上下文
 *
 * @param ctx 上一次 sha256_iter_next 返回的上下文指针
 * @param digest 32 字节输出缓冲区
 */
void sha256_iter_fin(
    struct sha256_ctx *ctx,
    uint8_t digest[32]
);

/**
 * 一次性计算数据的 SHA-256 哈希值
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度（字节）
 * @param digest 32 字节输出缓冲区
 */
void sha256(
    const void *data,
    size_t len,
    uint8_t digest[32]
);