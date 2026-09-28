/*
 * chars.c — Implementation of the chars stdlib module.
 * ASCII-only case folding on a Unicode codepoint (Grayscale `char` is a
 * 32-bit codepoint). Done by range check rather than <ctype.h> so the
 * result is locale-independent and leaves every non-ASCII codepoint alone.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "chars.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

int32_t gray_chars_to_upper(int32_t codepoint) {
    return (codepoint >= 'a' && codepoint <= 'z') ? codepoint - ('a' - 'A') : codepoint;
}

int32_t gray_chars_to_lower(int32_t codepoint) {
    return (codepoint >= 'A' && codepoint <= 'Z') ? codepoint + ('a' - 'A') : codepoint;
}

bool gray_chars_is_ascii(int32_t codepoint) {
    return codepoint >= 0 && codepoint <= 0x7F;
}

bool gray_chars_is_control(int32_t codepoint) {
    return (codepoint >= 0 && codepoint < 0x20) || codepoint == 0x7F;
}

bool gray_chars_is_printable(int32_t codepoint) {
    return codepoint >= 0x20 && codepoint < 0x7F;
}

bool gray_chars_is_punct(int32_t codepoint) {
    return (codepoint >= 0x21 && codepoint <= 0x2F) ||
           (codepoint >= 0x3A && codepoint <= 0x40) ||
           (codepoint >= 0x5B && codepoint <= 0x60) ||
           (codepoint >= 0x7B && codepoint <= 0x7E);
}

bool gray_chars_is_hex_digit(int32_t codepoint) {
    return (codepoint >= '0' && codepoint <= '9') ||
           (codepoint >= 'a' && codepoint <= 'f') ||
           (codepoint >= 'A' && codepoint <= 'F');
}

bool gray_chars_is_word_char(int32_t codepoint) {
    return (codepoint >= '0' && codepoint <= '9') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 'A' && codepoint <= 'Z') ||
           codepoint == '_';
}

/* === Display width (wcwidth semantics) ===
 *
 * Static range tables for the two Unicode properties display width needs:
 * codepoints that combine onto the previous one (width 0) and codepoints
 * whose East Asian Width is Wide or Fullwidth, plus default-presentation
 * emoji (width 2). Both tables are sorted ascending and non-overlapping so
 * membership is a binary search.
 *
 * Coverage is deliberately not exhaustive: it covers the combining-mark and
 * format-character blocks in common use (Latin/Cyrillic/Greek diacritics,
 * Hebrew points, Arabic marks, Thai/Lao vowel signs and tone marks, CJK tone
 * marks, joiners, variation selectors) and the standard CJK/Hangul/fullwidth
 * East Asian Width blocks plus the three major emoji blocks. Fine-grained
 * Indic and other complex-script combining marks are future work; an
 * unlisted codepoint falls through to width 1.
 */
typedef struct { int32_t low, high; } CharsWidthRange;

static const CharsWidthRange CHARS_ZERO_WIDTH[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF},
    {0x05C1, 0x05C2}, {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x06DF, 0x06E4},
    {0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0711, 0x0711}, {0x0730, 0x074A},
    {0x07A6, 0x07B0}, {0x0816, 0x0819}, {0x081B, 0x0823}, {0x0825, 0x0827},
    {0x0829, 0x082D}, {0x0859, 0x085B}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A},
    {0x0E47, 0x0E4E}, {0x0EB1, 0x0EB1}, {0x0EB4, 0x0EB9}, {0x0EBB, 0x0EBC},
    {0x0EC8, 0x0ECD}, {0x180B, 0x180D}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF},
    {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x2066, 0x206F},
    {0x20D0, 0x20FF}, {0x302A, 0x302F}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF}, {0xE0100, 0xE01EF},
};
#define CHARS_ZERO_WIDTH_COUNT (int)(sizeof(CHARS_ZERO_WIDTH) / sizeof(CHARS_ZERO_WIDTH[0]))

