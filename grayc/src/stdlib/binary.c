/*
 * binary.c — Implementation of the binary stdlib module.
 * Encodes and decodes integers and floats to/from byte arrays in
 * little-endian and big-endian byte order.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "binary.h"
#include <string.h>

static GrayArray make_bytes(GrayArena *arena, const void *data, int32_t size) {
    GrayArray arr = gray_array_new(arena, sizeof(uint8_t), size);
    for (int32_t i = 0; i < size; i++) {
        uint8_t b = ((const uint8_t *)data)[i];
        GRAY_ARRAY_PUSH(arena, &arr, &b);
    }
    return arr;
}

static GrayArray make_bytes_reversed(GrayArena *arena, const void *data, int32_t size) {
    GrayArray arr = gray_array_new(arena, sizeof(uint8_t), size);
    for (int32_t i = size - 1; i >= 0; i--) {
        uint8_t b = ((const uint8_t *)data)[i];
        GRAY_ARRAY_PUSH(arena, &arr, &b);
    }
    return arr;
}

/* --- Codec macros for encode/decode generation --- */

#define BINARY_ENCODE_LE(NAME, TYPE, SIZE)                                          \
    GrayArray gray_binary_encode_##NAME##_le(GrayArena *arena, TYPE val) {           \
        return make_bytes(arena, &val, SIZE);                                        \
    }

#define BINARY_ENCODE_BE(NAME, TYPE, SIZE)                                          \
    GrayArray gray_binary_encode_##NAME##_be(GrayArena *arena, TYPE val) {           \
        return make_bytes_reversed(arena, &val, SIZE);                               \
    }

#define BINARY_DECODE_LE(NAME, TYPE, SIZE)                                          \
    TYPE gray_binary_decode_##NAME##_le(GrayArray *bytes) {                          \
        TYPE v; memcpy(&v, bytes->data, SIZE); return v;                             \
    }

#define BINARY_DECODE_BE(NAME, TYPE, SIZE)                                          \
    TYPE gray_binary_decode_##NAME##_be(GrayArray *bytes) {                          \
        uint8_t *d = (uint8_t *)bytes->data;                                         \
        uint8_t rev[SIZE];                                                           \
        for (int32_t _i = 0; _i < SIZE; _i++) rev[_i] = d[SIZE - 1 - _i];          \
        TYPE v; memcpy(&v, rev, SIZE); return v;                                     \
    }

#define BINARY_CODEC(NAME, TYPE, SIZE)                                              \
    BINARY_ENCODE_LE(NAME, TYPE, SIZE)                                              \
    BINARY_ENCODE_BE(NAME, TYPE, SIZE)                                              \
    BINARY_DECODE_LE(NAME, TYPE, SIZE)                                              \
    BINARY_DECODE_BE(NAME, TYPE, SIZE)

/* --- 8-bit (no endianness) --- */
GrayArray gray_binary_encode_i8(GrayArena *arena, int8_t val) { return make_bytes(arena, &val, 1); }
GrayArray gray_binary_encode_u8(GrayArena *arena, uint8_t val) { return make_bytes(arena, &val, 1); }
int8_t gray_binary_decode_i8(GrayArray *bytes) { return *(int8_t *)bytes->data; }
uint8_t gray_binary_decode_u8(GrayArray *bytes) { return *(uint8_t *)bytes->data; }

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
BINARY_ENCODE_LE(i128, gray_i128, 16)
BINARY_ENCODE_BE(i128, gray_i128, 16)
BINARY_DECODE_LE(i128, gray_i128, 16)
gray_i128 gray_binary_decode_i128_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    gray_i128 v;
    uint64_t high = 0, low = 0;
    for (int i = 0; i < 8; i++) high = (high << 8) | d[i];
    for (int i = 8; i < 16; i++) low = (low << 8) | d[i];
    v.hi = (int64_t)high;
    v.lo = low;
    return v;
}

BINARY_ENCODE_LE(u128, gray_u128, 16)
BINARY_ENCODE_BE(u128, gray_u128, 16)
BINARY_DECODE_LE(u128, gray_u128, 16)
gray_u128 gray_binary_decode_u128_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    gray_u128 v;
    uint64_t high = 0, low = 0;
    for (int i = 0; i < 8; i++) high = (high << 8) | d[i];
    for (int i = 8; i < 16; i++) low = (low << 8) | d[i];
    v.hi = high;
    v.lo = low;
    return v;
}

/* --- 256-bit (custom BE decode for 4-limb struct layout) --- */
BINARY_ENCODE_LE(i256, gray_i256, 32)
BINARY_ENCODE_BE(i256, gray_i256, 32)
BINARY_DECODE_LE(i256, gray_i256, 32)
gray_i256 gray_binary_decode_i256_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    gray_i256 v;
    for (int w = 3; w >= 0; w--) {
        uint64_t limb = 0;
        for (int i = 0; i < 8; i++) limb = (limb << 8) | d[(3 - w) * 8 + i];
        v.w[w] = limb;
    }
    return v;
}

BINARY_ENCODE_LE(u256, gray_u256, 32)
BINARY_ENCODE_BE(u256, gray_u256, 32)
BINARY_DECODE_LE(u256, gray_u256, 32)
gray_u256 gray_binary_decode_u256_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    gray_u256 v;
    for (int w = 3; w >= 0; w--) {
        uint64_t limb = 0;
        for (int i = 0; i < 8; i++) limb = (limb << 8) | d[(3 - w) * 8 + i];
        v.w[w] = limb;
    }
    return v;
}

#undef BINARY_ENCODE_LE
#undef BINARY_ENCODE_BE
#undef BINARY_DECODE_LE
#undef BINARY_DECODE_BE
#undef BINARY_CODEC
