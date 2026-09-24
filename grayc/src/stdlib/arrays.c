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

void gray_arrays_append(GrayArena *arena, GrayArray *array, const void *value) {
    GRAY_ARRAY_PUSH(arena, array, value);
}

void gray_arrays_insert_at(GrayArena *arena, GrayArray *array, int64_t index, const void *value) {
    ARRAY_CHECK_ITER(array);
    if (index < 0 || index > array->len) {
        gray_panic_code("P0043",
            "arrays.insert_at: index %lld is out of bounds for an array of length %d",
            (long long)index, array->len);
    }

    /* NULL/0 keeps the P0130 panic locationless, as it has always been here. */
    gray_array_grow(arena, array, NULL, 0);

    size_t element_size = (size_t)array->elem_size;
    char *data = (char *)array->data;
    if (index < array->len) {
        memmove(data + (index + 1) * element_size, data + index * element_size,
                (size_t)(array->len - index) * element_size);
    }
    memcpy(data + index * element_size, value, element_size);
    array->len++;
}

void gray_arrays_prepend(GrayArena *arena, GrayArray *array, const void *value) {
    gray_arrays_insert_at(arena, array, 0, value);
}

void gray_arrays_remove_at(GrayArray *array, int64_t index) {
    ARRAY_CHECK_ITER(array);
    if (index < 0 || index >= array->len)
        gray_panic_code("P0044",
            "arrays.remove_at: index %lld is out of bounds for an array of length %d",
            (long long)index, array->len);
    char *data = (char *)array->data;
    size_t element_size = (size_t)array->elem_size;
    memmove(data + index * element_size, data + (index + 1) * element_size, (array->len - 1 - index) * element_size);
    array->len--;
}

/* Every function below that looks at an element's value does so through the
 * gray_elem_* helpers, by the array's elem_kind. A value argument points at a
 * value of the element type. */

#define ELEMENT_AT(arr, i) ((char *)(arr)->data + (size_t)(i) * (size_t)(arr)->elem_size)

int64_t gray_arrays_index_of(GrayArray *array, const void *value) {
    for (int32_t i = 0; i < array->len; i++) {
        if (gray_elem_equal(array->elem_kind, array->elem_size, ELEMENT_AT(array, i), value)) return i;
    }
    return -1;
}

void gray_arrays_remove(GrayArray *array, const void *value) {
    int64_t index = gray_arrays_index_of(array, value);
    if (index >= 0) gray_arrays_remove_at(array, index);
}

void gray_arrays_clear(GrayArray *array) {
    ARRAY_CHECK_ITER(array);
    array->len = 0;
}

void gray_arrays_fill(GrayArena *arena, GrayArray *array, const void *value, int64_t count) {
    gray_arrays_clear(array);
    if (count > INT32_MAX)
        gray_panic_code("P0130", "array capacity overflow");
    for (int64_t i = 0; i < count; i++) {
        GRAY_ARRAY_PUSH(arena, array, value);
    }
}

/* === Access === */

void *gray_arrays_first_ptr(GrayArray *array) {
    if (array->len == 0)
        gray_panic_code("P0045", "arrays.get_first called on an empty array");
    return array->data;
}

void *gray_arrays_last_ptr(GrayArray *array) {
    if (array->len == 0)
        gray_panic_code("P0046", "arrays.get_last called on an empty array");
    return (char *)array->data + (array->len - 1) * array->elem_size;
}

void gray_arrays_remove_first_raw(GrayArray *array, void *output) {
    ARRAY_CHECK_ITER(array);
    if (array->len == 0)
        gray_panic_code("P0047", "arrays.remove_first called on an empty array");
    memcpy(output, array->data, array->elem_size);
    gray_arrays_remove_at(array, 0);
}

void gray_arrays_remove_last_raw(GrayArray *array, void *output) {
    ARRAY_CHECK_ITER(array);
    if (array->len == 0)
        gray_panic_code("P0048", "arrays.remove_last called on an empty array");
    memcpy(output, (char *)array->data + (array->len - 1) * array->elem_size, array->elem_size);
    array->len--;
}

