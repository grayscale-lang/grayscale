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
#include "../runtime/platform_rt.h"
#include "../util/constants.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

#if GRAY_RT_WINDOWS
#include "../runtime/win32.h"
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#define GRAY_TOSTRING_BUF_SIZE    4096
#define GRAY_TOSTRING_SAFE_LIMIT  (GRAY_TOSTRING_BUF_SIZE - 96)
#define GRAY_FLOAT_STR_BUF        64
#define GRAY_INPUT_BUF_SIZE       4096

/* Encode Unicode codepoint to UTF-8; returns byte count (1-4). */
static int cp_to_utf8(int32_t cp, char *out) {
    if (cp < 0x80)   { out[0] = (char)cp; return 1; }
    if (cp < 0x800)  { out[0] = (char)(0xC0|(cp>>6)); out[1] = (char)(0x80|(cp&0x3F)); return 2; }
    if (cp < 0x10000){ out[0] = (char)(0xE0|(cp>>12)); out[1] = (char)(0x80|((cp>>6)&0x3F)); out[2] = (char)(0x80|(cp&0x3F)); return 3; }
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F)); out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}

/* Decode next UTF-8 character; returns bytes consumed (1-4).
   Writes decoded codepoint to *cp_out (0xFFFD on invalid input). */
int gray_builtin_utf8_next(const uint8_t *p, const uint8_t *end, int32_t *cp_out) {
    uint8_t b = *p;
    int32_t cp;
    int bytes;
    if (b < 0x80) {
        *cp_out = b; return 1;
    } else if ((b & 0xE0) == 0xC0) {
        cp = b & 0x1F; bytes = 2;
    } else if ((b & 0xF0) == 0xE0) {
        cp = b & 0x0F; bytes = 3;
    } else if ((b & 0xF8) == 0xF0) {
        cp = b & 0x07; bytes = 4;
    } else {
        *cp_out = 0xFFFD; return 1;
    }
    if (p + bytes > end) { *cp_out = 0xFFFD; return 1; }
    for (int i = 1; i < bytes; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp_out = 0xFFFD; return 1; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *cp_out = cp;
    return bytes;
}

/* Write a Unicode code point as UTF-8 to a FILE stream */
static void fput_utf8(int32_t codepoint, FILE *stream) {
    char buf[4];
    int len = cp_to_utf8(codepoint, buf);
    fwrite(buf, 1, (size_t)len, stream);
}

/* --- Core print helpers (one per type) --- */

static void print_core_str(GrayString str, FILE *stream, bool newline) {
    fwrite(str.data, 1, (size_t)str.len, stream);
    if (newline) fputc('\n', stream);
}

static void print_core_int(int64_t value, FILE *stream, bool newline) {
    fprintf(stream, "%" PRId64, value);
    if (newline) fputc('\n', stream);
}

static void print_core_uint(uint64_t value, FILE *stream, bool newline) {
    fprintf(stream, "%" PRIu64, value);
    if (newline) fputc('\n', stream);
}

static void print_core_float(double value, int bit_size, FILE *stream, bool newline) {
    char buf[GRAY_FLOAT_STR_BUF];
    gray_fmt_shortest_float(buf, sizeof(buf), value, bit_size);
    fprintf(stream, "%s", buf);
    if (newline) fputc('\n', stream);
}

static void print_core_bool(bool value, FILE *stream, bool newline) {
    fprintf(stream, "%s", value ? "true" : "false");
    if (newline) fputc('\n', stream);
}

static void print_core_char(int32_t codepoint, FILE *stream, bool newline) {
    fput_utf8(codepoint, stream);
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
PRINT_FAMILY(int,   int64_t)
PRINT_FAMILY(uint,  uint64_t)
PRINT_FAMILY(bool,  bool)
PRINT_FAMILY(char,  int32_t)
PRINT_FAMILY(addr,  uintptr_t)

/* A float also carries its bit size (32 or 64) so it prints at its own
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
    char buf[GRAY_INPUT_BUF_SIZE];
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return gray_string_lit("");
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        len--;
    } else if (len == GRAY_INPUT_BUF_SIZE - 1) {
        /* Buffer filled without reaching a newline — drain the rest of the
         * line so the next input() call reads the correct line. */
        int c;
        while ((c = getc(stdin)) != '\n' && c != EOF)
            ;
    }
    return gray_string_new(arena, buf, (int32_t)len);
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
#if GRAY_RT_WINDOWS
    if (seconds > 0) Sleep((DWORD)(seconds * MS_PER_SEC));
#else
    if (seconds > 0) sleep((unsigned int)seconds);
#endif
}

