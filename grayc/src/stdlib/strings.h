/*
 * strings.h — Public interface for the strings stdlib module.
 * Declares case conversion, trimming, splitting, joining, searching,
 * replacing, padding, and character classification functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_STRINGS_H
#define GRAY_STRINGS_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

/* Basename collides with the POSIX <strings.h>: in a grayc-generated program
 * this directory shadows libc, so step past it so `extern import "strings.h"`
 * reaches the real header (on macOS those names also live in <string.h>, so
 * this is a no-op there). See math.h for the full rationale. */
#ifdef GRAY_GENERATED_C
#  ifdef __has_include_next
#    if __has_include_next(<strings.h>)
#      include_next <strings.h>
#    endif
#  endif
#endif

/*@man to_upper
 *@module strings
 *@group Case
 *@sig to_upper(s string) -> string
 *@desc Returns a copy of s with all ASCII letters converted to uppercase.
 *@example
 *   import @strings
 *   println(strings.to_upper("hello"))
 *@end
 */
GrayString gray_strings_to_upper(GrayArena *arena, GrayString s);

/*@man to_lower
 *@module strings
 *@group Case
 *@sig to_lower(s string) -> string
 *@desc Returns a copy of s with all ASCII letters converted to lowercase.
 *@example
 *   import @strings
 *   println(strings.to_lower("HELLO"))
 *@end
 */
GrayString gray_strings_to_lower(GrayArena *arena, GrayString s);

/*@man to_title
 *@module strings
 *@group Case
 *@sig to_title(s string) -> string
 *@desc Returns a copy of s with the first letter of each whitespace-separated word uppercased and the rest of each word lowercased.
 *@example
 *   import @strings
 *   println(strings.to_title("hello WORLD"))
 *@end
 */
GrayString gray_strings_to_title(GrayArena *arena, GrayString s);

/*@man to_snake_case
 *@module strings
 *@group Case
 *@sig to_snake_case(s string) -> string
 *@desc Converts camelCase, PascalCase, spaces, and hyphens to snake_case. Acronym runs are kept together, so "HTTPServer" becomes "http_server".
 *@example
 *   import @strings
 *   println(strings.to_snake_case("userIDValue"))
 *@end
 */
GrayString gray_strings_to_snake_case(GrayArena *arena, GrayString s);

/*@man to_camel_case
 *@module strings
 *@group Case
 *@sig to_camel_case(s string) -> string
 *@desc Converts snake_case, spaces, and hyphens to camelCase. The first word is lowercased; each following word is capitalized.
 *@example
 *   import @strings
 *   println(strings.to_camel_case("user_id_value"))
 *@end
 */
GrayString gray_strings_to_camel_case(GrayArena *arena, GrayString s);

/*@man trim
 *@module strings
 *@group Trim
 *@sig trim(s string) -> string
 *@desc Returns a copy of s with leading and trailing whitespace removed.
 *@example
 *   import @strings
 *   println(strings.trim("  hello  "))
 *@end
 */
GrayString gray_strings_trim(GrayArena *arena, GrayString s);

/*@man trim_left
 *@module strings
 *@group Trim
 *@sig trim_left(s string) -> string
 *@desc Returns a copy of s with leading whitespace removed.
 *@example
 *   import @strings
 *   println(strings.trim_left("  hello  "))
 *@end
 */
GrayString gray_strings_trim_left(GrayArena *arena, GrayString s);

/*@man trim_right
 *@module strings
 *@group Trim
 *@sig trim_right(s string) -> string
 *@desc Returns a copy of s with trailing whitespace removed.
 *@example
 *   import @strings
 *   println(strings.trim_right("  hello  "))
 *@end
 */
GrayString gray_strings_trim_right(GrayArena *arena, GrayString s);

/*@man contains
 *@module strings
 *@group Query
 *@sig contains(s string, sub string) -> bool
 *@desc Returns true if s contains the substring sub.
 *@example
 *   import @strings
 *   println(strings.contains("hello world", "world"))
 *   println(strings.contains("hello world", "xyz"))
 *@end
 */
bool gray_strings_contains(GrayString s, GrayString sub);

/*@man starts_with
 *@module strings
 *@group Query
 *@sig starts_with(s string, prefix string) -> bool
 *@desc Returns true if s starts with prefix.
 *@example
 *   import @strings
 *   println(strings.starts_with("hello", "hel"))
 *@end
 */
