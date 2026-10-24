/*
 * threads.c — Implementation of the threads stdlib module.
 * Provides thread spawning, joining, detaching, ID queries, and
 * yield, all built on POSIX pthreads.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "threads.h"
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include "../runtime/atomic.h"
#include <string.h>
#include <time.h>
#include <errno.h>

typedef struct {
    pthread_t posix_thread;
    int alive;       /* 1 between entry and exit, 0 otherwise */
    int released;    /* 1 once join() or detach() has taken the handle */
} GrayThreadInternal;

static int64_t gray_threads_live_count = 0;

/* Unified thread wrapper: entry_without_argument is set for no-arg spawns, entry_with_argument for one-arg. */
typedef struct {
    void (*entry_without_argument)(void);
    void (*entry_with_argument)(int64_t);
    int64_t argument;
    GrayThreadInternal *state;
} ThreadArg;

static void *thread_entry(void *raw_argument) {
    ThreadArg *thread_argument = (ThreadArg *)raw_argument;
    GrayThreadInternal *state = thread_argument->state;
    gray_default_arena = gray_capped_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    /* new() allocates from the thread's heap arena, which a spawned thread does
     * not inherit from main; it lives until the thread exits. */
    gray_heap_arena = gray_capped_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    if (thread_argument->entry_with_argument)
        thread_argument->entry_with_argument(thread_argument->argument);
    else
        thread_argument->entry_without_argument();
    gray_arena_destroy(gray_default_arena, __FILE__, __LINE__);
    free(gray_default_arena);
    gray_default_arena = NULL;
    /* A pointer from new(), or a global assigned inside the thread, can be
     * read after the thread exits, so a heap arena that holds anything lives
     * until the program exits. */
    if (gray_arena_usage(gray_heap_arena) == 0) {
        gray_arena_destroy(gray_heap_arena, __FILE__, __LINE__);
        free(gray_heap_arena);
    }
    gray_heap_arena = NULL;
    __atomic_fetch_sub(&gray_threads_live_count, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&state->alive, 0, __ATOMIC_SEQ_CST);
    free(thread_argument);
    return NULL;
}

static GrayThread spawn_thread(ThreadArg *thread_argument) {
    GrayThreadInternal *state = thread_argument->state;
    /* alive=1 set before pthread_create so is_alive() is true immediately
     * after spawn() returns; otherwise callers race the scheduler. The
     * thread wrapper clears it on exit. */
    __atomic_store_n(&state->alive, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&state->released, 0, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&gray_threads_live_count, 1, __ATOMIC_SEQ_CST);
    int result_code = pthread_create(&state->posix_thread, NULL, thread_entry, thread_argument);
    if (result_code != 0) {
        /* thread_entry never runs, so unwind everything it would have owned:
         * the live count, the alive flag, and both allocations. */
        __atomic_fetch_sub(&gray_threads_live_count, 1, __ATOMIC_SEQ_CST);
        __atomic_store_n(&state->alive, 0, __ATOMIC_SEQ_CST);
        free(state);
        free(thread_argument);
        gray_panic_code("P0108",
            "threads.spawn: failed to create OS thread (%s); the process thread limit was likely reached",
            strerror(result_code));
    }
    GrayThread thread;
    thread._internal = state;
    return thread;
}

/* malloc that panics rather than returning NULL; thread state is small and
 * a failure here means the process is already out of memory. */
static void *thread_allocate(size_t size) {
    void *pointer = malloc(size);
    if (!pointer) gray_panic_code("P0109", "threads.spawn: out of memory allocating thread state");
    return pointer;
}

GrayThread gray_threads_spawn(void (*entry)(void)) {
    ThreadArg *thread_argument = thread_allocate(sizeof(ThreadArg));
    thread_argument->entry_without_argument = entry;
    thread_argument->entry_with_argument = NULL;
    thread_argument->argument = 0;
    thread_argument->state = thread_allocate(sizeof(GrayThreadInternal));
    return spawn_thread(thread_argument);
}

GrayThread gray_threads_spawn_arg(void (*entry)(int64_t), int64_t argument) {
    ThreadArg *thread_argument = thread_allocate(sizeof(ThreadArg));
    thread_argument->entry_without_argument = NULL;
    thread_argument->entry_with_argument = entry;
    thread_argument->argument = argument;
    thread_argument->state = thread_allocate(sizeof(GrayThreadInternal));
    return spawn_thread(thread_argument);
}

/* A joined or detached thread keeps its state, marked released: a copy of
 * the handle made before the join still points here, and freeing the state
 * would leave that copy reading a freed object. */
static void thread_unusable(const char *function_name) {
    gray_panic_code("P0170", "threads.%s on a thread handle that was never returned by threads.spawn() or was already joined or detached", function_name);
}

static GrayThreadInternal *live_thread(GrayThread thread, const char *function_name) {
    GrayThreadInternal *state = (GrayThreadInternal *)thread._internal;
    if (!state || __atomic_load_n(&state->released, __ATOMIC_SEQ_CST)) thread_unusable(function_name);
    return state;
}

static GrayThreadInternal *release_thread(GrayThread *thread, const char *function_name) {
    GrayThreadInternal *state = (GrayThreadInternal *)thread->_internal;
    if (!state || __atomic_exchange_n(&state->released, 1, __ATOMIC_SEQ_CST)) thread_unusable(function_name);
    thread->_internal = NULL;
    return state;
}

void gray_threads_join(GrayThread *thread) {
    GrayThreadInternal *state = release_thread(thread, "join");
    pthread_join(state->posix_thread, NULL);
}

void gray_threads_detach(GrayThread *thread) {
    GrayThreadInternal *state = release_thread(thread, "detach");
    pthread_detach(state->posix_thread);
}

bool gray_threads_is_alive(GrayThread thread) {
    GrayThreadInternal *state = live_thread(thread, "is_alive");
    return __atomic_load_n(&state->alive, __ATOMIC_SEQ_CST) != 0;
}

int64_t gray_threads_id(void) {
    return (int64_t)(uintptr_t)pthread_self();
}

void gray_threads_yield(void) {
    sched_yield();
}

int64_t gray_threads_thread_count(void) {
    return __atomic_load_n(&gray_threads_live_count, __ATOMIC_SEQ_CST);
}
