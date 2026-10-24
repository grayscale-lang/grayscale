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
#include "../src/stdlib/random.h"
#include "../src/stdlib/mem.h"
#include "../src/stdlib/runtime_mod.h"
#include "../src/stdlib/atomic_mod.h"
#include "../src/stdlib/channels.h"
#include "../src/stdlib/time.h"
#include "../src/stdlib/threads.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

static GrayArena *arena;

static void sleep_microseconds(long microseconds) {
    struct timespec pause = { 0, microseconds * 1000 };
    nanosleep(&pause, NULL);
}

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

static void test_chars_classification(void) {
    ASSERT(gray_chars_is_alpha('a'));
    ASSERT(gray_chars_is_alpha('Z'));
    ASSERT(!gray_chars_is_alpha('1'));
    ASSERT(gray_chars_is_digit('5'));
    ASSERT(!gray_chars_is_digit('a'));
    ASSERT(gray_chars_is_alnum('a'));
    ASSERT(gray_chars_is_alnum('3'));
    ASSERT(!gray_chars_is_alnum('!'));
    ASSERT(gray_chars_is_whitespace(' '));
    ASSERT(gray_chars_is_whitespace('\t'));
    ASSERT(!gray_chars_is_whitespace('a'));
    ASSERT(gray_chars_is_upper('A'));
    ASSERT(!gray_chars_is_upper('a'));
    ASSERT(gray_chars_is_lower('a'));
    ASSERT(!gray_chars_is_lower('A'));
    ASSERT(!gray_chars_is_alpha(0x141));
    ASSERT(!gray_chars_is_alnum(0x141));
    ASSERT(!gray_chars_is_upper(0x141));
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

static void test_maps_contains_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(int64_t), sizeof(int64_t), 0, GRAY_ELEM_I64, GRAY_ELEM_I64);
    int64_t k = 5, v = 50;
    ASSERT(!gray_maps_contains_key(&m, &k));
    GRAY_MAP_SET(arena, &m, &k, &v);
    ASSERT(gray_maps_contains_key(&m, &k));
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

static void test_fmt_i64_to_binary(void) {
    ASSERT_GRAY_STR(gray_fmt_i64_to_binary(arena, 10), "1010");
    ASSERT_GRAY_STR(gray_fmt_i64_to_binary(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_i64_to_binary(arena, 1), "1");
}

static void test_fmt_f64_to_fixed(void) {
    ASSERT_GRAY_STR(gray_fmt_f64_to_fixed(arena, 3.14159, 2), "3.14");
    ASSERT_GRAY_STR(gray_fmt_f64_to_fixed(arena, 1.0, 0), "1");
}

/* ===== encoding module ===== */

static void test_encoding_base64_encode(void) {
    ASSERT_GRAY_STR(gray_encoding_base64_encode(arena, gray_string_lit("Hello")), "SGVsbG8=");
    ASSERT_GRAY_STR(gray_encoding_base64_encode(arena, gray_string_lit("Hi")), "SGk=");
    ASSERT_GRAY_STR(gray_encoding_base64_encode(arena, gray_string_lit("Man")), "TWFu");
}

static void test_encoding_base64_decode(void) {
    ASSERT_GRAY_STR(gray_encoding_base64_decode(arena, gray_string_lit("SGVsbG8=")).v0, "Hello");
    ASSERT_GRAY_STR(gray_encoding_base64_decode(arena, gray_string_lit("SGk=")).v0, "Hi");
    ASSERT_GRAY_STR(gray_encoding_base64_decode(arena, gray_string_lit("TWFu")).v0, "Man");
}

static void test_encoding_base64_roundtrip(void) {
    GrayString original = gray_string_lit("Grayscale is awesome!");
    GrayString encoded = gray_encoding_base64_encode(arena, original);
    GrayString decoded = gray_encoding_base64_decode(arena, encoded).v0;
    ASSERT(gray_string_eq(decoded, original));
}

static void test_encoding_hex_encode(void) {
    ASSERT_GRAY_STR(gray_encoding_hex_encode(arena, gray_string_lit("Hi")), "4869");
    ASSERT_GRAY_STR(gray_encoding_hex_encode(arena, gray_string_lit("AB")), "4142");
}

static void test_encoding_hex_decode(void) {
    ASSERT_GRAY_STR(gray_encoding_hex_decode(arena, gray_string_lit("4869")).v0, "Hi");
    ASSERT_GRAY_STR(gray_encoding_hex_decode(arena, gray_string_lit("4142")).v0, "AB");
}

static void test_encoding_hex_roundtrip(void) {
    GrayString original = gray_string_lit("test123");
    GrayString encoded = gray_encoding_hex_encode(arena, original);
    GrayString decoded = gray_encoding_hex_decode(arena, encoded).v0;
    ASSERT(gray_string_eq(decoded, original));
}

static void test_encoding_url_encode(void) {
    ASSERT_GRAY_STR(gray_encoding_url_encode(arena, gray_string_lit("hello world")), "hello%20world");
    ASSERT_GRAY_STR(gray_encoding_url_encode(arena, gray_string_lit("a+b=c")), "a%2Bb%3Dc");
}

static void test_encoding_url_decode(void) {
    ASSERT_GRAY_STR(gray_encoding_url_decode(arena, gray_string_lit("hello%20world")).v0, "hello world");
    ASSERT_GRAY_STR(gray_encoding_url_decode(arena, gray_string_lit("a+b")).v0, "a b");
}

static void test_encoding_url_roundtrip(void) {
    GrayString original = gray_string_lit("hello world & friends!");
    GrayString encoded = gray_encoding_url_encode(arena, original);
    GrayString decoded = gray_encoding_url_decode(arena, encoded).v0;
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
    GrayString r = gray_json_encode_map_signed_integer(arena, &m);
    ASSERT_GRAY_STR(r, "{\"a\\\"b\":-9223372036854775808}");
}

static void test_json_encode_map_float_escaped_key(void) {
    GrayMap m = gray_map_new_kind(arena, sizeof(GrayString), sizeof(double), 0, GRAY_ELEM_STRING, GRAY_ELEM_F64);
    GrayString k = gray_string_lit("k\ny");
    double v = 3.5;
    gray_map_set(arena, &m, &k, &v, __FILE__, __LINE__);
    GrayString r = gray_json_encode_map_floating_point(arena, &m);
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
    GrayString r = gray_json_encode_array_signed_integer(arena, &a);
    ASSERT_GRAY_STR(r, "[1,2,3]");
}

static void test_json_encode_array_float(void) {
    GrayArray a = GRAY_ARRAY_FROM_F64(arena, 1.5, 2.0, 3.25);
    GrayString r = gray_json_encode_array_floating_point(arena, &a);
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
 * wired into the typechecker/codegen. Tested at the zero-duration boundary so the suite doesn't actually wait. */
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
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    fputs("hello world\n", tmp);
    rewind(tmp);
    /* stdin is a macro on mingw and cannot be assigned, so the temporary file
     * takes over the standard input descriptor instead. */
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    GrayString r = gray_builtin_input(arena);
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
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

/* ===== random module ===== */

static void test_random_seed_makes_i64_range_reproducible(void) {
    int64_t first[8];
    gray_random_seed(42);
    for (int i = 0; i < 8; i++) first[i] = gray_random_i64_range(0, 1000000);
    gray_random_seed(42);
    for (int i = 0; i < 8; i++) ASSERT_EQ(gray_random_i64_range(0, 1000000), first[i]);
}

static void test_random_seed_makes_f64_unit_reproducible(void) {
    gray_random_seed(7);
    double first = gray_random_f64_unit();
    gray_random_seed(7);
    ASSERT_FLOAT_EQ(gray_random_f64_unit(), first);
}

static void test_random_seed_makes_bytes_reproducible(void) {
    gray_random_seed(99);
    GrayArray first = gray_random_bytes(arena, 16);
    gray_random_seed(99);
    GrayArray second = gray_random_bytes(arena, 16);
    ASSERT_EQ(first.len, 16);
    ASSERT_EQ(memcmp(first.data, second.data, 16), 0);
}

static void test_random_seed_makes_rand_string_reproducible(void) {
    GrayString alphabet = gray_string_lit("abcdefghijklmnopqrstuvwxyz");
    gray_random_seed(5);
    GrayString first = gray_random_string(arena, 20, alphabet);
    gray_random_seed(5);
    GrayString second = gray_random_string(arena, 20, alphabet);
    ASSERT(gray_string_eq(first, second));
}

static void test_random_seed_makes_shuffle_reproducible(void) {
    GrayArray source = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    gray_random_seed(11);
    GrayArray first = gray_random_shuffle(arena, &source);
    gray_random_seed(11);
    GrayArray second = gray_random_shuffle(arena, &source);
    ASSERT_EQ(memcmp(first.data, second.data, 10 * sizeof(int64_t)), 0);
}

static void test_random_f64_unit_stays_in_unit_interval(void) {
    gray_random_seed(1);
    for (int i = 0; i < 2000; i++) {
        double value = gray_random_f64_unit();
        ASSERT(value >= 0.0 && value < 1.0);
    }
}

static void test_random_f64_range_stays_in_bounds(void) {
    gray_random_seed(2);
    for (int i = 0; i < 2000; i++) {
        double value = gray_random_f64_range(-5.0, 5.0);
        ASSERT(value >= -5.0 && value < 5.0);
    }
}

static void test_random_i64_range_stays_in_bounds(void) {
    gray_random_seed(3);
    for (int i = 0; i < 2000; i++) {
        int64_t value = gray_random_i64_range(-10, 10);
        ASSERT(value >= -10 && value < 10);
    }
}

static void test_random_i64_range_width_one_returns_minimum(void) {
    for (int i = 0; i < 20; i++) ASSERT_EQ(gray_random_i64_range(7, 8), 7);
}

static void test_random_i64_range_reaches_both_ends(void) {
    bool seen_minimum = false, seen_maximum = false;
    gray_random_seed(4);
    for (int i = 0; i < 500; i++) {
        int64_t value = gray_random_i64_range(0, 3);
        if (value == 0) seen_minimum = true;
        if (value == 2) seen_maximum = true;
    }
    ASSERT(seen_minimum);
    ASSERT(seen_maximum);
}

static void test_random_i64_range_spans_full_i64_width(void) {
    gray_random_seed(6);
    for (int i = 0; i < 200; i++) {
        int64_t value = gray_random_i64_range(INT64_MIN, INT64_MAX);
        ASSERT(value < INT64_MAX);
    }
}

static void test_random_choice_index_stays_below_length(void) {
    gray_random_seed(8);
    for (int i = 0; i < 500; i++) {
        int32_t index = gray_random_choice_index(5, __FILE__, __LINE__);
        ASSERT(index >= 0 && index < 5);
    }
}

static void test_random_choice_index_length_one(void) {
    for (int i = 0; i < 20; i++) ASSERT_EQ(gray_random_choice_index(1, __FILE__, __LINE__), 0);
}

static void test_random_bool_produces_both_values(void) {
    bool seen_true = false, seen_false = false;
    gray_random_seed(9);
    for (int i = 0; i < 200; i++) {
        if (gray_random_bool()) seen_true = true; else seen_false = true;
    }
    ASSERT(seen_true);
    ASSERT(seen_false);
}

static void test_random_u8_covers_more_than_one_value(void) {
    gray_random_seed(10);
    uint8_t first = gray_random_u8();
    bool differs = false;
    for (int i = 0; i < 50; i++) if (gray_random_u8() != first) differs = true;
    ASSERT(differs);
}

static void test_random_bytes_length_and_element_kind(void) {
    GrayArray bytes = gray_random_bytes(arena, 33);
    ASSERT_EQ(bytes.len, 33);
    ASSERT_EQ(bytes.elem_size, 1);
    ASSERT_EQ(bytes.elem_kind, GRAY_ELEM_U8);
}

static void test_random_bytes_zero_is_empty(void) {
    GrayArray bytes = gray_random_bytes(arena, 0);
    ASSERT_EQ(bytes.len, 0);
}

static void test_random_roll_dice_two_six_sided_in_range(void) {
    gray_random_seed(12);
    for (int i = 0; i < 1000; i++) {
        int64_t total = gray_random_roll_dice(2, 6);
        ASSERT(total >= 2 && total <= 12);
    }
}

static void test_random_roll_dice_one_sided_die_is_count(void) {
    ASSERT_EQ(gray_random_roll_dice(5, 1), 5);
}

static void test_random_roll_dice_single_die_reaches_every_face(void) {
    bool seen[7] = {false};
    gray_random_seed(13);
    for (int i = 0; i < 500; i++) {
        int64_t roll = gray_random_roll_dice(1, 6);
        ASSERT(roll >= 1 && roll <= 6);
        seen[roll] = true;
    }
    for (int face = 1; face <= 6; face++) ASSERT(seen[face]);
}

static void test_random_char_is_printable_ascii(void) {
    gray_random_seed(14);
    for (int i = 0; i < 1000; i++) {
        int32_t codepoint = gray_random_char();
        ASSERT(codepoint >= 32 && codepoint <= 126);
    }
}

static void test_random_char_range_stays_in_bounds(void) {
    gray_random_seed(15);
    for (int i = 0; i < 1000; i++) {
        int32_t codepoint = gray_random_char_range('a', 'f');
        ASSERT(codepoint >= 'a' && codepoint < 'f');
    }
}

static void test_random_char_range_never_yields_surrogate(void) {
    gray_random_seed(16);
    for (int i = 0; i < 1000; i++) {
        int32_t codepoint = gray_random_char_range(0xD7FF, 0xE001);
        ASSERT(codepoint == 0xD7FF || codepoint == 0xE000);
    }
}

static void test_random_string_zero_length_is_empty(void) {
    GrayString result = gray_random_string(arena, 0, gray_string_lit("abc"));
    ASSERT_EQ(result.len, 0);
}

static void test_random_string_draws_only_from_alphabet(void) {
    GrayString result = gray_random_string(arena, 200, gray_string_lit("xyz"));
    ASSERT_EQ(result.len, 200);
    for (int32_t i = 0; i < result.len; i++)
        ASSERT(result.data[i] == 'x' || result.data[i] == 'y' || result.data[i] == 'z');
}

static void test_random_string_single_character_alphabet(void) {
    GrayString result = gray_random_string(arena, 4, gray_string_lit("q"));
    ASSERT_GRAY_STR(result, "qqqq");
}

static void test_random_string_length_counts_characters_not_bytes(void) {
    GrayString result = gray_random_string(arena, 10, gray_string_lit("é€"));
    ASSERT_EQ(gray_builtin_char_count(result), 10);
    ASSERT_GT(result.len, 10);
}

static void test_random_string_multibyte_alphabet_stays_valid(void) {
    GrayString result = gray_random_string(arena, 100, gray_string_lit("é€"));
    const uint8_t *cursor = (const uint8_t *)result.data;
    const uint8_t *end_cursor = cursor + result.len;
    while (cursor < end_cursor) {
        int32_t codepoint;
        cursor += gray_builtin_utf8_next(cursor, end_cursor, &codepoint);
        ASSERT(codepoint == 0xE9 || codepoint == 0x20AC);
    }
}

static int64_t sum_i64_array(GrayArray *array) {
    int64_t total = 0;
    for (int32_t i = 0; i < array->len; i++) total += GRAY_ARRAY_GET(*array, int64_t, i);
    return total;
}

static void test_random_shuffle_keeps_elements(void) {
    GrayArray source = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5, 6, 7, 8);
    GrayArray shuffled = gray_random_shuffle(arena, &source);
    ASSERT_EQ(shuffled.len, 8);
    ASSERT_EQ(sum_i64_array(&shuffled), 36);
    for (int64_t wanted = 1; wanted <= 8; wanted++) {
        int count = 0;
        for (int32_t i = 0; i < shuffled.len; i++)
            if (GRAY_ARRAY_GET(shuffled, int64_t, i) == wanted) count++;
        ASSERT_EQ(count, 1);
    }
}

static void test_random_shuffle_leaves_original_untouched(void) {
    GrayArray source = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5, 6, 7, 8);
    gray_random_shuffle(arena, &source);
    for (int32_t i = 0; i < source.len; i++)
        ASSERT_EQ(GRAY_ARRAY_GET(source, int64_t, i), i + 1);
}

static void test_random_shuffle_empty_and_single(void) {
    GrayArray empty = GRAY_ARRAY_NEW_OF(arena, int64_t, 0);
    ASSERT_EQ(gray_random_shuffle(arena, &empty).len, 0);
    GrayArray single = GRAY_ARRAY_FROM_I64(arena, 9);
    GrayArray result = gray_random_shuffle(arena, &single);
    ASSERT_EQ(result.len, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(result, int64_t, 0), 9);
}

typedef struct { int64_t values[16]; } WideElement;

static void test_random_shuffle_wide_elements_stay_intact(void) {
    WideElement elements[6];
    for (int i = 0; i < 6; i++)
        for (int slot = 0; slot < 16; slot++) elements[i].values[slot] = i * 100 + slot;
    GrayArray source = GRAY_ARRAY_FROM_OF(arena, WideElement, elements, 6);
    GrayArray shuffled = gray_random_shuffle(arena, &source);
    ASSERT_EQ(shuffled.len, 6);
    for (int32_t i = 0; i < shuffled.len; i++) {
        WideElement element = GRAY_ARRAY_GET(shuffled, WideElement, i);
        for (int slot = 1; slot < 16; slot++)
            ASSERT_EQ(element.values[slot], element.values[0] + slot);
    }
}

static void test_random_sample_returns_unique_elements(void) {
    GrayArray source = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    GrayArray picked = gray_random_sample(arena, &source, 6);
    ASSERT_EQ(picked.len, 6);
    for (int32_t i = 0; i < picked.len; i++) {
        int64_t value = GRAY_ARRAY_GET(picked, int64_t, i);
        ASSERT(value >= 1 && value <= 10);
        for (int32_t j = i + 1; j < picked.len; j++)
            ASSERT_NE(value, GRAY_ARRAY_GET(picked, int64_t, j));
    }
}

static void test_random_sample_zero_is_empty(void) {
    GrayArray source = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    ASSERT_EQ(gray_random_sample(arena, &source, 0).len, 0);
}

static void test_random_sample_full_length_is_permutation(void) {
    GrayArray source = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4);
    GrayArray picked = gray_random_sample(arena, &source, 4);
    ASSERT_EQ(picked.len, 4);
    ASSERT_EQ(sum_i64_array(&picked), 10);
}

/* ===== mem module ===== */

static void test_mem_arena_starts_empty(void) {
    GrayArena *scratch = gray_mem_arena(4096);
    ASSERT_EQ(gray_mem_usage(scratch), 0);
    gray_mem_destroy(&scratch, __FILE__, __LINE__);
}

static void test_mem_arena_non_positive_size_uses_default(void) {
    GrayArena *zero_sized = gray_mem_arena(0);
    GrayArena *negative_sized = gray_mem_arena(-8);
    ASSERT(zero_sized != NULL);
    ASSERT(negative_sized != NULL);
    ASSERT_EQ(gray_mem_usage(zero_sized), 0);
    ASSERT_EQ(gray_mem_usage(negative_sized), 0);
    gray_mem_destroy(&zero_sized, __FILE__, __LINE__);
    gray_mem_destroy(&negative_sized, __FILE__, __LINE__);
}

static void test_mem_usage_grows_with_allocation(void) {
    GrayArena *scratch = gray_mem_arena(4096);
    gray_arena_alloc(scratch, 100);
    ASSERT_GE(gray_mem_usage(scratch), 100);
    int64_t after_first = gray_mem_usage(scratch);
    gray_arena_alloc(scratch, 100);
    ASSERT_GE(gray_mem_usage(scratch), after_first + 100);
    gray_mem_destroy(&scratch, __FILE__, __LINE__);
}

static void test_mem_reset_returns_usage_to_zero(void) {
    GrayArena *scratch = gray_mem_arena(4096);
    gray_arena_alloc(scratch, 512);
    ASSERT_GT(gray_mem_usage(scratch), 0);
    gray_mem_reset(scratch);
    ASSERT_EQ(gray_mem_usage(scratch), 0);
    gray_mem_destroy(&scratch, __FILE__, __LINE__);
}

static void test_mem_arena_is_reusable_after_reset(void) {
    GrayArena *scratch = gray_mem_arena(4096);
    gray_arena_alloc(scratch, 512);
    gray_mem_reset(scratch);
    gray_arena_alloc(scratch, 64);
    ASSERT_GE(gray_mem_usage(scratch), 64);
    gray_mem_destroy(&scratch, __FILE__, __LINE__);
}

static void test_mem_arena_allocations_do_not_count_toward_total(void) {
    GrayArena *scratch = gray_mem_arena(4096);
    size_t before = gray_total_alloc_count;
    gray_arena_alloc(scratch, 64);
    gray_arena_alloc(scratch, 64);
    ASSERT_EQ(gray_total_alloc_count, before);
    gray_mem_destroy(&scratch, __FILE__, __LINE__);
}

