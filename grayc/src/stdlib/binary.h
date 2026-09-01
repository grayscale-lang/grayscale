/*
 * binary.h — Public interface for the binary stdlib module.
 * Binary encoding/decoding for integers and floats in little-endian
 * and big-endian byte order.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_BINARY_H
#define GRAY_BINARY_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include "../runtime/bigint.h"

/*@man encode_i8
 *@module binary
 *@group 8-bit
 *@sig encode_i8(val int) -> [byte]
 *@desc Encodes a signed 8-bit integer as a 1-byte array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i8(-42)
 *@end
 */

/*@man decode_i8
 *@module binary
 *@group 8-bit
 *@sig decode_i8(bytes [byte]) -> int
 *@desc Decodes a 1-byte array as a signed 8-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i8(-42)
 *   mut val int = binary.decode_i8(bytes)
 *@end
 */

/*@man encode_u8
 *@module binary
 *@group 8-bit
 *@sig encode_u8(val int) -> [byte]
 *@desc Encodes an unsigned 8-bit integer as a 1-byte array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u8(200)
 *@end
 */

/*@man decode_u8
 *@module binary
 *@group 8-bit
 *@sig decode_u8(bytes [byte]) -> int
 *@desc Decodes a 1-byte array as an unsigned 8-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u8(200)
 *   mut val int = binary.decode_u8(bytes)
 *@end
 */

/*@man encode_i16_le
 *@module binary
 *@group 16-bit
 *@sig encode_i16_le(val int) -> [byte]
 *@desc Encodes a signed 16-bit integer as a 2-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i16_le(-1000)
 *@end
 */

/*@man decode_i16_le
 *@module binary
 *@group 16-bit
 *@sig decode_i16_le(bytes [byte]) -> int
 *@desc Decodes a 2-byte little-endian array as a signed 16-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i16_le(-1000)
 *   mut val int = binary.decode_i16_le(bytes)
 *@end
 */

/*@man encode_i16_be
 *@module binary
 *@group 16-bit
 *@sig encode_i16_be(val int) -> [byte]
 *@desc Encodes a signed 16-bit integer as a 2-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i16_be(-1000)
 *@end
 */

/*@man decode_i16_be
 *@module binary
 *@group 16-bit
 *@sig decode_i16_be(bytes [byte]) -> int
 *@desc Decodes a 2-byte big-endian array as a signed 16-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i16_be(-1000)
 *   mut val int = binary.decode_i16_be(bytes)
 *@end
 */

/*@man encode_u16_le
 *@module binary
 *@group 16-bit
 *@sig encode_u16_le(val int) -> [byte]
 *@desc Encodes an unsigned 16-bit integer as a 2-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u16_le(1000)
 *@end
 */

/*@man decode_u16_le
 *@module binary
 *@group 16-bit
 *@sig decode_u16_le(bytes [byte]) -> int
 *@desc Decodes a 2-byte little-endian array as an unsigned 16-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u16_le(1000)
 *   mut val int = binary.decode_u16_le(bytes)
 *@end
 */

/*@man encode_u16_be
 *@module binary
 *@group 16-bit
 *@sig encode_u16_be(val int) -> [byte]
 *@desc Encodes an unsigned 16-bit integer as a 2-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u16_be(1000)
 *@end
 */

/*@man decode_u16_be
 *@module binary
 *@group 16-bit
 *@sig decode_u16_be(bytes [byte]) -> int
 *@desc Decodes a 2-byte big-endian array as an unsigned 16-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u16_be(1000)
 *   mut val int = binary.decode_u16_be(bytes)
 *@end
 */

/*@man encode_i32_le
 *@module binary
 *@group 32-bit
 *@sig encode_i32_le(val int) -> [byte]
 *@desc Encodes a signed 32-bit integer as a 4-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i32_le(-1000)
 *@end
 */

/*@man decode_i32_le
 *@module binary
 *@group 32-bit
 *@sig decode_i32_le(bytes [byte]) -> int
 *@desc Decodes a 4-byte little-endian array as a signed 32-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i32_le(-1000)
 *   mut val int = binary.decode_i32_le(bytes)
 *@end
 */