bool gray_strings_starts_with(GrayString s, GrayString prefix);

/*@man ends_with
 *@module strings
 *@group Query
 *@sig ends_with(s string, suffix string) -> bool
 *@desc Returns true if s ends with suffix.
 *@example
 *   import @strings
 *   println(strings.ends_with("hello", "llo"))
 *@end
 */
bool gray_strings_ends_with(GrayString s, GrayString suffix);

/*@man index_of
 *@module strings
 *@group Query
 *@sig index_of(s string, sub string) -> int
 *@desc Returns the byte index of the first occurrence of sub in s, or -1 if not found.
 *@example
 *   import @strings
 *   println(strings.index_of("hello world", "world"))
 *   println(strings.index_of("hello world", "xyz"))
 *@end
 */
int64_t gray_strings_index_of(GrayString s, GrayString sub);

/*@man last_index_of
 *@module strings
 *@group Query
 *@sig last_index_of(s string, sub string) -> int
 *@desc Returns the byte index of the last occurrence of sub in s, or -1 if not found.
 *@example
 *   import @strings
 *   println(strings.last_index_of("hello world hello", "hello"))
 *   println(strings.last_index_of("hello world", "xyz"))
 *@end
 */
int64_t gray_strings_last_index_of(GrayString s, GrayString sub);

/*@man count
 *@module strings
 *@group Query
 *@sig count(s string, sub string) -> int
 *@desc Returns the number of non-overlapping occurrences of sub in s.
 *@example
 *   import @strings
 *   println(strings.count("banana", "a"))
 *@end
 */
int64_t gray_strings_count(GrayString s, GrayString sub);

/*@man is_empty
 *@module strings
 *@group Query
 *@sig is_empty(s string) -> bool
 *@desc Returns true if s has zero length. Does not trim whitespace first.
 *@example
 *   import @strings
 *   println(strings.is_empty(""))
 *   println(strings.is_empty("hi"))
 *@end
 */
bool gray_strings_is_empty(GrayString s);

/*@man contains_any
 *@module strings
 *@group Query
 *@sig contains_any(s string, chars string) -> bool
 *@desc Returns true if any single character from chars appears in s. Returns false when chars is empty.
 *@example
 *   import @strings
 *   println(strings.contains_any("hello", "xyz!l"))
 *@end
 */
bool gray_strings_contains_any(GrayString s, GrayString chars);

/*@man equal_fold
 *@module strings
 *@group Query
 *@sig equal_fold(a string, b string) -> bool
 *@desc Returns true if a and b are equal ignoring ASCII letter case.
 *@example
 *   import @strings
 *   println(strings.equal_fold("Hello", "HELLO"))
 *@end
 */
bool gray_strings_equal_fold(GrayString a, GrayString b);

/*@man compare
 *@module strings
 *@group Query
 *@sig compare(a string, b string) -> int
 *@desc Compares a and b bytewise and returns -1 if a sorts before b, 1 if it sorts after, and 0 if they are equal.
 *@example
 *   import @strings
 *   println(strings.compare("apple", "banana"))
 *@end
 */
int64_t gray_strings_compare(GrayString a, GrayString b);

/*@man remove_prefix
 *@module strings
 *@group Transformation
 *@sig remove_prefix(s string, prefix string) -> string
 *@desc Returns s with the given prefix removed. If s does not start with prefix, it is returned unchanged.
 *@example
 *   import @strings
 *   println(strings.remove_prefix("hello world", "hello "))
 *   println(strings.remove_prefix("hello world", "xyz"))
 *@end
 */
GrayString gray_strings_remove_prefix(GrayArena *arena, GrayString s, GrayString prefix);

/*@man remove_suffix
 *@module strings
 *@group Transformation
 *@sig remove_suffix(s string, suffix string) -> string
 *@desc Returns s with the given suffix removed. If s does not end with suffix, it is returned unchanged.
 *@example
 *   import @strings
 *   println(strings.remove_suffix("hello world", " world"))
 *   println(strings.remove_suffix("hello world", "xyz"))
 *@end
 */
GrayString gray_strings_remove_suffix(GrayArena *arena, GrayString s, GrayString suffix);

/*@man replace
 *@module strings
 *@group Transformation
 *@sig replace(s string, old string, new string) -> string
 *@desc Returns a copy of s with all occurrences of old replaced by new.
 *@example
 *   import @strings
 *   println(strings.replace("hello world", "world", "Grayscale"))
 *@end
 */
