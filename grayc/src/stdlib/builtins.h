/*
 * builtins.h — Public interface for built-in functions that
 * require no import. Declares println, print, input, len, typeof,
 * size_of, assert, exit, sleep, panic, and string conversion helpers.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_BUILTINS_H
#define GRAY_BUILTINS_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include "../runtime/map.h"

/*@man println
 *@sig println(value T)
 *@desc Prints any value to stdout followed by a newline. The argument is optional; called with no argument it prints a blank line.
 *@example
 *   println("hello, world")
 *   println(42)
 *   println(my_struct)
 *   println()
 *@end
 */
void gray_builtin_println_str(GrayString s);
void gray_builtin_println_int(int64_t v);
void gray_builtin_println_uint(uint64_t v);
void gray_builtin_println_float(double v);
void gray_builtin_println_bool(bool v);
void gray_builtin_println_char(int32_t c);
void gray_builtin_println_addr(uintptr_t v);

/*@man print
 *@sig print(value T)
 *@desc Prints any value to stdout without a trailing newline.
 *@example
 *   print("loading... ")
 *   print(42)
 *@end
 */
void gray_builtin_print_str(GrayString s);
void gray_builtin_print_int(int64_t v);
void gray_builtin_print_uint(uint64_t v);
void gray_builtin_print_float(double v);
void gray_builtin_print_bool(bool v);
void gray_builtin_print_char(int32_t c);
void gray_builtin_print_addr(uintptr_t v);

/*@man eprintln
 *@sig eprintln(value T)
 *@desc Prints any value to stderr followed by a newline. Supports all types: string, int, uint, float, bool, char, and pointers. The argument is optional; called with no argument it prints a blank line.
 *@example
 *   eprintln("error: something went wrong")
 *   eprintln(404)
 *   eprintln(3.14)
 *   eprintln(true)
 *   eprintln()
 *@end
 */
void gray_builtin_eprintln_str(GrayString s);
void gray_builtin_eprintln_int(int64_t v);
void gray_builtin_eprintln_uint(uint64_t v);
void gray_builtin_eprintln_float(double v);
void gray_builtin_eprintln_bool(bool v);
void gray_builtin_eprintln_char(int32_t c);
void gray_builtin_eprintln_addr(uintptr_t v);

/*@man eprint
 *@sig eprint(value T)
 *@desc Prints any value to stderr without a trailing newline. Supports all types: string, int, uint, float, bool, char, and pointers.
 *@example
 *   eprint("warning: ")
 *   eprint(404)
 *   eprint(3.14)
 *   eprint(true)
 *@end
 */
void gray_builtin_eprint_str(GrayString s);
void gray_builtin_eprint_int(int64_t v);
void gray_builtin_eprint_uint(uint64_t v);
void gray_builtin_eprint_float(double v);
void gray_builtin_eprint_bool(bool v);
void gray_builtin_eprint_char(int32_t c);
void gray_builtin_eprint_addr(uintptr_t v);

/*@man input
 *@sig input() -> string
 *@desc Reads a line from stdin and returns it as a string (newline stripped).
 *@example
 *   print("Name: ")
 *   mut name string = input()
 *   println("Hello, ${name}")
 *@end
 */
GrayString gray_builtin_input(GrayArena *arena);

/*@man assert
 *@sig assert(condition bool, message string = "")
 *@desc Terminates the program if condition is false, printing the message and source location.
 *@example
 *   assert(x > 0, "x must be positive")
 *   assert(len(items) > 0, "list cannot be empty")
 *@end
 */
void gray_builtin_assert(bool condition, GrayString message, const char *file, int line);

/*@man panic
 *@sig panic(message string)
 *@desc Immediately terminates the program with the given message and source location.
 *@example
 *   panic("unreachable code reached")
 *@end
 */
void gray_builtin_panic_msg(GrayString message);

/*@man exit
 *@sig exit(code int)
 *@desc Exits the program immediately with the given exit code.
 *@example
 *   exit(0)
 *   exit(1)
 *@end
 */
void gray_builtin_exit(int64_t code);

/*@man sleep_s
 *@sig sleep_s(seconds int)
 *@desc Pauses execution for the given number of seconds.
 *@example
 *   sleep_s(1)
 *@end
 */
void gray_builtin_sleep_s(int64_t seconds);

/*@man sleep_ms
 *@sig sleep_ms(milliseconds int)
 *@desc Pauses execution for the given number of milliseconds.
 *@example
 *   sleep_ms(500)
 *@end
 */
void gray_builtin_sleep_ms(int64_t ms);

/*@man sleep_ns
 *@sig sleep_ns(nanoseconds int)
 *@desc Pauses execution for the given number of nanoseconds.
 *@example
 *   sleep_ns(1000000)
 *@end
 */
void gray_builtin_sleep_ns(int64_t ns);

/*@man int
 *@sig int(value T) -> int
 *@desc Converts a value to int (64-bit signed). Truncates floats toward zero. char converts to codepoint.
 *@example
 *   mut x int = int(3.9)
 *   mut y int = int('A')
 *@end
 */

