/*
 * arrays.c — Implementation of the arrays stdlib module.
 * Provides append, insert, remove, sort, reverse, slice, and search
 * operations on GrayArray values.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "arrays.h"
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

void gray_arrays_insert_at(GrayArena *arena, GrayArray *arr, int32_t index, const void *value) {
    ARRAY_CHECK_ITER(arr);
    if (index < 0 || index > arr->len) {
        gray_panic_code("P0043",
            "arrays.insert_at: index %d is out of bounds for an array of length %d",
            index, arr->len);
    }

    /* Grow if needed — same policy as gray_array_push, including growing
     * into the array's owning arena rather than the ambient one */
    if (arr->arena) arena = arr->arena;
    if (arr->len >= arr->cap) {
        int32_t new_cap;
        if (arr->cap < GRAY_ARRAY_MIN_CAP) {
            new_cap = GRAY_ARRAY_MIN_CAP;
        } else if (arr->cap > INT32_MAX / 2) {
            gray_panic_code("P0035", "array capacity overflow");
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

void gray_arrays_remove_at(GrayArray *arr, int32_t index) {
    ARRAY_CHECK_ITER(arr);
    if (index < 0 || index >= arr->len)
        gray_panic_code("P0044",
            "arrays.remove_at: index %d is out of bounds for an array of length %d",
            index, arr->len);
    char *data = (char *)arr->data;
    size_t element_size = (size_t)arr->elem_size;
    memmove(data + index * element_size, data + (index + 1) * element_size, (arr->len - 1 - index) * element_size);
    arr->len--;
}

void gray_arrays_remove_int(GrayArray *arr, int64_t value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(int64_t *)((char *)arr->data + i * arr->elem_size) == value) {
            gray_arrays_remove_at(arr, i);
            return;
        }
    }
}

void gray_arrays_remove_float(GrayArray *arr, double value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(double *)((char *)arr->data + i * arr->elem_size) == value) {
            gray_arrays_remove_at(arr, i);
            return;
        }
    }
}

void gray_arrays_remove_str(GrayArray *arr, GrayString value) {
    for (int32_t i = 0; i < arr->len; i++) {
        GrayString *s = (GrayString *)((char *)arr->data + i * arr->elem_size);
        if (s->len == value.len && memcmp(s->data, value.data, s->len) == 0) {
            gray_arrays_remove_at(arr, i);
            return;
        }
    }
}

void gray_arrays_clear(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    arr->len = 0;
}

void gray_arrays_fill(GrayArena *arena, GrayArray *arr, const void *value, int32_t count) {
    gray_arrays_clear(arr);
    for (int32_t i = 0; i < count; i++) {
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

int64_t gray_arrays_get_first(GrayArray *arr) {
    if (arr->len == 0) gray_panic_code("P0045", "arrays.get_first called on an empty array");
    return *(int64_t *)arr->data;
}

int64_t gray_arrays_get_last(GrayArray *arr) {
    if (arr->len == 0) gray_panic_code("P0046", "arrays.get_last called on an empty array");
    return *(int64_t *)((char *)arr->data + (arr->len - 1) * arr->elem_size);
}

int64_t gray_arrays_remove_last(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len == 0) gray_panic_code("P0048", "arrays.remove_last called on an empty array");
    int64_t val = *(int64_t *)((char *)arr->data + (arr->len - 1) * arr->elem_size);
    arr->len--;
    return val;
}

int64_t gray_arrays_remove_first(GrayArray *arr) {
    if (arr->len == 0) gray_panic_code("P0047", "arrays.remove_first called on an empty array");
    int64_t val = *(int64_t *)arr->data;
    gray_arrays_remove_at(arr, 0);
    return val;
}

/* === Query === */

bool gray_arrays_is_empty(GrayArray *arr) {
    return arr->len == 0;
}

bool gray_arrays_contains_int(GrayArray *arr, int64_t value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(int64_t *)((char *)arr->data + i * arr->elem_size) == value) return true;
    }
    return false;
}

bool gray_arrays_contains_char(GrayArray *arr, int32_t value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(int32_t *)((char *)arr->data + i * arr->elem_size) == value) return true;
    }
    return false;
}

bool gray_arrays_contains_byte(GrayArray *arr, uint8_t value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(uint8_t *)((char *)arr->data + i * arr->elem_size) == value) return true;
    }
    return false;
}

bool gray_arrays_contains_float(GrayArray *arr, double value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(double *)((char *)arr->data + i * arr->elem_size) == value) return true;
    }
    return false;
}

