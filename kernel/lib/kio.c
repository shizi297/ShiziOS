/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#include <kio.h>
#include <asm/serial.h>
#include <minmax.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <shizi/types.h>

// 回调类型，用于函数进行输出文字
typedef void (*kio_putchar_t)(char c);

// 内部缓冲区大小，用于数字转换的临时存储
#define TMP_BUF_SIZE 128

static inline size_t print_strlen(const char *s) {
    extern size_t strlen(const char *);
    return strlen(s);
}

static inline char *format_dec(uint64_t n, char *end) {
    do {
        *--end = '0' + (n % 10);
        n /= 10;
    } while (n);
    return end;
}

static inline char *format_oct(uint64_t n, char *end) {
    do {
        *--end = '0' + (n & 7);
        n >>= 3;
    } while (n);
    return end;
}

static inline char *format_hex(uint64_t n, char *end, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        *--end = digits[n & 0xF];
        n >>= 4;
    } while (n);
    return end;
}

#define FLAG_ALT   (1 << 0)   // '#'
#define FLAG_ZERO  (1 << 1)   // '0'
#define FLAG_LEFT  (1 << 2)   // '-'
#define FLAG_POS   (1 << 3)   // '+'
#define FLAG_SPACE (1 << 4)   // ' '

enum len_mod {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_Z,
    LEN_T,
    LEN_J,
};

// 格式说明符解析结果
struct fmt_spec {
    int flags;
    int width;
    int prec;
    enum len_mod len_mod;
    char conv;
};

/**
 * 解析数字或 '*' 参数。
 * 如果当前字符为 '*'
 * 从可变参数列表获取 int 值并返回
 * 否则解析十进制数字串并返回其整数值
 * 
 * @param p 指向格式串当前解析位置的指针，解析过程中会前移
 * @param args 可变参数列表
 * 
 * @return 解析得到的整数值
 */
static int parse_int_or_star(const char **p, va_list args) {
    if (**p == '*') {
        (*p)++;
        return va_arg(args, int);
    }
    int val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val;
}

/**
 * 解析完整的 % 格式说明符
 * 从当前字符开始解析
 * 结果存入 spec
 * 并将 *p 更新到转换符
 * 
 * @param p 指向格式串当前解析位置的指针，解析中会前移
 * @param args 可变参数列表
 * @param spec 存放解析出的格式说明
 */
static void parse_fmt_spec(const char **p, va_list args, struct fmt_spec *spec) {
    spec->flags = 0;
    spec->width = -1;
    spec->prec = -1;
    spec->len_mod = LEN_NONE;
    spec->conv = 0;

    // 标志
    while (1) {
        char c = *++(*p);
        if (c == '#') {
            spec->flags |= FLAG_ALT;
        } else if (c == '0') {
            spec->flags |= FLAG_ZERO;
        } else if (c == '-') {
            spec->flags |= FLAG_LEFT;
        } else if (c == '+') {
            spec->flags |= FLAG_POS;
        } else if (c == ' ') {
            spec->flags |= FLAG_SPACE;
        } else {
            break;
        }
    }

    // 宽度
    spec->width = parse_int_or_star(p, args);
    if (spec->width < 0) {
        spec->flags |= FLAG_LEFT;
        spec->width = -spec->width;
    }

    // 精度
    if (**p == '.') {
        (*p)++;
        int pr = parse_int_or_star(p, args);
        if (pr >= 0) {
            spec->prec = pr;
        }
    }

    // 长度修饰符
    if (**p == 'h') {
        (*p)++;
        if (**p == 'h') {
            spec->len_mod = LEN_HH;
            (*p)++;
        } else {
            spec->len_mod = LEN_H;
        }
    } else if (**p == 'l') {
        (*p)++;
        if (**p == 'l') {
            spec->len_mod = LEN_LL;
            (*p)++;
        } else {
            spec->len_mod = LEN_L;
        }
    } else if (**p == 'z') {
        spec->len_mod = LEN_Z;
        (*p)++;
    } else if (**p == 't') {
        spec->len_mod = LEN_T;
        (*p)++;
    } else if (**p == 'j') {
        spec->len_mod = LEN_J;
        (*p)++;
    }

    // 转换说明符
    spec->conv = *(*p)++;
    (*p)--;
}

