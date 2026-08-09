/*
 * runtime.c — Core runtime implementation for the Grayscale compiler.
 * Provides arena-based memory allocation, string construction, panic
 * handling, and scope lifecycle management used by all generated C code.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "runtime.h"
#include "util/colors.h"
#include <stdarg.h>
#include <unistd.h>

static inline int panic_use_color(void) {
    return isatty(STDERR_FILENO) && !getenv("NO_COLOR");
}

/* --- Per-thread default arena --- */

_Thread_local GrayArena *gray_default_arena = NULL;

/* --- Persistent heap arena (used by new()) --- */

_Thread_local GrayArena *gray_heap_arena = NULL;

/* --- Arena Allocator --- */

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

static GrayArenaBlock *gray_arena_block_create(size_t size) {
    GrayArenaBlock *block = (GrayArenaBlock *)malloc(sizeof(GrayArenaBlock) + size);
    if (!block) {
        fprintf(stderr, "Grayscale runtime: out of memory\n");
        exit(1);
    }
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

GrayArena *gray_arena_create(size_t initial_size) {
    GrayArena *arena = (GrayArena *)malloc(sizeof(GrayArena));
    if (!arena) {
        fprintf(stderr, "Grayscale runtime: out of memory\n");
        exit(1);
    }
    arena->default_block_size = initial_size;
    arena->first = gray_arena_block_create(initial_size);
    arena->current = arena->first;
    arena->max_bytes = 0;
    arena->total_allocated = initial_size;
    arena->destroyed = false;
    return arena;
}

void *gray_arena_alloc_uninitialized(GrayArena *arena, size_t size) {
    if (arena->destroyed)
        gray_panic_code("P0001", "cannot allocate from a destroyed arena; mem.destroy() was already called on this arena");
    size = ALIGN_UP(size, 8);
    if (size > arena->current->size - arena->current->used) {
        size_t block_size = arena->default_block_size;
        if (size > block_size) block_size = size;
        if (arena->max_bytes > 0 &&
            arena->total_allocated + block_size > arena->max_bytes) {
            gray_panic_code("P0104",
                "arena memory limit exceeded: attempted to grow beyond the maximum of %zu bytes",
                arena->max_bytes);
        }
        GrayArenaBlock *block = gray_arena_block_create(block_size);
        arena->current->next = block;
        arena->current = block;
        arena->total_allocated += block_size;
    }
    void *ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    return ptr;
}

void *gray_arena_alloc(GrayArena *arena, size_t size) {
    void *ptr = gray_arena_alloc_uninitialized(arena, size);
    memset(ptr, 0, ALIGN_UP(size, 8));
    return ptr;
}

void gray_arena_reset(GrayArena *arena) {
    /* Reset all blocks — reuse memory without freeing */
    GrayArenaBlock *block = arena->first;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    arena->current = arena->first;
    arena->total_allocated = arena->first ? arena->first->size : 0;
}

void gray_arena_destroy(GrayArena *arena, const char *file, int line) {
    if (!arena) return;
    if (arena->destroyed)
        gray_panic_code_at(file, line, "P0002", "mem.destroy() called on an arena that was already destroyed; each arena can only be destroyed once");
    GrayArenaBlock *block = arena->first;
    while (block) {
        GrayArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->first = NULL;
    arena->current = NULL;
    arena->destroyed = true;
    /* Don't free the arena struct — keep it alive so the destroyed flag
     * can be checked if the user calls destroy again. It will be cleaned
     * up at process exit. */
}

size_t gray_arena_usage(GrayArena *arena) {
    size_t total = 0;
    GrayArenaBlock *block = arena->first;
    while (block) {
        total += block->used;
        block = block->next;
    }
    return total;
}

/* --- Error --- */

GrayError *gray_error_new(GrayArena *arena, GrayString message) {
    GrayError *err = (GrayError *)gray_arena_alloc(arena, sizeof(GrayError));
    err->message = gray_string_new(arena, message.data, message.len);
    err->code = gray_string_lit("");
    return err;
}

/* --- String --- */

GrayString gray_string_new(GrayArena *arena, const char *s, int32_t len) {
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len + 1);
    memcpy(data, s, (size_t)len);
    data[len] = '\0';
    GrayString str;
    str.data = data;
    str.len = len;
    return str;
}

GrayString gray_c_string_dup(GrayArena *arena, const char *s) {
    if (s == NULL) return gray_string_lit("");
    size_t n = strlen(s);
    if (n > (size_t)INT32_MAX) n = (size_t)INT32_MAX;
    char *data = (char *)gray_arena_alloc_uninitialized(arena, n + 1);
    memcpy(data, s, n);
    data[n] = '\0';
    GrayString str;
    str.data = data;
    str.len = (int32_t)n;
    return str;
}

GrayString gray_string_format(GrayArena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0) {
        return gray_string_lit("");
    }

    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)needed + 1);
    va_start(args, fmt);
    vsnprintf(data, (size_t)needed + 1, fmt, args);
    va_end(args);

    GrayString str;
    str.data = data;
    str.len = (int32_t)needed;
    return str;
}

