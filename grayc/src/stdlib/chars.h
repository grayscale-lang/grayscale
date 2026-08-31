/*
 * chars.h — Public interface for the chars stdlib module.
 * Scalar operations on a single `char`: ASCII case folding that
 * leaves non-letters and non-ASCII bytes untouched.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_CHARS_H
#define GRAY_CHARS_H

#include "../runtime/runtime.h"

/*@man to_upper
 *@module chars
 *@group Case
 *@sig to_upper(c char) -> char
 *@desc Returns the ASCII uppercase form of c. Anything that is not a lowercase ASCII letter — digits, symbols, whitespace, and non-ASCII codepoints — is returned unchanged. Never fails.
 *@example
 *   import @chars
 *   println(chars.to_upper('a'))   // 'A'
 *   println(chars.to_upper('5'))   // '5'
 *@end
 */
int32_t gray_chars_to_upper(int32_t c);

/*@man to_lower
 *@module chars
 *@group Case
 *@sig to_lower(c char) -> char
 *@desc Returns the ASCII lowercase form of c. Anything that is not an uppercase ASCII letter — digits, symbols, whitespace, and non-ASCII codepoints — is returned unchanged. Never fails.
 *@example
 *   import @chars
 *   println(chars.to_lower('Z'))   // 'z'
 *   println(chars.to_lower('#'))   // '#'
 *@end
 */
int32_t gray_chars_to_lower(int32_t c);

#endif
