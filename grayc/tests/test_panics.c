/*
 * test_panics.c — Fork-based unit tests for runtime panic codes.
 * Each test forks a child, runs a function that should panic, and
 * verifies the child exits non-zero with the expected P-code on stderr.
 *
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"

#include "../src/runtime/runtime.h"
#include "../src/runtime/array.h"
#include "../src/runtime/map.h"
#include "../src/runtime/bigint.h"

#include "../src/stdlib/encoding.h"
#include "../src/stdlib/arrays.h"
#include "../src/stdlib/builtins.h"
#include "../src/stdlib/strconv.h"
#include "../src/stdlib/random.h"
#include "../src/stdlib/strings.h"
#include "../src/stdlib/io.h"
#include "../src/stdlib/math.h"

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

static GrayArena *arena;

/* ---------------------------------------------------------------------------
 * ASSERT_PANICS infrastructure — fork child, redirect stderr to pipe,
 * run fn(), check non-zero exit + expected P-code in stderr output.
 * ---------------------------------------------------------------------------*/

static int _assert_panics(const char *expected_code, void (*fn)(void)) {
    int pipefd[2];
    pipe(pipefd);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        /* Ignore SIGPIPE so exit(1) in gray_panic_code doesn't get
           killed before flushing stderr when stdout's pipe is gone. */
        signal(SIGPIPE, SIG_IGN);
        fn();
        _exit(0); /* should not reach here */
    }
    close(pipefd[1]);
    char buf[4096];
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);
    if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
    int status;
    waitpid(pid, &status, 0);
    /* Accept non-zero exit OR signal death (e.g. SIGPIPE) as "panicked" */
    int died = (WIFEXITED(status) && WEXITSTATUS(status) != 0) ||
               WIFSIGNALED(status);
    if (!died) return 0;
    return strstr(buf, expected_code) != NULL;
}

#define ASSERT_PANICS(code, fn) do { \
    if (!_assert_panics(code, fn)) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: expected panic %s from %s\n", \
            __FILE__, __LINE__, code, #fn); \
        _test_failed_this = 1; return; \
    } \
} while(0)

/* ===========================================================================
 * Sized arithmetic overflow
 * ===========================================================================*/

static void trigger_P0012(void) {
    gray_sized_sub_check(-32768, 1, -32768, 32767, "i16", __FILE__, __LINE__);
}
static void test_panic_P0012(void) { ASSERT_PANICS("P0012", trigger_P0012); }

static void trigger_P0013(void) {
    gray_sized_mul_check(32767, 2, -32768, 32767, "i16", __FILE__, __LINE__);
}
static void test_panic_P0013(void) { ASSERT_PANICS("P0013", trigger_P0013); }

static void trigger_P0015(void) {
    gray_usized_add_check(65535, 1, 65535, "u16", __FILE__, __LINE__);
}
static void test_panic_P0015(void) { ASSERT_PANICS("P0015", trigger_P0015); }

static void trigger_P0017(void) {
    gray_usized_mul_check(65535, 2, 65535, "u16", __FILE__, __LINE__);
}
static void test_panic_P0017(void) { ASSERT_PANICS("P0017", trigger_P0017); }

/* ===========================================================================
 * Encoding
 * ===========================================================================*/

static void trigger_P0036(void) {
    gray_encoding_base64_decode(arena, gray_string_lit("abc"));
}
static void test_panic_P0036(void) { ASSERT_PANICS("P0036", trigger_P0036); }

static void trigger_P0037(void) {
    /* Padding in a non-final quad triggers P0037 */
    gray_encoding_base64_decode(arena, gray_string_lit("abc=abcd"));
}
static void test_panic_P0037(void) { ASSERT_PANICS("P0037", trigger_P0037); }

static void trigger_P0038(void) {
    gray_encoding_base64_decode(arena, gray_string_lit("ab=c"));
}
static void test_panic_P0038(void) { ASSERT_PANICS("P0038", trigger_P0038); }

static void trigger_P0039(void) {
    gray_encoding_base64_decode(arena, gray_string_lit("@@@@"));
}
static void test_panic_P0039(void) { ASSERT_PANICS("P0039", trigger_P0039); }

static void trigger_P0040(void) {
    gray_encoding_hex_decode(arena, gray_string_lit("abc"));
}
static void test_panic_P0040(void) { ASSERT_PANICS("P0040", trigger_P0040); }

static void trigger_P0041(void) {
    gray_encoding_hex_decode(arena, gray_string_lit("zz"));
}
static void test_panic_P0041(void) { ASSERT_PANICS("P0041", trigger_P0041); }

/* ===========================================================================
 * Array/string bounds
 * ===========================================================================*/

static void trigger_P0044(void) {
    GrayArray a = gray_array_new(arena, sizeof(int64_t), 4);
    int64_t v = 1;
    GRAY_ARRAY_PUSH(arena, &a, &v);
    gray_arrays_remove_at(&a, 5);
}
static void test_panic_P0044(void) { ASSERT_PANICS("P0044", trigger_P0044); }

