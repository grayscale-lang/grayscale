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

static inline gray_i128 gray_i128_from_i64(int64_t v) {
    gray_i128 r;
    r.lo = (uint64_t)v;
    r.hi = (v < 0) ? -1 : 0;
    return r;
}

static inline gray_u128 gray_u128_from_u64(uint64_t v) {
    gray_u128 r;
    r.lo = v;
    r.hi = 0;
    return r;
}

static inline gray_i256 gray_i256_from_i64(int64_t v) {
    gray_i256 r;
    r.w[0] = (uint64_t)v;
    int64_t sign = (v < 0) ? -1 : 0;
    r.w[1] = (uint64_t)sign;
    r.w[2] = (uint64_t)sign;
    r.w[3] = (uint64_t)sign;
    return r;
}

static inline gray_u256 gray_u256_from_u64(uint64_t v) {
    gray_u256 r;
    r.w[0] = v;
    r.w[1] = 0;
    r.w[2] = 0;
    r.w[3] = 0;
    return r;
}

/* --- Size Casting (range-checked) --- */

static inline int64_t gray_i128_to_i64(gray_i128 a, const char *file, int line) {
    if (a.hi != ((int64_t)a.lo >> 63)) {
        gray_panic_code_at(file, line, "P0093", "cast from i128 failed; value is outside the representable range of int64");
    }
    return (int64_t)a.lo;
}

static inline uint64_t gray_i128_to_u64(gray_i128 a, const char *file, int line) {
    if (a.hi != 0) {
        gray_panic_code_at(file, line, "P0094", "cast from i128 failed; value is negative or outside the representable range of uint64");
    }
    return a.lo;
}

static inline int64_t gray_u128_to_i64(gray_u128 a, const char *file, int line) {
    if (a.hi != 0 || a.lo > (uint64_t)INT64_MAX) {
        gray_panic_code_at(file, line, "P0095", "cast from u128 failed; value exceeds the representable range of int64");
    }
    return (int64_t)a.lo;
}

static inline uint64_t gray_u128_to_u64(gray_u128 a, const char *file, int line) {
    if (a.hi != 0) {
        gray_panic_code_at(file, line, "P0096", "cast from u128 failed; value exceeds the representable range of uint64");
    }
    return a.lo;
}

static inline int64_t gray_i256_to_i64(gray_i256 a, const char *file, int line) {
    uint64_t sign_ext = (uint64_t)((int64_t)a.w[0] >> 63);
    if (a.w[1] != sign_ext || a.w[2] != sign_ext || a.w[3] != sign_ext) {
        gray_panic_code_at(file, line, "P0097", "cast from i256 failed; value is outside the representable range of int64");
    }
    return (int64_t)a.w[0];
}

static inline uint64_t gray_i256_to_u64(gray_i256 a, const char *file, int line) {
    if (a.w[1] != 0 || a.w[2] != 0 || a.w[3] != 0) {
        gray_panic_code_at(file, line, "P0098", "cast from i256 failed; value is negative or outside the representable range of uint64");
    }
    return a.w[0];
}

static inline int64_t gray_u256_to_i64(gray_u256 a, const char *file, int line) {
    if (a.w[1] != 0 || a.w[2] != 0 || a.w[3] != 0 || a.w[0] > (uint64_t)INT64_MAX) {
        gray_panic_code_at(file, line, "P0099", "cast from u256 failed; value exceeds the representable range of int64");
    }
    return (int64_t)a.w[0];
}

static inline uint64_t gray_u256_to_u64(gray_u256 a, const char *file, int line) {
    if (a.w[1] != 0 || a.w[2] != 0 || a.w[3] != 0) {
        gray_panic_code_at(file, line, "P0100", "cast from u256 failed; value exceeds the representable range of uint64");
    }
    return a.w[0];
}

static inline gray_u128 gray_u128_from_i128(gray_i128 a) {
    gray_u128 r;
    r.lo = a.lo;
    r.hi = (uint64_t)a.hi;
    return r;
}

static inline gray_i128 gray_i128_from_u128(gray_u128 a) {
    gray_i128 r;
    r.lo = a.lo;
    r.hi = (int64_t)a.hi;
    return r;
}

static inline gray_i256 gray_i256_from_i128(gray_i128 a) {
    gray_i256 r;
    r.w[0] = a.lo;
    r.w[1] = (uint64_t)a.hi;
    int64_t sign = (a.hi < 0) ? -1 : 0;
    r.w[2] = (uint64_t)sign;
    r.w[3] = (uint64_t)sign;
    return r;
}

