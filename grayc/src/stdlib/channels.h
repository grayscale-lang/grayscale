/*
 * channels.h — Public interface for the channels stdlib module.
 * Bounded, blocking channels for message passing between threads,
 * built on POSIX pthreads.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_CHANNELS_H
#define GRAY_CHANNELS_H

#include "../runtime/runtime.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *_internal; /* GrayChannelInternal* */
} GrayChannel;

/*@man open
 *@module channels
 *@group Lifecycle
 *@sig open(capacity int) -> Channel
 *@desc Create a buffered channel with the given capacity.
 *@example
 *   import @channels
 *   mut ch Channel = channels.open(10)
 *@end
 */
/* Create a buffered channel with given capacity. */
GrayChannel gray_channels_open(int64_t capacity);

/*@man send
 *@module channels
 *@group Send/Receive
 *@sig send(ch Channel, value int)
 *@desc Send a value into a channel. Blocks if the channel is full.
 *@example
 *   import @channels
 *   channels.send(ch, 42)
 *@end
 */
/* Send a value into the channel. Blocks if full. */
void gray_channels_send(GrayChannel ch, int64_t value);

/*@man receive
 *@module channels
 *@group Send/Receive
 *@sig receive(ch Channel) -> int
 *@desc Receive a value from a channel. Blocks if the channel is empty.
 *@example
 *   import @channels
 *   mut val int = channels.receive(ch)
 *@end
 */
/* Receive a value from the channel. Blocks if empty. */
int64_t gray_channels_receive(GrayChannel ch);

/*@man close
 *@module channels
 *@group Lifecycle
 *@sig close(ch Channel)
 *@desc Close a channel.
 *@example
 *   import @channels
 *   channels.close(ch)
 *@end
 */
/* Close a channel. */
void gray_channels_close(GrayChannel ch);

/*@man try_send
 *@module channels
 *@group Send/Receive
 *@sig try_send(ch Channel, value int) -> bool
 *@desc Non-blocking send. Returns true if the value was sent, false if the channel is full.
 *@example
 *   import @channels
 *   if channels.try_send(ch, 42) {
 *       println("sent")
 *   }
 *@end
 */
bool gray_channels_try_send(GrayChannel ch, int64_t value);

/*@man try_receive
 *@module channels
 *@group Send/Receive
 *@sig try_receive(ch Channel) -> (int, bool)
 *@desc Non-blocking receive. Returns the value and true if available, or (0, false) if empty. Always use destructuring.
 *@example
 *   import @channels
 *   mut val int, mut ok bool = channels.try_receive(ch)
 *   if ok { println("got ${val}") }
 *@end
 */
typedef struct { int64_t v0; bool v1; } GrayChannelTryRecv;
GrayChannelTryRecv gray_channels_try_receive(GrayChannel ch);

#endif
