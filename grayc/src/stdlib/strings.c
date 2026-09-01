/*
 * strings.c — Implementation of the strings stdlib module.
 * Provides case conversion, trimming, splitting, joining, searching,
 * replacing, padding, and character classification on GrayString values.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "strings.h"
#include "builtins.h" /* gray_builtin_char_to_utf8 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

GrayString gray_strings_to_upper(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)s.len + 1);
    for (int32_t i = 0; i < s.len; i++) buf[i] = (char)toupper((unsigned char)s.data[i]);
    buf[s.len] = '\0';
    GrayString r = { buf, s.len };
    return r;
}

GrayString gray_strings_to_lower(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)s.len + 1);
    for (int32_t i = 0; i < s.len; i++) buf[i] = (char)tolower((unsigned char)s.data[i]);
    buf[s.len] = '\0';
    GrayString r = { buf, s.len };
    return r;
}

GrayString gray_strings_to_title(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)s.len + 1);
    bool at_word_start = true;
    for (int32_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (isspace(c)) {
            buf[i] = (char)c;
            at_word_start = true;
            continue;
        }
        buf[i] = at_word_start ? (char)toupper(c) : (char)tolower(c);
        at_word_start = false;
    }
    buf[s.len] = '\0';
    GrayString r = { buf, s.len };
    return r;
}

/* Every character can emit at most one separator plus itself, so the worst
 * case is twice the input length. */
GrayString gray_strings_to_snake_case(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)s.len * 2 + 1);
    int32_t pos = 0;
    /* Deferred rather than emitted on sight, so a separator only produces a
     * '_' once a real character arrives to follow it. That drops trailing
     * separators for free and collapses runs, the same way to_camel_case
     * defers its capitalization. */
    bool pending_sep = false;
    for (int32_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == ' ' || c == '-' || c == '_') {
            /* Leading separators are dropped rather than opening with '_'. */
            pending_sep = pos > 0;
            continue;
        }
        if (isupper(c)) {
            unsigned char prev = i > 0 ? (unsigned char)s.data[i - 1] : 0;
            unsigned char next = i + 1 < s.len ? (unsigned char)s.data[i + 1] : 0;
            /* Break after a lowercase run, and at the tail of an acronym run
             * so "HTTPServer" splits as "http_server" rather than "h_t_t_p...". */
            bool boundary = islower(prev) || isdigit(prev) || (isupper(prev) && islower(next));
            if (boundary && pos > 0) pending_sep = true;
        }
        if (pending_sep) {
            buf[pos++] = '_';
            pending_sep = false;
        }
        buf[pos++] = (char)tolower(c);
    }
    buf[pos] = '\0';
    GrayString r = { buf, pos };
    return r;
}

GrayString gray_strings_to_camel_case(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)s.len + 1);
    int32_t pos = 0;
    bool upper_next = false;
    for (int32_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '_' || c == '-' || c == ' ') {
            /* Leading separators are dropped rather than capitalizing the
             * first word. */
            upper_next = pos > 0;
            continue;
        }
        buf[pos++] = upper_next ? (char)toupper(c) : (char)tolower(c);
        upper_next = false;
    }
    buf[pos] = '\0';
    GrayString r = { buf, pos };
    return r;
}

GrayString gray_strings_trim(GrayArena *arena, GrayString s) {
    int32_t start = 0, end = s.len;
    while (start < end && isspace((unsigned char)s.data[start])) start++;
    while (end > start && isspace((unsigned char)s.data[end - 1])) end--;
    return gray_string_new(arena, s.data + start, end - start);
}

GrayString gray_strings_trim_left(GrayArena *arena, GrayString s) {
    int32_t start = 0;
    while (start < s.len && isspace((unsigned char)s.data[start])) start++;
    return gray_string_new(arena, s.data + start, s.len - start);
}

GrayString gray_strings_trim_right(GrayArena *arena, GrayString s) {
    int32_t end = s.len;
    while (end > 0 && isspace((unsigned char)s.data[end - 1])) end--;
    return gray_string_new(arena, s.data, end);
}

