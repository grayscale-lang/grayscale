/*
 * csv.c — Implementation of the csv stdlib module.
 * RFC 4180 compliant CSV parser and formatter with support for
 * quoted fields, header extraction, and row-by-row iteration.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "csv.h"
#include "strings.h" /* GrayStringsBuilder */
#include "json.h"    /* gray_json_encode_map */
#include "../runtime/map.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* RFC 4180 parser with a caller-chosen field delimiter. gray_csv_parse and
 * gray_csv_parse_delimited are thin wrappers over this. */
static GrayArray csv_parse_delim(GrayArena *arena, GrayString csv_string, char delim) {
    GrayArray rows = gray_array_new(arena, sizeof(GrayArray), 8);
    const char *cursor = csv_string.data;
    const char *end = cursor + csv_string.len;

    while (cursor < end) {
        GrayArray row = gray_array_new(arena, sizeof(GrayString), 8);
        while (cursor < end && *cursor != '\n' && *cursor != '\r') {
            const char *field_start;
            int32_t field_length;

            if (*cursor == '"') {
                /* Quoted field */
                cursor++;
                field_start = cursor;
                while (cursor < end && !(*cursor == '"' && (cursor + 1 >= end || *(cursor + 1) != '"'))) {
                    if (*cursor == '"' && *(cursor + 1) == '"') cursor += 2;
                    else cursor++;
                }
                field_length = (int32_t)(cursor - field_start);
                if (cursor < end) cursor++; /* skip closing quote */

                /* RFC 4180 §2.7: unescape doubled quotes ("") to single (") */
                if (memchr(field_start, '"', (size_t)field_length)) {
                    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)field_length);
                    int32_t out = 0;
                    for (int32_t k = 0; k < field_length; k++) {
                        buf[out++] = field_start[k];
                        if (field_start[k] == '"' && k + 1 < field_length && field_start[k + 1] == '"')
                            k++; /* skip second quote of pair */
                    }
                    field_start = buf;
                    field_length = out;
                }
            } else {
                /* Unquoted field */
                field_start = cursor;
                while (cursor < end && *cursor != delim && *cursor != '\n' && *cursor != '\r') cursor++;
                field_length = (int32_t)(cursor - field_start);
            }

            GrayString field = gray_string_new(arena, field_start, field_length);
            GRAY_ARRAY_PUSH(arena, &row, &field);

            if (cursor < end && *cursor == delim) cursor++;
        }
        GRAY_ARRAY_PUSH(arena, &rows, &row);

        /* Skip line ending */
        if (cursor < end && *cursor == '\r') cursor++;
        if (cursor < end && *cursor == '\n') cursor++;
    }
    return rows;
}

GrayArray gray_csv_parse(GrayArena *arena, GrayString csv_string) {
    return csv_parse_delim(arena, csv_string, ',');
}

GrayArray gray_csv_parse_delimited(GrayArena *arena, GrayString csv_string, int32_t delimiter) {
    return csv_parse_delim(arena, csv_string, (char)delimiter);
}

/* --- Helpers shared by the [[string]] / [map[string:string]] views --- */

/* Row `index` of parsed data (index must be in range). */
static GrayArray *csv_row(GrayArray *data, int32_t index) {
    return (GrayArray *)((char *)data->data + (size_t)index * sizeof(GrayArray));
}

/* Cell `index` of a row, or "" when the row is short. */
static GrayString csv_cell(GrayArray *row, int32_t index) {
    if (index < 0 || index >= row->len) return gray_string_lit("");
    return *(GrayString *)((char *)row->data + (size_t)index * sizeof(GrayString));
}

static bool csv_str_eq(GrayString left, GrayString right) {
    return left.len == right.len && memcmp(left.data, right.data, (size_t)left.len) == 0;
}

static int csv_str_cmp(GrayString left, GrayString right) {
    int32_t common_len = left.len < right.len ? left.len : right.len;
    int cmp = memcmp(left.data, right.data, (size_t)common_len);
    if (cmp != 0) return cmp;
    return (left.len > right.len) - (left.len < right.len);
}

/* Index of column `name` in the header (row 0), or -1. */
static int32_t csv_col_index(GrayArray *data, GrayString name) {
    if (data->len == 0) return -1;
    GrayArray *header = csv_row(data, 0);
    for (int32_t j = 0; j < header->len; j++) {
        if (csv_str_eq(csv_cell(header, j), name)) return j;
    }
    return -1;
}

static _Noreturn void csv_no_such_column(GrayString name) {
    gray_panic_code("P0125", "csv: column '%.*s' is not in the header",
        (int)name.len, name.data);
}