static void test_mem_copy_copies_exactly_n_bytes(void) {
    uint8_t source[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dest[8] = {0};
    gray_mem_copy(dest, source, 5);
    for (int i = 0; i < 5; i++) ASSERT_EQ(dest[i], source[i]);
    for (int i = 5; i < 8; i++) ASSERT_EQ(dest[i], 0);
}

static void test_mem_copy_zero_bytes_is_noop(void) {
    uint8_t source[2] = {9, 9};
    uint8_t dest[2] = {1, 2};
    gray_mem_copy(dest, source, 0);
    ASSERT_EQ(dest[0], 1);
    ASSERT_EQ(dest[1], 2);
}

static void test_mem_zero_clears_exactly_n_bytes(void) {
    uint8_t buffer[6] = {7, 7, 7, 7, 7, 7};
    gray_mem_zero(buffer, 4);
    for (int i = 0; i < 4; i++) ASSERT_EQ(buffer[i], 0);
    ASSERT_EQ(buffer[4], 7);
    ASSERT_EQ(buffer[5], 7);
}

static void test_mem_set_fills_exactly_n_bytes(void) {
    uint8_t buffer[6] = {0};
    gray_mem_set(buffer, 0xAB, 3);
    for (int i = 0; i < 3; i++) ASSERT_EQ(buffer[i], 0xAB);
    for (int i = 3; i < 6; i++) ASSERT_EQ(buffer[i], 0);
}

/* ===== runtime module ===== */

static void test_runtime_arena_usage_tracks_default_arena(void) {
    int64_t before = gray_runtime_arena_usage();
    ASSERT_EQ(before, (int64_t)gray_arena_usage(gray_default_arena));
    gray_arena_alloc(gray_default_arena, 128);
    ASSERT_GE(gray_runtime_arena_usage(), before + 128);
}

static void test_runtime_heap_usage_tracks_heap_arena(void) {
    GrayArena *saved_heap = gray_heap_arena;
    gray_heap_arena = gray_arena_create(GRAY_MIN_ARENA_SIZE);
    int64_t before = gray_runtime_heap_usage();
    gray_arena_alloc(gray_heap_arena, 256);
    ASSERT_GE(gray_runtime_heap_usage(), before + 256);
    gray_arena_destroy(gray_heap_arena, __FILE__, __LINE__);
    gray_heap_arena = saved_heap;
}

static void test_runtime_total_usage_is_arena_plus_heap(void) {
    GrayArena *saved_heap = gray_heap_arena;
    gray_heap_arena = gray_arena_create(GRAY_MIN_ARENA_SIZE);
    gray_arena_alloc(gray_heap_arena, 64);
    ASSERT_EQ(gray_runtime_total_usage(), gray_runtime_arena_usage() + gray_runtime_heap_usage());
    gray_arena_destroy(gray_heap_arena, __FILE__, __LINE__);
    gray_heap_arena = saved_heap;
}

static void test_runtime_peak_usage_is_at_least_total_usage(void) {
    GrayArena *saved_heap = gray_heap_arena;
    gray_heap_arena = gray_arena_create(GRAY_MIN_ARENA_SIZE);
    gray_arena_alloc(gray_default_arena, 64);
    gray_arena_alloc(gray_heap_arena, 64);
    ASSERT_GE(gray_runtime_peak_usage(), gray_runtime_total_usage());
    gray_arena_destroy(gray_heap_arena, __FILE__, __LINE__);
    gray_heap_arena = saved_heap;
}

static void test_runtime_alloc_count_increases_per_allocation(void) {
    int64_t before = gray_runtime_alloc_count();
    gray_arena_alloc(gray_default_arena, 16);
    gray_arena_alloc(gray_default_arena, 16);
    ASSERT_EQ(gray_runtime_alloc_count(), before + 2);
}

static void test_runtime_arena_blocks_counts_chained_blocks(void) {
    ASSERT_EQ(gray_runtime_arena_blocks(), (int64_t)gray_arena_block_count(gray_default_arena));
    ASSERT_GE(gray_runtime_arena_blocks(), 1);
}

static void test_runtime_heap_blocks_counts_chained_blocks(void) {
    GrayArena *saved_heap = gray_heap_arena;
    gray_heap_arena = gray_arena_create(GRAY_MIN_ARENA_SIZE);
    ASSERT_EQ(gray_runtime_heap_blocks(), (int64_t)gray_arena_block_count(gray_heap_arena));
    ASSERT_GE(gray_runtime_heap_blocks(), 1);
    gray_arena_destroy(gray_heap_arena, __FILE__, __LINE__);
    gray_heap_arena = saved_heap;
}

static void test_runtime_arena_limit_reports_maximum_bytes(void) {
    size_t saved_limit = gray_default_arena->maximum_bytes;
    gray_default_arena->maximum_bytes = 123456;
    ASSERT_EQ(gray_runtime_arena_limit(), 123456);
    gray_default_arena->maximum_bytes = saved_limit;
}

static void test_runtime_call_depth_follows_enter_and_exit(void) {
    int64_t base = gray_runtime_call_depth();
    gray_enter_func(__FILE__, __LINE__, "outer");
    ASSERT_EQ(gray_runtime_call_depth(), base + 1);
    gray_enter_func(__FILE__, __LINE__, "inner");
    ASSERT_EQ(gray_runtime_call_depth(), base + 2);
    gray_exit_func();
    gray_exit_func();
    ASSERT_EQ(gray_runtime_call_depth(), base);
}

static void test_runtime_current_and_caller_name(void) {
    gray_enter_func(__FILE__, __LINE__, "outer");
    gray_enter_func(__FILE__, __LINE__, "inner");
    GrayString current = gray_runtime_current_name();
    GrayString caller = gray_runtime_caller_name();
    gray_exit_func();
    gray_exit_func();
    ASSERT_GRAY_STR(current, "inner");
    ASSERT_GRAY_STR(caller, "outer");
}

static void test_runtime_caller_name_is_empty_at_top_level(void) {
    int saved_depth = gray_call_depth;
    gray_call_depth = 1;
    GrayString caller = gray_runtime_caller_name();
    gray_call_depth = saved_depth;
    ASSERT_EQ(caller.len, 0);
}

static void test_runtime_current_name_is_empty_outside_any_function(void) {
    int saved_depth = gray_call_depth;
    gray_call_depth = 0;
    GrayString current = gray_runtime_current_name();
    gray_call_depth = saved_depth;
    ASSERT_EQ(current.len, 0);
}

/* ===== builtins: conversions and UTF-8 ===== */

static void test_builtin_to_string_i64(void) {
    ASSERT_GRAY_STR(gray_builtin_to_string_i64(arena, 0), "0");
    ASSERT_GRAY_STR(gray_builtin_to_string_i64(arena, -42), "-42");
    ASSERT_GRAY_STR(gray_builtin_to_string_i64(arena, INT64_MAX), "9223372036854775807");
    ASSERT_GRAY_STR(gray_builtin_to_string_i64(arena, INT64_MIN), "-9223372036854775808");
}

static void test_builtin_to_string_u64(void) {
    ASSERT_GRAY_STR(gray_builtin_to_string_u64(arena, 0), "0");
    ASSERT_GRAY_STR(gray_builtin_to_string_u64(arena, UINT64_MAX), "18446744073709551615");
}

static void test_builtin_to_string_bool(void) {
    ASSERT_GRAY_STR(gray_builtin_to_string_bool(arena, true), "true");
    ASSERT_GRAY_STR(gray_builtin_to_string_bool(arena, false), "false");
}

static void test_builtin_to_string_floating_point_shortest(void) {
    ASSERT_GRAY_STR(gray_builtin_to_string_floating_point(arena, 1.5, 64), "1.5");
    ASSERT_GRAY_STR(gray_builtin_to_string_floating_point(arena, 0.1, 64), "0.1");
    ASSERT_GRAY_STR(gray_builtin_to_string_floating_point(arena, -2.25, 64), "-2.25");
}

static void test_builtin_to_string_floating_point_f32_is_shortest_for_f32(void) {
    ASSERT_GRAY_STR(gray_builtin_to_string_floating_point(arena, (double)0.1f, 32), "0.1");
}

static void test_builtin_format_floating_point_matches_to_string(void) {
    GrayString formatted = gray_builtin_format_floating_point(arena, 3.14159, 64);
    GrayString converted = gray_builtin_to_string_floating_point(arena, 3.14159, 64);
    ASSERT(gray_string_eq(formatted, converted));
}

static void test_builtin_string_to_i64(void) {
    ASSERT_EQ(gray_builtin_string_to_i64(gray_string_lit("0")), 0);
    ASSERT_EQ(gray_builtin_string_to_i64(gray_string_lit("-17")), -17);
    ASSERT_EQ(gray_builtin_string_to_i64(gray_string_lit("9223372036854775807")), INT64_MAX);
    ASSERT_EQ(gray_builtin_string_to_i64(gray_string_lit("-9223372036854775808")), INT64_MIN);
}

static void test_builtin_string_to_i64_ignores_trailing_spaces(void) {
    ASSERT_EQ(gray_builtin_string_to_i64(gray_string_lit("12  ")), 12);
}

static void test_builtin_string_to_u64(void) {
    ASSERT_EQ(gray_builtin_string_to_u64(gray_string_lit("0")), 0);
    ASSERT(gray_builtin_string_to_u64(gray_string_lit("18446744073709551615")) == UINT64_MAX);
}

static void test_builtin_string_to_i128_round_trips_negative(void) {
    gray_i128 value = gray_builtin_string_to_i128(gray_string_lit("-1"), __FILE__, __LINE__);
    ASSERT(value.low == UINT64_MAX);
    ASSERT_EQ(value.high, -1);
}

static void test_builtin_string_to_u128_above_u64(void) {
    gray_u128 value = gray_builtin_string_to_u128(gray_string_lit("18446744073709551616"), __FILE__, __LINE__);
    ASSERT_EQ(value.low, 0);
    ASSERT_EQ(value.high, 1);
}

static void test_builtin_string_to_i256_negative_sign_extends(void) {
    gray_i256 value = gray_builtin_string_to_i256(gray_string_lit("-1"), __FILE__, __LINE__);
    for (int word = 0; word < 4; word++) ASSERT(value.w[word] == UINT64_MAX);
}

static void test_builtin_string_to_u256_above_u128(void) {
    gray_u256 value = gray_builtin_string_to_u256(
        gray_string_lit("340282366920938463463374607431768211456"), __FILE__, __LINE__);
    ASSERT_EQ(value.w[0], 0);
    ASSERT_EQ(value.w[1], 0);
    ASSERT_EQ(value.w[2], 1);
    ASSERT_EQ(value.w[3], 0);
}

static void test_builtin_char_count_counts_codepoints(void) {
    ASSERT_EQ(gray_builtin_char_count(gray_string_lit("")), 0);
    ASSERT_EQ(gray_builtin_char_count(gray_string_lit("abc")), 3);
    ASSERT_EQ(gray_builtin_char_count(gray_string_lit("aé€😀")), 4);
}

static void test_builtin_char_to_utf8_encodes_each_width(void) {
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, 'A'), "A");
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, 0xE9), "é");
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, 0x20AC), "€");
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, 0x1F600), "😀");
}

static void test_builtin_char_to_utf8_invalid_scalar_is_replacement(void) {
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, 0xD800), "\xEF\xBF\xBD");
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, 0x110000), "\xEF\xBF\xBD");
    ASSERT_GRAY_STR(gray_builtin_char_to_utf8(arena, -1), "\xEF\xBF\xBD");
}

static void test_builtin_utf8_next_reports_width(void) {
    const char *text = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    const uint8_t *cursor = (const uint8_t *)text;
    const uint8_t *end_cursor = cursor + strlen(text);
    int32_t codepoint;
    int widths[4] = {1, 2, 3, 4};
    int32_t expected[4] = {'A', 0xE9, 0x20AC, 0x1F600};
    for (int i = 0; i < 4; i++) {
        int width = gray_builtin_utf8_next(cursor, end_cursor, &codepoint);
        ASSERT_EQ(width, widths[i]);
        ASSERT_EQ(codepoint, expected[i]);
        cursor += width;
    }
    ASSERT(cursor == end_cursor);
}

static void test_builtin_utf8_next_stray_continuation_byte_is_replacement(void) {
    const uint8_t stray[1] = {0x80};
    int32_t codepoint;
    ASSERT_EQ(gray_builtin_utf8_next(stray, stray + 1, &codepoint), 1);
    ASSERT_EQ(codepoint, 0xFFFD);
}

static void test_builtin_array_to_string_integers(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &values, 0), "{1, 2, 3}");
}

static void test_builtin_array_to_string_empty(void) {
    GrayArray values = GRAY_ARRAY_NEW_OF(arena, int64_t, 0);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &values, 0), "{}");
}

/* ===== atomic module ===== */

static void test_atomic_mod_load_store(void) {
    int64_t cell = 3;
    ASSERT_EQ(gray_atomic_mod_load(&cell), 3);
    gray_atomic_mod_store(&cell, 11);
    ASSERT_EQ(cell, 11);
}

static void test_atomic_mod_add_sub_return_previous_value(void) {
    int64_t cell = 10;
    ASSERT_EQ(gray_atomic_mod_add(&cell, 4), 10);
    ASSERT_EQ(gray_atomic_mod_sub(&cell, 6), 14);
    ASSERT_EQ(cell, 8);
}

static void test_atomic_mod_exchange_returns_previous_value(void) {
    int64_t cell = 1;
    ASSERT_EQ(gray_atomic_mod_exchange(&cell, 2), 1);
    ASSERT_EQ(cell, 2);
}

static void test_atomic_mod_cas_reports_whether_swap_happened(void) {
    int64_t cell = 4;
    ASSERT(!gray_atomic_mod_cas(&cell, 5, 8));
    ASSERT_EQ(cell, 4);
    ASSERT(gray_atomic_mod_cas(&cell, 4, 8));
    ASSERT_EQ(cell, 8);
}

static void test_atomic_mod_bitwise_return_previous_value(void) {
    int64_t cell = 0xF0;
    ASSERT_EQ(gray_atomic_mod_and(&cell, 0x3C), 0xF0);
    ASSERT_EQ(gray_atomic_mod_or(&cell, 0x05), 0x30);
    ASSERT_EQ(gray_atomic_mod_xor(&cell, 0xFF), 0x35);
    ASSERT_EQ(cell, 0xCA);
}

static void test_atomic_mod_spinlock_lifecycle(void) {
    GraySpinLock lock = gray_atomic_mod_spinlock();
    ASSERT(gray_atomic_mod_spin_trylock(lock));
    ASSERT(!gray_atomic_mod_spin_trylock(lock));
    gray_atomic_mod_spin_unlock(lock);
    ASSERT(gray_atomic_mod_spin_trylock(lock));
    gray_atomic_mod_spin_unlock(lock);
    gray_atomic_mod_spinlock_destroy(&lock);
}

static void test_atomic_mod_spin_lock_acquires_new_lock(void) {
    GraySpinLock lock = gray_atomic_mod_spinlock();
    gray_atomic_mod_spin_lock(lock);
    ASSERT(!gray_atomic_mod_spin_trylock(lock));
    gray_atomic_mod_spin_unlock(lock);
    gray_atomic_mod_spinlock_destroy(&lock);
}

static void test_atomic_mod_spinlock_copy_shares_the_lock(void) {
    GraySpinLock lock = gray_atomic_mod_spinlock();
    GraySpinLock copy = lock;
    gray_atomic_mod_spin_lock(lock);
    ASSERT(!gray_atomic_mod_spin_trylock(copy));
    gray_atomic_mod_spin_unlock(copy);
    ASSERT(gray_atomic_mod_spin_trylock(lock));
    gray_atomic_mod_spin_unlock(lock);
    gray_atomic_mod_spinlock_destroy(&lock);
}

static void test_atomic_mod_spinlocks_are_independent(void) {
    GraySpinLock first = gray_atomic_mod_spinlock();
    GraySpinLock second = gray_atomic_mod_spinlock();
    gray_atomic_mod_spin_lock(first);
    ASSERT(gray_atomic_mod_spin_trylock(second));
    gray_atomic_mod_spin_unlock(second);
    gray_atomic_mod_spin_unlock(first);
    gray_atomic_mod_spinlock_destroy(&first);
    gray_atomic_mod_spinlock_destroy(&second);
}

static void test_atomic_mod_fence_returns(void) {
    gray_atomic_mod_fence();
}

/* ===== channels module ===== */

static void test_channels_receives_in_send_order(void) {
    GrayChannel channel = gray_channels_open(4);
    gray_channels_send(channel, 10);
    gray_channels_send(channel, 20);
    gray_channels_send(channel, 30);
    ASSERT_EQ(gray_channels_receive(channel), 10);
    ASSERT_EQ(gray_channels_receive(channel), 20);
    ASSERT_EQ(gray_channels_receive(channel), 30);
}

static void test_channels_len_counts_buffered_values(void) {
    GrayChannel channel = gray_channels_open(4);
    ASSERT_EQ(gray_channels_len(channel), 0);
    gray_channels_send(channel, 1);
    gray_channels_send(channel, 2);
    ASSERT_EQ(gray_channels_len(channel), 2);
    gray_channels_receive(channel);
    ASSERT_EQ(gray_channels_len(channel), 1);
}

static void test_channels_try_send_fails_when_full(void) {
    GrayChannel channel = gray_channels_open(2);
    ASSERT(gray_channels_try_send(channel, 1));
    ASSERT(gray_channels_try_send(channel, 2));
    ASSERT(!gray_channels_try_send(channel, 3));
    ASSERT_EQ(gray_channels_len(channel), 2);
}

static void test_channels_try_send_succeeds_again_after_a_receive(void) {
    GrayChannel channel = gray_channels_open(1);
    ASSERT(gray_channels_try_send(channel, 1));
    ASSERT(!gray_channels_try_send(channel, 2));
    ASSERT_EQ(gray_channels_receive(channel), 1);
    ASSERT(gray_channels_try_send(channel, 3));
    ASSERT_EQ(gray_channels_receive(channel), 3);
}

static void test_channels_try_receive_on_empty_reports_failure(void) {
    GrayChannel channel = gray_channels_open(2);
    GrayChannelTryRecv result = gray_channels_try_receive(channel);
    ASSERT(!result.v1);
    ASSERT_EQ(result.v0, 0);
}

static void test_channels_try_receive_returns_value_and_success(void) {
    GrayChannel channel = gray_channels_open(2);
    gray_channels_send(channel, 77);
    GrayChannelTryRecv result = gray_channels_try_receive(channel);
    ASSERT(result.v1);
    ASSERT_EQ(result.v0, 77);
    ASSERT_EQ(gray_channels_len(channel), 0);
}

static void test_channels_buffer_wraps_around(void) {
    GrayChannel channel = gray_channels_open(3);
    for (int64_t round = 0; round < 10; round++) {
        gray_channels_send(channel, round * 2);
        gray_channels_send(channel, round * 2 + 1);
        ASSERT_EQ(gray_channels_receive(channel), round * 2);
        ASSERT_EQ(gray_channels_receive(channel), round * 2 + 1);
    }
    ASSERT_EQ(gray_channels_len(channel), 0);
}

static void test_channels_holds_extreme_values(void) {
    GrayChannel channel = gray_channels_open(2);
    gray_channels_send(channel, INT64_MIN);
    gray_channels_send(channel, INT64_MAX);
    ASSERT_EQ(gray_channels_receive(channel), INT64_MIN);
    ASSERT_EQ(gray_channels_receive(channel), INT64_MAX);
}

static void test_channels_channels_are_independent(void) {
    GrayChannel first = gray_channels_open(2);
    GrayChannel second = gray_channels_open(2);
    gray_channels_send(first, 1);
    gray_channels_send(second, 2);
    ASSERT_EQ(gray_channels_receive(second), 2);
    ASSERT_EQ(gray_channels_receive(first), 1);
}

static void test_channels_close_keeps_buffered_values_receivable(void) {
    GrayChannel channel = gray_channels_open(4);
    gray_channels_send(channel, 5);
    gray_channels_send(channel, 6);
    gray_channels_close(channel);
    ASSERT_EQ(gray_channels_receive(channel), 5);
    ASSERT_EQ(gray_channels_receive(channel), 6);
}

static void test_channels_receive_on_closed_empty_channel_returns_zero(void) {
    GrayChannel channel = gray_channels_open(2);
    gray_channels_close(channel);
    ASSERT_EQ(gray_channels_receive(channel), 0);
}

static void test_channels_try_send_after_close_fails(void) {
    GrayChannel channel = gray_channels_open(2);
    gray_channels_close(channel);
    ASSERT(!gray_channels_try_send(channel, 9));
    ASSERT_EQ(gray_channels_len(channel), 0);
}

typedef struct {
    GrayChannel channel;
    int64_t count;
} ChannelProducerArguments;

static void *channel_producer(void *raw) {
    ChannelProducerArguments *arguments = raw;
    for (int64_t value = 1; value <= arguments->count; value++) gray_channels_send(arguments->channel, value);
    return NULL;
}

static void test_channels_blocking_send_and_receive_across_threads(void) {
    GrayChannel channel = gray_channels_open(2);
    ChannelProducerArguments arguments = {channel, 1000};
    pthread_t producer;
    pthread_create(&producer, NULL, channel_producer, &arguments);
    int64_t total = 0;
    for (int64_t expected = 1; expected <= 1000; expected++) {
        int64_t value = gray_channels_receive(channel);
        ASSERT_EQ(value, expected);
        total += value;
    }
    pthread_join(producer, NULL);
    ASSERT_EQ(total, 500500);
}

static void *channel_closer(void *raw) {
    sleep_microseconds(50000);
    gray_channels_close(*(GrayChannel *)raw);
    return NULL;
}

static void test_channels_close_wakes_a_blocked_receiver(void) {
    GrayChannel channel = gray_channels_open(2);
    pthread_t closer;
    pthread_create(&closer, NULL, channel_closer, &channel);
    ASSERT_EQ(gray_channels_receive(channel), 0);
    pthread_join(closer, NULL);
}

/* ===== chars: remaining classification, escape, mirror, width ===== */

static void test_chars_is_ascii_boundaries(void) {
    ASSERT(gray_chars_is_ascii(0));
    ASSERT(gray_chars_is_ascii(127));
    ASSERT(!gray_chars_is_ascii(128));
    ASSERT(!gray_chars_is_ascii(0x4E2D));
}

static void test_chars_is_control_ranges(void) {
    ASSERT(gray_chars_is_control(0));
    ASSERT(gray_chars_is_control(31));
    ASSERT(gray_chars_is_control(127));
    ASSERT(!gray_chars_is_control(32));
    ASSERT(!gray_chars_is_control(126));
    ASSERT(!gray_chars_is_control(0x85));
}

static void test_chars_is_printable_ranges(void) {
    ASSERT(gray_chars_is_printable(32));
    ASSERT(gray_chars_is_printable('~'));
    ASSERT(!gray_chars_is_printable(31));
    ASSERT(!gray_chars_is_printable(127));
    ASSERT(!gray_chars_is_printable(0xE9));
}

static void test_chars_is_punct_ascii_punctuation_only(void) {
    for (int32_t codepoint = 0; codepoint < 128; codepoint++) {
        bool expected = codepoint > 32 && codepoint < 127 &&
                        !(codepoint >= '0' && codepoint <= '9') &&
                        !(codepoint >= 'a' && codepoint <= 'z') &&
                        !(codepoint >= 'A' && codepoint <= 'Z');
        ASSERT_EQ(gray_chars_is_punct(codepoint), expected);
    }
    ASSERT(!gray_chars_is_punct(0xA1));
}

static void test_chars_is_hex_digit(void) {
    ASSERT(gray_chars_is_hex_digit('0'));
    ASSERT(gray_chars_is_hex_digit('9'));
    ASSERT(gray_chars_is_hex_digit('a'));
    ASSERT(gray_chars_is_hex_digit('f'));
    ASSERT(gray_chars_is_hex_digit('A'));
    ASSERT(gray_chars_is_hex_digit('F'));
    ASSERT(!gray_chars_is_hex_digit('g'));
    ASSERT(!gray_chars_is_hex_digit('G'));
    ASSERT(!gray_chars_is_hex_digit(' '));
    ASSERT(!gray_chars_is_hex_digit(0xFF21));
}

static void test_chars_is_word_char(void) {
    ASSERT(gray_chars_is_word_char('a'));
    ASSERT(gray_chars_is_word_char('Z'));
    ASSERT(gray_chars_is_word_char('5'));
    ASSERT(gray_chars_is_word_char('_'));
    ASSERT(!gray_chars_is_word_char('-'));
    ASSERT(!gray_chars_is_word_char(' '));
    ASSERT(!gray_chars_is_word_char(0xE9));
}

static void test_chars_is_vowel(void) {
    const char *vowels = "aeiouAEIOU";
    for (int i = 0; vowels[i]; i++) ASSERT(gray_chars_is_vowel(vowels[i]));
    ASSERT(!gray_chars_is_vowel('y'));
    ASSERT(!gray_chars_is_vowel('Y'));
    ASSERT(!gray_chars_is_vowel('b'));
    ASSERT(!gray_chars_is_vowel('1'));
    ASSERT(!gray_chars_is_vowel(0xE9));
}

static void test_chars_is_consonant(void) {
    ASSERT(gray_chars_is_consonant('b'));
    ASSERT(gray_chars_is_consonant('Z'));
    ASSERT(gray_chars_is_consonant('y'));
    ASSERT(!gray_chars_is_consonant('a'));
    ASSERT(!gray_chars_is_consonant('E'));
    ASSERT(!gray_chars_is_consonant('1'));
    ASSERT(!gray_chars_is_consonant(' '));
    ASSERT(!gray_chars_is_consonant(0xE9));
}

static void test_chars_escape_two_character_escapes(void) {
    ASSERT_GRAY_STR(gray_chars_escape(arena, '\\'), "\\\\");
    ASSERT_GRAY_STR(gray_chars_escape(arena, '\n'), "\\n");
    ASSERT_GRAY_STR(gray_chars_escape(arena, '\t'), "\\t");
    ASSERT_GRAY_STR(gray_chars_escape(arena, '\r'), "\\r");
    ASSERT_GRAY_STR(gray_chars_escape(arena, 0), "\\0");
}

static void test_chars_escape_printable_ascii_is_unchanged(void) {
    ASSERT_GRAY_STR(gray_chars_escape(arena, 'a'), "a");
    ASSERT_GRAY_STR(gray_chars_escape(arena, ' '), " ");
    ASSERT_GRAY_STR(gray_chars_escape(arena, '~'), "~");
    ASSERT_GRAY_STR(gray_chars_escape(arena, '"'), "\"");
}

static void test_chars_escape_other_control_characters_are_hex(void) {
    ASSERT_GRAY_STR(gray_chars_escape(arena, 1), "\\x01");
    ASSERT_GRAY_STR(gray_chars_escape(arena, 7), "\\x07");
    ASSERT_GRAY_STR(gray_chars_escape(arena, 0x10), "\\x10");
}

static void test_chars_escape_del_is_hex(void) {
    GrayString result = gray_chars_escape(arena, 127);
    ASSERT_EQ(result.len, 4);
    ASSERT(result.data[0] == '\\' && result.data[1] == 'x' && result.data[2] == '7');
}

static void test_chars_escape_non_ascii_is_unicode_escape(void) {
    ASSERT_GRAY_STR(gray_chars_escape(arena, 0x100), "\\u{100}");
    ASSERT_GRAY_STR(gray_chars_escape(arena, 0x10000), "\\u{10000}");
}

static void test_chars_mirror_swaps_each_bracket_pair(void) {
    ASSERT_EQ(gray_chars_mirror('('), ')');
    ASSERT_EQ(gray_chars_mirror(')'), '(');
    ASSERT_EQ(gray_chars_mirror('['), ']');
    ASSERT_EQ(gray_chars_mirror(']'), '[');
    ASSERT_EQ(gray_chars_mirror('{'), '}');
    ASSERT_EQ(gray_chars_mirror('}'), '{');
    ASSERT_EQ(gray_chars_mirror('<'), '>');
    ASSERT_EQ(gray_chars_mirror('>'), '<');
}

static void test_chars_mirror_leaves_other_codepoints_alone(void) {
    ASSERT_EQ(gray_chars_mirror('a'), 'a');
    ASSERT_EQ(gray_chars_mirror(' '), ' ');
    ASSERT_EQ(gray_chars_mirror('/'), '/');
    ASSERT_EQ(gray_chars_mirror(0x4E2D), 0x4E2D);
}

static void test_chars_width_ascii_is_one(void) {
    ASSERT_EQ(gray_chars_width('A'), 1);
    ASSERT_EQ(gray_chars_width(' '), 1);
    ASSERT_EQ(gray_chars_width('~'), 1);
}

static void test_chars_width_control_is_minus_one(void) {
    ASSERT_EQ(gray_chars_width(1), -1);
    ASSERT_EQ(gray_chars_width('\n'), -1);
    ASSERT_EQ(gray_chars_width(0x85), -1);
}

static void test_chars_width_wide_codepoints_are_two(void) {
    ASSERT_EQ(gray_chars_width(0x4E2D), 2);
    ASSERT_EQ(gray_chars_width(0xAC00), 2);
    ASSERT_EQ(gray_chars_width(0xFF21), 2);
    ASSERT_EQ(gray_chars_width(0x1F600), 2);
}

static void test_chars_width_zero_width_codepoints_are_zero(void) {
    ASSERT_EQ(gray_chars_width(0x0301), 0);
    ASSERT_EQ(gray_chars_width(0x200B), 0);
    ASSERT_EQ(gray_chars_width(0x200D), 0);
    ASSERT_EQ(gray_chars_width(0xFE0F), 0);
}

static void test_chars_width_ambiguous_width_is_one(void) {
    ASSERT_EQ(gray_chars_width(0x00E9), 1);
    ASSERT_EQ(gray_chars_width(0x00B1), 1);
    ASSERT_EQ(gray_chars_width(0x03A9), 1);
}

static void test_chars_string_width_sums_codepoints(void) {
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("")), 0);
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("abc")), 3);
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("café")), 4);
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("中文")), 4);
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("a中b")), 4);
}

static void test_chars_string_width_counts_controls_as_zero(void) {
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("a\tb")), 2);
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("a\nb")), 2);
}

static void test_chars_string_width_counts_combining_marks_as_zero(void) {
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("e\xCC\x81")), 1);
}

static void test_chars_string_width_sums_zwj_sequence_parts(void) {
    ASSERT_EQ(gray_chars_string_width(gray_string_lit("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9")), 4);
}

/* ===== time module ===== */

#define TIME_2023_11_14_22_13_20 1700000000
#define TIME_2000_02_29_MIDNIGHT 951782400

static void test_time_components_of_known_timestamp(void) {
    ASSERT_EQ(gray_time_year(TIME_2023_11_14_22_13_20), 2023);
    ASSERT_EQ(gray_time_month(TIME_2023_11_14_22_13_20), 11);
    ASSERT_EQ(gray_time_day(TIME_2023_11_14_22_13_20), 14);
    ASSERT_EQ(gray_time_hour(TIME_2023_11_14_22_13_20), 22);
    ASSERT_EQ(gray_time_minute(TIME_2023_11_14_22_13_20), 13);
    ASSERT_EQ(gray_time_second(TIME_2023_11_14_22_13_20), 20);
    ASSERT_EQ(gray_time_weekday(TIME_2023_11_14_22_13_20), 2);
}