static const CharsWidthRange CHARS_WIDE[] = {
    {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
    {0xFE30, 0xFE4F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6},
    {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF},
    {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
};
#define CHARS_WIDE_COUNT (int)(sizeof(CHARS_WIDE) / sizeof(CHARS_WIDE[0]))

static bool chars_width_range_has(const CharsWidthRange *ranges, int count, int32_t codepoint) {
    int low = 0, hi = count;
    while (low < hi) {
        int middle = low + (hi - low) / 2;
        if (codepoint < ranges[middle].low) hi = middle;
        else if (codepoint > ranges[middle].high) low = middle + 1;
        else return true;
    }
    return false;
}

int32_t gray_chars_width(int32_t codepoint) {
    if (codepoint < 0x20 || codepoint == 0x7F) return -1;              /* C0 + DEL */
    if (codepoint >= 0x80 && codepoint <= 0x9F) return -1;             /* C1 */
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return -1;         /* surrogate: not a valid scalar value */
    if (chars_width_range_has(CHARS_ZERO_WIDTH, CHARS_ZERO_WIDTH_COUNT, codepoint)) return 0;
    if (chars_width_range_has(CHARS_WIDE, CHARS_WIDE_COUNT, codepoint)) return 2;
    return 1;
}

/* Decode the next UTF-8 codepoint; returns bytes consumed (1-4). Mirrors
 * builtins.c's utf8_next, duplicated here to keep chars.c self-contained. */
static int chars_utf8_next(const uint8_t *cursor, const uint8_t *end_cursor, int32_t *cp_out) {
    uint8_t lead_byte = *cursor;
    int32_t codepoint;
    int bytes;
    if (lead_byte < 0x80) { *cp_out = lead_byte; return 1; }
    else if ((lead_byte & 0xE0) == 0xC0) { codepoint = lead_byte & 0x1F; bytes = 2; }
    else if ((lead_byte & 0xF0) == 0xE0) { codepoint = lead_byte & 0x0F; bytes = 3; }
    else if ((lead_byte & 0xF8) == 0xF0) { codepoint = lead_byte & 0x07; bytes = 4; }
    else { *cp_out = 0xFFFD; return 1; }
    if (cursor + bytes > end_cursor) { *cp_out = 0xFFFD; return 1; }
    for (int i = 1; i < bytes; i++) {
        if ((cursor[i] & 0xC0) != 0x80) { *cp_out = 0xFFFD; return 1; }
        codepoint = (codepoint << 6) | (cursor[i] & 0x3F);
    }
    *cp_out = codepoint;
    return bytes;
}

int64_t gray_chars_string_width(GrayString string) {
    const uint8_t *cursor = (const uint8_t *)string.data;
    const uint8_t *end_cursor = cursor + string.len;
    int64_t total = 0;
    while (cursor < end_cursor) {
        int32_t codepoint;
        cursor += chars_utf8_next(cursor, end_cursor, &codepoint);
        int32_t width = gray_chars_width(codepoint);
        if (width > 0) total += width;
    }
    return total;
}

GrayString gray_chars_escape(GrayArena *arena, int32_t codepoint) {
    char buffer[16];
    int written_length = 0;
    switch (codepoint) {
    case '\\': buffer[written_length++] = '\\'; buffer[written_length++] = '\\'; break;
    case '\n': buffer[written_length++] = '\\'; buffer[written_length++] = 'n';  break;
    case '\t': buffer[written_length++] = '\\'; buffer[written_length++] = 't';  break;
    case '\r': buffer[written_length++] = '\\'; buffer[written_length++] = 'r';  break;
    case '\0': buffer[written_length++] = '\\'; buffer[written_length++] = '0';  break;
    default:
        if (codepoint >= 0x20 && codepoint < 0x7F) {
            buffer[written_length++] = (char)codepoint;
        } else if ((codepoint >= 0 && codepoint < 0x20) || codepoint == 0x7F) {
            written_length = snprintf(buffer, sizeof(buffer), "\\x%02X", (unsigned)codepoint);
        } else {
            written_length = snprintf(buffer, sizeof(buffer), "\\u{%X}", (unsigned)codepoint);
        }
        break;
    }
    return gray_string_new(arena, buffer, written_length);
}
