/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <acpi.h>
#include <klibc.h>
#include <kio.h>
#include <uacpi/namespace.h>
#include <uacpi/utilities.h>
#include <uacpi/uacpi.h>
#include <drivers/base/drivers.h>

#define PLATFORM_DEV_PANIC(fmt, ...) \
    printp("[PLATFORM DEV] ERROR: " fmt, ##__VA_ARGS__)

#define PLATFORM_MAX_NAME_COUNT 128  
#define PLATFORM_MAX_HID_COUNT 9    
#define PLATFORM_MAX_UID_COUNT 64   

// 用于存储平台设备的私有结构体
struct platform_device {
    struct device dev;  // 实际注册的设备
    uint16_t segment;   // 设备段组号
};

/**
 * 获取下一个 PCI 根桥的信息
 * 
 * @param iterator 迭代器指针
 * @param hid 输出 HID 字符串缓冲区（大小至少 PLATFORM_MAX_HID_COUNT）
 * @param uid 输出 UID 字符串缓冲区（大小至少 PLATFORM_MAX_UID_COUNT）
 * @param seg 输出 SEG 值
 * 
 * @return true 成功获取一个 PCI 根桥的信息
 * @return false 没有更多根桥（或出错）
 */
bool platform_next_get_info(void **iterator, char *hid, char *uid, uint16_t *seg) {
    if (!iterator) return false;

    // 获取 \_SB 节点
    uacpi_namespace_node *sb = uacpi_namespace_get_predefined(UACPI_PREDEFINED_NAMESPACE_SB);
    if (!sb) return false;

    // 保存上一次找到的节点
    uacpi_namespace_node *current = (uacpi_namespace_node *)*iterator;
    uacpi_namespace_node *next = NULL;

    // 如果迭代器为 NULL，从 \_SB 的第一个子节点开始
    if (current == NULL) 
        current = sb;

    // 遍历命名空间
    while (uacpi_namespace_node_next(current, &next) == UACPI_STATUS_OK) {
        // 只关心 Device 类型
        uacpi_bool is_device;
        if (uacpi_namespace_node_is(next, UACPI_OBJECT_DEVICE, &is_device) != UACPI_STATUS_OK || !is_device) {
            current = next;
            continue;
        }

        // 获取 _HID
        uacpi_id_string *hid_str = NULL;
        if (uacpi_eval_hid(next, &hid_str) != UACPI_STATUS_OK || !hid_str) {
            current = next;
            continue;
        }

        // 填充 HID
        if (hid) {
            strncpy(hid, hid_str->value, PLATFORM_MAX_HID_COUNT - 1);
            hid[PLATFORM_MAX_HID_COUNT - 1] = '\0';
        }

        uacpi_free_id_string(hid_str);

        // 填充 UID：若获取失败或无值，则使用默认值
        if (uid) {
            uacpi_id_string *uid_str = NULL;
            if (
                uacpi_eval_uid(next, &uid_str) == UACPI_STATUS_OK && 
                uid_str && 
                uid_str->value[0] != '\0'
            ) {
                strncpy(uid, uid_str->value, PLATFORM_MAX_UID_COUNT - 1);
                uid[PLATFORM_MAX_UID_COUNT - 1] = '\0';
                uacpi_free_id_string(uid_str);
            } else {
                uid[0] = '0';
                uid[1] = '\0';
                if (uid_str) uacpi_free_id_string(uid_str);
            }
        }

        // 填充 SEG
        if (seg) {
            uacpi_u64 val = 0;
            if (uacpi_eval_integer(next, "_SEG", NULL, &val) != UACPI_STATUS_OK) {
                val = 0;
            }
            *seg = (uint16_t)val;
        }

        // 更新迭代器
        *iterator = (void *)next;
        return true;
    }

    // 没有更多了
    return false;
}

/**
 * 生成 "HID:UID" 格式的设备名
 * 
 * @param hid 硬件 ID
 * @param uid 实例 ID
 * @param buf 输出的缓存区
 * 
 * 调用者需要确保输出缓存区至少为 PLATFORM_NAME_BUF_SIZE
 */
void platform_create_name(const char *hid, const char *uid, char *buf) {
    size_t hid_len = strlen(hid);
    memcpy(buf, hid, hid_len);
    buf[hid_len] = ':';
    strcpy(buf + hid_len + 1, uid);  
}

/**
 * 从 name 中提取 HID 到 out_buf
 * 
 * @param name 设备名
 * @param buf 输出缓冲区,失败时 buf[0] = '\0'
 *
 * 调用者需要确保输出缓存区至少为 PLATFORM_NAME_BUF_SIZE
 */
void platform_name_to_hid(const char *name, char *buf) {
    const char *colon = strchr(name, ':');
    size_t len = colon ? (size_t)(colon - name) : strlen(name);
    memcpy(buf, name, len);
    buf[len] = '\0';
}

/**
 * 用于匹配设备与驱动
 *
 * @param dev 设备结构体
 * @param drv 驱动结构体
 */
static bool platform_match(struct device *dev, struct driver *drv) {
    char hid[PLATFORM_MAX_NAME_COUNT];
    platform_name_to_hid(dev->name, hid);

    return strcmp(hid, drv->name) == 0;
}

/**
 * 释放设备私有数据
 * 
 * @param dev 设备结构体指针
 */
static void platform_free_device(struct device *dev) {
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    kheap_free(pdev);
}

struct bus platform_bus_type = {
    .name = "platform",
    .match = platform_match,
    .devices = {0},
    .drivers = {0},
    .lock = SPIN_LOCK_INIT,
    .node = {0},
    .priv = NULL,
    .free_device = platform_free_device,
};

// 初始化
bool platform_dev_init(void) {
    INIT_LIST_HEAD(&platform_bus_type.devices);
    INIT_LIST_HEAD(&platform_bus_type.drivers);
    INIT_LIST_HEAD(&platform_bus_type.node);

    // 注册平台总线类型
    if (!drivers_add_bus(&platform_bus_type))
        return false;

    // 遍历 ACPI 命名空间，为每个 PCI 主桥创建平台设备
    void *iter = NULL;                        // 迭代器初始为 NULL，表示从头开始
    char hid[PLATFORM_MAX_HID_COUNT];
    char uid[PLATFORM_MAX_UID_COUNT];
    uint16_t seg;

    while (platform_next_get_info(&iter, hid, uid, &seg)) {
        // 分配平台设备结构体
        struct platform_device *pdev = kheap_alloc(sizeof(*pdev));
        if (!pdev)
            PLATFORM_DEV_PANIC("out of memory allocating platform_device");

        memset(pdev, 0, sizeof(*pdev));

        // 构造设备名 
        char name[PLATFORM_MAX_NAME_COUNT];
        platform_create_name(hid, uid, name);

        pdev->dev.name = strdup(name);
        if (!pdev->dev.name)
            PLATFORM_DEV_PANIC("strdup failed");

        pdev->dev.bus = &platform_bus_type;
        pdev->segment = seg;                 // 保存段组号，供主桥驱动使用

        atomic_init(&pdev->dev.refcnt, 1);
        INIT_LIST_HEAD(&pdev->dev.children);
        INIT_LIST_HEAD(&pdev->dev.sibling);
        INIT_LIST_HEAD(&pdev->dev.node);
        INIT_LIST_HEAD(&pdev->dev.unmatched_node);

        // 注册到驱动框架, 等待后续驱动注册后 probe
        drivers_add_device(&pdev->dev);
    }

    return true;
}

// 获取段组号
uint16_t platform_get_segment(struct device *dev) {
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    return pdev->segment;
}