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
#include "../runtime/platform_rt.h"
#if GRAY_RUNTIME_WINDOWS
#include "../runtime/regex_win.h" /* Windows ships no <regex.h> */
#else
#include <regex.h>
#endif
#include <string.h>
#include <stdio.h>
#include <pthread.h>


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
static char *regex_c_string(GrayArena *arena, GrayString text) {
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)text.len + 1);
    if (text.len > 0) memcpy(buffer, text.data, (size_t)text.len);
    buffer[text.len] = '\0';
    return buffer;
}

/* The platform regex engine is not thread-safe: macOS libc regexec races on
 * process-global scratch even when each thread owns its regex_t, so two regex
 * calls on different threads corrupt each other's results. Serialize every
 * compile/exec/free session on this lock. compile_pattern acquires it on
 * success; regex_session_end releases it. (regex_win.h's engine is reentrant,
 * but one code path is simpler and the lock is uncontended single-threaded.) */
static pthread_mutex_t gray_regex_lock = PTHREAD_MUTEX_INITIALIZER;

/* Helper: compile pattern into a null-terminated C string and regex_t.
 * Returns 0 on success (with gray_regex_lock held — release it with
 * regex_session_end), non-zero on error (lock not held). */
static int compile_pattern(GrayString pattern, regex_t *regex, int flags) {
    char *pattern_buffer = regex_c_string(gray_default_arena, pattern);
    pthread_mutex_lock(&gray_regex_lock);
    if (pattern_has_unsupported_escape(pattern_buffer)) {
        pthread_mutex_unlock(&gray_regex_lock);
        return REG_BADPAT;
    }
    int result_code = regcomp(regex, pattern_buffer, flags | REG_EXTENDED);
    if (result_code != 0) pthread_mutex_unlock(&gray_regex_lock);
    return result_code;
}

/* Pairs with the lock compile_pattern took: free the compiled pattern and
 * release the regex engine for other threads. */
static void regex_session_end(regex_t *regex) {
    regfree(regex);
    pthread_mutex_unlock(&gray_regex_lock);
}

bool gray_regex_is_valid(GrayString pattern) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, REG_EXTENDED | REG_NOSUB) != 0) return false;
    regex_session_end(&regex);
    return true;
}

bool gray_regex_match(GrayString pattern, GrayString text) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, REG_NOSUB) != 0) return false;

    char *text_buffer = regex_c_string(gray_default_arena, text);

    int result = regexec(&regex, text_buffer, 0, NULL, 0);
    regex_session_end(&regex);
    return result == 0;
}

/* Internal helpers that operate on a pre-compiled regex_t.
 * Caller owns the regex_t lifetime (compile_pattern + regex_session_end). */

static GrayString regex_find_compiled(GrayArena *arena, regex_t *regex, GrayString text) {
    char *text_buffer = regex_c_string(arena, text);

    regmatch_t match;
    if (regexec(regex, text_buffer, 1, &match, 0) != 0)
        return (GrayString){"", 0};

    return gray_string_new(arena, text_buffer + match.rm_so, (int32_t)(match.rm_eo - match.rm_so));
}

static GrayArray regex_find_all_compiled(GrayArena *arena, regex_t *regex, GrayString text) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);

    char *text_buffer = regex_c_string(arena, text);

    const char *cursor = text_buffer;
    regmatch_t match;

    /* REG_NOTBOL past the first attempt: cursor[0] is no longer the string
     * start, so `^` must not re-anchor there on each advance. */
    while (regexec(regex, cursor, 1, &match, cursor == text_buffer ? 0 : REG_NOTBOL) == 0) {
        int32_t match_length = (int32_t)(match.rm_eo - match.rm_so);
        GrayString subject = gray_string_new(arena, cursor + match.rm_so, match_length);
        GRAY_ARRAY_PUSH(arena, &array, &subject);

        cursor += match.rm_eo;
        /* A zero-width match (rm_so == rm_eo) makes no forward progress on its
         * own — step one char or stop, so `$`/`\b` etc. can't re-match in place. */
        if (match.rm_so == match.rm_eo) {
            if (*cursor) cursor++;
            else break;
        }
    }

    return array;
}

static GrayString regex_replace_compiled(GrayArena *arena, regex_t *regex, GrayString text, GrayString replacement) {
    char *text_buffer = regex_c_string(arena, text);

    char *replacement_buffer = regex_c_string(arena, replacement);
    int replacement_length = (int)replacement.len;

    /* First pass: compute exact output size */
    size_t out_size = 0;
    int match_count = 0;
    const char *cursor = text_buffer;
    regmatch_t match;

    while (regexec(regex, cursor, 1, &match, cursor == text_buffer ? 0 : REG_NOTBOL) == 0) {
        out_size += (size_t)match.rm_so;
        out_size += (size_t)replacement_length;
        cursor += match.rm_eo;
        match_count++;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) { out_size++; cursor++; }
            else break;
        }
    }
    out_size += strlen(cursor);

    if (match_count == 0) return text;

    /* Second pass: build result into arena-allocated buffer */
    char *result = (char *)gray_arena_alloc_uninitialized(arena, out_size + 1);
    int position = 0;
    cursor = text_buffer;

    while (regexec(regex, cursor, 1, &match, cursor == text_buffer ? 0 : REG_NOTBOL) == 0) {
        int prefix_length = (int)match.rm_so;
        memcpy(result + position, cursor, (size_t)prefix_length);
        position += prefix_length;

        memcpy(result + position, replacement_buffer, (size_t)replacement_length);
        position += replacement_length;

        cursor += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) result[position++] = *cursor++;
            else break;
        }
    }

    int remaining = (int)strlen(cursor);
    memcpy(result + position, cursor, (size_t)remaining);
    position += remaining;
    result[position] = '\0';

    return (GrayString){ result, (int32_t)position };
}

