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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

GrayString gray_strings_to_upper(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc(arena, (size_t)s.len + 1);
    for (int32_t i = 0; i < s.len; i++) buf[i] = (char)toupper((unsigned char)s.data[i]);
    buf[s.len] = '\0';
    GrayString r = { buf, s.len };
    return r;
}

GrayString gray_strings_to_lower(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc(arena, (size_t)s.len + 1);
    for (int32_t i = 0; i < s.len; i++) buf[i] = (char)tolower((unsigned char)s.data[i]);
    buf[s.len] = '\0';
    GrayString r = { buf, s.len };
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
    char *buf = gray_arena_alloc(arena, (size_t)new_len + 1);
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
    char *buf = gray_arena_alloc(arena, (size_t)new_len + 1);
    for (int64_t i = 0; i < count; i++) {
        memcpy(buf + i * s.len, s.data, (size_t)s.len);
    }
    buf[new_len] = '\0';
    GrayString r = { buf, new_len };
    return r;
}

GrayString gray_strings_reverse(GrayArena *arena, GrayString s) {
    char *buf = gray_arena_alloc(arena, (size_t)s.len + 1);
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

GrayString gray_strings_join(GrayArena *arena, GrayArray arr, GrayString sep) {
    if (arr.len == 0) return gray_string_lit("");
    /* Calculate total length */
    int32_t total = 0;
    for (int32_t i = 0; i < arr.len; i++) {
        GrayString *s = (GrayString *)((char *)arr.data + (size_t)i * sizeof(GrayString));
        total += s->len;
        if (i > 0) total += sep.len;
    }
    char *buf = gray_arena_alloc(arena, (size_t)total + 1);
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
    char *buf = gray_arena_alloc(arena, (size_t)n + 1);
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

bool gray_strings_is_alpha(char c)      { return isalpha((unsigned char)c) != 0; }
bool gray_strings_is_digit(char c)      { return isdigit((unsigned char)c) != 0; }
bool gray_strings_is_alnum(char c)      { return isalnum((unsigned char)c) != 0; }
bool gray_strings_is_whitespace(char c) { return isspace((unsigned char)c) != 0; }
bool gray_strings_is_upper(char c)      { return isupper((unsigned char)c) != 0; }
bool gray_strings_is_lower(char c)      { return islower((unsigned char)c) != 0; }
