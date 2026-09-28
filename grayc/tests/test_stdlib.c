/*
 * test_stdlib.c — Unit tests for the Grayscale stdlib modules:
 * strings, arrays, maps, math, fmt, encoding, strconv, json, io, regex,
 * csv, crypto, uuid, binary.
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
#include "../src/stdlib/chars.h"
#include "../src/stdlib/arrays.h"
#include "../src/stdlib/maps.h"
#include "../src/stdlib/math.h"
#include "../src/stdlib/fmt.h"
#include "../src/stdlib/encoding.h"
#include "../src/stdlib/strconv.h"
#include "../src/stdlib/json.h"
#include "../src/stdlib/io.h"
#include "../src/stdlib/regex.h"
#include "../src/stdlib/builtins.h"
#include "../src/stdlib/net.h"
#include "../src/stdlib/csv.h"
#include "../src/stdlib/crypto.h"
#include "../src/stdlib/uuid.h"
#include "../src/stdlib/binary.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

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
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 3, GRAY_ELEM_STRING);
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
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
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
    GrayArray chars = gray_array_new(arena, sizeof(int32_t), 3, GRAY_ELEM_I32);
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

/* --- char editing (#2560): each returns a new string, input untouched --- */

static void test_strings_append_char(void) {
    ASSERT_GRAY_STR(gray_strings_append_char(arena, gray_string_lit("hell"), 'o'), "hello");
    ASSERT_GRAY_STR(gray_strings_append_char(arena, gray_string_lit(""), 'x'), "x");
}

static void test_strings_prepend_char(void) {
    ASSERT_GRAY_STR(gray_strings_prepend_char(arena, gray_string_lit("ello"), 'h'), "hello");
    ASSERT_GRAY_STR(gray_strings_prepend_char(arena, gray_string_lit(""), 'x'), "x");
}

static void test_strings_insert_char_at(void) {
    ASSERT_GRAY_STR(gray_strings_insert_char_at(arena, gray_string_lit("helo"), 3, 'l'), "hello");
    ASSERT_GRAY_STR(gray_strings_insert_char_at(arena, gray_string_lit("bc"), 0, 'a'), "abc");
    /* an index equal to the length appends */
    ASSERT_GRAY_STR(gray_strings_insert_char_at(arena, gray_string_lit("ab"), 2, 'c'), "abc");
}

static void test_strings_remove_at(void) {
    ASSERT_GRAY_STR(gray_strings_remove_at(arena, gray_string_lit("hello!"), 5), "hello");
    ASSERT_GRAY_STR(gray_strings_remove_at(arena, gray_string_lit("abc"), 0), "bc");
}

static void test_strings_set_char_at(void) {
    ASSERT_GRAY_STR(gray_strings_set_char_at(arena, gray_string_lit("hello"), 0, 'H'), "Hello");
    ASSERT_GRAY_STR(gray_strings_set_char_at(arena, gray_string_lit("cat"), 1, 'u'), "cut");
}

/* ===== chars module (#2559) ===== */

static void test_chars_to_upper(void) {
    ASSERT_EQ(gray_chars_to_upper('a'), 'A');
    ASSERT_EQ(gray_chars_to_upper('z'), 'Z');
    ASSERT_EQ(gray_chars_to_upper('A'), 'A');           /* already uppercase */
    ASSERT_EQ(gray_chars_to_upper('5'), '5');           /* digit unchanged */
    ASSERT_EQ(gray_chars_to_upper(0x00E9), 0x00E9);     /* non-ASCII 'é' unchanged */
}

static void test_chars_to_lower(void) {
    ASSERT_EQ(gray_chars_to_lower('Z'), 'z');
    ASSERT_EQ(gray_chars_to_lower('A'), 'a');
    ASSERT_EQ(gray_chars_to_lower('a'), 'a');           /* already lowercase */
    ASSERT_EQ(gray_chars_to_lower('#'), '#');           /* symbol unchanged */
    ASSERT_EQ(gray_chars_to_lower(0x00C9), 0x00C9);     /* non-ASCII 'É' unchanged */
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
    gray_arrays_remove(&arr, &(int64_t){2});
    ASSERT_EQ(arr.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 3);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 2), 2);
}

static void test_arrays_remove_float(void) {
    GrayArray arr = GRAY_ARRAY_FROM_F64(arena, 1.5, 2.5, 3.5, 2.5);
    gray_arrays_remove(&arr, &(double){2.5});
    ASSERT_EQ(arr.len, 3);
    ASSERT(GRAY_ARRAY_GET(arr, double, 0) == 1.5);
    ASSERT(GRAY_ARRAY_GET(arr, double, 1) == 3.5);
    ASSERT(GRAY_ARRAY_GET(arr, double, 2) == 2.5);
}

static void test_arrays_remove_str(void) {
    GrayArray arr = GRAY_ARRAY_FROM_STR(arena,
        gray_string_lit("a"), gray_string_lit("b"), gray_string_lit("c"), gray_string_lit("b"));
    GrayString needle = gray_string_lit("b"); gray_arrays_remove(&arr, &needle);
    ASSERT_EQ(arr.len, 3);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(arr, GrayString, 0), "a");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(arr, GrayString, 1), "c");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(arr, GrayString, 2), "b");
}

static void test_arrays_clear(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    gray_arrays_clear(&arr);
    ASSERT_EQ(arr.len, 0);
}

static void test_arrays_fill(void) {
    GrayArray arr = gray_array_new(arena, sizeof(int64_t), 0, GRAY_ELEM_I64);
    int64_t v = 7;
    gray_arrays_fill(arena, &arr, &v, 5);
    ASSERT_EQ(arr.len, 5);
    for (int i = 0; i < 5; i++)
        ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, i), 7);
}

static void test_arrays_get_first(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    ASSERT_EQ(*(int64_t *)gray_arrays_first_ptr(&arr), 10);
}

static void test_arrays_get_last(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    ASSERT_EQ(*(int64_t *)gray_arrays_last_ptr(&arr), 30);
}

static void test_arrays_remove_first(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    int64_t val; gray_arrays_remove_first_raw(&arr, &val);
    ASSERT_EQ(val, 10);
    ASSERT_EQ(arr.len, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 20);
}

static void test_arrays_remove_last(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    int64_t val; gray_arrays_remove_last_raw(&arr, &val);
    ASSERT_EQ(val, 30);
    ASSERT_EQ(arr.len, 2);
}

static void test_arrays_is_empty(void) {
    GrayArray empty = gray_array_new(arena, sizeof(int64_t), 0, GRAY_ELEM_I64);
    GrayArray nonempty = GRAY_ARRAY_FROM_I64(arena, 1);
    ASSERT(gray_arrays_is_empty(&empty));
    ASSERT(!gray_arrays_is_empty(&nonempty));
}

static void test_arrays_contains_i64(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    ASSERT(gray_arrays_contains(&arr, &(int64_t){2}));
    ASSERT(!gray_arrays_contains(&arr, &(int64_t){99}));
}

static void test_arrays_contains_str(void) {
    GrayArray arr = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("a"), gray_string_lit("b"));
    GrayString present = gray_string_lit("a"), absent = gray_string_lit("c");
    ASSERT(gray_arrays_contains(&arr, &present));
    ASSERT(!gray_arrays_contains(&arr, &absent));
}

static void test_arrays_index_of_int(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 10, 20, 30);
    ASSERT_EQ(gray_arrays_index_of(&arr, &(int64_t){20}), 1);
    ASSERT_EQ(gray_arrays_index_of(&arr, &(int64_t){99}), -1);
}

static void test_arrays_count(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 1, 2, 2, 3, 2);
    ASSERT_EQ(gray_arrays_count(&arr, &(int64_t){2}), 3);
    ASSERT_EQ(gray_arrays_count(&arr, &(int64_t){99}), 0);
}

static void test_arrays_is_equal_prim(void) {
    GrayArray a = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray b = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray c = GRAY_ARRAY_FROM_I64(arena, 1, 2, 4);
    ASSERT(gray_arrays_is_equal(&a, &b));
    ASSERT(!gray_arrays_is_equal(&a, &c));
}

static void test_arrays_is_equal_str(void) {
    GrayArray a = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("x"), gray_string_lit("y"));
    GrayArray b = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("x"), gray_string_lit("y"));
    GrayArray c = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("x"), gray_string_lit("z"));
    ASSERT(gray_arrays_is_equal(&a, &b));
    ASSERT(!gray_arrays_is_equal(&a, &c));
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
    GrayArray outer = gray_array_new(arena, sizeof(GrayArray), 2, GRAY_ELEM_ARRAY);
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
    int64_t total; gray_arrays_get_sum(&arr, &total, __FILE__, __LINE__); ASSERT_EQ(total, 10);
}

static void test_arrays_get_min(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    int64_t smallest; gray_arrays_get_min(&arr, &smallest); ASSERT_EQ(smallest, 1);
}

static void test_arrays_get_max(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    int64_t largest; gray_arrays_get_max(&arr, &largest); ASSERT_EQ(largest, 5);
}

static void test_arrays_sort_asc(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    gray_arrays_sort(&arr, false);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 1), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 4), 5);
}

