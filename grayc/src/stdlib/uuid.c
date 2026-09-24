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
#include "crypto.h"
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

#define GRAY_UUID_LENGTH             36
#define GRAY_UUID_COMPACT_LENGTH     32

/* Cryptographically-suitable random bytes. Prefers getentropy() (macOS,
 * BSDs, glibc 2.25+) and falls back to /dev/urandom; on Windows, rand_s
 * (RtlGenRandom under the hood, no extra link library). On failure, returns
 * false; callers should treat that as fatal since UUID uniqueness is the
 * whole point. */
static bool gray_uuid_random_bytes(uint8_t *buffer, size_t count) {
#ifdef _WIN32
    for (size_t i = 0; i < count; i += sizeof(unsigned int)) {
        unsigned int random_word;
        if (rand_s(&random_word) != 0) return false;
        size_t chunk = (count - i < sizeof(random_word)) ? count - i : sizeof(random_word);
        memcpy(buffer + i, &random_word, chunk);
    }
    return true;
#else
#if defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__linux__)
    if (count <= 256 && getentropy(buffer, count) == 0) return true;
#endif
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom) return false;
    size_t bytes_read = fread(buffer, 1, count, urandom);
    fclose(urandom);
    return bytes_read == count;
#endif
}

static void gray_uuid_format_hyphenated(const uint8_t *bytes, char *buffer) {
    snprintf(buffer, GRAY_UUID_LENGTH + 1,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
}

static int uuid_hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return 0;
}

/* Decode the canonical 36-char hyphenated form into 16 bytes. A value that
 * is not 36 chars (failed generate, or the default-zero struct) yields the
 * nil UUID's bytes. */
static void uuid_to_bytes16(GrayUUID uuid, uint8_t output[16]) {
    if (uuid.value.len != GRAY_UUID_LENGTH) {
        memset(output, 0, 16);
        return;
    }
    int byte_index = 0;
    for (int i = 0; i < GRAY_UUID_LENGTH && byte_index < 16; i++) {
        if (uuid.value.data[i] == '-') continue;
        output[byte_index++] = (uint8_t)((uuid_hex_value(uuid.value.data[i]) << 4) |
                             uuid_hex_value(uuid.value.data[i + 1]));
        i++;
    }
}

GrayArray gray_uuid_to_bytes(GrayArena *arena, GrayUUID uuid) {
    uint8_t bytes[16];
    uuid_to_bytes16(uuid, bytes);
    return gray_array_from(arena, bytes, sizeof(uint8_t), 16, GRAY_ELEM_U8);
}

GrayUUID gray_uuid_from_bytes(GrayArena *arena, GrayArray *bytes) {
    if (bytes->len < 16) {
        gray_builtin_panic_msg(gray_string_lit("uuid.from_bytes: need 16 bytes"));
    }
    uint8_t raw_bytes[16];
    for (int i = 0; i < 16; i++) {
        raw_bytes[i] = ((const uint8_t *)bytes->data)[i];
    }
    char buffer[GRAY_UUID_LENGTH + 1];
    gray_uuid_format_hyphenated(raw_bytes, buffer);
    GrayUUID uuid;
    uuid.value = gray_string_new(arena, buffer, GRAY_UUID_LENGTH);
    return uuid;
}

int64_t gray_uuid_version(GrayUUID uuid) {
    uint8_t bytes[16];
    uuid_to_bytes16(uuid, bytes);
    return (bytes[6] >> 4) & 0x0F;
}

GrayUuidTimestamp gray_uuid_timestamp(GrayUUID uuid) {
    uint8_t byte_value[16];
    uuid_to_bytes16(uuid, byte_value);
    GrayUuidTimestamp result = { 0, false };
    int version = (byte_value[6] >> 4) & 0x0F;

    if (version == 7) {
        /* RFC 9562 §5.7: bytes 0..5 are a 48-bit big-endian Unix ms count. */
        uint64_t milliseconds = ((uint64_t)byte_value[0] << 40) | ((uint64_t)byte_value[1] << 32) |
                      ((uint64_t)byte_value[2] << 24) | ((uint64_t)byte_value[3] << 16) |
                      ((uint64_t)byte_value[4] << 8)  | (uint64_t)byte_value[5];
        result.v0 = (int64_t)milliseconds;
        result.v1 = true;
    } else if (version == 1) {
        /* RFC 4122 §4.1.2: 60-bit count of 100ns intervals since the Gregorian
         * epoch (1582-10-15), split across time_low / time_mid / time_hi. */
        uint64_t time_low = ((uint64_t)byte_value[0] << 24) | ((uint64_t)byte_value[1] << 16) |
                            ((uint64_t)byte_value[2] << 8)  | (uint64_t)byte_value[3];
        uint64_t time_mid = ((uint64_t)byte_value[4] << 8) | (uint64_t)byte_value[5];
        uint64_t time_hi  = (((uint64_t)byte_value[6] & 0x0F) << 8) | (uint64_t)byte_value[7];
        uint64_t ticks = (time_hi << 48) | (time_mid << 32) | time_low;
        uint64_t gregorian_offset = 0x01B21DD213814000ULL; /* 100ns from 1582 to 1970 */
        if (ticks >= gregorian_offset) {
            result.v0 = (int64_t)((ticks - gregorian_offset) / 10000ULL);
            result.v1 = true;
        }
    }
    return result;
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
    char buffer[GRAY_UUID_LENGTH + 1];
    gray_uuid_format_hyphenated(bytes, buffer);
    uuid.value = gray_string_new(arena, buffer, GRAY_UUID_LENGTH);
    return uuid;
}

