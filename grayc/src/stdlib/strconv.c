/*
 * strconv.c — Implementation of the strconv stdlib module.
 * Converts between strings and numeric types (i64, u64, f64)
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

#define STRCONV_BUFFER_SIZE 64

/* Format a floating-point value using the shortest representation that round-trips at
 * `bit_size` (32 or 64): 6-9 significant digits for a 32-bit floating-point value, 15-17
 * for a 64-bit double. Shared by builtins (print, to_string) and strconv
 * (from_float). */
int gray_fmt_shortest_float(char *buffer, size_t buffer_size, double value, int bit_size) {
    int minimum_precision = bit_size == 32 ? 6 : 15;
    int maximum_precision = bit_size == 32 ? 9 : 17;
    int written_length = 0;
    for (int prec = minimum_precision; prec <= maximum_precision; prec++) {
        written_length = snprintf(buffer, buffer_size, "%.*g", prec, value);
        double round_tripped;
        if (sscanf(buffer, "%lf", &round_tripped) != 1) continue;
        if (bit_size == 32 ? (float)round_tripped == (float)value : round_tripped == value) break;
    }
    bool has_special = false;
    for (int i = 0; buffer[i]; i++) {
        if (buffer[i] == '.' || buffer[i] == 'e' || buffer[i] == 'i' || buffer[i] == 'n') {
            has_special = true;
            break;
        }
    }
    if (!has_special && written_length + 2 < (int)buffer_size) {
        buffer[written_length++] = '.';
        buffer[written_length++] = '0';
        buffer[written_length] = '\0';
    }
    return written_length;
}

/* Truncate a GrayString into a stack buffer and null-terminate it.
   Returns the (possibly clamped) length. */
static int strconv_prepare(GrayString string, char *buffer, size_t buffer_size) {
    int length = string.len < (int32_t)buffer_size - 1 ? string.len : (int32_t)buffer_size - 1;
    memcpy(buffer, string.data, (size_t)length);
    buffer[length] = '\0';
    return length;
}

/* --- Panicking conversions --- */

int64_t gray_strconv_to_int(GrayString string, int64_t base) {
    if (base < 2 || base > 36)
        gray_panic_code("P0054", "strconv.to_int: invalid base %lld; must be between 2 and 36", (long long)base);
    char buffer[STRCONV_BUFFER_SIZE];
    int length = strconv_prepare(string, buffer, sizeof(buffer));
    if (length > 0 && isspace((unsigned char)buffer[0]))
        gray_panic_code("P0055", "strconv.to_int: cannot convert '%s' to i64 (base %lld)", buffer, (long long)base);
    char *end_cursor = NULL;
    errno = 0;
    int64_t result = strtoll(buffer, &end_cursor, base);
    if (end_cursor == buffer || *end_cursor != '\0' || errno == ERANGE)
        gray_panic_code("P0055", "strconv.to_int: cannot convert '%s' to i64 (base %lld)", buffer, (long long)base);
    return result;
}

uint64_t gray_strconv_to_uint(GrayString string, int64_t base) {
    if (base < 2 || base > 36)
        gray_panic_code("P0056", "strconv.to_uint: invalid base %lld; must be between 2 and 36", (long long)base);
    char buffer[STRCONV_BUFFER_SIZE];
    int length = strconv_prepare(string, buffer, sizeof(buffer));
    if (length > 0 && isspace((unsigned char)buffer[0]))
        gray_panic_code("P0057", "strconv.to_uint: cannot convert '%s' to u64 (base %lld)", buffer, (long long)base);
    /* Reject negative numbers */
    for (int i = 0; i < length; i++) {
        if (buffer[i] == '-')
            gray_panic_code("P0058", "strconv.to_uint: cannot convert '%s' to u64; value is negative", buffer);
        if (!isspace((unsigned char)buffer[i])) break;
    }
    char *end_cursor = NULL;
    errno = 0;
    uint64_t result = strtoull(buffer, &end_cursor, base);
    if (end_cursor == buffer || *end_cursor != '\0' || errno == ERANGE)
        gray_panic_code("P0057", "strconv.to_uint: cannot convert '%s' to u64 (base %lld)", buffer, (long long)base);
    return result;
}

