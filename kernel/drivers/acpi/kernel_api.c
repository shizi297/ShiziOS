/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <bootboot.h>
#include <asm/mm_addr.h>
#include <heap.h>
#include <kio.h>
#include <time.h>
#include <spinlock.h>
#include <timecycle.h> 
#include <processor.h>
#include <stdatomic.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <klibc.h>
#include <mutex.h>
#include <uacpi/kernel_api.h>

#define UACPI_LOG(level, str) \
    printk("[UACPI][LEVEL: %d] %s", level, str)

typedef struct uacpi_event {
    atomic_int count;           // 事件计数器
    wait_queue_head_t wait_queue; // 等待队列
} uacpi_event_t;

struct uacpi_work_item {
    uacpi_work_handler handler;
    uacpi_handle ctx;
};

extern uint32_t pci_config_read(
    uint16_t segment, uint8_t bus, 
    uint8_t dev, uint8_t func, 
    uint16_t offset, int size
);

extern void pci_config_write(
    uint16_t segment, uint8_t bus, 
    uint8_t dev, uint8_t func, 
    uint16_t offset, int size, 
    uint32_t val
);

static struct {
    atomic_int pending;
    wait_queue_head_t done_wq;
} work = {
    .pending = ATOMIC_VAR_INIT(0),
    .done_wq = {0},
};

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;
static clocksource_handle_t acpi_clocksource = NULL;

// 获取当前纳秒，若句柄未初始化则尝试获取
static inline uint64_t get_ns(void) {
    if (!acpi_clocksource) {
        acpi_clocksource = clocksource_get(NULL);
        if (!acpi_clocksource) return 0;
    }
    return clocksource_read(acpi_clocksource);
}

void uacpi_kernel_api_init(void) {
    waitqueue_head_init(&work.done_wq);    
}

/**
 * 获取rsdp物理地址
 * 
 * @param out_rsdp_address 存储rsdp物理地址的指针
 */
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    uacpi_phys_addr addr = bootboot->arch.x86_64.acpi_ptr;
    const char *expected = "RSD PTR ";
    uacpi_phys_addr found_addr = 0;
    uint8_t *mapped = NULL;

    // 检查给定的地址是否是RSDP
    mapped = uacpi_kernel_map(addr, 8);
    if (mapped) {
        if (!memcmp(mapped, expected, 8)) {
            found_addr = addr;
            goto found;
        }
        uacpi_kernel_unmap(mapped, 8);
        mapped = NULL;
    }

    // 向前搜索最大64KB（不超过addr）
    uacpi_size max_forward = (addr < 65536) ? (uacpi_size)addr : 65536;
    for (uacpi_size offset = 1; offset <= max_forward; offset++) {
        uacpi_phys_addr candidate = addr - offset;
        mapped = uacpi_kernel_map(candidate, 8);
        if (!mapped) continue;
        if (!memcmp(mapped, expected, 8)) {
            found_addr = candidate;
            goto found;
        }
        uacpi_kernel_unmap(mapped, 8);
        mapped = NULL;
    }

    // 向后搜索64KB
    for (uacpi_size offset = 1; offset <= 65536; offset++) {
        uacpi_phys_addr candidate = addr + offset;
        mapped = uacpi_kernel_map(candidate, 8);
        if (!mapped) continue;
        if (!memcmp(mapped, expected, 8)) {
            found_addr = candidate;
            goto found;
        }
        uacpi_kernel_unmap(mapped, 8);
        mapped = NULL;
    }

    // 搜索传统BIOS区域（0xE0000 - 0xFFFFF）
    for (uacpi_phys_addr candidate = 0xE0000; candidate <= 0xFFFFF; candidate += 16) {
        mapped = uacpi_kernel_map(candidate, 8);
        if (!mapped) continue;
        if (!memcmp(mapped, expected, 8)) {
            found_addr = candidate;
            goto found;
        }
        uacpi_kernel_unmap(mapped, 8);
        mapped = NULL;
    }

    // 未找到
    UACPI_LOG(UACPI_LOG_ERROR, "RSDP not found\n");
    return UACPI_STATUS_NOT_FOUND;

found:
    if (mapped) {
        uacpi_kernel_unmap(mapped, 8);
        mapped = NULL;
    }
    *out_rsdp_address = found_addr;
    return UACPI_STATUS_OK;
}

/**
 * 映射物理地址到虚拟地址
 * 
 * @param phys_addr 要映射的物理地址
 * @param len       映射长度（字节）
 * 
 * @return 映射后的虚拟地址
 */
