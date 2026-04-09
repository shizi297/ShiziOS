# ShiziOS

ShiziOS is a lightweight operating system. Currently, it supports only the x86_64 architecture, uses the bootboot bootloader, supports multicore, and is under active development.

## How to Use

### Precompiled Version
This project provides a precompiled version. The `disk` folder contains an image file, which has been tested in QEMU.

### Build from Source
You need to install the following components:
- clang
- cmake
- make

Execute the following commands in the project root directory:
1. `mkdir build && cd build`
2. `cmake ..`
3. `make`
4. `make img`

## Project Directory Structure
- `include`: System header files
- `kernel`: Kernel main directory
  - `kernel/include`: Kernel internal public header files
  - `kernel/init`: Kernel initialization
  - `kernel/arch/${ARCH}`: Architecture‑dependent code
  - `kernel/arch/${ARCH}/include`: Architecture‑internal header files
  - `kernel/arch/${ARCH}/include/asm`: Architecture‑specific headers provided to generic code
  - `kernel/mm`: Generic memory management
  - `kernel/task`: Task management
  - `kernel/sync`: Synchronization primitives
  - `kernel/ipc`: Inter‑process communication
  - `kernel/time`: Time subsystem
  - `kernel/lib`: Kernel utility library
  - `kernel/drivers`: Device drivers
  - `kernel/fs`: File system
  - `kernel/test`: Testing module

## Project Home
**Primary Development Platform: GitHub.**

GitHub Repository: [https://github.com/shizi297/ShiziOS/](https://github.com/shizi297/ShiziOS/)

All Issues and Pull Requests should be submitted to GitHub.

## License
This project is licensed under the Apache License, Version 2.0. For details, see the `LICENSE` file.