/*
 * strconv.h — Public interface for the strconv stdlib module.
 * Declares string-to-numeric and numeric-to-string conversion
 * functions with panicking and result-returning variants.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_STRCONV_H
#define GRAY_STRCONV_H

#include "../runtime/runtime.h"
#include "io.h" /* GrayResult_string */

/* Result types for fallible conversions */
#ifndef GRAY_RESULT_BOOL_DEFINED
#define GRAY_RESULT_BOOL_DEFINED
typedef struct { bool v0; GrayError *v1; } GrayResult_bool;
#endif
typedef struct { uint64_t v0; GrayError *v1; } GrayResult_uint;
typedef struct { double v0; GrayError *v1; } GrayResult_float;

/*@man to_int
 *@module strconv
 *@group Parsing
 *@sig to_int(s string, base int = 10) -> (int, Error)
 *@desc Parses s as a signed integer in the given base (2–36). With single-var assignment, panics on invalid input. With destructuring, returns an Error instead. Leading/trailing whitespace is not tolerated.
 *@example
 *   import @strconv
 *   mut n int = strconv.to_int("42")
 *   mut val, err = strconv.to_int("ff", strconv.BASE_16)
 *@end
 */
/* Panicking conversions (single-var assignment) */
int64_t gray_strconv_to_int(GrayString s, int base);

/*@man to_uint
 *@module strconv
 *@group Parsing
 *@sig to_uint(s string, base int = 10) -> (uint, Error)
 *@desc Parses s as an unsigned integer in the given base (2–36). Rejects strings with a leading minus sign. With single-var assignment, panics on invalid input. With destructuring, returns an Error instead.
 *@example
 *   import @strconv
 *   mut n uint = strconv.to_uint("255")
 *   mut val, err = strconv.to_uint("ff", strconv.BASE_16)
 *@end
 */
uint64_t gray_strconv_to_uint(GrayString s, int base);

/*@man to_float
 *@module strconv
 *@group Parsing
 *@sig to_float(s string) -> (float, Error)
 *@desc Parses s as a floating-point number. Accepts standard decimal notation and "inf", "infinity", "nan" (case-insensitive). With single-var assignment, panics on invalid input. With destructuring, returns an Error instead.
 *@example
 *   import @strconv
 *   mut f float = strconv.to_float("3.14")
 *   mut val, err = strconv.to_float("not a number")
 *@end
 */
double gray_strconv_to_float(GrayString s);

/*@man to_bool
 *@module strconv
 *@group Parsing
 *@sig to_bool(s string) -> (bool, Error)
 *@desc Parses s as a boolean. Accepts "true" and "false" (case-insensitive). All other strings produce an error or panic. With single-var assignment, panics on invalid input. With destructuring, returns an Error instead.
 *@example
 *   import @strconv
 *   mut b bool = strconv.to_bool("true")
 *   mut val, err = strconv.to_bool("yes")
 *@end
 */
bool gray_strconv_to_bool(GrayString s);

/* Fallible conversions (multi-var destructuring) */
GrayResult_int gray_strconv_to_int_result(GrayString s, int base);
GrayResult_uint gray_strconv_to_uint_result(GrayString s, int base);
GrayResult_float gray_strconv_to_float_result(GrayString s);
GrayResult_bool gray_strconv_to_bool_result(GrayString s);

/*@man from_int
 *@module strconv
 *@group Formatting
 *@sig from_int(n int) -> string
 *@desc Converts an integer to its decimal string representation. Never fails.
 *@example
 *   import @strconv
 *   println(strconv.from_int(42))
 *@end
 */
/* Format a double using the shortest representation that round-trips.
 * Used by both builtins (to_string) and strconv (from_float). */
int gray_fmt_shortest_float(char *buf, size_t buffer_size, double v);

/* Type to string conversions */
GrayString gray_strconv_from_int(GrayArena *arena, int64_t n);

/*@man from_uint
 *@module strconv
 *@group Formatting
 *@sig from_uint(n uint) -> string
 *@desc Converts an unsigned integer to its decimal string representation. Never fails.
 *@example
 *   import @strconv
 *   println(strconv.from_uint(255))
 *@end
 */
GrayString gray_strconv_from_uint(GrayArena *arena, uint64_t n);

/*@man format_int
 *@module strconv
 *@group Formatting
 *@sig format_int(n int, base int) -> string
 *@desc Converts a signed integer to its string representation in the given base (2–36). Negative values are prefixed with '-'. Digits above 9 use lowercase letters 'a'–'z'. Panics if base is out of range. Never fails otherwise.
 *@example
 *   import @strconv
 *   println(strconv.format_int(255, 16))
 *   println(strconv.format_int(-10, 2))
 *@end
 */
