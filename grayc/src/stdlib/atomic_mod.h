/*
 * atomic_mod.h — Public interface for the atomic stdlib module.
 * Lock-free atomic operations (load, store, add, sub, CAS, bitwise)
 * backed by hand-written ARM64 and x86_64 assembly.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_ATOMIC_MOD_H
#define GRAY_ATOMIC_MOD_H

#include "../runtime/atomic.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *_internal; /* int32_t* lock value */
} GraySpinLock;

/*@man load
 *@module atomic
 *@group 64-bit Atomics
 *@sig load(ptr ^i64) -> i64
 *@desc Atomically load a value.
 *@example
 *   import @atomic
 *   mut val i64 = atomic.load(ptr)
 *@end
 */
/*@man store
 *@module atomic
 *@group 64-bit Atomics
 *@sig store(ptr ^i64, val i64)
 *@desc Atomically store a value.
 *@example
 *   import @atomic
 *   atomic.store(ptr, 42)
 *@end
 */
/*@man add
 *@module atomic
 *@group 64-bit Atomics
 *@sig add(ptr ^i64, val i64) -> i64
 *@desc Atomic add. Returns the previous value.
 *@example
 *   import @atomic
 *   mut old i64 = atomic.add(ptr, 1)
 *@end
 */
/*@man sub
 *@module atomic
 *@group 64-bit Atomics
 *@sig sub(ptr ^i64, val i64) -> i64
 *@desc Atomic subtract. Returns the previous value.
 *@example
 *   import @atomic
 *   mut old i64 = atomic.sub(ptr, 1)
 *@end
 */
/*@man exchange
 *@module atomic
 *@group 64-bit Atomics
 *@sig exchange(ptr ^i64, val i64) -> i64
 *@desc Atomic swap. Returns the previous value.
 *@example
 *   import @atomic
 *   mut old i64 = atomic.exchange(ptr, 99)
 *@end
 */
/*@man cas
 *@module atomic
 *@group 64-bit Atomics
 *@sig cas(ptr ^i64, expected i64, desired i64) -> bool
 *@desc Compare-and-swap. Returns true if the swap succeeded.
 *@example
 *   import @atomic
 *   if atomic.cas(ptr, 0, 1) {
 *       println("swapped")
 *   }
 *@end
 */
/*@man and
 *@module atomic
 *@group 64-bit Atomics
 *@sig and(ptr ^i64, val i64) -> i64
 *@desc Atomic bitwise AND. Returns the previous value.
 *@example
 *   import @atomic
 *   mut old i64 = atomic.and(ptr, 0xFF)
 *@end
 */
/*@man or
 *@module atomic
 *@group 64-bit Atomics
 *@sig or(ptr ^i64, val i64) -> i64
 *@desc Atomic bitwise OR. Returns the previous value.
 *@example
 *   import @atomic
 *   mut old i64 = atomic.or(ptr, 0x01)
 *@end
 */
/*@man xor
 *@module atomic
 *@group 64-bit Atomics
 *@sig xor(ptr ^i64, val i64) -> i64
 *@desc Atomic bitwise XOR. Returns the previous value.
 *@example
 *   import @atomic
 *   mut old i64 = atomic.xor(ptr, 0xFF)
 *@end
 */
/* 64-bit atomics */
int64_t gray_atomic_mod_load(int64_t *target);
void    gray_atomic_mod_store(int64_t *target, int64_t value);
int64_t gray_atomic_mod_add(int64_t *target, int64_t value);
int64_t gray_atomic_mod_sub(int64_t *target, int64_t value);
int64_t gray_atomic_mod_exchange(int64_t *target, int64_t value);
bool    gray_atomic_mod_cas(int64_t *target, int64_t expected, int64_t desired);
int64_t gray_atomic_mod_and(int64_t *target, int64_t value);
int64_t gray_atomic_mod_or(int64_t *target, int64_t value);
int64_t gray_atomic_mod_xor(int64_t *target, int64_t value);

/*@man spinlock
 *@module atomic
 *@group Spinlock
 *@sig spinlock() -> SpinLock
 *@desc Create a new spinlock.
 *@example
 *   import @atomic
 *   mut lk SpinLock = atomic.spinlock()
 *@end
 */
/*@man spin_lock
 *@module atomic
 *@group Spinlock
 *@sig spin_lock(lk SpinLock)
 *@desc Acquire a spinlock. Spins until acquired.
 *@example
 *   import @atomic
 *   atomic.spin_lock(lk)
 *   // critical section
 *   atomic.spin_unlock(lk)
 *@end
 */
/*@man spin_trylock
 *@module atomic
 *@group Spinlock
 *@sig spin_trylock(lk SpinLock) -> bool
 *@desc Try to acquire a spinlock without blocking. Returns true if acquired.
 *@example
 *   import @atomic
 *   if atomic.spin_trylock(lk) {
 *       // critical section
 *       atomic.spin_unlock(lk)
 *   }
 *@end
 */
/*@man spin_unlock
 *@module atomic
 *@group Spinlock
 *@sig spin_unlock(lk SpinLock)
 *@desc Release a spinlock.
 *@example
 *   import @atomic
 *   atomic.spin_unlock(lk)
 *@end
 */
/*@man spinlock_destroy
 *@module atomic
 *@group Spinlock
 *@sig spinlock_destroy(lk SpinLock)
 *@desc Destroy a spinlock and free its resources.
 *@example
 *   import @atomic
 *   atomic.spinlock_destroy(lk)
 *@end
 */

/* Spinlock */
GraySpinLock gray_atomic_mod_spinlock(void);
void       gray_atomic_mod_spinlock_destroy(GraySpinLock *lock);
void       gray_atomic_mod_spin_lock(GraySpinLock lock);
bool       gray_atomic_mod_spin_trylock(GraySpinLock lock);
void       gray_atomic_mod_spin_unlock(GraySpinLock lock);

/*@man fence
 *@module atomic
 *@group Memory Barrier
 *@sig fence()
 *@desc Full memory barrier with sequential consistency.
 *@example
 *   import @atomic
 *   atomic.fence()
 *@end
 */
/* Memory barrier */
void gray_atomic_mod_fence(void);

#endif
