/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <list.h>
#include <shizi/types.h>
#include <vfs.h>
#include <heap.h>

// 解释器嵌套的最大深度限制
#define EXEC_MAX_DEPTH 4

// 辅助向量类型
typedef enum {
    AT_NULL     = 0,   // 结束标志
    AT_IGNORE   = 1,   // 忽略条目
    AT_EXECFD   = 2,   // 可执行文件的文件描述符
    AT_PHDR     = 3,   // 程序头表地址
    AT_PHENT    = 4,   // 程序头条目大小
    AT_PHNUM    = 5,   // 程序头条目数量
    AT_PAGESZ   = 6,   // 页大小
    AT_BASE     = 7,   // 解释器基地址
    AT_FLAGS    = 8,   // 标志位
    AT_ENTRY    = 9,   // 程序入口地址
    AT_NOTELF   = 10,  // 文件不是 ELF
    AT_UID      = 11,  // 真实 UID
    AT_EUID     = 12,  // 有效 UID
    AT_GID      = 13,  // 真实 GID
    AT_EGID     = 14,  // 有效 GID
    AT_PLATFORM = 15,  // 平台名称
    AT_HWCAP    = 16,  // CPU 硬件能力
    AT_CLKTCK   = 17,  // 时钟滴答频率
} auxv_type_t;

typedef struct {
    uint64_t type;
    uint64_t value;
} auxv_entry_t;

// 描述一个需要映射的内存段
struct exec_segment {
    uintptr_t vaddr;      // 虚拟起始地址
    size_t memsz;         // 内存大小
    size_t filesz;        // 文件中的实际数据大小
    uint64_t offset;      // 在文件中的偏移
    vm_prot_t prot;       // 内存权限
};

/*
 * 加载器返回给框架的加载信息
 * 框架根据这些信息执行映射、解释器加载和栈构造
 */
struct exec_loader_info {
    uintptr_t entry;        // 程序入口地址
    uintptr_t interp_base;  // 解释器加载基址
    char *interp_path;      // 解释器路径（NULL 表示静态链接）
    uintptr_t phdr;         // 程序头表虚拟地址
    uint32_t phnum;         // 程序头条目数量
    uint32_t phent;         // 程序头条目大小
    uint32_t is_elf;        // 0=ELF，非0=其他格式
    uint32_t count;         // 段数量
    struct exec_segment segments[];
};

/*
 * 加载器结构体
 * 每个支持的二进制格式注册一个
 */
struct binfmt {
    /*
     * 解析文件
     *
     * @param file 已打开的文件
     *
     * @return 程序的所有信息
     */
    struct exec_loader_info *(*load)(struct file *file);

    /*
     * 释放 load 分配的资源
     *
     * @param info 先前分配的程序信息指针
     */
    void (*destroy)(struct exec_loader_info *info);

    const char *name;
    struct list_head node;
};

/*
 * 注册一个加载器到框架
 *
 * @param fmt 加载器结构体指针
 */
void exec_register_binfmt(struct binfmt *fmt);

/*
 * 从框架注销一个加载器
 *
 * @param fmt 加载器结构体指针
 */
void exec_unregister_binfmt(struct binfmt *fmt);