GrayArray gray_csv_to_maps(GrayArena *arena, GrayArray *data) {
    GrayArray out = gray_array_new(arena, sizeof(GrayMap), data->len > 1 ? data->len - 1 : 0);
    if (data->len <= 1) return out;
    GrayArray *header = csv_row(data, 0);
    for (int32_t i = 1; i < data->len; i++) {
        GrayArray *row = csv_row(data, i);
        GrayMap map = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString),
                                 header->len > 0 ? header->len : 8);
        int32_t pair_count = row->len < header->len ? row->len : header->len;
        for (int32_t j = 0; j < pair_count; j++) {
            GrayString key = csv_cell(header, j);
            GrayString val = csv_cell(row, j);
            GRAY_MAP_SET(arena, &map, &key, &val);
        }
        GRAY_ARRAY_PUSH(arena, &out, &map);
    }
    return out;
}

GrayArray gray_csv_from_maps(GrayArena *arena, GrayArray *rows) {
    GrayArray out = gray_array_new(arena, sizeof(GrayArray), rows->len + 1);
    if (rows->len == 0) return out;

    /* Header = union of keys across all rows, in first-seen order. */
    GrayArray header = gray_array_new(arena, sizeof(GrayString), 8);
    for (int32_t i = 0; i < rows->len; i++) {
        GrayMap *map = (GrayMap *)((char *)rows->data + (size_t)i * sizeof(GrayMap));
        for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
            int32_t slot = map->order[order_index];
            if (slot < 0 || map->states[slot] != 1) continue;
            GrayString *key = (GrayString *)((char *)map->keys + (size_t)slot * (size_t)map->key_size);
            bool seen = false;
            for (int32_t h = 0; h < header.len; h++) {
                if (csv_str_eq(csv_cell(&header, h), *key)) { seen = true; break; }
            }
            if (!seen) GRAY_ARRAY_PUSH(arena, &header, key);
        }
    }
    GRAY_ARRAY_PUSH(arena, &out, &header);

    for (int32_t i = 0; i < rows->len; i++) {
        GrayMap *map = (GrayMap *)((char *)rows->data + (size_t)i * sizeof(GrayMap));
        GrayArray cells = gray_array_new(arena, sizeof(GrayString), header.len);
        for (int32_t h = 0; h < header.len; h++) {
            GrayString key = csv_cell(&header, h);
            GrayString *found = (GrayString *)gray_map_get_str(map, key);
            GrayString cell = found ? *found : gray_string_lit("");
            GRAY_ARRAY_PUSH(arena, &cells, &cell);
        }
        GRAY_ARRAY_PUSH(arena, &out, &cells);
    }
    return out;
}

GrayArray gray_csv_column(GrayArena *arena, GrayArray *data, GrayString name) {
    int32_t col = csv_col_index(data, name);
    if (col < 0) csv_no_such_column(name);
    GrayArray out = gray_array_new(arena, sizeof(GrayString), data->len > 1 ? data->len - 1 : 0);
    for (int32_t i = 1; i < data->len; i++) {
        GrayString cell = csv_cell(csv_row(data, i), col);
        GRAY_ARRAY_PUSH(arena, &out, &cell);
    }
    return out;
}

GrayArray gray_csv_select(GrayArena *arena, GrayArray *data, GrayArray *names) {
    int32_t *idx = gray_arena_alloc_uninitialized(arena,
        (size_t)(names->len > 0 ? names->len : 1) * sizeof(int32_t));
    for (int32_t k = 0; k < names->len; k++) {
        GrayString column_name = csv_cell(names, k);
        int32_t col_index = csv_col_index(data, column_name);
        if (col_index < 0) csv_no_such_column(column_name);
        idx[k] = col_index;
    }
    GrayArray out = gray_array_new(arena, sizeof(GrayArray), data->len);
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row = csv_row(data, i);
        GrayArray proj = gray_array_new(arena, sizeof(GrayString), names->len);
        for (int32_t k = 0; k < names->len; k++) {
            GrayString cell = csv_cell(row, idx[k]);
            GRAY_ARRAY_PUSH(arena, &proj, &cell);
        }
        GRAY_ARRAY_PUSH(arena, &out, &proj);
    }
    return out;
}

typedef struct { GrayString key; int32_t original_index; } CsvSortEnt;

static int csv_sort_cmp(const void *left, const void *right) {
    const CsvSortEnt *left_ent = (const CsvSortEnt *)left;
    const CsvSortEnt *right_ent = (const CsvSortEnt *)right;
    int cmp = csv_str_cmp(left_ent->key, right_ent->key);
    if (cmp != 0) return cmp;
    return (left_ent->original_index > right_ent->original_index) - (left_ent->original_index < right_ent->original_index); /* stable */
}

