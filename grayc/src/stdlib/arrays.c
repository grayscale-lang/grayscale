/*
 * arrays.c — Implementation of the arrays stdlib module.
 * Provides append, insert, remove, sort, reverse, slice, and search
 * operations on GrayArray values.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 *
 * Contributors:
 *  - @SAY-5
 */

#include "arrays.h"
#include "../runtime/bigint.h"
#include <string.h>
#include <stdlib.h>

#define ARRAY_CHECK_ITER(arr) \
    do { if ((arr)->iterating > 0) \
        gray_panic_code("P0034", "cannot modify array during for_each iteration"); \
    } while (0)

/* === Modification === */

void gray_arrays_append(GrayArena *arena, GrayArray *arr, const void *value) {
    GRAY_ARRAY_PUSH(arena, arr, value);
}

void gray_arrays_insert_at(GrayArena *arena, GrayArray *arr, int64_t index, const void *value) {
    ARRAY_CHECK_ITER(arr);
    if (index < 0 || index > arr->len) {
        gray_panic_code("P0043",
            "arrays.insert_at: index %lld is out of bounds for an array of length %d",
            (long long)index, arr->len);
    }

    /* NULL/0 keeps the P0130 panic locationless, as it has always been here. */
    gray_array_grow(arena, arr, NULL, 0);

    size_t element_size = (size_t)arr->elem_size;
    char *data = (char *)arr->data;
    if (index < arr->len) {
        memmove(data + (index + 1) * element_size, data + index * element_size,
                (size_t)(arr->len - index) * element_size);
    }
    memcpy(data + index * element_size, value, element_size);
    arr->len++;
}

void gray_arrays_prepend(GrayArena *arena, GrayArray *arr, const void *value) {
    gray_arrays_insert_at(arena, arr, 0, value);
}

void gray_arrays_remove_at(GrayArray *arr, int64_t index) {
    ARRAY_CHECK_ITER(arr);
    if (index < 0 || index >= arr->len)
        gray_panic_code("P0044",
            "arrays.remove_at: index %lld is out of bounds for an array of length %d",
            (long long)index, arr->len);
    char *data = (char *)arr->data;
    size_t element_size = (size_t)arr->elem_size;
    memmove(data + index * element_size, data + (index + 1) * element_size, (arr->len - 1 - index) * element_size);
    arr->len--;
}

/* Every function below that looks at an element's value does so through the
 * gray_elem_* helpers, by the array's elem_kind. A value argument points at a
 * value of the element type. */

#define ELEM_AT(arr, i) ((char *)(arr)->data + (size_t)(i) * (size_t)(arr)->elem_size)

int64_t gray_arrays_index_of(GrayArray *arr, const void *value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (gray_elem_equal(arr->elem_kind, arr->elem_size, ELEM_AT(arr, i), value)) return i;
    }
    return -1;
}

void gray_arrays_remove(GrayArray *arr, const void *value) {
    int64_t index = gray_arrays_index_of(arr, value);
    if (index >= 0) gray_arrays_remove_at(arr, index);
}

void gray_arrays_clear(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    arr->len = 0;
}

void gray_arrays_fill(GrayArena *arena, GrayArray *arr, const void *value, int64_t count) {
    gray_arrays_clear(arr);
    if (count > INT32_MAX)
        gray_panic_code("P0130", "array capacity overflow");
    for (int64_t i = 0; i < count; i++) {
        GRAY_ARRAY_PUSH(arena, arr, value);
    }
}

/* === Access === */

void *gray_arrays_first_ptr(GrayArray *arr) {
    if (arr->len == 0)
        gray_panic_code("P0045", "arrays.get_first called on an empty array");
    return arr->data;
}

void *gray_arrays_last_ptr(GrayArray *arr) {
    if (arr->len == 0)
        gray_panic_code("P0046", "arrays.get_last called on an empty array");
    return (char *)arr->data + (arr->len - 1) * arr->elem_size;
}