bool gray_arrays_contains_str(GrayArray *arr, GrayString value) {
    for (int32_t i = 0; i < arr->len; i++) {
        GrayString *s = (GrayString *)((char *)arr->data + i * arr->elem_size);
        if (s->len == value.len && memcmp(s->data, value.data, s->len) == 0) return true;
    }
    return false;
}

int64_t gray_arrays_index_of_int(GrayArray *arr, int64_t value) {
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(int64_t *)((char *)arr->data + i * arr->elem_size) == value) return i;
    }
    return -1;
}

int64_t gray_arrays_index_of_str(GrayArray *arr, GrayString value) {
    for (int32_t i = 0; i < arr->len; i++) {
        GrayString *s = (GrayString *)((char *)arr->data + i * arr->elem_size);
        if (s->len == value.len && memcmp(s->data, value.data, s->len) == 0) return i;
    }
    return -1;
}

int64_t gray_arrays_count(GrayArray *arr, int64_t value) {
    int64_t c = 0;
    for (int32_t i = 0; i < arr->len; i++) {
        if (*(int64_t *)((char *)arr->data + i * arr->elem_size) == value) c++;
    }
    return c;
}

bool gray_arrays_is_equal_prim(GrayArray *a, GrayArray *b) {
    if (a->len != b->len) return false;
    if (a->elem_size != b->elem_size) return false;
    if (a->len == 0) return true;
    return memcmp(a->data, b->data, (size_t)a->len * (size_t)a->elem_size) == 0;
}

bool gray_arrays_is_equal_str(GrayArray *a, GrayArray *b) {
    if (a->len != b->len) return false;
    for (int32_t i = 0; i < a->len; i++) {
        GrayString *sa = (GrayString *)((char *)a->data + i * a->elem_size);
        GrayString *sb = (GrayString *)((char *)b->data + i * b->elem_size);
        if (sa->len != sb->len) return false;
        if (sa->len > 0 && memcmp(sa->data, sb->data, sa->len) != 0) return false;
    }
    return true;
}

/* === Transformation === */

GrayArray gray_arrays_reverse(GrayArena *arena, GrayArray *arr) {
    GrayArray result = gray_array_new(arena, arr->elem_size, arr->len);
    char *src = (char *)arr->data;
    for (int32_t i = arr->len - 1; i >= 0; i--) {
        GRAY_ARRAY_PUSH(arena, &result, src + i * arr->elem_size);
    }
    return result;
}

GrayArray gray_arrays_slice(GrayArena *arena, GrayArray *arr, int32_t start, int32_t end) {
    if (start < 0) start = 0;
    if (end > arr->len) end = arr->len;
    if (start >= end) return gray_array_new(arena, arr->elem_size, 1);
    int32_t count = end - start;
    return gray_array_from(arena, (char *)arr->data + start * arr->elem_size, arr->elem_size, count);
}

GrayArray gray_arrays_concat(GrayArena *arena, GrayArray *a, GrayArray *b) {
    GrayArray result = gray_array_copy(arena, a);
    char *src = (char *)b->data;
    for (int32_t i = 0; i < b->len; i++) {
        GRAY_ARRAY_PUSH(arena, &result, src + i * b->elem_size);
    }
    return result;
}

