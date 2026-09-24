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
#include "map.h"
#include "platform_rt.h"
#include "util/colors.h"
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

static inline int panic_use_color(void) {
    return gray_runtime_isatty(gray_runtime_stderr_fileno()) && !getenv("NO_COLOR");
}

/* --- Per-thread default arena --- */

_Thread_local GrayArena *gray_default_arena = NULL;

/* --- Persistent heap arena (used by new()) --- */

_Thread_local GrayArena *gray_heap_arena = NULL;

_Thread_local size_t gray_total_alloc_count = 0;

/* --- Stdlib call site, for locating panics raised from stdlib C code --- */

_Thread_local const char *gray_panic_call_file = NULL;
_Thread_local int gray_panic_call_line = 0;

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
    arena->maximum_bytes = 0;
    arena->total_allocated = initial_size;
    arena->peak_bytes = initial_size;
    arena->alloc_count = 0;
    arena->is_destroyed = false;
    return arena;
}

void *gray_arena_alloc_uninitialized(GrayArena *arena, size_t size) {
    if (arena->is_destroyed)
        gray_panic_code("P0001", "cannot allocate from a destroyed arena; mem.destroy() was already called on this arena");
    arena->alloc_count++;
    if (arena == gray_default_arena || arena == gray_heap_arena) gray_total_alloc_count++;
    size = ALIGN_UP(size, 8);
    if (size > arena->current->size - arena->current->used) {
        size_t block_size = arena->default_block_size;
        if (size > block_size) block_size = size;
        if (arena->maximum_bytes > 0 &&
            arena->total_allocated + block_size > arena->maximum_bytes) {
            gray_panic_code("P0104",
                "arena memory limit exceeded: attempted to grow beyond the maximum of %zu bytes",
                arena->maximum_bytes);
        }
        GrayArenaBlock *block = gray_arena_block_create(block_size);
        arena->current->next = block;
        arena->current = block;
        arena->total_allocated += block_size;
        if (arena->total_allocated > arena->peak_bytes)
            arena->peak_bytes = arena->total_allocated;
    }
    void *allocation = arena->current->data + arena->current->used;
    arena->current->used += size;
    return allocation;
}

