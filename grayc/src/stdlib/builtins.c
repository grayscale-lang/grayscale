/*
 * builtins.c — Implementation of built-in functions available
 * without any import. Includes println, print, input, len, typeof,
 * size_of, assert, exit, sleep, panic, and string conversion helpers.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "builtins.h"
#include "strconv.h"
#include "../runtime/bigint.h"
#include "../runtime/platform_rt.h"
#include "../util/constants.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

#if GRAY_RUNTIME_WINDOWS
#include "../runtime/win32.h"
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#define GRAY_TO_STRING_BUFFER_SIZE    4096
#define GRAY_TO_STRING_SAFE_LIMIT  (GRAY_TO_STRING_BUFFER_SIZE - 96)
#define GRAY_FLOATING_POINT_STRING_BUFFER_SIZE        64
#define GRAY_INPUT_BUFFER_SIZE       4096

/* Encode Unicode codepoint to UTF-8; returns byte count (1-4). */
static int codepoint_to_utf8(int32_t codepoint, char *output) {
    if (codepoint < 0x80)   { output[0] = (char)codepoint; return 1; }
    if (codepoint < 0x800)  { output[0] = (char)(0xC0|(codepoint>>6)); output[1] = (char)(0x80|(codepoint&0x3F)); return 2; }
    if (codepoint < 0x10000){ output[0] = (char)(0xE0|(codepoint>>12)); output[1] = (char)(0x80|((codepoint>>6)&0x3F)); output[2] = (char)(0x80|(codepoint&0x3F)); return 3; }
    output[0]=(char)(0xF0|(codepoint>>18)); output[1]=(char)(0x80|((codepoint>>12)&0x3F)); output[2]=(char)(0x80|((codepoint>>6)&0x3F)); output[3]=(char)(0x80|(codepoint&0x3F)); return 4;
}

/* Decode next UTF-8 character; returns bytes consumed (1-4).
   Writes decoded codepoint to *cp_out (0xFFFD on invalid input). */
int gray_builtin_utf8_next(const uint8_t *cursor, const uint8_t *end_cursor, int32_t *cp_out) {
    uint8_t lead_byte = *cursor;
    int32_t codepoint;
    int bytes;
    if (lead_byte < 0x80) {
        *cp_out = lead_byte; return 1;
    } else if ((lead_byte & 0xE0) == 0xC0) {
        codepoint = lead_byte & 0x1F; bytes = 2;
    } else if ((lead_byte & 0xF0) == 0xE0) {
        codepoint = lead_byte & 0x0F; bytes = 3;
    } else if ((lead_byte & 0xF8) == 0xF0) {
        codepoint = lead_byte & 0x07; bytes = 4;
    } else {
        *cp_out = 0xFFFD; return 1;
    }
    if (cursor + bytes > end_cursor) { *cp_out = 0xFFFD; return 1; }
    for (int i = 1; i < bytes; i++) {
        if ((cursor[i] & 0xC0) != 0x80) { *cp_out = 0xFFFD; return 1; }
        codepoint = (codepoint << 6) | (cursor[i] & 0x3F);
    }
    *cp_out = codepoint;
    return bytes;
}

/* Write a Unicode code point as UTF-8 to a FILE stream */
static void write_utf8(int32_t codepoint, FILE *stream) {
    char buffer[4];
    int length = codepoint_to_utf8(codepoint, buffer);
    fwrite(buffer, 1, (size_t)length, stream);
}

/* --- Core print helpers (one per type) --- */

static void print_core_str(GrayString string, FILE *stream, bool newline) {
    fwrite(string.data, 1, (size_t)string.len, stream);
    if (newline) fputc('\n', stream);
}

static void print_core_i64(int64_t value, FILE *stream, bool newline) {
    fprintf(stream, "%" PRId64, value);
    if (newline) fputc('\n', stream);
}

static void print_core_u64(uint64_t value, FILE *stream, bool newline) {
    fprintf(stream, "%" PRIu64, value);
    if (newline) fputc('\n', stream);
}

static void print_core_float(double value, int bit_size, FILE *stream, bool newline) {
    char buffer[GRAY_FLOATING_POINT_STRING_BUFFER_SIZE];
    gray_fmt_shortest_float(buffer, sizeof(buffer), value, bit_size);
    fprintf(stream, "%s", buffer);
    if (newline) fputc('\n', stream);
}