/*@man encode_i32_be
 *@module binary
 *@group 32-bit
 *@sig encode_i32_be(val int) -> [byte]
 *@desc Encodes a signed 32-bit integer as a 4-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i32_be(-1000)
 *@end
 */

/*@man decode_i32_be
 *@module binary
 *@group 32-bit
 *@sig decode_i32_be(bytes [byte]) -> int
 *@desc Decodes a 4-byte big-endian array as a signed 32-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i32_be(-1000)
 *   mut val int = binary.decode_i32_be(bytes)
 *@end
 */

/*@man encode_u32_le
 *@module binary
 *@group 32-bit
 *@sig encode_u32_le(val int) -> [byte]
 *@desc Encodes an unsigned 32-bit integer as a 4-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u32_le(1000)
 *@end
 */

/*@man decode_u32_le
 *@module binary
 *@group 32-bit
 *@sig decode_u32_le(bytes [byte]) -> int
 *@desc Decodes a 4-byte little-endian array as an unsigned 32-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u32_le(1000)
 *   mut val int = binary.decode_u32_le(bytes)
 *@end
 */

/*@man encode_u32_be
 *@module binary
 *@group 32-bit
 *@sig encode_u32_be(val int) -> [byte]
 *@desc Encodes an unsigned 32-bit integer as a 4-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u32_be(1000)
 *@end
 */

/*@man decode_u32_be
 *@module binary
 *@group 32-bit
 *@sig decode_u32_be(bytes [byte]) -> int
 *@desc Decodes a 4-byte big-endian array as an unsigned 32-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u32_be(1000)
 *   mut val int = binary.decode_u32_be(bytes)
 *@end
 */

/*@man encode_i64_le
 *@module binary
 *@group 64-bit
 *@sig encode_i64_le(val int) -> [byte]
 *@desc Encodes a signed 64-bit integer as an 8-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i64_le(-1000)
 *@end
 */

/*@man decode_i64_le
 *@module binary
 *@group 64-bit
 *@sig decode_i64_le(bytes [byte]) -> int
 *@desc Decodes an 8-byte little-endian array as a signed 64-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i64_le(-1000)
 *   mut val int = binary.decode_i64_le(bytes)
 *@end
 */

/*@man encode_i64_be
 *@module binary
 *@group 64-bit
 *@sig encode_i64_be(val int) -> [byte]
 *@desc Encodes a signed 64-bit integer as an 8-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i64_be(-1000)
 *@end
 */

/*@man decode_i64_be
 *@module binary
 *@group 64-bit
 *@sig decode_i64_be(bytes [byte]) -> int
 *@desc Decodes an 8-byte big-endian array as a signed 64-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i64_be(-1000)
 *   mut val int = binary.decode_i64_be(bytes)
 *@end
 */

/*@man encode_u64_le
 *@module binary
 *@group 64-bit
 *@sig encode_u64_le(val int) -> [byte]
 *@desc Encodes an unsigned 64-bit integer as an 8-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u64_le(1000)
 *@end
 */

/*@man decode_u64_le
 *@module binary
 *@group 64-bit
 *@sig decode_u64_le(bytes [byte]) -> int
 *@desc Decodes an 8-byte little-endian array as an unsigned 64-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u64_le(1000)
 *   mut val int = binary.decode_u64_le(bytes)
 *@end
 */

/*@man encode_u64_be
 *@module binary
 *@group 64-bit
 *@sig encode_u64_be(val int) -> [byte]
 *@desc Encodes an unsigned 64-bit integer as an 8-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u64_be(1000)
 *@end
 */

/*@man decode_u64_be
 *@module binary
 *@group 64-bit
 *@sig decode_u64_be(bytes [byte]) -> int
 *@desc Decodes an 8-byte big-endian array as an unsigned 64-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u64_be(1000)
 *   mut val int = binary.decode_u64_be(bytes)
 *@end
 */

