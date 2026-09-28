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

GrayArray gray_array_new(GrayArena *arena, int32_t element_size, int32_t initial_capacity, int32_t element_kind) {
    GrayArray array;
    array.arena = arena;
    array.elem_size = element_size;
    array.elem_kind = element_kind;
    array.len = 0;
    array.capacity = initial_capacity > 0 ? initial_capacity : GRAY_ARRAY_MIN_CAPACITY;
    array.iterating = 0;
    /* Uninitialized: len is 0, so every slot is written by push/set (or a
     * full-length fill loop) before it can be read — reads are len-gated.
     * Matches gray_array_from and gray_array_grow. */
    array.data = gray_arena_alloc_uninitialized(arena, (size_t)array.capacity * (size_t)array.elem_size);
    return array;
}

GrayArray gray_array_from(GrayArena *arena, const void *data, int32_t element_size, int32_t count,
                          int32_t element_kind) {
    GrayArray array;
    array.arena = arena;
    array.elem_size = element_size;
    array.elem_kind = element_kind;
    array.len = count;
    array.capacity = count > 0 ? count : GRAY_ARRAY_MIN_CAPACITY;
    array.iterating = 0;
    array.data = gray_arena_alloc_uninitialized(arena, (size_t)array.capacity * (size_t)array.elem_size);
    if (count > 0 && data) {
        memcpy(array.data, data, (size_t)count * (size_t)element_size);
    }
    return array;
}

void gray_array_oob_panic(int64_t index, int32_t length, const char *file, int line) {
    gray_panic_code_at(file, line, "P0033", "index out of bounds; tried to access index %lld but the length is %d", (long long)index, length);
}

void gray_array_iterating_panic(const char *file, int line) {
    gray_panic_code_at(file, line, "P0034", "cannot modify array during for_each iteration");
}

void *gray_array_get_ptr(GrayArray *array, int64_t index, const char *file, int line) {
    if (index < 0 || index >= array->len) gray_array_oob_panic(index, array->len, file, line);
    return (char *)array->data + (size_t)index * (size_t)array->elem_size;
}

void gray_array_set(GrayArray *array, int64_t index, const void *value, const char *file, int line) {
    if (gray_atomic_load32(&array->iterating) > 0) gray_array_iterating_panic(file, line);
    if (index < 0 || index >= array->len) gray_array_oob_panic(index, array->len, file, line);
    memcpy((char *)array->data + (size_t)index * (size_t)array->elem_size,
           value, (size_t)array->elem_size);
}

/* Grow into the arena the array was created in, not the caller's ambient
 * arena. An array reached through a mutable reference outlives the function
 * that appends to it; allocating the new backing store in a short-lived
 * scope arena leaves the array pointing at reclaimed memory once that scope
 * unwinds. */
void gray_array_grow(GrayArena *arena, GrayArray *array, const char *file, int line) {
    if (array->arena) arena = array->arena;
    if (array->len < array->capacity) return;

    int32_t new_capacity;
    if (array->capacity < GRAY_ARRAY_MIN_CAPACITY) {
        new_capacity = GRAY_ARRAY_MIN_CAPACITY;
    } else if (array->capacity > INT32_MAX / 2) {
        gray_panic_code_at(file, line, "P0130", "array capacity overflow");
    } else {
        new_capacity = array->capacity * 2;
    }
    void *new_data = gray_arena_alloc_uninitialized(arena, (size_t)new_capacity * (size_t)array->elem_size);
    if (array->data && array->len > 0) {
        memcpy(new_data, array->data, (size_t)array->len * (size_t)array->elem_size);
    }
    array->data = new_data;
    array->capacity = new_capacity;
}

/* Appending during a for_each is allowed: the loop captures its length up
 * front, so new elements land past the last index it will visit and existing
 * indices keep their elements. Destructive operations that shift or drop
 * elements still panic with P0034. */
void gray_array_push(GrayArena *arena, GrayArray *array, const void *value, const char *file, int line) {
    gray_array_grow(arena, array, file, line);
    memcpy((char *)array->data + (size_t)array->len * (size_t)array->elem_size,
           value, (size_t)array->elem_size);
    array->len++;
}

GrayArray gray_array_copy(GrayArena *arena, GrayArray *source) {
    return gray_array_from(arena, source->data, source->elem_size, source->len, source->elem_kind);
}

/* --- Element helpers --- */

#define GRAY_ORDER(a, b) (((a) > (b)) - ((a) < (b)))

static int string_order(const GrayString *left, const GrayString *right) {
    int32_t shorter = left->len < right->len ? left->len : right->len;
    int bytes = shorter > 0 ? memcmp(left->data, right->data, (size_t)shorter) : 0;
    if (bytes != 0) return bytes < 0 ? -1 : 1;
    return GRAY_ORDER(left->len, right->len);
}