static void test_arrays_sort_desc(void) {
    GrayArray arr = GRAY_ARRAY_FROM_I64(arena, 3, 1, 4, 1, 5);
    gray_arrays_sort(&arr, true);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 0), 5);
    ASSERT_EQ(GRAY_ARRAY_GET(arr, int64_t, 4), 1);
}

static void test_arrays_sort_asc_str(void) {
    GrayArray arr = GRAY_ARRAY_FROM_STR(arena,
        gray_string_lit("cherry"),
        gray_string_lit("apple"),
        gray_string_lit("banana"));
    gray_arrays_sort(&arr, false);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(arr, GrayString, 0), gray_string_lit("apple")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(arr, GrayString, 1), gray_string_lit("banana")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(arr, GrayString, 2), gray_string_lit("cherry")));
}

/* ===== maps module ===== */

static void test_maps_get_keys(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString k1 = gray_string_lit("a");
    GrayString k2 = gray_string_lit("b");
    int64_t v1 = 1, v2 = 2;
    gray_map_set(arena, &m, &k1, &v1, __FILE__, __LINE__);
    gray_map_set(arena, &m, &k2, &v2, __FILE__, __LINE__);
    GrayArray keys = gray_maps_get_keys(arena, &m);
    ASSERT_EQ(keys.len, 2);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(keys, GrayString, 0), gray_string_lit("a")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(keys, GrayString, 1), gray_string_lit("b")));
}

static void test_maps_get_values(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString k1 = gray_string_lit("x");
    int64_t v1 = 42;
    gray_map_set(arena, &m, &k1, &v1, __FILE__, __LINE__);
    GrayArray vals = gray_maps_get_values(arena, &m);
    ASSERT_EQ(vals.len, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(vals, int64_t, 0), 42);
}

static void test_maps_has_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k = 5, v = 50;
    ASSERT(!gray_maps_has_key(&m, &k));
    GRAY_MAP_SET(arena, &m, &k, &v);
    ASSERT(gray_maps_has_key(&m, &k));
}

static void test_maps_is_empty(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    ASSERT(gray_maps_is_empty(&m));
    int64_t k = 1, v = 10;
    GRAY_MAP_SET(arena, &m, &k, &v);
    ASSERT(!gray_maps_is_empty(&m));
}

static void test_maps_contains_value(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k1 = 1, v1 = 100, k2 = 2, v2 = 200;
    GRAY_MAP_SET(arena, &m, &k1, &v1);
    GRAY_MAP_SET(arena, &m, &k2, &v2);
    ASSERT(gray_maps_contains_value(&m, &v1));
    int64_t v3 = 999;
    ASSERT(!gray_maps_contains_value(&m, &v3));
}

static void test_maps_is_equal(void) {
    GrayMap a = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    GrayMap b = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k1 = 1, v1 = 10, k2 = 2, v2 = 20;
    GRAY_MAP_SET(arena, &a, &k1, &v1);
    GRAY_MAP_SET(arena, &a, &k2, &v2);
    GRAY_MAP_SET(arena, &b, &k1, &v1);
    GRAY_MAP_SET(arena, &b, &k2, &v2);
    ASSERT(gray_maps_is_equal(&a, &b, false, false));
}

static void test_maps_is_equal_str_keys(void) {
    GrayMap a = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayMap b = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString k = gray_string_lit("key");
    int64_t v = 42;
    gray_map_set(arena, &a, &k, &v, __FILE__, __LINE__);
    gray_map_set(arena, &b, &k, &v, __FILE__, __LINE__);
    ASSERT(gray_maps_is_equal(&a, &b, true, false));
}

static void test_maps_is_equal_different(void) {
    GrayMap a = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    GrayMap b = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k1 = 1, v1 = 10, v2 = 99;
    GRAY_MAP_SET(arena, &a, &k1, &v1);
    GRAY_MAP_SET(arena, &b, &k1, &v2);
    ASSERT(!gray_maps_is_equal(&a, &b, false, false));
}

static void test_maps_merge(void) {
    GrayMap m1 = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    GrayMap m2 = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
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
    ASSERT_EQ(gray_math_abs_i64(-5), 5);
    ASSERT_EQ(gray_math_abs_i64(5), 5);
    ASSERT_EQ(gray_math_abs_i64(0), 0);
    ASSERT_FLOAT_EQ(gray_math_abs_f64(-3.14), 3.14);
}

static void test_math_sign(void) {
    ASSERT_EQ(gray_math_sign(-7), -1);
    ASSERT_EQ(gray_math_sign(0), 0);
    ASSERT_EQ(gray_math_sign(3), 1);
}

static void test_math_min_max(void) {
    ASSERT_EQ(gray_math_min_i64(3, 7), 3);
    ASSERT_EQ(gray_math_max_i64(3, 7), 7);
    ASSERT_FLOAT_EQ(gray_math_min_f64(1.5, 2.5), 1.5);
    ASSERT_FLOAT_EQ(gray_math_max_f64(1.5, 2.5), 2.5);
}