void gray_builtin_sleep_ms(int64_t ms) {
    if (ms < 0) gray_panic_code("P0083", "sleep duration cannot be negative (%lld)", (long long)ms);
#if GRAY_RT_WINDOWS
    if (ms > 0) Sleep((DWORD)ms);
#else
    if (ms > 0) {
        struct timespec ts;
        ts.tv_sec = ms / MS_PER_SEC;
        ts.tv_nsec = (ms % MS_PER_SEC) * NS_PER_MS;
        nanosleep(&ts, NULL);
    }
#endif
}

void gray_builtin_sleep_ns(int64_t ns) {
#if GRAY_RT_WINDOWS
    /* Sleep() has millisecond granularity; round up so we never sleep 0. */
    if (ns > 0) Sleep((DWORD)((ns + NS_PER_MS - 1) / NS_PER_MS));
#else
    if (ns > 0) {
        struct timespec ts;
        ts.tv_sec = ns / NS_PER_SEC;
        ts.tv_nsec = ns % NS_PER_SEC;
        nanosleep(&ts, NULL);
    }
#endif
}

/* --- system --- */

int64_t gray_builtin_system(GrayString cmd) {
    char *cstr = malloc((size_t)cmd.len + 1);
    if (!cstr) return -1;
    memcpy(cstr, cmd.data, (size_t)cmd.len);
    cstr[cmd.len] = '\0';
    /* Flush our buffered output so it lands before the child's, which inherits
     * the same stdout fd. */
    fflush(stdout);
    fflush(stderr);
    int status = system(cstr);
    free(cstr);
#if GRAY_RT_WINDOWS
    return (int64_t)status;
#else
    if (WIFEXITED(status)) return (int64_t)WEXITSTATUS(status);
    return -1;
#endif
}

/* --- to_string --- */

GrayString gray_builtin_to_string_int(GrayArena *arena, int64_t value) {
    return gray_strconv_from_int(arena, value);
}

GrayString gray_builtin_to_string_uint(GrayArena *arena, uint64_t value) {
    return gray_strconv_from_uint(arena, value);
}

GrayString gray_builtin_to_string_float(GrayArena *arena, double value, int bit_size) {
    char buf[GRAY_FLOAT_STR_BUF];
    int len = gray_fmt_shortest_float(buf, sizeof(buf), value, bit_size);
    return gray_string_new(arena, buf, len);
}

GrayString gray_builtin_format_float(GrayArena *arena, double value, int bit_size) {
    return gray_builtin_to_string_float(arena, value, bit_size);
}

GrayString gray_builtin_to_string_bool(GrayArena *arena, bool value) {
    return value ? gray_string_lit("true") : gray_string_lit("false");
}

/* --- from_string --- */

int64_t gray_builtin_string_to_int(GrayString str) {
    char buf[GRAY_FLOAT_STR_BUF];
    int len = str.len < (int32_t)sizeof(buf) - 1 ? str.len : (int32_t)sizeof(buf) - 1;
    memcpy(buf, str.data, (size_t)len);
    buf[len] = '\0';
    char *end = NULL;
    int64_t result = strtoll(buf, &end, 10);
    if (end == buf || (*end != '\0' && *end != ' ')) {
        gray_panic_code("P0084", "cannot convert '%s' to int", buf);
    }
    return result;
}

double gray_builtin_string_to_float(GrayString str) {
    char buf[GRAY_FLOAT_STR_BUF];
    int len = str.len < (int32_t)sizeof(buf) - 1 ? str.len : (int32_t)sizeof(buf) - 1;
    memcpy(buf, str.data, (size_t)len);
    buf[len] = '\0';
    char *end = NULL;
    double result = strtod(buf, &end);
    if (end == buf || (*end != '\0' && *end != ' ')) {
        gray_panic_code("P0085", "cannot convert '%s' to float", buf);
    }
    return result;
}

