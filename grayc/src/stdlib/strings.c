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

GrayString gray_strings_to_upper(GrayArena *arena, GrayString string) {
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    for (int32_t i = 0; i < string.len; i++) buffer[i] = (char)gray_ascii_upper((unsigned char)string.data[i]);
    buffer[string.len] = '\0';
    GrayString result = { buffer, string.len };
    return result;
}

GrayString gray_strings_to_lower(GrayArena *arena, GrayString string) {
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    for (int32_t i = 0; i < string.len; i++) buffer[i] = (char)gray_ascii_lower((unsigned char)string.data[i]);
    buffer[string.len] = '\0';
    GrayString result = { buffer, string.len };
    return result;
}

GrayString gray_strings_to_title(GrayArena *arena, GrayString string) {
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    bool at_word_start = true;
    for (int32_t i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        if (gray_ascii_is_space(character)) {
            buffer[i] = (char)character;
            at_word_start = true;
            continue;
        }
        buffer[i] = at_word_start ? (char)gray_ascii_upper(character) : (char)gray_ascii_lower(character);
        at_word_start = false;
    }
    buffer[string.len] = '\0';
    GrayString result = { buffer, string.len };
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
static GrayString strings_delimit_words(GrayArena *arena, GrayString string, char separator) {
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len * 2 + 1);
    int32_t position = 0;
    bool has_pending_separator = false;
    for (int32_t i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        if (character == ' ' || character == '-' || character == '_') {
            /* Leading separators are dropped rather than opening with sep. */
            has_pending_separator = position > 0;
            continue;
        }
        if (gray_ascii_is_upper(character)) {
            unsigned char previous_character = i > 0 ? (unsigned char)string.data[i - 1] : 0;
            unsigned char next = i + 1 < string.len ? (unsigned char)string.data[i + 1] : 0;
            /* Break after a lowercase run, and at the tail of an acronym run
             * so "HTTPServer" splits as "http_server" rather than "h_t_t_p...". */
            bool boundary = gray_ascii_is_lower(previous_character) || gray_ascii_is_digit(previous_character)
                || (gray_ascii_is_upper(previous_character) && gray_ascii_is_lower(next));
            if (boundary && position > 0) has_pending_separator = true;
        }
        if (has_pending_separator) {
            buffer[position++] = separator;
            has_pending_separator = false;
        }
        buffer[position++] = (char)gray_ascii_lower(character);
    }
    buffer[position] = '\0';
    GrayString result = { buffer, position };
    return result;
}

GrayString gray_strings_to_snake_case(GrayArena *arena, GrayString string) {
    return strings_delimit_words(arena, string, '_');
}

GrayString gray_strings_to_kebab_case(GrayArena *arena, GrayString string) {
    return strings_delimit_words(arena, string, '-');
}

GrayString gray_strings_to_screaming_snake_case(GrayArena *arena, GrayString string) {
    return gray_strings_to_upper(arena, strings_delimit_words(arena, string, '_'));
}

/* Shared by to_camel_case (first_upper = false) and to_pascal_case
 * (first_upper = true). */
static GrayString strings_camelish(GrayArena *arena, GrayString string, bool first_upper) {
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    int32_t position = 0;
    bool upper_next = first_upper;
    for (int32_t i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        if (character == '_' || character == '-' || character == ' ') {
            /* A leading separator still leaves the first real character cased by
             * first_upper; a later one always capitalizes the next word. */
            upper_next = first_upper || position > 0;
            continue;
        }
        buffer[position++] = upper_next ? (char)gray_ascii_upper(character) : (char)gray_ascii_lower(character);
        upper_next = false;
    }
    buffer[position] = '\0';
    GrayString result = { buffer, position };
    return result;
}

GrayString gray_strings_to_camel_case(GrayArena *arena, GrayString string) {
    return strings_camelish(arena, string, false);
}

GrayString gray_strings_to_pascal_case(GrayArena *arena, GrayString string) {
    return strings_camelish(arena, string, true);
}

GrayString gray_strings_capitalize(GrayArena *arena, GrayString string) {
    if (string.len == 0) return gray_string_lit("");
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    memcpy(buffer, string.data, (size_t)string.len);
    buffer[0] = (char)gray_ascii_upper((unsigned char)buffer[0]);
    buffer[string.len] = '\0';
    GrayString result = { buffer, string.len };
    return result;
}

/* Byte-oriented like the rest of the module (slice, char_at, len): max and the
 * ellipsis length are byte counts. */
