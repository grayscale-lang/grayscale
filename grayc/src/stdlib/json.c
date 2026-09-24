/*
 * json.c — Implementation of the json stdlib module.
 * Minimal recursive-descent JSON parser and emitter supporting
 * strings, numbers, bools, null, objects, and arrays. Objects are
 * represented as GrayMap[string:string].
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "json.h"
#include "strconv.h"
#include "../runtime/bigint.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>

/* --- Encoder --- */

/* Exact byte count that json_append_escaped would write (includes quotes). */
size_t json_escaped_len(GrayString string) {
    size_t byte_count = 2; /* opening + closing quote */
    for (int32_t i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        if (character == '"' || character == '\\') byte_count += 2;
        else if (character == '\b' || character == '\f' || character == '\n' || character == '\r' || character == '\t') byte_count += 2;
        else if (character < 0x20) byte_count += 6; /* \uXXXX */
        else byte_count += 1;
    }
    return byte_count;
}

void json_append_escaped(char *buffer, int *position, GrayString string) {
    static const char hex[] = "0123456789abcdef";
    buffer[(*position)++] = '"';
    for (int32_t i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        if (character == '"') { buffer[(*position)++] = '\\'; buffer[(*position)++] = '"'; }
        else if (character == '\\') { buffer[(*position)++] = '\\'; buffer[(*position)++] = '\\'; }
        else if (character == '\b') { buffer[(*position)++] = '\\'; buffer[(*position)++] = 'b'; }
        else if (character == '\f') { buffer[(*position)++] = '\\'; buffer[(*position)++] = 'f'; }
        else if (character == '\n') { buffer[(*position)++] = '\\'; buffer[(*position)++] = 'n'; }
        else if (character == '\r') { buffer[(*position)++] = '\\'; buffer[(*position)++] = 'r'; }
        else if (character == '\t') { buffer[(*position)++] = '\\'; buffer[(*position)++] = 't'; }
        else if (character < 0x20) {
            buffer[(*position)++] = '\\'; buffer[(*position)++] = 'u';
            buffer[(*position)++] = '0'; buffer[(*position)++] = '0';
            buffer[(*position)++] = hex[character >> 4]; buffer[(*position)++] = hex[character & 0xf];
        }
        else buffer[(*position)++] = (char)character;
    }
    buffer[(*position)++] = '"';
}

/* Enum field helpers. Both fall through every variant, matching the linear
 * scan gray_enum_cast_check uses for a plain-enum cast; a #json struct's
 * enum field count is small enough that this never needs a table. */
int64_t gray_json_enum_from_number(GrayString raw_text, int64_t value,
    const int64_t *variants, int32_t count, const char *type_name) {
    for (int32_t i = 0; i < count; i++) {
        if (variants[i] == value) return value;
    }
    char buffer[64];
    int length = raw_text.len < (int32_t)sizeof(buffer) - 1 ? raw_text.len : (int32_t)sizeof(buffer) - 1;
    memcpy(buffer, raw_text.data, (size_t)length);
    buffer[length] = '\0';
    gray_panic_code("P0129", "cannot convert '%s' to enum %s", buffer, type_name);
    return value;
}

GrayString gray_json_enum_from_str(GrayString raw_text,
    const GrayString *variants, int32_t count, const char *type_name) {
    for (int32_t i = 0; i < count; i++) {
        if (variants[i].len == raw_text.len &&
            (raw_text.len == 0 || memcmp(variants[i].data, raw_text.data, (size_t)raw_text.len) == 0)) {
            return raw_text;
        }
    }
    char buffer[64];
    int length = raw_text.len < (int32_t)sizeof(buffer) - 1 ? raw_text.len : (int32_t)sizeof(buffer) - 1;
    memcpy(buffer, raw_text.data, (size_t)length);
    buffer[length] = '\0';
    gray_panic_code("P0129", "cannot convert '%s' to enum %s", buffer, type_name);
    return raw_text;
}

/* Value kind for the shared map encoder. map[string:string] values are always
 * quoted (STRING) — never infer JSON types from string content. */
typedef enum {
    JSON_MAP_VAL_STRING,
    JSON_MAP_VAL_INT,
    JSON_MAP_VAL_UINT,
    JSON_MAP_VAL_FLOAT,
    JSON_MAP_VAL_BOOL,
} JsonMapValKind;

