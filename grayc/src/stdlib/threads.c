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
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <errno.h>

typedef struct {
    pthread_t posix_thread;
    _Atomic int alive;       /* 1 between entry and exit, 0 otherwise */
    _Atomic int detached;    /* 1 once detach() has been called */
} GrayThreadInternal;

static _Atomic int64_t gray_threads_live_count = 0;

/* Unified thread wrapper: entry_no_arg is set for no-arg spawns, entry_with_arg for one-arg. */
typedef struct {
    void (*entry_no_arg)(void);
    void (*entry_with_arg)(int64_t);
    int64_t arg;
    GrayThreadInternal *state;
} ThreadArg;

static void *thread_entry(void *raw) {
    ThreadArg *thread_arg = (ThreadArg *)raw;
    GrayThreadInternal *state = thread_arg->state;
    gray_default_arena = gray_arena_create(GRAY_DEFAULT_ARENA_SIZE);
    if (thread_arg->entry_with_arg)
        thread_arg->entry_with_arg(thread_arg->arg);
    else
        thread_arg->entry_no_arg();
    gray_arena_destroy(gray_default_arena, __FILE__, __LINE__);
    free(gray_default_arena);
    gray_default_arena = NULL;
    atomic_fetch_sub(&gray_threads_live_count, 1);
    atomic_store(&state->alive, 0);
    /* Rendezvous with detach(): both sides increment detached, and
     * whichever sees the old value as 1 is the last to arrive and
     * owns the free.  If join() is used instead, the increment is
     * harmless — join() frees after pthread_join returns. */
    if (atomic_fetch_add(&state->detached, 1) == 1) free(state);
    free(thread_arg);
    return NULL;
}

static GrayThread spawn_thread(ThreadArg *thread_arg) {
    GrayThreadInternal *state = thread_arg->state;
    /* alive=1 set before pthread_create so is_alive() is true immediately
     * after spawn() returns; otherwise callers race the scheduler. The
     * thread wrapper clears it on exit. */
    atomic_store(&state->alive, 1);
    atomic_store(&state->detached, 0);
    atomic_fetch_add(&gray_threads_live_count, 1);
    int rc = pthread_create(&state->posix_thread, NULL, thread_entry, thread_arg);
    if (rc != 0) {
        /* thread_entry never runs, so unwind everything it would have owned:
         * the live count, the alive flag, and both allocations. */
        atomic_fetch_sub(&gray_threads_live_count, 1);
        atomic_store(&state->alive, 0);
        free(state);
        free(thread_arg);
        gray_panic_code("P0108",
            "threads.spawn: failed to create OS thread (%s); the process thread limit was likely reached",
            strerror(rc));
    }
    GrayThread t;
    t._internal = state;
    return t;
}

/* malloc that panics rather than returning NULL; thread state is small and
 * a failure here means the process is already out of memory. */
static void *thread_alloc(size_t n) {
    void *p = malloc(n);
    if (!p) gray_panic_code("P0109", "threads.spawn: out of memory allocating thread state");
    return p;
}

GrayThread gray_threads_spawn(void (*fn)(void)) {
    ThreadArg *thread_arg = thread_alloc(sizeof(ThreadArg));
    thread_arg->entry_no_arg = fn;
    thread_arg->entry_with_arg = NULL;
    thread_arg->arg = 0;
    thread_arg->state = thread_alloc(sizeof(GrayThreadInternal));
    return spawn_thread(thread_arg);
}

GrayThread gray_threads_spawn_arg(void (*fn)(int64_t), int64_t arg) {
    ThreadArg *thread_arg = thread_alloc(sizeof(ThreadArg));
    thread_arg->entry_no_arg = NULL;
    thread_arg->entry_with_arg = fn;
    thread_arg->arg = arg;
    thread_arg->state = thread_alloc(sizeof(GrayThreadInternal));
    return spawn_thread(thread_arg);
}

void gray_threads_join(GrayThread t) {
    if (!t._internal) return;
    GrayThreadInternal *state = (GrayThreadInternal *)t._internal;
    pthread_join(state->posix_thread, NULL);
    free(state);
}

void gray_threads_detach(GrayThread t) {
    if (!t._internal) return;
    GrayThreadInternal *state = (GrayThreadInternal *)t._internal;
    pthread_detach(state->posix_thread);
    /* Rendezvous with thread exit: both sides increment detached, and
     * whichever sees the old value as 1 is last to arrive and frees. */
    if (atomic_fetch_add(&state->detached, 1) == 1) free(state);
}

bool gray_threads_is_alive(GrayThread t) {
    if (!t._internal) return false;
    GrayThreadInternal *state = (GrayThreadInternal *)t._internal;
    return atomic_load(&state->alive) != 0;
}

int64_t gray_threads_id(void) {
    return (int64_t)(uintptr_t)pthread_self();
}

int64_t gray_threads_current(void) {
    return gray_threads_id();
}

void gray_threads_yield(void) {
    sched_yield();
}

void gray_threads_sleep(int64_t ms) {
    if (ms < 0) ms = 0;
    struct timespec req;
    req.tv_sec  = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms % 1000) * 1000000L);
    /* Restart on signal interruption — the user asked for a sleep, not a
     * sleep-or-signal. */
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
}

int64_t gray_threads_thread_count(void) {
    return atomic_load(&gray_threads_live_count);
}
