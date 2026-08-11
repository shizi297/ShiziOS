/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

// ELF 魔数
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// 文件位宽类型
typedef enum : uint8_t {
    ELFCLASSNONE = 0, // 无效
    ELFCLASS32   = 1, // 32 位
    ELFCLASS64   = 2, // 64 位
} elf_class_t;

// 字节序
typedef enum : uint8_t {
    ELFDATANONE = 0, // 无效
    ELFDATA2LSB = 1, // 小端序
    ELFDATA2MSB = 2, // 大端序
} elf_data_t;

// 目标操作系统 ABI
typedef enum : uint8_t {
    ELFOSABI_NONE    = 0, // 无特定 ABI
    ELFOSABI_SYSV    = 0, 
} elf_osabi_t;

// 文件类型
typedef enum : uint16_t {
    ET_NONE = 0, // 无类型
    ET_REL  = 1, // 可重定位文件
    ET_EXEC = 2, // 可执行文件
    ET_DYN  = 3, // 共享对象/PIE
} elf_type_t;

// 目标指令集
typedef enum : uint16_t {
    EM_NONE   = 0,  // 未指定
    EM_X86_32 = 3,  // x86 32 位
    EM_X86_64 = 62, // x86-64
} elf_machine_t;

// 程序头类型
typedef enum : uint32_t {
    PT_NULL      = 0,          // 未使用
    PT_LOAD      = 1,          // 可加载段
    PT_DYNAMIC   = 2,          // 动态链接信息
    PT_INTERP    = 3,          // 动态链接器路径
    PT_NOTE      = 4,          // 辅助信息
    PT_PHDR      = 6,          // 程序头表自身
    PT_GNU_STACK = 0x6474e551, // 栈权限控制
    PT_GNU_RELRO = 0x6474e552, // 只读重定位区域
} elf_phdr_type_t;

// 段权限
typedef enum : uint32_t {
    PF_X = 1U << 0, // 可执行
    PF_W = 1U << 1, // 可写
    PF_R = 1U << 2, // 可读
} elf_phdr_flags_t;

// 节头类型
typedef enum : uint32_t {
    SHT_NULL     = 0, // 未使用
    SHT_PROGBITS = 1, // 程序数据
    SHT_SYMTAB   = 2, // 符号表
    SHT_STRTAB   = 3, // 字符串表
    SHT_RELA     = 4, // 重定位条目
    SHT_NOBITS   = 8, // 不占文件空间
} elf_shdr_type_t;

// x86-64 重定位类型
typedef enum : uint32_t {
    R_X86_64_NONE     = 0, // 无重定位
    R_X86_64_RELATIVE = 8, // 基址重定位
} elf_rela_type_t;

// ELF 文件头
struct elf_ehdr {
    struct {
        uint8_t magic[4];    
        elf_class_t class;    // 文件位宽
        elf_data_t data;      // 字节序
        uint8_t version;      // ELF 规范版本
        elf_osabi_t osabi;    // 目标操作系统 ABI
        uint8_t abiversion;   // ABI 版本号
        uint8_t pad[7];       // 保留填充
    } e_ident;                // 魔数和元信息
    elf_type_t e_type;        // 文件类型
    elf_machine_t e_machine;  // 目标指令集
    uint32_t e_version;       // ELF 规范版本
    uint64_t e_entry;         // 入口虚拟地址
    uint64_t e_phoff;         // 程序头表文件偏移
    uint64_t e_shoff;         // 节头表文件偏移
    uint32_t e_flags;         // 架构特定标志
    uint16_t e_ehsize;        // ELF 头大小
    uint16_t e_phentsize;     // 程序头条目大小
    uint16_t e_phnum;         // 程序头条目数量
    uint16_t e_shentsize;     // 节头条目大小
    uint16_t e_shnum;         // 节头条目数量
    uint16_t e_shstrndx;      // 节字符串表索引
} __attribute__((packed));

// 程序头
struct elf_phdr {
    elf_phdr_type_t p_type;  // 类型
    elf_phdr_flags_t p_flags; // 权限
    uint64_t p_offset;        // 段数据在文件中的字节偏移
    uint64_t p_vaddr;         // 目标虚拟地址
    uint64_t p_paddr;         // 物理地址
    uint64_t p_filesz;        // 段在文件中的字节数
    uint64_t p_memsz;         // 段在内存中的字节数
    uint64_t p_align;         // 对齐要求
} __attribute__((packed));

// 节头
struct elf_shdr {
    uint32_t sh_name;          // 节名索引
    elf_shdr_type_t sh_type;   // 类型
    uint64_t sh_flags;         // 节属性标志
    uint64_t sh_addr;          // 虚拟地址
    uint64_t sh_offset;        // 文件偏移
    uint64_t sh_size;          // 节大小
    uint32_t sh_link;          // 关联节索引
    uint32_t sh_info;          // 额外信息
    uint64_t sh_addralign;     // 对齐要求
    uint64_t sh_entsize;       // 条目大小
};

// 重定位条目
struct elf_rela {
    uint64_t r_offset; // 重定位目标偏移
    union {
        uint64_t raw;          
        struct {
            uint32_t type;     // 重定位类型
            uint32_t sym;      // 符号索引
        } info;
    };
    int64_t r_addend;  // 加数
} __attribute__((packed));