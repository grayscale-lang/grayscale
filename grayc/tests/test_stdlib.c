/*
 * test_stdlib.c — Unit tests for the Grayscale stdlib modules:
 * strings, arrays, maps, math, fmt, encoding, strconv.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include "../src/runtime/runtime.h"
#include "../src/runtime/array.h"
#include "../src/runtime/map.h"
#include "../src/stdlib/strings.h"
#include "../src/stdlib/arrays.h"
#include "../src/stdlib/maps.h"
#include "../src/stdlib/math.h"
#include "../src/stdlib/fmt.h"
#include "../src/stdlib/encoding.h"
#include "../src/stdlib/strconv.h"
#include <math.h>
#include <stdint.h>

static GrayArena *arena;

/* Helper: assert a GrayString equals a C string literal */
#define ASSERT_GRAY_STR(gs, expected) do { \
    GrayString _exp = gray_string_lit(expected); \
    if (!gray_string_eq((gs), _exp)) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: \"%.*s\" != \"%s\"\n", \
            __FILE__, __LINE__, (int)(gs).len, (gs).data, expected); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

/* Helper: assert two doubles are equal within tolerance */
#define ASSERT_FLOAT_EQ(a, b) do { \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) > 1e-9) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %g != %g\n", \
            __FILE__, __LINE__, _a, _b); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

/* ===== strings module ===== */

static void test_strings_to_upper(void) {
    GrayString r = gray_strings_to_upper(arena, gray_string_lit("hello"));
    ASSERT_GRAY_STR(r, "HELLO");
}

static void test_strings_to_upper_mixed(void) {
    GrayString r = gray_strings_to_upper(arena, gray_string_lit("Hello World 123"));
    ASSERT_GRAY_STR(r, "HELLO WORLD 123");
}

/* Separators were emitted on sight, so a trailing one appended a '_' that
 * nothing followed: "foo " became "foo_". Leading separators were already
 * dropped by the pos > 0 guard, which is where the asymmetry showed. */
static void test_strings_to_snake_case_separators(void) {
    static const struct { const char *in; const char *out; } cases[] = {
        { "-foo", "foo" },          { "foo-", "foo" },
        { " foo", "foo" },          { "foo ", "foo" },
        { "_foo", "foo" },          { "foo_", "foo" },
        { "foo bar ", "foo_bar" },  { "--foo--bar--", "foo_bar" },
        { "foo--bar", "foo_bar" },  { "User Name ", "user_name" },
        { "  a  b  ", "a_b" },      { "already_snake_case", "already_snake_case" },
        { "", "" },                 { "---", "" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        GrayString r = gray_strings_to_snake_case(arena, gray_string_lit(cases[i].in));
        ASSERT_GRAY_STR(r, cases[i].out);
    }
}

/* The acronym and camelCase boundary rules must survive the change, in every
 * position relative to a separator. */
static void test_strings_to_snake_case_boundaries(void) {
    static const struct { const char *in; const char *out; } cases[] = {
        { "HTTPServer", "http_server" },   { "HTTPServer ", "http_server" },
        { " HTTPServer", "http_server" },  { "parseHTTPResponse", "parse_http_response" },
        { "XMLHttpRequest", "xml_http_request" },
        { "fooBar", "foo_bar" },           { "foo_Bar", "foo_bar" },
        { "foo2Bar", "foo2_bar" },         { "A", "a" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        GrayString r = gray_strings_to_snake_case(arena, gray_string_lit(cases[i].in));
        ASSERT_GRAY_STR(r, cases[i].out);
    }
}

static void test_strings_to_lower(void) {
    GrayString r = gray_strings_to_lower(arena, gray_string_lit("HELLO"));
    ASSERT_GRAY_STR(r, "hello");
}

static void test_strings_trim(void) {
    GrayString r = gray_strings_trim(arena, gray_string_lit("  hello  "));
    ASSERT_GRAY_STR(r, "hello");
}

static void test_strings_trim_tabs_newlines(void) {
    GrayString r = gray_strings_trim(arena, gray_string_lit("\t\n hello \n\t"));
    ASSERT_GRAY_STR(r, "hello");
}

static void test_strings_trim_left(void) {
    GrayString r = gray_strings_trim_left(arena, gray_string_lit("  hello  "));
    ASSERT_GRAY_STR(r, "hello  ");
}

static void test_strings_trim_right(void) {
    GrayString r = gray_strings_trim_right(arena, gray_string_lit("  hello  "));
    ASSERT_GRAY_STR(r, "  hello");
}

static void test_strings_contains(void) {
    ASSERT(gray_strings_contains(gray_string_lit("hello world"), gray_string_lit("world")));
    ASSERT(!gray_strings_contains(gray_string_lit("hello world"), gray_string_lit("xyz")));
}

static void test_strings_contains_empty(void) {
    ASSERT(gray_strings_contains(gray_string_lit("hello"), gray_string_lit("")));
}

static void test_strings_starts_with(void) {
    ASSERT(gray_strings_starts_with(gray_string_lit("hello"), gray_string_lit("hel")));
    ASSERT(!gray_strings_starts_with(gray_string_lit("hello"), gray_string_lit("world")));
}

static void test_strings_ends_with(void) {
    ASSERT(gray_strings_ends_with(gray_string_lit("hello"), gray_string_lit("llo")));
    ASSERT(!gray_strings_ends_with(gray_string_lit("hello"), gray_string_lit("hel")));
}

static void test_strings_index_of(void) {
    ASSERT_EQ(gray_strings_index_of(gray_string_lit("hello world"), gray_string_lit("world")), 6);
    ASSERT_EQ(gray_strings_index_of(gray_string_lit("hello"), gray_string_lit("xyz")), -1);
    ASSERT_EQ(gray_strings_index_of(gray_string_lit("hello"), gray_string_lit("")), 0);
}

static void test_strings_last_index_of(void) {
    ASSERT_EQ(gray_strings_last_index_of(gray_string_lit("hello hello"), gray_string_lit("hello")), 6);
    ASSERT_EQ(gray_strings_last_index_of(gray_string_lit("hello"), gray_string_lit("xyz")), -1);
}

static void test_strings_count(void) {
    ASSERT_EQ(gray_strings_count(gray_string_lit("banana"), gray_string_lit("a")), 3);
    ASSERT_EQ(gray_strings_count(gray_string_lit("aaa"), gray_string_lit("aa")), 1);
    ASSERT_EQ(gray_strings_count(gray_string_lit("hello"), gray_string_lit("xyz")), 0);
}

static void test_strings_is_empty(void) {
    ASSERT(gray_strings_is_empty(gray_string_lit("")));
    ASSERT(!gray_strings_is_empty(gray_string_lit("x")));
}

static void test_strings_remove_prefix(void) {
    GrayString r = gray_strings_remove_prefix(arena, gray_string_lit("hello world"), gray_string_lit("hello "));
    ASSERT_GRAY_STR(r, "world");
}

static void test_strings_remove_prefix_no_match(void) {
    GrayString s = gray_string_lit("hello");
    GrayString r = gray_strings_remove_prefix(arena, s, gray_string_lit("xyz"));
    ASSERT(gray_string_eq(r, s));
}

static void test_strings_remove_suffix(void) {
    GrayString r = gray_strings_remove_suffix(arena, gray_string_lit("hello world"), gray_string_lit(" world"));
    ASSERT_GRAY_STR(r, "hello");
}

static void test_strings_replace(void) {
    GrayString r = gray_strings_replace(arena, gray_string_lit("hello world"), gray_string_lit("world"), gray_string_lit("grayscale"));
    ASSERT_GRAY_STR(r, "hello grayscale");
}

static void test_strings_replace_multiple(void) {
    GrayString r = gray_strings_replace(arena, gray_string_lit("aXaXa"), gray_string_lit("X"), gray_string_lit("--"));
    ASSERT_GRAY_STR(r, "a--a--a");
}

static void test_strings_replace_no_match(void) {
    GrayString s = gray_string_lit("hello");
    GrayString r = gray_strings_replace(arena, s, gray_string_lit("xyz"), gray_string_lit("abc"));
    ASSERT(gray_string_eq(r, s));
}

static void test_strings_repeat(void) {
    GrayString r = gray_strings_repeat(arena, gray_string_lit("ab"), 3);
    ASSERT_GRAY_STR(r, "ababab");
}

static void test_strings_repeat_zero(void) {
    GrayString r = gray_strings_repeat(arena, gray_string_lit("hello"), 0);
    ASSERT_EQ(r.len, 0);
}

static void test_strings_reverse(void) {
    GrayString r = gray_strings_reverse(arena, gray_string_lit("hello"));
    ASSERT_GRAY_STR(r, "olleh");
}

static void test_strings_reverse_empty(void) {
    GrayString r = gray_strings_reverse(arena, gray_string_lit(""));
    ASSERT_EQ(r.len, 0);
}

static void test_strings_slice(void) {
    GrayString r = gray_strings_slice(arena, gray_string_lit("hello world"), 6, 11);
    ASSERT_GRAY_STR(r, "world");
}

static void test_strings_slice_clamped(void) {
    GrayString r = gray_strings_slice(arena, gray_string_lit("hello"), -5, 100);
    ASSERT_GRAY_STR(r, "hello");
}

static void test_strings_slice_empty(void) {
    GrayString r = gray_strings_slice(arena, gray_string_lit("hello"), 3, 2);
    ASSERT_EQ(r.len, 0);
}

static void test_strings_split(void) {
    GrayArray parts = gray_strings_split(arena, gray_string_lit("a,b,c"), gray_string_lit(","));
    ASSERT_EQ(parts.len, 3);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(parts, GrayString, 0), gray_string_lit("a")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(parts, GrayString, 1), gray_string_lit("b")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(parts, GrayString, 2), gray_string_lit("c")));
}

