/* SPDX-License-Identifier: Apache-2.0 */

#include <kernel.h>
#include <serial.h>  
#include "mm/init.h"

__attribute__((noreturn))
void kernel_main(void) {
    serial_puts("[KERNEL]ShiziOS KERNEL v");
    serial_puts(KERNEL_VERSION);
    serial_puts("\n");
    
    memory_init();
  
    while (1) {
        __asm__ __volatile__("hlt");
    }
}