bool gray_strings_contains(GrayString s, GrayString sub) {
    if (sub.len == 0) return true;
    if (sub.len > s.len) return false;
    for (int32_t i = 0; i <= s.len - sub.len; i++) {
        if (memcmp(s.data + i, sub.data, (size_t)sub.len) == 0) return true;
    }
    return false;
}

bool gray_strings_starts_with(GrayString s, GrayString prefix) {
    if (prefix.len > s.len) return false;
    return memcmp(s.data, prefix.data, (size_t)prefix.len) == 0;
}

bool gray_strings_ends_with(GrayString s, GrayString suffix) {
    if (suffix.len > s.len) return false;
    return memcmp(s.data + s.len - suffix.len, suffix.data, (size_t)suffix.len) == 0;
}

int64_t gray_strings_index_of(GrayString s, GrayString sub) {
    if (sub.len == 0) return 0;
    if (sub.len > s.len) return -1;
    for (int32_t i = 0; i <= s.len - sub.len; i++) {
        if (memcmp(s.data + i, sub.data, (size_t)sub.len) == 0) return i;
    }
    return -1;
}

int64_t gray_strings_last_index_of(GrayString s, GrayString sub) {
    if (sub.len == 0) return s.len;
    if (sub.len > s.len) return -1;
    for (int32_t i = s.len - sub.len; i >= 0; i--) {
        if (memcmp(s.data + i, sub.data, (size_t)sub.len) == 0) return i;
    }
    return -1;
}

int64_t gray_strings_count(GrayString s, GrayString sub) {
    if (sub.len == 0) return 0;
    int64_t count = 0;
    for (int32_t i = 0; i <= s.len - sub.len; i++) {
        if (memcmp(s.data + i, sub.data, (size_t)sub.len) == 0) {
            count++;
            i += sub.len - 1;
        }
    }
    return count;
}

bool gray_strings_is_empty(GrayString s) {
    return s.len == 0;
}

GrayString gray_strings_remove_prefix(GrayArena *arena, GrayString s, GrayString prefix) {
    if (prefix.len > s.len || memcmp(s.data, prefix.data, (size_t)prefix.len) != 0) return s;
    int32_t new_len = s.len - prefix.len;
    return gray_string_new(arena, s.data + prefix.len, new_len);
}

GrayString gray_strings_remove_suffix(GrayArena *arena, GrayString s, GrayString suffix) {
    if (suffix.len > s.len || memcmp(s.data + s.len - suffix.len, suffix.data, (size_t)suffix.len) != 0) return s;
    int32_t new_len = s.len - suffix.len;
    return gray_string_new(arena, s.data, new_len);
}

GrayString gray_strings_replace(GrayArena *arena, GrayString s, GrayString old_s, GrayString new_s) {
    if (old_s.len == 0) return s;
    /* Count occurrences to size the buffer */
    int64_t count = gray_strings_count(s, old_s);
    if (count == 0) return s;
    int64_t new_len64 = (int64_t)s.len + count * ((int64_t)new_s.len - (int64_t)old_s.len);
    if (new_len64 < 0 || new_len64 > INT32_MAX) {
        gray_panic_code("P0071", "strings.replace() result exceeds maximum string length");
    }
    int32_t new_len = (int32_t)new_len64;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    int32_t pos = 0;
    for (int32_t i = 0; i < s.len; ) {
        if (i <= s.len - old_s.len && memcmp(s.data + i, old_s.data, (size_t)old_s.len) == 0) {
            memcpy(buf + pos, new_s.data, (size_t)new_s.len);
            pos += new_s.len;
            i += old_s.len;
        } else {
            buf[pos++] = s.data[i++];
        }
    }
    buf[pos] = '\0';
    GrayString r = { buf, pos };
    return r;
}

GrayString gray_strings_repeat(GrayArena *arena, GrayString s, int64_t count) {
    if (count < 0) gray_panic_code("P0072", "strings.repeat() count cannot be negative (%lld)", (long long)count);
    if (count == 0 || s.len == 0) return gray_string_lit("");
    int64_t new_len64 = (int64_t)s.len * count;
    if (new_len64 > INT32_MAX) {
        gray_panic_code("P0073", "strings.repeat() result exceeds maximum string length");
    }
    int32_t new_len = (int32_t)new_len64;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    for (int64_t i = 0; i < count; i++) {
        memcpy(buf + i * s.len, s.data, (size_t)s.len);
    }
    buf[new_len] = '\0';
    GrayString r = { buf, new_len };
    return r;
}

