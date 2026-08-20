/*
 * map.c — Hash map implementation for the Grayscale runtime.
 * Open-addressing table with linear probing, FNV-1a hashing, and
 * special handling for float key normalization and string keys.
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
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(&gray_hash_seed, sizeof(gray_hash_seed), 1, f);
        fclose(f);
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
 * floats (which says +0.0 == -0.0) and gives NaN keys a single bucket
 * instead of one per source-of-NaN. */

static uint64_t hash_f64(const void *key) {
    double v;
    memcpy(&v, key, sizeof(v));
    uint64_t bits;
    if (v == 0.0) {
        bits = 0;                          /* +0.0 / -0.0 collapse */
    } else if (v != v) {
        bits = 0x7FF8000000000000ULL;      /* canonical quiet NaN */
    } else {
        memcpy(&bits, &v, sizeof(bits));
    }
    return hash_bytes(&bits, sizeof(bits));
}

static uint64_t hash_f32(const void *key) {
    float v;
    memcpy(&v, key, sizeof(v));
    uint32_t bits;
    if (v == 0.0f) {
        bits = 0;
    } else if (v != v) {
        bits = 0x7FC00000U;
    } else {
        memcpy(&bits, &v, sizeof(bits));
    }
    return hash_bytes(&bits, sizeof(bits));
}

static bool floats_equal_f64(const void *a, const void *b) {
    double da, db;
    memcpy(&da, a, sizeof(da));
    memcpy(&db, b, sizeof(db));
    if (da == 0.0 && db == 0.0) return true;
    if (da != da && db != db) return true;
    return da == db;
}

static bool floats_equal_f32(const void *a, const void *b) {
    float fa, fb;
    memcpy(&fa, a, sizeof(fa));
    memcpy(&fb, b, sizeof(fb));
    if (fa == 0.0f && fb == 0.0f) return true;
    if (fa != fa && fb != fb) return true;
    return fa == fb;
}

/* Hash a key according to its kind. */
static uint64_t hash_key(const void *key, int32_t key_size, int8_t key_kind) {
    switch (key_kind) {
        case GRAY_MAP_KEY_STRING: {
            const GrayString *s = (const GrayString *)key;
            return hash_bytes(s->data, s->len);
        }
        case GRAY_MAP_KEY_F64:
            return hash_f64(key);
        case GRAY_MAP_KEY_F32:
            return hash_f32(key);
        default:
            return hash_bytes(key, key_size);
    }
}

/* Compare two keys according to their kind. */
static bool keys_equal(const void *a, const void *b, int32_t key_size, int8_t key_kind) {
    switch (key_kind) {
        case GRAY_MAP_KEY_STRING: {
            const GrayString *sa = (const GrayString *)a;
            const GrayString *sb = (const GrayString *)b;
            if (sa->len != sb->len) return false;
            return memcmp(sa->data, sb->data, (size_t)sa->len) == 0;
        }
        case GRAY_MAP_KEY_F64:
            return floats_equal_f64(a, b);
        case GRAY_MAP_KEY_F32:
            return floats_equal_f32(a, b);
        default:
            return memcmp(a, b, (size_t)key_size) == 0;
    }
}

static void *key_ptr(GrayMap *m, int32_t idx) {
    return (char *)m->keys + (size_t)idx * (size_t)m->key_size;
}

static void *val_ptr(GrayMap *m, int32_t idx) {
    return (char *)m->values + (size_t)idx * (size_t)m->value_size;
}

GrayMap gray_map_new_kind(GrayArena *arena, int32_t key_size, int32_t value_size, int32_t initial_cap, int8_t key_kind) {
    if (initial_cap < GRAY_MAP_MIN_CAP) initial_cap = GRAY_MAP_MIN_CAP;
    GrayMap m;
    m.arena = arena;
    m.key_size = key_size;
    m.value_size = value_size;
    m.count = 0;
    m.capacity = initial_cap;
    m.order_len = 0;
    m.iterating = 0;
    m.key_kind = key_kind;
    m.keys = gray_arena_alloc_uninitialized(arena, (size_t)initial_cap * (size_t)key_size);
    m.values = gray_arena_alloc_uninitialized(arena, (size_t)initial_cap * (size_t)value_size);
    m.states = gray_arena_alloc(arena, (size_t)initial_cap);
    m.order = gray_arena_alloc_uninitialized(arena, (size_t)initial_cap * sizeof(int32_t));
    m.order_pos = gray_arena_alloc_uninitialized(arena, (size_t)initial_cap * sizeof(int32_t));
    return m;
}

