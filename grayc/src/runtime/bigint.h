/*
 * bigint.h — Wide integer types (i128, u128, i256, u256) for
 * the Grayscale runtime. Portable struct-based implementation backed
 * by uint64_t limbs with inline arithmetic, comparison, and printing.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_BIGINT_H
#define GRAY_BIGINT_H

#include "../runtime/runtime.h"
#include <inttypes.h>

/* --- Type Definitions --- */

typedef struct { uint64_t lo; int64_t hi; } gray_i128;
typedef struct { uint64_t lo; uint64_t hi; } gray_u128;
typedef struct { uint64_t w[4]; } gray_i256;  /* w[0]=lo ... w[3]=hi */
typedef struct { uint64_t w[4]; } gray_u256;

/* Low 32 bits of a uint64_t — the halves the schoolbook multiplies split on. */
#define LOW32_MASK 0xFFFFFFFFu

/* --- Max decimal digits for string rendering --- */
#define I128_MAX_DIGITS     21
#define U128_MAX_DIGITS     21
#define I256_MAX_DIGITS     40
#define U256_MAX_DIGITS     80

/* --- Zero Constants --- */

#define GRAY_I128_ZERO  ((gray_i128){0, 0})
#define GRAY_U128_ZERO  ((gray_u128){0, 0})
#define GRAY_I256_ZERO  ((gray_i256){{0, 0, 0, 0}})
#define GRAY_U256_ZERO  ((gray_u256){{0, 0, 0, 0}})

/* --- Constructors --- */

static inline gray_i128 gray_i128_from_i64(int64_t value) {
    gray_i128 result;
    result.lo = (uint64_t)value;
    result.hi = (value < 0) ? -1 : 0;
    return result;
}

/* Widen an unsigned 64-bit value into a signed wide integer. i128 and i256
 * represent every uint64_t exactly, so the high words stay zero. from_i64
 * cannot serve here: it sign-extends, turning any value above INT64_MAX
 * negative. */
static inline gray_i128 gray_i128_from_u64(uint64_t value) {
    gray_i128 result;
    result.lo = value;
    result.hi = 0;
    return result;
}

static inline gray_u128 gray_u128_from_u64(uint64_t value) {
    gray_u128 result;
    result.lo = value;
    result.hi = 0;
    return result;
}

static inline gray_i256 gray_i256_from_i64(int64_t value) {
    gray_i256 result;
    result.w[0] = (uint64_t)value;
    int64_t sign = (value < 0) ? -1 : 0;
    result.w[1] = (uint64_t)sign;
    result.w[2] = (uint64_t)sign;
    result.w[3] = (uint64_t)sign;
    return result;
}

static inline gray_i256 gray_i256_from_u64(uint64_t value) {
    gray_i256 result;
    result.w[0] = value;
    result.w[1] = 0;
    result.w[2] = 0;
    result.w[3] = 0;
    return result;
}

static inline gray_u256 gray_u256_from_u64(uint64_t value) {
    gray_u256 result;
    result.w[0] = value;
    result.w[1] = 0;
    result.w[2] = 0;
    result.w[3] = 0;
    return result;
}

/* --- Size Casting (range-checked) --- */

static inline int64_t gray_i128_to_i64(gray_i128 value, const char *file, int line) {
    if (value.hi != ((int64_t)value.lo >> 63)) {
        gray_panic_code_at(file, line, "P0093", "cast from i128 failed; value is outside the representable range of int64");
    }
    return (int64_t)value.lo;
}

static inline uint64_t gray_i128_to_u64(gray_i128 value, const char *file, int line) {
    if (value.hi != 0) {
        gray_panic_code_at(file, line, "P0094", "cast from i128 failed; value is negative or outside the representable range of uint64");
    }
    return value.lo;
}

static inline int64_t gray_u128_to_i64(gray_u128 value, const char *file, int line) {
    if (value.hi != 0 || value.lo > (uint64_t)INT64_MAX) {
        gray_panic_code_at(file, line, "P0095", "cast from u128 failed; value exceeds the representable range of int64");
    }
    return (int64_t)value.lo;
}

static inline uint64_t gray_u128_to_u64(gray_u128 value, const char *file, int line) {
    if (value.hi != 0) {
        gray_panic_code_at(file, line, "P0096", "cast from u128 failed; value exceeds the representable range of uint64");
    }
    return value.lo;
}

static inline int64_t gray_i256_to_i64(gray_i256 value, const char *file, int line) {
    uint64_t sign_ext = (uint64_t)((int64_t)value.w[0] >> 63);
    if (value.w[1] != sign_ext || value.w[2] != sign_ext || value.w[3] != sign_ext) {
        gray_panic_code_at(file, line, "P0097", "cast from i256 failed; value is outside the representable range of int64");
    }
    return (int64_t)value.w[0];
}

static inline uint64_t gray_i256_to_u64(gray_i256 value, const char *file, int line) {
    if (value.w[1] != 0 || value.w[2] != 0 || value.w[3] != 0) {
        gray_panic_code_at(file, line, "P0098", "cast from i256 failed; value is negative or outside the representable range of uint64");
    }
    return value.w[0];
}

static inline int64_t gray_u256_to_i64(gray_u256 value, const char *file, int line) {
    if (value.w[1] != 0 || value.w[2] != 0 || value.w[3] != 0 || value.w[0] > (uint64_t)INT64_MAX) {
        gray_panic_code_at(file, line, "P0099", "cast from u256 failed; value exceeds the representable range of int64");
    }
    return (int64_t)value.w[0];
}

