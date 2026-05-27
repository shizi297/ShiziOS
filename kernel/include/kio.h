/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2025 shizi <https://github.com/shizi297>
 */

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

void printk(const char *fmt, ...);

__attribute__((noreturn))
void printp(const char *fmt, ...);