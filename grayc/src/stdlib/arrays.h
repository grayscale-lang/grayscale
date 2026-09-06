/*
 * arrays.h — Public interface for the arrays stdlib module.
 * Declares array manipulation functions such as append, insert,
 * remove, sort, reverse, and search.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_ARRAYS_H
#define GRAY_ARRAYS_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

/* Modification */

/*@man append
 *@module arrays
 *@group Modification
 *@sig append(&arr [T], value T)
 *@desc Appends value to the end of arr. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   arrays.append(nums, 4)
 *   println(nums)
 *@end
 */
void gray_arrays_append(GrayArena *arena, GrayArray *arr, const void *value);

/*@man insert_at
 *@module arrays
 *@group Modification
 *@sig insert_at(&arr [T], index int, value T)
 *@desc Inserts value at the given index, shifting subsequent elements right. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   arrays.insert_at(nums, 1, 99)
 *   println(nums)
 *@end
 */
void gray_arrays_insert_at(GrayArena *arena, GrayArray *arr, int32_t index, const void *value);

/*@man prepend
 *@module arrays
 *@group Modification
 *@sig prepend(&arr [T], value T)
 *@desc Inserts value at the front of arr, shifting all elements right. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {2, 3, 4}
 *   arrays.prepend(nums, 1)
 *   println(nums)
 *@end
 */
void gray_arrays_prepend(GrayArena *arena, GrayArray *arr, const void *value);

/*@man remove_at
 *@module arrays
 *@group Modification
 *@sig remove_at(&arr [T], index int)
 *@desc Removes the element at the given index, shifting subsequent elements left. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   arrays.remove_at(nums, 1)
 *   println(nums)
 *@end
 */
void gray_arrays_remove_at(GrayArray *arr, int32_t index);

/*@man remove
 *@module arrays
 *@group Modification
 *@sig remove(&arr [T], value T)
 *@desc Removes the first occurrence of value from arr. Modifies the array in place. Does nothing if value is not found.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3, 2}
 *   arrays.remove(nums, 2)
 *   println(nums)
 *@end
 */
void gray_arrays_remove_int(GrayArray *arr, int64_t value);
void gray_arrays_remove_float(GrayArray *arr, double value);
void gray_arrays_remove_str(GrayArray *arr, GrayString value);

/*@man clear
 *@module arrays
 *@group Modification
 *@sig clear(&arr [T])
 *@desc Removes all elements from arr, leaving it empty. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   arrays.clear(nums)
 *   println(nums)
 *@end
 */
void gray_arrays_clear(GrayArray *arr);

/*@man fill
 *@module arrays
 *@group Modification
 *@sig fill(&arr [T], value T, count int)
 *@desc Appends count copies of value to arr. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {}
 *   arrays.fill(nums, 0, 5)
 *   println(nums)
 *@end
 */
void gray_arrays_fill(GrayArena *arena, GrayArray *arr, const void *value, int32_t count);

/* Access — typed via codegen cast */

/*@man get_first
 *@module arrays
 *@group Access
 *@sig get_first(arr [T]) -> T
 *@desc Returns the first element of arr. Panics if arr is empty.
 *@example
 *   import @arrays
 *   mut nums [int] = {10, 20, 30}
 *   println(arrays.get_first(nums))
 *@end
 */
void *gray_arrays_first_ptr(GrayArray *arr);

/*@man get_last
 *@module arrays
 *@group Access
 *@sig get_last(arr [T]) -> T
 *@desc Returns the last element of arr. Panics if arr is empty.
 *@example
 *   import @arrays
 *   mut nums [int] = {10, 20, 30}
 *   println(arrays.get_last(nums))
 *@end
 */
void *gray_arrays_last_ptr(GrayArray *arr);

/*@man remove_first
 *@module arrays
 *@group Modification
 *@sig remove_first(&arr [T]) -> T
 *@desc Removes and returns the first element of arr. Panics if arr is empty. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {10, 20, 30}
 *   mut val int = arrays.remove_first(nums)
 *   println(val)
 *@end
 */
void  gray_arrays_remove_first_raw(GrayArray *arr, void *out);

/*@man remove_last
 *@module arrays
 *@group Modification
 *@sig remove_last(&arr [T]) -> T
 *@desc Removes and returns the last element of arr. Panics if arr is empty. Modifies the array in place.
 *@example
 *   import @arrays
 *   mut nums [int] = {10, 20, 30}
 *   mut val int = arrays.remove_last(nums)
 *   println(val)
 *@end
 */
