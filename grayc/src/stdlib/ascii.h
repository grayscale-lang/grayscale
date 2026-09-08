/*
 * ascii.h — Branchless ASCII case + classification helpers.
 *
 * The strings module operates on bytes and is documented as ASCII-only
 * ("all ASCII letters", "ASCII letter case"). The runtime never calls
 * setlocale, so <ctype.h>'s toupper/isspace/... run in the C locale, where
 * they already transform only a-z / A-Z and leave bytes >= 128 untouched.
 * These helpers are exactly that behaviour, but as inlinable, branchless
 * expressions the C compiler can vectorize — a per-byte call into the
 * locale machinery on Darwin costs ~1.4 ns/byte, this costs ~0.02.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_ASCII_H
#define GRAY_ASCII_H

#include <stdbool.h>

/* Takes and returns an unsigned byte value (0-255). */
static inline unsigned char gray_ascii_upper(unsigned char c) {
    return (c >= 'a' && c <= 'z') ? (unsigned char)(c - 32) : c;
}

static inline unsigned char gray_ascii_lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

/* isspace() in the C locale: space plus the contiguous run \t \n \v \f \r. */
static inline bool gray_ascii_is_space(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static inline bool gray_ascii_is_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
static inline bool gray_ascii_is_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }
static inline bool gray_ascii_is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static inline bool gray_ascii_is_alpha(unsigned char c) {
    return gray_ascii_is_upper(c) || gray_ascii_is_lower(c);
}

static inline bool gray_ascii_is_alnum(unsigned char c) {
    return gray_ascii_is_alpha(c) || gray_ascii_is_digit(c);
}

#endif /* GRAY_ASCII_H */
