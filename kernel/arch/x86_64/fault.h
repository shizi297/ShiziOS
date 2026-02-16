/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#ifndef ARCH_FAULT_H
#define ARCH_FAULT_H

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

// 异常向量号
#define EXC_DE     0   // 除零错误
#define EXC_DB     1   // 调试异常
#define EXC_NMI    2   // 不可屏蔽中断
#define EXC_BP     3   // 断点异常
#define EXC_OF     4   // 溢出异常
#define EXC_BR     5   // 边界检查异常
#define EXC_UD     6   // 无效操作码
#define EXC_NM     7   // 设备不可用
#define EXC_DF     8   // 双重故障
#define EXC_CSO    9   // 协处理器段超限
#define EXC_TS     10  // 无效TSS
#define EXC_NP     11  // 段不存在
#define EXC_SS     12  // 栈段故障
#define EXC_GP     13  // 一般保护故障
#define EXC_PF     14  // 页故障
#define EXC_SPUR   15  // 伪中断
#define EXC_MF     16  // 浮点异常
#define EXC_AC     17  // 对齐检查
#define EXC_MC     18  // 机器检查
#define EXC_XM     19  // SIMD浮点异常
#define EXC_VE     20  // 虚拟化异常

// 需要错误代码的异常向量掩码
#define EXC_HAS_ERRCODE  ((1ULL << EXC_DF) | (1ULL << EXC_TS) | \
                          (1ULL << EXC_NP) | (1ULL << EXC_SS) | \
                          (1ULL << EXC_GP) | (1ULL << EXC_PF) | \
                          (1ULL << EXC_AC) | (1ULL << EXC_VE))

// 最大异常向量号
#define EXC_MAX_VEC 31

// 异常门类型位图 (0-31): 1=陷阱门, 0=中断门
#define FAULT_GATE_TYPE_BITMAP \
    ((1ULL << EXC_DE)   | \
     (1ULL << EXC_DB)   | \
     (1ULL << EXC_NMI)  | \
     (1ULL << EXC_BP)   | \
     (1ULL << EXC_OF)   | \
     (1ULL << EXC_BR)   | \
     (1ULL << EXC_UD)   | \
     (1ULL << EXC_NM)   | \
     (1ULL << EXC_DF)   | \
     (1ULL << EXC_CSO)  | \
     (1ULL << EXC_TS)   | \
     (1ULL << EXC_NP)   | \
     (1ULL << EXC_SS)   | \
     (1ULL << EXC_GP)   | \
     (1ULL << EXC_PF)   | \
     (1ULL << EXC_SPUR) | \
     (1ULL << EXC_MF)   | \
     (1ULL << EXC_AC)   | \
     (1ULL << EXC_MC)   | \
     (1ULL << EXC_XM)   | \
     (1ULL << EXC_VE))

// 异常DPL位图 (0-31): 1=用户态(DPL=3), 0=内核态(DPL=0) 
#define FAULT_DPL_BITMAP \
    (1ULL << EXC_BP)

#ifndef __ASSEMBLER__

// 异常处理函数
void exc_de(void);
void exc_db(void);
void exc_nmi(void);
void exc_bp(void);
void exc_of(void);
void exc_br(void);
void exc_ud(void);
void exc_nm(void);
void exc_df(void);
void exc_cso(void);
void exc_ts(void);
void exc_np(void);
void exc_ss(void);
void exc_gp(void);
void exc_pf(void);
void exc_mf(void);
void exc_ac(void);
void exc_mc(void);
void exc_xm(void);
void exc_ve(void);

#endif

#endif  // ARCH_FAULT_H