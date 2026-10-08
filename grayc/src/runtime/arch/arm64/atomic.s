# atomic.s — ARM64 (AArch64) lock-free atomic primitives for the Grayscale runtime.
# Provides load, store, add, sub, CAS, exchange, bitwise, fence, and spinlock operations
# at 64-bit, 32-bit, and 8-bit widths using sequential consistency (dmb ish barriers).
#
# Author:  Marshall A Burns (@SchoolyB)
# Copyright (c) 2025-Present Marshall A Burns
# Licensed under the MIT License. See LICENSE for details.
#
# Calling convention: AAPCS64
#   arg1 = x0  (pointer to atomic value)
#   arg2 = x1  (value / expected)
#   arg3 = x2  (desired, for CAS)
#   return = x0

.text
.p2align 2

# ─────────────────────────────────────────────
# int64_t gray_atomic_load(int64_t *ptr)
# ─────────────────────────────────────────────
.globl _gray_atomic_load
.globl gray_atomic_load
_gray_atomic_load:
gray_atomic_load:
    dmb     ish
    ldr     x0, [x0]
    dmb     ish
    ret

# ─────────────────────────────────────────────
# void gray_atomic_store(int64_t *ptr, int64_t val)
# ─────────────────────────────────────────────
.globl _gray_atomic_store
.globl gray_atomic_store
_gray_atomic_store:
gray_atomic_store:
    dmb     ish
    str     x1, [x0]
    dmb     ish
    ret

# ─────────────────────────────────────────────
# int64_t gray_atomic_add(int64_t *ptr, int64_t val)
# Returns the previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_add
.globl gray_atomic_add
_gray_atomic_add:
gray_atomic_add:
1:
    ldxr    x2, [x0]           // exclusive load old value
    add     x3, x2, x1         // new = old + val
    stlxr   w4, x3, [x0]       // exclusive store with release
    cbnz    w4, 1b              // retry if store failed
    dmb     ish
    mov     x0, x2              // return old value
    ret

# ─────────────────────────────────────────────
# int64_t gray_atomic_sub(int64_t *ptr, int64_t val)
# Returns the previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_sub
.globl gray_atomic_sub
_gray_atomic_sub:
gray_atomic_sub:
1:
    ldxr    x2, [x0]           // exclusive load old value
    sub     x3, x2, x1         // new = old - val
    stlxr   w4, x3, [x0]       // exclusive store with release
    cbnz    w4, 1b              // retry if store failed
    dmb     ish
    mov     x0, x2              // return old value
    ret

# ─────────────────────────────────────────────
# bool gray_atomic_cas(int64_t *ptr, int64_t expected, int64_t desired)
# Compare-and-swap. Returns 1 on success, 0 on failure.
# ─────────────────────────────────────────────
.globl _gray_atomic_cas
.globl gray_atomic_cas
_gray_atomic_cas:
gray_atomic_cas:
1:
    ldxr    x3, [x0]           // exclusive load current value
    cmp     x3, x1             // compare with expected
    b.ne    2f                  // if not equal, fail
    stlxr   w4, x2, [x0]       // try store desired
    cbnz    w4, 1b              // retry if exclusive store failed
    dmb     ish
    mov     x0, #1              // success
    ret
2:
    clrex                       // clear exclusive monitor
    dmb     ish                 // enforce seq_cst on failure
    mov     x0, #0              // failure
    ret

# ─────────────────────────────────────────────
# int64_t gray_atomic_exchange(int64_t *ptr, int64_t val)
# Atomically swap *ptr with val. Returns old value.
# ─────────────────────────────────────────────
.globl _gray_atomic_exchange
.globl gray_atomic_exchange
_gray_atomic_exchange:
gray_atomic_exchange:
1:
    ldxr    x2, [x0]           // exclusive load old value
    stlxr   w3, x1, [x0]       // exclusive store new value
    cbnz    w3, 1b              // retry if store failed
    dmb     ish
    mov     x0, x2              // return old value
    ret