void  gray_arrays_remove_last_raw(GrayArray *arr, void *out);

/* Access — int64_t variants for internal use */
int64_t gray_arrays_get_first(GrayArray *arr);
int64_t gray_arrays_get_last(GrayArray *arr);
int64_t gray_arrays_remove_last(GrayArray *arr);
int64_t gray_arrays_remove_first(GrayArray *arr);

/* Query */

/*@man is_empty
 *@module arrays
 *@group Query
 *@sig is_empty(arr [T]) -> bool
 *@desc Returns true if arr has no elements.
 *@example
 *   import @arrays
 *   mut nums [int] = {}
 *   println(arrays.is_empty(nums))
 *@end
 */
bool gray_arrays_is_empty(GrayArray *arr);

/*@man contains
 *@module arrays
 *@group Query
 *@sig contains(arr [T], value T) -> bool
 *@desc Returns true if arr contains value.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   println(arrays.contains(nums, 2))
 *@end
 */
bool gray_arrays_contains_int(GrayArray *arr, int64_t value);
bool gray_arrays_contains_char(GrayArray *arr, int32_t value);
bool gray_arrays_contains_byte(GrayArray *arr, uint8_t value);
bool gray_arrays_contains_float(GrayArray *arr, double value);
bool gray_arrays_contains_str(GrayArray *arr, GrayString value);

/*@man index_of
 *@module arrays
 *@group Query
 *@sig index_of(arr [T], value T) -> int
 *@desc Returns the index of the first occurrence of value in arr, or -1 if not found.
 *@example
 *   import @arrays
 *   mut nums [int] = {10, 20, 30}
 *   println(arrays.index_of(nums, 20))
 *@end
 */
int64_t gray_arrays_index_of_int(GrayArray *arr, int64_t value);
int64_t gray_arrays_index_of_str(GrayArray *arr, GrayString value);

/*@man count
 *@module arrays
 *@group Query
 *@sig count(arr [T], value T) -> int
 *@desc Returns the number of times value appears in arr.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 2, 3}
 *   println(arrays.count(nums, 2))
 *@end
 */
int64_t gray_arrays_count(GrayArray *arr, int64_t value);

/*@man is_equal
 *@module arrays
 *@group Query
 *@sig is_equal(a [T], b [T]) -> bool
 *@desc Returns true if a and b have the same length and identical elements. T must be a primitive or string. Use this instead of == which is not allowed on arrays.
 *@example
 *   import @arrays
 *   mut a [int] = {1, 2, 3}
 *   mut b [int] = {1, 2, 3}
 *   println(arrays.is_equal(a, b))
 *@end
 */
bool gray_arrays_is_equal_prim(GrayArray *a, GrayArray *b);
bool gray_arrays_is_equal_str(GrayArray *a, GrayArray *b);

/*@man is_sorted
 *@module arrays
 *@group Query
 *@sig is_sorted(arr [T]) -> bool
 *@desc Returns true if the elements of arr are in ascending order (each element is <= the next). Empty and single-element arrays are sorted. T must be comparable, as for sort_asc.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 2, 5}
 *   println(arrays.is_sorted(nums))
 *@end
 */
bool gray_arrays_is_sorted(GrayArray *arr);
bool gray_arrays_is_sorted_float(GrayArray *arr);
bool gray_arrays_is_sorted_str(GrayArray *arr);

/* Transformation */

/*@man reverse
 *@module arrays
 *@group Transformation
 *@sig reverse(arr [T]) -> [T]
 *@desc Returns a new array with elements in reverse order. Does not modify the original.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   println(arrays.reverse(nums))
 *@end
 */
GrayArray gray_arrays_reverse(GrayArena *arena, GrayArray *arr);

/*@man slice
 *@module arrays
 *@group Transformation
 *@sig slice(arr [T], start int, end int) -> [T]
 *@desc Returns a new array containing elements from index start (inclusive) to end (exclusive). Does not modify the original.
 *@example
 *   import @arrays
 *   mut nums [int] = {10, 20, 30, 40}
 *   println(arrays.slice(nums, 1, 3))
 *@end
 */
GrayArray gray_arrays_slice(GrayArena *arena, GrayArray *arr, int32_t start, int32_t end);

