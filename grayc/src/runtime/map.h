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
#include "array.h"

#define GRAY_MAP_MIN_CAP      8
#define GRAY_MAP_LOAD_NUM     3
#define GRAY_MAP_LOAD_DEN     4

typedef struct {
    void *keys;
    void *values;
    uint8_t *states;        /* 0=empty, 1=occupied, 2=tombstone */
    /* order[] is append-only: gray_map_remove writes -1 over an entry
     * instead of shifting the tail down, so every reader must skip
     * negative slots. Holes are reclaimed by a rebuild, never by an
     * in-place compaction, so copies of this struct stay consistent. */
    int32_t *order;         /* insertion-order slot indices; -1 marks a hole */
    int32_t *order_pos;     /* slot -> its index in order (occupied slots only) */
    GrayArena *arena;       /* arena owning keys/values/states/order */
    int32_t count;
    int32_t capacity;
    int32_t key_size;
    int32_t value_size;
    int32_t order_len;      /* entries in order array, holes included */
    int32_t iterating;      /* >0 while a for_each is active */
    /* GrayElemKind of the keys and of the values. The key kind also picks
     * the hash and equality: a string key hashes its content, a float key
     * treats -0.0 as 0.0 and NaN as equal to NaN, anything else is bytes. */
    int8_t  key_kind;
    int8_t  value_kind;
} GrayMap;

/* Create an empty map of key_kind keys and value_kind values. */
GrayMap gray_map_new_kind(GrayArena *arena, int32_t key_size, int32_t value_size, int32_t initial_cap,
                          int8_t key_kind, int8_t value_kind);

/* Get a pointer to the value for a key, or NULL if not found */
void *gray_map_get(GrayMap *map, const void *key);

/* Set a key-value pair (inserts or updates) */
void gray_map_set(GrayArena *arena, GrayMap *map, const void *key, const void *value, const char *file, int line);

/* Check if a key exists */
bool gray_map_has(GrayMap *map, const void *key);

/* Remove a key */
bool gray_map_remove(GrayMap *map, const void *key, const char *file, int line);

/* Convenience macros for stdlib callers (uses C file/line) */
#define GRAY_MAP_SET(arena, map, key, value) gray_map_set((arena), (map), (key), (value), __FILE__, __LINE__)
#define GRAY_MAP_REMOVE(map, key) gray_map_remove((map), (key), __FILE__, __LINE__)

/* Clear all entries */
void gray_map_clear(GrayMap *map, const char *file, int line);

/* String-keyed convenience functions */
void *gray_map_get_str(GrayMap *map, GrayString key);
void gray_map_set_str(GrayArena *arena, GrayMap *map, GrayString key, const void *value, const char *file, int line);

/* Get key at internal index (for iteration) */
void *gray_map_key_at(GrayMap *map, int32_t internal_idx);
void *gray_map_value_at(GrayMap *map, int32_t internal_idx);

/* Deep copy: allocate a fresh map with independent backing storage
 * (keys, values, states, order) so mutations to the copy do not affect
 * the original. */
GrayMap gray_map_copy(GrayArena *arena, const GrayMap *src);

/* Initialize the per-process hash seed (called by gray_runtime_init). */
void gray_map_init_seed(void);

#endif
