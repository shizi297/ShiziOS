/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include "core.h"
#include <klibc.h>
#include <minmax.h>
#include <list.h>
#include <dynarr.h>
#include <bootboot.h>
#include <stdatomic.h>
#include <asm/mm_addr.h>

#define VIRTIO_ISR_GET_DIDX(index)  ((uint32_t)((index) & 0xFFFFFFFF))
#define VIRTIO_ISR_GET_QIDX(index)  ((uint32_t)(((index) >> 32) & 0xFFFFFFFF))
#define VIRTIO_ISR_PACK(didx, qidx) (((uint64_t)(qidx) << 32) | (uint64_t)(didx))

#define VIRTIO_PACKED_EVENT_SIZE       sizeof(struct pvirtq_event_suppress)

#define VIRTIO_PACKED_DEVICE_EVENT_OFFSET(vq) \
    ((size_t)(vq)->size * sizeof(struct virtio_desc))

#define VIRTIO_PACKED_DRIVER_EVENT_OFFSET(vq) \
    ((size_t)(vq)->size * sizeof(struct virtio_desc) + VIRTIO_PACKED_EVENT_SIZE)

// 位域掩码
#define VIRTIO_EVENT_FLAGS_MASK         0x3
#define VIRTIO_EVENT_OFF_SHIFT          2
#define VIRTIO_EVENT_OFF_MASK           0x7FFF
#define VIRTIO_EVENT_WRAP_MASK          0x1

// 字段提取
#define VIRTIO_EVENT_GET_FLAGS(ev)  ((ev)->lo & VIRTIO_EVENT_FLAGS_MASK)
#define VIRTIO_EVENT_GET_OFF(ev)    ((ev)->lo >> VIRTIO_EVENT_OFF_SHIFT)
#define VIRTIO_EVENT_GET_WRAP(ev)   ((ev)->hi & VIRTIO_EVENT_WRAP_MASK)

// 字段构造
#define VIRTIO_EVENT_MAKE_LO(flags, off) \
    (((flags) & VIRTIO_EVENT_FLAGS_MASK) | \
     (((off) & VIRTIO_EVENT_OFF_MASK) << VIRTIO_EVENT_OFF_SHIFT))

#define VIRTIO_EVENT_MAKE_HI(wrap) \
    ((wrap) & VIRTIO_EVENT_WRAP_MASK)

#define VIRTIO_MAX_VIRTQUEUE_SIZE   1024

struct virtio_device_type {
    const char *name;   // 设备类型名
    dynarr_t *devices;  // 已注册的设备
};

// 描述符标志位
typedef enum : uint16_t {
    VIRTQ_DESC_F_NEXT     = (1 << 0),  // 有下一个
    VIRTQ_DESC_F_WRITE    = (1 << 1),  // 设备可写
    VIRTQ_DESC_F_INDIRECT = (1 << 2),  // 间接描述符表
    VIRTQ_DESC_F_AVAIL    = (1 << 7),  // 驱动已提交，硬件可处理（仅 Packed Ring）
    VIRTQ_DESC_F_USED     = (1 << 15), // 硬件已完成，驱动可回收（仅 Packed Ring）
} virtio_desc_flags_t;

/*
 * Packed Ring Event Suppression
 *
 * struct pvirtq_event_suppress {
 *     le16 {
 *         desc_event_off : 15;
 *         desc_event_wrap : 1;
 *     } desc;
 *     le16 {
 *         desc_event_flags : 2;
 *         reserved : 14;
 *     } flags;
 * };
 */
struct pvirtq_event_suppress {
    uint16_t lo;
    uint16_t hi;
} __attribute__((packed));

typedef enum : uint16_t {
    RING_EVENT_FLAGS_ENABLE  = 0x0,   // 启用事件
    RING_EVENT_FLAGS_DISABLE = 0x1,   // 禁用事件
    RING_EVENT_FLAGS_DESC    = 0x2,   // 指定描述符事件
} pvirtq_event_flags_t;

static uint8_t virtio_device_counter = 0;
static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

static bool create_indirect_disabled(
    struct virtioqueue *vq,
    struct device *dev,
    uint32_t count,
    uint16_t *desc_idx
) {
    (void)vq;
    (void)dev;
    (void)count;
    (void)desc_idx;
    return false;
}

static bool create_indirect_enabled(
    struct virtioqueue *vq,
    struct device *dev,
    uint32_t count,
    uint16_t *desc_idx
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv->feature_ops.create_indirect_raw(vq, dev,count, desc_idx);
}

static bool check_notify_disabled(struct virtioqueue *vq, struct device *dev) {
    (void)vq;
    (void)dev;
    return true;
}

static bool check_notify_enabled(struct virtioqueue *vq, struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv->feature_ops.check_notify_raw(vq);
}

static uint64_t get_notify_data_nodata(struct virtioqueue *vq, struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return VIRTIO_PACK_NOTIFICATION_DATA(priv->feature_ops.get_notify_vqn(vq), 0, 0, 0);
}

static uint64_t get_notify_data_data(struct virtioqueue *vq, struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv->feature_ops.get_notify_data_raw(vq, dev);
}

