/*
 * regex.c — Implementation of the regex stdlib module.
 * Provides match, find, find_all, replace, and split operations
 * using POSIX extended regular expressions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "regex.h"
#include "utf8.h"
#include "../runtime/platform_rt.h"
#if !GRAY_RUNTIME_WINDOWS
#include <regex.h>
#define GRAY_REGEX_SPAN_TYPE regmatch_t
#endif
/* The character-aware engine: all of Windows, which ships no <regex.h>, and a
 * POSIX system with no UTF-8 locale, where libc would match bytes. */
#include "../runtime/regex_win.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#if !GRAY_RUNTIME_WINDOWS
#include <locale.h>
#if defined(__APPLE__) && !defined(__clang__)
#include <xlocale.h> /* GCC's <locale.h> does not declare locale_t on macOS */
#endif
#endif


/* POSIX ERE has no \d \w \s \b (or \D \W \S \B). regcomp accepts them and
 * treats \x as the literal x, so "\d+" silently matches "ddd" instead of
 * digits. Reject any pattern that uses one up front, so is_valid() reports
 * false and the fallible functions return an error rather than matching the
 * wrong thing. A pattern that wants those classes uses [[:digit:]] etc. */
static bool pattern_has_unsupported_escape(const char *pattern) {
    for (const char *cursor = pattern; *cursor; cursor++) {
        if (*cursor != '\\' || !cursor[1]) continue;
        if (strchr("dDwWsSbB", cursor[1])) return true;
        cursor++; /* consume the escaped character (covers "\\") */
    }
    return false;
}

/* Null-terminate a GrayString into a fresh arena buffer sized to the input.
 * regexec needs a NUL terminator; the fixed 8 KB stack buffer this replaced
 * silently truncated (and produced wrong match counts on) longer text. */
/* regcomp and regexec take C strings, so an embedded NUL in a pattern or text
 * would silently cut it short. */
static char *regex_c_string(GrayArena *arena, GrayString text) {
    if (memchr(text.data, '\0', (size_t)text.len))
        gray_panic_code("P0146", "regex: a pattern or text contains an embedded null byte");
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)text.len + 1);
    if (text.len > 0) memcpy(buffer, text.data, (size_t)text.len);
    buffer[text.len] = '\0';
    return buffer;
}

/* The text a regex engine searches. An invalid UTF-8 byte is one U+FFFD
 * character, but a UTF-8 locale's regexec refuses any text that holds one, so
 * each such byte is replaced by ASCII SUB: a single character that no pattern
 * class matches and "." does. The replacement is one byte for one byte, so
 * every offset the engine reports is also an offset into `text`, and results are
 * sliced from `text`, which keeps the original bytes. */
static char *regex_text_buffer(GrayArena *arena, GrayString text) {
    char *buffer = regex_c_string(arena, text);
    const uint8_t *start = (const uint8_t *)buffer;
    const uint8_t *end_cursor = start + text.len;
    for (const uint8_t *cursor = start; cursor < end_cursor; ) {
        int32_t codepoint;
        int length = gray_utf8_decode(cursor, end_cursor, &codepoint);
        if (codepoint == 0xFFFD && length == 1) buffer[cursor - start] = 0x1A;
        cursor += length;
    }
    return buffer;
}

/* The platform regex engine is not thread-safe: macOS libc regexec races on
 * process-global scratch even when each thread owns its regex_t, so two regex
 * calls on different threads corrupt each other's results. Serialize every
 * compile/exec/free session on this lock. compile_pattern acquires it on
 * success; regex_session_end releases it. (regex_win.h's engine is reentrant,
 * but one code path is simpler and the lock is uncontended single-threaded.) */
static pthread_mutex_t gray_regex_lock = PTHREAD_MUTEX_INITIALIZER;

#if !GRAY_RUNTIME_WINDOWS
/* regcomp and regexec follow the thread's locale. A string is UTF-8 text, so
 * while the lock is held the calling thread runs in a UTF-8 locale and `.`,
 * bracket expressions and quantifiers work on whole characters. Created on
 * first use, under the lock; NULL when the system has no UTF-8 locale. */
static locale_t gray_regex_locale;
static bool gray_regex_locale_ready;
static locale_t gray_regex_previous_locale;

/* Switch this thread to the UTF-8 locale; false when the system has none. */
static bool regex_enter_utf8_locale(void) {
    if (!gray_regex_locale_ready) {
        gray_regex_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
        if (!gray_regex_locale) gray_regex_locale = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
        gray_regex_locale_ready = true;
    }
    if (!gray_regex_locale) return false;
    gray_regex_previous_locale = uselocale(gray_regex_locale);
    return true;
}