static inline gray_u256 gray_u256_from_u128(gray_u128 a) {
    gray_u256 r;
    r.w[0] = a.lo;
    r.w[1] = a.hi;
    r.w[2] = 0;
    r.w[3] = 0;
    return r;
}

static inline gray_i128 gray_i128_from_i256(gray_i256 a) {
    gray_i128 r;
    r.lo = a.w[0];
    r.hi = (int64_t)a.w[1];
    return r;
}

static inline gray_u128 gray_u128_from_u256(gray_u256 a) {
    gray_u128 r;
    r.lo = a.w[0];
    r.hi = a.w[1];
    return r;
}

/* --- i128 Arithmetic --- */

/* The hi-limb arithmetic must be unsigned: these wrap by design, and the
 * *_checked wrappers detect overflow from the wrapped sign afterward. Done
 * in int64_t, the wrap is signed-overflow UB, and GCC at -O2 uses that to
 * prove the panic branches unreachable and delete them. */
static inline gray_i128 gray_i128_add(gray_i128 a, gray_i128 b) {
    gray_i128 r;
    r.lo = a.lo + b.lo;
    r.hi = (int64_t)((uint64_t)a.hi + (uint64_t)b.hi + (r.lo < a.lo ? 1 : 0));
    return r;
}

static inline gray_i128 gray_i128_sub(gray_i128 a, gray_i128 b) {
    gray_i128 r;
    r.lo = a.lo - b.lo;
    r.hi = (int64_t)((uint64_t)a.hi - (uint64_t)b.hi - (a.lo < b.lo ? 1 : 0));
    return r;
}

static inline gray_i128 gray_i128_neg(gray_i128 a) {
    gray_i128 r;
    r.lo = ~a.lo + 1;
    r.hi = (int64_t)(~(uint64_t)a.hi + (r.lo == 0 ? 1 : 0));
    return r;
}

static inline gray_i128 gray_i128_mul(gray_i128 a, gray_i128 b) {
    /* Schoolbook multiplication on 64-bit halves */
    uint64_t a_lo = a.lo, b_lo = b.lo;
    uint64_t a_hi = (uint64_t)a.hi, b_hi = (uint64_t)b.hi;

    /* Split each 64-bit value into 32-bit halves for overflow-safe multiply */
    uint64_t a0 = a_lo & 0xFFFFFFFF, a1 = a_lo >> 32;
    uint64_t b0 = b_lo & 0xFFFFFFFF, b1 = b_lo >> 32;

    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;

    uint64_t mid = p01 + (p00 >> 32);
    uint64_t carry = ((mid & 0xFFFFFFFF) + p10) >> 32;

    gray_i128 r;
    r.lo = a_lo * b_lo;
    r.hi = (int64_t)(p11 + (mid >> 32) + carry + a_lo * b_hi + a_hi * b_lo);
    return r;
}

/* Division helper: unsigned 128-bit divide */
static inline void gray_u128_divmod(gray_u128 a, gray_u128 b, gray_u128 *q, gray_u128 *rem, const char *file, int line) {
    if (b.hi == 0 && b.lo == 0) {
        gray_panic_code_at(file, line, "P0078", "division by zero");
    }
    if (a.hi == 0 && b.hi == 0) {
        /* Both fit in 64 bits */
        q->hi = 0; q->lo = a.lo / b.lo;
        rem->hi = 0; rem->lo = a.lo % b.lo;
        return;
    }
    /* Binary long division */
    gray_u128 quotient = GRAY_U128_ZERO;
    gray_u128 remainder = GRAY_U128_ZERO;
    for (int i = 127; i >= 0; i--) {
        /* remainder <<= 1 */
        remainder.hi = (remainder.hi << 1) | (remainder.lo >> 63);
        remainder.lo <<= 1;
        /* Get bit i of a */
        if (i >= 64) {
            remainder.lo |= (a.hi >> (i - 64)) & 1;
        } else {
            remainder.lo |= (a.lo >> i) & 1;
        }
        /* if remainder >= b */
        if (remainder.hi > b.hi || (remainder.hi == b.hi && remainder.lo >= b.lo)) {
            /* remainder -= b */
            uint64_t borrow = (remainder.lo < b.lo) ? 1 : 0;
            remainder.lo -= b.lo;
            remainder.hi -= b.hi + borrow;
            /* Set bit i of quotient */
            if (i >= 64) {
                quotient.hi |= (uint64_t)1 << (i - 64);
            } else {
                quotient.lo |= (uint64_t)1 << i;
            }
        }
    }
    *q = quotient;
    *rem = remainder;
}

