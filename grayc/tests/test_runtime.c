/*
 * test_runtime.c — Unit tests for the Grayscale runtime: GrayArena,
 * GrayString, GrayArray, and GrayMap.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include "../src/runtime/runtime.h"
#include "../src/runtime/array.h"
#include "../src/runtime/map.h"
#include "../src/runtime/atomic.h"
#include "../src/runtime/bigint.h"
#include "../src/runtime/test.h"
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>

/* Global arena used by string, array, and map tests. Arena tests create
 * their own short-lived arenas to exercise arena-specific behavior. */
static GrayArena *arena;

/* ===== GrayArena Tests ===== */

static void test_gray_arena_create(void) {
    GrayArena *a = gray_arena_create(4096);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(a->first);
    ASSERT_EQ(a->default_block_size, 4096);
    ASSERT(a->first == a->current);
    ASSERT_EQ(a->is_destroyed, false);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_alloc_basic(void) {
    GrayArena *a = gray_arena_create(4096);
    void *ptr = gray_arena_alloc(a, 32);
    ASSERT_NOT_NULL(ptr);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_alloc_alignment(void) {
    GrayArena *a = gray_arena_create(4096);
    gray_arena_alloc(a, 1);
    void *ptr2 = gray_arena_alloc(a, 8);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_EQ((uintptr_t)ptr2 % 8, 0);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_multi_block(void) {
    GrayArena *a = gray_arena_create(64);
    gray_arena_alloc(a, 64);
    void *ptr = gray_arena_alloc(a, 16);
    ASSERT_NOT_NULL(ptr);
    ASSERT(a->first->next != NULL);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_oversized(void) {
    GrayArena *a = gray_arena_create(64);
    void *ptr = gray_arena_alloc(a, 256);
    ASSERT_NOT_NULL(ptr);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_reset(void) {
    GrayArena *a = gray_arena_create(4096);
    gray_arena_alloc(a, 128);
    ASSERT_GT(gray_arena_usage(a), 0);
    gray_arena_reset(a);
    ASSERT_EQ(gray_arena_usage(a), 0);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_usage(void) {
    GrayArena *a = gray_arena_create(4096);
    ASSERT_EQ(gray_arena_usage(a), 0);
    gray_arena_alloc(a, 32);
    ASSERT_GE(gray_arena_usage(a), 32);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_scope_save_restore(void) {
    GrayArena *a = gray_arena_create(4096);
    gray_arena_alloc(a, 64);
    size_t usage_before = gray_arena_usage(a);
    GrayScopeMark mark = gray_scope_save(a);
    gray_arena_alloc(a, 128);
    ASSERT_GT(gray_arena_usage(a), usage_before);
    gray_scope_restore(a, mark);
    ASSERT_EQ(gray_arena_usage(a), usage_before);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

static void test_gray_arena_nested_scopes(void) {
    GrayArena *a = gray_arena_create(4096);
    GrayScopeMark outer = gray_scope_save(a);
    gray_arena_alloc(a, 64);
    size_t usage_after_outer = gray_arena_usage(a);
    GrayScopeMark inner = gray_scope_save(a);
    gray_arena_alloc(a, 128);
    ASSERT_GT(gray_arena_usage(a), usage_after_outer);
    gray_scope_restore(a, inner);
    ASSERT_EQ(gray_arena_usage(a), usage_after_outer);
    gray_scope_restore(a, outer);
    ASSERT_EQ(gray_arena_usage(a), 0);
    gray_arena_destroy(a, __FILE__, __LINE__);
}

/* ===== GrayString Tests ===== */

static void test_gray_string_lit(void) {
    GrayString s = gray_string_lit("hello");
    ASSERT_EQ(s.len, 5);
    ASSERT_STR_EQ(s.data, "hello");
}

static void test_gray_string_new(void) {
    GrayString s = gray_string_new(arena, "world", 5);
    ASSERT_EQ(s.len, 5);
    ASSERT_STR_EQ(s.data, "world");
}

static void test_gray_string_eq(void) {
    GrayString a = gray_string_lit("abc");
    GrayString b = gray_string_lit("abc");
    ASSERT(gray_string_eq(a, b));
    GrayString c = gray_string_lit("xyz");
    ASSERT(!gray_string_eq(a, c));
    GrayString d = gray_string_lit("ab");
    ASSERT(!gray_string_eq(a, d));
}

static void test_gray_string_concat(void) {
    GrayString a = gray_string_lit("hello");
    GrayString b = gray_string_lit(" world");
    GrayString c = gray_string_concat(arena, a, b);
    ASSERT_EQ(c.len, 11);
    ASSERT_STR_EQ(c.data, "hello world");
}

static void test_gray_string_concat_n(void) {
    GrayString r = gray_string_concat_n(arena, 3,
        gray_string_lit("a"), gray_string_lit("bc"), gray_string_lit("def"));
    ASSERT_EQ(r.len, 6);
    ASSERT_STR_EQ(r.data, "abcdef");
    /* a zero count yields the empty string */
    GrayString empty = gray_string_concat_n(arena, 0);
    ASSERT_EQ(empty.len, 0);
    /* embedded empty parts contribute nothing */
    GrayString g = gray_string_concat_n(arena, 3,
        gray_string_lit("x"), gray_string_lit(""), gray_string_lit("y"));
    ASSERT_EQ(g.len, 2);
    ASSERT_STR_EQ(g.data, "xy");
}

static void test_gray_string_format(void) {
    GrayString s = gray_string_format(arena, "value=%d name=%s", 42, "test");
    ASSERT_EQ(s.len, 18);
    ASSERT_STR_EQ(s.data, "value=42 name=test");
}

static void test_gray_c_string_dup(void) {
    GrayString s = gray_c_string_dup(arena, "duplicate me");
    ASSERT_EQ(s.len, 12);
    ASSERT_STR_EQ(s.data, "duplicate me");
    /* NULL input yields empty string */
    GrayString n = gray_c_string_dup(arena, NULL);
    ASSERT_EQ(n.len, 0);
}

static void test_gray_string_empty(void) {
    GrayString s = gray_string_lit("");
    ASSERT_EQ(s.len, 0);
    GrayString s2 = gray_string_new(arena, "", 0);
    ASSERT_EQ(s2.len, 0);
    ASSERT(gray_string_eq(s, s2));
}

/* ===== GrayArray Tests ===== */

static void test_gray_array_new(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0, GRAY_ELEM_I64);
    ASSERT_EQ(arr.len, 0);
    ASSERT_GE(arr.capacity, GRAY_ARRAY_MIN_CAPACITY);
    ASSERT_EQ(arr.elem_size, (int32_t)sizeof(int64_t));
}

static void test_gray_array_push_and_get(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0, GRAY_ELEM_I64);
    int64_t val = 42;
    GRAY_ARRAY_PUSH(arena, &arr, &val);
    ASSERT_EQ(arr.len, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 42);
}

static void test_gray_array_push_growth(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 2, GRAY_ELEM_I64);
    for (int i = 0; i < 10; i++) {
        int64_t val = i * 10;
        GRAY_ARRAY_PUSH(arena, &arr, &val);
    }
    ASSERT_EQ(arr.len, 10);
    ASSERT_GE(arr.capacity, 10);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 9), 90);
}

static void test_gray_array_set(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0, GRAY_ELEM_I64);
    int64_t val = 100;
    GRAY_ARRAY_PUSH(arena, &arr, &val);
    val = 200;
    gray_array_set(&arr, 0, &val, __FILE__, __LINE__);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 200);
}

static void test_gray_array_from(void) {
    int64_t data[] = {10, 20, 30};
    GrayArray arr = gray_array_from(arena, data, sizeof(int64_t), 3, GRAY_ELEM_I64);
    ASSERT_EQ(arr.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 10);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 20);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 2), 30);
}