GrayString gray_strings_reverse(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)s.len + 1);
    for (int32_t i = 0; i < s.len; i++) buf[i] = s.data[s.len - 1 - i];
    buf[s.len] = '\0';
    GrayString r = { buf, s.len };
    return r;
}

GrayString gray_strings_slice(GrayArena *arena, GrayString s, int64_t start, int64_t end) {
    if (start < 0) start = 0;
    if (end > s.len) end = s.len;
    if (start >= end) return gray_string_lit("");
    return gray_string_new(arena, s.data + start, (int32_t)(end - start));
}

bool gray_strings_contains_any(GrayString s, GrayString chars) {
    for (int32_t i = 0; i < s.len; i++) {
        for (int32_t j = 0; j < chars.len; j++) {
            if (s.data[i] == chars.data[j]) return true;
        }
    }
    return false;
}

bool gray_strings_equal_fold(GrayString a, GrayString b) {
    if (a.len != b.len) return false;
    for (int32_t i = 0; i < a.len; i++) {
        if (tolower((unsigned char)a.data[i]) != tolower((unsigned char)b.data[i])) return false;
    }
    return true;
}

int64_t gray_strings_compare(GrayString a, GrayString b) {
    int32_t n = a.len < b.len ? a.len : b.len;
    for (int32_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.len == b.len) return 0;
    return a.len < b.len ? -1 : 1;
}

GrayArray gray_strings_split(GrayArena *arena, GrayString s, GrayString sep) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    if (sep.len == 0) {
        GRAY_ARRAY_PUSH(arena, &arr, &s);
        return arr;
    }
    int32_t start = 0;
    for (int32_t i = 0; i <= s.len - sep.len; i++) {
        if (memcmp(s.data + i, sep.data, (size_t)sep.len) == 0) {
            GrayString part = gray_string_new(arena, s.data + start, i - start);
            GRAY_ARRAY_PUSH(arena, &arr, &part);
            i += sep.len - 1;
            start = i + 1;
        }
    }
    GrayString last = gray_string_new(arena, s.data + start, s.len - start);
    GRAY_ARRAY_PUSH(arena, &arr, &last);
    return arr;
}

GrayArray gray_strings_split_whitespace(GrayArena *arena, GrayString s) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    int32_t i = 0;
    while (i < s.len) {
        while (i < s.len && isspace((unsigned char)s.data[i])) i++;
        if (i >= s.len) break;
        int32_t start = i;
        while (i < s.len && !isspace((unsigned char)s.data[i])) i++;
        GrayString part = gray_string_new(arena, s.data + start, i - start);
        GRAY_ARRAY_PUSH(arena, &arr, &part);
    }
    return arr;
}

GrayArray gray_strings_split_n(GrayArena *arena, GrayString s, GrayString sep, int64_t n) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    if (n <= 0) return arr;
    if (sep.len == 0) {
        GRAY_ARRAY_PUSH(arena, &arr, &s);
        return arr;
    }
    int32_t start = 0;
    /* Stop splitting once one slot is left; it takes the whole remainder. */
    for (int32_t i = 0; n > 1 && i <= s.len - sep.len; i++) {
        if (memcmp(s.data + i, sep.data, (size_t)sep.len) == 0) {
            GrayString part = gray_string_new(arena, s.data + start, i - start);
            GRAY_ARRAY_PUSH(arena, &arr, &part);
            i += sep.len - 1;
            start = i + 1;
            n--;
        }
    }
    GrayString last = gray_string_new(arena, s.data + start, s.len - start);
    GRAY_ARRAY_PUSH(arena, &arr, &last);
    return arr;
}

