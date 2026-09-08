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
#include "ascii.h"    /* branchless ASCII case + classification */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GrayString gray_strings_to_upper(GrayArena *arena, GrayString str) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    for (int32_t i = 0; i < str.len; i++) buf[i] = (char)gray_ascii_upper((unsigned char)str.data[i]);
    buf[str.len] = '\0';
    GrayString result = { buf, str.len };
    return result;
}

GrayString gray_strings_to_lower(GrayArena *arena, GrayString str) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    for (int32_t i = 0; i < str.len; i++) buf[i] = (char)gray_ascii_lower((unsigned char)str.data[i]);
    buf[str.len] = '\0';
    GrayString result = { buf, str.len };
    return result;
}

GrayString gray_strings_to_title(GrayArena *arena, GrayString str) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    bool at_word_start = true;
    for (int32_t i = 0; i < str.len; i++) {
        unsigned char c = (unsigned char)str.data[i];
        if (gray_ascii_is_space(c)) {
            buf[i] = (char)c;
            at_word_start = true;
            continue;
        }
        buf[i] = at_word_start ? (char)gray_ascii_upper(c) : (char)gray_ascii_lower(c);
        at_word_start = false;
    }
    buf[str.len] = '\0';
    GrayString result = { buf, str.len };
    return result;
}

/* Word-boundary splitter shared by to_snake_case / to_kebab_case /
 * to_screaming_snake_case. Every character can emit at most one separator plus
 * itself, so the worst case is twice the input length.
 *
 * The separator is deferred rather than emitted on sight, so it only produces a
 * character once a real character arrives to follow it. That drops trailing
 * separators for free and collapses runs, the same way to_camel_case defers its
 * capitalization. */
static GrayString strings_delimit_words(GrayArena *arena, GrayString str, char sep) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len * 2 + 1);
    int32_t pos = 0;
    bool pending_sep = false;
    for (int32_t i = 0; i < str.len; i++) {
        unsigned char c = (unsigned char)str.data[i];
        if (c == ' ' || c == '-' || c == '_') {
            /* Leading separators are dropped rather than opening with sep. */
            pending_sep = pos > 0;
            continue;
        }
        if (gray_ascii_is_upper(c)) {
            unsigned char prev = i > 0 ? (unsigned char)str.data[i - 1] : 0;
            unsigned char next = i + 1 < str.len ? (unsigned char)str.data[i + 1] : 0;
            /* Break after a lowercase run, and at the tail of an acronym run
             * so "HTTPServer" splits as "http_server" rather than "h_t_t_p...". */
            bool boundary = gray_ascii_is_lower(prev) || gray_ascii_is_digit(prev)
                || (gray_ascii_is_upper(prev) && gray_ascii_is_lower(next));
            if (boundary && pos > 0) pending_sep = true;
        }
        if (pending_sep) {
            buf[pos++] = sep;
            pending_sep = false;
        }
        buf[pos++] = (char)gray_ascii_lower(c);
    }
    buf[pos] = '\0';
    GrayString result = { buf, pos };
    return result;
}

GrayString gray_strings_to_snake_case(GrayArena *arena, GrayString str) {
    return strings_delimit_words(arena, str, '_');
}

GrayString gray_strings_to_kebab_case(GrayArena *arena, GrayString str) {
    return strings_delimit_words(arena, str, '-');
}

GrayString gray_strings_to_screaming_snake_case(GrayArena *arena, GrayString str) {
    return gray_strings_to_upper(arena, strings_delimit_words(arena, str, '_'));
}

/* Shared by to_camel_case (first_upper = false) and to_pascal_case
 * (first_upper = true). */
