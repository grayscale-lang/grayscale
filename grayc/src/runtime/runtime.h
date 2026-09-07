/*
 * runtime.h — Core runtime header included in all generated C code.
 * Defines the arena allocator, GrayString, GrayError, scope management,
 * overflow-checked arithmetic, and panic infrastructure.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_RUNTIME_H
#define GRAY_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#include "../util/error_code_builtins.h"

/* --- Arena Defaults --- */
#define GRAY_DEFAULT_ARENA_SIZE   (1024 * 1024)
#define GRAY_MIN_ARENA_SIZE       4096

/* --- Arena Allocator --- */

typedef struct GrayArenaBlock {
    struct GrayArenaBlock *next;
    size_t size;
    size_t used;
    char data[];
} GrayArenaBlock;

typedef struct {
    GrayArenaBlock *first;
    GrayArenaBlock *current;
    size_t default_block_size;
    size_t max_bytes;         /* 0 = unlimited (user arenas) */
    size_t total_allocated;   /* cumulative bytes across all blocks */
    size_t peak_bytes;        /* high-water mark of total_allocated */
    size_t alloc_count;       /* cumulative allocations on this arena */
    bool destroyed;
} GrayArena;

GrayArena *gray_arena_create(size_t initial_size);
void *gray_arena_alloc(GrayArena *arena, size_t size);
void *gray_arena_alloc_uninitialized(GrayArena *arena, size_t size);
void gray_arena_reset(GrayArena *arena);
void gray_arena_destroy(GrayArena *arena, const char *file, int line);
size_t gray_arena_usage(GrayArena *arena);
size_t gray_arena_block_count(GrayArena *arena);

/* Per-thread default arena — each thread (including spawned threads) gets its own. */
extern _Thread_local GrayArena *gray_default_arena;

/* Persistent heap arena — lives for the lifetime of the program.
 * Used by new() so returned pointers are never dangling. */
extern _Thread_local GrayArena *gray_heap_arena;

/* Cumulative count of allocations made against whichever arena is installed
 * as the default or heap arena. Tracked separately from GrayArena::alloc_count
 * because a loop body swaps in a fresh per-iteration arena and destroys it
 * every pass: reading the installed arena's own counter makes the total drop
 * to zero on loop entry and discards everything the body allocated.
 * Thread-local to match the arena globals it follows. */
extern _Thread_local size_t gray_total_alloc_count;

/* --- String --- */

typedef struct {
    const char *data;
    int32_t len;
} GrayString;

/* --- Error --- */

/* Builtin ErrorCode slots, shared by the runtime/stdlib and generated code.
 * User #error_code enum variants are numbered after these by codegen. */
enum {
#define GRAY_ERR_SLOT(name) GRAY_ERR_##name,
    GRAY_ERROR_CODE_BUILTINS(GRAY_ERR_SLOT)
#undef GRAY_ERR_SLOT
    GRAY_ERR_BUILTIN_COUNT
};

/* code is an ErrorCode slot number (see error_code_builtins.h); slot 0 is
 * Unknown. "No error" is nil one level up, never a GrayError with code 0. */
typedef struct {
    int64_t code;
    GrayString msg;
} GrayError;

/* Create an error on the default arena */
GrayError *gray_error_new(GrayArena *arena, int64_t code, GrayString msg);

/* Map a C errno value to the closest builtin ErrorCode slot. */
int64_t gray_errno_code(int err);

/* gray_error_code_name(int64_t) — variant name for an ErrorCode slot — is
 * emitted per-program into the generated C (builtins + #error_code variants),
 * not provided by the runtime library. */

/* --- SourceLocation: compile-time substituted by the here() builtin --- */

typedef struct {
    GrayString file;
    int64_t line;
    int64_t column;
} GrayStruct_SourceLocation;

/* Create a string from a C string literal (no copy, points to static data) */
static inline GrayString gray_string_lit(const char *text) {
    GrayString str;
    str.data = text;
    str.len = (int32_t)strlen(text);
    return str;
}

/* String literal with explicit length — for strings containing null bytes */
static inline GrayString gray_string_lit_len(const char *text, int32_t len) {
    GrayString str;
    str.data = text;
    str.len = len;
    return str;
}

/* Compile-time string literal — works at file scope (C11 compliant) */
#define GRAY_STRING_LIT(s) ((GrayString){ (s), sizeof(s) - 1 })

/* Create a string with a copy on the arena */
GrayString gray_string_new(GrayArena *arena, const char *text, int32_t len);

/* Create a Grayscale string from a C char* by copying onto the arena.
 * NULL input -> empty string. Length is clamped at INT32_MAX. The
 * result has the same lifetime contract as every other arena string,
 * regardless of what happens to the source pointer afterwards. */