static inline uint64_t gray_u256_to_u64(gray_u256 value, const char *file, int line) {
    if (value.w[1] != 0 || value.w[2] != 0 || value.w[3] != 0) {
        gray_panic_code_at(file, line, "P0100", "cast from u256 failed; value exceeds the representable range of uint64");
    }
    return value.w[0];
}

static inline gray_u128 gray_u128_from_i128(gray_i128 value) {
    gray_u128 result;
    result.lo = value.lo;
    result.hi = (uint64_t)value.hi;
    return result;
}

static inline gray_i128 gray_i128_from_u128(gray_u128 value) {
    gray_i128 result;
    result.lo = value.lo;
    result.hi = (int64_t)value.hi;
    return result;
}

static inline gray_i256 gray_i256_from_i128(gray_i128 value) {
    gray_i256 result;
    result.w[0] = value.lo;
    result.w[1] = (uint64_t)value.hi;
    int64_t sign = (value.hi < 0) ? -1 : 0;
    result.w[2] = (uint64_t)sign;
    result.w[3] = (uint64_t)sign;
    return result;
}

static inline gray_u256 gray_u256_from_u128(gray_u128 value) {
    gray_u256 result;
    result.w[0] = value.lo;
    result.w[1] = value.hi;
    result.w[2] = 0;
    result.w[3] = 0;
    return result;
}

static inline gray_i128 gray_i128_from_i256(gray_i256 value) {
    gray_i128 result;
    result.lo = value.w[0];
    result.hi = (int64_t)value.w[1];
    return result;
}

static inline gray_u128 gray_u128_from_u256(gray_u256 value) {
    gray_u128 result;
    result.lo = value.w[0];
    result.hi = value.w[1];
    return result;
}

/* --- i128 Arithmetic --- */

/* The hi-limb arithmetic must be unsigned: these wrap by design, and the
 * *_checked wrappers detect overflow from the wrapped sign afterward. Done
 * in int64_t, the wrap is signed-overflow UB, and GCC at -O2 uses that to
 * prove the panic branches unreachable and delete them. */
static inline gray_i128 gray_i128_add(gray_i128 left, gray_i128 right) {
    gray_i128 result;
    result.lo = left.lo + right.lo;
    result.hi = (int64_t)((uint64_t)left.hi + (uint64_t)right.hi + (result.lo < left.lo ? 1 : 0));
    return result;
}

static inline gray_i128 gray_i128_sub(gray_i128 left, gray_i128 right) {
    gray_i128 result;
    result.lo = left.lo - right.lo;
    result.hi = (int64_t)((uint64_t)left.hi - (uint64_t)right.hi - (left.lo < right.lo ? 1 : 0));
    return result;
}

static inline gray_i128 gray_i128_neg(gray_i128 value) {
    gray_i128 result;
    result.lo = ~value.lo + 1;
    result.hi = (int64_t)(~(uint64_t)value.hi + (result.lo == 0 ? 1 : 0));
    return result;
}

static inline gray_i128 gray_i128_mul(gray_i128 left, gray_i128 right) {
    /* Schoolbook multiplication on 64-bit halves */
    uint64_t a_lo = left.lo, b_lo = right.lo;
    uint64_t a_hi = (uint64_t)left.hi, b_hi = (uint64_t)right.hi;

    /* Split each 64-bit value into 32-bit halves for overflow-safe multiply */
    uint64_t a0 = a_lo & LOW32_MASK, a1 = a_lo >> 32;
    uint64_t b0 = b_lo & LOW32_MASK, b1 = b_lo >> 32;

    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;

    uint64_t mid = p01 + (p00 >> 32);
    uint64_t carry = ((mid & LOW32_MASK) + p10) >> 32;

    gray_i128 result;
    result.lo = a_lo * b_lo;
    result.hi = (int64_t)(p11 + (mid >> 32) + carry + a_lo * b_hi + a_hi * b_lo);
    return result;
}

/* Division helper: unsigned 128-bit divide */
static inline void gray_u128_divmod(gray_u128 dividend, gray_u128 divisor,
                                    gray_u128 *quotient_out, gray_u128 *remainder_out,
                                    const char *file, int line) {
    if (divisor.hi == 0 && divisor.lo == 0) {
        gray_panic_code_at(file, line, "P0078", "division by zero");
    }
    if (dividend.hi == 0 && divisor.hi == 0) {
        /* Both fit in 64 bits */
        quotient_out->hi = 0; quotient_out->lo = dividend.lo / divisor.lo;
        remainder_out->hi = 0; remainder_out->lo = dividend.lo % divisor.lo;
        return;
    }
    /* Binary long division */
    gray_u128 quotient = GRAY_U128_ZERO;
    gray_u128 remainder = GRAY_U128_ZERO;
    for (int bit = 127; bit >= 0; bit--) {
        /* remainder <<= 1 */
        remainder.hi = (remainder.hi << 1) | (remainder.lo >> 63);
        remainder.lo <<= 1;
        /* Get bit `bit` of the dividend */
        if (bit >= 64) {
            remainder.lo |= (dividend.hi >> (bit - 64)) & 1;
        } else {
            remainder.lo |= (dividend.lo >> bit) & 1;
        }
        /* if remainder >= divisor */
        if (remainder.hi > divisor.hi || (remainder.hi == divisor.hi && remainder.lo >= divisor.lo)) {
            /* remainder -= divisor */
            uint64_t borrow = (remainder.lo < divisor.lo) ? 1 : 0;
            remainder.lo -= divisor.lo;
            remainder.hi -= divisor.hi + borrow;
            /* Set bit `bit` of the quotient */
            if (bit >= 64) {
                quotient.hi |= (uint64_t)1 << (bit - 64);
            } else {
                quotient.lo |= (uint64_t)1 << bit;
            }
        }
    }
    *quotient_out = quotient;
    *remainder_out = remainder;
}

