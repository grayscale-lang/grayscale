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
int32_t gray_chars_to_upper(int32_t codepoint);

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
int32_t gray_chars_to_lower(int32_t codepoint);

/*@man is_ascii
 *@module chars
 *@group Classification
 *@sig is_ascii(c char) -> bool
 *@desc True when c is a 7-bit ASCII codepoint (0 through 127). Never fails.
 *@example
 *   import @chars
 *   println(chars.is_ascii('A'))       // true
 *   println(chars.is_ascii('é'))       // false
 *@end
 */
bool gray_chars_is_ascii(int32_t codepoint);

/*@man is_control
 *@module chars
 *@group Classification
 *@sig is_control(c char) -> bool
 *@desc True when c is an ASCII control character: the C0 range (0 through 31) or DEL (127). Never fails.
 *@example
 *   import @chars
 *   println(chars.is_control('\n'))    // true
 *   println(chars.is_control('a'))     // false
 *@end
 */
bool gray_chars_is_control(int32_t codepoint);

/*@man is_printable
 *@module chars
 *@group Classification
 *@sig is_printable(c char) -> bool
 *@desc True when c is a printable ASCII character, space (32) through tilde (126). Non-ASCII codepoints are not classified as printable. Never fails.
 *@example
 *   import @chars
 *   println(chars.is_printable(' '))   // true
 *   println(chars.is_printable('\t'))  // false
 *@end
 */
bool gray_chars_is_printable(int32_t codepoint);

/*@man is_punct
 *@module chars
 *@group Classification
 *@sig is_punct(c char) -> bool
 *@desc True when c is an ASCII punctuation or symbol character: printable, but neither a letter, a digit, nor a space. Never fails.
 *@example
 *   import @chars
 *   println(chars.is_punct('!'))       // true
 *   println(chars.is_punct('a'))       // false
 *@end
 */
bool gray_chars_is_punct(int32_t codepoint);

/*@man is_hex_digit
 *@module chars
 *@group Classification
 *@sig is_hex_digit(c char) -> bool
 *@desc True when c is a hexadecimal digit: 0 through 9, a through f, or A through F. Never fails.
 *@example
 *   import @chars
 *   println(chars.is_hex_digit('F'))   // true
 *   println(chars.is_hex_digit('g'))   // false
 *@end
 */
bool gray_chars_is_hex_digit(int32_t codepoint);

/*@man is_word_char
 *@module chars
 *@group Classification
 *@sig is_word_char(c char) -> bool
 *@desc True when c is an ASCII identifier character: a letter, a digit, or an underscore. Matches the regex class [A-Za-z0-9_]. Never fails.
 *@example
 *   import @chars
 *   println(chars.is_word_char('_'))   // true
 *   println(chars.is_word_char('-'))   // false
 *@end
 */
bool gray_chars_is_word_char(int32_t codepoint);

/*@man escape
 *@module chars
 *@group Transform
 *@sig escape(c char) -> string
 *@desc Returns a printable rendering of c for debugging or codegen. Backslash and the common control characters render as two-character escapes; other control characters and DEL render as hex escapes; non-ASCII codepoints render as braced Unicode escapes; printable ASCII is returned unchanged. Never fails.
 *@example
 *   import @chars
 *   println(chars.escape('\t'))        // \t
 *   println(chars.escape('A'))         // A
 *@end
 */
GrayString gray_chars_escape(GrayArena *arena, int32_t codepoint);

#endif
