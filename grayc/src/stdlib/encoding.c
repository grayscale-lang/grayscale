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

GrayString gray_encoding_base64_encode(GrayArena *arena, GrayString string) {
    int32_t output_length = ((string.len + 2) / 3) * 4;
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    int j = 0;
    for (int i = 0; i < string.len; i += 3) {
        uint32_t first_byte = (uint8_t)string.data[i];
        uint32_t second_byte = (i + 1 < string.len) ? (uint8_t)string.data[i + 1] : 0;
        uint32_t third_byte = (i + 2 < string.len) ? (uint8_t)string.data[i + 2] : 0;
        uint32_t triple = (first_byte << 16) | (second_byte << 8) | third_byte;
        output[j++] = b64_table[(triple >> 18) & 0x3F];
        output[j++] = b64_table[(triple >> 12) & 0x3F];
        output[j++] = (i + 1 < string.len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        output[j++] = (i + 2 < string.len) ? b64_table[triple & 0x3F] : '=';
    }
    output[j] = '\0';
    GrayString result = { output, (int32_t)j };
    return result;
}

static int base64_value(char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

GrayString gray_encoding_base64_decode(GrayArena *arena, GrayString string) {
    if (string.len == 0) return gray_string_lit("");
    if (string.len % 4 != 0) {
        gray_panic_code("P0036",
            "encoding.base64_decode: input length %d is not a multiple of 4",
            string.len);
    }

    /* Padding is only valid in the last quad: 0, 1, or 2 '=' at the end. */
    int32_t padding = 0;
    if (string.data[string.len - 1] == '=') padding++;
    if (string.len >= 2 && string.data[string.len - 2] == '=') padding++;

    int32_t output_length = (string.len / 4) * 3 - padding;
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    int32_t j = 0;

    for (int32_t i = 0; i < string.len; i += 4) {
        int last_quad = (i + 4 == string.len);
        char third_character = string.data[i + 2];
        char fourth_character = string.data[i + 3];
        int is_third_padding = (third_character == '=');
        int is_fourth_padding = (fourth_character == '=');

        if ((is_third_padding || is_fourth_padding) && !last_quad) {
            gray_panic_code("P0037",
                "encoding.base64_decode: padding character '=' before end of input");
        }
        if (is_third_padding && !is_fourth_padding) {
            gray_panic_code("P0038", "encoding.base64_decode: invalid padding");
        }

        int first_value = base64_value(string.data[i]);
        int second_value = base64_value(string.data[i + 1]);
        int third_value = is_third_padding ? 0 : base64_value(third_character);
        int fourth_value = is_fourth_padding ? 0 : base64_value(fourth_character);
        if (first_value < 0 || second_value < 0 || third_value < 0 || fourth_value < 0) {
            gray_panic_code("P0039", "encoding.base64_decode: invalid character in input");
        }

        uint32_t triple = ((uint32_t)first_value << 18) | ((uint32_t)second_value << 12) |
                          ((uint32_t)third_value << 6)  | (uint32_t)fourth_value;
        output[j++] = (char)((triple >> 16) & 0xFF);
        if (!is_third_padding) output[j++] = (char)((triple >> 8) & 0xFF);
        if (!is_fourth_padding) output[j++] = (char)(triple & 0xFF);
    }
    output[j] = '\0';
    GrayString result = { output, j };
    return result;
}

GrayString gray_encoding_hex_encode(GrayArena *arena, GrayString string) {
    int32_t output_length = string.len * 2;
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    for (int i = 0; i < string.len; i++) {
        snprintf(output + i * 2, 3, "%02x", (uint8_t)string.data[i]);
    }
    output[output_length] = '\0';
    GrayString result = { output, output_length };
    return result;
}

GrayString gray_encoding_hex_decode(GrayArena *arena, GrayString string) {
    if (string.len % 2 != 0) {
        gray_panic_code("P0040", "encoding.hex_decode: input length %d is not even", string.len);
    }
    int32_t output_length = string.len / 2;
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    for (int i = 0; i < output_length; i++) {
        unsigned char high_nibble = (unsigned char)string.data[i * 2];
        unsigned char low_nibble = (unsigned char)string.data[i * 2 + 1];
        if (!isxdigit(high_nibble) || !isxdigit(low_nibble)) {
            gray_panic_code("P0041", "encoding.hex_decode: invalid hex character at position %d", i * 2);
        }
        int high_value = (high_nibble <= '9') ? high_nibble - '0' : (high_nibble <= 'F') ? high_nibble - 'A' + 10 : high_nibble - 'a' + 10;
        int low_value = (low_nibble <= '9') ? low_nibble - '0' : (low_nibble <= 'F') ? low_nibble - 'A' + 10 : low_nibble - 'a' + 10;
        output[i] = (char)((high_value << 4) | low_value);
    }
    output[output_length] = '\0';
    GrayString result = { output, output_length };
    return result;
}

GrayString gray_encoding_url_encode(GrayArena *arena, GrayString string) {
    /* Worst case: every char becomes %XX (3x) */
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)string.len * 3 + 1);
    int j = 0;
    for (int i = 0; i < string.len; i++) {
        unsigned char character = (unsigned char)string.data[i];
        if (isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
            output[j++] = character;
        } else {
            j += snprintf(output + j, 4, "%%%02X", (uint8_t)character);
        }
    }
    output[j] = '\0';
    GrayString result = { output, (int32_t)j };
    return result;
}

