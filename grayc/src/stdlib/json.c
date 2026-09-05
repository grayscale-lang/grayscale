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
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>

/* --- Encoder --- */

/* Exact byte count that json_append_escaped would write (includes quotes). */
size_t json_escaped_len(GrayString s) {
    size_t n = 2; /* opening + closing quote */
    for (int32_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '"' || c == '\\') n += 2;
        else if (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') n += 2;
        else if (c < 0x20) n += 6; /* \uXXXX */
        else n += 1;
    }
    return n;
}

void json_append_escaped(char *buf, int *pos, GrayString s) {
    static const char hex[] = "0123456789abcdef";
    buf[(*pos)++] = '"';
    for (int32_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '"') { buf[(*pos)++] = '\\'; buf[(*pos)++] = '"'; }
        else if (c == '\\') { buf[(*pos)++] = '\\'; buf[(*pos)++] = '\\'; }
        else if (c == '\b') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 'b'; }
        else if (c == '\f') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 'f'; }
        else if (c == '\n') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 'n'; }
        else if (c == '\r') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 'r'; }
        else if (c == '\t') { buf[(*pos)++] = '\\'; buf[(*pos)++] = 't'; }
        else if (c < 0x20) {
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 'u';
            buf[(*pos)++] = '0'; buf[(*pos)++] = '0';
            buf[(*pos)++] = hex[c >> 4]; buf[(*pos)++] = hex[c & 0xf];
        }
        else buf[(*pos)++] = (char)c;
    }
    buf[(*pos)++] = '"';
}

/* Value kind for the shared map encoder. map[string:string] values are always
 * quoted (STRING) — never infer JSON types from string content. */
