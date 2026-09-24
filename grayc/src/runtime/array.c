/*
 * array.c — Dynamic array implementation for the Grayscale runtime.
 * Provides creation, push, pop, insert, remove, deep copy, and iteration
 * operations, all backed by arena-allocated storage.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "array.h"
#include "bigint.h"
#include <string.h>

GrayArray gray_array_new(GrayArena *arena, int32_t elem_size, int32_t initial_cap, int32_t elem_kind) {
    GrayArray arr;
    arr.arena = arena;
    arr.elem_size = elem_size;
    arr.elem_kind = elem_kind;
    arr.len = 0;
    arr.cap = initial_cap > 0 ? initial_cap : GRAY_ARRAY_MIN_CAP;
    arr.iterating = 0;
    /* Uninitialized: len is 0, so every slot is written by push/set (or a
     * full-length fill loop) before it can be read — reads are len-gated.
     * Matches gray_array_from and gray_array_grow. */
    arr.data = gray_arena_alloc_uninitialized(arena, (size_t)arr.cap * (size_t)arr.elem_size);
    return arr;
}

GrayArray gray_array_from(GrayArena *arena, const void *data, int32_t elem_size, int32_t count,
                          int32_t elem_kind) {
    GrayArray arr;
    arr.arena = arena;
    arr.elem_size = elem_size;
    arr.elem_kind = elem_kind;
    arr.len = count;
    arr.cap = count > 0 ? count : GRAY_ARRAY_MIN_CAP;
    arr.iterating = 0;
    arr.data = gray_arena_alloc_uninitialized(arena, (size_t)arr.cap * (size_t)arr.elem_size);
    if (count > 0 && data) {
        memcpy(arr.data, data, (size_t)count * (size_t)elem_size);
    }
    return arr;
}

void gray_array_oob_panic(int64_t index, int32_t len, const char *file, int line) {
    gray_panic_code_at(file, line, "P0033", "index out of bounds; tried to access index %lld but the length is %d", (long long)index, len);
}

void gray_array_iterating_panic(const char *file, int line) {
    gray_panic_code_at(file, line, "P0034", "cannot modify array during for_each iteration");
}

void *gray_array_get_ptr(GrayArray *arr, int64_t index, const char *file, int line) {
    if (index < 0 || index >= arr->len) gray_array_oob_panic(index, arr->len, file, line);
    return (char *)arr->data + (size_t)index * (size_t)arr->elem_size;
}

void gray_array_set(GrayArray *arr, int64_t index, const void *value, const char *file, int line) {
    if (gray_atomic_load32(&arr->iterating) > 0) gray_array_iterating_panic(file, line);
    if (index < 0 || index >= arr->len) gray_array_oob_panic(index, arr->len, file, line);
    memcpy((char *)arr->data + (size_t)index * (size_t)arr->elem_size,
           value, (size_t)arr->elem_size);
}

/* Grow into the arena the array was created in, not the caller's ambient
 * arena. An array reached through a mutable reference outlives the function
 * that appends to it; allocating the new backing store in a short-lived
 * scope arena leaves the array pointing at reclaimed memory once that scope
 * unwinds. */
void gray_array_grow(GrayArena *arena, GrayArray *arr, const char *file, int line) {
    if (arr->arena) arena = arr->arena;
    if (arr->len < arr->cap) return;

    int32_t new_cap;
    if (arr->cap < GRAY_ARRAY_MIN_CAP) {
        new_cap = GRAY_ARRAY_MIN_CAP;
    } else if (arr->cap > INT32_MAX / 2) {
        gray_panic_code_at(file, line, "P0130", "array capacity overflow");
    } else {
        new_cap = arr->cap * 2;
    }
    void *new_data = gray_arena_alloc_uninitialized(arena, (size_t)new_cap * (size_t)arr->elem_size);
    if (arr->data && arr->len > 0) {
        memcpy(new_data, arr->data, (size_t)arr->len * (size_t)arr->elem_size);
    }
    arr->data = new_data;
    arr->cap = new_cap;
}

/* Appending during a for_each is allowed: the loop captures its length up
 * front, so new elements land past the last index it will visit and existing
 * indices keep their elements. Destructive operations that shift or drop
 * elements still panic with P0034. */
void gray_array_push(GrayArena *arena, GrayArray *arr, const void *value, const char *file, int line) {
    gray_array_grow(arena, arr, file, line);
    memcpy((char *)arr->data + (size_t)arr->len * (size_t)arr->elem_size,
           value, (size_t)arr->elem_size);
    arr->len++;
}

GrayArray gray_array_copy(GrayArena *arena, GrayArray *src) {
    return gray_array_from(arena, src->data, src->elem_size, src->len, src->elem_kind);
}

/* --- Element helpers --- */

#define GRAY_ORDER(a, b) (((a) > (b)) - ((a) < (b)))