/*@man concat
 *@module arrays
 *@group Transformation
 *@sig concat(a [T], b [T]) -> [T]
 *@desc Returns a new array containing all elements of a followed by all elements of b.
 *@example
 *   import @arrays
 *   mut a [int] = {1, 2}
 *   mut b [int] = {3, 4}
 *   println(arrays.concat(a, b))
 *@end
 */
GrayArray gray_arrays_concat(GrayArena *arena, GrayArray *a, GrayArray *b);

/*@man deduplicate
 *@module arrays
 *@group Transformation
 *@sig deduplicate(arr [T]) -> [T]
 *@desc Returns a new array with duplicate values removed, preserving the order of first occurrence.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 2, 3, 1}
 *   println(arrays.deduplicate(nums))
 *@end
 */
GrayArray gray_arrays_deduplicate(GrayArena *arena, GrayArray *arr);

/*@man flatten
 *@module arrays
 *@group Transformation
 *@sig flatten(arr [[T]]) -> [T]
 *@desc Flattens one level of nesting, returning a single array of all inner elements.
 *@example
 *   import @arrays
 *   mut nested [[int]] = {{1, 2}, {3, 4}}
 *   println(arrays.flatten(nested))
 *@end
 */
GrayArray gray_arrays_flatten(GrayArena *arena, GrayArray *arr);

/*@man split_every
 *@module arrays
 *@group Transformation
 *@sig split_every(arr [T], size int) -> [[T]]
 *@desc Splits arr into sub-arrays of at most size elements each. The last chunk may be smaller.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3, 4, 5}
 *   println(arrays.split_every(nums, 2))
 *@end
 */
GrayArray gray_arrays_split_every(GrayArena *arena, GrayArray *arr, int32_t size);

/*@man pair
 *@module arrays
 *@group Transformation
 *@sig pair(a [T], b [T]) -> [[T]]
 *@desc Returns an array of two-element arrays, pairing each element of a with the corresponding element of b. Length is determined by the shorter array.
 *@example
 *   import @arrays
 *   mut keys [int] = {1, 2, 3}
 *   mut vals [int] = {10, 20, 30}
 *   println(arrays.pair(keys, vals))
 *@end
 */
GrayArray gray_arrays_pair(GrayArena *arena, GrayArray *a, GrayArray *b);

/* Computation */

/*@man get_sum
 *@module arrays
 *@group Computation
 *@sig get_sum(arr [T]) -> T
 *@desc Returns the sum of all elements. Accepts int, float, or sized numeric types.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3, 4}
 *   println(arrays.get_sum(nums))
 *@end
 */
int64_t gray_arrays_get_sum(GrayArray *arr);

/*@man get_min
 *@module arrays
 *@group Computation
 *@sig get_min(arr [T]) -> T
 *@desc Returns the smallest element in arr.
 *@example
 *   import @arrays
 *   mut nums [int] = {3, 1, 4, 1, 5}
 *   println(arrays.get_min(nums))
 *@end
 */
int64_t gray_arrays_get_min(GrayArray *arr);

/*@man get_max
 *@module arrays
 *@group Computation
 *@sig get_max(arr [T]) -> T
 *@desc Returns the largest element in arr.
 *@example
 *   import @arrays
 *   mut nums [int] = {3, 1, 4, 1, 5}
 *   println(arrays.get_max(nums))
 *@end
 */
int64_t gray_arrays_get_max(GrayArray *arr);

/* Sort */

/*@man sort_asc
 *@module arrays
 *@group Modification
 *@sig sort_asc(&arr [T])
 *@desc Sorts arr in ascending order in place. Works on int, float, and string arrays.
 *@example
 *   import @arrays
 *   mut nums [int] = {3, 1, 4, 1, 5}
 *   arrays.sort_asc(nums)
 *   println(nums)
 *@end
 */
void gray_arrays_sort_asc(GrayArray *arr);
void gray_arrays_sort_asc_float(GrayArray *arr);
void gray_arrays_sort_asc_str(GrayArray *arr);

/*@man sort_desc
 *@module arrays
 *@group Modification
 *@sig sort_desc(&arr [T])
 *@desc Sorts arr in descending order in place. Works on int, float, and string arrays.
 *@example
 *   import @arrays
 *   mut nums [int] = {3, 1, 4, 1, 5}
 *   arrays.sort_desc(nums)
 *   println(nums)
 *@end
 */
void gray_arrays_sort_desc(GrayArray *arr);
void gray_arrays_sort_desc_float(GrayArray *arr);
void gray_arrays_sort_desc_str(GrayArray *arr);