void *uacpi_kernel_map(uacpi_phys_addr phys_addr, uacpi_size len) {
    return (void *)PHYS_TO_LINEAR(phys_addr);
}

/**
 * 解除映射
 * 
 * @param virt_addr 之前返回的虚拟地址
 * @param len       之前映射的长度
 */
void uacpi_kernel_unmap(void *virt_addr, uacpi_size len) {
    // 不处理
}

/**
 * 输出日志
 * 
 * @param level   日志级别
 * @param message 日志消息
 */
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *message) {
    UACPI_LOG(level, message);
}

/**
 * 分配内存
 * 
 * @param size 要分配的字节数
 * 
 * @return 指向分配内存的指针
 */
void *uacpi_kernel_alloc(uacpi_size size) {
    return kheap_alloc((uint64_t)size);
}

/**
 * 释放内存
 * 
 * @param mem 要释放的内存指针
 */
void uacpi_kernel_free(void *mem) {
    kheap_free(mem);
}

/**
 * 获取启动后纳秒数
 * 
 * @return 纳秒数
 */
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    return get_ns();
}

/**
 * 忙等待
 * 
 * @param usec 要等待的微秒数
 */
void uacpi_kernel_stall(uacpi_u8 usec) {
    time_stall(usec);
}

/**
 * 睡眠
 * 
 * @param msec 要睡眠的毫秒数
 */
void uacpi_kernel_sleep(uacpi_u64 msec) {
    time_sleep(msec);
}

/**
 * 创建互斥锁
 * 
 * @return 互斥锁句柄
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
    mutex_t *mtx = (mutex_t *)uacpi_kernel_alloc(sizeof(mutex_t));
    if (!mtx)
        return UACPI_NULL;

    mutex_init(mtx);
    return (uacpi_handle)mtx;
}

/**
 * 释放互斥锁
 * 
 * @param mutex 要释放的互斥锁句柄
 */
void uacpi_kernel_free_mutex(uacpi_handle mutex) {
    if (mutex)
        uacpi_kernel_free((void *)mutex);
}

/**
 * 尝试获取互斥锁
 * 
 * @param mutex   互斥锁句柄
 * @param timeout 超时毫秒数（0 非阻塞，0xFFFF 无限等待）
 */
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle mutex, uacpi_u16 timeout) {
    mutex_t *mtx = (mutex_t *)mutex;
    if (!mtx)
        return UACPI_STATUS_INVALID_ARGUMENT;

    if (timeout == 0) {
        return mutex_trylock(mtx) ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
    }

    // 目前 timeout == 0xFFFF 或其他有限值，都当作无限等待
    mutex_lock(mtx);
    return UACPI_STATUS_OK;
}

/**
 * 释放互斥锁
 * 
 * @param mutex 互斥锁句柄
 */
void uacpi_kernel_release_mutex(uacpi_handle mutex) {
    mutex_t *mtx = (mutex_t *)mutex;
    if (mtx)
        mutex_unlock(mtx);
}

/**
 * 创建事件
 * 
 * @return 事件句柄
 */
uacpi_handle uacpi_kernel_create_event(void) {
    uacpi_event_t *ev = (uacpi_event_t *)uacpi_kernel_alloc(sizeof(uacpi_event_t));
    if (!ev)
        return UACPI_NULL;

    atomic_init(&ev->count, 0);
    waitqueue_head_init(&ev->wait_queue);
    return (uacpi_handle)ev;
}

/**
 * 释放事件
 * 
 * @param event 要释放的事件句柄
 */
void uacpi_kernel_free_event(uacpi_handle event) {
    if (event)
        uacpi_kernel_free((void *)event);
}

/**
 * 等待事件
 * 
 * @param event      事件句柄
 * @param timeout    超时毫秒数（0xFFFF 无限等待）
 */
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle event, uacpi_u16 timeout) {
    uacpi_event_t *ev = (uacpi_event_t *)event;
    if (!ev) return UACPI_FALSE;

    if (timeout == 0) {
        int old = atomic_load(&ev->count);
        while (old > 0) {   
            // 尝试获取事件
            if (atomic_compare_exchange_weak(&ev->count, &old, old - 1))
                return UACPI_TRUE;  // 当前线程获得了该事件
        }
        
        // 当前值 <= 0，没有可消费的事件，直接返回
        return UACPI_FALSE;
    }

    // 等待新的事件
    waitqueue_event(&ev->wait_queue, atomic_load(&ev->count) > 0);

    // 被唤醒后，此时 count > 0，原子减 1，消费事件
    atomic_fetch_sub(&ev->count, 1);
    return UACPI_TRUE;
}

