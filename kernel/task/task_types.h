/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

typedef struct vm_area {
    uintptr_t start;      // 虚拟起始地址
    uintptr_t end;        // 虚拟结束地址
    uint8_t flags;        // 权限标志
    struct vm_area* next; 
    uint64_t pfn[];         // 起始物理页号
} vm_area_t;