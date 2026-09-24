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
#include <string.h>

#define GRAY_ARRAY_MIN_CAP            4

/* What an array's (or a map's keys' or values') elements are. Stdlib code
 * reads, writes and compares elements by this kind, through the gray_elem_*
 * helpers below — never by guessing from elem_size. */
typedef enum {
    GRAY_ELEM_OTHER = 0,    /* struct, pointer, function reference: compared as bytes */
    GRAY_ELEM_I8, GRAY_ELEM_I16, GRAY_ELEM_I32, GRAY_ELEM_I64, GRAY_ELEM_I128, GRAY_ELEM_I256,
    GRAY_ELEM_U8, GRAY_ELEM_U16, GRAY_ELEM_U32, GRAY_ELEM_U64, GRAY_ELEM_U128, GRAY_ELEM_U256,
    GRAY_ELEM_F32, GRAY_ELEM_F64,
    GRAY_ELEM_BOOL, GRAY_ELEM_CHAR,
    GRAY_ELEM_STRING, GRAY_ELEM_ARRAY, GRAY_ELEM_MAP,
} GrayElemKind;

typedef struct {
    void *data;
    GrayArena *arena;           /* arena owning data; NULL if unknown */
    int32_t len;
    int32_t cap;
    int32_t elem_size;
    int32_t iterating;          /* >0 while a for_each is active */
    int32_t elem_kind;          /* GrayElemKind */
} GrayArray;

/* Create an empty array of elem_kind elements elem_size bytes wide */
GrayArray gray_array_new(GrayArena *arena, int32_t elem_size, int32_t initial_cap, int32_t elem_kind);

/* Create an array from a C literal (copies data into arena) */
GrayArray gray_array_from(GrayArena *arena, const void *data, int32_t elem_size, int32_t count,
                          int32_t elem_kind);

/* The GrayElemKind of C element type T. Generated code creates every array
 * through GRAY_ARRAY_NEW_OF / GRAY_ARRAY_FROM_OF, naming the element's C type,
 * so an array's kind always matches the type its elements are stored as. A
 * C enum is compatible with an int type and takes that kind; a wide integer,
 * string, array or map struct its own; anything else (a struct, a pointer)
 * is OTHER. Expanded only in generated code, which includes bigint.h and
 * map.h for the wide-integer and map types it names. */
#define GRAY_ELEM_KIND_OF(T) _Generic((T){0}, \
    signed char: GRAY_ELEM_I8, short: GRAY_ELEM_I16, int: GRAY_ELEM_I32, \
    long: (sizeof(long) == 8 ? GRAY_ELEM_I64 : GRAY_ELEM_I32), long long: GRAY_ELEM_I64, \
    unsigned char: GRAY_ELEM_U8, unsigned short: GRAY_ELEM_U16, unsigned int: GRAY_ELEM_U32, \
    unsigned long: (sizeof(long) == 8 ? GRAY_ELEM_U64 : GRAY_ELEM_U32), \
    unsigned long long: GRAY_ELEM_U64, \
    float: GRAY_ELEM_F32, double: GRAY_ELEM_F64, bool: GRAY_ELEM_BOOL, \
    gray_i128: GRAY_ELEM_I128, gray_u128: GRAY_ELEM_U128, \
    gray_i256: GRAY_ELEM_I256, gray_u256: GRAY_ELEM_U256, \
    GrayString: GRAY_ELEM_STRING, GrayArray: GRAY_ELEM_ARRAY, GrayMap: GRAY_ELEM_MAP, \
    default: GRAY_ELEM_OTHER)

#define GRAY_ARRAY_NEW_OF(arena, T, cap) \
    gray_array_new((arena), (int32_t)sizeof(T), (cap), GRAY_ELEM_KIND_OF(T))
#define GRAY_ARRAY_FROM_OF(arena, T, data, count) \
    gray_array_from((arena), (data), (int32_t)sizeof(T), (count), GRAY_ELEM_KIND_OF(T))
#define GRAY_MAP_NEW_OF(arena, K, V, cap) \
    gray_map_new_kind((arena), (int32_t)sizeof(K), (int32_t)sizeof(V), (cap), \
                      GRAY_ELEM_KIND_OF(K), GRAY_ELEM_KIND_OF(V))

/* The element helpers. Each takes the elements' kind and, for a kind whose
 * width it cannot know (OTHER, ARRAY, MAP), their size. */

/* <0, 0 or >0 as a orders before, with or after b: numbers by value, strings
 * by bytes then length, false before true, anything else by bytes. */
int gray_elem_compare(int32_t kind, int32_t size, const void *a, const void *b);

/* Whether a equals b: numbers by value (so 0.0 equals -0.0), strings by
 * content, anything else by bytes. */
bool gray_elem_equal(int32_t kind, int32_t size, const void *a, const void *b);

/* A hash consistent with gray_elem_equal. */
uint32_t gray_elem_hash(int32_t kind, int32_t size, const void *p);

/* *acc += *value in the element type, panicking on overflow the way `+` on
 * that type does. */
void gray_elem_add(int32_t kind, void *acc, const void *value, const char *file, int line);

/* The number element at p as a double. */
double gray_elem_to_double(int32_t kind, const void *p);

/* The integer element at p (no wider than 64 bits) as an int64_t or a
 * uint64_t; a float element converts, bool and char read as integers. */
int64_t gray_elem_to_i64(int32_t kind, const void *p);
uint64_t gray_elem_to_u64(int32_t kind, const void *p);

