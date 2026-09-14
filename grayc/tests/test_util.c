/*
 * test_util.c — Unit tests for the arena allocator, growable buffer,
 * scope/symbol-table utilities, and the source formatter.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include "../src/util/arena.h"
#include "../src/util/buf.h"
#include "../src/typechecker/scope.h"
#include "../src/typechecker/types.h"
#include "../src/fmt/fmt.h"
#include <stdint.h>

/* ===== Arena Tests ===== */

static void test_arena_create(void) {
    Arena *arena = arena_create(4096);
    ASSERT_NOT_NULL(arena);
    ASSERT_NOT_NULL(arena->first);
    ASSERT_EQ(arena->default_block_size, 4096);
    arena_destroy(arena);
}

static void test_arena_alloc_basic(void) {
    Arena *arena = arena_create(4096);
    void *ptr = arena_alloc(arena, 32);
    ASSERT_NOT_NULL(ptr);
    arena_destroy(arena);
}

static void test_arena_alloc_alignment(void) {
    Arena *arena = arena_create(4096);
    arena_alloc(arena, 1);  /* 1 byte, gets aligned up to 8 */
    void *ptr2 = arena_alloc(arena, 8);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_EQ((uintptr_t)ptr2 % 8, 0);
    arena_destroy(arena);
}

static void test_arena_alloc_second_block(void) {
    Arena *arena = arena_create(64);
    /* Exhaust the first block */
    arena_alloc(arena, 64);
    /* This should trigger a new block */
    void *ptr = arena_alloc(arena, 16);
    ASSERT_NOT_NULL(ptr);
    ASSERT(arena->first->next != NULL);
    arena_destroy(arena);
}

static void test_arena_alloc_oversized(void) {
    Arena *arena = arena_create(64);
    void *ptr = arena_alloc(arena, 256);
    ASSERT_NOT_NULL(ptr);
    arena_destroy(arena);
}

static void test_arena_copy_string(void) {
    Arena *arena = arena_create(4096);
    char *string = arena_copy_string(arena, "hello world");
    ASSERT_NOT_NULL(string);
    ASSERT_STR_EQ(string, "hello world");
    arena_destroy(arena);
}

static void test_arena_copy_string_empty(void) {
    Arena *arena = arena_create(4096);
    char *string = arena_copy_string(arena, "");
    ASSERT_NOT_NULL(string);
    ASSERT_STR_EQ(string, "");
    ASSERT_EQ(strlen(string), 0);
    arena_destroy(arena);
}

static void test_arena_copy_string_with_length(void) {
    Arena *arena = arena_create(4096);
    char *string = arena_copy_string_with_length(arena, "hello world", 5);
    ASSERT_NOT_NULL(string);
    ASSERT_STR_EQ(string, "hello");
    arena_destroy(arena);
}

/* ===== Buf Tests ===== */

static void test_buffer_create(void) {
    Buf buffer = buffer_create(64);
    ASSERT_EQ(buffer.len, 0);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "");
    buffer_destroy(&buffer);
}

static void test_append_string_to_buffer(void) {
    Buf buffer = buffer_create(64);
    append_string_to_buffer(&buffer, "hello");
    ASSERT_EQ(buffer.len, 5);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "hello");
    buffer_destroy(&buffer);
}

static void test_append_bytes_to_buffer(void) {
    Buf buffer = buffer_create(64);
    append_bytes_to_buffer(&buffer, "hello", 3);
    ASSERT_EQ(buffer.len, 3);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "hel");
    buffer_destroy(&buffer);
}

static void test_buffer_append_growth(void) {
    Buf buffer = buffer_create(8);
    append_string_to_buffer(&buffer, "this string is much longer than the initial capacity");
    ASSERT_STR_EQ(buffer_to_string(&buffer), "this string is much longer than the initial capacity");
    buffer_destroy(&buffer);
}

static void test_append_format_to_buffer(void) {
    Buf buffer = buffer_create(64);
    append_format_to_buffer(&buffer, "%d items", 42);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "42 items");
    buffer_destroy(&buffer);
}