int gray_elem_compare(int32_t kind, int32_t size, const void *left, const void *right) {
    switch (kind) {
    case GRAY_ELEM_I8:   return GRAY_ORDER(*(const int8_t *)left, *(const int8_t *)right);
    case GRAY_ELEM_I16:  return GRAY_ORDER(*(const int16_t *)left, *(const int16_t *)right);
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: return GRAY_ORDER(*(const int32_t *)left, *(const int32_t *)right);
    case GRAY_ELEM_I64:  return GRAY_ORDER(*(const int64_t *)left, *(const int64_t *)right);
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: return GRAY_ORDER(*(const uint8_t *)left, *(const uint8_t *)right);
    case GRAY_ELEM_U16:  return GRAY_ORDER(*(const uint16_t *)left, *(const uint16_t *)right);
    case GRAY_ELEM_U32:  return GRAY_ORDER(*(const uint32_t *)left, *(const uint32_t *)right);
    case GRAY_ELEM_U64:  return GRAY_ORDER(*(const uint64_t *)left, *(const uint64_t *)right);
    case GRAY_ELEM_F32:  return GRAY_ORDER(*(const float *)left, *(const float *)right);
    case GRAY_ELEM_F64:  return GRAY_ORDER(*(const double *)left, *(const double *)right);
    case GRAY_ELEM_I128: return gray_i128_lt(*(const gray_i128 *)left, *(const gray_i128 *)right) ? -1 :
                                gray_i128_lt(*(const gray_i128 *)right, *(const gray_i128 *)left) ? 1 : 0;
    case GRAY_ELEM_U128: return gray_u128_lt(*(const gray_u128 *)left, *(const gray_u128 *)right) ? -1 :
                                gray_u128_lt(*(const gray_u128 *)right, *(const gray_u128 *)left) ? 1 : 0;
    case GRAY_ELEM_I256: return gray_i256_lt(*(const gray_i256 *)left, *(const gray_i256 *)right) ? -1 :
                                gray_i256_lt(*(const gray_i256 *)right, *(const gray_i256 *)left) ? 1 : 0;
    case GRAY_ELEM_U256: return gray_u256_lt(*(const gray_u256 *)left, *(const gray_u256 *)right) ? -1 :
                                gray_u256_lt(*(const gray_u256 *)right, *(const gray_u256 *)left) ? 1 : 0;
    case GRAY_ELEM_STRING: return string_order((const GrayString *)left, (const GrayString *)right);
    default: {
        int bytes = memcmp(left, right, (size_t)size);
        return (bytes > 0) - (bytes < 0);
    }
    }
}

bool gray_elem_equal(int32_t kind, int32_t size, const void *left_element, const void *right_element) {
    switch (kind) {
    case GRAY_ELEM_F32: return *(const float *)left_element == *(const float *)right_element;
    case GRAY_ELEM_F64: return *(const double *)left_element == *(const double *)right_element;
    case GRAY_ELEM_STRING: {
        const GrayString *left = left_element, *right = right_element;
        return left->len == right->len &&
               (left->len == 0 || memcmp(left->data, right->data, (size_t)left->len) == 0);
    }
    default: return memcmp(left_element, right_element, (size_t)size) == 0;
    }
}

