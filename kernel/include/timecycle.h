/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

#define NSEC_PER_SEC  1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL

/**
 * 初始化所有时间转换参数
 * 
 * @param hz 时钟频率
 * @param max_seconds 最大时间跨度
 * @param mult 输出正向乘数
 * @param shift 输出正向移位
 * @param mult_inv 输出反向乘数
 * @param shift_inv 输出反向移位
 */
void timecycle_init_params(
    uint64_t hz, uint64_t max_seconds,
    uint32_t *mult, uint32_t *shift,
    uint32_t *mult_inv, uint32_t *shift_inv
);

/**
 * 将周期值转换为纳秒时间
 * 
 * @param cycles 周期值
 * @param mult 乘数
 * @param shift 移位
 * @return 纳秒时间
 */
static inline uint64_t timecycle_cycles_to_ns(
    uint64_t cycles,
    uint32_t mult, uint32_t shift
) {
    return (uint64_t)(((__uint128_t)cycles * mult) >> shift);
}

/**
 * 将纳秒时间转换为周期值
 * 
 * @param ns 纳秒时间
 * @param mult_inv 反向乘数
 * @param shift_inv 反向移位
 * @return 周期值
 */
static inline uint64_t timecycle_ns_to_cycles(
    uint64_t ns,
    uint32_t mult_inv, uint32_t shift_inv
) {
    return (uint64_t)(((__uint128_t)ns * mult_inv) >> shift_inv);
}

/**
 * 将微秒时间转换为纳秒时间
 * 
 * @param usec 微秒时间
 * @return 纳秒时间
 */
#define timecycle_usec_to_ns(usec) ((uint64_t)(usec) * NSEC_PER_USEC)

/**
 * 将毫秒时间转换为纳秒时间
 * 
 * @param msec 毫秒时间
 * @return 纳秒时间
 */
#define timecycle_msec_to_ns(msec) ((uint64_t)(msec) * NSEC_PER_MSEC)

/**
 * 将纳秒时间转换为微秒时间
 * 
 * @param ns 纳秒时间
 * @return 微秒时间
 */
#define timecycle_ns_to_usec(ns) ((uint64_t)(ns) / NSEC_PER_USEC)

/**
 * 将纳秒时间转换为毫秒时间
 * 
 * @param ns 纳秒时间
 * @return 毫秒时间
 */
#define timecycle_ns_to_msec(ns) ((uint64_t)(ns) / NSEC_PER_MSEC)

/**
 * 将秒转为纳秒
 * 
 * @param s 秒时间
 * @return 纳秒秒时间
 */
#define timecycle_s_to_ns(s) ((__uint128_t)(s) * NSEC_PER_SEC)