static uint16_t get_notify_vqn_index(struct virtioqueue *vq) {
    return vq->notify_default_idx;
}

static uint16_t get_notify_vqn_data(struct virtioqueue *vq) {
    return vq->queue_notify_data;
}

static bool alloc_desc_inorder(
    struct virtioqueue *vq,
    struct device *dev,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv->feature_ops.alloc_inorder(vq, count, desc_idx, last_idx);
}

static bool alloc_desc_noorder(
    struct virtioqueue *vq,
    struct device *dev,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv->feature_ops.alloc_noorder(vq, count, desc_idx, last_idx);
}

static bool _split_alloc_desc(
    struct virtioqueue *vq,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    uint16_t head = vq->split.free_desc;
    if (head == 0xFFFF)
        return false;

    // 沿 next 指针遍历 count 步
    uint16_t cur = head;
    for (uint32_t i = 0; i < count - 1; i++) {
        if (cur == 0xFFFF)
            return false;

        cur = vq->desc[cur].split.next;
    }

    if (cur == 0xFFFF)
        return false;

    // cur 是最后一个槽位，cur->next 是剩余链表头
    *desc_idx = head;
    *last_idx = cur;
    vq->split.free_desc = vq->desc[cur].split.next;

    return true;
}

static bool split_alloc_desc_inorder(
    struct virtioqueue *vq,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    return _split_alloc_desc(vq, count, desc_idx, last_idx);
}

static bool split_alloc_desc_noorder(
    struct virtioqueue *vq,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    return _split_alloc_desc(vq, count, desc_idx, last_idx);
}

static void split_fill_desc(
    struct virtioqueue *vq,
    uint16_t desc_idx,
    struct scatterlist *sg,
    uint32_t out,
    uint32_t in
) {
    struct virtio_desc *desc = vq->desc;
    struct virtio_desc *base_desc;
    uint32_t total = out + in;
    bool is_indirect = desc[desc_idx].split.flags & VIRTQ_DESC_F_INDIRECT;

    // 间接描述符，数据写入间接表
    if (is_indirect) {
        base_desc = (struct virtio_desc *)PHYS_TO_LINEAR(desc[desc_idx].addr);
    } else {
        // 直接描述符，数据写入描述符环
        base_desc = desc;
    }

    uint16_t cur = desc_idx;

    // 填充所有协议描述符
    for (uint32_t i = 0; i < total; i++) {
        struct virtio_desc *target;

        if (is_indirect) {
            target = &base_desc[i];
        } else {
            target = &base_desc[cur];
            cur = target->split.next;
        }

        target->addr = sg[i].addr;
        target->len = sg[i].length;

        if (i < out) {
            target->split.flags = VIRTQ_DESC_F_NEXT;
        } else {
            target->split.flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
        }
    }

    // 最后一个协议描述符的 next 置 0，表示链结束
    if (is_indirect) {
        base_desc[total - 1].split.next = 0;
    } else {
        base_desc[cur].split.next = 0;
    }

    // 确保描述符内容写入完成后，再更新 avail_idx
    atomic_thread_fence(memory_order_release);

    // 更新 available ring
    uint16_t idx = vq->split.avail->idx;
    vq->split.avail->ring[idx % vq->size] = desc_idx;
    vq->split.avail->idx = idx + 1;
}

static void *split_recycle(struct virtioqueue *vq) {
    // 确保能读取到硬件最新写入的 used_idx
    atomic_thread_fence(memory_order_acquire);

    // 检查是否有新的完成项
    if (vq->last_used_idx == vq->split.used->idx)
        return NULL;

    // 获取已完成描述符的编号
    uint16_t desc_id = vq->split.used->ring[vq->last_used_idx % vq->size].id;

    // 从独立数组中取出 caller_data
    void *caller_data = vq->caller_data[desc_id];
    vq->caller_data[desc_id] = NULL;

    // 间接描述符：释放间接表，归还主描述符
    if (vq->desc[desc_id].split.flags & VIRTQ_DESC_F_INDIRECT) {
        struct virtio_desc *indirect = (struct virtio_desc *)PHYS_TO_LINEAR(vq->desc[desc_id].addr);
        kheap_free(indirect);
        vq->desc[desc_id].split.next = vq->split.free_desc;
        vq->split.free_desc = desc_id;
    } else {
        // 直接描述符：将整个描述符链归还到空闲链表
        struct virtio_desc *desc = vq->desc;
        uint16_t cur = desc_id;

        // 遍历到链末尾
        while (desc[cur].split.flags & VIRTQ_DESC_F_NEXT)
            cur = desc[cur].split.next;

        desc[cur].split.next = vq->split.free_desc;
        vq->split.free_desc = desc_id;
    }

    // 更新 last_used_idx
    vq->last_used_idx++;

    return caller_data;
}