static inline gray_i128 gray_i128_div(gray_i128 left, gray_i128 right, const char *file, int line) {
    bool negate = false;
    gray_u128 mag_left, mag_right;
    if (left.hi < 0) { gray_i128 negated = gray_i128_neg(left); mag_left.lo = negated.lo; mag_left.hi = (uint64_t)negated.hi; negate = !negate; }
    else { mag_left.lo = left.lo; mag_left.hi = (uint64_t)left.hi; }
    if (right.hi < 0) { gray_i128 negated = gray_i128_neg(right); mag_right.lo = negated.lo; mag_right.hi = (uint64_t)negated.hi; negate = !negate; }
    else { mag_right.lo = right.lo; mag_right.hi = (uint64_t)right.hi; }
    gray_u128 quotient, remainder;
    gray_u128_divmod(mag_left, mag_right, &quotient, &remainder, file, line);
    gray_i128 result;
    result.lo = quotient.lo; result.hi = (int64_t)quotient.hi;
    return negate ? gray_i128_neg(result) : result;
}

static inline gray_i128 gray_i128_mod(gray_i128 left, gray_i128 right, const char *file, int line) {
    bool left_is_negative = (left.hi < 0);
    gray_u128 mag_left, mag_right;
    if (left_is_negative) { gray_i128 negated = gray_i128_neg(left); mag_left.lo = negated.lo; mag_left.hi = (uint64_t)negated.hi; }
    else { mag_left.lo = left.lo; mag_left.hi = (uint64_t)left.hi; }
    if (right.hi < 0) { gray_i128 negated = gray_i128_neg(right); mag_right.lo = negated.lo; mag_right.hi = (uint64_t)negated.hi; }
    else { mag_right.lo = right.lo; mag_right.hi = (uint64_t)right.hi; }
    gray_u128 quotient, remainder;
    gray_u128_divmod(mag_left, mag_right, &quotient, &remainder, file, line);
    gray_i128 result;
    result.lo = remainder.lo; result.hi = (int64_t)remainder.hi;
    return left_is_negative ? gray_i128_neg(result) : result;
}

/* --- i128 Comparison --- */

static inline bool gray_i128_eq(gray_i128 left, gray_i128 right) { return left.lo == right.lo && left.hi == right.hi; }
static inline bool gray_i128_ne(gray_i128 left, gray_i128 right) { return left.lo != right.lo || left.hi != right.hi; }
static inline bool gray_i128_lt(gray_i128 left, gray_i128 right) {
    return left.hi < right.hi || (left.hi == right.hi && left.lo < right.lo);
}
static inline bool gray_i128_gt(gray_i128 left, gray_i128 right) { return gray_i128_lt(right, left); }
static inline bool gray_i128_le(gray_i128 left, gray_i128 right) { return !gray_i128_gt(left, right); }
static inline bool gray_i128_ge(gray_i128 left, gray_i128 right) { return !gray_i128_lt(left, right); }

/* --- u128 Arithmetic --- */

static inline gray_u128 gray_u128_add(gray_u128 left, gray_u128 right) {
    gray_u128 result;
    result.lo = left.lo + right.lo;
    result.hi = left.hi + right.hi + (result.lo < left.lo ? 1 : 0);
    return result;
}

static inline gray_u128 gray_u128_sub(gray_u128 left, gray_u128 right) {
    gray_u128 result;
    result.lo = left.lo - right.lo;
    result.hi = left.hi - right.hi - (left.lo < right.lo ? 1 : 0);
    return result;
}

static inline gray_u128 gray_u128_mul(gray_u128 left, gray_u128 right) {
    uint64_t a0 = left.lo & LOW32_MASK, a1 = left.lo >> 32;
    uint64_t b0 = right.lo & LOW32_MASK, b1 = right.lo >> 32;

    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;

    uint64_t mid = p01 + (p00 >> 32);
    uint64_t carry = ((mid & LOW32_MASK) + p10) >> 32;

    gray_u128 result;
    result.lo = left.lo * right.lo;
    result.hi = p11 + (mid >> 32) + carry + left.lo * right.hi + left.hi * right.lo;
    return result;
}

static inline gray_u128 gray_u128_div(gray_u128 left, gray_u128 right, const char *file, int line) {
    gray_u128 quotient, remainder;
    gray_u128_divmod(left, right, &quotient, &remainder, file, line);
    return quotient;
}

static inline gray_u128 gray_u128_mod(gray_u128 left, gray_u128 right, const char *file, int line) {
    gray_u128 quotient, remainder;
    gray_u128_divmod(left, right, &quotient, &remainder, file, line);
    return remainder;
}

/* --- u128 Comparison --- */

static inline bool gray_u128_eq(gray_u128 left, gray_u128 right) { return left.lo == right.lo && left.hi == right.hi; }
static inline bool gray_u128_ne(gray_u128 left, gray_u128 right) { return left.lo != right.lo || left.hi != right.hi; }
static inline bool gray_u128_lt(gray_u128 left, gray_u128 right) {
    return left.hi < right.hi || (left.hi == right.hi && left.lo < right.lo);
}
static inline bool gray_u128_gt(gray_u128 left, gray_u128 right) { return gray_u128_lt(right, left); }
static inline bool gray_u128_le(gray_u128 left, gray_u128 right) { return !gray_u128_gt(left, right); }
static inline bool gray_u128_ge(gray_u128 left, gray_u128 right) { return !gray_u128_lt(left, right); }

/* --- i256 Arithmetic --- */