static GrayString strings_camelish(GrayArena *arena, GrayString str, bool first_upper) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    int32_t pos = 0;
    bool upper_next = first_upper;
    for (int32_t i = 0; i < str.len; i++) {
        unsigned char c = (unsigned char)str.data[i];
        if (c == '_' || c == '-' || c == ' ') {
            /* A leading separator still leaves the first real character cased by
             * first_upper; a later one always capitalizes the next word. */
            upper_next = first_upper || pos > 0;
            continue;
        }
        buf[pos++] = upper_next ? (char)gray_ascii_upper(c) : (char)gray_ascii_lower(c);
        upper_next = false;
    }
    buf[pos] = '\0';
    GrayString result = { buf, pos };
    return result;
}

GrayString gray_strings_to_camel_case(GrayArena *arena, GrayString str) {
    return strings_camelish(arena, str, false);
}

GrayString gray_strings_to_pascal_case(GrayArena *arena, GrayString str) {
    return strings_camelish(arena, str, true);
}

GrayString gray_strings_capitalize(GrayArena *arena, GrayString str) {
    if (str.len == 0) return gray_string_lit("");
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    memcpy(buf, str.data, (size_t)str.len);
    buf[0] = (char)gray_ascii_upper((unsigned char)buf[0]);
    buf[str.len] = '\0';
    GrayString result = { buf, str.len };
    return result;
}

/* Byte-oriented like the rest of the module (slice, char_at, len): max and the
 * ellipsis length are byte counts. */
GrayString gray_strings_truncate(GrayArena *arena, GrayString str, int64_t max, GrayString ellipsis) {
    if (max < ellipsis.len) {
        gray_panic_code("P0119",
            "strings.truncate: max (%lld) is smaller than the ellipsis length (%d)",
            (long long)max, (int)ellipsis.len);
    }
    if (str.len <= max) return str;
    int32_t head = (int32_t)(max - ellipsis.len);
    int32_t new_len = head + ellipsis.len;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    memcpy(buf, str.data, (size_t)head);
    memcpy(buf + head, ellipsis.data, (size_t)ellipsis.len);
    buf[new_len] = '\0';
    GrayString result = { buf, new_len };
    return result;
}

GrayString gray_strings_trim(GrayArena *arena, GrayString str) {
    int32_t start = 0, end = str.len;
    while (start < end && gray_ascii_is_space((unsigned char)str.data[start])) start++;
    while (end > start && gray_ascii_is_space((unsigned char)str.data[end - 1])) end--;
    return gray_string_new(arena, str.data + start, end - start);
}

GrayString gray_strings_trim_left(GrayArena *arena, GrayString str) {
    int32_t start = 0;
    while (start < str.len && gray_ascii_is_space((unsigned char)str.data[start])) start++;
    return gray_string_new(arena, str.data + start, str.len - start);
}

GrayString gray_strings_trim_right(GrayArena *arena, GrayString str) {
    int32_t end = str.len;
    while (end > 0 && gray_ascii_is_space((unsigned char)str.data[end - 1])) end--;
    return gray_string_new(arena, str.data, end);
}

bool gray_strings_contains(GrayString str, GrayString sub) {
    if (sub.len == 0) return true;
    if (sub.len > str.len) return false;
    for (int32_t i = 0; i <= str.len - sub.len; i++) {
        if (memcmp(str.data + i, sub.data, (size_t)sub.len) == 0) return true;
    }
    return false;
}

bool gray_strings_starts_with(GrayString str, GrayString prefix) {
    if (prefix.len > str.len) return false;
    return memcmp(str.data, prefix.data, (size_t)prefix.len) == 0;
}

bool gray_strings_ends_with(GrayString str, GrayString suffix) {
    if (suffix.len > str.len) return false;
    return memcmp(str.data + str.len - suffix.len, suffix.data, (size_t)suffix.len) == 0;
}

int64_t gray_strings_index_of(GrayString str, GrayString sub) {
    if (sub.len == 0) return 0;
    if (sub.len > str.len) return -1;
    for (int32_t i = 0; i <= str.len - sub.len; i++) {
        if (memcmp(str.data + i, sub.data, (size_t)sub.len) == 0) return i;
    }
    return -1;
}

