/* SPDX-License-Identifier: Apache-2.0 */
 
#include <bootboot.h>
#include <serial.h>
#include <mm/bootmem/linear_map.h>

static temp_linear_map_t temp_map = {0};
static uint64_t memory_base = 0;

/* 获取当前PML4 */
static uint64_t* get_pml4(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t*)(cr3 & ~0xFFF);
}

/* 在bootboot映射的16G内找2M内存 */
static uint64_t find_memory(uint64_t size) {
    BOOTBOOT* bootboot = (BOOTBOOT*)BOOTBOOT_INFO;
    MMapEnt* mmap = &bootboot->mmap;
    uint64_t count = (bootboot->size - 128) / sizeof(MMapEnt);
    
    for (uint64_t i = 0; i < count; i++) {
        MMapEnt* entry = &mmap[i];
        
        if (MMapEnt_Type(entry) != MMAP_FREE) continue;
        
        uint64_t start = MMapEnt_Ptr(entry);
        uint64_t size_avail = MMapEnt_Size(entry);
        
        if (start < (16ULL << 30) && size_avail >= size) {
            return start;
        }
    }
    
    panic("[linear_map] ERROR: No memory found\n");
}

/* 记录分配 */
static void record(uint64_t start_phys) {
    if (temp_map.count >= TEMP_RECORD_MAX) {
        panic("[linear_map] ERROR: Record full\n");
    }
    
    temp_map.start_pfn[temp_map.count] = start_phys >> 12;
    temp_map.count++;
}

/* 页分配器 */
static uint64_t physical_alloc_page(void) {
    if (memory_base == 0) {
        panic("[linear_map] ERROR: No memory base\n");
    }
    
    uint64_t alloc_addr;
    if (temp_map.count == 0) {
        alloc_addr = memory_base;
    } else {
        uint64_t last_pfn = temp_map.start_pfn[temp_map.count - 1];
        alloc_addr = (last_pfn + 1) << 12;
    }
    
    if (alloc_addr + 4096 > memory_base + 2 * 1024 * 1024) {
        panic("[linear_map] ERROR: 2MB full\n");
    }
    
    /* 清空页面 */
    uint64_t* page = (uint64_t*)alloc_addr;
    for (uint64_t j = 0; j < PAGE_TABLE_ENTRIES; j++) {
        page[j] = 0;
    }
    
    record(alloc_addr);
    return alloc_addr;
}

/*
 * 建立8TB线性映射
 * 物理0-8TB -> LINEAR_MAP_START开始
 * 用1GB大页
 */
void linear_map_setup(void) {
    serial_puts("[linear_map] Setting up mapping\n");
    
    memory_base = find_memory(2 * 1024 * 1024);
    if (!memory_base) return;
    
    serial_puts("[linear_map] Found: ");
    serial_put_hex(memory_base);
    serial_puts("\n");
    
    uint64_t* pml4 = get_pml4();
    uint64_t pml4_idx = PML4_INDEX(LINEAR_MAP_START);
    
    /* 分配16个PDPT */
    for (uint64_t i = 0; i < 16; i++) {
        if (!(pml4[pml4_idx + i] & PAGE_PRESENT)) {
            uint64_t pdpt_phys = physical_alloc_page();
            if (!pdpt_phys) return;
            
            pml4[pml4_idx + i] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        }
    }
    
    /* 1GB大页映射 */
    for (uint64_t i = 0; i < LINEAR_MAP_PAGES; i++) {
        uint64_t virt = LINEAR_MAP_START + (i * PAGE_1GB_SIZE);
        uint64_t phys = i * PAGE_1GB_SIZE;
        
        uint64_t current_pml4_idx = PML4_INDEX(virt);
        uint64_t* current_pdpt = (uint64_t*)(pml4[current_pml4_idx] & ~0xFFF);
        uint64_t pdpt_idx = PDPT_INDEX(virt);
        
        current_pdpt[pdpt_idx] = phys | PAGE_1GB_FLAGS;
    }
    
    /* 刷新TLB */
    __asm__ __volatile__("mov %%cr3, %%rax\nmov %%rax, %%cr3" : : : "rax", "memory");
    
    serial_puts("[linear_map] Mapping done\n");
}

/* 获取分配记录 */
temp_linear_map_t* linear_map_get_temp(void) {
    return &temp_map;
}