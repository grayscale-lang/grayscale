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

typedef struct { uint64_t low; int64_t high; } gray_i128;
typedef struct { uint64_t low; uint64_t high; } gray_u128;
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
    result.low = (uint64_t)value;
    result.high = (value < 0) ? -1 : 0;
    return result;
}

/* Widen an unsigned 64-bit value into a signed wide integer. i128 and i256
 * represent every uint64_t exactly, so the high words stay zero. from_i64
 * cannot serve here: it sign-extends, turning any value above INT64_MAX
 * negative. */
static inline gray_i128 gray_i128_from_u64(uint64_t value) {
    gray_i128 result;
    result.low = value;
    result.high = 0;
    return result;
}

static inline gray_u128 gray_u128_from_u64(uint64_t value) {
    gray_u128 result;
    result.low = value;
    result.high = 0;
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
    if (value.high != ((int64_t)value.low >> 63)) {
        gray_panic_code_at(file, line, "P0093", "cast from i128 failed; value is outside the representable range of i64");
    }
    return (int64_t)value.low;
}

static inline uint64_t gray_i128_to_u64(gray_i128 value, const char *file, int line) {
    if (value.high != 0) {
        gray_panic_code_at(file, line, "P0094", "cast from i128 failed; value is negative or outside the representable range of u64");
    }
    return value.low;
}

static inline int64_t gray_u128_to_i64(gray_u128 value, const char *file, int line) {
    if (value.high != 0 || value.low > (uint64_t)INT64_MAX) {
        gray_panic_code_at(file, line, "P0095", "cast from u128 failed; value exceeds the representable range of i64");
    }
    return (int64_t)value.low;
}

static inline uint64_t gray_u128_to_u64(gray_u128 value, const char *file, int line) {
    if (value.high != 0) {
        gray_panic_code_at(file, line, "P0096", "cast from u128 failed; value exceeds the representable range of u64");
    }
    return value.low;
}

static inline int64_t gray_i256_to_i64(gray_i256 value, const char *file, int line) {
    uint64_t sign_extension = (uint64_t)((int64_t)value.w[0] >> 63);
    if (value.w[1] != sign_extension || value.w[2] != sign_extension || value.w[3] != sign_extension) {
        gray_panic_code_at(file, line, "P0097", "cast from i256 failed; value is outside the representable range of i64");
    }
    return (int64_t)value.w[0];
}

static inline uint64_t gray_i256_to_u64(gray_i256 value, const char *file, int line) {
    if (value.w[1] != 0 || value.w[2] != 0 || value.w[3] != 0) {
        gray_panic_code_at(file, line, "P0098", "cast from i256 failed; value is negative or outside the representable range of u64");
    }
    return value.w[0];
}

static inline int64_t gray_u256_to_i64(gray_u256 value, const char *file, int line) {
    if (value.w[1] != 0 || value.w[2] != 0 || value.w[3] != 0 || value.w[0] > (uint64_t)INT64_MAX) {
        gray_panic_code_at(file, line, "P0099", "cast from u256 failed; value exceeds the representable range of i64");
    }
    return (int64_t)value.w[0];
}

static inline uint64_t gray_u256_to_u64(gray_u256 value, const char *file, int line) {
    if (value.w[1] != 0 || value.w[2] != 0 || value.w[3] != 0) {
        gray_panic_code_at(file, line, "P0100", "cast from u256 failed; value exceeds the representable range of u64");
    }
    return value.w[0];
}

static inline gray_u128 gray_u128_from_i128(gray_i128 value) {
    gray_u128 result;
    result.low = value.low;
    result.high = (uint64_t)value.high;
    return result;
}

static inline gray_i128 gray_i128_from_u128(gray_u128 value) {
    gray_i128 result;
    result.low = value.low;
    result.high = (int64_t)value.high;
    return result;
}

static inline gray_i256 gray_i256_from_i128(gray_i128 value) {
    gray_i256 result;
    result.w[0] = value.low;
    result.w[1] = (uint64_t)value.high;
    int64_t sign = (value.high < 0) ? -1 : 0;
    result.w[2] = (uint64_t)sign;
    result.w[3] = (uint64_t)sign;
    return result;
}

static inline gray_u256 gray_u256_from_u128(gray_u128 value) {
    gray_u256 result;
    result.w[0] = value.low;
    result.w[1] = value.high;
    result.w[2] = 0;
    result.w[3] = 0;
    return result;
}

static inline gray_i128 gray_i128_from_i256(gray_i256 value) {
    gray_i128 result;
    result.low = value.w[0];
    result.high = (int64_t)value.w[1];
    return result;
}

