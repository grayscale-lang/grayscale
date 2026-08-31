/*
 * math.h — Public interface for the math stdlib module.
 * Thin inline wrappers around <math.h> for trigonometry, rounding,
 * powers, and logarithms. Constants (PI, E, etc.) are emitted
 * directly by codegen.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_MATH_H
#define GRAY_MATH_H

#include "../runtime/runtime.h"
/* This header is named math.h and generated programs see its directory via
 * -isystem, so a plain #include <math.h> resolves back to this file and the
 * libc math functions are never declared. include_next (GNU C, required by
 * this project) continues the search past this directory to the real one. */
#include_next <math.h>

/*@man abs
 *@module math
 *@group Arithmetic
 *@sig abs(n T) -> T
 *@desc Returns the absolute value of n. Works on int and float. Returns the same type as the input.
 *@example
 *   import @math
 *   println(math.abs(-5))
 *   println(math.abs(-3.14))
 *@end
 */
static inline int64_t gray_math_abs_int(int64_t n) { return n < 0 ? -n : n; }
static inline double  gray_math_abs_float(double n) { return fabs(n); }

/*@man neg
 *@module math
 *@group Arithmetic
 *@sig neg(n T) -> T
 *@desc Returns the negation of n. Works on int and float. Returns the same type as the input.
 *@example
 *   import @math
 *   println(math.neg(5))
 *   println(math.neg(-3.14))
 *@end
 */
/* neg is handled entirely by codegen — no C function needed */

/*@man sign
 *@module math
 *@group Arithmetic
 *@sig sign(n int) -> int
 *@desc Returns -1 if n is negative, 0 if zero, or 1 if positive.
 *@example
 *   import @math
 *   println(math.sign(-7))
 *   println(math.sign(0))
 *   println(math.sign(3))
 *@end
 */
static inline int64_t gray_math_sign(int64_t n) { return n > 0 ? 1 : (n < 0 ? -1 : 0); }

/*@man min
 *@module math
 *@group Min/Max/Clamp
 *@sig min(a T, b T) -> T
 *@desc Returns the smaller of two values. Works on int and float.
 *@example
 *   import @math
 *   println(math.min(3, 7))
 *   println(math.min(1.5, 2.5))
 *@end
 */
static inline int64_t gray_math_min_int(int64_t a, int64_t b) { return a < b ? a : b; }
static inline double  gray_math_min_float(double a, double b) { return a < b ? a : b; }

/*@man max
 *@module math
 *@group Min/Max/Clamp
 *@sig max(a T, b T) -> T
 *@desc Returns the larger of two values. Works on int and float.
 *@example
 *   import @math
 *   println(math.max(3, 7))
 *   println(math.max(1.5, 2.5))
 *@end
 */
static inline int64_t gray_math_max_int(int64_t a, int64_t b) { return a > b ? a : b; }
static inline double  gray_math_max_float(double a, double b) { return a > b ? a : b; }

/*@man clamp
 *@module math
 *@group Min/Max/Clamp
 *@sig clamp(value T, min T, max T) -> T
 *@desc Clamps value to the range [min, max]. Works on int and float.
 *@example
 *   import @math
 *   println(math.clamp(15, 1, 10))
 *   println(math.clamp(1.5, 0.0, 1.0))
 *@end
 */
