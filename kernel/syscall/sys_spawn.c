/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <task.h>
#include <klibc.h>
#include <uaccess.h>
#include <heap.h>

#define SYS_SPAWN_PATH_MAX  4096

/*
 * 复制用户空间的字符串数组到内核空间
 *
 * @param user_array 用户空间字符串数组（以 NULL 结尾）
 * @param count_out 输出数组元素个数
 *
 * @return 内核空间字符串数组
 */
static char **copy_user_string_array(const char __uptr *const __uptr *user_array, size_t *count_out) {
    size_t count = 0;
    char **kernel_array = NULL;
    const char __uptr *str;
    int ret;

    // 遍历用户空间数组，计算有效字符串数量
    while (1) {
        ret = uaccess_get_user(str, &user_array[count]);
        if (ret < 0)
            return NULL;
        if (str == NULL)
            break;
        count++;
        if (count > 1024)
            return NULL;
    }

    // 分配内核指针数组（含 NULL 结尾）
    kernel_array = kheap_alloc((count + 1) * sizeof(char *));
    if (!kernel_array)
        return NULL;

    // 逐个复制字符串内容
    for (size_t i = 0; i < count; i++) {
        ret = uaccess_get_user(str, &user_array[i]);
        if (ret < 0)
            goto fail;

        size_t len = uaccess_strnlen_user(str, PAGE_SIZE);
        if (len < 0)
            goto fail;

        char *buf = kheap_alloc(len + 1);
        if (!buf)
            goto fail;

        ret = uaccess_copy_from_user(buf, str, len + 1);
        if (ret < 0) {
            kheap_free(buf);
            goto fail;
        }
        kernel_array[i] = buf;
    }
    kernel_array[count] = NULL;
    *count_out = count;
    return kernel_array;

fail:
    for (size_t i = 0; i < count; i++) {
        if (kernel_array[i])
            kheap_free(kernel_array[i]);
    }
    kheap_free(kernel_array);
    return NULL;
}

/*
 * 释放 copy_user_string_array 分配的内存
 *
 * @param arr 内核字符串数组
 * @param count 数组长度
 */
static void free_kernel_string_array(char **arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; i++) {
        if (arr[i])
            kheap_free(arr[i]);
    }
    kheap_free(arr);
}

/*
 * 创建新进程或线程
 *
 * @param attrs 用户空间 task_attrs 结构体指针
 * @param size 结构体大小
 *
 * @return 子进程 PID
 */
__err pid_t sys_spawn(struct task_attrs __uptr *user_attrs, size_t size) {
    struct task_attrs attrs;
    int err = 0;
    int ret;

    if (size < sizeof(struct task_attrs))
        return -EINVAL;

    ret = uaccess_copy_from_user(&attrs, user_attrs, sizeof(struct task_attrs));
    if (ret < 0)
        return ret;

    if (attrs.flags & ~(TASK_IS_THREAD | TASK_WAIT_PARENT | TASK_WAKE_ON_EXIT))
        return -EINVAL;

    int *kernel_fds = NULL;
    if (attrs.fd_count > 0) {
        if (!attrs.inherit_fds)
            return -EINVAL;
        kernel_fds = kheap_alloc(attrs.fd_count * sizeof(int));
        if (!kernel_fds)
            return -ENOMEM;
        ret = uaccess_copy_from_user(kernel_fds, attrs.inherit_fds, attrs.fd_count * sizeof(int));
        if (ret < 0) {
            kheap_free(kernel_fds);
            return ret;
        }
        attrs.inherit_fds = kernel_fds;
    } else {
        attrs.inherit_fds = NULL;
    }

    char **kernel_argv = NULL;
    char **kernel_envp = NULL;
    size_t argv_count = 0, envp_count = 0;
    char *path_buf = NULL;

    // 进程模式：从用户空间复制 exec_path、argv、envp
    if (!(attrs.flags & TASK_IS_THREAD)) {
        if (attrs.process.exec_path) {
            size_t len = uaccess_strnlen_user(attrs.process.exec_path, SYS_SPAWN_PATH_MAX);
            if (len < 0 || len == 0 || len >= SYS_SPAWN_PATH_MAX) {
                err = (len < 0) ? -EFAULT : -ENAMETOOLONG;
                goto cleanup;
            }
            path_buf = kheap_alloc(len + 1);
            if (!path_buf) {
                err = -ENOMEM;
                goto cleanup;
            }
            ret = uaccess_copy_from_user(path_buf, attrs.process.exec_path, len + 1);
            if (ret < 0) {
                kheap_free(path_buf);
                err = ret;
                goto cleanup;
            }
            attrs.process.exec_path = path_buf;
        }

        if (attrs.process.argv) {
            kernel_argv = copy_user_string_array(attrs.process.argv, &argv_count);
            if (!kernel_argv) {
                err = -EFAULT;
                goto cleanup;
            }
            attrs.process.argv = (const char **)kernel_argv;
        }

        if (attrs.process.envp) {
            kernel_envp = copy_user_string_array(attrs.process.envp, &envp_count);
            if (!kernel_envp) {
                err = -EFAULT;
                goto cleanup;
            }
            attrs.process.envp = (const char **)kernel_envp;
        }
    }

    kptr task_res = task_create_new(&attrs, sizeof(struct task_attrs));
    K_ERR_LABEL_AND_SAVE(task_res, cleanup, err);

    struct task_struct *task = (struct task_struct *)task_res.ptr;
    pid_t child_pid = task->pid;

cleanup:
    if (kernel_fds)
        kheap_free(kernel_fds);
    if (kernel_argv)
        free_kernel_string_array(kernel_argv, argv_count);
    if (kernel_envp)
        free_kernel_string_array(kernel_envp, envp_count);
    if (path_buf)
        kheap_free(path_buf);

    if (err)
        return err;
    return child_pid;
}