/* === Query === */

bool gray_arrays_is_empty(GrayArray *array) {
    return array->len == 0;
}

bool gray_arrays_contains(GrayArray *array, const void *value) {
    return gray_arrays_index_of(array, value) >= 0;
}

int64_t gray_arrays_count(GrayArray *array, const void *value) {
    int64_t count = 0;
    for (int32_t i = 0; i < array->len; i++) {
        if (gray_elem_equal(array->elem_kind, array->elem_size, ELEMENT_AT(array, i), value)) count++;
    }
    return count;
}

bool gray_arrays_is_equal(GrayArray *left, GrayArray *right) {
    if (left->len != right->len) return false;
    for (int32_t i = 0; i < left->len; i++) {
        if (!gray_elem_equal(left->elem_kind, left->elem_size, ELEMENT_AT(left, i), ELEMENT_AT(right, i)))
            return false;
    }
    return true;
}

/* === Transformation === */

/* The result-building helpers below allocate the array at its exact final
 * size, then fill it with a direct strided memcpy instead of a
 * gray_array_push per element (a non-inline call that re-runs the capacity
 * check and blocks the compiler from vectorizing the copy). */

GrayArray gray_arrays_reverse(GrayArena *arena, GrayArray *array) {
    GrayArray result = gray_array_new(arena, array->elem_size, array->len, array->elem_kind);
    size_t element_size = (size_t)array->elem_size;
    const char *source = (const char *)array->data;
    char *destination = (char *)result.data;
    for (int32_t i = 0; i < array->len; i++) {
        memcpy(destination + (size_t)i * element_size, source + (size_t)(array->len - 1 - i) * element_size, element_size);
    }
    result.len = array->len;
    return result;
}

GrayArray gray_arrays_slice(GrayArena *arena, GrayArray *array, int64_t start, int64_t end_index) {
    if (start < 0) start = 0;
    if (end_index > array->len) end_index = array->len;
    if (start >= end_index) return gray_array_new(arena, array->elem_size, 1, array->elem_kind);
    int32_t count = (int32_t)(end_index - start);
    return gray_array_from(arena, (char *)array->data + start * array->elem_size, array->elem_size, count, array->elem_kind);
}

GrayArray gray_arrays_concat(GrayArena *arena, GrayArray *left, GrayArray *right) {
    int32_t total = left->len + right->len;
    size_t element_size = (size_t)left->elem_size;
    GrayArray result = gray_array_new(arena, left->elem_size, total, left->elem_kind);
    if (left->len > 0)  memcpy(result.data, left->data, (size_t)left->len * element_size);
    if (right->len > 0) memcpy((char *)result.data + (size_t)left->len * element_size,
                               right->data, (size_t)right->len * element_size);
    result.len = total;
    return result;
}

GrayArray gray_arrays_deduplicate(GrayArena *arena, GrayArray *array) {
    if (array->len <= 1) return gray_array_copy(arena, array);

    size_t element_size = (size_t)array->elem_size;
    char *data = (char *)array->data;

    /* Hash set (open addressing, power-of-two capacity, ~50% load).
     * Slots store source indices; -1 means empty. */
    uint32_t capacity = 16;
    while (capacity < (uint32_t)array->len * 2) capacity *= 2;
    int32_t *table = gray_arena_alloc(arena, (size_t)capacity * sizeof(int32_t));
    memset(table, -1, (size_t)capacity * sizeof(int32_t));
    uint32_t mask = capacity - 1;

    GrayArray result = gray_array_new(arena, array->elem_size, array->len, array->elem_kind);

    for (int32_t i = 0; i < array->len; i++) {
        const char *element = data + i * element_size;

        uint32_t hash = gray_elem_hash(array->elem_kind, array->elem_size, element);
        uint32_t slot = hash & mask;
        bool found = false;
        while (table[slot] >= 0) {
            if (gray_elem_equal(array->elem_kind, array->elem_size,
                                data + (size_t)table[slot] * element_size, element)) {
                found = true;
                break;
            }
            slot = (slot + 1) & mask;
        }

        if (!found) {
            table[slot] = i;
            GRAY_ARRAY_PUSH(arena, &result, element);
        }
    }

    return result;
}

