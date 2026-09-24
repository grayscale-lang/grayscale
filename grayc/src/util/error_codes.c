/*
 * error_codes.c — Runtime lookup for error and warning messages, backed
 * by the registry in error_codes.h via sorted binary search.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "error_codes.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const char *code; const char *message; } ErrorEntry;

static ErrorEntry entries[] = {
#define GRAY_ERROR(code, category, message) { code, message },
#define GRAY_WARNING(code, category, message) { code, message },
#define GRAY_PANIC(code, category, message) { code, message },
    GRAY_LEXER_ERRORS
    GRAY_PARSER_ERRORS
    GRAY_TYPE_ERRORS
    GRAY_REFERENCE_ERRORS
    GRAY_USAGE_ERRORS
    GRAY_IMPORT_ERRORS
    GRAY_STDLIB_ERRORS
    GRAY_BITWISE_ERRORS
    GRAY_PANIC_CODES
    GRAY_WARNINGS
#undef GRAY_ERROR
#undef GRAY_WARNING
#undef GRAY_PANIC
};

#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

static int error_code_compare(const void *left, const void *right) {
    return strcmp(((const ErrorEntry *)left)->code, ((const ErrorEntry *)right)->code);
}

static int is_sorted = 0;

const char *gray_error_message(const char *code) {
    if (!code) return NULL;
    if (!is_sorted) {
        qsort(entries, ENTRY_COUNT, sizeof(ErrorEntry), error_code_compare);
        is_sorted = 1;
    }
    ErrorEntry key = { code, NULL };
    ErrorEntry *matching_entry = bsearch(&key, entries, ENTRY_COUNT, sizeof(ErrorEntry), error_code_compare);
    return matching_entry ? matching_entry->message : NULL;
}