static void test_strings_split_no_match(void) {
    GrayArray parts = gray_strings_split(arena, gray_string_lit("hello"), gray_string_lit(","));
    ASSERT_EQ(parts.len, 1);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(parts, GrayString, 0), gray_string_lit("hello")));
}

static void test_strings_join(void) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 3);
    GrayString a = gray_string_lit("a");
    GrayString b = gray_string_lit("b");
    GrayString c = gray_string_lit("c");
    GRAY_ARRAY_PUSH(arena, &arr, &a);
    GRAY_ARRAY_PUSH(arena, &arr, &b);
    GRAY_ARRAY_PUSH(arena, &arr, &c);
    GrayString r = gray_strings_join(arena, arr, gray_string_lit("-"));
    ASSERT_GRAY_STR(r, "a-b-c");
}

static void test_strings_join_empty_array(void) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 0);
    GrayString r = gray_strings_join(arena, arr, gray_string_lit(","));
    ASSERT_EQ(r.len, 0);
}

static void test_strings_char_at(void) {
    ASSERT_EQ(gray_strings_char_at(gray_string_lit("hello"), 0), 'h');
    ASSERT_EQ(gray_strings_char_at(gray_string_lit("hello"), 4), 'o');
}

static void test_strings_to_chars(void) {
    GrayArray chars = gray_strings_to_chars(arena, gray_string_lit("abc"));
    ASSERT_EQ(chars.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(chars, int32_t, 0), 'a');
    ASSERT_EQ(GRAY_ARRAY_GET(chars, int32_t, 1), 'b');
    ASSERT_EQ(GRAY_ARRAY_GET(chars, int32_t, 2), 'c');
}

static void test_strings_from_chars(void) {
    GrayArray chars = gray_array_new(arena, sizeof(int32_t), 3);
    int32_t a = 'h', b = 'i';
    GRAY_ARRAY_PUSH(arena, &chars, &a);
    GRAY_ARRAY_PUSH(arena, &chars, &b);
    GrayString r = gray_strings_from_chars(arena, &chars);
    ASSERT_GRAY_STR(r, "hi");
}

static void test_strings_classification(void) {
    ASSERT(gray_strings_is_alpha('a'));
    ASSERT(gray_strings_is_alpha('Z'));
    ASSERT(!gray_strings_is_alpha('1'));
    ASSERT(gray_strings_is_digit('5'));
    ASSERT(!gray_strings_is_digit('a'));
    ASSERT(gray_strings_is_alnum('a'));
    ASSERT(gray_strings_is_alnum('3'));
    ASSERT(!gray_strings_is_alnum('!'));
    ASSERT(gray_strings_is_whitespace(' '));
    ASSERT(gray_strings_is_whitespace('\t'));
    ASSERT(!gray_strings_is_whitespace('a'));
    ASSERT(gray_strings_is_upper('A'));
    ASSERT(!gray_strings_is_upper('a'));
    ASSERT(gray_strings_is_lower('a'));
    ASSERT(!gray_strings_is_lower('A'));
}