void gray_arrays_remove_first_raw(GrayArray *arr, void *out) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len == 0)
        gray_panic_code("P0047", "arrays.remove_first called on an empty array");
    memcpy(out, arr->data, arr->elem_size);
    gray_arrays_remove_at(arr, 0);
}

void gray_arrays_remove_last_raw(GrayArray *arr, void *out) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len == 0)
        gray_panic_code("P0048", "arrays.remove_last called on an empty array");
    memcpy(out, (char *)arr->data + (arr->len - 1) * arr->elem_size, arr->elem_size);
    arr->len--;
}

/* === Query === */

bool gray_arrays_is_empty(GrayArray *arr) {
    return arr->len == 0;
}

bool gray_arrays_contains(GrayArray *arr, const void *value) {
    return gray_arrays_index_of(arr, value) >= 0;
}

int64_t gray_arrays_count(GrayArray *arr, const void *value) {
    int64_t count = 0;
    for (int32_t i = 0; i < arr->len; i++) {
        if (gray_elem_equal(arr->elem_kind, arr->elem_size, ELEM_AT(arr, i), value)) count++;
    }
    return count;
}

bool gray_arrays_is_equal(GrayArray *left, GrayArray *right) {
    if (left->len != right->len) return false;
    for (int32_t i = 0; i < left->len; i++) {
        if (!gray_elem_equal(left->elem_kind, left->elem_size, ELEM_AT(left, i), ELEM_AT(right, i)))
            return false;
    }
    return true;
}

/* === Transformation === */

/* The result-building helpers below allocate the array at its exact final
 * size, then fill it with a direct strided memcpy instead of a
 * gray_array_push per element (a non-inline call that re-runs the capacity
 * check and blocks the compiler from vectorizing the copy). */

GrayArray gray_arrays_reverse(GrayArena *arena, GrayArray *arr) {
    GrayArray result = gray_array_new(arena, arr->elem_size, arr->len, arr->elem_kind);
    size_t es = (size_t)arr->elem_size;
    const char *src = (const char *)arr->data;
    char *dst = (char *)result.data;
    for (int32_t i = 0; i < arr->len; i++) {
        memcpy(dst + (size_t)i * es, src + (size_t)(arr->len - 1 - i) * es, es);
    }
    result.len = arr->len;
    return result;
}

GrayArray gray_arrays_slice(GrayArena *arena, GrayArray *arr, int64_t start, int64_t end) {
    if (start < 0) start = 0;
    if (end > arr->len) end = arr->len;
    if (start >= end) return gray_array_new(arena, arr->elem_size, 1, arr->elem_kind);
    int32_t count = (int32_t)(end - start);
    return gray_array_from(arena, (char *)arr->data + start * arr->elem_size, arr->elem_size, count, arr->elem_kind);
}

GrayArray gray_arrays_concat(GrayArena *arena, GrayArray *left, GrayArray *right) {
    int32_t total = left->len + right->len;
    size_t es = (size_t)left->elem_size;
    GrayArray result = gray_array_new(arena, left->elem_size, total, left->elem_kind);
    if (left->len > 0)  memcpy(result.data, left->data, (size_t)left->len * es);
    if (right->len > 0) memcpy((char *)result.data + (size_t)left->len * es,
                               right->data, (size_t)right->len * es);
    result.len = total;
    return result;
}