static bool split_init(struct device *dev, struct virtioqueue *vq, uint64_t size) {
    if (size == 0)
        size = VIRTIO_MAX_VIRTQUEUE_SIZE;

    uint64_t desc_size = size * VIRTIO_DESC_SIZE;
    uint64_t avail_size = VIRTIO_AVAIL_HEADER + size * VIRTIO_AVAIL_ENTRY;
    uint64_t used_size = VIRTIO_USED_HEADER + size * VIRTIO_USED_ENTRY;
    uint64_t total_size = desc_size + avail_size + used_size;

    void *ring = kheap_alloc(total_size);
    if (!ring)
        return false;

    memset(ring, 0, total_size);

    vq->ring = ring;
    vq->ring_phys = LINEAR_TO_PHYS(ring);
    vq->size = size;

    uint8_t *base = (uint8_t *)ring;
    vq->desc = (struct virtio_desc *)base;
    vq->split.avail = (struct virtio_avail *)(base + desc_size);
    vq->split.used = (struct virtio_used *)(base + desc_size + avail_size);

    vq->desc_phys = vq->ring_phys;
    vq->avail_phys = vq->ring_phys + desc_size;
    vq->used_phys = vq->ring_phys + desc_size + avail_size;

    // 初始化空闲描述符链表
    for (uint64_t i = 0; i < size - 1; i++)
        vq->desc[i].split.next = (uint16_t)(i + 1);
    vq->desc[size - 1].split.next = 0xFFFF;
    vq->split.free_desc = 0;
    vq->last_used_idx = 0;

    // 分配 caller_data 数组
    vq->caller_data = kheap_alloc(size * sizeof(void *));
    if (!vq->caller_data) {
        kheap_free(ring);
        return false;
    }
    memset(vq->caller_data, 0, size * sizeof(void *));

    // 确保初始化完成后硬件可见
    atomic_thread_fence(memory_order_release);

    return true;
}

static bool split_create_indirect_raw(
    struct virtioqueue *vq,
    struct device *dev,
    uint32_t count,
    uint16_t *desc_idx
) {
    // 分配间接描述符表
    uint64_t table_size = count * sizeof(struct virtio_desc);
    struct virtio_desc *table = kheap_alloc(table_size);
    if (!table)
        return false;

    memset(table, 0, table_size);

    // 从空闲链表获取一个主槽位
    uint16_t head, last;
    if (!_split_alloc_desc(vq, 1, &head, &last)) {
        kheap_free(table);
        return false;
    }

    // 设置主槽位指向间接表
    vq->desc[head].addr = LINEAR_TO_PHYS((uintptr_t)table);
    vq->desc[head].len = table_size;
    vq->desc[head].split.flags = VIRTQ_DESC_F_INDIRECT;

    *desc_idx = head;
    return true;
}

static bool split_check_notify_raw(struct virtioqueue *vq) {
    uint16_t *avail_event = 
        (uint16_t *)(
            (char *)vq->split.used + 
            sizeof(struct virtio_used) + 
            vq->size * sizeof(struct virtio_used_elem)
        );

    return vq->split.avail->idx == *avail_event;
}

static uint64_t split_get_notify_data_raw(struct virtioqueue *vq, struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    uint16_t idx = vq->split.avail->idx;
    uint16_t next_off = idx & (vq->size - 1);
    uint8_t next_wrap = (idx >> 15) & 1;
    return VIRTIO_PACK_NOTIFICATION_DATA(priv->feature_ops.get_notify_vqn(vq), next_off, next_wrap, 1);
}

static bool packed_alloc_desc_inorder(
    struct virtioqueue *vq,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    uint16_t start = vq->packed.next_avail_idx;

    *desc_idx = start;
    *last_idx = (start + count - 1) % vq->size;

    // 推进 next_avail_idx，处理回绕
    uint16_t new_next = (start + count) % vq->size;
    if (new_next < start)
        vq->packed.avail_wrap_count ^= 1;
    vq->packed.next_avail_idx = new_next;

    return true;
}

static bool packed_alloc_desc_noorder(
    struct virtioqueue *vq,
    uint32_t count,
    uint16_t *desc_idx,
    uint16_t *last_idx
) {
    uint16_t start = vq->packed.next_avail_idx;

    // 检查连续 count 个槽位是否全部空闲
    for (uint32_t i = 0; i < count; i++) {
        uint16_t idx = (start + i) % vq->size;
        if (vq->desc[idx].packed.flags & (VIRTQ_DESC_F_AVAIL | VIRTQ_DESC_F_USED))
            return false;
    }

    *desc_idx = start;
    *last_idx = (start + count - 1) % vq->size;

    // 推进 next_avail_idx
    uint16_t new_next = (start + count) % vq->size;
    if (new_next < start)
        vq->packed.avail_wrap_count ^= 1;   // 回绕
    vq->packed.next_avail_idx = new_next;

    return true;
}

