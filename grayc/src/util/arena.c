/*
 * arena.c — Block-based arena allocator for compiler-internal memory,
 * providing bump allocation with bulk free on compilation finish.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define ARENA_INTERN_INITIAL_CAP 256

static ArenaBlock *arena_block_create(size_t size) {
    ArenaBlock *block = malloc(sizeof(ArenaBlock) + size);
    if (!block) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

Arena *arena_create(size_t initial_size) {
    Arena *arena = malloc(sizeof(Arena));
    if (!arena) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    arena->default_block_size = initial_size;
    arena->first = arena_block_create(initial_size);
    arena->current = arena->first;
    arena->intern_table = NULL;
    arena->intern_count = 0;
    arena->intern_cap = 0;
    return arena;
}

void *arena_alloc(Arena *arena, size_t size) {
    size = ALIGN_UP(size, 8);

    if (arena->current->used + size > arena->current->size) {
        size_t block_size = arena->default_block_size;
        if (size > block_size) {
            block_size = size;
        }
        ArenaBlock *block = arena_block_create(block_size);
        arena->current->next = block;
        arena->current = block;
    }

    void *ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    return ptr;
}

char *arena_copy_string(Arena *arena, const char *source) {
    size_t len = strlen(source);
    char *duplicated = arena_alloc(arena, len + 1);
    memcpy(duplicated, source, len + 1);
    return duplicated;
}

char *arena_copy_string_with_length(Arena *arena, const char *source, size_t len) {
    char *duplicated = arena_alloc(arena, len + 1);
    memcpy(duplicated, source, len);
    duplicated[len] = '\0';
    return duplicated;
}

static uint32_t intern_hash(const char *source, size_t len) {
    uint32_t h = 5381u;
    for (size_t i = 0; i < len; i++)
        h = h * 33u ^ (uint32_t)(unsigned char)source[i];
    return h;
}

/* Rehash an existing (already-deduplicated) entry into the grown table.
 * Only used by intern_table_grow, which owns entries with no duplicates
 * to check for, so it skips straight to the first empty slot. */
static void intern_table_insert(Arena *arena, const char *str, size_t len) {
    uint32_t mask = (uint32_t)(arena->intern_cap - 1);
    uint32_t h = intern_hash(str, len) & mask;
    while (arena->intern_table[h].str) h = (h + 1) & mask;
    arena->intern_table[h].str = str;
    arena->intern_table[h].len = len;
}

static void intern_table_grow(Arena *arena) {
    InternEntry *old_table = arena->intern_table;
    int old_cap = arena->intern_cap;
    arena->intern_cap = old_cap ? old_cap * 2 : ARENA_INTERN_INITIAL_CAP;
    arena->intern_table = calloc((size_t)arena->intern_cap, sizeof(InternEntry));
    if (!arena->intern_table) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    for (int i = 0; i < old_cap; i++) {
        if (old_table[i].str) intern_table_insert(arena, old_table[i].str, old_table[i].len);
    }
    free(old_table);
}

const char *arena_intern_string(Arena *arena, const char *source, size_t len) {
    if ((arena->intern_count + 1) * 2 > arena->intern_cap) intern_table_grow(arena);

    uint32_t mask = (uint32_t)(arena->intern_cap - 1);
    uint32_t h = intern_hash(source, len) & mask;
    for (;;) {
        InternEntry *entry = &arena->intern_table[h];
        if (!entry->str) {
            char *copy = arena_copy_string_with_length(arena, source, len);
            entry->str = copy;
            entry->len = len;
            arena->intern_count++;
            return copy;
        }
        if (entry->len == len && memcmp(entry->str, source, len) == 0) return entry->str;
        h = (h + 1) & mask;
    }
}

void arena_destroy(Arena *arena) {
    ArenaBlock *block = arena->first;
    while (block) {
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    free(arena->intern_table);
    free(arena);
}
