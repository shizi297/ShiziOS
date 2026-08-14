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

    .initcall : ALIGN(8) {
        __start_initcall_fs_0 = .;
        KEEP(*(.initcall_fs_0))
        __stop_initcall_fs_0 = .;
    
        __start_initcall_kthreadtest_0 = .;
        KEEP(*(.initcall_kthreadtest_0))
        __stop_initcall_kthreadtest_0 = .;
    
        __start_initcall_drivers_0 = .;
        KEEP(*(.initcall_drivers_0))
        __stop_initcall_drivers_0 = .;
    
        __start_initcall_exec_0 = .;
        KEEP(*(.initcall_exec_0))
        __stop_initcall_exec_0 = .;
    }

    .data : {
        *(.data .data.*)
        . = ALIGN(8);
        PROVIDE(__stack_chk_guard = .);
        LONG(0x595e9fbd94fda766);
    } :all

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :all

    /DISCARD/ : { 
        *(.eh_frame) 
        *(.comment) 
    }
}