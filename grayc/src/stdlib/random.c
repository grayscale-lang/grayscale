/*
 * random.c — Implementation of the random stdlib module.
 * Provides pseudo-random number generation for floats and integers,
 * array shuffling, random element selection, and manual seeding.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

/* Must precede every <stdlib.h> inclusion (also transitive ones via random.h)
 * so the CRT declares rand_s, the Windows entropy source. */
#ifdef _WIN32
#define _CRT_RAND_S
#include <stdlib.h>
#endif

#include "random.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* arc4random_buf is hidden by _POSIX_C_SOURCE on Apple/BSD — declare explicitly */
#if defined(__APPLE__) || defined(__FreeBSD__)
void arc4random_buf(void *buf, size_t nbytes);
#endif

static bool _seeded = false;
static bool _user_seeded = false;
static void ensure_seed(void) {
    if (!_seeded) {
        unsigned seed;
#if defined(__APPLE__) || defined(__FreeBSD__)
        arc4random_buf(&seed, sizeof(seed));
#elif defined(_WIN32)
        /* rand_s is RtlGenRandom under the hood: CSPRNG, no extra link lib. */
        if (rand_s(&seed) != 0) seed = (unsigned)time(NULL) ^ (unsigned)getpid();
#else
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) { fread(&seed, sizeof(seed), 1, f); fclose(f); }
        else { seed = (unsigned)time(NULL) ^ (unsigned)getpid(); }
#endif
        srand(seed);
        _seeded = true;
    }
}

void gray_random_seed(int64_t value) {
    srand((unsigned)value);
    _seeded = true;
    _user_seeded = true;
}

double gray_random_float_unit(void) {
    ensure_seed();
    return (double)rand() / RAND_MAX;
}

double gray_random_float_range(double min, double max) {
    return min + gray_random_float_unit() * (max - min);
}

/* Generate a uniform random uint64_t across the full 64-bit range.
 * When the user has explicitly seeded via random.seed(), use srand/rand
 * so the sequence is deterministic.  Otherwise use OS entropy. */
static uint64_t rand64(void) {
    if (_user_seeded) {
        return ((uint64_t)(unsigned)rand() << 33) ^
               ((uint64_t)(unsigned)rand() << 2)  ^
               ((uint64_t)(unsigned)rand());
    }
    uint64_t v;
#if defined(__APPLE__) || defined(__FreeBSD__)
    arc4random_buf(&v, sizeof(v));
#elif defined(_WIN32)
    unsigned int hi, lo;
    if (rand_s(&hi) == 0 && rand_s(&lo) == 0) {
        v = ((uint64_t)hi << 32) | lo;
    } else {
        v = ((uint64_t)(unsigned)rand() << 33) ^
            ((uint64_t)(unsigned)rand() << 2)  ^
            ((uint64_t)(unsigned)rand());
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { fread(&v, sizeof(v), 1, f); fclose(f); }
    else {
        v = ((uint64_t)(unsigned)rand() << 33) ^
            ((uint64_t)(unsigned)rand() << 2)  ^
            ((uint64_t)(unsigned)rand());
    }
#endif
    return v;
}

int64_t gray_random_int_max(int64_t max) {
    ensure_seed();
    if (max <= 0) return 0;
    return (int64_t)(rand64() % (uint64_t)max);
}

int64_t gray_random_int_range(int64_t min, int64_t max) {
    ensure_seed();
    if (min >= max) return min;
    return min + (int64_t)(rand64() % (uint64_t)(max - min));
}

bool gray_random_bool(void) {
    ensure_seed();
    return rand() % 2 == 0;
}

uint8_t gray_random_byte(void) {
    ensure_seed();
    return (uint8_t)(rand() % 256);
}

int32_t gray_random_char(void) {
    ensure_seed();
    return (int32_t)(32 + rand() % 95); /* printable ASCII */
}

int32_t gray_random_char_range(int32_t min, int32_t max) {
    ensure_seed();
    if (min >= max) return min;
    return min + (int32_t)(rand() % (max - min));
}

GrayArray gray_random_shuffle(GrayArena *arena, GrayArray *arr) {
    ensure_seed();
    GrayArray result = gray_array_copy(arena, arr);
    char *data = (char *)result.data;
    size_t element_size = (size_t)result.elem_size;
    /* Scratch slot sized to the actual element width. The previous
     * fixed char tmp[64] overflowed the stack for any element type
     * larger than 64 bytes (struct arrays, nested arrays/maps). */
    void *tmp = gray_arena_alloc(arena, element_size);
    for (int32_t i = result.len - 1; i > 0; i--) {
        int32_t j = rand() % (i + 1);
        memcpy(tmp, data + i * element_size, element_size);
        memcpy(data + i * element_size, data + j * element_size, element_size);
        memcpy(data + j * element_size, tmp, element_size);
    }
    return result;
}

GrayArray gray_random_sample(GrayArena *arena, GrayArray *arr, int32_t n) {
    if (n > arr->len)
        gray_panic_code("P0062", "random.sample() count %d exceeds array length %d", (int)n, (int)arr->len);
    if (n < 0)
        gray_panic_code("P0063", "random.sample() count cannot be negative (%d)", (int)n);
    GrayArray shuffled = gray_random_shuffle(arena, arr);
    shuffled.len = n;
    return shuffled;
}

