/*
 * maps.h — Public interface for the maps stdlib module.
 * Declares key/value extraction, membership testing, removal, and
 * merge operations on GrayMap values.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_MAPS_H
#define GRAY_MAPS_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include "../runtime/map.h"

/*@man get_keys
 *@module maps
 *@group Query
 *@sig get_keys(m map[K:V]) -> [K]
 *@desc Returns all keys in m as an array. Order reflects insertion order.
 *@example
 *   import @maps
 *   mut ages map[string:int] = {"alice": 30, "bob": 25}
 *   println(maps.get_keys(ages))
 *@end
 */
/* maps.keys(m) — return array of keys */
GrayArray gray_maps_get_keys(GrayArena *arena, GrayMap *m);

/*@man get_values
 *@module maps
 *@group Query
 *@sig get_values(m map[K:V]) -> [V]
 *@desc Returns all values in m as an array. Order reflects insertion order.
 *@example
 *   import @maps
 *   mut ages map[string:int] = {"alice": 30, "bob": 25}
 *   println(maps.get_values(ages))
 *@end
 */
/* maps.values(m) — return array of values */
GrayArray gray_maps_get_values(GrayArena *arena, GrayMap *m);

/*@man has_key
 *@module maps
 *@group Query
 *@sig has_key(m map[K:V], key K) -> bool
 *@desc Returns true if m contains an entry with the given key.
 *@example
 *   import @maps
 *   mut ages map[string:int] = {"alice": 30}
 *   println(maps.has_key(ages, "alice"))
 *   println(maps.has_key(ages, "bob"))
 *@end
 */
/* maps.has_key(m, key) — check if key exists */
bool gray_maps_has_key(GrayMap *m, const void *key);

/*@man is_empty
 *@module maps
 *@group Query
 *@sig is_empty(m map[K:V]) -> bool
 *@desc Returns true if m has no entries.
 *@example
 *   import @maps
 *   mut m map[string:int] = {:}
 *   println(maps.is_empty(m))
 *@end
 */
/* maps.is_empty(m) — true if map has no entries */
bool gray_maps_is_empty(GrayMap *m);

/*@man contains_value
 *@module maps
 *@group Query
 *@sig contains_value(m map[K:V], value V) -> bool
 *@desc Returns true if any entry in m has the given value.
 *@example
 *   import @maps
 *   mut scores map[string:int] = {"alice": 100, "bob": 85}
 *   println(maps.contains_value(scores, 100))
 *@end
 */
/* maps.contains_value(m, value) — check if any entry has the given value */
bool gray_maps_contains_value(GrayMap *m, const void *value);

/*@man is_equal
 *@module maps
 *@group Query
 *@sig is_equal(a map[K:V], b map[K:V]) -> bool
 *@desc Returns true if a and b have the same keys and values. K and V must be primitives or string. Use this instead of == which is not allowed on maps.
 *@example
 *   import @maps
 *   mut a map[string:int] = {"x": 1}
 *   mut b map[string:int] = {"x": 1}
 *   println(maps.is_equal(a, b))
 *@end
 */
/* maps.is_equal(a, b) — structural equality.
 * str_keys: true if keys are GrayString (look up via gray_map_get_str)
 * str_values: true if values are GrayString (compare contents, not blob)
 * Composite key/value types (nested arrays, maps, structs) are rejected
 * at typecheck and never reach this function. */
bool gray_maps_is_equal(GrayMap *a, GrayMap *b, bool str_keys, bool str_values);

/*@man merge
 *@module maps
 *@group Transformation
 *@sig merge(m1 map[K:V], m2 map[K:V]) -> map[K:V]
 *@desc Returns a new map containing all entries from m1 and m2. When both maps share a key, m2's value wins.
 *@example
 *   import @maps
 *   mut a map[string:int] = {"x": 1, "y": 2}
 *   mut b map[string:int] = {"y": 99, "z": 3}
 *   println(maps.merge(a, b))
 *@end
 */
/* maps.merge(m1, m2) — combine two maps (m2 overwrites m1 on conflict) */
GrayMap gray_maps_merge(GrayArena *arena, GrayMap *m1, GrayMap *m2);

/*@man get_or_default
 *@module maps
 *@group Query
 *@sig get_or_default(m map[K:V], key K, default V) -> V
 *@desc Returns the value for key if it exists, otherwise returns default.
 *@example
 *   import @maps
 *   mut scores map[string:int] = {"alice": 42}
 *   println(maps.get_or_default(scores, "alice", 0))
 *   println(maps.get_or_default(scores, "bob", 0))
 *@end
 */
/* maps.get_or_default(m, key, default) — return value or fallback */
/* NOTE: implemented inline in codegen due to type-generic return */

/*@man remove_key
 *@module maps
 *@group Modification
 *@sig remove_key(&m map[K:V], key K)
 *@desc Removes the entry with the given key from m. Does nothing if the key is not present. Modifies the map in place.
 *@example
 *   import @maps
 *   mut ages map[string:int] = {"alice": 30, "bob": 25}
 *   maps.remove_key(ages, "bob")
 *   println(ages)
 *@end
 */
/* remove_key handled inline by codegen */

/*@man clear
 *@module maps
 *@group Modification
 *@sig clear(&m map[K:V])
 *@desc Removes all entries from m, leaving it empty. Modifies the map in place.
 *@example
 *   import @maps
 *   mut ages map[string:int] = {"alice": 30, "bob": 25}
 *   maps.clear(ages)
 *   println(ages)
 *@end
 */
/* clear handled inline by codegen */

#endif