static void test_math_clamp(void) {
    ASSERT_EQ(gray_math_clamp_i64(15, 1, 10), 10);
    ASSERT_EQ(gray_math_clamp_i64(-5, 1, 10), 1);
    ASSERT_EQ(gray_math_clamp_i64(5, 1, 10), 5);
    ASSERT_FLOAT_EQ(gray_math_clamp_f64(1.5, 0.0, 1.0), 1.0);
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

static void test_fmt_i64_to_hex(void) {
    ASSERT_GRAY_STR(gray_fmt_i64_to_hex(arena, 255), "ff");
    ASSERT_GRAY_STR(gray_fmt_i64_to_hex(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_i64_to_hex(arena, 16), "10");
}

static void test_fmt_i64_to_binary(void) {
    ASSERT_GRAY_STR(gray_fmt_i64_to_binary(arena, 10), "1010");
    ASSERT_GRAY_STR(gray_fmt_i64_to_binary(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_i64_to_binary(arena, 1), "1");
}

static void test_fmt_i64_to_octal(void) {
    ASSERT_GRAY_STR(gray_fmt_i64_to_octal(arena, 8), "10");
    ASSERT_GRAY_STR(gray_fmt_i64_to_octal(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_i64_to_octal(arena, 255), "377");
}

static void test_fmt_f64_to_fixed(void) {
    ASSERT_GRAY_STR(gray_fmt_f64_to_fixed(arena, 3.14159, 2), "3.14");
    ASSERT_GRAY_STR(gray_fmt_f64_to_fixed(arena, 1.0, 0), "1");
}

static void test_fmt_f64_to_scientific(void) {
    GrayString r = gray_fmt_f64_to_scientific(arena, 1234.5);
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

static void test_strconv_to_i64(void) {
    ASSERT_EQ(gray_strconv_to_i64(gray_string_lit("42"), 10), 42);
    ASSERT_EQ(gray_strconv_to_i64(gray_string_lit("-100"), 10), -100);
    ASSERT_EQ(gray_strconv_to_i64(gray_string_lit("ff"), 16), 255);
    ASSERT_EQ(gray_strconv_to_i64(gray_string_lit("101"), 2), 5);
}

static void test_strconv_to_u64(void) {
    ASSERT_EQ((int64_t)gray_strconv_to_u64(gray_string_lit("255"), 10), 255);
    ASSERT_EQ((int64_t)gray_strconv_to_u64(gray_string_lit("ff"), 16), 255);
}

static void test_strconv_to_f64(void) {
    ASSERT_FLOAT_EQ(gray_strconv_to_f64(gray_string_lit("3.14")), 3.14);
    ASSERT_FLOAT_EQ(gray_strconv_to_f64(gray_string_lit("0.0")), 0.0);
    ASSERT_FLOAT_EQ(gray_strconv_to_f64(gray_string_lit("-1.5")), -1.5);
}

static void test_strconv_to_bool(void) {
    ASSERT_EQ(gray_strconv_to_bool_result(gray_string_lit("true")).v0, true);
    ASSERT_EQ(gray_strconv_to_bool_result(gray_string_lit("false")).v0, false);
    ASSERT_EQ(gray_strconv_to_bool_result(gray_string_lit("TRUE")).v0, true);
    ASSERT_EQ(gray_strconv_to_bool_result(gray_string_lit("False")).v0, false);
}

static void test_strconv_to_i64_result_ok(void) {
    GrayResult_i64 r = gray_strconv_to_i64_result(gray_string_lit("42"), 10);
    ASSERT_EQ(r.v0, 42);
    ASSERT(r.v1 == NULL);
}

static void test_strconv_to_i64_result_err(void) {
    GrayResult_i64 r = gray_strconv_to_i64_result(gray_string_lit("abc"), 10);
    ASSERT_NOT_NULL(r.v1);
}

static void test_strconv_to_u64_result_negative(void) {
    GrayResult_u64 r = gray_strconv_to_u64_result(gray_string_lit("-5"), 10);
    ASSERT_NOT_NULL(r.v1);
}

static void test_strconv_to_f64_result_ok(void) {
    GrayResult_f64 r = gray_strconv_to_f64_result(gray_string_lit("3.14"));
    ASSERT_FLOAT_EQ(r.v0, 3.14);
    ASSERT(r.v1 == NULL);
}

static void test_strconv_to_f64_result_err(void) {
    GrayResult_f64 r = gray_strconv_to_f64_result(gray_string_lit("xyz"));
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

static void test_strconv_from_i64(void) {
    ASSERT_GRAY_STR(gray_strconv_from_i64(arena, 42), "42");
    ASSERT_GRAY_STR(gray_strconv_from_i64(arena, -100), "-100");
    ASSERT_GRAY_STR(gray_strconv_from_i64(arena, 0), "0");
}

static void test_strconv_from_u64(void) {
    ASSERT_GRAY_STR(gray_strconv_from_u64(arena, 255), "255");
    ASSERT_GRAY_STR(gray_strconv_from_u64(arena, 0), "0");
}

static void test_strconv_from_f64(void) {
    ASSERT_GRAY_STR(gray_strconv_from_f64(arena, 3.14), "3.14");
    ASSERT_GRAY_STR(gray_strconv_from_f64(arena, 0.0), "0.0");
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

/* --- format_i64 / format_u64 / quote / unquote (#2434) --- */

static void test_strconv_format_i64(void) {
    ASSERT_GRAY_STR(gray_strconv_format_i64(arena, 255, 16), "ff");
    ASSERT_GRAY_STR(gray_strconv_format_i64(arena, -10, 2), "-1010");
    ASSERT_GRAY_STR(gray_strconv_format_i64(arena, 0, 10), "0");
    ASSERT_GRAY_STR(gray_strconv_format_i64(arena, 35, 36), "z");   /* digits above 9 are a-z */
}

static void test_strconv_format_u64(void) {
    ASSERT_GRAY_STR(gray_strconv_format_u64(arena, 255, 16), "ff");
    ASSERT_GRAY_STR(gray_strconv_format_u64(arena, 8, 8), "10");
    ASSERT_GRAY_STR(gray_strconv_format_u64(arena, 0, 2), "0");
}

/* STANDARD: to_i64(format_i64(n, b), b) == n */
static void test_strconv_format_i64_roundtrip(void) {
    GrayString s = gray_strconv_format_i64(arena, -12345, 16);
    ASSERT_EQ(gray_strconv_to_i64(s, 16), -12345);
}

static void test_strconv_quote(void) {
    ASSERT_GRAY_STR(gray_strconv_quote(arena, gray_string_lit("hi")), "\"hi\"");
    ASSERT_GRAY_STR(gray_strconv_quote(arena, gray_string_lit("a\tb\nc")), "\"a\\tb\\nc\"");
    ASSERT_GRAY_STR(gray_strconv_quote(arena, gray_string_lit("say \"hi\"")), "\"say \\\"hi\\\"\"");
    /* a control byte with no named escape becomes \xNN */
    ASSERT_GRAY_STR(gray_strconv_quote(arena, gray_string_lit("\x01")), "\"\\x01\"");
}

static void test_strconv_unquote_ok(void) {
    GrayResult_string r = gray_strconv_unquote_result(arena, gray_string_lit("\"a\\tb\""));
    ASSERT(r.v1 == NULL);
    ASSERT_GRAY_STR(r.v0, "a\tb");
}

static void test_strconv_unquote_err(void) {
    /* no surrounding double quotes */
    GrayResult_string r = gray_strconv_unquote_result(arena, gray_string_lit("nope"));
    ASSERT_NOT_NULL(r.v1);
}

/* STANDARD: unquote(quote(s)) returns s */
static void test_strconv_quote_unquote_roundtrip(void) {
    GrayString original = gray_string_lit("tab\there\nand \"quotes\"");
    GrayResult_string r = gray_strconv_unquote_result(arena, gray_strconv_quote(arena, original));
    ASSERT(r.v1 == NULL);
    ASSERT(gray_string_eq(r.v0, original));
}

/* ===== json module ===== */

static void test_json_encode_map(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    GrayString k = gray_string_lit("name");
    GrayString v = gray_string_lit("Alice");
    gray_map_set(arena, &m, &k, &v, __FILE__, __LINE__);
    GrayString r = gray_json_encode_map(arena, &m);
    ASSERT_GRAY_STR(r, "{\"name\":\"Alice\"}");
}

/* Every typed map encoder shares one two-pass implementation; each is checked
 * with a key that requires escaping and a worst-case value. */

static void test_json_encode_map_string_escaped_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    GrayString k = gray_string_lit("a\"b");
    GrayString v = gray_string_lit("v\\x");
    gray_map_set(arena, &m, &k, &v, __FILE__, __LINE__);
    GrayString r = gray_json_encode_map(arena, &m);
    ASSERT_GRAY_STR(r, "{\"a\\\"b\":\"v\\\\x\"}");
}

static void test_json_encode_map_int_escaped_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString k = gray_string_lit("a\"b");
    int64_t v = INT64_MIN; /* longest int64 output: -9223372036854775808 */
    gray_map_set(arena, &m, &k, &v, __FILE__, __LINE__);
    GrayString r = gray_json_encode_map_int(arena, &m);
    ASSERT_GRAY_STR(r, "{\"a\\\"b\":-9223372036854775808}");
}

static void test_json_encode_map_float_escaped_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(double), 0, GRAY_ELEM_STRING, GRAY_ELEM_F64);
    GrayString k = gray_string_lit("k\ny");
    double v = 3.5;
    gray_map_set(arena, &m, &k, &v, __FILE__, __LINE__);
    GrayString r = gray_json_encode_map_float(arena, &m);
    ASSERT_GRAY_STR(r, "{\"k\\ny\":3.5}");
}

static void test_json_encode_map_bool_escaped_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(bool), 0, GRAY_ELEM_STRING, GRAY_ELEM_BOOL);
    GrayString k1 = gray_string_lit("t\"1");
    GrayString k2 = gray_string_lit("f\"2");
    bool v1 = true, v2 = false;
    gray_map_set(arena, &m, &k1, &v1, __FILE__, __LINE__);
    gray_map_set(arena, &m, &k2, &v2, __FILE__, __LINE__);
    GrayString r = gray_json_encode_map_bool(arena, &m);
    ASSERT_GRAY_STR(r, "{\"t\\\"1\":true,\"f\\\"2\":false}");
}

static void test_json_encode_array_int(void) {
    GrayArray a = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayString r = gray_json_encode_array_int(arena, &a);
    ASSERT_GRAY_STR(r, "[1,2,3]");
}

static void test_json_encode_array_float(void) {
    GrayArray a = GRAY_ARRAY_FROM_F64(arena, 1.5, 2.0, 3.25);
    GrayString r = gray_json_encode_array_float(arena, &a);
    ASSERT_GRAY_STR(r, "[1.5,2,3.25]");
}

static void test_json_encode_array_bool(void) {
    GrayArray a = GRAY_ARRAY_FROM_BOOL(arena, true, false, true);
    GrayString r = gray_json_encode_array_bool(arena, &a);
    ASSERT_GRAY_STR(r, "[true,false,true]");
}

static void test_json_encode_array_string(void) {
    GrayArray a = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("a"), gray_string_lit("b"));
    GrayString r = gray_json_encode_array_string(arena, &a);
    ASSERT_GRAY_STR(r, "[\"a\",\"b\"]");
}

static void test_json_is_valid(void) {
    ASSERT(gray_json_is_valid(gray_string_lit("{\"a\":1}")));
    ASSERT(gray_json_is_valid(gray_string_lit("[1, 2, 3]")));
    ASSERT(gray_json_is_valid(gray_string_lit("\"hello\"")));
    ASSERT(gray_json_is_valid(gray_string_lit("{\"nested\":{\"k\":[1,2]}}")));
    ASSERT(!gray_json_is_valid(gray_string_lit("{bad json}")));
    ASSERT(!gray_json_is_valid(gray_string_lit("")));
    ASSERT(!gray_json_is_valid(gray_string_lit("{\"a\":}")));
}

static void test_json_decode(void) {
    GrayMap m = gray_json_decode(arena, gray_string_lit("{\"x\":\"1\",\"y\":\"two\"}"));
    ASSERT_EQ(m.count, 2);
    GrayString *x = (GrayString *)gray_map_get_str(&m, gray_string_lit("x"));
    ASSERT_NOT_NULL(x);
    ASSERT(gray_string_eq(*x, gray_string_lit("1")));
    GrayString *y = (GrayString *)gray_map_get_str(&m, gray_string_lit("y"));
    ASSERT_NOT_NULL(y);
    ASSERT(gray_string_eq(*y, gray_string_lit("two")));
}

static void test_json_decode_result_ok(void) {
    GrayResult_map r = gray_json_decode_result(arena, gray_string_lit("{\"k\":\"v\"}"));
    ASSERT(r.v1 == NULL);
    ASSERT_EQ(r.v0.count, 1);
}

static void test_json_decode_result_err(void) {
    GrayResult_map r = gray_json_decode_result(arena, gray_string_lit("{not json}"));
    ASSERT_NOT_NULL(r.v1);
}

static void test_json_roundtrip(void) {
    GrayString text = gray_string_lit("{\"a\":\"1\",\"b\":\"2\"}");
    GrayMap m = gray_json_decode(arena, text);
    GrayString back = gray_json_encode_map(arena, &m);
    ASSERT_GRAY_STR(back, "{\"a\":\"1\",\"b\":\"2\"}");
}

