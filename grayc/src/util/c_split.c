/*
 * c_split.c — Splits the generated C of one program into several sources that
 * a C compiler can build in parallel. See c_split.h.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2026-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "c_split.h"
#include "buf.h"
#include "xalloc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* How a line of the generated C is shared among the sources. */
typedef enum {
    LINE_COMMON,
    LINE_PROTOTYPE,
    LINE_FUNCTION_FIRST,
    LINE_FUNCTION_BODY,
    LINE_UNIT_ZERO,
    LINE_GLOBAL
} LineKind;

typedef struct {
    const char *text;
    size_t length;
    LineKind kind;
    int owner;
} SourceLine;

#define STATIC_PREFIX "static "
#define EXTERN_PREFIX "extern "
#define MAIN_PREFIX "int main("
#define TABLE_PREFIX "const "
#define GLOBAL_INITIALIZER " = "

/* The first characters of a top-level line that is not a variable definition. */
static const char *const NOT_A_GLOBAL_PREFIXES[] = {
    "#", " ", "/", "}", "typedef", "struct", "enum", "union", "extern", "static", "const"
};

static bool line_starts_with(const SourceLine *line, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return line->length >= prefix_length && memcmp(line->text, prefix, prefix_length) == 0;
}

static bool line_ends_with(const SourceLine *line, const char *suffix) {
    size_t suffix_length = strlen(suffix);
    return line->length >= suffix_length &&
           memcmp(line->text + line->length - suffix_length, suffix, suffix_length) == 0;
}

static bool line_contains(const SourceLine *line, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length > line->length) return false;
    for (size_t offset = 0; offset + needle_length <= line->length; offset++) {
        if (memcmp(line->text + offset, needle, needle_length) == 0) return true;
    }
    return false;
}

static bool is_global_definition(const SourceLine *line) {
    if (line->length == 0 || !line_ends_with(line, ";")) return false;
    for (size_t index = 0; index < sizeof(NOT_A_GLOBAL_PREFIXES) / sizeof(NOT_A_GLOBAL_PREFIXES[0]); index++) {
        if (line_starts_with(line, NOT_A_GLOBAL_PREFIXES[index])) return false;
    }
    return !line_starts_with(line, MAIN_PREFIX);
}

/* The length of the type and name of a global, before any initializer or the
 * closing semicolon. */
static size_t global_declarator_length(const SourceLine *line) {
    size_t initializer_length = strlen(GLOBAL_INITIALIZER);
    for (size_t offset = 0; offset + initializer_length <= line->length; offset++) {
        if (memcmp(line->text + offset, GLOBAL_INITIALIZER, initializer_length) == 0) return offset;
    }
    return line->length - 1;
}

static bool declarator_has_parenthesis(const SourceLine *line) {
    size_t declarator_length = global_declarator_length(line);
    return memchr(line->text, '(', declarator_length) != NULL;
}

/* A line that starts a variable definition whose initializer runs on to the next line. */
static bool is_multiline_initializer(const SourceLine *line) {
    return line_contains(line, GLOBAL_INITIALIZER) && !line_starts_with(line, "#") &&
           !line_starts_with(line, " ") && !line_ends_with(line, ";") && !line_ends_with(line, "{") &&
           !line_ends_with(line, "\\") && !line_starts_with(line, "/") && !line_starts_with(line, "}");
}

static void append_line(StringBuffer *buffer, const char *text, size_t length) {
    append_bytes_to_buffer(buffer, text, length);
    append_char_to_buffer(buffer, '\n');
}