/**
 * 将绝对值转为显示用的数字字符串和替代前缀
 * 
 * @param abs_val 整数的绝对值
 * @param conv 转换说明符 
 * @param prec 精度值（-1 表示未指定）
 * @param flags 格式标志位
 * @param tmp 临时缓冲区，用于生成原始数字串
 * @param digits_buf 存放精度补零后的数字串
 * @param digits 指向最终数字字符串
 * @param prefix 前缀缓冲区（至少 3 字节），以 '\0' 结束
 * @param has_prefix 表示是否有替代前缀
 */
static void prepare_number(
    uint64_t abs_val,
    char conv,
    int prec,
    int flags,
    char *tmp,
    char *digits_buf,
    char **digits,
    char *prefix,
    bool *has_prefix
) {
    bool zero_val = (abs_val == 0);
    bool upper = (conv == 'X');
    char *end = tmp + TMP_BUF_SIZE - 1;
    *end = '\0';
    char *raw_digits;

    if (conv == 'o') {
        raw_digits = format_oct(abs_val, end);
    } else if (conv == 'x' || conv == 'X') {
        raw_digits = format_hex(abs_val, end, upper);
    } else {
        raw_digits = format_dec(abs_val, end);
    }

    // 精度处理
    int min_digits = (prec >= 0) ? prec : 1;
    if (prec == 0 && zero_val) {
        digits_buf[0] = '\0';
        *digits = digits_buf;
    } else {
        int raw_len = end - raw_digits;
        int zeros = max(0, min_digits - raw_len);
        char *d = digits_buf;
        for (int i = 0; i < zeros; i++) {
            *d++ = '0';
        }
        for (int i = 0; i < raw_len; i++) {
            *d++ = raw_digits[i];
        }
        *d = '\0';
        *digits = digits_buf;
    }

    // 替代前缀
    *has_prefix = false;
    prefix[0] = '\0';
    if ((flags & FLAG_ALT) && (conv == 'o' || conv == 'x' || conv == 'X')) {
        if (conv == 'o') {
            if (zero_val && prec == 0) {
                // #.0o 且值为 0：强制输出 "0"
                digits_buf[0] = '0';
                digits_buf[1] = '\0';
                *digits = digits_buf;
            } else {
                if ((*digits)[0] != '0') {
                    prefix[0] = '0';
                    prefix[1] = '\0';
                    *has_prefix = true;
                }
            }
        } else { 
            // 16 进制
            if (!zero_val) {
                prefix[0] = '0';
                prefix[1] = upper ? 'X' : 'x';
                prefix[2] = '\0';
                *has_prefix = true;
            }
        }
    }
}

/**
 * 通过回调输出文字
 * 
 * @param put 输出字符的回调函数
 * @param written 指向局部输出字符计数器的指针，每输出一个字符递增
 * @param sign_char 符号字符（'\0' 表示无符号）
 * @param prefix 替代前缀（NULL 表示无前缀）
 * @param digits 精度补零后的数字字符串
 * @param width 最小字段宽度（-1 表示未指定）
 * @param flags 格式标志位
 * @param prec 精度值（-1 表示未指定精度）
 */
static void output_int(
    kio_putchar_t put,
    int *written,
    char sign_char,
    const char *prefix,
    const char *digits,
    int width,
    int flags,
    int prec
) {
    int sign_len = sign_char ? 1 : 0;
    int prefix_len = prefix ? print_strlen(prefix) : 0;
    int digits_len = print_strlen(digits);
    int content_len = sign_len + prefix_len + digits_len;

    if (width <= content_len) {
        // 无填充
        if (sign_char) {
            put(sign_char);
            (*written)++;
        }

        if (prefix) {
            for (int i = 0; prefix[i]; i++) {
                put(prefix[i]);
                (*written)++;
            }
        }

        for (int i = 0; digits[i]; i++) {
            put(digits[i]);
            (*written)++;
        }
        
        return;
    }

    int pad = max(0, width - content_len);

    if (flags & FLAG_LEFT) {
        // 左对齐：内容在前，空格在后
        if (sign_char) {
            put(sign_char);
            (*written)++;
        }

        if (prefix) {
            for (int i = 0; prefix[i]; i++) {
                put(prefix[i]);
                (*written)++;
            }
        }
        
        for (int i = 0; digits[i]; i++) {
            put(digits[i]);
            (*written)++;
        }

        for (int i = 0; i < pad; i++) {
            put(' ');
            (*written)++;
        }

        return;
    }

    bool zero_pad = (flags & FLAG_ZERO) && (prec < 0);

    if (zero_pad) {
        // 符号和前缀后、数字前补零
        if (sign_char) {
            put(sign_char);
            (*written)++;
        }
        if (prefix) {
            for (int i = 0; prefix[i]; i++) {
                put(prefix[i]);
                (*written)++;
            }
        }
        for (int i = 0; i < pad; i++) {
            put('0');
            (*written)++;
        }
        for (int i = 0; digits[i]; i++) {
            put(digits[i]);
            (*written)++;
        }
    } else {
        // 整个内容左侧填空格
        for (int i = 0; i < pad; i++) {
            put(' ');
            (*written)++;
        }
        if (sign_char) {
            put(sign_char);
            (*written)++;
        }
        if (prefix) {
            for (int i = 0; prefix[i]; i++) {
                put(prefix[i]);
                (*written)++;
            }
        }
        for (int i = 0; digits[i]; i++) {
            put(digits[i]);
            (*written)++;
        }
    }
}