GrayString gray_string_concat(GrayArena *arena, GrayString a, GrayString b) {
    if (b.len > INT32_MAX - a.len) {
        fprintf(stderr, "Grayscale runtime: string concatenation overflow\n");
        exit(1);
    }
    int32_t new_len = a.len + b.len;
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    memcpy(data, a.data, (size_t)a.len);
    memcpy(data + a.len, b.data, (size_t)b.len);
    data[new_len] = '\0';
    GrayString s = { data, new_len };
    return s;
}

/* --- Scope-based memory management --- */

GrayScopeMark gray_scope_save(GrayArena *arena) {
    GrayScopeMark m;
    m.block = arena->current;
    m.used = arena->current ? arena->current->used : 0;
    return m;
}

void gray_scope_restore(GrayArena *arena, GrayScopeMark mark) {
    /* Reset all blocks AFTER the marked block */
    if (!mark.block) return;
    GrayArenaBlock *block = mark.block->next;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    /* Reset the marked block to the saved position */
    mark.block->used = mark.used;
    arena->current = mark.block;
}

/* --- Stack depth guard --- */
int gray_call_depth = 0;

/* --- Runtime Init/Shutdown --- */

void gray_runtime_init(size_t arena_limit) {
    gray_default_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    gray_heap_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    if (arena_limit > 0) {
        gray_default_arena->max_bytes = arena_limit;
        gray_heap_arena->max_bytes = arena_limit;
    }
}

void gray_runtime_shutdown(void) {
    if (gray_default_arena) {
        gray_arena_destroy(gray_default_arena, __FILE__, __LINE__);
        gray_default_arena = NULL;
    }
    if (gray_heap_arena) {
        gray_arena_destroy(gray_heap_arena, __FILE__, __LINE__);
        gray_heap_arena = NULL;
    }
}

/* --- Panic --- */

static _Noreturn void gray_panic_impl(const char *code, const char *file,
    int line, const char *fmt, va_list args) {
    fflush(stdout);
    int c = panic_use_color();

    /* Label: "panic" or "panic[CODE]" in bold red */
    fprintf(stderr, "%s%spanic", c ? COL_BOLD : "", c ? COL_RED : "");
    if (code) {
        fprintf(stderr, "[%s]", code);
        if (!file) fputc(':', stderr);
    }
    fprintf(stderr, "%s", c ? COL_RESET : "");

    /* Location (uncolored) */
    if (file) fprintf(stderr, " at %s:%d:", file, line);

    /* Message in bold */
    fprintf(stderr, " %s", c ? COL_BOLD : "");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "%s\n", c ? COL_RESET : "");
    exit(1);
}

void gray_panic(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gray_panic_impl(NULL, file, line, fmt, args);
}

void gray_panic_code(const char *code, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gray_panic_impl(code, NULL, 0, fmt, args);
}

void gray_panic_code_at(const char *file, int line, const char *code, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gray_panic_impl(code, file, line, fmt, args);
}
