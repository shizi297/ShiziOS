# ShiziOS

ShiziOS 是一个轻量级的操作系统内核。目前仅支持 x86_64 架构，使用 bootboot 引导程序，支持多核，正在积极开发中。

## 如何使用

### 预编译版本
本项目提供预编译版本。`disk` 文件夹中包含一个镜像文件，该镜像已在 QEMU 中测试通过。

### 从源码构建
你需要安装以下组件：
- qemu
- x86_64-elf-gcc
- nasm
- cmake
- make

在项目根目录下执行以下命令：
1. `mkdir build && cd build`
2. `cmake ..`
3. `make`
4. `make img`

运行QEMU：
`make run`

## 项目目录说明
- `include`：系统头文件
- `kernel`：内核相关
- `kernel/include`：内核头文件
- `kernel/task`：任务管理
- `kernel/mm`：内存管理
- `kernel/net`：网络
- `kernel/fs`：文件系统

## 项目主页
**主要开发平台：GitHub。**

GitHub 仓库：[https://github.com/shizi297/ShiziOS/](https://github.com/shizi297/ShiziOS/)

所有 Issues 和 Pull Requests 请提交到 GitHub。

## 许可证
本项目采用 Apache License, Version 2.0 开源协议，详情请见 `LICENSE` 文件。