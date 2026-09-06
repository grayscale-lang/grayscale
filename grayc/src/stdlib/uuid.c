/*
 * uuid.c — Implementation of the uuid stdlib module.
 * Generates RFC 4122 version 4 UUIDs using cryptographically
 * suitable random bytes (getentropy or /dev/urandom).
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

/* Must precede every <stdlib.h> inclusion (also transitive ones via uuid.h)
 * so the CRT declares rand_s, the Windows entropy source below. */
#ifdef _WIN32
#define _CRT_RAND_S
#include <stdlib.h>
#endif

#include "uuid.h"
#include "builtins.h"
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__linux__)
#include <unistd.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <sys/random.h>
#endif

#define GRAY_UUID_LEN             36
#define GRAY_UUID_COMPACT_LEN     32

/* Cryptographically-suitable random bytes. Prefers getentropy() (macOS,
 * BSDs, glibc 2.25+) and falls back to /dev/urandom; on Windows, rand_s
 * (RtlGenRandom under the hood, no extra link library). On failure, returns
 * false; callers should treat that as fatal since UUID uniqueness is the
 * whole point. */
static bool gray_uuid_random_bytes(uint8_t *buf, size_t count) {
#ifdef _WIN32
    for (size_t i = 0; i < count; i += sizeof(unsigned int)) {
        unsigned int random_word;
        if (rand_s(&random_word) != 0) return false;
        size_t chunk = (count - i < sizeof(random_word)) ? count - i : sizeof(random_word);
        memcpy(buf + i, &random_word, chunk);
    }
    return true;
#else
#if defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__linux__)
    if (count <= 256 && getentropy(buf, count) == 0) return true;
#endif
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom) return false;
    size_t bytes_read = fread(buf, 1, count, urandom);
    fclose(urandom);
    return bytes_read == count;
#endif
}

