/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#define EPERM            1   // 操作不允许
#define ENOENT           2   // 无此文件或目录
#define ESRCH            3   // 无此进程
#define EINTR            4   // 系统调用被中断
#define EIO              5   // I/O 错误
#define ENXIO            6   // 无此设备或地址
#define E2BIG            7   // 参数列表过长
#define ENOEXEC          8   // 执行格式错误
#define EBADF            9   // 错误的文件描述符
#define ECHILD          10   // 无子进程
#define EAGAIN          11   // 重试
#define ENOMEM          12   // 内存不足
#define EACCES          13   // 权限不足
#define EFAULT          14   // 地址错误
#define ENOTBLK         15   // 需要块设备
#define EBUSY           16   // 设备或资源忙
#define EEXIST          17   // 文件已存在
#define EXDEV           18   // 跨设备链接
#define ENODEV          19   // 无此设备
#define ENOTDIR         20   // 不是目录
#define EISDIR          21   // 是目录
#define EINVAL          22   // 无效参数
#define ENFILE          23   // 文件表溢出
#define EMFILE          24   // 打开文件过多
#define ENOTTY          25   // 不是终端设备
#define ETXTBSY         26   // 文本文件忙
#define EFBIG           27   // 文件过大
#define ENOSPC          28   // 无剩余空间
#define ESPIPE          29   // 非法偏移
#define EROFS           30   // 只读文件系统
#define EMLINK          31   // 链接数过多
#define EPIPE           32   // 管道破裂
#define EDOM            33   // 数学参数超出定义域
#define ERANGE          34   // 数学结果不可表示
#define EDEADLK         35   // 资源死锁
#define ENAMETOOLONG    36   // 文件名过长
#define ENOLCK          37   // 无记录锁
#define ENOSYS          38   // 功能未实现
#define ENOTEMPTY       39   // 目录非空
#define ELOOP           40   // 符号链接嵌套过深
#define EWOULDBLOCK     EAGAIN
#define ENOMSG          42   // 无所需消息
#define EIDRM           43   // 标识符被删除
#define ECHRNG          44   // 通道号超出范围
#define EL2NSYNC        45   // 2级未同步
#define EL3HLT          46   // 3级挂起
#define EL3RST          47   // 3级复位
#define ELNRNG          48   // 链接号超出范围
#define EUNATCH         49   // 协议驱动未连接
#define ENOCSI          50   // 无CSI结构
#define EL2HLT          51   // 2级挂起
#define EBADE           52   // 无效交换
#define EBADR           53   // 无效请求描述符
#define EXFULL          54   // 交换满
#define ENOANO          55   // 无阳极
#define EBADRQC         56   // 无效请求码
#define EBADSLT         57   // 无效槽位
#define EDEADLOCK       EDEADLK
#define EBFONT          59   // 错误的字体文件格式
#define ENOSTR          60   // 设备不是流
#define ENODATA         61   // 无可用数据
#define ETIME           62   // 计时器过期
#define ENOSR           63   // 流资源耗尽
#define ENONET          64   // 机器不在网络中
#define ENOPKG          65   // 包未安装
#define EREMOTE         66   // 对象远程
#define ENOLINK         67   // 链接已切断
#define EADV            68   // 广告错误
#define ESRMNT          69   // srmount错误
#define ECOMM           70   // 通信错误
#define EPROTO          71   // 协议错误
#define EMULTIHOP       72   // 尝试多跳
#define EDOTDOT         73   // RFS特定错误
#define EBADMSG         74   // 非数据消息
#define EOVERFLOW       75   // 值过大
#define ENOTUNIQ        76   // 名称不唯一
#define EBADFD          77   // 文件描述符状态错误
#define EREMCHG         78   // 远程地址改变
#define ELIBACC         79   // 无法访问共享库
#define ELIBBAD         80   // 共享库损坏
#define ELIBSCN         81   // .lib段损坏
#define ELIBMAX         82   // 共享库过多
#define ELIBEXEC        83   // 无法直接执行共享库
#define EILSEQ          84   // 非法字节序列
#define ERESTART        85   // 应重启的系统调用
#define ESTRPIPE        86   // 流管道错误
#define EUSERS          87   // 用户过多
#define ENOTSOCK        88   // 非套接字
#define EDESTADDRREQ    89   // 需要目标地址
#define EMSGSIZE        90   // 消息过长
#define EPROTOTYPE      91   // 协议类型错误
#define ENOPROTOOPT     92   // 协议不可用
#define EPROTONOSUPPORT 93   // 协议不支持
#define ESOCKTNOSUPPORT 94   // 套接字类型不支持
#define EOPNOTSUPP      95   // 操作不支持
#define ENOTSUP         EOPNOTSUPP
#define EPFNOSUPPORT    96   // 协议族不支持
#define EAFNOSUPPORT    97   // 地址族不支持
#define EADDRINUSE      98   // 地址已使用
#define EADDRNOTAVAIL   99   // 地址不可用
#define ENETDOWN        100  // 网络关闭
#define ENETUNREACH     101  // 网络不可达
#define ENETRESET       102  // 网络连接重置
#define ECONNABORTED    103  // 连接中止
#define ECONNRESET      104  // 连接重置
#define ENOBUFS         105  // 无缓冲空间
#define EISCONN         106  // 已连接
#define ENOTCONN        107  // 未连接
#define ESHUTDOWN       108  // 已关闭
#define ETOOMANYREFS    109  // 引用过多
#define ETIMEDOUT       110  // 连接超时
#define ECONNREFUSED    111  // 连接拒绝
#define EHOSTDOWN       112  // 主机关闭
#define EHOSTUNREACH    113  // 主机不可达
#define EALREADY        114  // 操作已在进程
#define EINPROGRESS     115  // 操作进行中
#define ESTALE          116  // 过时的文件句柄
#define EUCLEAN         117  // 结构需清理
#define ENOTNAM         118  // 非XENIX命名文件
#define ENAVAIL         119  // 无XENIX信号量
#define EISNAM          120  // 是命名文件
#define EREMOTEIO       121  // 远程I/O错误
#define EDQUOT          122  // 超出配额
#define ENOMEDIUM       123  // 无介质
#define EMEDIUMTYPE     124  // 介质类型错误
#define ECANCELED       125  // 操作取消
#define ENOKEY          126  // 所需密钥不可用
#define EKEYEXPIRED     127  // 密钥过期
#define EKEYREVOKED     128  // 密钥撤销
#define EKEYREJECTED    129  // 密钥被拒绝
#define EOWNERDEAD      130  // 所有者死亡
#define ENOTRECOVERABLE 131  // 状态不可恢复
#define ERFKILL         132  // 射频kill
#define EHWPOISON       133  // 硬件内存错误

// 判断值是否为错误码
#define IS_ERR_VALUE(x) ((unsigned long)(x) >= (unsigned long)-4095)

// 将错误码转换为指针
#define ERR_PTR(error)   ((void *)(long)(error))

// 从错误指针提取错误码
#define PTR_ERR(ptr)     ((long)(ptr))

// 将错误码转为指针
#define ERR_CAST(x) ((void *)(long)(x))

// 判断指针是否为错误指针
#define IS_ERR(ptr)      IS_ERR_VALUE((unsigned long)(ptr))

// 判断指针是否为NULL或错误指针
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))