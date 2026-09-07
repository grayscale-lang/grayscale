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

GrayString gray_encoding_base64_encode(GrayArena *arena, GrayString str) {
    int32_t output_length = ((str.len + 2) / 3) * 4;
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    int j = 0;
    for (int i = 0; i < str.len; i += 3) {
        uint32_t a = (uint8_t)str.data[i];
        uint32_t b = (i + 1 < str.len) ? (uint8_t)str.data[i + 1] : 0;
        uint32_t c = (i + 2 < str.len) ? (uint8_t)str.data[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < str.len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < str.len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    GrayString result = { out, (int32_t)j };
    return result;
}

static int b64_val(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

GrayString gray_encoding_base64_decode(GrayArena *arena, GrayString str) {
    if (str.len == 0) return gray_string_lit("");
    if (str.len % 4 != 0) {
        gray_panic_code("P0036",
            "encoding.base64_decode: input length %d is not a multiple of 4",
            str.len);
    }

    /* Padding is only valid in the last quad: 0, 1, or 2 '=' at the end. */
    int32_t pad = 0;
    if (str.data[str.len - 1] == '=') pad++;
    if (str.len >= 2 && str.data[str.len - 2] == '=') pad++;

    int32_t output_length = (str.len / 4) * 3 - pad;
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    int32_t j = 0;

    for (int32_t i = 0; i < str.len; i += 4) {
        int last_quad = (i + 4 == str.len);
        char c_ch = str.data[i + 2];
        char d_ch = str.data[i + 3];
        int c_pad = (c_ch == '=');
        int d_pad = (d_ch == '=');

        if ((c_pad || d_pad) && !last_quad) {
            gray_panic_code("P0037",
                "encoding.base64_decode: padding character '=' before end of input");
        }
        if (c_pad && !d_pad) {
            gray_panic_code("P0038", "encoding.base64_decode: invalid padding");
        }

        int a = b64_val(str.data[i]);
        int b = b64_val(str.data[i + 1]);
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
    GrayString result = { out, j };
    return result;
}

GrayString gray_encoding_hex_encode(GrayArena *arena, GrayString str) {
    int32_t output_length = str.len * 2;
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    for (int i = 0; i < str.len; i++) {
        snprintf(out + i * 2, 3, "%02x", (uint8_t)str.data[i]);
    }
    out[output_length] = '\0';
    GrayString result = { out, output_length };
    return result;
}

GrayString gray_encoding_hex_decode(GrayArena *arena, GrayString str) {
    if (str.len % 2 != 0) {
        gray_panic_code("P0040", "encoding.hex_decode: input length %d is not even", str.len);
    }
    int32_t output_length = str.len / 2;
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    for (int i = 0; i < output_length; i++) {
        unsigned char hi = (unsigned char)str.data[i * 2];
        unsigned char lo = (unsigned char)str.data[i * 2 + 1];
        if (!isxdigit(hi) || !isxdigit(lo)) {
            gray_panic_code("P0041", "encoding.hex_decode: invalid hex character at position %d", i * 2);
        }
        int hi_v = (hi <= '9') ? hi - '0' : (hi <= 'F') ? hi - 'A' + 10 : hi - 'a' + 10;
        int lo_v = (lo <= '9') ? lo - '0' : (lo <= 'F') ? lo - 'A' + 10 : lo - 'a' + 10;
        out[i] = (char)((hi_v << 4) | lo_v);
    }
    out[output_length] = '\0';
    GrayString result = { out, output_length };
    return result;
}

GrayString gray_encoding_url_encode(GrayArena *arena, GrayString str) {
    /* Worst case: every char becomes %XX (3x) */
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)str.len * 3 + 1);
    int j = 0;
    for (int i = 0; i < str.len; i++) {
        unsigned char c = (unsigned char)str.data[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = c;
        } else {
            j += snprintf(out + j, 4, "%%%02X", (uint8_t)c);
        }
    }
    out[j] = '\0';
    GrayString result = { out, (int32_t)j };
    return result;
}

GrayString gray_encoding_url_decode(GrayArena *arena, GrayString str) {
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    int j = 0;
    for (int i = 0; i < str.len; i++) {
        if (str.data[i] == '%' && i + 2 < str.len) {
            unsigned char hi = (unsigned char)str.data[i + 1];
            unsigned char lo = (unsigned char)str.data[i + 2];
            if (!isxdigit(hi) || !isxdigit(lo)) {
                gray_panic_code("P0042", "encoding.url_decode: invalid percent-escape at position %d", i);
            }
            int hi_v = (hi <= '9') ? hi - '0' : (hi <= 'F') ? hi - 'A' + 10 : hi - 'a' + 10;
            int lo_v = (lo <= '9') ? lo - '0' : (lo <= 'F') ? lo - 'A' + 10 : lo - 'a' + 10;
            out[j++] = (char)((hi_v << 4) | lo_v);
            i += 2;
        } else if (str.data[i] == '+') {
            out[j++] = ' ';
        } else {
            out[j++] = str.data[i];
        }
    }
    out[j] = '\0';
    GrayString result = { out, (int32_t)j };
    return result;
}

GrayString gray_encoding_base64_url_encode(GrayArena *arena, GrayString str) {
    GrayString std = gray_encoding_base64_encode(arena, str);
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)std.len + 1);
    int32_t j = 0;
    for (int32_t i = 0; i < std.len; i++) {
        char c = std.data[i];
        if (c == '=') break;
        out[j++] = (c == '+') ? '-' : (c == '/') ? '_' : c;
    }
    out[j] = '\0';
    GrayString result = { out, j };
    return result;
}