GrayString gray_strings_truncate(GrayArena *arena, GrayString string, int64_t maximum, GrayString ellipsis) {
    if (maximum < ellipsis.len) {
        gray_panic_code("P0119",
            "strings.truncate: max (%lld) is smaller than the ellipsis length (%d)",
            (long long)maximum, (int)ellipsis.len);
    }
    if (string.len <= maximum) return string;
    int32_t head = (int32_t)(maximum - ellipsis.len);
    int32_t new_length = head + ellipsis.len;
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)new_length + 1);
    memcpy(buffer, string.data, (size_t)head);
    memcpy(buffer + head, ellipsis.data, (size_t)ellipsis.len);
    buffer[new_length] = '\0';
    GrayString result = { buffer, new_length };
    return result;
}

GrayString gray_strings_trim(GrayArena *arena, GrayString string) {
    int32_t start = 0, end = string.len;
    while (start < end && gray_ascii_is_space((unsigned char)string.data[start])) start++;
    while (end > start && gray_ascii_is_space((unsigned char)string.data[end - 1])) end--;
    return gray_string_new(arena, string.data + start, end - start);
}

GrayString gray_strings_trim_left(GrayArena *arena, GrayString string) {
    int32_t start = 0;
    while (start < string.len && gray_ascii_is_space((unsigned char)string.data[start])) start++;
    return gray_string_new(arena, string.data + start, string.len - start);
}

GrayString gray_strings_trim_right(GrayArena *arena, GrayString string) {
    int32_t end_index = string.len;
    while (end_index > 0 && gray_ascii_is_space((unsigned char)string.data[end_index - 1])) end_index--;
    return gray_string_new(arena, string.data, end_index);
}

bool gray_strings_contains(GrayString string, GrayString substring) {
    if (substring.len == 0) return true;
    if (substring.len > string.len) return false;
    for (int32_t i = 0; i <= string.len - substring.len; i++) {
        if (memcmp(string.data + i, substring.data, (size_t)substring.len) == 0) return true;
    }
    return false;
}

bool gray_strings_starts_with(GrayString string, GrayString prefix) {
    if (prefix.len > string.len) return false;
    return memcmp(string.data, prefix.data, (size_t)prefix.len) == 0;
}

bool gray_strings_ends_with(GrayString string, GrayString suffix) {
    if (suffix.len > string.len) return false;
    return memcmp(string.data + string.len - suffix.len, suffix.data, (size_t)suffix.len) == 0;
}

int64_t gray_strings_index_of(GrayString string, GrayString substring) {
    if (substring.len == 0) return 0;
    if (substring.len > string.len) return -1;
    for (int32_t i = 0; i <= string.len - substring.len; i++) {
        if (memcmp(string.data + i, substring.data, (size_t)substring.len) == 0) return i;
    }
    return -1;
}

int64_t gray_strings_last_index_of(GrayString string, GrayString substring) {
    if (substring.len == 0) return string.len;
    if (substring.len > string.len) return -1;
    for (int32_t i = string.len - substring.len; i >= 0; i--) {
        if (memcmp(string.data + i, substring.data, (size_t)substring.len) == 0) return i;
    }
    return -1;
}

int64_t gray_strings_count(GrayString string, GrayString substring) {
    if (substring.len == 0) return 0;
    int64_t count = 0;
    for (int32_t i = 0; i <= string.len - substring.len; i++) {
        if (memcmp(string.data + i, substring.data, (size_t)substring.len) == 0) {
            count++;
            i += substring.len - 1;
        }
    }
    return count;
}

bool gray_strings_is_empty(GrayString string) {
    return string.len == 0;
}

GrayString gray_strings_remove_prefix(GrayArena *arena, GrayString string, GrayString prefix) {
    if (prefix.len > string.len || memcmp(string.data, prefix.data, (size_t)prefix.len) != 0) return string;
    int32_t new_length = string.len - prefix.len;
    return gray_string_new(arena, string.data + prefix.len, new_length);
}

GrayString gray_strings_remove_suffix(GrayArena *arena, GrayString string, GrayString suffix) {
    if (suffix.len > string.len || memcmp(string.data + string.len - suffix.len, suffix.data, (size_t)suffix.len) != 0) return string;
    int32_t new_length = string.len - suffix.len;
    return gray_string_new(arena, string.data, new_length);
}

