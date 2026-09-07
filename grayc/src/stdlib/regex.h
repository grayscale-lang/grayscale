/*
 * regex.h — Public interface for the regex stdlib module.
 * Declares match, find, find_all, replace, and split operations
 * using POSIX extended regular expressions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_REGEX_H
#define GRAY_REGEX_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include "io.h" /* GrayResult_string, GrayResult_array */

/* Basename collides with the POSIX <regex.h>: in a grayc-generated program
 * this directory shadows libc, so step past it so `extern import "regex.h"`
 * reaches the real header. See math.h for the full rationale. */
#ifdef GRAY_GENERATED_C
#  ifdef __has_include_next
#    if __has_include_next(<regex.h>)
#      include_next <regex.h>
#    endif
#  endif
#endif

/*@man is_valid
 *@module regex
 *@group Matching
 *@sig is_valid(pattern string) -> bool
 *@desc Check whether a regex pattern is syntactically valid.
 *@example
 *   import @regex
 *   if regex.is_valid("[a-z]+") { println("valid") }
 *@end
 */
/* regex.is_valid(pattern) -> bool */
bool gray_regex_is_valid(GrayString pattern);

/*@man is_match
 *@module regex
 *@group Matching
 *@sig is_match(pattern string, text string) -> bool
 *@desc Test whether the pattern matches anywhere in text.
 *@example
 *   import @regex
 *   if regex.is_match("[0-9]+", "abc123") { println("found digits") }
 *@end
 */
/* regex.match(pattern, text) -> bool */
bool gray_regex_match(GrayString pattern, GrayString text);

/*@man find
 *@module regex
 *@group Search
 *@sig find(pattern string, text string) -> (string, Error)
 *@desc Return the first match of pattern in text, or an empty string if none. Always use destructuring (`mut m, err = ...` or `mut m, _ = ...`) — single-variable assignment is a compile error. An invalid pattern yields a non-nil error and, with `_`, an empty string.
 *@example
 *   import @regex
 *   mut m, err = regex.find("[0-9]+", "abc123def")
 *   println(m)
 *@end
 */
/* regex.find(pattern, text) -> string (first match, or empty) */
GrayString gray_regex_find(GrayArena *arena, GrayString pattern, GrayString text);

/*@man find_all
 *@module regex
 *@group Search
 *@sig find_all(pattern string, text string) -> ([string], Error)
 *@desc Return all non-overlapping matches of pattern in text. Always use destructuring (`mut matches, err = ...` or `mut matches, _ = ...`) — single-variable assignment is a compile error. An invalid pattern yields a non-nil error and, with `_`, an empty array.
 *@example
 *   import @regex
 *   mut matches, err = regex.find_all("[0-9]+", "a1b2c3")
 *@end
 */
/* regex.find_all(pattern, text) -> [string] */
GrayArray gray_regex_find_all(GrayArena *arena, GrayString pattern, GrayString text);

/*@man replace
 *@module regex
 *@group Transform
 *@sig replace(pattern string, text string, replacement string) -> (string, Error)
 *@desc Replace all matches of pattern in text with replacement. Always use destructuring (`mut result, err = ...` or `mut result, _ = ...`) — single-variable assignment is a compile error. An invalid pattern yields a non-nil error and, with `_`, the original text.
 *@example
 *   import @regex
 *   mut result, err = regex.replace("[0-9]+", "a1b2", "X")
 *   println(result)
 *@end
 */
/* regex.replace(pattern, text, replacement) -> string */
GrayString gray_regex_replace(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement);

/*@man split
 *@module regex
 *@group Transform
 *@sig split(pattern string, text string) -> ([string], Error)
 *@desc Split text on all matches of pattern. Always use destructuring (`mut parts, err = ...` or `mut parts, _ = ...`) — single-variable assignment is a compile error. An invalid pattern yields a non-nil error and, with `_`, a single-element array holding the original text.
 *@example
 *   import @regex
 *   mut parts, err = regex.split("[,;]+", "a,b;;c")
 *@end
 */
/* regex.split(pattern, text) -> [string] */
GrayArray gray_regex_split(GrayArena *arena, GrayString pattern, GrayString text);

/*@man find_groups
 *@module regex
 *@group Search
 *@sig find_groups(pattern string, text string) -> ([string], Error)
 *@desc Return the capture groups of the first match: index 0 is the whole match, 1..n are the parenthesized groups in order. A group that did not participate is an empty string. Returns an empty array when there is no match. Always use destructuring. An invalid pattern yields a non-nil error and, with `_`, an empty array.
 *@example
 *   import @regex
 *   mut g, err = regex.find_groups("([0-9]+)-([0-9]+)", "order 12-34")
 *   println(g[1])   // 12
 *@end
 */
/* regex.find_groups(pattern, text) -> [string] */
GrayArray gray_regex_find_groups(GrayArena *arena, GrayString pattern, GrayString text);

/*@man find_all_groups
 *@module regex
 *@group Search
 *@sig find_all_groups(pattern string, text string) -> ([[string]], Error)
 *@desc Like find_groups, but for every non-overlapping match: returns an array of group arrays. Always use destructuring. An invalid pattern yields a non-nil error and, with `_`, an empty array.
 *@example
 *   import @regex
 *   mut all, err = regex.find_all_groups("([a-z])([0-9])", "a1 b2")
 *   println(all[1][2])   // 2
 *@end
 */
/* regex.find_all_groups(pattern, text) -> [[string]] */
GrayArray gray_regex_find_all_groups(GrayArena *arena, GrayString pattern, GrayString text);

/*@man count
 *@module regex
 *@group Search
 *@sig count(pattern string, text string) -> int
 *@desc Return the number of non-overlapping matches of pattern in text. An invalid pattern returns 0.
 *@example
 *   import @regex
 *   println(regex.count("[0-9]+", "a1b22c333"))   // 3
 *@end
 */
/* regex.count(pattern, text) -> int */
int64_t gray_regex_count(GrayString pattern, GrayString text);

/*@man escape
 *@module regex
 *@group Utility
 *@sig escape(s string) -> string
 *@desc Backslash-escape every character that is special in a POSIX extended regex, so s matches literally when spliced into a pattern.
 *@example
 *   import @regex
 *   mut pat string = regex.escape("a.b(c)") + "+"
 *@end
 */
/* regex.escape(s) -> string */
GrayString gray_regex_escape(GrayArena *arena, GrayString str);

/* _result variants */
GrayResult_string gray_regex_find_result(GrayArena *arena, GrayString pattern, GrayString text);
GrayResult_array gray_regex_find_all_result(GrayArena *arena, GrayString pattern, GrayString text);
GrayResult_array gray_regex_find_groups_result(GrayArena *arena, GrayString pattern, GrayString text);
GrayResult_array gray_regex_find_all_groups_result(GrayArena *arena, GrayString pattern, GrayString text);
GrayResult_string gray_regex_replace_result(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement);
GrayResult_array gray_regex_split_result(GrayArena *arena, GrayString pattern, GrayString text);

#endif
