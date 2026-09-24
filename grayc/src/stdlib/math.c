/*
 * math.c — Implementation of the math stdlib module.
 * Provides factorial, GCD/LCM, primality testing, and random number
 * generation (non-inline functions).
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "math.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* arc4random_buf is hidden by _POSIX_C_SOURCE on Apple/BSD — declare explicitly */
#if defined(__APPLE__) || defined(__FreeBSD__)
void arc4random_buf(void *buf, size_t nbytes);
#endif

static bool _rand_seeded = false;
static void ensure_seeded(void) {
    if (!_rand_seeded) {
        unsigned seed;
#if defined(__APPLE__) || defined(__FreeBSD__)
        arc4random_buf(&seed, sizeof(seed));
#else
        FILE *urandom = fopen("/dev/urandom", "rb");
        if (urandom) { fread(&seed, sizeof(seed), 1, urandom); fclose(urandom); }
        else { seed = (unsigned)time(NULL) ^ (unsigned)getpid(); }
#endif
        srand(seed);
        _rand_seeded = true;
    }
}

int64_t gray_math_random_int(int64_t minimum, int64_t maximum) {
    ensure_seeded();
    if (minimum >= maximum) return minimum;
    return minimum + (int64_t)(rand() % (int)(maximum - minimum));
}

double gray_math_random_float(double minimum, double maximum) {
    ensure_seeded();
    return minimum + ((double)rand() / RAND_MAX) * (maximum - minimum);
}

int64_t gray_math_factorial(int64_t number) {
    if (number < 0) gray_panic_code("P0070", "math.factorial() requires a non-negative integer, got %lld", (long long)number);
    if (number <= 1) return 1;
    int64_t result = 1;
    for (int64_t i = 2; i <= number; i++)
        if (__builtin_mul_overflow(result, i, &result))
            gray_panic_code("P0006", "multiplication result is too large; value exceeds the range of i64");
    return result;
}

/* Works on the magnitudes as uint64_t so the most negative i64 has one; only
 * a result that is itself 2^63 does not fit back in i64. */
int64_t gray_math_gcd(int64_t left, int64_t right) {
    uint64_t first = left < 0 ? 0 - (uint64_t)left : (uint64_t)left;
    uint64_t second = right < 0 ? 0 - (uint64_t)right : (uint64_t)right;
    while (second != 0) { uint64_t remainder = second; second = first % second; first = remainder; }
    if (first > (uint64_t)INT64_MAX)
        gray_panic_code("P0007", "negation result is too large; value exceeds the range of i64");
    return (int64_t)first;
}

int64_t gray_math_lcm(int64_t left, int64_t right) {
    if (left == 0 || right == 0) return 0;
    int64_t divisor = gray_math_gcd(left, right);
    int64_t result;
    if (__builtin_mul_overflow(left / divisor, right, &result))
        gray_panic_code("P0006", "multiplication result is too large; value exceeds the range of i64");
    return result;
}

GrayMathModf gray_math_modf(double value) {
    double integral;
    double frac = modf(value, &integral);
    GrayMathModf result = { integral, frac };
    return result;
}

bool gray_math_is_power_of_two(int64_t number) {
    return number > 0 && (number & (number - 1)) == 0;
}

int64_t gray_math_next_power_of_two(int64_t number) {
    if (number <= 1) return 1;
    /* 2^62 is the largest power of two an int64 holds; anything above it
     * would round up past MAX_I64. */
    if (number > (int64_t)1 << 62) {
        gray_panic_code("P0106",
            "math.next_power_of_two() result is too large for i64, got %lld", (long long)number);
    }
    int64_t power = 1;
    while (power < number) power <<= 1;
    return power;
}

bool gray_math_is_prime(int64_t number) {
    if (number < 2) return false;
    if (number < 4) return true;
    if (number % 2 == 0 || number % 3 == 0) return false;
    for (int64_t i = 5; i * i <= number; i += 6) {
        if (number % i == 0 || number % (i + 2) == 0) return false;
    }
    return true;
}
