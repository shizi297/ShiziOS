/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <stdbool.h>
#include <kio.h>
#include <heap.h>
#include <fs/vfs.h>
#include <initcall.h>
#include <task.h>
#include <klibc.h>

#define FS_TEST_PRINT(fmt, ...) \
    printk("[FS_TEST]" fmt, ##__VA_ARGS__)

static int pass_count = 0;
static int fail_count = 0;

// 获取当前任务的根路径和pwd路径
static void get_paths(struct path **root, struct path **pwd) {
    *root = vfs_get_root_path();
    *pwd  = vfs_get_root_path();  
}

// 释放路径
static void put_paths(struct path *root, struct path *pwd) {
    vfs_path_put(root);
    vfs_path_put(pwd);
}

// 基本文件操作
TEST_ENTRY(test_file_basic, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[32];
    ssize_t ret;
    struct kstat stat;

    // 创建文件并写入
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create and write /test_basic");
        file = vfs_open(
            "/test_basic",
            O_CREAT | O_RDWR | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create /test_basic failed");

        ret = vfs_write(file, "hello", 5, NULL);
        TEST_ASSERT_STEP(global_step + step, ret == 5, "write failed: got %d", ret);
    });

    // 只读测试：以只读方式打开，期望写入失败
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "reopen O_RDONLY and verify read-only");
        file = vfs_open("/test_basic", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "reopen /test_basic (O_RDONLY) failed");

        memset(buf, 0, sizeof(buf));
        ret = vfs_read(file, buf, 5, NULL);
        TEST_ASSERT_STEP(global_step + step, ret == 5, "read (O_RDONLY) failed: got %d", ret);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "hello", 5), "data mismatch (O_RDONLY): got '%s'", buf);

        ret = vfs_write(file, " world", 6, NULL);
        TEST_ASSERT_STEP(global_step + step, ret == -EBADF, "write on O_RDONLY should return -EBADF, got %d", ret);

        vfs_close(file);
    });

    // 重新打开（读写方式）并进行追加写入、读取等完整测试
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "reopen O_RDWR, append and read full");
        file = vfs_open("/test_basic", O_RDWR, 0, pwd);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "reopen /test_basic (O_RDWR) failed");

        off_t pos = vfs_lseek(file, 0, SEEK_END);
        TEST_ASSERT_STEP(global_step + step, pos == 5, "lseek SEEK_END failed: got %d", pos);

        ret = vfs_write(file, " world", 6, NULL);
        TEST_ASSERT_STEP(global_step + step, ret == 6, "append write failed: got %d", ret);

        vfs_lseek(file, 0, SEEK_SET);
        memset(buf, 0, sizeof(buf));
        ret = vfs_read(file, buf, 11, NULL);
        TEST_ASSERT_STEP(global_step + step, ret == 11, "read full failed: got %d", ret);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "hello world", 11), "full data mismatch");

        vfs_close(file);
    });

    // getattr
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "getattr /test_basic");
        memset(&stat, 0, sizeof(stat));
        int err = vfs_getattr("/test_basic", &stat, pwd);
        TEST_ASSERT_STEP(global_step + step, err == 0, "getattr failed: %d", err);
        TEST_ASSERT_STEP(global_step + step, stat.st_size == 11, "size mismatch: got %d", stat.st_size);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/test_basic", pwd);
    });
})

// 目录操作
TEST_ENTRY(test_directory, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    int ret;

    // 创建目录并在目录中创建文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "mkdir and create file in dir");
        ret = vfs_mkdir(
            "/testdir",
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, ret == 0, "mkdir failed: %d", ret);

        file = vfs_open(
            "/testdir/file",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create file in dir failed");

        vfs_write(file, "test", 4, NULL);
        vfs_close(file);
    });

    // 删除非空目录应该失败
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "rmdir non-empty dir");
        ret = vfs_rmdir("/testdir", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == -ENOTEMPTY, "rmdir non-empty should fail: got %d", ret);
    });

    // 删除文件后删除目录
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "unlink file and rmdir empty dir");
        ret = vfs_unlink("/testdir/file", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "unlink file in dir failed: %d", ret);

        ret = vfs_rmdir("/testdir", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "rmdir empty failed: %d", ret);
    });

    // 删除不存在的文件的错误情况
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "unlink nonexistent file");
        ret = vfs_unlink("/nonexistent", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == -ENOENT, "unlink nonexistent should fail: got %d", ret);
    });

    TEST_CLEANUP(do_run, {
        // 目录已在前面删除
    });
})

