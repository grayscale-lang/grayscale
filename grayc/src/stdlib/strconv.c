/*
 * strconv.c — Implementation of the strconv stdlib module.
 * Converts between strings and numeric types (int, uint, float)
 * with support for custom bases, validation, and both panicking
 * and result-returning variants.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "strconv.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <strings.h>

#define STRCONV_BUF_SIZE 64

/* Format a double using the shortest representation that round-trips.
 * Shared by builtins (to_string) and strconv (from_float). */
int gray_fmt_shortest_float(char *buf, size_t buffer_size, double v) {
    int n = 0;
    for (int prec = 15; prec <= 17; prec++) {
        n = snprintf(buf, buffer_size, "%.*g", prec, v);
        double rt;
        if (sscanf(buf, "%lf", &rt) == 1 && rt == v) break;
    }
    bool has_special = false;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'i' || buf[i] == 'n') {
            has_special = true;
            break;
        }
    }
    if (!has_special && n + 2 < (int)buffer_size) {
        buf[n++] = '.';
        buf[n++] = '0';
        buf[n] = '\0';
    }
    return n;
}

/* Truncate a GrayString into a stack buffer and null-terminate it.
   Returns the (possibly clamped) length. */
static int strconv_prepare(GrayString s, char *buf, size_t buf_size) {
    int len = s.len < (int32_t)buf_size - 1 ? s.len : (int32_t)buf_size - 1;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* --- Panicking conversions --- */

int64_t gray_strconv_to_int(GrayString s, int base) {
    if (base < 2 || base > 36)
        gray_panic_code("P0054", "strconv.to_int: invalid base %d; must be between 2 and 36", base);
    char buf[STRCONV_BUF_SIZE];
    int len = strconv_prepare(s, buf, sizeof(buf));
    if (len > 0 && isspace((unsigned char)buf[0]))
        gray_panic_code("P0055", "strconv.to_int: cannot convert '%s' to int (base %d)", buf, base);
    char *end = NULL;
    errno = 0;
    int64_t result = strtoll(buf, &end, base);
    if (end == buf || *end != '\0' || errno == ERANGE)
        gray_panic_code("P0055", "strconv.to_int: cannot convert '%s' to int (base %d)", buf, base);
    return result;
}

uint64_t gray_strconv_to_uint(GrayString s, int base) {
    if (base < 2 || base > 36)
        gray_panic_code("P0056", "strconv.to_uint: invalid base %d; must be between 2 and 36", base);
    char buf[STRCONV_BUF_SIZE];
    int len = strconv_prepare(s, buf, sizeof(buf));
    if (len > 0 && isspace((unsigned char)buf[0]))
        gray_panic_code("P0057", "strconv.to_uint: cannot convert '%s' to uint (base %d)", buf, base);
    /* Reject negative numbers */
    for (int i = 0; i < len; i++) {
        if (buf[i] == '-')
            gray_panic_code("P0058", "strconv.to_uint: cannot convert '%s' to uint; value is negative", buf);
        if (!isspace((unsigned char)buf[i])) break;
    }
    char *end = NULL;
    errno = 0;
    uint64_t result = strtoull(buf, &end, base);
    if (end == buf || *end != '\0' || errno == ERANGE)
        gray_panic_code("P0057", "strconv.to_uint: cannot convert '%s' to uint (base %d)", buf, base);
    return result;
}

double gray_strconv_to_float(GrayString s) {
    char buf[STRCONV_BUF_SIZE];
    int len = strconv_prepare(s, buf, sizeof(buf));
    if (len > 0 && isspace((unsigned char)buf[0]))
        gray_panic_code("P0059", "strconv.to_float: cannot convert '%s' to float", buf);
    char *end = NULL;
    errno = 0;
    double result = strtod(buf, &end);
    if (end == buf || *end != '\0' || errno == ERANGE)
        gray_panic_code("P0059", "strconv.to_float: cannot convert '%s' to float", buf);
    return result;
}

bool gray_strconv_to_bool(GrayString s) {
    if (s.len == 4 && strncasecmp(s.data, "true", 4) == 0) return true;
    if (s.len == 5 && strncasecmp(s.data, "false", 5) == 0) return false;
    char buf[STRCONV_BUF_SIZE];
    strconv_prepare(s, buf, sizeof(buf));
    gray_panic_code("P0060", "strconv.to_bool: cannot convert '%s' to bool", buf);
}

/* --- Fallible conversions (result versions) --- */

GrayResult_int gray_strconv_to_int_result(GrayString s, int base) {
    if (base < 2 || base > 36) {
        GrayString msg = gray_string_lit("invalid base for integer conversion (must be 2-36)");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_int){0, err};
    }
    char buf[STRCONV_BUF_SIZE];
    int len = strconv_prepare(s, buf, sizeof(buf));
    if (len > 0 && isspace((unsigned char)buf[0])) {
        GrayString msg = gray_string_lit("cannot convert string to int");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_int){0, err};
    }
    char *end = NULL;
    errno = 0;
    int64_t result = strtoll(buf, &end, base);
    if (end == buf || *end != '\0' || errno == ERANGE) {
        GrayString msg = gray_string_lit("cannot convert string to int");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_int){0, err};
    }
    return (GrayResult_int){result, NULL};
}

