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
    GrayArena *arena = gray_arena_create((size_t)size);
    arena->counts_toward_total = false;
    return arena;
}

GrayArena *gray_mem_live(GrayArena *arena, const char *function_name) {
    if (!arena) gray_panic_code("P0171", "mem.%s on an arena that was never created with mem.arena() or was already destroyed", function_name);
    return arena;
}

void gray_mem_destroy(GrayArena **arena, const char *file, int line) {
    gray_arena_destroy(gray_mem_live(*arena, "destroy"), file, line);
    *arena = NULL;
}

void gray_mem_reset(GrayArena *arena) {
    gray_arena_reset(gray_mem_live(arena, "reset"));
}

int64_t gray_mem_usage(GrayArena *arena) {
    return (int64_t)gray_arena_usage(gray_mem_live(arena, "usage"));
}

void gray_mem_copy(void *dest, const void *source, int64_t byte_count) {
    if (dest && source && byte_count > 0) memcpy(dest, source, (size_t)byte_count);
}

void gray_mem_zero(void *pointer, int64_t byte_count) {
    if (pointer && byte_count > 0) memset(pointer, 0, (size_t)byte_count);
}

void gray_mem_set(void *pointer, int64_t value, int64_t byte_count) {
    if (pointer && byte_count > 0) memset(pointer, (int)value, (size_t)byte_count);
}
