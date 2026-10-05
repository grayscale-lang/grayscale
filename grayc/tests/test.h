/*
 * test.h — Minimal C test framework providing ASSERT macros, RUN_TEST
 * dispatch, and pass/fail result reporting for grayc unit tests.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_TEST_H
#define GRAYC_TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int _test_pass = 0;
static int _test_fail = 0;
static int _test_total = 0;
static int _test_failed_this = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s != %s (%lld != %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, (a), (b)); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s == %s (%lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)(a)); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_GT(a, b) do { \
    if (!((a) > (b))) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s <= %s (%lld <= %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_LT(a, b) do { \
    if (!((a) < (b))) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s >= %s (%lld >= %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    if (!((a) >= (b))) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s < %s (%lld < %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "  \033[0;31mFAIL\033[0m %s:%d: %s is NULL\n", __FILE__, __LINE__, #ptr); \
        _test_failed_this = 1; \
        return; \
    } \
} while(0)

#define RUN_TEST(func) do { \
    _test_total++; \
    _test_failed_this = 0; \
    func(); \
    if (_test_failed_this) { \
        _test_fail++; \
    } else { \
        printf("  \033[0;32mPASS\033[0m %s\n", #func); \
        _test_pass++; \
    } \
} while(0)

#define PRINT_RESULTS() do { \
    printf("\n"); \
    if (_test_fail == 0) { \
        printf("\033[1;32m%d passed\033[0m, %d failed (%d total)\n\n", \
            _test_pass, _test_fail, _test_total); \
    } else { \
        printf("\033[1;32m%d passed\033[0m, \033[1;31m%d failed\033[0m (%d total)\n\n", \
            _test_pass, _test_fail, _test_total); \
    } \
} while(0)

/* Build a GrayArray from a literal list of values */
#define GRAY_ARRAY_FROM_I64(arena, ...) \
    gray_array_from((arena), (int64_t[]){__VA_ARGS__}, sizeof(int64_t), \
        sizeof((int64_t[]){__VA_ARGS__}) / sizeof(int64_t), GRAY_ELEM_I64)

#define GRAY_ARRAY_FROM_F64(arena, ...) \
    gray_array_from((arena), (double[]){__VA_ARGS__}, sizeof(double), \
        sizeof((double[]){__VA_ARGS__}) / sizeof(double), GRAY_ELEM_F64)

#define GRAY_ARRAY_FROM_BOOL(arena, ...) \
    gray_array_from((arena), (bool[]){__VA_ARGS__}, sizeof(bool), \
        sizeof((bool[]){__VA_ARGS__}) / sizeof(bool), GRAY_ELEM_BOOL)

#define GRAY_ARRAY_FROM_STR(arena, ...) \
    gray_array_from((arena), (GrayString[]){__VA_ARGS__}, sizeof(GrayString), \
        sizeof((GrayString[]){__VA_ARGS__}) / sizeof(GrayString), GRAY_ELEM_STRING)

#endif