static GrayArray regex_split_compiled(GrayArena *arena, regex_t *regex, GrayString text) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);

    char *text_buffer = regex_c_string(arena, text);

    const char *piece_start = text_buffer;  /* start of the field being accumulated */
    const char *cursor = text_buffer;       /* scan position for the next separator */
    regmatch_t match;

    while (regexec(regex, cursor, 1, &match, cursor == text_buffer ? 0 : REG_NOTBOL) == 0) {
        /* A zero-width match is not a separator — you can't split on nothing.
         * Step past one character so the scan makes progress; that character
         * stays part of the current field. */
        if (match.rm_so == match.rm_eo) {
            cursor += match.rm_eo;
            if (!*cursor) break;
            cursor++;
            continue;
        }

        int32_t piece_length = (int32_t)(cursor + match.rm_so - piece_start);
        GrayString piece = gray_string_new(arena, piece_start, piece_length);
        GRAY_ARRAY_PUSH(arena, &array, &piece);

        cursor += match.rm_eo;
        piece_start = cursor;
    }

    int32_t remaining = (int32_t)strlen(piece_start);
    GrayString last = gray_string_new(arena, piece_start, remaining);
    GRAY_ARRAY_PUSH(arena, &array, &last);

    return array;
}

int64_t gray_regex_count(GrayString pattern, GrayString text) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0) return 0;

    char *text_buffer = regex_c_string(gray_default_arena, text);

    const char *cursor = text_buffer;
    regmatch_t match;
    int64_t count = 0;

    while (regexec(&regex, cursor, 1, &match, cursor == text_buffer ? 0 : REG_NOTBOL) == 0) {
        count++;
        cursor += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) cursor++;
            else break;
        }
    }

    regex_session_end(&regex);
    return count;
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
                                      const regmatch_t *pmatch, size_t ngroups) {
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
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0)
        return gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);

    /* One regmatch_t per group (plus [0] for the whole match), sized to the
     * compiled pattern — a fixed cap silently dropped groups past it. */
    size_t ngroups = regex.re_nsub + 1;
    char *text_buffer = regex_c_string(arena, text);

    regmatch_t *pmatch = gray_arena_alloc(arena, ngroups * sizeof(regmatch_t));
    GrayArray array;
    if (regexec(&regex, text_buffer, ngroups, pmatch, 0) != 0) {
        array = gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
    } else {
        array = regex_groups_of_match(arena, text_buffer, pmatch, ngroups);
    }
    regex_session_end(&regex);
    return array;
}

GrayArray gray_regex_find_all_groups(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0)
        return gray_array_new(arena, sizeof(GrayArray), 0, GRAY_ELEM_ARRAY);

    size_t ngroups = regex.re_nsub + 1;
    char *text_buffer = regex_c_string(arena, text);

    GrayArray outer = gray_array_new(arena, sizeof(GrayArray), 8, GRAY_ELEM_ARRAY);
    const char *cursor = text_buffer;
    regmatch_t *pmatch = gray_arena_alloc(arena, ngroups * sizeof(regmatch_t));

    while (regexec(&regex, cursor, ngroups, pmatch, cursor == text_buffer ? 0 : REG_NOTBOL) == 0) {
        GrayArray inner = regex_groups_of_match(arena, cursor, pmatch, ngroups);
        GRAY_ARRAY_PUSH(arena, &outer, &inner);

        cursor += (int)pmatch[0].rm_eo;
        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (*cursor) cursor++;
            else break;
        }
    }
    regex_session_end(&regex);
    return outer;
}

/* Public API — compile, delegate to _compiled helper, free. */

GrayString gray_regex_find(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0)
        return (GrayString){"", 0};
    GrayString result = regex_find_compiled(arena, &regex, text);
    regex_session_end(&regex);
    return result;
}

GrayArray gray_regex_find_all(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0)
        return gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);
    GrayArray result = regex_find_all_compiled(arena, &regex, text);
    regex_session_end(&regex);
    return result;
}

GrayString gray_regex_replace(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0)
        return text;
    GrayString result = regex_replace_compiled(arena, &regex, text, replacement);
    regex_session_end(&regex);
    return result;
}

GrayArray gray_regex_split(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0) {
        GrayArray array = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);
        GRAY_ARRAY_PUSH(arena, &array, &text);
        return array;
    }
    GrayArray result = regex_split_compiled(arena, &regex, text);
    regex_session_end(&regex);
    return result;
}

/* _result variants — compile once, reuse for the actual work. */

GrayResult_string gray_regex_find_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_string result;
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0) {
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
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0) {
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
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0) {
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
    regex_t regex;
    if (compile_pattern(pattern, &regex, 0) != 0) {
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
