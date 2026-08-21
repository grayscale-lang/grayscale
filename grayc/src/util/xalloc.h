/*
 * xalloc.h — Allocation wrappers (xmalloc, xcalloc, xrealloc) that abort
 * on out-of-memory, used throughout the compiler for non-recoverable allocs.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 *
 * Contributors:
 *  - @confucius40
 */

#ifndef GRAYC_XALLOC_H
#define GRAYC_XALLOC_H

#include "arena.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void *xmalloc(size_t size) {
    void *ptr = malloc(size);

    if (!ptr) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }

    return ptr;
}

static inline void *xcalloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);

    if (!ptr) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }

    return ptr;
}

static inline void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);

    if (!new_ptr) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }

    return new_ptr;
}

/* Read an entire seekable file into a malloc'd NUL-terminated string.
 * Returns NULL on open failure or OOM. */
static inline char *read_file_to_string(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t n = fread(buffer, 1, (size_t)size, file);
    buffer[n] = '\0';

    fclose(file);
    return buffer;
}

/* Initial capacity for GROW_ARRAY — small enough to avoid waste,
 * large enough to cover the common case without early resizes. */
#define GROW_ARRAY_INIT_CAP 8

/* The shared doubling policy: the next capacity after cap. */
#define GROW_NEXT_CAP(cap) ((cap) ? (cap) * 2 : GROW_ARRAY_INIT_CAP)

/* Grow a dynamic array when count reaches capacity.
 * Doubles capacity (starting from GROW_ARRAY_INIT_CAP), then xrealloc's. */
#define GROW_ARRAY(arr, count, cap) \
    do { \
        if ((count) >= (cap)) { \
            (cap) = GROW_NEXT_CAP(cap); \
            (arr) = xrealloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
        } \
    } while (0)

/* Reallocate an arena-backed array to newcap elements, carrying the first
 * count across. The arena has no realloc, so growth is always allocate-and-
 * copy; the old allocation lives on until the arena is destroyed.
 * The count guard keeps the first growth of a NULL/empty array out of
 * memcpy, which may not take a NULL source even for zero bytes.
 * Use this directly only for parallel arrays that share one capacity: bump
 * the shared cap once with GROW_NEXT_CAP, then grow each array to it.
 * Otherwise prefer ARENA_GROW. */
#define ARENA_GROW_TO(arena, arr, count, newcap) \
    do { \
        void *arena_grown_ = arena_alloc((arena), sizeof(*(arr)) * (size_t)(newcap)); \
        if ((count) > 0) \
            memcpy(arena_grown_, (arr), sizeof(*(arr)) * (size_t)(count)); \
        (arr) = arena_grown_; \
    } while (0)

/* Arena counterpart to GROW_ARRAY: same doubling policy, allocate-and-copy
 * instead of xrealloc. */
#define ARENA_GROW(arena, arr, count, cap) \
    do { \
        if ((count) >= (cap)) { \
            (cap) = GROW_NEXT_CAP(cap); \
            ARENA_GROW_TO((arena), (arr), (count), (cap)); \
        } \
    } while (0)

#endif
