/*
 * binary.c — Implementation of the binary stdlib module.
 * Encodes and decodes integers and floating-point numbers to/from byte arrays in
 * little-endian and big-endian byte order.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "binary.h"
#include <string.h>

/* Byte width of the wide integer types. */
#define WIDE128_BYTES 16
#define WIDE256_BYTES 32

static GrayArray make_bytes(GrayArena *arena, const void *data, int32_t size) {
    GrayArray array = gray_array_new(arena, sizeof(uint8_t), size, GRAY_ELEM_U8);
    for (int32_t i = 0; i < size; i++) {
        uint8_t byte_value = ((const uint8_t *)data)[i];
        GRAY_ARRAY_PUSH(arena, &array, &byte_value);
    }
    return array;
}

static GrayArray make_bytes_reversed(GrayArena *arena, const void *data, int32_t size) {
    GrayArray array = gray_array_new(arena, sizeof(uint8_t), size, GRAY_ELEM_U8);
    for (int32_t i = size - 1; i >= 0; i--) {
        uint8_t byte_value = ((const uint8_t *)data)[i];
        GRAY_ARRAY_PUSH(arena, &array, &byte_value);
    }
    return array;
}

/* Every decode_* reads a fixed number of bytes from `bytes->data` starting
 * at offset 0; nothing about that memcpy stops it from reading past
 * `bytes->len`. Panic P0113 instead of silently exposing whatever
 * uninitialized or stale arena memory follows a too-short array. */
static void gray_binary_check_length(GrayArray *bytes, int32_t need, const char *function_name,
                                  const char *file, int line) {
    if (bytes->len < need) {
        gray_panic_code_at(file, line, "P0113",
            "binary.%s: byte array too short to decode; need %d bytes but have %d",
            function_name, (int)need, (int)bytes->len);
    }
}

/* --- Codec macros for encode/decode generation --- */

#define BINARY_ENCODE_LE(NAME, TYPE, SIZE)                                          \
    GrayArray gray_binary_encode_##NAME##_le(GrayArena *arena, TYPE value) {           \
        return make_bytes(arena, &value, SIZE);                                        \
    }

#define BINARY_ENCODE_BE(NAME, TYPE, SIZE)                                          \
    GrayArray gray_binary_encode_##NAME##_be(GrayArena *arena, TYPE value) {           \
        return make_bytes_reversed(arena, &value, SIZE);                               \
    }

#define BINARY_DECODE_LE(NAME, TYPE, SIZE)                                          \
    TYPE gray_binary_decode_##NAME##_le(GrayArray *bytes, const char *file, int line) { \
        gray_binary_check_length(bytes, SIZE, "decode_" #NAME "_le", file, line);       \
        TYPE value; memcpy(&value, bytes->data, SIZE); return value;                             \
    }

#define BINARY_DECODE_BE(NAME, TYPE, SIZE)                                          \
    TYPE gray_binary_decode_##NAME##_be(GrayArray *bytes, const char *file, int line) { \
        gray_binary_check_length(bytes, SIZE, "decode_" #NAME "_be", file, line);       \
        uint8_t *bytes_data = (uint8_t *)bytes->data;                                         \
        uint8_t reversed_bytes[SIZE];                                                           \
        for (int32_t byte_index = 0; byte_index < SIZE; byte_index++) reversed_bytes[byte_index] = bytes_data[SIZE - 1 - byte_index];          \
        TYPE value; memcpy(&value, reversed_bytes, SIZE); return value;                                     \
    }

#define BINARY_CODEC(NAME, TYPE, SIZE)                                              \
    BINARY_ENCODE_LE(NAME, TYPE, SIZE)                                              \
    BINARY_ENCODE_BE(NAME, TYPE, SIZE)                                              \
    BINARY_DECODE_LE(NAME, TYPE, SIZE)                                              \
    BINARY_DECODE_BE(NAME, TYPE, SIZE)

/* --- 8-bit (no endianness) --- */
GrayArray gray_binary_encode_i8(GrayArena *arena, int8_t value) { return make_bytes(arena, &value, 1); }
GrayArray gray_binary_encode_u8(GrayArena *arena, uint8_t value) { return make_bytes(arena, &value, 1); }
int8_t gray_binary_decode_i8(GrayArray *bytes, const char *file, int line) {
    gray_binary_check_length(bytes, 1, "decode_i8", file, line);
    return *(int8_t *)bytes->data;
}
uint8_t gray_binary_decode_u8(GrayArray *bytes, const char *file, int line) {
    gray_binary_check_length(bytes, 1, "decode_u8", file, line);
    return *(uint8_t *)bytes->data;
}