int64_t gray_strings_last_index_of(GrayString str, GrayString sub) {
    if (sub.len == 0) return str.len;
    if (sub.len > str.len) return -1;
    for (int32_t i = str.len - sub.len; i >= 0; i--) {
        if (memcmp(str.data + i, sub.data, (size_t)sub.len) == 0) return i;
    }
    return -1;
}

int64_t gray_strings_count(GrayString str, GrayString sub) {
    if (sub.len == 0) return 0;
    int64_t count = 0;
    for (int32_t i = 0; i <= str.len - sub.len; i++) {
        if (memcmp(str.data + i, sub.data, (size_t)sub.len) == 0) {
            count++;
            i += sub.len - 1;
        }
    }
    return count;
}

bool gray_strings_is_empty(GrayString str) {
    return str.len == 0;
}

GrayString gray_strings_remove_prefix(GrayArena *arena, GrayString str, GrayString prefix) {
    if (prefix.len > str.len || memcmp(str.data, prefix.data, (size_t)prefix.len) != 0) return str;
    int32_t new_len = str.len - prefix.len;
    return gray_string_new(arena, str.data + prefix.len, new_len);
}

GrayString gray_strings_remove_suffix(GrayArena *arena, GrayString str, GrayString suffix) {
    if (suffix.len > str.len || memcmp(str.data + str.len - suffix.len, suffix.data, (size_t)suffix.len) != 0) return str;
    int32_t new_len = str.len - suffix.len;
    return gray_string_new(arena, str.data, new_len);
}

GrayString gray_strings_replace(GrayArena *arena, GrayString str, GrayString old_s, GrayString new_s) {
    if (old_s.len == 0) return str;
    /* Count occurrences to size the buffer */
    int64_t count = gray_strings_count(str, old_s);
    if (count == 0) return str;
    int64_t new_len64 = (int64_t)str.len + count * ((int64_t)new_s.len - (int64_t)old_s.len);
    if (new_len64 < 0 || new_len64 > INT32_MAX) {
        gray_panic_code("P0071", "strings.replace() result exceeds maximum string length");
    }
    int32_t new_len = (int32_t)new_len64;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    int32_t pos = 0;
    for (int32_t i = 0; i < str.len; ) {
        if (i <= str.len - old_s.len && memcmp(str.data + i, old_s.data, (size_t)old_s.len) == 0) {
            memcpy(buf + pos, new_s.data, (size_t)new_s.len);
            pos += new_s.len;
            i += old_s.len;
        } else {
            buf[pos++] = str.data[i++];
        }
    }
    buf[pos] = '\0';
    GrayString result = { buf, pos };
    return result;
}

GrayString gray_strings_repeat(GrayArena *arena, GrayString str, int64_t count) {
    if (count < 0) gray_panic_code("P0072", "strings.repeat() count cannot be negative (%lld)", (long long)count);
    if (count == 0 || str.len == 0) return gray_string_lit("");
    int64_t new_len64 = (int64_t)str.len * count;
    if (new_len64 > INT32_MAX) {
        gray_panic_code("P0073", "strings.repeat() result exceeds maximum string length");
    }
    int32_t new_len = (int32_t)new_len64;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    for (int64_t i = 0; i < count; i++) {
        memcpy(buf + i * str.len, str.data, (size_t)str.len);
    }
    buf[new_len] = '\0';
    GrayString result = { buf, new_len };
    return result;
}

GrayString gray_strings_reverse(GrayArena *arena, GrayString str) {
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    for (int32_t i = 0; i < str.len; i++) buf[i] = str.data[str.len - 1 - i];
    buf[str.len] = '\0';
    GrayString result = { buf, str.len };
    return result;
}

GrayString gray_strings_slice(GrayArena *arena, GrayString str, int64_t start, int64_t end) {
    if (start < 0) start = 0;
    if (end > str.len) end = str.len;
    if (start >= end) return gray_string_lit("");
    return gray_string_new(arena, str.data + start, (int32_t)(end - start));
}