static inline gray_i128 gray_i128_div(gray_i128 a, gray_i128 b, const char *file, int line) {
    bool neg = false;
    gray_u128 ua, ub;
    if (a.hi < 0) { gray_i128 t = gray_i128_neg(a); ua.lo = t.lo; ua.hi = (uint64_t)t.hi; neg = !neg; }
    else { ua.lo = a.lo; ua.hi = (uint64_t)a.hi; }
    if (b.hi < 0) { gray_i128 t = gray_i128_neg(b); ub.lo = t.lo; ub.hi = (uint64_t)t.hi; neg = !neg; }
    else { ub.lo = b.lo; ub.hi = (uint64_t)b.hi; }
    gray_u128 q, rem;
    gray_u128_divmod(ua, ub, &q, &rem, file, line);
    gray_i128 result;
    result.lo = q.lo; result.hi = (int64_t)q.hi;
    return neg ? gray_i128_neg(result) : result;
}

static inline gray_i128 gray_i128_mod(gray_i128 a, gray_i128 b, const char *file, int line) {
    bool neg_a = (a.hi < 0);
    gray_u128 ua, ub;
    if (neg_a) { gray_i128 t = gray_i128_neg(a); ua.lo = t.lo; ua.hi = (uint64_t)t.hi; }
    else { ua.lo = a.lo; ua.hi = (uint64_t)a.hi; }
    if (b.hi < 0) { gray_i128 t = gray_i128_neg(b); ub.lo = t.lo; ub.hi = (uint64_t)t.hi; }
    else { ub.lo = b.lo; ub.hi = (uint64_t)b.hi; }
    gray_u128 q, rem;
    gray_u128_divmod(ua, ub, &q, &rem, file, line);
    gray_i128 result;
    result.lo = rem.lo; result.hi = (int64_t)rem.hi;
    return neg_a ? gray_i128_neg(result) : result;
}

/* --- i128 Comparison --- */

static inline bool gray_i128_eq(gray_i128 a, gray_i128 b) { return a.lo == b.lo && a.hi == b.hi; }
static inline bool gray_i128_ne(gray_i128 a, gray_i128 b) { return a.lo != b.lo || a.hi != b.hi; }
static inline bool gray_i128_lt(gray_i128 a, gray_i128 b) {
    return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
}
static inline bool gray_i128_gt(gray_i128 a, gray_i128 b) { return gray_i128_lt(b, a); }
static inline bool gray_i128_le(gray_i128 a, gray_i128 b) { return !gray_i128_gt(a, b); }
static inline bool gray_i128_ge(gray_i128 a, gray_i128 b) { return !gray_i128_lt(a, b); }

/* --- u128 Arithmetic --- */

static inline gray_u128 gray_u128_add(gray_u128 a, gray_u128 b) {
    gray_u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
    return r;
}

static inline gray_u128 gray_u128_sub(gray_u128 a, gray_u128 b) {
    gray_u128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0);
    return r;
}

static inline gray_u128 gray_u128_mul(gray_u128 a, gray_u128 b) {
    uint64_t a0 = a.lo & 0xFFFFFFFF, a1 = a.lo >> 32;
    uint64_t b0 = b.lo & 0xFFFFFFFF, b1 = b.lo >> 32;

    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;

    uint64_t mid = p01 + (p00 >> 32);
    uint64_t carry = ((mid & 0xFFFFFFFF) + p10) >> 32;

    gray_u128 r;
    r.lo = a.lo * b.lo;
    r.hi = p11 + (mid >> 32) + carry + a.lo * b.hi + a.hi * b.lo;
    return r;
}

static inline gray_u128 gray_u128_div(gray_u128 a, gray_u128 b, const char *file, int line) {
    gray_u128 q, rem;
    gray_u128_divmod(a, b, &q, &rem, file, line);
    return q;
}

static inline gray_u128 gray_u128_mod(gray_u128 a, gray_u128 b, const char *file, int line) {
    gray_u128 q, rem;
    gray_u128_divmod(a, b, &q, &rem, file, line);
    return rem;
}

/* --- u128 Comparison --- */

static inline bool gray_u128_eq(gray_u128 a, gray_u128 b) { return a.lo == b.lo && a.hi == b.hi; }
static inline bool gray_u128_ne(gray_u128 a, gray_u128 b) { return a.lo != b.lo || a.hi != b.hi; }
static inline bool gray_u128_lt(gray_u128 a, gray_u128 b) {
    return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
}
static inline bool gray_u128_gt(gray_u128 a, gray_u128 b) { return gray_u128_lt(b, a); }
static inline bool gray_u128_le(gray_u128 a, gray_u128 b) { return !gray_u128_gt(a, b); }
static inline bool gray_u128_ge(gray_u128 a, gray_u128 b) { return !gray_u128_lt(a, b); }

