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
    const char *s = csv_string.data;
    const char *end = s + csv_string.len;

    while (s < end) {
        GrayArray row = gray_array_new(arena, sizeof(GrayString), 8);
        while (s < end && *s != '\n' && *s != '\r') {
            const char *field_start;
            int32_t field_length;

            if (*s == '"') {
                /* Quoted field */
                s++;
                field_start = s;
                while (s < end && !(*s == '"' && (s + 1 >= end || *(s + 1) != '"'))) {
                    if (*s == '"' && *(s + 1) == '"') s += 2;
                    else s++;
                }
                field_length = (int32_t)(s - field_start);
                if (s < end) s++; /* skip closing quote */

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
                field_start = s;
                while (s < end && *s != delim && *s != '\n' && *s != '\r') s++;
                field_length = (int32_t)(s - field_start);
            }

            GrayString field = gray_string_new(arena, field_start, field_length);
            GRAY_ARRAY_PUSH(arena, &row, &field);

            if (s < end && *s == delim) s++;
        }
        GRAY_ARRAY_PUSH(arena, &rows, &row);

        /* Skip line ending */
        if (s < end && *s == '\r') s++;
        if (s < end && *s == '\n') s++;
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

/* Row i of parsed data (i must be in range). */
static GrayArray *csv_row(GrayArray *data, int32_t i) {
    return (GrayArray *)((char *)data->data + (size_t)i * sizeof(GrayArray));
}

/* Cell j of a row, or "" when the row is short. */
static GrayString csv_cell(GrayArray *row, int32_t j) {
    if (j < 0 || j >= row->len) return gray_string_lit("");
    return *(GrayString *)((char *)row->data + (size_t)j * sizeof(GrayString));
}

static bool csv_str_eq(GrayString a, GrayString b) {
    return a.len == b.len && memcmp(a.data, b.data, (size_t)a.len) == 0;
}

static int csv_str_cmp(GrayString a, GrayString b) {
    int32_t n = a.len < b.len ? a.len : b.len;
    int c = memcmp(a.data, b.data, (size_t)n);
    if (c != 0) return c;
    return (a.len > b.len) - (a.len < b.len);
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
        GrayMap m = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString),
                                 header->len > 0 ? header->len : 8);
        int32_t n = row->len < header->len ? row->len : header->len;
        for (int32_t j = 0; j < n; j++) {
            GrayString key = csv_cell(header, j);
            GrayString val = csv_cell(row, j);
            GRAY_MAP_SET(arena, &m, &key, &val);
        }
        GRAY_ARRAY_PUSH(arena, &out, &m);
    }
    return out;
}

