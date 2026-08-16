/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <spinlock.h>
#include <dynarr.h>
#include <klibc.h>
#include <vfs.h>

struct files_descriptor;

/*
 * 分配一个新的文件描述符表
 *
 * @return 文件描述符表指针
 */
struct files_descriptor *task_files_alloc(void);

/*
 * 增加文件描述符表的引用计数
 *
 * @param fdt 文件描述符表
 */
void task_files_get(struct files_descriptor *fdt);

/*
 * 减少文件描述符表的引用计数，归零时释放
 *
 * @param fdt 文件描述符表
 */
void task_files_put(struct files_descriptor *fdt);

/*
 * 分配一个空闲的文件描述符编号
 *
 * @param fdt 文件描述符表
 * @param out_fd 输出分配到的 fd
 */
int task_files_alloc_fd(struct files_descriptor *fdt, uint32_t *out_fd);

/*
 * 将文件对象安装到指定的文件描述符编号
 *
 * @param fdt 文件描述符表
 * @param fd 文件描述符编号
 * @param file 文件对象指针
 */
int task_files_install(struct files_descriptor *fdt, int fd, struct file *file);

/*
 * 通过文件描述符编号获取文件对象
 *
 * @param fdt 文件描述符表
 * @param fd 文件描述符编号
 *
 * @return 文件对象指针（已添加引用）
 */
struct file *task_files_open(struct files_descriptor *fdt, int fd);

/*
 * 关闭文件描述符
 *
 * @param fdt 文件描述符表
 * @param fd 文件描述符编号
 */
int task_files_close(struct files_descriptor *fdt, int fd);

/*
 * 从源文件描述符表中复制指定 fd 列表到目标表
 *
 * @param dst 目标文件描述符表
 * @param src 源文件描述符表
 * @param fd_list 要复制的 fd 编号数组
 * @param fd_count 数组长度
 */
int task_files_copy_list(
    struct files_descriptor *dst,
    struct files_descriptor *src,
    int *fd_list,
    uint32_t fd_count
);