char **split_c_units(const char *source, int unit_count) {
    if (unit_count < 2 || unit_count > MAX_C_UNITS) return NULL;

    size_t line_count = 1;
    for (const char *cursor = source; *cursor; cursor++) {
        if (*cursor == '\n') line_count++;
    }
    SourceLine *lines = xmalloc(sizeof(SourceLine) * line_count);
    size_t *function_starts = xmalloc(sizeof(size_t) * line_count);
    size_t *function_sizes = xmalloc(sizeof(size_t) * line_count);
    size_t function_count = 0;
    char **units = NULL;

    bool is_inside_function = false;
    bool is_inside_main = false;
    bool is_inside_table = false;
    size_t block_size = 0;
    size_t total_size = 0;
    size_t line_index = 0;
    const char *line_start = source;
    for (;;) {
        const char *line_end = strchr(line_start, '\n');
        size_t length = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        SourceLine *line = &lines[line_index];
        line->text = line_start;
        line->length = length;
        line->kind = LINE_COMMON;
        line->owner = 0;
        if (is_inside_function) {
            line->kind = LINE_FUNCTION_BODY;
            block_size += length + 1;
            if (length == 1 && line_start[0] == '}') {
                is_inside_function = false;
                function_sizes[function_count - 1] = block_size;
                total_size += block_size;
            }
        } else if (is_inside_main) {
            line->kind = LINE_UNIT_ZERO;
            if (length == 1 && line_start[0] == '}') is_inside_main = false;
        } else if (is_inside_table) {
            line->kind = LINE_UNIT_ZERO;
            if (line_ends_with(line, ";")) is_inside_table = false;
        } else if (line_starts_with(line, STATIC_PREFIX)) {
            if (line_ends_with(line, ") {")) {
                line->kind = LINE_FUNCTION_FIRST;
                is_inside_function = true;
                block_size = length + 1;
                function_starts[function_count++] = line_index;
            } else if (line_ends_with(line, ");")) {
                line->kind = LINE_PROTOTYPE;
            } else {
                goto unknown_shape;
            }
        } else if (line_starts_with(line, MAIN_PREFIX)) {
            line->kind = LINE_UNIT_ZERO;
            is_inside_main = true;
        } else if (line_starts_with(line, TABLE_PREFIX)) {
            line->kind = LINE_UNIT_ZERO;
            is_inside_table = !line_ends_with(line, ";");
        } else if (is_global_definition(line)) {
            line->kind = declarator_has_parenthesis(line) ? LINE_COMMON : LINE_GLOBAL;
        } else if (is_multiline_initializer(line)) {
            goto unknown_shape;
        }
        line_index++;
        if (!line_end) break;
        line_start = line_end + 1;
    }
    if (is_inside_function || is_inside_main || is_inside_table || total_size == 0) goto unknown_shape;

    /* Each function goes to the source its first byte falls in. */
    size_t size_before = 0;
    for (size_t function_index = 0; function_index < function_count; function_index++) {
        int owner = (int)(size_before * (size_t)unit_count / total_size);
        size_t body_index = function_starts[function_index];
        lines[body_index].owner = owner;
        for (body_index++; body_index < line_count && lines[body_index].kind == LINE_FUNCTION_BODY; body_index++) {
            lines[body_index].owner = owner;
        }
        size_before += function_sizes[function_index];
    }

    units = xmalloc(sizeof(char *) * (size_t)unit_count);
    for (int unit_index = 0; unit_index < unit_count; unit_index++) {
        StringBuffer unit = buffer_create(strlen(source) / (size_t)unit_count + 4096);
        for (size_t index = 0; index < line_count; index++) {
            const SourceLine *line = &lines[index];
            switch (line->kind) {
            case LINE_COMMON:
                append_line(&unit, line->text, line->length);
                break;
            case LINE_PROTOTYPE:
                append_line(&unit, line->text + strlen(STATIC_PREFIX), line->length - strlen(STATIC_PREFIX));
                break;
            case LINE_FUNCTION_FIRST:
                if (line->owner == unit_index)
                    append_line(&unit, line->text + strlen(STATIC_PREFIX), line->length - strlen(STATIC_PREFIX));
                break;
            case LINE_FUNCTION_BODY:
                if (line->owner == unit_index) append_line(&unit, line->text, line->length);
                break;
            case LINE_UNIT_ZERO:
                if (unit_index == 0) append_line(&unit, line->text, line->length);
                break;
            case LINE_GLOBAL:
                if (unit_index == 0) {
                    append_line(&unit, line->text, line->length);
                } else {
                    append_string_to_buffer(&unit, EXTERN_PREFIX);
                    append_bytes_to_buffer(&unit, line->text, global_declarator_length(line));
                    append_string_to_buffer(&unit, ";\n");
                }
                break;
            }
        }
        units[unit_index] = unit.data;
    }

unknown_shape:
    free(lines);
    free(function_starts);
    free(function_sizes);
    return units;
}