/* Get a pointer to element at index (with bounds checking) */
void *gray_array_get_ptr(GrayArray *arr, int64_t index, const char *file, int line);

/* Set element at index (with bounds checking) */
void gray_array_set(GrayArray *arr, int64_t index, const void *value, const char *file, int line);

/* Panic paths of the index macros, out of line so the inline check stays small */
void gray_array_oob_panic(int64_t index, int32_t len, const char *file, int line)
    __attribute__((noreturn, cold));
void gray_array_iterating_panic(const char *file, int line)
    __attribute__((noreturn, cold));

/* Ensure room for one more element, growing the backing store if full.
 * Growth is allocate-and-copy: the arena has no realloc, so the old store
 * lives on until the arena is reset or destroyed. Grows into the array's
 * owning arena when it has one, so an array reached through a mutable
 * reference does not end up backed by a shorter-lived scope arena.
 * file/line locate the P0130 panic on capacity overflow; pass NULL/0 for
 * a panic without a source location. */
void gray_array_grow(GrayArena *arena, GrayArray *arr, const char *file, int line);

/* Append an element (may reallocate on the arena) */
void gray_array_push(GrayArena *arena, GrayArray *arr, const void *value, const char *file, int line);

/* Convenience macro for stdlib callers (uses C file/line) */
#define GRAY_ARRAY_PUSH(arena, arr, val) gray_array_push((arena), (arr), (val), __FILE__, __LINE__)

/* Deep copy an array */
GrayArray gray_array_copy(GrayArena *arena, GrayArray *src);

/* Typed access macros — stdlib callers (use C file/line) */
#define GRAY_ARRAY_GET(arr, type, i) (*(type *)gray_array_get_ptr(&(arr), (i), __FILE__, __LINE__))
#define GRAY_ARRAY_SET(arr, type, i, val) do { type _v = (val); gray_array_set(&(arr), (i), &_v, __FILE__, __LINE__); } while(0)

/* Typed access macros — codegen callers (pass Grayscale source location).
 * The bounds check is inline; SET stores sizeof(type) bytes when the array's
 * elem_size matches, and elem_size bytes otherwise, as gray_array_set does. */
#define GRAY_ARRAY_GET_AT(arr, type, i, f, l) \
    (*(type *)({ \
        GrayArray *_ga = &(arr); int64_t _gi = (i); \
        if (__builtin_expect(_gi < 0 || _gi >= _ga->len, 0)) \
            gray_array_oob_panic(_gi, _ga->len, (f), (l)); \
        (void *)((char *)_ga->data + (size_t)_gi * (size_t)_ga->elem_size); \
    }))
#define GRAY_ARRAY_SET_AT(arr, type, i, val, f, l) do { \
        type _v = (val); GrayArray *_ga = &(arr); int64_t _gi = (i); \
        if (__builtin_expect(__atomic_load_n(&_ga->iterating, __ATOMIC_RELAXED) > 0, 0)) \
            gray_array_iterating_panic((f), (l)); \
        if (__builtin_expect(_gi < 0 || _gi >= _ga->len, 0)) \
            gray_array_oob_panic(_gi, _ga->len, (f), (l)); \
        char *_gp = (char *)_ga->data + (size_t)_gi * (size_t)_ga->elem_size; \
        if (_ga->elem_size == (int32_t)sizeof(type)) memcpy(_gp, &_v, sizeof(type)); \
        else memcpy(_gp, &_v, (size_t)_ga->elem_size); \
    } while(0)

/* Pointer to element i for a read-modify-write (`xs[i] += v`), making the
 * same no-mutation-during-for_each and bounds checks GRAY_ARRAY_SET_AT
 * makes. `arr_ptr` is a GrayArray *. */
#define GRAY_ARRAY_PTR_FOR_WRITE(arr_ptr, type, i, f, l) \
    ((type *)({ \
        GrayArray *_ga = (arr_ptr); int64_t _gi = (i); \
        if (__builtin_expect(__atomic_load_n(&_ga->iterating, __ATOMIC_RELAXED) > 0, 0)) \
            gray_array_iterating_panic((f), (l)); \
        if (__builtin_expect(_gi < 0 || _gi >= _ga->len, 0)) \
            gray_array_oob_panic(_gi, _ga->len, (f), (l)); \
        (void *)((char *)_ga->data + (size_t)_gi * (size_t)_ga->elem_size); \
    }))

/* Create from typed literal — helper macros */
#define GRAY_ARRAY_FROM_I64(arena, ...) \
    gray_array_from((arena), (int64_t[]){__VA_ARGS__}, sizeof(int64_t), \
        sizeof((int64_t[]){__VA_ARGS__}) / sizeof(int64_t), GRAY_ELEM_I64)

#define GRAY_ARRAY_FROM_F64(arena, ...) \
    gray_array_from((arena), (double[]){__VA_ARGS__}, sizeof(double), \
        sizeof((double[]){__VA_ARGS__}) / sizeof(double), GRAY_ELEM_F64)

#define GRAY_ARRAY_FROM_BOOL(arena, ...) \
    gray_array_from((arena), (bool[]){__VA_ARGS__}, sizeof(bool), \
        sizeof((bool[]){__VA_ARGS__}) / sizeof(bool), GRAY_ELEM_BOOL)

#define GRAY_ARRAY_FROM_STR(arena, ...) \
    gray_array_from((arena), (GrayString[]){__VA_ARGS__}, sizeof(GrayString), \
        sizeof((GrayString[]){__VA_ARGS__}) / sizeof(GrayString), GRAY_ELEM_STRING)

#endif