/**
 * 格式化字符串并通过回调输出
 * 
 * @param put 每输出一个字符时调用的回调函数
 * @param fmt 格式控制字符串
 * @param args 可变参数列表
 * 
 * @return 输出的字符总数（不含 '\0'）
 */
static int vfprintk(kio_putchar_t put, const char *fmt, va_list args) {
    char tmp[TMP_BUF_SIZE];
    const char *p;
    int written = 0;

    // 扫描格式串，遇到 % 则解析并处理，否则直接输出
    for (p = fmt; *p; p++) {
        if (*p != '%') {
            put(*p);
            written++;
            continue;
        }

        struct fmt_spec spec;
        parse_fmt_spec(&p, args, &spec);

        if (spec.conv == '%') {
            put('%');
            written++;
            continue;
        }

        // 整数转换
        if (
            spec.conv == 'd' || spec.conv == 'i' || spec.conv == 'u' ||
            spec.conv == 'o' || spec.conv == 'x' || spec.conv == 'X'
        ) {
            bool is_signed = (spec.conv == 'd' || spec.conv == 'i');
            uint64_t abs_val = 0;
            bool is_negative = false;

            // 根据长度修饰符和符号取值
            if (is_signed) {
                int64_t val;
                switch (spec.len_mod) {
                case LEN_HH:
                    val = (signed char)va_arg(args, int);
                    break;
                case LEN_H:
                    val = (short)va_arg(args, int);
                    break;
                case LEN_NONE:
                    val = va_arg(args, int);
                    break;
                case LEN_L:
                    val = va_arg(args, long);
                    break;
                case LEN_LL:
                    val = va_arg(args, long long);
                    break;
                case LEN_Z:
                    val = (int64_t)va_arg(args, ssize_t);
                    break;
                case LEN_T:
                    val = (int64_t)va_arg(args, ptrdiff_t);
                    break;
                case LEN_J:
                    val = va_arg(args, intmax_t);
                    break;
                default:
                    val = va_arg(args, int);
                    break;
                }
                if (val < 0) {
                    is_negative = true;
                    abs_val = (uint64_t)(-val);
                } else {
                    abs_val = (uint64_t)val;
                }
            } else {
                switch (spec.len_mod) {
                case LEN_HH:
                    abs_val = (unsigned char)va_arg(args, unsigned int);
                    break;
                case LEN_H:
                    abs_val = (unsigned short)va_arg(args, unsigned int);
                    break;
                case LEN_NONE:
                    abs_val = va_arg(args, unsigned int);
                    break;
                case LEN_L:
                    abs_val = va_arg(args, unsigned long);
                    break;
                case LEN_LL:
                    abs_val = va_arg(args, unsigned long long);
                    break;
                case LEN_Z:
                    abs_val = va_arg(args, size_t);
                    break;
                case LEN_T:
                    abs_val = (uint64_t)va_arg(args, ptrdiff_t);
                    break;
                case LEN_J:
                    abs_val = va_arg(args, uintmax_t);
                    break;
                default:
                    abs_val = va_arg(args, unsigned int);
                    break;
                }
            }

            // 确定符号
            char sign_char = 0;
            if (is_signed) {
                if (is_negative) {
                    sign_char = '-';
                } else if (spec.flags & FLAG_POS) {
                    sign_char = '+';
                } else if (spec.flags & FLAG_SPACE) {
                    sign_char = ' ';
                }
            }

            // 准备数字串和前缀
            char digits_buf[TMP_BUF_SIZE];
            char *digits;
            char prefix[3];
            bool has_prefix;

            prepare_number(
                abs_val, 
                spec.conv, spec.prec, spec.flags,
                tmp, digits_buf, &digits, 
                prefix, &has_prefix
            );

            // 输出
            output_int(
                put, &written, sign_char,
                has_prefix ? prefix : NULL, digits,
                spec.width, spec.flags, spec.prec
            );

            continue;
        }

        // 字符串
        if (spec.conv == 's') {
            char *s = va_arg(args, char *);
            if (!s) {
                s = "(null)";
            }

            size_t len = print_strlen(s);
            if (spec.prec >= 0) {
                len = min((size_t)spec.prec, len);
            }

            if (spec.width > (int)len && !(spec.flags & FLAG_LEFT)) {
                for (int i = 0; i < spec.width - (int)len; i++) {
                    put(' ');
                    written++;
                }
            }

            for (size_t i = 0; i < len; i++) {
                put(s[i]);
                written++;
            }

            if (spec.width > (int)len && (spec.flags & FLAG_LEFT)) {
                for (int i = 0; i < spec.width - (int)len; i++) {
                    put(' ');
                    written++;
                }
            }

            continue;
        }

        // 字符
        if (spec.conv == 'c') {
            char c = (char)va_arg(args, int);
            if (spec.width > 1 && !(spec.flags & FLAG_LEFT)) {
                for (int i = 0; i < spec.width - 1; i++) {
                    put(' ');
                    written++;
                }
            }

            put(c);
            written++;
            if (spec.width > 1 && (spec.flags & FLAG_LEFT)) {
                for (int i = 0; i < spec.width - 1; i++) {
                    put(' ');
                    written++;
                }
            }

            continue;
        }

        // 指针
        if (spec.conv == 'p') {
            void *ptr = va_arg(args, void *);
            uintptr_t val = (uintptr_t)ptr;

            put('0');
            written++;
            put('x');
            written++;

            int ptr_digits = 2 * sizeof(void *);
            char *end = tmp + TMP_BUF_SIZE - 1;
            *end = '\0';
            char *pstart = format_hex(val, end, false);
            int len = end - pstart;
            int pad = max(0, ptr_digits - len);
            for (int i = 0; i < pad; i++) {
                put('0');
                written++;
            }

            for (int i = 0; i < len; i++) {
                put(pstart[i]);
                written++;
            }

            continue;
        }

        // 把字符数写入当前局部计数器
        if (spec.conv == 'n') {
            switch (spec.len_mod) {
            case LEN_HH:
                *(va_arg(args, signed char *)) = (signed char)written;
                break;
            case LEN_H:
                *(va_arg(args, short *)) = (short)written;
                break;
            case LEN_NONE:
                *(va_arg(args, int *)) = (int)written;
                break;
            case LEN_L:
                *(va_arg(args, long *)) = (long)written;
                break;
            case LEN_LL:
                *(va_arg(args, long long *)) = (long long)written;
                break;
            case LEN_Z:
                *(va_arg(args, ssize_t *)) = (ssize_t)written;
                break;
            case LEN_T:
                *(va_arg(args, ptrdiff_t *)) = (ptrdiff_t)written;
                break;
            case LEN_J:
                *(va_arg(args, intmax_t *)) = (intmax_t)written;
                break;
            default:
                *(va_arg(args, int *)) = (int)written;
                break;
            }
            continue;
        }

        // 未知转换符：原样输出 % 和字符
        put('%');
        written++;
        put(spec.conv);
        written++;
    }

    return written;
}

void printk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    uint64_t irqflags;
    spin_lock_irqsave(&serial_lock, &irqflags);
    vfprintk(serial_putchar_nolock, fmt, args);
    spin_unlock_irqrestore(&serial_lock, irqflags);
    va_end(args);
}

__attribute__((noreturn))
void printp(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    irq_off();
    vfprintk(serial_putchar_nolock, fmt, args);
    va_end(args);
    while (1) cpu_halt();
}