static void test_time_components_of_epoch(void) {
    ASSERT_EQ(gray_time_year(0), 1970);
    ASSERT_EQ(gray_time_month(0), 1);
    ASSERT_EQ(gray_time_day(0), 1);
    ASSERT_EQ(gray_time_hour(0), 0);
    ASSERT_EQ(gray_time_weekday(0), 4);
}

static void test_time_components_before_epoch(void) {
    ASSERT_EQ(gray_time_year(-1), 1969);
    ASSERT_EQ(gray_time_month(-1), 12);
    ASSERT_EQ(gray_time_day(-1), 31);
    ASSERT_EQ(gray_time_hour(-1), 23);
    ASSERT_EQ(gray_time_minute(-1), 59);
    ASSERT_EQ(gray_time_second(-1), 59);
}

static void test_time_weekday_name_follows_weekday_numbering(void) {
    ASSERT_GRAY_STR(gray_time_weekday_name(arena, 0), "Thursday");
    ASSERT_GRAY_STR(gray_time_weekday_name(arena, TIME_2023_11_14_22_13_20), "Tuesday");
    ASSERT_GRAY_STR(gray_time_weekday_name(arena, 3 * 86400), "Sunday");
}

static void test_time_month_name(void) {
    ASSERT_GRAY_STR(gray_time_month_name(arena, 0), "January");
    ASSERT_GRAY_STR(gray_time_month_name(arena, TIME_2023_11_14_22_13_20), "November");
    ASSERT_GRAY_STR(gray_time_month_name(arena, TIME_2000_02_29_MIDNIGHT), "February");
}

static void test_time_day_of_year(void) {
    ASSERT_EQ(gray_time_day_of_year(0), 1);
    ASSERT_EQ(gray_time_day_of_year(TIME_2000_02_29_MIDNIGHT), 60);
    ASSERT_EQ(gray_time_day_of_year(TIME_2023_11_14_22_13_20), 318);
}

static void test_time_is_leap_year(void) {
    ASSERT(gray_time_is_leap_year(2024));
    ASSERT(gray_time_is_leap_year(2000));
    ASSERT(!gray_time_is_leap_year(1900));
    ASSERT(!gray_time_is_leap_year(2023));
    ASSERT(gray_time_is_leap_year(1600));
}

static void test_time_days_in_month_every_month(void) {
    int expected[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int month = 1; month <= 12; month++) ASSERT_EQ(gray_time_days_in_month(2023, month), expected[month - 1]);
}

static void test_time_days_in_month_leap_february(void) {
    ASSERT_EQ(gray_time_days_in_month(2024, 2), 29);
    ASSERT_EQ(gray_time_days_in_month(2000, 2), 29);
    ASSERT_EQ(gray_time_days_in_month(1900, 2), 28);
}

static void test_time_format_directives(void) {
    ASSERT_GRAY_STR(gray_time_format(arena, gray_string_lit("%Y-%m-%d %H:%M:%S"), TIME_2023_11_14_22_13_20), "2023-11-14 22:13:20");
    ASSERT_GRAY_STR(gray_time_format(arena, gray_string_lit("%d/%m/%y"), TIME_2023_11_14_22_13_20), "14/11/23");
    ASSERT_GRAY_STR(gray_time_format(arena, gray_string_lit("no directives"), 0), "no directives");
}

static void test_time_to_iso_date_and_clock(void) {
    ASSERT_GRAY_STR(gray_time_to_iso(arena, TIME_2023_11_14_22_13_20), "2023-11-14T22:13:20Z");
    ASSERT_GRAY_STR(gray_time_date(arena, TIME_2023_11_14_22_13_20), "2023-11-14");
    ASSERT_GRAY_STR(gray_time_to_clock(arena, TIME_2023_11_14_22_13_20), "22:13:20");
    ASSERT_GRAY_STR(gray_time_to_clock(arena, 0), "00:00:00");
}

static void test_time_from_parts_builds_timestamp(void) {
    GrayResult_i64 result = gray_time_from_parts_result(2023, 11, 14, 22, 13, 20);
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0, TIME_2023_11_14_22_13_20);
}

static void test_time_from_parts_epoch_and_leap_day(void) {
    ASSERT_EQ(gray_time_from_parts_result(1970, 1, 1, 0, 0, 0).v0, 0);
    GrayResult_i64 leap = gray_time_from_parts_result(2000, 2, 29, 0, 0, 0);
    ASSERT(leap.v1 == NULL);
    ASSERT_EQ(leap.v0, TIME_2000_02_29_MIDNIGHT);
}

static void test_time_from_parts_rejects_impossible_dates(void) {
    GrayResult_i64 results[] = {
        gray_time_from_parts_result(2023, 13, 1, 0, 0, 0),
        gray_time_from_parts_result(2023, 0, 1, 0, 0, 0),
        gray_time_from_parts_result(2023, 2, 29, 0, 0, 0),
        gray_time_from_parts_result(2023, 4, 31, 0, 0, 0),
        gray_time_from_parts_result(2023, 1, 0, 0, 0, 0),
        gray_time_from_parts_result(2023, 1, 1, 24, 0, 0),
        gray_time_from_parts_result(2023, 1, 1, 0, 60, 0),
        gray_time_from_parts_result(2023, 1, 1, 0, 0, 60),
        gray_time_from_parts_result(2023, 1, 1, -1, 0, 0),
        gray_time_from_parts_result(3000000000LL, 1, 1, 0, 0, 0),
    };
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        ASSERT(results[i].v1 != NULL);
        ASSERT_EQ(results[i].v0, 0);
    }
}

static void test_time_from_parts_inverts_the_components(void) {
    GrayResult_i64 result = gray_time_from_parts_result(1999, 12, 31, 23, 59, 58);
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(gray_time_year(result.v0), 1999);
    ASSERT_EQ(gray_time_month(result.v0), 12);
    ASSERT_EQ(gray_time_day(result.v0), 31);
    ASSERT_EQ(gray_time_hour(result.v0), 23);
    ASSERT_EQ(gray_time_minute(result.v0), 59);
    ASSERT_EQ(gray_time_second(result.v0), 58);
}

static void test_time_parse_with_layout(void) {
    GrayResult_i64 result = gray_time_parse_result(gray_string_lit("2023-11-14 22:13:20"), gray_string_lit("%Y-%m-%d %H:%M:%S"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0, TIME_2023_11_14_22_13_20);
}

static void test_time_parse_rejects_bad_input(void) {
    GrayString layout = gray_string_lit("%Y-%m-%d");
    ASSERT(gray_time_parse_result(gray_string_lit("not a date"), layout).v1 != NULL);
    ASSERT(gray_time_parse_result(gray_string_lit("2023-11-14 trailing"), layout).v1 != NULL);
    ASSERT(gray_time_parse_result(gray_string_lit("2023-02-29"), layout).v1 != NULL);
    ASSERT(gray_time_parse_result(gray_string_lit("2023-04-31"), layout).v1 != NULL);
    ASSERT(gray_time_parse_result(gray_string_lit("2023-00-10"), layout).v1 != NULL);
    ASSERT(gray_time_parse_result(gray_string_lit("2023-01-00"), layout).v1 != NULL);
    ASSERT_EQ(gray_time_parse_result(gray_string_lit("junk"), layout).v0, 0);
}

static void test_time_parse_round_trips_through_format(void) {
    GrayString layout = gray_string_lit("%Y-%m-%d %H:%M:%S");
    GrayString text = gray_time_format(arena, layout, TIME_2000_02_29_MIDNIGHT);
    GrayResult_i64 result = gray_time_parse_result(text, layout);
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0, TIME_2000_02_29_MIDNIGHT);
}

static void test_time_parse_duration_units(void) {
    ASSERT_EQ(gray_time_parse_duration_result(gray_string_lit("90s")).v0, 90);
    ASSERT_EQ(gray_time_parse_duration_result(gray_string_lit("5m")).v0, 300);
    ASSERT_EQ(gray_time_parse_duration_result(gray_string_lit("2h")).v0, 7200);
    ASSERT_EQ(gray_time_parse_duration_result(gray_string_lit("2d")).v0, 172800);
}

static void test_time_parse_duration_combined(void) {
    GrayResult_i64 result = gray_time_parse_duration_result(gray_string_lit("1h30m15s"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0, 5415);
    ASSERT_EQ(gray_time_parse_duration_result(gray_string_lit("1d1h1m1s")).v0, 90061);
}

static void test_time_parse_duration_rejects_bad_input(void) {
    const char *bad[] = {"", "abc", "10", "10x", "h", "1h30", "-5s", "1 h", "1.5h", "99999999999999999999s", "9223372036854775807d"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        GrayResult_i64 result = gray_time_parse_duration_result(gray_string_lit(bad[i]));
        ASSERT(result.v1 != NULL);
        ASSERT_EQ(result.v0, 0);
    }
}

static void test_time_diff_is_end_minus_start(void) {
    ASSERT_EQ(gray_time_diff(100, 160), 60);
    ASSERT_EQ(gray_time_diff(160, 100), -60);
    ASSERT_EQ(gray_time_diff(5, 5), 0);
}

static void test_time_since_tracks_now(void) {
    int64_t now = gray_time_now();
    int64_t elapsed = gray_time_since(now - 100);
    ASSERT(elapsed >= 100 && elapsed <= 105);
}

static void test_time_now_units_agree(void) {
    int64_t seconds = gray_time_now();
    int64_t milliseconds = gray_time_now_ms();
    int64_t nanoseconds = gray_time_now_ns();
    ASSERT(milliseconds / 1000 >= seconds && milliseconds / 1000 <= seconds + 2);
    ASSERT(nanoseconds / 1000000000 >= seconds && nanoseconds / 1000000000 <= seconds + 2);
    ASSERT_GT(seconds, 1700000000);
}

static void test_time_tick_is_monotonic_and_elapsed_ms_is_non_negative(void) {
    int64_t first = gray_time_tick();
    int64_t second = gray_time_tick();
    ASSERT_GE(second, first);
    ASSERT_GE(gray_time_elapsed_ms(first), 0);
}

static void test_time_add_shifts_by_unit(void) {
    ASSERT_EQ(gray_time_add_seconds(100, 5), 105);
    ASSERT_EQ(gray_time_add_minutes(100, 2), 220);
    ASSERT_EQ(gray_time_add_hours(0, 3), 10800);
    ASSERT_EQ(gray_time_add_days(0, 2), 172800);
    ASSERT_EQ(gray_time_add_days(172800, -2), 0);
    ASSERT_EQ(gray_time_add_hours(0, -1), -3600);
}

static void test_time_add_months_clamps_to_last_day(void) {
    int64_t january_31 = gray_time_from_parts_result(2023, 1, 31, 10, 20, 30).v0;
    int64_t result = gray_time_add_months(january_31, 1);
    ASSERT_EQ(gray_time_month(result), 2);
    ASSERT_EQ(gray_time_day(result), 28);
    ASSERT_EQ(gray_time_hour(result), 10);
    ASSERT_EQ(gray_time_minute(result), 20);
    ASSERT_EQ(gray_time_second(result), 30);
}

static void test_time_add_months_leap_february(void) {
    int64_t january_31 = gray_time_from_parts_result(2024, 1, 31, 0, 0, 0).v0;
    ASSERT_EQ(gray_time_day(gray_time_add_months(january_31, 1)), 29);
}

static void test_time_add_months_crosses_years(void) {
    int64_t november = gray_time_from_parts_result(2023, 11, 15, 0, 0, 0).v0;
    int64_t later = gray_time_add_months(november, 3);
    ASSERT_EQ(gray_time_year(later), 2024);
    ASSERT_EQ(gray_time_month(later), 2);
    ASSERT_EQ(gray_time_day(later), 15);
    int64_t earlier = gray_time_add_months(november, -11);
    ASSERT_EQ(gray_time_year(earlier), 2022);
    ASSERT_EQ(gray_time_month(earlier), 12);
}

static void test_time_add_months_zero_keeps_timestamp(void) {
    ASSERT_EQ(gray_time_add_months(TIME_2023_11_14_22_13_20, 0), TIME_2023_11_14_22_13_20);
}

static void test_time_start_and_end_of_day(void) {
    int64_t start = gray_time_start_of_day(TIME_2023_11_14_22_13_20);
    ASSERT_EQ(gray_time_hour(start), 0);
    ASSERT_EQ(gray_time_minute(start), 0);
    ASSERT_EQ(gray_time_second(start), 0);
    ASSERT_EQ(gray_time_day(start), 14);
    ASSERT_EQ(gray_time_end_of_day(TIME_2023_11_14_22_13_20), start + 86399);
}

static void test_time_start_of_day_before_epoch(void) {
    ASSERT_EQ(gray_time_start_of_day(-1), -86400);
    ASSERT_EQ(gray_time_end_of_day(-1), -1);
}

static void test_time_start_and_end_of_month(void) {
    int64_t start = gray_time_start_of_month(TIME_2023_11_14_22_13_20);
    ASSERT_EQ(gray_time_day(start), 1);
    ASSERT_EQ(gray_time_month(start), 11);
    ASSERT_EQ(gray_time_hour(start), 0);
    int64_t end = gray_time_end_of_month(TIME_2023_11_14_22_13_20);
    ASSERT_EQ(gray_time_day(end), 30);
    ASSERT_EQ(gray_time_month(end), 11);
    ASSERT_EQ(gray_time_hour(end), 23);
    ASSERT_EQ(gray_time_minute(end), 59);
    ASSERT_EQ(gray_time_second(end), 59);
}

static void test_time_end_of_month_leap_february(void) {
    ASSERT_EQ(gray_time_day(gray_time_end_of_month(TIME_2000_02_29_MIDNIGHT)), 29);
    int64_t february_2023 = gray_time_from_parts_result(2023, 2, 10, 0, 0, 0).v0;
    ASSERT_EQ(gray_time_day(gray_time_end_of_month(february_2023)), 28);
}

static void test_time_end_of_month_december_rolls_the_year_correctly(void) {
    int64_t december = gray_time_from_parts_result(2023, 12, 5, 0, 0, 0).v0;
    int64_t end = gray_time_end_of_month(december);
    ASSERT_EQ(gray_time_year(end), 2023);
    ASSERT_EQ(gray_time_day(end), 31);
}

static void test_time_humanize_zero_and_units(void) {
    ASSERT_GRAY_STR(gray_time_humanize(arena, 0), "just now");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 1), "1 second ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 45), "45 seconds ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 60), "1 minute ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 3600), "1 hour ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 2 * 86400), "2 days ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 14 * 86400), "2 weeks ago");
}

static void test_time_humanize_uses_the_largest_whole_unit(void) {
    ASSERT_GRAY_STR(gray_time_humanize(arena, 3599), "59 minutes ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 7199), "1 hour ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 86399), "23 hours ago");
}

static void test_time_humanize_negative_is_future(void) {
    ASSERT_GRAY_STR(gray_time_humanize(arena, -3600), "in 1 hour");
    ASSERT_GRAY_STR(gray_time_humanize(arena, -120), "in 2 minutes");
    ASSERT_GRAY_STR(gray_time_humanize(arena, -1), "in 1 second");
}

static void test_time_humanize_months_and_years(void) {
    ASSERT_GRAY_STR(gray_time_humanize(arena, 2592000), "1 month ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, 31536000), "1 year ago");
    ASSERT_GRAY_STR(gray_time_humanize(arena, -2 * 31536000), "in 2 years");
}

static void test_time_format_duration(void) {
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 0), "0s");
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 5415), "1h 30m 15s");
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 3600), "1h");
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 90), "1m 30s");
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 45), "45s");
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 3605), "1h 5s");
}

static void test_time_format_duration_caps_at_hours(void) {
    ASSERT_GRAY_STR(gray_time_format_duration(arena, 90000), "25h");
}

static void test_time_format_duration_negative_has_leading_minus(void) {
    ASSERT_GRAY_STR(gray_time_format_duration(arena, -90), "-1m 30s");
    ASSERT_GRAY_STR(gray_time_format_duration(arena, -5), "-5s");
}

/* ===== encoding: url-safe base64, html, shell, byte conversion ===== */

static void test_encoding_base64_url_encode_is_unpadded_and_url_safe(void) {
    GrayString encoded = gray_encoding_base64_url_encode(arena, gray_string_lit("hello?>"));
    ASSERT_GRAY_STR(encoded, "aGVsbG8_Pg");
    ASSERT_GRAY_STR(gray_encoding_base64_url_encode(arena, gray_string_lit("")), "");
}

static void test_encoding_base64_url_encode_uses_dash_and_underscore(void) {
    GrayString encoded = gray_encoding_base64_url_encode(arena, gray_string_lit("\xFB\xFF\xFE"));
    ASSERT_GRAY_STR(encoded, "-__-");
}

static void test_encoding_base64_url_decode_with_and_without_padding(void) {
    GrayResult_string unpadded = gray_encoding_base64_url_decode(arena, gray_string_lit("aGVsbG8_Pg"));
    ASSERT(unpadded.v1 == NULL);
    ASSERT_GRAY_STR(unpadded.v0, "hello?>");
    GrayResult_string padded = gray_encoding_base64_url_decode(arena, gray_string_lit("aGVsbG8_Pg=="));
    ASSERT(padded.v1 == NULL);
    ASSERT_GRAY_STR(padded.v0, "hello?>");
}

static void test_encoding_base64_url_decode_rejects_malformed_input(void) {
    ASSERT(gray_encoding_base64_url_decode(arena, gray_string_lit("a$b")).v1 != NULL);
    ASSERT(gray_encoding_base64_url_decode(arena, gray_string_lit("a")).v1 != NULL);
}

static void test_encoding_base64_url_round_trips_every_length(void) {
    const char *samples[] = {"", "a", "ab", "abc", "abcd", "\xFF\xFE\xFD\xFC\xFB"};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        GrayString original = gray_string_new(arena, samples[i], (int32_t)strlen(samples[i]));
        GrayResult_string decoded = gray_encoding_base64_url_decode(arena, gray_encoding_base64_url_encode(arena, original));
        ASSERT(decoded.v1 == NULL);
        ASSERT(gray_string_eq(decoded.v0, original));
    }
}

static void test_encoding_base64_decode_rejects_bad_characters_and_lengths(void) {
    ASSERT(gray_encoding_base64_decode(arena, gray_string_lit("@@@@")).v1 != NULL);
    ASSERT(gray_encoding_base64_decode(arena, gray_string_lit("abc")).v1 != NULL);
    ASSERT(gray_encoding_base64_decode(arena, gray_string_lit("YQ=")).v1 != NULL);
}

static void test_encoding_hex_decode_rejects_odd_length_and_bad_digits(void) {
    ASSERT(gray_encoding_hex_decode(arena, gray_string_lit("abc")).v1 != NULL);
    ASSERT(gray_encoding_hex_decode(arena, gray_string_lit("zz")).v1 != NULL);
}

static void test_encoding_html_escape_replaces_the_five_characters(void) {
    ASSERT_GRAY_STR(gray_encoding_html_escape(arena, gray_string_lit("a & b")), "a &amp; b");
    ASSERT_GRAY_STR(gray_encoding_html_escape(arena, gray_string_lit("<tag>")), "&lt;tag&gt;");
    ASSERT_GRAY_STR(gray_encoding_html_escape(arena, gray_string_lit("say \"hi\"")), "say &quot;hi&quot;");
    GrayString apostrophe = gray_encoding_html_escape(arena, gray_string_lit("it's"));
    ASSERT(memchr(apostrophe.data, '\'', (size_t)apostrophe.len) == NULL);
    ASSERT_EQ(gray_encoding_html_escape(arena, gray_string_lit("")).len, 0);
    ASSERT_GRAY_STR(gray_encoding_html_escape(arena, gray_string_lit("plain text 123")), "plain text 123");
}

static void test_encoding_html_unescape_named_entities(void) {
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&amp;&lt;&gt;&quot;&apos;")), "&<>\"'");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("a &amp; b")), "a & b");
}

static void test_encoding_html_unescape_numeric_references(void) {
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&#39;c&#39;")), "'c'");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&#65;&#x42;&#X43;")), "ABC");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&#xe9;")), "é");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&#x20AC;")), "€");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&#x1F600;")), "😀");
}

static void test_encoding_html_unescape_leaves_unknown_entities_untouched(void) {
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&nosuch;")), "&nosuch;");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("a & b")), "a & b");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&amp")), "&amp");
    ASSERT_GRAY_STR(gray_encoding_html_unescape(arena, gray_string_lit("&#;")), "&#;");
}

static void test_encoding_html_escape_unescape_round_trip(void) {
    GrayString original = gray_string_lit("<a href=\"x\" title='y'>R&D</a>");
    GrayString restored = gray_encoding_html_unescape(arena, gray_encoding_html_escape(arena, original));
    ASSERT(gray_string_eq(restored, original));
}

static void test_encoding_shell_escape_safe_text_is_unchanged(void) {
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("hello")), "hello");
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("file-1.txt")), "file-1.txt");
}

static void test_encoding_shell_escape_quotes_unsafe_text(void) {
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("hello world")), "'hello world'");
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("a;b")), "'a;b'");
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("$HOME")), "'$HOME'");
}

static void test_encoding_shell_escape_embedded_single_quote(void) {
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("it's here")), "'it'\\''s here'");
}

static void test_encoding_shell_escape_empty_string_is_quoted(void) {
    ASSERT_GRAY_STR(gray_encoding_shell_escape(arena, gray_string_lit("")), "''");
}

static void test_encoding_from_string_and_to_string_round_trip(void) {
    GrayArray bytes = gray_encoding_from_string(arena, gray_string_lit("héllo"));
    ASSERT_EQ(bytes.len, 6);
    ASSERT_EQ(bytes.elem_kind, GRAY_ELEM_U8);
    ASSERT_GRAY_STR(gray_encoding_to_string(arena, &bytes), "héllo");
}

static void test_encoding_from_string_empty(void) {
    GrayArray bytes = gray_encoding_from_string(arena, gray_string_lit(""));
    ASSERT_EQ(bytes.len, 0);
}

static void test_encoding_to_hex_and_from_hex(void) {
    GrayArray bytes = gray_encoding_from_string(arena, gray_string_lit("AB\x01"));
    ASSERT_GRAY_STR(gray_encoding_to_hex(arena, &bytes), "414201");
    GrayResult_array decoded = gray_encoding_from_hex(arena, gray_string_lit("414201"));
    ASSERT(decoded.v1 == NULL);
    ASSERT_EQ(decoded.v0.len, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(decoded.v0, uint8_t, 0), 0x41);
    ASSERT_EQ(GRAY_ARRAY_GET(decoded.v0, uint8_t, 2), 0x01);
}

static void test_encoding_from_hex_accepts_uppercase_and_rejects_bad_input(void) {
    GrayResult_array upper = gray_encoding_from_hex(arena, gray_string_lit("FF00"));
    ASSERT(upper.v1 == NULL);
    ASSERT_EQ(GRAY_ARRAY_GET(upper.v0, uint8_t, 0), 255);
    ASSERT(gray_encoding_from_hex(arena, gray_string_lit("abc")).v1 != NULL);
    ASSERT(gray_encoding_from_hex(arena, gray_string_lit("xy")).v1 != NULL);
}

static void test_encoding_to_base64_and_from_base64(void) {
    GrayArray bytes = gray_encoding_from_string(arena, gray_string_lit("hello"));
    ASSERT_GRAY_STR(gray_encoding_to_base64(arena, &bytes), "aGVsbG8=");
    GrayResult_array decoded = gray_encoding_from_base64(arena, gray_string_lit("aGVsbG8="));
    ASSERT(decoded.v1 == NULL);
    ASSERT_GRAY_STR(gray_encoding_to_string(arena, &decoded.v0), "hello");
    ASSERT(gray_encoding_from_base64(arena, gray_string_lit("@@@@")).v1 != NULL);
}

static void test_encoding_to_hex_and_to_base64_of_empty_array(void) {
    GrayArray empty = gray_encoding_from_string(arena, gray_string_lit(""));
    ASSERT_EQ(gray_encoding_to_hex(arena, &empty).len, 0);
    ASSERT_EQ(gray_encoding_to_base64(arena, &empty).len, 0);
}

/* ===== binary: single bytes and wide integers ===== */

static void test_binary_decode_single_bytes(void) {
    GrayArray bytes = GRAY_ARRAY_FROM_OF(arena, uint8_t, ((uint8_t[]){0xFF}), 1);
    ASSERT_EQ(gray_binary_decode_u8(&bytes, __FILE__, __LINE__), 255);
    ASSERT_EQ(gray_binary_decode_i8(&bytes, __FILE__, __LINE__), -1);
    GrayArray small = GRAY_ARRAY_FROM_OF(arena, uint8_t, ((uint8_t[]){0x2A}), 1);
    ASSERT_EQ(gray_binary_decode_u8(&small, __FILE__, __LINE__), 42);
}

static void test_binary_i128_negative_one_is_all_ones_in_both_orders(void) {
    gray_i128 minus_one = {UINT64_MAX, -1};
    GrayArray little = gray_binary_encode_i128_le(arena, minus_one);
    GrayArray big = gray_binary_encode_i128_be(arena, minus_one);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, i), 0xFF);
        ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, i), 0xFF);
    }
    gray_i128 from_little = gray_binary_decode_i128_le(&little, __FILE__, __LINE__);
    gray_i128 from_big = gray_binary_decode_i128_be(&big, __FILE__, __LINE__);
    ASSERT(from_little.low == UINT64_MAX && from_little.high == -1);
    ASSERT(from_big.low == UINT64_MAX && from_big.high == -1);
}

static void test_binary_i128_high_and_low_limbs_swap_between_orders(void) {
    gray_i128 value = {0x0807060504030201ULL, 0x100F0E0D0C0B0A09LL};
    GrayArray little = gray_binary_encode_i128_le(arena, value);
    GrayArray big = gray_binary_encode_i128_be(arena, value);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, i), i + 1);
        ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, i), 16 - i);
    }
    gray_i128 round_trip = gray_binary_decode_i128_be(&big, __FILE__, __LINE__);
    ASSERT(round_trip.low == value.low && round_trip.high == value.high);
}