GrayArray gray_arrays_deduplicate(GrayArena *arena, GrayArray *arr) {
    if (arr->len <= 1) return gray_array_copy(arena, arr);

    size_t element_size = (size_t)arr->elem_size;
    char *data = (char *)arr->data;

    /* Hash set (open addressing, power-of-two capacity, ~50% load).
     * Slots store source indices; -1 means empty. */
    uint32_t capacity = 16;
    while (capacity < (uint32_t)arr->len * 2) capacity *= 2;
    int32_t *table = gray_arena_alloc(arena, (size_t)capacity * sizeof(int32_t));
    memset(table, -1, (size_t)capacity * sizeof(int32_t));
    uint32_t mask = capacity - 1;

    GrayArray result = gray_array_new(arena, arr->elem_size, arr->len, arr->elem_kind);

    for (int32_t i = 0; i < arr->len; i++) {
        const char *elem = data + i * element_size;

        uint32_t hash = gray_elem_hash(arr->elem_kind, arr->elem_size, elem);
        uint32_t slot = hash & mask;
        bool found = false;
        while (table[slot] >= 0) {
            if (gray_elem_equal(arr->elem_kind, arr->elem_size,
                                data + (size_t)table[slot] * element_size, elem)) {
                found = true;
                break;
            }
            slot = (slot + 1) & mask;
        }

        if (!found) {
            table[slot] = i;
            GRAY_ARRAY_PUSH(arena, &result, elem);
        }
    }

    return result;
}

GrayArray gray_arrays_flatten(GrayArena *arena, GrayArray *arr) {
    /* Flatten one level: [[T]] -> [T]. Each element of `arr` is a GrayArray,
     * and the result holds elements as wide as the inner arrays' own. */
    size_t out_es = 0;
    int32_t out_kind = GRAY_ELEM_I64;
    int64_t total = 0;
    for (int32_t i = 0; i < arr->len; i++) {
        GrayArray *inner = (GrayArray *)((char *)arr->data + (size_t)i * arr->elem_size);
        total += inner->len;
        if (out_es == 0) { out_es = (size_t)inner->elem_size; out_kind = inner->elem_kind; }
    }
    if (out_es == 0) out_es = sizeof(int64_t);
    GrayArray result = gray_array_new(arena, (int32_t)out_es, (int32_t)total, out_kind);
    char *out = (char *)result.data;
    int32_t pos = 0;
    for (int32_t i = 0; i < arr->len; i++) {
        GrayArray *inner = (GrayArray *)((char *)arr->data + (size_t)i * arr->elem_size);
        if (inner->len <= 0) continue;
        memcpy(out + (size_t)pos * out_es, inner->data, (size_t)inner->len * out_es);
        pos += inner->len;
    }
    result.len = pos;
    return result;
}

GrayArray gray_arrays_split_every(GrayArena *arena, GrayArray *arr, int64_t size) {
    if (size <= 0) size = 1;
    GrayArray result = gray_array_new(arena, sizeof(GrayArray), 4, GRAY_ELEM_ARRAY);
    char *data = (char *)arr->data;
    size_t element_size = (size_t)arr->elem_size;
    for (int64_t i = 0; i < arr->len; i += size) {
        int32_t chunk_len = (size <= arr->len - i) ? (int32_t)size : (int32_t)(arr->len - i);
        GrayArray chunk = gray_array_from(arena, data + (size_t)i * element_size, arr->elem_size, chunk_len, arr->elem_kind);
        GRAY_ARRAY_PUSH(arena, &result, &chunk);
    }
    return result;
}

GrayArray gray_arrays_pair(GrayArena *arena, GrayArray *left, GrayArray *right) {
    int32_t len = left->len < right->len ? left->len : right->len;
    GrayArray result = gray_array_new(arena, sizeof(GrayArray), len, GRAY_ELEM_ARRAY);
    for (int32_t i = 0; i < len; i++) {
        GrayArray pair_arr = gray_array_new(arena, left->elem_size, 2, left->elem_kind);
        GRAY_ARRAY_PUSH(arena, &pair_arr, (char *)left->data + i * left->elem_size);
        GRAY_ARRAY_PUSH(arena, &pair_arr, (char *)right->data + i * right->elem_size);
        GRAY_ARRAY_PUSH(arena, &result, &pair_arr);
    }
    return result;
}

GrayArray gray_arrays_rotate(GrayArena *arena, GrayArray *arr, int64_t n) {
    int32_t len = arr->len;
    if (len == 0) return gray_array_new(arena, arr->elem_size, 1, arr->elem_kind);
    size_t es = (size_t)arr->elem_size;
    int32_t shift = (int32_t)(((n % len) + len) % len);
    GrayArray result = gray_array_new(arena, arr->elem_size, len, arr->elem_kind);
    const char *src = (const char *)arr->data;
    char *dst = (char *)result.data;
    for (int32_t i = 0; i < len; i++) {
        memcpy(dst + (size_t)i * es, src + (size_t)((i + shift) % len) * es, es);
    }
    result.len = len;
    return result;
}