static inline gray_i256 gray_i256_add(gray_i256 left, gray_i256 right) {
    gray_i256 result;
    uint64_t carry = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t sum = left.w[i] + right.w[i] + carry;
        carry = (sum < left.w[i] || (carry && sum == left.w[i])) ? 1 : 0;
        result.w[i] = sum;
    }
    return result;
}

static inline gray_i256 gray_i256_sub(gray_i256 left, gray_i256 right) {
    gray_i256 result;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t diff = left.w[i] - right.w[i] - borrow;
        borrow = (left.w[i] < right.w[i]) || (left.w[i] - right.w[i] < borrow) ? 1 : 0;
        result.w[i] = diff;
    }
    return result;
}

static inline gray_i256 gray_i256_neg(gray_i256 value) {
    gray_i256 result;
    uint64_t carry = 1;
    for (int i = 0; i < 4; i++) {
        uint64_t sum = ~value.w[i] + carry;
        carry = (sum < ~value.w[i]) ? 1 : 0;
        result.w[i] = sum;
    }
    return result;
}

static inline gray_i256 gray_i256_mul(gray_i256 left, gray_i256 right) {
    /* Schoolbook multiplication — only need lower 256 bits */
    gray_i256 result = GRAY_I256_ZERO;
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4 - i; j++) {
            /* Multiply left.w[j] * right.w[i] and add to result.w[i+j] */
            uint64_t a_lo = left.w[j] & LOW32_MASK, a_hi = left.w[j] >> 32;
            uint64_t b_lo = right.w[i] & LOW32_MASK, b_hi = right.w[i] >> 32;
            uint64_t p00 = a_lo * b_lo;
            uint64_t p01 = a_lo * b_hi;
            uint64_t p10 = a_hi * b_lo;
            uint64_t p11 = a_hi * b_hi;
            uint64_t mid = p01 + (p00 >> 32);
            uint64_t lo = (p00 & LOW32_MASK) | ((mid & LOW32_MASK) << 32) + (p10 << 32);

            /* Simpler approach: just use the truncating product */
            lo = left.w[j] * right.w[i];
            uint64_t hi_part = p11 + (mid >> 32) + (((mid & LOW32_MASK) + p10) >> 32);

            uint64_t old = result.w[i + j];
            result.w[i + j] = old + lo + carry;
            carry = hi_part + (result.w[i + j] < old + lo ? 1 : 0);
            if (old + lo < old) carry++;
        }
    }
    return result;
}

/* 256-bit unsigned divmod (binary long division) */
static inline void gray_u256_divmod(gray_u256 dividend, gray_u256 divisor,
                                    gray_u256 *quotient_out, gray_u256 *remainder_out,
                                    const char *file, int line) {
    int divisor_zero = (divisor.w[0] == 0 && divisor.w[1] == 0 && divisor.w[2] == 0 && divisor.w[3] == 0);
    if (divisor_zero) { gray_panic_code_at(file, line, "P0078", "division by zero"); }
    if (dividend.w[1] == 0 && dividend.w[2] == 0 && dividend.w[3] == 0 &&
        divisor.w[1] == 0 && divisor.w[2] == 0 && divisor.w[3] == 0) {
        *quotient_out = GRAY_U256_ZERO; quotient_out->w[0] = dividend.w[0] / divisor.w[0];
        *remainder_out = GRAY_U256_ZERO; remainder_out->w[0] = dividend.w[0] % divisor.w[0];
        return;
    }
    gray_u256 quotient = GRAY_U256_ZERO;
    gray_u256 remainder = GRAY_U256_ZERO;
    for (int i = 255; i >= 0; i--) {
        /* remainder <<= 1 */
        remainder.w[3] = (remainder.w[3] << 1) | (remainder.w[2] >> 63);
        remainder.w[2] = (remainder.w[2] << 1) | (remainder.w[1] >> 63);
        remainder.w[1] = (remainder.w[1] << 1) | (remainder.w[0] >> 63);
        remainder.w[0] <<= 1;
        /* Get bit i of the dividend */
        int word = i / 64;
        int bit = i % 64;
        remainder.w[0] |= (dividend.w[word] >> bit) & 1;
        /* if remainder >= divisor */
        bool remainder_ge_divisor = false;
        for (int k = 3; k >= 0; k--) {
            if (remainder.w[k] > divisor.w[k]) { remainder_ge_divisor = true; break; }
            if (remainder.w[k] < divisor.w[k]) break;
            if (k == 0) remainder_ge_divisor = true;
        }
        if (remainder_ge_divisor) {
            /* remainder -= divisor */
            uint64_t borrow = 0;
            for (int k = 0; k < 4; k++) {
                uint64_t diff = remainder.w[k] - divisor.w[k] - borrow;
                borrow = (remainder.w[k] < divisor.w[k]) || (remainder.w[k] - divisor.w[k] < borrow) ? 1 : 0;
                remainder.w[k] = diff;
            }
            quotient.w[word] |= (uint64_t)1 << bit;
        }
    }
    *quotient_out = quotient;
    *remainder_out = remainder;
}

static inline bool gray_i256_is_neg(gray_i256 value) { return (int64_t)value.w[3] < 0; }

static inline gray_i256 gray_i256_div(gray_i256 left, gray_i256 right, const char *file, int line) {
    bool negate = false;
    gray_u256 mag_left, mag_right;
    if (gray_i256_is_neg(left)) { gray_i256 negated = gray_i256_neg(left); memcpy(&mag_left, &negated, sizeof(mag_left)); negate = !negate; }
    else { memcpy(&mag_left, &left, sizeof(mag_left)); }
    if (gray_i256_is_neg(right)) { gray_i256 negated = gray_i256_neg(right); memcpy(&mag_right, &negated, sizeof(mag_right)); negate = !negate; }
    else { memcpy(&mag_right, &right, sizeof(mag_right)); }
    gray_u256 quotient, remainder;
    gray_u256_divmod(mag_left, mag_right, &quotient, &remainder, file, line);
    gray_i256 result; memcpy(&result, &quotient, sizeof(result));
    return negate ? gray_i256_neg(result) : result;
}