GrayArray gray_csv_sort_by_column(GrayArena *arena, GrayArray *data, GrayString name) {
    int32_t col = csv_col_index(data, name);
    if (col < 0) csv_no_such_column(name);
    GrayArray out = gray_array_new(arena, sizeof(GrayArray), data->len);
    if (data->len == 0) return out;
    GRAY_ARRAY_PUSH(arena, &out, csv_row(data, 0)); /* header stays first */

    int32_t body_row_count = data->len - 1;
    if (body_row_count <= 0) return out;
    CsvSortEnt *entries = gray_arena_alloc_uninitialized(arena, (size_t)body_row_count * sizeof(CsvSortEnt));
    for (int32_t i = 0; i < body_row_count; i++) {
        entries[i].key = csv_cell(csv_row(data, i + 1), col);
        entries[i].original_index = i;
    }
    qsort(entries, (size_t)body_row_count, sizeof(CsvSortEnt), csv_sort_cmp);
    for (int32_t i = 0; i < body_row_count; i++) {
        GRAY_ARRAY_PUSH(arena, &out, csv_row(data, entries[i].original_index + 1));
    }
    return out;
}

int32_t gray_csv_detect_delimiter(GrayString sample) {
    static const char candidates[] = {',', ';', '\t', '|'};
    int32_t line_end = sample.len;
    for (int32_t i = 0; i < sample.len; i++) {
        if (sample.data[i] == '\n' || sample.data[i] == '\r') { line_end = i; break; }
    }
    int best_count = 0;
    char best = ',';
    for (int candidate_index = 0; candidate_index < 4; candidate_index++) {
        int count = 0;
        for (int32_t i = 0; i < line_end; i++) {
            if (sample.data[i] == candidates[candidate_index]) count++;
        }
        if (count > best_count) { best_count = count; best = candidates[candidate_index]; }
    }
    return (int32_t)(unsigned char)best;
}

GrayString gray_csv_to_json(GrayArena *arena, GrayArray *data) {
    GrayArray maps = gray_csv_to_maps(arena, data);
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    gray_strings_builder_append_char(builder, '[');
    for (int32_t i = 0; i < maps.len; i++) {
        if (i > 0) gray_strings_builder_append_char(builder, ',');
        GrayMap *map = (GrayMap *)((char *)maps.data + (size_t)i * sizeof(GrayMap));
        gray_strings_builder_append(builder, gray_json_encode_map(arena, map));
    }
    gray_strings_builder_append_char(builder, ']');
    return gray_strings_build(arena, builder);
}

GrayString gray_csv_to_markdown(GrayArena *arena, GrayArray *data) {
    if (data->len == 0) return gray_string_lit("");
    GrayStringsBuilder *builder = gray_strings_builder(arena);
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row = csv_row(data, i);
        gray_strings_builder_append_char(builder, '|');
        for (int32_t j = 0; j < row->len; j++) {
            GrayString cell = csv_cell(row, j);
            for (int32_t k = 0; k < cell.len; k++) {
                if (cell.data[k] == '|') gray_strings_builder_append_char(builder, '\\');
                gray_strings_builder_append_char(builder, (int32_t)(unsigned char)cell.data[k]);
            }
            gray_strings_builder_append_char(builder, '|');
        }
        gray_strings_builder_append_char(builder, '\n');
        if (i == 0) {
            gray_strings_builder_append_char(builder, '|');
            for (int32_t j = 0; j < row->len; j++) {
                gray_strings_builder_append(builder, gray_string_lit("---|"));
            }
            gray_strings_builder_append_char(builder, '\n');
        }
    }
    return gray_strings_build(arena, builder);
}

/* Row-filter callback plumbing: filter_rows is emitted inline by codegen (it
 * takes a Grayscale func value), so there is no C entry point for it here. */

/* RFC 4180 §2.6-2.7: a field must be quoted when it contains the comma
 * delimiter, a double-quote, CR, or LF. Quoting wraps it in double-quotes and
 * doubles every embedded double-quote. */
static bool csv_field_needs_quote(GrayString field) {
    for (int32_t i = 0; i < field.len; i++) {
        char c = field.data[i];
        if (c == ',' || c == '"' || c == '\r' || c == '\n') return true;
    }
    return false;
}

/* Byte length of `field` once encoded: unchanged if it needs no quoting, else
 * the field plus the two surrounding quotes and one extra byte per embedded
 * quote. */
static int32_t csv_field_encoded_len(GrayString field) {
    if (!csv_field_needs_quote(field)) return field.len;
    int32_t n = field.len + 2;
    for (int32_t i = 0; i < field.len; i++)
        if (field.data[i] == '"') n++;
    return n;
}

