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
#include "builtins.h" /* gray_builtin_char_to_utf8 */
#include <inttypes.h>

#define GRAY_I64_BITS       64
#define GRAY_FMT_INTEGER_BUFFER_SIZE      32
#define GRAY_FMT_FLOATING_POINT_BUFFER_SIZE    64

/* Fill `count` repetitions of the UTF-8 encoding of pad codepoint `ch` into
 * `buf`, returning the number of bytes written. A Grayscale char is a full
 * Unicode codepoint (int32_t), so a raw memset(buf, ch, count) truncates
 * anything above U+007F to its low byte instead of writing a proper
 * multi-byte UTF-8 sequence. */
static int64_t fmt_fill_pad_character(GrayArena *arena, char *buffer, int64_t count, int32_t fill_character) {
    GrayString encoded = gray_builtin_char_to_utf8(arena, fill_character);
    char *cursor = buffer;
    for (int64_t i = 0; i < count; i++) {
        memcpy(cursor, encoded.data, (size_t)encoded.len);
        cursor += encoded.len;
    }
    return (int64_t)encoded.len * count;
}

/* Bytes for `pad` pad characters (up to 4 UTF-8 bytes each) plus `fixed`
 * bytes of text. A pad count too large for this arithmetic saturates, so the
 * allocator refuses it with its arena-limit panic instead of the size
 * wrapping to a small allocation the fill loop then runs off the end of. */
static size_t fmt_pad_buffer_size(int64_t padding, int64_t fixed) {
    if (padding > (INT64_MAX - fixed) / 4) return SIZE_MAX / 2;
    return (size_t)(padding * 4 + fixed);
}

GrayString gray_fmt_pad_left(GrayArena *arena, GrayString string, int64_t width, int32_t fill_character) {
    if (string.len >= width) return string;
    int64_t padding = width - string.len;
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, fmt_pad_buffer_size(padding, string.len));
    int64_t pad_bytes = fmt_fill_pad_character(arena, buffer, padding, fill_character);
    memcpy(buffer + pad_bytes, string.data, (size_t)string.len);
    return (GrayString){buffer, (int32_t)(pad_bytes + string.len)};
}

GrayString gray_fmt_pad_right(GrayArena *arena, GrayString string, int64_t width, int32_t fill_character) {
    if (string.len >= width) return string;
    int64_t padding = width - string.len;
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, fmt_pad_buffer_size(padding, string.len));
    memcpy(buffer, string.data, (size_t)string.len);
    int64_t pad_bytes = fmt_fill_pad_character(arena, buffer + string.len, padding, fill_character);
    return (GrayString){buffer, (int32_t)(string.len + pad_bytes)};
}

GrayString gray_fmt_center(GrayArena *arena, GrayString string, int64_t width, int32_t fill_character) {
    if (string.len >= width) return string;
    int64_t total_pad = width - string.len;
    int64_t left_pad = total_pad / 2;
    int64_t right_pad = total_pad - left_pad;
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, fmt_pad_buffer_size(total_pad, string.len));
    int64_t left_bytes = fmt_fill_pad_character(arena, buffer, left_pad, fill_character);
    memcpy(buffer + left_bytes, string.data, (size_t)string.len);
    int64_t right_bytes = fmt_fill_pad_character(arena, buffer + left_bytes + string.len, right_pad, fill_character);
    return (GrayString){buffer, (int32_t)(left_bytes + string.len + right_bytes)};
}

GrayString gray_fmt_int_to_hex(GrayArena *arena, int64_t value) {
    char temporary[GRAY_FMT_INTEGER_BUFFER_SIZE];
    int length = snprintf(temporary, sizeof(temporary), "%" PRIx64, (uint64_t)value);
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length);
    memcpy(buffer, temporary, (size_t)length);
    return (GrayString){buffer, length};
}

GrayString gray_fmt_int_to_binary(GrayArena *arena, int64_t value) {
    if (value == 0) {
        char *buffer = (char *)gray_arena_alloc_uninitialized(arena, 1);
        buffer[0] = '0';
        return (GrayString){buffer, 1};
    }
    char temporary[GRAY_I64_BITS + 1];
    int position = GRAY_I64_BITS;
    uint64_t bits = (uint64_t)value;
    while (bits > 0) {
        temporary[--position] = (char)('0' + (bits & 1));
        bits >>= 1;
    }
    int length = GRAY_I64_BITS - position;
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length);
    memcpy(buffer, temporary + position, (size_t)length);
    return (GrayString){buffer, length};
}

GrayString gray_fmt_int_to_octal(GrayArena *arena, int64_t value) {
    char temporary[GRAY_FMT_INTEGER_BUFFER_SIZE];
    int length = snprintf(temporary, sizeof(temporary), "%" PRIo64, (uint64_t)value);
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length);
    memcpy(buffer, temporary, (size_t)length);
    return (GrayString){buffer, length};
}

GrayString gray_fmt_float_fixed(GrayArena *arena, double value, int64_t decimals) {
    char temporary[GRAY_FMT_FLOATING_POINT_BUFFER_SIZE];
    int length = snprintf(temporary, sizeof(temporary), "%.*f", (int)decimals, value);
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length);
    memcpy(buffer, temporary, (size_t)length);
    return (GrayString){buffer, length};
}

GrayString gray_fmt_float_sci(GrayArena *arena, double value) {
    char temporary[GRAY_FMT_FLOATING_POINT_BUFFER_SIZE];
    int length = snprintf(temporary, sizeof(temporary), "%e", value);
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length);
    memcpy(buffer, temporary, (size_t)length);
    return (GrayString){buffer, length};
}

GrayString gray_fmt_format_number(GrayArena *arena, int64_t value) {
    bool is_negative = value < 0;
    /* Negate into uint64 without overflowing on INT64_MIN. */
    uint64_t magnitude = is_negative ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    char digits[GRAY_FMT_INTEGER_BUFFER_SIZE];
    int digit_count = snprintf(digits, sizeof(digits), "%" PRIu64, magnitude);
    int commas = (digit_count - 1) / 3;
    int total = digit_count + commas + (is_negative ? 1 : 0);
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)total);
    int output_position = 0;
    if (is_negative) buffer[output_position++] = '-';
    for (int i = 0; i < digit_count; i++) {
        if (i > 0 && (digit_count - i) % 3 == 0) buffer[output_position++] = ',';
        buffer[output_position++] = digits[i];
    }
    return (GrayString){buffer, total};
}

GrayString gray_fmt_format_bytes(GrayArena *arena, int64_t value) {
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    bool is_negative = value < 0;
    uint64_t magnitude = is_negative ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    char temporary[GRAY_FMT_FLOATING_POINT_BUFFER_SIZE];
    int length;
    if (magnitude < 1024) {
        length = snprintf(temporary, sizeof(temporary), "%s%" PRIu64 " B", is_negative ? "-" : "", magnitude);
    } else {
        double scaled = (double)magnitude;
        int unit_index = 0;
        while (scaled >= 1024.0 && unit_index < 5) { scaled /= 1024.0; unit_index++; }
        length = snprintf(temporary, sizeof(temporary), "%s%.1f %s", is_negative ? "-" : "", scaled, units[unit_index]);
    }
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)length);
    memcpy(buffer, temporary, (size_t)length);
    return (GrayString){buffer, length};
}