static inline gray_i256 gray_i256_mod(gray_i256 left, gray_i256 right, const char *file, int line) {
    bool left_is_negative = gray_i256_is_neg(left);
    gray_u256 mag_left, mag_right;
    if (left_is_negative) { gray_i256 negated = gray_i256_neg(left); memcpy(&mag_left, &negated, sizeof(mag_left)); }
    else { memcpy(&mag_left, &left, sizeof(mag_left)); }
    if (gray_i256_is_neg(right)) { gray_i256 negated = gray_i256_neg(right); memcpy(&mag_right, &negated, sizeof(mag_right)); }
    else { memcpy(&mag_right, &right, sizeof(mag_right)); }
    gray_u256 quotient, remainder;
    gray_u256_divmod(mag_left, mag_right, &quotient, &remainder, file, line);
    gray_i256 result; memcpy(&result, &remainder, sizeof(result));
    return left_is_negative ? gray_i256_neg(result) : result;
}

/* --- i256 Comparison --- */

static inline bool gray_i256_eq(gray_i256 left, gray_i256 right) {
    return left.w[0] == right.w[0] && left.w[1] == right.w[1] && left.w[2] == right.w[2] && left.w[3] == right.w[3];
}
static inline bool gray_i256_ne(gray_i256 left, gray_i256 right) { return !gray_i256_eq(left, right); }
static inline bool gray_i256_lt(gray_i256 left, gray_i256 right) {
    if ((int64_t)left.w[3] != (int64_t)right.w[3]) return (int64_t)left.w[3] < (int64_t)right.w[3];
    if (left.w[2] != right.w[2]) return left.w[2] < right.w[2];
    if (left.w[1] != right.w[1]) return left.w[1] < right.w[1];
    return left.w[0] < right.w[0];
}
static inline bool gray_i256_gt(gray_i256 left, gray_i256 right) { return gray_i256_lt(right, left); }
static inline bool gray_i256_le(gray_i256 left, gray_i256 right) { return !gray_i256_gt(left, right); }
static inline bool gray_i256_ge(gray_i256 left, gray_i256 right) { return !gray_i256_lt(left, right); }

/* --- u256 Arithmetic --- */

static inline gray_u256 gray_u256_add(gray_u256 left, gray_u256 right) {
    gray_u256 result;
    uint64_t carry = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t sum = left.w[i] + right.w[i] + carry;
        carry = (sum < left.w[i] || (carry && sum == left.w[i])) ? 1 : 0;
        result.w[i] = sum;
    }
    return result;
}

static inline gray_u256 gray_u256_sub(gray_u256 left, gray_u256 right) {
    gray_u256 result;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t diff = left.w[i] - right.w[i] - borrow;
        borrow = (left.w[i] < right.w[i]) || (left.w[i] - right.w[i] < borrow) ? 1 : 0;
        result.w[i] = diff;
    }
    return result;
}

static inline gray_u256 gray_u256_mul(gray_u256 left, gray_u256 right) {
    gray_u256 result = GRAY_U256_ZERO;
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4 - i; j++) {
            uint64_t a_lo = left.w[j] & LOW32_MASK, a_hi = left.w[j] >> 32;
            uint64_t b_lo = right.w[i] & LOW32_MASK, b_hi = right.w[i] >> 32;
            uint64_t p00 = a_lo * b_lo;
            uint64_t p01 = a_lo * b_hi;
            uint64_t p10 = a_hi * b_lo;
            uint64_t p11 = a_hi * b_hi;
            uint64_t mid = p01 + (p00 >> 32);
            uint64_t lo = left.w[j] * right.w[i];
            uint64_t hi_part = p11 + (mid >> 32) + (((mid & LOW32_MASK) + p10) >> 32);

            uint64_t old = result.w[i + j];
            result.w[i + j] = old + lo + carry;
            carry = hi_part + (result.w[i + j] < old + lo ? 1 : 0);
            if (old + lo < old) carry++;
        }
    }
    return result;
}

static inline gray_u256 gray_u256_div(gray_u256 left, gray_u256 right, const char *file, int line) {
    gray_u256 quotient, remainder;
    gray_u256_divmod(left, right, &quotient, &remainder, file, line);
    return quotient;
}

static inline gray_u256 gray_u256_mod(gray_u256 left, gray_u256 right, const char *file, int line) {
    gray_u256 quotient, remainder;
    gray_u256_divmod(left, right, &quotient, &remainder, file, line);
    return remainder;
}

/* --- u256 Comparison --- */

static inline bool gray_u256_eq(gray_u256 left, gray_u256 right) {
    return left.w[0] == right.w[0] && left.w[1] == right.w[1] && left.w[2] == right.w[2] && left.w[3] == right.w[3];
}
static inline bool gray_u256_ne(gray_u256 left, gray_u256 right) { return !gray_u256_eq(left, right); }
static inline bool gray_u256_lt(gray_u256 left, gray_u256 right) {
    if (left.w[3] != right.w[3]) return left.w[3] < right.w[3];
    if (left.w[2] != right.w[2]) return left.w[2] < right.w[2];
    if (left.w[1] != right.w[1]) return left.w[1] < right.w[1];
    return left.w[0] < right.w[0];
}
static inline bool gray_u256_gt(gray_u256 left, gray_u256 right) { return gray_u256_lt(right, left); }
static inline bool gray_u256_le(gray_u256 left, gray_u256 right) { return !gray_u256_gt(left, right); }
static inline bool gray_u256_ge(gray_u256 left, gray_u256 right) { return !gray_u256_lt(left, right); }

