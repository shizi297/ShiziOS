/* SPDX-License-Identifier: Apache-2.0 */
 
#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

#define MAX_CPUS 507
typedef uint64_t gdt_descriptor;

typedef struct {
    gdt_descriptor null;
    gdt_descriptor kernel_code;
    gdt_descriptor kernel_data;
    gdt_descriptor user_code;
    gdt_descriptor user_data;
    gdt_descriptor tss[max_cpus];
} __attribute__((aligned(4096))) gdt_t;

#endif // _GDT_H
