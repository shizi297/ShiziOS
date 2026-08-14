/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <exec/core.h>
#include <vfs.h>
#include <heap.h>
#include <klibc.h>
#include <config.h>
#include <note.h>
#include <initcall.h>

/*
 * 释放 ELF 加载器分配的资源
 *
 * @param info 先前分配的程序信息指针
 */
static void elf_free(struct exec_loader_info *info) {
    if (!info) return;
    if (info->interp_path) kheap_free(info->interp_path);
    kheap_free(info);
}

/*
 * 将 ELF 机器码转换为 ShiziOS 架构宏
 *
 * @param e_machine ELF e_machine 字段
 *
 * @return ARCH_* 宏值
 */
static int elf_arch_to_shizios(uint16_t e_machine) {
    switch (e_machine) {
        case EM_X86_64:
            return ARCH_X86_64;
        case EM_AARCH64:
            return ARCH_ARM;
        case EM_RISCV:
            return ARCH_RISCV;
        default:
            return ARCH_UNKNOWN;
    }
}

/*
 * 检查 NOTE 段是否包含 ShiziOS ABI 标识
 *
 * @param file 已打开的文件
 * @param phdr PT_NOTE 段程序头指针
 *
 * @return 包含有效标识
 */
static bool elf_check_note(struct file *file, struct elf_phdr *phdr) {
    char *buf = NULL;
    off_t offset = phdr->p_offset;
    bool found = false;

    // 分配缓冲区，用于读取整个 NOTE 段
    buf = kheap_alloc(phdr->p_filesz);
    if (!buf) return false;

    // 读取 NOTE 段数据到缓冲区
    ssize_t ret = vfs_read(file, buf, phdr->p_filesz, &offset);
    if (ret != phdr->p_filesz) {
        kheap_free(buf);
        return false;
    }

    // 遍历 NOTE 条目，查找匹配 ShiziOS ABI 标识的条目
    uint64_t pos = 0;
    while (pos + sizeof(struct note_shizios) <= phdr->p_filesz) {
        struct note_shizios *note = (struct note_shizios *)(buf + pos);

        // 检查 NOTE 条目的 namesz、type 和 name 是否匹配 ShiziOS 标识
        if (note->namesz == sizeof(NOTE_SHIZIOS_NAME) &&
            note->type == NOTE_SHIZIOS_TYPE &&
            strncmp(note->name, NOTE_SHIZIOS_NAME, sizeof(NOTE_SHIZIOS_NAME)) == 0) {
            found = true;
            break;
        }

        // NOTE 条目按 4 字节对齐，计算下一个条目的偏移
        uint64_t align = 4;
        uint64_t entry_size = sizeof(uint32_t) * 3 + note->namesz + note->descsz;
        pos += (entry_size + align - 1) & ~(align - 1);
    }

    kheap_free(buf);
    return found;
}

static struct exec_loader_info *elf_load(struct file *file) {
    struct elf_ehdr ehdr;
    struct elf_phdr *phdrs = NULL;
    struct exec_loader_info *info = NULL;
    uint32_t seg_count = 0;
    uint32_t i;
    ssize_t ret;
    off_t pos;
    int arch;

    // 读取 ELF 头
    ret = vfs_read(file, (char *)&ehdr, sizeof(ehdr), NULL);
    if (ret != sizeof(ehdr)) return NULL;

    // 验证魔数：ELF 文件必须以 \x7f E L F 开头
    if (ehdr.e_ident.magic[0] != ELFMAG0 ||
        ehdr.e_ident.magic[1] != ELFMAG1 ||
        ehdr.e_ident.magic[2] != ELFMAG2 ||
        ehdr.e_ident.magic[3] != ELFMAG3) {
        return NULL;
    }

    // 验证位宽：只支持 64 位 ELF
    if (ehdr.e_ident.class != ELFCLASS64) return NULL;

    // 验证字节序：只支持小端
    if (ehdr.e_ident.data != ELFDATA2LSB) return NULL;

