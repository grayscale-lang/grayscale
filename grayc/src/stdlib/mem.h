/*
 * mem.h — Public interface for the mem stdlib module.
 * Declares arena creation, destruction, reset, usage queries, and
 * raw memory copy/zero operations for manual memory management.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_MEM_H
#define GRAY_MEM_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

/*@man arena
 *@module mem
 *@group Arena
 *@sig arena(size int) -> Arena
 *@desc Create a new arena with the given initial block size in bytes.
 *@example
 *   import @mem
 *   mut a Arena = mem.arena(4096)
 *@end
 */
/* mem.arena(size) — create a new arena with initial block size */
GrayArena *gray_mem_arena(int64_t size);

/*@man destroy
 *@module mem
 *@group Arena
 *@sig destroy(a Arena)
 *@desc Destroy an arena and free all its memory.
 *@example
 *   import @mem
 *   mem.destroy(a)
 *@end
 */
/* mem.destroy(arena) — destroy an arena and free all its memory */
void gray_mem_destroy(GrayArena *arena, const char *file, int line);

/*@man reset
 *@module mem
 *@group Arena
 *@sig reset(a Arena)
 *@desc Reset an arena for reuse. Keeps allocated blocks but marks all memory as free.
 *@example
 *   import @mem
 *   mem.reset(a)
 *@end
 */
/* mem.reset(arena) — reset arena for reuse (keeps allocated blocks) */
void gray_mem_reset(GrayArena *arena);

/*@man usage
 *@module mem
 *@group Arena
 *@sig usage(a Arena) -> int
 *@desc Return the number of bytes currently used in the arena.
 *@example
 *   import @mem
 *   mut used int = mem.usage(a)
 *   println("${used} bytes used")
 *@end
 */
/* mem.usage(arena) — return bytes currently used in the arena */
int64_t gray_mem_usage(GrayArena *arena);

/*@man alloc
 *@module mem
 *@group Allocation
 *@sig alloc(a Arena, value T) -> ^T
 *@desc Allocate a value on the given arena and return a pointer to it. Works with primitives, strings, and arrays.
 *@example
 *   import @mem
 *   mut a Arena = mem.arena(4096)
 *   mut p ^int = mem.alloc(a, 42)
 *@end
 */
/*
 * mem.alloc(arena, value) is handled by the codegen directly —
 * it emits the allocation on the specified arena instead of
 * gray_default_arena. No C function needed.
 */

/*@man init
 *@module mem
 *@group Allocation
 *@sig init(a Arena, Type) -> ^Type
 *@desc Allocate a zero-initialized value of the given type on the arena and return a pointer to it.
 *@example
 *   import @mem
 *   mut a Arena = mem.arena(4096)
 *   mut p ^int = mem.init(a, int)
 *@end
 */
/* mem.init(arena, Type) — handled by codegen */

/*@man raw_copy
 *@module mem
 *@group Raw
 *@sig raw_copy(dest ^T, src ^T, n int)
 *@desc Copy n bytes from src to dest.
 *@example
 *   import @mem
 *   mem.raw_copy(dest, src, 64)
 *@end
 */
/* mem.copy(dest, src, n) — copy n bytes from src to dest (memcpy) */
void gray_mem_copy(void *dest, const void *src, int64_t n);

/*@man zero
 *@module mem
 *@group Raw
 *@sig zero(ptr ^T, n int)
 *@desc Zero n bytes starting at ptr.
 *@example
 *   import @mem
 *   mem.zero(ptr, 128)
 *@end
 */
/* mem.zero(ptr, n) — zero n bytes starting at ptr (memset 0) */
void gray_mem_zero(void *ptr, int64_t n);

/*@man fill
 *@module mem
 *@group Raw
 *@sig fill(ptr ^T, val int, n int)
 *@desc Set n bytes starting at ptr to val.
 *@example
 *   import @mem
 *   mem.fill(ptr, 0xFF, 64)
 *@end
 */
/* mem.set(ptr, val, n) — set n bytes to val (memset) */
void gray_mem_set(void *ptr, int64_t val, int64_t n);

/* mem.size_of(Type) — handled by codegen (sizeof) */

#endif