/*@man encode_i128_le
 *@module binary
 *@group 128-bit
 *@sig encode_i128_le(val i128) -> [byte]
 *@desc Encodes a signed 128-bit integer as a 16-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i128_le(i128(-1000))
 *@end
 */

/*@man decode_i128_le
 *@module binary
 *@group 128-bit
 *@sig decode_i128_le(bytes [byte]) -> i128
 *@desc Decodes a 16-byte little-endian array as a signed 128-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i128_le(i128(-1000))
 *   if binary.decode_i128_le(bytes) == i128(-1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_i128_be
 *@module binary
 *@group 128-bit
 *@sig encode_i128_be(val i128) -> [byte]
 *@desc Encodes a signed 128-bit integer as a 16-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i128_be(i128(-1000))
 *@end
 */

/*@man decode_i128_be
 *@module binary
 *@group 128-bit
 *@sig decode_i128_be(bytes [byte]) -> i128
 *@desc Decodes a 16-byte big-endian array as a signed 128-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i128_be(i128(-1000))
 *   if binary.decode_i128_be(bytes) == i128(-1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_u128_le
 *@module binary
 *@group 128-bit
 *@sig encode_u128_le(val u128) -> [byte]
 *@desc Encodes an unsigned 128-bit integer as a 16-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u128_le(u128(1000))
 *@end
 */

/*@man decode_u128_le
 *@module binary
 *@group 128-bit
 *@sig decode_u128_le(bytes [byte]) -> u128
 *@desc Decodes a 16-byte little-endian array as an unsigned 128-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u128_le(u128(1000))
 *   if binary.decode_u128_le(bytes) == u128(1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_u128_be
 *@module binary
 *@group 128-bit
 *@sig encode_u128_be(val u128) -> [byte]
 *@desc Encodes an unsigned 128-bit integer as a 16-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u128_be(u128(1000))
 *@end
 */

/*@man decode_u128_be
 *@module binary
 *@group 128-bit
 *@sig decode_u128_be(bytes [byte]) -> u128
 *@desc Decodes a 16-byte big-endian array as an unsigned 128-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u128_be(u128(1000))
 *   if binary.decode_u128_be(bytes) == u128(1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_i256_le
 *@module binary
 *@group 256-bit
 *@sig encode_i256_le(val i256) -> [byte]
 *@desc Encodes a signed 256-bit integer as a 32-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i256_le(i256(-1000))
 *@end
 */

/*@man decode_i256_le
 *@module binary
 *@group 256-bit
 *@sig decode_i256_le(bytes [byte]) -> i256
 *@desc Decodes a 32-byte little-endian array as a signed 256-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i256_le(i256(-1000))
 *   if binary.decode_i256_le(bytes) == i256(-1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_i256_be
 *@module binary
 *@group 256-bit
 *@sig encode_i256_be(val i256) -> [byte]
 *@desc Encodes a signed 256-bit integer as a 32-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i256_be(i256(-1000))
 *@end
 */

/*@man decode_i256_be
 *@module binary
 *@group 256-bit
 *@sig decode_i256_be(bytes [byte]) -> i256
 *@desc Decodes a 32-byte big-endian array as a signed 256-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_i256_be(i256(-1000))
 *   if binary.decode_i256_be(bytes) == i256(-1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_u256_le
 *@module binary
 *@group 256-bit
 *@sig encode_u256_le(val u256) -> [byte]
 *@desc Encodes an unsigned 256-bit integer as a 32-byte little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u256_le(u256(1000))
 *@end
 */

/*@man decode_u256_le
 *@module binary
 *@group 256-bit
 *@sig decode_u256_le(bytes [byte]) -> u256
 *@desc Decodes a 32-byte little-endian array as an unsigned 256-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u256_le(u256(1000))
 *   if binary.decode_u256_le(bytes) == u256(1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_u256_be
 *@module binary
 *@group 256-bit
 *@sig encode_u256_be(val u256) -> [byte]
 *@desc Encodes an unsigned 256-bit integer as a 32-byte big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u256_be(u256(1000))
 *@end
 */