/*@man uint
 *@sig uint(value T) -> uint
 *@desc Converts a value to uint (64-bit unsigned integer).
 *@example
 *   mut x uint = uint(42)
 *@end
 */

/*@man float
 *@sig float(value T) -> float
 *@desc Converts a value to float (64-bit double). Integer promotion is lossless within range.
 *@example
 *   mut x float = float(42)
 *@end
 */

/*@man string
 *@sig string(value T) -> string
 *@desc Converts any value to its string representation.
 *@example
 *   mut s string = string(123)
 *   mut s2 string = string(true)
 *@end
 */

/*@man char
 *@sig char(codepoint int) -> char
 *@desc Converts an integer Unicode codepoint to a char value.
 *@example
 *   mut c char = char(65)
 *   println(c)
 *@end
 */

/*@man byte
 *@sig byte(value T) -> byte
 *@desc Converts a value to byte (uint8, 0-255).
 *@example
 *   mut b byte = byte(255)
 *@end
 */

/*@man bool
 *@sig bool(value T) -> bool
 *@desc Converts a value to bool.
 *@example
 *   mut flag bool = true
 *   mut done bool = false
 *@end
 */

/*@man func
 *@sig func_name(param T) -> T {}
 *@desc The function reference type. Written with a full typed signature: parameter list and return type. Holds a reference to a named function created with ()name or ref(name). Used as parameter types, struct fields, and in arrays/maps. References are const-only and not printable.
 *@example
 *   do double(n int) -> int { return n * 2 }
 *   do apply(f func(int) -> int, x int) -> int { return f(x) }
 *   const g func(int) -> int = ()double
 *   println(apply(()double, 5))   // 10
 *@end
 */

/*@man cast
 *@sig cast(value T, TargetType) -> TargetType
 *@desc Explicit type conversion. Required for sized integer types (i8, u32, etc). Truncates floats. Enforces range at runtime.
 *@example
 *   mut x int = cast(3.7, int)
 *   mut b u8 = cast(200, u8)
 *   mut f float = cast(my_int, float)
 *@end
 */



/*@man i128
 *@sig i128(value T) -> i128
 *@desc 128-bit signed integer. Supports all arithmetic and comparisons. Overflow panics at runtime.
 *@example
 *   mut a i128 = i128(99999999999999999999)
 *   mut b int = int(a)
 *@end
 */

/*@man u128
 *@sig u128(value T) -> u128
 *@desc 128-bit unsigned integer. Supports all arithmetic and comparisons. Overflow panics at runtime.
 *@example
 *   mut a u128 = u128(99999999999999999999)
 *@end
 */

/*@man i256
 *@sig i256(value T) -> i256
 *@desc 256-bit signed integer. Supports all arithmetic and comparisons. Overflow panics at runtime.
 *@example
 *   mut a i256 = i256(0)
 *@end
 */

/*@man u256
 *@sig u256(value T) -> u256
 *@desc 256-bit unsigned integer. Supports all arithmetic and comparisons. Overflow panics at runtime.
 *@example
 *   mut a u256 = u256(0)
 *@end
 */

/*@man len
 *@sig len(collection T) -> int
 *@desc Returns the length of an array, map, or string. For strings returns byte length, not character count (use char_count for that).
 *@example
 *   println(len("hello"))
 *   println(len(my_array))
 *   println(len(my_map))
 *@end
 */

/*@man type_of
 *@sig type_of(value T) -> string
 *@desc Returns the Grayscale type name of any value as a string at runtime.
 *@example
 *   println(type_of(42))
 *   println(type_of("hi"))
 *   println(type_of(my_struct))
 *@end
 */

/*@man fields
 *@sig fields(instance T) -> [string]
 *@desc Returns the field names of a struct as an array of strings in declaration order. Accepts struct instances and pointers to structs.
 *@example
 *   const Point struct { x int; y int }
 *   mut p = Point{x: 1, y: 2}
 *   println(fields(p))
 *@end
 */

/*@man size_of
 *@sig size_of(Type) -> int
 *@desc Returns the size in bytes of a type. int=8, float=8, bool=1, string=16.
 *@example
 *   println(size_of(int))
 *   println(size_of(MyStruct))
 *@end
 */

/*@man new
 *@sig new(Type) -> ^Type
 *@desc Allocates a zero-initialized struct on the heap and returns a pointer to it.
 *@example
 *   mut p = new(Point)
 *   p.x = 10
 *   p.y = 20
 *@end
 */

/*@man ref
 *@sig ref(variable T) -> ref<T>
 *@desc Creates a transparent alias to a variable. Reads and writes through the alias affect the original. Also used to take a function reference.
 *@example
 *   mut x int = 10
 *   mut r = ref(x)
 *   r = 99
 *   println(x)
 *@end
 */

/*@man addr
 *@sig addr(variable T) -> ^T
 *@desc Returns a pointer to the memory address of a variable.
 *@example
 *   mut x int = 10
 *   mut p ^int = addr(x)
 *   println(p^)
 *@end
 */

