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
#if GRAY_RT_WINDOWS
#include "../runtime/regex_win.h" /* Windows ships no <regex.h> */
#else
#include <regex.h>
#endif
#include <string.h>
#include <stdio.h>


/* POSIX ERE has no \d \w \s \b (or \D \W \S \B). regcomp accepts them and
 * treats \x as the literal x, so "\d+" silently matches "ddd" instead of
 * digits. Reject any pattern that uses one up front, so is_valid() reports
 * false and the fallible functions return an error rather than matching the
 * wrong thing. A pattern that wants those classes uses [[:digit:]] etc. */
static bool pattern_has_unsupported_escape(const char *pat) {
    for (const char *p = pat; *p; p++) {
        if (*p != '\\' || !p[1]) continue;
        if (strchr("dDwWsSbB", p[1])) return true;
        p++; /* consume the escaped character (covers "\\") */
    }
    return false;
}

/* Null-terminate a GrayString into a fresh arena buffer sized to the input.
 * regexec needs a NUL terminator; the fixed 8 KB stack buffer this replaced
 * silently truncated (and produced wrong match counts on) longer text. */
static char *regex_cstr(GrayArena *arena, GrayString s) {
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, (size_t)s.len + 1);
    if (s.len > 0) memcpy(buf, s.data, (size_t)s.len);
    buf[s.len] = '\0';
    return buf;
}

/* Helper: compile pattern into a null-terminated C string and regex_t.
 * Returns 0 on success, non-zero on error. Caller must regfree on success. */
static int compile_pattern(GrayString pattern, regex_t *re, int flags) {
    char *pat_buf = regex_cstr(gray_default_arena, pattern);
    if (pattern_has_unsupported_escape(pat_buf)) return REG_BADPAT;
    return regcomp(re, pat_buf, flags | REG_EXTENDED);
}

bool gray_regex_is_valid(GrayString pattern) {
    regex_t re;
    if (compile_pattern(pattern, &re, REG_EXTENDED | REG_NOSUB) != 0) return false;
    regfree(&re);
    return true;
}

bool gray_regex_match(GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, REG_NOSUB) != 0) return false;

    char *txt_buf = regex_cstr(gray_default_arena, text);

    int result = regexec(&re, txt_buf, 0, NULL, 0);
    regfree(&re);
    return result == 0;
}

/* Internal helpers that operate on a pre-compiled regex_t.
 * Caller owns the regex_t lifetime (compile + regfree). */

static GrayString regex_find_compiled(GrayArena *arena, regex_t *re, GrayString text) {
    char *txt_buf = regex_cstr(arena, text);

    regmatch_t match;
    if (regexec(re, txt_buf, 1, &match, 0) != 0)
        return (GrayString){"", 0};

    return gray_string_new(arena, txt_buf + match.rm_so, (int32_t)(match.rm_eo - match.rm_so));
}

static GrayArray regex_find_all_compiled(GrayArena *arena, regex_t *re, GrayString text) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 8);

    char *txt_buf = regex_cstr(arena, text);

    const char *cursor = txt_buf;
    regmatch_t match;

    /* REG_NOTBOL past the first attempt: cursor[0] is no longer the string
     * start, so `^` must not re-anchor there on each advance. */
    while (regexec(re, cursor, 1, &match, cursor == txt_buf ? 0 : REG_NOTBOL) == 0) {
        int32_t match_length = (int32_t)(match.rm_eo - match.rm_so);
        GrayString s = gray_string_new(arena, cursor + match.rm_so, match_length);
        GRAY_ARRAY_PUSH(arena, &arr, &s);

        cursor += match.rm_eo;
        /* A zero-width match (rm_so == rm_eo) makes no forward progress on its
         * own — step one char or stop, so `$`/`\b` etc. can't re-match in place. */
        if (match.rm_so == match.rm_eo) {
            if (*cursor) cursor++;
            else break;
        }
    }

    return arr;
}

