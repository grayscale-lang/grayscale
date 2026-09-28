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
static GrayArray csv_parse_delimited(GrayArena *arena, GrayString csv_string, char delim) {
    GrayArray rows = gray_array_new(arena, sizeof(GrayArray), 8, GRAY_ELEM_ARRAY);
    const char *cursor = csv_string.data;
    const char *end_cursor = cursor + csv_string.len;

    while (cursor < end_cursor) {
        GrayArray row_array = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);
        while (cursor < end_cursor && *cursor != '\n' && *cursor != '\r') {
            const char *field_start;
            int32_t field_length;

            if (*cursor == '"') {
                /* Quoted field */
                cursor++;
                field_start = cursor;
                while (cursor < end_cursor && !(*cursor == '"' && (cursor + 1 >= end_cursor || *(cursor + 1) != '"'))) {
                    if (*cursor == '"' && *(cursor + 1) == '"') cursor += 2;
                    else cursor++;
                }
                field_length = (int32_t)(cursor - field_start);
                if (cursor < end_cursor) cursor++; /* skip closing quote */

                /* RFC 4180 §2.7: unescape doubled quotes ("") to single (") */
                if (memchr(field_start, '"', (size_t)field_length)) {
                    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)field_length);
                    int32_t row_count = 0;
                    for (int32_t field_index = 0; field_index < field_length; field_index++) {
                        buffer[row_count++] = field_start[field_index];
                        if (field_start[field_index] == '"' && field_index + 1 < field_length && field_start[field_index + 1] == '"')
                            field_index++; /* skip second quote of pair */
                    }
                    field_start = buffer;
                    field_length = row_count;
                }
            } else {
                /* Unquoted field */
                field_start = cursor;
                while (cursor < end_cursor && *cursor != delim && *cursor != '\n' && *cursor != '\r') cursor++;
                field_length = (int32_t)(cursor - field_start);
            }

            GrayString field = gray_string_new(arena, field_start, field_length);
            GRAY_ARRAY_PUSH(arena, &row_array, &field);

            if (cursor < end_cursor && *cursor == delim) cursor++;
        }
        GRAY_ARRAY_PUSH(arena, &rows, &row_array);

        /* Skip line ending */
        if (cursor < end_cursor && *cursor == '\r') cursor++;
        if (cursor < end_cursor && *cursor == '\n') cursor++;
    }
    return rows;
}

GrayArray gray_csv_parse(GrayArena *arena, GrayString csv_string) {
    return csv_parse_delimited(arena, csv_string, ',');
}

GrayArray gray_csv_parse_delimited(GrayArena *arena, GrayString csv_string, int32_t delimiter) {
    return csv_parse_delimited(arena, csv_string, (char)delimiter);
}

/* --- Helpers shared by the [[string]] / [map[string:string]] views --- */

/* Row `index` of parsed data (index must be in range). */
static GrayArray *csv_row(GrayArray *data, int32_t index) {
    return (GrayArray *)((char *)data->data + (size_t)index * sizeof(GrayArray));
}

/* Cell `index` of a row, or "" when the row is short. */
static GrayString csv_cell(GrayArray *row_array, int32_t index) {
    if (index < 0 || index >= row_array->len) return gray_string_lit("");
    return *(GrayString *)((char *)row_array->data + (size_t)index * sizeof(GrayString));
}

static bool csv_string_equal(GrayString left, GrayString right) {
    return left.len == right.len && memcmp(left.data, right.data, (size_t)left.len) == 0;
}

static int csv_string_compare(GrayString left, GrayString right) {
    int32_t common_length = left.len < right.len ? left.len : right.len;
    int comparison = memcmp(left.data, right.data, (size_t)common_length);
    if (comparison != 0) return comparison;
    return (left.len > right.len) - (left.len < right.len);
}

/* Index of column `name` in the header (row 0), or -1. */
static int32_t csv_column_index(GrayArray *data, GrayString name) {
    if (data->len == 0) return -1;
    GrayArray *header = csv_row(data, 0);
    for (int32_t j = 0; j < header->len; j++) {
        if (csv_string_equal(csv_cell(header, j), name)) return j;
    }
    return -1;
}