GrayString gray_strings_replace(GrayArena *arena, GrayString string, GrayString old_text, GrayString new_text) {
    if (old_text.len == 0) return string;
    /* Count occurrences to size the buffer */
    int64_t count = gray_strings_count(string, old_text);
    if (count == 0) return string;
    int64_t new_length_wide = (int64_t)string.len + count * ((int64_t)new_text.len - (int64_t)old_text.len);
    if (new_length_wide < 0 || new_length_wide > INT32_MAX) {
        gray_panic_code("P0071", "strings.replace() result exceeds maximum string length");
    }
    int32_t new_length = (int32_t)new_length_wide;
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)new_length + 1);
    int32_t position = 0;
    for (int32_t i = 0; i < string.len; ) {
        if (i <= string.len - old_text.len && memcmp(string.data + i, old_text.data, (size_t)old_text.len) == 0) {
            memcpy(buffer + position, new_text.data, (size_t)new_text.len);
            position += new_text.len;
            i += old_text.len;
        } else {
            buffer[position++] = string.data[i++];
        }
    }
    buffer[position] = '\0';
    GrayString result = { buffer, position };
    return result;
}

GrayString gray_strings_repeat(GrayArena *arena, GrayString string, int64_t count) {
    if (count < 0) gray_panic_code("P0072", "strings.repeat() count cannot be negative (%lld)", (long long)count);
    if (count == 0 || string.len == 0) return gray_string_lit("");
    if (count > INT32_MAX / string.len) {
        gray_panic_code("P0073", "strings.repeat() result exceeds maximum string length");
    }
    int32_t new_length = (int32_t)(string.len * count);
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)new_length + 1);
    for (int64_t i = 0; i < count; i++) {
        memcpy(buffer + i * string.len, string.data, (size_t)string.len);
    }
    buffer[new_length] = '\0';
    GrayString result = { buffer, new_length };
    return result;
}

GrayString gray_strings_reverse(GrayArena *arena, GrayString string) {
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    for (int32_t i = 0; i < string.len; i++) buffer[i] = string.data[string.len - 1 - i];
    buffer[string.len] = '\0';
    GrayString result = { buffer, string.len };
    return result;
}

GrayString gray_strings_slice(GrayArena *arena, GrayString string, int64_t start, int64_t end_index) {
    if (start < 0) start = 0;
    if (end_index > string.len) end_index = string.len;
    if (start >= end_index) return gray_string_lit("");
    return gray_string_new(arena, string.data + start, (int32_t)(end_index - start));
}

bool gray_strings_contains_any(GrayString string, GrayString chars) {
    for (int32_t i = 0; i < string.len; i++) {
        for (int32_t j = 0; j < chars.len; j++) {
            if (string.data[i] == chars.data[j]) return true;
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
    int32_t common_length = left.len < right.len ? left.len : right.len;
    for (int32_t i = 0; i < common_length; i++) {
        unsigned char left_character = (unsigned char)left.data[i];
        unsigned char right_character = (unsigned char)right.data[i];
        if (left_character != right_character) return left_character < right_character ? -1 : 1;
    }
    if (left.len == right.len) return 0;
    return left.len < right.len ? -1 : 1;
}

GrayArray gray_strings_split(GrayArena *arena, GrayString string, GrayString separator) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 4, GRAY_ELEM_STRING);
    if (separator.len == 0) {
        GRAY_ARRAY_PUSH(arena, &array, &string);
        return array;
    }
    int32_t start = 0;
    for (int32_t i = 0; i <= string.len - separator.len; i++) {
        if (memcmp(string.data + i, separator.data, (size_t)separator.len) == 0) {
            GrayString part = gray_string_new(arena, string.data + start, i - start);
            GRAY_ARRAY_PUSH(arena, &array, &part);
            i += separator.len - 1;
            start = i + 1;
        }
    }
    GrayString last = gray_string_new(arena, string.data + start, string.len - start);
    GRAY_ARRAY_PUSH(arena, &array, &last);
    return array;
}

GrayArray gray_strings_split_whitespace(GrayArena *arena, GrayString string) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 4, GRAY_ELEM_STRING);
    int32_t i = 0;
    while (i < string.len) {
        while (i < string.len && gray_ascii_is_space((unsigned char)string.data[i])) i++;
        if (i >= string.len) break;
        int32_t start = i;
        while (i < string.len && !gray_ascii_is_space((unsigned char)string.data[i])) i++;
        GrayString part = gray_string_new(arena, string.data + start, i - start);
        GRAY_ARRAY_PUSH(arena, &array, &part);
    }
    return array;
}

