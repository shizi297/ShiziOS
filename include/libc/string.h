/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 shizi <https://github.com/shizi297>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
void *memrchr(const void *, int, size_t);

char *strcpy(char *__restrict, const char *__restrict);
char *strncpy(char *__restrict, const char *__restrict, size_t);
char *stpcpy(char *__restrict, const char *__restrict);
char *stpncpy(char *__restrict, const char *__restrict, size_t);

char *strcat(char *__restrict, const char *__restrict);
char *strncat(char *__restrict, const char *__restrict, size_t);

int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);

char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strchrnul(const char *, int);
char *strstr(const char *, const char *);
char *strpbrk(const char *, const char *);
size_t strspn(const char *, const char *);
size_t strcspn(const char *, const char *);

size_t strlen(const char *);
size_t strnlen(const char *, size_t);

char *strtok(char *__restrict, const char *__restrict);
char *strtok_r(char *__restrict, const char *__restrict, char **__restrict);

#ifndef KERNEL
    char *strdup(const char *);
    char *strndup(const char *, size_t);
#endif

#ifdef __cplusplus
}
#endif