/*
 * test.c — Test-runner implementation for `gray test`.
 * See test.h. The failure hook (gray_test_active / gray_test_vfail) is
 * declared in runtime.h so the panic path (runtime.c) and assert
 * (stdlib/builtins.c) can redirect into it without a link on this file
 * in non-test builds.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include <stdio.h>
#include <stdlib.h>

/* Per-run nonce the `gray test` runner passes in via the environment. Every
 * protocol line is prefixed with it so a #test that prints "GRAYTEST ..." to
 * stdout (a logger, a formatter, or a hostile test) cannot forge a result.
 * Empty when the binary is run directly, so the raw protocol is still usable.
 * Captured once in gray_test_begin() so a test mutating the environment can't
 * change it under the runner. */
static char gray_test_nonce_buf[128];

static const char *gray_test_nonce(void) {
    return gray_test_nonce_buf;
}

/* --- Failure hook (referenced from runtime.c and builtins.c) --- */

bool gray_test_active = false;
jmp_buf gray_test_env;

static char        gray_test_fail_msg[1024];
static const char *gray_test_fail_file = NULL;
static int         gray_test_fail_line = 0;

_Noreturn void gray_test_vfail(const char *code, const char *file, int line,
                               const char *fmt, va_list args) {
    (void)code;
    vsnprintf(gray_test_fail_msg, sizeof(gray_test_fail_msg), fmt, args);
    /* Result lines are tab-delimited and newline-terminated; a message that
     * smuggled in either byte would desync the parser. */
    for (char *p = gray_test_fail_msg; *p; p++)
        if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    gray_test_fail_file = file;
    gray_test_fail_line = line;
    gray_test_active = false;
    longjmp(gray_test_env, 1);
}

_Noreturn void gray_test_fail(const char *code, const char *file, int line,
                              const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gray_test_vfail(code, file, line, fmt, args);
}

/* --- Runner --- */

static int gray_test_pass_count = 0;
static int gray_test_fail_count = 0;

void gray_test_begin(void) {
    gray_test_pass_count = 0;
    gray_test_fail_count = 0;

    const char *n = getenv("GRAY_TEST_PROTOCOL_NONCE");
    if (n) {
        snprintf(gray_test_nonce_buf, sizeof(gray_test_nonce_buf), "%s", n);
    } else {
        gray_test_nonce_buf[0] = '\0';
    }
}

void gray_test_run(const char *name, void (*fn)(void), const char *file, int line) {
    gray_test_fail_msg[0] = '\0';
    gray_test_fail_file = NULL;
    gray_test_fail_line = 0;

    /* A panicking test longjmps straight back here, so the pending
     * gray_exit_func() decrements on the unwound C frames never run and
     * gray_call_depth is left pinned wherever the panic hit it. Snapshot it
     * now and restore below so the next test starts from a clean baseline
     * instead of tripping the recursion guard on its first call.
     * Not modified after this point, so it survives the longjmp untouched. */
    int saved_call_depth = gray_call_depth;

    if (setjmp(gray_test_env) == 0) {
        gray_test_active = true;
        fn();
        gray_test_active = false;
        printf("%sGRAYTEST PASS %s\n", gray_test_nonce(), name);
        gray_test_pass_count++;
    } else {
        /* gray_test_vfail already cleared gray_test_active */
        printf("%sGRAYTEST FAIL %s\t%s:%d\t%s\n", gray_test_nonce(), name,
               gray_test_fail_file ? gray_test_fail_file : file,
               gray_test_fail_file ? gray_test_fail_line : line,
               gray_test_fail_msg[0] ? gray_test_fail_msg : "test failed");
        gray_test_fail_count++;
    }
    gray_call_depth = saved_call_depth;
    fflush(stdout);
}

int gray_test_end(void) {
    printf("%sGRAYTEST DONE %d %d\n", gray_test_nonce(),
           gray_test_pass_count, gray_test_fail_count);
    fflush(stdout);
    return gray_test_fail_count > 0 ? 1 : 0;
}