/* Write the encoded form of `field` at `dst`; returns the bytes written. */
static int32_t csv_field_encode(char *dst, GrayString field) {
    if (!csv_field_needs_quote(field)) {
        memcpy(dst, field.data, (size_t)field.len);
        return field.len;
    }
    int32_t pos = 0;
    dst[pos++] = '"';
    for (int32_t i = 0; i < field.len; i++) {
        if (field.data[i] == '"') dst[pos++] = '"';
        dst[pos++] = field.data[i];
    }
    dst[pos++] = '"';
    return pos;
}

GrayString gray_csv_stringify(GrayArena *arena, GrayArray *data) {
    /* Accept [string] — each string is a pre-formatted CSV row.
     * Join with newlines. */
    if (data->elem_size == (int32_t)sizeof(GrayString)) {
        int32_t total = 0;
        for (int32_t i = 0; i < data->len; i++) {
            GrayString line = GRAY_ARRAY_GET(*data, GrayString, i);
            total += line.len + 1;
        }
        char *buf = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
        int32_t pos = 0;
        for (int32_t i = 0; i < data->len; i++) {
            GrayString line = GRAY_ARRAY_GET(*data, GrayString, i);
            memcpy(buf + pos, line.data, (size_t)line.len);
            pos += line.len;
            if (i < data->len - 1) buf[pos++] = '\n';
        }
        buf[pos] = '\0';
        return (GrayString){ buf, pos };
    }

    /* Fallback: array of arrays ([[string]] rows).
     * First pass: compute exact required size to avoid heap overflow. */
    int32_t total = 0;
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row = (GrayArray *)((char *)data->data + (size_t)i * sizeof(GrayArray));
        for (int32_t j = 0; j < row->len; j++) {
            if (j > 0) total++; /* comma */
            GrayString *field = (GrayString *)((char *)row->data + (size_t)j * sizeof(GrayString));
            total += csv_field_encoded_len(*field);
        }
        total++; /* newline */
    }
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
    int32_t pos = 0;
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row = (GrayArray *)((char *)data->data + (size_t)i * sizeof(GrayArray));
        for (int32_t j = 0; j < row->len; j++) {
            if (j > 0) buf[pos++] = ',';
            GrayString *field = (GrayString *)((char *)row->data + (size_t)j * sizeof(GrayString));
            pos += csv_field_encode(buf + pos, *field);
        }
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return (GrayString){ buf, pos };
}

GrayArray gray_csv_headers(GrayArena *arena, GrayArray *data) {
    if (data->len > 0) {
        GrayArray first_row = GRAY_ARRAY_GET(*data, GrayArray, 0);
        return gray_array_copy(arena, &first_row);
    }
    return gray_array_new(arena, sizeof(GrayString), 0);
}

GrayArray gray_csv_read(GrayArena *arena, GrayString path) {
    FILE *file = fopen(path.data, "rb");
    if (!file) return gray_array_new(arena, sizeof(GrayArray), 1);
    GrayString content = gray_io_read_file_impl(arena, file);
    fclose(file);
    if (content.data == NULL)
        gray_panic_code("P0114", "csv.read_file: input exceeds maximum string length");
    return gray_csv_parse(arena, content);
}

bool gray_csv_write(GrayArena *arena, GrayString path, GrayArray *data) {
    GrayString csv = gray_csv_stringify(arena, data);
    FILE *file = fopen(path.data, "wb");
    if (!file) return false;
    fwrite(csv.data, 1, (size_t)csv.len, file);
    fclose(file);
    return true;
}

/* _result variants */

GrayResult_array gray_csv_read_result(GrayArena *arena, GrayString path) {
    GrayResult_array result;
    FILE *file = fopen(path.data, "rb");
    if (!file) {
        result.v0 = gray_array_new(arena, sizeof(GrayArray), 0);
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot read CSV file '%s'", path.data));
        return result;
    }
    GrayString content = gray_io_read_file_impl(arena, file);
    fclose(file);
    if (content.data == NULL) {
        result.v0 = gray_array_new(arena, sizeof(GrayArray), 0);
        result.v1 = gray_error_new(arena, GRAY_ERR_OutOfRange, gray_string_format(arena,
            "cannot read '%s': file exceeds maximum string length", path.data));
        return result;
    }
    result.v0 = gray_csv_parse(arena, content);
    result.v1 = NULL;
    return result;
}

GrayResult_bool gray_csv_write_result(GrayArena *arena, GrayString path, GrayArray *data) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_csv_write(arena, path, data), gray_errno_code(errno),
        gray_string_format(arena, "cannot write CSV file '%s'", path.data));
}
