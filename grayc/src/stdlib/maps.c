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

GrayArray gray_maps_get_keys(GrayArena *arena, GrayMap *map) {
    GrayArray arr = gray_array_new(arena, map->key_size, map->count > 0 ? map->count : 4);
    /* Iterate in insertion order */
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t slot = map->order[order_index];
        if (slot >= 0 && map->states[slot] == 1) {
            void *key = (char *)map->keys + (size_t)slot * (size_t)map->key_size;
            GRAY_ARRAY_PUSH(arena, &arr, key);
        }
    }
    return arr;
}

GrayArray gray_maps_get_values(GrayArena *arena, GrayMap *map) {
    GrayArray arr = gray_array_new(arena, map->value_size, map->count > 0 ? map->count : 4);
    /* Iterate in insertion order */
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t slot = map->order[order_index];
        if (slot >= 0 && map->states[slot] == 1) {
            void *val = (char *)map->values + (size_t)slot * (size_t)map->value_size;
            GRAY_ARRAY_PUSH(arena, &arr, val);
        }
    }
    return arr;
}

bool gray_maps_has_key(GrayMap *map, const void *key) {
    return gray_map_has(map, key);
}

bool gray_maps_is_empty(GrayMap *map) {
    return map->count == 0;
}

GrayMap gray_maps_merge(GrayArena *arena, GrayMap *left, GrayMap *right) {
    GrayMap result = gray_map_new_kind(arena, left->key_size, left->value_size,
        left->count + right->count > 8 ? (left->count + right->count) * 2 : 8,
        left->key_kind);
    /* Copy all entries from left */
    for (int32_t order_index = 0; order_index < left->order_len; order_index++) {
        int32_t slot = left->order[order_index];
        if (slot >= 0 && left->states[slot] == 1) {
            void *key = (char *)left->keys + (size_t)slot * (size_t)left->key_size;
            void *val = (char *)left->values + (size_t)slot * (size_t)left->value_size;
            GRAY_MAP_SET(arena, &result, key, val);
        }
    }
    /* Copy all entries from right (overwrites left on conflict) */
    for (int32_t order_index = 0; order_index < right->order_len; order_index++) {
        int32_t slot = right->order[order_index];
        if (slot >= 0 && right->states[slot] == 1) {
            void *key = (char *)right->keys + (size_t)slot * (size_t)right->key_size;
            void *val = (char *)right->values + (size_t)slot * (size_t)right->value_size;
            GRAY_MAP_SET(arena, &result, key, val);
        }
    }
    return result;
}

bool gray_maps_contains_value(GrayMap *map, const void *value) {
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t slot = map->order[order_index];
        if (slot >= 0 && map->states[slot] == 1) {
            void *val = (char *)map->values + (size_t)slot * (size_t)map->value_size;
            if (memcmp(val, value, (size_t)map->value_size) == 0) return true;
        }
    }
    return false;
}

bool gray_maps_is_equal(GrayMap *left, GrayMap *right, bool str_keys, bool str_values) {
    if (left->count != right->count) return false;
    if (left->key_size != right->key_size) return false;
    if (left->value_size != right->value_size) return false;
    for (int32_t order_index = 0; order_index < left->order_len; order_index++) {
        int32_t slot = left->order[order_index];
        if (slot < 0 || left->states[slot] != 1) continue;
        void *left_key = (char *)left->keys + (size_t)slot * (size_t)left->key_size;
        void *left_val = (char *)left->values + (size_t)slot * (size_t)left->value_size;
        void *right_val = str_keys
            ? gray_map_get_str(right, *(GrayString *)left_key)
            : gray_map_get(right, left_key);
        if (!right_val) return false;
        if (str_values) {
            GrayString *left_str = (GrayString *)left_val;
            GrayString *right_str = (GrayString *)right_val;
            if (left_str->len != right_str->len) return false;
            if (left_str->len > 0 && memcmp(left_str->data, right_str->data, (size_t)left_str->len) != 0) return false;
        } else {
            if (memcmp(left_val, right_val, (size_t)left->value_size) != 0) return false;
        }
    }
    return true;
}