GrayMap gray_map_new(GrayArena *arena, int32_t key_size, int32_t value_size, int32_t initial_cap) {
    int8_t kind = (key_size == (int32_t)sizeof(GrayString))
        ? GRAY_MAP_KEY_STRING
        : GRAY_MAP_KEY_BYTES;
    return gray_map_new_kind(arena, key_size, value_size, initial_cap, kind);
}

static int32_t find_slot(GrayMap *m, const void *key) {
    uint64_t h = hash_key(key, m->key_size, m->key_kind);
    int32_t idx = (int32_t)(h % (uint64_t)m->capacity);
    for (int32_t i = 0; i < m->capacity; i++) {
        int32_t probe = (idx + i) % m->capacity;
        if (m->states[probe] == 0) return -1; /* empty — not found */
        if (m->states[probe] == 1 && keys_equal(key_ptr(m, probe), key, m->key_size, m->key_kind)) {
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
static void map_rebuild(GrayArena *arena, GrayMap *m, int32_t new_cap) {
    if (m->arena) arena = m->arena;
    void *old_keys = m->keys;
    void *old_values = m->values;
    uint8_t *old_states = m->states;
    int32_t *old_order = m->order;
    int32_t old_order_len = m->order_len;

    m->capacity = new_cap;
    m->keys = gray_arena_alloc_uninitialized(arena, (size_t)m->capacity * (size_t)m->key_size);
    m->values = gray_arena_alloc_uninitialized(arena, (size_t)m->capacity * (size_t)m->value_size);
    m->states = gray_arena_alloc(arena, (size_t)m->capacity);
    m->order = gray_arena_alloc_uninitialized(arena, (size_t)m->capacity * sizeof(int32_t));
    m->order_pos = gray_arena_alloc_uninitialized(arena, (size_t)m->capacity * sizeof(int32_t));
    m->count = 0;
    m->order_len = 0;

    /* Re-insert in original insertion order to preserve it */
    for (int32_t i = 0; i < old_order_len; i++) {
        int32_t slot = old_order[i];
        if (slot >= 0 && old_states[slot] == 1) {
            gray_map_set(arena, m,
                (char *)old_keys + (size_t)slot * (size_t)m->key_size,
                (char *)old_values + (size_t)slot * (size_t)m->value_size,
                __FILE__, __LINE__);
        }
    }
}

void *gray_map_get(GrayMap *m, const void *key) {
    int32_t idx = find_slot(m, key);
    if (idx < 0) return NULL;
    return val_ptr(m, idx);
}

void gray_map_set(GrayArena *arena, GrayMap *m, const void *key, const void *value, const char *file, int line) {
    if (gray_atomic_load32(&m->iterating) > 0)
        gray_panic_code_at(file, line, "P0035", "cannot modify map during for_each iteration");
    /* Check load factor */
    if (m->count * GRAY_MAP_LOAD_DEN >= m->capacity * GRAY_MAP_LOAD_NUM) {
        map_rebuild(arena, m, m->capacity * 2);
    }
    /* gray_map_remove leaves holes rather than shifting the tail down, so
     * order_len creeps toward capacity independently of count. Rebuild at
     * the same capacity to squeeze them out; the load factor above caps
     * count at 3/4 capacity, so this frees at least a quarter of the array
     * and the append stays amortized O(1). Rebuilding (rather than
     * compacting in place) keeps stale GrayMap copies pointing at the old,
     * still-consistent arrays instead of a half-rewritten shared one. */
    if (m->order && m->order_len >= m->capacity) {
        map_rebuild(arena, m, m->capacity);
    }

    uint64_t h = hash_key(key, m->key_size, m->key_kind);
    int32_t idx = (int32_t)(h % (uint64_t)m->capacity);
    int32_t first_tombstone = -1;
    for (int32_t i = 0; i < m->capacity; i++) {
        int32_t probe = (idx + i) % m->capacity;
        if (m->states[probe] == 2) {
            /* Tombstone — record it and keep scanning for an existing key */
            if (first_tombstone < 0) first_tombstone = probe;
            continue;
        }
        if (m->states[probe] == 0) {
            /* Empty — key definitely not in map; insert at tombstone if seen, else here */
            int32_t slot = (first_tombstone >= 0) ? first_tombstone : probe;
            memcpy(key_ptr(m, slot), key, (size_t)m->key_size);
            memcpy(val_ptr(m, slot), value, (size_t)m->value_size);
            m->states[slot] = 1;
            if (m->order) { m->order_pos[slot] = m->order_len; m->order[m->order_len++] = slot; }
            m->count++;
            return;
        }
        if (keys_equal(key_ptr(m, probe), key, m->key_size, m->key_kind)) {
            /* Update existing — never creates a duplicate */
            memcpy(val_ptr(m, probe), value, (size_t)m->value_size);
            return;
        }
    }
    /* Probe chain full of tombstones and the key was not found — use first tombstone */
    if (first_tombstone >= 0) {
        memcpy(key_ptr(m, first_tombstone), key, (size_t)m->key_size);
        memcpy(val_ptr(m, first_tombstone), value, (size_t)m->value_size);
        m->states[first_tombstone] = 1;
        if (m->order) { m->order_pos[first_tombstone] = m->order_len; m->order[m->order_len++] = first_tombstone; }
        m->count++;
    }
}

bool gray_map_has(GrayMap *m, const void *key) {
    return find_slot(m, key) >= 0;
}

bool gray_map_remove(GrayMap *m, const void *key, const char *file, int line) {
    if (gray_atomic_load32(&m->iterating) > 0)
        gray_panic_code_at(file, line, "P0035", "cannot modify map during for_each iteration");
    int32_t idx = find_slot(m, key);
    if (idx < 0) return false;
    m->states[idx] = 2; /* tombstone */
    m->count--;
    /* Punch a hole instead of shifting the tail down: order_pos gives the
     * entry's index directly, so this is O(1) where the old linear scan
     * plus memmove was O(n). Readers skip the -1; gray_map_set reclaims
     * the space when the array fills. */
    if (m->order) {
        int32_t pos = m->order_pos[idx];
        if (pos >= 0 && pos < m->order_len && m->order[pos] == idx) m->order[pos] = -1;
    }
    return true;
}

void gray_map_clear(GrayMap *m, const char *file, int line) {
    if (gray_atomic_load32(&m->iterating) > 0)
        gray_panic_code_at(file, line, "P0035", "cannot modify map during for_each iteration");
    if (m->states) memset(m->states, 0, sizeof(uint8_t) * (size_t)m->capacity);
    m->count = 0;
    m->order_len = 0;
}

void *gray_map_get_str(GrayMap *m, GrayString key) {
    return gray_map_get(m, &key);
}

void gray_map_set_str(GrayArena *arena, GrayMap *m, GrayString key, const void *value, const char *file, int line) {
    gray_map_set(arena, m, &key, value, file, line);
}

void *gray_map_key_at(GrayMap *m, int32_t internal_idx) {
    return key_ptr(m, internal_idx);
}

void *gray_map_value_at(GrayMap *m, int32_t internal_idx) {
    return val_ptr(m, internal_idx);
}

GrayMap gray_map_copy(GrayArena *arena, const GrayMap *src) {
    GrayMap m;
    m.arena = arena;
    m.key_size = src->key_size;
    m.value_size = src->value_size;
    m.count = src->count;
    m.capacity = src->capacity;
    m.order_len = src->order_len;
    m.iterating = 0;
    m.key_kind = src->key_kind;

    size_t keys_bytes = (size_t)src->capacity * (size_t)src->key_size;
    size_t vals_bytes = (size_t)src->capacity * (size_t)src->value_size;
    size_t order_bytes = (size_t)src->capacity * sizeof(int32_t);

    m.keys = gray_arena_alloc_uninitialized(arena, keys_bytes);
    m.values = gray_arena_alloc_uninitialized(arena, vals_bytes);
    m.states = gray_arena_alloc_uninitialized(arena, (size_t)src->capacity);
    m.order = gray_arena_alloc_uninitialized(arena, order_bytes);
    m.order_pos = gray_arena_alloc_uninitialized(arena, order_bytes);

    if (keys_bytes)  memcpy(m.keys,   src->keys,   keys_bytes);
    if (vals_bytes)  memcpy(m.values, src->values, vals_bytes);
    memcpy(m.states, src->states, (size_t)src->capacity);
    if (order_bytes) memcpy(m.order,  src->order,  order_bytes);
    if (order_bytes) memcpy(m.order_pos, src->order_pos, order_bytes);

    /* String keys store a pointer into the source arena — deep-copy the
     * character data so the returned map owns its key strings. */
    if (src->key_kind == GRAY_MAP_KEY_STRING) {
        for (int32_t i = 0; i < src->capacity; i++) {
            if (m.states[i] == 1) {
                GrayString *ks = (GrayString *)((char *)m.keys + (size_t)i * (size_t)m.key_size);
                *ks = gray_string_new(arena, ks->data, ks->len);
            }
        }
    }

    return m;
}
