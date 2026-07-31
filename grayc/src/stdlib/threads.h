/*
 * threads.h — Public interface for the threads stdlib module.
 * Declares thread spawning, joining, detaching, ID queries, and
 * yield functions built on POSIX pthreads.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_THREADS_H
#define GRAY_THREADS_H

#include "../runtime/runtime.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *_internal; /* points to an internal {pthread_t, alive flag} */
} GrayThread;

/*@man spawn
 *@module threads
 *@group Lifecycle
 *@sig spawn(fn func) -> Thread
 *@desc Spawn a new thread running fn. If a second int argument is provided, it is passed to the function.
 *@example
 *   import @threads
 *   mut t Thread = threads.spawn(my_func)
 *   threads.join(t)
 *@end
 */
/* Spawn a new thread running the given function.
 * The function pointer must match: void (*fn)(void) or void (*fn)(int64_t)
 * Returns a thread handle for joining. */
GrayThread gray_threads_spawn(void (*fn)(void));
GrayThread gray_threads_spawn_arg(void (*fn)(int64_t), int64_t arg);

/*@man join
 *@module threads
 *@group Lifecycle
 *@sig join(t Thread)
 *@desc Wait for a thread to finish. Frees the underlying handle.
 *@example
 *   import @threads
 *   mut t Thread = threads.spawn(work)
 *   threads.join(t)
 *@end
 */
/* Wait for a thread to finish. Frees the underlying handle. */
void gray_threads_join(GrayThread t);

/*@man detach
 *@module threads
 *@group Lifecycle
 *@sig detach(t Thread)
 *@desc Release ownership; the thread runs independently. After detach the handle must not be joined or queried.
 *@example
 *   import @threads
 *   mut t Thread = threads.spawn(background_work)
 *   threads.detach(t)
 *@end
 */
/* Release ownership; the thread runs to completion independently and its
 * underlying handle is freed by pthread itself. After detach() the
 * GrayThread must not be joined or queried for is_alive(). */
void gray_threads_detach(GrayThread t);

/*@man is_alive
 *@module threads
 *@group Query
 *@sig is_alive(t Thread) -> bool
 *@desc True while the thread's body has not returned. Not valid after detach or join.
 *@example
 *   import @threads
 *   mut t Thread = threads.spawn(work)
 *   if threads.is_alive(t) { println("still running") }
 *@end
 */
/* True while the thread's entry function has not returned. Safe to call
 * before join() (and meaningless after). Not valid after detach(). */
bool gray_threads_is_alive(GrayThread t);

/*@man get_id
 *@module threads
 *@group Query
 *@sig get_id() -> int
 *@desc Get the current thread's ID.
 *@example
 *   import @threads
 *   mut id int = threads.get_id()
 *   println("thread ${id}")
 *@end
 */
/*@man current
 *@module threads
 *@group Query
 *@sig current() -> int
 *@desc Get the current thread's ID. Alias for get_id.
 *@example
 *   import @threads
 *   mut id int = threads.current()
 *@end
 */
/* Get current thread id (for debugging). Identical to `current()`; both
 * names exist so callers can pick whichever reads better at the call
 * site. */
int64_t gray_threads_id(void);
int64_t gray_threads_current(void);

/*@man yield
 *@module threads
 *@group Control
 *@sig yield()
 *@desc Hint the scheduler to run another runnable thread.
 *@example
 *   import @threads
 *   threads.yield()
 *@end
 */
/* Hint the scheduler to run another runnable thread. */
void gray_threads_yield(void);

/*@man sleep
 *@module threads
 *@group Control
 *@sig sleep(ms int)
 *@desc Sleep the current thread for ms milliseconds.
 *@example
 *   import @threads
 *   threads.sleep(1000)
 *@end
 */
/* Sleep the current thread for `ms` milliseconds. */
void gray_threads_sleep(int64_t ms);

/*@man thread_count
 *@module threads
 *@group Query
 *@sig thread_count() -> int
 *@desc Number of live threads spawned through this module. Excludes the main thread and non-Grayscale threads.
 *@example
 *   import @threads
 *   mut n int = threads.thread_count()
 *   println("${n} threads running")
 *@end
 */
/* Number of live threads currently spawned through this module. Excludes
 * the main thread and any non-Grayscale threads in the process. */
int64_t gray_threads_thread_count(void);

#endif