bool gray_strings_contains_any(GrayString str, GrayString chars) {
    for (int32_t i = 0; i < str.len; i++) {
        for (int32_t j = 0; j < chars.len; j++) {
            if (str.data[i] == chars.data[j]) return true;
        }
    }
    return false;
}

bool gray_strings_equal_fold(GrayString left, GrayString right) {
    if (left.len != right.len) return false;
    for (int32_t i = 0; i < left.len; i++) {
        if (gray_ascii_lower((unsigned char)left.data[i]) != gray_ascii_lower((unsigned char)right.data[i])) return false;
    }
    return true;
}

int64_t gray_strings_compare(GrayString left, GrayString right) {
    int32_t common_len = left.len < right.len ? left.len : right.len;
    for (int32_t i = 0; i < common_len; i++) {
        unsigned char left_ch = (unsigned char)left.data[i];
        unsigned char right_ch = (unsigned char)right.data[i];
        if (left_ch != right_ch) return left_ch < right_ch ? -1 : 1;
    }
    if (left.len == right.len) return 0;
    return left.len < right.len ? -1 : 1;
}

GrayArray gray_strings_split(GrayArena *arena, GrayString str, GrayString sep) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    if (sep.len == 0) {
        GRAY_ARRAY_PUSH(arena, &arr, &str);
        return arr;
    }
    int32_t start = 0;
    for (int32_t i = 0; i <= str.len - sep.len; i++) {
        if (memcmp(str.data + i, sep.data, (size_t)sep.len) == 0) {
            GrayString part = gray_string_new(arena, str.data + start, i - start);
            GRAY_ARRAY_PUSH(arena, &arr, &part);
            i += sep.len - 1;
            start = i + 1;
        }
    }
    GrayString last = gray_string_new(arena, str.data + start, str.len - start);
    GRAY_ARRAY_PUSH(arena, &arr, &last);
    return arr;
}

GrayArray gray_strings_split_whitespace(GrayArena *arena, GrayString str) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    int32_t i = 0;
    while (i < str.len) {
        while (i < str.len && gray_ascii_is_space((unsigned char)str.data[i])) i++;
        if (i >= str.len) break;
        int32_t start = i;
        while (i < str.len && !gray_ascii_is_space((unsigned char)str.data[i])) i++;
        GrayString part = gray_string_new(arena, str.data + start, i - start);
        GRAY_ARRAY_PUSH(arena, &arr, &part);
    }
    return arr;
}

GrayArray gray_strings_split_n(GrayArena *arena, GrayString str, GrayString sep, int64_t max_parts) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    if (max_parts <= 0) return arr;
    if (sep.len == 0) {
        GRAY_ARRAY_PUSH(arena, &arr, &str);
        return arr;
    }
    int32_t start = 0;
    /* Stop splitting once one slot is left; it takes the whole remainder. */
    for (int32_t i = 0; max_parts > 1 && i <= str.len - sep.len; i++) {
        if (memcmp(str.data + i, sep.data, (size_t)sep.len) == 0) {
            GrayString part = gray_string_new(arena, str.data + start, i - start);
            GRAY_ARRAY_PUSH(arena, &arr, &part);
            i += sep.len - 1;
            start = i + 1;
            max_parts--;
        }
    }
    GrayString last = gray_string_new(arena, str.data + start, str.len - start);
    GRAY_ARRAY_PUSH(arena, &arr, &last);
    return arr;
}

GrayString gray_strings_join(GrayArena *arena, GrayArray arr, GrayString sep) {
    if (arr.len == 0) return gray_string_lit("");
    /* Calculate total length */
    int32_t total = 0;
    for (int32_t i = 0; i < arr.len; i++) {
        GrayString *part = (GrayString *)((char *)arr.data + (size_t)i * sizeof(GrayString));
        total += part->len;
        if (i > 0) total += sep.len;
    }
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
    int32_t pos = 0;
    for (int32_t i = 0; i < arr.len; i++) {
        if (i > 0) { memcpy(buf + pos, sep.data, (size_t)sep.len); pos += sep.len; }
        GrayString *part = (GrayString *)((char *)arr.data + (size_t)i * sizeof(GrayString));
        memcpy(buf + pos, part->data, (size_t)part->len);
        pos += part->len;
    }
    buf[pos] = '\0';
    GrayString result = { buf, pos };
    return result;
}