/*@man swap
 *@module arrays
 *@group Modification
 *@sig swap(&arr [T], i int, j int)
 *@desc Swaps the elements at indices i and j in place. Panics if either index is out of bounds.
 *@example
 *   import @arrays
 *   mut nums [int] = {1, 2, 3}
 *   arrays.swap(nums, 0, 2)
 *   println(nums)
 *@end
 */
void gray_arrays_swap(GrayArray *arr, int64_t i, int64_t j);

/* Higher-Order */

/*@man average
 *@module arrays
 *@group Computation
 *@sig average(arr [T]) -> float
 *@desc Returns the arithmetic mean of arr as a float. T must be numeric. Panics on an empty array.
 *@example
 *   import @arrays
 *   mut nums [int] = {2, 4, 6}
 *   println(arrays.average(nums))
 *@end
 */

/*@man find
 *@module arrays
 *@group Higher-Order
 *@sig find(arr [T], predicate func(T) -> bool) -> (T, bool)
 *@desc Returns the first element for which predicate returns true and the boolean true, or the zero value and false when no element matches. The result must be destructured.
 *@example
 *   import @arrays
 *   do is_even(x int) -> bool { return x % 2 == 0 }
 *   mut nums [int] = {1, 3, 4, 7}
 *   mut hit int, ok bool = arrays.find(nums, ()is_even)
 *   println("${hit} ${ok}")
 *@end
 */

/*@man find_index
 *@module arrays
 *@group Higher-Order
 *@sig find_index(arr [T], predicate func(T) -> bool) -> int
 *@desc Returns the index of the first element for which predicate returns true, or -1 when no element matches.
 *@example
 *   import @arrays
 *   do is_even(x int) -> bool { return x % 2 == 0 }
 *   mut nums [int] = {1, 3, 4, 7}
 *   println(arrays.find_index(nums, ()is_even))
 *@end
 */

/*@man map
 *@module arrays
 *@group Higher-Order
 *@sig map(arr [T], transform func(T) -> T) -> [T]
 *@desc Returns a new array with transform applied to each element. transform must be a function that takes T and returns T. Does not modify the original.
 *@example
 *   import @arrays
 *   do double_it(x int) -> int { return x * 2 }
 *   mut nums [int] = {1, 2, 3}
 *   mut result [int] = arrays.map(nums, ()double_it)
 *   println(result)
 *@end
 */

/*@man filter
 *@module arrays
 *@group Higher-Order
 *@sig filter(arr [T], predicate func(T) -> bool) -> [T]
 *@desc Returns a new array containing only elements for which predicate returns true. predicate must be a function that takes T and returns bool. Does not modify the original.
 *@example
 *   import @arrays
 *   do is_even(x int) -> bool { return x % 2 == 0 }
 *   mut nums [int] = {1, 2, 3, 4}
 *   mut result [int] = arrays.filter(nums, ()is_even)
 *   println(result)
 *@end
 */

/*@man reduce
 *@module arrays
 *@group Higher-Order
 *@sig reduce(arr [T], initial T, accumulator func(T, T) -> T) -> T
 *@desc Reduces arr to a single value by applying accumulator(acc, element) for each element, starting with initial. accumulator must take two T parameters and return T. Does not modify the original.
 *@example
 *   import @arrays
 *   do add(a int, b int) -> int { return a + b }
 *   mut nums [int] = {1, 2, 3, 4}
 *   mut total int = arrays.reduce(nums, 0, ()add)
 *   println(total)
 *@end
 */

/*@man any
 *@module arrays
 *@group Higher-Order
 *@sig any(arr [T], predicate func(T) -> bool) -> bool
 *@desc Returns true if predicate returns true for at least one element. predicate must be a function that takes T and returns bool.
 *@example
 *   import @arrays
 *   do is_negative(x int) -> bool { return x < 0 }
 *   mut nums [int] = {1, -2, 3}
 *   println(arrays.any(nums, ()is_negative))
 *@end
 */

/*@man all
 *@module arrays
 *@group Higher-Order
 *@sig all(arr [T], predicate func(T) -> bool) -> bool
 *@desc Returns true if predicate returns true for every element. predicate must be a function that takes T and returns bool.
 *@example
 *   import @arrays
 *   do is_positive(x int) -> bool { return x > 0 }
 *   mut nums [int] = {1, 2, 3}
 *   println(arrays.all(nums, ()is_positive))
 *@end
 */

#endif