double gray_strconv_to_float(GrayString string) {
    char buffer[STRCONV_BUFFER_SIZE];
    int length = strconv_prepare(string, buffer, sizeof(buffer));
    if (length > 0 && isspace((unsigned char)buffer[0]))
        gray_panic_code("P0059", "strconv.to_float: cannot convert '%s' to f64", buffer);
    char *end_cursor = NULL;
    errno = 0;
    double result = strtod(buffer, &end_cursor);
    if (end_cursor == buffer || *end_cursor != '\0' || errno == ERANGE)
        gray_panic_code("P0059", "strconv.to_float: cannot convert '%s' to f64", buffer);
    return result;
}

bool gray_strconv_to_bool(GrayString string) {
    if (string.len == 4 && strncasecmp(string.data, "true", 4) == 0) return true;
    if (string.len == 5 && strncasecmp(string.data, "false", 5) == 0) return false;
    char buffer[STRCONV_BUFFER_SIZE];
    strconv_prepare(string, buffer, sizeof(buffer));
    gray_panic_code("P0060", "strconv.to_bool: cannot convert '%s' to bool", buffer);
}

/* --- Fallible conversions (result versions) --- */

GrayResult_i64 gray_strconv_to_int_result(GrayString string, int64_t base) {
    if (base < 2 || base > 36) {
        GrayString message = gray_string_lit("invalid base for integer conversion (must be 2-36)");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_InvalidInput, message);
        return (GrayResult_i64){0, error};
    }
    char buffer[STRCONV_BUFFER_SIZE];
    int length = strconv_prepare(string, buffer, sizeof(buffer));
    if (length > 0 && isspace((unsigned char)buffer[0])) {
        GrayString message = gray_string_lit("cannot convert string to i64");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
        return (GrayResult_i64){0, error};
    }
    char *end_cursor = NULL;
    errno = 0;
    int64_t result = strtoll(buffer, &end_cursor, base);
    if (end_cursor == buffer || *end_cursor != '\0' || errno == ERANGE) {
        GrayString message = gray_string_lit("cannot convert string to i64");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
        return (GrayResult_i64){0, error};
    }
    return (GrayResult_i64){result, NULL};
}

GrayResult_u64 gray_strconv_to_uint_result(GrayString string, int64_t base) {
    if (base < 2 || base > 36) {
        GrayString message = gray_string_lit("invalid base for integer conversion (must be 2-36)");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_InvalidInput, message);
        return (GrayResult_u64){0, error};
    }
    char buffer[STRCONV_BUFFER_SIZE];
    int length = strconv_prepare(string, buffer, sizeof(buffer));
    if (length > 0 && isspace((unsigned char)buffer[0])) {
        GrayString message = gray_string_lit("cannot convert string to u64");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
        return (GrayResult_u64){0, error};
    }
    /* Reject negative numbers */
    for (int i = 0; i < length; i++) {
        if (buffer[i] == '-') {
            GrayString message = gray_string_lit("cannot convert negative string to u64");
            GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_InvalidInput, message);
            return (GrayResult_u64){0, error};
        }
        if (!isspace((unsigned char)buffer[i])) break;
    }
    char *end_cursor = NULL;
    errno = 0;
    uint64_t result = strtoull(buffer, &end_cursor, base);
    if (end_cursor == buffer || *end_cursor != '\0' || errno == ERANGE) {
        GrayString message = gray_string_lit("cannot convert string to u64");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
        return (GrayResult_u64){0, error};
    }
    return (GrayResult_u64){result, NULL};
}

GrayResult_f64 gray_strconv_to_float_result(GrayString string) {
    char buffer[STRCONV_BUFFER_SIZE];
    int length = strconv_prepare(string, buffer, sizeof(buffer));
    if (length > 0 && isspace((unsigned char)buffer[0])) {
        GrayString message = gray_string_lit("cannot convert string to f64");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
        return (GrayResult_f64){0.0, error};
    }
    char *end_cursor = NULL;
    errno = 0;
    double result = strtod(buffer, &end_cursor);
    if (end_cursor == buffer || *end_cursor != '\0' || errno == ERANGE) {
        GrayString message = gray_string_lit("cannot convert string to f64");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
        return (GrayResult_f64){0.0, error};
    }
    return (GrayResult_f64){result, NULL};
}

