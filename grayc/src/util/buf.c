/*
 * buf.c — Growable string buffer implementation used by the code generator
 * to build C source output via append, formatted append, and indentation.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "buf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void grow_buffer(StringBuffer *buffer, size_t needed) {
    if (buffer->length + needed + 1 <= buffer->capacity) return;
    size_t new_capacity = buffer->capacity * 2;
    if (new_capacity < buffer->length + needed + 1) {
        new_capacity = buffer->length + needed + 1;
    }
    buffer->data = realloc(buffer->data, new_capacity);
    if (!buffer->data) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    buffer->capacity = new_capacity;
}

StringBuffer buffer_create(size_t initial_capacity) {
    StringBuffer buffer;
    buffer.data = malloc(initial_capacity);
    if (!buffer.data) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    buffer.data[0] = '\0';
    buffer.length = 0;
    buffer.capacity = initial_capacity;
    return buffer;
}

void append_string_to_buffer(StringBuffer *buffer, const char *string) {
    size_t length = strlen(string);
    append_bytes_to_buffer(buffer, string, length);
}

void append_bytes_to_buffer(StringBuffer *buffer, const char *data, size_t length) {
    grow_buffer(buffer, length);
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

void append_format_to_buffer(StringBuffer *buffer, const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    int needed = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);

    if (needed < 0) return;

    grow_buffer(buffer, (size_t)needed);

    va_start(arguments, format);
    vsnprintf(buffer->data + buffer->length, (size_t)needed + 1, format, arguments);
    va_end(arguments);

    buffer->length += (size_t)needed;
}

void append_char_to_buffer(StringBuffer *buffer, char character) {
    grow_buffer(buffer, 1);
    buffer->data[buffer->length++] = character;
    buffer->data[buffer->length] = '\0';
}

#define BUFFER_INDENT_WIDTH 4

void append_indent_to_buffer(StringBuffer *buffer, int depth) {
    static const char spaces[] =
        "                                                                ";
    int space_count = depth * BUFFER_INDENT_WIDTH;
    if (space_count < (int)sizeof(spaces)) {
        append_bytes_to_buffer(buffer, spaces, (size_t)space_count);
    } else {
        for (int i = 0; i < depth; i++)
            append_bytes_to_buffer(buffer, spaces, BUFFER_INDENT_WIDTH);
    }
}

const char *buffer_to_string(StringBuffer *buffer) {
    return buffer->data;
}

void buffer_destroy(StringBuffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}