# ─────────────────────────────────────────────
# int64_t gray_atomic_and(int64_t *ptr, int64_t val)
# Atomic bitwise AND. Returns previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_and
.globl gray_atomic_and
_gray_atomic_and:
gray_atomic_and:
1:
    ldxr    x2, [x0]
    and     x3, x2, x1
    stlxr   w4, x3, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     x0, x2
    ret

# ─────────────────────────────────────────────
# int64_t gray_atomic_or(int64_t *ptr, int64_t val)
# Atomic bitwise OR. Returns previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_or
.globl gray_atomic_or
_gray_atomic_or:
gray_atomic_or:
1:
    ldxr    x2, [x0]
    orr     x3, x2, x1
    stlxr   w4, x3, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     x0, x2
    ret

# ─────────────────────────────────────────────
# int64_t gray_atomic_xor(int64_t *ptr, int64_t val)
# Atomic bitwise XOR. Returns previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_xor
.globl gray_atomic_xor
_gray_atomic_xor:
gray_atomic_xor:
1:
    ldxr    x2, [x0]
    eor     x3, x2, x1
    stlxr   w4, x3, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     x0, x2
    ret

# ─────────────────────────────────────────────
# void gray_atomic_fence(void)
# Full memory barrier.
# ─────────────────────────────────────────────
.globl _gray_atomic_fence
.globl gray_atomic_fence
_gray_atomic_fence:
gray_atomic_fence:
    dmb     ish
    ret

# =============================================================================
# 32-bit Atomic Operations
# =============================================================================

# ─────────────────────────────────────────────
# int32_t gray_atomic_load32(int32_t *ptr)
# ─────────────────────────────────────────────
.globl _gray_atomic_load32
.globl gray_atomic_load32
_gray_atomic_load32:
gray_atomic_load32:
    dmb     ish
    ldr     w0, [x0]
    dmb     ish
    ret

# ─────────────────────────────────────────────
# void gray_atomic_store32(int32_t *ptr, int32_t val)
# ─────────────────────────────────────────────
.globl _gray_atomic_store32
.globl gray_atomic_store32
_gray_atomic_store32:
gray_atomic_store32:
    dmb     ish
    str     w1, [x0]
    dmb     ish
    ret

# ─────────────────────────────────────────────
# int32_t gray_atomic_add32(int32_t *ptr, int32_t val)
# Returns the previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_add32
.globl gray_atomic_add32
_gray_atomic_add32:
gray_atomic_add32:
1:
    ldxr    w2, [x0]
    add     w3, w2, w1
    stlxr   w4, w3, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     w0, w2
    ret

# ─────────────────────────────────────────────
# int32_t gray_atomic_sub32(int32_t *ptr, int32_t val)
# Returns the previous value.
# ─────────────────────────────────────────────
.globl _gray_atomic_sub32
.globl gray_atomic_sub32
_gray_atomic_sub32:
gray_atomic_sub32:
1:
    ldxr    w2, [x0]
    sub     w3, w2, w1
    stlxr   w4, w3, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     w0, w2
    ret

# ─────────────────────────────────────────────
# bool gray_atomic_cas32(int32_t *ptr, int32_t expected, int32_t desired)
# Compare-and-swap. Returns 1 on success, 0 on failure.
# ─────────────────────────────────────────────
.globl _gray_atomic_cas32
.globl gray_atomic_cas32
_gray_atomic_cas32:
gray_atomic_cas32:
1:
    ldxr    w3, [x0]
    cmp     w3, w1
    b.ne    2f
    stlxr   w4, w2, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     w0, #1
    ret
2:
    clrex
    dmb     ish                 // enforce seq_cst on failure
    mov     w0, #0
    ret

# ─────────────────────────────────────────────
# int32_t gray_atomic_exchange32(int32_t *ptr, int32_t val)
# Atomically swap *ptr with val. Returns old value.
# ─────────────────────────────────────────────
.globl _gray_atomic_exchange32
.globl gray_atomic_exchange32
_gray_atomic_exchange32:
gray_atomic_exchange32:
1:
    ldxr    w2, [x0]
    stlxr   w3, w1, [x0]
    cbnz    w3, 1b
    dmb     ish
    mov     w0, w2
    ret