static int string_order(const GrayString *a, const GrayString *b) {
    int32_t shorter = a->len < b->len ? a->len : b->len;
    int bytes = shorter > 0 ? memcmp(a->data, b->data, (size_t)shorter) : 0;
    if (bytes != 0) return bytes < 0 ? -1 : 1;
    return GRAY_ORDER(a->len, b->len);
}

int gray_elem_compare(int32_t kind, int32_t size, const void *a, const void *b) {
    switch (kind) {
    case GRAY_ELEM_I8:   return GRAY_ORDER(*(const int8_t *)a, *(const int8_t *)b);
    case GRAY_ELEM_I16:  return GRAY_ORDER(*(const int16_t *)a, *(const int16_t *)b);
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: return GRAY_ORDER(*(const int32_t *)a, *(const int32_t *)b);
    case GRAY_ELEM_I64:  return GRAY_ORDER(*(const int64_t *)a, *(const int64_t *)b);
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: return GRAY_ORDER(*(const uint8_t *)a, *(const uint8_t *)b);
    case GRAY_ELEM_U16:  return GRAY_ORDER(*(const uint16_t *)a, *(const uint16_t *)b);
    case GRAY_ELEM_U32:  return GRAY_ORDER(*(const uint32_t *)a, *(const uint32_t *)b);
    case GRAY_ELEM_U64:  return GRAY_ORDER(*(const uint64_t *)a, *(const uint64_t *)b);
    case GRAY_ELEM_F32:  return GRAY_ORDER(*(const float *)a, *(const float *)b);
    case GRAY_ELEM_F64:  return GRAY_ORDER(*(const double *)a, *(const double *)b);
    case GRAY_ELEM_I128: return gray_i128_lt(*(const gray_i128 *)a, *(const gray_i128 *)b) ? -1 :
                                gray_i128_lt(*(const gray_i128 *)b, *(const gray_i128 *)a) ? 1 : 0;
    case GRAY_ELEM_U128: return gray_u128_lt(*(const gray_u128 *)a, *(const gray_u128 *)b) ? -1 :
                                gray_u128_lt(*(const gray_u128 *)b, *(const gray_u128 *)a) ? 1 : 0;
    case GRAY_ELEM_I256: return gray_i256_lt(*(const gray_i256 *)a, *(const gray_i256 *)b) ? -1 :
                                gray_i256_lt(*(const gray_i256 *)b, *(const gray_i256 *)a) ? 1 : 0;
    case GRAY_ELEM_U256: return gray_u256_lt(*(const gray_u256 *)a, *(const gray_u256 *)b) ? -1 :
                                gray_u256_lt(*(const gray_u256 *)b, *(const gray_u256 *)a) ? 1 : 0;
    case GRAY_ELEM_STRING: return string_order((const GrayString *)a, (const GrayString *)b);
    default: {
        int bytes = memcmp(a, b, (size_t)size);
        return (bytes > 0) - (bytes < 0);
    }
    }
}

bool gray_elem_equal(int32_t kind, int32_t size, const void *a, const void *b) {
    switch (kind) {
    case GRAY_ELEM_F32: return *(const float *)a == *(const float *)b;
    case GRAY_ELEM_F64: return *(const double *)a == *(const double *)b;
    case GRAY_ELEM_STRING: {
        const GrayString *left = a, *right = b;
        return left->len == right->len &&
               (left->len == 0 || memcmp(left->data, right->data, (size_t)left->len) == 0);
    }
    default: return memcmp(a, b, (size_t)size) == 0;
    }
}

