/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <shizi/types.h>
#include <vfs.h>
#include <heap.h>

// 初始化
void exec_init(void);

/*
 * 加载可执行文件到指定的地址空间
 *
 * @param as 目标地址空间
 * @param path 可执行文件路径
 * @param pwd 当前工作目录
 * @param argv 命令行参数数组（以 NULL 结尾）
 * @param envp 环境变量数组（以 NULL 结尾）
 * @param stack_top 调用者分配的栈顶地址
 * @param out_stack_top 输出：布局后的栈顶地址
 *
 * @return 入口地址
 */
kuptr exec_load(
    as_t *as,
    const char *path,
    const struct path *pwd,
    const char **argv,
    const char **envp,
    uintptr_t stack_top,
    uintptr_t *out_stack_top
);