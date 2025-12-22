ENTRY(_start)

mmio        = 0xfffffffff8000000;
fb          = 0xfffffffffc000000;
bootboot    = 0xffffffffffe00000;
environment = 0xffffffffffe01000;

PHDRS
{
  all PT_LOAD FLAGS(7);    
}

SECTIONS
{
    . = 0xffffffffffe02000;
    
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :all

    .rodata : {
        *(.rodata .rodata.*)
    } :all

    .data : {
        *(.data .data.*)
    } :all

    .bss : {  
        . = ALIGN(16);
        *(COMMON)
        *(.bss .bss.*)
        
        . = ALIGN(16);
        kernel_stack_bottom = .;
        . += 64K;
        kernel_stack_top = .;
    } :all

    /DISCARD/ : { 
        *(.eh_frame) 
        *(.comment) 
    }
}