static void test_json_pretty_map(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    GrayString k = gray_string_lit("k");
    GrayString v = gray_string_lit("v");
    gray_map_set(arena, &m, &k, &v, __FILE__, __LINE__);
    GrayString pretty = gray_json_pretty_map(arena, &m, 2);
    GrayString compact = gray_json_encode_map(arena, &m);
    ASSERT_GT(pretty.len, compact.len);
    char buf[256];
    const char *s = gray_cstr(pretty, buf, sizeof(buf));
    ASSERT(strstr(s, "\"k\"") != NULL);
    ASSERT(strstr(s, "\n") != NULL);
}

static void test_json_split_array(void) {
    GrayArray parts = gray_json_split_array(arena, gray_string_lit("[{\"a\":1},{\"b\":2},{\"c\":3}]"));
    ASSERT_EQ(parts.len, 3);
}

/* ===== io module ===== */

/* io round-trips write to a real temp directory so the suite stays portable
 * (no assumption that /tmp exists). */
static GrayString io_tmp_path(const char *name) {
    GrayString parts_data[2];
    parts_data[0] = gray_io_temp_dir_result(arena).v0;
    parts_data[1] = gray_string_lit(name);
    GrayArray parts = gray_array_from(arena, parts_data, sizeof(GrayString), 2, GRAY_ELEM_STRING);
    return gray_io_path_join(arena, parts);
}

static void test_io_write_read_roundtrip(void) {
    GrayString path = io_tmp_path("grayc_ut_rw.txt");
    ASSERT(gray_io_write_file_result(arena, path, gray_string_lit("hello world")).v0);
    ASSERT(gray_io_file_exists(path));
    ASSERT(gray_io_is_file(path));
    GrayString content = gray_io_read_file_result(arena, path).v0;
    ASSERT_GRAY_STR(content, "hello world");
    ASSERT_EQ(gray_io_file_size_result(arena, path).v0, 11);
    ASSERT(gray_io_delete_file_result(arena, path).v0);
    ASSERT(!gray_io_file_exists(path));
}

static void test_io_append_file(void) {
    GrayString path = io_tmp_path("grayc_ut_append.txt");
    ASSERT(gray_io_write_file_result(arena, path, gray_string_lit("a")).v0);
    ASSERT(gray_io_append_file(path, gray_string_lit("b")));
    GrayString content = gray_io_read_file_result(arena, path).v0;
    ASSERT_GRAY_STR(content, "ab");
    gray_io_delete_file_result(arena, path);
}

static void test_io_read_lines(void) {
    GrayString path = io_tmp_path("grayc_ut_lines.txt");
    ASSERT(gray_io_write_file_result(arena, path, gray_string_lit("one\ntwo\nthree")).v0);
    GrayArray lines = gray_io_read_lines_result(arena, path, 0).v0;
    ASSERT_EQ(lines.len, 3);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(lines, GrayString, 0), gray_string_lit("one")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(lines, GrayString, 2), gray_string_lit("three")));
    /* limit caps the count; asking for more than the file has yields all */
    GrayArray head = gray_io_read_lines_result(arena, path, 2).v0;
    ASSERT_EQ(head.len, 2);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(head, GrayString, 1), gray_string_lit("two")));
    GrayArray over = gray_io_read_lines_result(arena, path, 99).v0;
    ASSERT_EQ(over.len, 3);
    gray_io_delete_file_result(arena, path);
}

static void test_io_bytes_roundtrip(void) {
    GrayString path = io_tmp_path("grayc_ut_bytes.bin");
    uint8_t bytes[3] = {72, 105, 33};
    GrayArray data = gray_array_from(arena, bytes, sizeof(uint8_t), 3, GRAY_ELEM_U8);
    ASSERT(gray_io_write_bytes_result(arena, path, data).v0);
    GrayArray back = gray_io_read_bytes_result(arena, path).v0;
    ASSERT_EQ(back.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(back, uint8_t, 0), 72);
    ASSERT_EQ(GRAY_ARRAY_GET(back, uint8_t, 2), 33);
    gray_io_delete_file_result(arena, path);
}

static void test_io_read_file_result_err(void) {
    GrayResult_string r = gray_io_read_file_result(arena, gray_string_lit("/no/such/grayc/path/x.txt"));
    ASSERT_NOT_NULL(r.v1);
}

static void test_io_make_remove_dir(void) {
    GrayString path = io_tmp_path("grayc_ut_dir");
    ASSERT(gray_io_make_dir(path));
    ASSERT(gray_io_is_directory(path));
    ASSERT(gray_io_remove_dir(path));
    ASSERT(!gray_io_file_exists(path));
}

static void test_io_dirname(void) {
    GrayString r = gray_io_dirname(arena, gray_string_lit("/usr/local/bin/gray"));
    ASSERT_GRAY_STR(r, "/usr/local/bin");
}

static void test_io_basename(void) {
    GrayString r = gray_io_basename(arena, gray_string_lit("/usr/local/bin/gray"));
    ASSERT_GRAY_STR(r, "gray");
}

static void test_io_extension(void) {
    ASSERT_GRAY_STR(gray_io_extension(arena, gray_string_lit("main.gray")), ".gray");
    ASSERT_GRAY_STR(gray_io_extension(arena, gray_string_lit("noext")), "");
}

static void test_io_path_join(void) {
    GrayArray parts = GRAY_ARRAY_FROM_STR(arena, gray_string_lit("a"),
        gray_string_lit("b"), gray_string_lit("c"));
    GrayString r = gray_io_path_join(arena, parts);
    ASSERT_GRAY_STR(r, "a/b/c");
}

static void test_io_is_absolute(void) {
    ASSERT(gray_io_is_absolute(gray_string_lit("/etc/hosts")));
    ASSERT(!gray_io_is_absolute(gray_string_lit("etc/hosts")));
}

static void test_io_normalize(void) {
    GrayString r = gray_io_normalize(arena, gray_string_lit("a/./b/../c"));
    ASSERT_GRAY_STR(r, "a/c");
}

/* ===== regex module ===== */

static void test_regex_is_valid(void) {
    ASSERT(gray_regex_is_valid(gray_string_lit("[a-z]+")));
    ASSERT(gray_regex_is_valid(gray_string_lit("^[0-9]{3}$")));
    ASSERT(!gray_regex_is_valid(gray_string_lit("[a-z")));
    ASSERT(!gray_regex_is_valid(gray_string_lit("(unclosed")));
}

static void test_regex_match(void) {
    ASSERT(gray_regex_match(gray_string_lit("^h.llo$"), gray_string_lit("hello")));
    ASSERT(gray_regex_match(gray_string_lit("[0-9]+"), gray_string_lit("abc123")));
    ASSERT(!gray_regex_match(gray_string_lit("^[0-9]+$"), gray_string_lit("abc123")));
}

static void test_regex_find(void) {
    GrayString r = gray_regex_find_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("ab12cd345")).v0;
    ASSERT_GRAY_STR(r, "12");
}

static void test_regex_find_no_match(void) {
    GrayString r = gray_regex_find_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("no digits")).v0;
    ASSERT_EQ(r.len, 0);
}

static void test_regex_find_all(void) {
    GrayArray all = gray_regex_find_all_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("a1b22c333")).v0;
    ASSERT_EQ(all.len, 3);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(all, GrayString, 1), gray_string_lit("22")));
}

static void test_regex_replace(void) {
    GrayString r = gray_regex_replace_result(arena, gray_string_lit("[0-9]+"),
        gray_string_lit("a1b2c3"), gray_string_lit("#")).v0;
    ASSERT_GRAY_STR(r, "a#b#c#");
}

static void test_regex_split(void) {
    GrayArray parts = gray_regex_split_result(arena, gray_string_lit("[,;]"),
        gray_string_lit("a,b;c,d")).v0;
    ASSERT_EQ(parts.len, 4);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(parts, GrayString, 3), gray_string_lit("d")));
}

static void test_regex_find_result_err(void) {
    GrayResult_string r = gray_regex_find_result(arena, gray_string_lit("[a-z"),
        gray_string_lit("text"));
    ASSERT_NOT_NULL(r.v1);
}

/* ===== builtins module ===== */

/* Not currently reachable from Grayscale source — no bare `sleep()` is
 * wired into the typechecker/codegen (only @threads' threads.sleep is).
 * Tested at the zero-duration boundary so the suite doesn't actually wait. */
static void test_builtin_sleep_s_zero(void) {
    gray_builtin_sleep_s(0);
}

static void test_builtin_sleep_ms_zero(void) {
    gray_builtin_sleep_ms(0);
}

static void test_builtin_sleep_ns_zero(void) {
    gray_builtin_sleep_ns(0);
}

/* ===== net module ===== */

/* gray_net_listen, gray_net_accept, gray_net_send, gray_net_recv, and
 * gray_net_set_timeout have no dedicated coverage anywhere (the integration
 * suite only exercises resolve/listen/close and a connect-refused error) —
 * a real accept+send+recv round trip needs a concurrent peer, so the client
 * side runs on its own thread with its own arena (GrayArena isn't safe to
 * share across threads without the language's own per-thread isolation). */
#define TEST_NET_PORT 18734

static void *net_client_thread(void *arg) {
    (void)arg;
    GrayArena *client_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    GraySocket sock = gray_net_dial(client_arena, gray_string_lit("127.0.0.1"), TEST_NET_PORT);
    gray_net_send(sock, gray_string_lit("hello"));
    gray_net_close(sock);
    gray_arena_destroy(client_arena, __FILE__, __LINE__);
    return NULL;
}

