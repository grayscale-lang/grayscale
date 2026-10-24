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
#include "../src/util/error.h"
#include "../src/util/xalloc.h"
#include "../src/util/platform.h"
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <stdio.h>

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
    void *ptr = arena_allocate(arena, 32);
    ASSERT_NOT_NULL(ptr);
    arena_destroy(arena);
}

static void test_arena_alloc_alignment(void) {
    Arena *arena = arena_create(4096);
    arena_allocate(arena, 1);  /* 1 byte, gets aligned up to 8 */
    void *ptr2 = arena_allocate(arena, 8);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_EQ((uintptr_t)ptr2 % 8, 0);
    arena_destroy(arena);
}

static void test_arena_alloc_second_block(void) {
    Arena *arena = arena_create(64);
    /* Exhaust the first block */
    arena_allocate(arena, 64);
    /* This should trigger a new block */
    void *ptr = arena_allocate(arena, 16);
    ASSERT_NOT_NULL(ptr);
    ASSERT(arena->first->next != NULL);
    arena_destroy(arena);
}

static void test_arena_alloc_oversized(void) {
    Arena *arena = arena_create(64);
    void *ptr = arena_allocate(arena, 256);
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

/* ===== StringBuffer Tests ===== */

static void test_buffer_create(void) {
    StringBuffer buffer = buffer_create(64);
    ASSERT_EQ(buffer.length, 0);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "");
    buffer_destroy(&buffer);
}

static void test_append_string_to_buffer(void) {
    StringBuffer buffer = buffer_create(64);
    append_string_to_buffer(&buffer, "hello");
    ASSERT_EQ(buffer.length, 5);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "hello");
    buffer_destroy(&buffer);
}

static void test_append_bytes_to_buffer(void) {
    StringBuffer buffer = buffer_create(64);
    append_bytes_to_buffer(&buffer, "hello", 3);
    ASSERT_EQ(buffer.length, 3);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "hel");
    buffer_destroy(&buffer);
}

static void test_buffer_append_growth(void) {
    StringBuffer buffer = buffer_create(8);
    append_string_to_buffer(&buffer, "this string is much longer than the initial capacity");
    ASSERT_STR_EQ(buffer_to_string(&buffer), "this string is much longer than the initial capacity");
    buffer_destroy(&buffer);
}

static void test_append_format_to_buffer(void) {
    StringBuffer buffer = buffer_create(64);
    append_format_to_buffer(&buffer, "%d items", 42);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "42 items");
    buffer_destroy(&buffer);
}

static void test_buffer_append_formatted_large(void) {
    StringBuffer buffer = buffer_create(8);
    append_format_to_buffer(&buffer, "value=%d, name=%s, flag=%s", 12345, "test_variable", "true");
    ASSERT_STR_EQ(buffer_to_string(&buffer), "value=12345, name=test_variable, flag=true");
    buffer_destroy(&buffer);
}

static void test_append_char_to_buffer(void) {
    StringBuffer buffer = buffer_create(64);
    append_char_to_buffer(&buffer, 'X');
    ASSERT_EQ(buffer.length, 1);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "X");
    buffer_destroy(&buffer);
}

static void test_buffer_append_char_boundary(void) {
    StringBuffer buffer = buffer_create(4);
    for (int i = 0; i < 20; i++)
        append_char_to_buffer(&buffer, 'A');
    ASSERT_EQ(buffer.length, 20);
    /* Verify all chars are 'A' */
    const char *string = buffer_to_string(&buffer);
    for (int i = 0; i < 20; i++)
        ASSERT_EQ(string[i], 'A');
    buffer_destroy(&buffer);
}

static void test_buffer_append_indent_zero(void) {
    StringBuffer buffer = buffer_create(64);
    append_indent_to_buffer(&buffer, 0);
    ASSERT_EQ(buffer.length, 0);
    ASSERT_STR_EQ(buffer_to_string(&buffer), "");
    buffer_destroy(&buffer);
}

static void test_append_indent_to_buffer(void) {
    StringBuffer buffer = buffer_create(64);
    append_indent_to_buffer(&buffer, 2);
    ASSERT_EQ(buffer.length, 8);  /* 2 * 4 spaces */
    ASSERT_STR_EQ(buffer_to_string(&buffer), "        ");
    buffer_destroy(&buffer);
}

static void test_buffer_append_indent_large(void) {
    StringBuffer buffer = buffer_create(128);
    append_indent_to_buffer(&buffer, 20);
    ASSERT_EQ(buffer.length, 80);  /* 20 * 4 spaces */
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
    GrayType *int_type = type_from_name("i64");
    GrayType *string_type = type_from_name("string");
    scope_define(scope, "x", int_type, true);
    scope_define(scope, "x", string_type, false);
    Symbol *symbol = scope_lookup(scope, "x");
    ASSERT_NOT_NULL(symbol);
    ASSERT(symbol->type == string_type);
    ASSERT_EQ(symbol->is_mutable, false);
    ASSERT_EQ(scope->count, 1);  /* still one symbol, not two */
    scope_destroy(scope);
}

static void test_scope_hash_rebuild(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("i64");
    char names[12][16];
    for (int i = 0; i < 12; i++) {
        snprintf(names[i], sizeof(names[i]), "var_%d", i);
        scope_define(scope, names[i], type, true);
    }
    /* 12 symbols should have triggered at least one hash rebuild (initial hash_cap=16, rebuild when count*2 > hash_cap) */
    ASSERT(scope->hash_capacity >= 32);
    /* Verify all symbols are still findable */
    for (int i = 0; i < 12; i++) {
        Symbol *symbol = scope_lookup(scope, names[i]);
        ASSERT_NOT_NULL(symbol);
    }
    scope_destroy(scope);
}

