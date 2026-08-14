/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <heap.h>
#include <klibc.h>
#include <kio.h>
#include <initcall.h>
#include <mutex.h>
#include <dynarr.h>
#include <asm/processor.h>

#define EXEC_PRINT(str) \
    printk("[TIME]" str "\n")

#define EXEC_REGISTER_PRINT(name) \
    printk("[EXEC] register binfmt : [\"name\" = \"%s\"]\n", name)

// 加载器列表上下文
static struct {
    mutex_t lock;
    struct list_head head;
} binfmt_ctx;

// 加载栈帧，每一层代表一个可执行文件（主程序或解释器）
struct exec_load_frame {
    struct binfmt *fmt;                 // 匹配的加载器
    struct exec_loader_info *info;      // 加载器返回的信息
    char *path;                         // 该层文件的路径
    struct file *file;                  // 已打开的文件（用于解析）
    uint32_t mapped_count;              // 已映射的段数量（用于回滚）
    uintptr_t base;                     // 该层的加载基址（用于 AT_BASE）
};

/*
 * 构造用户栈
 *
 * @param stack_top 调用者分配的栈顶地址
 * @param argv 命令行参数数组（以 NULL 结尾）
 * @param envp 环境变量数组（以 NULL 结尾）
 * @param entry 主程序入口地址
 * @param phdr 主程序程序头表虚拟地址
 * @param phnum 程序头条目数量
 * @param phent 程序头条目大小
 * @param is_elf 0=ELF，非0=其他格式
 * @param interp_base 解释器加载基址（0 表示静态链接）
 * @param out_stack_top 输出：布局后的栈顶地址
 */
static int exec_setup_user_stack(
    uintptr_t stack_top,
    char **argv,
    char **envp,
    uintptr_t entry,
    uintptr_t phdr,
    uint32_t phnum,
    uint32_t phent,
    uint32_t is_elf,
    uintptr_t interp_base,
    uintptr_t *out_stack_top
) {
    uint64_t argc = 0;
    uint64_t envc = 0;
    uint64_t str_total = 0;
    uintptr_t pos;
    uint64_t i;

    // 计算 argc
    while (argv[argc]) argc++;

    // 计算 envc
    while (envp[envc]) envc++;

    // 计算所有字符串的总长度
    for (i = 0; i < argc; i++) {
        str_total += strlen(argv[i]) + 1;
    }
    for (i = 0; i < envc; i++) {
        str_total += strlen(envp[i]) + 1;
    }

    // AT_PLATFORM 字符串
    const char *platform = PLATFORM_STRING;
    size_t platform_len = sizeof(PLATFORM_STRING);
    str_total += platform_len;

    // 收集 auxv 条目
    dynarr_t *auxv = dynarr_create(sizeof(auxv_entry_t), 0);
    if (!auxv) return -ENOMEM;

    auxv_entry_t auxv_entry;
    uint64_t platform_index = 0;

    // AT_PHDR
    auxv_entry.type = AT_PHDR;
    auxv_entry.value = phdr;
    dynarr_append(auxv, &auxv_entry);

    // AT_PHENT
    if (phent != 0) {
        auxv_entry.type = AT_PHENT;
        auxv_entry.value = phent;
        dynarr_append(auxv, &auxv_entry);
    }

    // AT_PHNUM
    auxv_entry.type = AT_PHNUM;
    auxv_entry.value = phnum;
    dynarr_append(auxv, &auxv_entry);

    // AT_ENTRY
    auxv_entry.type = AT_ENTRY;
    auxv_entry.value = entry;
    dynarr_append(auxv, &auxv_entry);

    // AT_BASE
    auxv_entry.type = AT_BASE;
    auxv_entry.value = interp_base;
    dynarr_append(auxv, &auxv_entry);

    // AT_PAGESZ
    auxv_entry.type = AT_PAGESZ;
    auxv_entry.value = PAGE_SIZE;
    dynarr_append(auxv, &auxv_entry);

    // AT_FLAGS
    auxv_entry.type = AT_FLAGS;
    auxv_entry.value = 0;
    dynarr_append(auxv, &auxv_entry);

    // AT_PLATFORM（占位，稍后更新）
    auxv_entry.type = AT_PLATFORM;
    auxv_entry.value = 0;
    dynarr_append(auxv, &auxv_entry);
    platform_index = dynarr_count(auxv) - 1;

    // AT_NOTELF
    auxv_entry.type = AT_NOTELF;
    auxv_entry.value = is_elf;
    dynarr_append(auxv, &auxv_entry);

    // AT_NULL
    auxv_entry.type = AT_NULL;
    auxv_entry.value = 0;
    dynarr_append(auxv, &auxv_entry);

    uint64_t auxv_count = dynarr_count(auxv);
    uint64_t auxv_size = auxv_count * sizeof(auxv_entry_t);
    uint64_t envp_size = (envc + 1) * sizeof(uintptr_t);
    uint64_t argv_size = (argc + 1) * sizeof(uintptr_t);
    uint64_t argc_size = sizeof(uint64_t);

    uint64_t total_size = auxv_size + envp_size + argv_size + argc_size + str_total;

    // 从 stack_top 向下分配，16 字节对齐
    pos = stack_top - total_size;
    pos = pos & ~15;

    uintptr_t stack_ptr = pos;

    uintptr_t argv_base = pos + argc_size;
    uintptr_t envp_base = argv_base + argv_size;
    uintptr_t auxv_base = envp_base + envp_size;
    uintptr_t str_base = auxv_base + auxv_size;

    // 更新 AT_PLATFORM 的值为实际地址
    uintptr_t platform_actual = str_base;
    auxv_entry_t *platform_entry = (auxv_entry_t *)dynarr_get(auxv, platform_index);
    platform_entry->value = platform_actual;

    // 复制 auxv 数组到栈上
    auxv_entry_t *auxv_array = (auxv_entry_t *)dynarr_get(auxv, 0);
    memcpy((void *)auxv_base, auxv_array, auxv_size);

    // 复制 AT_PLATFORM 字符串
    memcpy((char *)str_base, platform, platform_len);
    str_base += platform_len;

    // 复制 argv 指针数组（包括 NULL 结尾）
    uintptr_t *argv_ptrs = (uintptr_t *)argv_base;
    memcpy(argv_ptrs, argv, (argc + 1) * sizeof(uintptr_t));

    // 复制 envp 指针数组（包括 NULL 结尾）
    uintptr_t *envp_ptrs = (uintptr_t *)envp_base;
    memcpy(envp_ptrs, envp, (envc + 1) * sizeof(uintptr_t));

    // 复制 argv 字符串
    uintptr_t str_pos = str_base;
    for (i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        argv_ptrs[i] = str_pos;
        memcpy((char *)str_pos, argv[i], len);
        str_pos += len;
    }

    // 复制 envp 字符串
    for (i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        envp_ptrs[i] = str_pos;
        memcpy((char *)str_pos, envp[i], len);
        str_pos += len;
    }

    // 设置 argc
    *(uint64_t *)stack_ptr = argc;

    *out_stack_top = stack_ptr;

    dynarr_destroy(auxv);
    return 0;
}