GrayArray gray_strings_split_n(GrayArena *arena, GrayString string, GrayString separator, int64_t maximum_parts) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 4, GRAY_ELEM_STRING);
    if (maximum_parts <= 0) return array;
    if (separator.len == 0) {
        GRAY_ARRAY_PUSH(arena, &array, &string);
        return array;
    }
    int32_t start = 0;
    /* Stop splitting once one slot is left; it takes the whole remainder. */
    for (int32_t i = 0; maximum_parts > 1 && i <= string.len - separator.len; i++) {
        if (memcmp(string.data + i, separator.data, (size_t)separator.len) == 0) {
            GrayString part = gray_string_new(arena, string.data + start, i - start);
            GRAY_ARRAY_PUSH(arena, &array, &part);
            i += separator.len - 1;
            start = i + 1;
            maximum_parts--;
        }
    }
    GrayString last = gray_string_new(arena, string.data + start, string.len - start);
    GRAY_ARRAY_PUSH(arena, &array, &last);
    return array;
}

GrayString gray_strings_join(GrayArena *arena, GrayArray array, GrayString separator) {
    if (array.len == 0) return gray_string_lit("");
    /* Calculate total length */
    int32_t total = 0;
    for (int32_t i = 0; i < array.len; i++) {
        GrayString *part = (GrayString *)((char *)array.data + (size_t)i * sizeof(GrayString));
        total += part->len;
        if (i > 0) total += separator.len;
    }
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
    int32_t position = 0;
    for (int32_t i = 0; i < array.len; i++) {
        if (i > 0) { memcpy(buffer + position, separator.data, (size_t)separator.len); position += separator.len; }
        GrayString *part = (GrayString *)((char *)array.data + (size_t)i * sizeof(GrayString));
        memcpy(buffer + position, part->data, (size_t)part->len);
        position += part->len;
    }
    buffer[position] = '\0';
    GrayString result = { buffer, position };
    return result;
}


GrayArray gray_strings_to_chars(GrayArena *arena, GrayString string) {
    /* A char is a full Unicode codepoint, not a raw byte — decode UTF-8
     * instead of widening each byte directly. Codepoint count is at most
     * string.len (one array slot per byte is an over-allocation for any
     * multi-byte content, but never too small). */
    GrayArray array = gray_array_new(arena, sizeof(int32_t), string.len, GRAY_ELEM_CHAR);
    int32_t *output = (int32_t *)array.data;
    const uint8_t *cursor = (const uint8_t *)string.data;
    const uint8_t *end_cursor = cursor + string.len;
    int32_t count = 0;
    while (cursor < end_cursor) {
        int32_t codepoint;
        cursor += gray_builtin_utf8_next(cursor, end_cursor, &codepoint);
        output[count++] = codepoint;
    }
    array.len = count;
    return array;
}

GrayString gray_strings_from_chars(GrayArena *arena, GrayArray *chars) {
    int32_t count = chars->len;
    int32_t *data = (int32_t *)chars->data;
    /* Each codepoint UTF-8-encodes to at most 4 bytes. */
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)count * 4 + 1);
    int32_t position = 0;
    for (int32_t i = 0; i < count; i++) {
        GrayString encoded = gray_builtin_char_to_utf8(arena, data[i]);
        memcpy(buffer + position, encoded.data, (size_t)encoded.len);
        position += encoded.len;
    }
    buffer[position] = '\0';
    return gray_string_new(arena, buffer, position);
}

char gray_strings_char_at(GrayString string, int64_t index) {
    if (index < 0 || index >= string.len) {
        gray_panic_code("P0082", "string index %lld out of bounds (length %d)",
                      (long long)index, (int)string.len);
    }
    return string.data[index];
}

/* Build a new string: bytes [0,cut) of str, then ins, then bytes [cut+drop,str.len) of str.
   `drop` is 0 for insertions and 1 for a replacement. */
static GrayString strings_splice(GrayArena *arena, GrayString string, int32_t cut_position,
                                 int32_t drop, GrayString insertion) {
    int32_t tail = string.len - cut_position - drop;
    int32_t new_length = cut_position + insertion.len + tail;
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)new_length + 1);
    memcpy(buffer, string.data, (size_t)cut_position);
    memcpy(buffer + cut_position, insertion.data, (size_t)insertion.len);
    memcpy(buffer + cut_position + insertion.len, string.data + cut_position + drop, (size_t)tail);
    buffer[new_length] = '\0';
    return (GrayString){ buffer, new_length };
}

GrayString gray_strings_append_char(GrayArena *arena, GrayString string, int32_t codepoint) {
    return strings_splice(arena, string, string.len, 0, gray_builtin_char_to_utf8(arena, codepoint));
}

GrayString gray_strings_prepend_char(GrayArena *arena, GrayString string, int32_t codepoint) {
    return strings_splice(arena, string, 0, 0, gray_builtin_char_to_utf8(arena, codepoint));
}

