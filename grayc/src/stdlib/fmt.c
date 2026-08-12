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