static void test_scope_many_symbols(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("i64");
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
    GrayType *type = type_from_name("i64");
    scope_define(scope, "a", type, true);
    scope_define(scope, "b", type, false);
    scope_define(scope, "c", type, true);
    scope_destroy(scope);
    /* If we get here without crashing, the test passes */
    ASSERT(1);
}

static void test_scope_immutable(void) {
    Scope *scope = scope_create(NULL);
    GrayType *type = type_from_name("i64");
    scope_define(scope, "constant", type, false);
    Symbol *symbol = scope_lookup(scope, "constant");
    ASSERT_NOT_NULL(symbol);
    ASSERT_EQ(symbol->is_mutable, false);
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

/* ===== Diagnostic rendering ===== */

static char rendered_output[8192];

/* Runs `emit` with stderr redirected into a temporary file and returns what
 * was written. Colour is off for a list made while stderr is not a terminal,
 * but the list is told so explicitly to keep the tests independent of that. */
static const char *capture_stderr(void (*emit)(DiagnosticList *), DiagnosticList *diagnostics) {
    fflush(stderr);
    int saved_descriptor = dup(fileno(stderr));
    FILE *sink = tmpfile();
    dup2(fileno(sink), fileno(stderr));
    emit(diagnostics);
    fflush(stderr);
    dup2(saved_descriptor, fileno(stderr));
    close(saved_descriptor);
    rewind(sink);
    size_t length = fread(rendered_output, 1, sizeof(rendered_output) - 1, sink);
    rendered_output[length] = '\0';
    fclose(sink);
    return rendered_output;
}

static void emit_all(DiagnosticList *diagnostics) { diagnostic_print_all(diagnostics); }
static void emit_summary(DiagnosticList *diagnostics) { diagnostic_print_summary(diagnostics); }

static DiagnosticList *plain_diagnostics(void) {
    DiagnosticList *diagnostics = diagnostic_create();
    diagnostics->should_use_color = false;
    return diagnostics;
}

static void test_diagnostic_counts_errors_and_warnings_separately(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_message(diagnostics, "E3001", "first", "f.gray", 1, 1, 1);
    diagnostic_error_message(diagnostics, "E3001", "second", "f.gray", 2, 1, 1);
    diagnostic_warning_message(diagnostics, "W1001", "third", "f.gray", 3, 1, 1);
    ASSERT_EQ(diagnostic_error_count(diagnostics), 2);
    ASSERT_EQ(diagnostic_warning_count(diagnostics), 1);
    ASSERT(diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_warnings_alone_are_not_errors(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_warning_message(diagnostics, "W1001", "only a warning", "f.gray", 1, 1, 1);
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_error_renders_severity_code_message_and_location(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_set_source(diagnostics, "main.gray", "do main() {\n    mut x = 1\n}\n");
    diagnostic_error_message(diagnostics, "E3001", "type mismatch", "main.gray", 2, 9, 9);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "error[E3001]: type mismatch") != NULL);
    ASSERT(strstr(output, "--> main.gray:2:9") != NULL);
    ASSERT(strstr(output, "mut x = 1") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_underline_aligns_with_the_start_column_and_spans_the_range(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_set_source(diagnostics, "main.gray", "abcdefghij\n");
    diagnostic_error_message(diagnostics, "E3001", "span", "main.gray", 1, 3, 6);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "|   ^^^^\n") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_end_column_before_start_underlines_one_character(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_set_source(diagnostics, "main.gray", "abcdefghij\n");
    diagnostic_error_message(diagnostics, "E3001", "single", "main.gray", 1, 5, 2);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "|     ^\n") != NULL);
    ASSERT(strstr(output, "^^") == NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_warning_renders_as_a_warning(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_set_source(diagnostics, "main.gray", "x\n");
    diagnostic_warning_message(diagnostics, "W1001", "unused", "main.gray", 1, 1, 1);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "warning[W1001]: unused") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_help_text_is_rendered_under_the_source(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_set_source(diagnostics, "main.gray", "x\n");
    diagnostic_error_help(diagnostics, "E3001", "bad", "main.gray", 1, 1, 1, "try this instead");
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "= help: try this instead") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_without_a_file_prints_only_the_message(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_message(diagnostics, "E3001", "no location", NULL, 0, 0, 0);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "error[E3001]: no location") != NULL);
    ASSERT(strstr(output, "-->") == NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_reads_the_source_line_from_disk_when_not_provided(void) {
    char path[] = "/tmp/grayc_diag_testXXXXXX";
    int descriptor = mkstemp(path);
    ASSERT_NE(descriptor, -1);
    const char *contents = "first line\nsecond line\nthird line\n";
    ASSERT_EQ((long long)write(descriptor, contents, strlen(contents)), (long long)strlen(contents));
    close(descriptor);
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_message(diagnostics, "E3001", "from disk", path, 2, 1, 6);
    const char *output = capture_stderr(emit_all, diagnostics);
    remove(path);
    ASSERT(strstr(output, "second line") != NULL);
    ASSERT(strstr(output, "first line") == NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_missing_file_still_prints_the_header(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_message(diagnostics, "E3001", "gone", "/no/such/grayc/file.gray", 3, 1, 1);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "error[E3001]: gone") != NULL);
    ASSERT(strstr(output, "--> /no/such/grayc/file.gray:3:1") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_many_files_are_read_through_the_bounded_cache(void) {
    char paths[6][40];
    DiagnosticList *diagnostics = plain_diagnostics();
    for (int i = 0; i < 6; i++) {
        snprintf(paths[i], sizeof(paths[i]), "/tmp/grayc_diag_cacheXXXXXX");
        int descriptor = mkstemp(paths[i]);
        ASSERT_NE(descriptor, -1);
        char contents[32];
        snprintf(contents, sizeof(contents), "content of file %d\n", i);
        ASSERT_EQ((long long)write(descriptor, contents, strlen(contents)), (long long)strlen(contents));
        close(descriptor);
        diagnostic_error_message(diagnostics, "E3001", "cache", paths[i], 1, 1, 1);
    }
    const char *output = capture_stderr(emit_all, diagnostics);
    for (int i = 0; i < 6; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "content of file %d", i);
        ASSERT(strstr(output, expected) != NULL);
        remove(paths[i]);
    }
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_long_source_line_is_cut_around_the_error_column(void) {
    char line[400];
    memset(line, 'a', sizeof(line));
    memcpy(line + 300, "TARGET", 6);
    line[399] = '\0';
    char source[420];
    snprintf(source, sizeof(source), "%s\n", line);
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_set_source(diagnostics, "main.gray", source);
    diagnostic_error_message(diagnostics, "E3001", "far right", "main.gray", 1, 301, 306);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "TARGET") != NULL);
    ASSERT(strlen(output) < 700);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_color_codes_appear_only_when_enabled(void) {
    DiagnosticList *diagnostics = diagnostic_create();
    diagnostics->should_use_color = true;
    diagnostic_error_message(diagnostics, "E3001", "colored", NULL, 0, 0, 0);
    ASSERT(strstr(capture_stderr(emit_all, diagnostics), "\033[") != NULL);
    diagnostics->should_use_color = false;
    ASSERT(strstr(capture_stderr(emit_all, diagnostics), "\033[") == NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_suppressed_warning_code_is_not_printed(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    static const char *suppressed[] = {"W1001"};
    diagnostics->suppressed_codes = suppressed;
    diagnostics->suppressed_count = 1;
    diagnostic_warning_message(diagnostics, "W1001", "silenced", NULL, 0, 0, 0);
    diagnostic_warning_message(diagnostics, "W1002", "shown", NULL, 0, 0, 0);
    diagnostic_error_message(diagnostics, "E3001", "errors are always printed", NULL, 0, 0, 0);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "silenced") == NULL);
    ASSERT(strstr(output, "shown") != NULL);
    ASSERT(strstr(output, "errors are always printed") != NULL);
    diagnostics->suppressed_codes = NULL;
    diagnostics->suppressed_count = 0;
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_suppress_all_warnings_keeps_errors(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostics->should_suppress_all_warnings = true;
    diagnostic_warning_message(diagnostics, "W1001", "hidden warning", NULL, 0, 0, 0);
    diagnostic_error_message(diagnostics, "E3001", "visible error", NULL, 0, 0, 0);
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "hidden warning") == NULL);
    ASSERT(strstr(output, "visible error") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_summary_counts_and_pluralizes(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_message(diagnostics, "E3001", "a", NULL, 0, 0, 0);
    ASSERT(strstr(capture_stderr(emit_summary, diagnostics), "1 error. compilation failed.") != NULL);
    diagnostic_error_message(diagnostics, "E3001", "b", NULL, 0, 0, 0);
    diagnostic_warning_message(diagnostics, "W1001", "c", NULL, 0, 0, 0);
    const char *output = capture_stderr(emit_summary, diagnostics);
    ASSERT(strstr(output, "2 errors, 1 warning. compilation failed.") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_summary_of_warnings_only_has_no_failure_text(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_warning_message(diagnostics, "W1001", "a", NULL, 0, 0, 0);
    diagnostic_warning_message(diagnostics, "W1002", "b", NULL, 0, 0, 0);
    const char *output = capture_stderr(emit_summary, diagnostics);
    ASSERT(strstr(output, "2 warnings") != NULL);
    ASSERT(strstr(output, "compilation failed") == NULL);
    ASSERT(strstr(output, "hint: suppress warnings with -q") != NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_summary_is_silent_when_nothing_was_reported(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    ASSERT_EQ(strlen(capture_stderr(emit_summary, diagnostics)), 0);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_summary_leaves_out_suppressed_warnings_and_the_hint(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostics->should_suppress_all_warnings = true;
    diagnostic_warning_message(diagnostics, "W1001", "a", NULL, 0, 0, 0);
    ASSERT_EQ(strlen(capture_stderr(emit_summary, diagnostics)), 0);
    diagnostic_error_message(diagnostics, "E3001", "b", NULL, 0, 0, 0);
    const char *output = capture_stderr(emit_summary, diagnostics);
    ASSERT(strstr(output, "1 error.") != NULL);
    ASSERT(strstr(output, "warning") == NULL);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_registry_helpers_use_the_registered_message(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_code(diagnostics, "E3010", NULL, 0, 0, 0);
    diagnostic_error_code_formatted(diagnostics, "E3047", NULL, 0, 0, 0, "Color", "PURPLE");
    diagnostic_warning_code_formatted(diagnostics, "W1001", NULL, 0, 0, 0, "unused_name");
    diagnostic_warning_code(diagnostics, "W1005", NULL, 0, 0, 0);
    diagnostic_error_code_help(diagnostics, "E3010", NULL, 0, 0, 0, "registry help");
    diagnostic_error_code_formatted_help(diagnostics, "E3047", NULL, 0, 0, 0, "formatted help", "Shape", "Hexagon");
    diagnostic_warning_code_formatted_help(diagnostics, "W1001", NULL, 0, 0, 0, "warning help", "other_name");
    const char *output = capture_stderr(emit_all, diagnostics);
    ASSERT(strstr(output, "enum 'Color' has no member 'PURPLE'") != NULL);
    ASSERT(strstr(output, "enum 'Shape' has no member 'Hexagon'") != NULL);
    ASSERT(strstr(output, "'unused_name'") != NULL);
    ASSERT(strstr(output, "= help: registry help") != NULL);
    ASSERT(strstr(output, "= help: formatted help") != NULL);
    ASSERT(strstr(output, "= help: warning help") != NULL);
    ASSERT_EQ(diagnostic_error_count(diagnostics), 4);
    ASSERT_EQ(diagnostic_warning_count(diagnostics), 3);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_duplicates_are_dropped_only_while_requested(void) {
    DiagnosticList *diagnostics = plain_diagnostics();
    diagnostic_error_message(diagnostics, "E3001", "same", "f.gray", 1, 1, 1);
    diagnostic_error_message(diagnostics, "E3001", "same", "f.gray", 1, 1, 1);
    ASSERT_EQ(diagnostic_error_count(diagnostics), 2);
    diagnostics->should_skip_duplicates = true;
    diagnostic_error_message(diagnostics, "E3001", "same", "f.gray", 1, 1, 1);
    ASSERT_EQ(diagnostic_error_count(diagnostics), 2);
    diagnostic_error_message(diagnostics, "E3001", "different", "f.gray", 1, 1, 1);
    ASSERT_EQ(diagnostic_error_count(diagnostics), 3);
    diagnostic_destroy(diagnostics);
}

static void test_diagnostic_edit_distance(void) {
    ASSERT_EQ(diagnostic_edit_distance("", ""), 0);
    ASSERT_EQ(diagnostic_edit_distance("abc", ""), 3);
    ASSERT_EQ(diagnostic_edit_distance("", "abcd"), 4);
    ASSERT_EQ(diagnostic_edit_distance("kitten", "sitting"), 3);
    ASSERT_EQ(diagnostic_edit_distance("same", "same"), 0);
    ASSERT_EQ(diagnostic_edit_distance("retrun", "return"), 2);
}

static void test_diagnostic_edit_distance_with_a_long_second_string(void) {
    char long_text[400];
    memset(long_text, 'a', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    ASSERT_EQ(diagnostic_edit_distance("a", long_text), 398);
    ASSERT_EQ(diagnostic_edit_distance(long_text, long_text), 0);
}

static void test_diagnostic_suggest_statement_name(void) {
    ASSERT_STR_EQ(diagnostic_suggest_statement_name("retrun"), "return");
    ASSERT_STR_EQ(diagnostic_suggest_statement_name("pritnln"), "println");
    ASSERT_STR_EQ(diagnostic_suggest_statement_name("brek"), "break");
    ASSERT(diagnostic_suggest_statement_name("return") == NULL);
    ASSERT(diagnostic_suggest_statement_name("completely_unrelated") == NULL);
}

/* ===== Platform layer ===== */

static void make_scratch_path(char *destination, size_t size, const char *name) {
    snprintf(destination, size, "%s%s%s_%ld", gray_temporary_directory(), GRAY_PATH_SEPARATOR_STRING, name, (long)getpid());
}

static void test_platform_path_join_inserts_exactly_one_separator(void) {
    char buffer[64];
    gray_path_join(buffer, sizeof(buffer), "dir", "file");
    ASSERT_STR_EQ(buffer, "dir" GRAY_PATH_SEPARATOR_STRING "file");
    gray_path_join(buffer, sizeof(buffer), "dir/", "/file");
    ASSERT_STR_EQ(buffer, "dir" GRAY_PATH_SEPARATOR_STRING "file");
    gray_path_join(buffer, sizeof(buffer), "dir///", "///file");
    ASSERT_STR_EQ(buffer, "dir" GRAY_PATH_SEPARATOR_STRING "file");
}

static void test_platform_path_join_with_empty_sides(void) {
    char buffer[64];
    gray_path_join(buffer, sizeof(buffer), "", "file");
    ASSERT_STR_EQ(buffer, "file");
    gray_path_join(buffer, sizeof(buffer), "dir", "");
    ASSERT_STR_EQ(buffer, "dir");
    gray_path_join(buffer, sizeof(buffer), "/", "file");
    ASSERT_STR_EQ(buffer, GRAY_PATH_SEPARATOR_STRING "file");
}

static void test_platform_path_join_reports_truncation_like_snprintf(void) {
    char buffer[6];
    int needed = gray_path_join(buffer, sizeof(buffer), "directory", "file");
    ASSERT_GE(needed, (int)sizeof(buffer));
}

static void test_platform_path_basename(void) {
    ASSERT_STR_EQ(gray_path_basename("/a/b/c.gray"), "c.gray");
    ASSERT_STR_EQ(gray_path_basename("c.gray"), "c.gray");
    ASSERT_STR_EQ(gray_path_basename(""), "");
}

static void test_platform_path_last_separator(void) {
    char path[] = "/a/b/c";
    char *separator = gray_path_last_separator(path);
    ASSERT_NOT_NULL(separator);
    ASSERT_EQ(separator - path, 4);
    char bare[] = "name";
    ASSERT(gray_path_last_separator(bare) == NULL);
}

static void test_platform_path_is_root(void) {
    ASSERT(gray_path_is_root("/"));
    ASSERT(gray_path_is_root("//"));
    ASSERT(!gray_path_is_root("/a"));
    ASSERT(!gray_path_is_root("a/"));
}

static void test_platform_path_is_absolute(void) {
    ASSERT(gray_path_is_absolute("/usr/bin"));
    ASSERT(!gray_path_is_absolute("relative/path"));
    ASSERT(!gray_path_is_absolute(""));
    ASSERT(!gray_path_is_absolute(NULL));
}

static void test_platform_realpath_resolves_and_rejects(void) {
    char *resolved = gray_realpath(gray_temporary_directory());
    ASSERT_NOT_NULL(resolved);
    ASSERT(gray_path_is_absolute(resolved));
    free(resolved);
    ASSERT(gray_realpath("/no/such/grayc/path") == NULL);
    char buffer[1024];
    ASSERT(gray_realpath_into(gray_temporary_directory(), buffer, sizeof(buffer)));
    char tiny[2];
    ASSERT(!gray_realpath_into(gray_temporary_directory(), tiny, sizeof(tiny)));
    ASSERT(!gray_realpath_into("/no/such/grayc/path", buffer, sizeof(buffer)));
}

static void test_platform_path_equal(void) {
    ASSERT(gray_path_equal("/a/b", "/a/b"));
    ASSERT(!gray_path_equal("/a/b", "/a/c"));
    ASSERT(!gray_path_equal("/a/b", "/a/b/"));
}

static void test_platform_file_queries(void) {
    char path[512];
    make_scratch_path(path, sizeof(path), "grayc_platform_file");
    ASSERT(!gray_file_readable(path));
    ASSERT(!gray_is_file(path));
    ASSERT(gray_write_file_mode(path, "abc", 3));
    ASSERT(gray_file_readable(path));
    ASSERT(gray_is_file(path));
    ASSERT(!gray_is_directory(path));
    ASSERT(gray_is_directory(gray_temporary_directory()));
    ASSERT(!gray_is_file(gray_temporary_directory()));
    ASSERT(gray_remove_file(path));
    ASSERT(!gray_remove_file(path));
}

static void test_platform_write_then_read_round_trips_binary_bytes(void) {
    char path[512];
    make_scratch_path(path, sizeof(path), "grayc_platform_binary");
    const unsigned char bytes[] = {0x00, 0x01, 0xFF, '\r', '\n', 0x7F};
    ASSERT(gray_write_file_mode(path, bytes, sizeof(bytes)));
    char *contents = gray_read_file(path, false);
    ASSERT_NOT_NULL(contents);
    ASSERT_EQ(memcmp(contents, bytes, sizeof(bytes)), 0);
    ASSERT_EQ(contents[sizeof(bytes)], 0);
    free(contents);
    gray_remove_file(path);
}

static void test_platform_write_file_truncates_existing_contents(void) {
    char path[512];
    make_scratch_path(path, sizeof(path), "grayc_platform_truncate");
    gray_write_file_mode(path, "a much longer first version", 27);
    gray_write_file_mode(path, "short", 5);
    char *contents = gray_read_file(path, false);
    ASSERT_NOT_NULL(contents);
    ASSERT_STR_EQ(contents, "short");
    free(contents);
    gray_remove_file(path);
}

static void test_platform_write_file_into_a_missing_directory_fails(void) {
    ASSERT(!gray_write_file_mode("/no/such/grayc/dir/file", "x", 1));
}

static void test_platform_read_file_empty_and_missing(void) {
    char path[512];
    make_scratch_path(path, sizeof(path), "grayc_platform_empty");
    gray_write_file_mode(path, "", 0);
    char *contents = gray_read_file(path, false);
    ASSERT_NOT_NULL(contents);
    ASSERT_EQ(strlen(contents), 0);
    free(contents);
    gray_remove_file(path);
    ASSERT(gray_read_file("/no/such/grayc/file", false) == NULL);
}

static void test_platform_read_file_larger_than_one_chunk(void) {
    char path[512];
    make_scratch_path(path, sizeof(path), "grayc_platform_large");
    size_t size = 300000;
    char *data = malloc(size);
    memset(data, 'z', size);
    ASSERT(gray_write_file_mode(path, data, size));
    char *contents = gray_read_file(path, false);
    ASSERT_NOT_NULL(contents);
    ASSERT_EQ(strlen(contents), size);
    free(contents);
    free(data);
    gray_remove_file(path);
}

#ifndef _WIN32
static void test_platform_read_file_from_a_pipe(void) {
    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    ASSERT_EQ((long long)write(descriptors[1], "from a pipe", 11), 11);
    close(descriptors[1]);
    char path[64];
    snprintf(path, sizeof(path), "/dev/fd/%d", descriptors[0]);
    char *contents = gray_read_file(path, false);
    close(descriptors[0]);
    if (contents) {
        ASSERT_STR_EQ(contents, "from a pipe");
        free(contents);
    }
}
#endif

typedef struct { int count; int stop_after; char first[64]; } ScanState;

static bool scan_visitor(const char *name, void *context) {
    ScanState *state = context;
    if (state->count == 0) snprintf(state->first, sizeof(state->first), "%s", name);
    state->count++;
    return state->stop_after == 0 || state->count < state->stop_after;
}

static void test_platform_scandir_visits_entries_without_dot_entries(void) {
    char directory[512], file_a[600], file_b[600];
    make_scratch_path(directory, sizeof(directory), "grayc_platform_scan");
#ifdef _WIN32
    _mkdir(directory);
#else
    mkdir(directory, 0755);
#endif
    gray_path_join(file_a, sizeof(file_a), directory, "one.txt");
    gray_path_join(file_b, sizeof(file_b), directory, "two.txt");
    gray_write_file_mode(file_a, "1", 1);
    gray_write_file_mode(file_b, "2", 1);
    ScanState state = {0, 0, ""};
    ASSERT(gray_scandir(directory, scan_visitor, &state));
    ASSERT_EQ(state.count, 2);
    ASSERT(strcmp(state.first, ".") != 0 && strcmp(state.first, "..") != 0);
    ScanState early = {0, 1, ""};
    ASSERT(gray_scandir(directory, scan_visitor, &early));
    ASSERT_EQ(early.count, 1);
    gray_remove_file(file_a);
    gray_remove_file(file_b);
    rmdir(directory);
}

static void test_platform_scandir_missing_directory_fails(void) {
    ScanState state = {0, 0, ""};
    ASSERT(!gray_scandir("/no/such/grayc/dir", scan_visitor, &state));
    ASSERT_EQ(state.count, 0);
}

static void test_platform_temporary_directory_exists_without_a_trailing_separator(void) {
    const char *directory = gray_temporary_directory();
    ASSERT_NOT_NULL(directory);
    ASSERT_GT(strlen(directory), 0);
    ASSERT(gray_is_directory(directory));
    if (strlen(directory) > 1) ASSERT(!gray_is_path_separator(directory[strlen(directory) - 1]));
}

static void test_platform_temporary_path_has_prefix_suffix_and_is_unique(void) {
    char first[512], second[512];
    int first_length = gray_temporary_path(first, sizeof(first), "grayc_pre_", ".suf");
    int second_length = gray_temporary_path(second, sizeof(second), "grayc_pre_", ".suf");
    ASSERT_GT(first_length, 0);
    ASSERT_GT(second_length, 0);
    ASSERT(strstr(first, "grayc_pre_") != NULL);
    ASSERT_EQ(strcmp(first + strlen(first) - 4, ".suf"), 0);
    ASSERT(strcmp(first, second) != 0);
    ASSERT(!gray_file_readable(first));
}

static void test_platform_temporary_path_reports_truncation(void) {
    char tiny[8];
    int needed = gray_temporary_path(tiny, sizeof(tiny), "prefix_", ".suffix");
    ASSERT(needed == -1 || needed >= (int)sizeof(tiny));
}

static void test_platform_private_cache_directory_is_usable(void) {
    char directory[512];
    ASSERT(gray_private_cache_directory(directory, sizeof(directory)));
    ASSERT(gray_is_directory(directory));
    char tiny[2];
    ASSERT(!gray_private_cache_directory(tiny, sizeof(tiny)));
}

static void test_platform_tmpfile_is_read_write(void) {
    FILE *file = gray_tmpfile();
    ASSERT_NOT_NULL(file);
    fputs("scratch", file);
    rewind(file);
    char buffer[16] = {0};
    ASSERT_NOT_NULL(fgets(buffer, sizeof(buffer), file));
    ASSERT_STR_EQ(buffer, "scratch");
    fclose(file);
}

static void test_platform_getcwd_returns_an_absolute_directory(void) {
    char directory[1024];
    ASSERT(gray_getcwd(directory, sizeof(directory)));
    ASSERT(gray_path_is_absolute(directory));
    char tiny[2];
    ASSERT(!gray_getcwd(tiny, sizeof(tiny)));
}

static void test_platform_self_directory_is_a_directory(void) {
    const char *directory = gray_self_directory("./test_util");
    ASSERT_NOT_NULL(directory);
    ASSERT(gray_is_directory(directory));
}

static void test_platform_signal_name(void) {
    ASSERT_NOT_NULL(gray_signal_name(11));
    ASSERT_GT(strlen(gray_signal_name(11)), 0);
}

#ifndef _WIN32
static void test_platform_command_on_path(void) {
    ASSERT(gray_command_on_path("sh"));
    ASSERT(gray_command_on_path("/bin/sh"));
    ASSERT(!gray_command_on_path("grayc-no-such-command-anywhere"));
    ASSERT(!gray_command_on_path("/no/such/dir/command"));
}

static void test_platform_spawn_path_returns_the_exit_code(void) {
    const char *succeed[] = {"sh", "-c", "exit 0", NULL};
    const char *fail_three[] = {"sh", "-c", "exit 3", NULL};
    ASSERT_EQ(gray_spawn_path(succeed), 0);
    ASSERT_EQ(gray_spawn_path(fail_three), 3);
}

static void test_platform_spawn_path_unknown_program_is_minus_one_or_127(void) {
    const char *missing[] = {"grayc-no-such-command-anywhere", NULL};
    int code = gray_spawn_path(missing);
    ASSERT(code == -1 || code == 127);
}

static void test_platform_spawn_exact_uses_the_path_verbatim(void) {
    const char *succeed[] = {"/bin/sh", "-c", "exit 4", NULL};
    int termination_signal = -1;
    ASSERT_EQ(gray_spawn_exact(succeed, &termination_signal), 4);
    ASSERT_EQ(termination_signal, 0);
    const char *bare[] = {"sh", "-c", "exit 0", NULL};
    ASSERT(gray_spawn_exact(bare, NULL) != 0);
}

static void test_platform_spawn_exact_reports_a_signal_death(void) {
    const char *killed[] = {"/bin/sh", "-c", "kill -9 $$", NULL};
    int termination_signal = 0;
    ASSERT_EQ(gray_spawn_exact(killed, &termination_signal), 128 + 9);
    ASSERT_EQ(termination_signal, 9);
}

static void test_platform_spawn_arguments_are_not_reinterpreted_by_a_shell(void) {
    const char *check[] = {"sh", "-c", "test \"$1\" = 'a b; exit 9'", "ignored", "a b; exit 9", NULL};
    ASSERT_EQ(gray_spawn_path(check), 0);
}

static void test_platform_spawn_quiet_discards_output(void) {
    const char *noisy[] = {"sh", "-c", "echo out; echo err 1>&2; exit 2", NULL};
    ASSERT_EQ(gray_spawn_quiet(noisy), 2);
}

static void test_platform_spawn_capture_stdout_keeps_only_stdout(void) {
    FILE *capture = tmpfile();
    const char *noisy[] = {"sh", "-c", "echo out; echo err 1>&2", NULL};
    ASSERT_EQ(gray_spawn_capture_stdout(noisy, capture), 0);
    rewind(capture);
    char buffer[64] = {0};
    fread(buffer, 1, sizeof(buffer) - 1, capture);
    fclose(capture);
    ASSERT_STR_EQ(buffer, "out\n");
}

static void test_platform_spawn_capture_stderr_keeps_only_stderr(void) {
    FILE *capture = tmpfile();
    const char *noisy[] = {"sh", "-c", "echo out; echo err 1>&2; exit 5", NULL};
    ASSERT_EQ(gray_spawn_capture_stderr(noisy, capture), 5);
    rewind(capture);
    char buffer[64] = {0};
    fread(buffer, 1, sizeof(buffer) - 1, capture);
    fclose(capture);
    ASSERT_STR_EQ(buffer, "err\n");
}
#endif

/* ===== xalloc ===== */

static void test_xmalloc_returns_writable_memory(void) {
    char *block = xmalloc(64);
    ASSERT_NOT_NULL(block);
    memset(block, 'x', 64);
    ASSERT_EQ(block[63], 'x');
    free(block);
}

static void test_xcalloc_returns_zeroed_memory(void) {
    int64_t *values = xcalloc(16, sizeof(int64_t));
    ASSERT_NOT_NULL(values);
    for (int i = 0; i < 16; i++) ASSERT_EQ(values[i], 0);
    free(values);
}

static void test_xrealloc_grows_and_keeps_contents(void) {
    char *block = xmalloc(4);
    memcpy(block, "abc", 4);
    block = xrealloc(block, 4096);
    ASSERT_NOT_NULL(block);
    ASSERT_STR_EQ(block, "abc");
    free(block);
}

static void test_xrealloc_from_null_allocates(void) {
    void *block = xrealloc(NULL, 32);
    ASSERT_NOT_NULL(block);
    free(block);
}

static void test_read_file_to_string_returns_the_whole_file(void) {
    char path[] = "/tmp/grayc_xalloc_testXXXXXX";
    int descriptor = mkstemp(path);
    ASSERT_NE(descriptor, -1);
    const char *contents = "line one\nline two\n";
    ASSERT_EQ((long long)write(descriptor, contents, strlen(contents)), (long long)strlen(contents));
    close(descriptor);
    char *read_back = read_file_to_string(path);
    remove(path);
    ASSERT_NOT_NULL(read_back);
    ASSERT_STR_EQ(read_back, contents);
    free(read_back);
}

static void test_read_file_to_string_empty_file(void) {
    char path[] = "/tmp/grayc_xalloc_emptyXXXXXX";
    int descriptor = mkstemp(path);
    ASSERT_NE(descriptor, -1);
    close(descriptor);
    char *read_back = read_file_to_string(path);
    remove(path);
    ASSERT_NOT_NULL(read_back);
    ASSERT_EQ(strlen(read_back), 0);
    free(read_back);
}

static void test_read_file_to_string_missing_file_is_null(void) {
    ASSERT(read_file_to_string("/no/such/grayc/file") == NULL);
}

/* ===== xalloc: allocation failure ===== */

#ifndef _WIN32
static int exit_status_of_child(void (*body)(void), char *stderr_text, size_t stderr_size) {
    int pipe_descriptors[2];
    if (pipe(pipe_descriptors) != 0) return -1;
    fflush(stdout);
    fflush(stderr);
    pid_t child = fork();
    if (child == 0) {
        close(pipe_descriptors[0]);
        dup2(pipe_descriptors[1], fileno(stderr));
        close(pipe_descriptors[1]);
        body();
        _exit(0);
    }
    close(pipe_descriptors[1]);
    size_t length = 0;
    ssize_t received;
    while (length + 1 < stderr_size && (received = read(pipe_descriptors[0], stderr_text + length, stderr_size - 1 - length)) > 0)
        length += (size_t)received;
    stderr_text[length] = '\0';
    close(pipe_descriptors[0]);
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void *volatile kept_allocation;

static void allocate_too_much_with_xmalloc(void) { kept_allocation = xmalloc((size_t)-1); }
static void allocate_too_much_with_xcalloc(void) { kept_allocation = xcalloc((size_t)-1 / 2, 4); }
static void allocate_too_much_with_xrealloc(void) { kept_allocation = xrealloc(NULL, (size_t)-1); }

static void test_xmalloc_failure_reports_out_of_memory_and_exits(void) {
    char message[128];
    ASSERT_EQ(exit_status_of_child(allocate_too_much_with_xmalloc, message, sizeof(message)), 1);
    ASSERT(strstr(message, "grayc: out of memory") != NULL);
}

static void test_xcalloc_failure_reports_out_of_memory_and_exits(void) {
    char message[128];
    ASSERT_EQ(exit_status_of_child(allocate_too_much_with_xcalloc, message, sizeof(message)), 1);
    ASSERT(strstr(message, "grayc: out of memory") != NULL);
}

static void test_xrealloc_failure_reports_out_of_memory_and_exits(void) {
    char message[128];
    ASSERT_EQ(exit_status_of_child(allocate_too_much_with_xrealloc, message, sizeof(message)), 1);
    ASSERT(strstr(message, "grayc: out of memory") != NULL);
}
#endif

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

    printf("--- diagnostics ---\n");
    RUN_TEST(test_diagnostic_counts_errors_and_warnings_separately);
    RUN_TEST(test_diagnostic_warnings_alone_are_not_errors);
    RUN_TEST(test_diagnostic_error_renders_severity_code_message_and_location);
    RUN_TEST(test_diagnostic_underline_aligns_with_the_start_column_and_spans_the_range);
    RUN_TEST(test_diagnostic_end_column_before_start_underlines_one_character);
    RUN_TEST(test_diagnostic_warning_renders_as_a_warning);
    RUN_TEST(test_diagnostic_help_text_is_rendered_under_the_source);
    RUN_TEST(test_diagnostic_without_a_file_prints_only_the_message);
    RUN_TEST(test_diagnostic_reads_the_source_line_from_disk_when_not_provided);
    RUN_TEST(test_diagnostic_missing_file_still_prints_the_header);
    RUN_TEST(test_diagnostic_many_files_are_read_through_the_bounded_cache);
    RUN_TEST(test_diagnostic_long_source_line_is_cut_around_the_error_column);
    RUN_TEST(test_diagnostic_color_codes_appear_only_when_enabled);
    RUN_TEST(test_diagnostic_suppressed_warning_code_is_not_printed);
    RUN_TEST(test_diagnostic_suppress_all_warnings_keeps_errors);
    RUN_TEST(test_diagnostic_summary_counts_and_pluralizes);
    RUN_TEST(test_diagnostic_summary_of_warnings_only_has_no_failure_text);
    RUN_TEST(test_diagnostic_summary_is_silent_when_nothing_was_reported);
    RUN_TEST(test_diagnostic_summary_leaves_out_suppressed_warnings_and_the_hint);
    RUN_TEST(test_diagnostic_registry_helpers_use_the_registered_message);
    RUN_TEST(test_diagnostic_duplicates_are_dropped_only_while_requested);
    RUN_TEST(test_diagnostic_edit_distance);
    RUN_TEST(test_diagnostic_edit_distance_with_a_long_second_string);
    RUN_TEST(test_diagnostic_suggest_statement_name);

    printf("--- platform ---\n");
    RUN_TEST(test_platform_path_join_inserts_exactly_one_separator);
    RUN_TEST(test_platform_path_join_with_empty_sides);
    RUN_TEST(test_platform_path_join_reports_truncation_like_snprintf);
    RUN_TEST(test_platform_path_basename);
    RUN_TEST(test_platform_path_last_separator);
    RUN_TEST(test_platform_path_is_root);
    RUN_TEST(test_platform_path_is_absolute);
    RUN_TEST(test_platform_realpath_resolves_and_rejects);
    RUN_TEST(test_platform_path_equal);
    RUN_TEST(test_platform_file_queries);
    RUN_TEST(test_platform_write_then_read_round_trips_binary_bytes);
    RUN_TEST(test_platform_write_file_truncates_existing_contents);
    RUN_TEST(test_platform_write_file_into_a_missing_directory_fails);
    RUN_TEST(test_platform_read_file_empty_and_missing);
    RUN_TEST(test_platform_read_file_larger_than_one_chunk);
    RUN_TEST(test_platform_read_file_from_a_pipe);
    RUN_TEST(test_platform_scandir_visits_entries_without_dot_entries);
    RUN_TEST(test_platform_scandir_missing_directory_fails);
    RUN_TEST(test_platform_temporary_directory_exists_without_a_trailing_separator);
    RUN_TEST(test_platform_temporary_path_has_prefix_suffix_and_is_unique);
    RUN_TEST(test_platform_temporary_path_reports_truncation);
    RUN_TEST(test_platform_private_cache_directory_is_usable);
    RUN_TEST(test_platform_tmpfile_is_read_write);
    RUN_TEST(test_platform_getcwd_returns_an_absolute_directory);
    RUN_TEST(test_platform_self_directory_is_a_directory);
    RUN_TEST(test_platform_signal_name);
    RUN_TEST(test_platform_command_on_path);
    RUN_TEST(test_platform_spawn_path_returns_the_exit_code);
    RUN_TEST(test_platform_spawn_path_unknown_program_is_minus_one_or_127);
    RUN_TEST(test_platform_spawn_exact_uses_the_path_verbatim);
    RUN_TEST(test_platform_spawn_exact_reports_a_signal_death);
    RUN_TEST(test_platform_spawn_arguments_are_not_reinterpreted_by_a_shell);
    RUN_TEST(test_platform_spawn_quiet_discards_output);
    RUN_TEST(test_platform_spawn_capture_stdout_keeps_only_stdout);
    RUN_TEST(test_platform_spawn_capture_stderr_keeps_only_stderr);

    printf("--- xalloc ---\n");
    RUN_TEST(test_xmalloc_returns_writable_memory);
    RUN_TEST(test_xcalloc_returns_zeroed_memory);
    RUN_TEST(test_xrealloc_grows_and_keeps_contents);
    RUN_TEST(test_xrealloc_from_null_allocates);
    RUN_TEST(test_read_file_to_string_returns_the_whole_file);
    RUN_TEST(test_read_file_to_string_empty_file);
    RUN_TEST(test_read_file_to_string_missing_file_is_null);

    printf("--- xalloc failure ---\n");
#ifndef _WIN32
    RUN_TEST(test_xmalloc_failure_reports_out_of_memory_and_exits);
    RUN_TEST(test_xcalloc_failure_reports_out_of_memory_and_exits);
    RUN_TEST(test_xrealloc_failure_reports_out_of_memory_and_exits);
#endif

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