GrayArray gray_strings_to_chars(GrayArena *arena, GrayString str) {
    /* Result is exactly str.len wide; fill it with a direct byte->int32
     * widening loop the compiler can vectorize, not a call per byte. */
    GrayArray arr = gray_array_new(arena, sizeof(int32_t), str.len);
    int32_t *out = (int32_t *)arr.data;
    for (int32_t i = 0; i < str.len; i++) {
        out[i] = (int32_t)(unsigned char)str.data[i];
    }
    arr.len = str.len;
    return arr;
}

GrayString gray_strings_from_chars(GrayArena *arena, GrayArray *chars) {
    int32_t count = chars->len;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)count + 1);
    int32_t *data = (int32_t *)chars->data;
    for (int32_t i = 0; i < count; i++) {
        buf[i] = (char)data[i];
    }
    buf[count] = '\0';
    return gray_string_new(arena, buf, count);
}

char gray_strings_char_at(GrayString str, int64_t index) {
    if (index < 0 || index >= str.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)str.len);
    }
    return str.data[index];
}

/* Build a new string: bytes [0,cut) of str, then ins, then bytes [cut+drop,str.len) of str.
   `drop` is 0 for insertions and 1 for a replacement. */
static GrayString strings_splice(GrayArena *arena, GrayString str, int32_t cut,
                                 int32_t drop, GrayString ins) {
    int32_t tail = str.len - cut - drop;
    int32_t new_len = cut + ins.len + tail;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)new_len + 1);
    memcpy(buf, str.data, (size_t)cut);
    memcpy(buf + cut, ins.data, (size_t)ins.len);
    memcpy(buf + cut + ins.len, str.data + cut + drop, (size_t)tail);
    buf[new_len] = '\0';
    return (GrayString){ buf, new_len };
}

GrayString gray_strings_append_char(GrayArena *arena, GrayString str, int32_t codepoint) {
    return strings_splice(arena, str, str.len, 0, gray_builtin_char_to_utf8(arena, codepoint));
}

GrayString gray_strings_prepend_char(GrayArena *arena, GrayString str, int32_t codepoint) {
    return strings_splice(arena, str, 0, 0, gray_builtin_char_to_utf8(arena, codepoint));
}

GrayString gray_strings_insert_char_at(GrayArena *arena, GrayString str, int64_t index, int32_t codepoint) {
    if (index < 0 || index > str.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)str.len);
    }
    return strings_splice(arena, str, (int32_t)index, 0, gray_builtin_char_to_utf8(arena, codepoint));
}

GrayString gray_strings_remove_at(GrayArena *arena, GrayString str, int64_t index) {
    if (index < 0 || index >= str.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)str.len);
    }
    return strings_splice(arena, str, (int32_t)index, 1, gray_string_lit(""));
}

GrayString gray_strings_set_char_at(GrayArena *arena, GrayString str, int64_t index, int32_t codepoint) {
    if (index < 0 || index >= str.len) {
        gray_panic_code("P0082", "string index %d out of bounds (length %d)",
                      (int)index, (int)str.len);
    }
    return strings_splice(arena, str, (int32_t)index, 1, gray_builtin_char_to_utf8(arena, codepoint));
}

bool gray_strings_is_alpha(char c)      { return gray_ascii_is_alpha((unsigned char)c); }
bool gray_strings_is_digit(char c)      { return gray_ascii_is_digit((unsigned char)c); }
bool gray_strings_is_alnum(char c)      { return gray_ascii_is_alnum((unsigned char)c); }
bool gray_strings_is_whitespace(char c) { return gray_ascii_is_space((unsigned char)c); }
bool gray_strings_is_upper(char c)      { return gray_ascii_is_upper((unsigned char)c); }
bool gray_strings_is_lower(char c)      { return gray_ascii_is_lower((unsigned char)c); }