# =============================================================================
# 8-bit Atomic Operations
# =============================================================================

# ─────────────────────────────────────────────
# uint8_t gray_atomic_load8(uint8_t *ptr)
# ─────────────────────────────────────────────
.globl _gray_atomic_load8
.globl gray_atomic_load8
_gray_atomic_load8:
gray_atomic_load8:
    dmb     ish
    ldrb    w0, [x0]
    dmb     ish
    ret

# ─────────────────────────────────────────────
# void gray_atomic_store8(uint8_t *ptr, uint8_t val)
# ─────────────────────────────────────────────
.globl _gray_atomic_store8
.globl gray_atomic_store8
_gray_atomic_store8:
gray_atomic_store8:
    dmb     ish
    strb    w1, [x0]
    dmb     ish
    ret

# ─────────────────────────────────────────────
# bool gray_atomic_cas8(uint8_t *ptr, uint8_t expected, uint8_t desired)
# Compare-and-swap. Returns 1 on success, 0 on failure.
# ─────────────────────────────────────────────
.globl _gray_atomic_cas8
.globl gray_atomic_cas8
_gray_atomic_cas8:
gray_atomic_cas8:
1:
    ldxrb   w3, [x0]
    cmp     w3, w1
    b.ne    2f
    stlxrb  w4, w2, [x0]
    cbnz    w4, 1b
    dmb     ish
    mov     w0, #1
    ret
2:
    clrex
    dmb     ish                 // enforce seq_cst on failure
    mov     w0, #0
    ret

# ─────────────────────────────────────────────
# uint8_t gray_atomic_exchange8(uint8_t *ptr, uint8_t val)
# Atomically swap *ptr with val. Returns old value.
# ─────────────────────────────────────────────
.globl _gray_atomic_exchange8
.globl gray_atomic_exchange8
_gray_atomic_exchange8:
gray_atomic_exchange8:
1:
    ldxrb   w2, [x0]
    stlxrb  w3, w1, [x0]
    cbnz    w3, 1b
    dmb     ish
    mov     w0, w2
    ret

# =============================================================================
# Spinlock Primitives
# =============================================================================

# ─────────────────────────────────────────────
# void gray_spin_lock(int32_t *lock)
# Acquire spinlock. Spins until lock is obtained.
# ─────────────────────────────────────────────
.globl _gray_spin_lock
.globl gray_spin_lock
_gray_spin_lock:
gray_spin_lock:
1:
    ldxr    w1, [x0]
    cbnz    w1, 2f              // if locked, spin
    mov     w2, #1
    stxr    w3, w2, [x0]
    cbnz    w3, 1b              // retry if store failed
    dmb     ish
    ret
2:
    yield                       // hint: we're spinning
    ldr     w1, [x0]           // test without exclusive (TTAS)
    cbnz    w1, 2b
    b       1b

# ─────────────────────────────────────────────
# bool gray_spin_trylock(int32_t *lock)
# Try to acquire spinlock. Returns 1 if acquired, 0 if not.
# ─────────────────────────────────────────────
.globl _gray_spin_trylock
.globl gray_spin_trylock
_gray_spin_trylock:
gray_spin_trylock:
    ldxr    w1, [x0]
    cbnz    w1, 1f              // already locked
    mov     w2, #1
    stxr    w3, w2, [x0]
    cbnz    w3, 1f              // store failed
    dmb     ish
    mov     w0, #1              // acquired
    ret
1:
    clrex
    mov     w0, #0              // not acquired
    ret

# ─────────────────────────────────────────────
# void gray_spin_unlock(int32_t *lock)
# Release spinlock.
# ─────────────────────────────────────────────
.globl _gray_spin_unlock
.globl gray_spin_unlock
_gray_spin_unlock:
gray_spin_unlock:
    dmb     ish
    str     wzr, [x0]
    ret