/* Two-pass encoder shared by every gray_json_encode_map* entry point. The only
 * per-type variation is the value's byte budget (pass 1) and how it is written
 * (pass 2); everything else — order walk, tombstone skip, key escaping, comma
 * and brace framing — is identical.
 *
 * The per-type budgets below are worst-case exact, with one comma of slack for
 * the final entry, so pass 1 and pass 2 must stay in step for each kind:
 *   STRING  json_escaped_len(*val)   quoted + escaped
 *   INT     21   "-9223372036854775808" + NUL
 *   FLOAT   24   %g
 *   BOOL    4 or 5   "true" / "false"
 * The INT/FLOAT snprintf never truncates given those budgets; the else branch
 * is defensive, and clamps pos so the closing brace and NUL stay in bounds. */
static GrayString json_encode_map_typed(GrayArena *arena, GrayMap *map, JsonMapValKind kind) {
    /* Pass 1: size */
    size_t need = 2; /* { } */
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t i = map->order[order_index];
        if (i < 0) continue;
        need += 1; /* comma */
        GrayString *key = (GrayString *)((char *)map->keys + (size_t)i * (size_t)map->key_size);
        void *value = (char *)map->values + (size_t)i * (size_t)map->value_size;
        need += json_escaped_len(*key) + 1 /* colon */;
        switch (kind) {
            case JSON_MAP_VAL_STRING: need += json_escaped_len(*(GrayString *)value); break;
            case JSON_MAP_VAL_INT:
            case JSON_MAP_VAL_UINT:   need += 21; break;
            case JSON_MAP_VAL_FLOAT:  need += 24; break;
            case JSON_MAP_VAL_BOOL:   need += *(bool *)value ? 4 : 5; break;
        }
    }
    /* Pass 2: write */
    char *buffer = gray_arena_alloc_uninitialized(arena, need + 1);
    int position = 0;
    buffer[position++] = '{';
    bool json_first = true;
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t i = map->order[order_index];
        if (i < 0) continue;
        if (!json_first) { buffer[position++] = ','; }
        json_first = false;
        GrayString *key = (GrayString *)((char *)map->keys + (size_t)i * (size_t)map->key_size);
        void *value = (char *)map->values + (size_t)i * (size_t)map->value_size;
        json_append_escaped(buffer, &position, *key);
        buffer[position++] = ':';
        bool truncated = false;
        switch (kind) {
            case JSON_MAP_VAL_STRING:
                json_append_escaped(buffer, &position, *(GrayString *)value);
                break;
            case JSON_MAP_VAL_INT: {
                int written = snprintf(buffer + position, need + 1 - (size_t)position, "%" PRId64,
                    gray_elem_to_i64(map->value_kind, value));
                if (written > 0 && (size_t)written < need + 1 - (size_t)position) position += written;
                else truncated = true;
                break;
            }
            case JSON_MAP_VAL_UINT: {
                int written = snprintf(buffer + position, need + 1 - (size_t)position, "%" PRIu64,
                    gray_elem_to_u64(map->value_kind, value));
                if (written > 0 && (size_t)written < need + 1 - (size_t)position) position += written;
                else truncated = true;
                break;
            }
            case JSON_MAP_VAL_FLOAT: {
                int written = snprintf(buffer + position, need + 1 - (size_t)position, "%g",
                    gray_elem_to_double(map->value_kind, value));
                if (written > 0 && (size_t)written < need + 1 - (size_t)position) position += written;
                else truncated = true;
                break;
            }
            case JSON_MAP_VAL_BOOL:
                if (*(bool *)value) { memcpy(buffer + position, "true", 4); position += 4; }
                else { memcpy(buffer + position, "false", 5); position += 5; }
                break;
        }
        if (truncated) { position = (int)need - 1; break; }
    }
    buffer[position++] = '}';
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

GrayString gray_json_encode_map(GrayArena *arena, GrayMap *map) {
    return json_encode_map_typed(arena, map, JSON_MAP_VAL_STRING);
}

/* --- Array Encoders --- */