/* --- composite to_string --- */

/* An integer element of `size` bytes, widened to 64 bits. The container
 * records the width its elements were stored at, so a [i32] or [u8] is read
 * back at that width instead of as consecutive int64 values. */
static int64_t element_as_signed(const void *p, int32_t size) {
    switch (size) {
    case 1: return *(const int8_t *)p;
    case 2: return *(const int16_t *)p;
    case 4: return *(const int32_t *)p;
    default: return *(const int64_t *)p;
    }
}

static uint64_t element_as_unsigned(const void *p, int32_t size) {
    switch (size) {
    case 1: return *(const uint8_t *)p;
    case 2: return *(const uint16_t *)p;
    case 4: return *(const uint32_t *)p;
    default: return *(const uint64_t *)p;
    }
}

/* A float element in its shortest round-trip form at its stored width: a
 * 4-byte f32 or an 8-byte double. */
static void format_float_element(char *out, size_t out_size, const void *p, int32_t size) {
    double value = size == 4 ? (double)*(const float *)p : *(const double *)p;
    gray_fmt_shortest_float(out, out_size, value, size * 8);
}

/* Append one element/value of the given kind (see the to_string callers) at
 * buf[pos]; returns the new pos. */
static int format_value_into(char *buf, size_t buf_size, int pos, int kind,
                             const void *value_ptr, int32_t value_size) {
    switch (kind) {
    case 0:
        pos += snprintf(buf + pos, buf_size - pos, "%" PRId64, element_as_signed(value_ptr, value_size));
        break;
    case 1: {
        char float_buffer[GRAY_FLOAT_STR_BUF];
        format_float_element(float_buffer, sizeof(float_buffer), value_ptr, value_size);
        pos += snprintf(buf + pos, buf_size - pos, "%s", float_buffer);
        break;
    }
    case 2: {
        const GrayString *element = (const GrayString *)value_ptr;
        pos += snprintf(buf + pos, buf_size - pos, "\"%.*s\"",
            (int)element->len, element->data ? element->data : "");
        break;
    }
    case 3:
        pos += snprintf(buf + pos, buf_size - pos, "%s", *(const bool *)value_ptr ? "true" : "false");
        break;
    case 4:
        pos += snprintf(buf + pos, buf_size - pos, "%" PRIu64, element_as_unsigned(value_ptr, value_size));
        break;
    case 5:
        pos += snprintf(buf + pos, buf_size - pos, "%u", (unsigned)*(const uint8_t *)value_ptr);
        break;
    case 6: {
        int32_t cp = *(const int32_t *)value_ptr;
        char utf8[4]; int utf8_length = cp_to_utf8(cp, utf8);
        if (pos + 2 + utf8_length < (int)buf_size) {
            buf[pos++] = '\'';
            memcpy(buf + pos, utf8, (size_t)utf8_length); pos += utf8_length;
            buf[pos++] = '\'';
        }
        break;
    }
    case 7:
        pos += snprintf(buf + pos, buf_size - pos, "%d", *(const int *)value_ptr);
        break;
    }
    return pos;
}

GrayString gray_builtin_array_to_string(GrayArena *arena, GrayArray *arr, int elem_kind) {
    char buf[GRAY_TOSTRING_BUF_SIZE];
    int pos = 0;
    buf[pos++] = '{';
    for (int32_t i = 0; i < arr->len && pos < GRAY_TOSTRING_SAFE_LIMIT; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        pos = format_value_into(buf, sizeof(buf), pos, elem_kind,
            (char *)arr->data + (size_t)i * (size_t)arr->elem_size, arr->elem_size);
    }
    buf[pos++] = '}';
    buf[pos] = '\0';
    return gray_string_new(arena, buf, (int32_t)pos);
}

/* --- GrayFmtOut: the in-memory stream generated print code can target --- */

