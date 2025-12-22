; SPDX-License-Identifier: Apache-2.0

section .text
global _start
extern kernel_main

_start:
    ; 核心0执行内核初始化，其他核心等待唤醒 
    test rdi, rdi
    jnz .ap_wait
    
    ; 核心0跳转到内核入口点
    call kernel_main
    jmp .halt

.ap_wait:
    pause
    jmp .ap_wait

.halt:
    hlt
    jmp .halt