/*@man decode_u256_be
 *@module binary
 *@group 256-bit
 *@sig decode_u256_be(bytes [byte]) -> u256
 *@desc Decodes a 32-byte big-endian array as an unsigned 256-bit integer.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_u256_be(u256(1000))
 *   if binary.decode_u256_be(bytes) == u256(1000) { println("round-trip ok") }
 *@end
 */

/*@man encode_f32_le
 *@module binary
 *@group Floats
 *@sig encode_f32_le(val float) -> [byte]
 *@desc Encodes a float as a 4-byte IEEE 754 little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f32_le(3.14)
 *@end
 */

/*@man decode_f32_le
 *@module binary
 *@group Floats
 *@sig decode_f32_le(bytes [byte]) -> float
 *@desc Decodes a 4-byte IEEE 754 little-endian array as a float.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f32_le(3.14)
 *   mut val float = binary.decode_f32_le(bytes)
 *@end
 */

/*@man encode_f32_be
 *@module binary
 *@group Floats
 *@sig encode_f32_be(val float) -> [byte]
 *@desc Encodes a float as a 4-byte IEEE 754 big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f32_be(3.14)
 *@end
 */

/*@man decode_f32_be
 *@module binary
 *@group Floats
 *@sig decode_f32_be(bytes [byte]) -> float
 *@desc Decodes a 4-byte IEEE 754 big-endian array as a float.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f32_be(3.14)
 *   mut val float = binary.decode_f32_be(bytes)
 *@end
 */

/*@man encode_f64_le
 *@module binary
 *@group Floats
 *@sig encode_f64_le(val float) -> [byte]
 *@desc Encodes a float as an 8-byte IEEE 754 little-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f64_le(3.14)
 *@end
 */

/*@man decode_f64_le
 *@module binary
 *@group Floats
 *@sig decode_f64_le(bytes [byte]) -> float
 *@desc Decodes an 8-byte IEEE 754 little-endian array as a float.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f64_le(3.14)
 *   mut val float = binary.decode_f64_le(bytes)
 *@end
 */

/*@man encode_f64_be
 *@module binary
 *@group Floats
 *@sig encode_f64_be(val float) -> [byte]
 *@desc Encodes a float as an 8-byte IEEE 754 big-endian array.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f64_be(3.14)
 *@end
 */

/*@man decode_f64_be
 *@module binary
 *@group Floats
 *@sig decode_f64_be(bytes [byte]) -> float
 *@desc Decodes an 8-byte IEEE 754 big-endian array as a float.
 *@example
 *   import @binary
 *   mut bytes [byte] = binary.encode_f64_be(3.14)
 *   mut val float = binary.decode_f64_be(bytes)
 *@end
 */

/* --- 8-bit --- */
GrayArray gray_binary_encode_i8(GrayArena *arena, int8_t val);
GrayArray gray_binary_encode_u8(GrayArena *arena, uint8_t val);
int8_t gray_binary_decode_i8(GrayArray *bytes, const char *file, int line);
uint8_t gray_binary_decode_u8(GrayArray *bytes, const char *file, int line);

/* --- 16-bit signed --- */
GrayArray gray_binary_encode_i16_le(GrayArena *arena, int16_t val);
GrayArray gray_binary_encode_i16_be(GrayArena *arena, int16_t val);
int16_t gray_binary_decode_i16_le(GrayArray *bytes, const char *file, int line);
int16_t gray_binary_decode_i16_be(GrayArray *bytes, const char *file, int line);

/* --- 16-bit unsigned --- */
GrayArray gray_binary_encode_u16_le(GrayArena *arena, uint16_t val);
GrayArray gray_binary_encode_u16_be(GrayArena *arena, uint16_t val);
uint16_t gray_binary_decode_u16_le(GrayArray *bytes, const char *file, int line);
uint16_t gray_binary_decode_u16_be(GrayArray *bytes, const char *file, int line);

