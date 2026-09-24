/*
 * random.c — Implementation of the random stdlib module.
 * Provides pseudo-random number generation for floating-point numbers and integers,
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
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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
        if (rand_s(&seed) != 0) seed = (unsigned)time(NULL) ^ (unsigned)_getpid();
#else
        FILE *urandom = fopen("/dev/urandom", "rb");
        if (urandom) { fread(&seed, sizeof(seed), 1, urandom); fclose(urandom); }
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

double gray_random_f64_unit(void) {
    ensure_seed();
    return (double)rand() / RAND_MAX;
}

double gray_random_f64_range(double minimum, double maximum) {
    return minimum + gray_random_f64_unit() * (maximum - minimum);
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
    uint64_t bits;
#if defined(__APPLE__) || defined(__FreeBSD__)
    arc4random_buf(&bits, sizeof(bits));
#elif defined(_WIN32)
    unsigned int high_bits, lo;
    if (rand_s(&high_bits) == 0 && rand_s(&lo) == 0) {
        bits = ((uint64_t)high_bits << 32) | lo;
    } else {
        bits = ((uint64_t)(unsigned)rand() << 33) ^
            ((uint64_t)(unsigned)rand() << 2)  ^
            ((uint64_t)(unsigned)rand());
    }
#else
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) { fread(&bits, sizeof(bits), 1, urandom); fclose(urandom); }
    else {
        bits = ((uint64_t)(unsigned)rand() << 33) ^
            ((uint64_t)(unsigned)rand() << 2)  ^
            ((uint64_t)(unsigned)rand());
    }
#endif
    return bits;
}

int64_t gray_random_i64_max(int64_t maximum) {
    ensure_seed();
    if (maximum <= 0) return 0;
    return (int64_t)(rand64() % (uint64_t)maximum);
}

int64_t gray_random_i64_range(int64_t minimum, int64_t maximum) {
    ensure_seed();
    if (minimum >= maximum) return minimum;
    return minimum + (int64_t)(rand64() % (uint64_t)(maximum - minimum));
}

bool gray_random_bool(void) {
    ensure_seed();
    return rand() % 2 == 0;
}

uint8_t gray_random_u8(void) {
    ensure_seed();
    return (uint8_t)(rand() % 256);
}

int32_t gray_random_char(void) {
    ensure_seed();
    return (int32_t)(32 + rand() % 95); /* printable ASCII */
}

int32_t gray_random_char_range(int32_t minimum, int32_t maximum) {
    ensure_seed();
    if (minimum >= maximum) return minimum;
    return minimum + (int32_t)(rand() % (maximum - minimum));
}

GrayString gray_random_string(GrayArena *arena, int64_t length, GrayString alphabet) {
    if (length <= 0) return gray_string_lit("");
    if (alphabet.len == 0) {
        gray_panic_code("P0123",
            "random.rand_string: alphabet is empty but length is %lld", (long long)length);
    }
    ensure_seed();
    char *buffer = gray_arena_alloc_uninitialized(arena, (size_t)length + 1);
    for (int64_t i = 0; i < length; i++) {
        buffer[i] = alphabet.data[rand() % alphabet.len];
    }
    buffer[length] = '\0';
    return gray_string_new(arena, buffer, (int32_t)length);
}

GrayArray gray_random_shuffle(GrayArena *arena, GrayArray *array) {
    ensure_seed();
    GrayArray result = gray_array_copy(arena, array);
    char *data = (char *)result.data;
    size_t element_size = (size_t)result.elem_size;
    /* Scratch slot sized to the actual element width. The previous
     * fixed char tmp[64] overflowed the stack for any element type
     * larger than 64 bytes (struct arrays, nested arrays/maps). */
    void *scratch = gray_arena_alloc_uninitialized(arena, element_size);
    for (int32_t i = result.len - 1; i > 0; i--) {
        int32_t swap_index = rand() % (i + 1);
        memcpy(scratch, data + i * element_size, element_size);
        memcpy(data + i * element_size, data + swap_index * element_size, element_size);
        memcpy(data + swap_index * element_size, scratch, element_size);
    }
    return result;
}

GrayArray gray_random_sample(GrayArena *arena, GrayArray *array, int64_t count) {
    if (count > array->len)
        gray_panic_code("P0062", "random.sample() count %lld exceeds array length %d", (long long)count, (int)array->len);
    if (count < 0)
        gray_panic_code("P0063", "random.sample() count cannot be negative (%lld)", (long long)count);
    GrayArray shuffled = gray_random_shuffle(arena, array);
    shuffled.len = (int32_t)count;
    return shuffled;
}