static void print_core_bool(bool value, FILE *stream, bool newline) {
    fprintf(stream, "%s", value ? "true" : "false");
    if (newline) fputc('\n', stream);
}

static void print_core_char(int32_t codepoint, FILE *stream, bool newline) {
    write_utf8(codepoint, stream);
    if (newline) fputc('\n', stream);
}

static void print_core_addr(uintptr_t value, FILE *stream, bool newline) {
    fprintf(stream, "0x%" PRIxPTR, value);
    if (newline) fputc('\n', stream);
}

/* --- Public print/println/eprint/eprintln wrappers --- */

#define PRINT_FAMILY(SUFFIX, CTYPE)                                                         \
    void gray_builtin_println_##SUFFIX(CTYPE value)  { print_core_##SUFFIX(value, stdout, true);  } \
    void gray_builtin_print_##SUFFIX(CTYPE value)    { print_core_##SUFFIX(value, stdout, false); } \
    void gray_builtin_eprintln_##SUFFIX(CTYPE value) { print_core_##SUFFIX(value, stderr, true);  } \
    void gray_builtin_eprint_##SUFFIX(CTYPE value)   { print_core_##SUFFIX(value, stderr, false); }

PRINT_FAMILY(str,   GrayString)
PRINT_FAMILY(i64,   int64_t)
PRINT_FAMILY(u64,   uint64_t)
PRINT_FAMILY(bool,  bool)
PRINT_FAMILY(char,  int32_t)
PRINT_FAMILY(addr,  uintptr_t)

/* A floating-point value also carries its bit size (32 or 64) so it prints at its own
 * precision. */
void gray_builtin_println_float(double value, int bit_size)  { print_core_float(value, bit_size, stdout, true);  }
void gray_builtin_print_float(double value, int bit_size)    { print_core_float(value, bit_size, stdout, false); }
void gray_builtin_eprintln_float(double value, int bit_size) { print_core_float(value, bit_size, stderr, true);  }
void gray_builtin_eprint_float(double value, int bit_size)   { print_core_float(value, bit_size, stderr, false); }

#undef PRINT_FAMILY

/* --- flush --- */

void gray_builtin_flush(void) {
    fflush(stdout);
}

/* --- input --- */

GrayString gray_builtin_input(GrayArena *arena) {
    char buffer[GRAY_INPUT_BUFFER_SIZE];
    fflush(stdout);
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return gray_string_lit("");
    }
    size_t length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        length--;
    } else if (length == GRAY_INPUT_BUFFER_SIZE - 1) {
        /* Buffer filled without reaching a newline — drain the rest of the
         * line so the next input() call reads the correct line. */
        int character;
        while ((character = getc(stdin)) != '\n' && character != EOF)
            ;
    }
    return gray_string_new(arena, buffer, (int32_t)length);
}

/* --- assert --- */

void gray_builtin_assert(bool condition, GrayString message, const char *file, int line) {
    if (condition) return;

    /* Under `gray test`, a failed assert is a test failure — record it and
     * longjmp back to the runner instead of ending the process. */
    if (gray_test_active) {
        if (message.len > 0)
            gray_test_fail("P0075", file, line, "assertion failed: %.*s",
                           (int)message.len, message.data);
        else
            gray_test_fail("P0075", file, line, "assertion failed");
    }

    fflush(stdout);
    fprintf(stderr, "panic[P0075]: assertion failed");
    if (message.len > 0) {
        fprintf(stderr, ": ");
        fwrite(message.data, 1, (size_t)message.len, stderr);
    }
    fputc('\n', stderr);
    exit(1);
}

/* --- panic --- */

void gray_builtin_panic_msg(GrayString message) {
    /* Under `gray test`, a panic is a test failure — same as gray_builtin_assert. */
    if (gray_test_active)
        gray_test_fail("P0076", NULL, 0, "%.*s", (int)message.len, message.data);

    fflush(stdout);
    fprintf(stderr, "panic[P0076]: ");
    fwrite(message.data, 1, (size_t)message.len, stderr);
    fputc('\n', stderr);
    exit(1);
}

/* --- exit --- */

void gray_builtin_exit(int64_t code) {
    exit((int)code);
}

/* --- sleep --- */

