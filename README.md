# ShiziOS

ShiziOS is a lightweight operating system kernel. Currently, it supports only the x86_64 architecture, uses the bootboot bootloader, is partially POSIX-compliant, supports multicore, and is under active development.

## How to Use

### Precompiled Version
This project provides a precompiled version. The `disk` folder contains an image file, which has been tested in QEMU.

### Build from Source
You need to install the following components:
- qemu
- x86_64-elf-gcc
- nasm

Execute the following commands in the project root directory:
- `make`: Build the project.
- `make img`: Create a disk image.
- `make run`: Run the system in QEMU.

## Project Directory Structure
- `include`: System header files
- `kernel`: Kernel-related code
- `kernel/include`: Kernel header files
- `kernel/task`: Task management
- `kernel/mm`: Memory management
- `kernel/net`: Networking
- `kernel/fs`: File system

## System Notes
The buddy system algorithm in this kernel's memory management differs from the traditional Linux buddy system.

## Other
[![gitee]](your-gitee-mirror-repository-link) [![github]](your-github-main-repository-link)

**Primary Development Platform: GitHub.**

All Issues and Pull Requests **should be submitted to GitHub**.

## License
This project is licensed under the Apache License, Version 2.0. For details, see the `LICENSE` file.