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