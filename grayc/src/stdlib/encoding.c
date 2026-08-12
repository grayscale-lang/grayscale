/*
 * encoding.c — Implementation of the encoding stdlib module.
 * Provides base64, hex, and URL encode/decode transformations on
 * Grayscale strings.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "encoding.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

GrayString gray_encoding_base64_encode(GrayArena *arena, GrayString s) {
    int32_t output_length = ((s.len + 2) / 3) * 4;
    char *out = gray_arena_alloc(arena, (size_t)output_length + 1);
    int j = 0;
    for (int i = 0; i < s.len; i += 3) {
        uint32_t a = (uint8_t)s.data[i];
        uint32_t b = (i + 1 < s.len) ? (uint8_t)s.data[i + 1] : 0;
        uint32_t c = (i + 2 < s.len) ? (uint8_t)s.data[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < s.len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < s.len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    GrayString r = { out, (int32_t)j };
    return r;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

GrayString gray_encoding_base64_decode(GrayArena *arena, GrayString s) {
    if (s.len == 0) return gray_string_lit("");
    if (s.len % 4 != 0) {
        gray_panic_code("P0036",
            "encoding.base64_decode: input length %d is not a multiple of 4",
            s.len);
    }

    /* Padding is only valid in the last quad: 0, 1, or 2 '=' at the end. */
    int32_t pad = 0;
    if (s.data[s.len - 1] == '=') pad++;
    if (s.len >= 2 && s.data[s.len - 2] == '=') pad++;

    int32_t output_length = (s.len / 4) * 3 - pad;
    char *out = gray_arena_alloc(arena, (size_t)output_length + 1);
    int32_t j = 0;

    for (int32_t i = 0; i < s.len; i += 4) {
        int last_quad = (i + 4 == s.len);
        char c_ch = s.data[i + 2];
        char d_ch = s.data[i + 3];
        int c_pad = (c_ch == '=');
        int d_pad = (d_ch == '=');

        if ((c_pad || d_pad) && !last_quad) {
            gray_panic_code("P0037",
                "encoding.base64_decode: padding character '=' before end of input");
        }
        if (c_pad && !d_pad) {
            gray_panic_code("P0038", "encoding.base64_decode: invalid padding");
        }

        int a = b64_val(s.data[i]);
        int b = b64_val(s.data[i + 1]);
        int c = c_pad ? 0 : b64_val(c_ch);
        int d = d_pad ? 0 : b64_val(d_ch);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            gray_panic_code("P0039", "encoding.base64_decode: invalid character in input");
        }

        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                          ((uint32_t)c << 6)  | (uint32_t)d;
        out[j++] = (char)((triple >> 16) & 0xFF);
        if (!c_pad) out[j++] = (char)((triple >> 8) & 0xFF);
        if (!d_pad) out[j++] = (char)(triple & 0xFF);
    }
    out[j] = '\0';
    GrayString r = { out, j };
    return r;
}

GrayString gray_encoding_hex_encode(GrayArena *arena, GrayString s) {
    int32_t output_length = s.len * 2;
    char *out = gray_arena_alloc(arena, (size_t)output_length + 1);
    for (int i = 0; i < s.len; i++) {
        snprintf(out + i * 2, 3, "%02x", (uint8_t)s.data[i]);
    }
    out[output_length] = '\0';
    GrayString r = { out, output_length };
    return r;
}

GrayString gray_encoding_hex_decode(GrayArena *arena, GrayString s) {
    if (s.len % 2 != 0) {
        gray_panic_code("P0040", "encoding.hex_decode: input length %d is not even", s.len);
    }
    int32_t output_length = s.len / 2;
    char *out = gray_arena_alloc(arena, (size_t)output_length + 1);
    for (int i = 0; i < output_length; i++) {
        unsigned char hi = (unsigned char)s.data[i * 2];
        unsigned char lo = (unsigned char)s.data[i * 2 + 1];
        if (!isxdigit(hi) || !isxdigit(lo)) {
            gray_panic_code("P0041", "encoding.hex_decode: invalid hex character at position %d", i * 2);
        }
        int hi_v = (hi <= '9') ? hi - '0' : (hi <= 'F') ? hi - 'A' + 10 : hi - 'a' + 10;
        int lo_v = (lo <= '9') ? lo - '0' : (lo <= 'F') ? lo - 'A' + 10 : lo - 'a' + 10;
        out[i] = (char)((hi_v << 4) | lo_v);
    }
    out[output_length] = '\0';
    GrayString r = { out, output_length };
    return r;
}