GrayString gray_encoding_url_decode(GrayArena *arena, GrayString string) {
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    int j = 0;
    for (int i = 0; i < string.len; i++) {
        if (string.data[i] == '%' && i + 2 < string.len) {
            unsigned char high_nibble = (unsigned char)string.data[i + 1];
            unsigned char low_nibble = (unsigned char)string.data[i + 2];
            if (!isxdigit(high_nibble) || !isxdigit(low_nibble)) {
                gray_panic_code("P0042", "encoding.url_decode: invalid percent-escape at position %d", i);
            }
            int high_value = (high_nibble <= '9') ? high_nibble - '0' : (high_nibble <= 'F') ? high_nibble - 'A' + 10 : high_nibble - 'a' + 10;
            int low_value = (low_nibble <= '9') ? low_nibble - '0' : (low_nibble <= 'F') ? low_nibble - 'A' + 10 : low_nibble - 'a' + 10;
            output[j++] = (char)((high_value << 4) | low_value);
            i += 2;
        } else if (string.data[i] == '+') {
            output[j++] = ' ';
        } else {
            output[j++] = string.data[i];
        }
    }
    output[j] = '\0';
    GrayString result = { output, (int32_t)j };
    return result;
}

GrayString gray_encoding_base64_url_encode(GrayArena *arena, GrayString string) {
    GrayString standard_text = gray_encoding_base64_encode(arena, string);
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)standard_text.len + 1);
    int32_t j = 0;
    for (int32_t i = 0; i < standard_text.len; i++) {
        char character = standard_text.data[i];
        if (character == '=') break;
        output[j++] = (character == '+') ? '-' : (character == '/') ? '_' : character;
    }
    output[j] = '\0';
    GrayString result = { output, j };
    return result;
}

GrayString gray_encoding_base64_url_decode(GrayArena *arena, GrayString string) {
    int32_t padding = (4 - (string.len % 4)) % 4;
    int32_t buffer_length = string.len + padding;
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)buffer_length + 1);
    for (int32_t i = 0; i < string.len; i++) {
        char character = string.data[i];
        if (character == '-') buffer[i] = '+';
        else if (character == '_') buffer[i] = '/';
        else buffer[i] = character;
    }
    for (int32_t i = 0; i < padding; i++) buffer[string.len + i] = '=';
    buffer[buffer_length] = '\0';
    GrayString standard_text = { buffer, buffer_length };
    return gray_encoding_base64_decode(arena, standard_text);
}

