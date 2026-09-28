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

GrayChannel gray_channels_open(int64_t capacity) {
    if (capacity < 1) capacity = 1;
    GrayChannelInternal *channel = malloc(sizeof(GrayChannelInternal));
    channel->buffer = malloc(sizeof(int64_t) * (size_t)capacity);
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
    GrayChannelInternal *channel = (GrayChannelInternal *)handle._internal;
    if (!channel || channel->closed) return;

    pthread_mutex_lock(&channel->mutex);
    while (channel->count == channel->capacity && !channel->closed) {
        pthread_cond_wait(&channel->not_full, &channel->mutex);
    }
    if (channel->closed) {
        pthread_mutex_unlock(&channel->mutex);
        return;
    }
    channel->buffer[channel->tail] = value;
    channel->tail = (channel->tail + 1) % channel->capacity;
    channel->count++;
    pthread_cond_signal(&channel->not_empty);
    pthread_mutex_unlock(&channel->mutex);
}

int64_t gray_channels_receive(GrayChannel handle) {
    GrayChannelInternal *channel = (GrayChannelInternal *)handle._internal;
    if (!channel) return 0;

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
    GrayChannelInternal *channel = (GrayChannelInternal *)handle._internal;
    if (!channel || channel->closed) return false;

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
    GrayChannelInternal *channel = (GrayChannelInternal *)handle._internal;
    if (!channel) return result;

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
    GrayChannelInternal *channel = (GrayChannelInternal *)handle._internal;
    if (!channel) return;

    pthread_mutex_lock(&channel->mutex);
    channel->closed = true;
    pthread_cond_broadcast(&channel->not_full);
    pthread_cond_broadcast(&channel->not_empty);
    pthread_mutex_unlock(&channel->mutex);
}