GrayResult_bool gray_strconv_to_bool_result(GrayString string) {
    if (string.len == 4 && strncasecmp(string.data, "true", 4) == 0) {
        return (GrayResult_bool){true, NULL};
    }
    if (string.len == 5 && strncasecmp(string.data, "false", 5) == 0) {
        return (GrayResult_bool){false, NULL};
    }
    GrayString message = gray_string_lit("cannot convert string to bool");
    GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ConversionFailure, message);
    return (GrayResult_bool){false, error};
}

/* --- Type to string conversions --- */

GrayString gray_strconv_from_int(GrayArena *arena, int64_t value) {
    char buffer[STRCONV_BUFFER_SIZE];
    int length = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(data, buffer, (size_t)length + 1);
    return (GrayString){data, (int32_t)length};
}

GrayString gray_strconv_from_uint(GrayArena *arena, uint64_t value) {
    char buffer[STRCONV_BUFFER_SIZE];
    int length = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(data, buffer, (size_t)length + 1);
    return (GrayString){data, (int32_t)length};
}

GrayString gray_strconv_from_float(GrayArena *arena, double value) {
    char buffer[STRCONV_BUFFER_SIZE];
    int length = gray_fmt_shortest_float(buffer, sizeof(buffer), value, 64);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(data, buffer, (size_t)length + 1);
    return (GrayString){data, (int32_t)length};
}

GrayString gray_strconv_from_bool(bool value) {
    if (value) return gray_string_lit("true");
    return gray_string_lit("false");
}

/* --- Arbitrary-base integer formatting --- */

/* Write the base-`base` digits of `value` into buf (which must hold at least
   64 bytes), most significant first. Returns the number of digits written. */
static int strconv_format_digits(char *buffer, uint64_t value, int base) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char temporary[64];
    int position = (int)sizeof(temporary);
    if (value == 0) temporary[--position] = '0';
    while (value > 0) {
        temporary[--position] = digits[value % (uint64_t)base];
        value /= (uint64_t)base;
    }
    int length = (int)sizeof(temporary) - position;
    memcpy(buffer, temporary + position, (size_t)length);
    return length;
}

GrayString gray_strconv_format_int(GrayArena *arena, int64_t value, int64_t base) {
    if (base < 2 || base > 36)
        gray_panic_code("P0110", "strconv.format_int: invalid base %lld; must be between 2 and 36",
            (long long)base);
    bool is_negative = value < 0;
    /* Negate in unsigned space so INT64_MIN does not overflow. */
    uint64_t magnitude = is_negative ? ~(uint64_t)value + 1 : (uint64_t)value;
    char temporary[65];
    int offset = 0;
    if (is_negative) temporary[offset++] = '-';
    offset += strconv_format_digits(temporary + offset, magnitude, (int)base);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)offset + 1);
    memcpy(data, temporary, (size_t)offset);
    data[offset] = '\0';
    return (GrayString){data, (int32_t)offset};
}

GrayString gray_strconv_format_uint(GrayArena *arena, uint64_t value, int64_t base) {
    if (base < 2 || base > 36)
        gray_panic_code("P0111", "strconv.format_uint: invalid base %lld; must be between 2 and 36",
            (long long)base);
    char temporary[64];
    int length = strconv_format_digits(temporary, value, (int)base);
    char *data = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    memcpy(data, temporary, (size_t)length);
    data[length] = '\0';
    return (GrayString){data, (int32_t)length};
}

/* --- Quoting --- */

GrayString gray_strconv_quote(GrayArena *arena, GrayString string) {
    static const char hex[] = "0123456789abcdef";
    /* Worst case: every byte becomes \xNN (4x), plus the two surrounding
       quotes and a null terminator. */
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)string.len * 4 + 3);
    int32_t j = 0;
    buffer[j++] = '"';
    for (int i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        switch (character) {
        case '"':  buffer[j++] = '\\'; buffer[j++] = '"';  break;
        case '\\': buffer[j++] = '\\'; buffer[j++] = '\\'; break;
        case '\n': buffer[j++] = '\\'; buffer[j++] = 'n';  break;
        case '\r': buffer[j++] = '\\'; buffer[j++] = 'r';  break;
        case '\t': buffer[j++] = '\\'; buffer[j++] = 't';  break;
        default:
            if (character < 0x20 || character == 0x7f) {
                buffer[j++] = '\\'; buffer[j++] = 'x';
                buffer[j++] = hex[character >> 4]; buffer[j++] = hex[character & 0xf];
            } else {
                buffer[j++] = (char)character;
            }
        }
    }
    buffer[j++] = '"';
    buffer[j] = '\0';
    return (GrayString){buffer, j};
}

