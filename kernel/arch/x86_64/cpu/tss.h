/* SPDX-License-Identifier: Apache-2.0 */
 
#ifndef _TSS_H
#define _TSS_H

#include <stdint.h>

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;     
    uint64_t rsp1;     
    uint64_t rsp2;     
    uint64_t reserved1;
    uint64_t ist[8];   
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;  

#endif // _TSS_H