static void packed_fill_desc(
    struct virtioqueue *vq,
    uint16_t desc_idx,
    struct scatterlist *sg,
    uint32_t out,
    uint32_t in
) {
    struct virtio_desc *desc = vq->desc;
    struct virtio_desc *base_desc;
    uint16_t avail_flag = vq->packed.avail_wrap_count ? VIRTQ_DESC_F_AVAIL : 0;
    uint32_t total = out + in;
    bool is_indirect = desc[desc_idx].packed.flags & VIRTQ_DESC_F_INDIRECT;

    // 间接描述符，数据写入间接表
    if (is_indirect) {
        base_desc = (struct virtio_desc *)PHYS_TO_LINEAR(desc[desc_idx].addr);
    } else {
        // 直接描述符，数据写入描述符环
        base_desc = desc;
    }

    uint16_t cur = desc_idx;

    // 填充所有协议描述符
    for (uint32_t i = 0; i < total; i++) {
        struct virtio_desc *target;
        uint16_t id = (uint16_t)i;

        if (is_indirect) {
            target = &base_desc[i];
        } else {
            target = &base_desc[cur];
            cur = (cur + 1) % vq->size;
        }

        target->addr = sg[i].addr;
        target->len = sg[i].length;
        target->packed.id = id;

        if (i < out) {
            target->packed.flags = VIRTQ_DESC_F_NEXT | avail_flag;
        } else {
            target->packed.flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT | avail_flag;
        }
    }

    // 最后一个协议描述符的 flags 移除 NEXT，表示链结束
    if (is_indirect) {
        base_desc[total - 1].packed.flags &= ~VIRTQ_DESC_F_NEXT;
    } else {
        base_desc[cur].packed.flags &= ~VIRTQ_DESC_F_NEXT;
    }

    // 确保描述符内容写入完成后硬件可见
    atomic_thread_fence(memory_order_release);
}

static void *packed_recycle(struct virtioqueue *vq) {
    // 确保能读取到硬件最新写入的 USED 标志
    atomic_thread_fence(memory_order_acquire);

    uint16_t idx = vq->last_used_idx % vq->size;
    uint16_t used_flag = vq->packed.used_wrap_count ? VIRTQ_DESC_F_USED : 0;

    // 检查当前槽位是否被设备标记为 USED
    if ((vq->desc[idx].packed.flags & VIRTQ_DESC_F_USED) != used_flag)
        return NULL;

    struct virtio_desc *desc = vq->desc;
    uint16_t desc_id = idx;
    void *caller_data;

    // 从独立数组中取出 caller_data
    caller_data = vq->caller_data[desc_id];
    vq->caller_data[desc_id] = NULL;

    // 间接描述符：释放间接表，归还主描述符
    if (desc[desc_id].packed.flags & VIRTQ_DESC_F_INDIRECT) {
        struct virtio_desc *indirect = (struct virtio_desc *)PHYS_TO_LINEAR(desc[desc_id].addr);
        kheap_free(indirect);
        desc[desc_id].addr = 0;
        desc[desc_id].len = 0;
        desc[desc_id].packed.flags = 0;
        desc[desc_id].packed.id = 0;

        // 推进 last_used_idx
        vq->last_used_idx = (vq->last_used_idx + 1) % vq->size;
        if (vq->last_used_idx == 0)
            vq->packed.used_wrap_count ^= 1;

        return caller_data;
    }

    // 直接描述符：计算链长度，取回 caller_data，清除所有槽位并归还
    uint16_t cur = desc_id;

    // 遍历到链末尾
    while (desc[cur].packed.flags & VIRTQ_DESC_F_NEXT)
        cur = (cur + 1) % vq->size;

    // 计算链长度
    uint16_t chain_len = 1;
    uint16_t tmp = desc_id;
    while (desc[tmp].packed.flags & VIRTQ_DESC_F_NEXT) {
        chain_len++;
        tmp = (tmp + 1) % vq->size;
    }

    // 遍历整条描述符链，清除所有槽位
    uint16_t clear_idx = desc_id;
    while (1) {
        desc[clear_idx].addr = 0;
        desc[clear_idx].len = 0;
        desc[clear_idx].packed.flags = 0;
        desc[clear_idx].packed.id = 0;

        if (clear_idx == cur)
            break;

        clear_idx = (clear_idx + 1) % vq->size;
    }

    // 更新 last_used_idx
    uint16_t new_last = (vq->last_used_idx + chain_len) % vq->size;
    if (vq->last_used_idx + chain_len >= vq->size)
        vq->packed.used_wrap_count ^= 1;

    vq->last_used_idx = new_last;

    return caller_data;
}

static bool packed_init(struct device *dev, struct virtioqueue *vq, uint64_t size) {
    if (size == 0)
        size = VIRTIO_MAX_VIRTQUEUE_SIZE;

    uint64_t total_size = size * sizeof(struct virtio_desc);
    void *ring = kheap_alloc(total_size);
    if (!ring)
        return false;

    memset(ring, 0, total_size);

    vq->ring = ring;
    vq->ring_phys = LINEAR_TO_PHYS(ring);
    vq->size = size;
    vq->desc = (struct virtio_desc *)ring;

    // Packed Ring 只有一个描述符环，三个地址相同
    vq->desc_phys = vq->ring_phys;
    vq->avail_phys = vq->ring_phys;
    vq->used_phys = vq->ring_phys;

    vq->last_used_idx = 0;

    // 初始化 Packed Ring 状态变量
    vq->packed.next_avail_idx = 0;
    vq->packed.avail_wrap_count = 1;
    vq->packed.used_wrap_count = 1;

    // 分配 caller_data 数组
    vq->caller_data = kheap_alloc(size * sizeof(void *));
    if (!vq->caller_data) {
        kheap_free(ring);
        return false;
    }
    memset(vq->caller_data, 0, size * sizeof(void *));

    // 确保初始化完成后硬件可见
    atomic_thread_fence(memory_order_release);

    return true;
}

