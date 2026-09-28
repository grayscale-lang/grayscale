/*
 * buf.h — Public interface for the growable string buffer used by the
 * code generator to emit C source output.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_BUF_H
#define GRAYC_BUF_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StringBuffer;

StringBuffer buffer_create(size_t initial_capacity);
void append_string_to_buffer(StringBuffer *buffer, const char *string);
void append_bytes_to_buffer(StringBuffer *buffer, const char *data, size_t length);
void append_format_to_buffer(StringBuffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
void append_char_to_buffer(StringBuffer *buffer, char character);
void append_indent_to_buffer(StringBuffer *buffer, int depth);
const char *buffer_to_string(StringBuffer *buffer);
void buffer_destroy(StringBuffer *buffer);

#endif