static void fmt_out_reserve(GrayFmtOut *out, size_t extra) {
    if (out->len + extra + 1 <= out->cap) return;
    size_t cap = out->cap ? out->cap * 2 : 128;
    while (cap < out->len + extra + 1) cap *= 2;
    out->data = realloc(out->data, cap);
    if (!out->data) {
        fprintf(stderr, "grayc: out of memory\n");
        exit(1);
    }
    out->cap = cap;
}

int gray_fmt_out_printf(GrayFmtOut *out, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (needed < 0) return needed;

    fmt_out_reserve(out, (size_t)needed);
    va_start(args, format);
    vsnprintf(out->data + out->len, (size_t)needed + 1, format, args);
    va_end(args);
    out->len += (size_t)needed;
    return needed;
}

size_t gray_fmt_out_write(const void *data, size_t size, size_t count, GrayFmtOut *out) {
    size_t total = size * count;
    fmt_out_reserve(out, total);
    memcpy(out->data + out->len, data, total);
    out->len += total;
    out->data[out->len] = '\0';
    return count;
}

GrayString gray_fmt_out_finish(GrayArena *arena, GrayFmtOut *out) {
    GrayString result = gray_string_new(arena, out->data ? out->data : "", (int32_t)out->len);
    free(out->data);
    out->data = NULL;
    out->len = out->cap = 0;
    return result;
}

/* --- to_char / char_count — Unicode codepoint access --- */

int32_t gray_builtin_to_char(GrayString str, int64_t index, const char *file, int line) {
    if (index < 0) {
        gray_panic_code("P0049", "to_char() index out of bounds; index %lld is negative", (long long)index);
    }
    const uint8_t *p = (const uint8_t *)str.data;
    const uint8_t *end = p + str.len;
    int64_t cp_idx = 0;
    while (p < end) {
        int32_t cp;
        int bytes = gray_builtin_utf8_next(p, end, &cp);
        if (cp_idx == index) return cp;
        p += bytes;
        cp_idx++;
    }
    gray_panic_code("P0050", "to_char() index out of bounds; index %lld but string has %lld characters",
        (long long)index, (long long)cp_idx);
    return 0; /* unreachable */
}

int64_t gray_builtin_char_count(GrayString str) {
    const uint8_t *p = (const uint8_t *)str.data;
    const uint8_t *end = p + str.len;
    int64_t count = 0;
    while (p < end) {
        int32_t cp;
        p += gray_builtin_utf8_next(p, end, &cp);
        count++;
    }
    return count;
}

GrayString gray_builtin_char_to_utf8(GrayArena *arena, int32_t cp) {
    char buf[4];
    int len;
    if (cp >= 0x110000) {
        /* Invalid codepoint — replacement character U+FFFD */
        len = cp_to_utf8(0xFFFD, buf);
    } else {
        len = cp_to_utf8(cp, buf);
    }
    return gray_string_new(arena, buf, (int32_t)len);
}

GrayString gray_builtin_map_to_string(GrayArena *arena, GrayMap *map, int val_kind) {
    char buf[GRAY_TOSTRING_BUF_SIZE];
    int pos = 0;
    buf[pos++] = '{';
    bool first_entry = true;
    for (int32_t order_index = 0; order_index < map->order_len && pos < GRAY_TOSTRING_SAFE_LIMIT; order_index++) {
        int32_t i = map->order[order_index];
        if (i < 0 || map->states[i] != 1) continue;
        if (!first_entry) { buf[pos++] = ','; buf[pos++] = ' '; }
        first_entry = false;
        GrayString *kp = (GrayString *)((char *)map->keys + (size_t)i * map->key_size);
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"%.*s\": ",
            (int)kp->len, kp->data ? kp->data : "");
        void *vp = (char *)map->values + (size_t)i * map->value_size;
        pos = format_value_into(buf, sizeof(buf), pos, val_kind, vp, (int32_t)map->value_size);
    }
    if (map->count == 0) { buf[pos++] = ':'; }
    buf[pos++] = '}';
    buf[pos] = '\0';
    return gray_string_new(arena, buf, (int32_t)pos);
}
