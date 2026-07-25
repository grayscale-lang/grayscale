/*
 * maps.c — Implementation of the maps stdlib module.
 * Provides key/value extraction, membership testing, removal, and
 * merge operations on GrayMap values.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "maps.h"
#include <string.h>

GrayArray gray_maps_get_keys(GrayArena *arena, GrayMap *m) {
    GrayArray arr = gray_array_new(arena, m->key_size, m->count > 0 ? m->count : 4);
    /* Iterate in insertion order */
    for (int32_t order_index = 0; order_index < m->order_len; order_index++) {
        int32_t slot = m->order[order_index];
        if (m->states[slot] == 1) {
            void *key = (char *)m->keys + (size_t)slot * (size_t)m->key_size;
            GRAY_ARRAY_PUSH(arena, &arr, key);
        }
    }
    return arr;
}

GrayArray gray_maps_get_values(GrayArena *arena, GrayMap *m) {
    GrayArray arr = gray_array_new(arena, m->value_size, m->count > 0 ? m->count : 4);
    /* Iterate in insertion order */
    for (int32_t order_index = 0; order_index < m->order_len; order_index++) {
        int32_t slot = m->order[order_index];
        if (m->states[slot] == 1) {
            void *val = (char *)m->values + (size_t)slot * (size_t)m->value_size;
            GRAY_ARRAY_PUSH(arena, &arr, val);
        }
    }
    return arr;
}

bool gray_maps_has_key(GrayMap *m, const void *key) {
    return gray_map_has(m, key);
}

bool gray_maps_is_empty(GrayMap *m) {
    return m->count == 0;
}

GrayMap gray_maps_merge(GrayArena *arena, GrayMap *m1, GrayMap *m2) {
    GrayMap result = gray_map_new_kind(arena, m1->key_size, m1->value_size,
        m1->count + m2->count > 8 ? (m1->count + m2->count) * 2 : 8,
        m1->key_kind);
    /* Copy all entries from m1 */
    for (int32_t order_index = 0; order_index < m1->order_len; order_index++) {
        int32_t slot = m1->order[order_index];
        if (m1->states[slot] == 1) {
            void *key = (char *)m1->keys + (size_t)slot * (size_t)m1->key_size;
            void *val = (char *)m1->values + (size_t)slot * (size_t)m1->value_size;
            GRAY_MAP_SET(arena, &result, key, val);
        }
    }
    /* Copy all entries from m2 (overwrites m1 on conflict) */
    for (int32_t order_index = 0; order_index < m2->order_len; order_index++) {
        int32_t slot = m2->order[order_index];
        if (m2->states[slot] == 1) {
            void *key = (char *)m2->keys + (size_t)slot * (size_t)m2->key_size;
            void *val = (char *)m2->values + (size_t)slot * (size_t)m2->value_size;
            GRAY_MAP_SET(arena, &result, key, val);
        }
    }
    return result;
}

bool gray_maps_contains_value(GrayMap *m, const void *value) {
    for (int32_t order_index = 0; order_index < m->order_len; order_index++) {
        int32_t slot = m->order[order_index];
        if (m->states[slot] == 1) {
            void *val = (char *)m->values + (size_t)slot * (size_t)m->value_size;
            if (memcmp(val, value, (size_t)m->value_size) == 0) return true;
        }
    }
    return false;
}

bool gray_maps_is_equal(GrayMap *a, GrayMap *b, bool str_keys, bool str_values) {
    if (a->count != b->count) return false;
    if (a->key_size != b->key_size) return false;
    if (a->value_size != b->value_size) return false;
    for (int32_t order_index = 0; order_index < a->order_len; order_index++) {
        int32_t slot = a->order[order_index];
        if (a->states[slot] != 1) continue;
        void *ka = (char *)a->keys + (size_t)slot * (size_t)a->key_size;
        void *va = (char *)a->values + (size_t)slot * (size_t)a->value_size;
        void *vb = str_keys
            ? gray_map_get_str(b, *(GrayString *)ka)
            : gray_map_get(b, ka);
        if (!vb) return false;
        if (str_values) {
            GrayString *sa = (GrayString *)va;
            GrayString *sb = (GrayString *)vb;
            if (sa->len != sb->len) return false;
            if (sa->len > 0 && memcmp(sa->data, sb->data, (size_t)sa->len) != 0) return false;
        } else {
            if (memcmp(va, vb, (size_t)a->value_size) != 0) return false;
        }
    }
    return true;
}
