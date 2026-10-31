/*
 * utf8.h — The one strict UTF-8 decoder shared by the compiler and the runtime.
 *
 * Only well-formed sequences decode to their codepoint: overlong forms,
 * surrogates (U+D800 to U+DFFF) and values above U+10FFFF are invalid. Every
 * invalid byte decodes as one U+FFFD and consumes exactly one byte.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_UTF8_H
#define GRAY_UTF8_H

#include <stdint.h>

/* Decode the character at `cursor` (which must be < `end_cursor`). Returns the
 * bytes consumed (1-4) and writes the codepoint, or 0xFFFD for an invalid byte,
 * to *codepoint_out. */
static inline int gray_utf8_decode(const uint8_t *cursor, const uint8_t *end_cursor, int32_t *codepoint_out) {
    uint8_t lead_byte = *cursor;
    int32_t codepoint;
    int bytes;
    uint8_t second_min = 0x80;
    uint8_t second_max = 0xBF;
    if (lead_byte < 0x80) {
        *codepoint_out = lead_byte;
        return 1;
    } else if (lead_byte >= 0xC2 && lead_byte <= 0xDF) {
        codepoint = lead_byte & 0x1F; bytes = 2;
    } else if (lead_byte >= 0xE0 && lead_byte <= 0xEF) {
        codepoint = lead_byte & 0x0F; bytes = 3;
        if (lead_byte == 0xE0) second_min = 0xA0;
        if (lead_byte == 0xED) second_max = 0x9F;
    } else if (lead_byte >= 0xF0 && lead_byte <= 0xF4) {
        codepoint = lead_byte & 0x07; bytes = 4;
        if (lead_byte == 0xF0) second_min = 0x90;
        if (lead_byte == 0xF4) second_max = 0x8F;
    } else {
        *codepoint_out = 0xFFFD;
        return 1;
    }
    if (end_cursor - cursor < bytes) { *codepoint_out = 0xFFFD; return 1; }
    if (cursor[1] < second_min || cursor[1] > second_max) { *codepoint_out = 0xFFFD; return 1; }
    codepoint = (codepoint << 6) | (cursor[1] & 0x3F);
    for (int index = 2; index < bytes; index++) {
        if ((cursor[index] & 0xC0) != 0x80) { *codepoint_out = 0xFFFD; return 1; }
        codepoint = (codepoint << 6) | (cursor[index] & 0x3F);
    }
    *codepoint_out = codepoint;
    return bytes;
}

#endif /* GRAY_UTF8_H */