static bool packed_create_indirect_raw(
    struct virtioqueue *vq,
    struct device *dev,
    uint32_t count,
    uint16_t *desc_idx
) {
    struct virtio_dev_priv *priv = dev->driver_data;

    // 分配间接描述符表
    uint64_t table_size = count * sizeof(struct virtio_desc);
    struct virtio_desc *table = kheap_alloc(table_size);
    if (!table)
        return false;

    memset(table, 0, table_size);

    // 从空闲池获取一个主槽位
    uint16_t head, last;
    if (!priv->feature_ops.alloc_desc(vq, dev, 1, &head, &last)) {
        kheap_free(table);
        return false;
    }

    // 设置主槽位指向间接表
    vq->desc[head].addr = LINEAR_TO_PHYS((uintptr_t)table);
    vq->desc[head].len = table_size;
    vq->desc[head].packed.flags = VIRTQ_DESC_F_INDIRECT | VIRTQ_DESC_F_AVAIL;

    *desc_idx = head;
    return true;
}

static bool packed_check_notify_raw(struct virtioqueue *vq) {
    struct pvirtq_event_suppress *ev =
        (struct pvirtq_event_suppress *)((char *)vq->desc + VIRTIO_PACKED_DEVICE_EVENT_OFFSET(vq));

    uint16_t flags = VIRTIO_EVENT_GET_FLAGS(ev);

    if (flags == RING_EVENT_FLAGS_DISABLE)
        return false;
    if (flags == RING_EVENT_FLAGS_ENABLE)
        return true;

    uint16_t off = VIRTIO_EVENT_GET_OFF(ev);
    uint8_t wrap = VIRTIO_EVENT_GET_WRAP(ev);

    return (
        vq->packed.next_avail_idx == off &&
        vq->packed.avail_wrap_count == wrap
    );
}

static uint64_t packed_get_notify_data_raw(struct virtioqueue *vq, struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;

    return VIRTIO_PACK_NOTIFICATION_DATA(
        priv->feature_ops.get_notify_vqn(vq),
        vq->packed.next_avail_idx,
        vq->packed.avail_wrap_count,
        1
    );
}

static bool virtio_match(struct device *dev, struct driver *drv) {
    struct virtio_dev_priv *priv = dev->driver_data;
    const struct virtio_device_id *id_table = drv->id_table;

    if (!priv || !id_table)
        return false;

    for (const struct virtio_device_id *id = id_table; id->type != 0; id++) {
        if (id->type == priv->type)
            return true;
    }

    return false;
}

static void virtio_free_device(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    if (priv)
        kheap_free(priv);
    if (dev->name)
        kheap_free((void *)dev->name);
}

struct bus virtio_bus_type = {
    .name = "virtio",
    .match = virtio_match,
    .devices = {0},
    .drivers = {0},
    .lock = SPIN_LOCK_INIT,
    .node = {0},
    .priv = NULL,
    .free_device = virtio_free_device,
};

static void virtio_format_device_name(char *buf, uint16_t type, uint8_t count) {
    const char dec[] = "0123456789";

    buf[0] = dec[(type / 10) % 10];
    buf[1] = dec[type % 10];
    buf[2] = ':';

    buf[3] = dec[(count / 100) % 10];
    buf[4] = dec[(count / 10) % 10];
    buf[5] = dec[count % 10];
    buf[6] = '\0';
}

int virtio_probe(struct device *dev, struct virtio_dev_ops *ops, struct device *parent) {
    int ret = -ENODEV;
    struct virtio_dev_priv *priv;
    struct device *virtio_dev = NULL;
    char *name = NULL;

    // 调用传输层的硬件初始化
    priv = ops->init(dev);
    if (!priv)
        goto err_ret;

    // 后面失败均为内存不足
    ret = -ENOMEM;

    // 创建虚拟设备
    virtio_dev = kheap_alloc(sizeof(*virtio_dev));
    if (!virtio_dev)
        goto err_destroy;

    // 构造设备名
    name = kheap_alloc(VIRTIO_DEVICE_NAME_LEN);
    if (!name)
        goto err_free_device;

    memset(virtio_dev, 0, sizeof(*virtio_dev));
    virtio_format_device_name(name, priv->type, virtio_device_counter++);
    virtio_dev->name = name;
    virtio_dev->bus = &virtio_bus_type;
    virtio_dev->parent = parent;
    virtio_dev->driver_data = priv;

    atomic_init(&virtio_dev->refcnt, 0);
    INIT_LIST_HEAD(&virtio_dev->children);
    INIT_LIST_HEAD(&virtio_dev->sibling);
    INIT_LIST_HEAD(&virtio_dev->node);
    INIT_LIST_HEAD(&virtio_dev->unmatched_node);

    // 注册到 VirtIO 总线
    drivers_add_device(virtio_dev);
    return 0;

err_free_device:
    kheap_free(virtio_dev);
err_destroy:
    kheap_free(priv);
err_ret:
    return ret;
}