    // 验证文件类型：只支持 ET_EXEC 和 ET_DYN（PIE）
    if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) return NULL;

    // 验证目标架构是否匹配当前内核架构
    arch = elf_arch_to_shizios(ehdr.e_machine);
    if (arch == ARCH_UNKNOWN || arch != ARCH) return NULL;

    // 程序头表必须存在
    if (ehdr.e_phnum == 0) return NULL;

    // 检查程序头表偏移是否会导致整数溢出
    if (ehdr.e_phoff + ehdr.e_phnum * ehdr.e_phentsize < ehdr.e_phoff) {
        return NULL;
    }

    // 读取程序头表
    phdrs = kheap_alloc(ehdr.e_phnum * sizeof(struct elf_phdr));
    if (!phdrs) return NULL;

    pos = ehdr.e_phoff;
    ret = vfs_read(file, (char *)phdrs, ehdr.e_phnum * sizeof(struct elf_phdr), &pos);
    if (ret != ehdr.e_phnum * sizeof(struct elf_phdr)) {
        kheap_free(phdrs);
        return NULL;
    }

    // 检查 NOTE 段：必须包含 ShiziOS ABI 标识
    bool note_found = false;
    for (i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_NOTE) {
            if (elf_check_note(file, &phdrs[i])) {
                note_found = true;
                break;
            }
        }
    }
    if (!note_found) {
        kheap_free(phdrs);
        return NULL;
    }

    // 统计 PT_LOAD 段数量，用于分配 exec_loader_info
    for (i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) seg_count++;
    }

    // 分配 exec_loader_info 结构体，末尾附带 seg_count 个 exec_segment 数组
    info = kheap_alloc(sizeof(struct exec_loader_info) + seg_count * sizeof(struct exec_segment));
    if (!info) {
        kheap_free(phdrs);
        return NULL;
    }

    // 初始化 info 结构体
    info->entry = ehdr.e_entry;
    info->interp_base = 0;
    info->interp_path = NULL;
    info->phdr = 0;
    info->phnum = ehdr.e_phnum;
    info->count = 0;
    info->phent = sizeof(struct elf_phdr);   // 新增：程序头条目大小
    info->is_elf = 0;                        // 新增：0 表示是 ELF 格式

    // 遍历程序头，填充段、解释器路径、PHDR 地址
    uint32_t seg_idx = 0;
    bool has_phdr = false;
    struct elf_phdr *first_load = NULL;

    for (i = 0; i < ehdr.e_phnum; i++) {
        struct elf_phdr *ph = &phdrs[i];

        switch (ph->p_type) {
            case PT_LOAD: {
                // 填充一个可加载段
                struct exec_segment *seg = &info->segments[seg_idx];
                seg->vaddr = ph->p_vaddr;
                seg->memsz = ph->p_memsz;
                seg->filesz = ph->p_filesz;
                seg->offset = ph->p_offset;

                // 转换 ELF 段权限为 VM_* 权限
                seg->prot = 0;
                if (ph->p_flags & PF_R) seg->prot |= VM_READ;
                if (ph->p_flags & PF_W) seg->prot |= VM_WRITE;
                if (ph->p_flags & PF_X) seg->prot |= VM_EXEC;

                seg_idx++;
                info->count++;

                // 记录第一个 PT_LOAD 段，用于后续 PHDR 推算
                if (!first_load) first_load = ph;
                break;
            }

            case PT_INTERP: {
                // 读取解释器路径字符串
                char *interp = kheap_alloc(ph->p_filesz);
                if (!interp) {
                    elf_free(info);
                    kheap_free(phdrs);
                    return NULL;
                }

                pos = ph->p_offset;
                ret = vfs_read(file, interp, ph->p_filesz, &pos);
                if (ret != ph->p_filesz) {
                    kheap_free(interp);
                    elf_free(info);
                    kheap_free(phdrs);
                    return NULL;
                }

                // 检查是否以 '\0' 结尾，如果不是则拒绝加载
                if (interp[ph->p_filesz - 1] != '\0') {
                    kheap_free(interp);
                    elf_free(info);
                    kheap_free(phdrs);
                    return NULL;
                }

                info->interp_path = interp;
                break;
            }

            case PT_PHDR:
                // 程序头表自身的段，直接记录其虚拟地址
                info->phdr = ph->p_vaddr;
                has_phdr = true;
                break;

            default:
                break;
        }
    }

    // 如果没找到 PT_PHDR 段，从第一个 PT_LOAD 段推算
    if (!has_phdr && first_load) {
        // 只有当第一个 PT_LOAD 段的 p_offset 为 0 时才能可靠推算
        if (first_load->p_offset == 0) {
            info->phdr = first_load->p_vaddr + ehdr.e_phoff;
            // 验证推算结果是否在第一个段的有效范围内
            if (info->phdr < first_load->p_vaddr ||
                info->phdr >= first_load->p_vaddr + first_load->p_memsz) {
                elf_free(info);
                kheap_free(phdrs);
                return NULL;
            }
        } else {
            // 无法推算，拒绝加载
            elf_free(info);
            kheap_free(phdrs);
            return NULL;
        }
    }

    kheap_free(phdrs);
    return info;
}

static void elf_destroy(struct exec_loader_info *info) {
    elf_free(info);
}

static struct binfmt elf_binfmt = {
    .load = elf_load,
    .destroy = elf_destroy,
    .name = "elf",
    .node = {0}
};

static void elf_init(void) {
    INIT_LIST_HEAD(&elf_binfmt.node);
    exec_register_binfmt(&elf_binfmt);
}

INITCALL(exec, 0, elf_init);