GrayArray gray_arrays_flatten(GrayArena *arena, GrayArray *array) {
    /* Flatten one level: [[T]] -> [T]. Each element of `array` is a GrayArray,
     * and the result holds elements as wide as the inner arrays' own. */
    size_t out_es = 0;
    int32_t out_kind = GRAY_ELEM_I64;
    int64_t total = 0;
    for (int32_t i = 0; i < array->len; i++) {
        GrayArray *inner = (GrayArray *)((char *)array->data + (size_t)i * array->elem_size);
        total += inner->len;
        if (out_es == 0) { out_es = (size_t)inner->elem_size; out_kind = inner->elem_kind; }
    }
    if (out_es == 0) out_es = sizeof(int64_t);
    GrayArray result = gray_array_new(arena, (int32_t)out_es, (int32_t)total, out_kind);
    char *output = (char *)result.data;
    int32_t position = 0;
    for (int32_t i = 0; i < array->len; i++) {
        GrayArray *inner = (GrayArray *)((char *)array->data + (size_t)i * array->elem_size);
        if (inner->len <= 0) continue;
        memcpy(output + (size_t)position * out_es, inner->data, (size_t)inner->len * out_es);
        position += inner->len;
    }
    result.len = position;
    return result;
}

GrayArray gray_arrays_split_every(GrayArena *arena, GrayArray *array, int64_t size) {
    if (size <= 0) size = 1;
    GrayArray result = gray_array_new(arena, sizeof(GrayArray), 4, GRAY_ELEM_ARRAY);
    char *data = (char *)array->data;
    size_t element_size = (size_t)array->elem_size;
    for (int64_t i = 0; i < array->len; i += size) {
        int32_t chunk_length = (size <= array->len - i) ? (int32_t)size : (int32_t)(array->len - i);
        GrayArray chunk = gray_array_from(arena, data + (size_t)i * element_size, array->elem_size, chunk_length, array->elem_kind);
        GRAY_ARRAY_PUSH(arena, &result, &chunk);
    }
    return result;
}

GrayArray gray_arrays_pair(GrayArena *arena, GrayArray *left, GrayArray *right) {
    int32_t length = left->len < right->len ? left->len : right->len;
    GrayArray result = gray_array_new(arena, sizeof(GrayArray), length, GRAY_ELEM_ARRAY);
    for (int32_t i = 0; i < length; i++) {
        GrayArray pair_arr = gray_array_new(arena, left->elem_size, 2, left->elem_kind);
        GRAY_ARRAY_PUSH(arena, &pair_arr, (char *)left->data + i * left->elem_size);
        GRAY_ARRAY_PUSH(arena, &pair_arr, (char *)right->data + i * right->elem_size);
        GRAY_ARRAY_PUSH(arena, &result, &pair_arr);
    }
    return result;
}

GrayArray gray_arrays_rotate(GrayArena *arena, GrayArray *array, int64_t shift_count) {
    int32_t length = array->len;
    if (length == 0) return gray_array_new(arena, array->elem_size, 1, array->elem_kind);
    size_t element_size = (size_t)array->elem_size;
    int32_t shift = (int32_t)(((shift_count % length) + length) % length);
    GrayArray result = gray_array_new(arena, array->elem_size, length, array->elem_kind);
    const char *source = (const char *)array->data;
    char *destination = (char *)result.data;
    for (int32_t i = 0; i < length; i++) {
        memcpy(destination + (size_t)i * element_size, source + (size_t)((i + shift) % length) * element_size, element_size);
    }
    result.len = length;
    return result;
}