static inline gray_u128 gray_u128_from_u256(gray_u256 value) {
    gray_u128 result;
    result.low = value.w[0];
    result.high = value.w[1];
    return result;
}

/* --- i128 Arithmetic --- */

/* The hi-limb arithmetic must be unsigned: these wrap by design, and the
 * *_checked wrappers detect overflow from the wrapped sign afterward. Done
 * in int64_t, the wrap is signed-overflow UB, and GCC at -O2 uses that to
 * prove the panic branches unreachable and delete them. */
static inline gray_i128 gray_i128_add(gray_i128 left, gray_i128 right) {
    gray_i128 result;
    result.low = left.low + right.low;
    result.high = (int64_t)((uint64_t)left.high + (uint64_t)right.high + (result.low < left.low ? 1 : 0));
    return result;
}

static inline gray_i128 gray_i128_sub(gray_i128 left, gray_i128 right) {
    gray_i128 result;
    result.low = left.low - right.low;
    result.high = (int64_t)((uint64_t)left.high - (uint64_t)right.high - (left.low < right.low ? 1 : 0));
    return result;
}

static inline gray_i128 gray_i128_neg(gray_i128 value) {
    gray_i128 result;
    result.low = ~value.low + 1;
    result.high = (int64_t)(~(uint64_t)value.high + (result.low == 0 ? 1 : 0));
    return result;
}

static inline gray_i128 gray_i128_mul(gray_i128 left, gray_i128 right) {
    /* Schoolbook multiplication on 64-bit halves */
    uint64_t left_low = left.low, right_low = right.low;
    uint64_t left_high = (uint64_t)left.high, right_high = (uint64_t)right.high;

    /* Split each 64-bit value into 32-bit halves for overflow-safe multiply */
    uint64_t left_low_half = left_low & LOW32_MASK, left_high_half = left_low >> 32;
    uint64_t right_low_half = right_low & LOW32_MASK, right_high_half = right_low >> 32;

    uint64_t product_low_low = left_low_half * right_low_half;
    uint64_t product_low_high = left_low_half * right_high_half;
    uint64_t product_high_low = left_high_half * right_low_half;
    uint64_t product_high_high = left_high_half * right_high_half;

    uint64_t middle = product_low_high + (product_low_low >> 32);
    uint64_t carry = ((middle & LOW32_MASK) + product_high_low) >> 32;

    gray_i128 result;
    result.low = left_low * right_low;
    result.high = (int64_t)(product_high_high + (middle >> 32) + carry + left_low * right_high + left_high * right_low);
    return result;
}

/* Division helper: unsigned 128-bit divide */
static inline void gray_u128_divmod(gray_u128 dividend, gray_u128 divisor,
                                    gray_u128 *quotient_out, gray_u128 *remainder_out,
                                    const char *file, int line) {
    if (divisor.high == 0 && divisor.low == 0) {
        gray_panic_code_at(file, line, "P0078", "division by zero");
    }
    if (dividend.high == 0 && divisor.high == 0) {
        /* Both fit in 64 bits */
        quotient_out->high = 0; quotient_out->low = dividend.low / divisor.low;
        remainder_out->high = 0; remainder_out->low = dividend.low % divisor.low;
        return;
    }
    /* Binary long division */
    gray_u128 quotient = GRAY_U128_ZERO;
    gray_u128 remainder = GRAY_U128_ZERO;
    for (int bit_index = 127; bit_index >= 0; bit_index--) {
        /* remainder <<= 1 */
        remainder.high = (remainder.high << 1) | (remainder.low >> 63);
        remainder.low <<= 1;
        /* Get bit `bit_index` of the dividend */
        if (bit_index >= 64) {
            remainder.low |= (dividend.high >> (bit_index - 64)) & 1;
        } else {
            remainder.low |= (dividend.low >> bit_index) & 1;
        }
        /* if remainder >= divisor */
        if (remainder.high > divisor.high || (remainder.high == divisor.high && remainder.low >= divisor.low)) {
            /* remainder -= divisor */
            uint64_t borrow = (remainder.low < divisor.low) ? 1 : 0;
            remainder.low -= divisor.low;
            remainder.high -= divisor.high + borrow;
            /* Set bit `bit_index` of the quotient */
            if (bit_index >= 64) {
                quotient.high |= (uint64_t)1 << (bit_index - 64);
            } else {
                quotient.low |= (uint64_t)1 << bit_index;
            }
        }
    }
    *quotient_out = quotient;
    *remainder_out = remainder;
}