/* === Computation === */

/* The sum of the elements in their own type, overflowing the way `+` on that
 * type does; zero for an empty array. Written to *out. */
void gray_arrays_get_sum(GrayArray *arr, void *out, const char *file, int line) {
    memset(out, 0, (size_t)arr->elem_size);
    for (int32_t i = 0; i < arr->len; i++) gray_elem_add(arr->elem_kind, out, ELEM_AT(arr, i), file, line);
}

double gray_arrays_average(GrayArray *arr, const char *file, int line) {
    if (arr->len == 0) gray_panic_code_at(file, line, "P0121", "arrays.average called on an empty array");
    double sum = 0.0;
    for (int32_t i = 0; i < arr->len; i++) sum += gray_elem_to_double(arr->elem_kind, ELEM_AT(arr, i));
    return sum / (double)arr->len;
}

/* Index of the smallest (want_max false) or largest element; -1 if empty. */
static int64_t extreme_index(GrayArray *arr, bool want_max) {
    if (arr->len == 0) return -1;
    int64_t best = 0;
    for (int32_t i = 1; i < arr->len; i++) {
        int order = gray_elem_compare(arr->elem_kind, arr->elem_size, ELEM_AT(arr, i), ELEM_AT(arr, best));
        if (want_max ? order > 0 : order < 0) best = i;
    }
    return best;
}

int64_t gray_arrays_min_index(GrayArray *arr) { return extreme_index(arr, false); }
int64_t gray_arrays_max_index(GrayArray *arr) { return extreme_index(arr, true); }

/* The smallest / largest element, written to *out; zero for an empty array. */
void gray_arrays_get_min(GrayArray *arr, void *out) {
    int64_t index = extreme_index(arr, false);
    if (index < 0) memset(out, 0, (size_t)arr->elem_size);
    else memcpy(out, ELEM_AT(arr, index), (size_t)arr->elem_size);
}

void gray_arrays_get_max(GrayArray *arr, void *out) {
    int64_t index = extreme_index(arr, true);
    if (index < 0) memset(out, 0, (size_t)arr->elem_size);
    else memcpy(out, ELEM_AT(arr, index), (size_t)arr->elem_size);
}

/* === Sort ===
 *
 * Type-specialized introsort (quicksort + median-of-3 pivot, insertion-sort
 * cutoff, heapsort fallback once recursion passes 2*log2(n)), one per element
 * kind, picked by elem_kind. libc qsort ran an indirect call through a
 * comparator function pointer for every comparison; here it is one inlined
 * expression, and the depth limit gives a hard O(n log n) bound. Descending
 * sorts ascending then reverses — equal elements are indistinguishable, so
 * the flipped order of an equal run is unobservable. Wide integers, whose
 * compare is a function, go through qsort. */

#define GRAY_SORT_INSERTION_CUTOFF 24

static inline bool gray_sort_str_lt(GrayString a, GrayString b) {
    int32_t min_len = a.len < b.len ? a.len : b.len;
    int cmp = memcmp(a.data, b.data, (size_t)min_len);
    if (cmp != 0) return cmp < 0;
    return a.len < b.len;
}

