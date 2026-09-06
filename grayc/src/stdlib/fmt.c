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

GrayString gray_fmt_pad_left(GrayArena *arena, GrayString s, int64_t width, char ch) {
    if (s.len >= width) return s;
    int64_t pad = width - s.len;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)width);
    memset(buf, ch, (size_t)pad);
    memcpy(buf + pad, s.data, (size_t)s.len);
    return (GrayString){buf, width};
}

GrayString gray_fmt_pad_right(GrayArena *arena, GrayString s, int64_t width, char ch) {
    if (s.len >= width) return s;
    int64_t pad = width - s.len;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)width);
    memcpy(buf, s.data, (size_t)s.len);
    memset(buf + s.len, ch, (size_t)pad);
    return (GrayString){buf, width};
}

GrayString gray_fmt_center(GrayArena *arena, GrayString s, int64_t width, char ch) {
    if (s.len >= width) return s;
    int64_t total_pad = width - s.len;
    int64_t left_pad = total_pad / 2;
    int64_t right_pad = total_pad - left_pad;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)width);
    memset(buf, ch, (size_t)left_pad);
    memcpy(buf + left_pad, s.data, (size_t)s.len);
    memset(buf + left_pad + s.len, ch, (size_t)right_pad);
    return (GrayString){buf, width};
}

GrayString gray_fmt_int_to_hex(GrayArena *arena, int64_t n) {
    char tmp[GRAY_FMT_INT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%" PRIx64, (uint64_t)n);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_int_to_binary(GrayArena *arena, int64_t n) {
    if (n == 0) {
        char *buf = (char *)gray_arena_alloc_uninitialized(arena, 1);
        buf[0] = '0';
        return (GrayString){buf, 1};
    }
    char tmp[GRAY_INT64_BITS + 1];
    int pos = GRAY_INT64_BITS;
    uint64_t v = (uint64_t)n;
    while (v > 0) {
        tmp[--pos] = (char)('0' + (v & 1));
        v >>= 1;
    }
    int len = GRAY_INT64_BITS - pos;
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp + pos, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_int_to_octal(GrayArena *arena, int64_t n) {
    char tmp[GRAY_FMT_INT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%" PRIo64, (uint64_t)n);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_float_fixed(GrayArena *arena, double f, int64_t decimals) {
    char tmp[GRAY_FMT_FLOAT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%.*f", (int)decimals, f);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_float_sci(GrayArena *arena, double f) {
    char tmp[GRAY_FMT_FLOAT_BUF];
    int len = snprintf(tmp, sizeof(tmp), "%e", f);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}

GrayString gray_fmt_format_number(GrayArena *arena, int64_t n) {
    bool neg = n < 0;
    /* Negate into uint64 without overflowing on INT64_MIN. */
    uint64_t v = neg ? (uint64_t)(-(n + 1)) + 1u : (uint64_t)n;
    char digits[GRAY_FMT_INT_BUF];
    int dlen = snprintf(digits, sizeof(digits), "%" PRIu64, v);
    int commas = (dlen - 1) / 3;
    int total = dlen + commas + (neg ? 1 : 0);
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)total);
    int bi = 0;
    if (neg) buf[bi++] = '-';
    for (int i = 0; i < dlen; i++) {
        if (i > 0 && (dlen - i) % 3 == 0) buf[bi++] = ',';
        buf[bi++] = digits[i];
    }
    return (GrayString){buf, total};
}

GrayString gray_fmt_format_bytes(GrayArena *arena, int64_t n) {
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    bool neg = n < 0;
    uint64_t v = neg ? (uint64_t)(-(n + 1)) + 1u : (uint64_t)n;
    char tmp[GRAY_FMT_FLOAT_BUF];
    int len;
    if (v < 1024) {
        len = snprintf(tmp, sizeof(tmp), "%s%" PRIu64 " B", neg ? "-" : "", v);
    } else {
        double d = (double)v;
        int u = 0;
        while (d >= 1024.0 && u < 5) { d /= 1024.0; u++; }
        len = snprintf(tmp, sizeof(tmp), "%s%.1f %s", neg ? "-" : "", d, units[u]);
    }
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)len);
    memcpy(buf, tmp, (size_t)len);
    return (GrayString){buf, len};
}