static void test_net_listen_host_accept_send_recv(void) {
    GraySocket listener = gray_net_listen(arena, TEST_NET_PORT, gray_string_lit("127.0.0.1"));
    gray_net_set_timeout(listener, 2000);

    pthread_t client;
    pthread_create(&client, NULL, net_client_thread, NULL);

    GraySocket accepted = gray_net_accept(arena, listener);
    GrayString received = gray_net_recv(arena, accepted, 64);
    ASSERT_GRAY_STR(received, "hello");

    pthread_join(client, NULL);
    gray_net_close(accepted);
    gray_net_close(listener);
}

static void test_builtin_input(void) {
    FILE *saved_stdin = stdin;
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    fputs("hello world\n", tmp);
    rewind(tmp);
    stdin = tmp;
    GrayString r = gray_builtin_input(arena);
    stdin = saved_stdin;
    fclose(tmp);
    ASSERT_GRAY_STR(r, "hello world");
}

/* ===== csv module ===== */

static GrayString csv_cell_at(GrayArray data, int32_t row, int32_t column) {
    GrayArray row_array = GRAY_ARRAY_GET(data, GrayArray, row);
    return GRAY_ARRAY_GET(row_array, GrayString, column);
}

#define ASSERT_CSV_CELL(data, row, column, expected) do { \
    GrayString _cell = csv_cell_at((data), (row), (column)); \
    ASSERT_GRAY_STR(_cell, expected); \
} while(0)

static void test_csv_parse(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b\n1,2"));
    ASSERT_EQ(data.len, 2);
    ASSERT_CSV_CELL(data, 0, 0, "a");
    ASSERT_CSV_CELL(data, 0, 1, "b");
    ASSERT_CSV_CELL(data, 1, 0, "1");
    ASSERT_CSV_CELL(data, 1, 1, "2");
}

static void test_csv_parse_quoted_fields(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("\"x,y\",\"say \"\"hi\"\"\",\"l1\nl2\""));
    ASSERT_EQ(data.len, 1);
    ASSERT_CSV_CELL(data, 0, 0, "x,y");
    ASSERT_CSV_CELL(data, 0, 1, "say \"hi\"");
    ASSERT_CSV_CELL(data, 0, 2, "l1\nl2");
}

static void test_csv_parse_delimited_semicolon(void) {
    GrayArray data = gray_csv_parse_delimited(arena, gray_string_lit("a;b\n1;2"), ';');
    ASSERT_EQ(data.len, 2);
    ASSERT_CSV_CELL(data, 0, 1, "b");
    ASSERT_CSV_CELL(data, 1, 0, "1");
}

static void test_csv_parse_delimited_tab_keeps_commas(void) {
    GrayArray data = gray_csv_parse_delimited(arena, gray_string_lit("a,b\tc"), '\t');
    ASSERT_CSV_CELL(data, 0, 0, "a,b");
    ASSERT_CSV_CELL(data, 0, 1, "c");
}

static void test_csv_parse_delimited_quoting_still_applies(void) {
    GrayArray data = gray_csv_parse_delimited(arena, gray_string_lit("\"a;b\";c"), ';');
    ASSERT_CSV_CELL(data, 0, 0, "a;b");
    ASSERT_CSV_CELL(data, 0, 1, "c");
}

static void test_csv_detect_delimiter(void) {
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("a;b;c")), ';');
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("a\tb\tc")), '\t');
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("a|b|c,d")), '|');
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("a,b,c")), ',');
}

static void test_csv_detect_delimiter_default_comma(void) {
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("abc")), ',');
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("")), ',');
}

static void test_csv_detect_delimiter_first_line_only(void) {
    ASSERT_EQ(gray_csv_detect_delimiter(gray_string_lit("a,b\n;;;;;")), ',');
}

static void test_csv_headers(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("name,age\nann,30"));
    GrayArray headers = gray_csv_headers(arena, &data);
    ASSERT_EQ(headers.len, 2);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(headers, GrayString, 0), gray_string_lit("name")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(headers, GrayString, 1), gray_string_lit("age")));
}

static void test_csv_to_maps(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("name,age\nann,30\nbob,41"));
    GrayArray rows = gray_csv_to_maps(arena, &data);
    ASSERT_EQ(rows.len, 2);
    GrayMap first = GRAY_ARRAY_GET(rows, GrayMap, 0);
    GrayMap second = GRAY_ARRAY_GET(rows, GrayMap, 1);
    GrayString *name = gray_map_get_str(&first, gray_string_lit("name"));
    GrayString *age = gray_map_get_str(&second, gray_string_lit("age"));
    ASSERT_NOT_NULL(name);
    ASSERT_NOT_NULL(age);
    ASSERT(gray_string_eq(*name, gray_string_lit("ann")));
    ASSERT(gray_string_eq(*age, gray_string_lit("41")));
}

static void test_csv_to_maps_short_row_omits_keys(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b,c\n1,2"));
    GrayArray rows = gray_csv_to_maps(arena, &data);
    GrayMap row = GRAY_ARRAY_GET(rows, GrayMap, 0);
    ASSERT_NOT_NULL(gray_map_get_str(&row, gray_string_lit("b")));
    ASSERT(gray_map_get_str(&row, gray_string_lit("c")) == NULL);
}

static void test_csv_to_maps_long_row_drops_extras(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b\n1,2,3"));
    GrayArray rows = gray_csv_to_maps(arena, &data);
    GrayMap row = GRAY_ARRAY_GET(rows, GrayMap, 0);
    ASSERT_EQ(row.count, 2);
}

static void test_csv_to_maps_header_only_is_empty(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b"));
    GrayArray rows = gray_csv_to_maps(arena, &data);
    ASSERT_EQ(rows.len, 0);
}

static void test_csv_from_maps_round_trip(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("name,age\nann,30\nbob,41"));
    GrayArray rows = gray_csv_to_maps(arena, &data);
    GrayArray rebuilt = gray_csv_from_maps(arena, &rows);
    ASSERT_EQ(rebuilt.len, 3);
    ASSERT_CSV_CELL(rebuilt, 0, 0, "name");
    ASSERT_CSV_CELL(rebuilt, 0, 1, "age");
    ASSERT_CSV_CELL(rebuilt, 2, 0, "bob");
    ASSERT_CSV_CELL(rebuilt, 2, 1, "41");
}

static void test_csv_from_maps_header_is_key_union_in_first_seen_order(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b\n1,2"));
    GrayArray first_rows = gray_csv_to_maps(arena, &data);
    GrayArray other = gray_csv_parse(arena, gray_string_lit("b,c\n3,4"));
    GrayArray other_rows = gray_csv_to_maps(arena, &other);
    GrayMap first = GRAY_ARRAY_GET(first_rows, GrayMap, 0);
    GrayMap second = GRAY_ARRAY_GET(other_rows, GrayMap, 0);
    GrayArray rows = gray_array_new(arena, sizeof(GrayMap), 2, GRAY_ELEM_MAP);
    GRAY_ARRAY_PUSH(arena, &rows, &first);
    GRAY_ARRAY_PUSH(arena, &rows, &second);
    GrayArray rebuilt = gray_csv_from_maps(arena, &rows);
    ASSERT_EQ(rebuilt.len, 3);
    ASSERT_CSV_CELL(rebuilt, 0, 0, "a");
    ASSERT_CSV_CELL(rebuilt, 0, 1, "b");
    ASSERT_CSV_CELL(rebuilt, 0, 2, "c");
    ASSERT_CSV_CELL(rebuilt, 1, 2, "");
    ASSERT_CSV_CELL(rebuilt, 2, 0, "");
    ASSERT_CSV_CELL(rebuilt, 2, 2, "4");
}

static void test_csv_column(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("name,age\nann,30\nbob,41"));
    GrayArray column = gray_csv_column(arena, &data, gray_string_lit("age"));
    ASSERT_EQ(column.len, 2);
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(column, GrayString, 0), gray_string_lit("30")));
    ASSERT(gray_string_eq(GRAY_ARRAY_GET(column, GrayString, 1), gray_string_lit("41")));
}

static void test_csv_select_reorders_and_keeps_header(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b,c\n1,2,3"));
    GrayString names_data[] = { gray_string_lit("c"), gray_string_lit("a") };
    GrayArray names = gray_array_from(arena, names_data, sizeof(GrayString), 2, GRAY_ELEM_STRING);
    GrayArray projected = gray_csv_select(arena, &data, &names);
    ASSERT_EQ(projected.len, 2);
    ASSERT_CSV_CELL(projected, 0, 0, "c");
    ASSERT_CSV_CELL(projected, 0, 1, "a");
    ASSERT_CSV_CELL(projected, 1, 0, "3");
    ASSERT_CSV_CELL(projected, 1, 1, "1");
}

static void test_csv_sort_by_column_is_stable_and_keeps_header_first(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("n,v\nb,2\na,1\nc,3\na,0"));
    GrayArray sorted = gray_csv_sort_by_column(arena, &data, gray_string_lit("n"));
    ASSERT_EQ(sorted.len, 5);
    ASSERT_CSV_CELL(sorted, 0, 0, "n");
    ASSERT_CSV_CELL(sorted, 1, 1, "1");
    ASSERT_CSV_CELL(sorted, 2, 1, "0");
    ASSERT_CSV_CELL(sorted, 3, 0, "b");
    ASSERT_CSV_CELL(sorted, 4, 0, "c");
}

