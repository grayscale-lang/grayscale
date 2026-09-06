/*
 * mem.c — Implementation of the mem stdlib module.
 * Provides user-facing arena creation, destruction, reset, usage
 * queries, and raw memory copy/zero operations.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "mem.h"

/* Maximum arena size: 1 GB */
#define GRAY_MAX_ARENA_SIZE (1024LL * 1024 * 1024)

GrayArena *gray_mem_arena(int64_t size) {
    if (size <= 0) size = GRAY_MIN_ARENA_SIZE;
    if (size > GRAY_MAX_ARENA_SIZE) {
        gray_panic_code("P0061", "mem.arena() size %lld bytes exceeds the maximum allowed size of 1 GB",
            (long long)size);
    }
    return gray_arena_create((size_t)size);
}

void gray_mem_destroy(GrayArena *arena, const char *file, int line) {
    if (arena) gray_arena_destroy(arena, file, line);
}

void gray_mem_reset(GrayArena *arena) {
    if (arena) gray_arena_reset(arena);
}

int64_t gray_mem_usage(GrayArena *arena) {
    if (!arena) return 0;
    return (int64_t)gray_arena_usage(arena);
}

void gray_mem_copy(void *dest, const void *src, int64_t byte_count) {
    if (dest && src && byte_count > 0) memcpy(dest, src, (size_t)byte_count);
}

void gray_mem_zero(void *ptr, int64_t byte_count) {
    if (ptr && byte_count > 0) memset(ptr, 0, (size_t)byte_count);
}

void gray_mem_set(void *ptr, int64_t value, int64_t byte_count) {
    if (ptr && byte_count > 0) memset(ptr, (int)value, (size_t)byte_count);
}