/* === Computation === */

/* The sum of the elements in their own type, overflowing the way `+` on that
 * type does; zero for an empty array. Written to *out. */
void gray_arrays_get_sum(GrayArray *array, void *output, const char *file, int line) {
    memset(output, 0, (size_t)array->elem_size);
    for (int32_t i = 0; i < array->len; i++) gray_elem_add(array->elem_kind, output, ELEMENT_AT(array, i), file, line);
}

double gray_arrays_average(GrayArray *array, const char *file, int line) {
    if (array->len == 0) gray_panic_code_at(file, line, "P0121", "arrays.average called on an empty array");
    double total = 0.0;
    for (int32_t i = 0; i < array->len; i++) total += gray_elem_to_double(array->elem_kind, ELEMENT_AT(array, i));
    return total / (double)array->len;
}

/* Index of the smallest (want_max false) or largest element; -1 if empty. */
static int64_t extreme_index(GrayArray *array, bool wants_maximum) {
    if (array->len == 0) return -1;
    int64_t best = 0;
    for (int32_t i = 1; i < array->len; i++) {
        int order = gray_elem_compare(array->elem_kind, array->elem_size, ELEMENT_AT(array, i), ELEMENT_AT(array, best));
        if (wants_maximum ? order > 0 : order < 0) best = i;
    }
    return best;
}

int64_t gray_arrays_min_index(GrayArray *array) { return extreme_index(array, false); }
int64_t gray_arrays_max_index(GrayArray *array) { return extreme_index(array, true); }

/* The smallest / largest element, written to *out; zero for an empty array. */
void gray_arrays_get_min(GrayArray *array, void *output) {
    int64_t index = extreme_index(array, false);
    if (index < 0) memset(output, 0, (size_t)array->elem_size);
    else memcpy(output, ELEMENT_AT(array, index), (size_t)array->elem_size);
}