static void regex_leave_utf8_locale(void) {
    if (gray_regex_locale) uselocale(gray_regex_previous_locale);
}
#else
static bool regex_enter_utf8_locale(void) { return false; }
static void regex_leave_utf8_locale(void) {}
#endif

/* The position after the UTF-8 character that starts at `cursor`. */
static const char *regex_next_character(const char *cursor) {
    size_t available = 0;
    while (available < 4 && cursor[available]) available++;
    int32_t codepoint;
    return cursor + gray_utf8_decode((const uint8_t *)cursor, (const uint8_t *)cursor + available, &codepoint);
}

/* A compiled pattern: libc's when the thread has a UTF-8 locale, else the
 * character-aware engine. */
typedef struct {
#if !GRAY_RUNTIME_WINDOWS
    regex_t libc;
    bool is_libc;
#endif
    GrayRegexEngine engine;
} RegexProgram;

#if !GRAY_RUNTIME_WINDOWS
/* POSIX classes are ASCII only, but libc runs them in the UTF-8 locale and so
 * matches non-ASCII letters too. Spell each [:class:] inside a bracket
 * expression as its ASCII members. */
static const char *regex_ascii_class_members(const char *name, size_t name_length) {
    static const struct { const char *name; const char *members; } classes[] = {
        {"alpha", "a-zA-Z"}, {"digit", "0-9"}, {"alnum", "a-zA-Z0-9"}, {"upper", "A-Z"},
        {"lower", "a-z"}, {"space", " \t\n\v\f\r"}, {"blank", " \t"}, {"punct", "!-/:-@[-`{-~"},
        {"print", " -~"}, {"graph", "!-~"}, {"cntrl", "\x01-\x1f\x7f"}, {"xdigit", "0-9A-Fa-f"},
    };
    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        if (strlen(classes[i].name) == name_length && memcmp(classes[i].name, name, name_length) == 0)
            return classes[i].members;
    }
    return NULL;
}

static char *regex_ascii_classes(GrayArena *arena, const char *pattern) {
    if (!strstr(pattern, "[:")) return (char *)pattern;
    /* The longest expansion is shorter than the "[:xdigit:]" it replaces plus
     * a few bytes, so 2x is always enough. */
    char *output = (char *)gray_arena_alloc_uninitialized(arena, strlen(pattern) * 2 + 1);
    char *write = output;
    const char *cursor = pattern;
    while (*cursor) {
        if (*cursor == '\\' && cursor[1]) { *write++ = *cursor++; *write++ = *cursor++; continue; }
        if (*cursor != '[') { *write++ = *cursor++; continue; }
        /* A bracket expression: copy it through, expanding its [:class:] items. */
        *write++ = *cursor++;
        if (*cursor == '^') *write++ = *cursor++;
        if (*cursor == ']') *write++ = *cursor++;
        while (*cursor && *cursor != ']') {
            if (cursor[0] == '[' && (cursor[1] == ':' || cursor[1] == '.' || cursor[1] == '=')) {
                char kind = cursor[1];
                const char *close = cursor + 2;
                while (*close && !(close[0] == kind && close[1] == ']')) close++;
                if (!*close) break;
                const char *members = kind == ':' ? regex_ascii_class_members(cursor + 2, (size_t)(close - cursor - 2)) : NULL;
                if (members) {
                    size_t length = strlen(members);
                    memcpy(write, members, length);
                    write += length;
                } else {
                    memcpy(write, cursor, (size_t)(close + 2 - cursor));
                    write += close + 2 - cursor;
                }
                cursor = close + 2;
                continue;
            }
            *write++ = *cursor++;
        }
        if (*cursor == ']') *write++ = *cursor++;
    }
    *write = '\0';
    return output;
}
#endif

#if !GRAY_RUNTIME_WINDOWS
/* True when a bracket expression holds a range with a non-ASCII endpoint.
 * libc orders those by the locale's collation, which on some systems matches
 * nothing, so the character-aware engine runs them by codepoint. */
static bool regex_has_non_ascii_range(const char *pattern) {
    bool in_bracket = false;
    for (const char *cursor = pattern; *cursor; cursor++) {
        if (*cursor == '\\' && cursor[1]) { cursor++; continue; }
        if (*cursor == '[') in_bracket = true;
        else if (*cursor == ']') in_bracket = false;
        else if (in_bracket && *cursor == '-' && cursor > pattern &&
                 (((unsigned char)cursor[-1] & 0x80) || ((unsigned char)cursor[1] & 0x80)))
            return true;
    }
    return false;
}
#endif

