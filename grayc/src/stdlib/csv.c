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
#include <string.h>
#include <stdio.h>
#include <errno.h>

GrayArray gray_csv_parse(GrayArena *arena, GrayString csv_string) {
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
                while (s < end && *s != ',' && *s != '\n' && *s != '\r') s++;
                field_length = (int32_t)(s - field_start);
            }

            GrayString field = gray_string_new(arena, field_start, field_length);
            GRAY_ARRAY_PUSH(arena, &row, &field);

            if (s < end && *s == ',') s++;
        }
        GRAY_ARRAY_PUSH(arena, &rows, &row);

        /* Skip line ending */
        if (s < end && *s == '\r') s++;
        if (s < end && *s == '\n') s++;
    }
    return rows;
}

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
