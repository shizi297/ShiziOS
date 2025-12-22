/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>

// Zone 类型
#define ZONE_DMA     0
#define ZONE_DMA32   1    
#define ZONE_NORMAL  2    

#define MAX_ORDER 11
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#include "pmm_types.h"

#endif // PMM_H