/**
 * 触发事件
 * 
 * @param event 事件句柄
 */
void uacpi_kernel_signal_event(uacpi_handle event) {
    uacpi_event_t *ev = (uacpi_event_t *)event;
    if (!ev) return;
    int old = atomic_fetch_add(&ev->count, 1);
    if (old == 0) {
        waitqueue_wake_up(&ev->wait_queue);
    }
}

/**
 * 重置事件计数器为0
 * 
 * @param event 事件句柄
 */
void uacpi_kernel_reset_event(uacpi_handle event) {
    uacpi_event_t *ev = (uacpi_event_t *)event;
    if (!ev) return;
    atomic_store(&ev->count, 0);

    // 不主动清空等待队列，等待者会继续等待，直到下次 signal
}

/**
 * 获取当前线程id
 * 
 * @return 线程id
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return (uacpi_thread_id)(uintptr_t)task_get_current_thread_id();
}

/**
 * 创建自旋锁
 * 
 * @return 自旋锁句柄
 */
uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock_t *spinlock = (spinlock_t *)uacpi_kernel_alloc(sizeof(spinlock_t));
    if (spinlock) {
        spinlock_init(spinlock);
    }
    return (uacpi_handle)spinlock;
}

/**
 * 释放自旋锁
 * 
 * @param spinlock 要释放的自旋锁句柄
 */
void uacpi_kernel_free_spinlock(uacpi_handle spinlock) {
    kheap_free((void *)spinlock);
}

/**
 * 获取自旋锁并关中断
 * 
 * @param spinlock 自旋锁句柄
 * 
 * @return 关中断前的cpu标志
 */
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle spinlock) {
    uacpi_cpu_flags flags;
    spin_lock_irqsave((spinlock_t *)spinlock, &flags);
    return flags;
}

/**
 * 释放自旋锁并恢复中断
 * 
 * @param spinlock 自旋锁句柄
 * @param flags    之前保存的cpu标志
 */
void uacpi_kernel_unlock_spinlock(uacpi_handle spinlock, uacpi_cpu_flags flags) {
    spin_unlock_irqrestore((spinlock_t *)spinlock, flags);
}

/**
 * 映射io端口
 * 
 * @param base       io端口基址
 * @param len        范围长度（字节）
 * @param out_handle 输出句柄
 */
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
    // 对于x86_64，io端口通过特殊指令访问，不需要实际映射，直接返回基址
    *out_handle = (uacpi_handle)base;
    return UACPI_STATUS_OK;
}

/**
 * 解除io映射
 * 
 * @param handle io句柄
 */
void uacpi_kernel_io_unmap(uacpi_handle handle) {
    // 目前不需要处理
}

/**
 * 读8位io端口
 * 
 * @param handle    io句柄
 * @param offset    端口偏移
 * @param out_value 输出值
 */
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    *out_value = inb(addr);
    return UACPI_STATUS_OK;
}

/**
 * 读16位io端口
 * 
 * @param handle    io句柄
 * @param offset    端口偏移
 * @param out_value 输出值
 */
uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    *out_value = inw(addr);
    return UACPI_STATUS_OK;
}

/**
 * 读32位io端口
 * 
 * @param handle    io句柄
 * @param offset    端口偏移
 * @param out_value 输出值
 */
uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    *out_value = inl(addr);
    return UACPI_STATUS_OK;
}

/**
 * 写8位io端口
 * 
 * @param handle io句柄
 * @param offset 端口偏移
 * @param value  要写入的值
 */
uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    outb(addr, value);
    return UACPI_STATUS_OK;
}

/**
 * 写16位io端口
 * 
 * @param handle io句柄
 * @param offset 端口偏移
 * @param value  要写入的值
 */
uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    outw(addr, value);
    return UACPI_STATUS_OK;
}

/**
 * 写32位io端口
 * 
 * @param handle io句柄
 * @param offset 端口偏移
 * @param value  要写入的值
 */
uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    outl(addr, value);
    return UACPI_STATUS_OK;
}

struct pci_dev_handle {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

/**
 * 打开PCI设备
 * 
 * @param address    pci设备地址
 * @param out_handle 输出句柄
 */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
    struct pci_dev_handle *handle = kheap_alloc(sizeof(*handle));
    if (!handle)
        return UACPI_STATUS_OUT_OF_MEMORY;

    handle->segment = address.segment;
    handle->bus = address.bus;
    handle->device = address.device;
    handle->function = address.function;

    *out_handle = (uacpi_handle)handle;
    return UACPI_STATUS_OK;
}

