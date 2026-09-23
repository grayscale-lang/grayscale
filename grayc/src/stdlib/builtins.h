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
void gray_builtin_println_str(GrayString str);
void gray_builtin_println_i64(int64_t value);
void gray_builtin_println_u64(uint64_t value);
void gray_builtin_println_float(double value, int bit_size);
void gray_builtin_println_bool(bool value);
void gray_builtin_println_char(int32_t codepoint);
void gray_builtin_println_addr(uintptr_t value);

/*@man print
 *@sig print(value T)
 *@desc Prints any value to stdout without a trailing newline.
 *@example
 *   print("loading... ")
 *   print(42)
 *@end
 */
void gray_builtin_print_str(GrayString str);
void gray_builtin_print_i64(int64_t value);
void gray_builtin_print_u64(uint64_t value);
void gray_builtin_print_float(double value, int bit_size);
void gray_builtin_print_bool(bool value);
void gray_builtin_print_char(int32_t codepoint);
void gray_builtin_print_addr(uintptr_t value);

/*@man flush
 *@sig flush()
 *@desc Flushes buffered stdout so partial-line output (prompts, progress indicators) appears immediately, even when stdout is a pipe or file.
 *@example
 *   print("working")
 *   flush()
 *@end
 */
void gray_builtin_flush(void);

/*@man eprintln
 *@sig eprintln(value T)
 *@desc Prints any value to stderr followed by a newline. Supports all types: string, i64, u64, f64, bool, char, and pointers. The argument is optional; called with no argument it prints a blank line.
 *@example
 *   eprintln("error: something went wrong")
 *   eprintln(404)
 *   eprintln(3.14)
 *   eprintln(true)
 *   eprintln()
 *@end
 */
void gray_builtin_eprintln_str(GrayString str);
void gray_builtin_eprintln_i64(int64_t value);
void gray_builtin_eprintln_u64(uint64_t value);
void gray_builtin_eprintln_float(double value, int bit_size);
void gray_builtin_eprintln_bool(bool value);
void gray_builtin_eprintln_char(int32_t codepoint);
void gray_builtin_eprintln_addr(uintptr_t value);

/*@man eprint
 *@sig eprint(value T)
 *@desc Prints any value to stderr without a trailing newline. Supports all types: string, i64, u64, f64, bool, char, and pointers.
 *@example
 *   eprint("warning: ")
 *   eprint(404)
 *   eprint(3.14)
 *   eprint(true)
 *@end
 */
void gray_builtin_eprint_str(GrayString str);
void gray_builtin_eprint_i64(int64_t value);
void gray_builtin_eprint_u64(uint64_t value);
void gray_builtin_eprint_float(double value, int bit_size);
void gray_builtin_eprint_bool(bool value);
void gray_builtin_eprint_char(int32_t codepoint);
void gray_builtin_eprint_addr(uintptr_t value);

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
 *@sig exit(code i64)
 *@desc Exits the program immediately with the given exit code.
 *@example
 *   exit(0)
 *   exit(1)
 *@end
 */
void gray_builtin_exit(int64_t code);

/*@man sleep_s
 *@sig sleep_s(seconds i64)
 *@desc Pauses execution for the given number of seconds.
 *@example
 *   sleep_s(1)
 *@end
 */
void gray_builtin_sleep_s(int64_t seconds);

/*@man sleep_ms
 *@sig sleep_ms(milliseconds i64)
 *@desc Pauses execution for the given number of milliseconds.
 *@example
 *   sleep_ms(500)
 *@end
 */
void gray_builtin_sleep_ms(int64_t ms);

/*@man sleep_ns
 *@sig sleep_ns(nanoseconds i64)
 *@desc Pauses execution for the given number of nanoseconds.
 *@example
 *   sleep_ns(1000000)
 *@end
 */
void gray_builtin_sleep_ns(int64_t ns);

/*@man string
 *@sig string(value T) -> string
 *@desc Converts any value to its string representation.
 *@example
 *   mut s string = string(123)
 *   mut s2 string = string(true)
 *@end
 */

/*@man char
 *@sig char(codepoint i64) -> char
 *@desc Converts an integer Unicode codepoint to a char value.
 *@example
 *   mut c char = char(65)
 *   println(c)
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
 *   do double(n i64) -> i64 { return n * 2 }
 *   do apply(f func(i64) -> i64, x i64) -> i64 { return f(x) }
 *   const g func(i64) -> i64 = ()double
 *   println(apply(g, 5))   // 10
 *@end
 */

/*@man cast
 *@sig cast(value T, TargetType) -> TargetType
 *@desc Explicit type conversion between primitive types. Truncates floats toward zero; a string target type parses the string and panics if it is not a number. Enforces range at runtime.
 *@example
 *   mut x i64 = cast(3.7, i64)
 *   mut b u8 = cast(200, u8)
 *   mut f f64 = cast(count, f64)
 *   mut n i64 = cast("42", i64)
 *@end
 */



/*@man i128
 *@sig i128(value T) -> i128
 *@desc 128-bit signed integer. Supports all arithmetic and comparisons. Overflow panics at runtime.
 *@example
 *   mut an i128 = i128(99999999999999999999)
 *   mut b i128 = a * i128(2)
 *   println(b)
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
 *   mut an i256 = i256(0)
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
 *@sig len(collection T) -> i64
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
 *   const Point struct {
 *       x i64
 *       y i64
 *   }
 *   mut p = Point{x: 1, y: 2}
 *   println(fields(p))
 *@end
 */