static void test_gray_array_from_i64(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5);
    ASSERT_EQ(arr.len, 5);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 4), 5);
}

static void test_gray_array_from_f64(void) {
    GrayArray arr = GRAY_ARRAY_FROM_F64(arena, 1.5, 2.25, 3.0);
    ASSERT_EQ(arr.len, 3);
    ASSERT_EQ(arr.elem_size, (int32_t)sizeof(double));
    ASSERT(GRAY_ARRAY_GET(arr, double, 0) == 1.5);
    ASSERT(GRAY_ARRAY_GET(arr, double, 2) == 3.0);
}

static void test_gray_array_from_bool(void) {
    GrayArray arr = GRAY_ARRAY_FROM_BOOL(arena, true, false, true);
    ASSERT_EQ(arr.len, 3);
    ASSERT_EQ(arr.elem_size, (int32_t)sizeof(bool));
    ASSERT(GRAY_ARRAY_GET(arr, bool, 0) == true);
    ASSERT(GRAY_ARRAY_GET(arr, bool, 1) == false);
}

static void test_gray_array_from_str(void) {
    GrayArray arr = GRAY_ARRAY_FROM_STR(arena,
        gray_string_lit("alpha"),
        gray_string_lit("beta"));
    ASSERT_EQ(arr.len, 2);
    GrayString s0 = GRAY_ARRAY_GET(arr, GrayString, 0);
    ASSERT(gray_string_eq(s0, gray_string_lit("alpha")));
    GrayString s1 = GRAY_ARRAY_GET(arr, GrayString, 1);
    ASSERT(gray_string_eq(s1, gray_string_lit("beta")));
}

static void test_gray_array_copy(void) {
    GrayArray src = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    GrayArray dst = gray_array_copy(arena, &src);
    ASSERT_EQ(dst.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(dst, int64_t, 1), 20);
    int64_t updated = 999;
    gray_array_set(&src, 1, &updated, __FILE__, __LINE__);
    ASSERT_EQ(GRAY_ARRAY_GET(dst, int64_t, 1), 20);
}

static void test_gray_array_multiple_types(void) {
    GrayArray arr = gray_array_new(arena, sizeof(double), 0, GRAY_ELEM_F64);
    double v = 3.14;
    GRAY_ARRAY_PUSH(arena, &arr, &v);
    ASSERT_EQ(arr.len, 1);
    double got = GRAY_ARRAY_GET(arr, double, 0);
    ASSERT(got == 3.14);
}

static void test_gray_array_empty(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0, GRAY_ELEM_I64);
    ASSERT_EQ(arr.len, 0);
}

/* ===== GrayMap Tests ===== */

static void test_gray_map_new_kind(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    ASSERT_EQ(m.count, 0);
    ASSERT_GE(m.capacity, GRAY_MAP_MIN_CAPACITY);
}

static void test_gray_map_set_get_int(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t key = 1, val = 100;
    GRAY_MAP_SET(arena, &m, &key, &val);
    void *got = gray_map_get(&m, &key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(*(int64_t *)got, 100);
}

static void test_gray_map_set_get_string(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString key = gray_string_lit("name");
    int64_t val = 42;
    gray_map_set(arena, &m, &key, &val, __FILE__, __LINE__);
    void *got = gray_map_get_str(&m, key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(*(int64_t *)got, 42);
}

static void test_gray_map_has(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t key = 5, val = 50;
    ASSERT(!gray_map_has(&m, &key));
    GRAY_MAP_SET(arena, &m, &key, &val);
    ASSERT(gray_map_has(&m, &key));
}

static void test_gray_map_remove(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t key = 7, val = 70;
    GRAY_MAP_SET(arena, &m, &key, &val);
    ASSERT_EQ(m.count, 1);
    gray_map_remove(&m, &key, __FILE__, __LINE__);
    ASSERT_EQ(m.count, 0);
    ASSERT(gray_map_get(&m, &key) == NULL);
}

static void test_gray_map_overwrite(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t key = 1, v1 = 10, v2 = 20;
    GRAY_MAP_SET(arena, &m, &key, &v1);
    GRAY_MAP_SET(arena, &m, &key, &v2);
    ASSERT_EQ(m.count, 1);
    ASSERT_EQ(*(int64_t *)gray_map_get(&m, &key), 20);
}

static void test_gray_map_clear(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k1 = 1, v1 = 10, k2 = 2, v2 = 20;
    GRAY_MAP_SET(arena, &m, &k1, &v1);
    GRAY_MAP_SET(arena, &m, &k2, &v2);
    gray_map_clear(&m, __FILE__, __LINE__);
    ASSERT_EQ(m.count, 0);
}

static void test_gray_map_insertion_order(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t keys[] = {30, 10, 20};
    int64_t vals[] = {300, 100, 200};
    for (int i = 0; i < 3; i++)
        GRAY_MAP_SET(arena, &m, &keys[i], &vals[i]);
    ASSERT_EQ(m.order_len, 3);
    for (int i = 0; i < 3; i++) {
        int32_t slot = m.order[i];
        int64_t *k = (int64_t *)gray_map_key_at(&m, slot);
        ASSERT_EQ(*k, keys[i]);
    }
}

/* gray_map_remove punches a hole in the order array rather than shifting
 * it down. Readers skip the holes, so insertion order must survive a
 * removal from the front and the middle. */
static void test_gray_map_order_after_remove(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t keys[] = {50, 40, 30, 20, 10};
    for (int i = 0; i < 5; i++) {
        int64_t v = keys[i] * 10;
        GRAY_MAP_SET(arena, &m, &keys[i], &v);
    }
    /* Drop the first and middle entries — the worst case for the old
     * linear-scan-and-memmove removal. */
    gray_map_remove(&m, &keys[0], __FILE__, __LINE__);
    gray_map_remove(&m, &keys[2], __FILE__, __LINE__);
    ASSERT_EQ(m.count, 3);

    int64_t expected[] = {40, 20, 10};
    int live = 0;
    for (int32_t i = 0; i < m.order_len; i++) {
        int32_t slot = m.order[i];
        if (slot < 0) continue;
        ASSERT_EQ(*(int64_t *)gray_map_key_at(&m, slot), expected[live]);
        live++;
    }
    ASSERT_EQ(live, 3);

    /* Re-inserting a removed key appends it at the end, not its old spot. */
    int64_t re = 500;
    GRAY_MAP_SET(arena, &m, &keys[0], &re);
    int32_t last = -1;
    for (int32_t i = 0; i < m.order_len; i++) if (m.order[i] >= 0) last = m.order[i];
    ASSERT_EQ(*(int64_t *)gray_map_key_at(&m, last), 50);
}

/* A stale by-value copy of a GrayMap must never observe an entry twice.
 * Reclaiming holes by rebuilding (rather than compacting the shared order
 * array in place) is what keeps the copy's order_len consistent with the
 * array it still points at. */
static void test_gray_map_copy_sees_no_duplicates(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 8, GRAY_ELEM_I64, GRAY_ELEM_I64);
    for (int64_t i = 0; i < 6; i++) { int64_t v = i; GRAY_MAP_SET(arena, &m, &i, &v); }
    int64_t drop = 0;
    gray_map_remove(&m, &drop, __FILE__, __LINE__);
    drop = 3;
    gray_map_remove(&m, &drop, __FILE__, __LINE__);

    /* Iterate through a struct copy, the way generated code passes maps to
     * gray_builtin_map_to_string, then iterate the original again. */
    GrayMap view = m;
    int seen_via_copy = 0;
    for (int32_t i = 0; i < view.order_len; i++) if (view.order[i] >= 0) seen_via_copy++;
    ASSERT_EQ(seen_via_copy, 4);

    int seen_via_original = 0;
    for (int32_t i = 0; i < m.order_len; i++) if (m.order[i] >= 0) seen_via_original++;
    ASSERT_EQ(seen_via_original, 4);
}

/* Sustained remove+set churn must not grow order_len past capacity: the
 * insert path rebuilds once the order array fills, which is what keeps
 * O(1) removal from overflowing the allocation. */
static void test_gray_map_order_churn_bounded(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 8, GRAY_ELEM_I64, GRAY_ELEM_I64);
    for (int64_t i = 0; i < 32; i++) { int64_t v = i; GRAY_MAP_SET(arena, &m, &i, &v); }
    for (int round = 0; round < 500; round++) {
        int64_t key = round % 32;
        gray_map_remove(&m, &key, __FILE__, __LINE__);
        int64_t v = key * 2;
        GRAY_MAP_SET(arena, &m, &key, &v);
        ASSERT(m.order_len <= m.capacity);
    }
    ASSERT_EQ(m.count, 32);
    int live = 0;
    for (int32_t i = 0; i < m.order_len; i++) if (m.order[i] >= 0) live++;
    ASSERT_EQ(live, 32);
    int64_t probe = 7;
    ASSERT_EQ(*(int64_t *)gray_map_get(&m, &probe), 14);
}

