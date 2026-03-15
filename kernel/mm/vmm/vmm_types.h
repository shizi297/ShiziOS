/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>

// 虚拟内存权限标志
typedef enum {
    VM_NONE  = 0,
    VM_READ  = 1 << 0,  // 可读
    VM_WRITE = 1 << 1,  // 可写
    VM_EXEC  = 1 << 2,  // 可执行
    VM_USER  = 1 << 3,  // 用户空间（否则为内核空间）
    VM_UC    = 1 << 4, 
} vm_prot_t;

// VMM返回码
typedef enum {
    VMM_OK                = 0,   // 操作成功
    
    // 参数错误
    VMM_INVALID_ARGUMENT  = 1,   // 参数无效
    VMM_INVALID_ADDRESS   = 2,   // 地址无效（未对齐或超出范围）
    VMM_INVALID_SIZE      = 3,   // 大小无效（为0或不对齐或者超出大小）
    VMM_INVALID_PERMISSION = 4,  // 权限标志无效（超出允许范围）
    
    // 资源错误
    VMM_OUT_OF_MEMORY     = 10,  // 物理内存不足（PMM分配失败）
    VMM_OUT_OF_ADDRESS_SPACE = 11,  // 虚拟内存不足
    
    // 状态错误
    VMM_ALREADY_MAPPED    = 20,  // 地址已映射
    VMM_NOT_MAPPED        = 21,  // 地址未映射（取消映射时）
    
    // 内部错误
    VMM_INTERNAL_ERROR    = 30,  // 内部错误
    VMM_UNSUPPORTED_OP    = 31,  // 不支持的操作
} vmm_result_t;

// 页表条目类型
typedef uint64_t pte_t;