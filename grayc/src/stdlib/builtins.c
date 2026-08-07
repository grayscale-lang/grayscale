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
#include "../util/constants.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>

#define GRAY_TOSTRING_BUF_SIZE    4096
#define GRAY_TOSTRING_SAFE_LIMIT  (GRAY_TOSTRING_BUF_SIZE - 96)
#define GRAY_INT_STR_BUF          32
#define GRAY_FLOAT_STR_BUF        64
#define GRAY_INPUT_BUF_SIZE       4096

/* Format a double using the shortest representation that round-trips */
static int fmt_shortest_float(char *buf, size_t buffer_size, double v) {
    int n = 0;
    for (int prec = 15; prec <= 17; prec++) {
        n = snprintf(buf, buffer_size, "%.*g", prec, v);
        double rt;
        if (sscanf(buf, "%lf", &rt) == 1 && rt == v) break;
    }
    bool has_special = false;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'i' || buf[i] == 'n') {
            has_special = true;
            break;
        }
    }
    if (!has_special && n + 2 < (int)buffer_size) {
        buf[n++] = '.';
        buf[n++] = '0';
        buf[n] = '\0';
    }
    return n;
}

/* Encode Unicode codepoint to UTF-8; returns byte count (1-4). */
static int cp_to_utf8(int32_t cp, char *out) {
    if (cp < 0x80)   { out[0] = (char)cp; return 1; }
    if (cp < 0x800)  { out[0] = (char)(0xC0|(cp>>6)); out[1] = (char)(0x80|(cp&0x3F)); return 2; }
    if (cp < 0x10000){ out[0] = (char)(0xE0|(cp>>12)); out[1] = (char)(0x80|((cp>>6)&0x3F)); out[2] = (char)(0x80|(cp&0x3F)); return 3; }
    out[0]=(char)(0xF0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&0x3F)); out[2]=(char)(0x80|((cp>>6)&0x3F)); out[3]=(char)(0x80|(cp&0x3F)); return 4;
}

/* Decode next UTF-8 character; returns bytes consumed (1-4).
   Writes decoded codepoint to *cp_out (0xFFFD on invalid input). */
