/*
 * channels.c — Implementation of the channels stdlib module.
 * Bounded, blocking channels for inter-thread message passing, built
 * on POSIX pthreads mutexes and condition variables.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "channels.h"
#include <pthread.h>
#include <stdlib.h>

typedef struct {
    int64_t *buffer;
    int64_t capacity;
    int64_t count;
    int64_t head;
    int64_t tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    bool closed;
} GrayChannelInternal;

static GrayChannelInternal *live_channel(GrayChannel handle, const char *function_name) {
    if (!handle._internal) gray_panic_code("P0169", "channels.%s on a channel that was never created with channels.open()", function_name);
    return (GrayChannelInternal *)handle._internal;
}

GrayChannel gray_channels_open(int64_t capacity) {
    if (capacity < 1) gray_panic_code("P0173", "channels.open capacity %lld is below 1", (long long)capacity);
    if ((uint64_t)capacity > SIZE_MAX / sizeof(int64_t))
        gray_panic_code("P0159", "channels.open capacity is too large to allocate a buffer");
    size_t arena_limit = gray_default_arena->maximum_bytes;
    if (arena_limit > 0 && (size_t)capacity * sizeof(int64_t) > arena_limit)
        gray_panic_code("P0104",
            "arena memory limit exceeded: attempted to grow beyond the maximum of %zu bytes",
            arena_limit);
    GrayChannelInternal *channel = malloc(sizeof(GrayChannelInternal));
    if (!channel) gray_panic_code("P0160", "channels.open: out of memory allocating the channel");
    channel->buffer = malloc(sizeof(int64_t) * (size_t)capacity);
    if (!channel->buffer) {
        free(channel);
        gray_panic_code("P0160", "channels.open: out of memory allocating the channel");
    }
    channel->capacity = capacity;
    channel->count = 0;
    channel->head = 0;
    channel->tail = 0;
    channel->closed = false;
    pthread_mutex_init(&channel->mutex, NULL);
    pthread_cond_init(&channel->not_full, NULL);
    pthread_cond_init(&channel->not_empty, NULL);
    GrayChannel result;
    result._internal = channel;
    return result;
}

void gray_channels_send(GrayChannel handle, int64_t value) {
    GrayChannelInternal *channel = live_channel(handle, "send");

    pthread_mutex_lock(&channel->mutex);
    while (channel->count == channel->capacity && !channel->closed) {
        pthread_cond_wait(&channel->not_full, &channel->mutex);
    }
    if (channel->closed) {
        pthread_mutex_unlock(&channel->mutex);
        gray_panic_code("P0172", "channels.send on a closed channel");
    }
    channel->buffer[channel->tail] = value;
    channel->tail = (channel->tail + 1) % channel->capacity;
    channel->count++;
    pthread_cond_signal(&channel->not_empty);
    pthread_mutex_unlock(&channel->mutex);
}

int64_t gray_channels_receive(GrayChannel handle) {
    GrayChannelInternal *channel = live_channel(handle, "receive");

    pthread_mutex_lock(&channel->mutex);
    while (channel->count == 0 && !channel->closed) {
        pthread_cond_wait(&channel->not_empty, &channel->mutex);
    }
    if (channel->count == 0 && channel->closed) {
        pthread_mutex_unlock(&channel->mutex);
        return 0;
    }
    int64_t value = channel->buffer[channel->head];
    channel->head = (channel->head + 1) % channel->capacity;
    channel->count--;
    pthread_cond_signal(&channel->not_full);
    pthread_mutex_unlock(&channel->mutex);
    return value;
}

bool gray_channels_try_send(GrayChannel handle, int64_t value) {
    GrayChannelInternal *channel = live_channel(handle, "try_send");

    pthread_mutex_lock(&channel->mutex);
    if (channel->count == channel->capacity || channel->closed) {
        pthread_mutex_unlock(&channel->mutex);
        return false;
    }
    channel->buffer[channel->tail] = value;
    channel->tail = (channel->tail + 1) % channel->capacity;
    channel->count++;
    pthread_cond_signal(&channel->not_empty);
    pthread_mutex_unlock(&channel->mutex);
    return true;
}

GrayChannelTryRecv gray_channels_try_receive(GrayChannel handle) {
    GrayChannelTryRecv result = {0, false};
    GrayChannelInternal *channel = live_channel(handle, "try_receive");

    pthread_mutex_lock(&channel->mutex);
    if (channel->count == 0) {
        pthread_mutex_unlock(&channel->mutex);
        return result;
    }
    result.v0 = channel->buffer[channel->head];
    channel->head = (channel->head + 1) % channel->capacity;
    channel->count--;
    result.v1 = true;
    pthread_cond_signal(&channel->not_full);
    pthread_mutex_unlock(&channel->mutex);
    return result;
}

void gray_channels_close(GrayChannel handle) {
    GrayChannelInternal *channel = live_channel(handle, "close");

    pthread_mutex_lock(&channel->mutex);
    channel->closed = true;
    pthread_cond_broadcast(&channel->not_full);
    pthread_cond_broadcast(&channel->not_empty);
    pthread_mutex_unlock(&channel->mutex);
}

int64_t gray_channels_len(GrayChannel handle) {
    GrayChannelInternal *channel = live_channel(handle, "len");

    pthread_mutex_lock(&channel->mutex);
    int64_t buffered = channel->count;
    pthread_mutex_unlock(&channel->mutex);
    return buffered;
}
