# ShiziOS

ShiziOS is a lightweight operating system kernel. Currently, it supports only the x86_64 architecture, uses the bootboot bootloader, supports multicore, and is under active development.

## How to Use

### Precompiled Version
This project provides a precompiled version. The `disk` folder contains an image file, which has been tested in QEMU.

### Build from Source
You need to install the following components:
- qemu
- x86_64-elf-gcc
- nasm
- cmake
- make

Execute the following commands in the project root directory:
1. `mkdir build && cd build`
2. `cmake ..`
3. `make`
4. `make img`

Run QEMU：
`make run`

## Project Directory Structure
- `include`: System header files
- `kernel`: Kernel-related code
- `kernel/include`: Kernel header files
- `kernel/task`: Task management
- `kernel/mm`: Memory management
- `kernel/net`: Networking
- `kernel/fs`: File system

## Project Home
**Primary Development Platform: GitHub.**

GitHub Repository: [https://github.com/shizi297/ShiziOS/](https://github.com/shizi297/ShiziOS/)

All Issues and Pull Requests should be submitted to GitHub.

## License
This project is licensed under the Apache License, Version 2.0. For details, see the `LICENSE` file.