static void test_binary_u128_round_trip_in_both_orders(void) {
    gray_u128 value = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL};
    GrayArray little = gray_binary_encode_u128_le(arena, value);
    GrayArray big = gray_binary_encode_u128_be(arena, value);
    gray_u128 from_little = gray_binary_decode_u128_le(&little, __FILE__, __LINE__);
    gray_u128 from_big = gray_binary_decode_u128_be(&big, __FILE__, __LINE__);
    ASSERT(from_little.low == value.low && from_little.high == value.high);
    ASSERT(from_big.low == value.low && from_big.high == value.high);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 0), 0x99);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 15), 0x88);
    ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, 0), 0x88);
    ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, 15), 0x99);
}

static void test_binary_i256_round_trip_in_both_orders(void) {
    gray_i256 value = {{0x0102030405060708ULL, 0x1112131415161718ULL, 0x2122232425262728ULL, 0x3132333435363738ULL}};
    GrayArray little = gray_binary_encode_i256_le(arena, value);
    GrayArray big = gray_binary_encode_i256_be(arena, value);
    ASSERT_EQ(little.len, 32);
    ASSERT_EQ(big.len, 32);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 0), 0x31);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 31), 0x08);
    ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, 0), 0x08);
    ASSERT_EQ(GRAY_ARRAY_GET(little, uint8_t, 31), 0x31);
    gray_i256 from_little = gray_binary_decode_i256_le(&little, __FILE__, __LINE__);
    gray_i256 from_big = gray_binary_decode_i256_be(&big, __FILE__, __LINE__);
    for (int limb = 0; limb < 4; limb++) {
        ASSERT(from_little.w[limb] == value.w[limb]);
        ASSERT(from_big.w[limb] == value.w[limb]);
    }
}

static void test_binary_u256_round_trip_in_both_orders(void) {
    gray_u256 value = {{1, 2, 3, 0xFFFFFFFFFFFFFFFFULL}};
    GrayArray little = gray_binary_encode_u256_le(arena, value);
    GrayArray big = gray_binary_encode_u256_be(arena, value);
    gray_u256 from_little = gray_binary_decode_u256_le(&little, __FILE__, __LINE__);
    gray_u256 from_big = gray_binary_decode_u256_be(&big, __FILE__, __LINE__);
    for (int limb = 0; limb < 4; limb++) {
        ASSERT(from_little.w[limb] == value.w[limb]);
        ASSERT(from_big.w[limb] == value.w[limb]);
    }
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 0), 0xFF);
    ASSERT_EQ(GRAY_ARRAY_GET(big, uint8_t, 31), 0x01);
}

/* ===== fmt: fields, number and byte formatting ===== */

static void test_fmt_string_field_pads_to_width(void) {
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("ab"), 5, false, -1), "   ab");
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("ab"), 5, true, -1), "ab   ");
}

static void test_fmt_string_field_wider_than_width_is_unchanged(void) {
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abcdef"), 3, false, -1), "abcdef");
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abc"), 0, false, -1), "abc");
}

static void test_fmt_string_field_precision_truncates(void) {
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abcdef"), 0, false, 3), "abc");
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abc"), 0, false, 10), "abc");
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abcdef"), 0, false, 0), "");
}

static void test_fmt_string_field_precision_then_width(void) {
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abcdef"), 6, false, 2), "    ab");
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit("abcdef"), 6, true, 2), "ab    ");
}

static void test_fmt_string_field_empty_string(void) {
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit(""), 3, false, -1), "   ");
    ASSERT_GRAY_STR(gray_fmt_string_field(arena, gray_string_lit(""), 0, false, -1), "");
}

static void test_fmt_char_field_pads_to_width(void) {
    ASSERT_GRAY_STR(gray_fmt_char_field(arena, 'x', 4, false), "   x");
    ASSERT_GRAY_STR(gray_fmt_char_field(arena, 'x', 4, true), "x   ");
    ASSERT_GRAY_STR(gray_fmt_char_field(arena, 'x', 1, false), "x");
    ASSERT_GRAY_STR(gray_fmt_char_field(arena, 'x', 0, false), "x");
}

static void test_fmt_char_field_multibyte_character_counts_as_one_column(void) {
    ASSERT_GRAY_STR(gray_fmt_char_field(arena, 0xE9, 3, false), "  é");
    ASSERT_GRAY_STR(gray_fmt_char_field(arena, 0x20AC, 3, true), "€  ");
}

static void test_fmt_format_number_groups_thousands(void) {
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, 0), "0");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, 999), "999");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, 1000), "1,000");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, 1234567), "1,234,567");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, 100000), "100,000");
}

static void test_fmt_format_number_negative(void) {
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, -1000), "-1,000");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, -5), "-5");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, -123456), "-123,456");
}

static void test_fmt_format_number_extremes(void) {
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, INT64_MAX), "9,223,372,036,854,775,807");
    ASSERT_GRAY_STR(gray_fmt_format_number(arena, INT64_MIN), "-9,223,372,036,854,775,808");
}

static void test_fmt_format_bytes_below_one_kibibyte_is_whole_bytes(void) {
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 0), "0 B");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1), "1 B");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1023), "1023 B");
}

static void test_fmt_format_bytes_uses_binary_units_with_one_decimal(void) {
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1024), "1.0 KiB");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1536), "1.5 KiB");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1048576), "1.0 MiB");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1073741824LL), "1.0 GiB");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1099511627776LL), "1.0 TiB");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, 1125899906842624LL), "1.0 PiB");
}

static void test_fmt_format_bytes_largest_value_stays_in_pebibytes(void) {
    GrayString result = gray_fmt_format_bytes(arena, INT64_MAX);
    ASSERT(result.len > 4);
    ASSERT_EQ(memcmp(result.data + result.len - 3, "PiB", 3), 0);
}

static void test_fmt_format_bytes_negative(void) {
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, -512), "-512 B");
    ASSERT_GRAY_STR(gray_fmt_format_bytes(arena, -1536), "-1.5 KiB");
}

/* ===== strings: case words, comparison, splitting, truncation, builder ===== */

static void test_strings_to_title(void) {
    ASSERT_GRAY_STR(gray_strings_to_title(arena, gray_string_lit("hello world")), "Hello World");
    ASSERT_GRAY_STR(gray_strings_to_title(arena, gray_string_lit("hELLO wORLD")), "Hello World");
    ASSERT_GRAY_STR(gray_strings_to_title(arena, gray_string_lit("  two  spaces ")), "  Two  Spaces ");
    ASSERT_GRAY_STR(gray_strings_to_title(arena, gray_string_lit("")), "");
    ASSERT_GRAY_STR(gray_strings_to_title(arena, gray_string_lit("a")), "A");
}

static void test_strings_to_camel_case(void) {
    ASSERT_GRAY_STR(gray_strings_to_camel_case(arena, gray_string_lit("hello_world")), "helloWorld");
    ASSERT_GRAY_STR(gray_strings_to_camel_case(arena, gray_string_lit("hello world")), "helloWorld");
    ASSERT_GRAY_STR(gray_strings_to_camel_case(arena, gray_string_lit("hello-big-world")), "helloBigWorld");
    ASSERT_GRAY_STR(gray_strings_to_camel_case(arena, gray_string_lit("")), "");
}

static void test_strings_to_pascal_case(void) {
    ASSERT_GRAY_STR(gray_strings_to_pascal_case(arena, gray_string_lit("hello_world")), "HelloWorld");
    ASSERT_GRAY_STR(gray_strings_to_pascal_case(arena, gray_string_lit("hello world")), "HelloWorld");
    ASSERT_GRAY_STR(gray_strings_to_pascal_case(arena, gray_string_lit("hello-big-world")), "HelloBigWorld");
}

static void test_strings_capitalize(void) {
    ASSERT_GRAY_STR(gray_strings_capitalize(arena, gray_string_lit("hello")), "Hello");
    ASSERT_GRAY_STR(gray_strings_capitalize(arena, gray_string_lit("hELLO")), "HELLO");
    ASSERT_GRAY_STR(gray_strings_capitalize(arena, gray_string_lit("1abc")), "1abc");
    ASSERT_GRAY_STR(gray_strings_capitalize(arena, gray_string_lit("")), "");
    ASSERT_GRAY_STR(gray_strings_capitalize(arena, gray_string_lit("x")), "X");
}

static void test_strings_contains_any(void) {
    ASSERT(gray_strings_contains_any(gray_string_lit("hello"), gray_string_lit("xyzo")));
    ASSERT(!gray_strings_contains_any(gray_string_lit("hello"), gray_string_lit("xyz")));
    ASSERT(!gray_strings_contains_any(gray_string_lit("hello"), gray_string_lit("")));
    ASSERT(!gray_strings_contains_any(gray_string_lit(""), gray_string_lit("abc")));
    ASSERT(gray_strings_contains_any(gray_string_lit("héllo"), gray_string_lit("é")));
}

static void test_strings_equal_fold(void) {
    ASSERT(gray_strings_equal_fold(gray_string_lit("Hello"), gray_string_lit("hELLO")));
    ASSERT(gray_strings_equal_fold(gray_string_lit(""), gray_string_lit("")));
    ASSERT(!gray_strings_equal_fold(gray_string_lit("abc"), gray_string_lit("abd")));
    ASSERT(!gray_strings_equal_fold(gray_string_lit("abc"), gray_string_lit("abcd")));
    ASSERT(!gray_strings_equal_fold(gray_string_lit("a1"), gray_string_lit("a2")));
}

static void test_strings_compare(void) {
    ASSERT_EQ(gray_strings_compare(gray_string_lit("a"), gray_string_lit("b")), -1);
    ASSERT_EQ(gray_strings_compare(gray_string_lit("b"), gray_string_lit("a")), 1);
    ASSERT_EQ(gray_strings_compare(gray_string_lit("same"), gray_string_lit("same")), 0);
    ASSERT_EQ(gray_strings_compare(gray_string_lit("ab"), gray_string_lit("abc")), -1);
    ASSERT_EQ(gray_strings_compare(gray_string_lit("abc"), gray_string_lit("ab")), 1);
    ASSERT_EQ(gray_strings_compare(gray_string_lit(""), gray_string_lit("")), 0);
    ASSERT_EQ(gray_strings_compare(gray_string_lit(""), gray_string_lit("a")), -1);
    ASSERT_EQ(gray_strings_compare(gray_string_lit("Z"), gray_string_lit("a")), -1);
}

static void test_strings_truncate_leaves_short_strings_alone(void) {
    ASSERT_GRAY_STR(gray_strings_truncate(arena, gray_string_lit("hello"), 5, gray_string_lit("...")), "hello");
    ASSERT_GRAY_STR(gray_strings_truncate(arena, gray_string_lit("hi"), 10, gray_string_lit("...")), "hi");
}

static void test_strings_truncate_total_length_is_exactly_max(void) {
    GrayString result = gray_strings_truncate(arena, gray_string_lit("hello world"), 8, gray_string_lit("..."));
    ASSERT_GRAY_STR(result, "hello...");
    ASSERT_EQ(result.len, 8);
}

static void test_strings_truncate_with_empty_ellipsis(void) {
    ASSERT_GRAY_STR(gray_strings_truncate(arena, gray_string_lit("hello world"), 5, gray_string_lit("")), "hello");
    ASSERT_GRAY_STR(gray_strings_truncate(arena, gray_string_lit("hello"), 0, gray_string_lit("")), "");
}

static void test_strings_truncate_max_equal_to_ellipsis_length(void) {
    ASSERT_GRAY_STR(gray_strings_truncate(arena, gray_string_lit("hello world"), 3, gray_string_lit("...")), "...");
}

static void test_strings_split_whitespace(void) {
    GrayArray parts = gray_strings_split_whitespace(arena, gray_string_lit("  a  bb\tccc\n d "));
    ASSERT_EQ(parts.len, 4);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 0), "a");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 1), "bb");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 2), "ccc");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 3), "d");
}

static void test_strings_split_whitespace_blank_input_is_empty(void) {
    ASSERT_EQ(gray_strings_split_whitespace(arena, gray_string_lit("")).len, 0);
    ASSERT_EQ(gray_strings_split_whitespace(arena, gray_string_lit(" \t\n ")).len, 0);
}

static void test_strings_split_n_keeps_the_remainder_in_the_last_piece(void) {
    GrayArray parts = gray_strings_split_n(arena, gray_string_lit("a,b,c,d"), gray_string_lit(","), 3);
    ASSERT_EQ(parts.len, 3);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 0), "a");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 1), "b");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 2), "c,d");
}

static void test_strings_split_n_with_one_piece_is_the_whole_string(void) {
    GrayArray parts = gray_strings_split_n(arena, gray_string_lit("a,b,c"), gray_string_lit(","), 1);
    ASSERT_EQ(parts.len, 1);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 0), "a,b,c");
}

static void test_strings_split_n_non_positive_count_is_empty(void) {
    ASSERT_EQ(gray_strings_split_n(arena, gray_string_lit("a,b"), gray_string_lit(","), 0).len, 0);
    ASSERT_EQ(gray_strings_split_n(arena, gray_string_lit("a,b"), gray_string_lit(","), -4).len, 0);
}

static void test_strings_split_n_count_larger_than_pieces(void) {
    GrayArray parts = gray_strings_split_n(arena, gray_string_lit("a,b"), gray_string_lit(","), 10);
    ASSERT_EQ(parts.len, 2);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 1), "b");
}

static void test_strings_split_n_multi_character_separator(void) {
    GrayArray parts = gray_strings_split_n(arena, gray_string_lit("a::b::c"), gray_string_lit("::"), 2);
    ASSERT_EQ(parts.len, 2);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 0), "a");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 1), "b::c");
}

static void test_strings_builder_appends_in_order(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append(builder, gray_string_lit("ab"));
    gray_strings_builder_append(builder, gray_string_lit("cd"));
    ASSERT_EQ(gray_strings_builder_len(builder), 4);
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "abcd");
}

static void test_strings_builder_append_char_encodes_utf8(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append_char(builder, 'a');
    gray_strings_builder_append_char(builder, 0xE9);
    gray_strings_builder_append_char(builder, 0x20AC);
    gray_strings_builder_append_char(builder, 0x1F600);
    ASSERT_EQ(gray_strings_builder_len(builder), 10);
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "aé€😀");
}

static void test_strings_builder_append_bytes(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    GrayArray data = GRAY_ARRAY_FROM_OF(arena, uint8_t, ((uint8_t[]){'x', 'y', 'z'}), 3);
    gray_strings_builder_append_bytes(builder, data);
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "xyz");
}

static void test_strings_builder_append_i64(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append_i64(builder, 42);
    gray_strings_builder_append_i64(builder, -7);
    gray_strings_builder_append_i64(builder, 0);
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "42-70");
}

static void test_strings_builder_append_i64_extremes(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append_i64(builder, INT64_MIN);
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "-9223372036854775808");
}

static void test_strings_builder_append_line_adds_newline(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append_line(builder, gray_string_lit("one"));
    gray_strings_builder_append_line(builder, gray_string_lit("two"));
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "one\ntwo\n");
}

static void test_strings_builder_clear_resets_length_and_stays_usable(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append(builder, gray_string_lit("discarded"));
    gray_strings_builder_clear(builder);
    ASSERT_EQ(gray_strings_builder_len(builder), 0);
    gray_strings_builder_append(builder, gray_string_lit("kept"));
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "kept");
}

static void test_strings_builder_build_leaves_builder_usable(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append(builder, gray_string_lit("ab"));
    GrayString first = gray_strings_build(arena, builder);
    gray_strings_builder_append(builder, gray_string_lit("cd"));
    ASSERT_GRAY_STR(first, "ab");
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "abcd");
}

static void test_strings_builder_grows_past_initial_capacity(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    for (int i = 0; i < 5000; i++) gray_strings_builder_append(builder, gray_string_lit("0123456789"));
    ASSERT_EQ(gray_strings_builder_len(builder), 50000);
    GrayString built = gray_strings_build(arena, builder);
    ASSERT_EQ(built.len, 50000);
    ASSERT_EQ(memcmp(built.data + 49990, "0123456789", 10), 0);
}

static void test_strings_builder_reserve_does_not_change_contents(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append(builder, gray_string_lit("ab"));
    gray_strings_builder_reserve(builder, 4096);
    gray_strings_builder_reserve(builder, -5);
    ASSERT_EQ(gray_strings_builder_len(builder), 2);
    ASSERT_GRAY_STR(gray_strings_build(arena, builder), "ab");
}

static void test_strings_builder_empty_builds_empty_string(void) {
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    ASSERT_EQ(gray_strings_builder_len(builder), 0);
    ASSERT_EQ(gray_strings_build(arena, builder).len, 0);
}

/* ===== builtins: print family, stdin, system, composite to_string ===== */

/* Runs `emit` with `stream` (stdout or stderr) redirected into a temporary
 * file and returns what it wrote, in a static buffer. */
static char captured_output[512];

static const char *capture_stream(FILE *stream, void (*emit)(void)) {
    fflush(stream);
    int stream_descriptor = fileno(stream);
    int saved_descriptor = dup(stream_descriptor);
    FILE *sink = tmpfile();
    dup2(fileno(sink), stream_descriptor);
    emit();
    fflush(stream);
    dup2(saved_descriptor, stream_descriptor);
    close(saved_descriptor);
    rewind(sink);
    size_t length = fread(captured_output, 1, sizeof(captured_output) - 1, sink);
    captured_output[length] = '\0';
    fclose(sink);
    return captured_output;
}

#define CAPTURE_TEST(NAME, STREAM, CALL, EXPECTED) \
    static void emit_##NAME(void) { CALL; } \
    static void test_##NAME(void) { ASSERT_STR_EQ(capture_stream(STREAM, emit_##NAME), EXPECTED); }

CAPTURE_TEST(builtin_println_str, stdout, gray_builtin_println_str(gray_string_lit("hi")), "hi\n")
CAPTURE_TEST(builtin_print_str, stdout, gray_builtin_print_str(gray_string_lit("hi")), "hi")
CAPTURE_TEST(builtin_println_i64, stdout, gray_builtin_println_i64(-42), "-42\n")
CAPTURE_TEST(builtin_print_i64, stdout, gray_builtin_print_i64(INT64_MIN), "-9223372036854775808")
CAPTURE_TEST(builtin_println_u64, stdout, gray_builtin_println_u64(UINT64_MAX), "18446744073709551615\n")
CAPTURE_TEST(builtin_print_u64, stdout, gray_builtin_print_u64(7), "7")
CAPTURE_TEST(builtin_println_f64, stdout, gray_builtin_println_floating_point(1.5, 64), "1.5\n")
CAPTURE_TEST(builtin_print_f64, stdout, gray_builtin_print_floating_point(0.1, 64), "0.1")
CAPTURE_TEST(builtin_println_f32, stdout, gray_builtin_println_floating_point((double)0.1f, 32), "0.1\n")
CAPTURE_TEST(builtin_println_bool_true, stdout, gray_builtin_println_bool(true), "true\n")
CAPTURE_TEST(builtin_print_bool_false, stdout, gray_builtin_print_bool(false), "false")
CAPTURE_TEST(builtin_println_char_ascii, stdout, gray_builtin_println_char('A'), "A\n")
CAPTURE_TEST(builtin_print_char_multibyte, stdout, gray_builtin_print_char(0x20AC), "€")
CAPTURE_TEST(builtin_println_addr, stdout, gray_builtin_println_addr(0xBEEF), "0xbeef\n")
CAPTURE_TEST(builtin_print_addr, stdout, gray_builtin_print_addr(0x10), "0x10")
CAPTURE_TEST(builtin_eprintln_str, stderr, gray_builtin_eprintln_str(gray_string_lit("oops")), "oops\n")
CAPTURE_TEST(builtin_eprint_str, stderr, gray_builtin_eprint_str(gray_string_lit("oops")), "oops")
CAPTURE_TEST(builtin_eprintln_i64, stderr, gray_builtin_eprintln_i64(-3), "-3\n")
CAPTURE_TEST(builtin_eprint_i64, stderr, gray_builtin_eprint_i64(9), "9")
CAPTURE_TEST(builtin_eprintln_u64, stderr, gray_builtin_eprintln_u64(5), "5\n")
CAPTURE_TEST(builtin_eprint_u64, stderr, gray_builtin_eprint_u64(6), "6")
CAPTURE_TEST(builtin_eprintln_f64, stderr, gray_builtin_eprintln_floating_point(2.25, 64), "2.25\n")
CAPTURE_TEST(builtin_eprint_f64, stderr, gray_builtin_eprint_floating_point(2.25, 64), "2.25")
CAPTURE_TEST(builtin_eprintln_bool, stderr, gray_builtin_eprintln_bool(true), "true\n")
CAPTURE_TEST(builtin_eprint_bool, stderr, gray_builtin_eprint_bool(false), "false")
CAPTURE_TEST(builtin_eprintln_char, stderr, gray_builtin_eprintln_char('z'), "z\n")
CAPTURE_TEST(builtin_eprint_char, stderr, gray_builtin_eprint_char(0xE9), "é")
CAPTURE_TEST(builtin_eprintln_addr, stderr, gray_builtin_eprintln_addr(0xAB), "0xab\n")
CAPTURE_TEST(builtin_eprint_addr, stderr, gray_builtin_eprint_addr(0xCD), "0xcd")

static void emit_builtin_print_keeps_embedded_nul(void) {
    gray_builtin_print_str(gray_string_new(arena, "a\0b", 3));
}

static void test_builtin_print_keeps_embedded_nul(void) {
    capture_stream(stdout, emit_builtin_print_keeps_embedded_nul);
    ASSERT_EQ(captured_output[0], 'a');
    ASSERT_EQ(captured_output[1], 0);
    ASSERT_EQ(captured_output[2], 'b');
}

static void emit_builtin_print_then_flush(void) {
    gray_builtin_print_str(gray_string_lit("buffered"));
    gray_builtin_flush();
}

static void test_builtin_flush_writes_pending_output(void) {
    ASSERT_STR_EQ(capture_stream(stdout, emit_builtin_print_then_flush), "buffered");
}

static void test_builtin_input_strips_carriage_return_and_reads_one_line(void) {
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    fputs("first\r\nsecond\n", tmp);
    rewind(tmp);
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    GrayString first = gray_builtin_input(arena);
    GrayString second = gray_builtin_input(arena);
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
    fclose(tmp);
    ASSERT_GRAY_STR(first, "first");
    ASSERT_GRAY_STR(second, "second");
}

static void test_builtin_input_without_terminator_returns_the_rest(void) {
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    fputs("no newline", tmp);
    rewind(tmp);
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    GrayString line = gray_builtin_input(arena);
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
    fclose(tmp);
    ASSERT_GRAY_STR(line, "no newline");
}

static void test_builtin_input_longer_than_the_initial_buffer(void) {
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    for (int i = 0; i < 5000; i++) fputc('x', tmp);
    fputc('\n', tmp);
    rewind(tmp);
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    GrayString line = gray_builtin_input(arena);
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
    fclose(tmp);
    ASSERT_EQ(line.len, 5000);
}

static void test_builtin_sleep_ms_and_ns_wait_at_least_the_requested_time(void) {
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    gray_builtin_sleep_ms(30);
    gray_builtin_sleep_ns(20000000);
    clock_gettime(CLOCK_MONOTONIC, &after);
    long long elapsed_ns = (long long)(after.tv_sec - before.tv_sec) * 1000000000LL + (after.tv_nsec - before.tv_nsec);
    ASSERT_GE(elapsed_ns, 45000000LL);
}

#ifndef _WIN32
static void test_builtin_system_returns_the_exit_status(void) {
    ASSERT_EQ(gray_builtin_system(gray_string_lit("exit 0")), 0);
    ASSERT_EQ(gray_builtin_system(gray_string_lit("exit 3")), 3);
    ASSERT_EQ(gray_builtin_system(gray_string_lit("false")), 1);
}

static void test_builtin_system_runs_the_command(void) {
    char path[] = "/tmp/gray_system_testXXXXXX";
    int descriptor = mkstemp(path);
    ASSERT_NE(descriptor, -1);
    close(descriptor);
    char command[128];
    snprintf(command, sizeof(command), "echo ran > %s", path);
    ASSERT_EQ(gray_builtin_system(gray_string_new(arena, command, (int32_t)strlen(command))), 0);
    FILE *file = fopen(path, "r");
    ASSERT_NOT_NULL(file);
    char contents[16] = {0};
    fread(contents, 1, sizeof(contents) - 1, file);
    fclose(file);
    remove(path);
    ASSERT_STR_EQ(contents, "ran\n");
}
#endif

static void test_builtin_assert_true_is_a_no_op(void) {
    gray_builtin_assert(true, gray_string_lit("never shown"), __FILE__, __LINE__);
    gray_builtin_assert(true, gray_string_lit(""), __FILE__, __LINE__);
}

static void test_builtin_map_to_string_string_keys_and_integer_values(void) {
    GrayMap map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString first_key = gray_string_lit("a");
    GrayString second_key = gray_string_lit("b");
    int64_t one = 1, two = 2;
    gray_map_set(arena, &map, &first_key, &one, __FILE__, __LINE__);
    gray_map_set(arena, &map, &second_key, &two, __FILE__, __LINE__);
    ASSERT_GRAY_STR(gray_builtin_map_to_string(arena, &map, 0), "{\"a\": 1, \"b\": 2}");
}

static void test_builtin_map_to_string_empty_map(void) {
    GrayMap map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    ASSERT_GRAY_STR(gray_builtin_map_to_string(arena, &map, 0), "{:}");
}

static void test_builtin_map_to_string_string_values_are_quoted(void) {
    GrayMap map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    GrayString key = gray_string_lit("k");
    GrayString value = gray_string_lit("v");
    gray_map_set(arena, &map, &key, &value, __FILE__, __LINE__);
    ASSERT_GRAY_STR(gray_builtin_map_to_string(arena, &map, 2), "{\"k\": \"v\"}");
}