static inline int64_t gray_math_clamp_int(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline double  gray_math_clamp_float(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/*@man floor
 *@module math
 *@group Rounding
 *@sig floor(n float) -> float
 *@desc Rounds n down to the nearest integer value, returned as float.
 *@example
 *   import @math
 *   println(math.floor(3.7))
 *   println(math.floor(-1.2))
 *@end
 */
static inline double gray_math_floor(double n) { return floor(n); }

/*@man ceil
 *@module math
 *@group Rounding
 *@sig ceil(n float) -> float
 *@desc Rounds n up to the nearest integer value, returned as float.
 *@example
 *   import @math
 *   println(math.ceil(3.2))
 *   println(math.ceil(-1.8))
 *@end
 */
static inline double gray_math_ceil(double n) { return ceil(n); }

/*@man round
 *@module math
 *@group Rounding
 *@sig round(n float) -> float
 *@desc Rounds n to the nearest integer (half rounds away from zero), returned as float.
 *@example
 *   import @math
 *   println(math.round(3.5))
 *   println(math.round(3.4))
 *@end
 */
static inline double gray_math_round(double n) { return round(n); }

/*@man trunc
 *@module math
 *@group Rounding
 *@sig trunc(n float) -> float
 *@desc Truncates n toward zero, returned as float.
 *@example
 *   import @math
 *   println(math.trunc(3.9))
 *   println(math.trunc(-3.9))
 *@end
 */
static inline double gray_math_trunc(double n) { return trunc(n); }

/*@man pow
 *@module math
 *@group Powers/Roots
 *@sig pow(base float, exp float) -> float
 *@desc Returns base raised to the power exp.
 *@example
 *   import @math
 *   println(math.pow(2.0, 10.0))
 *@end
 */
static inline double gray_math_pow(double b, double e) { return pow(b, e); }

/*@man sqrt
 *@module math
 *@group Powers/Roots
 *@sig sqrt(n float) -> float
 *@desc Returns the square root of n. Panics at runtime if n is negative.
 *@example
 *   import @math
 *   println(math.sqrt(9.0))
 *   println(math.sqrt(2.0))
 *@end
 */
static inline double gray_math_sqrt(double n) {
    if (n < 0) gray_panic_code("P0064", "math.sqrt() requires a non-negative number, got %g", n);
    return sqrt(n);
}

/*@man cbrt
 *@module math
 *@group Powers/Roots
 *@sig cbrt(n float) -> float
 *@desc Returns the cube root of n.
 *@example
 *   import @math
 *   println(math.cbrt(27.0))
 *@end
 */
static inline double gray_math_cbrt(double n) { return cbrt(n); }

/*@man hypot
 *@module math
 *@group Powers/Roots
 *@sig hypot(x float, y float) -> float
 *@desc Returns the hypotenuse of a right triangle with legs x and y (sqrt(x^2 + y^2)).
 *@example
 *   import @math
 *   println(math.hypot(3.0, 4.0))
 *@end
 */
static inline double gray_math_hypot(double x, double y) { return hypot(x, y); }

/*@man exp
 *@module math
 *@group Powers/Roots
 *@sig exp(n float) -> float
 *@desc Returns e raised to the power n.
 *@example
 *   import @math
 *   println(math.exp(1.0))
 *@end
 */
static inline double gray_math_exp(double n) { return exp(n); }

/*@man exp2
 *@module math
 *@group Powers/Roots
 *@sig exp2(n float) -> float
 *@desc Returns 2 raised to the power n.
 *@example
 *   import @math
 *   println(math.exp2(8.0))
 *@end
 */
static inline double gray_math_exp2(double n) { return exp2(n); }

/*@man log
 *@module math
 *@group Logarithms
 *@sig log(n float) -> float
 *@desc Returns the natural (base-e) logarithm of n. Panics if n <= 0.
 *@example
 *   import @math
 *   println(math.log(math.E))
 *@end
 */
static inline double gray_math_log(double n) {
    if (n <= 0) gray_panic_code("P0065", "math.log() requires a positive number, got %g", n);
    return log(n);
}

/*@man log2
 *@module math
 *@group Logarithms
 *@sig log2(n float) -> float
 *@desc Returns the base-2 logarithm of n. Panics if n <= 0.
 *@example
 *   import @math
 *   println(math.log2(8.0))
 *@end
 */
static inline double gray_math_log2(double n) {
    if (n <= 0) gray_panic_code("P0066", "math.log2() requires a positive number, got %g", n);
    return log2(n);
}

/*@man log10
 *@module math
 *@group Logarithms
 *@sig log10(n float) -> float
 *@desc Returns the base-10 logarithm of n. Panics if n <= 0.
 *@example
 *   import @math
 *   println(math.log10(100.0))
 *@end
 */
static inline double gray_math_log10(double n) {
    if (n <= 0) gray_panic_code("P0067", "math.log10() requires a positive number, got %g", n);
    return log10(n);
}

/*@man log_base
 *@module math
 *@group Logarithms
 *@sig log_base(value float, base float) -> float
 *@desc Returns the logarithm of value in the given base.
 *@example
 *   import @math
 *   println(math.log_base(8.0, 2.0))
 *@end
 */
static inline double gray_math_log_base(double v, double b) { return log(v) / log(b); }

/*@man sin
 *@module math
 *@group Trigonometry
 *@sig sin(rad float) -> float
 *@desc Returns the sine of an angle in radians.
 *@example
 *   import @math
 *   println(math.sin(math.PI / 2.0))
 *@end
 */
static inline double gray_math_sin(double r) { return sin(r); }

/*@man cos
 *@module math
 *@group Trigonometry
 *@sig cos(rad float) -> float
 *@desc Returns the cosine of an angle in radians.
 *@example
 *   import @math
 *   println(math.cos(0.0))
 *@end
 */
static inline double gray_math_cos(double r) { return cos(r); }

/*@man tan
 *@module math
 *@group Trigonometry
 *@sig tan(rad float) -> float
 *@desc Returns the tangent of an angle in radians.
 *@example
 *   import @math
 *   println(math.tan(math.PI / 4.0))
 *@end
 */
static inline double gray_math_tan(double r) { return tan(r); }

/*@man asin
 *@module math
 *@group Trigonometry
 *@sig asin(n float) -> float
 *@desc Returns the arc sine in radians. Panics if n is outside [-1, 1].
 *@example
 *   import @math
 *   println(math.asin(1.0))
 *@end
 */
static inline double gray_math_asin(double n) {
    if (n < -1.0 || n > 1.0) gray_panic_code("P0068", "math.asin() requires value in [-1, 1], got %g", n);
    return asin(n);
}

/*@man acos
 *@module math
 *@group Trigonometry
 *@sig acos(n float) -> float
 *@desc Returns the arc cosine in radians. Panics if n is outside [-1, 1].
 *@example
 *   import @math
 *   println(math.acos(1.0))
 *@end
 */
static inline double gray_math_acos(double n) {
    if (n < -1.0 || n > 1.0) gray_panic_code("P0069", "math.acos() requires value in [-1, 1], got %g", n);
    return acos(n);
}

/*@man atan
 *@module math
 *@group Trigonometry
 *@sig atan(n float) -> float
 *@desc Returns the arc tangent in radians.
 *@example
 *   import @math
 *   println(math.atan(1.0))
 *@end
 */
static inline double gray_math_atan(double n) { return atan(n); }

/*@man atan2
 *@module math
 *@group Trigonometry
 *@sig atan2(y float, x float) -> float
 *@desc Returns the arc tangent of y/x in radians, using the signs of both to determine quadrant.
 *@example
 *   import @math
 *   println(math.atan2(1.0, 1.0))
 *@end
 */
static inline double gray_math_atan2(double y, double x) { return atan2(y, x); }

/*@man sinh
 *@module math
 *@group Trigonometry
 *@sig sinh(n float) -> float
 *@desc Returns the hyperbolic sine of n.
 *@example
 *   import @math
 *   println(math.sinh(1.0))
 *@end
 */
static inline double gray_math_sinh(double n) { return sinh(n); }

/*@man cosh
 *@module math
 *@group Trigonometry
 *@sig cosh(n float) -> float
 *@desc Returns the hyperbolic cosine of n.
 *@example
 *   import @math
 *   println(math.cosh(0.0))
 *@end
 */
static inline double gray_math_cosh(double n) { return cosh(n); }

/*@man tanh
 *@module math
 *@group Trigonometry
 *@sig tanh(n float) -> float
 *@desc Returns the hyperbolic tangent of n.
 *@example
 *   import @math
 *   println(math.tanh(1.0))
 *@end
 */
static inline double gray_math_tanh(double n) { return tanh(n); }

/*@man deg_to_rad
 *@module math
 *@group Trigonometry
 *@sig deg_to_rad(deg float) -> float
 *@desc Converts degrees to radians.
 *@example
 *   import @math
 *   println(math.deg_to_rad(180.0))
 *@end
 */
static inline double gray_math_deg_to_rad(double d) { return d * 3.14159265358979323846 / 180.0; }

/*@man rad_to_deg
 *@module math
 *@group Trigonometry
 *@sig rad_to_deg(rad float) -> float
 *@desc Converts radians to degrees.
 *@example
 *   import @math
 *   println(math.rad_to_deg(math.PI))
 *@end
 */
static inline double gray_math_rad_to_deg(double r) { return r * 180.0 / 3.14159265358979323846; }

/*@man is_even
 *@module math
 *@group Properties
 *@sig is_even(n int) -> bool
 *@desc Returns true if n is even.
 *@example
 *   import @math
 *   println(math.is_even(4))
 *@end
 */
static inline bool gray_math_is_even(int64_t n) { return n % 2 == 0; }

/*@man is_odd
 *@module math
 *@group Properties
 *@sig is_odd(n int) -> bool
 *@desc Returns true if n is odd.
 *@example
 *   import @math
 *   println(math.is_odd(3))
 *@end
 */
static inline bool gray_math_is_odd(int64_t n) { return n % 2 != 0; }

/*@man is_infinite
 *@module math
 *@group Properties
 *@sig is_infinite(n float) -> bool
 *@desc Returns true if n is positive or negative infinity.
 *@example
 *   import @math
 *   println(math.is_infinite(math.INF))
 *@end
 */
static inline bool gray_math_is_infinite(double n) { return isinf(n); }

/*@man is_nan
 *@module math
 *@group Properties
 *@sig is_nan(n float) -> bool
 *@desc Returns true if n is NaN (Not a Number).
 *@example
 *   import @math
 *   mut nan_val float = math.INF - math.INF
 *   println(math.is_nan(nan_val))
 *@end
 */
static inline bool gray_math_is_nan(double n) { return isnan(n); }

/*@man is_finite
 *@module math
 *@group Properties
 *@sig is_finite(n float) -> bool
 *@desc Returns true if n is a finite number (not infinite or NaN).
 *@example
 *   import @math
 *   println(math.is_finite(3.14))
 *@end
 */
static inline bool gray_math_is_finite(double n) { return !isinf(n) && !isnan(n); }

/*@man factorial
 *@module math
 *@group Statistical
 *@sig factorial(n int) -> int
 *@desc Returns n! (n factorial). n must be non-negative. Panics on negative input.
 *@example
 *   import @math
 *   println(math.factorial(5))
 *@end
 */
int64_t gray_math_factorial(int64_t n);

/*@man gcd
 *@module math
 *@group Statistical
 *@sig gcd(a int, b int) -> int
 *@desc Returns the greatest common divisor of a and b.
 *@example
 *   import @math
 *   println(math.gcd(12, 8))
 *@end
 */
int64_t gray_math_gcd(int64_t a, int64_t b);

/*@man lcm
 *@module math
 *@group Statistical
 *@sig lcm(a int, b int) -> int
 *@desc Returns the least common multiple of a and b.
 *@example
 *   import @math
 *   println(math.lcm(4, 6))
 *@end
 */
int64_t gray_math_lcm(int64_t a, int64_t b);

/*@man is_prime
 *@module math
 *@group Statistical
 *@sig is_prime(n int) -> bool
 *@desc Returns true if n is a prime number.
 *@example
 *   import @math
 *   println(math.is_prime(7))
 *   println(math.is_prime(9))
 *@end
 */
bool gray_math_is_prime(int64_t n);

/*@man lerp
 *@module math
 *@group Utility
 *@sig lerp(a float, b float, t float) -> float
 *@desc Linearly interpolates between a and b by factor t. t=0 returns a, t=1 returns b.
 *@example
 *   import @math
 *   println(math.lerp(0.0, 10.0, 0.5))
 *@end
 */
static inline double gray_math_lerp(double a, double b, double t) { return a + (b - a) * t; }

/*@man distance
 *@module math
 *@group Utility
 *@sig distance(x1 float, y1 float, x2 float, y2 float) -> float
 *@desc Returns the Euclidean distance between two 2D points (x1, y1) and (x2, y2).
 *@example
 *   import @math
 *   println(math.distance(0.0, 0.0, 3.0, 4.0))
 *@end
 */
static inline double gray_math_distance(double x1, double y1, double x2, double y2) {
    return hypot(x2 - x1, y2 - y1);
}

/*@man mod
 *@module math
 *@group Arithmetic
 *@sig mod(x float, y float) -> float
 *@desc Returns the floating-point remainder of x divided by y, carrying the sign of x.
 *@example
 *   import @math
 *   println(math.mod(7.5, 2.0))
 *@end
 */
static inline double gray_math_mod(double x, double y) {
    return fmod(x, y);
}

/*@man copysign
 *@module math
 *@group Arithmetic
 *@sig copysign(x float, y float) -> float
 *@desc Returns the magnitude of x with the sign of y.
 *@example
 *   import @math
 *   println(math.copysign(3.0, -1.0))
 *@end
 */
static inline double gray_math_copysign(double x, double y) {
    return copysign(x, y);
}

/*@man fma
 *@module math
 *@group Arithmetic
 *@sig fma(x float, y float, z float) -> float
 *@desc Fused multiply-add: computes x * y + z with a single rounding step, which is more accurate than writing the two operations separately.
 *@example
 *   import @math
 *   println(math.fma(2.0, 3.0, 1.0))
 *@end
 */
static inline double gray_math_fma(double x, double y, double z) {
    return fma(x, y, z);
}

/* Integer and fractional halves of a float, in destructuring order. */
typedef struct { double v0; double v1; } GrayMathModf;

/*@man modf
 *@module math
 *@group Arithmetic
 *@sig modf(x float) -> (float, float)
 *@desc Splits x into its integer and fractional parts, both carrying the sign of x.
 *@example
 *   import @math
 *   mut whole, frac = math.modf(3.75)
 *   println(whole)
 *   println(frac)
 *@end
 */
GrayMathModf gray_math_modf(double x);

/*@man is_power_of_two
 *@module math
 *@group Integer
 *@sig is_power_of_two(n int) -> bool
 *@desc Returns true if n is a positive power of two. Zero and negative values are not.
 *@example
 *   import @math
 *   println(math.is_power_of_two(64))
 *@end
 */
bool gray_math_is_power_of_two(int64_t n);

/*@man next_power_of_two
 *@module math
 *@group Integer
 *@sig next_power_of_two(n int) -> int
 *@desc Returns the smallest power of two greater than or equal to n, or 1 when n is zero or negative. Panics if the result would exceed MAX_INT.
 *@example
 *   import @math
 *   println(math.next_power_of_two(100))
 *@end
 */
int64_t gray_math_next_power_of_two(int64_t n);

/*@man PI
 *@module math
 *@group Constants
 *@kind const
 *@sig 3.14159265358979323846
 *@desc Pi — the ratio of a circle's circumference to its diameter.
 *@end
 */

/*@man E
 *@module math
 *@group Constants
 *@kind const
 *@sig 2.71828182845904523536
 *@desc Euler's number — the base of the natural logarithm.
 *@end
 */

/*@man PHI
 *@module math
 *@group Constants
 *@kind const
 *@sig 1.61803398874989484820
 *@desc The golden ratio.
 *@end
 */

/*@man SQRT2
 *@module math
 *@group Constants
 *@kind const
 *@sig 1.41421356237309504880
 *@desc The square root of 2.
 *@end
 */

/*@man LN2
 *@module math
 *@group Constants
 *@kind const
 *@sig 0.69314718055994530942
 *@desc The natural logarithm of 2.
 *@end
 */

/*@man LN10
 *@module math
 *@group Constants
 *@kind const
 *@sig 2.30258509299404568402
 *@desc The natural logarithm of 10.
 *@end
 */

/*@man TAU
 *@module math
 *@group Constants
 *@kind const
 *@sig 6.28318530717958647692
 *@desc Tau — equal to 2 * PI.
 *@end
 */

/*@man INF
 *@module math
 *@group Constants
 *@kind const
 *@sig +Inf
 *@desc Positive infinity.
 *@end
 */

/*@man NEG_INF
 *@module math
 *@group Constants
 *@kind const
 *@sig -Inf
 *@desc Negative infinity.
 *@end
 */

/*@man MAX_INT
 *@module math
 *@group Constants
 *@kind const
 *@sig 9223372036854775807
 *@desc The largest value an int can hold.
 *@end
 */

/*@man MIN_INT
 *@module math
 *@group Constants
 *@kind const
 *@sig -9223372036854775808
 *@desc The smallest value an int can hold.
 *@end
 */

/*@man MAX_FLOAT
 *@module math
 *@group Constants
 *@kind const
 *@sig 1.7976931348623157e308
 *@desc The largest finite value a float can hold.
 *@end
 */

/*@man MIN_FLOAT
 *@module math
 *@group Constants
 *@kind const
 *@sig -1.7976931348623157e308
 *@desc The smallest (most negative) finite value a float can hold. For the smallest positive magnitude a float can represent, see EPSILON.
 *@end
 */

#endif
