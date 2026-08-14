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
#include <string.h>

GrayArray gray_array_new(GrayArena *arena, int32_t elem_size, int32_t initial_cap) {
    GrayArray arr;
    arr.elem_size = elem_size;
    arr.len = 0;
    arr.cap = initial_cap > 0 ? initial_cap : GRAY_ARRAY_MIN_CAP;
    arr.iterating = 0;
    arr.data = gray_arena_alloc(arena, (size_t)arr.cap * (size_t)arr.elem_size);
    return arr;
}

GrayArray gray_array_from(GrayArena *arena, const void *data, int32_t elem_size, int32_t count) {
    GrayArray arr;
    arr.elem_size = elem_size;
    arr.len = count;
    arr.cap = count > 0 ? count : GRAY_ARRAY_MIN_CAP;
    arr.iterating = 0;
    arr.data = gray_arena_alloc_uninitialized(arena, (size_t)arr.cap * (size_t)arr.elem_size);
    if (count > 0 && data) {
        memcpy(arr.data, data, (size_t)count * (size_t)elem_size);
    }
    return arr;
}

void *gray_array_get_ptr(GrayArray *arr, int32_t index, const char *file, int line) {
    if (index < 0 || index >= arr->len) {
        gray_panic_code_at(file, line, "P0033", "index out of bounds; tried to access index %d but the length is %d", index, arr->len);
    }
    return (char *)arr->data + (size_t)index * (size_t)arr->elem_size;
}

void gray_array_set(GrayArray *arr, int32_t index, const void *value, const char *file, int line) {
    if (gray_atomic_load32(&arr->iterating) > 0)
        gray_panic_code_at(file, line, "P0034", "cannot modify array during for_each iteration");
    if (index < 0 || index >= arr->len) {
        gray_panic_code_at(file, line, "P0033", "index out of bounds; tried to access index %d but the length is %d", index, arr->len);
    }
    memcpy((char *)arr->data + (size_t)index * (size_t)arr->elem_size,
           value, (size_t)arr->elem_size);
}

void gray_array_push(GrayArena *arena, GrayArray *arr, const void *value, const char *file, int line) {
    if (gray_atomic_load32(&arr->iterating) > 0)
        gray_panic_code_at(file, line, "P0034", "cannot modify array during for_each iteration");
    if (arr->len >= arr->cap) {
        int32_t new_cap;
        if (arr->cap < GRAY_ARRAY_MIN_CAP) {
            new_cap = GRAY_ARRAY_MIN_CAP;
        } else if (arr->cap > INT32_MAX / 2) {
            gray_panic_code_at(file, line, "P0035", "array capacity overflow");
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
    memcpy((char *)arr->data + (size_t)arr->len * (size_t)arr->elem_size,
           value, (size_t)arr->elem_size);
    arr->len++;
}

GrayArray gray_array_copy(GrayArena *arena, GrayArray *src) {
    return gray_array_from(arena, src->data, src->elem_size, src->len);
}
