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

int32_t gray_chars_to_upper(int32_t codepoint) {
    return (codepoint >= 'a' && codepoint <= 'z') ? codepoint - ('a' - 'A') : codepoint;
}

int32_t gray_chars_to_lower(int32_t codepoint) {
    return (codepoint >= 'A' && codepoint <= 'Z') ? codepoint + ('a' - 'A') : codepoint;
}
