/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#include <string.h>
#include <heap.h>

static inline char *strdup(const char *s) {
    size_t l = strlen(s);
    char *d = kheap_alloc(l + 1);
    if (!d) return NULL;
    return memcpy(d, s, l + 1);
}

static inline char *strndup(const char *s, size_t n) {
    size_t l = strnlen(s, n);
    char *d = kheap_alloc(l + 1);
    if (!d) return NULL;
    memcpy(d, s, l);
    d[l] = '\0';
    return d;
}