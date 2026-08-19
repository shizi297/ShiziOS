/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <uaccess.h>
#include <string.h>

/*
 * 从用户空间复制数据到内核空间
 *
 * @param to 内核空间目标地址
 * @param from 用户空间源地址
 * @param n 复制字节数
 */
int uaccess_copy_from_user(void *to, const void __uptr *from, size_t n) {
    if (!uaccess_uaddr_ok(from, n))
        return -EFAULT;

    return UACCESS_COPY_FROM_USER(to, from, n);
}

/*
 * 从内核空间复制数据到用户空间
 *
 * @param to 用户空间目标地址
 * @param from 内核空间源地址
 * @param n 复制字节数
 */
int uaccess_copy_to_user(void __uptr *to, const void *from, size_t n) {
    if (!uaccess_uaddr_ok(to, n))
        return -EFAULT;

    return UACCESS_COPY_TO_USER(to, from, n);
}

/*
 * 从用户空间复制字符串到内核空间
 *
 * @param dst 内核空间目标地址
 * @param src 用户空间源地址
 * @param max 最大复制字节数
 *
 * @return 字符串长度（不含 '\0'）
 */
int uaccess_strncpy_from_user(char *dst, const char __uptr *src, size_t max) {
    if (!uaccess_uaddr_ok(src, max))
        return -EFAULT;

    for (size_t i = 0; i < max; i++) {
        char c;
        int ret = uaccess_get_user(c, src + i);
        if (ret < 0)
            return ret;
        dst[i] = c;
        if (c == '\0')
            return i;
    }
    return -EFAULT;
}

/*
 * 获取用户空间字符串长度
 *
 * @param s 用户空间字符串地址
 * @param max 最大检查长度
 *
 * @return 字符串长度（不含 '\0'）
 */
int uaccess_strnlen_user(const char __uptr *s, size_t max) {
    if (!uaccess_uaddr_ok(s, max))
        return -EFAULT;

    for (size_t i = 0; i < max; i++) {
        char c;
        int ret = uaccess_get_user(c, s + i);
        if (ret < 0)
            return ret;
        if (c == '\0')
            return i;
    }
    return -EFAULT;
}