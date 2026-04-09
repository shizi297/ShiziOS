# ShiziOS

ShiziOS 是一个轻量级的操作系统。目前仅支持 x86_64 架构，使用 bootboot 引导程序，支持多核，正在积极开发中。

## 如何使用

### 预编译版本
本项目提供预编译版本。`disk` 文件夹中包含一个镜像文件，该镜像已在 QEMU 中测试通过。

### 从源码构建
你需要安装以下组件：
- clang
- cmake
- make

在项目根目录下执行以下命令：
1. `mkdir build && cd build`
2. `cmake ..`
3. `make`
4. `make img`

## 项目目录说明
- `include`：系统头文件
- `kernel`：内核主目录
  - `kernel/include`：内核内部公共头文件
  - `kernel/init`：内核初始化
  - `kernel/arch/${ARCH}`：架构相关代码
  - `kernel/arch/${ARCH}/include`：架构内部头文件
  - `kernel/arch/${ARCH}/include/asm`：架构相关提供给通用的头文件
  - `kernel/mm`：通用内存管理
  - `kernel/task`：任务管理
  - `kernel/sync`：同步原语
  - `kernel/ipc`：进程间通信
  - `kernel/time`：时间子系统
  - `kernel/lib`：内核库函数
  - `kernel/drivers`：设备驱动
  - `kernel/fs`：文件系统
  - `kernel/test`：测试模块

## 项目主页
**主要开发平台：GitHub。**

GitHub 仓库：[https://github.com/shizi297/ShiziOS/](https://github.com/shizi297/ShiziOS/)

所有 Issues 和 Pull Requests 请提交到 GitHub。

## 许可证
本项目采用 Apache License, Version 2.0 开源协议，详情请见 `LICENSE` 文件。