/* --- i256 Arithmetic --- */

static inline gray_i256 gray_i256_add(gray_i256 a, gray_i256 b) {
    gray_i256 r;
    uint64_t carry = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t sum = a.w[i] + b.w[i] + carry;
        carry = (sum < a.w[i] || (carry && sum == a.w[i])) ? 1 : 0;
        r.w[i] = sum;
    }
    return r;
}

static inline gray_i256 gray_i256_sub(gray_i256 a, gray_i256 b) {
    gray_i256 r;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t diff = a.w[i] - b.w[i] - borrow;
        borrow = (a.w[i] < b.w[i]) || (a.w[i] - b.w[i] < borrow) ? 1 : 0;
        r.w[i] = diff;
    }
    return r;
}

static inline gray_i256 gray_i256_neg(gray_i256 a) {
    gray_i256 r;
    uint64_t carry = 1;
    for (int i = 0; i < 4; i++) {
        uint64_t sum = ~a.w[i] + carry;
        carry = (sum < ~a.w[i]) ? 1 : 0;
        r.w[i] = sum;
    }
    return r;
}

static inline gray_i256 gray_i256_mul(gray_i256 a, gray_i256 b) {
    /* Schoolbook multiplication — only need lower 256 bits */
    gray_i256 r = GRAY_I256_ZERO;
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4 - i; j++) {
            /* Multiply a.w[j] * b.w[i] and add to r.w[i+j] */
            uint64_t a_lo = a.w[j] & 0xFFFFFFFF, a_hi = a.w[j] >> 32;
            uint64_t b_lo = b.w[i] & 0xFFFFFFFF, b_hi = b.w[i] >> 32;
            uint64_t p00 = a_lo * b_lo;
            uint64_t p01 = a_lo * b_hi;
            uint64_t p10 = a_hi * b_lo;
            uint64_t p11 = a_hi * b_hi;
            uint64_t mid = p01 + (p00 >> 32);
            uint64_t lo = (p00 & 0xFFFFFFFF) | ((mid & 0xFFFFFFFF) << 32) + (p10 << 32);

            /* Simpler approach: just use the truncating product */
            lo = a.w[j] * b.w[i];
            uint64_t hi_part = p11 + (mid >> 32) + (((mid & 0xFFFFFFFF) + p10) >> 32);

            uint64_t old = r.w[i + j];
            r.w[i + j] = old + lo + carry;
            carry = hi_part + (r.w[i + j] < old + lo ? 1 : 0);
            if (old + lo < old) carry++;
        }
    }
    return r;
}

/* 256-bit unsigned divmod (binary long division) */
static inline void gray_u256_divmod(gray_u256 a, gray_u256 b, gray_u256 *q, gray_u256 *rem, const char *file, int line) {
    int b_zero = (b.w[0] == 0 && b.w[1] == 0 && b.w[2] == 0 && b.w[3] == 0);
    if (b_zero) { gray_panic_code_at(file, line, "P0078", "division by zero"); }
    if (a.w[1] == 0 && a.w[2] == 0 && a.w[3] == 0 &&
        b.w[1] == 0 && b.w[2] == 0 && b.w[3] == 0) {
        *q = GRAY_U256_ZERO; q->w[0] = a.w[0] / b.w[0];
        *rem = GRAY_U256_ZERO; rem->w[0] = a.w[0] % b.w[0];
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
        /* Get bit i of a */
        int word = i / 64;
        int bit = i % 64;
        remainder.w[0] |= (a.w[word] >> bit) & 1;
        /* if remainder >= b */
        bool ge = false;
        for (int k = 3; k >= 0; k--) {
            if (remainder.w[k] > b.w[k]) { ge = true; break; }
            if (remainder.w[k] < b.w[k]) break;
            if (k == 0) ge = true;
        }
        if (ge) {
            /* remainder -= b */
            uint64_t borrow = 0;
            for (int k = 0; k < 4; k++) {
                uint64_t diff = remainder.w[k] - b.w[k] - borrow;
                borrow = (remainder.w[k] < b.w[k]) || (remainder.w[k] - b.w[k] < borrow) ? 1 : 0;
                remainder.w[k] = diff;
            }
            quotient.w[word] |= (uint64_t)1 << bit;
        }
    }
    *q = quotient;
    *rem = remainder;
}

static inline bool gray_i256_is_neg(gray_i256 a) { return (int64_t)a.w[3] < 0; }

