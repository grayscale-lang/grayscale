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
static inline unsigned char gray_ascii_upper(unsigned char character) {
    return (character >= 'a' && character <= 'z') ? (unsigned char)(character - 32) : character;
}

static inline unsigned char gray_ascii_lower(unsigned char character) {
    return (character >= 'A' && character <= 'Z') ? (unsigned char)(character + 32) : character;
}

/* isspace() in the C locale: space plus the contiguous run \t \n \v \f \r. */
static inline bool gray_ascii_is_space(unsigned char character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

static inline bool gray_ascii_is_upper(unsigned char character) { return character >= 'A' && character <= 'Z'; }
static inline bool gray_ascii_is_lower(unsigned char character) { return character >= 'a' && character <= 'z'; }
static inline bool gray_ascii_is_digit(unsigned char character) { return character >= '0' && character <= '9'; }

static inline bool gray_ascii_is_alpha(unsigned char character) {
    return gray_ascii_is_upper(character) || gray_ascii_is_lower(character);
}

static inline bool gray_ascii_is_alnum(unsigned char character) {
    return gray_ascii_is_alpha(character) || gray_ascii_is_digit(character);
}

#endif /* GRAY_ASCII_H */