static inline gray_i128 gray_i128_div(gray_i128 left, gray_i128 right, const char *file, int line) {
    bool negate = false;
    gray_u128 mag_left, mag_right;
    if (left.high < 0) { gray_i128 negated = gray_i128_neg(left); mag_left.low = negated.low; mag_left.high = (uint64_t)negated.high; negate = !negate; }
    else { mag_left.low = left.low; mag_left.high = (uint64_t)left.high; }
    if (right.high < 0) { gray_i128 negated = gray_i128_neg(right); mag_right.low = negated.low; mag_right.high = (uint64_t)negated.high; negate = !negate; }
    else { mag_right.low = right.low; mag_right.high = (uint64_t)right.high; }
    gray_u128 quotient, remainder;
    gray_u128_divmod(mag_left, mag_right, &quotient, &remainder, file, line);
    gray_i128 result;
    result.low = quotient.low; result.high = (int64_t)quotient.high;
    return negate ? gray_i128_neg(result) : result;
}

static inline gray_i128 gray_i128_mod(gray_i128 left, gray_i128 right, const char *file, int line) {
    bool left_is_negative = (left.high < 0);
    gray_u128 mag_left, mag_right;
    if (left_is_negative) { gray_i128 negated = gray_i128_neg(left); mag_left.low = negated.low; mag_left.high = (uint64_t)negated.high; }
    else { mag_left.low = left.low; mag_left.high = (uint64_t)left.high; }
    if (right.high < 0) { gray_i128 negated = gray_i128_neg(right); mag_right.low = negated.low; mag_right.high = (uint64_t)negated.high; }
    else { mag_right.low = right.low; mag_right.high = (uint64_t)right.high; }
    gray_u128 quotient, remainder;
    gray_u128_divmod(mag_left, mag_right, &quotient, &remainder, file, line);
    gray_i128 result;
    result.low = remainder.low; result.high = (int64_t)remainder.high;
    return left_is_negative ? gray_i128_neg(result) : result;
}

/* --- i128 Comparison --- */

static inline bool gray_i128_eq(gray_i128 left, gray_i128 right) { return left.low == right.low && left.high == right.high; }
static inline bool gray_i128_ne(gray_i128 left, gray_i128 right) { return left.low != right.low || left.high != right.high; }
static inline bool gray_i128_lt(gray_i128 left, gray_i128 right) {
    return left.high < right.high || (left.high == right.high && left.low < right.low);
}
static inline bool gray_i128_gt(gray_i128 left, gray_i128 right) { return gray_i128_lt(right, left); }
static inline bool gray_i128_le(gray_i128 left, gray_i128 right) { return !gray_i128_gt(left, right); }
static inline bool gray_i128_ge(gray_i128 left, gray_i128 right) { return !gray_i128_lt(left, right); }

/* --- u128 Arithmetic --- */

static inline gray_u128 gray_u128_add(gray_u128 left, gray_u128 right) {
    gray_u128 result;
    result.low = left.low + right.low;
    result.high = left.high + right.high + (result.low < left.low ? 1 : 0);
    return result;
}

static inline gray_u128 gray_u128_sub(gray_u128 left, gray_u128 right) {
    gray_u128 result;
    result.low = left.low - right.low;
    result.high = left.high - right.high - (left.low < right.low ? 1 : 0);
    return result;
}

static inline gray_u128 gray_u128_mul(gray_u128 left, gray_u128 right) {
    uint64_t left_low_half = left.low & LOW32_MASK, left_high_half = left.low >> 32;
    uint64_t right_low_half = right.low & LOW32_MASK, right_high_half = right.low >> 32;

    uint64_t product_low_low = left_low_half * right_low_half;
    uint64_t product_low_high = left_low_half * right_high_half;
    uint64_t product_high_low = left_high_half * right_low_half;
    uint64_t product_high_high = left_high_half * right_high_half;

    uint64_t middle = product_low_high + (product_low_low >> 32);
    uint64_t carry = ((middle & LOW32_MASK) + product_high_low) >> 32;

    gray_u128 result;
    result.low = left.low * right.low;
    result.high = product_high_high + (middle >> 32) + carry + left.low * right.high + left.high * right.low;
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

static inline bool gray_u128_eq(gray_u128 left, gray_u128 right) { return left.low == right.low && left.high == right.high; }
static inline bool gray_u128_ne(gray_u128 left, gray_u128 right) { return left.low != right.low || left.high != right.high; }
static inline bool gray_u128_lt(gray_u128 left, gray_u128 right) {
    return left.high < right.high || (left.high == right.high && left.low < right.low);
}
static inline bool gray_u128_gt(gray_u128 left, gray_u128 right) { return gray_u128_lt(right, left); }
static inline bool gray_u128_le(gray_u128 left, gray_u128 right) { return !gray_u128_gt(left, right); }
static inline bool gray_u128_ge(gray_u128 left, gray_u128 right) { return !gray_u128_lt(left, right); }

/* --- i256 Arithmetic --- */

static inline gray_i256 gray_i256_add(gray_i256 left, gray_i256 right) {
    gray_i256 result;
    uint64_t carry = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t limb_sum = left.w[i] + right.w[i] + carry;
        carry = (limb_sum < left.w[i] || (carry && limb_sum == left.w[i])) ? 1 : 0;
        result.w[i] = limb_sum;
    }
    return result;
}