// 链接操作
TEST_ENTRY(test_links, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[8];
    int ret;

    // 创建源文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create source file");
        file = vfs_open(
            "/link_src",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create link_src failed");

        vfs_write(file, "linkdata", 8, NULL);
        vfs_close(file);
    });

    // 创建符号链接并读取
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "symlink and read through symlink");
        ret = vfs_symlink("/link_src", "/symlink", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "symlink failed: %d", ret);

        file = vfs_open("/symlink", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "open symlink failed");

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 8, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 8, "symlink read failed: got %d", r);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "linkdata", 8), "symlink data mismatch");

        vfs_close(file);
    });

    // 创建硬链接，删除源文件后硬链接仍可用
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "hardlink, delete src, read hardlink");
        ret = vfs_link("/link_src", "/hardlink", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "link failed: %d", ret);

        ret = vfs_unlink("/link_src", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "unlink src failed: %d", ret);

        file = vfs_open("/hardlink", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "open hardlink after src removal failed");

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 8, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 8, "hardlink read failed: got %d", r);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "linkdata", 8), "hardlink data mismatch");

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/hardlink", pwd);
        vfs_unlink("/symlink", pwd);
    });
})

// 重命名
TEST_ENTRY(test_rename, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[8];
    int ret;

    // 创建文件并写入
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create file for rename");
        file = vfs_open(
            "/rename_old",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create rename_old failed");

        vfs_write(file, "renamed", 7, NULL);
        vfs_close(file);
    });

    // 重命名
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "rename file");
        ret = vfs_rename("/rename_old", "/rename_new", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "rename failed: %d", ret);
    });

    // 旧路径应该不存在，新路径应该可用且数据一致
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "verify old path gone, new path valid");
        file = vfs_open("/rename_old", O_RDONLY, 0, pwd);
        ret = IS_ERR(file) ? PTR_ERR(file) : 0;
        TEST_ASSERT_STEP(global_step + step, ret == -ENOENT, "old path should be gone");

        file = vfs_open("/rename_new", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "open rename_new failed");

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 7, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 7, "rename read failed: got %d", r);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "renamed", 7), "rename data mismatch");

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/rename_new", pwd);
    });
})

// 截断
TEST_ENTRY(test_truncate, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[16];
    struct kstat stat;
    int ret;

    // 创建文件并写入初始数据
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create file and write initial data");
        file = vfs_open(
            "/trunc_test",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create trunc_test failed");

        ret = vfs_write(file, "1234567890", 10, NULL);
        TEST_ASSERT_STEP(global_step + step, ret == 10, "write failed: got %d", ret);
    });

    // 截断到5字节并验证
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "truncate to 5 bytes");
        ret = vfs_truncate(file, 5);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "truncate failed: %d", ret);

        memset(&stat, 0, sizeof(stat));
        ret = vfs_getattr("/trunc_test", &stat, pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "getattr after trunc failed: %d", ret);
        TEST_ASSERT_STEP(global_step + step, stat.st_size == 5, "size after trunc: got %d", stat.st_size);

        vfs_lseek(file, 0, SEEK_SET);
        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 5, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 5, "read after trunc failed");
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "12345", 5), "trunc data mismatch");
    });

    // 扩展到10字节并验证
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "expand to 10 bytes");
        ret = vfs_truncate(file, 10);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "expand trunc failed: %d", ret);

        memset(&stat, 0, sizeof(stat));
        ret = vfs_getattr("/trunc_test", &stat, pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "getattr after expand failed: %d", ret);
        TEST_ASSERT_STEP(global_step + step, stat.st_size == 10, "size after expand: got %d", stat.st_size);

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/trunc_test", pwd);
    });
})

