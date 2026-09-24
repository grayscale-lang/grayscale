/*
 * atomic_builtin.c — Compiler-intrinsic implementation of the atomic
 * primitives declared in atomic.h.
 *
 * The hand-written arch/x86_64/atomic.s and arch/arm64/atomic.s follow the
 * System V AMD64 / AAPCS calling conventions, taking their arguments in
 * rdi/rsi/rdx. Windows x64 passes them in rcx/rdx/r8, so that assembly reads
 * the wrong registers there. It still assembles and links without a warning,
 * which makes the result silent corruption rather than a build failure.
 *
 * Rather than maintain a third assembly variant for the Windows ABI, this file
 * expresses the same operations with the compiler's __atomic builtins and lets
 * the backend emit correct code for whatever ABI it targets. Semantics match
 * the assembly exactly: add/sub/and/or/xor return the value held *before* the
 * operation, cas returns whether the swap happened, and every operation is
 * sequentially consistent.
 *
 * Author:  Aristomedes (@Aristomedes)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "atomic.h"

#define SEQUENTIALLY_CONSISTENT __ATOMIC_SEQ_CST

/* ── 64-bit ─────────────────────────────────────────────────────── */

int64_t gray_atomic_load(int64_t *target) {
    return __atomic_load_n(target, SEQUENTIALLY_CONSISTENT);
}

void gray_atomic_store(int64_t *target, int64_t value) {
    __atomic_store_n(target, value, SEQUENTIALLY_CONSISTENT);
}

int64_t gray_atomic_add(int64_t *target, int64_t value) {
    return __atomic_fetch_add(target, value, SEQUENTIALLY_CONSISTENT);
}

int64_t gray_atomic_sub(int64_t *target, int64_t value) {
    return __atomic_fetch_sub(target, value, SEQUENTIALLY_CONSISTENT);
}

int64_t gray_atomic_exchange(int64_t *target, int64_t value) {
    return __atomic_exchange_n(target, value, SEQUENTIALLY_CONSISTENT);
}

bool gray_atomic_cas(int64_t *target, int64_t expected, int64_t desired) {
    /* Strong CAS: no spurious failures, matching the cmpxchg in the assembly.
     * `expected` is by-value here, so the builtin writing the observed value
     * back into it is harmless. */
    return __atomic_compare_exchange_n(target, &expected, desired, false, SEQUENTIALLY_CONSISTENT, SEQUENTIALLY_CONSISTENT);
}

int64_t gray_atomic_and(int64_t *target, int64_t value) {
    return __atomic_fetch_and(target, value, SEQUENTIALLY_CONSISTENT);
}

int64_t gray_atomic_or(int64_t *target, int64_t value) {
    return __atomic_fetch_or(target, value, SEQUENTIALLY_CONSISTENT);
}

int64_t gray_atomic_xor(int64_t *target, int64_t value) {
    return __atomic_fetch_xor(target, value, SEQUENTIALLY_CONSISTENT);
}

/* ── 32-bit ─────────────────────────────────────────────────────── */

int32_t gray_atomic_load32(int32_t *target) {
    return __atomic_load_n(target, SEQUENTIALLY_CONSISTENT);
}

void gray_atomic_store32(int32_t *target, int32_t value) {
    __atomic_store_n(target, value, SEQUENTIALLY_CONSISTENT);
}

int32_t gray_atomic_add32(int32_t *target, int32_t value) {
    return __atomic_fetch_add(target, value, SEQUENTIALLY_CONSISTENT);
}

int32_t gray_atomic_sub32(int32_t *target, int32_t value) {
    return __atomic_fetch_sub(target, value, SEQUENTIALLY_CONSISTENT);
}

int32_t gray_atomic_exchange32(int32_t *target, int32_t value) {
    return __atomic_exchange_n(target, value, SEQUENTIALLY_CONSISTENT);
}

bool gray_atomic_cas32(int32_t *target, int32_t expected, int32_t desired) {
    return __atomic_compare_exchange_n(target, &expected, desired, false, SEQUENTIALLY_CONSISTENT, SEQUENTIALLY_CONSISTENT);
}

/* ── 8-bit ──────────────────────────────────────────────────────── */

uint8_t gray_atomic_load8(uint8_t *target) {
    return __atomic_load_n(target, SEQUENTIALLY_CONSISTENT);
}

void gray_atomic_store8(uint8_t *target, uint8_t value) {
    __atomic_store_n(target, value, SEQUENTIALLY_CONSISTENT);
}

uint8_t gray_atomic_exchange8(uint8_t *target, uint8_t value) {
    return __atomic_exchange_n(target, value, SEQUENTIALLY_CONSISTENT);
}

bool gray_atomic_cas8(uint8_t *target, uint8_t expected, uint8_t desired) {
    return __atomic_compare_exchange_n(target, &expected, desired, false, SEQUENTIALLY_CONSISTENT, SEQUENTIALLY_CONSISTENT);
}

/* ── Spinlock ───────────────────────────────────────────────────── */
/* 0 means free and 1 means held, matching the assembly. */

void gray_spin_lock(int32_t *lock) {
    while (__atomic_exchange_n(lock, 1, SEQUENTIALLY_CONSISTENT) != 0) {
        /* Spin until the previous value observed is 0. */
    }
}

bool gray_spin_trylock(int32_t *lock) {
    return __atomic_exchange_n(lock, 1, SEQUENTIALLY_CONSISTENT) == 0;
}

void gray_spin_unlock(int32_t *lock) {
    __atomic_store_n(lock, 0, SEQUENTIALLY_CONSISTENT);
}

/* ── Memory barrier ─────────────────────────────────────────────── */

void gray_atomic_fence(void) {
    __atomic_thread_fence(SEQUENTIALLY_CONSISTENT);
}