/* ===== arrays module ===== */

static void test_arrays_append(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    int64_t v = 4;
    gray_arrays_append(arena, &arr, &v);
    ASSERT_EQ(arr.len, 4);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 3), 4);
}

static void test_arrays_insert_at(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    int64_t v = 99;
    gray_arrays_insert_at(arena, &arr, 1, &v);
    ASSERT_EQ(arr.len, 4);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 99);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 2), 2);
}

static void test_arrays_prepend(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 2, 3, 4);
    int64_t v = 1;
    gray_arrays_prepend(arena, &arr, &v);
    ASSERT_EQ(arr.len, 4);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
}

static void test_arrays_remove_at(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    gray_arrays_remove_at(&arr, 1);
    ASSERT_EQ(arr.len, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 10);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 30);
}

static void test_arrays_remove_int(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 2);
    gray_arrays_remove_int(&arr, 2);
    ASSERT_EQ(arr.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 3);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 2), 2);
}

static void test_arrays_clear(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    gray_arrays_clear(&arr);
    ASSERT_EQ(arr.len, 0);
}

static void test_arrays_fill(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0);
    int64_t v = 7;
    gray_arrays_fill(arena, &arr, &v, 5);
    ASSERT_EQ(arr.len, 5);
    for (int i = 0; i < 5; i++)
        ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, i), 7);
}

static void test_arrays_get_first(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    ASSERT_EQ(gray_arrays_get_first(&arr), 10);
}

static void test_arrays_get_last(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    ASSERT_EQ(gray_arrays_get_last(&arr), 30);
}

static void test_arrays_remove_first(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    int64_t val = gray_arrays_remove_first(&arr);
    ASSERT_EQ(val, 10);
    ASSERT_EQ(arr.len, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 20);
}

static void test_arrays_remove_last(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    int64_t val = gray_arrays_remove_last(&arr);
    ASSERT_EQ(val, 30);
    ASSERT_EQ(arr.len, 2);
}

static void test_arrays_is_empty(void) {
    GrayArray empty = gray_array_new(arena, sizeof(int64_t), 0);
    GrayArray nonempty = GRAY_ARRAY_FROM_I64(arena, 1);
    ASSERT(gray_arrays_is_empty(&empty));
    ASSERT(!gray_arrays_is_empty(&nonempty));
}

static void test_arrays_contains_int(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    ASSERT(gray_arrays_contains_int(&arr, 2));
    ASSERT(!gray_arrays_contains_int(&arr, 99));
}

static void test_arrays_contains_str(void) {
    GrayArray arr = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("a"), gray_string_lit("b"));
    ASSERT(gray_arrays_contains_str(&arr, gray_string_lit("a")));
    ASSERT(!gray_arrays_contains_str(&arr, gray_string_lit("c")));
}

static void test_arrays_index_of_int(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    ASSERT_EQ(gray_arrays_index_of_int(&arr, 20), 1);
    ASSERT_EQ(gray_arrays_index_of_int(&arr, 99), -1);
}

static void test_arrays_count(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 2, 3, 2);
    ASSERT_EQ(gray_arrays_count(&arr, 2), 3);
    ASSERT_EQ(gray_arrays_count(&arr, 99), 0);
}

static void test_arrays_is_equal_prim(void) {
    GrayArray a = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray b = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray c = GRAY_ARRAY_FROM_I64(arena, 1, 2, 4);
    ASSERT(gray_arrays_is_equal_prim(&a, &b));
    ASSERT(!gray_arrays_is_equal_prim(&a, &c));
}

static void test_arrays_is_equal_str(void) {
    GrayArray a = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("x"), gray_string_lit("y"));
    GrayArray b = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("x"), gray_string_lit("y"));
    GrayArray c = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("x"), gray_string_lit("z"));
    ASSERT(gray_arrays_is_equal_str(&a, &b));
    ASSERT(!gray_arrays_is_equal_str(&a, &c));
}

static void test_arrays_reverse(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray rev = gray_arrays_reverse(arena, &arr);
    ASSERT_EQ(rev.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(rev, int64_t, 0), 3);
    ASSERT_EQ(GRAY_ARRAY_GET(rev, int64_t, 1), 2);
    ASSERT_EQ(GRAY_ARRAY_GET(rev, int64_t, 2), 1);
}

static void test_arrays_slice(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30, 40);
    GrayArray s = gray_arrays_slice(arena, &arr, 1, 3);
    ASSERT_EQ(s.len, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(s, int64_t, 0), 20);
    ASSERT_EQ(GRAY_ARRAY_GET(s, int64_t, 1), 30);
}