void gray_builtin_sleep_s(int64_t seconds) {
    if (seconds < 0) gray_panic_code("P0083", "sleep duration cannot be negative (%lld)", (long long)seconds);
#if GRAY_RUNTIME_WINDOWS
    if (seconds > 0) Sleep((DWORD)(seconds * MILLISECONDS_PER_SECOND));
#else
    if (seconds > 0) sleep((unsigned int)seconds);
#endif
}

void gray_builtin_sleep_ms(int64_t milliseconds) {
    if (milliseconds < 0) gray_panic_code("P0083", "sleep duration cannot be negative (%lld)", (long long)milliseconds);
#if GRAY_RUNTIME_WINDOWS
    if (milliseconds > 0) Sleep((DWORD)milliseconds);
#else
    if (milliseconds > 0) {
        struct timespec time_spec;
        time_spec.tv_sec = milliseconds / MILLISECONDS_PER_SECOND;
        time_spec.tv_nsec = (milliseconds % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
        nanosleep(&time_spec, NULL);
    }
#endif
}

void gray_builtin_sleep_ns(int64_t nanoseconds) {
#if GRAY_RUNTIME_WINDOWS
    /* Sleep() has millisecond granularity; round up so we never sleep 0. */
    if (nanoseconds > 0) Sleep((DWORD)((nanoseconds + NANOSECONDS_PER_MILLISECOND - 1) / NANOSECONDS_PER_MILLISECOND));
#else
    if (nanoseconds > 0) {
        struct timespec time_spec;
        time_spec.tv_sec = nanoseconds / NANOSECONDS_PER_SECOND;
        time_spec.tv_nsec = nanoseconds % NANOSECONDS_PER_SECOND;
        nanosleep(&time_spec, NULL);
    }
#endif
}

/* --- system --- */

int64_t gray_builtin_system(GrayString command) {
    char *cstr = malloc((size_t)command.len + 1);
    if (!cstr) return -1;
    memcpy(cstr, command.data, (size_t)command.len);
    cstr[command.len] = '\0';
    /* Flush our buffered output so it lands before the child's, which inherits
     * the same stdout fd. */
    fflush(stdout);
    fflush(stderr);
    int status = system(cstr);
    free(cstr);
#if GRAY_RUNTIME_WINDOWS
    return (int64_t)status;
#else
    if (WIFEXITED(status)) return (int64_t)WEXITSTATUS(status);
    return -1;
#endif
}

/* --- to_string --- */

GrayString gray_builtin_to_string_i64(GrayArena *arena, int64_t value) {
    return gray_strconv_from_int(arena, value);
}

GrayString gray_builtin_to_string_u64(GrayArena *arena, uint64_t value) {
    return gray_strconv_from_uint(arena, value);
}

GrayString gray_builtin_to_string_float(GrayArena *arena, double value, int bit_size) {
    char buffer[GRAY_FLOATING_POINT_STRING_BUFFER_SIZE];
    int length = gray_fmt_shortest_float(buffer, sizeof(buffer), value, bit_size);
    return gray_string_new(arena, buffer, length);
}

GrayString gray_builtin_format_float(GrayArena *arena, double value, int bit_size) {
    return gray_builtin_to_string_float(arena, value, bit_size);
}

GrayString gray_builtin_to_string_bool(GrayArena *arena, bool value) {
    return value ? gray_string_lit("true") : gray_string_lit("false");
}

/* --- from_string --- */

int64_t gray_builtin_string_to_i64(GrayString string) {
    char buffer[GRAY_FLOATING_POINT_STRING_BUFFER_SIZE];
    int length = string.len < (int32_t)sizeof(buffer) - 1 ? string.len : (int32_t)sizeof(buffer) - 1;
    memcpy(buffer, string.data, (size_t)length);
    buffer[length] = '\0';
    char *end_cursor = NULL;
    int64_t result = strtoll(buffer, &end_cursor, 10);
    if (end_cursor == buffer || (*end_cursor != '\0' && *end_cursor != ' ')) {
        gray_panic_code("P0084", "cannot convert '%s' to i64", buffer);
    }
    return result;
}

double gray_builtin_string_to_f64(GrayString string) {
    char buffer[GRAY_FLOATING_POINT_STRING_BUFFER_SIZE];
    int length = string.len < (int32_t)sizeof(buffer) - 1 ? string.len : (int32_t)sizeof(buffer) - 1;
    memcpy(buffer, string.data, (size_t)length);
    buffer[length] = '\0';
    char *end_cursor = NULL;
    double result = strtod(buffer, &end_cursor);
    if (end_cursor == buffer || (*end_cursor != '\0' && *end_cursor != ' ')) {
        gray_panic_code("P0085", "cannot convert '%s' to f64", buffer);
    }
    return result;
}

/* --- composite to_string --- */

/* A floating-point element in its shortest round-trip form at its own width. */
static void format_float_element(char *output, size_t out_size, const void *element, int32_t element_kind) {
    gray_fmt_shortest_float(output, out_size, gray_elem_to_double(element_kind, element),
        element_kind == GRAY_ELEM_F32 ? 32 : 64);
}

/* An integer element of any width, read by its element kind, as decimal
 * text at buf[pos]; returns the new pos. */
static int format_integer_element(GrayArena *arena, char *buffer, size_t buffer_size, int position,
                                  const void *element, int32_t element_kind) {
    GrayString text;
    switch (element_kind) {
    case GRAY_ELEM_I128: text = gray_i128_to_string(arena, *(const gray_i128 *)element); break;
    case GRAY_ELEM_U128: text = gray_u128_to_string(arena, *(const gray_u128 *)element); break;
    case GRAY_ELEM_I256: text = gray_i256_to_string(arena, *(const gray_i256 *)element); break;
    case GRAY_ELEM_U256: text = gray_u256_to_string(arena, *(const gray_u256 *)element); break;
    case GRAY_ELEM_U8: case GRAY_ELEM_U16: case GRAY_ELEM_U32: case GRAY_ELEM_U64:
        return position + snprintf(buffer + position, buffer_size - position, "%" PRIu64, gray_elem_to_u64(element_kind, element));
    default:
        return position + snprintf(buffer + position, buffer_size - position, "%" PRId64, gray_elem_to_i64(element_kind, element));
    }
    return position + snprintf(buffer + position, buffer_size - position, "%.*s", (int)text.len, text.data);
}

/* Append one element/value of the given kind (see the to_string callers) at
 * buf[pos]; returns the new pos. An integer is read by its element kind. */
static int format_value_into(GrayArena *arena, char *buffer, size_t buffer_size, int position, int kind,
                             const void *value_pointer, int32_t element_kind) {
    switch (kind) {
    case 0:
        position = format_integer_element(arena, buffer, buffer_size, position, value_pointer, element_kind);
        break;
    case 1: {
        char floating_point_buffer[GRAY_FLOATING_POINT_STRING_BUFFER_SIZE];
        format_float_element(floating_point_buffer, sizeof(floating_point_buffer), value_pointer, element_kind);
        position += snprintf(buffer + position, buffer_size - position, "%s", floating_point_buffer);
        break;
    }
    case 2: {
        const GrayString *element = (const GrayString *)value_pointer;
        position += snprintf(buffer + position, buffer_size - position, "\"%.*s\"",
            (int)element->len, element->data ? element->data : "");
        break;
    }
    case 3:
        position += snprintf(buffer + position, buffer_size - position, "%s", *(const bool *)value_pointer ? "true" : "false");
        break;
    case 6: {
        int32_t codepoint = *(const int32_t *)value_pointer;
        char utf8[4]; int utf8_length = codepoint_to_utf8(codepoint, utf8);
        if (position + 2 + utf8_length < (int)buffer_size) {
            buffer[position++] = '\'';
            memcpy(buffer + position, utf8, (size_t)utf8_length); position += utf8_length;
            buffer[position++] = '\'';
        }
        break;
    }
    case 7:
        position += snprintf(buffer + position, buffer_size - position, "%d", *(const int *)value_pointer);
        break;
    }
    return position;
}

GrayString gray_builtin_array_to_string(GrayArena *arena, GrayArray *array, int element_kind) {
    char buffer[GRAY_TO_STRING_BUFFER_SIZE];
    int position = 0;
    buffer[position++] = '{';
    for (int32_t i = 0; i < array->len && position < GRAY_TO_STRING_SAFE_LIMIT; i++) {
        if (i > 0) { buffer[position++] = ','; buffer[position++] = ' '; }
        position = format_value_into(arena, buffer, sizeof(buffer), position, element_kind,
            (char *)array->data + (size_t)i * (size_t)array->elem_size, array->elem_kind);
    }
    buffer[position++] = '}';
    buffer[position] = '\0';
    return gray_string_new(arena, buffer, (int32_t)position);
}

/* --- GrayFmtOut: the in-memory stream generated print code can target --- */

static void format_output_reserve(GrayFmtOut *output, size_t extra) {
    if (output->len + extra + 1 <= output->capacity) return;
    size_t capacity = output->capacity ? output->capacity * 2 : 128;
    while (capacity < output->len + extra + 1) capacity *= 2;
    output->data = realloc(output->data, capacity);
    if (!output->data) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    output->capacity = capacity;
}

int gray_fmt_out_printf(GrayFmtOut *output, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int needed = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    if (needed < 0) return needed;

    format_output_reserve(output, (size_t)needed);
    va_start(arguments, format);
    vsnprintf(output->data + output->len, (size_t)needed + 1, format, arguments);
    va_end(arguments);
    output->len += (size_t)needed;
    return needed;
}

size_t gray_fmt_out_write(const void *data, size_t size, size_t count, GrayFmtOut *output) {
    size_t total = size * count;
    format_output_reserve(output, total);
    memcpy(output->data + output->len, data, total);
    output->len += total;
    output->data[output->len] = '\0';
    return count;
}

GrayString gray_fmt_out_finish(GrayArena *arena, GrayFmtOut *output) {
    GrayString result = gray_string_new(arena, output->data ? output->data : "", (int32_t)output->len);
    free(output->data);
    output->data = NULL;
    output->len = output->capacity = 0;
    return result;
}

/* --- to_char / char_count — Unicode codepoint access --- */

int32_t gray_builtin_to_char(GrayString string, int64_t index, const char *file, int line) {
    if (index < 0) {
        gray_panic_code("P0049", "to_char() index out of bounds; index %lld is negative", (long long)index);
    }
    const uint8_t *cursor = (const uint8_t *)string.data;
    const uint8_t *end_cursor = cursor + string.len;
    int64_t codepoint_index = 0;
    while (cursor < end_cursor) {
        int32_t codepoint;
        int bytes = gray_builtin_utf8_next(cursor, end_cursor, &codepoint);
        if (codepoint_index == index) return codepoint;
        cursor += bytes;
        codepoint_index++;
    }
    gray_panic_code("P0050", "to_char() index out of bounds; index %lld but string has %lld characters",
        (long long)index, (long long)codepoint_index);
    return 0; /* unreachable */
}

int64_t gray_builtin_char_count(GrayString string) {
    const uint8_t *cursor = (const uint8_t *)string.data;
    const uint8_t *end_cursor = cursor + string.len;
    int64_t count = 0;
    while (cursor < end_cursor) {
        int32_t codepoint;
        cursor += gray_builtin_utf8_next(cursor, end_cursor, &codepoint);
        count++;
    }
    return count;
}

GrayString gray_builtin_char_to_utf8(GrayArena *arena, int32_t codepoint) {
    char buffer[4];
    int length;
    if (codepoint >= 0x110000) {
        /* Invalid codepoint — replacement character U+FFFD */
        length = codepoint_to_utf8(0xFFFD, buffer);
    } else {
        length = codepoint_to_utf8(codepoint, buffer);
    }
    return gray_string_new(arena, buffer, (int32_t)length);
}

GrayString gray_builtin_map_to_string(GrayArena *arena, GrayMap *map, int value_kind) {
    char buffer[GRAY_TO_STRING_BUFFER_SIZE];
    int position = 0;
    buffer[position++] = '{';
    bool first_entry = true;
    for (int32_t order_index = 0; order_index < map->order_len && position < GRAY_TO_STRING_SAFE_LIMIT; order_index++) {
        int32_t i = map->order[order_index];
        if (i < 0 || map->states[i] != 1) continue;
        if (!first_entry) { buffer[position++] = ','; buffer[position++] = ' '; }
        first_entry = false;
        GrayString *key_pointer = (GrayString *)((char *)map->keys + (size_t)i * map->key_size);
        position += snprintf(buffer + position, sizeof(buffer) - position, "\"%.*s\": ",
            (int)key_pointer->len, key_pointer->data ? key_pointer->data : "");
        void *value_pointer = (char *)map->values + (size_t)i * map->value_size;
        position = format_value_into(arena, buffer, sizeof(buffer), position, value_kind, value_pointer, map->value_kind);
    }
    if (map->count == 0) { buffer[position++] = ':'; }
    buffer[position++] = '}';
    buffer[position] = '\0';
    return gray_string_new(arena, buffer, (int32_t)position);
}