static _Noreturn void csv_no_such_column(GrayString name) {
    gray_panic_code("P0125", "csv: column '%.*s' is not in the header",
        (int)name.len, name.data);
}

GrayArray gray_csv_to_maps(GrayArena *arena, GrayArray *data) {
    GrayArray output = gray_array_new(arena, sizeof(GrayMap), data->len > 1 ? data->len - 1 : 0, GRAY_ELEM_MAP);
    if (data->len <= 1) return output;
    GrayArray *header = csv_row(data, 0);
    for (int32_t i = 1; i < data->len; i++) {
        GrayArray *row_array = csv_row(data, i);
        GrayMap map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), header->len > 0 ? header->len : 8, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
        int32_t pair_count = row_array->len < header->len ? row_array->len : header->len;
        for (int32_t j = 0; j < pair_count; j++) {
            GrayString key = csv_cell(header, j);
            GrayString value = csv_cell(row_array, j);
            GRAY_MAP_SET(arena, &map, &key, &value);
        }
        GRAY_ARRAY_PUSH(arena, &output, &map);
    }
    return output;
}

GrayArray gray_csv_from_maps(GrayArena *arena, GrayArray *rows) {
    GrayArray output = gray_array_new(arena, sizeof(GrayArray), rows->len + 1, GRAY_ELEM_ARRAY);
    if (rows->len == 0) return output;

    /* Header = union of keys across all rows, in first-seen order. */
    GrayArray header = gray_array_new(arena, sizeof(GrayString), 8, GRAY_ELEM_STRING);
    for (int32_t i = 0; i < rows->len; i++) {
        GrayMap *map = (GrayMap *)((char *)rows->data + (size_t)i * sizeof(GrayMap));
        for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
            int32_t slot = map->order[order_index];
            if (slot < 0 || map->states[slot] != 1) continue;
            GrayString *key = (GrayString *)((char *)map->keys + (size_t)slot * (size_t)map->key_size);
            bool seen = false;
            for (int32_t header_index = 0; header_index < header.len; header_index++) {
                if (csv_string_equal(csv_cell(&header, header_index), *key)) { seen = true; break; }
            }
            if (!seen) GRAY_ARRAY_PUSH(arena, &header, key);
        }
    }
    GRAY_ARRAY_PUSH(arena, &output, &header);

    for (int32_t i = 0; i < rows->len; i++) {
        GrayMap *map = (GrayMap *)((char *)rows->data + (size_t)i * sizeof(GrayMap));
        GrayArray cells = gray_array_new(arena, sizeof(GrayString), header.len, GRAY_ELEM_STRING);
        for (int32_t header_index = 0; header_index < header.len; header_index++) {
            GrayString key = csv_cell(&header, header_index);
            GrayString *found = (GrayString *)gray_map_get_str(map, key);
            GrayString cell = found ? *found : gray_string_lit("");
            GRAY_ARRAY_PUSH(arena, &cells, &cell);
        }
        GRAY_ARRAY_PUSH(arena, &output, &cells);
    }
    return output;
}

GrayArray gray_csv_column(GrayArena *arena, GrayArray *data, GrayString name) {
    int32_t column = csv_column_index(data, name);
    if (column < 0) csv_no_such_column(name);
    GrayArray output = gray_array_new(arena, sizeof(GrayString), data->len > 1 ? data->len - 1 : 0, GRAY_ELEM_STRING);
    for (int32_t i = 1; i < data->len; i++) {
        GrayString cell = csv_cell(csv_row(data, i), column);
        GRAY_ARRAY_PUSH(arena, &output, &cell);
    }
    return output;
}

