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

/* --- 8-bit --- */
GrayArray gray_binary_encode_i8(GrayArena *arena, int8_t val) { return make_bytes(arena, &val, 1); }
GrayArray gray_binary_encode_u8(GrayArena *arena, uint8_t val) { return make_bytes(arena, &val, 1); }
int8_t gray_binary_decode_i8(GrayArray *bytes) { return *(int8_t *)bytes->data; }
uint8_t gray_binary_decode_u8(GrayArray *bytes) { return *(uint8_t *)bytes->data; }

/* --- 16-bit signed --- */
GrayArray gray_binary_encode_i16_le(GrayArena *arena, int16_t val) { return make_bytes(arena, &val, 2); }
GrayArray gray_binary_encode_i16_be(GrayArena *arena, int16_t val) { return make_bytes_reversed(arena, &val, 2); }
int16_t gray_binary_decode_i16_le(GrayArray *bytes) { int16_t v; memcpy(&v, bytes->data, 2); return v; }
int16_t gray_binary_decode_i16_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    return (int16_t)((d[0] << 8) | d[1]);
}

/* --- 16-bit unsigned --- */
GrayArray gray_binary_encode_u16_le(GrayArena *arena, uint16_t val) { return make_bytes(arena, &val, 2); }
GrayArray gray_binary_encode_u16_be(GrayArena *arena, uint16_t val) { return make_bytes_reversed(arena, &val, 2); }
uint16_t gray_binary_decode_u16_le(GrayArray *bytes) { uint16_t v; memcpy(&v, bytes->data, 2); return v; }
uint16_t gray_binary_decode_u16_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    return (uint16_t)((d[0] << 8) | d[1]);
}

/* --- 32-bit signed --- */
GrayArray gray_binary_encode_i32_le(GrayArena *arena, int32_t val) { return make_bytes(arena, &val, 4); }
GrayArray gray_binary_encode_i32_be(GrayArena *arena, int32_t val) { return make_bytes_reversed(arena, &val, 4); }
int32_t gray_binary_decode_i32_le(GrayArray *bytes) { int32_t v; memcpy(&v, bytes->data, 4); return v; }
int32_t gray_binary_decode_i32_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    return (int32_t)(((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3]);
}

/* --- 32-bit unsigned --- */
GrayArray gray_binary_encode_u32_le(GrayArena *arena, uint32_t val) { return make_bytes(arena, &val, 4); }
GrayArray gray_binary_encode_u32_be(GrayArena *arena, uint32_t val) { return make_bytes_reversed(arena, &val, 4); }
uint32_t gray_binary_decode_u32_le(GrayArray *bytes) { uint32_t v; memcpy(&v, bytes->data, 4); return v; }
uint32_t gray_binary_decode_u32_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
}

/* --- 64-bit signed --- */
GrayArray gray_binary_encode_i64_le(GrayArena *arena, int64_t val) { return make_bytes(arena, &val, 8); }
GrayArray gray_binary_encode_i64_be(GrayArena *arena, int64_t val) { return make_bytes_reversed(arena, &val, 8); }
int64_t gray_binary_decode_i64_le(GrayArray *bytes) { int64_t v; memcpy(&v, bytes->data, 8); return v; }
int64_t gray_binary_decode_i64_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    int64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | d[i];
    return v;
}

/* --- 64-bit unsigned --- */
GrayArray gray_binary_encode_u64_le(GrayArena *arena, uint64_t val) { return make_bytes(arena, &val, 8); }
GrayArray gray_binary_encode_u64_be(GrayArena *arena, uint64_t val) { return make_bytes_reversed(arena, &val, 8); }
uint64_t gray_binary_decode_u64_le(GrayArray *bytes) { uint64_t v; memcpy(&v, bytes->data, 8); return v; }
uint64_t gray_binary_decode_u64_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | d[i];
    return v;
}

/* --- 128-bit --- */
GrayArray gray_binary_encode_i128_le(GrayArena *arena, gray_i128 val) { return make_bytes(arena, &val, 16); }
GrayArray gray_binary_encode_i128_be(GrayArena *arena, gray_i128 val) { return make_bytes_reversed(arena, &val, 16); }
gray_i128 gray_binary_decode_i128_le(GrayArray *bytes) { gray_i128 v; memcpy(&v, bytes->data, 16); return v; }
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

GrayArray gray_binary_encode_u128_le(GrayArena *arena, gray_u128 val) { return make_bytes(arena, &val, 16); }
GrayArray gray_binary_encode_u128_be(GrayArena *arena, gray_u128 val) { return make_bytes_reversed(arena, &val, 16); }
gray_u128 gray_binary_decode_u128_le(GrayArray *bytes) { gray_u128 v; memcpy(&v, bytes->data, 16); return v; }
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

/* --- 256-bit --- */
GrayArray gray_binary_encode_i256_le(GrayArena *arena, gray_i256 val) { return make_bytes(arena, &val, 32); }
GrayArray gray_binary_encode_i256_be(GrayArena *arena, gray_i256 val) { return make_bytes_reversed(arena, &val, 32); }
gray_i256 gray_binary_decode_i256_le(GrayArray *bytes) { gray_i256 v; memcpy(&v, bytes->data, 32); return v; }
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

GrayArray gray_binary_encode_u256_le(GrayArena *arena, gray_u256 val) { return make_bytes(arena, &val, 32); }
GrayArray gray_binary_encode_u256_be(GrayArena *arena, gray_u256 val) { return make_bytes_reversed(arena, &val, 32); }
gray_u256 gray_binary_decode_u256_le(GrayArray *bytes) { gray_u256 v; memcpy(&v, bytes->data, 32); return v; }
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

/* --- Float --- */
GrayArray gray_binary_encode_f32_le(GrayArena *arena, float val) { return make_bytes(arena, &val, 4); }
GrayArray gray_binary_encode_f32_be(GrayArena *arena, float val) { return make_bytes_reversed(arena, &val, 4); }
float gray_binary_decode_f32_le(GrayArray *bytes) { float v; memcpy(&v, bytes->data, 4); return v; }
float gray_binary_decode_f32_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    uint8_t reversed[4] = {d[3], d[2], d[1], d[0]};
    float v; memcpy(&v, reversed, 4); return v;
}

GrayArray gray_binary_encode_f64_le(GrayArena *arena, double val) { return make_bytes(arena, &val, 8); }
GrayArray gray_binary_encode_f64_be(GrayArena *arena, double val) { return make_bytes_reversed(arena, &val, 8); }
double gray_binary_decode_f64_le(GrayArray *bytes) { double v; memcpy(&v, bytes->data, 8); return v; }
double gray_binary_decode_f64_be(GrayArray *bytes) {
    uint8_t *d = (uint8_t *)bytes->data;
    uint8_t reversed[8] = {d[7], d[6], d[5], d[4], d[3], d[2], d[1], d[0]};
    double v; memcpy(&v, reversed, 8); return v;
}