static GrayString regex_replace_compiled(GrayArena *arena, regex_t *re, GrayString text, GrayString replacement) {
    char *txt_buf = regex_cstr(arena, text);

    char *repl_buf = regex_cstr(arena, replacement);
    int repl_len = (int)replacement.len;

    /* First pass: compute exact output size */
    size_t out_size = 0;
    int match_count = 0;
    const char *cursor = txt_buf;
    regmatch_t match;

    while (regexec(re, cursor, 1, &match, cursor == txt_buf ? 0 : REG_NOTBOL) == 0) {
        out_size += (size_t)match.rm_so;
        out_size += (size_t)repl_len;
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
    int pos = 0;
    cursor = txt_buf;

    while (regexec(re, cursor, 1, &match, cursor == txt_buf ? 0 : REG_NOTBOL) == 0) {
        int pre_len = (int)match.rm_so;
        memcpy(result + pos, cursor, (size_t)pre_len);
        pos += pre_len;

        memcpy(result + pos, repl_buf, (size_t)repl_len);
        pos += repl_len;

        cursor += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) result[pos++] = *cursor++;
            else break;
        }
    }

    int remaining = (int)strlen(cursor);
    memcpy(result + pos, cursor, (size_t)remaining);
    pos += remaining;
    result[pos] = '\0';

    return (GrayString){ result, (int32_t)pos };
}

static GrayArray regex_split_compiled(GrayArena *arena, regex_t *re, GrayString text) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 8);

    char *txt_buf = regex_cstr(arena, text);

    const char *piece_start = txt_buf;  /* start of the field being accumulated */
    const char *cursor = txt_buf;       /* scan position for the next separator */
    regmatch_t match;

    while (regexec(re, cursor, 1, &match, cursor == txt_buf ? 0 : REG_NOTBOL) == 0) {
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
        GRAY_ARRAY_PUSH(arena, &arr, &piece);

        cursor += match.rm_eo;
        piece_start = cursor;
    }

    int32_t remaining = (int32_t)strlen(piece_start);
    GrayString last = gray_string_new(arena, piece_start, remaining);
    GRAY_ARRAY_PUSH(arena, &arr, &last);

    return arr;
}

int64_t gray_regex_count(GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) return 0;

    char *txt_buf = regex_cstr(gray_default_arena, text);

    const char *cursor = txt_buf;
    regmatch_t match;
    int64_t count = 0;

    while (regexec(&re, cursor, 1, &match, cursor == txt_buf ? 0 : REG_NOTBOL) == 0) {
        count++;
        cursor += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*cursor) cursor++;
            else break;
        }
    }

    regfree(&re);
    return count;
}

GrayString gray_regex_escape(GrayArena *arena, GrayString str) {
    /* Worst case: every character needs a backslash. */
    char *out = (char *)gray_arena_alloc_uninitialized(arena, (size_t)str.len * 2 + 1);
    int32_t j = 0;
    for (int32_t i = 0; i < str.len; i++) {
        char c = str.data[i];
        if (c != '\0' && strchr(".^$*+?()[]{}|\\", c) != NULL) {
            out[j++] = '\\';
        }
        out[j++] = c;
    }
    out[j] = '\0';
    return (GrayString){ out, j };
}

/* Capture-group extraction. pmatch[0] is the whole match, pmatch[1..] the
 * parenthesized groups; a group that did not participate has rm_so == -1 and
 * becomes an empty string. */
#define GRAY_REGEX_MAX_GROUPS 64

static GrayArray regex_groups_of_match(GrayArena *arena, const char *base,
                                      const regmatch_t *pmatch, size_t ngroups) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), (int32_t)ngroups);
    for (size_t g = 0; g < ngroups; g++) {
        GrayString s;
        if (pmatch[g].rm_so < 0) {
            s = (GrayString){"", 0};
        } else {
            s = gray_string_new(arena, base + pmatch[g].rm_so,
                                (int32_t)(pmatch[g].rm_eo - pmatch[g].rm_so));
        }
        GRAY_ARRAY_PUSH(arena, &arr, &s);
    }
    return arr;
}

