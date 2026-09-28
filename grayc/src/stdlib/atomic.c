/*
 * atomic.c — Implementation of the atomic stdlib module.
 * Thin wrappers around the runtime's assembly-backed atomic
 * primitives, exposing load, store, add, sub, CAS, and bitwise
 * operations to Grayscale code.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "atomic_mod.h"
#include <stdlib.h>

/* 64-bit atomics */

int64_t gray_atomic_mod_load(int64_t *target) { return gray_atomic_load(target); }
void    gray_atomic_mod_store(int64_t *target, int64_t value) { gray_atomic_store(target, value); }
int64_t gray_atomic_mod_add(int64_t *target, int64_t value) { return gray_atomic_add(target, value); }
int64_t gray_atomic_mod_sub(int64_t *target, int64_t value) { return gray_atomic_sub(target, value); }
int64_t gray_atomic_mod_exchange(int64_t *target, int64_t value) { return gray_atomic_exchange(target, value); }
bool    gray_atomic_mod_cas(int64_t *target, int64_t expected, int64_t desired) { return gray_atomic_cas(target, expected, desired); }
int64_t gray_atomic_mod_and(int64_t *target, int64_t value) { return gray_atomic_and(target, value); }
int64_t gray_atomic_mod_or(int64_t *target, int64_t value) { return gray_atomic_or(target, value); }
int64_t gray_atomic_mod_xor(int64_t *target, int64_t value) { return gray_atomic_xor(target, value); }

/* Spinlock */

GraySpinLock gray_atomic_mod_spinlock(void) {
    int32_t *lock = malloc(sizeof(int32_t));
    *lock = 0;
    GraySpinLock result;
    result._internal = lock;
    return result;
}

void gray_atomic_mod_spinlock_destroy(GraySpinLock *lock) {
    if (lock->_internal) {
        free(lock->_internal);
        lock->_internal = NULL;
    }
}

void gray_atomic_mod_spin_lock(GraySpinLock lock) {
    if (lock._internal) {
        gray_spin_lock((int32_t *)lock._internal);
    }
}

bool gray_atomic_mod_spin_trylock(GraySpinLock lock) {
    if (lock._internal) {
        return gray_spin_trylock((int32_t *)lock._internal);
    }
    return false;
}

void gray_atomic_mod_spin_unlock(GraySpinLock lock) {
    if (lock._internal) {
        gray_spin_unlock((int32_t *)lock._internal);
    }
}

/* Memory barrier */

void gray_atomic_mod_fence(void) { gray_atomic_fence(); }