GrayString gray_c_string_dup(GrayArena *arena, const char *text);

/* String formatting (for interpolation) */
GrayString gray_string_format(GrayArena *arena, const char *fmt, ...);

/* Null-terminate a GrayString into a caller-provided buffer.
 * Truncates to buf_size-1 if needed. Returns buf for convenience. */
static inline const char *gray_cstr(GrayString str, char *buf, size_t buf_size) {
    size_t len = (size_t)str.len < buf_size - 1 ? (size_t)str.len : buf_size - 1;
    memcpy(buf, str.data, len);
    buf[len] = '\0';
    return buf;
}

/* String comparison */
static inline bool gray_string_eq(GrayString left, GrayString right) {
    if (left.len != right.len) return false;
    return memcmp(left.data, right.data, (size_t)left.len) == 0;
}

/* String concatenation */
GrayString gray_string_concat(GrayArena *arena, GrayString left, GrayString right);

/* --- Runtime Init/Shutdown --- */

void gray_runtime_init(size_t arena_limit);
void gray_runtime_shutdown(void);

/* Seconds elapsed since gray_runtime_init() was called */
double gray_runtime_uptime(void);

/* --- Scope-based memory management --- */

/* Watermark for mark-and-reset scoping. Save the arena's usage at
 * scope entry; on scope exit, reset to the watermark to free all
 * allocations made during the scope. */
typedef struct {
    GrayArenaBlock *block;
    size_t used;
} GrayScopeMark;

/* Save current arena state */
GrayScopeMark gray_scope_save(GrayArena *arena);

/* Restore arena to a saved state — frees everything allocated after
 * the mark. Only called for void scopes (no return value to preserve). */
void gray_scope_restore(GrayArena *arena, GrayScopeMark mark);

/* --- Panic --- */

/* Source location of the statement currently executing. Generated code stamps
 * this as it enters each statement so a panic raised from stdlib or builtin C
 * code via gray_panic_code() — which has no location of its own — still
 * reports the .gray file and line, the same as a language-level panic. NULL
 * before the first statement of a program runs. */
extern _Thread_local const char *gray_panic_call_file;
extern _Thread_local int gray_panic_call_line;

void gray_panic_code(const char *code, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));

void gray_panic_code_at(const char *file, int line, const char *code, const char *fmt, ...)
    __attribute__((format(printf, 4, 5), noreturn));

/* --- Test-runner failure hook (see runtime/test.c) ---
 * While a #test function is executing under `gray test`, gray_test_active is
 * true and any panic or failed assert is redirected here instead of exit(1):
 * the message is captured and control longjmps back to the runner so the
 * remaining tests still run. In every other program these are inert. */
extern bool gray_test_active;
extern jmp_buf gray_test_env;

_Noreturn void gray_test_vfail(const char *code, const char *file, int line,
                               const char *fmt, va_list args);