static uint32_t fnv1a(const void *data, size_t byte_count, uint32_t hash) {
    for (size_t i = 0; i < byte_count; i++) {
        hash ^= ((const uint8_t *)data)[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t gray_elem_hash(int32_t kind, int32_t size, const void *element) {
    uint32_t seed = 2166136261u;
    switch (kind) {
    case GRAY_ELEM_F32: {
        float value = *(const float *)element;
        if (value == 0.0f) value = 0.0f;   /* -0.0 hashes as 0.0, as it compares */
        return fnv1a(&value, sizeof(value), seed);
    }
    case GRAY_ELEM_F64: {
        double double_value = *(const double *)element;
        if (double_value == 0.0) double_value = 0.0;
        return fnv1a(&double_value, sizeof(double_value), seed);
    }
    case GRAY_ELEM_STRING: {
        const GrayString *string = element;
        return fnv1a(string->data, (size_t)string->len, seed);
    }
    default: return fnv1a(element, (size_t)size, seed);
    }
}

void gray_elem_add(int32_t kind, void *accumulator, const void *value, const char *file, int line) {
    switch (kind) {
    case GRAY_ELEM_I8:  *(int8_t *)accumulator  = (int8_t)gray_sized_add_check(*(int8_t *)accumulator, *(const int8_t *)value, INT8_MIN, INT8_MAX, "i8", file, line); break;
    case GRAY_ELEM_I16: *(int16_t *)accumulator = (int16_t)gray_sized_add_check(*(int16_t *)accumulator, *(const int16_t *)value, INT16_MIN, INT16_MAX, "i16", file, line); break;
    case GRAY_ELEM_I32: *(int32_t *)accumulator = (int32_t)gray_sized_add_check(*(int32_t *)accumulator, *(const int32_t *)value, INT32_MIN, INT32_MAX, "i32", file, line); break;
    case GRAY_ELEM_I64: *(int64_t *)accumulator = gray_add_check(*(int64_t *)accumulator, *(const int64_t *)value, file, line); break;
    case GRAY_ELEM_U8:  *(uint8_t *)accumulator  = (uint8_t)gray_usized_add_check(*(uint8_t *)accumulator, *(const uint8_t *)value, UINT8_MAX, "u8", file, line); break;
    case GRAY_ELEM_U16: *(uint16_t *)accumulator = (uint16_t)gray_usized_add_check(*(uint16_t *)accumulator, *(const uint16_t *)value, UINT16_MAX, "u16", file, line); break;
    case GRAY_ELEM_U32: *(uint32_t *)accumulator = (uint32_t)gray_usized_add_check(*(uint32_t *)accumulator, *(const uint32_t *)value, UINT32_MAX, "u32", file, line); break;
    case GRAY_ELEM_U64: *(uint64_t *)accumulator = gray_uadd_check(*(uint64_t *)accumulator, *(const uint64_t *)value, file, line); break;
    case GRAY_ELEM_I128: *(gray_i128 *)accumulator = gray_i128_add_checked(*(gray_i128 *)accumulator, *(const gray_i128 *)value, file, line); break;
    case GRAY_ELEM_U128: *(gray_u128 *)accumulator = gray_u128_add_checked(*(gray_u128 *)accumulator, *(const gray_u128 *)value, file, line); break;
    case GRAY_ELEM_I256: *(gray_i256 *)accumulator = gray_i256_add_checked(*(gray_i256 *)accumulator, *(const gray_i256 *)value, file, line); break;
    case GRAY_ELEM_U256: *(gray_u256 *)accumulator = gray_u256_add_checked(*(gray_u256 *)accumulator, *(const gray_u256 *)value, file, line); break;
    case GRAY_ELEM_F32: *(float *)accumulator  += *(const float *)value; break;
    case GRAY_ELEM_F64: *(double *)accumulator += *(const double *)value; break;
    default: break;
    }
}

double gray_elem_to_double(int32_t kind, const void *element) {
    switch (kind) {
    case GRAY_ELEM_I8:   return *(const int8_t *)element;
    case GRAY_ELEM_I16:  return *(const int16_t *)element;
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: return *(const int32_t *)element;
    case GRAY_ELEM_I64:  return (double)*(const int64_t *)element;
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: return *(const uint8_t *)element;
    case GRAY_ELEM_U16:  return *(const uint16_t *)element;
    case GRAY_ELEM_U32:  return *(const uint32_t *)element;
    case GRAY_ELEM_U64:  return (double)*(const uint64_t *)element;
    case GRAY_ELEM_F32:  return *(const float *)element;
    case GRAY_ELEM_F64:  return *(const double *)element;
    case GRAY_ELEM_I128: {
        gray_i128 wide_value = *(const gray_i128 *)element;
        return (double)wide_value.high * 18446744073709551616.0 + (double)wide_value.low;
    }
    case GRAY_ELEM_U128: {
        gray_u128 wide_value = *(const gray_u128 *)element;
        return (double)wide_value.high * 18446744073709551616.0 + (double)wide_value.low;
    }
    case GRAY_ELEM_I256:
    case GRAY_ELEM_U256: {
        const uint64_t *limbs = kind == GRAY_ELEM_I256 ? ((const gray_i256 *)element)->w : ((const gray_u256 *)element)->w;
        bool negative = kind == GRAY_ELEM_I256 && (int64_t)limbs[3] < 0;
        double value = 0.0;
        for (int i = 3; i >= 0; i--)
            value = value * 18446744073709551616.0 + (double)(negative ? ~limbs[i] : limbs[i]);
        return negative ? -(value + 1.0) : value;
    }
    default: return 0.0;
    }
}

int64_t gray_elem_to_i64(int32_t kind, const void *element) {
    switch (kind) {
    case GRAY_ELEM_I8:   return *(const int8_t *)element;
    case GRAY_ELEM_I16:  return *(const int16_t *)element;
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: return *(const int32_t *)element;
    case GRAY_ELEM_I64:  return *(const int64_t *)element;
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: return *(const uint8_t *)element;
    case GRAY_ELEM_U16:  return *(const uint16_t *)element;
    case GRAY_ELEM_U32:  return *(const uint32_t *)element;
    case GRAY_ELEM_U64:  return (int64_t)*(const uint64_t *)element;
    case GRAY_ELEM_F32:
    case GRAY_ELEM_F64:  return (int64_t)gray_elem_to_double(kind, element);
    default:             return 0;
    }
}

uint64_t gray_elem_to_u64(int32_t kind, const void *element) {
    if (kind == GRAY_ELEM_U64) return *(const uint64_t *)element;
    return (uint64_t)gray_elem_to_i64(kind, element);
}
