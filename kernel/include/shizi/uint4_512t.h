/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#ifndef UINT4_512T
#define UINT4_512T

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 位数定义
typedef enum {
    PAGE_LEVEL_0 = 0,
    PAGE_LEVEL_1 = 1,
    PAGE_LEVEL_2 = 2,
    PAGE_LEVEL_3 =3,
} page_level_t;

// 计数器
typedef struct {
    uint16_t digit[4];    // 4位512进制数，对应4级页表
    bool carry_flags[4];  // 进位标记数组，表示对应位在下次增加是否会发生进位
} uint4_512t;

// 初始化函数
static inline void uint4_512_init(uint4_512t *num) {
    if (num == NULL) return;
    
    // 初始化所有位为0
    num->digit[0] = 0;
    num->digit[1] = 0;
    num->digit[2] = 0;
    num->digit[3] = 0;
    
    // 清除所有进位标记
    num->carry_flags[0] = false;
    num->carry_flags[1] = false;
    num->carry_flags[2] = false;
    num->carry_flags[3] = false;
}

/*
 * 设置对应位的值
 * 自动更新进位
 * 
 * @param num 变量的内存地址
 * @param type 位数
 * @param count 要设置的值
 */
static inline void uint4_512_set(uint4_512t *num, page_level_t type, uint16_t count) {
    if (num == NULL) return;
    if (type < 0 || type > 3) return; 
    
    // 确保值在有效范围内（0-511）
    if (count >= 512) {
        count %= 512;  
    }
    
    // 设置指定位置的值
    num->digit[type] = count;
    
    // 重新计算所有进位预测
    num->carry_flags[0] = (num->digit[0] == 511);
    num->carry_flags[1] = (num->digit[1] == 511) && num->carry_flags[0];
    num->carry_flags[2] = (num->digit[2] == 511) && num->carry_flags[1];
    num->carry_flags[3] = (num->digit[3] == 511) && num->carry_flags[2];
}

/*
 * 获取对应位的值
 * 进位标记不变
 * 
 * @param num 变量的内存地址
 * @param type 位数
 */
static inline uint16_t uint4_512_get(uint4_512t *num, page_level_t type) {
    if (num == NULL) return 0xFFFF;
    if (type < 0 || type > 3) return 0xFFFF; 

    return num->digit[type];
}

/*
 * 获取对应位的进位标记
 * 进位标记不变
 * 
 * @param num 变量的内存地址
 * @param type 位数
 */
static inline uint8_t uint4_512_get_carry(uint4_512t *num, page_level_t type) {
    if (num == NULL) return 0xFF;
    if (type < 0 || type > 3) return 0xFF; 

    return num->carry_flags[type];
}

/*
 * 自增
 * 相当于++
 * 自动更新进位标记（预测下一次自增时的进位情况）
 * 
 * @param num 变量的内存地址
 */
static inline void uint4_512_inc(uint4_512t *num) {
    if (num == NULL) return;
    
    // 执行自增操作
    if (++num->digit[0] == 512) {
        num->digit[0] = 0;
        
        if (++num->digit[1] == 512) {
            num->digit[1] = 0;
            
            if (++num->digit[2] == 512) {
                num->digit[2] = 0;
                
                if (++num->digit[3] == 512) {
                    num->digit[3] = 0;
                }
            }
        }
    }
    
    // 基于自增后的新值计算进位预测
    // 第0位的进位预测：如果当前是511，下次自增就会进位
    num->carry_flags[0] = (num->digit[0] == 511);
    
    // 第1位的进位预测：如果当前是511且第0位预测进位，下次自增就会进位
    num->carry_flags[1] = (num->digit[1] == 511) && num->carry_flags[0];
    
    // 第2位的进位预测：如果当前是511且第1位预测进位，下次自增就会进位
    num->carry_flags[2] = (num->digit[2] == 511) && num->carry_flags[1];
    
    // 第3位的进位预测：如果当前是511且第2位预测进位，下次自增就会进位
    num->carry_flags[3] = (num->digit[3] == 511) && num->carry_flags[2];
}

#endif // UINT4_512T