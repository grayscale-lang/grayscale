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

GrayString gray_chars_escape(GrayArena *arena, int32_t codepoint) {
    char buf[16];
    int n = 0;
    switch (codepoint) {
    case '\\': buf[n++] = '\\'; buf[n++] = '\\'; break;
    case '\n': buf[n++] = '\\'; buf[n++] = 'n';  break;
    case '\t': buf[n++] = '\\'; buf[n++] = 't';  break;
    case '\r': buf[n++] = '\\'; buf[n++] = 'r';  break;
    case '\0': buf[n++] = '\\'; buf[n++] = '0';  break;
    default:
        if (codepoint >= 0x20 && codepoint < 0x7F) {
            buf[n++] = (char)codepoint;
        } else if ((codepoint >= 0 && codepoint < 0x20) || codepoint == 0x7F) {
            n = snprintf(buf, sizeof(buf), "\\x%02X", (unsigned)codepoint);
        } else {
            n = snprintf(buf, sizeof(buf), "\\u{%X}", (unsigned)codepoint);
        }
        break;
    }
    return gray_string_new(arena, buf, n);
}