static int strconv_hex_digit(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/* Unquote s into a freshly allocated string. Returns true on success; on
   failure returns false and leaves *out untouched. */
static bool strconv_unquote_into(GrayArena *arena, GrayString string, GrayString *output) {
    if (string.len < 2 || string.data[0] != '"' || string.data[string.len - 1] != '"')
        return false;
    /* Output is never longer than the quoted interior. */
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)string.len);
    int32_t j = 0;
    int end_index = string.len - 1;
    for (int i = 1; i < end_index; i++) {
        char character = string.data[i];
        if (character == '"') return false; /* unescaped quote */
        if (character != '\\') { buffer[j++] = character; continue; }
        if (++i >= end_index) return false; /* trailing backslash */
        char escape = string.data[i];
        switch (escape) {
        case 'n':  buffer[j++] = '\n'; break;
        case 't':  buffer[j++] = '\t'; break;
        case 'r':  buffer[j++] = '\r'; break;
        case '\\': buffer[j++] = '\\'; break;
        case '"':  buffer[j++] = '"';  break;
        case '\'': buffer[j++] = '\''; break;
        case '0':  buffer[j++] = '\0'; break;
        case 'a':  buffer[j++] = '\a'; break;
        case 'b':  buffer[j++] = '\b'; break;
        case 'f':  buffer[j++] = '\f'; break;
        case 'v':  buffer[j++] = '\v'; break;
        case '$':  buffer[j++] = '$';  break;
        case 'x': {
            if (i + 2 >= end_index) return false;
            int high_digit = strconv_hex_digit(string.data[i + 1]);
            int low_digit = strconv_hex_digit(string.data[i + 2]);
            if (high_digit < 0 || low_digit < 0) return false;
            buffer[j++] = (char)((high_digit << 4) | low_digit);
            i += 2;
            break;
        }
        default: return false;
        }
    }
    buffer[j] = '\0';
    *output = (GrayString){buffer, j};
    return true;
}

GrayString gray_strconv_unquote(GrayArena *arena, GrayString string) {
    GrayString output;
    if (!strconv_unquote_into(arena, string, &output)) {
        char buffer[STRCONV_BUFFER_SIZE];
        strconv_prepare(string, buffer, sizeof(buffer));
        gray_panic_code("P0112", "strconv.unquote: cannot unquote '%s'", buffer);
    }
    return output;
}

GrayResult_string gray_strconv_unquote_result(GrayArena *arena, GrayString string) {
    GrayString output;
    if (!strconv_unquote_into(arena, string, &output)) {
        GrayString message = gray_string_lit("cannot unquote string");
        GrayError *error = gray_error_new(gray_default_arena, GRAY_ERR_ParseFailure, message);
        return (GrayResult_string){{"", 0}, error};
    }
    return (GrayResult_string){output, NULL};
}

/* --- Query functions --- */

bool gray_strconv_is_numeric(GrayString string) {
    if (string.len == 0) return false;
    int start = 0;
    if (string.data[0] == '-' || string.data[0] == '+') {
        start = 1;
        if (string.len == 1) return false;
    }
    bool has_dot = false;
    bool has_digit = false;
    for (int i = start; i < string.len; i++) {
        if (string.data[i] == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (isdigit((unsigned char)string.data[i])) {
            has_digit = true;
        } else {
            return false;
        }
    }
    return has_digit;
}

bool gray_strconv_is_integer(GrayString string) {
    if (string.len == 0) return false;
    int start = 0;
    if (string.data[0] == '-' || string.data[0] == '+') {
        start = 1;
        if (string.len == 1) return false;
    }
    for (int i = start; i < string.len; i++) {
        if (!isdigit((unsigned char)string.data[i])) return false;
    }
    return true;
}