static inline gray_i256 gray_i256_sub(gray_i256 left, gray_i256 right) {
    gray_i256 result;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t difference = left.w[i] - right.w[i] - borrow;
        borrow = (left.w[i] < right.w[i]) || (left.w[i] - right.w[i] < borrow) ? 1 : 0;
        result.w[i] = difference;
    }
    return result;
}

static inline gray_i256 gray_i256_neg(gray_i256 value) {
    gray_i256 result;
    uint64_t carry = 1;
    for (int i = 0; i < 4; i++) {
        uint64_t limb_sum = ~value.w[i] + carry;
        carry = (limb_sum < ~value.w[i]) ? 1 : 0;
        result.w[i] = limb_sum;
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
            uint64_t left_low = left.w[j] & LOW32_MASK, left_high = left.w[j] >> 32;
            uint64_t right_low = right.w[i] & LOW32_MASK, right_high = right.w[i] >> 32;
            uint64_t product_low_low = left_low * right_low;
            uint64_t product_low_high = left_low * right_high;
            uint64_t product_high_low = left_high * right_low;
            uint64_t product_high_high = left_high * right_high;
            uint64_t middle = product_low_high + (product_low_low >> 32);
            uint64_t low_limb = (product_low_low & LOW32_MASK) | ((middle & LOW32_MASK) << 32) + (product_high_low << 32);

            /* Simpler approach: just use the truncating product */
            low_limb = left.w[j] * right.w[i];
            uint64_t hi_part = product_high_high + (middle >> 32) + (((middle & LOW32_MASK) + product_high_low) >> 32);

            uint64_t previous_limb = result.w[i + j];
            result.w[i + j] = previous_limb + low_limb + carry;
            carry = hi_part + (result.w[i + j] < previous_limb + low_limb ? 1 : 0);
            if (previous_limb + low_limb < previous_limb) carry++;
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
        int bit_index = i % 64;
        remainder.w[0] |= (dividend.w[word] >> bit_index) & 1;
        /* if remainder >= divisor */
        bool is_remainder_at_least_divisor = false;
        for (int limb_index = 3; limb_index >= 0; limb_index--) {
            if (remainder.w[limb_index] > divisor.w[limb_index]) { is_remainder_at_least_divisor = true; break; }
            if (remainder.w[limb_index] < divisor.w[limb_index]) break;
            if (limb_index == 0) is_remainder_at_least_divisor = true;
        }
        if (is_remainder_at_least_divisor) {
            /* remainder -= divisor */
            uint64_t borrow = 0;
            for (int limb_index = 0; limb_index < 4; limb_index++) {
                uint64_t difference = remainder.w[limb_index] - divisor.w[limb_index] - borrow;
                borrow = (remainder.w[limb_index] < divisor.w[limb_index]) || (remainder.w[limb_index] - divisor.w[limb_index] < borrow) ? 1 : 0;
                remainder.w[limb_index] = difference;
            }
            quotient.w[word] |= (uint64_t)1 << bit_index;
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
        uint64_t limb_sum = left.w[i] + right.w[i] + carry;
        carry = (limb_sum < left.w[i] || (carry && limb_sum == left.w[i])) ? 1 : 0;
        result.w[i] = limb_sum;
    }
    return result;
}

static inline gray_u256 gray_u256_sub(gray_u256 left, gray_u256 right) {
    gray_u256 result;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t difference = left.w[i] - right.w[i] - borrow;
        borrow = (left.w[i] < right.w[i]) || (left.w[i] - right.w[i] < borrow) ? 1 : 0;
        result.w[i] = difference;
    }
    return result;
}