void virtio_remove(struct device *phys_dev, struct device *virtio_root) {
    struct device *virt_dev;
    struct virtio_dev_priv *priv;
    struct virtio_bus_priv *bus_priv = virtio_root->bus->priv;
    struct virtio_dev_ops *ops = bus_priv->ops;

    // 遍历 VirtIO 总线上的虚拟设备，查找与物理设备关联的虚拟设备
    list_for_each_entry(virt_dev, &virtio_bus_type.devices, node) {
        priv = virt_dev->driver_data;
        if (priv && priv->pdev == phys_dev) {
            ops->destroy(virt_dev);
            drivers_remove_device(virt_dev);
            return;
        }
    }
}

/**
 * 设置驱动私有数据
 * 
 * @param dev 虚拟设备
 * @param data 数据指针
 */
void virtio_set_drvdata(struct device *dev, void *data) {
    struct virtio_dev_priv *priv = dev->driver_data;
    if (priv)
        priv->driver_data = data;
}

/**
 * 获取设备私有数据
 * 
 * @param dev 虚拟设备
 * 
 * @return 数据指针
 */
void *virtio_get_drvdata(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv ? priv->driver_data : NULL;
}

/**
 * 写入特性位
 * 
 * @param dev 虚拟设备
 * @param features 要写入的特性
 */
void virtio_write_features(struct device *dev, uint64_t features) {
    struct virtio_bus_priv *bus_priv = dev->bus->priv;

    if (!bus_priv || !bus_priv->ops || !bus_priv->ops->write_features)
        return;

    bus_priv->ops->write_features(dev, features);
}

/**
 * 获取所有支持的特性
 * 
 * @param dev 虚拟设备
 */
uint64_t virtio_get_features(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    return priv ? priv->features : 0;
}

/**
 * 从设备配置空间读取数据
 * 
 * @param dev 虚拟设备
 * @param offset 配置空间偏移
 * @param buf 接收缓冲区
 * @param len 读取长度
 */
void virtio_read_device_config(struct device *dev, uint32_t offset, void *buf, size_t len) {
    struct virtio_bus_priv *bus_priv = dev->bus->priv;
    if (bus_priv && bus_priv->ops && bus_priv->ops->read_device_config)
        bus_priv->ops->read_device_config(dev, offset, buf, len);
}

/**
 * 向设备配置空间写入数据
 * 
 * @param dev 虚拟设备
 * @param offset 配置空间偏移
 * @param buf 数据缓冲区
 * @param len 写入长度
 */
void virtio_write_device_config(struct device *dev, uint32_t offset, const void *buf, size_t len) {
    struct virtio_bus_priv *bus_priv = dev->bus->priv;
    if (bus_priv && bus_priv->ops && bus_priv->ops->write_device_config)
        bus_priv->ops->write_device_config(dev, offset, buf, len);
}

/*
 * 为一类设备申请句柄
 *
 * @param name 设备类型名
 * 
 * @return 句柄指针
 */
struct virtio_device_type *virtio_register_type(const char *name) {
    struct virtio_device_type *type;

    type = kheap_alloc(sizeof(*type));
    if (!type)
        return NULL;

    type->name = strdup(name);
    if (!type->name) {
        kheap_free(type);
        return NULL;
    }

    type->devices = dynarr_create(sizeof(struct device *), 0);
    if (!type->devices) {
        kheap_free((void *)(char *)type->name);
        kheap_free(type);
        return NULL;
    }

    return type;
}

/*
 * 将设备注册到句柄下
 *
 * @param type 句柄
 * @param dev 虚拟设备
 */
bool virtio_register_device(struct virtio_device_type *type, struct device *dev) {
    if (!type || !dev)
        return false;

    // 遍历数组，查找第一个 NULL 槽位
    uint64_t count = dynarr_count(type->devices);
    for (uint64_t i = 0; i < count; i++) {
        struct device **slot = (struct device **)dynarr_get(type->devices, i);
        if (slot && *slot == NULL) {
            // 找到空位，填入设备指针
            dynarr_set(type->devices, i, &dev);
            return true;
        }
    }

    // 没有空位，追加到末尾
    return dynarr_append(type->devices, &dev);
}

/*
 * 从句柄注销设备
 *
 * @param type 句柄
 * @param dev 虚拟设备
 */
void virtio_unregister_device(struct virtio_device_type *type, struct device *dev) {
    struct device *null_ptr = NULL;

    if (!type || !dev)
        return;

    for (uint64_t i = 0; i < dynarr_count(type->devices); i++) {
        struct device **entry = dynarr_get(type->devices, i);
        if (entry && *entry == dev) {
            dynarr_set(type->devices, i, &null_ptr);
            return;
        }
    }
}

/*
 * 释放句柄
 *
 * @param type 句柄
 */
void virtio_unregister_type(struct virtio_device_type *type) {
    if (!type)
        return;

    dynarr_destroy(type->devices);
    kheap_free((void *)(char *)type->name);
    kheap_free(type);
}

