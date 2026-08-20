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
    ASSERT_EQ(a->destroyed, false);
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
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0);
    ASSERT_EQ(arr.len, 0);
    ASSERT_GE(arr.cap, GRAY_ARRAY_MIN_CAP);
    ASSERT_EQ(arr.elem_size, (int32_t)sizeof(int64_t));
}

static void test_gray_array_push_and_get(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0);
    int64_t val = 42;
    GRAY_ARRAY_PUSH(arena, &arr, &val);
    ASSERT_EQ(arr.len, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 42);
}

static void test_gray_array_push_growth(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 2);
    for (int i = 0; i < 10; i++) {
        int64_t val = i * 10;
        GRAY_ARRAY_PUSH(arena, &arr, &val);
    }
    ASSERT_EQ(arr.len, 10);
    ASSERT_GE(arr.cap, 10);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 9), 90);
}

static void test_gray_array_set(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0);
    int64_t val = 100;
    GRAY_ARRAY_PUSH(arena, &arr, &val);
    GRAY_ARRAY_SET(arr, int64_t, 0, 200);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 200);
}

static void test_gray_array_from(void) {
    int64_t data[] = {10, 20, 30};
    GrayArray arr = gray_array_from(arena, data, sizeof(int64_t), 3);
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
    GRAY_ARRAY_SET(src, int64_t, 1, 999);
    ASSERT_EQ(GRAY_ARRAY_GET(dst, int64_t, 1), 20);
}

static void test_gray_array_multiple_types(void) {
    GrayArray arr = gray_array_new(arena, sizeof(double), 0);
    double v = 3.14;
    GRAY_ARRAY_PUSH(arena, &arr, &v);
    ASSERT_EQ(arr.len, 1);
    double got = GRAY_ARRAY_GET(arr, double, 0);
    ASSERT(got == 3.14);
}

static void test_gray_array_empty(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0);
    ASSERT_EQ(arr.len, 0);
}

/* ===== GrayMap Tests ===== */

static void test_gray_map_new(void) {
    GrayMap m = gray_map_new(arena, sizeof(int64_t), sizeof(int64_t), 0);
    ASSERT_EQ(m.count, 0);
    ASSERT_GE(m.capacity, GRAY_MAP_MIN_CAP);
}

static void test_gray_map_set_get_int(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t key = 1, val = 100;
    GRAY_MAP_SET(arena, &m, &key, &val);
    void *got = gray_map_get(&m, &key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(*(int64_t *)got, 100);
}

static void test_gray_map_set_get_string(void) {
    GrayMap m = gray_map_new(arena, sizeof(GrayString), sizeof(int64_t), 0);
    GrayString key = gray_string_lit("name");
    int64_t val = 42;
    gray_map_set_str(arena, &m, key, &val, __FILE__, __LINE__);
    void *got = gray_map_get_str(&m, key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(*(int64_t *)got, 42);
}

static void test_gray_map_has(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t key = 5, val = 50;
    ASSERT(!gray_map_has(&m, &key));
    GRAY_MAP_SET(arena, &m, &key, &val);
    ASSERT(gray_map_has(&m, &key));
}

static void test_gray_map_remove(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t key = 7, val = 70;
    GRAY_MAP_SET(arena, &m, &key, &val);
    ASSERT_EQ(m.count, 1);
    GRAY_MAP_REMOVE(&m, &key);
    ASSERT_EQ(m.count, 0);
    ASSERT(gray_map_get(&m, &key) == NULL);
}

static void test_gray_map_overwrite(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t key = 1, v1 = 10, v2 = 20;
    GRAY_MAP_SET(arena, &m, &key, &v1);
    GRAY_MAP_SET(arena, &m, &key, &v2);
    ASSERT_EQ(m.count, 1);
    ASSERT_EQ(*(int64_t *)gray_map_get(&m, &key), 20);
}

static void test_gray_map_clear(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t k1 = 1, v1 = 10, k2 = 2, v2 = 20;
    GRAY_MAP_SET(arena, &m, &k1, &v1);
    GRAY_MAP_SET(arena, &m, &k2, &v2);
    gray_map_clear(&m, __FILE__, __LINE__);
    ASSERT_EQ(m.count, 0);
}

static void test_gray_map_insertion_order(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
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
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t keys[] = {50, 40, 30, 20, 10};
    for (int i = 0; i < 5; i++) {
        int64_t v = keys[i] * 10;
        GRAY_MAP_SET(arena, &m, &keys[i], &v);
    }
    /* Drop the first and middle entries — the worst case for the old
     * linear-scan-and-memmove removal. */
    GRAY_MAP_REMOVE(&m, &keys[0]);
    GRAY_MAP_REMOVE(&m, &keys[2]);
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
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 8, GRAY_MAP_KEY_BYTES);
    for (int64_t i = 0; i < 6; i++) { int64_t v = i; GRAY_MAP_SET(arena, &m, &i, &v); }
    int64_t drop = 0;
    GRAY_MAP_REMOVE(&m, &drop);
    drop = 3;
    GRAY_MAP_REMOVE(&m, &drop);

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
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 8, GRAY_MAP_KEY_BYTES);
    for (int64_t i = 0; i < 32; i++) { int64_t v = i; GRAY_MAP_SET(arena, &m, &i, &v); }
    for (int round = 0; round < 500; round++) {
        int64_t key = round % 32;
        GRAY_MAP_REMOVE(&m, &key);
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
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 8, GRAY_MAP_KEY_BYTES);
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
    GrayMap src = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
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
    GrayMap m = gray_map_new_kind(arena, sizeof(double), sizeof(int64_t), 0, GRAY_MAP_KEY_F64);
    double pos_zero = 0.0;
    double neg_zero = -0.0;
    int64_t val = 42;
    gray_map_set(arena, &m, &pos_zero, &val, __FILE__, __LINE__);
    void *got = gray_map_get(&m, &neg_zero);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(*(int64_t *)got, 42);
}

static void test_gray_map_empty(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    ASSERT_EQ(m.count, 0);
    int64_t key = 999;
    ASSERT(gray_map_get(&m, &key) == NULL);
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
    RUN_TEST(test_gray_array_from_str);
    RUN_TEST(test_gray_array_copy);
    RUN_TEST(test_gray_array_multiple_types);
    RUN_TEST(test_gray_array_empty);

    printf("--- GrayMap ---\n");
    RUN_TEST(test_gray_map_new);
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

    PRINT_RESULTS();
    gray_arena_destroy(arena, __FILE__, __LINE__);
    return _test_fail > 0 ? 1 : 0;
}
