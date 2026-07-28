/*
 * sync.h — Public interface for the sync stdlib module.
 * Declares mutex type and lock/unlock/try_lock operations built
 * on POSIX pthreads.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_SYNC_H
#define GRAY_SYNC_H

#include "../runtime/runtime.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *_internal; /* pthread_mutex_t* */
} GrayMutex;

/*@man mutex
 *@module sync
 *@group Mutex
 *@sig mutex() -> Mutex
 *@desc Create a new mutex.
 *@example
 *   import @sync
 *   mut m Mutex = sync.mutex()
 *@end
 */
/*@man lock
 *@module sync
 *@group Mutex
 *@sig lock(m Mutex)
 *@desc Acquire a mutex. Blocks until the lock is available.
 *@example
 *   import @sync
 *   sync.lock(m)
 *   // critical section
 *   sync.unlock(m)
 *@end
 */
/*@man unlock
 *@module sync
 *@group Mutex
 *@sig unlock(m Mutex)
 *@desc Release a mutex.
 *@example
 *   import @sync
 *   sync.lock(m)
 *   // critical section
 *   sync.unlock(m)
 *@end
 */
/*@man try_lock
 *@module sync
 *@group Mutex
 *@sig try_lock(m Mutex) -> bool
 *@desc Try to acquire a mutex without blocking. Returns true if acquired.
 *@example
 *   import @sync
 *   if sync.try_lock(m) {
 *       // critical section
 *       sync.unlock(m)
 *   }
 *@end
 */
/*@man destroy
 *@module sync
 *@group Mutex
 *@sig destroy(m Mutex)
 *@desc Destroy a mutex and free its resources.
 *@example
 *   import @sync
 *   sync.destroy(m)
 *@end
 */
GrayMutex gray_sync_mutex(void);
void gray_sync_lock(GrayMutex m);
void gray_sync_unlock(GrayMutex m);
bool gray_sync_try_lock(GrayMutex m);
void gray_sync_destroy(GrayMutex m);

#endif