/* --- Builder --- */

#define GRAY_BUILDER_MIN_CAP 16

/* Ensure the builder can hold `extra` more bytes, growing capacity by doubling.
   The arena has no realloc, so growth is allocate-and-copy: the old buffer lives
   until the arena is reset or destroyed, the same contract as gray_array_grow. */
static void builder_ensure(GrayStringsBuilder *builder, int64_t extra) {
    int64_t need = (int64_t)builder->len + extra;
    if (need <= builder->cap) return;
    if (need > INT32_MAX) {
        gray_panic_code("P0116", "string builder size exceeds maximum string length");
    }
    int64_t new_cap = builder->cap > 0 ? builder->cap : GRAY_BUILDER_MIN_CAP;
    while (new_cap < need) new_cap *= 2;
    if (new_cap > INT32_MAX) new_cap = INT32_MAX;
    char *new_data = gray_arena_alloc_uninitialized(builder->arena, (size_t)new_cap);
    if (builder->data && builder->len > 0) {
        memcpy(new_data, builder->data, (size_t)builder->len);
    }
    builder->data = new_data;
    builder->cap = (int32_t)new_cap;
}

GrayStringsBuilder *gray_strings_builder(GrayArena *arena) {
    GrayStringsBuilder *builder = gray_arena_alloc(arena, sizeof(GrayStringsBuilder));
    builder->data = NULL;
    builder->len = 0;
    builder->cap = 0;
    builder->arena = arena;
    return builder;
}

void gray_strings_builder_reserve(GrayStringsBuilder *builder, int64_t capacity) {
    if (capacity <= builder->cap) return;
    builder_ensure(builder, capacity - builder->len);
}

void gray_strings_builder_append(GrayStringsBuilder *builder, GrayString str) {
    if (str.len <= 0) return;
    builder_ensure(builder, str.len);
    memcpy(builder->data + builder->len, str.data, (size_t)str.len);
    builder->len += str.len;
}

void gray_strings_builder_append_char(GrayStringsBuilder *builder, int32_t codepoint) {
    gray_strings_builder_append(builder, gray_builtin_char_to_utf8(builder->arena, codepoint));
}

void gray_strings_builder_append_bytes(GrayStringsBuilder *builder, GrayArray data) {
    if (data.len <= 0) return;
    builder_ensure(builder, data.len);
    if (data.elem_size == 1) {
        memcpy(builder->data + builder->len, data.data, (size_t)data.len);
    } else {
        /* A [byte] built from an array literal is stored one element per
           machine word; take the low byte of each (little-endian). */
        const unsigned char *src = (const unsigned char *)data.data;
        for (int32_t i = 0; i < data.len; i++) {
            builder->data[builder->len + i] = (char)src[(size_t)i * (size_t)data.elem_size];
        }
    }
    builder->len += data.len;
}

void gray_strings_builder_append_int(GrayStringsBuilder *builder, int64_t value) {
    char buf[24];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    builder_ensure(builder, len);
    memcpy(builder->data + builder->len, buf, (size_t)len);
    builder->len += len;
}

void gray_strings_builder_append_line(GrayStringsBuilder *builder, GrayString str) {
    gray_strings_builder_append(builder, str);
    builder_ensure(builder, 1);
    builder->data[builder->len] = '\n';
    builder->len += 1;
}

int64_t gray_strings_builder_len(GrayStringsBuilder *builder) {
    return builder->len;
}

void gray_strings_builder_clear(GrayStringsBuilder *builder) {
    builder->len = 0;
}

GrayString gray_strings_build(GrayArena *arena, GrayStringsBuilder *builder) {
    return gray_string_new(arena, builder->data ? builder->data : "", builder->len);
}
