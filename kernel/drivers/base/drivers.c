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
    anon_state.bitmap = 
        dynarr_create(
            sizeof(bitmap_t),
            BITMAP_BYTES(ANON_MAX_COUNT) / sizeof(bitmap_t)
        );

    if (!anon_state.bitmap)
        return;

    // 保留 0 号设备号
    bitmap_t zero = 0;
    dynarr_set(anon_state.bitmap, 0, &zero);
    bitmap_set(anon_state.bitmap->arr, 0);
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

    uint32_t total_bits = dynarr_capacity(anon_state.bitmap) * BITS_PER_UNIT;
    uint32_t idx = bitmap_find(anon_state.bitmap->arr, total_bits, 1, false);

    if (idx >= total_bits) {
        // 位图已满，尝试扩容
        if (total_bits >= ANON_MAX_COUNT) {
            spin_unlock(&anon_state.lock);
            return -ENOSPC;
        }

        uint64_t new_cap = dynarr_capacity(anon_state.bitmap) * 2;
        if (!dynarr_expand_to(anon_state.bitmap, new_cap)) {
            spin_unlock(&anon_state.lock);
            return -ENOMEM;
        }

        // 扩容成功，新增区域已清零，重新查找
        total_bits = dynarr_capacity(anon_state.bitmap) * BITS_PER_UNIT;
        idx = bitmap_find(anon_state.bitmap->arr, total_bits, 1, false);
        if (idx >= total_bits) {
            spin_unlock(&anon_state.lock);
            return -ENOSPC;
        }
    }

    bitmap_set(anon_state.bitmap->arr, idx);
    spin_unlock(&anon_state.lock);

    *dev = MKDEV(ANON_DEV, idx);
    return 0;
}

// 释放一个匿名对象 ID
void drivers_free_anon_id(dev_t dev) {
    uint32_t mi = dev & ANON_MAX_COUNT;
    if (!mi) return;

    spin_lock(&anon_state.lock);

    if (anon_state.bitmap)
        bitmap_clear(anon_state.bitmap->arr, mi);

    spin_unlock(&anon_state.lock);
}