/* --- 32-bit signed --- */
GrayArray gray_binary_encode_i32_le(GrayArena *arena, int32_t val);
GrayArray gray_binary_encode_i32_be(GrayArena *arena, int32_t val);
int32_t gray_binary_decode_i32_le(GrayArray *bytes, const char *file, int line);
int32_t gray_binary_decode_i32_be(GrayArray *bytes, const char *file, int line);

/* --- 32-bit unsigned --- */
GrayArray gray_binary_encode_u32_le(GrayArena *arena, uint32_t val);
GrayArray gray_binary_encode_u32_be(GrayArena *arena, uint32_t val);
uint32_t gray_binary_decode_u32_le(GrayArray *bytes, const char *file, int line);
uint32_t gray_binary_decode_u32_be(GrayArray *bytes, const char *file, int line);

/* --- 64-bit signed --- */
GrayArray gray_binary_encode_i64_le(GrayArena *arena, int64_t val);
GrayArray gray_binary_encode_i64_be(GrayArena *arena, int64_t val);
int64_t gray_binary_decode_i64_le(GrayArray *bytes, const char *file, int line);
int64_t gray_binary_decode_i64_be(GrayArray *bytes, const char *file, int line);

/* --- 64-bit unsigned --- */
GrayArray gray_binary_encode_u64_le(GrayArena *arena, uint64_t val);
GrayArray gray_binary_encode_u64_be(GrayArena *arena, uint64_t val);
uint64_t gray_binary_decode_u64_le(GrayArray *bytes, const char *file, int line);
uint64_t gray_binary_decode_u64_be(GrayArray *bytes, const char *file, int line);

/* --- 128-bit --- */
GrayArray gray_binary_encode_i128_le(GrayArena *arena, gray_i128 val);
GrayArray gray_binary_encode_i128_be(GrayArena *arena, gray_i128 val);
gray_i128 gray_binary_decode_i128_le(GrayArray *bytes, const char *file, int line);
gray_i128 gray_binary_decode_i128_be(GrayArray *bytes, const char *file, int line);
GrayArray gray_binary_encode_u128_le(GrayArena *arena, gray_u128 val);
GrayArray gray_binary_encode_u128_be(GrayArena *arena, gray_u128 val);
gray_u128 gray_binary_decode_u128_le(GrayArray *bytes, const char *file, int line);
gray_u128 gray_binary_decode_u128_be(GrayArray *bytes, const char *file, int line);

/* --- 256-bit --- */
GrayArray gray_binary_encode_i256_le(GrayArena *arena, gray_i256 val);
GrayArray gray_binary_encode_i256_be(GrayArena *arena, gray_i256 val);
gray_i256 gray_binary_decode_i256_le(GrayArray *bytes, const char *file, int line);
gray_i256 gray_binary_decode_i256_be(GrayArray *bytes, const char *file, int line);
GrayArray gray_binary_encode_u256_le(GrayArena *arena, gray_u256 val);
GrayArray gray_binary_encode_u256_be(GrayArena *arena, gray_u256 val);
gray_u256 gray_binary_decode_u256_le(GrayArray *bytes, const char *file, int line);
gray_u256 gray_binary_decode_u256_be(GrayArray *bytes, const char *file, int line);

/* --- Float encode/decode --- */
GrayArray gray_binary_encode_f32_le(GrayArena *arena, float val);
GrayArray gray_binary_encode_f32_be(GrayArena *arena, float val);
float gray_binary_decode_f32_le(GrayArray *bytes, const char *file, int line);
float gray_binary_decode_f32_be(GrayArray *bytes, const char *file, int line);
GrayArray gray_binary_encode_f64_le(GrayArena *arena, double val);
GrayArray gray_binary_encode_f64_be(GrayArena *arena, double val);
double gray_binary_decode_f64_le(GrayArray *bytes, const char *file, int line);
double gray_binary_decode_f64_be(GrayArray *bytes, const char *file, int line);

#endif