void *gray_arena_alloc(GrayArena *arena, size_t size) {
    void *allocation = gray_arena_alloc_uninitialized(arena, size);
    memset(allocation, 0, ALIGN_UP(size, 8));
    return allocation;
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
    if (arena->is_destroyed)
        gray_panic_code_at(file, line, "P0002", "mem.destroy() called on an arena that was already destroyed; each arena can only be destroyed once");
    GrayArenaBlock *block = arena->first;
    while (block) {
        GrayArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->first = NULL;
    arena->current = NULL;
    arena->is_destroyed = true;
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

size_t gray_arena_block_count(GrayArena *arena) {
    size_t count = 0;
    GrayArenaBlock *block = arena->first;
    while (block) {
        count++;
        block = block->next;
    }
    return count;
}

/* --- Error --- */

GrayError *gray_error_new(GrayArena *arena, int64_t code, GrayString message) {
    GrayError *error = (GrayError *)gray_arena_alloc(arena, sizeof(GrayError));
    error->code = code;
    error->msg = gray_string_new(arena, message.data, message.len);
    return error;
}

/* Map a C errno value to the closest builtin ErrorCode slot. */
int64_t gray_errno_code(int error_number) {
    switch (error_number) {
        case ENOENT:       return GRAY_ERR_NotFound;
        case EEXIST:       return GRAY_ERR_AlreadyExists;
        case EACCES:
        case EPERM:        return GRAY_ERR_PermissionDenied;
        case EINVAL:       return GRAY_ERR_InvalidInput;
        case ERANGE:       return GRAY_ERR_OutOfRange;
        case ENOSYS:       return GRAY_ERR_Unsupported;
        case EINTR:        return GRAY_ERR_Interrupted;
        case ETIMEDOUT:    return GRAY_ERR_Timeout;
#ifdef EAGAIN
        case EAGAIN:       return GRAY_ERR_WouldBlock;
#endif
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:  return GRAY_ERR_WouldBlock;
#endif
#ifdef ECONNREFUSED
        case ECONNREFUSED: return GRAY_ERR_ConnectionRefused;
#endif
#ifdef ECONNRESET
        case ECONNRESET:   return GRAY_ERR_ConnectionReset;
#endif
#ifdef EADDRINUSE
        case EADDRINUSE:   return GRAY_ERR_AddressInUse;
#endif
#ifdef EPIPE
        case EPIPE:        return GRAY_ERR_BrokenPipe;
#endif
        default:           return GRAY_ERR_IoFailure;
    }
}

/* --- String --- */

GrayString gray_string_new(GrayArena *arena, const char *text, int32_t length) {
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(data, text, (size_t)length);
    data[length] = '\0';
    GrayString string;
    string.data = data;
    string.len = length;
    return string;
}

GrayString gray_c_string_dup(GrayArena *arena, const char *text) {
    if (text == NULL) return gray_string_lit("");
    size_t length = strlen(text);
    if (length > (size_t)INT32_MAX) length = (size_t)INT32_MAX;
    char *data = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
    memcpy(data, text, length);
    data[length] = '\0';
    GrayString string;
    string.data = data;
    string.len = (int32_t)length;
    return string;
}

GrayString gray_string_format(GrayArena *arena, const char *format, ...) {
    /* Format once into a stack buffer. The common callers — "%lld"/"%llu" for
     * an interpolated integer, println(i64) — never exceed 20 digits plus a
     * sign, so this is the whole job. Only a result that overflows the buffer
     * (a long "%s" path in a stdlib error message) pays the size-then-fill
     * fallback. */
    char buffer[32];
    va_list arguments;
    va_start(arguments, format);
    int needed = vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);

    if (needed < 0) {
        return gray_string_lit("");
    }

    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)needed + 1);
    if ((size_t)needed < sizeof buffer) {
        memcpy(data, buffer, (size_t)needed + 1);
    } else {
        va_start(arguments, format);
        vsnprintf(data, (size_t)needed + 1, format, arguments);
        va_end(arguments);
    }

    GrayString string;
    string.data = data;
    string.len = (int32_t)needed;
    return string;
}

GrayString gray_string_concat(GrayArena *arena, GrayString left, GrayString right) {
    if (right.len > INT32_MAX - left.len) {
        fprintf(stderr, "Grayscale runtime: string concatenation overflow\n");
        exit(1);
    }
    int32_t new_length = left.len + right.len;
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)new_length + 1);
    memcpy(data, left.data, (size_t)left.len);
    memcpy(data + left.len, right.data, (size_t)right.len);
    data[new_length] = '\0';
    GrayString result = { data, new_length };
    return result;
}

/* Join `count` GrayString parts in one pass: sum the lengths, allocate once,
 * copy each part exactly once. Used for string interpolation, where the
 * left-associative gray_string_concat chain would re-copy the accumulated
 * prefix at every boundary (O(n^2) in part count) and allocate n-1 dead
 * intermediates. Null-safe: a part with NULL data must have len 0. */
GrayString gray_string_concat_n(GrayArena *arena, int count, ...) {
    va_list arguments;

    va_start(arguments, count);
    int64_t total = 0;
    for (int i = 0; i < count; i++) {
        GrayString part = va_arg(arguments, GrayString);
        total += part.len;
    }
    va_end(arguments);

    if (total > INT32_MAX) {
        fprintf(stderr, "Grayscale runtime: string concatenation overflow\n");
        exit(1);
    }

    int32_t new_length = (int32_t)total;
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)new_length + 1);
    int32_t offset = 0;

    va_start(arguments, count);
    for (int i = 0; i < count; i++) {
        GrayString part = va_arg(arguments, GrayString);
        if (part.len > 0) {
            memcpy(data + offset, part.data, (size_t)part.len);
            offset += part.len;
        }
    }
    va_end(arguments);

    data[new_length] = '\0';
    GrayString result = { data, new_length };
    return result;
}