static inline gray_i256 gray_i256_div(gray_i256 a, gray_i256 b, const char *file, int line) {
    bool neg = false;
    gray_u256 ua, ub;
    if (gray_i256_is_neg(a)) { gray_i256 t = gray_i256_neg(a); memcpy(&ua, &t, sizeof(ua)); neg = !neg; }
    else { memcpy(&ua, &a, sizeof(ua)); }
    if (gray_i256_is_neg(b)) { gray_i256 t = gray_i256_neg(b); memcpy(&ub, &t, sizeof(ub)); neg = !neg; }
    else { memcpy(&ub, &b, sizeof(ub)); }
    gray_u256 q, rem;
    gray_u256_divmod(ua, ub, &q, &rem, file, line);
    gray_i256 result; memcpy(&result, &q, sizeof(result));
    return neg ? gray_i256_neg(result) : result;
}

static inline gray_i256 gray_i256_mod(gray_i256 a, gray_i256 b, const char *file, int line) {
    bool neg_a = gray_i256_is_neg(a);
    gray_u256 ua, ub;
    if (neg_a) { gray_i256 t = gray_i256_neg(a); memcpy(&ua, &t, sizeof(ua)); }
    else { memcpy(&ua, &a, sizeof(ua)); }
    if (gray_i256_is_neg(b)) { gray_i256 t = gray_i256_neg(b); memcpy(&ub, &t, sizeof(ub)); }
    else { memcpy(&ub, &b, sizeof(ub)); }
    gray_u256 q, rem;
    gray_u256_divmod(ua, ub, &q, &rem, file, line);
    gray_i256 result; memcpy(&result, &rem, sizeof(result));
    return neg_a ? gray_i256_neg(result) : result;
}

/* --- i256 Comparison --- */

static inline bool gray_i256_eq(gray_i256 a, gray_i256 b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3];
}
static inline bool gray_i256_ne(gray_i256 a, gray_i256 b) { return !gray_i256_eq(a, b); }
static inline bool gray_i256_lt(gray_i256 a, gray_i256 b) {
    if ((int64_t)a.w[3] != (int64_t)b.w[3]) return (int64_t)a.w[3] < (int64_t)b.w[3];
    if (a.w[2] != b.w[2]) return a.w[2] < b.w[2];
    if (a.w[1] != b.w[1]) return a.w[1] < b.w[1];
    return a.w[0] < b.w[0];
}
static inline bool gray_i256_gt(gray_i256 a, gray_i256 b) { return gray_i256_lt(b, a); }
static inline bool gray_i256_le(gray_i256 a, gray_i256 b) { return !gray_i256_gt(a, b); }
static inline bool gray_i256_ge(gray_i256 a, gray_i256 b) { return !gray_i256_lt(a, b); }

/* --- u256 Arithmetic --- */

static inline gray_u256 gray_u256_add(gray_u256 a, gray_u256 b) {
    gray_u256 r;
    uint64_t carry = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t sum = a.w[i] + b.w[i] + carry;
        carry = (sum < a.w[i] || (carry && sum == a.w[i])) ? 1 : 0;
        r.w[i] = sum;
    }
    return r;
}

static inline gray_u256 gray_u256_sub(gray_u256 a, gray_u256 b) {
    gray_u256 r;
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t diff = a.w[i] - b.w[i] - borrow;
        borrow = (a.w[i] < b.w[i]) || (a.w[i] - b.w[i] < borrow) ? 1 : 0;
        r.w[i] = diff;
    }
    return r;
}

static inline gray_u256 gray_u256_mul(gray_u256 a, gray_u256 b) {
    gray_u256 r = GRAY_U256_ZERO;
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4 - i; j++) {
            uint64_t a_lo = a.w[j] & 0xFFFFFFFF, a_hi = a.w[j] >> 32;
            uint64_t b_lo = b.w[i] & 0xFFFFFFFF, b_hi = b.w[i] >> 32;
            uint64_t p00 = a_lo * b_lo;
            uint64_t p01 = a_lo * b_hi;
            uint64_t p10 = a_hi * b_lo;
            uint64_t p11 = a_hi * b_hi;
            uint64_t mid = p01 + (p00 >> 32);
            uint64_t lo = a.w[j] * b.w[i];
            uint64_t hi_part = p11 + (mid >> 32) + (((mid & 0xFFFFFFFF) + p10) >> 32);

            uint64_t old = r.w[i + j];
            r.w[i + j] = old + lo + carry;
            carry = hi_part + (r.w[i + j] < old + lo ? 1 : 0);
            if (old + lo < old) carry++;
        }
    }
    return r;
}