static void test_gray_map_rehash(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 8, GRAY_ELEM_I64, GRAY_ELEM_I64);
    for (int i = 0; i < 7; i++) {
        int64_t key = i, val = i * 100;
        GRAY_MAP_SET(arena, &m, &key, &val);
    }
    ASSERT_GT(m.capacity, 8);
    for (int i = 0; i < 7; i++) {
        int64_t key = i;
        void *got = gray_map_get(&m, &key);
        ASSERT_NOT_NULL(got);
        ASSERT_EQ(*(int64_t *)got, i * 100);
    }
}

static void test_gray_map_copy(void) {
    GrayMap src = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k = 1, v = 10;
    GRAY_MAP_SET(arena, &src, &k, &v);
    GrayMap dst = gray_map_copy(arena, &src);
    ASSERT_EQ(dst.count, 1);
    ASSERT_EQ(*(int64_t *)gray_map_get(&dst, &k), 10);
    int64_t v2 = 999;
    GRAY_MAP_SET(arena, &src, &k, &v2);
    ASSERT_EQ(*(int64_t *)gray_map_get(&dst, &k), 10);
}

static void test_gray_map_float_normalization(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(double), sizeof(int64_t), 0, GRAY_ELEM_F64, GRAY_ELEM_I64);
    double pos_zero = 0.0;
    double neg_zero = -0.0;
    int64_t val = 42;
    gray_map_set(arena, &m, &pos_zero, &val, __FILE__, __LINE__);
    void *got = gray_map_get(&m, &neg_zero);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(*(int64_t *)got, 42);
}

static void test_gray_map_empty(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    ASSERT_EQ(m.count, 0);
    int64_t key = 999;
    ASSERT(gray_map_get(&m, &key) == NULL);
}

/* ===== atomics ===== */

static void test_atomic_load_store(void) {
    int64_t cell = 5;
    ASSERT_EQ(gray_atomic_load(&cell), 5);
    gray_atomic_store(&cell, -9);
    ASSERT_EQ(gray_atomic_load(&cell), -9);
    ASSERT_EQ(cell, -9);
}

static void test_atomic_add_returns_previous_value(void) {
    int64_t cell = 10;
    ASSERT_EQ(gray_atomic_add(&cell, 5), 10);
    ASSERT_EQ(cell, 15);
    ASSERT_EQ(gray_atomic_add(&cell, -20), 15);
    ASSERT_EQ(cell, -5);
}

static void test_atomic_sub_returns_previous_value(void) {
    int64_t cell = 10;
    ASSERT_EQ(gray_atomic_sub(&cell, 3), 10);
    ASSERT_EQ(cell, 7);
}

static void test_atomic_exchange_returns_previous_value(void) {
    int64_t cell = 1;
    ASSERT_EQ(gray_atomic_exchange(&cell, 2), 1);
    ASSERT_EQ(cell, 2);
}

static void test_atomic_cas_succeeds_when_expected_matches(void) {
    int64_t cell = 4;
    ASSERT(gray_atomic_cas(&cell, 4, 8));
    ASSERT_EQ(cell, 8);
}

static void test_atomic_cas_fails_and_leaves_value_when_expected_differs(void) {
    int64_t cell = 4;
    ASSERT(!gray_atomic_cas(&cell, 5, 8));
    ASSERT_EQ(cell, 4);
}

static void test_atomic_bitwise_return_previous_value(void) {
    int64_t cell = 0xF0;
    ASSERT_EQ(gray_atomic_and(&cell, 0x3C), 0xF0);
    ASSERT_EQ(cell, 0x30);
    ASSERT_EQ(gray_atomic_or(&cell, 0x05), 0x30);
    ASSERT_EQ(cell, 0x35);
    ASSERT_EQ(gray_atomic_xor(&cell, 0xFF), 0x35);
    ASSERT_EQ(cell, 0xCA);
}

static void test_atomic_add_wraps_at_i64_max(void) {
    int64_t cell = INT64_MAX;
    ASSERT_EQ(gray_atomic_add(&cell, 1), INT64_MAX);
    ASSERT_EQ(cell, INT64_MIN);
}