static inline gray_u256 gray_u256_mul(gray_u256 left, gray_u256 right) {
    gray_u256 result = GRAY_U256_ZERO;
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4 - i; j++) {
            uint64_t left_low = left.w[j] & LOW32_MASK, left_high = left.w[j] >> 32;
            uint64_t right_low = right.w[i] & LOW32_MASK, right_high = right.w[i] >> 32;
            uint64_t product_low_low = left_low * right_low;
            uint64_t product_low_high = left_low * right_high;
            uint64_t product_high_low = left_high * right_low;
            uint64_t product_high_high = left_high * right_high;
            uint64_t middle = product_low_high + (product_low_low >> 32);
            uint64_t low_limb = left.w[j] * right.w[i];
            uint64_t hi_part = product_high_high + (middle >> 32) + (((middle & LOW32_MASK) + product_high_low) >> 32);

            uint64_t previous_limb = result.w[i + j];
            result.w[i + j] = previous_limb + low_limb + carry;
            carry = hi_part + (result.w[i + j] < previous_limb + low_limb ? 1 : 0);
            if (previous_limb + low_limb < previous_limb) carry++;
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
    gray_u128 ten_value = {10, 0};
    while (*text) {
        if (*text == '_') { text++; continue; }
        if (*text < '0' || *text > '9') {
            gray_panic_code("P0102", "invalid digit '%c' in integer literal", *text);
        }
        result = gray_u128_mul(result, ten_value);
        gray_u128 digit = {(uint64_t)(*text - '0'), 0};
        result = gray_u128_add(result, digit);
        text++;
    }
    return result;
}

static inline gray_i128 gray_i128_from_decimal(const char *text) {
    bool is_negative = false;
    if (*text == '-') { is_negative = true; text++; }
    gray_u128 bits = gray_u128_from_decimal(text);
    gray_i128 result;
    result.low = bits.low;
    result.high = (int64_t)bits.high;
    return is_negative ? gray_i128_neg(result) : result;
}

static inline gray_u256 gray_u256_from_decimal(const char *text) {
    gray_u256 result = GRAY_U256_ZERO;
    gray_u256 ten_value = GRAY_U256_ZERO;
    ten_value.w[0] = 10;
    while (*text) {
        if (*text == '_') { text++; continue; }
        if (*text < '0' || *text > '9') {
            gray_panic_code("P0102", "invalid digit '%c' in integer literal", *text);
        }
        result = gray_u256_mul(result, ten_value);
        gray_u256 digit = GRAY_U256_ZERO;
        digit.w[0] = (uint64_t)(*text - '0');
        result = gray_u256_add(result, digit);
        text++;
    }
    return result;
}

static inline gray_i256 gray_i256_from_decimal(const char *text) {
    bool is_negative = false;
    if (*text == '-') { is_negative = true; text++; }
    gray_u256 bits = gray_u256_from_decimal(text);
    gray_i256 result;
    memcpy(&result, &bits, sizeof(result));
    return is_negative ? gray_i256_neg(result) : result;
}

/* --- Overflow-Checked Arithmetic --- */

static inline gray_i128 gray_i128_add_checked(gray_i128 left, gray_i128 right, const char *file, int line) {
    gray_i128 result = gray_i128_add(left, right);
    if ((left.high >= 0 && right.high >= 0 && result.high < 0) || (left.high < 0 && right.high < 0 && result.high >= 0)) {
        gray_panic_code_at(file, line, "P0021", "i128 addition result is too large; value exceeds the range of i128");
    }
    return result;
}

static inline gray_i128 gray_i128_sub_checked(gray_i128 left, gray_i128 right, const char *file, int line) {
    gray_i128 result = gray_i128_sub(left, right);
    if ((left.high >= 0 && right.high < 0 && result.high < 0) || (left.high < 0 && right.high >= 0 && result.high >= 0)) {
        gray_panic_code_at(file, line, "P0022", "i128 subtraction result is too large; value exceeds the range of i128");
    }
    return result;
}

static inline gray_i128 gray_i128_mul_checked(gray_i128 left, gray_i128 right, const char *file, int line) {
    gray_i128 result = gray_i128_mul(left, right);
    bool left_is_zero = (left.high == 0 && left.low == 0);
    bool right_is_zero = (right.high == 0 && right.low == 0);
    if (!left_is_zero && !right_is_zero) {
        gray_i128 check = gray_i128_div(result, right, file, line);
        if (!gray_i128_eq(check, left)) {
            gray_panic_code_at(file, line, "P0023", "i128 multiplication result is too large; value exceeds the range of i128");
        }
    }
    return result;
}

/* Negating the minimum value overflows; every other value negates exactly. */
static inline gray_i128 gray_i128_neg_checked(gray_i128 value, const char *file, int line) {
    gray_i128 result = gray_i128_neg(value);
    if (value.high < 0 && result.high < 0) {
        gray_panic_code_at(file, line, "P0014", "i128 negation result is too large; value exceeds the range of this type");
    }
    return result;
}