GrayString gray_strings_replace(GrayArena *arena, GrayString s, GrayString old_s, GrayString new_s);

/*@man repeat
 *@module strings
 *@group Transformation
 *@sig repeat(s string, count int) -> string
 *@desc Returns a string consisting of count copies of s concatenated together.
 *@example
 *   import @strings
 *   println(strings.repeat("ab", 3))
 *@end
 */
GrayString gray_strings_repeat(GrayArena *arena, GrayString s, int64_t count);

/*@man reverse
 *@module strings
 *@group Transformation
 *@sig reverse(s string) -> string
 *@desc Returns a copy of s with the bytes in reverse order.
 *@example
 *   import @strings
 *   println(strings.reverse("hello"))
 *@end
 */
GrayString gray_strings_reverse(GrayArena *arena, GrayString s);

/*@man slice
 *@module strings
 *@group Transformation
 *@sig slice(s string, start int, end int) -> string
 *@desc Returns the substring of s from byte index start (inclusive) to end (exclusive).
 *@example
 *   import @strings
 *   println(strings.slice("hello world", 6, 11))
 *@end
 */
GrayString gray_strings_slice(GrayArena *arena, GrayString s, int64_t start, int64_t end);

/*@man split
 *@module strings
 *@group Split/Join
 *@sig split(s string, sep string) -> [string]
 *@desc Splits s around each occurrence of sep and returns a string array.
 *@example
 *   import @strings
 *   mut parts = strings.split("a,b,c", ",")
 *   println(parts[0])
 *@end
 */
GrayArray gray_strings_split(GrayArena *arena, GrayString s, GrayString sep);

/*@man split_whitespace
 *@module strings
 *@group Split/Join
 *@sig split_whitespace(s string) -> [string]
 *@desc Splits s on runs of whitespace, discarding empty pieces. Leading and trailing whitespace is ignored, so a blank string yields an empty array.
 *@example
 *   import @strings
 *   println(strings.split_whitespace("  one   two \n three "))
 *@end
 */
GrayArray gray_strings_split_whitespace(GrayArena *arena, GrayString s);

/*@man split_n
 *@module strings
 *@group Split/Join
 *@sig split_n(s string, sep string, n int) -> [string]
 *@desc Splits s on sep into at most n pieces; the final piece holds the unsplit remainder. Returns an empty array when n is zero or negative.
 *@example
 *   import @strings
 *   println(strings.split_n("a=b=c", "=", 2))
 *@end
 */
GrayArray gray_strings_split_n(GrayArena *arena, GrayString s, GrayString sep, int64_t n);

/*@man join
 *@module strings
 *@group Split/Join
 *@sig join(arr [string], sep string) -> string
 *@desc Joins the elements of arr into a single string with sep between each element.
 *@example
 *   import @strings
 *   mut parts = strings.split("a,b,c", ",")
 *   println(strings.join(parts, "-"))
 *@end
 */
GrayString gray_strings_join(GrayArena *arena, GrayArray arr, GrayString sep);

/*@man char_at
 *@module strings
 *@group Access
 *@sig char_at(s string, index int) -> char
 *@desc Returns the character at the given byte index. Panics if the index is out of bounds.
 *@example
 *   import @strings
 *   println(strings.char_at("hello", 0))
 *   println(strings.char_at("hello", 4))
 *@end
 */
char gray_strings_char_at(GrayString s, int64_t index);

/*@man append_char
 *@module strings
 *@group Editing
 *@sig append_char(s string, c char) -> string
 *@desc Returns a new string with c (UTF-8 encoded) added at the end. s is unchanged.
 *@example
 *   import @strings
 *   println(strings.append_char("hell", 'o'))
 *@end
 */
GrayString gray_strings_append_char(GrayArena *arena, GrayString s, int32_t c);

/*@man prepend_char
 *@module strings
 *@group Editing
 *@sig prepend_char(s string, c char) -> string
 *@desc Returns a new string with c (UTF-8 encoded) added at the front. s is unchanged.
 *@example
 *   import @strings
 *   println(strings.prepend_char("ello", 'h'))
 *@end
 */
GrayString gray_strings_prepend_char(GrayArena *arena, GrayString s, int32_t c);

