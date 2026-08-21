/*
 * arena.h — Public interface for the block-based arena allocator used to
 * allocate AST nodes, strings, and other compiler data structures.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_ARENA_H
#define GRAYC_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t size;
    size_t used;
    char data[];
} ArenaBlock;

typedef struct {
    const char *str;
    size_t len;
} InternEntry;

typedef struct {
    ArenaBlock *first;
    ArenaBlock *current;
    size_t default_block_size;
    InternEntry *intern_table;
    int intern_count;
    int intern_cap;
} Arena;

Arena *arena_create(size_t initial_size);
void *arena_alloc(Arena *arena, size_t size);
char *arena_copy_string(Arena *arena, const char *source);
char *arena_copy_string_with_length(Arena *arena, const char *source, size_t len);

/* Deduplicated version of arena_copy_string_with_length: returns the same
 * arena-owned pointer for every occurrence of an identical (source, len)
 * span seen so far by this arena, copying only on first occurrence. */
const char *arena_intern_string(Arena *arena, const char *source, size_t len);

void arena_destroy(Arena *arena);

#endif