/* LESS(x, y) is a strict-weak-ordering expression yielding x < y. */
#define GRAY_DEFINE_INTROSORT(SUF, T, LESS)                                     \
static void gray_sort_ins_##SUF(T *v, int64_t lo, int64_t hi) {                \
    for (int64_t i = lo + 1; i <= hi; i++) {                                   \
        T x = v[i];                                                            \
        int64_t j = i - 1;                                                     \
        while (j >= lo && LESS(x, v[j])) { v[j + 1] = v[j]; j--; }             \
        v[j + 1] = x;                                                          \
    }                                                                         \
}                                                                             \
static void gray_sort_sift_##SUF(T *v, int64_t lo, int64_t n, int64_t i) {     \
    for (;;) {                                                                 \
        int64_t c = 2 * i + 1;                                                 \
        if (c >= n) break;                                                     \
        if (c + 1 < n && LESS(v[lo + c], v[lo + c + 1])) c++;                  \
        if (!LESS(v[lo + i], v[lo + c])) break;                               \
        T t = v[lo + i]; v[lo + i] = v[lo + c]; v[lo + c] = t;                \
        i = c;                                                                 \
    }                                                                         \
}                                                                             \
static void gray_sort_heap_##SUF(T *v, int64_t lo, int64_t hi) {              \
    int64_t n = hi - lo + 1;                                                   \
    for (int64_t i = n / 2 - 1; i >= 0; i--) gray_sort_sift_##SUF(v, lo, n, i);\
    for (int64_t end = n - 1; end > 0; end--) {                                \
        T t = v[lo]; v[lo] = v[lo + end]; v[lo + end] = t;                    \
        gray_sort_sift_##SUF(v, lo, end, 0);                                   \
    }                                                                         \
}                                                                             \
static void gray_sort_intro_##SUF(T *v, int64_t lo, int64_t hi, int depth) {   \
    while (hi - lo > GRAY_SORT_INSERTION_CUTOFF) {                             \
        if (depth-- == 0) { gray_sort_heap_##SUF(v, lo, hi); return; }         \
        int64_t mid = lo + ((hi - lo) >> 1);                                   \
        if (LESS(v[mid], v[lo]))  { T t = v[mid]; v[mid] = v[lo];  v[lo]  = t; }\
        if (LESS(v[hi],  v[lo]))  { T t = v[hi];  v[hi]  = v[lo];  v[lo]  = t; }\
        if (LESS(v[hi],  v[mid])) { T t = v[hi];  v[hi]  = v[mid]; v[mid] = t; }\
        T pivot = v[mid];                                                      \
        int64_t i = lo, j = hi;                                                \
        for (;;) {                                                             \
            while (LESS(v[i], pivot)) i++;                                     \
            while (LESS(pivot, v[j])) j--;                                     \
            if (i >= j) break;                                                 \
            T t = v[i]; v[i] = v[j]; v[j] = t;                                \
            i++; j--;                                                          \
        }                                                                     \
        if (j - lo < hi - (j + 1)) {                                           \
            gray_sort_intro_##SUF(v, lo, j, depth);                            \
            lo = j + 1;                                                        \
        } else {                                                              \
            gray_sort_intro_##SUF(v, j + 1, hi, depth);                       \
            hi = j;                                                            \
        }                                                                     \
    }                                                                         \
    gray_sort_ins_##SUF(v, lo, hi);                                            \
}                                                                             \
static void gray_sort_##SUF(void *data, int64_t n) {                          \
    int depth = 0;                                                            \
    for (int64_t t = n; t > 1; t >>= 1) depth += 2;                           \
    gray_sort_intro_##SUF((T *)data, 0, n - 1, depth);                        \
}

#define GRAY_SORT_LT(a, b) ((a) < (b))

