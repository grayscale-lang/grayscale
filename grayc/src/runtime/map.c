/*
 * map.c — Hash map implementation for the Grayscale runtime.
 * Open-addressing table with linear probing, FNV-1a hashing, and
 * special handling for floating-point key normalization and string keys.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "map.h"
#include <string.h>

/* arc4random_buf is hidden by _POSIX_C_SOURCE on Apple/BSD — declare explicitly */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
void arc4random_buf(void *buf, size_t nbytes);
#endif

/* Per-process random seed mixed into every hash to prevent collision DoS. */
static uint64_t gray_hash_seed = 0;

void gray_map_init_seed(void) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(&gray_hash_seed, sizeof(gray_hash_seed));
#else
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        fread(&gray_hash_seed, sizeof(gray_hash_seed), 1, urandom);
        fclose(urandom);
    }
#endif
}

/* FNV-1a hash with per-process seed */
static uint64_t hash_bytes(const void *data, int32_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = 14695981039346656037ULL ^ gray_hash_seed;
    for (int32_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* Float key normalization: -0.0 hashes/compares as +0.0; all NaN
 * payloads collide on a canonical quiet NaN. Matches Grayscale's `==` on
 * floating-point values (which says +0.0 == -0.0) and gives NaN keys a single bucket
 * instead of one per source-of-NaN. */

static uint64_t hash_f64(const void *key) {
    double value;
    memcpy(&value, key, sizeof(value));
    uint64_t bits;
    if (value == 0.0) {
        bits = 0;                          /* +0.0 / -0.0 collapse */
    } else if (value != value) {
        bits = 0x7FF8000000000000ULL;      /* canonical quiet NaN */
    } else {
        memcpy(&bits, &value, sizeof(bits));
    }
    return hash_bytes(&bits, sizeof(bits));
}

static uint64_t hash_f32(const void *key) {
    float value;
    memcpy(&value, key, sizeof(value));
    uint32_t bits;
    if (value == 0.0f) {
        bits = 0;
    } else if (value != value) {
        bits = 0x7FC00000U;
    } else {
        memcpy(&bits, &value, sizeof(bits));
    }
    return hash_bytes(&bits, sizeof(bits));
}

static bool floats_equal_f64(const void *left, const void *right) {
    double left_value, right_value;
    memcpy(&left_value, left, sizeof(left_value));
    memcpy(&right_value, right, sizeof(right_value));
    if (left_value == 0.0 && right_value == 0.0) return true;
    if (left_value != left_value && right_value != right_value) return true;
    return left_value == right_value;
}

static bool floats_equal_f32(const void *left, const void *right) {
    float left_value, right_value;
    memcpy(&left_value, left, sizeof(left_value));
    memcpy(&right_value, right, sizeof(right_value));
    if (left_value == 0.0f && right_value == 0.0f) return true;
    if (left_value != left_value && right_value != right_value) return true;
    return left_value == right_value;
}

/* Hash a key according to its kind. */
static uint64_t hash_key(const void *key, int32_t key_size, int8_t key_kind) {
    switch (key_kind) {
        case GRAY_ELEM_STRING: {
            const GrayString *key_string = (const GrayString *)key;
            return hash_bytes(key_string->data, key_string->len);
        }
        case GRAY_ELEM_F64:
            return hash_f64(key);
        case GRAY_ELEM_F32:
            return hash_f32(key);
        default:
            return hash_bytes(key, key_size);
    }
}

/* Compare two keys according to their kind. */
static bool keys_equal(const void *left, const void *right, int32_t key_size, int8_t key_kind) {
    switch (key_kind) {
        case GRAY_ELEM_STRING: {
            const GrayString *left_string = (const GrayString *)left;
            const GrayString *right_string = (const GrayString *)right;
            if (left_string->len != right_string->len) return false;
            return memcmp(left_string->data, right_string->data, (size_t)left_string->len) == 0;
        }
        case GRAY_ELEM_F64:
            return floats_equal_f64(left, right);
        case GRAY_ELEM_F32:
            return floats_equal_f32(left, right);
        default:
            return memcmp(left, right, (size_t)key_size) == 0;
    }
}

static void *key_pointer(GrayMap *map, int32_t index) {
    return (char *)map->keys + (size_t)index * (size_t)map->key_size;
}

static void *value_pointer(GrayMap *map, int32_t index) {
    return (char *)map->values + (size_t)index * (size_t)map->value_size;
}

/* Write a key into a slot, taking ownership of it. A GrayString key is only
 * a header pointing at character data the caller owns, and that data can
 * live in a shorter-lived arena than the map — a loop body's per-iteration
 * arena, say — so the header alone would dangle once the caller's scope
 * unwinds. Copy the characters into the map's own arena, the same way
 * gray_map_copy does, so a key stays valid for the map's whole lifetime.
 * Fall back to the caller's arena for maps built without an owning one. */
static void store_key(GrayArena *arena, GrayMap *map, int32_t slot, const void *key) {
    void *destination = key_pointer(map, slot);
    memcpy(destination, key, (size_t)map->key_size);
    if (map->key_kind == GRAY_ELEM_STRING) {
        GrayArena *owner = map->arena ? map->arena : arena;
        if (owner) {
            GrayString *key_string = (GrayString *)destination;
            *key_string = gray_string_new(owner, key_string->data, key_string->len);
        }
    }
}

/* Capacity is always a power of two so the probe loop masks (idx & (cap-1))
 * instead of running a divide per step. Callers may pass any hint (a map
 * literal's entry count, a SQL column count); round it up. The 1<<30
 * ceiling sits far above any realistic map and clear of int32 overflow. */
static int32_t gray_map_round_capacity(int32_t capacity) {
    if (capacity < GRAY_MAP_MIN_CAPACITY) return GRAY_MAP_MIN_CAPACITY;
    if (capacity > (1 << 30)) return 1 << 30;
    int32_t power_of_two = GRAY_MAP_MIN_CAPACITY;
    while (power_of_two < capacity) power_of_two <<= 1;
    return power_of_two;
}

GrayMap gray_map_new_kind(GrayArena *arena, int32_t key_size, int32_t value_size, int32_t initial_capacity,
                          int8_t key_kind, int8_t value_kind) {
    initial_capacity = gray_map_round_capacity(initial_capacity);
    GrayMap map;
    map.arena = arena;
    map.key_size = key_size;
    map.value_size = value_size;
    map.count = 0;
    map.capacity = initial_capacity;
    map.order_len = 0;
    map.iterating = 0;
    map.key_kind = key_kind;
    map.value_kind = value_kind;
    map.keys = gray_arena_alloc_uninitialized(arena, (size_t)initial_capacity * (size_t)key_size);
    map.values = gray_arena_alloc_uninitialized(arena, (size_t)initial_capacity * (size_t)value_size);
    map.states = gray_arena_alloc(arena, (size_t)initial_capacity);
    map.order = gray_arena_alloc_uninitialized(arena, (size_t)initial_capacity * sizeof(int32_t));
    map.order_position = gray_arena_alloc_uninitialized(arena, (size_t)initial_capacity * sizeof(int32_t));
    return map;
}

static int32_t find_slot(GrayMap *map, const void *key) {
    uint64_t hash = hash_key(key, map->key_size, map->key_kind);
    int32_t mask = map->capacity - 1;
    int32_t index = (int32_t)(hash & (uint64_t)mask);
    for (int32_t i = 0; i < map->capacity; i++) {
        int32_t probe = (index + i) & mask;
        if (map->states[probe] == 0) return -1; /* empty — not found */
        if (map->states[probe] == 1 && keys_equal(key_pointer(map, probe), key, map->key_size, map->key_kind)) {
            return probe;
        }
    }
    return -1;
}

/* Grow into the arena the map was created in, not the caller's ambient
 * arena. A map reached through a pointer or struct field outlives the
 * function that mutates it; allocating the new tables in a short-lived
 * scope arena leaves the map pointing at reclaimed memory once that
 * scope unwinds. */
static void map_rebuild(GrayArena *arena, GrayMap *map, int32_t new_capacity) {
    new_capacity = gray_map_round_capacity(new_capacity);
    if (map->arena) arena = map->arena;
    void *old_keys = map->keys;
    void *old_values = map->values;
    uint8_t *old_states = map->states;
    int32_t *old_order = map->order;
    int32_t old_order_length = map->order_len;

    map->capacity = new_capacity;
    map->keys = gray_arena_alloc_uninitialized(arena, (size_t)map->capacity * (size_t)map->key_size);
    map->values = gray_arena_alloc_uninitialized(arena, (size_t)map->capacity * (size_t)map->value_size);
    map->states = gray_arena_alloc(arena, (size_t)map->capacity);
    map->order = gray_arena_alloc_uninitialized(arena, (size_t)map->capacity * sizeof(int32_t));
    map->order_position = gray_arena_alloc_uninitialized(arena, (size_t)map->capacity * sizeof(int32_t));
    map->count = 0;
    map->order_len = 0;

    /* Re-insert in original insertion order to preserve it */
    for (int32_t i = 0; i < old_order_length; i++) {
        int32_t slot = old_order[i];
        if (slot >= 0 && old_states[slot] == 1) {
            gray_map_set(arena, map,
                (char *)old_keys + (size_t)slot * (size_t)map->key_size,
                (char *)old_values + (size_t)slot * (size_t)map->value_size,
                __FILE__, __LINE__);
        }
    }
}

void *gray_map_get(GrayMap *map, const void *key) {
    int32_t index = find_slot(map, key);
    if (index < 0) return NULL;
    return value_pointer(map, index);
}

void gray_map_set(GrayArena *arena, GrayMap *map, const void *key, const void *value, const char *file, int line) {
    if (gray_atomic_load32(&map->iterating) > 0)
        gray_panic_code_at(file, line, "P0035", "cannot modify map during for_each iteration");
    /* Check load factor */
    if (map->count * GRAY_MAP_LOAD_DENOMINATOR >= map->capacity * GRAY_MAP_LOAD_NUMERATOR) {
        map_rebuild(arena, map, map->capacity * 2);
    }
    /* gray_map_remove leaves holes rather than shifting the tail down, so
     * order_len creeps toward capacity independently of count. Rebuild at
     * the same capacity to squeeze them out; the load factor above caps
     * count at 3/4 capacity, so this frees at least a quarter of the array
     * and the append stays amortized O(1). Rebuilding (rather than
     * compacting in place) keeps stale GrayMap copies pointing at the old,
     * still-consistent arrays instead of a half-rewritten shared one. */
    if (map->order && map->order_len >= map->capacity) {
        map_rebuild(arena, map, map->capacity);
    }

    uint64_t hash = hash_key(key, map->key_size, map->key_kind);
    int32_t mask = map->capacity - 1;
    int32_t index = (int32_t)(hash & (uint64_t)mask);
    int32_t first_tombstone = -1;
    for (int32_t i = 0; i < map->capacity; i++) {
        int32_t probe = (index + i) & mask;
        if (map->states[probe] == 2) {
            /* Tombstone — record it and keep scanning for an existing key */
            if (first_tombstone < 0) first_tombstone = probe;
            continue;
        }
        if (map->states[probe] == 0) {
            /* Empty — key definitely not in map; insert at tombstone if seen, else here */
            int32_t slot = (first_tombstone >= 0) ? first_tombstone : probe;
            store_key(arena, map, slot, key);
            memcpy(value_pointer(map, slot), value, (size_t)map->value_size);
            map->states[slot] = 1;
            if (map->order) { map->order_position[slot] = map->order_len; map->order[map->order_len++] = slot; }
            map->count++;
            return;
        }
        if (keys_equal(key_pointer(map, probe), key, map->key_size, map->key_kind)) {
            /* Update existing — never creates a duplicate */
            memcpy(value_pointer(map, probe), value, (size_t)map->value_size);
            return;
        }
    }
    /* Probe chain full of tombstones and the key was not found — use first tombstone */
    if (first_tombstone >= 0) {
        store_key(arena, map, first_tombstone, key);
        memcpy(value_pointer(map, first_tombstone), value, (size_t)map->value_size);
        map->states[first_tombstone] = 1;
        if (map->order) { map->order_position[first_tombstone] = map->order_len; map->order[map->order_len++] = first_tombstone; }
        map->count++;
    }
}

bool gray_map_has(GrayMap *map, const void *key) {
    return find_slot(map, key) >= 0;
}

bool gray_map_remove(GrayMap *map, const void *key, const char *file, int line) {
    if (gray_atomic_load32(&map->iterating) > 0)
        gray_panic_code_at(file, line, "P0035", "cannot modify map during for_each iteration");
    int32_t index = find_slot(map, key);
    if (index < 0) return false;
    map->states[index] = 2; /* tombstone */
    map->count--;
    /* Punch a hole instead of shifting the tail down: order_pos gives the
     * entry's index directly, so this is O(1) where the old linear scan
     * plus memmove was O(n). Readers skip the -1; gray_map_set reclaims
     * the space when the array fills. */
    if (map->order) {
        int32_t position = map->order_position[index];
        if (position >= 0 && position < map->order_len && map->order[position] == index) map->order[position] = -1;
    }
    return true;
}

void gray_map_clear(GrayMap *map, const char *file, int line) {
    if (gray_atomic_load32(&map->iterating) > 0)
        gray_panic_code_at(file, line, "P0035", "cannot modify map during for_each iteration");
    if (map->states) memset(map->states, 0, sizeof(uint8_t) * (size_t)map->capacity);
    map->count = 0;
    map->order_len = 0;
}

void *gray_map_get_str(GrayMap *map, GrayString key) {
    return gray_map_get(map, &key);
}

void gray_map_set_str(GrayArena *arena, GrayMap *map, GrayString key, const void *value, const char *file, int line) {
    gray_map_set(arena, map, &key, value, file, line);
}

void *gray_map_key_at(GrayMap *map, int32_t internal_index) {
    return key_pointer(map, internal_index);
}

void *gray_map_value_at(GrayMap *map, int32_t internal_index) {
    return value_pointer(map, internal_index);
}

GrayMap gray_map_copy(GrayArena *arena, const GrayMap *source) {
    GrayMap map;
    map.arena = arena;
    map.key_size = source->key_size;
    map.value_size = source->value_size;
    map.count = source->count;
    map.capacity = source->capacity;
    map.order_len = source->order_len;
    map.iterating = 0;
    map.key_kind = source->key_kind;
    map.value_kind = source->value_kind;

    size_t keys_bytes = (size_t)source->capacity * (size_t)source->key_size;
    size_t values_bytes = (size_t)source->capacity * (size_t)source->value_size;
    size_t order_bytes = (size_t)source->capacity * sizeof(int32_t);

    map.keys = gray_arena_alloc_uninitialized(arena, keys_bytes);
    map.values = gray_arena_alloc_uninitialized(arena, values_bytes);
    map.states = gray_arena_alloc_uninitialized(arena, (size_t)source->capacity);
    map.order = gray_arena_alloc_uninitialized(arena, order_bytes);
    map.order_position = gray_arena_alloc_uninitialized(arena, order_bytes);

    if (keys_bytes)  memcpy(map.keys,   source->keys,   keys_bytes);
    if (values_bytes)  memcpy(map.values, source->values, values_bytes);
    memcpy(map.states, source->states, (size_t)source->capacity);
    if (order_bytes) memcpy(map.order,  source->order,  order_bytes);
    if (order_bytes) memcpy(map.order_position, source->order_position, order_bytes);

    /* String keys store a pointer into the source arena — deep-copy the
     * character data so the returned map owns its key strings. */
    if (source->key_kind == GRAY_ELEM_STRING) {
        for (int32_t i = 0; i < source->capacity; i++) {
            if (map.states[i] == 1) {
                GrayString *key_string = (GrayString *)((char *)map.keys + (size_t)i * (size_t)map.key_size);
                *key_string = gray_string_new(arena, key_string->data, key_string->len);
            }
        }
    }

    return map;
}