// 符号链接后接剩余路径组件
TEST_ENTRY(test_symlink_trailing_path, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[16];
    int ret;

    // 创建目标目录和文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "setup directory and subfile");
        ret = vfs_mkdir(
            "/real_dir",
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, ret == 0, "mkdir /real_dir failed: %d", ret);

        file = vfs_open(
            "/real_dir/subfile",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create /real_dir/subfile failed");

        vfs_write(file, "trailing", 8, NULL);
        vfs_close(file);
    });

    // 创建符号链接指向目录
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "symlink to directory");
        ret = vfs_symlink("/real_dir", "/link_to_dir", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "symlink /link_to_dir failed: %d", ret);
    });

    // 通过符号链接访问子文件——核心测试
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "access subfile via symlink");
        file = vfs_open("/link_to_dir/subfile", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "open /link_to_dir/subfile failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 8, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 8, "read /link_to_dir/subfile failed: got %d", r);
        TEST_ASSERT_STEP(
            global_step + step,
            !memcmp(buf, "trailing", 8),
            "data mismatch: got '%s'",
            buf
        );

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/link_to_dir", pwd);
        vfs_unlink("/real_dir/subfile", pwd);
        vfs_rmdir("/real_dir", pwd);
    });
})

// 嵌套符号链接
TEST_ENTRY(test_symlink_nested, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[8];
    int ret;

    // 创建源文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create source file");
        file = vfs_open(
            "/nested_src",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create /nested_src failed");

        vfs_write(file, "nestdata", 8, NULL);
        vfs_close(file);
    });

    // 创建符号链接链
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "build symlink chain");
        ret = vfs_symlink("/nested_src", "/sym2", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "symlink /sym2 failed: %d", ret);

        ret = vfs_symlink("/sym2", "/sym1", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "symlink /sym1 failed: %d", ret);
    });

    // 通过两层符号链接访问
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "read through nested symlinks");
        file = vfs_open("/sym1", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "open /sym1 failed");

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 8, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 8, "read /sym1 failed: got %d", r);
        TEST_ASSERT_STEP(
            global_step + step,
            !memcmp(buf, "nestdata", 8),
            "data mismatch: got '%s'",
            buf
        );

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/sym1", pwd);
        vfs_unlink("/sym2", pwd);
        vfs_unlink("/nested_src", pwd);
    });
})

// 符号链接指向不存在的目标
TEST_ENTRY(test_symlink_dangling, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    int ret;

    // 创建指向不存在目标的符号链接
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create dangling symlink");
        ret = vfs_symlink("/nonexistent_target", "/dangling_link", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "symlink /dangling_link failed: %d", ret);
    });

    // 尝试打开应该返回 -ENOENT
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "open dangling symlink");
        file = vfs_open("/dangling_link", O_RDONLY, 0, pwd);
        ret = IS_ERR(file) ? PTR_ERR(file) : 0;
        TEST_ASSERT_STEP(
            global_step + step,
            ret == -ENOENT,
            "dangling symlink should return -ENOENT, got %d",
            ret
        );
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/dangling_link", pwd);
    });
})

// 负缓存测试：先访问不存在，再创建，再访问
TEST_ENTRY(test_negative_cache, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[16];
    int ret;

    // 先访问不存在的文件，建立负缓存
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "first open - expect ENOENT");
        file = vfs_open("/neg_test", O_RDONLY, 0, pwd);
        ret = IS_ERR(file) ? PTR_ERR(file) : 0;
        TEST_ASSERT_STEP(
            global_step + step,
            ret == -ENOENT,
            "first open should return -ENOENT, got %d",
            ret
        );
    });

    // 创建这个文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create the file");
        file = vfs_open(
            "/neg_test",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create /neg_test failed");

        vfs_write(file, "negative", 8, NULL);
        vfs_close(file);
    });

    // 再次访问，验证负缓存已被正确失效
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "reopen - expect success");
        file = vfs_open("/neg_test", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "reopen /neg_test after create failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 8, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 8, "read /neg_test failed: got %d", r);
        TEST_ASSERT_STEP(
            global_step + step,
            !memcmp(buf, "negative", 8),
            "data mismatch: got '%s'",
            buf
        );

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/neg_test", pwd);
    });
})

// 路径规范化：.、..、连续斜杠
TEST_ENTRY(test_path_normalization, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[8];
    int ret;

    // 创建测试目录和文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "setup directory and file");
        ret = vfs_mkdir(
            "/normdir",
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, ret == 0, "mkdir /normdir failed: %d", ret);

        file = vfs_open(
            "/normdir/file",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create /normdir/file failed");

        vfs_write(file, "normdata", 8, NULL);
        vfs_close(file);
    });

    // 测试 .. 处理
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "test parent dir traversal");
        file = vfs_open("/normdir/../normdir/file", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "open /normdir/../normdir/file failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 8, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 8, "read .. test failed: got %d", r);

        vfs_close(file);
    });

    // 测试 . 处理
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "test current dir reference");
        file = vfs_open("/normdir/./file", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "open /normdir/./file failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        vfs_close(file);
    });

    // 测试连续斜杠
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "test multiple slashes");
        file = vfs_open("//normdir///file", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "open //normdir///file failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        vfs_close(file);
    });

    // 清理
    TEST_CLEANUP(do_run, {
        vfs_unlink("/normdir/file", pwd);
        vfs_rmdir("/normdir", pwd);
    });
})