/*@man insert_char_at
 *@module strings
 *@group Editing
 *@sig insert_char_at(s string, index int, c char) -> string
 *@desc Returns a new string with c (UTF-8 encoded) inserted at byte index. An index equal to the length appends; panics if the index is negative or greater than the length. s is unchanged.
 *@example
 *   import @strings
 *   println(strings.insert_char_at("helo", 3, 'l'))
 *@end
 */
GrayString gray_strings_insert_char_at(GrayArena *arena, GrayString s, int64_t index, int32_t c);

/*@man remove_at
 *@module strings
 *@group Editing
 *@sig remove_at(s string, index int) -> string
 *@desc Returns a new string with the byte at byte index removed. Panics if the index is out of bounds. s is unchanged.
 *@example
 *   import @strings
 *   println(strings.remove_at("hello!", 5))
 *@end
 */
GrayString gray_strings_remove_at(GrayArena *arena, GrayString s, int64_t index);

/*@man set_char_at
 *@module strings
 *@group Editing
 *@sig set_char_at(s string, index int, c char) -> string
 *@desc Returns a new string with the byte at byte index replaced by c (UTF-8 encoded). Panics if the index is out of bounds. s is unchanged.
 *@example
 *   import @strings
 *   println(strings.set_char_at("hello", 0, 'H'))
 *@end
 */
GrayString gray_strings_set_char_at(GrayArena *arena, GrayString s, int64_t index, int32_t c);

/*@man to_chars
 *@module strings
 *@group Conversion
 *@sig to_chars(s string) -> [char]
 *@desc Converts a string to an array of its individual characters.
 *@example
 *   import @strings
 *   mut chars [char] = strings.to_chars("hello")
 *   println(chars[0])
 *@end
 */
GrayArray gray_strings_to_chars(GrayArena *arena, GrayString s);

/*@man from_chars
 *@module strings
 *@group Conversion
 *@sig from_chars(chars [char]) -> string
 *@desc Converts an array of characters back into a string.
 *@example
 *   import @strings
 *   mut chars [char] = strings.to_chars("hello")
 *   mut s string = strings.from_chars(chars)
 *   println(s)
 *@end
 */
GrayString gray_strings_from_chars(GrayArena *arena, GrayArray *chars);

/*@man is_alpha
 *@module strings
 *@group Classification
 *@sig is_alpha(c char) -> bool
 *@desc Returns true if c is an ASCII letter (a-z or A-Z).
 *@example
 *   import @strings
 *   println(strings.is_alpha('a'))
 *   println(strings.is_alpha('1'))
 *@end
 */
bool gray_strings_is_alpha(char c);

/*@man is_digit
 *@module strings
 *@group Classification
 *@sig is_digit(c char) -> bool
 *@desc Returns true if c is a decimal digit (0-9).
 *@example
 *   import @strings
 *   println(strings.is_digit('5'))
 *   println(strings.is_digit('a'))
 *@end
 */
bool gray_strings_is_digit(char c);

/*@man is_alnum
 *@module strings
 *@group Classification
 *@sig is_alnum(c char) -> bool
 *@desc Returns true if c is an ASCII letter or decimal digit.
 *@example
 *   import @strings
 *   println(strings.is_alnum('a'))
 *   println(strings.is_alnum('3'))
 *   println(strings.is_alnum('!'))
 *@end
 */
bool gray_strings_is_alnum(char c);

/*@man is_whitespace
 *@module strings
 *@group Classification
 *@sig is_whitespace(c char) -> bool
 *@desc Returns true if c is a whitespace character (space, tab, newline, carriage return).
 *@example
 *   import @strings
 *   println(strings.is_whitespace(' '))
 *   println(strings.is_whitespace('a'))
 *@end
 */
bool gray_strings_is_whitespace(char c);

/*@man is_upper
 *@module strings
 *@group Classification
 *@sig is_upper(c char) -> bool
 *@desc Returns true if c is an uppercase ASCII letter (A-Z).
 *@example
 *   import @strings
 *   println(strings.is_upper('A'))
 *   println(strings.is_upper('a'))
 *@end
 */
bool gray_strings_is_upper(char c);

/*@man is_lower
 *@module strings
 *@group Classification
 *@sig is_lower(c char) -> bool
 *@desc Returns true if c is a lowercase ASCII letter (a-z).
 *@example
 *   import @strings
 *   println(strings.is_lower('a'))
 *   println(strings.is_lower('A'))
 *@end
 */
bool gray_strings_is_lower(char c);

/* --- Builder: amortized string assembly --- */