static void test_arrays_concat(void) {
    GrayArray a = GRAY_ARRAY_FROM_I64(arena, 1, 2);
    GrayArray b = GRAY_ARRAY_FROM_I64(arena, 3, 4);
    GrayArray c = gray_arrays_concat(arena, &a, &b);
    ASSERT_EQ(c.len, 4);
    ASSERT_EQ(GRAY_ARRAY_GET(c, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(c, int64_t, 3), 4);
}

static void test_arrays_deduplicate(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 2, 3, 1);
    GrayArray d = gray_arrays_deduplicate(arena, &arr);
    ASSERT_EQ(d.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(d, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(d, int64_t, 1), 2);
    ASSERT_EQ(GRAY_ARRAY_GET(d, int64_t, 2), 3);
}

static void test_arrays_flatten(void) {
    GrayArray inner1 = GRAY_ARRAY_FROM_I64(arena, 1, 2);
    GrayArray inner2 = GRAY_ARRAY_FROM_I64(arena, 3, 4);
    GrayArray outer = gray_array_new(arena, sizeof(GrayArray), 2);
    GRAY_ARRAY_PUSH(arena, &outer, &inner1);
    GRAY_ARRAY_PUSH(arena, &outer, &inner2);
    GrayArray flat = gray_arrays_flatten(arena, &outer);
    ASSERT_EQ(flat.len, 4);
    ASSERT_EQ(GRAY_ARRAY_GET(flat, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(flat, int64_t, 3), 4);
}

static void test_arrays_split_every(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5);
    GrayArray chunks = gray_arrays_split_every(arena, &arr, 2);
    ASSERT_EQ(chunks.len, 3);
    GrayArray c0 = GRAY_ARRAY_GET(chunks, GrayArray, 0);
    ASSERT_EQ(c0.len, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(c0, int64_t, 0), 1);
    GrayArray c2 = GRAY_ARRAY_GET(chunks, GrayArray, 2);
    ASSERT_EQ(c2.len, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(c2, int64_t, 0), 5);
}

static void test_arrays_pair(void) {
    GrayArray a = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray b = GRAY_ARRAY_FROM_I64(arena, 10, 20);
    GrayArray pairs = gray_arrays_pair(arena, &a, &b);
    ASSERT_EQ(pairs.len, 2);
    GrayArray p0 = GRAY_ARRAY_GET(pairs, GrayArray, 0);
    ASSERT_EQ(GRAY_ARRAY_GET(p0, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(p0, int64_t, 1), 10);
}

static void test_arrays_get_sum(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4);
    ASSERT_EQ(gray_arrays_get_sum(&arr), 10);
}

static void test_arrays_get_min(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    ASSERT_EQ(gray_arrays_get_min(&arr), 1);
}

static void test_arrays_get_max(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    ASSERT_EQ(gray_arrays_get_max(&arr), 5);
}

static void test_arrays_sort_asc(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    gray_arrays_sort_asc(&arr);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 4), 5);
}

static void test_arrays_sort_desc(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    gray_arrays_sort_desc(&arr);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 5);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 4), 1);
}

static void test_arrays_sort_asc_str(void) {
    GrayArray arr = GRAY_ARRAY_FROM_STR(arena,
        gray_string_lit("cherry"),
        gray_string_lit("apple"),
        gray_string_lit("banana"));
    gray_arrays_sort_asc_str(&arr);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(arr, GrayString, 0), gray_string_lit("apple")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(arr, GrayString, 1), gray_string_lit("banana")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(arr, GrayString, 2), gray_string_lit("cherry")));
}

/* ===== maps module ===== */

static void test_maps_get_keys(void) {
    GrayMap m = gray_map_new(arena, sizeof(GrayString), sizeof(int64_t), 0);
    GrayString k1 = gray_string_lit("a");
    GrayString k2 = gray_string_lit("b");
    int64_t v1 = 1, v2 = 2;
    gray_map_set_str(arena, &m, k1, &v1, __FILE__, __LINE__);
    gray_map_set_str(arena, &m, k2, &v2, __FILE__, __LINE__);
    GrayArray keys = gray_maps_get_keys(arena, &m);
    ASSERT_EQ(keys.len, 2);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(keys, GrayString, 0), gray_string_lit("a")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(keys, GrayString, 1), gray_string_lit("b")));
}

static void test_maps_get_values(void) {
    GrayMap m = gray_map_new(arena, sizeof(GrayString), sizeof(int64_t), 0);
    GrayString k1 = gray_string_lit("x");
    int64_t v1 = 42;
    gray_map_set_str(arena, &m, k1, &v1, __FILE__, __LINE__);
    GrayArray vals = gray_maps_get_values(arena, &m);
    ASSERT_EQ(vals.len, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(vals, int64_t, 0), 42);
}

static void test_maps_has_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t k = 5, v = 50;
    ASSERT(!gray_maps_has_key(&m, &k));
    GRAY_MAP_SET(arena, &m, &k, &v);
    ASSERT(gray_maps_has_key(&m, &k));
}

static void test_maps_is_empty(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    ASSERT(gray_maps_is_empty(&m));
    int64_t k = 1, v = 10;
    GRAY_MAP_SET(arena, &m, &k, &v);
    ASSERT(!gray_maps_is_empty(&m));
}

static void test_maps_contains_value(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t k1 = 1, v1 = 100, k2 = 2, v2 = 200;
    GRAY_MAP_SET(arena, &m, &k1, &v1);
    GRAY_MAP_SET(arena, &m, &k2, &v2);
    ASSERT(gray_maps_contains_value(&m, &v1));
    int64_t v3 = 999;
    ASSERT(!gray_maps_contains_value(&m, &v3));
}

static void test_maps_is_equal(void) {
    GrayMap a = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    GrayMap b = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t k1 = 1, v1 = 10, k2 = 2, v2 = 20;
    GRAY_MAP_SET(arena, &a, &k1, &v1);
    GRAY_MAP_SET(arena, &a, &k2, &v2);
    GRAY_MAP_SET(arena, &b, &k1, &v1);
    GRAY_MAP_SET(arena, &b, &k2, &v2);
    ASSERT(gray_maps_is_equal(&a, &b, false, false));
}

static void test_maps_is_equal_str_keys(void) {
    GrayMap a = gray_map_new(arena, sizeof(GrayString), sizeof(int64_t), 0);
    GrayMap b = gray_map_new(arena, sizeof(GrayString), sizeof(int64_t), 0);
    GrayString k = gray_string_lit("key");
    int64_t v = 42;
    gray_map_set_str(arena, &a, k, &v, __FILE__, __LINE__);
    gray_map_set_str(arena, &b, k, &v, __FILE__, __LINE__);
    ASSERT(gray_maps_is_equal(&a, &b, true, false));
}

static void test_maps_is_equal_different(void) {
    GrayMap a = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    GrayMap b = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t k1 = 1, v1 = 10, v2 = 99;
    GRAY_MAP_SET(arena, &a, &k1, &v1);
    GRAY_MAP_SET(arena, &b, &k1, &v2);
    ASSERT(!gray_maps_is_equal(&a, &b, false, false));
}