/* --- Decimal String Constructors --- */

static inline gray_u128 gray_u128_from_decimal(const char *text) {
    gray_u128 result = GRAY_U128_ZERO;
    gray_u128 ten = {10, 0};
    while (*text) {
        if (*text == '_') { text++; continue; }
        if (*text < '0' || *text > '9') {
            gray_panic_code("P0102", "invalid digit '%c' in integer literal", *text);
        }
        result = gray_u128_mul(result, ten);
        gray_u128 digit = {(uint64_t)(*text - '0'), 0};
        result = gray_u128_add(result, digit);
        text++;
    }
    return result;
}

static inline gray_i128 gray_i128_from_decimal(const char *text) {
    bool neg = false;
    if (*text == '-') { neg = true; text++; }
    gray_u128 bits = gray_u128_from_decimal(text);
    gray_i128 result;
    result.lo = bits.lo;
    result.hi = (int64_t)bits.hi;
    return neg ? gray_i128_neg(result) : result;
}

static inline gray_u256 gray_u256_from_decimal(const char *text) {
    gray_u256 result = GRAY_U256_ZERO;
    gray_u256 ten = GRAY_U256_ZERO;
    ten.w[0] = 10;
    while (*text) {
        if (*text == '_') { text++; continue; }
        if (*text < '0' || *text > '9') {
            gray_panic_code("P0102", "invalid digit '%c' in integer literal", *text);
        }
        result = gray_u256_mul(result, ten);
        gray_u256 digit = GRAY_U256_ZERO;
        digit.w[0] = (uint64_t)(*text - '0');
        result = gray_u256_add(result, digit);
        text++;
    }
    return result;
}

static inline gray_i256 gray_i256_from_decimal(const char *text) {
    bool neg = false;
    if (*text == '-') { neg = true; text++; }
    gray_u256 bits = gray_u256_from_decimal(text);
    gray_i256 result;
    memcpy(&result, &bits, sizeof(result));
    return neg ? gray_i256_neg(result) : result;
}

/* --- Overflow-Checked Arithmetic --- */

static inline gray_i128 gray_i128_add_checked(gray_i128 left, gray_i128 right, const char *file, int line) {
    gray_i128 result = gray_i128_add(left, right);
    if ((left.hi >= 0 && right.hi >= 0 && result.hi < 0) || (left.hi < 0 && right.hi < 0 && result.hi >= 0)) {
        gray_panic_code_at(file, line, "P0021", "i128 addition result is too large; value exceeds the range of i128");
    }
    return result;
}

static inline gray_i128 gray_i128_sub_checked(gray_i128 left, gray_i128 right, const char *file, int line) {
    gray_i128 result = gray_i128_sub(left, right);
    if ((left.hi >= 0 && right.hi < 0 && result.hi < 0) || (left.hi < 0 && right.hi >= 0 && result.hi >= 0)) {
        gray_panic_code_at(file, line, "P0022", "i128 subtraction result is too large; value exceeds the range of i128");
    }
    return result;
}

static inline gray_i128 gray_i128_mul_checked(gray_i128 left, gray_i128 right, const char *file, int line) {
    gray_i128 result = gray_i128_mul(left, right);
    bool left_is_zero = (left.hi == 0 && left.lo == 0);
    bool right_is_zero = (right.hi == 0 && right.lo == 0);
    if (!left_is_zero && !right_is_zero) {
        gray_i128 check = gray_i128_div(result, right, file, line);
        if (!gray_i128_eq(check, left)) {
            gray_panic_code_at(file, line, "P0023", "i128 multiplication result is too large; value exceeds the range of i128");
        }
    }
    return result;
}

static inline gray_u128 gray_u128_add_checked(gray_u128 left, gray_u128 right, const char *file, int line) {
    gray_u128 result = gray_u128_add(left, right);
    if (gray_u128_lt(result, left)) {
        gray_panic_code_at(file, line, "P0024", "u128 addition result is too large; value exceeds the range of u128");
    }
    return result;
}

static inline gray_u128 gray_u128_sub_checked(gray_u128 left, gray_u128 right, const char *file, int line) {
    if (gray_u128_lt(left, right)) {
        gray_panic_code_at(file, line, "P0025", "u128 subtraction result is negative, but u128 cannot hold negative values");
    }
    return gray_u128_sub(left, right);
}

static inline gray_u128 gray_u128_mul_checked(gray_u128 left, gray_u128 right, const char *file, int line) {
    gray_u128 result = gray_u128_mul(left, right);
    bool left_is_zero = (left.hi == 0 && left.lo == 0);
    bool right_is_zero = (right.hi == 0 && right.lo == 0);
    if (!left_is_zero && !right_is_zero) {
        gray_u128 check = gray_u128_div(result, right, file, line);
        if (!gray_u128_eq(check, left)) {
            gray_panic_code_at(file, line, "P0026", "u128 multiplication result is too large; value exceeds the range of u128");
        }
    }
    return result;
}

static inline gray_i256 gray_i256_add_checked(gray_i256 left, gray_i256 right, const char *file, int line) {
    gray_i256 result = gray_i256_add(left, right);
    bool left_is_neg = gray_i256_is_neg(left);
    bool right_is_neg = gray_i256_is_neg(right);
    bool result_is_neg = gray_i256_is_neg(result);
    if ((!left_is_neg && !right_is_neg && result_is_neg) || (left_is_neg && right_is_neg && !result_is_neg)) {
        gray_panic_code_at(file, line, "P0027", "i256 addition result is too large; value exceeds the range of i256");
    }
    return result;
}