static inline gray_u256 gray_u256_div(gray_u256 a, gray_u256 b, const char *file, int line) {
    gray_u256 q, rem;
    gray_u256_divmod(a, b, &q, &rem, file, line);
    return q;
}

static inline gray_u256 gray_u256_mod(gray_u256 a, gray_u256 b, const char *file, int line) {
    gray_u256 q, rem;
    gray_u256_divmod(a, b, &q, &rem, file, line);
    return rem;
}

/* --- u256 Comparison --- */

static inline bool gray_u256_eq(gray_u256 a, gray_u256 b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3];
}
static inline bool gray_u256_ne(gray_u256 a, gray_u256 b) { return !gray_u256_eq(a, b); }
static inline bool gray_u256_lt(gray_u256 a, gray_u256 b) {
    if (a.w[3] != b.w[3]) return a.w[3] < b.w[3];
    if (a.w[2] != b.w[2]) return a.w[2] < b.w[2];
    if (a.w[1] != b.w[1]) return a.w[1] < b.w[1];
    return a.w[0] < b.w[0];
}
static inline bool gray_u256_gt(gray_u256 a, gray_u256 b) { return gray_u256_lt(b, a); }
static inline bool gray_u256_le(gray_u256 a, gray_u256 b) { return !gray_u256_gt(a, b); }
static inline bool gray_u256_ge(gray_u256 a, gray_u256 b) { return !gray_u256_lt(a, b); }

/* --- Decimal String Constructors --- */

static inline gray_u128 gray_u128_from_decimal(const char *s) {
    gray_u128 result = GRAY_U128_ZERO;
    gray_u128 ten = {10, 0};
    while (*s) {
        if (*s == '_') { s++; continue; }
        if (*s < '0' || *s > '9') {
            gray_panic_code("P0102", "invalid digit '%c' in integer literal", *s);
        }
        result = gray_u128_mul(result, ten);
        gray_u128 digit = {(uint64_t)(*s - '0'), 0};
        result = gray_u128_add(result, digit);
        s++;
    }
    return result;
}

static inline gray_i128 gray_i128_from_decimal(const char *s) {
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    gray_u128 u = gray_u128_from_decimal(s);
    gray_i128 r;
    r.lo = u.lo;
    r.hi = (int64_t)u.hi;
    return neg ? gray_i128_neg(r) : r;
}

static inline gray_u256 gray_u256_from_decimal(const char *s) {
    gray_u256 result = GRAY_U256_ZERO;
    gray_u256 ten = GRAY_U256_ZERO;
    ten.w[0] = 10;
    while (*s) {
        if (*s == '_') { s++; continue; }
        if (*s < '0' || *s > '9') {
            gray_panic_code("P0102", "invalid digit '%c' in integer literal", *s);
        }
        result = gray_u256_mul(result, ten);
        gray_u256 digit = GRAY_U256_ZERO;
        digit.w[0] = (uint64_t)(*s - '0');
        result = gray_u256_add(result, digit);
        s++;
    }
    return result;
}

static inline gray_i256 gray_i256_from_decimal(const char *s) {
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    gray_u256 u = gray_u256_from_decimal(s);
    gray_i256 r;
    memcpy(&r, &u, sizeof(r));
    return neg ? gray_i256_neg(r) : r;
}

/* --- Overflow-Checked Arithmetic --- */

static inline gray_i128 gray_i128_add_checked(gray_i128 a, gray_i128 b, const char *file, int line) {
    gray_i128 r = gray_i128_add(a, b);
    if ((a.hi >= 0 && b.hi >= 0 && r.hi < 0) || (a.hi < 0 && b.hi < 0 && r.hi >= 0)) {
        gray_panic_code_at(file, line, "P0021", "i128 addition result is too large; value exceeds the range of i128");
    }
    return r;
}

static inline gray_i128 gray_i128_sub_checked(gray_i128 a, gray_i128 b, const char *file, int line) {
    gray_i128 r = gray_i128_sub(a, b);
    if ((a.hi >= 0 && b.hi < 0 && r.hi < 0) || (a.hi < 0 && b.hi >= 0 && r.hi >= 0)) {
        gray_panic_code_at(file, line, "P0022", "i128 subtraction result is too large; value exceeds the range of i128");
    }
    return r;
}

static inline gray_i128 gray_i128_mul_checked(gray_i128 a, gray_i128 b, const char *file, int line) {
    gray_i128 r = gray_i128_mul(a, b);
    bool a_zero = (a.hi == 0 && a.lo == 0);
    bool b_zero = (b.hi == 0 && b.lo == 0);
    if (!a_zero && !b_zero) {
        gray_i128 check = gray_i128_div(r, b, file, line);
        if (!gray_i128_eq(check, a)) {
            gray_panic_code_at(file, line, "P0023", "i128 multiplication result is too large; value exceeds the range of i128");
        }
    }
    return r;
}