GrayArray gray_csv_select(GrayArena *arena, GrayArray *data, GrayArray *names) {
    int32_t *indices = gray_arena_alloc_uninitialized(arena,
        (size_t)(names->len > 0 ? names->len : 1) * sizeof(int32_t));
    for (int32_t selected_index = 0; selected_index < names->len; selected_index++) {
        GrayString column_name = csv_cell(names, selected_index);
        int32_t column_index = csv_column_index(data, column_name);
        if (column_index < 0) csv_no_such_column(column_name);
        indices[selected_index] = column_index;
    }
    GrayArray output = gray_array_new(arena, sizeof(GrayArray), data->len, GRAY_ELEM_ARRAY);
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row_array = csv_row(data, i);
        GrayArray proj = gray_array_new(arena, sizeof(GrayString), names->len, GRAY_ELEM_STRING);
        for (int32_t selected_index = 0; selected_index < names->len; selected_index++) {
            GrayString cell = csv_cell(row_array, indices[selected_index]);
            GRAY_ARRAY_PUSH(arena, &proj, &cell);
        }
        GRAY_ARRAY_PUSH(arena, &output, &proj);
    }
    return output;
}

typedef struct { GrayString key; int32_t original_index; } CsvSortEnt;

static int csv_sort_compare(const void *left, const void *right) {
    const CsvSortEnt *left_ent = (const CsvSortEnt *)left;
    const CsvSortEnt *right_ent = (const CsvSortEnt *)right;
    int comparison = csv_string_compare(left_ent->key, right_ent->key);
    if (comparison != 0) return comparison;
    return (left_ent->original_index > right_ent->original_index) - (left_ent->original_index < right_ent->original_index); /* stable */
}

GrayArray gray_csv_sort_by_column(GrayArena *arena, GrayArray *data, GrayString name) {
    int32_t column = csv_column_index(data, name);
    if (column < 0) csv_no_such_column(name);
    GrayArray output = gray_array_new(arena, sizeof(GrayArray), data->len, GRAY_ELEM_ARRAY);
    if (data->len == 0) return output;
    GRAY_ARRAY_PUSH(arena, &output, csv_row(data, 0)); /* header stays first */

    int32_t body_row_count = data->len - 1;
    if (body_row_count <= 0) return output;
    CsvSortEnt *entries = gray_arena_alloc_uninitialized(arena, (size_t)body_row_count * sizeof(CsvSortEnt));
    for (int32_t i = 0; i < body_row_count; i++) {
        entries[i].key = csv_cell(csv_row(data, i + 1), column);
        entries[i].original_index = i;
    }
    qsort(entries, (size_t)body_row_count, sizeof(CsvSortEnt), csv_sort_compare);
    for (int32_t i = 0; i < body_row_count; i++) {
        GRAY_ARRAY_PUSH(arena, &output, csv_row(data, entries[i].original_index + 1));
    }
    return output;
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
        GrayArray *row_array = csv_row(data, i);
        gray_strings_builder_append_char(builder, '|');
        for (int32_t j = 0; j < row_array->len; j++) {
            GrayString cell = csv_cell(row_array, j);
            for (int32_t column_index = 0; column_index < cell.len; column_index++) {
                if (cell.data[column_index] == '|') gray_strings_builder_append_char(builder, '\\');
                gray_strings_builder_append_char(builder, (int32_t)(unsigned char)cell.data[column_index]);
            }
            gray_strings_builder_append_char(builder, '|');
        }
        gray_strings_builder_append_char(builder, '\n');
        if (i == 0) {
            gray_strings_builder_append_char(builder, '|');
            for (int32_t j = 0; j < row_array->len; j++) {
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
        char character = field.data[i];
        if (character == ',' || character == '"' || character == '\r' || character == '\n') return true;
    }
    return false;
}

/* Byte length of `field` once encoded: unchanged if it needs no quoting, else
 * the field plus the two surrounding quotes and one extra byte per embedded
 * quote. */
static int32_t csv_field_encoded_length(GrayString field) {
    if (!csv_field_needs_quote(field)) return field.len;
    int32_t length = field.len + 2;
    for (int32_t i = 0; i < field.len; i++)
        if (field.data[i] == '"') length++;
    return length;
}

/* Write the encoded form of `field` at `dst`; returns the bytes written. */
static int32_t csv_field_encode(char *destination, GrayString field) {
    if (!csv_field_needs_quote(field)) {
        memcpy(destination, field.data, (size_t)field.len);
        return field.len;
    }
    int32_t position = 0;
    destination[position++] = '"';
    for (int32_t i = 0; i < field.len; i++) {
        if (field.data[i] == '"') destination[position++] = '"';
        destination[position++] = field.data[i];
    }
    destination[position++] = '"';
    return position;
}

GrayString gray_csv_stringify(GrayArena *arena, GrayArray *data) {
    /* Accept [string] — each string is a pre-formatted CSV row.
     * Join with newlines. */
    if (data->elem_kind == GRAY_ELEM_STRING) {
        int32_t total = 0;
        for (int32_t i = 0; i < data->len; i++) {
            GrayString line = GRAY_ARRAY_GET(*data, GrayString, i);
            total += line.len + 1;
        }
        char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
        int32_t position = 0;
        for (int32_t i = 0; i < data->len; i++) {
            GrayString line = GRAY_ARRAY_GET(*data, GrayString, i);
            memcpy(buffer + position, line.data, (size_t)line.len);
            position += line.len;
            if (i < data->len - 1) buffer[position++] = '\n';
        }
        buffer[position] = '\0';
        return (GrayString){ buffer, position };
    }

    /* Fallback: array of arrays ([[string]] rows).
     * First pass: compute exact required size to avoid heap overflow. */
    int32_t total = 0;
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row_array = (GrayArray *)((char *)data->data + (size_t)i * sizeof(GrayArray));
        for (int32_t j = 0; j < row_array->len; j++) {
            if (j > 0) total++; /* comma */
            GrayString *field = (GrayString *)((char *)row_array->data + (size_t)j * sizeof(GrayString));
            total += csv_field_encoded_length(*field);
        }
        total++; /* newline */
    }
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
    int32_t position = 0;
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row_array = (GrayArray *)((char *)data->data + (size_t)i * sizeof(GrayArray));
        for (int32_t j = 0; j < row_array->len; j++) {
            if (j > 0) buffer[position++] = ',';
            GrayString *field = (GrayString *)((char *)row_array->data + (size_t)j * sizeof(GrayString));
            position += csv_field_encode(buffer + position, *field);
        }
        buffer[position++] = '\n';
    }
    buffer[position] = '\0';
    return (GrayString){ buffer, position };
}