static void gray_uuid_format_hyphenated(const uint8_t *bytes, char *buf) {
    snprintf(buf, GRAY_UUID_LEN + 1,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
}

static int uuid_hex_val(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 0;
}

/* Decode the canonical 36-char hyphenated form into 16 bytes. A value that
 * is not 36 chars (failed generate, or the default-zero struct) yields the
 * nil UUID's bytes. */
static void uuid_to_bytes16(GrayUUID id, uint8_t out[16]) {
    if (id.value.len != GRAY_UUID_LEN) {
        memset(out, 0, 16);
        return;
    }
    int b = 0;
    for (int i = 0; i < GRAY_UUID_LEN && b < 16; i++) {
        if (id.value.data[i] == '-') continue;
        out[b++] = (uint8_t)((uuid_hex_val(id.value.data[i]) << 4) |
                             uuid_hex_val(id.value.data[i + 1]));
        i++;
    }
}

GrayArray gray_uuid_to_bytes(GrayArena *arena, GrayUUID id) {
    uint8_t bytes[16];
    uuid_to_bytes16(id, bytes);
    return gray_array_from(arena, bytes, sizeof(uint8_t), 16);
}

GrayUUID gray_uuid_from_bytes(GrayArena *arena, GrayArray *bytes) {
    if (bytes->len < 16) {
        gray_builtin_panic_msg(gray_string_lit("uuid.from_bytes: need 16 bytes"));
    }
    uint8_t raw[16];
    for (int i = 0; i < 16; i++) {
        raw[i] = (bytes->elem_size == 1)
            ? ((const uint8_t *)bytes->data)[i]
            : (uint8_t)((const int64_t *)bytes->data)[i];
    }
    char buf[GRAY_UUID_LEN + 1];
    gray_uuid_format_hyphenated(raw, buf);
    GrayUUID uuid;
    uuid.value = gray_string_new(arena, buf, GRAY_UUID_LEN);
    return uuid;
}

GrayUUID gray_uuid_generate(GrayArena *arena) {
    uint8_t bytes[16];
    GrayUUID uuid;
    if (!gray_uuid_random_bytes(bytes, sizeof(bytes))) {
        uuid.value = gray_string_lit("");
        return uuid;
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40; /* version 4 */
    bytes[8] = (bytes[8] & 0x3F) | 0x80; /* variant 1 (RFC 4122) */
    char buf[GRAY_UUID_LEN + 1];
    gray_uuid_format_hyphenated(bytes, buf);
    uuid.value = gray_string_new(arena, buf, GRAY_UUID_LEN);
    return uuid;
}

GrayString gray_uuid_generate_compact(GrayArena *arena, GrayUUID id) {
    /* Strip hyphens from the canonical 36-char hyphenated form. */
    if (id.value.len != GRAY_UUID_LEN) return gray_string_lit("");
    char buf[GRAY_UUID_COMPACT_LEN + 1];
    int out_pos = 0;
    for (int i = 0; i < GRAY_UUID_LEN; i++) {
        if (id.value.data[i] != '-') {
            buf[out_pos++] = id.value.data[i];
        }
    }
    buf[GRAY_UUID_COMPACT_LEN] = '\0';
    return gray_string_new(arena, buf, GRAY_UUID_COMPACT_LEN);
}

GrayUUID gray_uuid_generate_random(GrayArena *arena) {
    /* Alias for the canonical hyphenated v4 form. */
    return gray_uuid_generate(arena);
}

/* RFC 9562 §5.7 — UUID v7:
 *   bytes[0..5]   48-bit Unix timestamp (ms), big-endian
 *   bytes[6]      version 7 (high nibble) + 4 random bits
 *   bytes[7]      8 random bits
 *   bytes[8]      variant 10 (high 2 bits) + 6 random bits
 *   bytes[9..15]  56 random bits
 */
GrayUUID gray_uuid_generate_time_ordered(GrayArena *arena) {
    uint8_t bytes[16];
    GrayUUID uuid;
    if (!gray_uuid_random_bytes(bytes, sizeof(bytes))) {
        uuid.value = gray_string_lit("");
        return uuid;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);

    bytes[0] = (uint8_t)(ms >> 40);
    bytes[1] = (uint8_t)(ms >> 32);
    bytes[2] = (uint8_t)(ms >> 24);
    bytes[3] = (uint8_t)(ms >> 16);
    bytes[4] = (uint8_t)(ms >> 8);
    bytes[5] = (uint8_t)(ms);
    bytes[6] = (bytes[6] & 0x0F) | 0x70; /* version 7 */
    bytes[8] = (bytes[8] & 0x3F) | 0x80; /* variant 1 */

    char buf[GRAY_UUID_LEN + 1];
    gray_uuid_format_hyphenated(bytes, buf);
    uuid.value = gray_string_new(arena, buf, GRAY_UUID_LEN);
    return uuid;
}

bool gray_uuid_is_valid(GrayString str) {
    if (str.len != GRAY_UUID_LEN) return false;
    for (int i = 0; i < GRAY_UUID_LEN; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (str.data[i] != '-') return false;
        } else {
            char ch = str.data[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
                return false;
        }
    }
    return true;
}

/* Strict parser: panics on invalid input. Callers that want a fallible
 * check should gate with gray_uuid_is_valid() first. Returns the input
 * normalized to lowercase, wrapped in a UUID struct. */
GrayUUID gray_uuid_parse(GrayArena *arena, GrayString str) {
    if (!gray_uuid_is_valid(str)) {
        gray_builtin_panic_msg(gray_string_lit("uuid.parse: invalid UUID string"));
    }
    char *buf = (char *)gray_arena_alloc_uninitialized(arena, GRAY_UUID_LEN + 1);
    for (int i = 0; i < GRAY_UUID_LEN; i++) {
        char ch = str.data[i];
        buf[i] = (ch >= 'A' && ch <= 'F') ? (char)(ch - 'A' + 'a') : ch;
    }
    buf[GRAY_UUID_LEN] = '\0';
    GrayUUID uuid;
    uuid.value.data = buf;
    uuid.value.len = GRAY_UUID_LEN;
    return uuid;
}

GrayString gray_uuid_to_string(GrayUUID id) {
    return id.value;
}

GrayUUID gray_uuid_nil(void) {
    GrayUUID uuid;
    uuid.value = gray_string_lit("00000000-0000-0000-0000-000000000000");
    return uuid;
}
