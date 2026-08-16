/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <kio.h>
#include <klibc.h>

#define TEST_K_ERR(res)      ((res).err)
#define TEST_K_IS_ERR(res)   (TEST_K_ERR(res) != 0)

struct test_result {
    int pass;   // 通过的断言数
    int fail;   // 失败的断言数
    int total;  // 测试步骤总数
};

/**
 * 定义测试用例
 *
 * @param func_name 测试函数名
 * @param cnt_var 步骤计数器变量名
 * @param exec_var 执行开关变量名
 * @param sig 函数签名参数列表(需要使用())
 * @param body 测试体(需要使用{})
 */
#define TEST_ENTRY(func_name, cnt_var, exec_var, sig, body) \
    static struct test_result func_name sig { \
        int cnt_var = 0; \
        int CONCAT(cnt_var, _total) = 0; \
        int exec_var = 0; \
        int __pass = 0; \
        int __fail = 0; \
        for ( \
            int __test_frame_loop = 0; \
            __test_frame_loop < 2; \
            __test_frame_loop++ \
        ) { \
            body; \
            if (__test_frame_loop == 0) { \
                CONCAT(cnt_var, _total) = cnt_var; \
                cnt_var = 0; \
                exec_var = 1; \
            } \
        } \
        return (struct test_result) { \
            .pass = __pass, \
            .fail = __fail, \
            .total = CONCAT(cnt_var, _total) \
        }; \
    }

/**
 * 定义测试步骤
 *
 * @param exec_var 执行开关
 * @param cnt_var 步骤计数器
 * @param block 副作用代码块(需要使用{})
 */
#define TEST_IMPL(exec_var, cnt_var, block) \
    do { \
        (cnt_var)++; \
        if (exec_var) { \
            block; \
        } \
    } while (0)
    
/**
 * 定义清理步骤
 *
 * @param do_run 执行开关
 * @param block 清理代码块(需要使用{}))
 */
#define TEST_CLEANUP(do_run, block) \
    do { \
        if (do_run) { \
            block; \
        } \
    } while (0)

/**
 * 输出步骤描述
 *
 * @param cnt_var 步骤计数器
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 */
#define TEST_DESC(cnt_var, fmt, ...) \
    do { \
        printk( \
            "[%d/%d] " fmt "\n", \
            cnt_var, \
            CONCAT(cnt_var, _total), \
            ##__VA_ARGS__ \
        ); \
    } while (0)

/**
 * 断言检查
 *
 * @param cond 条件表达式
 * @param fmt 失败信息格式串
 * @param ... 格式参数
 */
#define TEST_ASSERT(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            printk("[FAIL] " fmt "\n", ##__VA_ARGS__); \
            __fail++; \
        } else { \
            __pass++; \
        } \
    } while (0)

/**
 * 断言检查(失败时带进度)
 *
 * @param cnt_var 步骤计数器
 * @param cond 条件表达式
 * @param fmt 失败信息格式串
 * @param ... 格式参数
 */
#define TEST_ASSERT_STEP(cnt_var, cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            printk( \
                "[%d/%d] [FAIL] " fmt "\n", \
                cnt_var, \
                CONCAT(cnt_var, _total), \
                ##__VA_ARGS__ \
            ); \
            __fail++; \
        } else { \
            __pass++; \
        } \
    } while (0)