static void test_atomic_sub_wraps_at_i64_min(void) {
    int64_t cell = INT64_MIN;
    ASSERT_EQ(gray_atomic_sub(&cell, 1), INT64_MIN);
    ASSERT_EQ(cell, INT64_MAX);
}

static void test_atomic32_load_store(void) {
    int32_t cell = 5;
    ASSERT_EQ(gray_atomic_load32(&cell), 5);
    gray_atomic_store32(&cell, -9);
    ASSERT_EQ(gray_atomic_load32(&cell), -9);
}

static void test_atomic32_add_and_sub_return_previous_value(void) {
    int32_t cell = 10;
    ASSERT_EQ(gray_atomic_add32(&cell, 5), 10);
    ASSERT_EQ(cell, 15);
    ASSERT_EQ(gray_atomic_sub32(&cell, 20), 15);
    ASSERT_EQ(cell, -5);
}

static void test_atomic32_add_wraps_at_i32_max(void) {
    int32_t cell = INT32_MAX;
    gray_atomic_add32(&cell, 1);
    ASSERT_EQ(cell, INT32_MIN);
}

static void test_atomic32_exchange_returns_previous_value(void) {
    int32_t cell = 1;
    ASSERT_EQ(gray_atomic_exchange32(&cell, 2), 1);
    ASSERT_EQ(cell, 2);
}

static void test_atomic32_cas(void) {
    int32_t cell = 4;
    ASSERT(!gray_atomic_cas32(&cell, 5, 8));
    ASSERT_EQ(cell, 4);
    ASSERT(gray_atomic_cas32(&cell, 4, 8));
    ASSERT_EQ(cell, 8);
}

static void test_atomic32_operations_leave_neighbouring_words_untouched(void) {
    int32_t cells[3] = {7, 0, 9};
    gray_atomic_store32(&cells[1], -1);
    gray_atomic_add32(&cells[1], 1);
    ASSERT_EQ(cells[0], 7);
    ASSERT_EQ(cells[1], 0);
    ASSERT_EQ(cells[2], 9);
}

static void test_atomic8_load_store(void) {
    uint8_t cell = 5;
    ASSERT_EQ(gray_atomic_load8(&cell), 5);
    gray_atomic_store8(&cell, 250);
    ASSERT_EQ(gray_atomic_load8(&cell), 250);
}

static void test_atomic8_exchange_returns_previous_value(void) {
    uint8_t cell = 1;
    ASSERT_EQ(gray_atomic_exchange8(&cell, 2), 1);
    ASSERT_EQ(cell, 2);
}

static void test_atomic8_cas(void) {
    uint8_t cell = 4;
    ASSERT(!gray_atomic_cas8(&cell, 5, 8));
    ASSERT_EQ(cell, 4);
    ASSERT(gray_atomic_cas8(&cell, 4, 8));
    ASSERT_EQ(cell, 8);
}

static void test_atomic8_operations_leave_neighbouring_bytes_untouched(void) {
    uint8_t cells[3] = {7, 0, 9};
    gray_atomic_store8(&cells[1], 255);
    gray_atomic_exchange8(&cells[1], 0);
    ASSERT_EQ(cells[0], 7);
    ASSERT_EQ(cells[1], 0);
    ASSERT_EQ(cells[2], 9);
}

static void test_atomic_spinlock_trylock_and_unlock(void) {
    int32_t lock = 0;
    ASSERT(gray_spin_trylock(&lock));
    ASSERT(!gray_spin_trylock(&lock));
    gray_spin_unlock(&lock);
    ASSERT(gray_spin_trylock(&lock));
    gray_spin_unlock(&lock);
}

static void test_atomic_spin_lock_acquires_free_lock(void) {
    int32_t lock = 0;
    gray_spin_lock(&lock);
    ASSERT(!gray_spin_trylock(&lock));
    gray_spin_unlock(&lock);
}

static void test_atomic_fence_returns(void) {
    gray_atomic_fence();
}

#define ATOMIC_THREAD_COUNT 4
#define ATOMIC_THREAD_ITERATIONS 20000

static int64_t shared_atomic_counter;
static int64_t shared_locked_counter;
static int32_t shared_spinlock;

static void *atomic_add_worker(void *unused) {
    (void)unused;
    for (int i = 0; i < ATOMIC_THREAD_ITERATIONS; i++) gray_atomic_add(&shared_atomic_counter, 1);
    return NULL;
}

static void *spinlock_worker(void *unused) {
    (void)unused;
    for (int i = 0; i < ATOMIC_THREAD_ITERATIONS; i++) {
        gray_spin_lock(&shared_spinlock);
        shared_locked_counter = shared_locked_counter + 1;
        gray_spin_unlock(&shared_spinlock);
    }
    return NULL;
}

static void run_workers(void *(*worker)(void *)) {
    pthread_t threads[ATOMIC_THREAD_COUNT];
    for (int i = 0; i < ATOMIC_THREAD_COUNT; i++) pthread_create(&threads[i], NULL, worker, NULL);
    for (int i = 0; i < ATOMIC_THREAD_COUNT; i++) pthread_join(threads[i], NULL);
}

static void test_atomic_add_loses_no_updates_across_threads(void) {
    shared_atomic_counter = 0;
    run_workers(atomic_add_worker);
    ASSERT_EQ(shared_atomic_counter, (int64_t)ATOMIC_THREAD_COUNT * ATOMIC_THREAD_ITERATIONS);
}

static void test_atomic_spinlock_gives_mutual_exclusion_across_threads(void) {
    shared_locked_counter = 0;
    shared_spinlock = 0;
    run_workers(spinlock_worker);
    ASSERT_EQ(shared_locked_counter, (int64_t)ATOMIC_THREAD_COUNT * ATOMIC_THREAD_ITERATIONS);
}

/* ===== element helpers ===== */

#define ASSERT_FLOAT_COMPARE(actual, expected) do { \
    double actual_value_ = (actual), expected_value_ = (expected); \
    if (fabs(actual_value_ - expected_value_) > 1e-9 * (fabs(expected_value_) > 1.0 ? fabs(expected_value_) : 1.0)) { \
        fprintf(stderr, "  FAIL %s:%d: %g != %g\n", __FILE__, __LINE__, actual_value_, expected_value_); \
        _test_failed_this = 1; \
        return; \
    } \
} while (0)

#define ELEM_COMPARE_CASE(KIND, TYPE, SMALL, LARGE) do { \
    TYPE small_value = (SMALL), large_value = (LARGE); \
    ASSERT_EQ(gray_elem_compare(KIND, sizeof(TYPE), &small_value, &large_value), -1); \
    ASSERT_EQ(gray_elem_compare(KIND, sizeof(TYPE), &large_value, &small_value), 1); \
    ASSERT_EQ(gray_elem_compare(KIND, sizeof(TYPE), &small_value, &small_value), 0); \
} while (0)

