/* SPDX-License-Identifier: Apache-2.0 */

#include "mm/init.h"
#include <processor.h>
#include <kernel.h>
#include <serial.h>  

__attribute__((noreturn))
void kernel_main(void) {
    serial_puts("[KERNEL]ShiziOS KERNEL v");
    serial_puts(KERNEL_VERSION);
    serial_puts("\n");
    
    memory_init();

    processor_init();
    uint64_t *gdt_temp_addr = get_gdt_temp();
    struct idt_gate* idt_temp_addr = get_idt_temp();
    struct tss* tss_temp_addr = get_tss_temp(); 
  
    while (1) {
        __asm__ __volatile__("hlt");
    }
}