static void test_csv_sort_by_column_is_lexicographic(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("n\n10\n9\n2"));
    GrayArray sorted = gray_csv_sort_by_column(arena, &data, gray_string_lit("n"));
    ASSERT_CSV_CELL(sorted, 1, 0, "10");
    ASSERT_CSV_CELL(sorted, 2, 0, "2");
    ASSERT_CSV_CELL(sorted, 3, 0, "9");
}

static void test_csv_to_json(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b\n1,2\n3,4"));
    GrayString json = gray_csv_to_json(arena, &data);
    ASSERT_GRAY_STR(json, "[{\"a\":\"1\",\"b\":\"2\"},{\"a\":\"3\",\"b\":\"4\"}]");
}

static void test_csv_to_json_header_only_is_empty_array(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b"));
    GrayString json = gray_csv_to_json(arena, &data);
    ASSERT_GRAY_STR(json, "[]");
}

static void test_csv_to_markdown(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("a,b\n1,x|y"));
    GrayString table = gray_csv_to_markdown(arena, &data);
    ASSERT(gray_strings_contains(table, gray_string_lit("---")));
    ASSERT(gray_strings_contains(table, gray_string_lit("x\\|y")));
    ASSERT_EQ(table.data[table.len - 1], '\n');
}

static void test_csv_stringify_quotes_special_fields(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("\"a,b\",\"say \"\"hi\"\"\",\"l1\nl2\",plain"));
    GrayString text = gray_csv_stringify(arena, &data);
    ASSERT(gray_strings_contains(text, gray_string_lit("\"a,b\"")));
    ASSERT(gray_strings_contains(text, gray_string_lit("\"say \"\"hi\"\"\"")));
    ASSERT(gray_strings_contains(text, gray_string_lit("\"l1\nl2\"")));
    ASSERT(gray_strings_contains(text, gray_string_lit(",plain")));
}

static void test_csv_stringify_parse_round_trip(void) {
    GrayArray data = gray_csv_parse(arena, gray_string_lit("\"a,b\",\"q\"\"q\"\nx,y"));
    GrayString text = gray_csv_stringify(arena, &data);
    GrayArray again = gray_csv_parse(arena, text);
    ASSERT_EQ(again.len, 2);
    ASSERT_CSV_CELL(again, 0, 0, "a,b");
    ASSERT_CSV_CELL(again, 0, 1, "q\"q");
    ASSERT_CSV_CELL(again, 1, 1, "y");
}

/* ===== crypto module ===== */