static inline gray_i128 gray_i128_abs_checked(gray_i128 value, const char *file, int line) {
    return value.high < 0 ? gray_i128_neg_checked(value, file, line) : value;
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
    bool left_is_zero = (left.high == 0 && left.low == 0);
    bool right_is_zero = (right.high == 0 && right.low == 0);
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

static inline gray_i256 gray_i256_neg_checked(gray_i256 value, const char *file, int line) {
    gray_i256 result = gray_i256_neg(value);
    if (gray_i256_is_neg(value) && gray_i256_is_neg(result)) {
        gray_panic_code_at(file, line, "P0014", "i256 negation result is too large; value exceeds the range of this type");
    }
    return result;
}

static inline gray_i256 gray_i256_abs_checked(gray_i256 value, const char *file, int line) {
    return gray_i256_is_neg(value) ? gray_i256_neg_checked(value, file, line) : value;
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
    if (value.high == 0) {
        char buffer[U128_MAX_DIGITS];
        snprintf(buffer, sizeof(buffer), "%" PRIu64, value.low);
        size_t length = strlen(buffer);
        char *output = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
        memcpy(output, buffer, length + 1);
        return (GrayString){ output, (int32_t)length };
    }
    /* For large values, extract digits by repeated division */
    char buffer[I256_MAX_DIGITS];
    int position = I256_MAX_DIGITS - 1;
    buffer[position] = '\0';
    gray_u128 ten_value = { 10, 0 };
    gray_u128 remaining = value;
    while (remaining.high != 0 || remaining.low != 0) {
        gray_u128 quotient, remainder;
        gray_u128_divmod(remaining, ten_value, &quotient, &remainder, __FILE__, __LINE__);
        buffer[--position] = '0' + (char)remainder.low;
        remaining = quotient;
    }
    if (position == I256_MAX_DIGITS - 1) buffer[--position] = '0';
    size_t length = (I256_MAX_DIGITS - 1) - position;
    char *output = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
    memcpy(output, buffer + position, length + 1);
    return (GrayString){ output, (int32_t)length };
}

static inline GrayString gray_i128_to_string(GrayArena *arena, gray_i128 value) {
    if (value.high < 0) {
        gray_i128 negated = gray_i128_neg(value);
        gray_u128 bits;
        bits.low = negated.low; bits.high = (uint64_t)negated.high;
        GrayString digits = gray_u128_to_string(arena, bits);
        char *output = (char *)gray_arena_alloc_uninitialized(arena, digits.len + 2);
        output[0] = '-';
        memcpy(output + 1, digits.data, digits.len + 1);
        return (GrayString){ output, digits.len + 1 };
    }
    gray_u128 bits;
    bits.low = value.low; bits.high = (uint64_t)value.high;
    return gray_u128_to_string(arena, bits);
}

/* Helper: convert unsigned 256-bit to decimal string */
static inline GrayString gray_u256_to_string(GrayArena *arena, gray_u256 value) {
    if (value.w[1] == 0 && value.w[2] == 0 && value.w[3] == 0) {
        char buffer[U128_MAX_DIGITS];
        snprintf(buffer, sizeof(buffer), "%" PRIu64, value.w[0]);
        size_t length = strlen(buffer);
        char *output = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
        memcpy(output, buffer, length + 1);
        return (GrayString){ output, (int32_t)length };
    }
    char buffer[U256_MAX_DIGITS];
    int position = U256_MAX_DIGITS - 1;
    buffer[position] = '\0';
    gray_u256 ten_value = GRAY_U256_ZERO;
    ten_value.w[0] = 10;
    gray_u256 remaining = value;
    while (remaining.w[0] != 0 || remaining.w[1] != 0 || remaining.w[2] != 0 || remaining.w[3] != 0) {
        gray_u256 quotient, remainder;
        gray_u256_divmod(remaining, ten_value, &quotient, &remainder, __FILE__, __LINE__);
        buffer[--position] = '0' + (char)remainder.w[0];
        remaining = quotient;
    }
    if (position == U256_MAX_DIGITS - 1) buffer[--position] = '0';
    size_t length = (U256_MAX_DIGITS - 1) - position;
    char *output = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
    memcpy(output, buffer + position, length + 1);
    return (GrayString){ output, (int32_t)length };
}

static inline GrayString gray_i256_to_string(GrayArena *arena, gray_i256 value) {
    if (gray_i256_is_neg(value)) {
        gray_i256 negated = gray_i256_neg(value);
        gray_u256 bits; memcpy(&bits, &negated, sizeof(bits));
        GrayString digits = gray_u256_to_string(arena, bits);
        char *output = (char *)gray_arena_alloc_uninitialized(arena, digits.len + 2);
        output[0] = '-';
        memcpy(output + 1, digits.data, digits.len + 1);
        return (GrayString){ output, digits.len + 1 };
    }
    gray_u256 bits; memcpy(&bits, &value, sizeof(bits));
    return gray_u256_to_string(arena, bits);
}

/* --- Bitwise ---
 * Each value is handled as its little-endian array of 64-bit words (w[0] is
 * the low word), the layout every wide type shares. A signed right shift is
 * arithmetic, as it is for i64. */

static inline void gray_bigint_shift_left_words(uint64_t *words, int count, int amount) {
    int word_shift = amount / 64, bit_shift = amount % 64;
    for (int i = count - 1; i >= 0; i--) {
        int source = i - word_shift;
        uint64_t word = source >= 0 ? words[source] << bit_shift : 0;
        if (bit_shift != 0 && source - 1 >= 0) word |= words[source - 1] >> (64 - bit_shift);
        words[i] = word;
    }
}

static inline void gray_bigint_shift_right_words(uint64_t *words, int count, int amount, bool arithmetic) {
    uint64_t fill = (arithmetic && (words[count - 1] >> 63)) ? UINT64_MAX : 0;
    int word_shift = amount / 64, bit_shift = amount % 64;
    for (int i = 0; i < count; i++) {
        int source = i + word_shift;
        uint64_t word = source < count ? words[source] >> bit_shift : fill;
        if (bit_shift != 0) word |= (source + 1 < count ? words[source + 1] : fill) << (64 - bit_shift);
        words[i] = word;
    }
}

/* Generates T_and/_or/_xor/_not, the range-checked T_shl/_shr, and
 * T_shift_amount, which narrows a T used as a shift amount to int64_t or
 * panics when it cannot be an in-range amount for a max_amount operand. */
#define GRAY_BIGINT_BITWISE(TYPE, WORDS, SIGNED)                                              \
    static inline TYPE TYPE##_and(TYPE left, TYPE right) {                                            \
        uint64_t left_words[WORDS], right_words[WORDS]; memcpy(left_words, &left, sizeof(left_words)); memcpy(right_words, &right, sizeof(right_words)); \
        for (int i = 0; i < WORDS; i++) left_words[i] &= right_words[i];                                      \
        TYPE result; memcpy(&result, left_words, sizeof(result)); return result;                      \
    }                                                                                      \
    static inline TYPE TYPE##_or(TYPE left, TYPE right) {                                             \
        uint64_t left_words[WORDS], right_words[WORDS]; memcpy(left_words, &left, sizeof(left_words)); memcpy(right_words, &right, sizeof(right_words)); \
        for (int i = 0; i < WORDS; i++) left_words[i] |= right_words[i];                                      \
        TYPE result; memcpy(&result, left_words, sizeof(result)); return result;                      \
    }                                                                                      \
    static inline TYPE TYPE##_xor(TYPE left, TYPE right) {                                            \
        uint64_t left_words[WORDS], right_words[WORDS]; memcpy(left_words, &left, sizeof(left_words)); memcpy(right_words, &right, sizeof(right_words)); \
        for (int i = 0; i < WORDS; i++) left_words[i] ^= right_words[i];                                      \
        TYPE result; memcpy(&result, left_words, sizeof(result)); return result;                      \
    }                                                                                      \
    static inline TYPE TYPE##_not(TYPE value) {                                                    \
        uint64_t words[WORDS]; memcpy(words, &value, sizeof(words));                                   \
        for (int i = 0; i < WORDS; i++) words[i] = ~words[i];                                      \
        TYPE result; memcpy(&result, words, sizeof(result)); return result;                      \
    }                                                                                      \
    static inline TYPE TYPE##_shl(TYPE value, int64_t amount, const char *file, int line) {        \
        if (amount < 0 || amount >= WORDS * 64)                                            \
            gray_panic_code_at(file, line, "P0092", "shift amount %lld is out of range; must be in [0, %d] for this operand type", \
                (long long)amount, WORDS * 64 - 1);                                        \
        uint64_t words[WORDS]; memcpy(words, &value, sizeof(words));                                   \
        gray_bigint_shift_left_words(words, WORDS, (int)amount);                               \
        TYPE result; memcpy(&result, words, sizeof(result)); return result;                      \
    }                                                                                      \
    static inline TYPE TYPE##_shr(TYPE value, int64_t amount, const char *file, int line) {        \
        if (amount < 0 || amount >= WORDS * 64)                                            \
            gray_panic_code_at(file, line, "P0092", "shift amount %lld is out of range; must be in [0, %d] for this operand type", \
                (long long)amount, WORDS * 64 - 1);                                        \
        uint64_t words[WORDS]; memcpy(words, &value, sizeof(words));                                   \
        gray_bigint_shift_right_words(words, WORDS, (int)amount, SIGNED);                      \
        TYPE result; memcpy(&result, words, sizeof(result)); return result;                      \
    }                                                                                      \
    static inline int64_t TYPE##_shift_amount(TYPE value, int max_amount, const char *file, int line) { \
        uint64_t words[WORDS]; memcpy(words, &value, sizeof(words));                                   \
        uint64_t extension = (SIGNED && (words[0] >> 63)) ? UINT64_MAX : 0;                    \
        bool fits = SIGNED || (words[0] >> 63) == 0;                                           \
        for (int i = 1; i < WORDS; i++) if (words[i] != extension) fits = false;               \
        if (!fits)                                                                         \
            gray_panic_code_at(file, line, "P0092", "shift amount %s is out of range; must be in [0, %d] for this operand type", \
                TYPE##_to_string(gray_default_arena, value).data, max_amount);                \
        return (int64_t)words[0];                                                              \
    }