GrayString gray_json_encode_array_int(GrayArena *arena, GrayArray *array) {
    /* 21 chars max per int64 + comma, plus brackets + nul */
    size_t need = 2 + (array->len > 0 ? (size_t)array->len * 22 - 1 : 0);
    char *buffer = gray_arena_alloc_uninitialized(arena, need + 1);
    int position = 0;
    buffer[position++] = '[';
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) { buffer[position++] = ','; }
        int64_t value = gray_elem_to_i64(array->elem_kind, (char *)array->data + (size_t)i * (size_t)array->elem_size);
        int written = snprintf(buffer + position, need + 1 - (size_t)position, "%" PRId64, value);
        if (written > 0 && (size_t)written < need + 1 - (size_t)position) position += written;
        /* Defensive: clamp so the closing bracket and NUL stay in bounds. */
        else { position = (int)need - 1; break; }
    }
    buffer[position++] = ']';
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

GrayString gray_json_encode_array_uint(GrayArena *arena, GrayArray *array) {
    size_t need = 2 + (array->len > 0 ? (size_t)array->len * 22 - 1 : 0);
    char *buffer = gray_arena_alloc_uninitialized(arena, need + 1);
    int position = 0;
    buffer[position++] = '[';
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) { buffer[position++] = ','; }
        uint64_t value = gray_elem_to_u64(array->elem_kind, (char *)array->data + (size_t)i * (size_t)array->elem_size);
        int written = snprintf(buffer + position, need + 1 - (size_t)position, "%" PRIu64, value);
        if (written > 0 && (size_t)written < need + 1 - (size_t)position) position += written;
        else { position = (int)need - 1; break; }
    }
    buffer[position++] = ']';
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

GrayString gray_json_encode_array_float(GrayArena *arena, GrayArray *array) {
    /* 24 chars max per %g double + comma, plus brackets + nul */
    size_t need = 2 + (array->len > 0 ? (size_t)array->len * 25 - 1 : 0);
    char *buffer = gray_arena_alloc_uninitialized(arena, need + 1);
    int position = 0;
    buffer[position++] = '[';
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) { buffer[position++] = ','; }
        double value = gray_elem_to_double(array->elem_kind, (char *)array->data + (size_t)i * (size_t)array->elem_size);
        int written = snprintf(buffer + position, need + 1 - (size_t)position, "%g", value);
        if (written > 0 && (size_t)written < need + 1 - (size_t)position) position += written;
        /* Defensive: clamp so the closing bracket and NUL stay in bounds. */
        else { position = (int)need - 1; break; }
    }
    buffer[position++] = ']';
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

/* Scalar json.encode(string): a single quoted, escaped JSON string. */
GrayString gray_json_encode_string(GrayArena *arena, GrayString string) {
    char *buffer = gray_arena_alloc_uninitialized(arena, json_escaped_len(string) + 1);
    int position = 0;
    json_append_escaped(buffer, &position, string);
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

GrayString gray_json_encode_array_string(GrayArena *arena, GrayArray *array) {
    /* Pass 1: exact size */
    size_t need = 2; /* [ ] */
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) need += 1; /* comma */
        GrayString *value = (GrayString *)((char *)array->data + (size_t)i * (size_t)array->elem_size);
        need += json_escaped_len(*value);
    }
    /* Pass 2: write */
    char *buffer = gray_arena_alloc_uninitialized(arena, need + 1);
    int position = 0;
    buffer[position++] = '[';
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) { buffer[position++] = ','; }
        GrayString *value = (GrayString *)((char *)array->data + (size_t)i * (size_t)array->elem_size);
        json_append_escaped(buffer, &position, *value);
    }
    buffer[position++] = ']';
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

GrayString gray_json_encode_array_bool(GrayArena *arena, GrayArray *array) {
    /* Pass 1: exact size */
    size_t need = 2; /* [ ] */
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) need += 1;
        bool value = *(bool *)((char *)array->data + (size_t)i * (size_t)array->elem_size);
        need += value ? 4 : 5;
    }
    /* Pass 2: write */
    char *buffer = gray_arena_alloc_uninitialized(arena, need + 1);
    int position = 0;
    buffer[position++] = '[';
    for (int32_t i = 0; i < array->len; i++) {
        if (i > 0) { buffer[position++] = ','; }
        bool value = *(bool *)((char *)array->data + (size_t)i * (size_t)array->elem_size);
        if (value) { memcpy(buffer + position, "true", 4); position += 4; }
        else { memcpy(buffer + position, "false", 5); position += 5; }
    }
    buffer[position++] = ']';
    buffer[position] = '\0';
    return (GrayString){ buffer, (int32_t)position };
}

/* --- Typed Map Encoders --- */