/**
 * 关闭PCI设备
 * 
 * @param device 设备句柄
 */
void uacpi_kernel_pci_device_close(uacpi_handle device) {
    kheap_free(device);
}

/**
 * 读PCI设备8位配置空间
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  输出值
 */
uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
    struct pci_dev_handle *handle = (struct pci_dev_handle *)device;
    uint32_t val = pci_config_read(
        handle->segment, handle->bus,
        handle->device, handle->function,
        (uint16_t)offset, 1
    );
    *value = (uacpi_u8)val;
    return UACPI_STATUS_OK;
}

/**
 * 读PCI设备16位配置空间
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  输出值
 */
uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
    struct pci_dev_handle *handle = (struct pci_dev_handle *)device;
    uint32_t val = pci_config_read(
        handle->segment, handle->bus,
        handle->device, handle->function,
        (uint16_t)offset, 2
    );
    *value = (uacpi_u16)val;
    return UACPI_STATUS_OK;
}

/**
 * 读PCI设备32位配置空间
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  输出值
 */
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
    struct pci_dev_handle *handle = (struct pci_dev_handle *)device;
    uint32_t val = pci_config_read(
        handle->segment, handle->bus,
        handle->device, handle->function,
        (uint16_t)offset, 4
    );
    *value = val;
    return UACPI_STATUS_OK;
}

/**
 * 写PCI设备8位配置空间
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  要写入的值
 */
uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    struct pci_dev_handle *handle = (struct pci_dev_handle *)device;
    pci_config_write(
        handle->segment, handle->bus,
        handle->device, handle->function,
        (uint16_t)offset, 1, (uint32_t)value
    );
    return UACPI_STATUS_OK;
}

/**
 * 写PCI设备16位配置空间
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  要写入的值
 */
uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    struct pci_dev_handle *handle = (struct pci_dev_handle *)device;
    pci_config_write(
        handle->segment, handle->bus,
        handle->device, handle->function,
        (uint16_t)offset, 2, (uint32_t)value
    );
    return UACPI_STATUS_OK;
}

/**
 * 写PCI设备32位配置空间
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  要写入的值
 */
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    struct pci_dev_handle *handle = (struct pci_dev_handle *)device;
    pci_config_write(
        handle->segment, handle->bus,
        handle->device, handle->function,
        (uint16_t)offset, 4, value
    );
    return UACPI_STATUS_OK;
}

/**
 * 安装中断处理程序
 * 
 * @param irq 平台相关的外部设备中断号
 * @param handler 中断处理函数         
 * @param ctx 中断处理数据
 * @param out_irq_handle 返回的中断句柄  
 */
uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, 
    uacpi_interrupt_handler handler,
    uacpi_handle ctx, 
    uacpi_handle *out_irq_handle
) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 卸载中断处理程序
 * 
 * @param handler     处理函数
 * @param irq_handle  中断句柄
 */
uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler handler,
    uacpi_handle irq_handle
) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 工作回调 
 * 
 * @param data 数据
 */
static void work_wrapper(void *data) {
    struct uacpi_work_item *item = data;

    // 执行实际的工作回调
    item->handler(item->ctx);       

    uacpi_kernel_free(item);

    // 最后一个工作完成时，唤醒正在等待的线程
    if (atomic_fetch_sub(&work.pending, 1) == 1)
        waitqueue_wake_up(&work.done_wq);
}

/**
 * 使用 woeker 执行工作
 * 
 * @param type    工作类型
 * @param handler 处理函数
 * @param ctx     上下文
 */
uacpi_status uacpi_kernel_schedule_work(
    uacpi_work_type type,
    uacpi_work_handler handler,
    uacpi_handle ctx
) {
    struct uacpi_work_item *item = uacpi_kernel_alloc(sizeof(*item));
    if (!item)
        return UACPI_STATUS_OUT_OF_MEMORY;

    item->handler = handler;
    item->ctx = ctx;

    atomic_fetch_add(&work.pending, 1);     // 未完成工作计数+1
    task_submit_work(work_wrapper, item);   // 提交到内核工作队列
    return UACPI_STATUS_OK;
}

// 等待所有已调度工作和中断完成
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    // 若没有未完成的工作，直接返回
    while (atomic_load(&work.pending) > 0) {
        // 等待直到 pending 变为 0
        waitqueue_event(&work.done_wq, atomic_load(&work.pending) == 0);
    }

    return UACPI_STATUS_OK;
}

/**
 * 处理固件请求
 * 
 * @param req 固件请求结构体指针
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {

    return UACPI_STATUS_OK;
}