GRAY_BIGINT_BITWISE(gray_i128, 2, true)
GRAY_BIGINT_BITWISE(gray_u128, 2, false)
GRAY_BIGINT_BITWISE(gray_i256, 4, true)
GRAY_BIGINT_BITWISE(gray_u256, 4, false)

/* --- Hex / octal rendering ---
 * Used by the fmt module for %x / %X / %o directives. The value is rendered
 * from its raw bit pattern (like C printf), so the signed variants forward
 * to the unsigned ones without taking absolute value. Longest output is
 * 256-bit octal: 86 digits. */
#define GRAY_BIGINT_RADIX_BUFFER_SIZE 96

static inline GrayString gray_u128_to_radix_string(GrayArena *arena, gray_u128 value,
                                                   unsigned base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[GRAY_BIGINT_RADIX_BUFFER_SIZE];
    int position = GRAY_BIGINT_RADIX_BUFFER_SIZE;
    gray_u128 radix_big = { base, 0 };
    gray_u128 remaining = value;
    if (remaining.low == 0 && remaining.high == 0) buffer[--position] = '0';
    while (remaining.low != 0 || remaining.high != 0) {
        gray_u128 quotient, remainder;
        gray_u128_divmod(remaining, radix_big, &quotient, &remainder, __FILE__, __LINE__);
        buffer[--position] = digits[remainder.low];
        remaining = quotient;
    }
    size_t length = (size_t)(GRAY_BIGINT_RADIX_BUFFER_SIZE - position);
    char *output = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
    memcpy(output, buffer + position, length);
    output[length] = '\0';
    return (GrayString){ output, (int32_t)length };
}