static void test_buffer_append_formatted_large(void) {
    Buf buffer = buffer_create(8);
    append_format_to_buffer(&buffer, "value=%d, name=%s, flag=%s", 12345, "test_variable", "true");
    ASSERT_STR_EQ(buffer_to_string(&buffer), "value=12345, name=test_variable, flag=true");
    buffer_destroy(&buffer);
}

static void test_append_char_to_buffer(void) {
    Buf buffer = buffer_create(64);
    append_char_to_buffer(&buffer, 'X');
    ASSERT_EQ(buffer.len, 1);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "X");
    buffer_destroy(&buffer);
}

static void test_buffer_append_char_boundary(void) {
    Buf buffer = buffer_create(4);
    for (int i = 0; i < 20; i++)
        append_char_to_buffer(&buffer, 'A');
    ASSERT_EQ(buffer.len, 20);
    /* Verify all chars are 'A' */
    const char *string = buffer_to_string(&buffer);
    for (int i = 0; i < 20; i++)
        ASSERT_EQ(string[i], 'A');
    buffer_destroy(&buffer);
}

static void test_buffer_append_indent_zero(void) {
    Buf buffer = buffer_create(64);
    append_indent_to_buffer(&buffer, 0);
    ASSERT_EQ(buffer.len, 0);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "");
    buffer_destroy(&buffer);
}

static void test_append_indent_to_buffer(void) {
    Buf buffer = buffer_create(64);
    append_indent_to_buffer(&buffer, 2);
    ASSERT_EQ(buffer.len, 8);  /* 2 * 4 spaces */
    ASSERT_STR_EQ(buffer_to_string(&buffer), "        ");
    buffer_destroy(&buffer);
}

static void test_buffer_append_indent_large(void) {
    Buf buffer = buffer_create(128);
    append_indent_to_buffer(&buffer, 20);
    ASSERT_EQ(buffer.len, 80);  /* 20 * 4 spaces */
    /* Verify all spaces */
    const char *string = buffer_to_string(&buffer);
    for (int i = 0; i < 80; i++)
        ASSERT_EQ(string[i], ' ');
    buffer_destroy(&buffer);
}

/* ===== Scope Tests ===== */

static void test_scope_empty_lookup(void) {
    Scope *scope = scope_create(NULL);
    Symbol *symbol = scope_lookup(scope, "nonexistent");
    ASSERT(symbol == NULL);
    scope_destroy(scope);
}

static void test_scope_define_update(void) {
    Scope *scope = scope_create(NULL);
    GrayType *int_type = type_from_name("int");
    GrayType *string_type = type_from_name("string");
    scope_define(scope, "x", int_type, true);
    scope_define(scope, "x", string_type, false);
    Symbol *symbol = scope_lookup(scope, "x");
    ASSERT_NOT_NULL(symbol);
    ASSERT(symbol->type == string_type);
    ASSERT_EQ(symbol->mutable, false);
    ASSERT_EQ(scope->count, 1);  /* still one symbol, not two */
    scope_destroy(scope);
}

static void test_scope_hash_rebuild(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("int");
    char names[12][16];
    for (int i = 0; i < 12; i++) {
        snprintf(names[i], sizeof(names[i]), "var_%d", i);
        scope_define(scope, names[i], type, true);
    }
    /* 12 symbols should have triggered at least one hash rebuild (initial hash_cap=16, rebuild when count*2 > hash_cap) */
    ASSERT(scope->hash_cap >= 32);
    /* Verify all symbols are still findable */
    for (int i = 0; i < 12; i++) {
        Symbol *symbol = scope_lookup(scope, names[i]);
        ASSERT_NOT_NULL(symbol);
    }
    scope_destroy(scope);
}

static void test_scope_many_symbols(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("int");
    char names[60][16];
    for (int i = 0; i < 60; i++) {
        snprintf(names[i], sizeof(names[i]), "sym_%d", i);
        scope_define(scope, names[i], type, true);
    }
    ASSERT_EQ(scope->count, 60);
    /* Verify all symbols are retrievable */
    for (int i = 0; i < 60; i++) {
        Symbol *symbol = scope_lookup(scope, names[i]);
        ASSERT_NOT_NULL(symbol);
    }
    scope_destroy(scope);
}