GrayString gray_strings_insert_char_at(GrayArena *arena, GrayString string, int64_t index, int32_t codepoint) {
    if (index < 0 || index > string.len) {
        gray_panic_code("P0082", "string index %lld out of bounds (length %d)",
                      (long long)index, (int)string.len);
    }
    return strings_splice(arena, string, (int32_t)index, 0, gray_builtin_char_to_utf8(arena, codepoint));
}

GrayString gray_strings_remove_at(GrayArena *arena, GrayString string, int64_t index) {
    if (index < 0 || index >= string.len) {
        gray_panic_code("P0082", "string index %lld out of bounds (length %d)",
                      (long long)index, (int)string.len);
    }
    return strings_splice(arena, string, (int32_t)index, 1, gray_string_lit(""));
}

GrayString gray_strings_set_char_at(GrayArena *arena, GrayString string, int64_t index, int32_t codepoint) {
    if (index < 0 || index >= string.len) {
        gray_panic_code("P0082", "string index %lld out of bounds (length %d)",
                      (long long)index, (int)string.len);
    }
    return strings_splice(arena, string, (int32_t)index, 1, gray_builtin_char_to_utf8(arena, codepoint));
}

bool gray_strings_is_alpha(char character)      { return gray_ascii_is_alpha((unsigned char)character); }
bool gray_strings_is_digit(char character)      { return gray_ascii_is_digit((unsigned char)character); }
bool gray_strings_is_alnum(char character)      { return gray_ascii_is_alnum((unsigned char)character); }
bool gray_strings_is_whitespace(char character) { return gray_ascii_is_space((unsigned char)character); }
bool gray_strings_is_upper(char character)      { return gray_ascii_is_upper((unsigned char)character); }
bool gray_strings_is_lower(char character)      { return gray_ascii_is_lower((unsigned char)character); }

/* --- Builder --- */

#define GRAY_BUILDER_MIN_CAPACITY 16

/* Ensure the builder can hold `extra` more bytes, growing capacity by doubling.
   The arena has no realloc, so growth is allocate-and-copy: the old buffer lives
   until the arena is reset or destroyed, the same contract as gray_array_grow. */
static void builder_ensure(GrayStringsBuilder *builder, int64_t extra) {
    int64_t need = (int64_t)builder->len + extra;
    if (need <= builder->capacity) return;
    if (need > INT32_MAX) {
        gray_panic_code("P0116", "string builder size exceeds maximum string length");
    }
    int64_t new_capacity = builder->capacity > 0 ? builder->capacity : GRAY_BUILDER_MIN_CAPACITY;
    while (new_capacity < need) new_capacity *= 2;
    if (new_capacity > INT32_MAX) new_capacity = INT32_MAX;
    char *new_data = gray_arena_alloc_uninitialized(builder->arena, (size_t)new_capacity);
    if (builder->data && builder->len > 0) {
        memcpy(new_data, builder->data, (size_t)builder->len);
    }
    builder->data = new_data;
    builder->capacity = (int32_t)new_capacity;
}

GrayStringsBuilder *gray_strings_builder(GrayArena *arena) {
    GrayStringsBuilder *builder = gray_arena_alloc(arena, sizeof(GrayStringsBuilder));
    builder->data = NULL;
    builder->len = 0;
    builder->capacity = 0;
    builder->arena = arena;
    return builder;
}

void gray_strings_builder_reserve(GrayStringsBuilder *builder, int64_t capacity) {
    if (capacity <= builder->capacity) return;
    builder_ensure(builder, capacity - builder->len);
}

void gray_strings_builder_append(GrayStringsBuilder *builder, GrayString string) {
    if (string.len <= 0) return;
    builder_ensure(builder, string.len);
    memcpy(builder->data + builder->len, string.data, (size_t)string.len);
    builder->len += string.len;
}

void gray_strings_builder_append_char(GrayStringsBuilder *builder, int32_t codepoint) {
    gray_strings_builder_append(builder, gray_builtin_char_to_utf8(builder->arena, codepoint));
}

void gray_strings_builder_append_bytes(GrayStringsBuilder *builder, GrayArray data) {
    if (data.len <= 0) return;
    builder_ensure(builder, data.len);
    memcpy(builder->data + builder->len, data.data, (size_t)data.len);
    builder->len += data.len;
}

void gray_strings_builder_append_i64(GrayStringsBuilder *builder, int64_t value) {
    char buffer[24];
    int length = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    builder_ensure(builder, length);
    memcpy(builder->data + builder->len, buffer, (size_t)length);
    builder->len += length;
}

void gray_strings_builder_append_line(GrayStringsBuilder *builder, GrayString string) {
    gray_strings_builder_append(builder, string);
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