/*@man raw
 *@sig raw(variable T) -> ^T
 *@desc Returns a raw pointer to a variable. Unlike addr(), raw pointers are unsafe: dereferences skip nil-check panics and the compiler does not enforce const-source write protection. Use only in performance-critical code where pointer validity is guaranteed.
 *@example
 *   mut x int = 10
 *   mut p = raw(x)
 *   println(p^)
 *@end
 */

/*@man copy
 *@sig copy(value T) -> T
 *@desc Creates a deep copy of any value. Mutations to the copy do not affect the original.
 *@example
 *   mut arr2 = copy(arr)
 *   arr2[0] = 99
 *   println(arr[0])
 *   mut m2 = copy(my_map)
 *   mut s2 = copy(my_struct)
 *@end
 */

/*@man error
 *@sig error(message string) -> Error
 *@desc Creates an Error value with the given message. Access the message via string interpolation.
 *@example
 *   mut err Error = error("file not found")
 *   println("${err}")
 *@end
 */

/*@man range
 *@sig range(start int, end int, step int = 1) -> Range
 *@desc Returns a Range from start (inclusive) to end (exclusive). The step defaults to 1 and controls the increment. Step of 0 panics at runtime.
 *@example
 *   for i in range(0, 5) { println(i) }
 *   for i in range(0, 10, 2) { println(i) }
 *@end
 */

/*@man c_string
 *@sig c_string(ptr ^u8) -> string
 *@desc Wraps a null-terminated C char* pointer as a Grayscale string. Only valid with values from C interop (import c"header.h").
 *@example
 *   import c"mylib.h"
 *   mut s string = c_string(mylib_get_name())
 *@end
 */

/*@man to_char
 *@sig to_char(s string, index int) -> int
 *@desc Returns the Unicode codepoint at character position index (not byte position) in a UTF-8 string. Panics if out of bounds.
 *@example
 *   mut cp int = to_char("hello", 0)
 *   println(cp)
 *@end
 */

/*@man char_count
 *@sig char_count(s string) -> int
 *@desc Returns the number of Unicode codepoints in a UTF-8 string. Unlike len() which counts bytes.
 *@example
 *   println(char_count("hello"))
 *   println(len("hello"))
 *@end
 */

/*@man here
 *@sig here() -> SourceLocation
 *@desc Returns a SourceLocation with the current file, line, and column filled in at compile time.
 *@example
 *   mut loc = here()
 *   println(loc.file)
 *   println(loc.line)
 *@end
 */

/*@man SourceLocation
 *@kind type
 *@field file string
 *@field line int
 *@field column int
 *@desc Returned by here(). Contains the source file path, line number, and column at the call site, filled in at compile time.
 *@example
 *   mut loc = here()
 *   println(loc.file)
 *   println(loc.line)
 *   println(loc.column)
 *@end
 */

/*@man embed
 *@sig embed(path string) -> string
 *@desc Reads a file at compile time and returns its contents as a string literal baked into the binary. The path is resolved relative to the source file containing the embed() call. The argument must be a string literal — variables and expressions are rejected (E5017). If the file cannot be read, the compiler emits E5018. Valid at file scope and inside function bodies.
 *@example
 *   const LICENSE string = embed("../../LICENSE")
 *   const CONFIG string = embed("config/defaults.json")
 *   do main() {
 *       println(LICENSE)
 *   }
 *@end
 */

/*@man Error
 *@kind type
 *@desc Represents a runtime error. Opaque type with no accessible fields. Use string interpolation to get the message.
 *@example
 *   mut err Error = error("file not found")
 *   println("${err}")
 *@end
 */

/*@man system
 *@sig system(command string) -> int
 *@desc Runs a shell command and returns its exit code. Mirrors C's system(). Returns -1 if the process was killed by a signal.
 *@example
 *   mut code int = system("ls -la")
 *   println(code)
 *   system("echo hello")
 *@end
 */
int64_t gray_builtin_system(GrayString cmd);

/* to_string — internal runtime overloads, not user-callable by name */
GrayString gray_builtin_to_string_int(GrayArena *arena, int64_t v);
GrayString gray_builtin_to_string_uint(GrayArena *arena, uint64_t v);
GrayString gray_builtin_to_string_float(GrayArena *arena, double v);
GrayString gray_builtin_to_string_bool(GrayArena *arena, bool v);

/* from_string — internal runtime overloads */
int64_t gray_builtin_string_to_int(GrayString s);
double gray_builtin_string_to_float(GrayString s);

/* format float for interpolation */
GrayString gray_builtin_format_float(GrayArena *arena, double v);

/* composite to_string */
GrayString gray_builtin_array_to_string(GrayArena *arena, GrayArray *arr, int elem_kind);
GrayString gray_builtin_map_to_string(GrayArena *arena, GrayMap *m, int val_kind);

/* to_char / char_count — Unicode codepoint access */
int32_t gray_builtin_to_char(GrayString s, int64_t index, const char *file, int line);
int64_t gray_builtin_char_count(GrayString s);

/* char_to_utf8 — encode a codepoint to an GrayString (for interpolation) */
GrayString gray_builtin_char_to_utf8(GrayArena *arena, int32_t cp);

#endif
