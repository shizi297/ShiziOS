/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

// 将函数指针放入初始化段，由 initcall 遍历调用
#define INITCALL(group, level, fn) \
    static void (*__initcall_##fn)(void) \
    __attribute__((section(".initcall_" #group "_" #level))) \
    __attribute__((used)) = fn

// 按优先级顺序调用该段内所有注册的函数
#define initcall(group, level) \
    do { \
        extern void (*__start_initcall_##group##_##level)(void); \
        extern void (*__stop_initcall_##group##_##level)(void); \
        void (**fn)(void); \
        for (fn = &__start_initcall_##group##_##level; \
             fn < &__stop_initcall_##group##_##level; fn++) { \
            (*fn)(); \
        } \
    } while(0)