GrayString gray_json_encode_map_int(GrayArena *arena, GrayMap *map) {
    return json_encode_map_typed(arena, map, JSON_MAP_VAL_INT);
}

GrayString gray_json_encode_map_uint(GrayArena *arena, GrayMap *map) {
    return json_encode_map_typed(arena, map, JSON_MAP_VAL_UINT);
}

GrayString gray_json_encode_map_float(GrayArena *arena, GrayMap *map) {
    return json_encode_map_typed(arena, map, JSON_MAP_VAL_FLOAT);
}

GrayString gray_json_encode_map_bool(GrayArena *arena, GrayMap *map) {
    return json_encode_map_typed(arena, map, JSON_MAP_VAL_BOOL);
}

/* --- Decoder --- */

static void skip_whitespace(const char **cursor, const char *end_cursor) {
    while (*cursor < end_cursor && isspace((unsigned char)**cursor)) (*cursor)++;
}

/* Value of a hex digit, or -1. */
static int json_hex_digit(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/* The four hex digits at `p` as a code unit, or -1 when fewer than four
 * digits remain before `limit`. */
static int json_hex4(const char *cursor, const char *limit) {
    if (limit - cursor < 4) return -1;
    int value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = json_hex_digit(cursor[i]);
        if (digit < 0) return -1;
        value = value * 16 + digit;
    }
    return value;
}