/*@man Builder
 *@module strings
 *@group Builder
 *@kind type
 *@desc A growable byte accumulator for assembling a string from many pieces without the O(n^2) copying of repeated `+=`. Arena-managed like any struct: it goes out of scope with its enclosing arena, with no manual free. The buffer grows by doubling, so N appends totalling M bytes cost amortized O(M). Create one with strings.builder(), append with the builder_append* functions, and finalize once with strings.build(). Must be declared mut; a function that appends to a caller's builder takes it as &b Builder.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append(b, "Hello, ")
 *   strings.builder_append(b, "world")
 *   println(strings.build(b))
 *@end
 */
typedef struct {
    char *data;
    int32_t len;
    int32_t cap;
    GrayArena *arena;   /* arena the buffer grows into */
} GrayStringsBuilder;

/*@man builder
 *@module strings
 *@group Builder
 *@sig builder() -> Builder
 *@desc Creates an empty string builder. The buffer grows on demand as content is appended.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append(b, "text")
 *@end
 */
GrayStringsBuilder *gray_strings_builder(GrayArena *arena);

/*@man builder_reserve
 *@module strings
 *@group Builder
 *@sig builder_reserve(b Builder, n int) -> void
 *@desc Grows the builder's buffer so it can hold at least n bytes without reallocating. Optional; use it when the final size is known ahead of time. A negative n is ignored.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_reserve(b, 4096)
 *@end
 */
void gray_strings_builder_reserve(GrayStringsBuilder *b, int64_t n);

/*@man builder_append
 *@module strings
 *@group Builder
 *@sig builder_append(b Builder, s string) -> void
 *@desc Appends the bytes of s to the builder.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append(b, "row")
 *@end
 */
void gray_strings_builder_append(GrayStringsBuilder *b, GrayString s);

/*@man builder_append_char
 *@module strings
 *@group Builder
 *@sig builder_append_char(b Builder, c char) -> void
 *@desc Appends the codepoint c to the builder, UTF-8 encoded (1 to 4 bytes).
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append_char(b, '\n')
 *@end
 */
void gray_strings_builder_append_char(GrayStringsBuilder *b, int32_t c);

/*@man builder_append_bytes
 *@module strings
 *@group Builder
 *@sig builder_append_bytes(b Builder, data [byte]) -> void
 *@desc Appends every byte of data to the builder.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   mut data [byte] = {104, 105}
 *   strings.builder_append_bytes(b, data)
 *@end
 */
void gray_strings_builder_append_bytes(GrayStringsBuilder *b, GrayArray data);

/*@man builder_append_int
 *@module strings
 *@group Builder
 *@sig builder_append_int(b Builder, n int) -> void
 *@desc Appends the decimal text of n to the builder (e.g. -42 appends "-42").
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append_int(b, 2637)
 *@end
 */
void gray_strings_builder_append_int(GrayStringsBuilder *b, int64_t n);

/*@man builder_append_line
 *@module strings
 *@group Builder
 *@sig builder_append_line(b Builder, s string) -> void
 *@desc Appends s followed by a newline character.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append_line(b, "first")
 *   strings.builder_append_line(b, "second")
 *@end
 */
void gray_strings_builder_append_line(GrayStringsBuilder *b, GrayString s);

/*@man builder_len
 *@module strings
 *@group Builder
 *@sig builder_len(b Builder) -> int
 *@desc Returns the number of bytes accumulated in the builder so far.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append(b, "abc")
 *   println(strings.builder_len(b))
 *@end
 */
int64_t gray_strings_builder_len(GrayStringsBuilder *b);

/*@man builder_clear
 *@module strings
 *@group Builder
 *@sig builder_clear(b Builder) -> void
 *@desc Resets the builder's length to zero while keeping its allocated capacity, so it can be reused without reallocating.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append(b, "old")
 *   strings.builder_clear(b)
 *@end
 */
void gray_strings_builder_clear(GrayStringsBuilder *b);

/*@man build
 *@module strings
 *@group Builder
 *@sig build(b Builder) -> string
 *@desc Copies the accumulated bytes into a new string. The builder stays usable afterward; call build again for an updated snapshot.
 *@example
 *   import @strings
 *   mut b Builder = strings.builder()
 *   strings.builder_append(b, "done")
 *   mut out string = strings.build(b)
 *@end
 */
GrayString gray_strings_build(GrayArena *arena, GrayStringsBuilder *b);

#endif