GrayArray gray_csv_headers(GrayArena *arena, GrayArray *data) {
    if (data->len > 0) {
        GrayArray first_row = GRAY_ARRAY_GET(*data, GrayArray, 0);
        return gray_array_copy(arena, &first_row);
    }
    return gray_array_new(arena, sizeof(GrayString), 0, GRAY_ELEM_STRING);
}

GrayArray gray_csv_read(GrayArena *arena, GrayString path) {
    FILE *file = fopen(path.data, "rb");
    if (!file) return gray_array_new(arena, sizeof(GrayArray), 1, GRAY_ELEM_ARRAY);
    GrayString content = gray_io_read_file_impl(arena, file);
    fclose(file);
    if (content.data == NULL)
        gray_panic_code("P0114", "csv.read_file: input exceeds maximum string length");
    return gray_csv_parse(arena, content);
}

bool gray_csv_write(GrayArena *arena, GrayString path, GrayArray *data) {
    GrayString csv_text = gray_csv_stringify(arena, data);
    FILE *file = fopen(path.data, "wb");
    if (!file) return false;
    fwrite(csv_text.data, 1, (size_t)csv_text.len, file);
    fclose(file);
    return true;
}

/* _result variants */

GrayResult_array gray_csv_read_result(GrayArena *arena, GrayString path) {
    GrayResult_array result;
    FILE *file = fopen(path.data, "rb");
    if (!file) {
        result.v0 = gray_array_new(arena, sizeof(GrayArray), 0, GRAY_ELEM_ARRAY);
        result.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot read CSV file '%s'", path.data));
        return result;
    }
    GrayString content = gray_io_read_file_impl(arena, file);
    fclose(file);
    if (content.data == NULL) {
        result.v0 = gray_array_new(arena, sizeof(GrayArray), 0, GRAY_ELEM_ARRAY);
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