static int utf8_next(const uint8_t *p, const uint8_t *end, int32_t *cp_out) {
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
static void fput_utf8(int32_t c, FILE *f) {
    char buf[4];
    int len = cp_to_utf8(c, buf);
    fwrite(buf, 1, (size_t)len, f);
}

/* --- println --- */

void gray_builtin_println_str(GrayString s) {
    fwrite(s.data, 1, (size_t)s.len, stdout);
    putchar('\n');
}

void gray_builtin_println_int(int64_t v) {
    printf("%" PRId64 "\n", v);
}

void gray_builtin_println_uint(uint64_t v) {
    printf("%" PRIu64 "\n", v);
}

void gray_builtin_println_float(double v) {
    char buf[GRAY_FLOAT_STR_BUF];
    fmt_shortest_float(buf, sizeof(buf), v);
    printf("%s\n", buf);
}

void gray_builtin_println_bool(bool v) {
    printf("%s\n", v ? "true" : "false");
}

void gray_builtin_println_char(int32_t c) {
    fput_utf8(c, stdout);
    putchar('\n');
}

void gray_builtin_println_addr(uintptr_t v) {
    printf("0x%" PRIxPTR "\n", v);
}

/* --- print --- */

void gray_builtin_print_str(GrayString s) {
    fwrite(s.data, 1, (size_t)s.len, stdout);
}

void gray_builtin_print_int(int64_t v) {
    printf("%" PRId64, v);
}

void gray_builtin_print_uint(uint64_t v) {
    printf("%" PRIu64, v);
}

void gray_builtin_print_float(double v) {
    char buf[GRAY_FLOAT_STR_BUF];
    fmt_shortest_float(buf, sizeof(buf), v);
    printf("%s", buf);
}

void gray_builtin_print_bool(bool v) {
    printf("%s", v ? "true" : "false");
}

void gray_builtin_print_char(int32_t c) {
    fput_utf8(c, stdout);
}

void gray_builtin_print_addr(uintptr_t v) {
    printf("0x%" PRIxPTR, v);
}

/* --- eprintln / eprint --- */

void gray_builtin_eprintln_str(GrayString s) {
    fwrite(s.data, 1, (size_t)s.len, stderr);
    fputc('\n', stderr);
}

void gray_builtin_eprintln_int(int64_t v) {
    fprintf(stderr, "%" PRId64 "\n", v);
}

void gray_builtin_eprintln_uint(uint64_t v) {
    fprintf(stderr, "%" PRIu64 "\n", v);
}

void gray_builtin_eprintln_char(int32_t c) {
    fput_utf8(c, stderr);
    fputc('\n', stderr);
}

void gray_builtin_eprintln_float(double v) {
    char buf[GRAY_FLOAT_STR_BUF];
    fmt_shortest_float(buf, sizeof(buf), v);
    fprintf(stderr, "%s\n", buf);
}

void gray_builtin_eprintln_bool(bool v) {
    fprintf(stderr, "%s\n", v ? "true" : "false");
}

void gray_builtin_eprintln_addr(uintptr_t v) {
    fprintf(stderr, "0x%" PRIxPTR "\n", v);
}

void gray_builtin_eprint_str(GrayString s) {
    fwrite(s.data, 1, (size_t)s.len, stderr);
}

void gray_builtin_eprint_int(int64_t v) {
    fprintf(stderr, "%" PRId64, v);
}

void gray_builtin_eprint_uint(uint64_t v) {
    fprintf(stderr, "%" PRIu64, v);
}

void gray_builtin_eprint_float(double v) {
    char buf[GRAY_FLOAT_STR_BUF];
    fmt_shortest_float(buf, sizeof(buf), v);
    fprintf(stderr, "%s", buf);
}

void gray_builtin_eprint_bool(bool v) {
    fprintf(stderr, "%s", v ? "true" : "false");
}

void gray_builtin_eprint_char(int32_t c) {
    fput_utf8(c, stderr);
}

void gray_builtin_eprint_addr(uintptr_t v) {
    fprintf(stderr, "0x%" PRIxPTR, v);
}

/* --- input --- */

GrayString gray_builtin_input(GrayArena *arena) {
    char buf[GRAY_INPUT_BUF_SIZE];
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
    (void)file; (void)line;
    if (!condition) {
        fflush(stdout);
        fprintf(stderr, "panic[P0075]: assertion failed");
        if (message.len > 0) {
            fprintf(stderr, ": ");
            fwrite(message.data, 1, (size_t)message.len, stderr);
        }
        fputc('\n', stderr);
        exit(1);
    }
}

/* --- panic --- */

void gray_builtin_panic_msg(GrayString message) {
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
    if (seconds > 0) sleep((unsigned int)seconds);
}

void gray_builtin_sleep_ms(int64_t ms) {
    if (ms < 0) gray_panic_code("P0083", "sleep duration cannot be negative (%lld)", (long long)ms);
    if (ms > 0) {
        struct timespec ts;
        ts.tv_sec = ms / MS_PER_SEC;
        ts.tv_nsec = (ms % MS_PER_SEC) * NS_PER_MS;
        nanosleep(&ts, NULL);
    }
}

void gray_builtin_sleep_ns(int64_t ns) {
    if (ns > 0) {
        struct timespec ts;
        ts.tv_sec = ns / NS_PER_SEC;
        ts.tv_nsec = ns % NS_PER_SEC;
        nanosleep(&ts, NULL);
    }
}

/* --- to_string --- */

GrayString gray_builtin_to_string_int(GrayArena *arena, int64_t v) {
    char buf[GRAY_INT_STR_BUF];
    int len = snprintf(buf, sizeof(buf), "%" PRId64, v);
    return gray_string_new(arena, buf, (int32_t)len);
}

GrayString gray_builtin_to_string_uint(GrayArena *arena, uint64_t v) {
    char buf[GRAY_INT_STR_BUF];
    int len = snprintf(buf, sizeof(buf), "%" PRIu64, v);
    return gray_string_new(arena, buf, (int32_t)len);
}

GrayString gray_builtin_to_string_float(GrayArena *arena, double v) {
    char buf[GRAY_FLOAT_STR_BUF];
    int len = fmt_shortest_float(buf, sizeof(buf), v);
    return gray_string_new(arena, buf, (int32_t)len);
}

GrayString gray_builtin_format_float(GrayArena *arena, double v) {
    return gray_builtin_to_string_float(arena, v);
}

GrayString gray_builtin_to_string_bool(GrayArena *arena, bool v) {
    return v ? gray_string_lit("true") : gray_string_lit("false");
}

/* --- from_string --- */

int64_t gray_builtin_string_to_int(GrayString s) {
    char buf[GRAY_FLOAT_STR_BUF];
    int len = s.len < (int32_t)sizeof(buf) - 1 ? s.len : (int32_t)sizeof(buf) - 1;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = '\0';
    char *end = NULL;
    int64_t result = strtoll(buf, &end, 10);
    if (end == buf || (*end != '\0' && *end != ' ')) {
        gray_panic_code("P0084", "cannot convert '%s' to int", buf);
    }
    return result;
}

double gray_builtin_string_to_float(GrayString s) {
    char buf[GRAY_FLOAT_STR_BUF];
    int len = s.len < (int32_t)sizeof(buf) - 1 ? s.len : (int32_t)sizeof(buf) - 1;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = '\0';
    char *end = NULL;
    double result = strtod(buf, &end);
    if (end == buf || (*end != '\0' && *end != ' ')) {
        gray_panic_code("P0085", "cannot convert '%s' to float", buf);
    }
    return result;
}

/* --- composite to_string --- */

GrayString gray_builtin_array_to_string(GrayArena *arena, GrayArray *arr, int elem_kind) {
    char buf[GRAY_TOSTRING_BUF_SIZE];
    int pos = 0;
    buf[pos++] = '{';
    for (int32_t i = 0; i < arr->len && pos < GRAY_TOSTRING_SAFE_LIMIT; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        switch (elem_kind) {
        case 0:
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%" PRId64,
                GRAY_ARRAY_GET(*arr, int64_t, i));
            break;
        case 1: {
            char float_buffer[GRAY_FLOAT_STR_BUF];
            fmt_shortest_float(float_buffer, sizeof(float_buffer), GRAY_ARRAY_GET(*arr, double, i));
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", float_buffer);
            break;
        }
        case 2: {
            GrayString s = GRAY_ARRAY_GET(*arr, GrayString, i);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\"%.*s\"",
                (int)s.len, s.data ? s.data : "");
            break;
        }
        case 3:
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                GRAY_ARRAY_GET(*arr, bool, i) ? "true" : "false");
            break;
        case 4:
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%" PRIu64,
                GRAY_ARRAY_GET(*arr, uint64_t, i));
            break;
        case 5:
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%u",
                (unsigned)GRAY_ARRAY_GET(*arr, uint8_t, i));
            break;
        case 6: {
            int32_t cp = GRAY_ARRAY_GET(*arr, int32_t, i);
            char utf8[4]; int utf8_length = cp_to_utf8(cp, utf8);
            if (pos + 2 + utf8_length < (int)sizeof(buf)) {
                buf[pos++] = '\'';
                memcpy(buf + pos, utf8, (size_t)utf8_length); pos += utf8_length;
                buf[pos++] = '\'';
            }
            break;
        }
        case 7:
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%d",
                GRAY_ARRAY_GET(*arr, int, i));
            break;
        }
    }
    buf[pos++] = '}';
    buf[pos] = '\0';
    return gray_string_new(arena, buf, (int32_t)pos);
}