static uint32_t fnv1a(const void *p, size_t n, uint32_t hash) {
    for (size_t i = 0; i < n; i++) {
        hash ^= ((const uint8_t *)p)[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t gray_elem_hash(int32_t kind, int32_t size, const void *p) {
    uint32_t seed = 2166136261u;
    switch (kind) {
    case GRAY_ELEM_F32: {
        float f = *(const float *)p;
        if (f == 0.0f) f = 0.0f;   /* -0.0 hashes as 0.0, as it compares */
        return fnv1a(&f, sizeof(f), seed);
    }
    case GRAY_ELEM_F64: {
        double d = *(const double *)p;
        if (d == 0.0) d = 0.0;
        return fnv1a(&d, sizeof(d), seed);
    }
    case GRAY_ELEM_STRING: {
        const GrayString *str = p;
        return fnv1a(str->data, (size_t)str->len, seed);
    }
    default: return fnv1a(p, (size_t)size, seed);
    }
}

void gray_elem_add(int32_t kind, void *acc, const void *value, const char *file, int line) {
    switch (kind) {
    case GRAY_ELEM_I8:  *(int8_t *)acc  = (int8_t)gray_sized_add_check(*(int8_t *)acc, *(const int8_t *)value, INT8_MIN, INT8_MAX, "i8", file, line); break;
    case GRAY_ELEM_I16: *(int16_t *)acc = (int16_t)gray_sized_add_check(*(int16_t *)acc, *(const int16_t *)value, INT16_MIN, INT16_MAX, "i16", file, line); break;
    case GRAY_ELEM_I32: *(int32_t *)acc = (int32_t)gray_sized_add_check(*(int32_t *)acc, *(const int32_t *)value, INT32_MIN, INT32_MAX, "i32", file, line); break;
    case GRAY_ELEM_I64: *(int64_t *)acc = gray_add_check(*(int64_t *)acc, *(const int64_t *)value, file, line); break;
    case GRAY_ELEM_U8:  *(uint8_t *)acc  = (uint8_t)gray_usized_add_check(*(uint8_t *)acc, *(const uint8_t *)value, UINT8_MAX, "u8", file, line); break;
    case GRAY_ELEM_U16: *(uint16_t *)acc = (uint16_t)gray_usized_add_check(*(uint16_t *)acc, *(const uint16_t *)value, UINT16_MAX, "u16", file, line); break;
    case GRAY_ELEM_U32: *(uint32_t *)acc = (uint32_t)gray_usized_add_check(*(uint32_t *)acc, *(const uint32_t *)value, UINT32_MAX, "u32", file, line); break;
    case GRAY_ELEM_U64: *(uint64_t *)acc = gray_uadd_check(*(uint64_t *)acc, *(const uint64_t *)value, file, line); break;
    case GRAY_ELEM_I128: *(gray_i128 *)acc = gray_i128_add_checked(*(gray_i128 *)acc, *(const gray_i128 *)value, file, line); break;
    case GRAY_ELEM_U128: *(gray_u128 *)acc = gray_u128_add_checked(*(gray_u128 *)acc, *(const gray_u128 *)value, file, line); break;
    case GRAY_ELEM_I256: *(gray_i256 *)acc = gray_i256_add_checked(*(gray_i256 *)acc, *(const gray_i256 *)value, file, line); break;
    case GRAY_ELEM_U256: *(gray_u256 *)acc = gray_u256_add_checked(*(gray_u256 *)acc, *(const gray_u256 *)value, file, line); break;
    case GRAY_ELEM_F32: *(float *)acc  += *(const float *)value; break;
    case GRAY_ELEM_F64: *(double *)acc += *(const double *)value; break;
    default: break;
    }
}

double gray_elem_to_double(int32_t kind, const void *p) {
    switch (kind) {
    case GRAY_ELEM_I8:   return *(const int8_t *)p;
    case GRAY_ELEM_I16:  return *(const int16_t *)p;
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: return *(const int32_t *)p;
    case GRAY_ELEM_I64:  return (double)*(const int64_t *)p;
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: return *(const uint8_t *)p;
    case GRAY_ELEM_U16:  return *(const uint16_t *)p;
    case GRAY_ELEM_U32:  return *(const uint32_t *)p;
    case GRAY_ELEM_U64:  return (double)*(const uint64_t *)p;
    case GRAY_ELEM_F32:  return *(const float *)p;
    case GRAY_ELEM_F64:  return *(const double *)p;
    case GRAY_ELEM_I128: {
        gray_i128 v = *(const gray_i128 *)p;
        return (double)v.hi * 18446744073709551616.0 + (double)v.lo;
    }
    case GRAY_ELEM_U128: {
        gray_u128 v = *(const gray_u128 *)p;
        return (double)v.hi * 18446744073709551616.0 + (double)v.lo;
    }
    case GRAY_ELEM_I256:
    case GRAY_ELEM_U256: {
        const uint64_t *w = kind == GRAY_ELEM_I256 ? ((const gray_i256 *)p)->w : ((const gray_u256 *)p)->w;
        bool negative = kind == GRAY_ELEM_I256 && (int64_t)w[3] < 0;
        double d = 0.0;
        for (int i = 3; i >= 0; i--)
            d = d * 18446744073709551616.0 + (double)(negative ? ~w[i] : w[i]);
        return negative ? -(d + 1.0) : d;
    }
    default: return 0.0;
    }
}

int64_t gray_elem_to_i64(int32_t kind, const void *p) {
    switch (kind) {
    case GRAY_ELEM_I8:   return *(const int8_t *)p;
    case GRAY_ELEM_I16:  return *(const int16_t *)p;
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: return *(const int32_t *)p;
    case GRAY_ELEM_I64:  return *(const int64_t *)p;
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: return *(const uint8_t *)p;
    case GRAY_ELEM_U16:  return *(const uint16_t *)p;
    case GRAY_ELEM_U32:  return *(const uint32_t *)p;
    case GRAY_ELEM_U64:  return (int64_t)*(const uint64_t *)p;
    case GRAY_ELEM_F32:
    case GRAY_ELEM_F64:  return (int64_t)gray_elem_to_double(kind, p);
    default:             return 0;
    }
}

uint64_t gray_elem_to_u64(int32_t kind, const void *p) {
    if (kind == GRAY_ELEM_U64) return *(const uint64_t *)p;
    return (uint64_t)gray_elem_to_i64(kind, p);
}
