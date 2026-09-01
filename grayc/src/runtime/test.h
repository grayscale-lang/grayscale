/*
 * test.h — Test-runner entry points for `gray test`.
 * The generated main() of a --test build calls gray_test_begin() once,
 * gray_test_run() for each #test function, then gray_test_end(). Each test
 * runs behind a setjmp recovery point so a failed assert or panic is
 * reported and the runner moves on to the next test.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_TEST_H
#define GRAY_TEST_H

#include "runtime.h"

/* Reset the pass/fail counters. Called once before any test runs. */
void gray_test_begin(void);

/* Run one #test function. `fn` is the generated gray_fn_<name>; `file` and
 * `line` locate its declaration (used when a failure carries no location).
 * Prints one machine-readable result line to stdout:
 *   GRAYTEST PASS <name>
 *   GRAYTEST FAIL <name>\t<file>:<line>\t<message>
 */
void gray_test_run(const char *name, void (*fn)(void), const char *file, int line);

/* Print the trailer (GRAYTEST DONE <passed> <failed>) and return the process
 * exit code: 0 if every test passed, 1 otherwise. */
int gray_test_end(void);

#endif /* GRAY_TEST_H */