GrayString gray_encoding_html_escape(GrayArena *arena, GrayString string) {
    /* Worst case is "&quot;" / "&#39;" — 6 bytes per input byte. */
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)string.len * 6 + 1);
    int32_t j = 0;
    for (int32_t i = 0; i < string.len; i++) {
        switch (string.data[i]) {
        case '&':  memcpy(output + j, "&amp;",  5); j += 5; break;
        case '<':  memcpy(output + j, "&lt;",   4); j += 4; break;
        case '>':  memcpy(output + j, "&gt;",   4); j += 4; break;
        case '"':  memcpy(output + j, "&quot;", 6); j += 6; break;
        case '\'': memcpy(output + j, "&#39;",  5); j += 5; break;
        default:   output[j++] = string.data[i]; break;
        }
    }
    output[j] = '\0';
    GrayString result = { output, j };
    return result;
}

/* Encode a Unicode codepoint as UTF-8 into out (up to 4 bytes). Returns the
 * byte count, or 0 for an out-of-range codepoint. */
static int32_t encoding_utf8_encode(uint32_t codepoint, char *output) {
    if (codepoint < 0x80) {
        output[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        output[0] = (char)(0xC0 | (codepoint >> 6));
        output[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        output[0] = (char)(0xE0 | (codepoint >> 12));
        output[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        output[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        output[0] = (char)(0xF0 | (codepoint >> 18));
        output[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        output[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        output[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

GrayString gray_encoding_html_unescape(GrayArena *arena, GrayString string) {
    /* Output is never longer than the input. */
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)string.len + 1);
    int32_t j = 0;
    int32_t i = 0;
    while (i < string.len) {
        if (string.data[i] != '&') { output[j++] = string.data[i++]; continue; }

        /* Locate the terminating ';' within a bounded window. */
        int32_t semi = -1;
        for (int32_t entity_index = i + 1; entity_index < string.len && entity_index - i <= 10; entity_index++) {
            if (string.data[entity_index] == ';') { semi = entity_index; break; }
        }
        if (semi < 0) { output[j++] = string.data[i++]; continue; }

        const char *body = string.data + i + 1;
        int32_t body_length = semi - i - 1;
        int32_t consumed = 0;

        if (body_length == 3 && memcmp(body, "amp", 3) == 0)       { output[j++] = '&';  consumed = 1; }
        else if (body_length == 2 && memcmp(body, "lt", 2) == 0)   { output[j++] = '<';  consumed = 1; }
        else if (body_length == 2 && memcmp(body, "gt", 2) == 0)   { output[j++] = '>';  consumed = 1; }
        else if (body_length == 4 && memcmp(body, "quot", 4) == 0) { output[j++] = '"';  consumed = 1; }
        else if (body_length == 4 && memcmp(body, "apos", 4) == 0) { output[j++] = '\''; consumed = 1; }
        else if (body_length >= 2 && body[0] == '#') {
            uint32_t codepoint = 0;
            int is_valid = 1;
            if (body[1] == 'x' || body[1] == 'X') {
                if (body_length < 3) is_valid = 0;
                for (int32_t entity_index = 2; entity_index < body_length && is_valid; entity_index++) {
                    char decoded_character = body[entity_index];
                    codepoint *= 16;
                    if (decoded_character >= '0' && decoded_character <= '9') codepoint += (uint32_t)(decoded_character - '0');
                    else if (decoded_character >= 'a' && decoded_character <= 'f') codepoint += (uint32_t)(decoded_character - 'a' + 10);
                    else if (decoded_character >= 'A' && decoded_character <= 'F') codepoint += (uint32_t)(decoded_character - 'A' + 10);
                    else is_valid = 0;
                    if (codepoint > 0x10FFFF) is_valid = 0;
                }
            } else {
                for (int32_t entity_index = 1; entity_index < body_length && is_valid; entity_index++) {
                    char decoded_character = body[entity_index];
                    if (decoded_character < '0' || decoded_character > '9') { is_valid = 0; break; }
                    codepoint = codepoint * 10 + (uint32_t)(decoded_character - '0');
                    if (codepoint > 0x10FFFF) is_valid = 0;
                }
            }
            if (is_valid) {
                int32_t entity_length = encoding_utf8_encode(codepoint, output + j);
                if (entity_length > 0) { j += entity_length; consumed = 1; }
            }
        }

        if (consumed) {
            i = semi + 1;
        } else {
            output[j++] = string.data[i++];
        }
    }
    output[j] = '\0';
    GrayString result = { output, j };
    return result;
}

GrayString gray_encoding_shell_escape(GrayArena *arena, GrayString string) {
    if (string.len == 0) return gray_string_lit("''");

    int safe = 1;
    for (int32_t i = 0; i < string.len; i++) {
        char character = string.data[i];
        if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-' || character == '.' || character == '/' || character == ',' ||
              character == ':' || character == '@' || character == '+' || character == '=' || character == '%')) {
            safe = 0;
            break;
        }
    }
    if (safe) return gray_string_new(arena, string.data, string.len);

    /* Wrap in single quotes; each embedded ' becomes '\'' (4 chars). */
    char *output = gray_arena_alloc_uninitialized(arena, (size_t)string.len * 4 + 3);
    int32_t j = 0;
    output[j++] = '\'';
    for (int32_t i = 0; i < string.len; i++) {
        if (string.data[i] == '\'') {
            output[j++] = '\''; output[j++] = '\\'; output[j++] = '\''; output[j++] = '\'';
        } else {
            output[j++] = string.data[i];
        }
    }
    output[j++] = '\'';
    output[j] = '\0';
    GrayString result = { output, j };
    return result;
}

/* --- Byte conversion functions (formerly @bytes module) --- */

GrayArray gray_encoding_from_string(GrayArena *arena, GrayString string) {
    GrayArray array = gray_array_new(arena, sizeof(uint8_t), string.len, GRAY_ELEM_U8);
    for (int32_t i = 0; i < string.len; i++) {
        uint8_t byte_value = (uint8_t)string.data[i];
        GRAY_ARRAY_PUSH(arena, &array, &byte_value);
    }
    return array;
}

GrayString gray_encoding_to_string(GrayArena *arena, GrayArray *bytes) {
    return gray_string_new(arena, (const char *)bytes->data, bytes->len);
}

GrayArray gray_encoding_from_hex(GrayArena *arena, GrayString hex_text) {
    int32_t output_length = hex_text.len / 2;
    GrayArray array = gray_array_new(arena, sizeof(uint8_t), output_length, GRAY_ELEM_U8);
    for (int32_t i = 0; i < output_length; i++) {
        unsigned int byte;
        sscanf(hex_text.data + i * 2, "%02x", &byte);
        uint8_t byte_value = (uint8_t)byte;
        GRAY_ARRAY_PUSH(arena, &array, &byte_value);
    }
    return array;
}

GrayString gray_encoding_to_hex(GrayArena *arena, GrayArray *bytes) {
    int32_t output_length = bytes->len * 2;
    char *hex_text = gray_arena_alloc_uninitialized(arena, (size_t)output_length + 1);
    uint8_t *data = (uint8_t *)bytes->data;
    for (int32_t i = 0; i < bytes->len; i++) {
        snprintf(hex_text + i * 2, 3, "%02x", data[i]);
    }
    hex_text[output_length] = '\0';
    GrayString result = { hex_text, output_length };
    return result;
}

GrayArray gray_encoding_from_base64(GrayArena *arena, GrayString base64_text) {
    GrayString decoded = gray_encoding_base64_decode(arena, base64_text);
    return gray_encoding_from_string(arena, decoded);
}

GrayString gray_encoding_to_base64(GrayArena *arena, GrayArray *bytes) {
    GrayString string = gray_encoding_to_string(arena, bytes);
    return gray_encoding_base64_encode(arena, string);
}