/*@man size_of
 *@sig size_of(Type) -> i64
 *@desc Returns the size in bytes of a type. i64=8, f64=8, bool=1, string=16.
 *@example
 *   println(size_of(i64))
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
 *@sig ref(variable T) -> T
 *@desc Creates a transparent alias to a variable; the alias has the same type as the variable and cannot be explicitly annotated. Reads and writes through the alias affect the original. Also used to take a function reference.
 *@example
 *   mut x i64 = 10
 *   mut r = ref(x)
 *   r = 99
 *   println(x)
 *@end
 */

/*@man addr
 *@sig addr(variable T) -> ^T
 *@desc Returns a pointer to the memory address of a variable.
 *@example
 *   mut x i64 = 10
 *   mut p ^i64 = addr(x)
 *   println(p^)
 *@end
 */

/*@man raw
 *@sig raw(variable T) -> ^T
 *@desc Returns a raw pointer to a variable. Unlike addr(), raw pointers are unsafe: dereferences skip nil-check panics and the compiler does not enforce const-source write protection. Use only in performance-critical code where pointer validity is guaranteed.
 *@example
 *   mut x i64 = 10
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
 *@sig error(code ErrorCode = .Unknown, message string = "") -> Error
 *@desc Creates an Error value. Forms: error("msg") (code defaults to .Unknown), error(.Code), or error(.Code, "msg"). Inspect err.code with == or `when`; err.msg holds the message.
 *@example
 *   mut err Error = error(.NotFound, "file not found")
 *   when err.code { is .NotFound { println(err.msg) } default { } }
 *@end
 */

/*@man range
 *@sig range(start i64, end i64, step i64 = 1) -> Range
 *@desc Returns a Range from start (inclusive) to end (exclusive). The step defaults to 1 and controls the increment. Step of 0 panics at runtime.
 *@example
 *   for i in range(0, 5) { println(i) }
 *   for i in range(0, 10, 2) { println(i) }
 *@end
 */

/*@man c_string
 *@sig c_string(ptr ^u8) -> string
 *@desc Wraps a null-terminated C char* pointer as a Grayscale string. Only valid with values from C interop (extern import "header.h").
 *@example
 *   extern import "mylib.h"
 *   mut s string = c_string(mylib_get_name())
 *@end
 */

/*@man to_char
 *@sig to_char(s string, index i64) -> char
 *@desc Returns the char at character position index (not byte position) in a UTF-8 string. The char is a 32-bit Unicode codepoint; apply i64() to the result for its numeric value. Panics if out of bounds.
 *@example
 *   mut c char = to_char("hello", 0)
 *   println(c)
 *   println(cast(c, i64))
 *@end
 */

/*@man char_count
 *@sig char_count(s string) -> i64
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
 *@field line i64
 *@field column i64
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
 *@sig system(command string) -> i64
 *@desc Runs a shell command and returns its exit code. Mirrors C's system(). Returns -1 if the process was killed by a signal.
 *@example
 *   mut code i64 = system("ls -la")
 *   println(code)
 *   system("echo hello")
 *@end
 */
int64_t gray_builtin_system(GrayString cmd);

/* to_string — internal runtime overloads, not user-callable by name */
GrayString gray_builtin_to_string_i64(GrayArena *arena, int64_t value);
GrayString gray_builtin_to_string_u64(GrayArena *arena, uint64_t value);
GrayString gray_builtin_to_string_float(GrayArena *arena, double value, int bit_size);
GrayString gray_builtin_to_string_bool(GrayArena *arena, bool value);

/* from_string — internal runtime overloads */
int64_t gray_builtin_string_to_i64(GrayString str);
double gray_builtin_string_to_f64(GrayString str);

/* format float for interpolation */
GrayString gray_builtin_format_float(GrayArena *arena, double value, int bit_size);

/* composite to_string */
GrayString gray_builtin_array_to_string(GrayArena *arena, GrayArray *arr, int elem_kind);
GrayString gray_builtin_map_to_string(GrayArena *arena, GrayMap *map, int val_kind);

/* A growable text buffer that generated print code writes to in place of a
 * FILE, so println and string interpolation format a nested container with the
 * same code. gray_out_printf and gray_out_write take either kind of stream. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} GrayFmtOut;

int gray_fmt_out_printf(GrayFmtOut *out, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
size_t gray_fmt_out_write(const void *data, size_t size, size_t count, GrayFmtOut *out);
GrayString gray_fmt_out_finish(GrayArena *arena, GrayFmtOut *out);

#define gray_out_printf(stream, ...) \
    _Generic((stream), FILE *: fprintf, GrayFmtOut *: gray_fmt_out_printf)((stream), __VA_ARGS__)
#define gray_out_write(data, size, count, stream) \
    _Generic((stream), FILE *: fwrite, GrayFmtOut *: gray_fmt_out_write)((data), (size), (count), (stream))

/* to_char / char_count — Unicode codepoint access */
int32_t gray_builtin_to_char(GrayString str, int64_t index, const char *file, int line);
int64_t gray_builtin_char_count(GrayString str);

/* char_to_utf8 — encode a codepoint to an GrayString (for interpolation) */
GrayString gray_builtin_char_to_utf8(GrayArena *arena, int32_t cp);

/* Decode the next UTF-8 character starting at p (< end); writes the decoded
 * codepoint to *cp_out (0xFFFD on invalid input) and returns bytes consumed
 * (1-4). */
int gray_builtin_utf8_next(const uint8_t *p, const uint8_t *end, int32_t *cp_out);

#endif
