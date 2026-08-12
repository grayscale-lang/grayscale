/*
 * array.h — Dynamic array type for the Grayscale runtime.
 * GrayArray is a fat pointer (data + len + cap + elem_size) with all
 * backing storage allocated from an arena.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_ARRAY_H
#define GRAY_ARRAY_H

#include "runtime.h"
#include "atomic.h"

#define GRAY_ARRAY_MIN_CAP            4
#define GRAY_MAX_INLINE_ELEM_SIZE     64

typedef struct {
    void *data;
    int32_t len;
    int32_t cap;
    int32_t elem_size;
    int32_t iterating;          /* >0 while a for_each is active */
} GrayArray;

/* Create an empty array with given element size and initial capacity */
GrayArray gray_array_new(GrayArena *arena, int32_t elem_size, int32_t initial_cap);

/* Create an array from a C literal (copies data into arena) */
GrayArray gray_array_from(GrayArena *arena, const void *data, int32_t elem_size, int32_t count);

/* Get a pointer to element at index (with bounds checking) */
void *gray_array_get_ptr(GrayArray *arr, int32_t index, const char *file, int line);

/* Set element at index (with bounds checking) */
void gray_array_set(GrayArray *arr, int32_t index, const void *value, const char *file, int line);

/* Append an element (may reallocate on the arena) */
void gray_array_push(GrayArena *arena, GrayArray *arr, const void *value, const char *file, int line);

/* Convenience macro for stdlib callers (uses C file/line) */
#define GRAY_ARRAY_PUSH(arena, arr, val) gray_array_push((arena), (arr), (val), __FILE__, __LINE__)

/* Deep copy an array */
GrayArray gray_array_copy(GrayArena *arena, GrayArray *src);

/* Typed access macros — stdlib callers (use C file/line) */
#define GRAY_ARRAY_GET(arr, type, i) (*(type *)gray_array_get_ptr(&(arr), (i), __FILE__, __LINE__))
#define GRAY_ARRAY_SET(arr, type, i, val) do { type _v = (val); gray_array_set(&(arr), (i), &_v, __FILE__, __LINE__); } while(0)

/* Typed access macros — codegen callers (pass Grayscale source location) */
#define GRAY_ARRAY_GET_AT(arr, type, i, f, l) (*(type *)gray_array_get_ptr(&(arr), (i), (f), (l)))
#define GRAY_ARRAY_SET_AT(arr, type, i, val, f, l) do { type _v = (val); gray_array_set(&(arr), (i), &_v, (f), (l)); } while(0)

/* Create from typed literal — helper macros */
#define GRAY_ARRAY_FROM_I64(arena, ...) \
    gray_array_from((arena), (int64_t[]){__VA_ARGS__}, sizeof(int64_t), \
        sizeof((int64_t[]){__VA_ARGS__}) / sizeof(int64_t))

#define GRAY_ARRAY_FROM_F64(arena, ...) \
    gray_array_from((arena), (double[]){__VA_ARGS__}, sizeof(double), \
        sizeof((double[]){__VA_ARGS__}) / sizeof(double))

#define GRAY_ARRAY_FROM_BOOL(arena, ...) \
    gray_array_from((arena), (bool[]){__VA_ARGS__}, sizeof(bool), \
        sizeof((bool[]){__VA_ARGS__}) / sizeof(bool))

#define GRAY_ARRAY_FROM_STR(arena, ...) \
    gray_array_from((arena), (GrayString[]){__VA_ARGS__}, sizeof(GrayString), \
        sizeof((GrayString[]){__VA_ARGS__}) / sizeof(GrayString))

#endif