/* Write the UTF-8 encoding of `cp` at `out`; returns the byte count. */
static int json_put_utf8(char *output, uint32_t codepoint) {
    if (codepoint < 0x80) { output[0] = (char)codepoint; return 1; }
    if (codepoint < 0x800) {
        output[0] = (char)(0xC0 | (codepoint >> 6));
        output[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        output[0] = (char)(0xE0 | (codepoint >> 12));
        output[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        output[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    output[0] = (char)(0xF0 | (codepoint >> 18));
    output[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    output[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    output[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

/* Advance past a quoted string whose opening quote is at *cursor, stopping on
 * the closing quote (or at `end` when the string is unterminated). */
static const char *json_string_close(const char *cursor, const char *end_cursor) {
    while (cursor < end_cursor && *cursor != '"') {
        if (*cursor == '\\' && cursor + 1 < end_cursor) cursor++;
        cursor++;
    }
    return cursor;
}

/* Parse a quoted string, decoding its escapes. Every escape decodes to no
 * more bytes than it spans in the text, so the raw span bounds the result. */
static GrayString parse_json_string(GrayArena *arena, const char **cursor, const char *end_cursor) {
    if (*cursor >= end_cursor || **cursor != '"') return gray_string_lit("");
    (*cursor)++;
    const char *start = *cursor;
    const char *close = json_string_close(start, end_cursor);
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, (size_t)(close - start) + 1);
    int32_t length = 0;
    for (const char *scan_cursor = start; scan_cursor < close; ) {
        if (*scan_cursor != '\\') { buffer[length++] = *scan_cursor++; continue; }
        scan_cursor++;
        if (scan_cursor >= close) break;
        char escaped = *scan_cursor++;
        switch (escaped) {
        case 'b': buffer[length++] = '\b'; break;
        case 'f': buffer[length++] = '\f'; break;
        case 'n': buffer[length++] = '\n'; break;
        case 'r': buffer[length++] = '\r'; break;
        case 't': buffer[length++] = '\t'; break;
        case 'u': {
            int unit = json_hex4(scan_cursor, close);
            if (unit < 0) { buffer[length++] = 'u'; break; }
            scan_cursor += 4;
            uint32_t codepoint = (uint32_t)unit;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                int low_surrogate = (close - scan_cursor >= 2 && scan_cursor[0] == '\\' && scan_cursor[1] == 'u') ? json_hex4(scan_cursor + 2, close) : -1;
                if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + ((uint32_t)low_surrogate - 0xDC00);
                    scan_cursor += 6;
                } else {
                    codepoint = 0xFFFD;
                }
            } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                codepoint = 0xFFFD;
            }
            length += json_put_utf8(buffer + length, codepoint);
            break;
        }
        default: buffer[length++] = escaped; break; /* \" \\ \/ */
        }
    }
    buffer[length] = '\0';
    *cursor = close < end_cursor ? close + 1 : close;
    return (GrayString){ buffer, length };
}

/* The raw text of a nested array or object, through its matching close. */
static GrayString parse_json_nested_as_string(GrayArena *arena, const char **cursor, const char *end_cursor) {
    const char *start = *cursor;
    int depth = 0;
    while (*cursor < end_cursor) {
        char character = **cursor;
        if (character == '"') {
            const char *close = json_string_close(*cursor + 1, end_cursor);
            *cursor = close < end_cursor ? close + 1 : close;
            continue;
        }
        (*cursor)++;
        if (character == '{' || character == '[') depth++;
        else if ((character == '}' || character == ']') && --depth == 0) break;
    }
    return gray_string_new(arena, start, (int32_t)(*cursor - start));
}

static GrayString parse_json_value_as_string(GrayArena *arena, const char **cursor, const char *end_cursor) {
    skip_whitespace(cursor, end_cursor);
    if (*cursor >= end_cursor) return gray_string_lit("");

    if (**cursor == '"') {
        return parse_json_string(arena, cursor, end_cursor);
    }
    if (**cursor == '{' || **cursor == '[') {
        return parse_json_nested_as_string(arena, cursor, end_cursor);
    }

    /* Number, bool, null — read until delimiter */
    const char *start = *cursor;
    while (*cursor < end_cursor && **cursor != ',' && **cursor != '}' && **cursor != ']' && !isspace((unsigned char)**cursor)) (*cursor)++;
    return gray_string_new(arena, start, (int32_t)(*cursor - start));
}

GrayMap gray_json_decode(GrayArena *arena, GrayString text) {
    GrayMap map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 8, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
    const char *cursor = text.data;
    const char *end_cursor = cursor + text.len;
    skip_whitespace(&cursor, end_cursor);
    if (cursor >= end_cursor || *cursor != '{') return map;
    cursor++; /* skip { */

    while (cursor < end_cursor) {
        skip_whitespace(&cursor, end_cursor);
        /* A key that is not a string cannot be scanned past: stop rather than
         * loop on the same character. */
        if (cursor >= end_cursor || *cursor != '"') break;

        GrayString key = parse_json_string(arena, &cursor, end_cursor);
        skip_whitespace(&cursor, end_cursor);
        if (cursor < end_cursor && *cursor == ':') cursor++;
        GrayString value = parse_json_value_as_string(arena, &cursor, end_cursor);
        GRAY_MAP_SET(arena, &map, &key, &value);

        skip_whitespace(&cursor, end_cursor);
        if (cursor < end_cursor && *cursor == ',') cursor++;
    }
    return map;
}

/* --- Validator ---
 *
 * Proper recursive descent validator. The old implementation just
 * peeked at the first non-whitespace character and dispatched on it,
 * which meant anything starting with '{', '[', '"', a digit, '-',
 * 't', 'f', or 'n' was silently accepted (so `{broken` was "valid").
 * The new one walks the full grammar and requires the consumed
 * region to end at text.len with only trailing whitespace.
 *
 * Mutually recursive with validate_json_array / validate_json_object because a JSON value
 * can be an object or array of values. Parameters are (const char **cursor,
 * const char *end) so each helper advances `*cursor` on success and leaves
 * it unspecified on failure. */

#define GRAY_JSON_MAX_DEPTH 512

static bool validate_json_value(const char **cursor, const char *end_cursor, int depth);

static bool validate_json_string_literal(const char **cursor, const char *end_cursor) {
    if (*cursor >= end_cursor || **cursor != '"') return false;
    (*cursor)++;
    while (*cursor < end_cursor && **cursor != '"') {
        unsigned char character = (unsigned char)**cursor;
        if (character == '\\') {
            (*cursor)++;
            if (*cursor >= end_cursor) return false;
            char escaped = **cursor;
            if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' ||
                escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't') {
                (*cursor)++;
            } else if (escaped == 'u') {
                (*cursor)++;
                for (int i = 0; i < 4; i++) {
                    if (*cursor >= end_cursor || !isxdigit((unsigned char)**cursor)) return false;
                    (*cursor)++;
                }
            } else {
                return false;
            }
        } else if (character < 0x20) {
            /* Control characters must be escaped per RFC 8259. */
            return false;
        } else {
            (*cursor)++;
        }
    }
    if (*cursor >= end_cursor) return false;
    (*cursor)++; /* skip closing " */
    return true;
}

static bool validate_json_number(const char **cursor, const char *end_cursor) {
    if (*cursor >= end_cursor) return false;
    if (**cursor == '-') (*cursor)++;
    if (*cursor >= end_cursor) return false;
    if (**cursor == '0') {
        (*cursor)++;
    } else if (**cursor >= '1' && **cursor <= '9') {
        while (*cursor < end_cursor && isdigit((unsigned char)**cursor)) (*cursor)++;
    } else {
        return false;
    }
    /* Fractional part */
    if (*cursor < end_cursor && **cursor == '.') {
        (*cursor)++;
        if (*cursor >= end_cursor || !isdigit((unsigned char)**cursor)) return false;
        while (*cursor < end_cursor && isdigit((unsigned char)**cursor)) (*cursor)++;
    }
    /* Exponent */
    if (*cursor < end_cursor && (**cursor == 'e' || **cursor == 'E')) {
        (*cursor)++;
        if (*cursor < end_cursor && (**cursor == '+' || **cursor == '-')) (*cursor)++;
        if (*cursor >= end_cursor || !isdigit((unsigned char)**cursor)) return false;
        while (*cursor < end_cursor && isdigit((unsigned char)**cursor)) (*cursor)++;
    }
    return true;
}

static bool validate_json_literal(const char **cursor, const char *end_cursor, const char *literal) {
    size_t literal_length = strlen(literal);
    if ((size_t)(end_cursor - *cursor) < literal_length) return false;
    if (memcmp(*cursor, literal, literal_length) != 0) return false;
    *cursor += literal_length;
    return true;
}

static bool validate_json_array(const char **cursor, const char *end_cursor, int depth) {
    if (*cursor >= end_cursor || **cursor != '[') return false;
    (*cursor)++;
    skip_whitespace(cursor, end_cursor);
    if (*cursor < end_cursor && **cursor == ']') { (*cursor)++; return true; }
    for (;;) {
        skip_whitespace(cursor, end_cursor);
        if (!validate_json_value(cursor, end_cursor, depth + 1)) return false;
        skip_whitespace(cursor, end_cursor);
        if (*cursor >= end_cursor) return false;
        if (**cursor == ',') { (*cursor)++; continue; }
        if (**cursor == ']') { (*cursor)++; return true; }
        return false;
    }
}

static bool validate_json_object(const char **cursor, const char *end_cursor, int depth) {
    if (*cursor >= end_cursor || **cursor != '{') return false;
    (*cursor)++;
    skip_whitespace(cursor, end_cursor);
    if (*cursor < end_cursor && **cursor == '}') { (*cursor)++; return true; }
    for (;;) {
        skip_whitespace(cursor, end_cursor);
        if (!validate_json_string_literal(cursor, end_cursor)) return false;
        skip_whitespace(cursor, end_cursor);
        if (*cursor >= end_cursor || **cursor != ':') return false;
        (*cursor)++;
        skip_whitespace(cursor, end_cursor);
        if (!validate_json_value(cursor, end_cursor, depth + 1)) return false;
        skip_whitespace(cursor, end_cursor);
        if (*cursor >= end_cursor) return false;
        if (**cursor == ',') { (*cursor)++; continue; }
        if (**cursor == '}') { (*cursor)++; return true; }
        return false;
    }
}

static bool validate_json_value(const char **cursor, const char *end_cursor, int depth) {
    if (depth > GRAY_JSON_MAX_DEPTH) return false;
    skip_whitespace(cursor, end_cursor);
    if (*cursor >= end_cursor) return false;
    char character = **cursor;
    if (character == '{') return validate_json_object(cursor, end_cursor, depth);
    if (character == '[') return validate_json_array(cursor, end_cursor, depth);
    if (character == '"') return validate_json_string_literal(cursor, end_cursor);
    if (character == '-' || (character >= '0' && character <= '9')) return validate_json_number(cursor, end_cursor);
    if (character == 't') return validate_json_literal(cursor, end_cursor, "true");
    if (character == 'f') return validate_json_literal(cursor, end_cursor, "false");
    if (character == 'n') return validate_json_literal(cursor, end_cursor, "null");
    return false;
}

bool gray_json_is_valid(GrayString text) {
    if (text.len <= 0 || !text.data) return false;
    const char *cursor = text.data;
    const char *end_cursor = cursor + text.len;
    skip_whitespace(&cursor, end_cursor);
    if (cursor >= end_cursor) return false;
    if (!validate_json_value(&cursor, end_cursor, 0)) return false;
    skip_whitespace(&cursor, end_cursor);
    return cursor == end_cursor;
}

GrayString gray_json_pretty_map(GrayArena *arena, GrayMap *map, int64_t indent_size) {
    /* Pass 1: exact size */
    size_t indent = indent_size > 0 ? (size_t)indent_size : 0;
    size_t need = 3; /* { \n } */
    int counted = 0;
    for (int32_t i = 0; i < map->capacity; i++) {
        if (map->states[i] != 1) continue;
        if (counted > 0) need += 2; /* ,\n */
        GrayString *key = (GrayString *)((char *)map->keys + (size_t)i * (size_t)map->key_size);
        GrayString *value = (GrayString *)((char *)map->values + (size_t)i * (size_t)map->value_size);
        need += indent + json_escaped_len(*key) + 2 /* ": " */ + json_escaped_len(*value);
        counted++;
    }
    if (counted > 0) need += 1; /* trailing \n before } */
    /* Pass 2: write */
    char *buffer = gray_arena_alloc(arena, need + 1);
    int position = 0;
    buffer[position++] = '{';
    buffer[position++] = '\n';
    int entry = 0;
    for (int32_t i = 0; i < map->capacity; i++) {
        if (map->states[i] != 1) continue;
        if (entry > 0) { buffer[position++] = ','; buffer[position++] = '\n'; }
        for (size_t j = 0; j < indent; j++) buffer[position++] = ' ';
        GrayString *key = (GrayString *)((char *)map->keys + (size_t)i * (size_t)map->key_size);
        GrayString *value = (GrayString *)((char *)map->values + (size_t)i * (size_t)map->value_size);
        json_append_escaped(buffer, &position, *key);
        buffer[position++] = ':'; buffer[position++] = ' ';
        json_append_escaped(buffer, &position, *value);
        entry++;
    }
    buffer[position++] = '\n';
    buffer[position++] = '}';
    buffer[position] = '\0';
    GrayString result = { buffer, (int32_t)position };
    return result;
}

/* --- Array splitter ---
 * Splits a JSON array "[{...},{...},...]" into an GrayArray of GrayString,
 * where each element is the raw JSON text of one top-level element.
 * Handles nested braces, brackets, and quoted strings correctly. */

GrayArray gray_json_split_array(GrayArena *arena, GrayString text) {
    GrayArray array = gray_array_new(arena, sizeof(GrayString), 4, GRAY_ELEM_STRING);
    const char *cursor = text.data;
    const char *end_cursor = cursor + text.len;
    skip_whitespace(&cursor, end_cursor);
    if (cursor >= end_cursor || *cursor != '[') return array;
    cursor++; /* skip [ */

    while (cursor < end_cursor) {
        skip_whitespace(&cursor, end_cursor);
        if (cursor >= end_cursor || *cursor == ']') break;

        /* Mark start of this element */
        const char *element_start = cursor;
        int depth_brace = 0, depth_bracket = 0;
        bool in_string = false;

        /* Scan to end of element (respecting nesting and strings) */
        while (cursor < end_cursor) {
            char character = *cursor;
            if (in_string) {
                if (character == '\\') { cursor++; if (cursor < end_cursor) cursor++; continue; }
                if (character == '"') in_string = false;
                cursor++;
                continue;
            }
            if (character == '"') { in_string = true; cursor++; continue; }
            if (character == '{') { depth_brace++; cursor++; continue; }
            if (character == '}') { depth_brace--; cursor++; continue; }
            if (character == '[') { depth_bracket++; cursor++; continue; }
            if (character == ']') {
                if (depth_bracket == 0) break; /* end of outer array */
                depth_bracket--;
                cursor++;
                continue;
            }
            if (character == ',' && depth_brace == 0 && depth_bracket == 0) break;
            cursor++;
        }

        int32_t element_length = (int32_t)(cursor - element_start);
        if (element_length > 0) {
            GrayString element = gray_string_new(arena, element_start, element_length);
            GRAY_ARRAY_PUSH(arena, &array, &element);
        }

        skip_whitespace(&cursor, end_cursor);
        if (cursor < end_cursor && *cursor == ',') cursor++;
    }
    return array;
}

/* _result variant */

GrayResult_map gray_json_decode_result(GrayArena *arena, GrayString text) {
    GrayResult_map result;
    if (text.len <= 0 || !text.data) {
        result.v0 = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "empty JSON input"));
        return result;
    }
    if (!gray_json_is_valid(text)) {
        result.v0 = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid JSON"));
        return result;
    }
    const char *first = text.data;
    const char *text_end = first + text.len;
    skip_whitespace(&first, text_end);
    if (*first != '{') {
        result.v0 = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), 0, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "JSON value is not an object"));
        return result;
    }
    result.v0 = gray_json_decode(arena, text);
    result.v1 = NULL;
    return result;
}

