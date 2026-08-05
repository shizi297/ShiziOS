/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <drivers/virtio.h>
#include <drivers/drivers.h>
#include <initcall.h>
#include <block.h>
#include <klibc.h>
#include <asm/smp.h>
#include <spinlock.h>

#define VIRTIO_BLK_DEVICE_ID 2

#define VIRTIO_BLK_F_BARRIER         0   // Legacy 专用，屏障支持
#define VIRTIO_BLK_F_SIZE_MAX        1   // 单个 segment 最大字节数
#define VIRTIO_BLK_F_SEG_MAX         2   // 单个请求最大 segment 数
#define VIRTIO_BLK_F_GEOMETRY        4   // CHS 几何参数
#define VIRTIO_BLK_F_RO              5   // 只读设备
#define VIRTIO_BLK_F_BLK_SIZE        6   // 建议块大小
#define VIRTIO_BLK_F_FLUSH           9   // FLUSH 命令
#define VIRTIO_BLK_F_TOPOLOGY       10   // 拓扑信息
#define VIRTIO_BLK_F_CONFIG_WCE     11   // 动态写缓存
#define VIRTIO_BLK_F_MQ             12   // 多队列
#define VIRTIO_BLK_F_DISCARD        13   // DISCARD/TRIM
#define VIRTIO_BLK_F_WRITE_ZEROES   14   // 高效写零
#define VIRTIO_BLK_F_LIFETIME       15   // 存储寿命信息
#define VIRTIO_BLK_F_SECURE_ERASE   16   // 安全擦除

#define VIRTIO_BLK_F_START VIRTIO_BLK_F_BARRIER
#define VIRTIO_BLK_F_END   VIRTIO_BLK_F_SECURE_ERASE

/**
 * 读取块设备配置字段
 *
 * @param dev 虚拟设备
 * @param field 字段名
 *
 * @return 字段值
 */
#define VIRTIO_BLK_READ(dev, field) \
    ({ \
        __auto_type __val = 0; \
        virtio_read_device_config(dev, offsetof(struct virtio_blk_config, field), &__val, sizeof(__val)); \
        __val; \
    })

/**
 * 写入块设备配置字段
 *
 * @param dev 虚拟设备
 * @param field 字段名
 * @param val 要写入的值
 */
#define VIRTIO_BLK_WRITE(dev, field, val) \
    do { \
        __auto_type __val = (val); \
        virtio_write_device_config(dev, offsetof(struct virtio_blk_config, field), &__val, sizeof(__val)); \
    } while (0)

// 请求类型
typedef enum : uint32_t {
    VIRTIO_BLK_T_IN    = 0,   // 读取扇区
    VIRTIO_BLK_T_OUT   = 1,   // 写入扇区
    VIRTIO_BLK_T_FLUSH = 4,   // 刷新缓存
} virtio_blk_type_t;

// 设备返回状态
typedef enum : uint8_t {
    VIRTIO_BLK_S_OK     = 0,   // 操作成功
    VIRTIO_BLK_S_IOERR  = 1,   // I/O 错误
    VIRTIO_BLK_S_UNSUPP = 2,   // 操作不支持
} virtio_blk_status_t;

struct virtio_blk_features_ops {
    /**
     * 发送 FLUSH 命令
     *
     * @param dev 设备指针
     */
    bool (*flush)(struct virtio_blk_priv *priv, struct device *dev);
};

struct virtio_blk_priv {
    struct block_hdr hdr;          // 必须第一个
    struct virtio_blk_features_ops ops;
    uint32_t num_queues;           // 从配置空间读取，请求分发用
    spinlock_t *queue_locks;        // 每个队列的锁
    uint8_t irq_vector;
};

struct virtio_blk_ctx {
    struct task_struct *task;
    uint8_t *status;
};

struct virtio_blk_hdr {
    virtio_blk_type_t type;      // 请求类型
    uint32_t reserved;  // 必须为 0
    uint64_t sector;    // 起始扇区号
};

