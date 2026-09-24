/*
 * random.h — Public interface for the random stdlib module.
 * Declares pseudo-random number generation, array shuffling, random
 * element selection, and manual seeding functions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_RANDOM_H
#define GRAY_RANDOM_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

/*@man rand_f64
 *@module random
 *@group Generation
 *@sig rand_f64() -> f64  |  rand_f64(min f64, max f64) -> f64
 *@desc Returns a random f64. With no arguments returns a value in [0.0, 1.0). With two arguments returns a value in [min, max).
 *@example
 *   import @random
 *   mut f f64 = random.rand_f64()
 *   mut g f64 = random.rand_f64(1.0, 10.0)
 *@end
 */

/*@man rand_i64
 *@module random
 *@group Generation
 *@sig rand_i64(max i64) -> i64  |  rand_i64(min i64, max i64) -> i64
 *@desc Returns a random integer. With one argument returns a value in [0, max). With two arguments returns a value in [min, max).
 *@example
 *   import @random
 *   mut n i64 = random.rand_i64(100)
 *   mut m i64 = random.rand_i64(10, 50)
 *@end
 */

/*@man rand_bool
 *@module random
 *@group Generation
 *@sig rand_bool() -> bool
 *@desc Returns a random boolean value.
 *@example
 *   import @random
 *   mut b bool = random.rand_bool()
 *@end
 */

/*@man rand_u8
 *@module random
 *@group Generation
 *@sig rand_u8() -> u8
 *@desc Returns a random u8 value in [0, 255].
 *@example
 *   import @random
 *   mut b u8 = random.rand_u8()
 *@end
 */

/*@man rand_char
 *@module random
 *@group Generation
 *@sig rand_char() -> char  |  rand_char(min char, max char) -> char
 *@desc Returns a random character. With no arguments returns a random printable ASCII character. With two arguments returns a character in the range [min, max].
 *@example
 *   import @random
 *   mut c char = random.rand_char()
 *   mut d char = random.rand_char('a', 'z')
 *@end
 */

/*@man rand_string
 *@module random
 *@group Generation
 *@sig rand_string(length i64, alphabet string) -> string
 *@desc Returns a string of length characters, each drawn uniformly at random from alphabet using the module's non-cryptographic RNG. A length of 0 returns "". Panics if alphabet is empty and length is greater than 0.
 *@example
 *   import @random
 *   mut token string = random.rand_string(8, "abcdefghijklmnopqrstuvwxyz0123456789")
 *   println(token)
 *@end
 */
GrayString gray_random_string(GrayArena *arena, int64_t length, GrayString alphabet);

/*@man choice
 *@module random
 *@group Arrays
 *@sig choice(arr [T]) -> T
 *@desc Returns a random element from the array.
 *@example
 *   import @random
 *   mut colors [string] = {"red", "green", "blue"}
 *   mut picked string = random.choice(colors)
 *   println(picked)
 *@end
 */

/*@man shuffle
 *@module random
 *@group Arrays
 *@sig shuffle(arr [T]) -> [T]
 *@desc Returns a shuffled copy of the array. The original is not modified.
 *@example
 *   import @random
 *   mut nums [i64] = {1, 2, 3, 4, 5}
 *   mut shuffled [i64] = random.shuffle(nums)
 *@end
 */

/*@man sample
 *@module random
 *@group Arrays
 *@sig sample(arr [T], n i64) -> [T]
 *@desc Returns n unique randomly selected elements from the array.
 *@example
 *   import @random
 *   mut pool [i64] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
 *   mut picked [i64] = random.sample(pool, 3)
 *@end
 */

/*@man seed
 *@module random
 *@group Seeding
 *@sig seed(value i64)
 *@desc Seeds the random number generator with the given value. Useful for reproducible sequences.
 *@example
 *   import @random
 *   random.seed(42)
 *   mut n i64 = random.rand_i64(100)
 *@end
 */

double gray_random_f64_range(double minimum, double maximum);
double gray_random_f64_unit(void);
int64_t gray_random_i64_range(int64_t minimum, int64_t maximum);
int64_t gray_random_i64_max(int64_t maximum);
bool gray_random_bool(void);
uint8_t gray_random_u8(void);
int32_t gray_random_char(void);
int32_t gray_random_char_range(int32_t minimum, int32_t maximum);

/* Array operations */
GrayArray gray_random_shuffle(GrayArena *arena, GrayArray *array);
GrayArray gray_random_sample(GrayArena *arena, GrayArray *array, int64_t count);

/* Explicit seeding */
void gray_random_seed(int64_t value);

#endif