static void test_maps_merge(void) {
    GrayMap m1 = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    GrayMap m2 = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_MAP_KEY_BYTES);
    int64_t k1 = 1, v1 = 10, k2 = 2, v2 = 20, k3 = 1, v3 = 99;
    GRAY_MAP_SET(arena, &m1, &k1, &v1);
    GRAY_MAP_SET(arena, &m2, &k2, &v2);
    GRAY_MAP_SET(arena, &m2, &k3, &v3);
    GrayMap merged = gray_maps_merge(arena, &m1, &m2);
    ASSERT_EQ(merged.count, 2);
    ASSERT_EQ(*(int64_t *)gray_map_get(&merged, &k1), 99);
    ASSERT_EQ(*(int64_t *)gray_map_get(&merged, &k2), 20);
}

/* ===== math module ===== */

static void test_math_abs(void) {
    ASSERT_EQ(gray_math_abs_int(-5), 5);
    ASSERT_EQ(gray_math_abs_int(5), 5);
    ASSERT_EQ(gray_math_abs_int(0), 0);
    ASSERT_FLOAT_EQ(gray_math_abs_float(-3.14), 3.14);
}

static void test_math_sign(void) {
    ASSERT_EQ(gray_math_sign(-7), -1);
    ASSERT_EQ(gray_math_sign(0), 0);
    ASSERT_EQ(gray_math_sign(3), 1);
}

static void test_math_min_max(void) {
    ASSERT_EQ(gray_math_min_int(3, 7), 3);
    ASSERT_EQ(gray_math_max_int(3, 7), 7);
    ASSERT_FLOAT_EQ(gray_math_min_float(1.5, 2.5), 1.5);
    ASSERT_FLOAT_EQ(gray_math_max_float(1.5, 2.5), 2.5);
}

static void test_math_clamp(void) {
    ASSERT_EQ(gray_math_clamp_int(15, 1, 10), 10);
    ASSERT_EQ(gray_math_clamp_int(-5, 1, 10), 1);
    ASSERT_EQ(gray_math_clamp_int(5, 1, 10), 5);
    ASSERT_FLOAT_EQ(gray_math_clamp_float(1.5, 0.0, 1.0), 1.0);
}

static void test_math_rounding(void) {
    ASSERT_FLOAT_EQ(gray_math_floor(3.7), 3.0);
    ASSERT_FLOAT_EQ(gray_math_floor(-1.2), -2.0);
    ASSERT_FLOAT_EQ(gray_math_ceil(3.2), 4.0);
    ASSERT_FLOAT_EQ(gray_math_ceil(-1.8), -1.0);
    ASSERT_FLOAT_EQ(gray_math_round(3.5), 4.0);
    ASSERT_FLOAT_EQ(gray_math_round(3.4), 3.0);
    ASSERT_FLOAT_EQ(gray_math_trunc(3.9), 3.0);
    ASSERT_FLOAT_EQ(gray_math_trunc(-3.9), -3.0);
}

static void test_math_powers(void) {
    ASSERT_FLOAT_EQ(gray_math_pow(2.0, 10.0), 1024.0);
    ASSERT_FLOAT_EQ(gray_math_sqrt(9.0), 3.0);
    ASSERT_FLOAT_EQ(gray_math_cbrt(27.0), 3.0);
    ASSERT_FLOAT_EQ(gray_math_hypot(3.0, 4.0), 5.0);
}

static void test_math_exp_log(void) {
    ASSERT_FLOAT_EQ(gray_math_exp(0.0), 1.0);
    ASSERT_FLOAT_EQ(gray_math_exp2(3.0), 8.0);
    ASSERT_FLOAT_EQ(gray_math_log2(8.0), 3.0);
    ASSERT_FLOAT_EQ(gray_math_log10(100.0), 2.0);
    ASSERT_FLOAT_EQ(gray_math_log_base(8.0, 2.0), 3.0);
}

