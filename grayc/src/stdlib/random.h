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

/*@man rand_float
 *@module random
 *@group Generation
 *@sig rand_float() -> float  |  rand_float(min float, max float) -> float
 *@desc Returns a random float. With no arguments returns a value in [0.0, 1.0). With two arguments returns a value in [min, max).
 *@example
 *   import @random
 *   mut f float = random.rand_float()
 *   mut g float = random.rand_float(1.0, 10.0)
 *@end
 */

/*@man rand_int
 *@module random
 *@group Generation
 *@sig rand_int(max int) -> int  |  rand_int(min int, max int) -> int
 *@desc Returns a random integer. With one argument returns a value in [0, max). With two arguments returns a value in [min, max).
 *@example
 *   import @random
 *   mut n int = random.rand_int(100)
 *   mut m int = random.rand_int(10, 50)
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

/*@man rand_byte
 *@module random
 *@group Generation
 *@sig rand_byte() -> byte
 *@desc Returns a random byte value in [0, 255].
 *@example
 *   import @random
 *   mut b byte = random.rand_byte()
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
 *@sig rand_string(length int, alphabet string) -> string
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
 *   mut nums [int] = {1, 2, 3, 4, 5}
 *   mut shuffled [int] = random.shuffle(nums)
 *@end
 */

/*@man sample
 *@module random
 *@group Arrays
 *@sig sample(arr [T], n int) -> [T]
 *@desc Returns n unique randomly selected elements from the array.
 *@example
 *   import @random
 *   mut pool [int] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
 *   mut picked [int] = random.sample(pool, 3)
 *@end
 */

/*@man seed
 *@module random
 *@group Seeding
 *@sig seed(value int)
 *@desc Seeds the random number generator with the given value. Useful for reproducible sequences.
 *@example
 *   import @random
 *   random.seed(42)
 *   mut n int = random.rand_int(100)
 *@end
 */

double gray_random_float_range(double min, double max);
double gray_random_float_unit(void);
int64_t gray_random_int_range(int64_t min, int64_t max);
int64_t gray_random_int_max(int64_t max);
bool gray_random_bool(void);
uint8_t gray_random_byte(void);
int32_t gray_random_char(void);
int32_t gray_random_char_range(int32_t min, int32_t max);

/* Array operations */
GrayArray gray_random_shuffle(GrayArena *arena, GrayArray *arr);
GrayArray gray_random_sample(GrayArena *arena, GrayArray *arr, int32_t n);

/* Explicit seeding */
void gray_random_seed(int64_t value);

#endif
