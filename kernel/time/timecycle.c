/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <timecycle.h>
#include <asm/serial.h>

#define NSEC_PER_SEC 1000000000ULL
#define MAX_SHIFT 32

#define TIMECYCLE_PANIC(fmt, ...) \
    printp("[TIMECYCLE] ERROR: " fmt, ##__VA_ARGS__)

/**
 * @brief 计算最优的shift值
 * 
 * @param hz 时钟频率
 * @param max_seconds 最大时间跨度
 * 
 * @return 最优shift值
 */
static uint32_t calc_optimal_shift(uint64_t hz, uint64_t max_seconds) {
    uint32_t shift;
    
    for (shift = 0; shift <= MAX_SHIFT; shift++) {
        __uint128_t numerator = (__uint128_t)NSEC_PER_SEC << shift;
        uint64_t mult = (uint64_t)(numerator / hz);
        
        if (mult == 0) {
            continue;
        }
        
        if (mult > UINT32_MAX) {
            return shift > 0 ? shift - 1 : 0;
        }
        
        __uint128_t max_cycles = (__uint128_t)hz * max_seconds;
        __uint128_t max_ns = max_cycles * mult;
        
        if (max_ns >> shift > UINT64_MAX) {
            continue;
        }
        
        return shift;
    }
    
    return MAX_SHIFT;
}

/**
 * 计算正向转换参数
 * 
 * @param hz 时钟频率
 * @param shift 移位值
 * @param mult 输出乘数
 * @param shift_out 输出移位
 */
static void calc_forward_params(
    uint64_t hz, uint32_t shift,
    uint32_t *mult, uint32_t *shift_out
) {
    __uint128_t numerator = (__uint128_t)NSEC_PER_SEC << shift;
    *mult = (uint32_t)(numerator / hz);
    *shift_out = shift;
}

/**
 * 计算反向转换参数
 * 
 * @param mult 正向乘数
 * @param shift 正向移位
 * @param mult_inv 输出反向乘数
 * @param shift_inv 输出反向移位
 */
static void calc_inverse_params(
    uint32_t mult, uint32_t shift,
    uint32_t *mult_inv, uint32_t *shift_inv
) {
    // 从 32 开始向下尝试，找到第一个能让结果在 32 位内的 extra_shift
    for (uint32_t extra_shift = 32; extra_shift > 0; extra_shift--) {
        __uint128_t one = (__uint128_t)1 << (shift + extra_shift);
        uint64_t inv = (uint64_t)(one / mult);
        if (inv <= UINT32_MAX) {
            *mult_inv = (uint32_t)inv;
            *shift_inv = shift + extra_shift;
            return;
        }
    }
    // 如果所有尝试都失败
    *mult_inv = 0;
    *shift_inv = shift;
    TIMECYCLE_PANIC("cannot find valid inverse parameters\n");
}

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
) {
    if (hz == 0) {
        TIMECYCLE_PANIC("hz is zero\n");
    }
    
    if (max_seconds == 0) {
        TIMECYCLE_PANIC("max_seconds is zero\n");
    }
    
    uint32_t optimal_shift = calc_optimal_shift(hz, max_seconds);
    
    calc_forward_params(hz, optimal_shift, mult, shift);
    
    if (*mult == 0) {
        for (uint32_t s = optimal_shift + 1; s <= MAX_SHIFT; s++) {
            calc_forward_params(hz, s, mult, shift);
            if (*mult != 0) {
                optimal_shift = s;
                break;
            }
        }
    }
    
    calc_inverse_params(*mult, *shift, mult_inv, shift_inv);
    
    if (*mult == 0 || *mult_inv == 0) {
        TIMECYCLE_PANIC("invalid conversion parameters\n");
    }
}