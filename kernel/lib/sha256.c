/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <sha256.h>
#include <klibc.h>
#include <heap.h>

// SHA-256 计算上下文
struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t buf[64];
};

// 64 轮计算使用的轮常量 K[t]
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t sha256_sigma0(uint32_t x) {
    return __builtin_rotateright32(x, 2) ^
           __builtin_rotateright32(x, 13) ^
           __builtin_rotateright32(x, 22);
}

static inline uint32_t sha256_sigma1(uint32_t x) {
    return __builtin_rotateright32(x, 6) ^
           __builtin_rotateright32(x, 11) ^
           __builtin_rotateright32(x, 25);
}

static inline uint32_t sha256_gamma0(uint32_t x) {
    return __builtin_rotateright32(x, 7) ^
           __builtin_rotateright32(x, 18) ^
           (x >> 3);
}

static inline uint32_t sha256_gamma1(uint32_t x) {
    return __builtin_rotateright32(x, 17) ^
           __builtin_rotateright32(x, 19) ^
           (x >> 10);
}

/**
 * 处理一个 64 字节消息块，更新 8 个状态字
 *
 * @param state 当前 8 个状态字，被原地更新
 * @param block 64 字节消息块
 */
static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    uint32_t a_val, b_val, c_val, d_val, e_val, f_val, g_val, h_val;
    uint32_t t1, t2;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] <<  8) |
               ((uint32_t)block[i * 4 + 3] <<  0);
    }
    for (int i = 16; i < 64; i++)
        w[i] = sha256_gamma1(w[i - 2]) + w[i - 7] +
               sha256_gamma0(w[i - 15]) + w[i - 16];

    a_val = state[0];
    b_val = state[1];
    c_val = state[2];
    d_val = state[3];
    e_val = state[4];
    f_val = state[5];
    g_val = state[6];
    h_val = state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h_val + sha256_sigma1(e_val) +
             sha256_ch(e_val, f_val, g_val) + sha256_k[i] + w[i];
        t2 = sha256_sigma0(a_val) + sha256_maj(a_val, b_val, c_val);

        h_val = g_val;
        g_val = f_val;
        f_val = e_val;
        e_val = d_val + t1;
        d_val = c_val;
        c_val = b_val;
        b_val = a_val;
        a_val = t1 + t2;
    }

    state[0] += a_val;
    state[1] += b_val;
    state[2] += c_val;
    state[3] += d_val;
    state[4] += e_val;
    state[5] += f_val;
    state[6] += g_val;
    state[7] += h_val;
}

/**
 * 初始化 SHA-256 计算上下文
 *
 * @param ctx 指向调用者提供的上下文结构体
 */
static void sha256_init(struct sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

/**
 * 向 SHA-256 上下文添加数据
 *
 * @param ctx 上下文指针
 * @param data 数据缓冲区
 * @param len 数据长度（字节）
 */
static void sha256_update(struct sha256_ctx *ctx, const void *data, size_t len) {
    const uint8_t *src = data;
    size_t buf_pos = (size_t)(ctx->count % 64);

    ctx->count += (uint64_t)len;

    while (len > 0) {
        size_t chunk = 64 - buf_pos;
        if (len < chunk)
            chunk = len;
        memcpy(ctx->buf + buf_pos, src, chunk);
        buf_pos += chunk;
        src += chunk;
        len -= chunk;
        if (buf_pos == 64) {
            sha256_transform(ctx->state, ctx->buf);
            buf_pos = 0;
        }
    }
}

/**
 * 完成 SHA-256 计算并输出 32 字节哈希值
 *
 * @param ctx 上下文指针
 * @param digest 32 字节输出缓冲区
 */
static void sha256_final(struct sha256_ctx *ctx, uint8_t digest[32]) {
    uint64_t total_bits = ctx->count * 8;
    size_t buf_pos = (size_t)(ctx->count % 64);

    ctx->buf[buf_pos++] = 0x80;
    if (buf_pos > 56) {
        memset(ctx->buf + buf_pos, 0, 64 - buf_pos);
        sha256_transform(ctx->state, ctx->buf);
        buf_pos = 0;
    }
    memset(ctx->buf + buf_pos, 0, 56 - buf_pos);

    for (int i = 0; i < 8; i++)
        ctx->buf[56 + i] = (uint8_t)(total_bits >> (56 - i * 8));

    sha256_transform(ctx->state, ctx->buf);

    for (int i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >>  8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] >>  0);
    }
}

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
) {
    struct sha256_ctx *c = ctx;

    if (!c) {
        c = kheap_alloc(sizeof(*c));
        if (!c)
            return NULL;
        sha256_init(c);
    }

    sha256_update(c, data, len);
    return c;
}

/**
 * 完成 SHA-256 迭代器计算并释放上下文
 *
 * @param ctx 上一次 sha256_iter_next 返回的上下文指针
 * @param digest 32 字节输出缓冲区
 */
void sha256_iter_fin(
    struct sha256_ctx *ctx,
    uint8_t digest[32]
) {
    struct sha256_ctx *c = ctx;

    if (c) {
        sha256_final(c, digest);
        kheap_free(c);
    }
}

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
) {
    struct sha256_ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}