static void test_builtin_map_to_string_bool_and_float_values(void) {
    GrayMap flags = gray_map_new_kind(arena, sizeof(GrayString), sizeof(bool), 0, GRAY_ELEM_STRING, GRAY_ELEM_BOOL);
    GrayString key = gray_string_lit("on");
    bool yes = true;
    gray_map_set(arena, &flags, &key, &yes, __FILE__, __LINE__);
    ASSERT_GRAY_STR(gray_builtin_map_to_string(arena, &flags, 3), "{\"on\": true}");
    GrayMap ratios = gray_map_new_kind(arena, sizeof(GrayString), sizeof(double), 0, GRAY_ELEM_STRING, GRAY_ELEM_F64);
    GrayString ratio_key = gray_string_lit("half");
    double half = 0.5;
    gray_map_set(arena, &ratios, &ratio_key, &half, __FILE__, __LINE__);
    ASSERT_GRAY_STR(gray_builtin_map_to_string(arena, &ratios, 1), "{\"half\": 0.5}");
}

static void test_builtin_map_to_string_skips_removed_entries(void) {
    GrayMap map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(int64_t), 0, GRAY_ELEM_STRING, GRAY_ELEM_I64);
    GrayString first_key = gray_string_lit("a");
    GrayString second_key = gray_string_lit("b");
    int64_t one = 1, two = 2;
    gray_map_set(arena, &map, &first_key, &one, __FILE__, __LINE__);
    gray_map_set(arena, &map, &second_key, &two, __FILE__, __LINE__);
    gray_map_remove(&map, &first_key, __FILE__, __LINE__);
    ASSERT_GRAY_STR(gray_builtin_map_to_string(arena, &map, 0), "{\"b\": 2}");
}

static void test_builtin_array_to_string_strings_bools_floats_and_chars(void) {
    GrayString texts[2] = {gray_string_lit("x"), gray_string_lit("y")};
    GrayArray strings = GRAY_ARRAY_FROM_OF(arena, GrayString, texts, 2);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &strings, 2), "{\"x\", \"y\"}");
    bool flags[2] = {true, false};
    GrayArray bools = GRAY_ARRAY_FROM_OF(arena, bool, flags, 2);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &bools, 3), "{true, false}");
    double numbers[2] = {1.5, 2.0};
    GrayArray floats = GRAY_ARRAY_FROM_OF(arena, double, numbers, 2);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &floats, 1), "{1.5, 2.0}");
    int32_t letters[2] = {'a', 0xE9};
    GrayArray chars = gray_array_from(arena, letters, sizeof(int32_t), 2, GRAY_ELEM_CHAR);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &chars, 6), "{'a', 'é'}");
}

static void test_builtin_array_to_string_unsigned_and_wide_integers(void) {
    uint64_t big[2] = {UINT64_MAX, 0};
    GrayArray unsigned_array = GRAY_ARRAY_FROM_OF(arena, uint64_t, big, 2);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &unsigned_array, 0), "{18446744073709551615, 0}");
    gray_i128 wide[1] = {{UINT64_MAX, -1}};
    GrayArray wide_array = GRAY_ARRAY_FROM_OF(arena, gray_i128, wide, 1);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &wide_array, 0), "{-1}");
    int8_t small[2] = {-5, 7};
    GrayArray small_array = GRAY_ARRAY_FROM_OF(arena, int8_t, small, 2);
    ASSERT_GRAY_STR(gray_builtin_array_to_string(arena, &small_array, 0), "{-5, 7}");
}

static void test_builtin_string_to_wide_integers_reject_nothing_valid(void) {
    gray_i128 negative = gray_builtin_string_to_i128(gray_string_lit("-170141183460469231731687303715884105728"), __FILE__, __LINE__);
    ASSERT(negative.low == 0 && negative.high == INT64_MIN);
    gray_u128 largest = gray_builtin_string_to_u128(gray_string_lit("340282366920938463463374607431768211455"), __FILE__, __LINE__);
    ASSERT(largest.low == UINT64_MAX && largest.high == UINT64_MAX);
    gray_u256 zero = gray_builtin_string_to_u256(gray_string_lit("0"), __FILE__, __LINE__);
    for (int limb = 0; limb < 4; limb++) ASSERT(zero.w[limb] == 0);
    gray_i128 padded = gray_builtin_string_to_i128(gray_string_lit("  +12  "), __FILE__, __LINE__);
    ASSERT(padded.low == 12 && padded.high == 0);
}

/* ===== math and arrays: remaining functions ===== */

static void test_math_modf_splits_sign_carrying_parts(void) {
    GrayMathModf positive = gray_math_modf(3.75);
    ASSERT_FLOAT_EQ(positive.v0, 3.0);
    ASSERT_FLOAT_EQ(positive.v1, 0.75);
    GrayMathModf negative = gray_math_modf(-3.75);
    ASSERT_FLOAT_EQ(negative.v0, -3.0);
    ASSERT_FLOAT_EQ(negative.v1, -0.75);
    GrayMathModf whole = gray_math_modf(5.0);
    ASSERT_FLOAT_EQ(whole.v0, 5.0);
    ASSERT_FLOAT_EQ(whole.v1, 0.0);
}

static void test_math_is_power_of_two(void) {
    ASSERT(gray_math_is_power_of_two(1));
    ASSERT(gray_math_is_power_of_two(2));
    ASSERT(gray_math_is_power_of_two(1024));
    ASSERT(gray_math_is_power_of_two((int64_t)1 << 62));
    ASSERT(!gray_math_is_power_of_two(0));
    ASSERT(!gray_math_is_power_of_two(-4));
    ASSERT(!gray_math_is_power_of_two(6));
    ASSERT(!gray_math_is_power_of_two(INT64_MIN));
}

static void test_math_next_power_of_two(void) {
    ASSERT_EQ(gray_math_next_power_of_two(0), 1);
    ASSERT_EQ(gray_math_next_power_of_two(-9), 1);
    ASSERT_EQ(gray_math_next_power_of_two(1), 1);
    ASSERT_EQ(gray_math_next_power_of_two(2), 2);
    ASSERT_EQ(gray_math_next_power_of_two(3), 4);
    ASSERT_EQ(gray_math_next_power_of_two(1000), 1024);
    ASSERT_EQ(gray_math_next_power_of_two(1024), 1024);
    ASSERT_EQ(gray_math_next_power_of_two((int64_t)1 << 62), (int64_t)1 << 62);
}

static void test_math_factorial_values(void) {
    ASSERT_EQ(gray_math_factorial(0), 1);
    ASSERT_EQ(gray_math_factorial(1), 1);
    ASSERT_EQ(gray_math_factorial(5), 120);
    ASSERT_EQ(gray_math_factorial(20), 2432902008176640000LL);
}

static void test_math_gcd_and_lcm(void) {
    ASSERT_EQ(gray_math_gcd(12, 18), 6);
    ASSERT_EQ(gray_math_gcd(18, 12), 6);
    ASSERT_EQ(gray_math_gcd(7, 13), 1);
    ASSERT_EQ(gray_math_gcd(0, 5), 5);
    ASSERT_EQ(gray_math_gcd(-12, 18), 6);
    ASSERT_EQ(gray_math_lcm(4, 6), 12);
    ASSERT_EQ(gray_math_lcm(7, 13), 91);
    ASSERT_EQ(gray_math_lcm(0, 5), 0);
}

static void test_arrays_swap_exchanges_in_place(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4);
    gray_arrays_swap(&values, 0, 3);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 0), 4);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 3), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 1), 2);
    gray_arrays_swap(&values, 2, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 2), 3);
}

static void test_arrays_swap_strings(void) {
    GrayString texts[2] = {gray_string_lit("a"), gray_string_lit("b")};
    GrayArray values = GRAY_ARRAY_FROM_OF(arena, GrayString, texts, 2);
    gray_arrays_swap(&values, 0, 1);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(values, GrayString, 0), "b");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(values, GrayString, 1), "a");
}

static void test_arrays_rotate_left_and_right(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4, 5);
    GrayArray left = gray_arrays_rotate(arena, &values, 2);
    ASSERT_EQ(GRAY_ARRAY_GET(left, int64_t, 0), 3);
    ASSERT_EQ(GRAY_ARRAY_GET(left, int64_t, 4), 2);
    GrayArray right = gray_arrays_rotate(arena, &values, -1);
    ASSERT_EQ(GRAY_ARRAY_GET(right, int64_t, 0), 5);
    ASSERT_EQ(GRAY_ARRAY_GET(right, int64_t, 1), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 0), 1);
}

static void test_arrays_rotate_by_zero_or_a_full_turn_is_a_copy(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    GrayArray none = gray_arrays_rotate(arena, &values, 0);
    GrayArray full = gray_arrays_rotate(arena, &values, 3);
    GrayArray more = gray_arrays_rotate(arena, &values, 4);
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(GRAY_ARRAY_GET(none, int64_t, i), i + 1);
        ASSERT_EQ(GRAY_ARRAY_GET(full, int64_t, i), i + 1);
    }
    ASSERT_EQ(GRAY_ARRAY_GET(more, int64_t, 0), 2);
}

static void test_arrays_rotate_empty(void) {
    GrayArray empty = GRAY_ARRAY_NEW_OF(arena, int64_t, 0);
    ASSERT_EQ(gray_arrays_rotate(arena, &empty, 5).len, 0);
}

static void test_arrays_counts_tallies_in_first_seen_order(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 3, 1, 3, 3, 2, 1);
    GrayMap counts = gray_arrays_counts(arena, &values);
    ASSERT_EQ(counts.count, 3);
    int64_t key = 3;
    ASSERT_EQ(*(int64_t *)gray_map_get(&counts, &key), 3);
    key = 1;
    ASSERT_EQ(*(int64_t *)gray_map_get(&counts, &key), 2);
    key = 2;
    ASSERT_EQ(*(int64_t *)gray_map_get(&counts, &key), 1);
    ASSERT_EQ(*(int64_t *)((char *)counts.keys + (size_t)counts.order[0] * counts.key_size), 3);
}

static void test_arrays_counts_strings(void) {
    GrayString words[4] = {gray_string_lit("a"), gray_string_lit("b"), gray_string_lit("a"), gray_string_lit("a")};
    GrayArray values = GRAY_ARRAY_FROM_OF(arena, GrayString, words, 4);
    GrayMap counts = gray_arrays_counts(arena, &values);
    ASSERT_EQ(counts.count, 2);
    GrayString key = gray_string_lit("a");
    ASSERT_EQ(*(int64_t *)gray_map_get(&counts, &key), 3);
}

static void test_arrays_counts_empty(void) {
    GrayArray empty = GRAY_ARRAY_NEW_OF(arena, int64_t, 0);
    ASSERT_EQ(gray_arrays_counts(arena, &empty).count, 0);
}

static void test_arrays_binary_search_finds_and_misses(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 1, 3, 5, 7, 9, 11);
    int64_t present = 7, absent = 4, first = 1, last = 11, below = 0, above = 100;
    ASSERT_EQ(gray_arrays_binary_search(&values, &present), 3);
    ASSERT_EQ(gray_arrays_binary_search(&values, &first), 0);
    ASSERT_EQ(gray_arrays_binary_search(&values, &last), 5);
    ASSERT_EQ(gray_arrays_binary_search(&values, &absent), -1);
    ASSERT_EQ(gray_arrays_binary_search(&values, &below), -1);
    ASSERT_EQ(gray_arrays_binary_search(&values, &above), -1);
}

static void test_arrays_binary_search_empty_and_strings(void) {
    GrayArray empty = GRAY_ARRAY_NEW_OF(arena, int64_t, 0);
    int64_t needle = 1;
    ASSERT_EQ(gray_arrays_binary_search(&empty, &needle), -1);
    GrayString words[3] = {gray_string_lit("apple"), gray_string_lit("banana"), gray_string_lit("cherry")};
    GrayArray values = GRAY_ARRAY_FROM_OF(arena, GrayString, words, 3);
    GrayString wanted = gray_string_lit("banana");
    ASSERT_EQ(gray_arrays_binary_search(&values, &wanted), 1);
    GrayString missing = gray_string_lit("blueberry");
    ASSERT_EQ(gray_arrays_binary_search(&values, &missing), -1);
}

static void test_arrays_is_sorted(void) {
    GrayArray sorted = GRAY_ARRAY_FROM_I64(arena, 1, 2, 2, 5);
    GrayArray unsorted = GRAY_ARRAY_FROM_I64(arena, 1, 3, 2);
    GrayArray single = GRAY_ARRAY_FROM_I64(arena, 9);
    GrayArray empty = GRAY_ARRAY_NEW_OF(arena, int64_t, 0);
    ASSERT(gray_arrays_is_sorted(&sorted));
    ASSERT(!gray_arrays_is_sorted(&unsorted));
    ASSERT(gray_arrays_is_sorted(&single));
    ASSERT(gray_arrays_is_sorted(&empty));
}

static void test_arrays_is_sorted_strings(void) {
    GrayString ordered[2] = {gray_string_lit("a"), gray_string_lit("b")};
    GrayString reversed[2] = {gray_string_lit("b"), gray_string_lit("a")};
    GrayArray ascending = GRAY_ARRAY_FROM_OF(arena, GrayString, ordered, 2);
    GrayArray descending = GRAY_ARRAY_FROM_OF(arena, GrayString, reversed, 2);
    ASSERT(gray_arrays_is_sorted(&ascending));
    ASSERT(!gray_arrays_is_sorted(&descending));
}

static void test_arrays_average(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3, 4);
    ASSERT_FLOAT_EQ(gray_arrays_average(&values, __FILE__, __LINE__), 2.5);
    double floats[3] = {1.0, 2.0, 6.0};
    GrayArray decimals = GRAY_ARRAY_FROM_OF(arena, double, floats, 3);
    ASSERT_FLOAT_EQ(gray_arrays_average(&decimals, __FILE__, __LINE__), 3.0);
    GrayArray negatives = GRAY_ARRAY_FROM_I64(arena, -4, 4);
    ASSERT_FLOAT_EQ(gray_arrays_average(&negatives, __FILE__, __LINE__), 0.0);
}

static void test_arrays_sort_descending_and_strings(void) {
    GrayArray values = GRAY_ARRAY_FROM_I64(arena, 3, 1, 2);
    gray_arrays_sort(&values, true);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 0), 3);
    ASSERT_EQ(GRAY_ARRAY_GET(values, int64_t, 2), 1);
    GrayString words[3] = {gray_string_lit("pear"), gray_string_lit("apple"), gray_string_lit("fig")};
    GrayArray texts = GRAY_ARRAY_FROM_OF(arena, GrayString, words, 3);
    gray_arrays_sort(&texts, false);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(texts, GrayString, 0), "apple");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(texts, GrayString, 2), "pear");
    gray_arrays_sort(&texts, true);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(texts, GrayString, 0), "pear");
}

static void test_arrays_sort_floats_and_unsigned(void) {
    double floats[3] = {2.5, -1.0, 0.0};
    GrayArray decimals = GRAY_ARRAY_FROM_OF(arena, double, floats, 3);
    gray_arrays_sort(&decimals, false);
    ASSERT_FLOAT_EQ(GRAY_ARRAY_GET(decimals, double, 0), -1.0);
    ASSERT_FLOAT_EQ(GRAY_ARRAY_GET(decimals, double, 2), 2.5);
    uint64_t unsigned_values[3] = {UINT64_MAX, 0, 5};
    GrayArray unsigned_array = GRAY_ARRAY_FROM_OF(arena, uint64_t, unsigned_values, 3);
    gray_arrays_sort(&unsigned_array, false);
    ASSERT_EQ(GRAY_ARRAY_GET(unsigned_array, uint64_t, 0), 0);
    ASSERT(GRAY_ARRAY_GET(unsigned_array, uint64_t, 2) == UINT64_MAX);
}

/* ===== json: validation grammar, string escapes, typed field helpers ===== */

static GrayString json_decoded_value(GrayMap *map, const char *key) {
    GrayString *value = (GrayString *)gray_map_get_str(map, gray_string_lit(key));
    return value ? *value : gray_string_lit("<missing>");
}

static void test_json_is_valid_accepts_every_value_kind(void) {
    const char *valid[] = {
        "null", "true", "false", "0", "-0", "12", "-12", "1.5", "-1.5e10", "1E+5", "2e-3", "0.0",
        "\"\"", "\"a\"", "[]", "{}", "[ ]", "{ }", "  [1 , 2 ]  ", "{\"a\" : [ {\"b\":null} ] }",
        "\"\\n\\t\\\\\\/\\\"\\b\\f\\r\"", "\"\\u00e9\\uD83D\\uDE00\"", "[[[]]]", "[1,[2,[3]]]",
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        if (!gray_json_is_valid(gray_string_new(arena, valid[i], (int32_t)strlen(valid[i])))) {
            fprintf(stderr, "  expected valid: %s\n", valid[i]);
            ASSERT(false);
        }
    }
}

static void test_json_is_valid_rejects_malformed_documents(void) {
    const char *invalid[] = {
        "", "   ", "nul", "tru", "falsey", "01", "1.", ".5", "+1", "--1", "1e", "1e+", "0x10", "NaN", "Infinity",
        "\"unterminated", "\"bad \\q escape\"", "\"bad \\u12G4\"", "\"short \\u12\"", "'single'",
        "[1,]", "[,1]", "[1 2]", "[1,,2]", "[", "]", "{", "}", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{a:1}",
        "{\"a\":1 \"b\":2}", "{1:2}", "[1] [2]", "{} extra", "true false",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        if (gray_json_is_valid(gray_string_new(arena, invalid[i], (int32_t)strlen(invalid[i])))) {
            fprintf(stderr, "  expected invalid: %s\n", invalid[i]);
            ASSERT(false);
        }
    }
}

static void test_json_is_valid_rejects_raw_control_characters_in_strings(void) {
    ASSERT(!gray_json_is_valid(gray_string_new(arena, "\"a\nb\"", 5)));
    ASSERT(!gray_json_is_valid(gray_string_new(arena, "\"a\tb\"", 5)));
}

static void test_json_is_valid_rejects_excessive_nesting(void) {
    char deep[2048];
    int depth = 700;
    for (int i = 0; i < depth; i++) deep[i] = '[';
    for (int i = 0; i < depth; i++) deep[depth + i] = ']';
    ASSERT(!gray_json_is_valid(gray_string_new(arena, deep, depth * 2)));
    char shallow[64];
    for (int i = 0; i < 20; i++) shallow[i] = '[';
    for (int i = 0; i < 20; i++) shallow[20 + i] = ']';
    ASSERT(gray_json_is_valid(gray_string_new(arena, shallow, 40)));
}

static void test_json_decode_unescapes_simple_escapes(void) {
    GrayMap map = gray_json_decode(arena, gray_string_lit("{\"k\":\"a\\nb\\tc\\\\d\\\"e\\/f\\bg\\fh\\ri\"}"));
    ASSERT_GRAY_STR(json_decoded_value(&map, "k"), "a\nb\tc\\d\"e/f\bg\fh\ri");
}

static void test_json_decode_unicode_escapes_become_utf8(void) {
    GrayMap map = gray_json_decode(arena, gray_string_lit("{\"k\":\"\\u0041\\u00e9\\u20AC\"}"));
    ASSERT_GRAY_STR(json_decoded_value(&map, "k"), "Aé€");
}

static void test_json_decode_surrogate_pair_becomes_one_codepoint(void) {
    GrayMap map = gray_json_decode(arena, gray_string_lit("{\"k\":\"\\uD83D\\uDE00\"}"));
    ASSERT_GRAY_STR(json_decoded_value(&map, "k"), "😀");
}

static void test_json_decode_lone_surrogates_become_replacement_character(void) {
    GrayMap high = gray_json_decode(arena, gray_string_lit("{\"k\":\"\\uD83Dx\"}"));
    ASSERT_GRAY_STR(json_decoded_value(&high, "k"), "\xEF\xBF\xBD" "x");
    GrayMap low = gray_json_decode(arena, gray_string_lit("{\"k\":\"\\uDE00\"}"));
    ASSERT_GRAY_STR(json_decoded_value(&low, "k"), "\xEF\xBF\xBD");
}

static void test_json_decode_values_of_every_kind_become_text(void) {
    GrayMap map = gray_json_decode(arena, gray_string_lit("{\"n\":12,\"f\":1.5,\"t\":true,\"z\":null,\"s\":\"x\",\"a\":[1,2],\"o\":{\"i\":1}}"));
    ASSERT_EQ(map.count, 7);
    ASSERT_GRAY_STR(json_decoded_value(&map, "n"), "12");
    ASSERT_GRAY_STR(json_decoded_value(&map, "f"), "1.5");
    ASSERT_GRAY_STR(json_decoded_value(&map, "t"), "true");
    ASSERT_GRAY_STR(json_decoded_value(&map, "z"), "null");
    ASSERT_GRAY_STR(json_decoded_value(&map, "s"), "x");
    ASSERT_GRAY_STR(json_decoded_value(&map, "a"), "[1,2]");
    ASSERT_GRAY_STR(json_decoded_value(&map, "o"), "{\"i\":1}");
}

static void test_json_decode_nested_text_keeps_brackets_inside_strings_intact(void) {
    GrayMap map = gray_json_decode(arena, gray_string_lit("{\"a\":[\"]\",\"}\"],\"b\":1}"));
    ASSERT_GRAY_STR(json_decoded_value(&map, "a"), "[\"]\",\"}\"]");
    ASSERT_GRAY_STR(json_decoded_value(&map, "b"), "1");
}

static void test_json_decode_empty_object(void) {
    GrayMap map = gray_json_decode(arena, gray_string_lit("{}"));
    ASSERT_EQ(map.count, 0);
}

static void test_json_decode_result_rejects_non_objects(void) {
    ASSERT(gray_json_decode_result(arena, gray_string_lit("[1,2]")).v1 != NULL);
    ASSERT(gray_json_decode_result(arena, gray_string_lit("\"text\"")).v1 != NULL);
    ASSERT(gray_json_decode_result(arena, gray_string_lit("{bad")).v1 != NULL);
    ASSERT(gray_json_decode_result(arena, gray_string_lit("")).v1 != NULL);
    ASSERT(gray_json_decode_result(arena, gray_string_lit("  {\"a\":\"b\"}  ")).v1 == NULL);
}

static void test_json_unquote_decodes_a_quoted_element(void) {
    ASSERT_GRAY_STR(gray_json_unquote(arena, gray_string_lit("\"a\\u0042c\"")), "aBc");
    ASSERT_GRAY_STR(gray_json_unquote(arena, gray_string_lit("\"\"")), "");
}

static void test_json_split_array_separates_top_level_elements(void) {
    GrayArray parts = gray_json_split_array(arena, gray_string_lit("[1, \"a,b\", [2,3], {\"k\":[4,5]}, true]"));
    ASSERT_EQ(parts.len, 5);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 0), "1");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 1), "\"a,b\"");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 2), "[2,3]");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 3), "{\"k\":[4,5]}");
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 4), "true");
}

static void test_json_split_array_empty_and_non_array(void) {
    ASSERT_EQ(gray_json_split_array(arena, gray_string_lit("[]")).len, 0);
    ASSERT_EQ(gray_json_split_array(arena, gray_string_lit("[ ]")).len, 0);
    ASSERT_EQ(gray_json_split_array(arena, gray_string_lit("{\"a\":1}")).len, 0);
    ASSERT_EQ(gray_json_split_array(arena, gray_string_lit("")).len, 0);
}

static void test_json_split_array_string_with_escaped_quote(void) {
    GrayArray parts = gray_json_split_array(arena, gray_string_lit("[\"a\\\",b\", 2]"));
    ASSERT_EQ(parts.len, 2);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(parts, GrayString, 0), "\"a\\\",b\"");
}

static void test_json_encode_array_wide_integer(void) {
    gray_i128 values[3] = {{1, 0}, {UINT64_MAX, -1}, {0, 0}};
    GrayArray array = GRAY_ARRAY_FROM_OF(arena, gray_i128, values, 3);
    ASSERT_GRAY_STR(gray_json_encode_array_wide_integer(arena, &array), "[1,-1,0]");
    gray_u256 big[1] = {{{0, 0, 1, 0}}};
    GrayArray big_array = GRAY_ARRAY_FROM_OF(arena, gray_u256, big, 1);
    ASSERT_GRAY_STR(gray_json_encode_array_wide_integer(arena, &big_array), "[340282366920938463463374607431768211456]");
    GrayArray empty = GRAY_ARRAY_NEW_OF(arena, gray_i128, 0);
    ASSERT_GRAY_STR(gray_json_encode_array_wide_integer(arena, &empty), "[]");
}

static void test_json_number_text_for_each_kind(void) {
    int8_t signed_small = -5;
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_I8, &signed_small), "-5");
    uint16_t unsigned_small = 65535;
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_U16, &unsigned_small), "65535");
    uint64_t unsigned_big = UINT64_MAX;
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_U64, &unsigned_big), "18446744073709551615");
    int64_t signed_big = INT64_MIN;
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_I64, &signed_big), "-9223372036854775808");
    double half = 0.5;
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_F64, &half), "0.5");
    float quarter = 0.25f;
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_F32, &quarter), "0.25");
    gray_u128 wide = {5, 0};
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_U128, &wide), "5");
    gray_i256 wide_negative = {{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX}};
    ASSERT_GRAY_STR(gray_json_number_text(arena, GRAY_ELEM_I256, &wide_negative), "-1");
}

static void test_json_enum_from_number_accepts_known_variants(void) {
    int64_t variants[3] = {0, 5, 9};
    ASSERT_EQ(gray_json_enum_from_number(gray_string_lit("5"), 5, variants, 3, "Level"), 5);
    ASSERT_EQ(gray_json_enum_from_number(gray_string_lit("0"), 0, variants, 3, "Level"), 0);
}

static void test_json_enum_from_str_accepts_known_variants(void) {
    GrayString variants[3] = {gray_string_lit("LOW"), gray_string_lit("HIGH"), gray_string_lit("")};
    ASSERT_GRAY_STR(gray_json_enum_from_str(gray_string_lit("HIGH"), variants, 3, "Level"), "HIGH");
    ASSERT_GRAY_STR(gray_json_enum_from_str(gray_string_lit("LOW"), variants, 3, "Level"), "LOW");
    ASSERT_EQ(gray_json_enum_from_str(gray_string_lit(""), variants, 3, "Level").len, 0);
}