void gray_arrays_get_max(GrayArray *array, void *output) {
    int64_t index = extreme_index(array, true);
    if (index < 0) memset(output, 0, (size_t)array->elem_size);
    else memcpy(output, ELEMENT_AT(array, index), (size_t)array->elem_size);
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

static inline bool gray_sort_string_less_than(GrayString left, GrayString right) {
    int32_t minimum_length = left.len < right.len ? left.len : right.len;
    int comparison = memcmp(left.data, right.data, (size_t)minimum_length);
    if (comparison != 0) return comparison < 0;
    return left.len < right.len;
}

/* LESS(left, right) is a strict-weak-ordering expression yielding left < right. */
#define GRAY_DEFINE_INTROSORT(SUFFIX, ELEMENT_TYPE, LESS)                                     \
static void gray_sort_insertion_##SUFFIX(ELEMENT_TYPE *values, int64_t low, int64_t high) {                \
    for (int64_t i = low + 1; i <= high; i++) {                                   \
        ELEMENT_TYPE current = values[i];                                                            \
        int64_t j = i - 1;                                                     \
        while (j >= low && LESS(current, values[j])) { values[j + 1] = values[j]; j--; }             \
        values[j + 1] = current;                                                          \
    }                                                                         \
}                                                                             \
static void gray_sort_sift_##SUFFIX(ELEMENT_TYPE *values, int64_t low, int64_t count, int64_t i) {     \
    for (;;) {                                                                 \
        int64_t child = 2 * i + 1;                                                 \
        if (child >= count) break;                                                     \
        if (child + 1 < count && LESS(values[low + child], values[low + child + 1])) child++;                  \
        if (!LESS(values[low + i], values[low + child])) break;                               \
        ELEMENT_TYPE swap = values[low + i]; values[low + i] = values[low + child]; values[low + child] = swap;                \
        i = child;                                                                 \
    }                                                                         \
}                                                                             \
static void gray_sort_heap_##SUFFIX(ELEMENT_TYPE *values, int64_t low, int64_t high) {              \
    int64_t count = high - low + 1;                                                   \
    for (int64_t i = count / 2 - 1; i >= 0; i--) gray_sort_sift_##SUFFIX(values, low, count, i);\
    for (int64_t heap_end = count - 1; heap_end > 0; heap_end--) {                                \
        ELEMENT_TYPE swap = values[low]; values[low] = values[low + heap_end]; values[low + heap_end] = swap;                    \
        gray_sort_sift_##SUFFIX(values, low, heap_end, 0);                                   \
    }                                                                         \
}                                                                             \
static void gray_sort_introsort_##SUFFIX(ELEMENT_TYPE *values, int64_t low, int64_t high, int depth) {   \
    while (high - low > GRAY_SORT_INSERTION_CUTOFF) {                             \
        if (depth-- == 0) { gray_sort_heap_##SUFFIX(values, low, high); return; }         \
        int64_t middle = low + ((high - low) >> 1);                                   \
        if (LESS(values[middle], values[low]))  { ELEMENT_TYPE swap = values[middle]; values[middle] = values[low];  values[low]  = swap; }\
        if (LESS(values[high],  values[low]))  { ELEMENT_TYPE swap = values[high];  values[high]  = values[low];  values[low]  = swap; }\
        if (LESS(values[high],  values[middle])) { ELEMENT_TYPE swap = values[high];  values[high]  = values[middle]; values[middle] = swap; }\
        ELEMENT_TYPE pivot = values[middle];                                                      \
        int64_t i = low, j = high;                                                \
        for (;;) {                                                             \
            while (LESS(values[i], pivot)) i++;                                     \
            while (LESS(pivot, values[j])) j--;                                     \
            if (i >= j) break;                                                 \
            ELEMENT_TYPE swap = values[i]; values[i] = values[j]; values[j] = swap;                                \
            i++; j--;                                                          \
        }                                                                     \
        if (j - low < high - (j + 1)) {                                           \
            gray_sort_introsort_##SUFFIX(values, low, j, depth);                            \
            low = j + 1;                                                        \
        } else {                                                              \
            gray_sort_introsort_##SUFFIX(values, j + 1, high, depth);                       \
            high = j;                                                            \
        }                                                                     \
    }                                                                         \
    gray_sort_insertion_##SUFFIX(values, low, high);                                            \
}                                                                             \
static void gray_sort_##SUFFIX(void *data, int64_t count) {                          \
    int depth = 0;                                                            \
    for (int64_t remaining = count; remaining > 1; remaining >>= 1) depth += 2;                           \
    gray_sort_introsort_##SUFFIX((ELEMENT_TYPE *)data, 0, count - 1, depth);                        \
}

#define GRAY_SORT_LESS_THAN(left, right) ((left) < (right))