/*
 * 从句柄的动态数组中获取指定索引的设备指针
 *
 * @param type 设备类型句柄
 * @param index 设备在动态数组中的索引
 * 
 * @return 设备指针
 */
static inline struct device *virtio_get_device(struct virtio_device_type *type, uint64_t index) {
    if (!type || !type->devices)
        return NULL;

    struct device **entry = (struct device **)dynarr_get(type->devices, index);
    if (!entry)
        return NULL;

    return *entry;
}

/**
 * 初始化 VIRTIO 设备，用于后续队列操作
 * 
 * @param dev VIRTIO 设备
 */
bool virtio_device_init(struct device *dev) {
    struct virtio_dev_priv *priv = dev->driver_data;
    if (!priv)
        return false;

    // 遍历所有特性位，根据协商结果填充操作表
    for (int bit = VIRTIO_F_START; bit <= VIRTIO_F_END; bit++) {
        bool enabled = (priv->features & (1ULL << bit)) != 0;
        struct virtio_feature_ops *ops = &priv->feature_ops;

        switch (bit) {
            case VIRTIO_F_INDIRECT_DESC:
                if (!enabled) {
                    ops->create_indirect = create_indirect_disabled; 
                } else {
                    ops->create_indirect = create_indirect_enabled; 
                }
                break;
            case VIRTIO_F_EVENT_IDX:
                if (!enabled) {
                    ops->check_notify = check_notify_disabled;
                } else {
                    ops->check_notify = check_notify_enabled;
                }
                break;
            case VIRTIO_F_RING_PACKED:
                if (!enabled) {
                    ops->alloc_inorder = split_alloc_desc_inorder;
                    ops->alloc_noorder = split_alloc_desc_noorder;
                    ops->fill_desc = split_fill_desc;
                    ops->recycle = split_recycle;
                    ops->init = split_init;
                    ops->create_indirect_raw = split_create_indirect_raw;
                    ops->check_notify_raw = split_check_notify_raw;
                    ops->get_notify_data_raw = split_get_notify_data_raw;
                } else {
                    ops->alloc_inorder = packed_alloc_desc_inorder;
                    ops->alloc_noorder = packed_alloc_desc_noorder;
                    ops->fill_desc = packed_fill_desc;
                    ops->recycle = packed_recycle;
                    ops->init = packed_init;
                    ops->create_indirect_raw = packed_create_indirect_raw;
                    ops->check_notify_raw = packed_check_notify_raw;
                    ops->get_notify_data_raw = packed_get_notify_data_raw;
                }
                break;
            case VIRTIO_F_IN_ORDER:
                if (!enabled) {
                    ops->alloc_desc = alloc_desc_noorder;
                } else {
                    ops->alloc_desc = alloc_desc_inorder;
                }
                break;
            case VIRTIO_F_NOTIFICATION_DATA:
                if (!enabled) {
                    ops->get_notify_data = get_notify_data_nodata;
                } else {
                    ops->get_notify_data = get_notify_data_data;
                }
                break;
            case VIRTIO_F_NOTIF_CONFIG_DATA:
                if (!enabled) {
                    ops->get_notify_vqn = get_notify_vqn_index;
                } else {
                    ops->get_notify_vqn = get_notify_vqn_data;
                }
                break;
            default:
                break;
        }
    }

    return true;
}

/**
 * 创建并激活所有队列
 *
 * @param dev 虚拟设备
 * @param queue_size 每个队列的大小（0 表示使用默认值）
 * @param vector 中断向量号
 * @param max_queues 硬件支持的最大队列数
 * @param num_queues_out 输出参数，实际创建的队列数
 */
int virtioqueue_set_all(
    struct device *dev,
    uint64_t queue_size,
    uint32_t vector,
    uint32_t max_queues,
    int *num_queues_out
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtio_bus_priv *bus_priv = (struct virtio_bus_priv *)dev->bus->priv;
    struct virtio_dev_ops *ops = bus_priv->ops;
    struct virtioqueue *vq = NULL;
    int ret = -EINVAL;
    uint32_t i;

    if (!num_queues_out)
        goto err_ret;

    // 默认队列大小
    if (queue_size == 0)
        queue_size = VIRTIO_MAX_VIRTQUEUE_SIZE;

    uint32_t num = min((uint32_t)max_queues, (uint32_t)bootboot->numcores);
    if (num == 0)
        goto err_ret;

    // 分配 vqs 数组用于存放 vq 
    priv->vqs = kheap_alloc(num * sizeof(struct virtioqueue *));
    if (!priv->vqs) {
        ret = -ENOMEM;
        goto err_ret;
    }

    memset(priv->vqs, 0, num * sizeof(struct virtioqueue *));

    // 创建并激活队列
    for (i = 0; i < num; i++) {
        vq = kheap_alloc(sizeof(*vq));
        if (!vq) {
            ret = -ENOMEM;
            goto err_free_vqs;
        }

        memset(vq, 0, sizeof(*vq));
        vq->queue_index = i;

        if (!priv->feature_ops.init(dev, vq, queue_size)) {
            ret = -ENOMEM;
            goto err_free_vq;
        }

        uint32_t logical_id = i % bootboot->numcores;
        if (!ops->set_vq(dev, i, vq, logical_id, vector)) {
            ret = -ENODEV;
            goto err_free_vq_ring;
        }

        priv->vqs[i] = vq;
    }

    priv->num_vqs = num;
    *num_queues_out = (int)num;
    return 0;

err_free_vq_ring:
    if (vq && vq->ring)
        kheap_free(vq->ring);
err_free_vq:
    if (vq)
        kheap_free(vq);
err_free_vqs:
    for (uint32_t j = 0; j < i; j++) {
        struct virtioqueue *old = priv->vqs[j];
        if (old) {
            if (old->ring)
                kheap_free(old->ring);
            kheap_free(old);
        }
    }
    kheap_free(priv->vqs);
    priv->vqs = NULL;
    priv->num_vqs = 0;
err_ret:
    return ret;
}

