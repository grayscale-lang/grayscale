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

/* get_keys / get_values allocate exactly map->count slots (the live entry
 * count) and write each entry straight into the result, in insertion order,
 * rather than a gray_array_push per entry. */

GrayArray gray_maps_get_keys(GrayArena *arena, GrayMap *map) {
    GrayArray array = gray_array_new(arena, map->key_size, map->count > 0 ? map->count : 4, map->key_kind);
    size_t entry_key_size = (size_t)map->key_size;
    char *output = (char *)array.data;
    int32_t entry_count = 0;
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t slot = map->order[order_index];
        if (slot >= 0 && map->states[slot] == 1) {
            memcpy(output + (size_t)entry_count * entry_key_size, (char *)map->keys + (size_t)slot * entry_key_size, entry_key_size);
            entry_count++;
        }
    }
    array.len = entry_count;
    return array;
}

GrayArray gray_maps_get_values(GrayArena *arena, GrayMap *map) {
    GrayArray array = gray_array_new(arena, map->value_size, map->count > 0 ? map->count : 4, map->value_kind);
    size_t entry_value_size = (size_t)map->value_size;
    char *output = (char *)array.data;
    int32_t entry_count = 0;
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t slot = map->order[order_index];
        if (slot >= 0 && map->states[slot] == 1) {
            memcpy(output + (size_t)entry_count * entry_value_size, (char *)map->values + (size_t)slot * entry_value_size, entry_value_size);
            entry_count++;
        }
    }
    array.len = entry_count;
    return array;
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
        left->key_kind, left->value_kind);
    /* Copy all entries from left */
    for (int32_t order_index = 0; order_index < left->order_len; order_index++) {
        int32_t slot = left->order[order_index];
        if (slot >= 0 && left->states[slot] == 1) {
            void *key = (char *)left->keys + (size_t)slot * (size_t)left->key_size;
            void *value = (char *)left->values + (size_t)slot * (size_t)left->value_size;
            GRAY_MAP_SET(arena, &result, key, value);
        }
    }
    /* Copy all entries from right (overwrites left on conflict) */
    for (int32_t order_index = 0; order_index < right->order_len; order_index++) {
        int32_t slot = right->order[order_index];
        if (slot >= 0 && right->states[slot] == 1) {
            void *key = (char *)right->keys + (size_t)slot * (size_t)right->key_size;
            void *value = (char *)right->values + (size_t)slot * (size_t)right->value_size;
            GRAY_MAP_SET(arena, &result, key, value);
        }
    }
    return result;
}

bool gray_maps_contains_value(GrayMap *map, const void *value) {
    for (int32_t order_index = 0; order_index < map->order_len; order_index++) {
        int32_t slot = map->order[order_index];
        if (slot >= 0 && map->states[slot] == 1) {
            void *searched_value = (char *)map->values + (size_t)slot * (size_t)map->value_size;
            if (memcmp(searched_value, value, (size_t)map->value_size) == 0) return true;
        }
    }
    return false;
}

bool gray_maps_is_equal(GrayMap *left, GrayMap *right, bool has_string_keys, bool has_string_values) {
    if (left->count != right->count) return false;
    if (left->key_size != right->key_size) return false;
    if (left->value_size != right->value_size) return false;
    for (int32_t order_index = 0; order_index < left->order_len; order_index++) {
        int32_t slot = left->order[order_index];
        if (slot < 0 || left->states[slot] != 1) continue;
        void *left_key = (char *)left->keys + (size_t)slot * (size_t)left->key_size;
        void *left_value = (char *)left->values + (size_t)slot * (size_t)left->value_size;
        void *right_value = has_string_keys
            ? gray_map_get_str(right, *(GrayString *)left_key)
            : gray_map_get(right, left_key);
        if (!right_value) return false;
        if (has_string_values) {
            GrayString *left_string = (GrayString *)left_value;
            GrayString *right_string = (GrayString *)right_value;
            if (left_string->len != right_string->len) return false;
            if (left_string->len > 0 && memcmp(left_string->data, right_string->data, (size_t)left_string->len) != 0) return false;
        } else {
            if (memcmp(left_value, right_value, (size_t)left->value_size) != 0) return false;
        }
    }
    return true;
}