static void trigger_P0045(void) {
    GrayArray a = gray_array_new(arena, sizeof(int64_t), 0);
    gray_arrays_first_ptr(&a);
}
static void test_panic_P0045(void) { ASSERT_PANICS("P0045", trigger_P0045); }

static void trigger_P0046(void) {
    GrayArray a = gray_array_new(arena, sizeof(int64_t), 0);
    gray_arrays_last_ptr(&a);
}
static void test_panic_P0046(void) { ASSERT_PANICS("P0046", trigger_P0046); }

static void trigger_P0047(void) {
    GrayArray a = gray_array_new(arena, sizeof(int64_t), 0);
    int64_t out;
    gray_arrays_remove_first_raw(&a, &out);
}
static void test_panic_P0047(void) { ASSERT_PANICS("P0047", trigger_P0047); }

static void trigger_P0048(void) {
    GrayArray a = gray_array_new(arena, sizeof(int64_t), 0);
    int64_t out;
    gray_arrays_remove_last_raw(&a, &out);
}
static void test_panic_P0048(void) { ASSERT_PANICS("P0048", trigger_P0048); }

static void trigger_P0049(void) {
    gray_builtin_to_char(gray_string_lit("hello"), -1, __FILE__, __LINE__);
}
static void test_panic_P0049(void) { ASSERT_PANICS("P0049", trigger_P0049); }

static void trigger_P0050(void) {
    gray_builtin_to_char(gray_string_lit("hello"), 99, __FILE__, __LINE__);
}
static void test_panic_P0050(void) { ASSERT_PANICS("P0050", trigger_P0050); }

/* ===========================================================================
 * Math
 * ===========================================================================*/

static void trigger_P0066(void) {
    gray_math_log2(-1.0);
}
static void test_panic_P0066(void) { ASSERT_PANICS("P0066", trigger_P0066); }

static void trigger_P0067(void) {
    gray_math_log10(-1.0);
}
static void test_panic_P0067(void) { ASSERT_PANICS("P0067", trigger_P0067); }

/* ===========================================================================
 * Strconv
 * ===========================================================================*/

static void trigger_P0056(void) {
    gray_strconv_to_uint(gray_string_lit("42"), 0);
}
static void test_panic_P0056(void) { ASSERT_PANICS("P0056", trigger_P0056); }

/* ===========================================================================
 * Random
 * ===========================================================================*/

static void trigger_P0063(void) {
    GrayArray a = GRAY_ARRAY_FROM_I64(arena, 1, 2, 3);
    gray_random_sample(arena, &a, -1);
}
static void test_panic_P0063(void) { ASSERT_PANICS("P0063", trigger_P0063); }

/* ===========================================================================
 * Strings
 * ===========================================================================*/

static void trigger_P0073(void) {
    gray_strings_repeat(arena, gray_string_lit("abc"), 715827883);
}
static void test_panic_P0073(void) { ASSERT_PANICS("P0073", trigger_P0073); }

/* ===========================================================================
 * Builtins
 * ===========================================================================*/

static void trigger_P0084(void) {
    gray_builtin_string_to_int(gray_string_lit("abc"));
}
static void test_panic_P0084(void) { ASSERT_PANICS("P0084", trigger_P0084); }

static void trigger_P0085(void) {
    gray_builtin_string_to_float(gray_string_lit("xyz"));
}
static void test_panic_P0085(void) { ASSERT_PANICS("P0085", trigger_P0085); }

/* ===========================================================================
 * IO
 * ===========================================================================*/

static void trigger_P0086(void) {
    gray_io_read_file(arena, gray_string_lit("."));
}
static void test_panic_P0086(void) { ASSERT_PANICS("P0086", trigger_P0086); }

static void trigger_P0087(void) {
    gray_io_write_file(gray_string_lit("."), gray_string_lit("x"));
}
static void test_panic_P0087(void) { ASSERT_PANICS("P0087", trigger_P0087); }

static void trigger_P0088(void) {
    gray_io_append_file(gray_string_lit("."), gray_string_lit("x"));
}
static void test_panic_P0088(void) { ASSERT_PANICS("P0088", trigger_P0088); }

static void trigger_P0089(void) {
    gray_io_copy_file(gray_string_lit("."), gray_string_lit("/tmp/gray_test_P0089"));
}
static void test_panic_P0089(void) { ASSERT_PANICS("P0089", trigger_P0089); }

/* ===========================================================================
 * Bigint casts
 * ===========================================================================*/

static void trigger_P0093(void) {
    gray_i128 v = gray_i128_add(gray_i128_from_i64(INT64_MAX),
                                gray_i128_from_i64(1));
    gray_i128_to_i64(v, __FILE__, __LINE__);
}
static void test_panic_P0093(void) { ASSERT_PANICS("P0093", trigger_P0093); }