GrayString gray_strconv_format_int(GrayArena *arena, int64_t n, int64_t base);

/*@man format_uint
 *@module strconv
 *@group Formatting
 *@sig format_uint(n uint, base int) -> string
 *@desc Converts an unsigned integer to its string representation in the given base (2–36). Digits above 9 use lowercase letters 'a'–'z'. Panics if base is out of range. Never fails otherwise.
 *@example
 *   import @strconv
 *   println(strconv.format_uint(255, 16))
 *@end
 */
GrayString gray_strconv_format_uint(GrayArena *arena, uint64_t n, int64_t base);

/*@man from_float
 *@module strconv
 *@group Formatting
 *@sig from_float(f float) -> string
 *@desc Converts a float to its shortest accurate string representation. Never fails.
 *@example
 *   import @strconv
 *   println(strconv.from_float(3.14))
 *@end
 */
GrayString gray_strconv_from_float(GrayArena *arena, double f);

/*@man from_bool
 *@module strconv
 *@group Formatting
 *@sig from_bool(b bool) -> string
 *@desc Converts a boolean to "true" or "false". Never fails.
 *@example
 *   import @strconv
 *   println(strconv.from_bool(true))
 *@end
 */
GrayString gray_strconv_from_bool(bool b);

/*@man quote
 *@module strconv
 *@group Quoting
 *@sig quote(s string) -> string
 *@desc Returns s wrapped in double quotes with backslash, double-quote, newline, carriage return, and tab escaped, and other control bytes emitted as hex escapes. The inverse of unquote. Never fails.
 *@example
 *   import @strconv
 *   println(strconv.quote(raw_text))
 *@end
 */
GrayString gray_strconv_quote(GrayArena *arena, GrayString s);

/*@man unquote
 *@module strconv
 *@group Quoting
 *@sig unquote(s string) -> (string, Error)
 *@desc Removes one layer of surrounding double quotes from s and interprets the same escape sequences the lexer accepts in string literals, including two-digit hex escapes. With single-var assignment, panics on malformed input. With destructuring, returns an Error instead. The inverse of quote.
 *@example
 *   import @strconv
 *   mut s, err = strconv.unquote(quoted)
 *@end
 */
GrayString gray_strconv_unquote(GrayArena *arena, GrayString s);
GrayResult_string gray_strconv_unquote_result(GrayArena *arena, GrayString s);

/*@man is_numeric
 *@module strconv
 *@group Query
 *@sig is_numeric(s string) -> bool
 *@desc Returns true if s is a valid numeric representation (integer or decimal). Accepts an optional leading sign. Does not accept scientific notation, hex prefixes, or whitespace.
 *@example
 *   import @strconv
 *   println(strconv.is_numeric("3.14"))
 *   println(strconv.is_numeric("abc"))
 *@end
 */
/* Query functions */
bool gray_strconv_is_numeric(GrayString s);

/*@man is_integer
 *@module strconv
 *@group Query
 *@sig is_integer(s string) -> bool
 *@desc Returns true if s is a valid integer (digits only, optional leading sign). Does not validate whether the value fits in an int or uint.
 *@example
 *   import @strconv
 *   println(strconv.is_integer("42"))
 *   println(strconv.is_integer("3.14"))
 *@end
 */
bool gray_strconv_is_integer(GrayString s);

/*@man BASE_2
 *@module strconv
 *@group Constants
 *@kind const
 *@sig 2
 *@desc Base constant for binary. Pass as the base argument to to_int(), to_uint(), format_int(), or format_uint().
 *@end
 */

/*@man BASE_8
 *@module strconv
 *@group Constants
 *@kind const
 *@sig 8
 *@desc Base constant for octal. Pass as the base argument to to_int(), to_uint(), format_int(), or format_uint().
 *@end
 */

/*@man BASE_10
 *@module strconv
 *@group Constants
 *@kind const
 *@sig 10
 *@desc Base constant for decimal (the default for to_int()/to_uint()). Pass as the base argument to to_int(), to_uint(), format_int(), or format_uint().
 *@end
 */

/*@man BASE_16
 *@module strconv
 *@group Constants
 *@kind const
 *@sig 16
 *@desc Base constant for hexadecimal. Pass as the base argument to to_int(), to_uint(), format_int(), or format_uint().
 *@end
 */

/*@man BASE_36
 *@module strconv
 *@group Constants
 *@kind const
 *@sig 36
 *@desc Base constant for base-36 (digits 0-9 and letters A-Z). Pass as the base argument to to_int(), to_uint(), format_int(), or format_uint().
 *@end
 */

#endif