GrayUUID gray_uuid_generate_v5(GrayArena *arena, GrayUUID namespace_id, GrayString name) {
    uint8_t namespace_bytes[16];
    uuid_to_bytes16(namespace_id, namespace_bytes);

    /* SHA-1 over the namespace bytes followed by the name. */
    size_t length = 16 + (size_t)name.len;
    uint8_t *buffer = (uint8_t *)gray_arena_alloc_uninitialized(arena, length);
    memcpy(buffer, namespace_bytes, 16);
    if (name.len > 0) memcpy(buffer + 16, name.data, (size_t)name.len);

    uint8_t digest[20];
    gray_crypto_sha1_raw(arena, buffer, length, digest);

    uint8_t output[16];
    memcpy(output, digest, 16);
    output[6] = (output[6] & 0x0F) | 0x50; /* version 5 */
    output[8] = (output[8] & 0x3F) | 0x80; /* variant 1 (RFC 4122) */

    char strbuf[GRAY_UUID_LENGTH + 1];
    gray_uuid_format_hyphenated(output, strbuf);
    GrayUUID uuid;
    uuid.value = gray_string_new(arena, strbuf, GRAY_UUID_LENGTH);
    return uuid;
}

GrayString gray_uuid_generate_compact(GrayArena *arena, GrayUUID uuid) {
    /* Strip hyphens from the canonical 36-char hyphenated form. A
     * non-canonical value is the nil UUID (see gray_uuid_to_string). */
    if (uuid.value.len != GRAY_UUID_LENGTH)
        return gray_string_lit("00000000000000000000000000000000");
    char buffer[GRAY_UUID_COMPACT_LENGTH + 1];
    int output_position = 0;
    for (int i = 0; i < GRAY_UUID_LENGTH; i++) {
        if (uuid.value.data[i] != '-') {
            buffer[output_position++] = uuid.value.data[i];
        }
    }
    buffer[GRAY_UUID_COMPACT_LENGTH] = '\0';
    return gray_string_new(arena, buffer, GRAY_UUID_COMPACT_LENGTH);
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

    struct timespec time_spec;
    clock_gettime(CLOCK_REALTIME, &time_spec);
    uint64_t milliseconds = (uint64_t)time_spec.tv_sec * 1000ULL + (uint64_t)(time_spec.tv_nsec / 1000000);

    bytes[0] = (uint8_t)(milliseconds >> 40);
    bytes[1] = (uint8_t)(milliseconds >> 32);
    bytes[2] = (uint8_t)(milliseconds >> 24);
    bytes[3] = (uint8_t)(milliseconds >> 16);
    bytes[4] = (uint8_t)(milliseconds >> 8);
    bytes[5] = (uint8_t)(milliseconds);
    bytes[6] = (bytes[6] & 0x0F) | 0x70; /* version 7 */
    bytes[8] = (bytes[8] & 0x3F) | 0x80; /* variant 1 */

    char buffer[GRAY_UUID_LENGTH + 1];
    gray_uuid_format_hyphenated(bytes, buffer);
    uuid.value = gray_string_new(arena, buffer, GRAY_UUID_LENGTH);
    return uuid;
}

bool gray_uuid_is_valid(GrayString string) {
    if (string.len != GRAY_UUID_LENGTH) return false;
    for (int i = 0; i < GRAY_UUID_LENGTH; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (string.data[i] != '-') return false;
        } else {
            char character = string.data[i];
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F')))
                return false;
        }
    }
    return true;
}

/* Strict parser: panics on invalid input. Callers that want a fallible
 * check should gate with gray_uuid_is_valid() first. Returns the input
 * normalized to lowercase, wrapped in a UUID struct. */
GrayUUID gray_uuid_parse(GrayArena *arena, GrayString string) {
    if (!gray_uuid_is_valid(string)) {
        gray_builtin_panic_msg(gray_string_lit("uuid.parse: invalid UUID string"));
    }
    char *buffer = (char *)gray_arena_alloc_uninitialized(arena, GRAY_UUID_LENGTH + 1);
    for (int i = 0; i < GRAY_UUID_LENGTH; i++) {
        char character = string.data[i];
        buffer[i] = (character >= 'A' && character <= 'F') ? (char)(character - 'A' + 'a') : character;
    }
    buffer[GRAY_UUID_LENGTH] = '\0';
    GrayUUID uuid;
    uuid.value.data = buffer;
    uuid.value.len = GRAY_UUID_LENGTH;
    return uuid;
}

GrayString gray_uuid_to_string(GrayUUID uuid) {
    /* A non-canonical value (default-zero struct, failed generate) is the nil
     * UUID — render it as such, the way uuid_to_bytes16 already treats it. */
    if (uuid.value.len != GRAY_UUID_LENGTH)
        return gray_string_lit("00000000-0000-0000-0000-000000000000");
    return uuid.value;
}

GrayUUID gray_uuid_nil(void) {
    GrayUUID uuid;
    uuid.value = gray_string_lit("00000000-0000-0000-0000-000000000000");
    return uuid;
}