static void test_elem_compare_orders_every_integer_kind(void) {
    ELEM_COMPARE_CASE(GRAY_ELEM_I8, int8_t, -5, 5);
    ELEM_COMPARE_CASE(GRAY_ELEM_I16, int16_t, -300, 300);
    ELEM_COMPARE_CASE(GRAY_ELEM_I32, int32_t, -70000, 70000);
    ELEM_COMPARE_CASE(GRAY_ELEM_CHAR, int32_t, 'a', 'b');
    ELEM_COMPARE_CASE(GRAY_ELEM_I64, int64_t, INT64_MIN, INT64_MAX);
    ELEM_COMPARE_CASE(GRAY_ELEM_U8, uint8_t, 1, 200);
    ELEM_COMPARE_CASE(GRAY_ELEM_BOOL, bool, false, true);
    ELEM_COMPARE_CASE(GRAY_ELEM_U16, uint16_t, 1, 60000);
    ELEM_COMPARE_CASE(GRAY_ELEM_U32, uint32_t, 1, 4000000000u);
    ELEM_COMPARE_CASE(GRAY_ELEM_U64, uint64_t, 1, UINT64_MAX);
}

static void test_elem_compare_floats_with_nan_ordered_last(void) {
    double one = 1.0, two = 2.0, not_a_number = NAN, other_nan = -NAN;
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F64, 8, &one, &two), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F64, 8, &two, &one), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F64, 8, &one, &one), 0);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F64, 8, &one, &not_a_number), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F64, 8, &not_a_number, &one), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F64, 8, &not_a_number, &other_nan), 0);
    float small = 1.5f, large = 2.5f, float_nan = NAN;
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F32, 4, &small, &large), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_F32, 4, &float_nan, &small), 1);
}

static void test_elem_compare_wide_integers(void) {
    gray_i128 negative = {UINT64_MAX, -1}, positive = {1, 0};
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_I128, 16, &negative, &positive), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_I128, 16, &positive, &negative), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_I128, 16, &positive, &positive), 0);
    gray_u128 low = {5, 0}, high = {0, 1};
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_U128, 16, &low, &high), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_U128, 16, &high, &low), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_U128, 16, &low, &low), 0);
    gray_i256 minus_one = {{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX}}, one = {{1, 0, 0, 0}};
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_I256, 32, &minus_one, &one), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_I256, 32, &one, &minus_one), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_I256, 32, &one, &one), 0);
    gray_u256 small = {{9, 0, 0, 0}}, big = {{0, 0, 0, 1}};
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_U256, 32, &small, &big), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_U256, 32, &big, &small), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_U256, 32, &big, &big), 0);
}

static void test_elem_compare_strings_bytewise_then_by_length(void) {
    GrayString apple = gray_string_lit("apple"), apricot = gray_string_lit("apricot"), app = gray_string_lit("app"), empty = gray_string_lit("");
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_STRING, sizeof(GrayString), &apple, &apricot), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_STRING, sizeof(GrayString), &apricot, &apple), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_STRING, sizeof(GrayString), &app, &apple), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_STRING, sizeof(GrayString), &apple, &apple), 0);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_STRING, sizeof(GrayString), &empty, &app), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_STRING, sizeof(GrayString), &empty, &empty), 0);
}

static void test_elem_compare_other_kinds_compare_bytes(void) {
    uint8_t first[4] = {1, 2, 3, 4}, second[4] = {1, 2, 3, 5};
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_OTHER, 4, first, second), -1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_OTHER, 4, second, first), 1);
    ASSERT_EQ(gray_elem_compare(GRAY_ELEM_OTHER, 4, first, first), 0);
}

static void test_elem_equal_floats_follow_ieee_equality(void) {
    double zero = 0.0, negative_zero = -0.0, not_a_number = NAN, one = 1.0;
    ASSERT(gray_elem_equal(GRAY_ELEM_F64, 8, &zero, &negative_zero));
    ASSERT(!gray_elem_equal(GRAY_ELEM_F64, 8, &not_a_number, &not_a_number));
    ASSERT(!gray_elem_equal(GRAY_ELEM_F64, 8, &zero, &one));
    float float_zero = 0.0f, float_negative_zero = -0.0f;
    ASSERT(gray_elem_equal(GRAY_ELEM_F32, 4, &float_zero, &float_negative_zero));
}

static void test_elem_equal_strings_and_raw_bytes(void) {
    GrayString same_a = gray_string_lit("abc"), same_b = gray_string_lit("abc"), different = gray_string_lit("abd"), shorter = gray_string_lit("ab");
    GrayString empty_a = gray_string_lit(""), empty_b = gray_string_lit("");
    ASSERT(gray_elem_equal(GRAY_ELEM_STRING, sizeof(GrayString), &same_a, &same_b));
    ASSERT(!gray_elem_equal(GRAY_ELEM_STRING, sizeof(GrayString), &same_a, &different));
    ASSERT(!gray_elem_equal(GRAY_ELEM_STRING, sizeof(GrayString), &same_a, &shorter));
    ASSERT(gray_elem_equal(GRAY_ELEM_STRING, sizeof(GrayString), &empty_a, &empty_b));
    int64_t left = 7, right = 7, other = 8;
    ASSERT(gray_elem_equal(GRAY_ELEM_I64, 8, &left, &right));
    ASSERT(!gray_elem_equal(GRAY_ELEM_I64, 8, &left, &other));
}

static void test_elem_hash_equal_values_hash_equal(void) {
    int64_t first = 12345, second = 12345, different = 12346;
    ASSERT_EQ(gray_elem_hash(GRAY_ELEM_I64, 8, &first), gray_elem_hash(GRAY_ELEM_I64, 8, &second));
    ASSERT_NE(gray_elem_hash(GRAY_ELEM_I64, 8, &first), gray_elem_hash(GRAY_ELEM_I64, 8, &different));
}

static void test_elem_hash_strings_by_content(void) {
    GrayString first = gray_string_lit("hello"), second = gray_string_lit("hello"), different = gray_string_lit("world");
    ASSERT_EQ(gray_elem_hash(GRAY_ELEM_STRING, sizeof(GrayString), &first), gray_elem_hash(GRAY_ELEM_STRING, sizeof(GrayString), &second));
    ASSERT_NE(gray_elem_hash(GRAY_ELEM_STRING, sizeof(GrayString), &first), gray_elem_hash(GRAY_ELEM_STRING, sizeof(GrayString), &different));
}

static void test_elem_hash_negative_zero_hashes_as_zero(void) {
    double zero = 0.0, negative_zero = -0.0;
    ASSERT_EQ(gray_elem_hash(GRAY_ELEM_F64, 8, &zero), gray_elem_hash(GRAY_ELEM_F64, 8, &negative_zero));
    float float_zero = 0.0f, float_negative_zero = -0.0f;
    ASSERT_EQ(gray_elem_hash(GRAY_ELEM_F32, 4, &float_zero), gray_elem_hash(GRAY_ELEM_F32, 4, &float_negative_zero));
    double one = 1.0;
    ASSERT_NE(gray_elem_hash(GRAY_ELEM_F64, 8, &zero), gray_elem_hash(GRAY_ELEM_F64, 8, &one));
}