GRAY_DEFINE_INTROSORT(i8, int8_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(i16, int16_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(i32, int32_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(i64, int64_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(u8, uint8_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(u16, uint16_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(u32, uint32_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(u64, uint64_t, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(f32, float, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(f64, double, GRAY_SORT_LESS_THAN)
GRAY_DEFINE_INTROSORT(str, GrayString, gray_sort_string_less_than)

#define GRAY_DEFINE_WIDE_COMPARE(KIND, WIDE_TYPE)                                       \
static int compare_##WIDE_TYPE(const void *left, const void *right) {                         \
    return gray_elem_compare(KIND, (int32_t)sizeof(WIDE_TYPE), left, right);                  \
}
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_I128, gray_i128)
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_U128, gray_u128)
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_I256, gray_i256)
GRAY_DEFINE_WIDE_COMPARE(GRAY_ELEM_U256, gray_u256)

void gray_arrays_sort(GrayArray *array, bool descending) {
    ARRAY_CHECK_ITER(array);
    if (array->len <= 1) return;
    switch (array->elem_kind) {
    case GRAY_ELEM_I8:   gray_sort_i8(array->data, array->len); break;
    case GRAY_ELEM_I16:  gray_sort_i16(array->data, array->len); break;
    case GRAY_ELEM_I32:
    case GRAY_ELEM_CHAR: gray_sort_i32(array->data, array->len); break;
    case GRAY_ELEM_I64:  gray_sort_i64(array->data, array->len); break;
    case GRAY_ELEM_U8:
    case GRAY_ELEM_BOOL: gray_sort_u8(array->data, array->len); break;
    case GRAY_ELEM_U16:  gray_sort_u16(array->data, array->len); break;
    case GRAY_ELEM_U32:  gray_sort_u32(array->data, array->len); break;
    case GRAY_ELEM_U64:  gray_sort_u64(array->data, array->len); break;
    case GRAY_ELEM_F32:  gray_sort_f32(array->data, array->len); break;
    case GRAY_ELEM_F64:  gray_sort_f64(array->data, array->len); break;
    case GRAY_ELEM_STRING: gray_sort_str(array->data, array->len); break;
    case GRAY_ELEM_I128: qsort(array->data, (size_t)array->len, sizeof(gray_i128), compare_gray_i128); break;
    case GRAY_ELEM_U128: qsort(array->data, (size_t)array->len, sizeof(gray_u128), compare_gray_u128); break;
    case GRAY_ELEM_I256: qsort(array->data, (size_t)array->len, sizeof(gray_i256), compare_gray_i256); break;
    case GRAY_ELEM_U256: qsort(array->data, (size_t)array->len, sizeof(gray_u256), compare_gray_u256); break;
    default: return;
    }
    if (descending) {
        /* Every sortable kind is at most 32 bytes wide (i256/u256). */
        size_t element_size = (size_t)array->elem_size;
        char scratch[32];
        for (int64_t front_index = 0, back_index = array->len - 1; front_index < back_index; front_index++, back_index--) {
            memcpy(scratch, ELEMENT_AT(array, front_index), element_size);
            memcpy(ELEMENT_AT(array, front_index), ELEMENT_AT(array, back_index), element_size);
            memcpy(ELEMENT_AT(array, back_index), scratch, element_size);
        }
    }
}

/* Empty and single-element arrays are sorted by definition. */
bool gray_arrays_is_sorted(GrayArray *array) {
    for (int32_t i = 1; i < array->len; i++) {
        if (gray_elem_compare(array->elem_kind, array->elem_size, ELEMENT_AT(array, i - 1), ELEMENT_AT(array, i)) > 0)
            return false;
    }
    return true;
}

/* Binary search assumes arr is already sorted ascending (as by sort);
 * behavior on an unsorted array is undefined. */
int64_t gray_arrays_binary_search(GrayArray *array, const void *value) {
    int64_t low = 0, hi = (int64_t)array->len - 1;
    while (low <= hi) {
        int64_t middle = low + (hi - low) / 2;
        int order = gray_elem_compare(array->elem_kind, array->elem_size, ELEMENT_AT(array, middle), value);
        if (order < 0) low = middle + 1;
        else if (order > 0) hi = middle - 1;
        else return middle;
    }
    return -1;
}

/* In-place swap of the elem_size bytes at i and j. */
void gray_arrays_swap(GrayArray *array, int64_t i, int64_t j) {
    ARRAY_CHECK_ITER(array);
    if (i < 0 || i >= array->len || j < 0 || j >= array->len) {
        gray_panic_code("P0120",
            "arrays.swap: index out of bounds for an array of length %d", array->len);
    }
    if (i == j) return;
    size_t element_size = (size_t)array->elem_size;
    char *element_i = (char *)array->data + (size_t)i * element_size;
    char *element_j = (char *)array->data + (size_t)j * element_size;
    for (size_t byte_index = 0; byte_index < element_size; byte_index++) {
        char temporary = element_i[byte_index];
        element_i[byte_index] = element_j[byte_index];
        element_j[byte_index] = temporary;
    }
}