GrayString gray_encoding_base64_url_decode(GrayArena *arena, GrayString str) {
    int32_t pad = (4 - (str.len % 4)) % 4;
    int32_t buf_len = str.len + pad;
    char *buf = gray_arena_alloc_uninitialized(arena, (size_t)buf_len + 1);
    for (int32_t i = 0; i < str.len; i++) {
        char c = str.data[i];
        if (c == '-') buf[i] = '+';
        else if (c == '_') buf[i] = '/';
        else buf[i] = c;
    }
    for (int32_t i = 0; i < pad; i++) buf[str.len + i] = '=';
    buf[buf_len] = '\0';
    GrayString std = { buf, buf_len };
    return gray_encoding_base64_decode(arena, std);
}

GrayString gray_encoding_html_escape(GrayArena *arena, GrayString str) {
    /* Worst case is "&quot;" / "&#39;" — 6 bytes per input byte. */
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)str.len * 6 + 1);
    int32_t j = 0;
    for (int32_t i = 0; i < str.len; i++) {
        switch (str.data[i]) {
        case '&':  memcpy(out + j, "&amp;",  5); j += 5; break;
        case '<':  memcpy(out + j, "&lt;",   4); j += 4; break;
        case '>':  memcpy(out + j, "&gt;",   4); j += 4; break;
        case '"':  memcpy(out + j, "&quot;", 6); j += 6; break;
        case '\'': memcpy(out + j, "&#39;",  5); j += 5; break;
        default:   out[j++] = str.data[i]; break;
        }
    }
    out[j] = '\0';
    GrayString result = { out, j };
    return result;
}

/* Encode a Unicode codepoint as UTF-8 into out (up to 4 bytes). Returns the
 * byte count, or 0 for an out-of-range codepoint. */
static int32_t encoding_utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