GrayArray gray_arrays_deduplicate(GrayArena *arena, GrayArray *arr) {
    if (arr->len <= 1) return gray_array_copy(arena, arr);

    size_t element_size = (size_t)arr->elem_size;
    char *data = (char *)arr->data;

    /* Hash set (open addressing, power-of-two capacity, ~50% load).
     * Slots store source indices; -1 means empty. */
    uint32_t cap = 16;
    while (cap < (uint32_t)arr->len * 2) cap *= 2;
    int32_t *table = gray_arena_alloc(arena, (size_t)cap * sizeof(int32_t));
    memset(table, -1, (size_t)cap * sizeof(int32_t));
    uint32_t mask = cap - 1;

    GrayArray result = gray_array_new(arena, arr->elem_size, arr->len);

    for (int32_t i = 0; i < arr->len; i++) {
        const char *elem = data + i * element_size;

        /* FNV-1a hash over element bytes */
        uint32_t h = 2166136261u;
        for (size_t b = 0; b < element_size; b++) {
            h ^= (uint8_t)elem[b];
            h *= 16777619u;
        }

        uint32_t slot = h & mask;
        bool found = false;
        while (table[slot] >= 0) {
            if (memcmp(data + (size_t)table[slot] * element_size, elem, element_size) == 0) {
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
    /* Flatten one level: [[int]] -> [int]. Each element is an GrayArray. */
    GrayArray result = gray_array_new(arena, sizeof(int64_t), 8);
    for (int32_t i = 0; i < arr->len; i++) {
        GrayArray *inner = (GrayArray *)((char *)arr->data + i * arr->elem_size);
        char *idata = (char *)inner->data;
        for (int32_t j = 0; j < inner->len; j++) {
            GRAY_ARRAY_PUSH(arena, &result, idata + j * inner->elem_size);
        }
    }
    return result;
}

GrayArray gray_arrays_split_every(GrayArena *arena, GrayArray *arr, int32_t size) {
    if (size <= 0) size = 1;
    GrayArray result = gray_array_new(arena, sizeof(GrayArray), 4);
    char *data = (char *)arr->data;
    size_t element_size = (size_t)arr->elem_size;
    for (int32_t i = 0; i < arr->len; i += size) {
        int32_t chunk_len = (i + size <= arr->len) ? size : (arr->len - i);
        GrayArray chunk = gray_array_from(arena, data + i * element_size, arr->elem_size, chunk_len);
        GRAY_ARRAY_PUSH(arena, &result, &chunk);
    }
    return result;
}

GrayArray gray_arrays_pair(GrayArena *arena, GrayArray *a, GrayArray *b) {
    int32_t len = a->len < b->len ? a->len : b->len;
    GrayArray result = gray_array_new(arena, sizeof(GrayArray), len);
    for (int32_t i = 0; i < len; i++) {
        GrayArray pair_arr = gray_array_new(arena, a->elem_size, 2);
        GRAY_ARRAY_PUSH(arena, &pair_arr, (char *)a->data + i * a->elem_size);
        GRAY_ARRAY_PUSH(arena, &pair_arr, (char *)b->data + i * b->elem_size);
        GRAY_ARRAY_PUSH(arena, &result, &pair_arr);
    }
    return result;
}

/* === Computation === */

int64_t gray_arrays_get_sum(GrayArray *arr) {
    int64_t total = 0;
    for (int32_t i = 0; i < arr->len; i++) {
        total += *(int64_t *)((char *)arr->data + i * arr->elem_size);
    }
    return total;
}

int64_t gray_arrays_get_min(GrayArray *arr) {
    if (arr->len == 0) return 0;
    int64_t m = *(int64_t *)arr->data;
    for (int32_t i = 1; i < arr->len; i++) {
        int64_t v = *(int64_t *)((char *)arr->data + i * arr->elem_size);
        if (v < m) m = v;
    }
    return m;
}

int64_t gray_arrays_get_max(GrayArray *arr) {
    if (arr->len == 0) return 0;
    int64_t m = *(int64_t *)arr->data;
    for (int32_t i = 1; i < arr->len; i++) {
        int64_t v = *(int64_t *)((char *)arr->data + i * arr->elem_size);
        if (v > m) m = v;
    }
    return m;
}

/* === Sort === */

static int cmp_i64_asc(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static int cmp_i64_desc(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (vb > va) - (vb < va);
}

static int cmp_f64_asc(const void *a, const void *b) {
    double va = *(const double *)a;
    double vb = *(const double *)b;
    return (va > vb) - (va < vb);
}

static int cmp_f64_desc(const void *a, const void *b) {
    double va = *(const double *)a;
    double vb = *(const double *)b;
    return (vb > va) - (vb < va);
}

static int cmp_str_asc(const void *a, const void *b) {
    const GrayString *sa = (const GrayString *)a;
    const GrayString *sb = (const GrayString *)b;
    int32_t min_len = sa->len < sb->len ? sa->len : sb->len;
    int cmp = memcmp(sa->data, sb->data, (size_t)min_len);
    if (cmp != 0) return cmp;
    return (sa->len > sb->len) - (sa->len < sb->len);
}

static int cmp_str_desc(const void *a, const void *b) {
    return cmp_str_asc(b, a);
}

void gray_arrays_sort_asc(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    qsort(arr->data, (size_t)arr->len, (size_t)arr->elem_size, cmp_i64_asc);
}

void gray_arrays_sort_asc_float(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    qsort(arr->data, (size_t)arr->len, (size_t)arr->elem_size, cmp_f64_asc);
}

void gray_arrays_sort_asc_str(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    qsort(arr->data, (size_t)arr->len, (size_t)arr->elem_size, cmp_str_asc);
}

void gray_arrays_sort_desc(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    qsort(arr->data, (size_t)arr->len, (size_t)arr->elem_size, cmp_i64_desc);
}

void gray_arrays_sort_desc_float(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    qsort(arr->data, (size_t)arr->len, (size_t)arr->elem_size, cmp_f64_desc);
}

void gray_arrays_sort_desc_str(GrayArray *arr) {
    ARRAY_CHECK_ITER(arr);
    if (arr->len <= 1) return;
    qsort(arr->data, (size_t)arr->len, (size_t)arr->elem_size, cmp_str_desc);
}
