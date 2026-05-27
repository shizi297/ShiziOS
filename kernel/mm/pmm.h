/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// Zone 类型
#define ZONE_DMA     0
#define ZONE_DMA32   1    
#define ZONE_NORMAL  2    

#define MAX_ORDER 16
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define CACHE_LINE_SIZE 64

#ifndef NO_PMM
    #include "pmm_types.h"
#endif