GrayString gray_encoding_html_unescape(GrayArena *arena, GrayString str) {
    /* Output is never longer than the input. */
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)str.len + 1);
    int32_t j = 0;
    int32_t i = 0;
    while (i < str.len) {
        if (str.data[i] != '&') { out[j++] = str.data[i++]; continue; }

        /* Locate the terminating ';' within a bounded window. */
        int32_t semi = -1;
        for (int32_t k = i + 1; k < str.len && k - i <= 10; k++) {
            if (str.data[k] == ';') { semi = k; break; }
        }
        if (semi < 0) { out[j++] = str.data[i++]; continue; }

        const char *body = str.data + i + 1;
        int32_t blen = semi - i - 1;
        int32_t consumed = 0;

        if (blen == 3 && memcmp(body, "amp", 3) == 0)       { out[j++] = '&';  consumed = 1; }
        else if (blen == 2 && memcmp(body, "lt", 2) == 0)   { out[j++] = '<';  consumed = 1; }
        else if (blen == 2 && memcmp(body, "gt", 2) == 0)   { out[j++] = '>';  consumed = 1; }
        else if (blen == 4 && memcmp(body, "quot", 4) == 0) { out[j++] = '"';  consumed = 1; }
        else if (blen == 4 && memcmp(body, "apos", 4) == 0) { out[j++] = '\''; consumed = 1; }
        else if (blen >= 2 && body[0] == '#') {
            uint32_t cp = 0;
            int ok = 1;
            if (body[1] == 'x' || body[1] == 'X') {
                if (blen < 3) ok = 0;
                for (int32_t k = 2; k < blen && ok; k++) {
                    char d = body[k];
                    cp *= 16;
                    if (d >= '0' && d <= '9') cp += (uint32_t)(d - '0');
                    else if (d >= 'a' && d <= 'f') cp += (uint32_t)(d - 'a' + 10);
                    else if (d >= 'A' && d <= 'F') cp += (uint32_t)(d - 'A' + 10);
                    else ok = 0;
                    if (cp > 0x10FFFF) ok = 0;
                }
            } else {
                for (int32_t k = 1; k < blen && ok; k++) {
                    char d = body[k];
                    if (d < '0' || d > '9') { ok = 0; break; }
                    cp = cp * 10 + (uint32_t)(d - '0');
                    if (cp > 0x10FFFF) ok = 0;
                }
            }
            if (ok) {
                int32_t n = encoding_utf8_encode(cp, out + j);
                if (n > 0) { j += n; consumed = 1; }
            }
        }

        if (consumed) {
            i = semi + 1;
        } else {
            out[j++] = str.data[i++];
        }
    }
    out[j] = '\0';
    GrayString result = { out, j };
    return result;
}

GrayString gray_encoding_shell_escape(GrayArena *arena, GrayString str) {
    if (str.len == 0) return gray_string_lit("''");

    int safe = 1;
    for (int32_t i = 0; i < str.len; i++) {
        char c = str.data[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == '/' || c == ',' ||
              c == ':' || c == '@' || c == '+' || c == '=' || c == '%')) {
            safe = 0;
            break;
        }
    }
    if (safe) return gray_string_new(arena, str.data, str.len);

    /* Wrap in single quotes; each embedded ' becomes '\'' (4 chars). */
    char *out = gray_arena_alloc_uninitialized(arena, (size_t)str.len * 4 + 3);
    int32_t j = 0;
    out[j++] = '\'';
    for (int32_t i = 0; i < str.len; i++) {
        if (str.data[i] == '\'') {
            out[j++] = '\''; out[j++] = '\\'; out[j++] = '\''; out[j++] = '\'';
        } else {
            out[j++] = str.data[i];
        }
    }
    out[j++] = '\'';
    out[j] = '\0';
    GrayString result = { out, j };
    return result;
}

/* --- Byte conversion functions (formerly @bytes module) --- */

GrayArray gray_encoding_from_string(GrayArena *arena, GrayString str) {
    GrayArray arr = gray_array_new(arena, sizeof(uint8_t), str.len);
    for (int32_t i = 0; i < str.len; i++) {
        uint8_t b = (uint8_t)str.data[i];
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
    char *hex = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    uint8_t *data = (uint8_t *)bytes->data;
    for (int32_t i = 0; i < bytes->len; i++) {
        snprintf(hex + i * 2, 3, "%02x", data[i]);
    }
    hex[output_length] = '\0';
    GrayString result = { hex, output_length };
    return result;
}

GrayArray gray_encoding_from_base64(GrayArena *arena, GrayString b64) {
    GrayString decoded = gray_encoding_base64_decode(arena, b64);
    return gray_encoding_from_string(arena, decoded);
}

GrayString gray_encoding_to_base64(GrayArena *arena, GrayArray *bytes) {
    GrayString str = gray_encoding_to_string(arena, bytes);
    return gray_encoding_base64_encode(arena, str);
}
