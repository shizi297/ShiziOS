/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdbool.h>
#include <asm/serial.h>
#include <heap.h>
#include <fs/vfs.h>
#include <initcall.h>
#include <task.h>
#include <klibc.h>
#include <errno.h>

#define FS_TEST_PRINT(fmt, ...) \
    printk("[FS_TEST]" fmt, ##__VA_ARGS__)

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

#define TEST_ASSERT(cond, fmt, ...) \
    do { \
        test_count++; \
        if (!(cond)) { \
            fail_count++; \
            FS_TEST_PRINT("[T%d] FAIL: " fmt "\n", test_count, ##__VA_ARGS__); \
        } else { \
            pass_count++; \
            FS_TEST_PRINT("[T%d] PASS\n", test_count); \
        } \
    } while(0)

// 获取当前任务的根路径和pwd路径
static void get_paths(struct path **root, struct path **pwd) {
    *root = vfs_get_root_path();  // 内部已经 vfs_path_get
    *pwd  = vfs_get_root_path();  
}

// 释放路径
static void put_paths(struct path *root, struct path *pwd) {
    vfs_path_put(root);
    vfs_path_put(pwd);
}

// 测试1：基本文件操作
static void test_file_basic(struct path *pwd) {
    struct file *file;
    char buf[32];
    ssize_t ret;
    struct kstat stat;

    FS_TEST_PRINT("test_file_basic\n");

    // 创建文件并写入
    file = vfs_open(
        "/test_basic", 
        O_CREAT | O_RDWR | O_TRUNC,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 
        pwd
    );
    
    TEST_ASSERT(!IS_ERR(file), "create /test_basic failed");
    if (IS_ERR(file)) {
        FS_TEST_PRINT("test_file_basic: %d/%d passed\n", pass_count, test_count);
        return;
    }

    ret = vfs_write(file, "hello", 5, NULL);
    TEST_ASSERT(ret == 5, "write failed: got %d", ret);

    vfs_close(file);

    // 只读测试：以只读方式打开，期望写入失败
    file = vfs_open("/test_basic", O_RDONLY, 0, pwd);
    TEST_ASSERT(!IS_ERR(file), "reopen /test_basic (O_RDONLY) failed");
    if (IS_ERR(file)) {
        FS_TEST_PRINT("test_file_basic: %d/%d passed\n", pass_count, test_count);
        return;
    }

    memset(buf, 0, sizeof(buf));
    ret = vfs_read(file, buf, 5, NULL);
    TEST_ASSERT(ret == 5, "read (O_RDONLY) failed: got %d", ret);
    TEST_ASSERT(!memcmp(buf, "hello", 5), "data mismatch (O_RDONLY): got '%s'", buf);

    // 尝试写入，应该返回 -EBADF
    ret = vfs_write(file, " world", 6, NULL);
    TEST_ASSERT(ret == -EBADF, "write on O_RDONLY should return -EBADF, got %d", ret);

    vfs_close(file);

    // 重新打开（读写方式）并进行追加写入、读取等完整测试
    file = vfs_open("/test_basic", O_RDWR, 0, pwd);
    TEST_ASSERT(!IS_ERR(file), "reopen /test_basic (O_RDWR) failed");
    if (IS_ERR(file)) {
        FS_TEST_PRINT("test_file_basic: %d/%d passed\n", pass_count, test_count);
        return;
    }

    // 测试lseek
    off_t pos = vfs_lseek(file, 0, SEEK_END);
    TEST_ASSERT(pos == 5, "lseek SEEK_END failed: got %d", pos);

    ret = vfs_write(file, " world", 6, NULL);
    TEST_ASSERT(ret == 6, "append write failed: got %d", ret);

    // 读取全部
    vfs_lseek(file, 0, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    ret = vfs_read(file, buf, 11, NULL);
    TEST_ASSERT(ret == 11, "read full failed: got %d", ret);
    TEST_ASSERT(!memcmp(buf, "hello world", 11), "full data mismatch");

    // getattr
    memset(&stat, 0, sizeof(stat));
    int err = vfs_getattr("/test_basic", &stat, pwd);
    TEST_ASSERT(err == 0, "getattr failed: %d", err);
    TEST_ASSERT(stat.st_size == 11, "size mismatch: got %d", stat.st_size);

    vfs_close(file);

    // 清理
    err = vfs_unlink("/test_basic", pwd);
    TEST_ASSERT(err == 0, "unlink /test_basic failed: %d", err);

    FS_TEST_PRINT("test_file_basic: %d/%d passed\n", pass_count, test_count);
}

// 测试2：目录操作
static void test_directory(struct path *pwd) {
    int err;
    struct file *file;

    FS_TEST_PRINT("test_directory\n");

    // 创建目录
    err = vfs_mkdir("/testdir", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, pwd);
    TEST_ASSERT(err == 0, "mkdir failed: %d", err);
    if (err) {
        FS_TEST_PRINT("test_directory: %d/%d passed\n", pass_count, test_count);
        return;
    }

    // 在目录中创建文件
    file = vfs_open(
        "/testdir/file", 
        O_CREAT | O_RDWR,            
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 
        pwd
    );

    TEST_ASSERT(!IS_ERR(file), "create file in dir failed");
    if (!IS_ERR(file)) {
        vfs_write(file, "test", 4, NULL);
        vfs_close(file);
    }

    // 删除非空目录应该失败
    err = vfs_rmdir("/testdir", pwd);
    TEST_ASSERT(err == -ENOTEMPTY, "rmdir non-empty should fail: got %d", err);

    // 删除文件后删除目录
    err = vfs_unlink("/testdir/file", pwd);
    TEST_ASSERT(err == 0, "unlink file in dir failed: %d", err);

    err = vfs_rmdir("/testdir", pwd);
    TEST_ASSERT(err == 0, "rmdir empty failed: %d", err);

    // 删除文件的错误情况
    err = vfs_unlink("/nonexistent", pwd);
    TEST_ASSERT(err == -ENOENT, "unlink nonexistent should fail: got %d", err);

    FS_TEST_PRINT("test_directory: %d/%d passed\n", pass_count, test_count);
}

// 测试3：链接操作
static void test_links(struct path *pwd) {
    int err;
    struct file *file;
    char buf[8];

    FS_TEST_PRINT("test_links\n");

    // 创建源文件
    file = vfs_open(
        "/link_src", 
        O_CREAT | O_RDWR,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 
        pwd
    );

    TEST_ASSERT(!IS_ERR(file), "create link_src failed");
    if (IS_ERR(file)) {
        FS_TEST_PRINT("test_links: %d/%d passed\n", pass_count, test_count);
        return;
    }

    vfs_write(file, "linkdata", 8, NULL);
    vfs_close(file);

    // 创建符号链接
    err = vfs_symlink("/link_src", "/symlink", pwd);
    TEST_ASSERT(err == 0, "symlink failed: %d", err);

    // 通过符号链接读取
    file = vfs_open("/symlink", O_RDONLY, 0, pwd);
    TEST_ASSERT(!IS_ERR(file), "open symlink failed");
    if (!IS_ERR(file)) {
        memset(buf, 0, sizeof(buf));
        vfs_read(file, buf, 8, NULL);
        TEST_ASSERT(!memcmp(buf, "linkdata", 8), "symlink data mismatch");
        vfs_close(file);
    }

    // 创建硬链接
    err = vfs_link("/link_src", "/hardlink", pwd);
    TEST_ASSERT(err == 0, "link failed: %d", err);

    // 删除源文件后硬链接仍可用
    err = vfs_unlink("/link_src", pwd);
    TEST_ASSERT(err == 0, "unlink src failed: %d", err);

    file = vfs_open("/hardlink", O_RDONLY, 0, pwd);
    TEST_ASSERT(!IS_ERR(file), "open hardlink after src removal failed");
    if (!IS_ERR(file)) {
        memset(buf, 0, sizeof(buf));
        vfs_read(file, buf, 8, NULL);
        TEST_ASSERT(!memcmp(buf, "linkdata", 8), "hardlink data mismatch");
        vfs_close(file);
    }

    // 清理
    err = vfs_unlink("/hardlink", pwd);
    TEST_ASSERT(err == 0, "unlink hardlink failed: %d", err);

    err = vfs_unlink("/symlink", pwd);
    TEST_ASSERT(err == 0, "unlink symlink failed: %d", err);

    FS_TEST_PRINT("test_links: %d/%d passed\n", pass_count, test_count);
}

// 测试4：重命名
static void test_rename(struct path *pwd) {
    int err;
    struct file *file;
    char buf[8];

    FS_TEST_PRINT("test_rename\n");

    // 创建文件
    file = vfs_open(
        "/rename_old", 
        O_CREAT | O_RDWR,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 
        pwd
    );

    TEST_ASSERT(!IS_ERR(file), "create rename_old failed");
    if (IS_ERR(file)) {
        FS_TEST_PRINT("test_rename: %d/%d passed\n", pass_count, test_count);
        return;
    }

    vfs_write(file, "renamed", 7, NULL);
    vfs_close(file);

    // 重命名
    err = vfs_rename("/rename_old", "/rename_new", pwd);
    TEST_ASSERT(err == 0, "rename failed: %d", err);

    // 旧路径应该不存在
    file = vfs_open("/rename_old", O_RDONLY, 0, pwd);
    TEST_ASSERT(IS_ERR(file) && PTR_ERR(file) == -ENOENT, "old path should be gone");

    // 新路径应该可用且数据一致
    file = vfs_open("/rename_new", O_RDONLY, 0, pwd);
    TEST_ASSERT(!IS_ERR(file), "open rename_new failed");
    if (!IS_ERR(file)) {
        memset(buf, 0, sizeof(buf));
        vfs_read(file, buf, 7, NULL);
        TEST_ASSERT(!memcmp(buf, "renamed", 7), "rename data mismatch");
        vfs_close(file);
    }

    // 清理
    err = vfs_unlink("/rename_new", pwd);
    TEST_ASSERT(err == 0, "unlink rename_new failed: %d", err);

    FS_TEST_PRINT("test_rename: %d/%d passed\n", pass_count, test_count);
}

// 测试5：截断
static void test_truncate(struct path *pwd) {
    struct file *file;
    char buf[16];
    ssize_t ret;
    int err;

    FS_TEST_PRINT("test_truncate\n");

    file = vfs_open(
        "/trunc_test", O_CREAT | O_RDWR,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, pwd
    );

    TEST_ASSERT(!IS_ERR(file), "create trunc_test failed");
    if (IS_ERR(file)) {
        FS_TEST_PRINT("test_truncate: %d/%d passed\n", pass_count, test_count);
        return;
    }

    // 写入数据
    ret = vfs_write(file, "1234567890", 10, NULL);
    TEST_ASSERT(ret == 10, "write failed: got %d", ret);

    // 截断到5字节
    err = vfs_truncate(file, 5);
    TEST_ASSERT(err == 0, "truncate failed: %d", err);

    // 验证大小
    struct kstat stat;
    err = vfs_getattr("/trunc_test", &stat, pwd);
    TEST_ASSERT(err == 0, "getattr after trunc failed: %d", err);
    TEST_ASSERT(stat.st_size == 5, "size after trunc: got %d", stat.st_size);

    // 读取数据
    vfs_lseek(file, 0, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    ret = vfs_read(file, buf, 5, NULL);
    TEST_ASSERT(ret == 5, "read after trunc failed");
    TEST_ASSERT(!memcmp(buf, "12345", 5), "trunc data mismatch");

    // 扩展文件（截断到大于当前文件大小）
    err = vfs_truncate(file, 10);
    TEST_ASSERT(err == 0, "expand trunc failed: %d", err);

    err = vfs_getattr("/trunc_test", &stat, pwd);
    TEST_ASSERT(err == 0, "getattr after expand failed: %d", err);
    TEST_ASSERT(stat.st_size == 10, "size after expand: got %d", stat.st_size);

    vfs_close(file);

    err = vfs_unlink("/trunc_test", pwd);
    TEST_ASSERT(err == 0, "unlink trunc_test failed: %d", err);

    FS_TEST_PRINT("test_truncate: %d/%d passed\n", pass_count, test_count);
}

// 主测试入口
static void test(void) {
    struct path *root, *pwd;

    FS_TEST_PRINT("starting VFS tests\n");

    get_paths(&root, &pwd);

    test_file_basic(pwd);
    test_directory(pwd);
    test_links(pwd);
    test_rename(pwd);
    test_truncate(pwd);

    put_paths(root, pwd);

    FS_TEST_PRINT(
        "VFS tests completed: %d/%d passed (%d failed)\n",
        pass_count, test_count, fail_count
    );

    if (fail_count > 0) 
        FS_TEST_PRINT("FAILED %d tests\n", fail_count);
    else 
        FS_TEST_PRINT("ALL TESTS PASSED\n");
    
}

// 加入到需要内核线程的测试（因为有一些操作需要睡眠）
INITCALL(kthreadtest, 0, test);