typedef enum {
    JSON_MAP_VAL_STRING,
    JSON_MAP_VAL_INT,
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
static GrayString json_encode_map_typed(GrayArena *arena, GrayMap *m, JsonMapValKind kind) {
    /* Pass 1: size */
    size_t need = 2; /* { } */
    for (int32_t order_index = 0; order_index < m->order_len; order_index++) {
        int32_t i = m->order[order_index];
        if (i < 0) continue;
        need += 1; /* comma */
        GrayString *key = (GrayString *)((char *)m->keys + (size_t)i * (size_t)m->key_size);
        void *val = (char *)m->values + (size_t)i * (size_t)m->value_size;
        need += json_escaped_len(*key) + 1 /* colon */;
        switch (kind) {
            case JSON_MAP_VAL_STRING: need += json_escaped_len(*(GrayString *)val); break;
            case JSON_MAP_VAL_INT:    need += 21; break;
            case JSON_MAP_VAL_FLOAT:  need += 24; break;
            case JSON_MAP_VAL_BOOL:   need += *(bool *)val ? 4 : 5; break;
        }
    }
    /* Pass 2: write */
    char *buf = gray_arena_alloc_uninitialized(arena, need + 1);
    int pos = 0;
    buf[pos++] = '{';
    bool json_first = true;
    for (int32_t order_index = 0; order_index < m->order_len; order_index++) {
        int32_t i = m->order[order_index];
        if (i < 0) continue;
        if (!json_first) { buf[pos++] = ','; }
        json_first = false;
        GrayString *key = (GrayString *)((char *)m->keys + (size_t)i * (size_t)m->key_size);
        void *val = (char *)m->values + (size_t)i * (size_t)m->value_size;
        json_append_escaped(buf, &pos, *key);
        buf[pos++] = ':';
        bool truncated = false;
        switch (kind) {
            case JSON_MAP_VAL_STRING:
                json_append_escaped(buf, &pos, *(GrayString *)val);
                break;
            case JSON_MAP_VAL_INT: {
                int w = snprintf(buf + pos, need + 1 - (size_t)pos, "%" PRId64, *(int64_t *)val);
                if (w > 0 && (size_t)w < need + 1 - (size_t)pos) pos += w;
                else truncated = true;
                break;
            }
            case JSON_MAP_VAL_FLOAT: {
                int w = snprintf(buf + pos, need + 1 - (size_t)pos, "%g", *(double *)val);
                if (w > 0 && (size_t)w < need + 1 - (size_t)pos) pos += w;
                else truncated = true;
                break;
            }
            case JSON_MAP_VAL_BOOL:
                if (*(bool *)val) { memcpy(buf + pos, "true", 4); pos += 4; }
                else { memcpy(buf + pos, "false", 5); pos += 5; }
                break;
        }
        if (truncated) { pos = (int)need - 1; break; }
    }
    buf[pos++] = '}';
    buf[pos] = '\0';
    return (GrayString){ buf, (int32_t)pos };
}

GrayString gray_json_encode_map(GrayArena *arena, GrayMap *m) {
    return json_encode_map_typed(arena, m, JSON_MAP_VAL_STRING);
}

/* --- Array Encoders --- */

GrayString gray_json_encode_array_int(GrayArena *arena, GrayArray *arr) {
    /* 21 chars max per int64 + comma, plus brackets + nul */
    size_t need = 2 + (arr->len > 0 ? (size_t)arr->len * 22 - 1 : 0);
    char *buf = gray_arena_alloc_uninitialized(arena, need + 1);
    int pos = 0;
    buf[pos++] = '[';
    for (int32_t i = 0; i < arr->len; i++) {
        if (i > 0) { buf[pos++] = ','; }
        int64_t val = *(int64_t *)((char *)arr->data + (size_t)i * (size_t)arr->elem_size);
        int w = snprintf(buf + pos, need + 1 - (size_t)pos, "%" PRId64, val);
        if (w > 0 && (size_t)w < need + 1 - (size_t)pos) pos += w;
        /* Defensive: clamp so the closing bracket and NUL stay in bounds. */
        else { pos = (int)need - 1; break; }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return (GrayString){ buf, (int32_t)pos };
}

GrayString gray_json_encode_array_float(GrayArena *arena, GrayArray *arr) {
    /* 24 chars max per %g double + comma, plus brackets + nul */
    size_t need = 2 + (arr->len > 0 ? (size_t)arr->len * 25 - 1 : 0);
    char *buf = gray_arena_alloc_uninitialized(arena, need + 1);
    int pos = 0;
    buf[pos++] = '[';
    for (int32_t i = 0; i < arr->len; i++) {
        if (i > 0) { buf[pos++] = ','; }
        double val = *(double *)((char *)arr->data + (size_t)i * (size_t)arr->elem_size);
        int w = snprintf(buf + pos, need + 1 - (size_t)pos, "%g", val);
        if (w > 0 && (size_t)w < need + 1 - (size_t)pos) pos += w;
        /* Defensive: clamp so the closing bracket and NUL stay in bounds. */
        else { pos = (int)need - 1; break; }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return (GrayString){ buf, (int32_t)pos };
}

GrayString gray_json_encode_array_string(GrayArena *arena, GrayArray *arr) {
    /* Pass 1: exact size */
    size_t need = 2; /* [ ] */
    for (int32_t i = 0; i < arr->len; i++) {
        if (i > 0) need += 1; /* comma */
        GrayString *val = (GrayString *)((char *)arr->data + (size_t)i * (size_t)arr->elem_size);
        need += json_escaped_len(*val);
    }
    /* Pass 2: write */
    char *buf = gray_arena_alloc_uninitialized(arena, need + 1);
    int pos = 0;
    buf[pos++] = '[';
    for (int32_t i = 0; i < arr->len; i++) {
        if (i > 0) { buf[pos++] = ','; }
        GrayString *val = (GrayString *)((char *)arr->data + (size_t)i * (size_t)arr->elem_size);
        json_append_escaped(buf, &pos, *val);
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return (GrayString){ buf, (int32_t)pos };
}

GrayString gray_json_encode_array_bool(GrayArena *arena, GrayArray *arr) {
    /* Pass 1: exact size */
    size_t need = 2; /* [ ] */
    for (int32_t i = 0; i < arr->len; i++) {
        if (i > 0) need += 1;
        bool val = *(bool *)((char *)arr->data + (size_t)i * (size_t)arr->elem_size);
        need += val ? 4 : 5;
    }
    /* Pass 2: write */
    char *buf = gray_arena_alloc_uninitialized(arena, need + 1);
    int pos = 0;
    buf[pos++] = '[';
    for (int32_t i = 0; i < arr->len; i++) {
        if (i > 0) { buf[pos++] = ','; }
        bool val = *(bool *)((char *)arr->data + (size_t)i * (size_t)arr->elem_size);
        if (val) { memcpy(buf + pos, "true", 4); pos += 4; }
        else { memcpy(buf + pos, "false", 5); pos += 5; }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return (GrayString){ buf, (int32_t)pos };
}

/* --- Typed Map Encoders --- */

GrayString gray_json_encode_map_int(GrayArena *arena, GrayMap *m) {
    return json_encode_map_typed(arena, m, JSON_MAP_VAL_INT);
}

GrayString gray_json_encode_map_float(GrayArena *arena, GrayMap *m) {
    return json_encode_map_typed(arena, m, JSON_MAP_VAL_FLOAT);
}

GrayString gray_json_encode_map_bool(GrayArena *arena, GrayMap *m) {
    return json_encode_map_typed(arena, m, JSON_MAP_VAL_BOOL);
}

/* --- Decoder --- */

static void skip_ws(const char **cursor, const char *end) {
    while (*cursor < end && isspace((unsigned char)**cursor)) (*cursor)++;
}

static GrayString parse_json_string(GrayArena *arena, const char **cursor, const char *end) {
    if (**cursor != '"') return gray_string_lit("");
    (*cursor)++;
    const char *start = *cursor;
    while (*cursor < end && **cursor != '"') {
        if (**cursor == '\\') (*cursor)++;
        (*cursor)++;
    }
    GrayString r = gray_string_new(arena, start, (int32_t)(*cursor - start));
    if (*cursor < end) (*cursor)++; /* skip closing quote */
    return r;
}

static GrayString parse_json_value_as_string(GrayArena *arena, const char **cursor, const char *end) {
    skip_ws(cursor, end);
    if (*cursor >= end) return gray_string_lit("");

    if (**cursor == '"') {
        return parse_json_string(arena, cursor, end);
    }

    /* Number, bool, null — read until delimiter */
    const char *start = *cursor;
    while (*cursor < end && **cursor != ',' && **cursor != '}' && **cursor != ']' && !isspace((unsigned char)**cursor)) (*cursor)++;
    return gray_string_new(arena, start, (int32_t)(*cursor - start));
}

GrayMap gray_json_decode(GrayArena *arena, GrayString text) {
    GrayMap m = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), 8);
    const char *cursor = text.data;
    const char *end = cursor + text.len;
    skip_ws(&cursor, end);
    if (cursor >= end || *cursor != '{') return m;
    cursor++; /* skip { */

    while (cursor < end) {
        skip_ws(&cursor, end);
        if (cursor >= end || *cursor == '}') break;

        GrayString key = parse_json_string(arena, &cursor, end);
        skip_ws(&cursor, end);
        if (cursor < end && *cursor == ':') cursor++;
        GrayString val = parse_json_value_as_string(arena, &cursor, end);
        GRAY_MAP_SET(arena, &m, &key, &val);

        skip_ws(&cursor, end);
        if (cursor < end && *cursor == ',') cursor++;
    }
    return m;
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

static bool validate_json_value(const char **cursor, const char *end, int depth);

static bool validate_json_string_lit(const char **cursor, const char *end) {
    if (*cursor >= end || **cursor != '"') return false;
    (*cursor)++;
    while (*cursor < end && **cursor != '"') {
        unsigned char c = (unsigned char)**cursor;
        if (c == '\\') {
            (*cursor)++;
            if (*cursor >= end) return false;
            char esc = **cursor;
            if (esc == '"' || esc == '\\' || esc == '/' || esc == 'b' ||
                esc == 'f' || esc == 'n' || esc == 'r' || esc == 't') {
                (*cursor)++;
            } else if (esc == 'u') {
                (*cursor)++;
                for (int i = 0; i < 4; i++) {
                    if (*cursor >= end || !isxdigit((unsigned char)**cursor)) return false;
                    (*cursor)++;
                }
            } else {
                return false;
            }
        } else if (c < 0x20) {
            /* Control characters must be escaped per RFC 8259. */
            return false;
        } else {
            (*cursor)++;
        }
    }
    if (*cursor >= end) return false;
    (*cursor)++; /* skip closing " */
    return true;
}

static bool validate_json_number(const char **cursor, const char *end) {
    if (*cursor >= end) return false;
    if (**cursor == '-') (*cursor)++;
    if (*cursor >= end) return false;
    if (**cursor == '0') {
        (*cursor)++;
    } else if (**cursor >= '1' && **cursor <= '9') {
        while (*cursor < end && isdigit((unsigned char)**cursor)) (*cursor)++;
    } else {
        return false;
    }
    /* Fractional part */
    if (*cursor < end && **cursor == '.') {
        (*cursor)++;
        if (*cursor >= end || !isdigit((unsigned char)**cursor)) return false;
        while (*cursor < end && isdigit((unsigned char)**cursor)) (*cursor)++;
    }
    /* Exponent */
    if (*cursor < end && (**cursor == 'e' || **cursor == 'E')) {
        (*cursor)++;
        if (*cursor < end && (**cursor == '+' || **cursor == '-')) (*cursor)++;
        if (*cursor >= end || !isdigit((unsigned char)**cursor)) return false;
        while (*cursor < end && isdigit((unsigned char)**cursor)) (*cursor)++;
    }
    return true;
}

static bool validate_json_literal(const char **cursor, const char *end, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(end - *cursor) < n) return false;
    if (memcmp(*cursor, lit, n) != 0) return false;
    *cursor += n;
    return true;
}

static bool validate_json_array(const char **cursor, const char *end, int depth) {
    if (*cursor >= end || **cursor != '[') return false;
    (*cursor)++;
    skip_ws(cursor, end);
    if (*cursor < end && **cursor == ']') { (*cursor)++; return true; }
    for (;;) {
        skip_ws(cursor, end);
        if (!validate_json_value(cursor, end, depth + 1)) return false;
        skip_ws(cursor, end);
        if (*cursor >= end) return false;
        if (**cursor == ',') { (*cursor)++; continue; }
        if (**cursor == ']') { (*cursor)++; return true; }
        return false;
    }
}

static bool validate_json_object(const char **cursor, const char *end, int depth) {
    if (*cursor >= end || **cursor != '{') return false;
    (*cursor)++;
    skip_ws(cursor, end);
    if (*cursor < end && **cursor == '}') { (*cursor)++; return true; }
    for (;;) {
        skip_ws(cursor, end);
        if (!validate_json_string_lit(cursor, end)) return false;
        skip_ws(cursor, end);
        if (*cursor >= end || **cursor != ':') return false;
        (*cursor)++;
        skip_ws(cursor, end);
        if (!validate_json_value(cursor, end, depth + 1)) return false;
        skip_ws(cursor, end);
        if (*cursor >= end) return false;
        if (**cursor == ',') { (*cursor)++; continue; }
        if (**cursor == '}') { (*cursor)++; return true; }
        return false;
    }
}

static bool validate_json_value(const char **cursor, const char *end, int depth) {
    if (depth > GRAY_JSON_MAX_DEPTH) return false;
    skip_ws(cursor, end);
    if (*cursor >= end) return false;
    char c = **cursor;
    if (c == '{') return validate_json_object(cursor, end, depth);
    if (c == '[') return validate_json_array(cursor, end, depth);
    if (c == '"') return validate_json_string_lit(cursor, end);
    if (c == '-' || (c >= '0' && c <= '9')) return validate_json_number(cursor, end);
    if (c == 't') return validate_json_literal(cursor, end, "true");
    if (c == 'f') return validate_json_literal(cursor, end, "false");
    if (c == 'n') return validate_json_literal(cursor, end, "null");
    return false;
}

bool gray_json_is_valid(GrayString text) {
    if (text.len <= 0 || !text.data) return false;
    const char *cursor = text.data;
    const char *end = cursor + text.len;
    skip_ws(&cursor, end);
    if (cursor >= end) return false;
    if (!validate_json_value(&cursor, end, 0)) return false;
    skip_ws(&cursor, end);
    return cursor == end;
}

GrayString gray_json_pretty_map(GrayArena *arena, GrayMap *m, int64_t indent_size) {
    /* Pass 1: exact size */
    size_t indent = indent_size > 0 ? (size_t)indent_size : 0;
    size_t need = 3; /* { \n } */
    int counted = 0;
    for (int32_t i = 0; i < m->capacity; i++) {
        if (m->states[i] != 1) continue;
        if (counted > 0) need += 2; /* ,\n */
        GrayString *key = (GrayString *)((char *)m->keys + (size_t)i * (size_t)m->key_size);
        GrayString *val = (GrayString *)((char *)m->values + (size_t)i * (size_t)m->value_size);
        need += indent + json_escaped_len(*key) + 2 /* ": " */ + json_escaped_len(*val);
        counted++;
    }
    if (counted > 0) need += 1; /* trailing \n before } */
    /* Pass 2: write */
    char *buf = gray_arena_alloc(arena, need + 1);
    int pos = 0;
    buf[pos++] = '{';
    buf[pos++] = '\n';
    int entry = 0;
    for (int32_t i = 0; i < m->capacity; i++) {
        if (m->states[i] != 1) continue;
        if (entry > 0) { buf[pos++] = ','; buf[pos++] = '\n'; }
        for (size_t j = 0; j < indent; j++) buf[pos++] = ' ';
        GrayString *key = (GrayString *)((char *)m->keys + (size_t)i * (size_t)m->key_size);
        GrayString *val = (GrayString *)((char *)m->values + (size_t)i * (size_t)m->value_size);
        json_append_escaped(buf, &pos, *key);
        buf[pos++] = ':'; buf[pos++] = ' ';
        json_append_escaped(buf, &pos, *val);
        entry++;
    }
    buf[pos++] = '\n';
    buf[pos++] = '}';
    buf[pos] = '\0';
    GrayString r = { buf, (int32_t)pos };
    return r;
}

/* --- Array splitter ---
 * Splits a JSON array "[{...},{...},...]" into an GrayArray of GrayString,
 * where each element is the raw JSON text of one top-level element.
 * Handles nested braces, brackets, and quoted strings correctly. */

GrayArray gray_json_split_array(GrayArena *arena, GrayString text) {
    GrayArray arr = gray_array_new(arena, sizeof(GrayString), 4);
    const char *cursor = text.data;
    const char *end = cursor + text.len;
    skip_ws(&cursor, end);
    if (cursor >= end || *cursor != '[') return arr;
    cursor++; /* skip [ */

    while (cursor < end) {
        skip_ws(&cursor, end);
        if (cursor >= end || *cursor == ']') break;

        /* Mark start of this element */
        const char *elem_start = cursor;
        int depth_brace = 0, depth_bracket = 0;
        bool in_string = false;

        /* Scan to end of element (respecting nesting and strings) */
        while (cursor < end) {
            char c = *cursor;
            if (in_string) {
                if (c == '\\') { cursor++; if (cursor < end) cursor++; continue; }
                if (c == '"') in_string = false;
                cursor++;
                continue;
            }
            if (c == '"') { in_string = true; cursor++; continue; }
            if (c == '{') { depth_brace++; cursor++; continue; }
            if (c == '}') { depth_brace--; cursor++; continue; }
            if (c == '[') { depth_bracket++; cursor++; continue; }
            if (c == ']') {
                if (depth_bracket == 0) break; /* end of outer array */
                depth_bracket--;
                cursor++;
                continue;
            }
            if (c == ',' && depth_brace == 0 && depth_bracket == 0) break;
            cursor++;
        }

        int32_t elem_len = (int32_t)(cursor - elem_start);
        if (elem_len > 0) {
            GrayString elem = gray_string_new(arena, elem_start, elem_len);
            GRAY_ARRAY_PUSH(arena, &arr, &elem);
        }

        skip_ws(&cursor, end);
        if (cursor < end && *cursor == ',') cursor++;
    }
    return arr;
}

/* _result variant */

GrayResult_map gray_json_decode_result(GrayArena *arena, GrayString text) {
    GrayResult_map r;
    if (text.len <= 0 || !text.data) {
        r.v0 = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "empty JSON input"));
        return r;
    }
    if (!gray_json_is_valid(text)) {
        r.v0 = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "invalid JSON"));
        return r;
    }
    r.v0 = gray_json_decode(arena, text);
    r.v1 = NULL;
    return r;
}
