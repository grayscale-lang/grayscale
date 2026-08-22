/*
 * runtime_mod.h — Public interface for the runtime stdlib module.
 * Declares read-only introspection into compiler-managed arenas,
 * execution state, and build info.
 *
 * Named runtime_mod (not runtime) to avoid colliding with
 * runtime/runtime.h and runtime/runtime.c, which share the same
 * flattened include/archive namespace as this directory.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_RUNTIME_MOD_H
#define GRAY_RUNTIME_MOD_H

#include "../runtime/runtime.h"

/*@man arena_usage
 *@module runtime
 *@group Memory
 *@sig arena_usage() -> int
 *@desc Return the number of bytes currently used in the default (scope) arena.
 *@example
 *   import @runtime
 *   println("${runtime.arena_usage()} bytes")
 *@end
 */
int64_t gray_runtime_arena_usage(void);

/*@man heap_usage
 *@module runtime
 *@group Memory
 *@sig heap_usage() -> int
 *@desc Return the number of bytes currently used in the heap arena (backs new()).
 *@example
 *   import @runtime
 *   println("${runtime.heap_usage()} bytes")
 *@end
 */
int64_t gray_runtime_heap_usage(void);

/*@man total_usage
 *@module runtime
 *@group Memory
 *@sig total_usage() -> int
 *@desc Return the combined bytes used across the default and heap arenas.
 *@example
 *   import @runtime
 *   println("${runtime.total_usage()} bytes")
 *@end
 */
int64_t gray_runtime_total_usage(void);

/*@man peak_usage
 *@module runtime
 *@group Memory
 *@sig peak_usage() -> int
 *@desc Return the high-water mark of combined default and heap arena bytes committed at any point during execution.
 *@example
 *   import @runtime
 *   println("peak ${runtime.peak_usage()} bytes")
 *@end
 */
int64_t gray_runtime_peak_usage(void);

/*@man alloc_count
 *@module runtime
 *@group Memory
 *@sig alloc_count() -> int
 *@desc Return the total number of arena allocations across the default and heap arenas since program start.
 *@example
 *   import @runtime
 *   println("${runtime.alloc_count()} allocations")
 *@end
 */
int64_t gray_runtime_alloc_count(void);

/*@man arena_blocks
 *@module runtime
 *@group Memory
 *@sig arena_blocks() -> int
 *@desc Return the number of blocks chained in the default arena.
 *@example
 *   import @runtime
 *   println("${runtime.arena_blocks()} blocks")
 *@end
 */
int64_t gray_runtime_arena_blocks(void);

/*@man heap_blocks
 *@module runtime
 *@group Memory
 *@sig heap_blocks() -> int
 *@desc Return the number of blocks chained in the heap arena.
 *@example
 *   import @runtime
 *   println("${runtime.heap_blocks()} blocks")
 *@end
 */
int64_t gray_runtime_heap_blocks(void);

/*@man arena_limit
 *@module runtime
 *@group Memory
 *@sig arena_limit() -> int
 *@desc Return the current arena growth limit in bytes (from --arena-limit, or the 1 GB default).
 *@example
 *   import @runtime
 *   println("limit ${runtime.arena_limit()} bytes")
 *@end
 */
int64_t gray_runtime_arena_limit(void);

/*@man version
 *@module runtime
 *@group Build
 *@sig version() -> string
 *@desc Return the Grayscale version that compiled this binary.
 *@example
 *   import @runtime
 *   println("Grayscale ${runtime.version()}")
 *@end
 */
/* runtime.version() is handled by codegen directly — it emits
 * gray_string_lit(GRAY_VERSION) inline. No C function needed. */

/*@man call_depth
 *@module runtime
 *@group Execution
 *@sig call_depth() -> int
 *@desc Return the current call stack depth.
 *@example
 *   import @runtime
 *   println("depth ${runtime.call_depth()}")
 *@end
 */
int64_t gray_runtime_call_depth(void);

/*@man call_limit
 *@module runtime
 *@group Execution
 *@sig call_limit() -> int
 *@desc Return the maximum allowed call stack depth.
 *@example
 *   import @runtime
 *   println("limit ${runtime.call_limit()}")
 *@end
 */
int64_t gray_runtime_call_limit(void);

/*@man uptime
 *@module runtime
 *@group Execution
 *@sig uptime() -> float
 *@desc Return the number of seconds elapsed since the program started.
 *@example
 *   import @runtime
 *   println("up ${runtime.uptime()}s")
 *@end
 */
/* runtime.uptime() maps directly to gray_runtime_uptime(), declared
 * in runtime/runtime.h and always available in generated code. */

#endif
