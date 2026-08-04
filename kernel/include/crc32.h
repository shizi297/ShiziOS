/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * 计算 CRC-32 校验值
 *
 * @param crc 初始 CRC 值，若传入 0 则自动初始化为 0xFFFFFFFF
 * @param buf 待计算数据的缓冲区指针
 * @param len 数据长度（字节）
 * 
 * @return 计算后好的值
 */
uint32_t crc32(uint32_t crc, const void *buf, size_t len);