/* --- #json number fields --- */

void gray_json_field_decode(GrayString text, int32_t kind, void *output, const char *file, int line) {
    switch (kind) {
    case GRAY_ELEM_I8:  *(int8_t *)output  = (int8_t)gray_cast_check(gray_strconv_to_int(text, 10), INT8_MIN, INT8_MAX, "i8", file, line); break;
    case GRAY_ELEM_I16: *(int16_t *)output = (int16_t)gray_cast_check(gray_strconv_to_int(text, 10), INT16_MIN, INT16_MAX, "i16", file, line); break;
    case GRAY_ELEM_I32: *(int32_t *)output = (int32_t)gray_cast_check(gray_strconv_to_int(text, 10), INT32_MIN, INT32_MAX, "i32", file, line); break;
    case GRAY_ELEM_I64: *(int64_t *)output = gray_strconv_to_int(text, 10); break;
    case GRAY_ELEM_U8:  *(uint8_t *)output  = (uint8_t)gray_ucast_check_u64(gray_strconv_to_uint(text, 10), UINT8_MAX, "u8", file, line); break;
    case GRAY_ELEM_U16: *(uint16_t *)output = (uint16_t)gray_ucast_check_u64(gray_strconv_to_uint(text, 10), UINT16_MAX, "u16", file, line); break;
    case GRAY_ELEM_U32: *(uint32_t *)output = (uint32_t)gray_ucast_check_u64(gray_strconv_to_uint(text, 10), UINT32_MAX, "u32", file, line); break;
    case GRAY_ELEM_U64: *(uint64_t *)output = gray_strconv_to_uint(text, 10); break;
    case GRAY_ELEM_F32: *(float *)output  = (float)gray_strconv_to_float(text); break;
    case GRAY_ELEM_F64: *(double *)output = gray_strconv_to_float(text); break;
    case GRAY_ELEM_I128: case GRAY_ELEM_U128: case GRAY_ELEM_I256: case GRAY_ELEM_U256: {
        /* The wide parsers read a NUL-terminated decimal. */
        char digits[96];
        int32_t length = text.len < (int32_t)sizeof(digits) - 1 ? text.len : (int32_t)sizeof(digits) - 1;
        memcpy(digits, text.data, (size_t)length);
        digits[length] = '\0';
        if (kind == GRAY_ELEM_I128) *(gray_i128 *)output = gray_i128_from_decimal(digits);
        else if (kind == GRAY_ELEM_U128) *(gray_u128 *)output = gray_u128_from_decimal(digits);
        else if (kind == GRAY_ELEM_I256) *(gray_i256 *)output = gray_i256_from_decimal(digits);
        else *(gray_u256 *)output = gray_u256_from_decimal(digits);
        break;
    }
    default: break;
    }
}

GrayString gray_json_number_text(GrayArena *arena, int32_t kind, const void *value) {
    char buffer[48];
    int length = 0;
    switch (kind) {
    case GRAY_ELEM_I128: return gray_i128_to_string(arena, *(const gray_i128 *)value);
    case GRAY_ELEM_U128: return gray_u128_to_string(arena, *(const gray_u128 *)value);
    case GRAY_ELEM_I256: return gray_i256_to_string(arena, *(const gray_i256 *)value);
    case GRAY_ELEM_U256: return gray_u256_to_string(arena, *(const gray_u256 *)value);
    case GRAY_ELEM_F32:
    case GRAY_ELEM_F64:
        length = snprintf(buffer, sizeof(buffer), "%g", gray_elem_to_double(kind, value));
        break;
    case GRAY_ELEM_U8: case GRAY_ELEM_U16: case GRAY_ELEM_U32: case GRAY_ELEM_U64:
        length = snprintf(buffer, sizeof(buffer), "%" PRIu64, gray_elem_to_u64(kind, value));
        break;
    default:
        length = snprintf(buffer, sizeof(buffer), "%" PRId64, gray_elem_to_i64(kind, value));
        break;
    }
    return gray_string_new(arena, buffer, length);
}
