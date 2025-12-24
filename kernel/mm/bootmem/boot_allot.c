/* SPDX-License-Identifier: Apache-2.0 */
 
#include <bootboot.h>
#include <serial.h>
#include <mm/bootmem/boot_allot.h>
#include <mm/bootmem/linear_map.h>

static uint8_t* bitmap = NULL;
static uint64_t bitmap_phys = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static size_t bitmap_pages = 0;

// 位图操作
#define BITMAP_SET(bit)   (bitmap[(bit) / 8] |= (1 << ((bit) % 8)))
#define BITMAP_CLEAR(bit) (bitmap[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BITMAP_TEST(bit)  (bitmap[(bit) / 8] & (1 << ((bit) % 8)))

static void set_bitmap_region(uint64_t start_pfn, uint64_t count, int used)
{
    for (uint64_t i = 0; i < count; i++) {
        if (used) {
            BITMAP_SET(start_pfn + i);
        } else {
            BITMAP_CLEAR(start_pfn + i);
        }
    }
}

void boot_alloc_init(void)
{
    serial_puts("[boot_alloc] Initializing\n");
    
    // 通过临时记录结构体分配位图内存
    temp_linear_map_t* temp_map = linear_map_get_temp();
    
    if (temp_map->count == 0) {
        panic("[boot_alloc] Error: linear map must be initialized first\n");
    }
    
    // 计算需要的页数
    bitmap_pages = (BOOT_ALLOC_BITMAP_SIZE + 4095) / 4096;
    uint64_t last_pfn = temp_map->start_pfn[temp_map->count - 1];
    bitmap_phys = (last_pfn + 1) << 12;
    
    // 记录位图页面
    for (size_t i = 0; i < bitmap_pages; i++) {
        if (temp_map->count >= TEMP_RECORD_MAX) {
            panic("[boot_alloc] Error: temporary record full\n");
        }
        temp_map->start_pfn[temp_map->count++] = (bitmap_phys >> 12) + i;
    }
    
    bitmap = (uint8_t*)bitmap_phys;
    
    for (size_t i = 0; i < BOOT_ALLOC_BITMAP_SIZE; i++) {
        bitmap[i] = 0xFF;
    }
    
    // 遍历内存映射，标记空闲区域
    BOOTBOOT* bootboot = (BOOTBOOT*)BOOTBOOT_INFO;
    MMapEnt* mmap = &bootboot->mmap;
    uint64_t count = (bootboot->size - 128) / sizeof(MMapEnt);
    
    for (uint64_t i = 0; i < count; i++) {
        MMapEnt* entry = &mmap[i];
        uint64_t start = MMapEnt_Ptr(entry);
        uint64_t size = MMapEnt_Size(entry);
        uint64_t type = MMapEnt_Type(entry);
        
        if (start >= (1ULL << 30)) continue;
        if (type != 1) continue; // 只处理空闲内存
        
        uint64_t start_pfn = start >> 12;
        uint64_t end_pfn = (start + size) >> 12;
        if (end_pfn > BOOT_ALLOC_MAX_PAGES) {
            end_pfn = BOOT_ALLOC_MAX_PAGES;
        }
        
        if (end_pfn > start_pfn) {
            uint64_t page_count = end_pfn - start_pfn;
            set_bitmap_region(start_pfn, page_count, 0);
            free_pages += page_count;
        }
    }
    
    total_pages = BOOT_ALLOC_MAX_PAGES;
    
    // 标记位图自身占用的页面
    uint64_t bitmap_start_pfn = bitmap_phys >> 12;
    set_bitmap_region(bitmap_start_pfn, bitmap_pages, 1);
    free_pages -= bitmap_pages;
    
    // 标记临时记录结构体中已有的分配（线性映射的页表等）
    for (uint32_t i = 0; i < temp_map->count; i++) {
        uint64_t pfn = temp_map->start_pfn[i];
        
        // 跳过位图自身的页面
        if (pfn >= bitmap_start_pfn && pfn < (bitmap_start_pfn + bitmap_pages)) {
            continue;
        }
        
        // 标记这些页面为已使用
        if (pfn < BOOT_ALLOC_MAX_PAGES && !BITMAP_TEST(pfn)) {
            BITMAP_SET(pfn);
            free_pages--;
        }
    }
    
    serial_puts("[boot_alloc] Ready: ");
    serial_put_dec(free_pages);
    serial_puts(" free pages\n");
}

void* boot_alloc(size_t pages)
{
    if (pages == 0 || bitmap == NULL) {
        return NULL;
    }
    
    // 搜索连续空闲页
    uint64_t start_pfn = 0;
    uint64_t consecutive = 0;
    
    for (uint64_t i = 0; i < BOOT_ALLOC_MAX_PAGES; i++) {
        if (!BITMAP_TEST(i)) {
            consecutive++;
            if (consecutive == pages) {
                start_pfn = i - pages + 1;
                break;
            }
        } else {
            consecutive = 0;
        }
    }
    
    if (consecutive < pages) {
        serial_puts("[boot_alloc] Allocation failed: ");
        serial_put_dec(pages);
        serial_puts(" pages\n");
        return NULL;
    }
    
    // 标记为已使用
    set_bitmap_region(start_pfn, pages, 1);
    free_pages -= pages;
    
    // 返回线性映射虚拟地址
    uint64_t phys_addr = start_pfn << 12;
    return (void*)(LINEAR_MAP_START + phys_addr);
}

void boot_alloc_info(void)
{
    serial_puts("[boot_alloc] Memory: ");
    serial_put_dec(free_pages);
    serial_puts("/");
    serial_put_dec(total_pages);
    serial_puts(" pages free\n");
}

void* boot_alloc_get_bitmap(void)
{
    if (bitmap_phys == 0) {
        return NULL;
    }
    
    // 返回位图的线性映射虚拟地址
    return (void*)(LINEAR_MAP_START + bitmap_phys);
}