/**
 * 向指定队列提交请求
 *
 * @param dev 虚拟设备
 * @param queue_index 队列索引
 * @param sg 描述数据缓冲区的地址和长度
 * @param out 输出缓冲区数量
 * @param in 输入缓冲区数量
 * @param caller_data 驱动上下文，完成时通过 process_isr 返回
 */
int virtioqueue_add_buf(
    struct device *dev,
    uint32_t queue_index,
    struct scatterlist *sg,
    uint32_t out,
    uint32_t in,
    void *req
) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtioqueue *vq = priv->vqs[queue_index];
    uint32_t total = out + in;
    uint16_t desc_idx, last_idx;

    // 分配协议描述符
    if (!priv->feature_ops.alloc_desc(vq, dev, total, &desc_idx, &last_idx)) {
        // 直接分配失败，尝试间接描述符
        if (!priv->feature_ops.create_indirect(vq, dev, total, &desc_idx))
            return -ENOSPC;
    }

    // 填充描述符内容
    priv->feature_ops.fill_desc(vq, desc_idx, sg, out, in);

    // 保存 caller_data 到独立数组，供回收时使用
    vq->caller_data[desc_idx] = req;

    return 0;
}

/**
 * 通知指定队列有新请求
 *
 * @param dev 虚拟设备
 * @param queue_index 队列索引
 */
void virtioqueue_kick(struct device *dev, uint32_t queue_index) {
    struct virtio_dev_priv *priv = dev->driver_data;
    struct virtioqueue *vq = priv->vqs[queue_index];
    struct virtio_feature_ops *fops = &priv->feature_ops;
    struct virtio_bus_priv *bus_priv = (struct virtio_bus_priv *)dev->bus->priv;
    struct virtio_dev_ops *vops = bus_priv->ops;

    atomic_thread_fence(memory_order_release);

    if (fops->check_notify(vq, dev)) {
        uint64_t data = fops->get_notify_data(vq, dev);
        vops->notify(vq, dev, data);
    }
}

/**
 * 回收所有队列的已完成请求
 *
 * @param handle 设备类型句柄
 * @param index 存放当前查找进度
 * @param before 回收前回调
 * @param after 回收后回调
 *
 * @return 下一个 caller_data，无完成项时返回 NULL
 */
void *virtioqueue_process_isr(
    struct virtio_device_type *handle,
    uint64_t *index,
    virtio_isr_before_t before,
    virtio_isr_after_t after
) {
    if (!handle || !handle->devices || !index)
        return NULL;

    uint64_t count = dynarr_count(handle->devices);
    if (count == 0)
        return NULL;

    // 解析起始位置
    uint32_t start_didx = VIRTIO_ISR_GET_DIDX(*index);
    uint64_t start_qidx = VIRTIO_ISR_GET_QIDX(*index);

    // 如果起始设备索引越界，从头开始
    if (start_didx >= count) {
        start_didx = 0;
        start_qidx = 0;
    }

    uint32_t didx = start_didx;
    uint64_t qidx = start_qidx;
    struct virtio_dev_priv *priv = NULL;

    while (1) {
        struct device *dev = virtio_get_device(handle, didx);
        uint32_t num_q = 0;
        priv = NULL;

        if (dev) {
            priv = dev->driver_data;
            if (priv && priv->vqs)
                num_q = priv->num_vqs;
        }

        // 检查当前队列是否在有效范围内
        if (qidx < num_q) {
            struct virtioqueue *vq = priv->vqs[qidx];
            if (vq && priv->feature_ops.recycle) {
                void *ctx = before(priv, &qidx);
                void *data = priv->feature_ops.recycle(vq);
                after(ctx);
                if (data) {
                    // 成功回收，原地不动，更新外部索引并返回
                    *index = VIRTIO_ISR_PACK(didx, qidx);
                    return data;
                }
            }
            // 当前队列无完成项，推进到下一个队列
            qidx++;
        } else {
            // 当前设备无效或已经检查完所有队列，推进到下一个设备
            didx++;
            qidx = 0;
            if (didx >= count)
                didx = 0;
        }

        // 检查是否回到起始位置
        if (didx == start_didx && qidx == start_qidx)
            break;
    }

    return NULL;
}