/* Helper: compile pattern into a null-terminated C string and a program.
 * Returns 0 on success (with gray_regex_lock held — release it with
 * regex_session_end), non-zero on error (lock not held). */
static int compile_pattern(GrayString pattern, RegexProgram *regex, bool is_without_groups) {
    (void)is_without_groups;
    char *pattern_buffer = regex_c_string(gray_default_arena, pattern);
    pthread_mutex_lock(&gray_regex_lock);
    if (pattern_has_unsupported_escape(pattern_buffer)) {
        pthread_mutex_unlock(&gray_regex_lock);
        return 1;
    }
#if !GRAY_RUNTIME_WINDOWS
    regex->is_libc = !regex_has_non_ascii_range(pattern_buffer) && regex_enter_utf8_locale();
    if (regex->is_libc) {
        int result_code = regcomp(&regex->libc, regex_ascii_classes(gray_default_arena, pattern_buffer),
                                  REG_EXTENDED | (is_without_groups ? REG_NOSUB : 0));
        if (result_code != 0) {
            regex_leave_utf8_locale();
            pthread_mutex_unlock(&gray_regex_lock);
        }
        return result_code;
    }
#endif
    int result_code = gray_regex_engine_compile(&regex->engine, pattern_buffer);
    if (result_code != 0) pthread_mutex_unlock(&gray_regex_lock);
    return result_code;
}

/* Pairs with the lock compile_pattern took: free the compiled pattern and
 * release the regex engine for other threads. */
static void regex_session_end(RegexProgram *regex) {
#if !GRAY_RUNTIME_WINDOWS
    if (regex->is_libc) {
        regfree(&regex->libc);
        regex_leave_utf8_locale();
        pthread_mutex_unlock(&gray_regex_lock);
        return;
    }
#endif
    gray_regex_engine_release(&regex->engine);
    pthread_mutex_unlock(&gray_regex_lock);
}

/* Search text for a match, filling spans[0..nmatch). On a search that starts
 * past the beginning of the text, ^ must not match there. */
static bool regex_execute(const RegexProgram *regex, const char *text, size_t nmatch, GrayRegexSpan *spans,
                          bool is_after_start) {
#if !GRAY_RUNTIME_WINDOWS
    if (regex->is_libc) return regexec(&regex->libc, text, nmatch, spans, is_after_start ? REG_NOTBOL : 0) == 0;
#endif
    return gray_regex_engine_execute(&regex->engine, text, nmatch, spans, is_after_start) == 0;
}

static size_t regex_group_count(const RegexProgram *regex) {
#if !GRAY_RUNTIME_WINDOWS
    if (regex->is_libc) return regex->libc.re_nsub;
#endif
    return regex->engine.group_count;
}

bool gray_regex_is_valid(GrayString pattern) {
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, true) != 0) return false;
    regex_session_end(&regex);
    return true;
}

bool gray_regex_match(GrayString pattern, GrayString text) {
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, true) != 0) return false;

    char *text_buffer = regex_text_buffer(gray_default_arena, text);

    bool result = regex_execute(&regex, text_buffer, 0, NULL, false);
    regex_session_end(&regex);
    return result;
}

/* Internal helpers that operate on a pre-compiled regex_t.
 * Caller owns the regex_t lifetime (compile_pattern + regex_session_end). */

static GrayString regex_find_compiled(GrayArena *arena, RegexProgram *regex, GrayString text) {
    char *text_buffer = regex_text_buffer(arena, text);

    GrayRegexSpan match;
    if (!regex_execute(regex, text_buffer, 1, &match, false))
        return (GrayString){"", 0};

    return gray_string_new(arena, text.data + match.rm_so, (int32_t)(match.rm_eo - match.rm_so));
}

static GrayArray regex_find_all_compiled(GrayArena *arena, RegexProgram *regex, GrayString text) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);

    char *text_buffer = regex_text_buffer(arena, text);

    const char *cursor = text_buffer;
    GrayRegexSpan match;

    /* REG_NOTBOL past the first attempt: cursor[0] is no longer the string
     * start, so `^` must not re-anchor there on each advance. */
    while (regex_execute(regex, cursor, 1, &match, cursor != text_buffer)) {
        int32_t match_length = (int32_t)(match.rm_eo - match.rm_so);
        GrayString subject = gray_string_new(arena, text.data + (cursor - text_buffer) + match.rm_so, match_length);
        GRAY_ARRAY_PUSH(arena, &array, &subject);

        cursor += match.rm_eo;
        /* A zero-width match (rm_so == rm_eo) makes no forward progress on its
         * own — step one char or stop, so `$`/`\b` etc. can't re-match in place. */
        if (match.rm_so == match.rm_eo) {
            if (*cursor) cursor = regex_next_character(cursor);
            else break;
        }
    }

    return array;
}