/*
 * 注册一个加载器到框架
 *
 * @param fmt 加载器结构体指针
 */
void exec_register_binfmt(struct binfmt *fmt) {
    if (!fmt || !fmt->load) return;
    mutex_lock(&binfmt_ctx.lock);
    list_add_tail(&fmt->node, &binfmt_ctx.head);
    mutex_unlock(&binfmt_ctx.lock);
    
    if (fmt->name) EXEC_REGISTER_PRINT(fmt->name);
}

/*
 * 从框架注销一个加载器
 *
 * @param fmt 加载器结构体指针
 */
void exec_unregister_binfmt(struct binfmt *fmt) {
    if (!fmt) return;
    mutex_lock(&binfmt_ctx.lock);
    list_del_init(&fmt->node);
    mutex_unlock(&binfmt_ctx.lock);
}

// 初始化
void exec_init(void) {
    mutex_init(&binfmt_ctx.lock);
    INIT_LIST_HEAD(&binfmt_ctx.head);
    initcall(exec, 0);
    
    EXEC_PRINT("exec init success");
}

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
    char **argv,
    char **envp,
    uintptr_t stack_top,
    uintptr_t *out_stack_top
) {
    struct binfmt *fmt;
    struct exec_load_frame stack[EXEC_MAX_DEPTH];
    struct exec_load_frame *frame = NULL;
    int depth = 0;
    int ret = 0;
    uintptr_t final_entry = 0;
    uintptr_t main_phdr = 0;
    uint32_t main_phnum = 0;
    uint32_t main_phent = 0;
    uint32_t main_is_elf = 0;
    uintptr_t interp_base = 0;

    memset(stack, 0, sizeof(stack));

    // 初始化最外层
    stack[0].path = strdup(path);
    if (!stack[0].path) {
        return (kuptr)K_ERR(-ENOMEM);
    }

    while (1) {
        frame = &stack[depth];

        // 打开当前文件
        kptr file_res = vfs_open(frame->path, O_RDONLY, 0, pwd);
        K_ERR_LABEL_AND_SAVE(file_res, err_open, ret);
        frame->file = (struct file *)file_res.ptr;

        // 解析当前文件
        mutex_lock(&binfmt_ctx.lock);
        list_for_each_entry(fmt, &binfmt_ctx.head, node) {
            frame->info = fmt->load(frame->file);
            if (frame->info) break;
        }
        mutex_unlock(&binfmt_ctx.lock);

        if (!frame->info) {
            ret = -ENOEXEC;
            goto err_parse;
        }

        // 记录匹配的加载器
        frame->fmt = fmt;

        // 关闭文件（段映射由 vheap_file_alloc 独立打开）
        vfs_close(frame->file);
        frame->file = NULL;

        // 映射所有段
        for (uint32_t i = 0; i < frame->info->count; i++) {
            struct exec_segment *seg = &frame->info->segments[i];

            void *mapped = vheap_file_alloc(
                as,
                (void *)seg->vaddr,
                seg->memsz,
                seg->prot,
                0,
                frame->path,
                seg->offset,
                pwd
            );
            if (!mapped) {
                ret = -ENOMEM;
                frame->mapped_count = i;
                goto err_map;
            }

            // 记录该层的加载基址（第一个段的 vaddr）
            if (i == 0) {
                frame->base = seg->vaddr;
            }
        }
        frame->mapped_count = frame->info->count;

        // 记录主程序的信息
        if (depth == 0) {
            main_phdr = frame->info->phdr;
            main_phnum = frame->info->phnum;
            main_phent = frame->info->phent;
            main_is_elf = frame->info->is_elf;
        }

        // 更新最终入口
        final_entry = frame->info->entry;

        // 检查是否有解释器
        if (frame->info->interp_path) {
            depth++;
            if (depth >= EXEC_MAX_DEPTH) {
                ret = -ELOOP;
                goto err_depth;
            }

            stack[depth].path = strdup(frame->info->interp_path);
            if (!stack[depth].path) {
                ret = -ENOMEM;
                goto err_depth;
            }
            continue;
        } else {
            break;
        }
    }

    // 所有层加载成功，提取解释器基址
    if (depth > 0) {
        interp_base = stack[depth].base;
        if (interp_base == 0) {
            interp_base = stack[depth].info->entry;
        }
    }

    // 构造用户栈
    ret = exec_setup_user_stack(
        stack_top,
        argv,
        envp,
        final_entry,
        main_phdr,
        main_phnum,
        main_phent,
        main_is_elf,
        interp_base,
        out_stack_top
    );
    if (ret < 0) {
        goto err_stack;
    }

    // 释放所有层的资源
    for (int i = 0; i <= depth; i++) {
        struct exec_load_frame *f = &stack[i];
        if (f->info) {
            if (f->fmt && f->fmt->destroy) f->fmt->destroy(f->info);
            f->info = NULL;
        }
        if (f->path) kheap_free(f->path);
    }

    return (kuptr)K_OK(final_entry);

err_stack:
err_depth:
    // 逆序释放所有层已映射的段
    for (int i = depth; i >= 0; i--) {
        struct exec_load_frame *f = &stack[i];
        if (f->info && f->mapped_count > 0) {
            for (uint32_t j = f->mapped_count; j > 0; j--) {
                struct exec_segment *seg = &f->info->segments[j - 1];
                vheap_free(as, (void *)seg->vaddr);
            }
        }
        if (f->file) {
            vfs_close(f->file);
            f->file = NULL;
        }
        if (f->info) {
            if (f->fmt && f->fmt->destroy) f->fmt->destroy(f->info);
            f->info = NULL;
        }
        if (f->path) {
            kheap_free(f->path);
            f->path = NULL;
        }
    }
err_map:
    // 回滚当前层已映射的段
    if (frame->info && frame->mapped_count > 0) {
        for (uint32_t j = frame->mapped_count; j > 0; j--) {
            struct exec_segment *seg = &frame->info->segments[j - 1];
            vheap_free(as, (void *)seg->vaddr);
        }
    }
err_parse:
    // 关闭当前层文件，释放 info
    if (frame->file) vfs_close(frame->file);
    if (frame->info) {
        if (frame->fmt && frame->fmt->destroy) frame->fmt->destroy(frame->info);
        frame->info = NULL;
    }
err_open:
    // 释放当前层路径
    if (frame->path) kheap_free(frame->path);
    return (kuptr)K_ERR(ret);
}