static void test_elem_add_accumulates_each_kind(void) {
    int8_t i8 = 100; int8_t i8_add = 20;
    gray_elem_add(GRAY_ELEM_I8, &i8, &i8_add, __FILE__, __LINE__);
    ASSERT_EQ(i8, 120);
    int16_t i16 = 1000; int16_t i16_add = 234;
    gray_elem_add(GRAY_ELEM_I16, &i16, &i16_add, __FILE__, __LINE__);
    ASSERT_EQ(i16, 1234);
    int32_t i32 = 100000; int32_t i32_add = 23;
    gray_elem_add(GRAY_ELEM_I32, &i32, &i32_add, __FILE__, __LINE__);
    ASSERT_EQ(i32, 100023);
    int64_t i64 = 5; int64_t i64_add = -8;
    gray_elem_add(GRAY_ELEM_I64, &i64, &i64_add, __FILE__, __LINE__);
    ASSERT_EQ(i64, -3);
    uint8_t u8 = 200; uint8_t u8_add = 55;
    gray_elem_add(GRAY_ELEM_U8, &u8, &u8_add, __FILE__, __LINE__);
    ASSERT_EQ(u8, 255);
    uint16_t u16 = 60000; uint16_t u16_add = 5535;
    gray_elem_add(GRAY_ELEM_U16, &u16, &u16_add, __FILE__, __LINE__);
    ASSERT_EQ(u16, 65535);
    uint32_t u32 = 1; uint32_t u32_add = 2;
    gray_elem_add(GRAY_ELEM_U32, &u32, &u32_add, __FILE__, __LINE__);
    ASSERT_EQ(u32, 3);
    uint64_t u64 = 10; uint64_t u64_add = 20;
    gray_elem_add(GRAY_ELEM_U64, &u64, &u64_add, __FILE__, __LINE__);
    ASSERT_EQ(u64, 30);
    double f64 = 1.5; double f64_add = 2.25;
    gray_elem_add(GRAY_ELEM_F64, &f64, &f64_add, __FILE__, __LINE__);
    ASSERT_FLOAT_COMPARE(f64, 3.75);
    float f32 = 0.5f; float f32_add = 0.25f;
    gray_elem_add(GRAY_ELEM_F32, &f32, &f32_add, __FILE__, __LINE__);
    ASSERT_FLOAT_COMPARE(f32, 0.75);
}

static void test_elem_add_wide_integers(void) {
    gray_i128 i128 = {UINT64_MAX, 0}; gray_i128 i128_add = {1, 0};
    gray_elem_add(GRAY_ELEM_I128, &i128, &i128_add, __FILE__, __LINE__);
    ASSERT(i128.low == 0 && i128.high == 1);
    gray_u128 u128 = {UINT64_MAX, 0}; gray_u128 u128_add = {1, 0};
    gray_elem_add(GRAY_ELEM_U128, &u128, &u128_add, __FILE__, __LINE__);
    ASSERT(u128.low == 0 && u128.high == 1);
    gray_i256 i256 = {{UINT64_MAX, 0, 0, 0}}; gray_i256 i256_add = {{1, 0, 0, 0}};
    gray_elem_add(GRAY_ELEM_I256, &i256, &i256_add, __FILE__, __LINE__);
    ASSERT(i256.w[0] == 0 && i256.w[1] == 1);
    gray_u256 u256 = {{UINT64_MAX, 0, 0, 0}}; gray_u256 u256_add = {{1, 0, 0, 0}};
    gray_elem_add(GRAY_ELEM_U256, &u256, &u256_add, __FILE__, __LINE__);
    ASSERT(u256.w[0] == 0 && u256.w[1] == 1);
}

static void test_elem_add_ignores_kinds_that_are_not_numbers(void) {
    int64_t accumulator = 9; int64_t value = 4;
    gray_elem_add(GRAY_ELEM_STRING, &accumulator, &value, __FILE__, __LINE__);
    gray_elem_add(GRAY_ELEM_OTHER, &accumulator, &value, __FILE__, __LINE__);
    ASSERT_EQ(accumulator, 9);
}

static void test_elem_to_double_every_kind(void) {
    int8_t i8 = -3; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I8, &i8), -3.0);
    int16_t i16 = -300; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I16, &i16), -300.0);
    int32_t i32 = 70000; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I32, &i32), 70000.0);
    int32_t character = 'A'; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_CHAR, &character), 65.0);
    int64_t i64 = -9; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I64, &i64), -9.0);
    uint8_t u8 = 250; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_U8, &u8), 250.0);
    bool flag = true; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_BOOL, &flag), 1.0);
    uint16_t u16 = 60000; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_U16, &u16), 60000.0);
    uint32_t u32 = 4000000000u; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_U32, &u32), 4000000000.0);
    uint64_t u64 = 1u << 20; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_U64, &u64), 1048576.0);
    float f32 = 0.5f; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_F32, &f32), 0.5);
    double f64 = -2.5; ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_F64, &f64), -2.5);
    GrayString text = gray_string_lit("x"); ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_STRING, &text), 0.0);
}

static void test_elem_to_double_wide_integers(void) {
    gray_i128 negative = {0, -1};
    ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I128, &negative), -18446744073709551616.0);
    gray_i128 positive = {5, 0};
    ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I128, &positive), 5.0);
    gray_u128 two_to_64 = {0, 1};
    ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_U128, &two_to_64), 18446744073709551616.0);
    gray_i256 minus_five = {{UINT64_MAX - 4, UINT64_MAX, UINT64_MAX, UINT64_MAX}};
    ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I256, &minus_five), -5.0);
    gray_u256 seven = {{7, 0, 0, 0}};
    ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_U256, &seven), 7.0);
    gray_i256 wide_positive = {{3, 1, 0, 0}};
    ASSERT_FLOAT_COMPARE(gray_elem_to_double(GRAY_ELEM_I256, &wide_positive), 18446744073709551619.0);
}

static void test_elem_to_i64_and_to_u64(void) {
    int8_t i8 = -3; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_I8, &i8), -3);
    int16_t i16 = -300; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_I16, &i16), -300);
    int32_t i32 = -70000; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_I32, &i32), -70000);
    int32_t character = 'z'; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_CHAR, &character), 'z');
    int64_t i64 = INT64_MIN; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_I64, &i64), INT64_MIN);
    uint8_t u8 = 250; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_U8, &u8), 250);
    uint16_t u16 = 60000; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_U16, &u16), 60000);
    uint32_t u32 = 4000000000u; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_U32, &u32), 4000000000LL);
    bool flag = true; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_BOOL, &flag), 1);
    double f64 = 9.75; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_F64, &f64), 9);
    float f32 = -2.5f; ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_F32, &f32), -2);
    GrayString text = gray_string_lit("x"); ASSERT_EQ(gray_elem_to_i64(GRAY_ELEM_STRING, &text), 0);
    uint64_t big = UINT64_MAX;
    ASSERT(gray_elem_to_u64(GRAY_ELEM_U64, &big) == UINT64_MAX);
    ASSERT_EQ(gray_elem_to_u64(GRAY_ELEM_I64, &i64) == (uint64_t)INT64_MIN, 1);
    uint32_t small = 7; ASSERT(gray_elem_to_u64(GRAY_ELEM_U32, &small) == 7);
}