_Noreturn void gray_test_fail(const char *code, const char *file, int line,
                              const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Nil-check a pointer and return it, so a checked dereference stays an
 * lvalue: `((T*)gray_ptr_check(ptr, f, l))->field` can be assigned, indexed,
 * or have its address taken, unlike a statement-expression wrapper. */
static inline void *gray_ptr_check(void *ptr, const char *file, int line) {
    if (!ptr) gray_panic_code_at(file, line, "P0080", "nil pointer dereference");
    return ptr;
}

/* Arena-liveness check for a @mem pointer, composable the same way as
 * gray_ptr_check so a checked dereference stays an lvalue. Codegen emits
 * this ahead of a dereference of a variable it traced back to a direct
 * mem.init()/mem.alloc() call, re-checking the same arena expression that
 * produced the pointer — see is_stable_arena_expr in codegen.c. Catches a
 * use-after-destroy/reset the compile-time pointer checker couldn't trace
 * (an arena reached other than by a plain parameter name — STANDARD 11.7). */
static inline void *gray_mem_check_live(GrayArena *arena, void *ptr, const char *file, int line) {
    if (arena && arena->destroyed) {
        gray_panic_code_at(file, line, "P0117",
            "dereferenced a pointer into an arena that has been destroyed or reset");
    }
    return ptr;
}

/* --- Stack depth guard --- */

#define GRAY_MAX_CALL_DEPTH 10000
extern int gray_call_depth;

static inline void gray_enter_func(const char *file, int line) {
    if (++gray_call_depth > GRAY_MAX_CALL_DEPTH) {
        gray_panic_code_at(file, line, "P0003", "maximum recursion depth exceeded (%d calls deep)",
            GRAY_MAX_CALL_DEPTH);
    }
}

static inline void gray_exit_func(void) {
    gray_call_depth--;
}

/* Overflow-checked integer arithmetic */
static inline int64_t gray_add_check(int64_t left, int64_t right, const char *file, int line) {
    int64_t result;
    if (__builtin_add_overflow(left, right, &result))
        gray_panic_code_at(file, line, "P0004", "addition result is too large; value exceeds the range of int");
    return result;
}

static inline int64_t gray_sub_check(int64_t left, int64_t right, const char *file, int line) {
    int64_t result;
    if (__builtin_sub_overflow(left, right, &result))
        gray_panic_code_at(file, line, "P0005", "subtraction result is too large; value exceeds the range of int");
    return result;
}

static inline int64_t gray_mul_check(int64_t left, int64_t right, const char *file, int line) {
    int64_t result;
    if (__builtin_mul_overflow(left, right, &result))
        gray_panic_code_at(file, line, "P0006", "multiplication result is too large; value exceeds the range of int");
    return result;
}

static inline int64_t gray_neg_check(int64_t value, const char *file, int line) {
    int64_t result;
    if (__builtin_sub_overflow((int64_t)0, value, &result))
        gray_panic_code_at(file, line, "P0007", "negation result is too large; value exceeds the range of int");
    return result;
}

static inline int64_t gray_inc_check(int64_t value, const char *file, int line) {
    return gray_add_check(value, 1, file, line);
}

static inline int64_t gray_dec_check(int64_t value, const char *file, int line) {
    return gray_sub_check(value, 1, file, line);
}

/* Overflow-checked unsigned integer arithmetic */
static inline uint64_t gray_uadd_check(uint64_t left, uint64_t right, const char *file, int line) {
    uint64_t result;
    if (__builtin_add_overflow(left, right, &result))
        gray_panic_code_at(file, line, "P0008", "addition result is too large; value exceeds the range of uint");
    return result;
}

static inline uint64_t gray_usub_check(uint64_t left, uint64_t right, const char *file, int line) {
    if (right > left)
        gray_panic_code_at(file, line, "P0009", "subtraction result is negative, but uint cannot hold negative values");
    return left - right;
}

static inline uint64_t gray_umul_check(uint64_t left, uint64_t right, const char *file, int line) {
    uint64_t result;
    if (__builtin_mul_overflow(left, right, &result))
        gray_panic_code_at(file, line, "P0010", "multiplication result is too large; value exceeds the range of uint");
    return result;
}

/* Sized signed integer overflow checks (i8, i16, i32) */
static inline int64_t gray_sized_add_check(int64_t left, int64_t right, int64_t min_val, int64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = left + right;
    if (result < min_val || result > max_val)
        gray_panic_code_at(file, line, "P0011", "%s addition result is too large; value exceeds the range of this type", type_name);
    return result;
}

static inline int64_t gray_sized_sub_check(int64_t left, int64_t right, int64_t min_val, int64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = left - right;
    if (result < min_val || result > max_val)
        gray_panic_code_at(file, line, "P0012", "%s subtraction result is too large; value exceeds the range of this type", type_name);
    return result;
}

static inline int64_t gray_sized_mul_check(int64_t left, int64_t right, int64_t min_val, int64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = left * right;
    if (result < min_val || result > max_val)
        gray_panic_code_at(file, line, "P0013", "%s multiplication result is too large; value exceeds the range of this type", type_name);
    return result;
}

static inline int64_t gray_sized_neg_check(int64_t value, int64_t min_val, int64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = -value;
    if (result < min_val || result > max_val)
        gray_panic_code_at(file, line, "P0014", "%s negation result is too large; value exceeds the range of this type", type_name);
    return result;
}

/* Sized unsigned integer overflow checks (u8, u16, u32, byte).
 * Operands are int64_t so that signed operands (e.g. byte + int) are
 * handled correctly: a negative right-hand side must fire P0016, not
 * silently wrap to a large uint64 and trigger the wrong P0015 path. */
static inline uint64_t gray_usized_add_check(int64_t left, int64_t right, uint64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = left + right;
    if (result < 0)
        gray_panic_code_at(file, line, "P0016", "%s addition result is negative, but this unsigned type cannot hold negative values", type_name);
    if ((uint64_t)result > max_val)
        gray_panic_code_at(file, line, "P0015", "%s addition result is too large; value exceeds the range of this unsigned type", type_name);
    return (uint64_t)result;
}

static inline uint64_t gray_usized_sub_check(int64_t left, int64_t right, uint64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = left - right;
    if (result < 0)
        gray_panic_code_at(file, line, "P0016", "%s subtraction result is negative, but this unsigned type cannot hold negative values", type_name);
    if ((uint64_t)result > max_val)
        gray_panic_code_at(file, line, "P0015", "%s subtraction result is too large; value exceeds the range of this unsigned type", type_name);
    return (uint64_t)result;
}

static inline uint64_t gray_usized_mul_check(int64_t left, int64_t right, uint64_t max_val,
    const char *type_name, const char *file, int line) {
    int64_t result = left * right;
    if (result < 0)
        gray_panic_code_at(file, line, "P0016", "%s multiplication result is negative, but this unsigned type cannot hold negative values", type_name);
    if ((uint64_t)result > max_val)
        gray_panic_code_at(file, line, "P0017", "%s multiplication result is too large; value exceeds the range of this unsigned type", type_name);
    return (uint64_t)result;
}

/* Safe narrowing cast with overflow check */
static inline int64_t gray_cast_check(int64_t value, int64_t min_val, int64_t max_val,
    const char *type_name, const char *file, int line) {
    if (value < min_val || value > max_val)
        gray_panic_code_at(file, line, "P0018", "cast to %s failed; value %lld is outside the valid range (%lld to %lld)",
            type_name, (long long)value, (long long)min_val, (long long)max_val);
    return value;
}

static inline uint64_t gray_ucast_check(int64_t value, uint64_t max_val,
    const char *type_name, const char *file, int line) {
    if (value < 0 || (uint64_t)value > max_val)
        gray_panic_code_at(file, line, "P0019", "cast to %s failed; value %lld is outside the valid range (0 to %llu)",
            type_name, (long long)value, (unsigned long long)max_val);
    return (uint64_t)value;
}

/* Safe cast to an enum: the value must name a declared variant. A flags enum
 * is a set, so any combination of its variant bits is one of its values;
 * every other enum admits only the values it declares. */
static inline int64_t gray_enum_cast_check(int64_t value, const int64_t *variants, int32_t count,
    bool is_flags, const char *type_name, const char *file, int line) {
    if (is_flags) {
        int64_t mask = 0;
        for (int32_t i = 0; i < count; i++) mask |= variants[i];
        if (value >= 0 && (value & ~mask) == 0) return value;
    } else {
        for (int32_t i = 0; i < count; i++) {
            if (variants[i] == value) return value;
        }
    }
    gray_panic_code_at(file, line, "P0107", "cast to %s failed; value %lld does not match any variant of %s",
        type_name, (long long)value, type_name);
    return value;
}

/* Safe uint64 → int64 conversion: panics if value exceeds INT64_MAX */
static inline int64_t gray_uint_to_int_check(uint64_t value, const char *file, int line) {
    if (value > (uint64_t)9223372036854775807LL)
        gray_panic_code_at(file, line, "P0018", "cast to int failed; value %llu is outside the valid range (-9223372036854775808 to 9223372036854775807)",
            (unsigned long long)value);
    return (int64_t)value;
}

/* Safe float-to-int conversion with overflow check */
static inline int64_t gray_float_to_int(double value, const char *file, int line) {
    if (value > 9.223372036854775e+18 || value < -9.223372036854775e+18 ||
        value != value /* NaN */)
        gray_panic_code_at(file, line, "P0020", "cannot convert float to int; the value is too large, too small, or NaN");
    return (int64_t)value;
}

/* Safe float-to-uint conversion with range check */
static inline uint64_t gray_float_to_uint(double value, const char *file, int line) {
    /* 1.8446744073709552e+19 == 2^64 exactly as a double; any value >= it overflows uint64 */
    if (value < 0.0 || value >= 1.8446744073709552e+19 || value != value /* NaN */)
        gray_panic_code_at(file, line, "P0091", "cannot convert float to uint; the value is negative, too large, or NaN");
    return (uint64_t)value;
}

/* --- Result types for (value, Error) destructuring --- */

/* Forward declarations for types defined in other headers */
struct GrayArray_tag;
struct GrayMap_tag;

typedef struct { int64_t v0; GrayError *v1; } GrayResult_int;
typedef struct { void *v0; GrayError *v1; } GrayResult_ptr;

/* Wrap a bool-returning call into a GrayResult_bool.
 * Usage: GRAY_RESULT_WRAP_BOOL(arena, some_call(...), code, error_message); */
#define GRAY_RESULT_WRAP_BOOL(arena, call, code, err_msg) \
    do { GrayResult_bool _r; _r.v0 = (call); \
         if (_r.v0) { _r.v1 = NULL; } \
         else { _r.v1 = gray_error_new((arena), (code), (err_msg)); } \
         return _r; } while (0)

#endif