static inline gray_u128 gray_u128_add_checked(gray_u128 a, gray_u128 b, const char *file, int line) {
    gray_u128 r = gray_u128_add(a, b);
    if (gray_u128_lt(r, a)) {
        gray_panic_code_at(file, line, "P0024", "u128 addition result is too large; value exceeds the range of u128");
    }
    return r;
}

static inline gray_u128 gray_u128_sub_checked(gray_u128 a, gray_u128 b, const char *file, int line) {
    if (gray_u128_lt(a, b)) {
        gray_panic_code_at(file, line, "P0025", "u128 subtraction result is negative, but u128 cannot hold negative values");
    }
    return gray_u128_sub(a, b);
}

static inline gray_u128 gray_u128_mul_checked(gray_u128 a, gray_u128 b, const char *file, int line) {
    gray_u128 r = gray_u128_mul(a, b);
    bool a_zero = (a.hi == 0 && a.lo == 0);
    bool b_zero = (b.hi == 0 && b.lo == 0);
    if (!a_zero && !b_zero) {
        gray_u128 check = gray_u128_div(r, b, file, line);
        if (!gray_u128_eq(check, a)) {
            gray_panic_code_at(file, line, "P0026", "u128 multiplication result is too large; value exceeds the range of u128");
        }
    }
    return r;
}

static inline gray_i256 gray_i256_add_checked(gray_i256 a, gray_i256 b, const char *file, int line) {
    gray_i256 r = gray_i256_add(a, b);
    bool a_neg = gray_i256_is_neg(a);
    bool b_neg = gray_i256_is_neg(b);
    bool r_neg = gray_i256_is_neg(r);
    if ((!a_neg && !b_neg && r_neg) || (a_neg && b_neg && !r_neg)) {
        gray_panic_code_at(file, line, "P0027", "i256 addition result is too large; value exceeds the range of i256");
    }
    return r;
}

static inline gray_i256 gray_i256_sub_checked(gray_i256 a, gray_i256 b, const char *file, int line) {
    gray_i256 r = gray_i256_sub(a, b);
    bool a_neg = gray_i256_is_neg(a);
    bool b_neg = gray_i256_is_neg(b);
    bool r_neg = gray_i256_is_neg(r);
    if ((!a_neg && b_neg && r_neg) || (a_neg && !b_neg && !r_neg)) {
        gray_panic_code_at(file, line, "P0028", "i256 subtraction result is too large; value exceeds the range of i256");
    }
    return r;
}

static inline gray_i256 gray_i256_mul_checked(gray_i256 a, gray_i256 b, const char *file, int line) {
    gray_i256 r = gray_i256_mul(a, b);
    bool a_zero = (a.w[0] == 0 && a.w[1] == 0 && a.w[2] == 0 && a.w[3] == 0);
    bool b_zero = (b.w[0] == 0 && b.w[1] == 0 && b.w[2] == 0 && b.w[3] == 0);
    if (!a_zero && !b_zero) {
        gray_i256 check = gray_i256_div(r, b, file, line);
        if (!gray_i256_eq(check, a)) {
            gray_panic_code_at(file, line, "P0029", "i256 multiplication result is too large; value exceeds the range of i256");
        }
    }
    return r;
}

static inline gray_u256 gray_u256_add_checked(gray_u256 a, gray_u256 b, const char *file, int line) {
    gray_u256 r = gray_u256_add(a, b);
    if (gray_u256_lt(r, a)) {
        gray_panic_code_at(file, line, "P0030", "u256 addition result is too large; value exceeds the range of u256");
    }
    return r;
}

static inline gray_u256 gray_u256_sub_checked(gray_u256 a, gray_u256 b, const char *file, int line) {
    if (gray_u256_lt(a, b)) {
        gray_panic_code_at(file, line, "P0031", "u256 subtraction result is negative, but u256 cannot hold negative values");
    }
    return gray_u256_sub(a, b);
}

static inline gray_u256 gray_u256_mul_checked(gray_u256 a, gray_u256 b, const char *file, int line) {
    gray_u256 r = gray_u256_mul(a, b);
    bool a_zero = (a.w[0] == 0 && a.w[1] == 0 && a.w[2] == 0 && a.w[3] == 0);
    bool b_zero = (b.w[0] == 0 && b.w[1] == 0 && b.w[2] == 0 && b.w[3] == 0);
    if (!a_zero && !b_zero) {
        gray_u256 check = gray_u256_div(r, b, file, line);
        if (!gray_u256_eq(check, a)) {
            gray_panic_code_at(file, line, "P0032", "u256 multiplication result is too large; value exceeds the range of u256");
        }
    }
    return r;
}