struct virtio_blk_config {
    uint64_t capacity;                         // 设备容量，单位 512 字节
    uint32_t size_max;                         // 单个 segment 最大字节数（F_SIZE_MAX）
    uint32_t seg_max;                          // 单个请求最大 segment 数（F_SEG_MAX）
    struct {
        uint16_t cylinders;                    // 柱面数（F_GEOMETRY）
        uint8_t heads;                         // 磁头数（F_GEOMETRY）
        uint8_t sectors;                       // 每磁道扇区数（F_GEOMETRY）
    } geometry;
    uint32_t blk_size;                         // 设备建议块大小（F_BLK_SIZE）
    struct {
        uint8_t physical_block_exp;            // 物理块大小指数（F_TOPOLOGY）
        uint8_t alignment_offset;              // 对齐偏移（F_TOPOLOGY）
        uint16_t min_io_size;                  // 最小 I/O 大小（F_TOPOLOGY）
        uint32_t opt_io_size;                  // 最优 I/O 大小（F_TOPOLOGY）
    } topology;
    uint8_t writeback;                         // 写缓存模式，0=直写，1=回写（F_CONFIG_WCE）
    uint8_t unused0[3];
    uint16_t num_queues;                       // 队列数量（F_MQ）
    uint32_t max_discard_sectors;              // 最大 discard 扇区数（F_DISCARD）
    uint32_t max_discard_seg;                  // 最大 discard segment 数（F_DISCARD）
    uint32_t discard_sector_alignment;         // discard 扇区对齐（F_DISCARD）
    uint32_t max_write_zeroes_sectors;         // 最大 write zeroes 扇区数（F_WRITE_ZEROES）
    uint32_t max_write_zeroes_seg;             // 最大 write zeroes segment 数（F_WRITE_ZEROES）
    uint8_t write_zeroes_may_unmap;            // write zeroes 是否可 unmap（F_WRITE_ZEROES）
    uint8_t unused1[3];
    uint32_t max_secure_erase_sectors;         // 最大 secure erase 扇区数（F_SECURE_ERASE）
    uint32_t max_secure_erase_seg;             // 最大 secure erase segment 数（F_SECURE_ERASE）
    uint32_t secure_erase_sector_alignment;    // secure erase 扇区对齐（F_SECURE_ERASE）
} __attribute__((packed));

struct block_type *virtio_blk_type = NULL;

static void *blk_isr_before(void *priv, uint64_t *qid) {
    struct virtio_blk_priv *p = priv;
    spin_lock(&p->queue_locks[*qid]);
    return &p->queue_locks[*qid];
}

static void blk_isr_after(void *ctx) {
    spin_unlock((spinlock_t *)ctx);
}

static bool virtio_blk_flush_disabled(struct virtio_blk_priv *priv, struct device *dev) {
    (void)priv;
    (void)dev;
    return false;
}

static bool virtio_blk_flush_enabled(struct virtio_blk_priv *priv, struct device *dev) {
    return virtio_blk_request(dev, priv, VIRTIO_BLK_T_FLUSH, 0, NULL, 0) == 0;
}

static int virtio_blk_request(
    struct device *dev,
    void *priv,
    uint32_t type,
    uint64_t sector,
    void *buf,
    size_t sector_count
) {
    struct virtio_blk_priv *p = priv;
    struct virtio_blk_hdr hdr;
    struct scatterlist sg[3];
    uint32_t out = 0, in = 0;
    uint32_t dummy = 0;
    uint32_t *data_cnt = &dummy;
    uint32_t qid;
    int ret;
    uint8_t status;
    uint64_t flags;

    // 完成上下文
    struct virtio_blk_ctx ctx = {
        .task = smp_get_task_current(),
        .status = &status,
    };

    // 确定数据段方向（FLUSH 用 dummy）
    if (type == VIRTIO_BLK_T_IN)
        data_cnt = &in;
    else if (type == VIRTIO_BLK_T_OUT)
        data_cnt = &out;

    // 按当前 CPU 选择队列
    qid = smp_processor_id() % p->num_queues;

    // 构造请求头
    hdr.type = type;
    hdr.reserved = 0;
    hdr.sector = sector;

    // 组装 scatterlist，地址需转换为物理地址
    sg[out].addr = LINEAR_TO_PHYS((uintptr_t)&hdr);
    sg[out].length = sizeof(hdr);
    out++;

    // 数据段（FLUSH 时 sector_count=0，dummy 自增不影响 out/in）
    sg[*data_cnt].addr = LINEAR_TO_PHYS((uintptr_t)buf);
    sg[*data_cnt].length = sector_count * 512;
    (*data_cnt)++;

    // 状态段
    sg[in].addr = LINEAR_TO_PHYS((uintptr_t)&status);
    sg[in].length = 1;
    in++;

    // 提交请求
    spin_lock(&p->queue_locks[qid]);
    ret = virtioqueue_add_buf(dev, qid, sg, out, in, &ctx);
    spin_unlock(&p->queue_locks[qid]);
    
    if (ret < 0)
        return ret;

    // 通知设备
    virtqueue_kick(dev, qid);

    // 睡眠等待完成
    flags = get_cpu_flags();
    irq_off();
    task_sleep(true);
    task_sched();
    write_cpu_flags(flags);

    return (status == VIRTIO_BLK_S_OK) ? 0 : -EIO;
}

static int virtio_blk_read(
    void *priv,
    struct device *dev,
    uint64_t sector,
    void *buf,
    size_t sector_count
) {
    return virtio_blk_request(dev, priv, VIRTIO_BLK_T_IN, sector, buf, sector_count);
}

static int virtio_blk_write(
    void *priv,
    struct device *dev,
    uint64_t sector,
    const void *buf,
    size_t sector_count
) {
    return virtio_blk_request(dev, priv, VIRTIO_BLK_T_OUT, sector, (void *)buf, sector_count);
}