static inline gray_i256 gray_i256_sub_checked(gray_i256 left, gray_i256 right, const char *file, int line) {
    gray_i256 result = gray_i256_sub(left, right);
    bool left_is_neg = gray_i256_is_neg(left);
    bool right_is_neg = gray_i256_is_neg(right);
    bool result_is_neg = gray_i256_is_neg(result);
    if ((!left_is_neg && right_is_neg && result_is_neg) || (left_is_neg && !right_is_neg && !result_is_neg)) {
        gray_panic_code_at(file, line, "P0028", "i256 subtraction result is too large; value exceeds the range of i256");
    }
    return result;
}

static inline gray_i256 gray_i256_mul_checked(gray_i256 left, gray_i256 right, const char *file, int line) {
    gray_i256 result = gray_i256_mul(left, right);
    bool left_is_zero = (left.w[0] == 0 && left.w[1] == 0 && left.w[2] == 0 && left.w[3] == 0);
    bool right_is_zero = (right.w[0] == 0 && right.w[1] == 0 && right.w[2] == 0 && right.w[3] == 0);
    if (!left_is_zero && !right_is_zero) {
        gray_i256 check = gray_i256_div(result, right, file, line);
        if (!gray_i256_eq(check, left)) {
            gray_panic_code_at(file, line, "P0029", "i256 multiplication result is too large; value exceeds the range of i256");
        }
    }
    return result;
}

static inline gray_u256 gray_u256_add_checked(gray_u256 left, gray_u256 right, const char *file, int line) {
    gray_u256 result = gray_u256_add(left, right);
    if (gray_u256_lt(result, left)) {
        gray_panic_code_at(file, line, "P0030", "u256 addition result is too large; value exceeds the range of u256");
    }
    return result;
}

static inline gray_u256 gray_u256_sub_checked(gray_u256 left, gray_u256 right, const char *file, int line) {
    if (gray_u256_lt(left, right)) {
        gray_panic_code_at(file, line, "P0031", "u256 subtraction result is negative, but u256 cannot hold negative values");
    }
    return gray_u256_sub(left, right);
}

static inline gray_u256 gray_u256_mul_checked(gray_u256 left, gray_u256 right, const char *file, int line) {
    gray_u256 result = gray_u256_mul(left, right);
    bool left_is_zero = (left.w[0] == 0 && left.w[1] == 0 && left.w[2] == 0 && left.w[3] == 0);
    bool right_is_zero = (right.w[0] == 0 && right.w[1] == 0 && right.w[2] == 0 && right.w[3] == 0);
    if (!left_is_zero && !right_is_zero) {
        gray_u256 check = gray_u256_div(result, right, file, line);
        if (!gray_u256_eq(check, left)) {
            gray_panic_code_at(file, line, "P0032", "u256 multiplication result is too large; value exceeds the range of u256");
        }
    }
    return result;
}

/* --- Printing (to_string) --- */

/* Helper: convert unsigned 128-bit to decimal string */
static inline GrayString gray_u128_to_string(GrayArena *arena, gray_u128 value) {
    if (value.hi == 0) {
        char buf[U128_MAX_DIGITS];
        snprintf(buf, sizeof(buf), "%" PRIu64, value.lo);
        size_t len = strlen(buf);
        char *out = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
        memcpy(out, buf, len + 1);
        return (GrayString){ out, (int32_t)len };
    }
    /* For large values, extract digits by repeated division */
    char buf[I256_MAX_DIGITS];
    int pos = I256_MAX_DIGITS - 1;
    buf[pos] = '\0';
    gray_u128 ten = { 10, 0 };
    gray_u128 remaining = value;
    while (remaining.hi != 0 || remaining.lo != 0) {
        gray_u128 quotient, remainder;
        gray_u128_divmod(remaining, ten, &quotient, &remainder, __FILE__, __LINE__);
        buf[--pos] = '0' + (char)remainder.lo;
        remaining = quotient;
    }
    if (pos == I256_MAX_DIGITS - 1) buf[--pos] = '0';
    size_t len = (I256_MAX_DIGITS - 1) - pos;
    char *out = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
    memcpy(out, buf + pos, len + 1);
    return (GrayString){ out, (int32_t)len };
}

static inline GrayString gray_i128_to_string(GrayArena *arena, gray_i128 value) {
    if (value.hi < 0) {
        gray_i128 neg = gray_i128_neg(value);
        gray_u128 bits;
        bits.lo = neg.lo; bits.hi = (uint64_t)neg.hi;
        GrayString digits = gray_u128_to_string(arena, bits);
        char *out = (char *)gray_arena_alloc_uninitialized(arena, digits.len + 2);
        out[0] = '-';
        memcpy(out + 1, digits.data, digits.len + 1);
        return (GrayString){ out, digits.len + 1 };
    }
    gray_u128 bits;
    bits.lo = value.lo; bits.hi = (uint64_t)value.hi;
    return gray_u128_to_string(arena, bits);
}