GrayArray gray_regex_find_groups(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0)
        return gray_array_new(arena, sizeof(GrayString), 0);

    size_t ngroups = re.re_nsub + 1;
    if (ngroups > GRAY_REGEX_MAX_GROUPS) ngroups = GRAY_REGEX_MAX_GROUPS;

    char *txt_buf = regex_cstr(arena, text);

    regmatch_t pmatch[GRAY_REGEX_MAX_GROUPS];
    GrayArray arr;
    if (regexec(&re, txt_buf, ngroups, pmatch, 0) != 0) {
        arr = gray_array_new(arena, sizeof(GrayString), 0);
    } else {
        arr = regex_groups_of_match(arena, txt_buf, pmatch, ngroups);
    }
    regfree(&re);
    return arr;
}

GrayArray gray_regex_find_all_groups(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0)
        return gray_array_new(arena, sizeof(GrayArray), 0);

    size_t ngroups = re.re_nsub + 1;
    if (ngroups > GRAY_REGEX_MAX_GROUPS) ngroups = GRAY_REGEX_MAX_GROUPS;

    char *txt_buf = regex_cstr(arena, text);

    GrayArray outer = gray_array_new(arena, sizeof(GrayArray), 8);
    const char *cursor = txt_buf;
    regmatch_t pmatch[GRAY_REGEX_MAX_GROUPS];

    while (regexec(&re, cursor, ngroups, pmatch, cursor == txt_buf ? 0 : REG_NOTBOL) == 0) {
        GrayArray inner = regex_groups_of_match(arena, cursor, pmatch, ngroups);
        GRAY_ARRAY_PUSH(arena, &outer, &inner);

        cursor += (int)pmatch[0].rm_eo;
        if (pmatch[0].rm_so == pmatch[0].rm_eo) {
            if (*cursor) cursor++;
            else break;
        }
    }
    regfree(&re);
    return outer;
}

/* Public API — compile, delegate to _compiled helper, free. */

GrayString gray_regex_find(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0)
        return (GrayString){"", 0};
    GrayString result = regex_find_compiled(arena, &re, text);
    regfree(&re);
    return result;
}

GrayArray gray_regex_find_all(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0)
        return gray_array_new(arena, sizeof(GrayString), 8);
    GrayArray result = regex_find_all_compiled(arena, &re, text);
    regfree(&re);
    return result;
}

GrayString gray_regex_replace(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0)
        return text;
    GrayString result = regex_replace_compiled(arena, &re, text, replacement);
    regfree(&re);
    return result;
}

GrayArray gray_regex_split(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        GrayArray arr = gray_array_new(arena, sizeof(GrayString), 8);
        GRAY_ARRAY_PUSH(arena, &arr, &text);
        return arr;
    }
    GrayArray result = regex_split_compiled(arena, &re, text);
    regfree(&re);
    return result;
}

/* _result variants — compile once, reuse for the actual work. */

GrayResult_string gray_regex_find_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_string r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = (GrayString){"", 0};
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    r.v0 = regex_find_compiled(arena, &re, text);
    regfree(&re);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_regex_find_all_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = gray_array_new(arena, sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    r.v0 = regex_find_all_compiled(arena, &re, text);
    regfree(&re);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_regex_find_groups_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array r;
    if (!gray_regex_is_valid(pattern)) {
        r.v0 = gray_array_new(arena, sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena,
            "invalid regex pattern '%.*s'", pattern.len, pattern.data));
        return r;
    }
    r.v0 = gray_regex_find_groups(arena, pattern, text);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_regex_find_all_groups_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array r;
    if (!gray_regex_is_valid(pattern)) {
        r.v0 = gray_array_new(arena, sizeof(GrayArray), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena,
            "invalid regex pattern '%.*s'", pattern.len, pattern.data));
        return r;
    }
    r.v0 = gray_regex_find_all_groups(arena, pattern, text);
    r.v1 = NULL;
    return r;
}

GrayResult_string gray_regex_replace_result(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement) {
    GrayResult_string r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = text;
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    r.v0 = regex_replace_compiled(arena, &re, text, replacement);
    regfree(&re);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_regex_split_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = gray_array_new(arena, sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    r.v0 = regex_split_compiled(arena, &re, text);
    regfree(&re);
    r.v1 = NULL;
    return r;
}