static void test_json_is_null(void) {
    ASSERT(gray_json_is_null(gray_string_lit("null")));
    ASSERT(!gray_json_is_null(gray_string_lit("nul")));
    ASSERT(!gray_json_is_null(gray_string_lit("\"null\"")));
    ASSERT(!gray_json_is_null(gray_string_lit("")));
}

static void test_json_field_as_bool(void) {
    ASSERT(gray_json_field_as_bool(gray_string_lit("true"), gray_string_lit("flag"), __FILE__, __LINE__));
    ASSERT(!gray_json_field_as_bool(gray_string_lit("false"), gray_string_lit("flag"), __FILE__, __LINE__));
}

static void test_json_decode_fields_records_which_values_were_quoted(void) {
    GrayMap quoted = gray_map_new_kind(arena, sizeof(GrayString), sizeof(bool), 0, GRAY_ELEM_STRING, GRAY_ELEM_BOOL);
    GrayMap fields = gray_json_decode_fields(arena, gray_string_lit("{\"name\":\"Ann\",\"age\":30}"), &quoted, __FILE__, __LINE__);
    ASSERT_GRAY_STR(json_decoded_value(&fields, "name"), "Ann");
    ASSERT_GRAY_STR(json_decoded_value(&fields, "age"), "30");
    GrayString name_key = gray_string_lit("name");
    GrayString age_key = gray_string_lit("age");
    ASSERT_NOT_NULL(gray_map_get(&quoted, &name_key));
    ASSERT(gray_map_get(&quoted, &age_key) == NULL);
    gray_json_check_field_quoting(&quoted, name_key, true, "a string", __FILE__, __LINE__);
    gray_json_check_field_quoting(&quoted, age_key, false, "a number", __FILE__, __LINE__);
}

static void test_json_require_document_accepts_the_expected_opening(void) {
    gray_json_require_document(gray_string_lit("  {\"a\":1}"), '{', __FILE__, __LINE__);
    gray_json_require_document(gray_string_lit("[1]"), '[', __FILE__, __LINE__);
}

static void test_json_require_array_field_accepts_unquoted_array(void) {
    GrayMap quoted = gray_map_new_kind(arena, sizeof(GrayString), sizeof(bool), 0, GRAY_ELEM_STRING, GRAY_ELEM_BOOL);
    gray_json_require_array_field(&quoted, gray_string_lit("items"), gray_string_lit("  [1,2]"), __FILE__, __LINE__);
}

static void test_json_element_as_string_unquotes(void) {
    GrayString element = gray_json_element_as_string(arena, gray_string_lit("\"a\\nb\""), gray_string_lit("names"), __FILE__, __LINE__);
    ASSERT_GRAY_STR(element, "a\nb");
}

static void test_json_encode_string_escapes_special_characters(void) {
    ASSERT_GRAY_STR(gray_json_encode_string(arena, gray_string_lit("a\"b\\c\nd\te")), "\"a\\\"b\\\\c\\nd\\te\"");
    ASSERT_GRAY_STR(gray_json_encode_string(arena, gray_string_lit("")), "\"\"");
    ASSERT_GRAY_STR(gray_json_encode_string(arena, gray_string_new(arena, "\x01", 1)), "\"\\u0001\"");
    ASSERT_GRAY_STR(gray_json_encode_string(arena, gray_string_lit("é€")), "\"é€\"");
}

static void test_json_encode_then_decode_round_trips_a_string(void) {
    GrayString original = gray_string_lit("line1\nline2 \"quoted\" \\ tab\t é");
    GrayString encoded = gray_json_encode_string(arena, original);
    ASSERT(gray_json_is_valid(encoded));
    ASSERT(gray_string_eq(gray_json_unquote(arena, encoded), original));
}

/* ===== regex: groups, counting, escaping, result variants ===== */

static GrayString regex_group_at(GrayArray groups, int32_t index) {
    return GRAY_ARRAY_GET(groups, GrayString, index);
}

static void test_regex_find_groups_whole_match_then_captures(void) {
    GrayArray groups = gray_regex_find_groups(arena, gray_string_lit("([a-z]+)@([a-z]+)\\.com"), gray_string_lit("mail bob@example.com now"));
    ASSERT_EQ(groups.len, 3);
    ASSERT_GRAY_STR(regex_group_at(groups, 0), "bob@example.com");
    ASSERT_GRAY_STR(regex_group_at(groups, 1), "bob");
    ASSERT_GRAY_STR(regex_group_at(groups, 2), "example");
}

static void test_regex_find_groups_no_match_is_empty(void) {
    ASSERT_EQ(gray_regex_find_groups(arena, gray_string_lit("([0-9]+)"), gray_string_lit("letters")).len, 0);
}

static void test_regex_find_groups_non_participating_group_is_empty_string(void) {
    GrayArray groups = gray_regex_find_groups(arena, gray_string_lit("(a)|(b)"), gray_string_lit("b"));
    ASSERT_EQ(groups.len, 3);
    ASSERT_GRAY_STR(regex_group_at(groups, 0), "b");
    ASSERT_EQ(regex_group_at(groups, 1).len, 0);
    ASSERT_GRAY_STR(regex_group_at(groups, 2), "b");
}

static void test_regex_find_groups_pattern_without_groups_has_only_the_whole_match(void) {
    GrayArray groups = gray_regex_find_groups(arena, gray_string_lit("[0-9]+"), gray_string_lit("abc 123"));
    ASSERT_EQ(groups.len, 1);
    ASSERT_GRAY_STR(regex_group_at(groups, 0), "123");
}

static void test_regex_find_all_groups_every_match(void) {
    GrayArray all = gray_regex_find_all_groups(arena, gray_string_lit("([a-z])=([0-9])"), gray_string_lit("a=1, b=2, c=3"));
    ASSERT_EQ(all.len, 3);
    GrayArray second = GRAY_ARRAY_GET(all, GrayArray, 1);
    ASSERT_EQ(second.len, 3);
    ASSERT_GRAY_STR(regex_group_at(second, 0), "b=2");
    ASSERT_GRAY_STR(regex_group_at(second, 1), "b");
    ASSERT_GRAY_STR(regex_group_at(second, 2), "2");
}

static void test_regex_find_all_groups_no_match_is_empty(void) {
    ASSERT_EQ(gray_regex_find_all_groups(arena, gray_string_lit("([0-9])"), gray_string_lit("none")).len, 0);
}

static void test_regex_count_non_overlapping_matches(void) {
    GrayResult_i64 result = gray_regex_count_result(arena, gray_string_lit("ab"), gray_string_lit("ababab"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0, 3);
    ASSERT_EQ(gray_regex_count_result(arena, gray_string_lit("aa"), gray_string_lit("aaaa")).v0, 2);
    ASSERT_EQ(gray_regex_count_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("1 22 333")).v0, 3);
}

static void test_regex_count_zero_when_nothing_matches(void) {
    GrayResult_i64 result = gray_regex_count_result(arena, gray_string_lit("z"), gray_string_lit("abc"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0, 0);
    ASSERT_EQ(gray_regex_count_result(arena, gray_string_lit("a"), gray_string_lit("")).v0, 0);
}

static void test_regex_count_invalid_pattern_is_an_error(void) {
    GrayResult_i64 result = gray_regex_count_result(arena, gray_string_lit("("), gray_string_lit("abc"));
    ASSERT(result.v1 != NULL);
    ASSERT_EQ(result.v0, 0);
}

static void test_regex_escape_metacharacters(void) {
    ASSERT_GRAY_STR(gray_regex_escape(arena, gray_string_lit("a.b")), "a\\.b");
    ASSERT_GRAY_STR(gray_regex_escape(arena, gray_string_lit("1+1=2")), "1\\+1=2");
    ASSERT_GRAY_STR(gray_regex_escape(arena, gray_string_lit("plain")), "plain");
    ASSERT_GRAY_STR(gray_regex_escape(arena, gray_string_lit("")), "");
}

static void test_regex_escaped_text_matches_itself_literally(void) {
    const char *samples[] = {"a.b", "(x)", "[y]", "a|b", "^start$", "star*", "q?", "{2}", "back\\slash", "1+1"};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        GrayString literal = gray_string_new(arena, samples[i], (int32_t)strlen(samples[i]));
        GrayString pattern = gray_regex_escape(arena, literal);
        GrayResult_string found = gray_regex_find_result(arena, pattern, literal);
        ASSERT(found.v1 == NULL);
        ASSERT(gray_string_eq(found.v0, literal));
    }
    ASSERT(!gray_regex_match(gray_regex_escape(arena, gray_string_lit("a.b")), gray_string_lit("axb")));
}

static void test_regex_find_result_no_match_is_empty_string_without_error(void) {
    GrayResult_string result = gray_regex_find_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("letters"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 0);
}

static void test_regex_find_result_invalid_pattern(void) {
    GrayResult_string result = gray_regex_find_result(arena, gray_string_lit("[unclosed"), gray_string_lit("text"));
    ASSERT(result.v1 != NULL);
    ASSERT_EQ(result.v0.len, 0);
}

static void test_regex_find_all_result_matches_and_errors(void) {
    GrayResult_array result = gray_regex_find_all_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("a1 b22 c333"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 3);
    ASSERT_GRAY_STR(regex_group_at(result.v0, 2), "333");
    GrayResult_array bad = gray_regex_find_all_result(arena, gray_string_lit("(("), gray_string_lit("text"));
    ASSERT(bad.v1 != NULL);
    ASSERT_EQ(bad.v0.len, 0);
}

