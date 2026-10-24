/*
 * sync.c — Implementation of the sync stdlib module.
 * Provides mutex creation, lock, unlock, and try_lock operations
 * built on POSIX pthreads.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "sync.h"
#include <pthread.h>
#include <stdlib.h>
#include "../runtime/atomic.h"

/* A destroyed mutex keeps its allocation, marked destroyed: a copy of the
 * handle made before the destroy still points here, and freeing the memory
 * would leave that copy reading a freed object. */
typedef struct {
    pthread_mutex_t mutex;
    int destroyed;
} GrayMutexInternal;

GrayMutex gray_sync_mutex(void) {
    GrayMutexInternal *handle = malloc(sizeof(GrayMutexInternal));
    pthread_mutex_init(&handle->mutex, NULL);
    handle->destroyed = 0;
    GrayMutex result;
    result._internal = handle;
    return result;
}

/* A zero-value or destroyed mutex gives no mutual exclusion, so using one is
 * a bug the program must hear about rather than a silent no-op. */
static void mutex_unusable(const char *function_name) {
    gray_panic_code("P0168", "sync.%s on a mutex that was never created with sync.mutex() or was already destroyed", function_name);
}

static GrayMutexInternal *live_mutex(GrayMutex mutex, const char *function_name) {
    GrayMutexInternal *handle = (GrayMutexInternal *)mutex._internal;
    if (!handle || __atomic_load_n(&handle->destroyed, __ATOMIC_SEQ_CST)) mutex_unusable(function_name);
    return handle;
}

void gray_sync_lock(GrayMutex mutex) {
    pthread_mutex_lock(&live_mutex(mutex, "lock")->mutex);
}

void gray_sync_unlock(GrayMutex mutex) {
    pthread_mutex_unlock(&live_mutex(mutex, "unlock")->mutex);
}

bool gray_sync_try_lock(GrayMutex mutex) {
    return pthread_mutex_trylock(&live_mutex(mutex, "try_lock")->mutex) == 0;
}

void gray_sync_destroy(GrayMutex *mutex) {
    GrayMutexInternal *handle = (GrayMutexInternal *)mutex->_internal;
    if (!handle || __atomic_exchange_n(&handle->destroyed, 1, __ATOMIC_SEQ_CST)) mutex_unusable("destroy");
    pthread_mutex_destroy(&handle->mutex);
    mutex->_internal = NULL;
}
