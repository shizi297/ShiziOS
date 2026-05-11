/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <stdint.h>
#include <drivers.h>
#include <stdatomic.h>
#include <asm/serial.h>
#include <errno.h>
#include <bitmap.h>
#include <dynarr.h>
#include <spinlock.h>
#include <asm/serial.h>
#include <drivers/base/drivers.h>

#define DRIVERS_PRINT(fmt, ...) \
    printk("[DRIVERS] " fmt, ##__VA_ARGS__)

// 用于匿名对象的 ID 最大值
#define ANON_BIT_WIDTH 20
#define ANON_MAX_COUNT ((1ULL << ANON_BIT_WIDTH) - 1) 
#define ANON_DEV 0

// 主设备号
#define DEV_MAIN_BIT_WIDTH (32 - ANON_BIT_WIDTH)

// 设备号打包
#define MKDEV(ma, mi) (((ma) << ANON_BIT_WIDTH) | (mi))

// 设备号分配器状态
static struct {
    dynarr_t *bitmap;
    spinlock_t lock;
} anon_state = {
    .bitmap = NULL,
    .lock = SPIN_LOCK_INIT
};

// 初始化匿名设备号位图
static void drivers_anon_init(void) {
    anon_state.bitmap = dynarr_bitmap_create(ANON_MAX_COUNT);
    if (!anon_state.bitmap)
        return;

    // 保留 0 号设备号
    uint32_t dummy;
    dynarr_bitmap_alloc(anon_state.bitmap, 1, &dummy);
}

// 驱动框架初始化
void drivers_init(void) {
    drivers_anon_init();
    DRIVERS_PRINT("drivers init success\n");
}

// 生成一个新的匿名对象 ID
int drivers_get_anon_id(dev_t *dev) {
    if (!anon_state.bitmap)
        return -ENODEV;

    spin_lock(&anon_state.lock);

    uint32_t idx;
    if (!dynarr_bitmap_alloc(anon_state.bitmap, 1, &idx)) {
        spin_unlock(&anon_state.lock);
        return -ENOSPC;
    }

    spin_unlock(&anon_state.lock);

    *dev = MKDEV(ANON_DEV, idx);
    return 0;
}

// 释放一个匿名对象 ID
void drivers_free_anon_id(dev_t dev) {
    uint32_t mi = dev & ANON_MAX_COUNT;
    if (!mi) return;

    spin_lock(&anon_state.lock);
    dynarr_bitmap_free(anon_state.bitmap, mi);
    spin_unlock(&anon_state.lock);
}

// 查找对应驱动的文件操作表
struct file_operations *drivers_dev_find(dev_t dev, mode_t mode) {
    return NULL;
}