static void test_regex_find_groups_result_matches_and_errors(void) {
    GrayResult_array result = gray_regex_find_groups_result(arena, gray_string_lit("(a)(b)"), gray_string_lit("xaby"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 3);
    GrayResult_array bad = gray_regex_find_groups_result(arena, gray_string_lit("(a"), gray_string_lit("a"));
    ASSERT(bad.v1 != NULL);
    ASSERT_EQ(bad.v0.len, 0);
}

static void test_regex_find_all_groups_result_matches_and_errors(void) {
    GrayResult_array result = gray_regex_find_all_groups_result(arena, gray_string_lit("(a)"), gray_string_lit("aXa"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 2);
    GrayResult_array bad = gray_regex_find_all_groups_result(arena, gray_string_lit("[a"), gray_string_lit("a"));
    ASSERT(bad.v1 != NULL);
    ASSERT_EQ(bad.v0.len, 0);
}

static void test_regex_replace_result_replaces_every_match_with_literal_text(void) {
    GrayResult_string result = gray_regex_replace_result(arena, gray_string_lit("[0-9]+"), gray_string_lit("a1 b22"), gray_string_lit("#"));
    ASSERT(result.v1 == NULL);
    ASSERT_GRAY_STR(result.v0, "a# b#");
    GrayResult_string literal = gray_regex_replace_result(arena, gray_string_lit("a"), gray_string_lit("banana"), gray_string_lit("\\1&"));
    ASSERT_GRAY_STR(literal.v0, "b\\1&n\\1&n\\1&");
}

static void test_regex_replace_result_no_match_returns_original_text(void) {
    GrayResult_string result = gray_regex_replace_result(arena, gray_string_lit("z"), gray_string_lit("abc"), gray_string_lit("-"));
    ASSERT(result.v1 == NULL);
    ASSERT_GRAY_STR(result.v0, "abc");
}

static void test_regex_replace_result_with_empty_replacement_deletes_matches(void) {
    GrayResult_string result = gray_regex_replace_result(arena, gray_string_lit("[aeiou]"), gray_string_lit("education"), gray_string_lit(""));
    ASSERT_GRAY_STR(result.v0, "dctn");
}

static void test_regex_replace_result_invalid_pattern_returns_original_text(void) {
    GrayResult_string result = gray_regex_replace_result(arena, gray_string_lit("(["), gray_string_lit("keep"), gray_string_lit("x"));
    ASSERT(result.v1 != NULL);
    ASSERT_GRAY_STR(result.v0, "keep");
}

static void test_regex_replace_result_handles_utf8_text(void) {
    GrayResult_string result = gray_regex_replace_result(arena, gray_string_lit("é"), gray_string_lit("café é"), gray_string_lit("e"));
    ASSERT_GRAY_STR(result.v0, "cafe e");
}

static void test_regex_split_result_on_pattern(void) {
    GrayResult_array result = gray_regex_split_result(arena, gray_string_lit("[,;] *"), gray_string_lit("a, b;c ,d"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 4);
    ASSERT_GRAY_STR(regex_group_at(result.v0, 0), "a");
    ASSERT_GRAY_STR(regex_group_at(result.v0, 1), "b");
    ASSERT_GRAY_STR(regex_group_at(result.v0, 2), "c ");
    ASSERT_GRAY_STR(regex_group_at(result.v0, 3), "d");
}

static void test_regex_split_result_no_match_is_the_whole_text(void) {
    GrayResult_array result = gray_regex_split_result(arena, gray_string_lit("[,;]"), gray_string_lit("abc"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 1);
    ASSERT_GRAY_STR(regex_group_at(result.v0, 0), "abc");
}

static void test_regex_split_result_invalid_pattern_is_an_error(void) {
    GrayResult_array result = gray_regex_split_result(arena, gray_string_lit("(["), gray_string_lit("keep"));
    ASSERT(result.v1 != NULL);
}

static void test_regex_perl_shorthand_classes_are_invalid_patterns(void) {
    const char *shorthand[] = {"\\d", "\\w", "\\s", "\\b", "\\D", "\\W", "\\S", "\\B", "a\\d+"};
    for (size_t i = 0; i < sizeof(shorthand) / sizeof(shorthand[0]); i++)
        ASSERT(!gray_regex_is_valid(gray_string_new(arena, shorthand[i], (int32_t)strlen(shorthand[i]))));
    ASSERT(gray_regex_is_valid(gray_string_lit("[[:digit:]]+")));
}

static void test_regex_find_all_utf8_characters(void) {
    GrayResult_array result = gray_regex_find_all_result(arena, gray_string_lit("[^ ]+"), gray_string_lit("héllo wörld"));
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 2);
    ASSERT_GRAY_STR(regex_group_at(result.v0, 1), "wörld");
}

/* ===== net: result variants, receive_line, resolve, errors ===== */

/* A connected pair on loopback. The kernel queues the dial until accept, so
 * one thread can drive both ends. */
typedef struct {
    GraySocket listener;
    GraySocket client;
    GraySocket server;
} NetPair;

static NetPair net_open_pair(int64_t port) {
    NetPair pair;
    pair.listener = gray_net_listen(arena, port, gray_string_lit("127.0.0.1"));
    pair.client = gray_net_dial(arena, gray_string_lit("127.0.0.1"), port);
    pair.server = gray_net_accept(arena, pair.listener);
    return pair;
}

static void net_close_pair(NetPair pair) {
    gray_net_close(pair.client);
    gray_net_close(pair.server);
    gray_net_close(pair.listener);
}

static void test_net_listen_result_dial_result_and_accept_result(void) {
    GrayResult_socket listener = gray_net_listen_result(arena, 18741, gray_string_lit("127.0.0.1"));
    ASSERT(listener.v1 == NULL);
    GrayResult_socket client = gray_net_dial_result(arena, gray_string_lit("127.0.0.1"), 18741);
    ASSERT(client.v1 == NULL);
    GrayResult_socket server = gray_net_accept_result(arena, listener.v0);
    ASSERT(server.v1 == NULL);
    gray_net_close(client.v0);
    gray_net_close(server.v0);
    gray_net_close(listener.v0);
}

static void test_net_dial_result_refused_connection_is_an_error(void) {
    GrayResult_socket result = gray_net_dial_result(arena, gray_string_lit("127.0.0.1"), 1);
    ASSERT(result.v1 != NULL);
}

static void test_net_dial_result_unresolvable_host_is_an_error(void) {
    GrayResult_socket result = gray_net_dial_result(arena, gray_string_lit("no-such-host.invalid"), 80);
    ASSERT(result.v1 != NULL);
}

static void test_net_listen_result_port_in_use_is_an_error(void) {
    GrayResult_socket first = gray_net_listen_result(arena, 18742, gray_string_lit("127.0.0.1"));
    ASSERT(first.v1 == NULL);
    GrayResult_socket second = gray_net_listen_result(arena, 18742, gray_string_lit("127.0.0.1"));
    ASSERT(second.v1 != NULL);
    gray_net_close(first.v0);
}

static void test_net_send_result_reports_bytes_written(void) {
    NetPair pair = net_open_pair(18743);
    GrayResult_i64 sent = gray_net_send_result(arena, pair.client, gray_string_lit("hello world"));
    ASSERT(sent.v1 == NULL);
    ASSERT_EQ(sent.v0, 11);
    net_close_pair(pair);
}

static void test_net_send_and_recv_result_round_trip(void) {
    NetPair pair = net_open_pair(18744);
    gray_net_send_result(arena, pair.client, gray_string_lit("ping"));
    GrayResult_string received = gray_net_recv_result(arena, pair.server, 64);
    ASSERT(received.v1 == NULL);
    ASSERT_GRAY_STR(received.v0, "ping");
    net_close_pair(pair);
}

static void test_net_recv_result_respects_max_bytes(void) {
    NetPair pair = net_open_pair(18745);
    gray_net_send(pair.client, gray_string_lit("abcdef"));
    GrayResult_string first = gray_net_recv_result(arena, pair.server, 4);
    ASSERT(first.v1 == NULL);
    ASSERT_GRAY_STR(first.v0, "abcd");
    GrayResult_string rest = gray_net_recv_result(arena, pair.server, 64);
    ASSERT_GRAY_STR(rest.v0, "ef");
    net_close_pair(pair);
}

static void test_net_recv_result_after_peer_close_is_a_closed_error(void) {
    NetPair pair = net_open_pair(18746);
    gray_net_close(pair.client);
    GrayResult_string received = gray_net_recv_result(arena, pair.server, 64);
    ASSERT(received.v1 != NULL);
    ASSERT_EQ(received.v1->code, GRAY_ERR_Closed);
    ASSERT_EQ(received.v0.len, 0);
    gray_net_close(pair.server);
    gray_net_close(pair.listener);
}

static void test_net_recv_result_timeout_is_a_timeout_error(void) {
    NetPair pair = net_open_pair(18747);
    gray_net_set_timeout(pair.server, 50);
    GrayResult_string received = gray_net_recv_result(arena, pair.server, 64);
    ASSERT(received.v1 != NULL);
    ASSERT_EQ(received.v1->code, GRAY_ERR_Timeout);
    net_close_pair(pair);
}

static void test_net_receive_line_strips_lf_and_crlf(void) {
    NetPair pair = net_open_pair(18748);
    gray_net_send(pair.client, gray_string_lit("first\nsecond\r\nthird\n"));
    GrayResult_string a = gray_net_receive_line_result(arena, pair.server, 64);
    GrayResult_string b = gray_net_receive_line_result(arena, pair.server, 64);
    GrayResult_string c = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT(a.v1 == NULL && b.v1 == NULL && c.v1 == NULL);
    ASSERT_GRAY_STR(a.v0, "first");
    ASSERT_GRAY_STR(b.v0, "second");
    ASSERT_GRAY_STR(c.v0, "third");
    net_close_pair(pair);
}

static void test_net_receive_line_keeps_bytes_after_the_newline_for_receive(void) {
    NetPair pair = net_open_pair(18749);
    gray_net_send(pair.client, gray_string_lit("line\nrest"));
    GrayResult_string line = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT_GRAY_STR(line.v0, "line");
    GrayResult_string rest = gray_net_recv_result(arena, pair.server, 64);
    ASSERT_GRAY_STR(rest.v0, "rest");
    net_close_pair(pair);
}

static void test_net_receive_line_empty_line(void) {
    NetPair pair = net_open_pair(18750);
    gray_net_send(pair.client, gray_string_lit("\nafter\n"));
    GrayResult_string empty = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT(empty.v1 == NULL);
    ASSERT_EQ(empty.v0.len, 0);
    ASSERT_GRAY_STR(gray_net_receive_line_result(arena, pair.server, 64).v0, "after");
    net_close_pair(pair);
}

static void test_net_receive_line_waits_for_a_split_line(void) {
    NetPair pair = net_open_pair(18751);
    gray_net_send(pair.client, gray_string_lit("par"));
    gray_net_set_timeout(pair.server, 50);
    GrayResult_string early = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT(early.v1 != NULL);
    ASSERT_EQ(early.v1->code, GRAY_ERR_Timeout);
    gray_net_send(pair.client, gray_string_lit("tial\n"));
    GrayResult_string line = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT(line.v1 == NULL);
    ASSERT_GRAY_STR(line.v0, "partial");
    net_close_pair(pair);
}

static void test_net_receive_line_longer_than_max_bytes_is_out_of_range(void) {
    NetPair pair = net_open_pair(18752);
    gray_net_send(pair.client, gray_string_lit("0123456789\n"));
    GrayResult_string line = gray_net_receive_line_result(arena, pair.server, 4);
    ASSERT(line.v1 != NULL);
    ASSERT_EQ(line.v1->code, GRAY_ERR_OutOfRange);
    net_close_pair(pair);
}

static void test_net_receive_line_after_close_with_no_bytes_is_closed(void) {
    NetPair pair = net_open_pair(18753);
    gray_net_close(pair.client);
    GrayResult_string line = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT(line.v1 != NULL);
    ASSERT_EQ(line.v1->code, GRAY_ERR_Closed);
    gray_net_close(pair.server);
    gray_net_close(pair.listener);
}

static void test_net_receive_line_after_close_returns_the_partial_line(void) {
    NetPair pair = net_open_pair(18754);
    gray_net_send(pair.client, gray_string_lit("no newline"));
    gray_net_close(pair.client);
    GrayResult_string line = gray_net_receive_line_result(arena, pair.server, 64);
    ASSERT(line.v1 == NULL);
    ASSERT_GRAY_STR(line.v0, "no newline");
    gray_net_close(pair.server);
    gray_net_close(pair.listener);
}

static void test_net_receive_line_lines_arriving_in_one_segment_are_all_kept(void) {
    NetPair pair = net_open_pair(18755);
    gray_net_send(pair.client, gray_string_lit("a\nb\nc\nd"));
    ASSERT_GRAY_STR(gray_net_receive_line_result(arena, pair.server, 64).v0, "a");
    ASSERT_GRAY_STR(gray_net_receive_line_result(arena, pair.server, 64).v0, "b");
    ASSERT_GRAY_STR(gray_net_receive_line_result(arena, pair.server, 64).v0, "c");
    net_close_pair(pair);
}

static void test_net_resolve_localhost_and_ip_literal(void) {
    GrayResult_string literal = gray_net_resolve_result(arena, gray_string_lit("127.0.0.1"));
    ASSERT(literal.v1 == NULL);
    ASSERT_GRAY_STR(literal.v0, "127.0.0.1");
    GrayString loopback = gray_net_resolve(arena, gray_string_lit("localhost"));
    ASSERT(loopback.len > 0);
}

static void test_net_resolve_result_unknown_host_is_an_error(void) {
    GrayResult_string result = gray_net_resolve_result(arena, gray_string_lit("no-such-host.invalid"));
    ASSERT(result.v1 != NULL);
    ASSERT_EQ(result.v0.len, 0);
}

/* ===== io: directories, copying, listing, globbing, temp paths, stdin ===== */

static bool io_array_has_suffix(GrayArray entries, const char *suffix) {
    size_t suffix_length = strlen(suffix);
    for (int32_t i = 0; i < entries.len; i++) {
        GrayString entry = GRAY_ARRAY_GET(entries, GrayString, i);
        if ((size_t)entry.len >= suffix_length && memcmp(entry.data + entry.len - suffix_length, suffix, suffix_length) == 0)
            return true;
    }
    return false;
}

static bool io_array_has_exact(GrayArray entries, const char *name) {
    for (int32_t i = 0; i < entries.len; i++) {
        GrayString entry = GRAY_ARRAY_GET(entries, GrayString, i);
        if ((size_t)entry.len == strlen(name) && memcmp(entry.data, name, (size_t)entry.len) == 0) return true;
    }
    return false;
}

static GrayString io_join2(GrayString first, const char *second) {
    GrayString parts_data[2] = {first, gray_string_new(arena, second, (int32_t)strlen(second))};
    GrayArray parts = gray_array_from(arena, parts_data, sizeof(GrayString), 2, GRAY_ELEM_STRING);
    return gray_io_path_join(arena, parts);
}

/* A scratch directory holding a.txt, sub/b.txt and sub/deeper/c.log. */
static GrayString io_make_tree(const char *name) {
    GrayString root = io_tmp_path(name);
    gray_io_remove_dir_all_result(arena, root);
    gray_io_make_dir_all_result(arena, io_join2(root, "sub/deeper"));
    gray_io_write_file_result(arena, io_join2(root, "a.txt"), gray_string_lit("A"));
    gray_io_write_file_result(arena, io_join2(root, "sub/b.txt"), gray_string_lit("B"));
    gray_io_write_file_result(arena, io_join2(root, "sub/deeper/c.log"), gray_string_lit("C"));
    return root;
}

static void test_io_make_dir_all_creates_every_parent(void) {
    GrayString root = io_tmp_path("grayc_ut_makeall");
    gray_io_remove_dir_all_result(arena, root);
    GrayString nested = io_join2(root, "one/two/three");
    GrayResult_bool created = gray_io_make_dir_all_result(arena, nested);
    ASSERT(created.v1 == NULL);
    ASSERT(created.v0);
    ASSERT(gray_io_is_directory(nested));
    ASSERT(gray_io_make_dir_all(nested));
    gray_io_remove_dir_all_result(arena, root);
    ASSERT(!gray_io_file_exists(root));
}

static void test_io_make_dir_result_errors_when_parent_is_missing(void) {
    GrayString missing = io_join2(io_tmp_path("grayc_ut_no_parent"), "child");
    GrayResult_bool result = gray_io_make_dir_result(arena, missing);
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
}

static void test_io_remove_dir_result_refuses_a_non_empty_directory(void) {
    GrayString root = io_make_tree("grayc_ut_nonempty");
    GrayResult_bool result = gray_io_remove_dir_result(arena, root);
    ASSERT(result.v1 != NULL);
    ASSERT(gray_io_is_directory(root));
    ASSERT(gray_io_remove_dir_all_result(arena, root).v1 == NULL);
    ASSERT(!gray_io_file_exists(root));
}

static void test_io_remove_dir_all_removes_nested_contents(void) {
    GrayString root = io_make_tree("grayc_ut_removeall");
    ASSERT(gray_io_remove_dir_all(root));
    ASSERT(!gray_io_file_exists(root));
}

static void test_io_list_dir_result_names_entries_without_dot_entries(void) {
    GrayString root = io_make_tree("grayc_ut_listdir");
    GrayResult_array listing = gray_io_list_dir_result(arena, root);
    ASSERT(listing.v1 == NULL);
    ASSERT_EQ(listing.v0.len, 2);
    ASSERT(io_array_has_exact(listing.v0, "a.txt"));
    ASSERT(io_array_has_exact(listing.v0, "sub"));
    ASSERT(!io_array_has_exact(listing.v0, "."));
    ASSERT(!io_array_has_exact(listing.v0, ".."));
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_list_dir_result_missing_directory_is_an_error(void) {
    GrayResult_array listing = gray_io_list_dir_result(arena, io_join2(io_tmp_path("grayc_ut_absent"), "nope"));
    ASSERT(listing.v1 != NULL);
    ASSERT_EQ(listing.v0.len, 0);
}

static void test_io_list_dir_result_empty_directory(void) {
    GrayString root = io_tmp_path("grayc_ut_emptydir");
    gray_io_remove_dir_all_result(arena, root);
    gray_io_make_dir_all_result(arena, root);
    GrayResult_array listing = gray_io_list_dir_result(arena, root);
    ASSERT(listing.v1 == NULL);
    ASSERT_EQ(listing.v0.len, 0);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_walk_lists_files_at_every_depth(void) {
    GrayString root = io_make_tree("grayc_ut_walk");
    GrayResult_array walked = gray_io_walk_result(arena, root);
    ASSERT(walked.v1 == NULL);
    ASSERT(io_array_has_suffix(walked.v0, "a.txt"));
    ASSERT(io_array_has_suffix(walked.v0, "b.txt"));
    ASSERT(io_array_has_suffix(walked.v0, "c.log"));
    ASSERT_EQ(gray_io_walk(arena, root).len, walked.v0.len);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_walk_result_missing_directory_is_an_error(void) {
    GrayResult_array walked = gray_io_walk_result(arena, io_join2(io_tmp_path("grayc_ut_absent"), "nope"));
    ASSERT(walked.v1 != NULL);
    ASSERT_EQ(walked.v0.len, 0);
}

static void test_io_glob_matches_by_pattern(void) {
    GrayString root = io_make_tree("grayc_ut_glob");
    GrayResult_array texts = gray_io_glob_result(arena, io_join2(root, "*.txt"));
    ASSERT(texts.v1 == NULL);
    ASSERT_EQ(texts.v0.len, 1);
    ASSERT(io_array_has_suffix(texts.v0, "a.txt"));
    GrayResult_array nested = gray_io_glob_result(arena, io_join2(root, "sub/*.txt"));
    ASSERT_EQ(nested.v0.len, 1);
    ASSERT(io_array_has_suffix(nested.v0, "b.txt"));
    GrayResult_array question = gray_io_glob_result(arena, io_join2(root, "?.txt"));
    ASSERT_EQ(question.v0.len, 1);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_glob_without_matches_is_an_empty_array(void) {
    GrayString root = io_make_tree("grayc_ut_globnone");
    GrayResult_array none = gray_io_glob_result(arena, io_join2(root, "*.zip"));
    ASSERT(none.v1 == NULL);
    ASSERT_EQ(none.v0.len, 0);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_copy_file_duplicates_contents(void) {
    GrayString source = io_tmp_path("grayc_ut_copy_src.txt");
    GrayString destination = io_tmp_path("grayc_ut_copy_dst.txt");
    gray_io_write_file_result(arena, source, gray_string_lit("payload"));
    GrayResult_bool copied = gray_io_copy_file_result(arena, source, destination);
    ASSERT(copied.v1 == NULL);
    ASSERT(copied.v0);
    ASSERT_GRAY_STR(gray_io_read_file_result(arena, destination).v0, "payload");
    ASSERT(gray_io_file_exists(source));
    gray_io_delete_file_result(arena, source);
    gray_io_delete_file_result(arena, destination);
}

static void test_io_copy_file_overwrites_an_existing_destination(void) {
    GrayString source = io_tmp_path("grayc_ut_copy_over_src.txt");
    GrayString destination = io_tmp_path("grayc_ut_copy_over_dst.txt");
    gray_io_write_file_result(arena, source, gray_string_lit("new"));
    gray_io_write_file_result(arena, destination, gray_string_lit("old contents that are longer"));
    ASSERT(gray_io_copy_file(source, destination));
    ASSERT_GRAY_STR(gray_io_read_file_result(arena, destination).v0, "new");
    gray_io_delete_file_result(arena, source);
    gray_io_delete_file_result(arena, destination);
}

static void test_io_copy_file_missing_source_is_an_error(void) {
    GrayResult_bool result = gray_io_copy_file_result(arena, io_tmp_path("grayc_ut_copy_missing.txt"), io_tmp_path("grayc_ut_copy_never.txt"));
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
    ASSERT(!gray_io_file_exists(io_tmp_path("grayc_ut_copy_never.txt")));
}

static void test_io_copy_file_larger_than_one_buffer(void) {
    GrayString source = io_tmp_path("grayc_ut_copy_big_src.bin");
    GrayString destination = io_tmp_path("grayc_ut_copy_big_dst.bin");
    GrayArray data = gray_array_new(arena, sizeof(uint8_t), 200000, GRAY_ELEM_U8);
    for (int i = 0; i < 200000; i++) { uint8_t byte = (uint8_t)(i * 31); GRAY_ARRAY_PUSH(arena, &data, &byte); }
    ASSERT(gray_io_write_bytes_result(arena, source, data).v0);
    ASSERT(gray_io_copy_file(source, destination));
    ASSERT_EQ(gray_io_file_size_result(arena, destination).v0, 200000);
    GrayArray back = gray_io_read_bytes_result(arena, destination).v0;
    ASSERT_EQ(memcmp(back.data, data.data, 200000), 0);
    gray_io_delete_file_result(arena, source);
    gray_io_delete_file_result(arena, destination);
}

static void test_io_move_file_relocates_the_file(void) {
    GrayString source = io_tmp_path("grayc_ut_move_src.txt");
    GrayString destination = io_tmp_path("grayc_ut_move_dst.txt");
    gray_io_write_file_result(arena, source, gray_string_lit("moved"));
    GrayResult_bool moved = gray_io_move_file_result(arena, source, destination);
    ASSERT(moved.v1 == NULL);
    ASSERT(moved.v0);
    ASSERT(!gray_io_file_exists(source));
    ASSERT_GRAY_STR(gray_io_read_file_result(arena, destination).v0, "moved");
    gray_io_delete_file_result(arena, destination);
}

static void test_io_move_file_missing_source_is_an_error(void) {
    GrayResult_bool result = gray_io_move_file_result(arena, io_tmp_path("grayc_ut_move_missing.txt"), io_tmp_path("grayc_ut_move_never.txt"));
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
}

static void test_io_append_bytes_extends_the_file(void) {
    GrayString path = io_tmp_path("grayc_ut_append_bytes.bin");
    uint8_t first_bytes[2] = {1, 2};
    uint8_t second_bytes[2] = {3, 4};
    GrayArray first = gray_array_from(arena, first_bytes, sizeof(uint8_t), 2, GRAY_ELEM_U8);
    GrayArray second = gray_array_from(arena, second_bytes, sizeof(uint8_t), 2, GRAY_ELEM_U8);
    gray_io_delete_file_result(arena, path);
    ASSERT(gray_io_append_bytes_result(arena, path, first).v0);
    ASSERT(gray_io_append_bytes(path, second));
    GrayArray back = gray_io_read_bytes_result(arena, path).v0;
    ASSERT_EQ(back.len, 4);
    ASSERT_EQ(GRAY_ARRAY_GET(back, uint8_t, 0), 1);
    ASSERT_EQ(GRAY_ARRAY_GET(back, uint8_t, 3), 4);
    gray_io_delete_file_result(arena, path);
}

static void test_io_append_bytes_result_to_a_directory_is_an_error(void) {
    GrayString root = io_tmp_path("grayc_ut_append_dir");
    gray_io_make_dir_all_result(arena, root);
    uint8_t byte = 9;
    GrayArray data = gray_array_from(arena, &byte, sizeof(uint8_t), 1, GRAY_ELEM_U8);
    GrayResult_bool result = gray_io_append_bytes_result(arena, root, data);
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_write_file_result_to_a_directory_is_an_error(void) {
    GrayString root = io_tmp_path("grayc_ut_write_dir");
    gray_io_make_dir_all_result(arena, root);
    GrayResult_bool result = gray_io_write_file_result(arena, root, gray_string_lit("x"));
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_write_file_result_into_a_missing_directory_is_an_error(void) {
    GrayString path = io_join2(io_tmp_path("grayc_ut_absent_dir"), "file.txt");
    GrayResult_bool result = gray_io_write_file_result(arena, path, gray_string_lit("x"));
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
}

static void test_io_write_bytes_result_into_a_missing_directory_is_an_error(void) {
    uint8_t byte = 1;
    GrayArray data = gray_array_from(arena, &byte, sizeof(uint8_t), 1, GRAY_ELEM_U8);
    GrayResult_bool result = gray_io_write_bytes_result(arena, io_join2(io_tmp_path("grayc_ut_absent_dir"), "f.bin"), data);
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
}

static void test_io_write_file_overwrites_instead_of_appending(void) {
    GrayString path = io_tmp_path("grayc_ut_overwrite.txt");
    gray_io_write_file_result(arena, path, gray_string_lit("long original text"));
    gray_io_write_file_result(arena, path, gray_string_lit("short"));
    ASSERT_GRAY_STR(gray_io_read_file_result(arena, path).v0, "short");
    gray_io_delete_file_result(arena, path);
}

static void test_io_read_file_result_on_a_directory_is_an_error(void) {
    GrayString root = io_tmp_path("grayc_ut_read_dir");
    gray_io_make_dir_all_result(arena, root);
    GrayResult_string result = gray_io_read_file_result(arena, root);
    ASSERT(result.v1 != NULL);
    ASSERT_EQ(result.v0.len, 0);
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_read_file_result_empty_file(void) {
    GrayString path = io_tmp_path("grayc_ut_empty_file.txt");
    gray_io_write_file_result(arena, path, gray_string_lit(""));
    GrayResult_string result = gray_io_read_file_result(arena, path);
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 0);
    ASSERT_EQ(gray_io_file_size_result(arena, path).v0, 0);
    gray_io_delete_file_result(arena, path);
}

static void test_io_read_file_result_larger_than_one_read(void) {
    GrayString path = io_tmp_path("grayc_ut_big_file.txt");
    char *block = gray_arena_alloc(arena, 300000);
    memset(block, 'x', 300000);
    gray_io_write_file_result(arena, path, gray_string_new(arena, block, 300000));
    GrayResult_string result = gray_io_read_file_result(arena, path);
    ASSERT(result.v1 == NULL);
    ASSERT_EQ(result.v0.len, 300000);
    ASSERT_EQ(result.v0.data[299999], 'x');
    gray_io_delete_file_result(arena, path);
}

static void test_io_file_size_result_missing_file_is_an_error(void) {
    GrayResult_i64 result = gray_io_file_size_result(arena, io_tmp_path("grayc_ut_no_size.txt"));
    ASSERT(result.v1 != NULL);
}

static void test_io_delete_file_result_missing_file_is_an_error(void) {
    GrayResult_bool result = gray_io_delete_file_result(arena, io_tmp_path("grayc_ut_no_delete.txt"));
    ASSERT(result.v1 != NULL);
    ASSERT(!result.v0);
}

static void test_io_read_lines_strips_crlf_and_handles_blank_lines(void) {
    GrayString path = io_tmp_path("grayc_ut_crlf.txt");
    gray_io_write_file_result(arena, path, gray_string_lit("a\r\n\r\nb\n"));
    GrayArray lines = gray_io_read_lines_result(arena, path, 0).v0;
    ASSERT_EQ(lines.len, 3);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(lines, GrayString, 0), "a");
    ASSERT_EQ(GRAY_ARRAY_GET(lines, GrayString, 1).len, 0);
    ASSERT_GRAY_STR(GRAY_ARRAY_GET(lines, GrayString, 2), "b");
    gray_io_delete_file_result(arena, path);
}

static void test_io_temp_file_and_temp_dir_exist_until_exit(void) {
    GrayResult_string file = gray_io_temp_file_result(arena);
    ASSERT(file.v1 == NULL);
    ASSERT(file.v0.len > 0);
    ASSERT(gray_io_is_file(file.v0));
    GrayResult_string directory = gray_io_temp_dir_result(arena);
    ASSERT(directory.v1 == NULL);
    ASSERT(gray_io_is_directory(directory.v0));
    GrayResult_string other = gray_io_temp_file_result(arena);
    ASSERT(!gray_string_eq(file.v0, other.v0));
}

static void test_io_temp_file_is_writable(void) {
    GrayString path = gray_io_temp_file_result(arena).v0;
    ASSERT(gray_io_write_file_result(arena, path, gray_string_lit("scratch")).v0);
    ASSERT_GRAY_STR(gray_io_read_file_result(arena, path).v0, "scratch");
}

static void test_io_is_file_and_is_directory_distinguish_kinds(void) {
    GrayString root = io_make_tree("grayc_ut_kinds");
    ASSERT(gray_io_is_directory(root));
    ASSERT(!gray_io_is_file(root));
    ASSERT(gray_io_is_file(io_join2(root, "a.txt")));
    ASSERT(!gray_io_is_directory(io_join2(root, "a.txt")));
    ASSERT(!gray_io_file_exists(io_join2(root, "nope")));
    ASSERT(!gray_io_is_file(io_join2(root, "nope")));
    gray_io_remove_dir_all_result(arena, root);
}

static void test_io_stdin_reads_everything_and_reports_eof(void) {
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    fputs("line one\nline two\n", tmp);
    rewind(tmp);
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    ASSERT(!gray_io_stdin_at_eof());
    GrayString everything = gray_io_read_stdin_all(arena);
    ASSERT(gray_io_stdin_at_eof());
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
    fclose(tmp);
    ASSERT_GRAY_STR(everything, "line one\nline two\n");
}

static void test_io_stdin_bytes_are_packed(void) {
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    fwrite("\x01\x02\xFF", 1, 3, tmp);
    rewind(tmp);
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    GrayArray bytes = gray_io_read_stdin_bytes(arena);
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
    fclose(tmp);
    ASSERT_EQ(bytes.len, 3);
    ASSERT_EQ(bytes.elem_size, 1);
    ASSERT_EQ(GRAY_ARRAY_GET(bytes, uint8_t, 2), 0xFF);
}

static void test_io_stdin_empty_input_is_at_eof(void) {
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    int saved_descriptor = dup(fileno(stdin));
    dup2(fileno(tmp), fileno(stdin));
    bool at_eof = gray_io_stdin_at_eof();
    GrayString everything = gray_io_read_stdin_all(arena);
    dup2(saved_descriptor, fileno(stdin));
    close(saved_descriptor);
    clearerr(stdin);
    fclose(tmp);
    ASSERT(at_eof);
    ASSERT_EQ(everything.len, 0);
}

/* ===== threads: lifecycle ===== */

static int64_t thread_test_flag;
static int64_t thread_test_gate;
static int64_t thread_test_received;
static int64_t thread_test_id;

static void thread_sets_flag(void) { gray_atomic_store(&thread_test_flag, 1); }
static void thread_records_argument(int64_t argument) { gray_atomic_store(&thread_test_received, argument); }
static void thread_records_id(void) { gray_atomic_store(&thread_test_id, gray_threads_id()); }

static void thread_waits_for_gate(void) {
    while (gray_atomic_load(&thread_test_gate) == 0) gray_threads_yield();
}

static void test_threads_spawn_runs_the_function_and_join_waits_for_it(void) {
    gray_atomic_store(&thread_test_flag, 0);
    GrayThread thread = gray_threads_spawn(thread_sets_flag);
    gray_threads_join(&thread);
    ASSERT_EQ(gray_atomic_load(&thread_test_flag), 1);
}

static void test_threads_spawn_arg_passes_the_argument(void) {
    gray_atomic_store(&thread_test_received, 0);
    GrayThread thread = gray_threads_spawn_arg(thread_records_argument, 424242);
    gray_threads_join(&thread);
    ASSERT_EQ(gray_atomic_load(&thread_test_received), 424242);
}

static void test_threads_spawn_arg_passes_negative_and_extreme_arguments(void) {
    GrayThread negative = gray_threads_spawn_arg(thread_records_argument, -7);
    gray_threads_join(&negative);
    ASSERT_EQ(gray_atomic_load(&thread_test_received), -7);
    GrayThread extreme = gray_threads_spawn_arg(thread_records_argument, INT64_MAX);
    gray_threads_join(&extreme);
    ASSERT_EQ(gray_atomic_load(&thread_test_received), INT64_MAX);
}

static void test_threads_is_alive_until_the_body_returns(void) {
    gray_atomic_store(&thread_test_gate, 0);
    GrayThread thread = gray_threads_spawn(thread_waits_for_gate);
    ASSERT(gray_threads_is_alive(thread));
    gray_atomic_store(&thread_test_gate, 1);
    while (gray_threads_is_alive(thread)) gray_threads_yield();
    ASSERT(!gray_threads_is_alive(thread));
    gray_threads_join(&thread);
}

static void test_threads_thread_count_tracks_live_threads(void) {
    int64_t before = gray_threads_thread_count();
    gray_atomic_store(&thread_test_gate, 0);
    GrayThread first = gray_threads_spawn(thread_waits_for_gate);
    GrayThread second = gray_threads_spawn(thread_waits_for_gate);
    ASSERT_EQ(gray_threads_thread_count(), before + 2);
    gray_atomic_store(&thread_test_gate, 1);
    gray_threads_join(&first);
    gray_threads_join(&second);
    ASSERT_EQ(gray_threads_thread_count(), before);
}

static void test_threads_id_differs_between_threads(void) {
    gray_atomic_store(&thread_test_id, 0);
    int64_t main_id = gray_threads_id();
    GrayThread thread = gray_threads_spawn(thread_records_id);
    gray_threads_join(&thread);
    int64_t other_id = gray_atomic_load(&thread_test_id);
    ASSERT_NE(other_id, 0);
    ASSERT_NE(other_id, main_id);
    ASSERT_EQ(gray_threads_id(), main_id);
}

static void test_threads_detach_lets_the_thread_finish_on_its_own(void) {
    gray_atomic_store(&thread_test_flag, 0);
    GrayThread thread = gray_threads_spawn(thread_sets_flag);
    gray_threads_detach(&thread);
    for (int attempts = 0; attempts < 2000 && gray_atomic_load(&thread_test_flag) == 0; attempts++) sleep_microseconds(1000);
    ASSERT_EQ(gray_atomic_load(&thread_test_flag), 1);
}

static void test_threads_detach_of_a_running_thread_then_release(void) {
    int64_t before = gray_threads_thread_count();
    gray_atomic_store(&thread_test_gate, 0);
    GrayThread thread = gray_threads_spawn(thread_waits_for_gate);
    gray_threads_detach(&thread);
    ASSERT_EQ(gray_threads_thread_count(), before + 1);
    gray_atomic_store(&thread_test_gate, 1);
    for (int attempts = 0; attempts < 2000 && gray_threads_thread_count() != before; attempts++) sleep_microseconds(1000);
    ASSERT_EQ(gray_threads_thread_count(), before);
}

static void test_threads_yield_returns(void) {
    gray_threads_yield();
}

static void test_threads_join_and_detach_clear_the_handle(void) {
    GrayThread joined = gray_threads_spawn(thread_sets_flag);
    gray_threads_join(&joined);
    ASSERT(joined._internal == NULL);
    GrayThread detached = gray_threads_spawn(thread_sets_flag);
    gray_threads_detach(&detached);
    ASSERT(detached._internal == NULL);
}

static void test_threads_many_threads_all_run(void) {
    GrayThread threads[16];
    gray_atomic_store(&thread_test_received, 0);
    for (int i = 0; i < 16; i++) threads[i] = gray_threads_spawn(thread_sets_flag);
    for (int i = 0; i < 16; i++) gray_threads_join(&threads[i]);
    ASSERT_EQ(gray_atomic_load(&thread_test_flag), 1);
}

/* ===== math: logarithms ===== */

static void test_math_log2_of_powers_of_two(void) {
    ASSERT_FLOAT_EQ(gray_math_log2(1.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_log2(8.0), 3.0);
    ASSERT_FLOAT_EQ(gray_math_log2(0.5), -1.0);
}

static void test_math_log10_of_powers_of_ten(void) {
    ASSERT_FLOAT_EQ(gray_math_log10(1.0), 0.0);
    ASSERT_FLOAT_EQ(gray_math_log10(1000.0), 3.0);
    ASSERT_FLOAT_EQ(gray_math_log10(0.01), -2.0);
}

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
    RUN_TEST(test_chars_classification);
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
    RUN_TEST(test_maps_contains_key);
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
    RUN_TEST(test_fmt_i64_to_binary);
    RUN_TEST(test_fmt_f64_to_fixed);

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

    printf("--- random ---\n");
    RUN_TEST(test_random_seed_makes_i64_range_reproducible);
    RUN_TEST(test_random_seed_makes_f64_unit_reproducible);
    RUN_TEST(test_random_seed_makes_bytes_reproducible);
    RUN_TEST(test_random_seed_makes_rand_string_reproducible);
    RUN_TEST(test_random_seed_makes_shuffle_reproducible);
    RUN_TEST(test_random_f64_unit_stays_in_unit_interval);
    RUN_TEST(test_random_f64_range_stays_in_bounds);
    RUN_TEST(test_random_i64_range_stays_in_bounds);
    RUN_TEST(test_random_i64_range_width_one_returns_minimum);
    RUN_TEST(test_random_i64_range_reaches_both_ends);
    RUN_TEST(test_random_i64_range_spans_full_i64_width);
    RUN_TEST(test_random_choice_index_stays_below_length);
    RUN_TEST(test_random_choice_index_length_one);
    RUN_TEST(test_random_bool_produces_both_values);
    RUN_TEST(test_random_u8_covers_more_than_one_value);
    RUN_TEST(test_random_bytes_length_and_element_kind);
    RUN_TEST(test_random_bytes_zero_is_empty);
    RUN_TEST(test_random_roll_dice_two_six_sided_in_range);
    RUN_TEST(test_random_roll_dice_one_sided_die_is_count);
    RUN_TEST(test_random_roll_dice_single_die_reaches_every_face);
    RUN_TEST(test_random_char_is_printable_ascii);
    RUN_TEST(test_random_char_range_stays_in_bounds);
    RUN_TEST(test_random_char_range_never_yields_surrogate);
    RUN_TEST(test_random_string_zero_length_is_empty);
    RUN_TEST(test_random_string_draws_only_from_alphabet);
    RUN_TEST(test_random_string_single_character_alphabet);
    RUN_TEST(test_random_string_length_counts_characters_not_bytes);
    RUN_TEST(test_random_string_multibyte_alphabet_stays_valid);
    RUN_TEST(test_random_shuffle_keeps_elements);
    RUN_TEST(test_random_shuffle_leaves_original_untouched);
    RUN_TEST(test_random_shuffle_empty_and_single);
    RUN_TEST(test_random_shuffle_wide_elements_stay_intact);
    RUN_TEST(test_random_sample_returns_unique_elements);
    RUN_TEST(test_random_sample_zero_is_empty);
    RUN_TEST(test_random_sample_full_length_is_permutation);

    printf("--- mem ---\n");
    RUN_TEST(test_mem_arena_starts_empty);
    RUN_TEST(test_mem_arena_non_positive_size_uses_default);
    RUN_TEST(test_mem_usage_grows_with_allocation);
    RUN_TEST(test_mem_reset_returns_usage_to_zero);
    RUN_TEST(test_mem_arena_is_reusable_after_reset);
    RUN_TEST(test_mem_arena_allocations_do_not_count_toward_total);
    RUN_TEST(test_mem_copy_copies_exactly_n_bytes);
    RUN_TEST(test_mem_copy_zero_bytes_is_noop);
    RUN_TEST(test_mem_zero_clears_exactly_n_bytes);
    RUN_TEST(test_mem_set_fills_exactly_n_bytes);

    printf("--- runtime ---\n");
    RUN_TEST(test_runtime_arena_usage_tracks_default_arena);
    RUN_TEST(test_runtime_heap_usage_tracks_heap_arena);
    RUN_TEST(test_runtime_total_usage_is_arena_plus_heap);
    RUN_TEST(test_runtime_peak_usage_is_at_least_total_usage);
    RUN_TEST(test_runtime_alloc_count_increases_per_allocation);
    RUN_TEST(test_runtime_arena_blocks_counts_chained_blocks);
    RUN_TEST(test_runtime_heap_blocks_counts_chained_blocks);
    RUN_TEST(test_runtime_arena_limit_reports_maximum_bytes);
    RUN_TEST(test_runtime_call_depth_follows_enter_and_exit);
    RUN_TEST(test_runtime_current_and_caller_name);
    RUN_TEST(test_runtime_caller_name_is_empty_at_top_level);
    RUN_TEST(test_runtime_current_name_is_empty_outside_any_function);

    printf("--- builtins conversions and utf8 ---\n");
    RUN_TEST(test_builtin_to_string_i64);
    RUN_TEST(test_builtin_to_string_u64);
    RUN_TEST(test_builtin_to_string_bool);
    RUN_TEST(test_builtin_to_string_floating_point_shortest);
    RUN_TEST(test_builtin_to_string_floating_point_f32_is_shortest_for_f32);
    RUN_TEST(test_builtin_format_floating_point_matches_to_string);
    RUN_TEST(test_builtin_string_to_i64);
    RUN_TEST(test_builtin_string_to_i64_ignores_trailing_spaces);
    RUN_TEST(test_builtin_string_to_u64);
    RUN_TEST(test_builtin_string_to_i128_round_trips_negative);
    RUN_TEST(test_builtin_string_to_u128_above_u64);
    RUN_TEST(test_builtin_string_to_i256_negative_sign_extends);
    RUN_TEST(test_builtin_string_to_u256_above_u128);
    RUN_TEST(test_builtin_char_count_counts_codepoints);
    RUN_TEST(test_builtin_char_to_utf8_encodes_each_width);
    RUN_TEST(test_builtin_char_to_utf8_invalid_scalar_is_replacement);
    RUN_TEST(test_builtin_utf8_next_reports_width);
    RUN_TEST(test_builtin_utf8_next_stray_continuation_byte_is_replacement);
    RUN_TEST(test_builtin_array_to_string_integers);
    RUN_TEST(test_builtin_array_to_string_empty);

    printf("--- atomic ---\n");
    RUN_TEST(test_atomic_mod_load_store);
    RUN_TEST(test_atomic_mod_add_sub_return_previous_value);
    RUN_TEST(test_atomic_mod_exchange_returns_previous_value);
    RUN_TEST(test_atomic_mod_cas_reports_whether_swap_happened);
    RUN_TEST(test_atomic_mod_bitwise_return_previous_value);
    RUN_TEST(test_atomic_mod_spinlock_lifecycle);
    RUN_TEST(test_atomic_mod_spin_lock_acquires_new_lock);
    RUN_TEST(test_atomic_mod_spinlock_copy_shares_the_lock);
    RUN_TEST(test_atomic_mod_spinlocks_are_independent);
    RUN_TEST(test_atomic_mod_fence_returns);

    printf("--- channels ---\n");
    RUN_TEST(test_channels_receives_in_send_order);
    RUN_TEST(test_channels_len_counts_buffered_values);
    RUN_TEST(test_channels_try_send_fails_when_full);
    RUN_TEST(test_channels_try_send_succeeds_again_after_a_receive);
    RUN_TEST(test_channels_try_receive_on_empty_reports_failure);
    RUN_TEST(test_channels_try_receive_returns_value_and_success);
    RUN_TEST(test_channels_buffer_wraps_around);
    RUN_TEST(test_channels_holds_extreme_values);
    RUN_TEST(test_channels_channels_are_independent);
    RUN_TEST(test_channels_close_keeps_buffered_values_receivable);
    RUN_TEST(test_channels_receive_on_closed_empty_channel_returns_zero);
    RUN_TEST(test_channels_try_send_after_close_fails);
    RUN_TEST(test_channels_blocking_send_and_receive_across_threads);
    RUN_TEST(test_channels_close_wakes_a_blocked_receiver);

    printf("--- chars (remaining) ---\n");
    RUN_TEST(test_chars_is_ascii_boundaries);
    RUN_TEST(test_chars_is_control_ranges);
    RUN_TEST(test_chars_is_printable_ranges);
    RUN_TEST(test_chars_is_punct_ascii_punctuation_only);
    RUN_TEST(test_chars_is_hex_digit);
    RUN_TEST(test_chars_is_word_char);
    RUN_TEST(test_chars_is_vowel);
    RUN_TEST(test_chars_is_consonant);
    RUN_TEST(test_chars_escape_two_character_escapes);
    RUN_TEST(test_chars_escape_printable_ascii_is_unchanged);
    RUN_TEST(test_chars_escape_other_control_characters_are_hex);
    RUN_TEST(test_chars_escape_del_is_hex);
    RUN_TEST(test_chars_escape_non_ascii_is_unicode_escape);
    RUN_TEST(test_chars_mirror_swaps_each_bracket_pair);
    RUN_TEST(test_chars_mirror_leaves_other_codepoints_alone);
    RUN_TEST(test_chars_width_ascii_is_one);
    RUN_TEST(test_chars_width_control_is_minus_one);
    RUN_TEST(test_chars_width_wide_codepoints_are_two);
    RUN_TEST(test_chars_width_zero_width_codepoints_are_zero);
    RUN_TEST(test_chars_width_ambiguous_width_is_one);
    RUN_TEST(test_chars_string_width_sums_codepoints);
    RUN_TEST(test_chars_string_width_counts_controls_as_zero);
    RUN_TEST(test_chars_string_width_counts_combining_marks_as_zero);
    RUN_TEST(test_chars_string_width_sums_zwj_sequence_parts);

    printf("--- time ---\n");
    RUN_TEST(test_time_components_of_known_timestamp);
    RUN_TEST(test_time_components_of_epoch);
    RUN_TEST(test_time_components_before_epoch);
    RUN_TEST(test_time_weekday_name_follows_weekday_numbering);
    RUN_TEST(test_time_month_name);
    RUN_TEST(test_time_day_of_year);
    RUN_TEST(test_time_is_leap_year);
    RUN_TEST(test_time_days_in_month_every_month);
    RUN_TEST(test_time_days_in_month_leap_february);
    RUN_TEST(test_time_format_directives);
    RUN_TEST(test_time_to_iso_date_and_clock);
    RUN_TEST(test_time_from_parts_builds_timestamp);
    RUN_TEST(test_time_from_parts_epoch_and_leap_day);
    RUN_TEST(test_time_from_parts_rejects_impossible_dates);
    RUN_TEST(test_time_from_parts_inverts_the_components);
    RUN_TEST(test_time_parse_with_layout);
    RUN_TEST(test_time_parse_rejects_bad_input);
    RUN_TEST(test_time_parse_round_trips_through_format);
    RUN_TEST(test_time_parse_duration_units);
    RUN_TEST(test_time_parse_duration_combined);
    RUN_TEST(test_time_parse_duration_rejects_bad_input);
    RUN_TEST(test_time_diff_is_end_minus_start);
    RUN_TEST(test_time_since_tracks_now);
    RUN_TEST(test_time_now_units_agree);
    RUN_TEST(test_time_tick_is_monotonic_and_elapsed_ms_is_non_negative);
    RUN_TEST(test_time_add_shifts_by_unit);
    RUN_TEST(test_time_add_months_clamps_to_last_day);
    RUN_TEST(test_time_add_months_leap_february);
    RUN_TEST(test_time_add_months_crosses_years);
    RUN_TEST(test_time_add_months_zero_keeps_timestamp);
    RUN_TEST(test_time_start_and_end_of_day);
    RUN_TEST(test_time_start_of_day_before_epoch);
    RUN_TEST(test_time_start_and_end_of_month);
    RUN_TEST(test_time_end_of_month_leap_february);
    RUN_TEST(test_time_end_of_month_december_rolls_the_year_correctly);
    RUN_TEST(test_time_humanize_zero_and_units);
    RUN_TEST(test_time_humanize_uses_the_largest_whole_unit);
    RUN_TEST(test_time_humanize_negative_is_future);
    RUN_TEST(test_time_humanize_months_and_years);
    RUN_TEST(test_time_format_duration);
    RUN_TEST(test_time_format_duration_caps_at_hours);
    RUN_TEST(test_time_format_duration_negative_has_leading_minus);

    printf("--- encoding (remaining) ---\n");
    RUN_TEST(test_encoding_base64_url_encode_is_unpadded_and_url_safe);
    RUN_TEST(test_encoding_base64_url_encode_uses_dash_and_underscore);
    RUN_TEST(test_encoding_base64_url_decode_with_and_without_padding);
    RUN_TEST(test_encoding_base64_url_decode_rejects_malformed_input);
    RUN_TEST(test_encoding_base64_url_round_trips_every_length);
    RUN_TEST(test_encoding_base64_decode_rejects_bad_characters_and_lengths);
    RUN_TEST(test_encoding_hex_decode_rejects_odd_length_and_bad_digits);
    RUN_TEST(test_encoding_html_escape_replaces_the_five_characters);
    RUN_TEST(test_encoding_html_unescape_named_entities);
    RUN_TEST(test_encoding_html_unescape_numeric_references);
    RUN_TEST(test_encoding_html_unescape_leaves_unknown_entities_untouched);
    RUN_TEST(test_encoding_html_escape_unescape_round_trip);
    RUN_TEST(test_encoding_shell_escape_safe_text_is_unchanged);
    RUN_TEST(test_encoding_shell_escape_quotes_unsafe_text);
    RUN_TEST(test_encoding_shell_escape_embedded_single_quote);
    RUN_TEST(test_encoding_shell_escape_empty_string_is_quoted);
    RUN_TEST(test_encoding_from_string_and_to_string_round_trip);
    RUN_TEST(test_encoding_from_string_empty);
    RUN_TEST(test_encoding_to_hex_and_from_hex);
    RUN_TEST(test_encoding_from_hex_accepts_uppercase_and_rejects_bad_input);
    RUN_TEST(test_encoding_to_base64_and_from_base64);
    RUN_TEST(test_encoding_to_hex_and_to_base64_of_empty_array);

    printf("--- binary (wide) ---\n");
    RUN_TEST(test_binary_decode_single_bytes);
    RUN_TEST(test_binary_i128_negative_one_is_all_ones_in_both_orders);
    RUN_TEST(test_binary_i128_high_and_low_limbs_swap_between_orders);
    RUN_TEST(test_binary_u128_round_trip_in_both_orders);
    RUN_TEST(test_binary_i256_round_trip_in_both_orders);
    RUN_TEST(test_binary_u256_round_trip_in_both_orders);

    printf("--- fmt (fields and number formatting) ---\n");
    RUN_TEST(test_fmt_string_field_pads_to_width);
    RUN_TEST(test_fmt_string_field_wider_than_width_is_unchanged);
    RUN_TEST(test_fmt_string_field_precision_truncates);
    RUN_TEST(test_fmt_string_field_precision_then_width);
    RUN_TEST(test_fmt_string_field_empty_string);
    RUN_TEST(test_fmt_char_field_pads_to_width);
    RUN_TEST(test_fmt_char_field_multibyte_character_counts_as_one_column);
    RUN_TEST(test_fmt_format_number_groups_thousands);
    RUN_TEST(test_fmt_format_number_negative);
    RUN_TEST(test_fmt_format_number_extremes);
    RUN_TEST(test_fmt_format_bytes_below_one_kibibyte_is_whole_bytes);
    RUN_TEST(test_fmt_format_bytes_uses_binary_units_with_one_decimal);
    RUN_TEST(test_fmt_format_bytes_largest_value_stays_in_pebibytes);
    RUN_TEST(test_fmt_format_bytes_negative);

    printf("--- strings (case words, split, builder) ---\n");
    RUN_TEST(test_strings_to_title);
    RUN_TEST(test_strings_to_camel_case);
    RUN_TEST(test_strings_to_pascal_case);
    RUN_TEST(test_strings_capitalize);
    RUN_TEST(test_strings_contains_any);
    RUN_TEST(test_strings_equal_fold);
    RUN_TEST(test_strings_compare);
    RUN_TEST(test_strings_truncate_leaves_short_strings_alone);
    RUN_TEST(test_strings_truncate_total_length_is_exactly_max);
    RUN_TEST(test_strings_truncate_with_empty_ellipsis);
    RUN_TEST(test_strings_truncate_max_equal_to_ellipsis_length);
    RUN_TEST(test_strings_split_whitespace);
    RUN_TEST(test_strings_split_whitespace_blank_input_is_empty);
    RUN_TEST(test_strings_split_n_keeps_the_remainder_in_the_last_piece);
    RUN_TEST(test_strings_split_n_with_one_piece_is_the_whole_string);
    RUN_TEST(test_strings_split_n_non_positive_count_is_empty);
    RUN_TEST(test_strings_split_n_count_larger_than_pieces);
    RUN_TEST(test_strings_split_n_multi_character_separator);
    RUN_TEST(test_strings_builder_appends_in_order);
    RUN_TEST(test_strings_builder_append_char_encodes_utf8);
    RUN_TEST(test_strings_builder_append_bytes);
    RUN_TEST(test_strings_builder_append_i64);
    RUN_TEST(test_strings_builder_append_i64_extremes);
    RUN_TEST(test_strings_builder_append_line_adds_newline);
    RUN_TEST(test_strings_builder_clear_resets_length_and_stays_usable);
    RUN_TEST(test_strings_builder_build_leaves_builder_usable);
    RUN_TEST(test_strings_builder_grows_past_initial_capacity);
    RUN_TEST(test_strings_builder_reserve_does_not_change_contents);
    RUN_TEST(test_strings_builder_empty_builds_empty_string);

    printf("--- builtins (print, input, system, composite to_string) ---\n");
    RUN_TEST(test_builtin_println_str);
    RUN_TEST(test_builtin_print_str);
    RUN_TEST(test_builtin_println_i64);
    RUN_TEST(test_builtin_print_i64);
    RUN_TEST(test_builtin_println_u64);
    RUN_TEST(test_builtin_print_u64);
    RUN_TEST(test_builtin_println_f64);
    RUN_TEST(test_builtin_print_f64);
    RUN_TEST(test_builtin_println_f32);
    RUN_TEST(test_builtin_println_bool_true);
    RUN_TEST(test_builtin_print_bool_false);
    RUN_TEST(test_builtin_println_char_ascii);
    RUN_TEST(test_builtin_print_char_multibyte);
    RUN_TEST(test_builtin_println_addr);
    RUN_TEST(test_builtin_print_addr);
    RUN_TEST(test_builtin_eprintln_str);
    RUN_TEST(test_builtin_eprint_str);
    RUN_TEST(test_builtin_eprintln_i64);
    RUN_TEST(test_builtin_eprint_i64);
    RUN_TEST(test_builtin_eprintln_u64);
    RUN_TEST(test_builtin_eprint_u64);
    RUN_TEST(test_builtin_eprintln_f64);
    RUN_TEST(test_builtin_eprint_f64);
    RUN_TEST(test_builtin_eprintln_bool);
    RUN_TEST(test_builtin_eprint_bool);
    RUN_TEST(test_builtin_eprintln_char);
    RUN_TEST(test_builtin_eprint_char);
    RUN_TEST(test_builtin_eprintln_addr);
    RUN_TEST(test_builtin_eprint_addr);
    RUN_TEST(test_builtin_print_keeps_embedded_nul);
    RUN_TEST(test_builtin_flush_writes_pending_output);
    RUN_TEST(test_builtin_input_strips_carriage_return_and_reads_one_line);
    RUN_TEST(test_builtin_input_without_terminator_returns_the_rest);
    RUN_TEST(test_builtin_input_longer_than_the_initial_buffer);
    RUN_TEST(test_builtin_sleep_ms_and_ns_wait_at_least_the_requested_time);
    RUN_TEST(test_builtin_system_returns_the_exit_status);
    RUN_TEST(test_builtin_system_runs_the_command);
    RUN_TEST(test_builtin_assert_true_is_a_no_op);
    RUN_TEST(test_builtin_map_to_string_string_keys_and_integer_values);
    RUN_TEST(test_builtin_map_to_string_empty_map);
    RUN_TEST(test_builtin_map_to_string_string_values_are_quoted);
    RUN_TEST(test_builtin_map_to_string_bool_and_float_values);
    RUN_TEST(test_builtin_map_to_string_skips_removed_entries);
    RUN_TEST(test_builtin_array_to_string_strings_bools_floats_and_chars);
    RUN_TEST(test_builtin_array_to_string_unsigned_and_wide_integers);
    RUN_TEST(test_builtin_string_to_wide_integers_reject_nothing_valid);

    printf("--- math and arrays (remaining) ---\n");
    RUN_TEST(test_math_modf_splits_sign_carrying_parts);
    RUN_TEST(test_math_is_power_of_two);
    RUN_TEST(test_math_next_power_of_two);
    RUN_TEST(test_math_factorial_values);
    RUN_TEST(test_math_gcd_and_lcm);
    RUN_TEST(test_arrays_swap_exchanges_in_place);
    RUN_TEST(test_arrays_swap_strings);
    RUN_TEST(test_arrays_rotate_left_and_right);
    RUN_TEST(test_arrays_rotate_by_zero_or_a_full_turn_is_a_copy);
    RUN_TEST(test_arrays_rotate_empty);
    RUN_TEST(test_arrays_counts_tallies_in_first_seen_order);
    RUN_TEST(test_arrays_counts_strings);
    RUN_TEST(test_arrays_counts_empty);
    RUN_TEST(test_arrays_binary_search_finds_and_misses);
    RUN_TEST(test_arrays_binary_search_empty_and_strings);
    RUN_TEST(test_arrays_is_sorted);
    RUN_TEST(test_arrays_is_sorted_strings);
    RUN_TEST(test_arrays_average);
    RUN_TEST(test_arrays_sort_descending_and_strings);
    RUN_TEST(test_arrays_sort_floats_and_unsigned);

    printf("--- json (grammar, escapes, field helpers) ---\n");
    RUN_TEST(test_json_is_valid_accepts_every_value_kind);
    RUN_TEST(test_json_is_valid_rejects_malformed_documents);
    RUN_TEST(test_json_is_valid_rejects_raw_control_characters_in_strings);
    RUN_TEST(test_json_is_valid_rejects_excessive_nesting);
    RUN_TEST(test_json_decode_unescapes_simple_escapes);
    RUN_TEST(test_json_decode_unicode_escapes_become_utf8);
    RUN_TEST(test_json_decode_surrogate_pair_becomes_one_codepoint);
    RUN_TEST(test_json_decode_lone_surrogates_become_replacement_character);
    RUN_TEST(test_json_decode_values_of_every_kind_become_text);
    RUN_TEST(test_json_decode_nested_text_keeps_brackets_inside_strings_intact);
    RUN_TEST(test_json_decode_empty_object);
    RUN_TEST(test_json_decode_result_rejects_non_objects);
    RUN_TEST(test_json_unquote_decodes_a_quoted_element);
    RUN_TEST(test_json_split_array_separates_top_level_elements);
    RUN_TEST(test_json_split_array_empty_and_non_array);
    RUN_TEST(test_json_split_array_string_with_escaped_quote);
    RUN_TEST(test_json_encode_array_wide_integer);
    RUN_TEST(test_json_number_text_for_each_kind);
    RUN_TEST(test_json_enum_from_number_accepts_known_variants);
    RUN_TEST(test_json_enum_from_str_accepts_known_variants);
    RUN_TEST(test_json_is_null);
    RUN_TEST(test_json_field_as_bool);
    RUN_TEST(test_json_decode_fields_records_which_values_were_quoted);
    RUN_TEST(test_json_require_document_accepts_the_expected_opening);
    RUN_TEST(test_json_require_array_field_accepts_unquoted_array);
    RUN_TEST(test_json_element_as_string_unquotes);
    RUN_TEST(test_json_encode_string_escapes_special_characters);
    RUN_TEST(test_json_encode_then_decode_round_trips_a_string);

    printf("--- regex (groups, count, escape, results) ---\n");
    RUN_TEST(test_regex_find_groups_whole_match_then_captures);
    RUN_TEST(test_regex_find_groups_no_match_is_empty);
    RUN_TEST(test_regex_find_groups_non_participating_group_is_empty_string);
    RUN_TEST(test_regex_find_groups_pattern_without_groups_has_only_the_whole_match);
    RUN_TEST(test_regex_find_all_groups_every_match);
    RUN_TEST(test_regex_find_all_groups_no_match_is_empty);
    RUN_TEST(test_regex_count_non_overlapping_matches);
    RUN_TEST(test_regex_count_zero_when_nothing_matches);
    RUN_TEST(test_regex_count_invalid_pattern_is_an_error);
    RUN_TEST(test_regex_escape_metacharacters);
    RUN_TEST(test_regex_escaped_text_matches_itself_literally);
    RUN_TEST(test_regex_find_result_no_match_is_empty_string_without_error);
    RUN_TEST(test_regex_find_result_invalid_pattern);
    RUN_TEST(test_regex_find_all_result_matches_and_errors);
    RUN_TEST(test_regex_find_groups_result_matches_and_errors);
    RUN_TEST(test_regex_find_all_groups_result_matches_and_errors);
    RUN_TEST(test_regex_replace_result_replaces_every_match_with_literal_text);
    RUN_TEST(test_regex_replace_result_no_match_returns_original_text);
    RUN_TEST(test_regex_replace_result_with_empty_replacement_deletes_matches);
    RUN_TEST(test_regex_replace_result_invalid_pattern_returns_original_text);
    RUN_TEST(test_regex_replace_result_handles_utf8_text);
    RUN_TEST(test_regex_split_result_on_pattern);
    RUN_TEST(test_regex_split_result_no_match_is_the_whole_text);
    RUN_TEST(test_regex_split_result_invalid_pattern_is_an_error);
    RUN_TEST(test_regex_perl_shorthand_classes_are_invalid_patterns);
    RUN_TEST(test_regex_find_all_utf8_characters);

    printf("--- net (results, receive_line, resolve) ---\n");
    RUN_TEST(test_net_listen_result_dial_result_and_accept_result);
    RUN_TEST(test_net_dial_result_refused_connection_is_an_error);
    RUN_TEST(test_net_dial_result_unresolvable_host_is_an_error);
    RUN_TEST(test_net_listen_result_port_in_use_is_an_error);
    RUN_TEST(test_net_send_result_reports_bytes_written);
    RUN_TEST(test_net_send_and_recv_result_round_trip);
    RUN_TEST(test_net_recv_result_respects_max_bytes);
    RUN_TEST(test_net_recv_result_after_peer_close_is_a_closed_error);
    RUN_TEST(test_net_recv_result_timeout_is_a_timeout_error);
    RUN_TEST(test_net_receive_line_strips_lf_and_crlf);
    RUN_TEST(test_net_receive_line_keeps_bytes_after_the_newline_for_receive);
    RUN_TEST(test_net_receive_line_empty_line);
    RUN_TEST(test_net_receive_line_waits_for_a_split_line);
    RUN_TEST(test_net_receive_line_longer_than_max_bytes_is_out_of_range);
    RUN_TEST(test_net_receive_line_after_close_with_no_bytes_is_closed);
    RUN_TEST(test_net_receive_line_after_close_returns_the_partial_line);
    RUN_TEST(test_net_receive_line_lines_arriving_in_one_segment_are_all_kept);
    RUN_TEST(test_net_resolve_localhost_and_ip_literal);
    RUN_TEST(test_net_resolve_result_unknown_host_is_an_error);

    printf("--- io (directories, copy, glob, temp, stdin) ---\n");
    RUN_TEST(test_io_make_dir_all_creates_every_parent);
    RUN_TEST(test_io_make_dir_result_errors_when_parent_is_missing);
    RUN_TEST(test_io_remove_dir_result_refuses_a_non_empty_directory);
    RUN_TEST(test_io_remove_dir_all_removes_nested_contents);
    RUN_TEST(test_io_list_dir_result_names_entries_without_dot_entries);
    RUN_TEST(test_io_list_dir_result_missing_directory_is_an_error);
    RUN_TEST(test_io_list_dir_result_empty_directory);
    RUN_TEST(test_io_walk_lists_files_at_every_depth);
    RUN_TEST(test_io_walk_result_missing_directory_is_an_error);
    RUN_TEST(test_io_glob_matches_by_pattern);
    RUN_TEST(test_io_glob_without_matches_is_an_empty_array);
    RUN_TEST(test_io_copy_file_duplicates_contents);
    RUN_TEST(test_io_copy_file_overwrites_an_existing_destination);
    RUN_TEST(test_io_copy_file_missing_source_is_an_error);
    RUN_TEST(test_io_copy_file_larger_than_one_buffer);
    RUN_TEST(test_io_move_file_relocates_the_file);
    RUN_TEST(test_io_move_file_missing_source_is_an_error);
    RUN_TEST(test_io_append_bytes_extends_the_file);
    RUN_TEST(test_io_append_bytes_result_to_a_directory_is_an_error);
    RUN_TEST(test_io_write_file_result_to_a_directory_is_an_error);
    RUN_TEST(test_io_write_file_result_into_a_missing_directory_is_an_error);
    RUN_TEST(test_io_write_bytes_result_into_a_missing_directory_is_an_error);
    RUN_TEST(test_io_write_file_overwrites_instead_of_appending);
    RUN_TEST(test_io_read_file_result_on_a_directory_is_an_error);
    RUN_TEST(test_io_read_file_result_empty_file);
    RUN_TEST(test_io_read_file_result_larger_than_one_read);
    RUN_TEST(test_io_file_size_result_missing_file_is_an_error);
    RUN_TEST(test_io_delete_file_result_missing_file_is_an_error);
    RUN_TEST(test_io_read_lines_strips_crlf_and_handles_blank_lines);
    RUN_TEST(test_io_temp_file_and_temp_dir_exist_until_exit);
    RUN_TEST(test_io_temp_file_is_writable);
    RUN_TEST(test_io_is_file_and_is_directory_distinguish_kinds);
    RUN_TEST(test_io_stdin_reads_everything_and_reports_eof);
    RUN_TEST(test_io_stdin_bytes_are_packed);
    RUN_TEST(test_io_stdin_empty_input_is_at_eof);

    printf("--- threads ---\n");
    RUN_TEST(test_threads_spawn_runs_the_function_and_join_waits_for_it);
    RUN_TEST(test_threads_spawn_arg_passes_the_argument);
    RUN_TEST(test_threads_spawn_arg_passes_negative_and_extreme_arguments);
    RUN_TEST(test_threads_is_alive_until_the_body_returns);
    RUN_TEST(test_threads_thread_count_tracks_live_threads);
    RUN_TEST(test_threads_id_differs_between_threads);
    RUN_TEST(test_threads_detach_lets_the_thread_finish_on_its_own);
    RUN_TEST(test_threads_detach_of_a_running_thread_then_release);
    RUN_TEST(test_threads_yield_returns);
    RUN_TEST(test_threads_join_and_detach_clear_the_handle);
    RUN_TEST(test_threads_many_threads_all_run);

    printf("--- math (logarithms) ---\n");
    RUN_TEST(test_math_log2_of_powers_of_two);
    RUN_TEST(test_math_log10_of_powers_of_ten);

    PRINT_RESULTS();
    gray_arena_destroy(arena, __FILE__, __LINE__);
    return _test_fail > 0 ? 1 : 0;
}