/* --- 16 through 64-bit integers + floats --- */
BINARY_CODEC(i16, int16_t, 2)
BINARY_CODEC(u16, uint16_t, 2)
BINARY_CODEC(i32, int32_t, 4)
BINARY_CODEC(u32, uint32_t, 4)
BINARY_CODEC(i64, int64_t, 8)
BINARY_CODEC(u64, uint64_t, 8)
BINARY_CODEC(f32, float, 4)
BINARY_CODEC(f64, double, 8)

/* --- 128-bit (custom BE decode for multi-limb struct layout) --- */
BINARY_ENCODE_LE(i128, gray_i128, WIDE128_BYTES)
BINARY_ENCODE_BE(i128, gray_i128, WIDE128_BYTES)
BINARY_DECODE_LE(i128, gray_i128, WIDE128_BYTES)
gray_i128 gray_binary_decode_i128_be(GrayArray *bytes, const char *file, int line) {
    gray_binary_check_length(bytes, WIDE128_BYTES, "decode_i128_be", file, line);
    uint8_t *bytes_data = (uint8_t *)bytes->data;
    gray_i128 value;
    uint64_t high = 0, low = 0;
    for (int i = 0; i < 8; i++) high = (high << 8) | bytes_data[i];
    for (int i = 8; i < WIDE128_BYTES; i++) low = (low << 8) | bytes_data[i];
    value.high = (int64_t)high;
    value.low = low;
    return value;
}

BINARY_ENCODE_LE(u128, gray_u128, WIDE128_BYTES)
BINARY_ENCODE_BE(u128, gray_u128, WIDE128_BYTES)
BINARY_DECODE_LE(u128, gray_u128, WIDE128_BYTES)
gray_u128 gray_binary_decode_u128_be(GrayArray *bytes, const char *file, int line) {
    gray_binary_check_length(bytes, WIDE128_BYTES, "decode_u128_be", file, line);
    uint8_t *bytes_data = (uint8_t *)bytes->data;
    gray_u128 value;
    uint64_t high = 0, low = 0;
    for (int i = 0; i < 8; i++) high = (high << 8) | bytes_data[i];
    for (int i = 8; i < WIDE128_BYTES; i++) low = (low << 8) | bytes_data[i];
    value.high = high;
    value.low = low;
    return value;
}

/* --- 256-bit (custom BE decode for 4-limb struct layout) --- */
BINARY_ENCODE_LE(i256, gray_i256, WIDE256_BYTES)
BINARY_ENCODE_BE(i256, gray_i256, WIDE256_BYTES)
BINARY_DECODE_LE(i256, gray_i256, WIDE256_BYTES)
gray_i256 gray_binary_decode_i256_be(GrayArray *bytes, const char *file, int line) {
    gray_binary_check_length(bytes, WIDE256_BYTES, "decode_i256_be", file, line);
    uint8_t *bytes_data = (uint8_t *)bytes->data;
    gray_i256 value;
    for (int limb_index = 3; limb_index >= 0; limb_index--) {
        uint64_t limb = 0;
        for (int i = 0; i < 8; i++) limb = (limb << 8) | bytes_data[(3 - limb_index) * 8 + i];
        value.w[limb_index] = limb;
    }
    return value;
}

BINARY_ENCODE_LE(u256, gray_u256, WIDE256_BYTES)
BINARY_ENCODE_BE(u256, gray_u256, WIDE256_BYTES)
BINARY_DECODE_LE(u256, gray_u256, WIDE256_BYTES)
gray_u256 gray_binary_decode_u256_be(GrayArray *bytes, const char *file, int line) {
    gray_binary_check_length(bytes, WIDE256_BYTES, "decode_u256_be", file, line);
    uint8_t *bytes_data = (uint8_t *)bytes->data;
    gray_u256 value;
    for (int limb_index = 3; limb_index >= 0; limb_index--) {
        uint64_t limb = 0;
        for (int i = 0; i < 8; i++) limb = (limb << 8) | bytes_data[(3 - limb_index) * 8 + i];
        value.w[limb_index] = limb;
    }
    return value;
}

#undef BINARY_ENCODE_LE
#undef BINARY_ENCODE_BE
#undef BINARY_DECODE_LE
#undef BINARY_DECODE_BE
#undef BINARY_CODEC
