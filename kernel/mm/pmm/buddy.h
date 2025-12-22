/* SPDX-License-Identifier: Apache-2.0 */
 
#ifndef BUDDY_H
#define BUDDY_H

void pmm_init(void);

uint64_t pmm_alloc_pages(uint8_t order, uint8_t zone);

void pmm_free_pages(uint64_t pfn);

#endif 