static void test_math_trig(void) {
    ASSERT_FLOAT_EQ(gray_math_sin(0.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_cos(0.0), 1.0);
    ASSERT_FLOAT_EQ(gray_math_tan(0.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_asin(0.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_acos(1.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_atan(0.0), 0.0);
}

static void test_math_deg_rad(void) {
    ASSERT_FLOAT_EQ(gray_math_deg_to_rad(180.0), 3.14159265358979323846);
    ASSERT_FLOAT_EQ(gray_math_rad_to_deg(3.14159265358979323846), 180.0);
}

static void test_math_properties(void) {
    ASSERT(gray_math_is_even(4));
    ASSERT(!gray_math_is_even(3));
    ASSERT(gray_math_is_odd(3));
    ASSERT(!gray_math_is_odd(4));
    ASSERT(gray_math_is_infinite(1.0 / 0.0));
    ASSERT(!gray_math_is_infinite(1.0));
    ASSERT(gray_math_is_nan(0.0 / 0.0));
    ASSERT(!gray_math_is_nan(1.0));
    ASSERT(gray_math_is_finite(3.14));
    ASSERT(!gray_math_is_finite(1.0 / 0.0));
}

static void test_math_factorial(void) {
    ASSERT_EQ(gray_math_factorial(0), 1);
    ASSERT_EQ(gray_math_factorial(1), 1);
    ASSERT_EQ(gray_math_factorial(5), 120);
    ASSERT_EQ(gray_math_factorial(10), 3628800);
}

static void test_math_gcd(void) {
    ASSERT_EQ(gray_math_gcd(12, 8), 4);
    ASSERT_EQ(gray_math_gcd(7, 13), 1);
    ASSERT_EQ(gray_math_gcd(0, 5), 5);
    ASSERT_EQ(gray_math_gcd(-12, 8), 4);
}

static void test_math_lcm(void) {
    ASSERT_EQ(gray_math_lcm(4, 6), 12);
    ASSERT_EQ(gray_math_lcm(0, 5), 0);
    ASSERT_EQ(gray_math_lcm(7, 13), 91);
}

static void test_math_is_prime(void) {
    ASSERT(!gray_math_is_prime(0));
    ASSERT(!gray_math_is_prime(1));
    ASSERT(gray_math_is_prime(2));
    ASSERT(gray_math_is_prime(3));
    ASSERT(!gray_math_is_prime(4));
    ASSERT(gray_math_is_prime(7));
    ASSERT(!gray_math_is_prime(9));
    ASSERT(gray_math_is_prime(97));
}

static void test_math_lerp(void) {
    ASSERT_FLOAT_EQ(gray_math_lerp(0.0, 10.0, 0.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_lerp(0.0, 10.0, 1.0), 10.0);
    ASSERT_FLOAT_EQ(gray_math_lerp(0.0, 10.0, 0.5), 5.0);
}

static void test_math_distance(void) {
    ASSERT_FLOAT_EQ(gray_math_distance(0.0, 0.0, 3.0, 4.0), 5.0);
    ASSERT_FLOAT_EQ(gray_math_distance(1.0, 1.0, 1.0, 1.0), 0.0);
}

/* ===== fmt module ===== */

static void test_fmt_pad_left(void) {
    GrayString r = gray_fmt_pad_left(arena, gray_string_lit("42"), 5, '0');
    ASSERT_GRAY_STR(r, "00042");
}

static void test_fmt_pad_left_no_pad(void) {
    GrayString s = gray_string_lit("hello");
    GrayString r = gray_fmt_pad_left(arena, s, 3, '.');
    ASSERT(gray_string_eq(r, s));
}

static void test_fmt_pad_right(void) {
    GrayString r = gray_fmt_pad_right(arena, gray_string_lit("hi"), 6, '.');
    ASSERT_GRAY_STR(r, "hi....");
}

static void test_fmt_center(void) {
    GrayString r = gray_fmt_center(arena, gray_string_lit("hi"), 8, '-');
    ASSERT_GRAY_STR(r, "---hi---");
}

static void test_fmt_center_odd(void) {
    GrayString r = gray_fmt_center(arena, gray_string_lit("hi"), 7, '-');
    ASSERT_GRAY_STR(r, "--hi---");
}

static void test_fmt_int_to_hex(void) {
    ASSERT_GRAY_STR(gray_fmt_int_to_hex(arena, 255), "ff");
    ASSERT_GRAY_STR(gray_fmt_int_to_hex(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_int_to_hex(arena, 16), "10");
}

static void test_fmt_int_to_binary(void) {
    ASSERT_GRAY_STR(gray_fmt_int_to_binary(arena, 10), "1010");
    ASSERT_GRAY_STR(gray_fmt_int_to_binary(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_int_to_binary(arena, 1), "1");
}

static void test_fmt_int_to_octal(void) {
    ASSERT_GRAY_STR(gray_fmt_int_to_octal(arena, 8), "10");
    ASSERT_GRAY_STR(gray_fmt_int_to_octal(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_int_to_octal(arena, 255), "377");
}

static void test_fmt_float_fixed(void) {
    ASSERT_GRAY_STR(gray_fmt_float_fixed(arena, 3.14159, 2), "3.14");
    ASSERT_GRAY_STR(gray_fmt_float_fixed(arena, 1.0, 0), "1");
}

static void test_fmt_float_sci(void) {
    GrayString r = gray_fmt_float_sci(arena, 1234.5);
    /* Output is platform-dependent in exponent width, just verify it starts right */
    ASSERT(gray_strings_starts_with(r, gray_string_lit("1.2345")));
    ASSERT(gray_strings_contains(r, gray_string_lit("e+")));
}

/* ===== encoding module ===== */

static void test_encoding_base64_encode(void) {
    ASSERT_GRAY_STR(gray_encoding_base64_encode(arena, gray_string_lit("Hello")), "SGVsbG8=");
    ASSERT_GRAY_STR(gray_encoding_base64_encode(arena, gray_string_lit("Hi")), "SGk=");
    ASSERT_GRAY_STR(gray_encoding_base64_encode(arena, gray_string_lit("Man")), "TWFu");
}

static void test_encoding_base64_decode(void) {
    ASSERT_GRAY_STR(gray_encoding_base64_decode(arena, gray_string_lit("SGVsbG8=")), "Hello");
    ASSERT_GRAY_STR(gray_encoding_base64_decode(arena, gray_string_lit("SGk=")), "Hi");
    ASSERT_GRAY_STR(gray_encoding_base64_decode(arena, gray_string_lit("TWFu")), "Man");
}

static void test_encoding_base64_roundtrip(void) {
    GrayString original = gray_string_lit("Grayscale is awesome!");
    GrayString encoded = gray_encoding_base64_encode(arena, original);
    GrayString decoded = gray_encoding_base64_decode(arena, encoded);
    ASSERT(gray_string_eq(decoded, original));
}

static void test_encoding_hex_encode(void) {
    ASSERT_GRAY_STR(gray_encoding_hex_encode(arena, gray_string_lit("Hi")), "4869");
    ASSERT_GRAY_STR(gray_encoding_hex_encode(arena, gray_string_lit("AB")), "4142");
}

static void test_encoding_hex_decode(void) {
    ASSERT_GRAY_STR(gray_encoding_hex_decode(arena, gray_string_lit("4869")), "Hi");
    ASSERT_GRAY_STR(gray_encoding_hex_decode(arena, gray_string_lit("4142")), "AB");
}

static void test_encoding_hex_roundtrip(void) {
    GrayString original = gray_string_lit("test123");
    GrayString encoded = gray_encoding_hex_encode(arena, original);
    GrayString decoded = gray_encoding_hex_decode(arena, encoded);
    ASSERT(gray_string_eq(decoded, original));
}

static void test_encoding_url_encode(void) {
    ASSERT_GRAY_STR(gray_encoding_url_encode(arena, gray_string_lit("hello world")), "hello%20world");
    ASSERT_GRAY_STR(gray_encoding_url_encode(arena, gray_string_lit("a+b=c")), "a%2Bb%3Dc");
}

static void test_encoding_url_decode(void) {
    ASSERT_GRAY_STR(gray_encoding_url_decode(arena, gray_string_lit("hello%20world")), "hello world");
    ASSERT_GRAY_STR(gray_encoding_url_decode(arena, gray_string_lit("a+b")), "a b");
}

static void test_encoding_url_roundtrip(void) {
    GrayString original = gray_string_lit("hello world & friends!");
    GrayString encoded = gray_encoding_url_encode(arena, original);
    GrayString decoded = gray_encoding_url_decode(arena, encoded);
    ASSERT(gray_string_eq(decoded, original));
}

/* ===== strconv module ===== */

static void test_strconv_to_int(void) {
    ASSERT_EQ(gray_strconv_to_int(gray_string_lit("42"), 10), 42);
    ASSERT_EQ(gray_strconv_to_int(gray_string_lit("-100"), 10), -100);
    ASSERT_EQ(gray_strconv_to_int(gray_string_lit("ff"), 16), 255);
    ASSERT_EQ(gray_strconv_to_int(gray_string_lit("101"), 2), 5);
}

static void test_strconv_to_uint(void) {
    ASSERT_EQ((int64_t)gray_strconv_to_uint(gray_string_lit("255"), 10), 255);
    ASSERT_EQ((int64_t)gray_strconv_to_uint(gray_string_lit("ff"), 16), 255);
}

static void test_strconv_to_float(void) {
    ASSERT_FLOAT_EQ(gray_strconv_to_float(gray_string_lit("3.14")), 3.14);
    ASSERT_FLOAT_EQ(gray_strconv_to_float(gray_string_lit("0.0")), 0.0);
    ASSERT_FLOAT_EQ(gray_strconv_to_float(gray_string_lit("-1.5")), -1.5);
}

static void test_strconv_to_bool(void) {
    ASSERT_EQ(gray_strconv_to_bool(gray_string_lit("true")), true);
    ASSERT_EQ(gray_strconv_to_bool(gray_string_lit("false")), false);
    ASSERT_EQ(gray_strconv_to_bool(gray_string_lit("TRUE")), true);
    ASSERT_EQ(gray_strconv_to_bool(gray_string_lit("False")), false);
}

static void test_strconv_to_int_result_ok(void) {
    GrayResult_int r = gray_strconv_to_int_result(gray_string_lit("42"), 10);
    ASSERT_EQ(r.v0, 42);
    ASSERT(r.v1 == NULL);
}

static void test_strconv_to_int_result_err(void) {
    GrayResult_int r = gray_strconv_to_int_result(gray_string_lit("abc"), 10);
    ASSERT_NOT_NULL(r.v1);
}

static void test_strconv_to_uint_result_negative(void) {
    GrayResult_uint r = gray_strconv_to_uint_result(gray_string_lit("-5"), 10);
    ASSERT_NOT_NULL(r.v1);
}

static void test_strconv_to_float_result_ok(void) {
    GrayResult_float r = gray_strconv_to_float_result(gray_string_lit("3.14"));
    ASSERT_FLOAT_EQ(r.v0, 3.14);
    ASSERT(r.v1 == NULL);
}

static void test_strconv_to_float_result_err(void) {
    GrayResult_float r = gray_strconv_to_float_result(gray_string_lit("xyz"));
    ASSERT_NOT_NULL(r.v1);
}

static void test_strconv_to_bool_result_ok(void) {
    GrayResult_bool r = gray_strconv_to_bool_result(gray_string_lit("true"));
    ASSERT_EQ(r.v0, true);
    ASSERT(r.v1 == NULL);
}

static void test_strconv_to_bool_result_err(void) {
    GrayResult_bool r = gray_strconv_to_bool_result(gray_string_lit("yes"));
    ASSERT_NOT_NULL(r.v1);
}

static void test_strconv_from_int(void) {
    ASSERT_GRAY_STR(gray_strconv_from_int(arena, 42), "42");
    ASSERT_GRAY_STR(gray_strconv_from_int(arena, -100), "-100");
    ASSERT_GRAY_STR(gray_strconv_from_int(arena, 0), "0");
}

static void test_strconv_from_uint(void) {
    ASSERT_GRAY_STR(gray_strconv_from_uint(arena, 255), "255");
    ASSERT_GRAY_STR(gray_strconv_from_uint(arena, 0), "0");
}

static void test_strconv_from_float(void) {
    ASSERT_GRAY_STR(gray_strconv_from_float(arena, 3.14), "3.14");
    ASSERT_GRAY_STR(gray_strconv_from_float(arena, 0.0), "0.0");
}

static void test_strconv_from_bool(void) {
    ASSERT(gray_string_eq(gray_strconv_from_bool(true), gray_string_lit("true")));
    ASSERT(gray_string_eq(gray_strconv_from_bool(false), gray_string_lit("false")));
}

static void test_strconv_is_numeric(void) {
    ASSERT(gray_strconv_is_numeric(gray_string_lit("3.14")));
    ASSERT(gray_strconv_is_numeric(gray_string_lit("42")));
    ASSERT(gray_strconv_is_numeric(gray_string_lit("-7")));
    ASSERT(gray_strconv_is_numeric(gray_string_lit("+3.0")));
    ASSERT(!gray_strconv_is_numeric(gray_string_lit("abc")));
    ASSERT(!gray_strconv_is_numeric(gray_string_lit("")));
    ASSERT(!gray_strconv_is_numeric(gray_string_lit("1.2.3")));
}

static void test_strconv_is_integer(void) {
    ASSERT(gray_strconv_is_integer(gray_string_lit("42")));
    ASSERT(gray_strconv_is_integer(gray_string_lit("-7")));
    ASSERT(!gray_strconv_is_integer(gray_string_lit("3.14")));
    ASSERT(!gray_strconv_is_integer(gray_string_lit("abc")));
    ASSERT(!gray_strconv_is_integer(gray_string_lit("")));
}

/* ===== main ===== */

int main(void) {
    arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    gray_default_arena = arena;

    printf("\n");

    printf("--- strings ---\n");
    RUN_TEST(test_strings_to_upper);
    RUN_TEST(test_strings_to_upper_mixed);
    RUN_TEST(test_strings_to_snake_case_separators);
    RUN_TEST(test_strings_to_snake_case_boundaries);
    RUN_TEST(test_strings_to_lower);
    RUN_TEST(test_strings_trim);
    RUN_TEST(test_strings_trim_tabs_newlines);
    RUN_TEST(test_strings_trim_left);
    RUN_TEST(test_strings_trim_right);
    RUN_TEST(test_strings_contains);
    RUN_TEST(test_strings_contains_empty);
    RUN_TEST(test_strings_starts_with);
    RUN_TEST(test_strings_ends_with);
    RUN_TEST(test_strings_index_of);
    RUN_TEST(test_strings_last_index_of);
    RUN_TEST(test_strings_count);
    RUN_TEST(test_strings_is_empty);
    RUN_TEST(test_strings_remove_prefix);
    RUN_TEST(test_strings_remove_prefix_no_match);
    RUN_TEST(test_strings_remove_suffix);
    RUN_TEST(test_strings_replace);
    RUN_TEST(test_strings_replace_multiple);
    RUN_TEST(test_strings_replace_no_match);
    RUN_TEST(test_strings_repeat);
    RUN_TEST(test_strings_repeat_zero);
    RUN_TEST(test_strings_reverse);
    RUN_TEST(test_strings_reverse_empty);
    RUN_TEST(test_strings_slice);
    RUN_TEST(test_strings_slice_clamped);
    RUN_TEST(test_strings_slice_empty);
    RUN_TEST(test_strings_split);
    RUN_TEST(test_strings_split_no_match);
    RUN_TEST(test_strings_join);
    RUN_TEST(test_strings_join_empty_array);
    RUN_TEST(test_strings_char_at);
    RUN_TEST(test_strings_to_chars);
    RUN_TEST(test_strings_from_chars);
    RUN_TEST(test_strings_classification);

    printf("--- arrays ---\n");
    RUN_TEST(test_arrays_append);
    RUN_TEST(test_arrays_insert_at);
    RUN_TEST(test_arrays_prepend);
    RUN_TEST(test_arrays_remove_at);
    RUN_TEST(test_arrays_remove_int);
    RUN_TEST(test_arrays_clear);
    RUN_TEST(test_arrays_fill);
    RUN_TEST(test_arrays_get_first);
    RUN_TEST(test_arrays_get_last);
    RUN_TEST(test_arrays_remove_first);
    RUN_TEST(test_arrays_remove_last);
    RUN_TEST(test_arrays_is_empty);
    RUN_TEST(test_arrays_contains_int);
    RUN_TEST(test_arrays_contains_str);
    RUN_TEST(test_arrays_index_of_int);
    RUN_TEST(test_arrays_count);
    RUN_TEST(test_arrays_is_equal_prim);
    RUN_TEST(test_arrays_is_equal_str);
    RUN_TEST(test_arrays_reverse);
    RUN_TEST(test_arrays_slice);
    RUN_TEST(test_arrays_concat);
    RUN_TEST(test_arrays_deduplicate);
    RUN_TEST(test_arrays_flatten);
    RUN_TEST(test_arrays_split_every);
    RUN_TEST(test_arrays_pair);
    RUN_TEST(test_arrays_get_sum);
    RUN_TEST(test_arrays_get_min);
    RUN_TEST(test_arrays_get_max);
    RUN_TEST(test_arrays_sort_asc);
    RUN_TEST(test_arrays_sort_desc);
    RUN_TEST(test_arrays_sort_asc_str);

    printf("--- maps ---\n");
    RUN_TEST(test_maps_get_keys);
    RUN_TEST(test_maps_get_values);
    RUN_TEST(test_maps_has_key);
    RUN_TEST(test_maps_is_empty);
    RUN_TEST(test_maps_contains_value);
    RUN_TEST(test_maps_is_equal);
    RUN_TEST(test_maps_is_equal_str_keys);
    RUN_TEST(test_maps_is_equal_different);
    RUN_TEST(test_maps_merge);

    printf("--- math ---\n");
    RUN_TEST(test_math_abs);
    RUN_TEST(test_math_sign);
    RUN_TEST(test_math_min_max);
    RUN_TEST(test_math_clamp);
    RUN_TEST(test_math_rounding);
    RUN_TEST(test_math_powers);
    RUN_TEST(test_math_exp_log);
    RUN_TEST(test_math_trig);
    RUN_TEST(test_math_deg_rad);
    RUN_TEST(test_math_properties);
    RUN_TEST(test_math_factorial);
    RUN_TEST(test_math_gcd);
    RUN_TEST(test_math_lcm);
    RUN_TEST(test_math_is_prime);
    RUN_TEST(test_math_lerp);
    RUN_TEST(test_math_distance);

    printf("--- fmt ---\n");
    RUN_TEST(test_fmt_pad_left);
    RUN_TEST(test_fmt_pad_left_no_pad);
    RUN_TEST(test_fmt_pad_right);
    RUN_TEST(test_fmt_center);
    RUN_TEST(test_fmt_center_odd);
    RUN_TEST(test_fmt_int_to_hex);
    RUN_TEST(test_fmt_int_to_binary);
    RUN_TEST(test_fmt_int_to_octal);
    RUN_TEST(test_fmt_float_fixed);
    RUN_TEST(test_fmt_float_sci);

    printf("--- encoding ---\n");
    RUN_TEST(test_encoding_base64_encode);
    RUN_TEST(test_encoding_base64_decode);
    RUN_TEST(test_encoding_base64_roundtrip);
    RUN_TEST(test_encoding_hex_encode);
    RUN_TEST(test_encoding_hex_decode);
    RUN_TEST(test_encoding_hex_roundtrip);
    RUN_TEST(test_encoding_url_encode);
    RUN_TEST(test_encoding_url_decode);
    RUN_TEST(test_encoding_url_roundtrip);

    printf("--- strconv ---\n");
    RUN_TEST(test_strconv_to_int);
    RUN_TEST(test_strconv_to_uint);
    RUN_TEST(test_strconv_to_float);
    RUN_TEST(test_strconv_to_bool);
    RUN_TEST(test_strconv_to_int_result_ok);
    RUN_TEST(test_strconv_to_int_result_err);
    RUN_TEST(test_strconv_to_uint_result_negative);
    RUN_TEST(test_strconv_to_float_result_ok);
    RUN_TEST(test_strconv_to_float_result_err);
    RUN_TEST(test_strconv_to_bool_result_ok);
    RUN_TEST(test_strconv_to_bool_result_err);
    RUN_TEST(test_strconv_from_int);
    RUN_TEST(test_strconv_from_uint);
    RUN_TEST(test_strconv_from_float);
    RUN_TEST(test_strconv_from_bool);
    RUN_TEST(test_strconv_is_numeric);
    RUN_TEST(test_strconv_is_integer);

    PRINT_RESULTS();
    gray_arena_destroy(arena, __FILE__, __LINE__);
    return _test_fail > 0 ? 1 : 0;
}
