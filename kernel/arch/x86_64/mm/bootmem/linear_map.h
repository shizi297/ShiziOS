/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */
 
#ifndef LINEAR_MAP_H
#define LINEAR_MAP_H

#include <stdint.h>
#include <stddef.h>
#include <mm/bootmem/bootmem.h>
#include <mm_addr.h>

/* 8TB需要的1GB页数 */
#define LINEAR_MAP_PAGES   (8ULL * 1024ULL)

void linear_map_setup(void);
temp_linear_map_t* linear_map_get_temp(void);

#endif /* LINEAR_MAP_H */