static GrayString regex_replace_compiled(GrayArena *arena, RegexProgram *regex, GrayString text, GrayString replacement) {
    char *text_buffer = regex_text_buffer(arena, text);

    int replacement_length = (int)replacement.len;

    /* First pass: compute exact output size */
    size_t out_size = 0;
    int match_count = 0;
    const char *cursor = text_buffer;
    GrayRegexSpan match;

    while (regex_execute(regex, cursor, 1, &match, cursor != text_buffer)) {
        out_size += (size_t)match.rm_so;
        out_size += (size_t)replacement_length;
        cursor += match.rm_eo;
        match_count++;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) {
                const char *next_character = regex_next_character(cursor);
                out_size += (size_t)(next_character - cursor);
                cursor = next_character;
            } else break;
        }
    }
    out_size += strlen(cursor);

    if (match_count == 0) return text;

    /* Second pass: build result into arena-allocated buffer */
    char *result = (char *)gray_arena_alloc_uninitialized(arena, out_size + 1);
    int position = 0;
    cursor = text_buffer;

    while (regex_execute(regex, cursor, 1, &match, cursor != text_buffer)) {
        int prefix_length = (int)match.rm_so;
        memcpy(result + position, text.data + (cursor - text_buffer), (size_t)prefix_length);
        position += prefix_length;

        memcpy(result + position, replacement.data, (size_t)replacement_length);
        position += replacement_length;

        cursor += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) {
                const char *next_character = regex_next_character(cursor);
                memcpy(result + position, text.data + (cursor - text_buffer), (size_t)(next_character - cursor));
                position += (int)(next_character - cursor);
                cursor = next_character;
            } else break;
        }
    }

    int remaining = (int)strlen(cursor);
    memcpy(result + position, text.data + (cursor - text_buffer), (size_t)remaining);
    position += remaining;
    result[position] = '\0';

    return (GrayString){ result, (int32_t)position };
}

static GrayArray regex_split_compiled(GrayArena *arena, RegexProgram *regex, GrayString text) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);

    char *text_buffer = regex_text_buffer(arena, text);

    const char *piece_start = text_buffer;  /* start of the field being accumulated */
    const char *cursor = text_buffer;       /* scan position for the next separator */
    GrayRegexSpan match;

    while (regex_execute(regex, cursor, 1, &match, cursor != text_buffer)) {
        /* A zero-width match is not a separator — you can't split on nothing.
         * Step past one character so the scan makes progress; that character
         * stays part of the current field. */
        if (match.rm_so == match.rm_eo) {
            cursor += match.rm_eo;
            if (!*cursor) break;
            cursor = regex_next_character(cursor);
            continue;
        }

        int32_t piece_length = (int32_t)(cursor + match.rm_so - piece_start);
        GrayString piece = gray_string_new(arena, text.data + (piece_start - text_buffer), piece_length);
        GRAY_ARRAY_PUSH(arena, &array, &piece);

        cursor += match.rm_eo;
        piece_start = cursor;
    }

    int32_t remaining = (int32_t)strlen(piece_start);
    GrayString last = gray_string_new(arena, text.data + (piece_start - text_buffer), remaining);
    GRAY_ARRAY_PUSH(arena, &array, &last);

    return array;
}

GrayResult_i64 gray_regex_count_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_i64 result;
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0) {
        result.v0 = 0;
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return result;
    }

    char *text_buffer = regex_text_buffer(gray_default_arena, text);

    const char *cursor = text_buffer;
    GrayRegexSpan match;
    int64_t count = 0;

    while (regex_execute(&regex, cursor, 1, &match, cursor != text_buffer)) {
        count++;
        cursor += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) cursor = regex_next_character(cursor);
            else break;
        }
    }

    regex_session_end(&regex);
    result.v0 = count;
    result.v1 = NULL;
    return result;
}

GrayString gray_regex_escape(GrayArena *arena, GrayString string) {
    /* Worst case: every character needs a backslash. */
    char *output = (char *)gray_arena_alloc_uninitialized(arena, (size_t)string.len * 2 + 1);
    int32_t j = 0;
    for (int32_t i = 0; i < string.len; i++) {
        char character = string.data[i];
        if (character != '\0' && strchr(".^$*+?()[]{}|\\", character) != NULL) {
            output[j++] = '\\';
        }
        output[j++] = character;
    }
    output[j] = '\0';
    return (GrayString){ output, j };
}