static void test_scope_destroy(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("int");
    scope_define(scope, "a", type, true);
    scope_define(scope, "b", type, false);
    scope_define(scope, "c", type, true);
    scope_destroy(scope);
    /* If we get here without crashing, the test passes */
    ASSERT(1);
}

static void test_scope_immutable(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("int");
    scope_define(scope, "constant", type, false);
    Symbol *symbol = scope_lookup(scope, "constant");
    ASSERT_NOT_NULL(symbol);
    ASSERT_EQ(symbol->mutable, false);
    scope_destroy(scope);
}

/* ===== Fmt Tests ===== */

/* Runs gray_fmt_source over `src` and returns the formatted output as a
 * malloc'd, NUL-terminated string the caller must free. */
static char *fmt_string(const char *src) {
    FILE *out = tmpfile();
    if (!out) return NULL;

    int rc = gray_fmt_source(src, "test.gray", out);
    if (rc != 0) { fclose(out); return NULL; }

    long len = ftell(out);
    rewind(out);
    char *buf = malloc((size_t)len + 1);
    size_t got = fread(buf, 1, (size_t)len, out);
    buf[got] = '\0';
    fclose(out);
    return buf;
}

static void test_fmt_top_level_block_comment_preserved(void) {
    const char *src =
        "/*\n"
        " * hello\n"
        " * world\n"
        " */\n"
        "do main() {\n"
        "    println(\"x\")\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_nested_block_comment_preserved(void) {
    const char *src =
        "do main() {\n"
        "    /*\n"
        "     * note\n"
        "     */\n"
        "    println(\"x\")\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_line_comment_matches_enclosing_depth(void) {
    const char *src =
        "do main() {\n"
        "    // note\n"
        "    println(\"x\")\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_misindented_block_comment_shifts_as_unit(void) {
    const char *src =
        "do main() {\n"
        "/*\n"
        " * note\n"
        " */\n"
        "    println(\"x\")\n"
        "}\n";
    const char *want =
        "do main() {\n"
        "    /*\n"
        "     * note\n"
        "     */\n"
        "    println(\"x\")\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, want);
    free(got);
}

static void test_fmt_block_comment_idempotent(void) {
    const char *src =
        "do main() {\n"
        "/*\n"
        " * note\n"
        " */\n"
        "    println(\"x\")\n"
        "}\n";
    char *once = fmt_string(src);
    ASSERT_NOT_NULL(once);
    char *twice = fmt_string(once);
    ASSERT_NOT_NULL(twice);
    ASSERT_STR_EQ(once, twice);
    free(once);
    free(twice);
}

static void test_fmt_multiline_call_args_preserved(void) {
    const char *src =
        "do main() {\n"
        "    passed += want(\"i128 element not truncated\",\n"
        "        string(a[1]) == \"200000000000000000000\")\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_multiline_index_args_preserved(void) {
    const char *src =
        "do main() {\n"
        "    mut v = arr[compute_index(1,\n"
        "        2, 3)]\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_multiline_call_args_reindented_as_unit(void) {
    const char *src =
        "do main() {\n"
        "passed += want(\"note\",\n"
        "    string(a[1]) == \"x\")\n"
        "}\n";
    const char *want =
        "do main() {\n"
        "    passed += want(\"note\",\n"
        "        string(a[1]) == \"x\")\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, want);
    free(got);
}

static void test_fmt_multiline_call_args_idempotent(void) {
    const char *src =
        "do main() {\n"
        "passed += want(\"note\",\n"
        "    string(a[1]) == \"x\")\n"
        "}\n";
    char *once = fmt_string(src);
    ASSERT_NOT_NULL(once);
    char *twice = fmt_string(once);
    ASSERT_NOT_NULL(twice);
    ASSERT_STR_EQ(once, twice);
    free(once);
    free(twice);
}