static void test_crypto_sha256(void) {
    ASSERT_GRAY_STR(gray_crypto_sha256(arena, gray_string_lit("abc")),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ASSERT_GRAY_STR(gray_crypto_sha256(arena, gray_string_lit("")),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

static void test_crypto_sha1(void) {
    ASSERT_GRAY_STR(gray_crypto_sha1(arena, gray_string_lit("abc")),
        "a9993e364706816aba3e25717850c26c9cd0d89d");
}

static void test_crypto_sha512(void) {
    ASSERT_GRAY_STR(gray_crypto_sha512(arena, gray_string_lit("abc")),
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}

static void test_crypto_md5(void) {
    ASSERT_GRAY_STR(gray_crypto_md5(arena, gray_string_lit("abc")),
        "900150983cd24fb0d6963f7d28e17f72");
}

static void test_crypto_hmac_sha256(void) {
    ASSERT_GRAY_STR(gray_crypto_hmac_sha256(arena, gray_string_lit("key"),
        gray_string_lit("The quick brown fox jumps over the lazy dog")),
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

static void test_crypto_hmac_sha1(void) {
    ASSERT_GRAY_STR(gray_crypto_hmac_sha1(arena, gray_string_lit("key"),
        gray_string_lit("The quick brown fox jumps over the lazy dog")),
        "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
}

static void test_crypto_constant_time_equal(void) {
    ASSERT(gray_crypto_constant_time_equal(gray_string_lit("secret"), gray_string_lit("secret")));
    ASSERT(!gray_crypto_constant_time_equal(gray_string_lit("secret"), gray_string_lit("secreT")));
    ASSERT(gray_crypto_constant_time_equal(gray_string_lit(""), gray_string_lit("")));
}

static void test_crypto_constant_time_equal_length_difference(void) {
    ASSERT(!gray_crypto_constant_time_equal(gray_string_lit("abc"), gray_string_lit("abcd")));
    ASSERT(!gray_crypto_constant_time_equal(gray_string_lit("abcd"), gray_string_lit("abc")));
    ASSERT(!gray_crypto_constant_time_equal(gray_string_lit(""), gray_string_lit("a")));
}

static void test_crypto_crc32(void) {
    ASSERT_EQ(gray_crypto_crc32(gray_string_lit("123456789")), 0xCBF43926u);
    ASSERT_EQ(gray_crypto_crc32(gray_string_lit("")), 0u);
}

static void test_crypto_entropy(void) {
    ASSERT_FLOAT_EQ(gray_crypto_entropy(gray_string_lit("")), 0.0);
    ASSERT_FLOAT_EQ(gray_crypto_entropy(gray_string_lit("aaaa")), 0.0);
    ASSERT_FLOAT_EQ(gray_crypto_entropy(gray_string_lit("ab")), 1.0);
    ASSERT_FLOAT_EQ(gray_crypto_entropy(gray_string_lit("abcd")), 2.0);
}

static void test_crypto_totp_rfc6238_vectors(void) {
    GrayString secret = gray_string_lit("12345678901234567890");
    ASSERT_GRAY_STR(gray_crypto_totp(arena, secret, 59, 8), "94287082");
    ASSERT_GRAY_STR(gray_crypto_totp(arena, secret, 1111111109, 8), "07081804");
    ASSERT_GRAY_STR(gray_crypto_totp(arena, secret, 20000000000, 8), "65353130");
}

static void test_crypto_totp_digits_zero_padded_to_width(void) {
    GrayString secret = gray_string_lit("12345678901234567890");
    ASSERT_GRAY_STR(gray_crypto_totp(arena, secret, 59, 6), "287082");
    ASSERT_GRAY_STR(gray_crypto_totp(arena, secret, 1111111109, 8), "07081804");
}

/* ===== uuid module ===== */

static void test_uuid_parse_to_string_round_trip(void) {
    GrayUUID id = gray_uuid_parse(arena, gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
    ASSERT_GRAY_STR(gray_uuid_to_string(id), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
}

static void test_uuid_parse_normalizes_to_lowercase(void) {
    GrayUUID id = gray_uuid_parse(arena, gray_string_lit("6BA7B810-9DAD-11D1-80B4-00C04FD430C8"));
    ASSERT_GRAY_STR(gray_uuid_to_string(id), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
}

static void test_uuid_is_valid(void) {
    ASSERT(gray_uuid_is_valid(gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8")));
    ASSERT(!gray_uuid_is_valid(gray_string_lit("")));
    ASSERT(!gray_uuid_is_valid(gray_string_lit("not-a-uuid")));
    ASSERT(!gray_uuid_is_valid(gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c")));
    ASSERT(!gray_uuid_is_valid(gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430cz")));
    ASSERT(!gray_uuid_is_valid(gray_string_lit("6ba7b8109dad11d180b400c04fd430c8")));
}

static void test_uuid_nil(void) {
    GrayUUID nil = gray_uuid_nil();
    ASSERT_GRAY_STR(gray_uuid_to_string(nil), "00000000-0000-0000-0000-000000000000");
    ASSERT_EQ(gray_uuid_version(nil), 0);
}

static void test_uuid_generate_v5_known_vector(void) {
    GrayUUID dns_namespace = gray_uuid_parse(arena, gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
    GrayUUID id = gray_uuid_generate_v5(arena, dns_namespace, gray_string_lit("python.org"));
    ASSERT_GRAY_STR(gray_uuid_to_string(id), "886313e1-3b8a-5372-9b90-0c9aee199e5d");
    ASSERT_EQ(gray_uuid_version(id), 5);
}

static void test_uuid_generate_v5_is_deterministic(void) {
    GrayUUID dns_namespace = gray_uuid_parse(arena, gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
    GrayUUID first = gray_uuid_generate_v5(arena, dns_namespace, gray_string_lit("a"));
    GrayUUID again = gray_uuid_generate_v5(arena, dns_namespace, gray_string_lit("a"));
    GrayUUID other = gray_uuid_generate_v5(arena, dns_namespace, gray_string_lit("b"));
    ASSERT(gray_string_eq(gray_uuid_to_string(first), gray_uuid_to_string(again)));
    ASSERT(!gray_string_eq(gray_uuid_to_string(first), gray_uuid_to_string(other)));
}

static void test_uuid_generate_is_v4(void) {
    GrayUUID id = gray_uuid_generate(arena);
    GrayString text = gray_uuid_to_string(id);
    ASSERT_EQ(text.len, 36);
    ASSERT(gray_uuid_is_valid(text));
    ASSERT_EQ(gray_uuid_version(id), 4);
    ASSERT_EQ(gray_uuid_version(gray_uuid_generate_random(arena)), 4);
}

static void test_uuid_generate_time_ordered_is_v7(void) {
    GrayUUID id = gray_uuid_generate_time_ordered(arena);
    ASSERT_EQ(gray_uuid_version(id), 7);
    ASSERT(gray_uuid_timestamp(id).v1);
}

static void test_uuid_generate_time_ordered_sorts_by_creation(void) {
    GrayUUID first = gray_uuid_generate_time_ordered(arena);
    struct timespec pause = { 0, 2000000 };
    nanosleep(&pause, NULL);
    GrayUUID second = gray_uuid_generate_time_ordered(arena);
    GrayString first_text = gray_uuid_to_string(first);
    GrayString second_text = gray_uuid_to_string(second);
    ASSERT(memcmp(first_text.data, second_text.data, 36) < 0);
}

static void test_uuid_timestamp_v7_vector(void) {
    GrayUUID id = gray_uuid_parse(arena, gray_string_lit("017F22E2-79B0-7CC3-98C4-DC0C0C07398F"));
    GrayUuidTimestamp stamp = gray_uuid_timestamp(id);
    ASSERT(stamp.v1);
    ASSERT_EQ(stamp.v0, 1645557742000);
}

static void test_uuid_timestamp_false_for_non_time_versions(void) {
    GrayUUID dns_namespace = gray_uuid_parse(arena, gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
    GrayUUID v5 = gray_uuid_generate_v5(arena, dns_namespace, gray_string_lit("python.org"));
    ASSERT(!gray_uuid_timestamp(v5).v1);
    ASSERT(!gray_uuid_timestamp(gray_uuid_generate(arena)).v1);
}

static void test_uuid_generate_compact(void) {
    GrayUUID id = gray_uuid_parse(arena, gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
    ASSERT_GRAY_STR(gray_uuid_generate_compact(arena, id), "6ba7b8109dad11d180b400c04fd430c8");
}

static void test_uuid_to_bytes_big_endian(void) {
    GrayUUID id = gray_uuid_parse(arena, gray_string_lit("00010203-0405-0607-0809-0a0b0c0d0e0f"));
    GrayArray bytes = gray_uuid_to_bytes(arena, id);
    ASSERT_EQ(bytes.len, 16);
    for (int32_t i = 0; i < 16; i++) {
        ASSERT_EQ(GRAY_ARRAY_GET(bytes, uint8_t, i), i);
    }
}

static void test_uuid_from_bytes_round_trip(void) {
    GrayUUID id = gray_uuid_parse(arena, gray_string_lit("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
    GrayArray bytes = gray_uuid_to_bytes(arena, id);
    GrayUUID rebuilt = gray_uuid_from_bytes(arena, &bytes);
    ASSERT_GRAY_STR(gray_uuid_to_string(rebuilt), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
}

/* ===== binary module ===== */

#define ASSERT_BYTES(bytes_expression, ...) do { \
    GrayArray _bytes = (bytes_expression); \
    const uint8_t _expected[] = { __VA_ARGS__ }; \
    ASSERT_EQ(_bytes.len, (int32_t)sizeof(_expected)); \
    for (int32_t _i = 0; _i < (int32_t)sizeof(_expected); _i++) { \
        ASSERT_EQ(GRAY_ARRAY_GET(_bytes, uint8_t, _i), _expected[_i]); \
    } \
} while(0)

static void test_binary_8bit(void) {
    GrayArray signed_byte = gray_binary_encode_i8(arena, -1);
    ASSERT_BYTES(signed_byte, 0xFF);
    ASSERT_EQ(gray_binary_decode_i8(&signed_byte, __FILE__, __LINE__), -1);
    GrayArray unsigned_byte = gray_binary_encode_u8(arena, 200);
    ASSERT_BYTES(unsigned_byte, 0xC8);
    ASSERT_EQ(gray_binary_decode_u8(&unsigned_byte, __FILE__, __LINE__), 200);
}

static void test_binary_16bit_byte_order(void) {
    ASSERT_BYTES(gray_binary_encode_u16_le(arena, 0x0102), 0x02, 0x01);
    ASSERT_BYTES(gray_binary_encode_u16_be(arena, 0x0102), 0x01, 0x02);
    ASSERT_BYTES(gray_binary_encode_i16_le(arena, -2), 0xFE, 0xFF);
    ASSERT_BYTES(gray_binary_encode_i16_be(arena, -2), 0xFF, 0xFE);
}

static void test_binary_32bit_byte_order(void) {
    ASSERT_BYTES(gray_binary_encode_u32_le(arena, 0x01020304u), 0x04, 0x03, 0x02, 0x01);
    ASSERT_BYTES(gray_binary_encode_u32_be(arena, 0x01020304u), 0x01, 0x02, 0x03, 0x04);
    ASSERT_BYTES(gray_binary_encode_i32_le(arena, -1), 0xFF, 0xFF, 0xFF, 0xFF);
}

static void test_binary_64bit_byte_order(void) {
    ASSERT_BYTES(gray_binary_encode_u64_le(arena, 0x0102030405060708ull),
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01);
    ASSERT_BYTES(gray_binary_encode_u64_be(arena, 0x0102030405060708ull),
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
}

static void test_binary_integer_round_trip_extremes(void) {
    GrayArray bytes = gray_binary_encode_i16_le(arena, INT16_MIN);
    ASSERT_EQ(gray_binary_decode_i16_le(&bytes, __FILE__, __LINE__), INT16_MIN);
    bytes = gray_binary_encode_i16_be(arena, INT16_MAX);
    ASSERT_EQ(gray_binary_decode_i16_be(&bytes, __FILE__, __LINE__), INT16_MAX);
    bytes = gray_binary_encode_u16_be(arena, UINT16_MAX);
    ASSERT_EQ(gray_binary_decode_u16_be(&bytes, __FILE__, __LINE__), UINT16_MAX);
    bytes = gray_binary_encode_i32_be(arena, INT32_MIN);
    ASSERT_EQ(gray_binary_decode_i32_be(&bytes, __FILE__, __LINE__), INT32_MIN);
    bytes = gray_binary_encode_i32_le(arena, INT32_MAX);
    ASSERT_EQ(gray_binary_decode_i32_le(&bytes, __FILE__, __LINE__), INT32_MAX);
    bytes = gray_binary_encode_u32_le(arena, UINT32_MAX);
    ASSERT_EQ(gray_binary_decode_u32_le(&bytes, __FILE__, __LINE__), UINT32_MAX);
    bytes = gray_binary_encode_i64_le(arena, INT64_MIN);
    ASSERT_EQ(gray_binary_decode_i64_le(&bytes, __FILE__, __LINE__), INT64_MIN);
    bytes = gray_binary_encode_i64_be(arena, INT64_MAX);
    ASSERT_EQ(gray_binary_decode_i64_be(&bytes, __FILE__, __LINE__), INT64_MAX);
    bytes = gray_binary_encode_u64_be(arena, UINT64_MAX);
    ASSERT(gray_binary_decode_u64_be(&bytes, __FILE__, __LINE__) == UINT64_MAX);
    bytes = gray_binary_encode_u64_le(arena, UINT64_MAX);
    ASSERT(gray_binary_decode_u64_le(&bytes, __FILE__, __LINE__) == UINT64_MAX);
}

#define GRAY_U128_ONE ((gray_u128){1, 0})

static void test_binary_128bit_byte_order(void) {
    GrayArray little = gray_binary_encode_u128_le(arena, GRAY_U128_ONE);
    GrayArray big = gray_binary_encode_u128_be(arena, GRAY_U128_ONE);
    ASSERT_EQ(little.len, 16);
    ASSERT_EQ(big.len, 16);
    ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, 15), 0);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 0), 0);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 15), 1);
    ASSERT(gray_u128_eq(gray_binary_decode_u128_le(&little, __FILE__, __LINE__), GRAY_U128_ONE));
    ASSERT(gray_u128_eq(gray_binary_decode_u128_be(&big, __FILE__, __LINE__), GRAY_U128_ONE));
}

static void test_binary_float_byte_order(void) {
    ASSERT_BYTES(gray_binary_encode_f32_le(arena, 1.0f), 0x00, 0x00, 0x80, 0x3F);
    ASSERT_BYTES(gray_binary_encode_f32_be(arena, 1.0f), 0x3F, 0x80, 0x00, 0x00);
    ASSERT_BYTES(gray_binary_encode_f64_be(arena, 1.0),
        0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
    ASSERT_BYTES(gray_binary_encode_f64_le(arena, 1.0),
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F);
}

static void test_binary_float_round_trip(void) {
    GrayArray bytes = gray_binary_encode_f64_le(arena, -3.5);
    ASSERT_FLOAT_EQ(gray_binary_decode_f64_le(&bytes, __FILE__, __LINE__), -3.5);
    bytes = gray_binary_encode_f64_be(arena, 1234.5678);
    ASSERT_FLOAT_EQ(gray_binary_decode_f64_be(&bytes, __FILE__, __LINE__), 1234.5678);
    bytes = gray_binary_encode_f32_le(arena, 0.25f);
    ASSERT_FLOAT_EQ(gray_binary_decode_f32_le(&bytes, __FILE__, __LINE__), 0.25);
    bytes = gray_binary_encode_f32_be(arena, -8.5f);
    ASSERT_FLOAT_EQ(gray_binary_decode_f32_be(&bytes, __FILE__, __LINE__), -8.5);
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
    RUN_TEST(test_strings_append_char);
    RUN_TEST(test_strings_prepend_char);
    RUN_TEST(test_strings_insert_char_at);
    RUN_TEST(test_strings_remove_at);
    RUN_TEST(test_strings_set_char_at);

    printf("--- chars ---\n");
    RUN_TEST(test_chars_to_upper);
    RUN_TEST(test_chars_to_lower);

    printf("--- arrays ---\n");
    RUN_TEST(test_arrays_append);
    RUN_TEST(test_arrays_insert_at);
    RUN_TEST(test_arrays_prepend);
    RUN_TEST(test_arrays_remove_at);
    RUN_TEST(test_arrays_remove_int);
    RUN_TEST(test_arrays_remove_float);
    RUN_TEST(test_arrays_remove_str);
    RUN_TEST(test_arrays_clear);
    RUN_TEST(test_arrays_fill);
    RUN_TEST(test_arrays_get_first);
    RUN_TEST(test_arrays_get_last);
    RUN_TEST(test_arrays_remove_first);
    RUN_TEST(test_arrays_remove_last);
    RUN_TEST(test_arrays_is_empty);
    RUN_TEST(test_arrays_contains_i64);
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
    RUN_TEST(test_fmt_i64_to_hex);
    RUN_TEST(test_fmt_i64_to_binary);
    RUN_TEST(test_fmt_i64_to_octal);
    RUN_TEST(test_fmt_f64_to_fixed);
    RUN_TEST(test_fmt_f64_to_scientific);

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
    RUN_TEST(test_strconv_to_i64);
    RUN_TEST(test_strconv_to_u64);
    RUN_TEST(test_strconv_to_f64);
    RUN_TEST(test_strconv_to_bool);
    RUN_TEST(test_strconv_to_i64_result_ok);
    RUN_TEST(test_strconv_to_i64_result_err);
    RUN_TEST(test_strconv_to_u64_result_negative);
    RUN_TEST(test_strconv_to_f64_result_ok);
    RUN_TEST(test_strconv_to_f64_result_err);
    RUN_TEST(test_strconv_to_bool_result_ok);
    RUN_TEST(test_strconv_to_bool_result_err);
    RUN_TEST(test_strconv_from_i64);
    RUN_TEST(test_strconv_from_u64);
    RUN_TEST(test_strconv_from_f64);
    RUN_TEST(test_strconv_from_bool);
    RUN_TEST(test_strconv_is_numeric);
    RUN_TEST(test_strconv_is_integer);
    RUN_TEST(test_strconv_format_i64);
    RUN_TEST(test_strconv_format_u64);
    RUN_TEST(test_strconv_format_i64_roundtrip);
    RUN_TEST(test_strconv_quote);
    RUN_TEST(test_strconv_unquote_ok);
    RUN_TEST(test_strconv_unquote_err);
    RUN_TEST(test_strconv_quote_unquote_roundtrip);

    printf("--- json ---\n");
    RUN_TEST(test_json_encode_map);
    RUN_TEST(test_json_encode_map_string_escaped_key);
    RUN_TEST(test_json_encode_map_int_escaped_key);
    RUN_TEST(test_json_encode_map_float_escaped_key);
    RUN_TEST(test_json_encode_map_bool_escaped_key);
    RUN_TEST(test_json_encode_array_int);
    RUN_TEST(test_json_encode_array_float);
    RUN_TEST(test_json_encode_array_bool);
    RUN_TEST(test_json_encode_array_string);
    RUN_TEST(test_json_is_valid);
    RUN_TEST(test_json_decode);
    RUN_TEST(test_json_decode_result_ok);
    RUN_TEST(test_json_decode_result_err);
    RUN_TEST(test_json_roundtrip);
    RUN_TEST(test_json_pretty_map);
    RUN_TEST(test_json_split_array);

    printf("--- io ---\n");
    RUN_TEST(test_io_write_read_roundtrip);
    RUN_TEST(test_io_append_file);
    RUN_TEST(test_io_read_lines);
    RUN_TEST(test_io_bytes_roundtrip);
    RUN_TEST(test_io_read_file_result_err);
    RUN_TEST(test_io_make_remove_dir);
    RUN_TEST(test_io_dirname);
    RUN_TEST(test_io_basename);
    RUN_TEST(test_io_extension);
    RUN_TEST(test_io_path_join);
    RUN_TEST(test_io_is_absolute);
    RUN_TEST(test_io_normalize);

    printf("--- regex ---\n");
    RUN_TEST(test_regex_is_valid);
    RUN_TEST(test_regex_match);
    RUN_TEST(test_regex_find);
    RUN_TEST(test_regex_find_no_match);
    RUN_TEST(test_regex_find_all);
    RUN_TEST(test_regex_replace);
    RUN_TEST(test_regex_split);
    RUN_TEST(test_regex_find_result_err);

    printf("--- net ---\n");
    RUN_TEST(test_net_listen_host_accept_send_recv);

    printf("--- builtins ---\n");
    RUN_TEST(test_builtin_sleep_s_zero);
    RUN_TEST(test_builtin_sleep_ms_zero);
    RUN_TEST(test_builtin_sleep_ns_zero);
    RUN_TEST(test_builtin_input);

    printf("--- csv ---\n");
    RUN_TEST(test_csv_parse);
    RUN_TEST(test_csv_parse_quoted_fields);
    RUN_TEST(test_csv_parse_delimited_semicolon);
    RUN_TEST(test_csv_parse_delimited_tab_keeps_commas);
    RUN_TEST(test_csv_parse_delimited_quoting_still_applies);
    RUN_TEST(test_csv_detect_delimiter);
    RUN_TEST(test_csv_detect_delimiter_default_comma);
    RUN_TEST(test_csv_detect_delimiter_first_line_only);
    RUN_TEST(test_csv_headers);
    RUN_TEST(test_csv_to_maps);
    RUN_TEST(test_csv_to_maps_short_row_omits_keys);
    RUN_TEST(test_csv_to_maps_long_row_drops_extras);
    RUN_TEST(test_csv_to_maps_header_only_is_empty);
    RUN_TEST(test_csv_from_maps_round_trip);
    RUN_TEST(test_csv_from_maps_header_is_key_union_in_first_seen_order);
    RUN_TEST(test_csv_column);
    RUN_TEST(test_csv_select_reorders_and_keeps_header);
    RUN_TEST(test_csv_sort_by_column_is_stable_and_keeps_header_first);
    RUN_TEST(test_csv_sort_by_column_is_lexicographic);
    RUN_TEST(test_csv_to_json);
    RUN_TEST(test_csv_to_json_header_only_is_empty_array);
    RUN_TEST(test_csv_to_markdown);
    RUN_TEST(test_csv_stringify_quotes_special_fields);
    RUN_TEST(test_csv_stringify_parse_round_trip);

    printf("--- crypto ---\n");
    RUN_TEST(test_crypto_sha256);
    RUN_TEST(test_crypto_sha1);
    RUN_TEST(test_crypto_sha512);
    RUN_TEST(test_crypto_md5);
    RUN_TEST(test_crypto_hmac_sha256);
    RUN_TEST(test_crypto_hmac_sha1);
    RUN_TEST(test_crypto_constant_time_equal);
    RUN_TEST(test_crypto_constant_time_equal_length_difference);
    RUN_TEST(test_crypto_crc32);
    RUN_TEST(test_crypto_entropy);
    RUN_TEST(test_crypto_totp_rfc6238_vectors);
    RUN_TEST(test_crypto_totp_digits_zero_padded_to_width);

    printf("--- uuid ---\n");
    RUN_TEST(test_uuid_parse_to_string_round_trip);
    RUN_TEST(test_uuid_parse_normalizes_to_lowercase);
    RUN_TEST(test_uuid_is_valid);
    RUN_TEST(test_uuid_nil);
    RUN_TEST(test_uuid_generate_v5_known_vector);
    RUN_TEST(test_uuid_generate_v5_is_deterministic);
    RUN_TEST(test_uuid_generate_is_v4);
    RUN_TEST(test_uuid_generate_time_ordered_is_v7);
    RUN_TEST(test_uuid_generate_time_ordered_sorts_by_creation);
    RUN_TEST(test_uuid_timestamp_v7_vector);
    RUN_TEST(test_uuid_timestamp_false_for_non_time_versions);
    RUN_TEST(test_uuid_generate_compact);
    RUN_TEST(test_uuid_to_bytes_big_endian);
    RUN_TEST(test_uuid_from_bytes_round_trip);

    printf("--- binary ---\n");
    RUN_TEST(test_binary_8bit);
    RUN_TEST(test_binary_16bit_byte_order);
    RUN_TEST(test_binary_32bit_byte_order);
    RUN_TEST(test_binary_64bit_byte_order);
    RUN_TEST(test_binary_integer_round_trip_extremes);
    RUN_TEST(test_binary_128bit_byte_order);
    RUN_TEST(test_binary_float_byte_order);
    RUN_TEST(test_binary_float_round_trip);

    PRINT_RESULTS();
    gray_arena_destroy(arena, __FILE__, __LINE__);
    return _test_fail > 0 ? 1 : 0;
}
