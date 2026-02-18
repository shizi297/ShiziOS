/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#include <bootboot.h>
#include <mm_addr.h>
#include <heap.h>
#include <serial.h>
#include <time.h>
#include <spinlock.h>
#include <timecycle.h> 
#include <processor.h>
#include <stdatomic.h>
#include <smp.h>
#include <io.h>
#include <uacpi/kernel_api.h>

#define UACPI_LOG(level, str) \
    serial_puts("[UACPI][LEVEL: "); \
    serial_put_dec(level); \
    serial_puts("] "); \
    serial_puts(str); \
    serial_puts("\n")

static const BOOTBOOT *bootboot = (const BOOTBOOT *)BOOTBOOT_INFO;

/**
 * 获取rsdp物理地址
 * 
 * @param out_rsdp_address 存储rsdp物理地址的指针
 * 
 * @return UACPI_STATUS_OK  成功获取地址
 */
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    *out_rsdp_address = bootboot->arch.x86_64.acpi_ptr;
    return UACPI_STATUS_OK;
}

/**
 * 物理地址转虚拟地址（线性映射）
 * 
 * @param phys_addr 要映射的物理地址
 * @param len       映射长度（字节）
 * 
 * @return 映射后的虚拟地址，失败返回NULL
 */
void *uacpi_kernel_map(uacpi_phys_addr phys_addr, uacpi_size len) {
    return (void *)PHYS_TO_LINEAR(phys_addr);
}

/**
 * 解除映射（线性映射不需要实际unmap）
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
 * @return 分配的内存指针，失败返回NULL
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
 * @return 当前纳秒数，失败返回0
 */
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    uacpi_u64 ns = 0;
    bool is_success = clocksource_read(NULL, &ns);
    if (!is_success) {
        return 0;
    }
    return ns;
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
 * @return 互斥锁句柄，失败返回NULL
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
    // 目前先使用自旋锁模拟
    spinlock_t *mutex = (spinlock_t *)uacpi_kernel_alloc(sizeof(spinlock_t));
    if (mutex) {
        spinlock_init(mutex);
    }
    return (uacpi_handle)mutex;
}

/**
 * 释放互斥锁
 * 
 * @param mutex 要释放的互斥锁句柄
 */
void uacpi_kernel_free_mutex(uacpi_handle mutex) {
    kheap_free((void *)mutex);
}

/**
 * 尝试获取互斥锁
 * 
 * @param mutex   互斥锁句柄
 * @param timeout 超时毫秒数（0 非阻塞，0xFFFF 无限等待）
 * 
 * @return UACPI_STATUS_OK      成功获取
 * @return UACPI_STATUS_TIMEOUT 超时或无法获取
 */
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle mutex, uacpi_u16 timeout) {
    spinlock_t *lock = (spinlock_t *)mutex;

    if (timeout == 0) {
        return spin_trylock(lock) ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
    }

    uint64_t start_ns = 0;
    if (timeout != 0xFFFF) {
        if (!clocksource_read(NULL, &start_ns)) {
            return spin_trylock(lock) ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
        }
    }

    while (1) {
        if (spin_trylock(lock)) {
            return UACPI_STATUS_OK;
        }
        cpu_pause();

        if (timeout != 0xFFFF) {
            uint64_t now_ns;
            if (!clocksource_read(NULL, &now_ns)) {
                return UACPI_STATUS_TIMEOUT;
            }
            if (timecycle_ns_to_msec(now_ns - start_ns) >= timeout) {
                return UACPI_STATUS_TIMEOUT;
            }
        }
    }
}

/**
 * 释放互斥锁
 * 
 * @param mutex 互斥锁句柄
 */
void uacpi_kernel_release_mutex(uacpi_handle mutex) {
    spin_unlock((spinlock_t *)mutex);
}

/**
 * 创建事件（计数器信号量）
 * 
 * @return 事件句柄，失败返回NULL
 */
uacpi_handle uacpi_kernel_create_event(void) {
    _Atomic uint64_t *event = (_Atomic uint64_t *)uacpi_kernel_alloc(sizeof(_Atomic uint64_t));
    if (event) {
        atomic_init(event, 0);
    } 
    return (uacpi_handle)event;
}

/**
 * 释放事件
 * 
 * @param event 要释放的事件句柄
 */
void uacpi_kernel_free_event(uacpi_handle event) {
    kheap_free((void *)event);
}