static void test_fmt_multiline_collection_literal_preserved(void) {
    const char *src =
        "do main() {\n"
        "    keys [string] = {\"k00\", \"k01\", \"k02\",\n"
        "                     \"k03\", \"k04\", \"k05\"}\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_trailing_and_continuation_preserved(void) {
    const char *src =
        "do main() {\n"
        "    if ma == 0 && mb == 1 &&\n"
        "       mc == 100 {\n"
        "        println(\"x\")\n"
        "    }\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_leading_and_continuation_preserved(void) {
    const char *src =
        "do main() {\n"
        "    if len(ks) == 4 && ks[0] == \"one\"\n"
        "        && len(vs) == 4 && vs[0] == 1 {\n"
        "        println(\"x\")\n"
        "    }\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, src);
    free(got);
}

static void test_fmt_and_continuation_reindented_as_unit(void) {
    const char *src =
        "do main() {\n"
        "if ma == 0 &&\n"
        "   mb == 1 {\n"
        "    println(\"x\")\n"
        "}\n"
        "}\n";
    const char *want =
        "do main() {\n"
        "    if ma == 0 &&\n"
        "       mb == 1 {\n"
        "        println(\"x\")\n"
        "    }\n"
        "}\n";
    char *got = fmt_string(src);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got, want);
    free(got);
}

static void test_fmt_and_continuation_idempotent(void) {
    const char *src =
        "do main() {\n"
        "if ma == 0 &&\n"
        "   mb == 1 {\n"
        "    println(\"x\")\n"
        "}\n"
        "}\n";
    char *once = fmt_string(src);
    ASSERT_NOT_NULL(once);
    char *twice = fmt_string(once);
    ASSERT_NOT_NULL(twice);
    ASSERT_STR_EQ(once, twice);
    free(once);
    free(twice);
}

int main(void) {
    printf("\n");

    printf("--- Arena ---\n");
    RUN_TEST(test_arena_create);
    RUN_TEST(test_arena_alloc_basic);
    RUN_TEST(test_arena_alloc_alignment);
    RUN_TEST(test_arena_alloc_second_block);
    RUN_TEST(test_arena_alloc_oversized);
    RUN_TEST(test_arena_copy_string);
    RUN_TEST(test_arena_copy_string_empty);
    RUN_TEST(test_arena_copy_string_with_length);

    printf("--- Buffer ---\n");
    RUN_TEST(test_buffer_create);
    RUN_TEST(test_append_string_to_buffer);
    RUN_TEST(test_append_bytes_to_buffer);
    RUN_TEST(test_buffer_append_growth);
    RUN_TEST(test_append_format_to_buffer);
    RUN_TEST(test_buffer_append_formatted_large);
    RUN_TEST(test_append_char_to_buffer);
    RUN_TEST(test_buffer_append_char_boundary);
    RUN_TEST(test_buffer_append_indent_zero);
    RUN_TEST(test_append_indent_to_buffer);
    RUN_TEST(test_buffer_append_indent_large);

    printf("--- Scope ---\n");
    RUN_TEST(test_scope_empty_lookup);
    RUN_TEST(test_scope_define_update);
    RUN_TEST(test_scope_hash_rebuild);
    RUN_TEST(test_scope_many_symbols);
    RUN_TEST(test_scope_destroy);
    RUN_TEST(test_scope_immutable);

    printf("--- Fmt ---\n");
    RUN_TEST(test_fmt_top_level_block_comment_preserved);
    RUN_TEST(test_fmt_nested_block_comment_preserved);
    RUN_TEST(test_fmt_line_comment_matches_enclosing_depth);
    RUN_TEST(test_fmt_misindented_block_comment_shifts_as_unit);
    RUN_TEST(test_fmt_block_comment_idempotent);
    RUN_TEST(test_fmt_multiline_call_args_preserved);
    RUN_TEST(test_fmt_multiline_index_args_preserved);
    RUN_TEST(test_fmt_multiline_call_args_reindented_as_unit);
    RUN_TEST(test_fmt_multiline_call_args_idempotent);
    RUN_TEST(test_fmt_multiline_collection_literal_preserved);
    RUN_TEST(test_fmt_trailing_and_continuation_preserved);
    RUN_TEST(test_fmt_leading_and_continuation_preserved);
    RUN_TEST(test_fmt_and_continuation_reindented_as_unit);
    RUN_TEST(test_fmt_and_continuation_idempotent);

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