/* --- Scope-based memory management --- */

GrayScopeMark gray_scope_save(GrayArena *arena) {
    GrayScopeMark mark;
    mark.block = arena->current;
    mark.used = arena->current ? arena->current->used : 0;
    return mark;
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

static struct timespec gray_runtime_start_time;

void gray_runtime_init(size_t arena_limit) {
    gray_map_init_seed();
    gray_default_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    gray_heap_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    if (arena_limit > 0) {
        gray_default_arena->maximum_bytes = arena_limit;
        gray_heap_arena->maximum_bytes = arena_limit;
    }
    clock_gettime(CLOCK_MONOTONIC, &gray_runtime_start_time);
    /* Tear the runtime down at process exit rather than at the end of main, so
     * that a user callback registered with atexit() (which libc runs LIFO,
     * before this handler) still sees a live arena. */
    atexit(gray_runtime_shutdown);
}

double gray_runtime_uptime(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - gray_runtime_start_time.tv_sec) +
           (double)(now.tv_nsec - gray_runtime_start_time.tv_nsec) / 1e9;
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

static _Noreturn void gray_panic_implementation(const char *code, const char *file,
    int line, const char *format, va_list arguments) {
    /* Under `gray test`, a panic inside a test is a test failure, not a
     * process-ending event — hand it to the runner (never returns). */
    if (gray_test_active) {
        gray_test_vfail(code, file, line, format, arguments);
    }
    fflush(stdout);
    int use_color = panic_use_color();

    /* Label: "panic" or "panic[CODE]" in bold red */
    fprintf(stderr, "%s%spanic", use_color ? COLOR_BOLD : "", use_color ? COLOR_RED : "");
    if (code) {
        fprintf(stderr, "[%s]", code);
        if (!file) fputc(':', stderr);
    }
    fprintf(stderr, "%s", use_color ? COLOR_RESET : "");

    /* Location (uncolored) */
    if (file) fprintf(stderr, " at %s:%d:", file, line);

    /* Message in bold */
    fprintf(stderr, " %s", use_color ? COLOR_BOLD : "");
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "%s\n", use_color ? COLOR_RESET : "");
    exit(1);
}

void gray_panic_code(const char *code, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    /* No location of its own: fall back to the current statement's location,
     * stamped by generated code (NULL before the program's first statement). */
    gray_panic_implementation(code, gray_panic_call_file, gray_panic_call_line, format, arguments);
}

void gray_panic_code_at(const char *file, int line, const char *code, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    gray_panic_implementation(code, file, line, format, arguments);
}

/* Out-of-line failure tails for the checked-arithmetic helpers in runtime.h.
 * The Pxxxx code and message live here, once, instead of at every arithmetic
 * site in generated code. Messages mirror the registry in error_codes.h. */
#define GRAY_ARITHMETIC_TAIL(name, code, msg)                     \
    _Noreturn void name(const char *file, int line) {        \
        gray_panic_code_at(file, line, code, "%s", msg);     \
    }
GRAY_ARITHMETIC_TAIL(gray_arith_panic_add,  "P0004", "addition result is too large; value exceeds the range of i64")
GRAY_ARITHMETIC_TAIL(gray_arith_panic_sub,  "P0005", "subtraction result is too large; value exceeds the range of i64")
GRAY_ARITHMETIC_TAIL(gray_arith_panic_mul,  "P0006", "multiplication result is too large; value exceeds the range of i64")
GRAY_ARITHMETIC_TAIL(gray_arith_panic_neg,  "P0007", "negation result is too large; value exceeds the range of i64")
GRAY_ARITHMETIC_TAIL(gray_arith_panic_uadd, "P0008", "addition result is too large; value exceeds the range of u64")
GRAY_ARITHMETIC_TAIL(gray_arith_panic_usub, "P0009", "subtraction result is negative, but u64 cannot hold negative values")
GRAY_ARITHMETIC_TAIL(gray_arith_panic_umul, "P0010", "multiplication result is too large; value exceeds the range of u64")
#undef GRAY_ARITHMETIC_TAIL