GRAY_DEFINE_INTROSORT(i8, int8_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(i16, int16_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(i32, int32_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(i64, int64_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(u8, uint8_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(u16, uint16_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(u32, uint32_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(u64, uint64_t, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(f32, float, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(f64, double, GRAY_SORT_LT)
GRAY_DEFINE_INTROSORT(str, GrayString, gray_sort_str_lt)

#define GRAY_DEFINE_WIDE_COMPARE(KIND, T)                                       \
static int compare_##T(const void *l, const void *r) {                         \
    return gray_elem_compare(KIND, (int32_t)sizeof(T), l, r);                  \
}
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_I128, gray_i128)
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_U128, gray_u128)
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_I256, gray_i256)
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_U256, gray_u256)

void gray_arrays_sort(GrayArray *arr, bool descending) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    switch (arr->elem_kind) {
    case GRAY_ELEM_I8:   gray_sort_i8(arr->data, arr->len); break;
    case GRAY_ELEM_I16:  gray_sort_i16(arr->data, arr->len); break;
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: gray_sort_i32(arr->data, arr->len); break;
    case GRAY_ELEM_I64:  gray_sort_i64(arr->data, arr->len); break;
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: gray_sort_u8(arr->data, arr->len); break;
    case GRAY_ELEM_U16:  gray_sort_u16(arr->data, arr->len); break;
    case GRAY_ELEM_U32:  gray_sort_u32(arr->data, arr->len); break;
    case GRAY_ELEM_U64:  gray_sort_u64(arr->data, arr->len); break;
    case GRAY_ELEM_F32:  gray_sort_f32(arr->data, arr->len); break;
    case GRAY_ELEM_F64:  gray_sort_f64(arr->data, arr->len); break;
    case GRAY_ELEM_STRING: gray_sort_str(arr->data, arr->len); break;
    case GRAY_ELEM_I128: qsort(arr->data, (size_t)arr->len, sizeof(gray_i128), compare_gray_i128); break;
    case GRAY_ELEM_U128: qsort(arr->data, (size_t)arr->len, sizeof(gray_u128), compare_gray_u128); break;
    case GRAY_ELEM_I256: qsort(arr->data, (size_t)arr->len, sizeof(gray_i256), compare_gray_i256); break;
    case GRAY_ELEM_U256: qsort(arr->data, (size_t)arr->len, sizeof(gray_u256), compare_gray_u256); break;
    default: return;
    }
    if (descending) {
        /* Every sortable kind is at most 32 bytes wide (i256/u256). */
        size_t element_size = (size_t)arr->elem_size;
        char scratch[32];
        for (int64_t a = 0, b = arr->len - 1; a < b; a++, b--) {
            memcpy(scratch, ELEM_AT(arr, a), element_size);
            memcpy(ELEM_AT(arr, a), ELEM_AT(arr, b), element_size);
            memcpy(ELEM_AT(arr, b), scratch, element_size);
        }
    }
}

/* Empty and single-element arrays are sorted by definition. */
bool gray_arrays_is_sorted(GrayArray *arr) {
    for (int32_t i = 1; i < arr->len; i++) {
        if (gray_elem_compare(arr->elem_kind, arr->elem_size, ELEM_AT(arr, i - 1), ELEM_AT(arr, i)) > 0)
            return false;
    }
    return true;
}

/* Binary search assumes arr is already sorted ascending (as by sort);
 * behavior on an unsorted array is undefined. */
int64_t gray_arrays_binary_search(GrayArray *arr, const void *value) {
    int64_t lo = 0, hi = (int64_t)arr->len - 1;
    while (lo <= hi) {
        int64_t mid = lo + (hi - lo) / 2;
        int order = gray_elem_compare(arr->elem_kind, arr->elem_size, ELEM_AT(arr, mid), value);
        if (order < 0) lo = mid + 1;
        else if (order > 0) hi = mid - 1;
        else return mid;
    }
    return -1;
}

/* In-place swap of the elem_size bytes at i and j. */
void gray_arrays_swap(GrayArray *arr, int64_t i, int64_t j) {
    ARRAY_CHECK_ITER(arr);
    if (i < 0 || i >= arr->len || j < 0 || j >= arr->len) {
        gray_panic_code("P0120",
            "arrays.swap: index out of bounds for an array of length %d", arr->len);
    }
    if (i == j) return;
    size_t element_size = (size_t)arr->elem_size;
    char *elem_i = (char *)arr->data + (size_t)i * element_size;
    char *elem_j = (char *)arr->data + (size_t)j * element_size;
    for (size_t k = 0; k < element_size; k++) {
        char temp = elem_i[k];
        elem_i[k] = elem_j[k];
        elem_j[k] = temp;
    }
}