/* Capture-group extraction. pmatch[0] is the whole match, pmatch[1..] the
 * parenthesized groups; a group that did not participate has rm_so == -1 and
 * becomes an empty string. */

static GrayArray regex_groups_of_match(GrayArena *arena, const char *base,
                                      const GrayRegexSpan *pmatch, size_t ngroups) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), (int32_t)ngroups, GRAY_ELEM_STRING);
    for (size_t group_index = 0; group_index < ngroups; group_index++) {
        GrayString text;
        if (pmatch[group_index].rm_so < 0) {
            text = (GrayString){"", 0};
        } else {
            text = gray_string_new(arena, base + pmatch[group_index].rm_so,
                                (int32_t)(pmatch[group_index].rm_eo - pmatch[group_index].rm_so));
        }
        GRAY_ARRAY_PUSH(arena, &array, &text);
    }
    return array;
}

GrayArray gray_regex_find_groups(GrayArena *arena, GrayString pattern, GrayString text) {
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0)
        return gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);

    /* One GrayRegexSpan per group (plus [0] for the whole match), sized to the
     * compiled pattern — a fixed cap silently dropped groups past it. */
    size_t ngroups = regex_group_count(&regex) + 1;
    char *text_buffer = regex_text_buffer(arena, text);

    GrayRegexSpan *pmatch = gray_arena_alloc(arena, ngroups * sizeof(GrayRegexSpan));
    GrayArray array;
    if (!regex_execute(&regex, text_buffer, ngroups, pmatch, false)) {
        array = gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
    } else {
        array = regex_groups_of_match(arena, text.data, pmatch, ngroups);
    }
    regex_session_end(&regex);
    return array;
}

GrayArray gray_regex_find_all_groups(GrayArena *arena, GrayString pattern, GrayString text) {
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0)
        return gray_array_new(arena, sizeof(GrayArray), 0, GRAY_ELEM_ARRAY);

    size_t ngroups = regex_group_count(&regex) + 1;
    char *text_buffer = regex_text_buffer(arena, text);

    GrayArray outer = gray_array_new(arena, sizeof(GrayArray), 8, GRAY_ELEM_ARRAY);
    const char *cursor = text_buffer;
    GrayRegexSpan *pmatch = gray_arena_alloc(arena, ngroups * sizeof(GrayRegexSpan));

    while (regex_execute(&regex, cursor, ngroups, pmatch, cursor != text_buffer)) {
        GrayArray inner = regex_groups_of_match(arena, text.data + (cursor - text_buffer), pmatch, ngroups);
        GRAY_ARRAY_PUSH(arena, &outer, &inner);

        cursor += (int)pmatch[0].rm_eo;
        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (*cursor) cursor = regex_next_character(cursor);
            else break;
        }
    }
    regex_session_end(&regex);
    return outer;
}

/* _result variants — compile once, reuse for the actual work. */

GrayResult_string gray_regex_find_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_string result;
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0) {
        result.v0 = (GrayString){"", 0};
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return result;
    }
    result.v0 = regex_find_compiled(arena, &regex, text);
    regex_session_end(&regex);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_regex_find_all_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array result;
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0) {
        result.v0 = gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return result;
    }
    result.v0 = regex_find_all_compiled(arena, &regex, text);
    regex_session_end(&regex);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_regex_find_groups_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array result;
    if (!gray_regex_is_valid(pattern)) {
        result.v0 = gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena,
            "invalid regex pattern '%.*s'", pattern.len, pattern.data));
        return result;
    }
    result.v0 = gray_regex_find_groups(arena, pattern, text);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_regex_find_all_groups_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array result;
    if (!gray_regex_is_valid(pattern)) {
        result.v0 = gray_array_new(arena, sizeof(GrayArray), 0, GRAY_ELEM_ARRAY);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena,
            "invalid regex pattern '%.*s'", pattern.len, pattern.data));
        return result;
    }
    result.v0 = gray_regex_find_all_groups(arena, pattern, text);
    result.v1 = NULL;
    return result;
}

GrayResult_string gray_regex_replace_result(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement) {
    GrayResult_string result;
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0) {
        result.v0 = text;
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return result;
    }
    result.v0 = regex_replace_compiled(arena, &regex, text, replacement);
    regex_session_end(&regex);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_regex_split_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array result;
    RegexProgram regex;
    if (compile_pattern(pattern, &regex, false) != 0) {
        result.v0 = gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return result;
    }
    result.v0 = regex_split_compiled(arena, &regex, text);
    regex_session_end(&regex);
    result.v1 = NULL;
    return result;
}