/**
 * 等待事件（带超时）
 * 
 * @param event      事件句柄
 * @param timeout_ms 超时毫秒数（0xFFFF 无限等待）
 * 
 * @return UACPI_TRUE  成功等待到事件
 * @return UACPI_FALSE 超时或失败
 */
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle event, uacpi_u16 timeout_ms) {
    _Atomic uint64_t *event_counter = (_Atomic uint64_t *)event;
    uint64_t ns = 0;
    bool start = clocksource_read(NULL, &ns);
    if (!start) {
        return UACPI_FALSE;
    }

    while (1) {
        uint64_t old = atomic_load(event_counter);
        if (old > 0) {
            if (atomic_compare_exchange_weak(event_counter, &old, old - 1)) {
                return UACPI_TRUE;
            }
        }

        if (timeout_ms != 0xFFFF) {
            uint64_t now = 0;
            bool is_success = clocksource_read(NULL, &now);
            if (!is_success) {
                return UACPI_FALSE;
            }

            if (timecycle_ns_to_msec(now - ns) >= timeout_ms) {
                return UACPI_FALSE;
            }
        }
        cpu_pause();
    }
}

/**
 * 触发事件（计数器加1）
 * 
 * @param event 事件句柄
 */
void uacpi_kernel_signal_event(uacpi_handle event) {
    _Atomic uint64_t *event_counter = (_Atomic uint64_t *)event;
    atomic_fetch_add(event_counter, 1);
}

/**
 * 重置事件计数器为0
 * 
 * @param event 事件句柄
 */
void uacpi_kernel_reset_event(uacpi_handle event) {
    _Atomic uint64_t *event_counter = (_Atomic uint64_t *)event;
    atomic_init(event_counter, 0);
}

/**
 * 获取当前线程id
 * 
 * @return 线程id
 */
uacpi_thread_id uacpi_kernel_get_current_thread_id(void) {
    // 目前先用cpuid替代
    return (uacpi_thread_id)(uintptr_t)get_logical_id();
}

/**
 * 创建自旋锁
 * 
 * @return 成功：自旋锁句柄
 * @return 失败：NULL
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
    uint64_t flags = get_cpu_flags();
    irq_off();
    spin_lock((spinlock_t *)spinlock);
    return flags;
}

/**
 * 释放自旋锁并恢复中断
 * 
 * @param spinlock 自旋锁句柄
 * @param flags    之前保存的cpu标志
 */
void uacpi_kernel_unlock_spinlock(uacpi_handle spinlock, uacpi_cpu_flags flags) {
    spin_unlock((spinlock_t *)spinlock);
    write_cpu_flags(flags);
}

/**
 * 映射io端口（x86直接返回基址）
 * 
 * @param base       io端口基址
 * @param len        范围长度（字节）
 * @param out_handle 输出句柄
 * 
 * @return UACPI_STATUS_OK  成功
 */
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
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
 * 
 * @return UACPI_STATUS_OK  成功
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
 * 
 * @return UACPI_STATUS_OK  成功
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
 * 
 * @return UACPI_STATUS_OK  成功
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
 * 
 * @return UACPI_STATUS_OK  成功
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
 * 
 * @return UACPI_STATUS_OK  成功
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
 * 
 * @return UACPI_STATUS_OK  成功
 */
uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
    uacpi_io_addr addr = (uacpi_io_addr)handle + offset;
    outl(addr, value);
    return UACPI_STATUS_OK;
}

/**
 * 打开PCI设备（未实现）
 * 
 * @param address    pci设备地址
 * @param out_handle 输出句柄
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 关闭PCI设备（未实现）
 * 
 * @param device 设备句柄
 */
void uacpi_kernel_pci_device_close(uacpi_handle device) {
    // 目前不处理
}

/**
 * 读PCI设备8位配置空间（未实现）
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  输出值
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 读PCI设备16位配置空间（未实现）
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  输出值
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 读PCI设备32位配置空间（未实现）
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  输出值
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 写PCI设备8位配置空间（未实现）
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  要写入的值
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 写PCI设备16位配置空间（未实现）
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  要写入的值
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 写PCI设备32位配置空间（未实现）
 * 
 * @param device 设备句柄
 * @param offset 偏移
 * @param value  要写入的值
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 安装中断处理程序（未实现）
 * 
 * @param irq             中断号
 * @param handler         处理函数
 * @param ctx             上下文参数
 * @param out_irq_handle  输出中断句柄
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler,
    uacpi_handle ctx, uacpi_handle *out_irq_handle
) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 卸载中断处理程序（未实现）
 * 
 * @param handler     处理函数
 * @param irq_handle  中断句柄
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler handler,
    uacpi_handle irq_handle
) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 调度延迟工作（未实现）
 * 
 * @param type    工作类型
 * @param handler 处理函数
 * @param ctx     上下文
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_schedule_work(
    uacpi_work_type type, uacpi_work_handler handler,
    uacpi_handle ctx
) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

/**
 * 等待所有已调度工作和中断完成（未实现）
 * 
 * @return UACPI_STATUS_OK  认为没有工作要等待
 */
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_OK;
}

/**
 * 处理固件请求（未实现）
 * 
 * @param req 固件请求结构体指针
 * 
 * @return UACPI_STATUS_UNIMPLEMENTED
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {
    return UACPI_STATUS_UNIMPLEMENTED;
}