/* --- Printing (to_string) --- */

/* Helper: convert unsigned 128-bit to decimal string */
static inline GrayString gray_u128_to_string(GrayArena *arena, gray_u128 val) {
    if (val.hi == 0) {
        char buf[U128_MAX_DIGITS];
        snprintf(buf, sizeof(buf), "%" PRIu64, val.lo);
        size_t len = strlen(buf);
        char *s = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
        memcpy(s, buf, len + 1);
        return (GrayString){ s, (int32_t)len };
    }
    /* For large values, extract digits by repeated division */
    char buf[I256_MAX_DIGITS];
    int pos = I256_MAX_DIGITS - 1;
    buf[pos] = '\0';
    gray_u128 ten = { 10, 0 };
    gray_u128 v = val;
    while (v.hi != 0 || v.lo != 0) {
        gray_u128 q, rem;
        gray_u128_divmod(v, ten, &q, &rem, __FILE__, __LINE__);
        buf[--pos] = '0' + (char)rem.lo;
        v = q;
    }
    if (pos == I256_MAX_DIGITS - 1) buf[--pos] = '0';
    size_t len = (I256_MAX_DIGITS - 1) - pos;
    char *s = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
    memcpy(s, buf + pos, len + 1);
    return (GrayString){ s, (int32_t)len };
}

static inline GrayString gray_i128_to_string(GrayArena *arena, gray_i128 val) {
    if (val.hi < 0) {
        gray_i128 neg = gray_i128_neg(val);
        gray_u128 uval;
        uval.lo = neg.lo; uval.hi = (uint64_t)neg.hi;
        GrayString digits = gray_u128_to_string(arena, uval);
        char *s = (char *)gray_arena_alloc_uninitialized(arena, digits.len + 2);
        s[0] = '-';
        memcpy(s + 1, digits.data, digits.len + 1);
        return (GrayString){ s, digits.len + 1 };
    }
    gray_u128 uval;
    uval.lo = val.lo; uval.hi = (uint64_t)val.hi;
    return gray_u128_to_string(arena, uval);
}

/* Helper: convert unsigned 256-bit to decimal string */
static inline GrayString gray_u256_to_string(GrayArena *arena, gray_u256 val) {
    if (val.w[1] == 0 && val.w[2] == 0 && val.w[3] == 0) {
        char buf[U128_MAX_DIGITS];
        snprintf(buf, sizeof(buf), "%" PRIu64, val.w[0]);
        size_t len = strlen(buf);
        char *s = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
        memcpy(s, buf, len + 1);
        return (GrayString){ s, (int32_t)len };
    }
    char buf[U256_MAX_DIGITS];
    int pos = U256_MAX_DIGITS - 1;
    buf[pos] = '\0';
    gray_u256 ten = GRAY_U256_ZERO;
    ten.w[0] = 10;
    gray_u256 v = val;
    while (v.w[0] != 0 || v.w[1] != 0 || v.w[2] != 0 || v.w[3] != 0) {
        gray_u256 q, rem;
        gray_u256_divmod(v, ten, &q, &rem, __FILE__, __LINE__);
        buf[--pos] = '0' + (char)rem.w[0];
        v = q;
    }
    if (pos == U256_MAX_DIGITS - 1) buf[--pos] = '0';
    size_t len = (U256_MAX_DIGITS - 1) - pos;
    char *s = (char *)gray_arena_alloc_uninitialized(arena, len + 1);
    memcpy(s, buf + pos, len + 1);
    return (GrayString){ s, (int32_t)len };
}

static inline GrayString gray_i256_to_string(GrayArena *arena, gray_i256 val) {
    if (gray_i256_is_neg(val)) {
        gray_i256 neg = gray_i256_neg(val);
        gray_u256 uval; memcpy(&uval, &neg, sizeof(uval));
        GrayString digits = gray_u256_to_string(arena, uval);
        char *s = (char *)gray_arena_alloc_uninitialized(arena, digits.len + 2);
        s[0] = '-';
        memcpy(s + 1, digits.data, digits.len + 1);
        return (GrayString){ s, digits.len + 1 };
    }
    gray_u256 uval; memcpy(&uval, &val, sizeof(uval));
    return gray_u256_to_string(arena, uval);
}

#endif /* GRAY_BIGINT_H */