static inline GrayString gray_u256_to_radix_string(GrayArena *arena, gray_u256 value,
                                                   unsigned base, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[GRAY_BIGINT_RADIX_BUFFER_SIZE];
    int position = GRAY_BIGINT_RADIX_BUFFER_SIZE;
    gray_u256 radix_big = GRAY_U256_ZERO;
    radix_big.w[0] = base;
    gray_u256 remaining = value;
    if (remaining.w[0] == 0 && remaining.w[1] == 0 && remaining.w[2] == 0 && remaining.w[3] == 0) buffer[--position] = '0';
    while (remaining.w[0] != 0 || remaining.w[1] != 0 || remaining.w[2] != 0 || remaining.w[3] != 0) {
        gray_u256 quotient, remainder;
        gray_u256_divmod(remaining, radix_big, &quotient, &remainder, __FILE__, __LINE__);
        buffer[--position] = digits[remainder.w[0]];
        remaining = quotient;
    }
    size_t length = (size_t)(GRAY_BIGINT_RADIX_BUFFER_SIZE - position);
    char *output = (char *)gray_arena_alloc_uninitialized(arena, length + 1);
    memcpy(output, buffer + position, length);
    output[length] = '\0';
    return (GrayString){ output, (int32_t)length };
}

static inline GrayString gray_u128_to_hex_string(GrayArena *arena, gray_u128 value, bool upper) {
    return gray_u128_to_radix_string(arena, value, 16, upper);
}
static inline GrayString gray_u128_to_octal_string(GrayArena *arena, gray_u128 value) {
    return gray_u128_to_radix_string(arena, value, 8, false);
}
static inline GrayString gray_i128_to_hex_string(GrayArena *arena, gray_i128 value, bool upper) {
    gray_u128 bits = { value.low, (uint64_t)value.high };
    return gray_u128_to_radix_string(arena, bits, 16, upper);
}
static inline GrayString gray_i128_to_octal_string(GrayArena *arena, gray_i128 value) {
    gray_u128 bits = { value.low, (uint64_t)value.high };
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