GrayString gray_encoding_url_encode(GrayArena *arena, GrayString s) {
    /* Worst case: every char becomes %XX (3x) */
    char *out = gray_arena_alloc(arena, (size_t)s.len * 3 + 1);
    int j = 0;
    for (int i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = c;
        } else {
            j += snprintf(out + j, 4, "%%%02X", (uint8_t)c);
        }
    }
    out[j] = '\0';
    GrayString r = { out, (int32_t)j };
    return r;
}

GrayString gray_encoding_url_decode(GrayArena *arena, GrayString s) {
    char *out = gray_arena_alloc(arena, (size_t)s.len + 1);
    int j = 0;
    for (int i = 0; i < s.len; i++) {
        if (s.data[i] == '%' && i + 2 < s.len) {
            unsigned char hi = (unsigned char)s.data[i + 1];
            unsigned char lo = (unsigned char)s.data[i + 2];
            if (!isxdigit(hi) || !isxdigit(lo)) {
                gray_panic_code("P0042", "encoding.url_decode: invalid percent-escape at position %d", i);
            }
            int hi_v = (hi <= '9') ? hi - '0' : (hi <= 'F') ? hi - 'A' + 10 : hi - 'a' + 10;
            int lo_v = (lo <= '9') ? lo - '0' : (lo <= 'F') ? lo - 'A' + 10 : lo - 'a' + 10;
            out[j++] = (char)((hi_v << 4) | lo_v);
            i += 2;
        } else if (s.data[i] == '+') {
            out[j++] = ' ';
        } else {
            out[j++] = s.data[i];
        }
    }
    out[j] = '\0';
    GrayString r = { out, (int32_t)j };
    return r;
}

/* --- Byte conversion functions (formerly @bytes module) --- */

GrayArray gray_encoding_from_string(GrayArena *arena, GrayString s) {
    GrayArray arr = gray_array_new(arena, sizeof(uint8_t), s.len);
    for (int32_t i = 0; i < s.len; i++) {
        uint8_t b = (uint8_t)s.data[i];
        GRAY_ARRAY_PUSH(arena, &arr, &b);
    }
    return arr;
}

GrayString gray_encoding_to_string(GrayArena *arena, GrayArray *bytes) {
    return gray_string_new(arena, (const char *)bytes->data, bytes->len);
}

GrayArray gray_encoding_from_hex(GrayArena *arena, GrayString hex) {
    int32_t output_length = hex.len / 2;
    GrayArray arr = gray_array_new(arena, sizeof(uint8_t), output_length);
    for (int32_t i = 0; i < output_length; i++) {
        unsigned int byte;
        sscanf(hex.data + i * 2, "%02x", &byte);
        uint8_t b = (uint8_t)byte;
        GRAY_ARRAY_PUSH(arena, &arr, &b);
    }
    return arr;
}

GrayString gray_encoding_to_hex(GrayArena *arena, GrayArray *bytes) {
    int32_t output_length = bytes->len * 2;
    char *hex = gray_arena_alloc(arena, (size_t)output_length + 1);
    uint8_t *data = (uint8_t *)bytes->data;
    for (int32_t i = 0; i < bytes->len; i++) {
        snprintf(hex + i * 2, 3, "%02x", data[i]);
    }
    hex[output_length] = '\0';
    GrayString r = { hex, output_length };
    return r;
}

GrayArray gray_encoding_from_base64(GrayArena *arena, GrayString b64) {
    GrayString decoded = gray_encoding_base64_decode(arena, b64);
    return gray_encoding_from_string(arena, decoded);
}

GrayString gray_encoding_to_base64(GrayArena *arena, GrayArray *bytes) {
    GrayString s = gray_encoding_to_string(arena, bytes);
    return gray_encoding_base64_encode(arena, s);
}