// 挂载点与符号链接结合
TEST_ENTRY(test_mount_and_symlink, step, do_run, (struct path *pwd, int global_step), {
    struct file *file = NULL;
    char buf[8];
    int ret;
    struct vfsmount *mnt = NULL;

    // 创建挂载点并挂载 tmpfs
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "mount tmpfs");
        ret = vfs_mkdir(
            "/mnt",
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, ret == 0, "mkdir /mnt failed: %d", ret);

        mnt = vfs_mount(NULL, "/mnt", "tmpfs", MS_NONE, NULL, true);
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(mnt), "mount tmpfs on /mnt failed: %d", PTR_ERR(mnt));
    });

    // 在挂载点下创建文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "create file under mount");
        file = vfs_open(
            "/mnt/mountfile",
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
            pwd
        );
        TEST_ASSERT_STEP(global_step + step, !IS_ERR(file), "create /mnt/mountfile failed");

        vfs_write(file, "mounted", 7, NULL);
        vfs_close(file);
    });

    // 通过挂载点访问文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "access file through mount");
        file = vfs_open("/mnt/mountfile", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "open /mnt/mountfile failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 7, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 7, "read /mnt/mountfile failed: got %d", r);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "mounted", 7), "data mismatch");

        vfs_close(file);
    });

    // 创建符号链接指向挂载点内的文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "symlink to mounted file");
        ret = vfs_symlink("/mnt/mountfile", "/link_to_mount", pwd);
        TEST_ASSERT_STEP(global_step + step, ret == 0, "symlink to mount failed: %d", ret);
    });

    // 通过符号链接访问挂载点内的文件
    TEST_IMPL(do_run, step, {
        TEST_DESC(global_step + step, "access mount through symlink");
        file = vfs_open("/link_to_mount", O_RDONLY, 0, pwd);
        TEST_ASSERT_STEP(
            global_step + step,
            !IS_ERR(file),
            "open /link_to_mount failed: %d",
            IS_ERR(file) ? PTR_ERR(file) : 0
        );

        memset(buf, 0, sizeof(buf));
        ssize_t r = vfs_read(file, buf, 7, NULL);
        TEST_ASSERT_STEP(global_step + step, r == 7, "read /link_to_mount failed: got %d", r);
        TEST_ASSERT_STEP(global_step + step, !memcmp(buf, "mounted", 7), "data mismatch");

        vfs_close(file);
    });

    /**
     * 清理
     * 
     * TODO: 
     * 
     * TEST_CLEANUP(do_run, {
     * vfs_unlink("/link_to_mount", pwd);
     * vfs_unlink("/mnt/mountfile", pwd);
     * vfs_umount(xxx);
     * vfs_rmdir("/mnt", pwd);
     * });
     */
})

// 主测试入口
static void test(void) {
    struct path *root, *pwd;
    struct test_result result;
    int total_steps = 0;

    FS_TEST_PRINT("starting VFS tests\n");

    get_paths(&root, &pwd);

    // 基本功能测试
    result = test_file_basic(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_directory(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_links(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_rename(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_truncate(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    // 路径解析增强测试
    result = test_symlink_trailing_path(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_symlink_nested(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_symlink_dangling(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_negative_cache(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_path_normalization(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    result = test_mount_and_symlink(pwd, total_steps);
    pass_count += result.pass;
    fail_count += result.fail;
    total_steps += result.total;

    put_paths(root, pwd);

    FS_TEST_PRINT(
        "[%d/%d] all tests done: %d passed, %d failed\n",
        total_steps,
        total_steps,
        pass_count,
        fail_count
    );

    if (fail_count > 0) 
        FS_TEST_PRINT("FAILED %d tests\n", fail_count);
    else 
        FS_TEST_PRINT("ALL TESTS PASSED\n");
}

INITCALL(kthreadtest, 0, test);