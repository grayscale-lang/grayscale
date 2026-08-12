/*
 * map.h — Hash map type for the Grayscale runtime.
 * Open-addressing hash table with linear probing, storing keys and
 * values as fixed-size blobs with insertion-order tracking.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_MAP_H
#define GRAY_MAP_H

#include "runtime.h"
#include "atomic.h"

#define GRAY_MAP_MIN_CAP      8
#define GRAY_MAP_LOAD_NUM     3
#define GRAY_MAP_LOAD_DEN     4

/* Key-kind discriminator. Multiple Grayscale key types share a key_size
 * (e.g. int64/uint64/double/pointer all 8), so size alone cannot pick
 * the right hash/equality. Codegen tags each map with its kind. */
#define GRAY_MAP_KEY_BYTES    0   /* int, bool, pointer, struct: bytewise */
#define GRAY_MAP_KEY_STRING   1   /* GrayString: hash content, not struct bytes */
#define GRAY_MAP_KEY_F32      2   /* f32: normalize -0.0 and NaN */
#define GRAY_MAP_KEY_F64      3   /* f64/float: normalize -0.0 and NaN */

typedef struct {
    void *keys;
    void *values;
    uint8_t *states;        /* 0=empty, 1=occupied, 2=tombstone */
    int32_t *order;         /* insertion-order slot indices */
    int32_t count;
    int32_t capacity;
    int32_t key_size;
    int32_t value_size;
    int32_t order_len;      /* number of entries in order array */
    int32_t iterating;      /* >0 while a for_each is active */
    int8_t  key_kind;       /* GRAY_MAP_KEY_* */
} GrayMap;

/* Create an empty map. Auto-detects GrayString by key_size; defaults to
 * KEY_BYTES otherwise. Callers that need a specific kind (e.g. float
 * keys) should use gray_map_new_kind. */
GrayMap gray_map_new(GrayArena *arena, int32_t key_size, int32_t value_size, int32_t initial_cap);

/* Create an empty map with an explicit key kind. */
GrayMap gray_map_new_kind(GrayArena *arena, int32_t key_size, int32_t value_size, int32_t initial_cap, int8_t key_kind);

/* Get a pointer to the value for a key, or NULL if not found */
void *gray_map_get(GrayMap *m, const void *key);

/* Set a key-value pair (inserts or updates) */
void gray_map_set(GrayArena *arena, GrayMap *m, const void *key, const void *value, const char *file, int line);

/* Check if a key exists */
bool gray_map_has(GrayMap *m, const void *key);

/* Remove a key */
bool gray_map_remove(GrayMap *m, const void *key, const char *file, int line);

/* Convenience macros for stdlib callers (uses C file/line) */
#define GRAY_MAP_SET(arena, m, key, value) gray_map_set((arena), (m), (key), (value), __FILE__, __LINE__)
#define GRAY_MAP_REMOVE(m, key) gray_map_remove((m), (key), __FILE__, __LINE__)

/* Clear all entries */
void gray_map_clear(GrayMap *m, const char *file, int line);

/* String-keyed convenience functions */
void *gray_map_get_str(GrayMap *m, GrayString key);
void gray_map_set_str(GrayArena *arena, GrayMap *m, GrayString key, const void *value, const char *file, int line);

/* Get key at internal index (for iteration) */
void *gray_map_key_at(GrayMap *m, int32_t internal_idx);
void *gray_map_value_at(GrayMap *m, int32_t internal_idx);

/* Deep copy: allocate a fresh map with independent backing storage
 * (keys, values, states, order) so mutations to the copy do not affect
 * the original. */
GrayMap gray_map_copy(GrayArena *arena, const GrayMap *src);

#endif