/* --- to_char / char_count — Unicode codepoint access --- */

int32_t gray_builtin_to_char(GrayString s, int64_t index, const char *file, int line) {
    if (index < 0) {
        gray_panic_code("P0049", "to_char() index out of bounds; index %lld is negative", (long long)index);
    }
    const uint8_t *p = (const uint8_t *)s.data;
    const uint8_t *end = p + s.len;
    int64_t cp_idx = 0;
    while (p < end) {
        int32_t cp;
        int bytes = utf8_next(p, end, &cp);
        if (cp_idx == index) return cp;
        p += bytes;
        cp_idx++;
    }
    gray_panic_code("P0050", "to_char() index out of bounds; index %lld but string has %lld characters",
        (long long)index, (long long)cp_idx);
    return 0; /* unreachable */
}

int64_t gray_builtin_char_count(GrayString s) {
    const uint8_t *p = (const uint8_t *)s.data;
    const uint8_t *end = p + s.len;
    int64_t count = 0;
    while (p < end) {
        int32_t cp;
        p += utf8_next(p, end, &cp);
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

GrayString gray_builtin_map_to_string(GrayArena *arena, GrayMap *m, int val_kind) {
    char buf[GRAY_TOSTRING_BUF_SIZE];
    int pos = 0;
    buf[pos++] = '{';
    for (int32_t order_index = 0; order_index < m->order_len && pos < GRAY_TOSTRING_SAFE_LIMIT; order_index++) {
        int32_t i = m->order[order_index];
        if (m->states[i] != 1) continue;
        if (order_index > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        GrayString *kp = (GrayString *)((char *)m->keys + (size_t)i * m->key_size);
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"%.*s\": ",
            (int)kp->len, kp->data ? kp->data : "");
        void *vp = (char *)m->values + (size_t)i * m->value_size;
        switch (val_kind) {
        case 0: pos += snprintf(buf + pos, sizeof(buf) - pos, "%" PRId64, *(int64_t *)vp); break;
        case 1: {
            char float_buffer[GRAY_FLOAT_STR_BUF];
            fmt_shortest_float(float_buffer, sizeof(float_buffer), *(double *)vp);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", float_buffer);
            break;
        }
        case 2: {
            GrayString *sp = (GrayString *)vp;
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\"%.*s\"",
                (int)sp->len, sp->data ? sp->data : "");
            break;
        }
        case 3: pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
            *(bool *)vp ? "true" : "false"); break;
        case 4: pos += snprintf(buf + pos, sizeof(buf) - pos, "%" PRIu64, *(uint64_t *)vp); break;
        case 5: pos += snprintf(buf + pos, sizeof(buf) - pos, "%u", (unsigned)*(uint8_t *)vp); break;
        case 6: {
            int32_t cp = *(int32_t *)vp;
            char utf8[4]; int utf8_length = cp_to_utf8(cp, utf8);
            if (pos + 2 + utf8_length < (int)sizeof(buf)) {
                buf[pos++] = '\'';
                memcpy(buf + pos, utf8, (size_t)utf8_length); pos += utf8_length;
                buf[pos++] = '\'';
            }
            break;
        }
        case 7: pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", *(int *)vp); break;
        }
    }
    if (m->order_len == 0) { buf[pos++] = ':'; }
    buf[pos++] = '}';
    buf[pos] = '\0';
    return gray_string_new(arena, buf, (int32_t)pos);
}