GrayArray gray_csv_from_maps(GrayArena *arena, GrayArray *rows) {
    GrayArray out = gray_array_new(arena, sizeof(GrayArray), rows->len + 1);
    if (rows->len == 0) return out;

    /* Header = union of keys across all rows, in first-seen order. */
    GrayArray header = gray_array_new(arena, sizeof(GrayString), 8);
    for (int32_t i = 0; i < rows->len; i++) {
        GrayMap *m = (GrayMap *)((char *)rows->data + (size_t)i * sizeof(GrayMap));
        for (int32_t oi = 0; oi < m->order_len; oi++) {
            int32_t slot = m->order[oi];
            if (slot < 0 || m->states[slot] != 1) continue;
            GrayString *k = (GrayString *)((char *)m->keys + (size_t)slot * (size_t)m->key_size);
            bool seen = false;
            for (int32_t h = 0; h < header.len; h++) {
                if (csv_str_eq(csv_cell(&header, h), *k)) { seen = true; break; }
            }
            if (!seen) GRAY_ARRAY_PUSH(arena, &header, k);
        }
    }
    GRAY_ARRAY_PUSH(arena, &out, &header);

    for (int32_t i = 0; i < rows->len; i++) {
        GrayMap *m = (GrayMap *)((char *)rows->data + (size_t)i * sizeof(GrayMap));
        GrayArray cells = gray_array_new(arena, sizeof(GrayString), header.len);
        for (int32_t h = 0; h < header.len; h++) {
            GrayString key = csv_cell(&header, h);
            GrayString *v = (GrayString *)gray_map_get_str(m, key);
            GrayString cell = v ? *v : gray_string_lit("");
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
        GrayString nm = csv_cell(names, k);
        int32_t c = csv_col_index(data, nm);
        if (c < 0) csv_no_such_column(nm);
        idx[k] = c;
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

typedef struct { GrayString key; int32_t orig; } CsvSortEnt;

static int csv_sort_cmp(const void *a, const void *b) {
    const CsvSortEnt *ea = (const CsvSortEnt *)a;
    const CsvSortEnt *eb = (const CsvSortEnt *)b;
    int c = csv_str_cmp(ea->key, eb->key);
    if (c != 0) return c;
    return (ea->orig > eb->orig) - (ea->orig < eb->orig); /* stable */
}

GrayArray gray_csv_sort_by_column(GrayArena *arena, GrayArray *data, GrayString name) {
    int32_t col = csv_col_index(data, name);
    if (col < 0) csv_no_such_column(name);
    GrayArray out = gray_array_new(arena, sizeof(GrayArray), data->len);
    if (data->len == 0) return out;
    GRAY_ARRAY_PUSH(arena, &out, csv_row(data, 0)); /* header stays first */

    int32_t n = data->len - 1;
    if (n <= 0) return out;
    CsvSortEnt *ents = gray_arena_alloc_uninitialized(arena, (size_t)n * sizeof(CsvSortEnt));
    for (int32_t i = 0; i < n; i++) {
        ents[i].key = csv_cell(csv_row(data, i + 1), col);
        ents[i].orig = i;
    }
    qsort(ents, (size_t)n, sizeof(CsvSortEnt), csv_sort_cmp);
    for (int32_t i = 0; i < n; i++) {
        GRAY_ARRAY_PUSH(arena, &out, csv_row(data, ents[i].orig + 1));
    }
    return out;
}

int32_t gray_csv_detect_delimiter(GrayString sample) {
    static const char cands[] = {',', ';', '\t', '|'};
    int32_t line_end = sample.len;
    for (int32_t i = 0; i < sample.len; i++) {
        if (sample.data[i] == '\n' || sample.data[i] == '\r') { line_end = i; break; }
    }
    int best_count = 0;
    char best = ',';
    for (int c = 0; c < 4; c++) {
        int cnt = 0;
        for (int32_t i = 0; i < line_end; i++) {
            if (sample.data[i] == cands[c]) cnt++;
        }
        if (cnt > best_count) { best_count = cnt; best = cands[c]; }
    }
    return (int32_t)(unsigned char)best;
}

GrayString gray_csv_to_json(GrayArena *arena, GrayArray *data) {
    GrayArray maps = gray_csv_to_maps(arena, data);
    GrayStringsBuilder *b = gray_strings_builder(arena);
    gray_strings_builder_append_char(b, '[');
    for (int32_t i = 0; i < maps.len; i++) {
        if (i > 0) gray_strings_builder_append_char(b, ',');
        GrayMap *m = (GrayMap *)((char *)maps.data + (size_t)i * sizeof(GrayMap));
        gray_strings_builder_append(b, gray_json_encode_map(arena, m));
    }
    gray_strings_builder_append_char(b, ']');
    return gray_strings_build(arena, b);
}

GrayString gray_csv_to_markdown(GrayArena *arena, GrayArray *data) {
    if (data->len == 0) return gray_string_lit("");
    GrayStringsBuilder *b = gray_strings_builder(arena);
    for (int32_t i = 0; i < data->len; i++) {
        GrayArray *row = csv_row(data, i);
        gray_strings_builder_append_char(b, '|');
        for (int32_t j = 0; j < row->len; j++) {
            GrayString cell = csv_cell(row, j);
            for (int32_t k = 0; k < cell.len; k++) {
                if (cell.data[k] == '|') gray_strings_builder_append_char(b, '\\');
                gray_strings_builder_append_char(b, (int32_t)(unsigned char)cell.data[k]);
            }
            gray_strings_builder_append_char(b, '|');
        }
        gray_strings_builder_append_char(b, '\n');
        if (i == 0) {
            gray_strings_builder_append_char(b, '|');
            for (int32_t j = 0; j < row->len; j++) {
                gray_strings_builder_append(b, gray_string_lit("---|"));
            }
            gray_strings_builder_append_char(b, '\n');
        }
    }
    return gray_strings_build(arena, b);
}

/* Row-filter callback plumbing: filter_rows is emitted inline by codegen (it
 * takes a Grayscale func value), so there is no C entry point for it here. */

GrayString gray_csv_stringify(GrayArena *arena, GrayArray *data) {
    /* Accept [string] — each string is a pre-formatted CSV row.
     * Join with newlines. */
    if (data->elem_size == (int32_t)sizeof(GrayString)) {
        int32_t total = 0;
        for (int32_t i = 0; i < data->len; i++) {
            GrayString s = GRAY_ARRAY_GET(*data, GrayString, i);
            total += s.len + 1;
        }
        char *buf = gray_arena_alloc_uninitialized(arena, (size_t)total + 1);
        int32_t pos = 0;
        for (int32_t i = 0; i < data->len; i++) {
            GrayString s = GRAY_ARRAY_GET(*data, GrayString, i);
            memcpy(buf + pos, s.data, (size_t)s.len);
            pos += s.len;
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
            total += field->len;
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
            memcpy(buf + pos, field->data, (size_t)field->len);
            pos += field->len;
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
    FILE *f = fopen(path.data, "rb");
    if (!f) return gray_array_new(arena, sizeof(GrayArray), 1);
    GrayString content = gray_io_read_file_impl(arena, f);
    fclose(f);
    if (content.data == NULL)
        gray_panic_code("P0114", "csv.read_file: input exceeds maximum string length");
    return gray_csv_parse(arena, content);
}

bool gray_csv_write(GrayArena *arena, GrayString path, GrayArray *data) {
    GrayString csv = gray_csv_stringify(arena, data);
    FILE *f = fopen(path.data, "wb");
    if (!f) return false;
    fwrite(csv.data, 1, (size_t)csv.len, f);
    fclose(f);
    return true;
}

/* _result variants */

GrayResult_array gray_csv_read_result(GrayArena *arena, GrayString path) {
    GrayResult_array r;
    FILE *f = fopen(path.data, "rb");
    if (!f) {
        r.v0 = gray_array_new(arena, sizeof(GrayArray), 0);
        r.v1 = gray_error_new(arena, gray_errno_code(errno), gray_string_format(arena, "cannot read CSV file '%s'", path.data));
        return r;
    }
    GrayString content = gray_io_read_file_impl(arena, f);
    fclose(f);
    if (content.data == NULL) {
        r.v0 = gray_array_new(arena, sizeof(GrayArray), 0);
        r.v1 = gray_error_new(arena, GRAY_ERR_OutOfRange, gray_string_format(arena,
            "cannot read '%s': file exceeds maximum string length", path.data));
        return r;
    }
    r.v0 = gray_csv_parse(arena, content);
    r.v1 = NULL;
    return r;
}

GrayResult_bool gray_csv_write_result(GrayArena *arena, GrayString path, GrayArray *data) {
    GRAY_RESULT_WRAP_BOOL(arena, gray_csv_write(arena, path, data), gray_errno_code(errno),
        gray_string_format(arena, "cannot write CSV file '%s'", path.data));
}