/* Helper: convert unsigned 256-bit to decimal string */
static inline GrayString gray_u256_to_string(GrayArena *arena, gray_u256 value) {
    if (value.w[1] == 0 && value.w[2] == 0 && value.w[3] == 0) {
        char buf[U128_MAX_DIGITS];
        snprintf(buf, sizeof(buf), "%" PRIu64, value.w[0]);
        size_t len = strlen(buf);
        char *out = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
        memcpy(out, buf, len + 1);
        return (GrayString){ out, (int32_t)len };
    }
    char buf[U256_MAX_DIGITS];
    int pos = U256_MAX_DIGITS - 1;
    buf[pos] = '\0';
    gray_u256 ten = GRAY_U256_ZERO;
    ten.w[0] = 10;
    gray_u256 remaining = value;
    while (remaining.w[0] != 0 || remaining.w[1] != 0 || remaining.w[2] != 0 || remaining.w[3] != 0) {
        gray_u256 quotient, remainder;
        gray_u256_divmod(remaining, ten, &quotient, &remainder, __FILE__, __LINE__);
        buf[--pos] = '0' + (char)remainder.w[0];
        remaining = quotient;
    }
    if (pos == U256_MAX_DIGITS - 1) buf[--pos] = '0';
    size_t len = (U256_MAX_DIGITS - 1) - pos;
    char *out = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
    memcpy(out, buf + pos, len + 1);
    return (GrayString){ out, (int32_t)len };
}

static inline GrayString gray_i256_to_string(GrayArena *arena, gray_i256 value) {
    if (gray_i256_is_neg(value)) {
        gray_i256 neg = gray_i256_neg(value);
        gray_u256 bits; memcpy(&bits, &neg, sizeof(bits));
        GrayString digits = gray_u256_to_string(arena, bits);
        char *out = (char *)gray_arena_alloc_uninitialized(arena, digits.len + 2);
        out[0] = '-';
        memcpy(out + 1, digits.data, digits.len + 1);
        return (GrayString){ out, digits.len + 1 };
    }
    gray_u256 bits; memcpy(&bits, &value, sizeof(bits));
    return gray_u256_to_string(arena, bits);
}

/* --- Hex / octal rendering ---
 * Used by the fmt module for %x / %X / %o directives. The value is rendered
 * from its raw bit pattern (like C printf), so the signed variants forward
 * to the unsigned ones without taking absolute value. Longest output is
 * 256-bit octal: 86 digits. */
#define GRAY_BIGINT_RADIX_BUF 96

static inline GrayString gray_u128_to_radix_string(GrayArena *arena, gray_u128 value,
                                                   unsigned base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[GRAY_BIGINT_RADIX_BUF];
    int pos = GRAY_BIGINT_RADIX_BUF;
    gray_u128 radix_big = { base, 0 };
    gray_u128 remaining = value;
    if (remaining.lo == 0 && remaining.hi == 0) buf[--pos] = '0';
    while (remaining.lo != 0 || remaining.hi != 0) {
        gray_u128 quotient, remainder;
        gray_u128_divmod(remaining, radix_big, &quotient, &remainder, __FILE__, __LINE__);
        buf[--pos] = digits[remainder.lo];
        remaining = quotient;
    }
    size_t len = (size_t)(GRAY_BIGINT_RADIX_BUF - pos);
    char *out = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
    memcpy(out, buf + pos, len);
    out[len] = '\0';
    return (GrayString){ out, (int32_t)len };
}

static inline GrayString gray_u256_to_radix_string(GrayArena *arena, gray_u256 value,
                                                   unsigned base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[GRAY_BIGINT_RADIX_BUF];
    int pos = GRAY_BIGINT_RADIX_BUF;
    gray_u256 radix_big = GRAY_U256_ZERO;
    radix_big.w[0] = base;
    gray_u256 remaining = value;
    if (remaining.w[0] == 0 && remaining.w[1] == 0 && remaining.w[2] == 0 && remaining.w[3] == 0) buf[--pos] = '0';
    while (remaining.w[0] != 0 || remaining.w[1] != 0 || remaining.w[2] != 0 || remaining.w[3] != 0) {
        gray_u256 quotient, remainder;
        gray_u256_divmod(remaining, radix_big, &quotient, &remainder, __FILE__, __LINE__);
        buf[--pos] = digits[remainder.w[0]];
        remaining = quotient;
    }
    size_t len = (size_t)(GRAY_BIGINT_RADIX_BUF - pos);
    char *out = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
    memcpy(out, buf + pos, len);
    out[len] = '\0';
    return (GrayString){ out, (int32_t)len };
}

static inline GrayString gray_u128_to_hex_string(GrayArena *arena, gray_u128 value, bool upper) {
    return gray_u128_to_radix_string(arena, value, 16, upper);
}
static inline GrayString gray_u128_to_octal_string(GrayArena *arena, gray_u128 value) {
    return gray_u128_to_radix_string(arena, value, 8, false);
}
static inline GrayString gray_i128_to_hex_string(GrayArena *arena, gray_i128 value, bool upper) {
    gray_u128 bits = { value.lo, (uint64_t)value.hi };
    return gray_u128_to_radix_string(arena, bits, 16, upper);
}
static inline GrayString gray_i128_to_octal_string(GrayArena *arena, gray_i128 value) {
    gray_u128 bits = { value.lo, (uint64_t)value.hi };
    return gray_u128_to_radix_string(arena, bits, 8, false);
}

static inline GrayString gray_u256_to_hex_string(GrayArena *arena, gray_u256 value, bool upper) {
    return gray_u256_to_radix_string(arena, value, 16, upper);
}
static inline GrayString gray_u256_to_octal_string(GrayArena *arena, gray_u256 value) {
    return gray_u256_to_radix_string(arena, value, 8, false);
}
static inline GrayString gray_i256_to_hex_string(GrayArena *arena, gray_i256 value, bool upper) {
    gray_u256 bits; memcpy(&bits, &value, sizeof(bits));
    return gray_u256_to_radix_string(arena, bits, 16, upper);
}
static inline GrayString gray_i256_to_octal_string(GrayArena *arena, gray_i256 value) {
    gray_u256 bits; memcpy(&bits, &value, sizeof(bits));
    return gray_u256_to_radix_string(arena, bits, 8, false);
}

#endif /* GRAY_BIGINT_H */