static int virtio_blk_flush(void *priv, struct device *dev) {
    struct virtio_blk_priv *p = priv;
    return p->ops.flush(priv, dev) ? 0 : -EOPNOTSUPP;
}

VIRTIO_DRIVER_ISR_ENTRY(virtio_blk_isr, virtio_blk_type, blk_isr_before, blk_isr_after, data, {
    struct virtio_blk_ctx *ctx = data;
    task_wakeup(ctx->task);
});

static int virtio_blk_probe(struct device *dev) {
    struct virtio_blk_priv *priv;
    uint64_t features, final;
    uint64_t capacity;
    uint64_t blk_bits = 0;
    int ret;
    int created;
    ku32 irq_res;
    uint8_t vector;

    // 获取设备能力
    features = virtio_get_features(dev);

    // 分配驱动私有数据
    priv = kheap_alloc(sizeof(*priv));
    if (!priv)
        return -ENOMEM;
    memset(priv, 0, sizeof(*priv));

    priv->hdr.read = virtio_blk_read;
    priv->hdr.write = virtio_blk_write;
    priv->hdr.flush = virtio_blk_flush;

    // 读取配置空间
    capacity = VIRTIO_BLK_READ(dev, capacity);

    // 协商特性
    for (int bit = VIRTIO_BLK_F_START; bit <= VIRTIO_BLK_F_END; bit++) {
        bool enabled = (features & (1ULL << bit)) != 0;

        switch (bit) {
            case VIRTIO_BLK_F_FLUSH:
                if (!enabled) {
                    priv->ops.flush = virtio_blk_flush_disabled;
                } else {
                    blk_bits |= VIRTIO_BLK_F_FLUSH;
                    priv->ops.flush = virtio_blk_flush_enabled;
                }
                break;
            case VIRTIO_BLK_F_MQ:
                if (!enabled) {
                    priv->num_queues = 1;
                } else {
                    blk_bits |= VIRTIO_BLK_F_MQ;
                    priv->num_queues = VIRTIO_BLK_READ(dev, num_queues);
                }
                break;
            default:
                break;
        }
    }

    // 写入最终协商的特性
    final = (features & GENMASK_ULL(VIRTIO_F_END, VIRTIO_F_START)) | blk_bits;
    virtio_write_features(dev, final);

    // 分配并注册 CPU 中断号
    irq_res = smp_irq_alloc_handler((uint64_t)virtio_blk_isr);
    K_ERR_LABEL_AND_SAVE(irq_res, err_free_priv, ret);
    vector = (uint8_t)irq_res.val;

    // 注册到块设备层
    ret = block_add_device(virtio_blk_type, dev, priv, capacity);
    if (ret < 0)
        goto err_free_irq;

    // 初始化 virtqueue
    ret = virtqueue_set_all(dev, 0, vector, priv->num_queues, &created);
    if (ret < 0)
        goto err_remove_block;
        
    priv->num_queues = created;
    priv->irq_vector = vector;
    
    priv->queue_locks = kheap_alloc(priv->num_queues * sizeof(spinlock_t));
    if (!priv->queue_locks) {
        ret = -ENOMEM;
        goto err_free_vqs;
    }
    for (int i = 0; i < priv->num_queues; i++) 
        spinlock_init(&priv->queue_locks[i]);
    
    virtio_set_drvdata(dev, priv);

    return 0;

err_free_vqs:
    /* 
     * TODO :
     * 销毁所有 virtqueue
     * virtqueue_free_all(dev);
     */
    kheap_free(priv->queue_locks);
err_remove_block:
    block_remove_device(dev);
err_free_irq:
    smp_irq_unregister_handler(vector);
err_free_priv:
    kheap_free(priv);
    return ret;
}

static void virtio_blk_remove(struct device *dev) {
    struct virtio_blk_priv *priv;

    priv = virtio_get_drvdata(dev);
    if (!priv)
        return;

    // 从块设备层注销
    block_remove_device(dev);

    /* 
     * TODO :
     * 销毁所有 virtqueue
     * virtqueue_free_all(dev);
     */

    // 释放中断号
    smp_irq_unregister_handler(priv->irq_vector);

    // 释放 per-queue 锁数组
    if (priv->queue_locks)
        kheap_free(priv->queue_locks);

    kheap_free(priv);
    virtio_set_drvdata(dev, NULL);
}

static const struct virtio_device_id virtio_blk_id[] = {
    {VIRTIO_BLK_DEVICE_ID},
    {0}
};

struct driver virtio_blk_driver = {
    .name = "virtio-blk",
    .bus = &virtio_bus_type,
    .probe = virtio_blk_probe,
    .remove = virtio_blk_remove,
    .id_table = virtio_blk_id,
    .node = {0},
};

static void virtio_blk_init(void) {
    virtio_blk_type = block_register_type("virtio");
}

INITCALL(drivers, 0, virtio_blk_init);