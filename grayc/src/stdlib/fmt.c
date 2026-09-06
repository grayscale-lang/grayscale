/*
 * fmt.c — Implementation of the fmt stdlib module.
 * Provides string padding, centering, printf/sprintf-style formatted
 * output, and numeric base conversion for formatted I/O.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "fmt.h"
#include <inttypes.h>

#define GRAY_INT64_BITS       64
#define GRAY_FMT_INT_BUF      32
#define GRAY_FMT_FLOAT_BUF    64

GrayString gray_fmt_pad_left(GrayArena *arena, GrayString str, int64_t width, char ch) {
    if (str.len >= width) return str;
    int64_t pad = width - str.len;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)width);
    memset(buf, ch, (size_t)pad);
    memcpy(buf + pad, str.data, (size_t)str.len);
    return (GrayString){buf, width};
}

GrayString gray_fmt_pad_right(GrayArena *arena, GrayString str, int64_t width, char ch) {
    if (str.len >= width) return str;
    int64_t pad = width - str.len;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)width);
    memcpy(buf, str.data, (size_t)str.len);
    memset(buf + str.len, ch, (size_t)pad);
    return (GrayString){buf, width};
}

GrayString gray_fmt_center(GrayArena *arena, GrayString str, int64_t width, char ch) {
    if (str.len >= width) return str;
    int64_t total_pad = width - str.len;
    int64_t left_pad = total_pad / 2;
    int64_t right_pad = total_pad - left_pad;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)width);
    memset(buf, ch, (size_t)left_pad);
    memcpy(buf + left_pad, str.data, (size_t)str.len);
    memset(buf + left_pad + str.len, ch, (size_t)right_pad);
    return (GrayString){buf, width};
}

GrayString gray_fmt_int_to_hex(GrayArena *arena, int64_t value) {
    char tmp[GRAY_FMT_INT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%" PRIx64, (uint64_t)value);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_int_to_binary(GrayArena *arena, int64_t value) {
    if (value == 0) {
        char *buf = (char *)gray_arena_alloc_uninitialized(arena, 1);
        buf[0] = '0';
        return (GrayString){buf, 1};
    }
    char tmp[GRAY_INT64_BITS + 1];
    int pos = GRAY_INT64_BITS;
    uint64_t bits = (uint64_t)value;
    while (bits > 0) {
        tmp[--pos] = (char)('0' + (bits & 1));
        bits >>= 1;
    }
    int len = GRAY_INT64_BITS - pos;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp + pos, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_int_to_octal(GrayArena *arena, int64_t value) {
    char tmp[GRAY_FMT_INT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%" PRIo64, (uint64_t)value);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_float_fixed(GrayArena *arena, double value, int64_t decimals) {
    char tmp[GRAY_FMT_FLOAT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%.*f", (int)decimals, value);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_float_sci(GrayArena *arena, double value) {
    char tmp[GRAY_FMT_FLOAT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%e", value);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_format_number(GrayArena *arena, int64_t value) {
    bool neg = value < 0;
    /* Negate into uint64 without overflowing on INT64_MIN. */
    uint64_t magnitude = neg ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    char digits[GRAY_FMT_INT_BUF];
    int digit_count = snprintf(digits, sizeof(digits), "%" PRIu64, magnitude);
    int commas = (digit_count - 1) / 3;
    int total = digit_count + commas + (neg ? 1 : 0);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)total);
    int out_pos = 0;
    if (neg) buf[out_pos++] = '-';
    for (int i = 0; i < digit_count; i++) {
        if (i > 0 && (digit_count - i) % 3 == 0) buf[out_pos++] = ',';
        buf[out_pos++] = digits[i];
    }
    return (GrayString){buf, total};
}

GrayString gray_fmt_format_bytes(GrayArena *arena, int64_t value) {
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    bool neg = value < 0;
    uint64_t magnitude = neg ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    char tmp[GRAY_FMT_FLOAT_BUF];
    int len;
    if (magnitude < 1024) {
        len = snprintf(tmp, sizeof(tmp), "%s%" PRIu64 " B", neg ? "-" : "", magnitude);
    } else {
        double scaled = (double)magnitude;
        int unit_index = 0;
        while (scaled >= 1024.0 && unit_index < 5) { scaled /= 1024.0; unit_index++; }
        len = snprintf(tmp, sizeof(tmp), "%s%.1f %s", neg ? "-" : "", scaled, units[unit_index]);
    }
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}
