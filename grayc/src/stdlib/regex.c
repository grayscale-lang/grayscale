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

#define GRAY_REGEX_PAT_BUF        4096
#define GRAY_REGEX_TXT_BUF        8192

/* Helper: compile pattern into a null-terminated C string and regex_t.
 * Returns 0 on success, non-zero on error. Caller must regfree on success. */
static int compile_pattern(GrayString pattern, regex_t *re, int flags) {
    char pat_buf[GRAY_REGEX_PAT_BUF];
    gray_cstr(pattern, pat_buf, sizeof(pat_buf));
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

    char txt_buf[GRAY_REGEX_TXT_BUF];
    gray_cstr(text, txt_buf, sizeof(txt_buf));

    int result = regexec(&re, txt_buf, 0, NULL, 0);
    regfree(&re);
    return result == 0;
}

GrayString gray_regex_find(GrayArena *arena, GrayString pattern, GrayString text) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        return (GrayString){"", 0};
    }

    char txt_buf[GRAY_REGEX_TXT_BUF];
    gray_cstr(text, txt_buf, sizeof(txt_buf));

    regmatch_t match;
    if (regexec(&re, txt_buf, 1, &match, 0) != 0) {
        regfree(&re);
        return (GrayString){"", 0};
    }

    int32_t match_length = (int32_t)(match.rm_eo - match.rm_so);
    GrayString result = gray_string_new(arena, txt_buf + match.rm_so, match_length);
    regfree(&re);
    return result;
}

GrayArray gray_regex_find_all(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 8);
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) return arr;

    char txt_buf[GRAY_REGEX_TXT_BUF];
    gray_cstr(text, txt_buf, sizeof(txt_buf));

    const char *cursor = txt_buf;
    regmatch_t match;

    while (regexec(&re, cursor, 1, &match, 0) == 0) {
        int32_t match_length = (int32_t)(match.rm_eo - match.rm_so);
        GrayString s = gray_string_new(arena, cursor + match.rm_so, match_length);
        GRAY_ARRAY_PUSH(arena, &arr, &s);

        cursor += match.rm_eo;
        if (match.rm_eo == 0) {
            if (cursor >= txt_buf + (size_t)text.len) break;
            cursor++;
        }
    }

    regfree(&re);
    return arr;
}

GrayString gray_regex_replace(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement) {
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        return text;
    }

    char txt_buf[GRAY_REGEX_TXT_BUF];
    gray_cstr(text, txt_buf, sizeof(txt_buf));

    char repl_buf[GRAY_REGEX_PAT_BUF];
    gray_cstr(replacement, repl_buf, sizeof(repl_buf));
    int repl_len = (int)strlen(repl_buf);

    /* First pass: compute exact output size */
    size_t out_size = 0;
    int match_count = 0;
    const char *cursor = txt_buf;
    regmatch_t match;

    while (regexec(&re, cursor, 1, &match, 0) == 0) {
        out_size += (size_t)match.rm_so;
        out_size += (size_t)repl_len;
        cursor += match.rm_eo;
        match_count++;
        if (match.rm_eo == 0) {
            if (*cursor) { out_size++; cursor++; }
            else break;
        }
    }
    out_size += strlen(cursor);

    if (match_count == 0) {
        regfree(&re);
        return text;
    }

    /* Second pass: build result into arena-allocated buffer */
    char *result = (char *)gray_arena_alloc(arena, out_size + 1);
    int pos = 0;
    cursor = txt_buf;

    while (regexec(&re, cursor, 1, &match, 0) == 0) {
        int pre_len = (int)match.rm_so;
        memcpy(result + pos, cursor, (size_t)pre_len);
        pos += pre_len;

        memcpy(result + pos, repl_buf, (size_t)repl_len);
        pos += repl_len;

        cursor += match.rm_eo;
        if (match.rm_eo == 0) {
            if (*cursor) result[pos++] = *cursor++;
            else break;
        }
    }

    int remaining = (int)strlen(cursor);
    memcpy(result + pos, cursor, (size_t)remaining);
    pos += remaining;
    result[pos] = '\0';

    regfree(&re);
    return (GrayString){ result, (int32_t)pos };
}

GrayArray gray_regex_split(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 8);
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        /* On bad pattern, return array with original string */
        GRAY_ARRAY_PUSH(arena, &arr, &text);
        return arr;
    }

    char txt_buf[GRAY_REGEX_TXT_BUF];
    gray_cstr(text, txt_buf, sizeof(txt_buf));

    const char *cursor = txt_buf;
    regmatch_t match;

    while (regexec(&re, cursor, 1, &match, 0) == 0) {
        /* Piece before the match */
        int32_t piece_length = (int32_t)match.rm_so;
        GrayString piece = gray_string_new(arena, cursor, piece_length);
        GRAY_ARRAY_PUSH(arena, &arr, &piece);

        cursor += match.rm_eo;
        if (match.rm_eo == 0) {
            if (*cursor) cursor++;
            else break;
        }
    }

    /* Remaining text after last match */
    int32_t remaining = (int32_t)strlen(cursor);
    GrayString last = gray_string_new(arena, cursor, remaining);
    GRAY_ARRAY_PUSH(arena, &arr, &last);

    regfree(&re);
    return arr;
}

/* _result variants */

GrayResult_string gray_regex_find_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_string r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = (GrayString){"", 0};
        r.v1 = gray_error_new(arena, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    regfree(&re);
    r.v0 = gray_regex_find(arena, pattern, text);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_regex_find_all_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = gray_array_new(arena, sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    regfree(&re);
    r.v0 = gray_regex_find_all(arena, pattern, text);
    r.v1 = NULL;
    return r;
}

GrayResult_string gray_regex_replace_result(GrayArena *arena, GrayString pattern, GrayString text, GrayString replacement) {
    GrayResult_string r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = text;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    regfree(&re);
    r.v0 = gray_regex_replace(arena, pattern, text, replacement);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_regex_split_result(GrayArena *arena, GrayString pattern, GrayString text) {
    GrayResult_array r;
    regex_t re;
    if (compile_pattern(pattern, &re, 0) != 0) {
        r.v0 = gray_array_new(arena, sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "invalid regex pattern '%.*s'",
            pattern.len, pattern.data));
        return r;
    }
    regfree(&re);
    r.v0 = gray_regex_split(arena, pattern, text);
    r.v1 = NULL;
    return r;
}