/* ===== gray test runner protocol ===== */

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s((name), (value))
#define unsetenv(name) _putenv_s((name), "")
#endif

static char runner_output[4096];

static const char *capture_runner_output(void (*emit)(void)) {
    fflush(stdout);
    int saved_descriptor = dup(fileno(stdout));
    FILE *sink = tmpfile();
    dup2(fileno(sink), fileno(stdout));
    emit();
    fflush(stdout);
    dup2(saved_descriptor, fileno(stdout));
    close(saved_descriptor);
    rewind(sink);
    size_t length = fread(runner_output, 1, sizeof(runner_output) - 1, sink);
    runner_output[length] = '\0';
    fclose(sink);
    return runner_output;
}

static void runner_passing_body(void) {}
static void runner_failing_body(void) { gray_test_fail("P0075", "body.gray", 42, "expected %d got %d", 1, 2); }
static void runner_failing_without_message(void) { gray_test_fail("P0075", "body.gray", 7, "%s", ""); }
static void runner_failing_without_location(void) { gray_test_fail("P0075", NULL, 0, "no location"); }
static void runner_failing_with_control_characters(void) { gray_test_fail("P0075", "body.gray", 1, "tab\there\nnewline\rreturn"); }
static void runner_panicking_body(void) { gray_panic_code("P0090", "range step cannot be zero"); }
static void runner_deep_failure(void) { gray_enter_func("a.gray", 1, "outer"); gray_enter_func("a.gray", 2, "inner"); gray_test_fail("P0075", "a.gray", 2, "deep"); }

static void emit_pass(void) { gray_test_run("passes", runner_passing_body, "file.gray", 3); }
static void emit_fail(void) { gray_test_run("fails", runner_failing_body, "file.gray", 3); }
static void emit_fail_without_message(void) { gray_test_run("quiet", runner_failing_without_message, "file.gray", 3); }
static void emit_fail_without_location(void) { gray_test_run("lost", runner_failing_without_location, "decl.gray", 9); }
static void emit_fail_with_control_characters(void) { gray_test_run("messy", runner_failing_with_control_characters, "file.gray", 3); }
static void emit_panic(void) { gray_test_run("panics", runner_panicking_body, "file.gray", 3); }
static void emit_deep_failure(void) { gray_test_run("deep", runner_deep_failure, "file.gray", 3); }

static void test_runner_passing_test_prints_a_pass_line(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    ASSERT_STR_EQ(capture_runner_output(emit_pass), "GRAYTEST PASS passes\n");
}

static void test_runner_failing_test_prints_name_location_and_message(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    ASSERT_STR_EQ(capture_runner_output(emit_fail), "GRAYTEST FAIL fails\tbody.gray:42\texpected 1 got 2\n");
}

static void test_runner_empty_message_becomes_test_failed(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    ASSERT_STR_EQ(capture_runner_output(emit_fail_without_message), "GRAYTEST FAIL quiet\tbody.gray:7\ttest failed\n");
}

static void test_runner_failure_without_a_location_uses_the_declaration(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    ASSERT_STR_EQ(capture_runner_output(emit_fail_without_location), "GRAYTEST FAIL lost\tdecl.gray:9\tno location\n");
}

static void test_runner_message_cannot_forge_protocol_delimiters(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    const char *output = capture_runner_output(emit_fail_with_control_characters);
    ASSERT_STR_EQ(output, "GRAYTEST FAIL messy\tbody.gray:1\ttab here newline return\n");
}

static void test_runner_runtime_panic_inside_a_test_is_a_failure(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    const char *output = capture_runner_output(emit_panic);
    ASSERT(strstr(output, "GRAYTEST FAIL panics\t") != NULL);
    ASSERT(strstr(output, "range step cannot be zero") != NULL);
}

static void test_runner_restores_call_depth_after_a_failure(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    int depth_before = gray_call_depth;
    capture_runner_output(emit_deep_failure);
    ASSERT_EQ(gray_call_depth, depth_before);
}

static void test_runner_nonce_prefixes_every_line(void) {
    setenv("GRAY_TEST_PROTOCOL_NONCE", "abc123:", 1);
    gray_test_begin();
    const char *passed = capture_runner_output(emit_pass);
    ASSERT_STR_EQ(passed, "abc123:GRAYTEST PASS passes\n");
    const char *failed = capture_runner_output(emit_fail);
    ASSERT_STR_EQ(failed, "abc123:GRAYTEST FAIL fails\tbody.gray:42\texpected 1 got 2\n");
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
}

static void test_runner_nonce_is_captured_once_per_begin(void) {
    setenv("GRAY_TEST_PROTOCOL_NONCE", "first:", 1);
    gray_test_begin();
    setenv("GRAY_TEST_PROTOCOL_NONCE", "forged:", 1);
    ASSERT_STR_EQ(capture_runner_output(emit_pass), "first:GRAYTEST PASS passes\n");
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
}

static int runner_exit_code;
static void emit_trailer(void) { runner_exit_code = gray_test_end(); }

static void test_runner_end_prints_counts_and_exit_code_zero_when_all_pass(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    capture_runner_output(emit_pass);
    capture_runner_output(emit_pass);
    ASSERT_STR_EQ(capture_runner_output(emit_trailer), "GRAYTEST DONE 2 0\n");
    ASSERT_EQ(runner_exit_code, 0);
}

static void test_runner_end_reports_failures_with_exit_code_one(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    capture_runner_output(emit_pass);
    capture_runner_output(emit_fail);
    capture_runner_output(emit_fail);
    ASSERT_STR_EQ(capture_runner_output(emit_trailer), "GRAYTEST DONE 1 2\n");
    ASSERT_EQ(runner_exit_code, 1);
}

static void test_runner_begin_resets_the_counts(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    capture_runner_output(emit_fail);
    gray_test_begin();
    ASSERT_STR_EQ(capture_runner_output(emit_trailer), "GRAYTEST DONE 0 0\n");
    ASSERT_EQ(runner_exit_code, 0);
}

static void test_runner_a_failure_does_not_stop_the_next_test(void) {
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    capture_runner_output(emit_fail);
    ASSERT_STR_EQ(capture_runner_output(emit_pass), "GRAYTEST PASS passes\n");
}

static void test_runner_active_flag_is_set_only_while_a_test_runs(void) {
    ASSERT(!gray_test_active);
    unsetenv("GRAY_TEST_PROTOCOL_NONCE");
    gray_test_begin();
    capture_runner_output(emit_fail);
    ASSERT(!gray_test_active);
    capture_runner_output(emit_pass);
    ASSERT(!gray_test_active);
}

