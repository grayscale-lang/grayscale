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

int64_t gray_math_random_int(int64_t min, int64_t max) {
    ensure_seeded();
    if (min >= max) return min;
    return min + (int64_t)(rand() % (int)(max - min));
}

double gray_math_random_float(double min, double max) {
    ensure_seeded();
    return min + ((double)rand() / RAND_MAX) * (max - min);
}

int64_t gray_math_factorial(int64_t n) {
    if (n < 0) gray_panic_code("P0070", "math.factorial() requires a non-negative integer, got %lld", (long long)n);
    if (n <= 1) return 1;
    int64_t result = 1;
    for (int64_t i = 2; i <= n; i++) result *= i;
    return result;
}

int64_t gray_math_gcd(int64_t left, int64_t right) {
    if (left < 0) left = -left;
    if (right < 0) right = -right;
    while (right != 0) { int64_t temp = right; right = left % right; left = temp; }
    return left;
}

int64_t gray_math_lcm(int64_t left, int64_t right) {
    if (left == 0 || right == 0) return 0;
    int64_t divisor = gray_math_gcd(left, right);
    return (left / divisor) * right;
}

GrayMathModf gray_math_modf(double value) {
    double integral;
    double frac = modf(value, &integral);
    GrayMathModf result = { integral, frac };
    return result;
}

bool gray_math_is_power_of_two(int64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

int64_t gray_math_next_power_of_two(int64_t n) {
    if (n <= 1) return 1;
    /* 2^62 is the largest power of two an int64 holds; anything above it
     * would round up past MAX_INT. */
    if (n > (int64_t)1 << 62) {
        gray_panic_code("P0106",
            "math.next_power_of_two() result is too large for int, got %lld", (long long)n);
    }
    int64_t power = 1;
    while (power < n) power <<= 1;
    return power;
}

bool gray_math_is_prime(int64_t n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (int64_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return false;
    }
    return true;
}