GrayResult_uint gray_strconv_to_uint_result(GrayString s, int base) {
    if (base < 2 || base > 36) {
        GrayString msg = gray_string_lit("invalid base for integer conversion (must be 2-36)");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_uint){0, err};
    }
    char buf[STRCONV_BUF_SIZE];
    int len = strconv_prepare(s, buf, sizeof(buf));
    if (len > 0 && isspace((unsigned char)buf[0])) {
        GrayString msg = gray_string_lit("cannot convert string to uint");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_uint){0, err};
    }
    /* Reject negative numbers */
    for (int i = 0; i < len; i++) {
        if (buf[i] == '-') {
            GrayString msg = gray_string_lit("cannot convert negative string to uint");
            GrayError *err = gray_error_new(gray_default_arena, msg);
            return (GrayResult_uint){0, err};
        }
        if (!isspace((unsigned char)buf[i])) break;
    }
    char *end = NULL;
    errno = 0;
    uint64_t result = strtoull(buf, &end, base);
    if (end == buf || *end != '\0' || errno == ERANGE) {
        GrayString msg = gray_string_lit("cannot convert string to uint");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_uint){0, err};
    }
    return (GrayResult_uint){result, NULL};
}

GrayResult_float gray_strconv_to_float_result(GrayString s) {
    char buf[STRCONV_BUF_SIZE];
    int len = strconv_prepare(s, buf, sizeof(buf));
    if (len > 0 && isspace((unsigned char)buf[0])) {
        GrayString msg = gray_string_lit("cannot convert string to float");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_float){0.0, err};
    }
    char *end = NULL;
    errno = 0;
    double result = strtod(buf, &end);
    if (end == buf || *end != '\0' || errno == ERANGE) {
        GrayString msg = gray_string_lit("cannot convert string to float");
        GrayError *err = gray_error_new(gray_default_arena, msg);
        return (GrayResult_float){0.0, err};
    }
    return (GrayResult_float){result, NULL};
}

GrayResult_bool gray_strconv_to_bool_result(GrayString s) {
    if (s.len == 4 && strncasecmp(s.data, "true", 4) == 0) {
        return (GrayResult_bool){true, NULL};
    }
    if (s.len == 5 && strncasecmp(s.data, "false", 5) == 0) {
        return (GrayResult_bool){false, NULL};
    }
    GrayString msg = gray_string_lit("cannot convert string to bool");
    GrayError *err = gray_error_new(gray_default_arena, msg);
    return (GrayResult_bool){false, err};
}

/* --- Type to string conversions --- */

GrayString gray_strconv_from_int(GrayArena *arena, int64_t n) {
    char buf[STRCONV_BUF_SIZE];
    int len = snprintf(buf, sizeof(buf), "%" PRId64, n);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len + 1);
    memcpy(data, buf, (size_t)len + 1);
    return (GrayString){data, (int32_t)len};
}

GrayString gray_strconv_from_uint(GrayArena *arena, uint64_t n) {
    char buf[STRCONV_BUF_SIZE];
    int len = snprintf(buf, sizeof(buf), "%" PRIu64, n);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len + 1);
    memcpy(data, buf, (size_t)len + 1);
    return (GrayString){data, (int32_t)len};
}

GrayString gray_strconv_from_float(GrayArena *arena, double f) {
    char buf[STRCONV_BUF_SIZE];
    int len = gray_fmt_shortest_float(buf, sizeof(buf), f);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len + 1);
    memcpy(data, buf, (size_t)len + 1);
    return (GrayString){data, (int32_t)len};
}

GrayString gray_strconv_from_bool(bool b) {
    if (b) return gray_string_lit("true");
    return gray_string_lit("false");
}

/* --- Query functions --- */

bool gray_strconv_is_numeric(GrayString s) {
    if (s.len == 0) return false;
    int start = 0;
    if (s.data[0] == '-' || s.data[0] == '+') {
        start = 1;
        if (s.len == 1) return false;
    }
    bool has_dot = false;
    bool has_digit = false;
    for (int i = start; i < s.len; i++) {
        if (s.data[i] == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (isdigit((unsigned char)s.data[i])) {
            has_digit = true;
        } else {
            return false;
        }
    }
    return has_digit;
}

bool gray_strconv_is_integer(GrayString s) {
    if (s.len == 0) return false;
    int start = 0;
    if (s.data[0] == '-' || s.data[0] == '+') {
        start = 1;
        if (s.len == 1) return false;
    }
    for (int i = start; i < s.len; i++) {
        if (!isdigit((unsigned char)s.data[i])) return false;
    }
    return true;
}
