/* SPDX-License-Identifier: Apache-2.0 */

typedef struct vm_area {
    uintptr_t start;      // 虚拟起始地址
    uintptr_t end;        // 虚拟结束地址
    uint64_t pfn;         // 起始物理页号
    uint8_t order;        // 分配的order
    uint8_t flags;        // 权限标志
    struct vm_area* next; 
} vm_area_t;