GrayString gray_strings_join(GrayArena *arena, GrayArray arr, GrayString sep) {
    if (arr.len == 0) return gray_string_lit("");
    /* Calculate total length */
    int32_t total = 0;
    for (int32_t i = 0; i < arr.len; i++) {
        GrayString *s = (GrayString *)((char *)arr.data + (size_t)i * sizeof(GrayString));
        total += s->len;
        if (i > 0) total += sep.len;
    }
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
    int32_t pos = 0;
    for (int32_t i = 0; i < arr.len; i++) {
        if (i > 0) { memcpy(buf + pos, sep.data, (size_t)sep.len); pos += sep.len; }
        GrayString *s = (GrayString *)((char *)arr.data + (size_t)i * sizeof(GrayString));
        memcpy(buf + pos, s->data, (size_t)s->len);
        pos += s->len;
    }
    buf[pos] = '\0';
    GrayString r = { buf, pos };
    return r;
}


GrayArray gray_strings_to_chars(GrayArena *arena, GrayString s) {
    GrayArray arr = gray_array_new(arena, sizeof(int32_t), s.len);
    for (int32_t i = 0; i < s.len; i++) {
        int32_t c = (int32_t)(unsigned char)s.data[i];
        GRAY_ARRAY_PUSH(arena, &arr, &c);
    }
    return arr;
}

GrayString gray_strings_from_chars(GrayArena *arena, GrayArray *chars) {
    int32_t n = chars->len;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)n + 1);
    int32_t *data = (int32_t *)chars->data;
    for (int32_t i = 0; i < n; i++) {
        buf[i] = (char)data[i];
    }
    buf[n] = '\0';
    return gray_string_new(arena, buf, n);
}

char gray_strings_char_at(GrayString s, int64_t index) {
    if (index < 0 || index >= s.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)s.len);
    }
    return s.data[index];
}

/* Build a new string: bytes [0,cut) of s, then ins, then bytes [cut+drop,s.len) of s.
   `drop` is 0 for insertions and 1 for a replacement. */
static GrayString strings_splice(GrayArena *arena, GrayString s, int32_t cut,
                                 int32_t drop, GrayString ins) {
    int32_t tail = s.len - cut - drop;
    int32_t new_len = cut + ins.len + tail;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    memcpy(buf, s.data, (size_t)cut);
    memcpy(buf + cut, ins.data, (size_t)ins.len);
    memcpy(buf + cut + ins.len, s.data + cut + drop, (size_t)tail);
    buf[new_len] = '\0';
    return (GrayString){ buf, new_len };
}

GrayString gray_strings_append_char(GrayArena *arena, GrayString s, int32_t c) {
    return strings_splice(arena, s, s.len, 0, gray_builtin_char_to_utf8(arena, c));
}

GrayString gray_strings_prepend_char(GrayArena *arena, GrayString s, int32_t c) {
    return strings_splice(arena, s, 0, 0, gray_builtin_char_to_utf8(arena, c));
}

GrayString gray_strings_insert_char_at(GrayArena *arena, GrayString s, int64_t index, int32_t c) {
    if (index < 0 || index > s.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)s.len);
    }
    return strings_splice(arena, s, (int32_t)index, 0, gray_builtin_char_to_utf8(arena, c));
}

GrayString gray_strings_remove_at(GrayArena *arena, GrayString s, int64_t index) {
    if (index < 0 || index >= s.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)s.len);
    }
    return strings_splice(arena, s, (int32_t)index, 1, gray_string_lit(""));
}

GrayString gray_strings_set_char_at(GrayArena *arena, GrayString s, int64_t index, int32_t c) {
    if (index < 0 || index >= s.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)s.len);
    }
    return strings_splice(arena, s, (int32_t)index, 1, gray_builtin_char_to_utf8(arena, c));
}

bool gray_strings_is_alpha(char c)      { return isalpha((unsigned char)c) != 0; }
bool gray_strings_is_digit(char c)      { return isdigit((unsigned char)c) != 0; }
bool gray_strings_is_alnum(char c)      { return isalnum((unsigned char)c) != 0; }
bool gray_strings_is_whitespace(char c) { return isspace((unsigned char)c) != 0; }
bool gray_strings_is_upper(char c)      { return isupper((unsigned char)c) != 0; }
bool gray_strings_is_lower(char c)      { return islower((unsigned char)c) != 0; }