int main(void) {
    arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);

    printf("\n");

    printf("--- GrayArena ---\n");
    RUN_TEST(test_gray_arena_create);
    RUN_TEST(test_gray_arena_alloc_basic);
    RUN_TEST(test_gray_arena_alloc_alignment);
    RUN_TEST(test_gray_arena_multi_block);
    RUN_TEST(test_gray_arena_oversized);
    RUN_TEST(test_gray_arena_reset);
    RUN_TEST(test_gray_arena_usage);
    RUN_TEST(test_gray_arena_scope_save_restore);
    RUN_TEST(test_gray_arena_nested_scopes);

    printf("--- GrayString ---\n");
    RUN_TEST(test_gray_string_lit);
    RUN_TEST(test_gray_string_new);
    RUN_TEST(test_gray_string_eq);
    RUN_TEST(test_gray_string_concat);
    RUN_TEST(test_gray_string_concat_n);
    RUN_TEST(test_gray_string_format);
    RUN_TEST(test_gray_c_string_dup);
    RUN_TEST(test_gray_string_empty);

    printf("--- GrayArray ---\n");
    RUN_TEST(test_gray_array_new);
    RUN_TEST(test_gray_array_push_and_get);
    RUN_TEST(test_gray_array_push_growth);
    RUN_TEST(test_gray_array_set);
    RUN_TEST(test_gray_array_from);
    RUN_TEST(test_gray_array_from_i64);
    RUN_TEST(test_gray_array_from_f64);
    RUN_TEST(test_gray_array_from_bool);
    RUN_TEST(test_gray_array_from_str);
    RUN_TEST(test_gray_array_copy);
    RUN_TEST(test_gray_array_multiple_types);
    RUN_TEST(test_gray_array_empty);

    printf("--- GrayMap ---\n");
    RUN_TEST(test_gray_map_new_kind);
    RUN_TEST(test_gray_map_set_get_int);
    RUN_TEST(test_gray_map_set_get_string);
    RUN_TEST(test_gray_map_has);
    RUN_TEST(test_gray_map_remove);
    RUN_TEST(test_gray_map_overwrite);
    RUN_TEST(test_gray_map_clear);
    RUN_TEST(test_gray_map_insertion_order);
    RUN_TEST(test_gray_map_order_after_remove);
    RUN_TEST(test_gray_map_copy_sees_no_duplicates);
    RUN_TEST(test_gray_map_order_churn_bounded);
    RUN_TEST(test_gray_map_rehash);
    RUN_TEST(test_gray_map_copy);
    RUN_TEST(test_gray_map_float_normalization);
    RUN_TEST(test_gray_map_empty);

    printf("--- atomics ---\n");
    RUN_TEST(test_atomic_load_store);
    RUN_TEST(test_atomic_add_returns_previous_value);
    RUN_TEST(test_atomic_sub_returns_previous_value);
    RUN_TEST(test_atomic_exchange_returns_previous_value);
    RUN_TEST(test_atomic_cas_succeeds_when_expected_matches);
    RUN_TEST(test_atomic_cas_fails_and_leaves_value_when_expected_differs);
    RUN_TEST(test_atomic_bitwise_return_previous_value);
    RUN_TEST(test_atomic_add_wraps_at_i64_max);
    RUN_TEST(test_atomic_sub_wraps_at_i64_min);
    RUN_TEST(test_atomic32_load_store);
    RUN_TEST(test_atomic32_add_and_sub_return_previous_value);
    RUN_TEST(test_atomic32_add_wraps_at_i32_max);
    RUN_TEST(test_atomic32_exchange_returns_previous_value);
    RUN_TEST(test_atomic32_cas);
    RUN_TEST(test_atomic32_operations_leave_neighbouring_words_untouched);
    RUN_TEST(test_atomic8_load_store);
    RUN_TEST(test_atomic8_exchange_returns_previous_value);
    RUN_TEST(test_atomic8_cas);
    RUN_TEST(test_atomic8_operations_leave_neighbouring_bytes_untouched);
    RUN_TEST(test_atomic_spinlock_trylock_and_unlock);
    RUN_TEST(test_atomic_spin_lock_acquires_free_lock);
    RUN_TEST(test_atomic_fence_returns);
    RUN_TEST(test_atomic_add_loses_no_updates_across_threads);
    RUN_TEST(test_atomic_spinlock_gives_mutual_exclusion_across_threads);

    printf("--- element helpers ---\n");
    RUN_TEST(test_elem_compare_orders_every_integer_kind);
    RUN_TEST(test_elem_compare_floats_with_nan_ordered_last);
    RUN_TEST(test_elem_compare_wide_integers);
    RUN_TEST(test_elem_compare_strings_bytewise_then_by_length);
    RUN_TEST(test_elem_compare_other_kinds_compare_bytes);
    RUN_TEST(test_elem_equal_floats_follow_ieee_equality);
    RUN_TEST(test_elem_equal_strings_and_raw_bytes);
    RUN_TEST(test_elem_hash_equal_values_hash_equal);
    RUN_TEST(test_elem_hash_strings_by_content);
    RUN_TEST(test_elem_hash_negative_zero_hashes_as_zero);
    RUN_TEST(test_elem_add_accumulates_each_kind);
    RUN_TEST(test_elem_add_wide_integers);
    RUN_TEST(test_elem_add_ignores_kinds_that_are_not_numbers);
    RUN_TEST(test_elem_to_double_every_kind);
    RUN_TEST(test_elem_to_double_wide_integers);
    RUN_TEST(test_elem_to_i64_and_to_u64);

    printf("--- gray test runner ---\n");
    RUN_TEST(test_runner_passing_test_prints_a_pass_line);
    RUN_TEST(test_runner_failing_test_prints_name_location_and_message);
    RUN_TEST(test_runner_empty_message_becomes_test_failed);
    RUN_TEST(test_runner_failure_without_a_location_uses_the_declaration);
    RUN_TEST(test_runner_message_cannot_forge_protocol_delimiters);
    RUN_TEST(test_runner_runtime_panic_inside_a_test_is_a_failure);
    RUN_TEST(test_runner_restores_call_depth_after_a_failure);
    RUN_TEST(test_runner_nonce_prefixes_every_line);
    RUN_TEST(test_runner_nonce_is_captured_once_per_begin);
    RUN_TEST(test_runner_end_prints_counts_and_exit_code_zero_when_all_pass);
    RUN_TEST(test_runner_end_reports_failures_with_exit_code_one);
    RUN_TEST(test_runner_begin_resets_the_counts);
    RUN_TEST(test_runner_a_failure_does_not_stop_the_next_test);
    RUN_TEST(test_runner_active_flag_is_set_only_while_a_test_runs);

    PRINT_RESULTS();
    gray_arena_destroy(arena, __FILE__, __LINE__);
    return _test_fail > 0 ? 1 : 0;
}
