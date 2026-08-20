/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>

/*
 * 异常表条目
 */
struct extable_entry {
    uintptr_t insn;   // 可能触发异常的指令地址
    uintptr_t fixup;  // 修复地址
};

/*
 * 向异常表添加条目
 *
 * @param insn 可能触发异常的指令标签
 * @param fixup 修复地址标签
 */
#define EXTABLE_ADD(insn, fixup) \
    ".pushsection __ex_table, \"a\"\n" \
    ".balign 16\n" \
    ".quad " #insn "\n" \
    ".quad " #fixup "\n" \
    ".popsection\n"
    

/*
 * 在异常表中查找指定指令地址的条目
 *
 * @param addr 触发异常的指令地址
 *
 * @return 异常表条目指针
 */
static inline const struct extable_entry *extable_search(uintptr_t addr) {
    extern const struct extable_entry __ex_table_start[];
    extern const struct extable_entry __ex_table_end[];
    const struct extable_entry *start = __ex_table_start;
    const struct extable_entry *end = __ex_table_end;

    while (start < end) {
        if (start->insn == addr)
            return start;
        start++;
    }
    return NULL;
}