static void trigger_P0094(void) {
    gray_i128_to_u64(gray_i128_from_i64(-1), __FILE__, __LINE__);
}
static void test_panic_P0094(void) { ASSERT_PANICS("P0094", trigger_P0094); }

static void trigger_P0095(void) {
    gray_u128 v = gray_u128_add(gray_u128_from_u64((uint64_t)INT64_MAX),
                                gray_u128_from_u64(1));
    gray_u128_to_i64(v, __FILE__, __LINE__);
}
static void test_panic_P0095(void) { ASSERT_PANICS("P0095", trigger_P0095); }

static void trigger_P0096(void) {
    gray_u128 v = gray_u128_add(gray_u128_from_u64(UINT64_MAX),
                                gray_u128_from_u64(1));
    gray_u128_to_u64(v, __FILE__, __LINE__);
}
static void test_panic_P0096(void) { ASSERT_PANICS("P0096", trigger_P0096); }

static void trigger_P0097(void) {
    gray_i256 v = gray_i256_add(gray_i256_from_i64(INT64_MAX),
                                gray_i256_from_i64(1));
    gray_i256_to_i64(v, __FILE__, __LINE__);
}
static void test_panic_P0097(void) { ASSERT_PANICS("P0097", trigger_P0097); }

static void trigger_P0098(void) {
    gray_i256_to_u64(gray_i256_from_i64(-1), __FILE__, __LINE__);
}
static void test_panic_P0098(void) { ASSERT_PANICS("P0098", trigger_P0098); }

static void trigger_P0099(void) {
    gray_u256 v = gray_u256_add(gray_u256_from_u64((uint64_t)INT64_MAX),
                                gray_u256_from_u64(1));
    gray_u256_to_i64(v, __FILE__, __LINE__);
}
static void test_panic_P0099(void) { ASSERT_PANICS("P0099", trigger_P0099); }

static void trigger_P0100(void) {
    gray_u256 v = gray_u256_add(gray_u256_from_u64(UINT64_MAX),
                                gray_u256_from_u64(1));
    gray_u256_to_u64(v, __FILE__, __LINE__);
}
static void test_panic_P0100(void) { ASSERT_PANICS("P0100", trigger_P0100); }

/* ===========================================================================
 * Bigint parse
 * ===========================================================================*/

static void trigger_P0102(void) {
    gray_u128_from_decimal("xyz");
}
static void test_panic_P0102(void) { ASSERT_PANICS("P0102", trigger_P0102); }

/* ===========================================================================
 * main
 * ===========================================================================*/

int main(void) {
    arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);

    printf("\n");

    printf("--- Sized Arithmetic Overflow ---\n");
    RUN_TEST(test_panic_P0012);
    RUN_TEST(test_panic_P0013);
    RUN_TEST(test_panic_P0015);
    RUN_TEST(test_panic_P0017);

    printf("--- Encoding ---\n");
    RUN_TEST(test_panic_P0036);
    RUN_TEST(test_panic_P0037);
    RUN_TEST(test_panic_P0038);
    RUN_TEST(test_panic_P0039);
    RUN_TEST(test_panic_P0040);
    RUN_TEST(test_panic_P0041);

    printf("--- Array/String Bounds ---\n");
    RUN_TEST(test_panic_P0044);
    RUN_TEST(test_panic_P0045);
    RUN_TEST(test_panic_P0046);
    RUN_TEST(test_panic_P0047);
    RUN_TEST(test_panic_P0048);
    RUN_TEST(test_panic_P0049);
    RUN_TEST(test_panic_P0050);

    printf("--- Math ---\n");
    RUN_TEST(test_panic_P0066);
    RUN_TEST(test_panic_P0067);

    printf("--- Strconv ---\n");
    RUN_TEST(test_panic_P0056);

    printf("--- Random ---\n");
    RUN_TEST(test_panic_P0063);

    printf("--- Strings ---\n");
    RUN_TEST(test_panic_P0073);

    printf("--- Builtins ---\n");
    RUN_TEST(test_panic_P0084);
    RUN_TEST(test_panic_P0085);

    printf("--- IO ---\n");
    RUN_TEST(test_panic_P0086);
    RUN_TEST(test_panic_P0087);
    RUN_TEST(test_panic_P0088);
    RUN_TEST(test_panic_P0089);

    printf("--- Bigint Casts ---\n");
    RUN_TEST(test_panic_P0093);
    RUN_TEST(test_panic_P0094);
    RUN_TEST(test_panic_P0095);
    RUN_TEST(test_panic_P0096);
    RUN_TEST(test_panic_P0097);
    RUN_TEST(test_panic_P0098);
    RUN_TEST(test_panic_P0099);
    RUN_TEST(test_panic_P0100);

    printf("--- Bigint Parse ---\n");
    RUN_TEST(test_panic_P0102);

    PRINT_RESULTS();

    gray_arena_destroy(arena, __FILE__, __LINE__);
    return _test_fail > 0 ? 1 : 0;
}
