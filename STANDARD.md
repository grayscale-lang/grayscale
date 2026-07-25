# The Grayscale Programming Language Standard

> **Notice**
>
> This specification is a living document and subject to change as the language evolves.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Lexical Structure](#2-lexical-structure)
3. [Types](#3-types)
4. [Variables and Constants](#4-variables-and-constants)
5. [Expressions](#5-expressions)
6. [Statements](#6-statements)
7. [Functions](#7-functions)
8. [Modules](#8-modules)
9. [Standard Library](#9-standard-library)
10. [Error Handling](#10-error-handling)
11. [Memory Model](#11-memory-model)
12. [Program Execution](#12-program-execution)
13. [CLI Commands](#13-cli-commands)

---

## 1. Introduction

### 1.1 Purpose

This document defines the Grayscale programming language. It serves as the authoritative reference for the language's syntax, semantics, and behavior. Implementations of Grayscale must conform to this specification.

### 1.2 Overview

Grayscale is a compiled language where simplicity meets flexibility. Inspired by C, Odin, Rust, and Go. The language emphasizes:

- **Simplicity**: A minimal set of orthogonal features
- **Clarity**: Explicit syntax that reads naturally
- **Safety**: Static type checking and runtime bounds checking

---

## 2. Lexical Structure

### 2.1 Source Encoding

Grayscale source files must be encoded in UTF-8. However, identifiers are restricted to ASCII characters.

### 2.2 Line Terminators

Line terminators are the ASCII line feed character (LF, U+000A) or the ASCII carriage return character (CR, U+000D) optionally followed by a line feed.

### 2.3 Comments

Grayscale supports two forms of comments:

**Single-line comments** begin with `//` and extend to the end of the line:

```gray
// This is a single-line comment
mut x int = 42  // inline comment
```

**Multi-line comments** begin with `/*` and end with `*/`:

```gray
/* This is a
   multi-line comment */
```

Multi-line comments can also be used inline within a statement:

```gray
mut x int = /* default value */ 10
```

Multi-line comments do not nest. A `/*` inside a multi-line comment has no special meaning.

### 2.4 Identifiers

Identifiers name program entities such as variables, functions, types, and modules.

Identifiers must:
- Begin with an ASCII letter
- Contain only ASCII letters, digits, and underscores
- Not be a reserved keyword

Valid identifiers: `x`, `count`, `myVariable`, `point_2d`, `MAX_SIZE`

Invalid identifiers: `2fast`, `my-var`, `café`, `_private`

### 2.5 Keywords

The following words are reserved and may not be used as identifiers:

**Control flow:**
```
as_long_as   break       continue    default
else         ensure      for         for_each    if
is           loop        or          or_return   otherwise
return       when        while
```

**Declarations:**
```
const        do          enum        import      mut
new          private     struct      use*        using
```

> 💡 **Tip:** `use*` is reserved exclusively for the `import and use` statement. It has no other syntactic role.

**Types (reserved names):**
```
bool         byte        char        Error       float
func         int         map         nil         string
uint
```

**Sized types (reserved names):**
```
i8    i16    i32    i64    i128    i256
u8    u16    u32    u64    u128    u256
f32   f64
```

**Operators and values:**
```
bit_and      bit_not     bit_or      bit_shift_left   bit_shift_right
bit_xor      cast        false       in          not_in      range
true
```

#### Syntax Aliases

Some keywords have shorter or more familiar aliases. Both forms are identical and produce the same token:

| Alias   | Canonical      | Purpose              |
|---------|----------------|----------------------|
| `else`  | `otherwise`    | Default branch       |
| `while` | `as_long_as`   | Condition loop       |

> 💡 **Tip:** The `map` keyword is optional in type position — `[string:int]` and `map[string:int]` are identical. The parser normalizes both to the same canonical form.

### 2.6 Operators and Punctuation

```
+    -    *    /    %
==   !=   <    >    <=   >=
&&   ||   !
=    +=   -=   *=   /=   %=
++   --
^    &    @    #
(    )    {    }    [    ]
,    :    .    ->
```

- `^` — pointer type prefix and dereference postfix (`^Type`, `ptr^`)
- `&` — address-of (used internally)
- `@` — module prefix in imports (`import @math`)
- `#` — attribute prefix (`#doc`, `#flags`, `#strict`)

Bitwise operations use keyword syntax (`bit_and`, `bit_or`, etc.) because `^` and `&` are already used for pointer types and address-of. See [Section 5.2.7](#527-bitwise-operators).

### 2.7 Literals

#### 2.7.1 Integer Literals

Integer literals represent integer values.

Underscores may be used for readability but:
- Must not appear at the beginning or end
- Must not appear consecutively
- Must not appear adjacent to the decimal point

Examples: `42`, `1_000_000`, `0xFF`, `0xDEAD_BEEF`, `0o777`, `0o1_2_3`, `0b1010`, `0b1111_0000`

#### 2.7.2 Floating-Point Literals

Floating-point literals represent floating-point values.

Examples: `3.14159`, `0.5`, `100.0`

#### 2.7.3 String Literals

String literals represent string values.

Escape sequences:
- `\n` - line feed (U+000A)
- `\r` - carriage return (U+000D)
- `\t` - horizontal tab (U+0009)
- `\\` - backslash
- `\"` - double quote
- `\xNN` - hex byte value (e.g., `\x48` = 'H', `\x0a` = newline)

String interpolation allows embedding expressions within strings:

```gray
mut name string = "World"
mut greeting string = "Hello, ${name}!"  // "Hello, World!"
```

#### 2.7.4 Raw String Literals

Raw string literals are enclosed in backticks and do not process escape sequences or string interpolation:

Raw strings:
- Do not process escape sequences (`\n` is a literal backslash followed by `n`)
- Do not process string interpolation (`${x}` is literal text)
- May span multiple lines
- Cannot contain backticks (no escape mechanism)

```gray
mut path string = `C:\Users\test\file.txt`
mut pattern string = `\d+\.\d+`
mut multi string = `line1
line2
line3`
```

#### 2.7.5 Character Literals

Character literals represent single character values.

Examples: `'A'`, `'\n'`, `'\t'`

Character literals must contain exactly one character (or escape sequence).

#### 2.7.6 Boolean Literals

#### 2.7.7 Nil Literal

The literal `nil` represents the absence of a value.

---

## 3. Types

Grayscale is statically typed. Every variable and expression has a type known at check time.

### 3.1 Primitive Types

#### 3.1.1 Integer Type (`int`)

The `int` type represents a 64-bit signed integer. Arithmetic operations use checked arithmetic; overflow or underflow produces a runtime panic rather than silent wrapping.

```gray
mut small int = 42
mut large int = 9223372036854775807  // Max 64-bit signed value
```

#### 3.1.2 Unsigned Integer Type (`uint`)

The `uint` type represents a 64-bit unsigned integer. Like `int`, arithmetic is overflow-checked with a runtime panic on overflow.

```gray
mut count uint = 100
mut big uint = 18446744073709551615  // Max 64-bit unsigned value
```

Assigning a negative value to a `uint` produces a check-time error.

#### 3.1.3 Floating-Point Type (`float`)

The `float` type represents 64-bit IEEE 754 double-precision floating-point numbers.

Division by zero with floating-point operands produces a runtime panic. However, special IEEE 754 values (`NaN`, `Infinity`, `-Infinity`) can appear through stdlib math functions (e.g., edge cases in trigonometric or logarithmic functions). Use `math.is_nan()`, `math.is_infinite()`, and `math.is_finite()` to check for these values.

```gray
mut pi float = 3.14159
mut negative float = -2.5
```

Integer values are implicitly promoted to `float` when the target type is `float`, `f32`, or `f64`. This applies to variable declarations, assignments, function arguments, map literal values, and return statements:

```gray
mut x float = 5       // 5.0
mut y f64 = 1          // 1.0
x = 42                 // 42.0
```

No explicit `float()` cast is needed. The promotion is lossless for values within the floating-point range.

#### 3.1.4 String Type (`string`)

The `string` type represents a UTF-8 encoded byte sequence. String indexing (`str[i]`) returns the byte at byte position `i`, not a Unicode codepoint. `len()` returns the byte length, not the character count.

For ASCII strings, one byte equals one character, so indexing works as expected:

```gray
mut greeting string = "Hello, World!"
mut first_char char = greeting[0]  // 'H'
```

For multi-byte UTF-8 strings, individual bytes may not form complete characters:

```gray
mut s string = "日本語"
println(len(s))         // 9 (byte length, not 3 characters)
println(char_count(s))  // 3 (Unicode character count)
println(to_char(s, 0))  // 26085 (codepoint for '日')
```

Use `to_char()` to access characters by codepoint index and `char_count()` to get the true character count.

#### 3.1.5 Boolean Type (`bool`)

The `bool` type has exactly two values: `true` and `false`.

```gray
mut flag bool = true
mut result bool = 10 > 5  // true
```

#### 3.1.6 Character Type (`char`)

The `char` type represents a single character.

```gray
mut letter char = 'A'
mut newline char = '\n'
```

Characters can be converted to their integer code point using `int()`.

#### 3.1.7 Byte Type (`byte`)

The `byte` type represents an 8-bit unsigned integer with values from 0 to 255.

```gray
mut b byte = 0xFF  // 255
mut c byte = 128   // decimal also works
```

Assigning a value outside the range 0-255 to a `byte` is a check-time error.

#### 3.1.8 Sized Integer Types

Grayscale provides fixed-width integer types for precise control over integer size:

| Type | Width | Signed | Range |
|------|-------|--------|-------|
| `i8` | 8-bit | yes | -128 to 127 |
| `i16` | 16-bit | yes | -32,768 to 32,767 |
| `i32` | 32-bit | yes | -2^31 to 2^31-1 |
| `i64` | 64-bit | yes | -2^63 to 2^63-1 |
| `u8` | 8-bit | no | 0 to 255 |
| `u16` | 16-bit | no | 0 to 65,535 |
| `u32` | 32-bit | no | 0 to 2^32-1 |
| `u64` | 64-bit | no | 0 to 2^64-1 |

Use `cast` for sized type conversions: `cast(value, i32)`, `cast(value, u16)`.

#### 3.1.9 Wide Integer Types (`i128`, `u128`, `i256`, `u256`)

Grayscale provides portable wide integer types backed by struct-based arithmetic (no compiler extensions required):

| Type | Width | Signed | `size_of` |
|------|-------|--------|-----------|
| `i128` | 128-bit | yes | 16 |
| `u128` | 128-bit | no | 16 |
| `i256` | 256-bit | yes | 32 |
| `u256` | 256-bit | no | 32 |

Wide integers support all standard arithmetic (`+`, `-`, `*`, `/`, `%`) and comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`). Values are constructed using the type name as a function:

```gray
mut a i128 = i128(42)
mut b i128 = i128(100)
mut c i128 = a + b          // i128 addition
println(c)                   // prints "142"
println(type_of(c))          // "i128"
println(size_of(i128))       // 16

mut x int = int(c)           // cast back to int
mut s string = string(c)     // convert to string
```

Wide integers use the same overflow-checked arithmetic as `int` and `uint`; overflow produces a runtime panic.

#### 3.1.10 Pointer Type (`^Type`)

The pointer type `^Type` represents a memory address pointing to a value of `Type`.

| Syntax | Meaning |
|--------|---------|
| `^int` | Pointer to an `int` |
| `^MyStruct` | Pointer to a `MyStruct` |
| `addr(x)` | Get the address of `x` |
| `p^` | Dereference pointer `p` |

```gray
mut x int = 42
mut p ^int = addr(x)
println(p)   // 0x16d1ab9f8, prints the address as hex
println(p^)  // 42, explicit dereference reads the pointee
p^ = 100
println(x)   // 100
```

Printing a pointer value (`println(p)`, `print(p)`, etc.) outputs the address in hex (`0x...`) when non-nil and `nil` when null. Use `p^` to access the pointee.

Dereferencing a `nil` pointer causes a runtime panic.

> 💡 **Tip:** You can dereference directly on a call result without storing the pointer first. `new(Foo)^` allocates a `Foo` and immediately gives you the value, handy when a function returns `^Type` and you want the value right at the call site: `return new(Foo)^` or `mut val = make_thing()^`.

> 💡 **Tip:** The dot operator (`.`) automatically dereferences pointers to structs. If `p` is a `^MyStruct`, writing `p.field` is equivalent to `p^.field`. This auto-dereference applies to field access and struct function calls but does **not** apply in other contexts. For example, `println(p)` prints the address, and `return p` returns the pointer itself. Use explicit `p^` when you need the pointee value rather than field access.

### 3.2 Composite Types

#### 3.2.1 Arrays

Arrays are ordered collections of elements of the same type.

**Dynamic arrays** have variable length:

```gray
mut numbers [int] = {1, 2, 3, 4, 5}
mut empty [string] = {}
```

**Fixed-size arrays** have a length specified at declaration:

```gray
const fixed [int, 3] = {10, 20, 30}
```

Fixed-size arrays must be declared with `const`. Providing fewer values than the declared size is permitted; providing more values than the declared size is an error.

```gray
const a [int, 5] = {1, 2, 3}           // OK (3 of 5 slots used, remaining zero-initialized)
const b [int, 5] = {1, 2, 3, 4, 5, 6}  // Error: 6 values exceeds size of 5
```

The size specifier `N` may also be a compile-time integer constant of any integer type (`int`, `uint`, `i8`–`i64`, `u8`–`u64`). The constant must be declared before the array and must resolve to a value greater than zero.

```gray
const SIZE int = 4
const buf [byte, SIZE] = {0x01, 0x02, 0x03, 0x04}
```

**Multi-dimensional arrays**:

```gray
mut matrix [[int]] = {{1, 2}, {3, 4}}
mut cube [[[int]]] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}}
```

Array indexing is zero-based. Accessing an index outside the valid range produces a runtime error.

#### 3.2.2 Maps

Maps are unordered collections of key-value pairs. The `map` keyword is optional — `[K:V]` and `map[K:V]` are identical:

```gray
mut ages [string:int] = {
    "alice": 30,
    "bob": 25
}
mut empty [string:int] = {:}  // Empty map

// Long form is also valid:
mut scores map[string:int] = {"math": 95}
```

> 💡 **Tip:** Empty maps use `{:}`, not `{}`. The `{}` literal is an empty array; the colon is the tell!

```gray
mut arr [int] = {}       // Empty array
mut m [string:int] = {:}  // Empty map
```

**Bracket disambiguation:**

| Syntax    | Meaning                   | Example                   |
|-----------|---------------------------|---------------------------|
| `[T]`     | Dynamic array of `T`      | `[int]`                   |
| `[T,N]`   | Fixed-size array of `T`   | `[int,3]`                 |
| `[K:V]`   | Map from `K` to `V`       | `[string:int]`            |

Maps must be declared with `mut`. Declaring a map with `const` is a compile-time error. If the keys are known at compile time, use a struct instead.

Keys must be of a hashable type: `int`, `uint`, `float`, `string`, `bool`, `char`, or `byte`.

Accessing a key that does not exist produces a runtime error.

#### 3.2.3 Structs

Structs are user-defined composite types with named fields.

```gray
const Point struct {
    x int
    y int
}

const Person struct {
    name string
    age int
    active bool
}
```

> 💡 **Tip:** Struct and enum declarations must be at the top level of a file, never inside a function or block. Fields must be on separate lines; semicolons are not allowed. This is intentional. Unlike functions and control flow, structs and enums define *types*, not logic. Types belong where they are visible, nameable, and reusable. Burying a type inside a function makes it invisible to the rest of your program and harder to find when reading code.

#### Recursive Structs

A struct may reference itself through a **pointer field**. Value-type self-reference is rejected at compile time.

```gray
const Node struct {
    val  int
    next ^Node   // OK: pointer field
}

// Value-type self-reference is an error:
const Bad struct {
    val  int
    next Bad     // error: struct 'Bad' cannot contain itself by value; use ptr<Bad>
}
```

To traverse a recursive struct, dot notation automatically dereferences pointer fields with no explicit `^` required. The `^` suffix is also accepted if preferred:

```gray
mut a = new(Node)
mut b = new(Node)
a.val  = 1
b.val  = 2
a.next = b

println(a.val)        // 1
println(a.next.val)   // 2, implicit dereference
println(a.next^.val)  // 2, explicit dereference (also valid)
```

Mutual recursion through pointer fields is supported. Both structs must use pointer fields (`^Type`) to reference each other; value-type mutual reference is rejected at compile time.

Struct instantiation uses named field syntax:

```gray
mut origin Point = Point{x: 0, y: 0}
mut p Point = Point{}  // Zero-initialized: x=0, y=0
```

#### Default Field Values

Struct fields may specify a default value using `= expr` after the type. When a struct is created with `new()` or a struct literal that omits a field, the default value is used instead of zero-initialization.

```gray
const Config struct {
    host string = "localhost"
    port int = 8080
    verbose bool = false
}

do main() {
    mut c = new(Config)       // c.host = "localhost", c.port = 8080, c.verbose = false
    mut c2 = Config{port: 3000}  // c2.host = "localhost", c2.port = 3000, c2.verbose = false
}
```

Grouped fields share the same default:

```gray
const Point struct {
    x, y int = 0
    z int = 1
}
```

Fields without a default value remain zero-initialized when omitted.

`#json` structs cannot have default field values. They are data-only and always zero-initialized from JSON input.

Fields are accessed using dot notation:

```gray
mut x_value int = origin.x
origin.x = 10  // Modification (if variable is mut)
```

#### 3.2.4 Enums

Enums define a type with a fixed set of named values.

**Integer enums** (default, auto-incrementing from 0):

```gray
const Direction enum {
    NORTH    // 0
    EAST     // 1
    SOUTH    // 2
    WEST     // 3
}
```

> 💡 **Tip:** Enum variants must be on separate lines. Inline declarations like `const Color enum { RED; GREEN; BLUE }` are not allowed. Semicolons are never used in enum declarations.

> 💡 **Tip:** Enums are not integers. Even though integer enums are backed by numeric values under the hood, you cannot compare an enum variable with an integer (`d == 0`), assign an integer to an enum variable (`d = 2`), or perform arithmetic on enum values. Enums can only be compared with values of the same enum type using `==` and `!=`. Use `Direction.NORTH`, `.NORTH`, or another `Direction` variable — never a raw number.

> 💡 **Tip:** If you genuinely need to compare an enum value against an integer, use `cast()` to bridge the gap: `if cast(Direction.NORTH, int) == 0 { ... }`. You can also cast the other way: `cast(0, Direction)`.

**Flags enums** (powers of 2, annotated with `#flags`):

```gray
#flags
const Permissions enum {
    READ      // 1
    WRITE     // 2
    EXECUTE   // 4
    DELETE    // 8
}

Enum values are accessed using dot notation:

```gray
mut dir = Direction.NORTH
mut status = Status.TODO
```

**Implicit enum selector (`.VARIANT`):**

When the expected enum type is known from context, you can use the shorthand `.VARIANT` instead of the full `EnumName.VARIANT`. The compiler resolves the enum type automatically.

```gray
// Variable declaration with type annotation
mut dir Direction = .NORTH

// Assignment
dir = .SOUTH

// Function arguments
do move(d Direction) -> int { return 0 }
move(.EAST)

// When/is branches
when dir {
    is .NORTH { println("north") }
    is .SOUTH { println("south") }
    default { println("other") }
}

// Comparisons
if dir == .WEST { println("west") }

// Return statements
do get_dir() -> Direction { return .NORTH }

// Struct literal fields
const Config struct { dir Direction }
mut c = Config{ dir: .EAST }

// Array literals
mut dirs [Direction] = {.NORTH, .SOUTH}
```

The full `EnumName.VARIANT` form is always valid and is required when no type context is available.

**Tagged Enums (Variants with Data):**

Enum variants can carry associated data (payloads), making the enum a tagged union. A variant's payload is declared with positional types in parentheses:

```gray
const Shape enum {
    Circle(float)
    Rect(float, float)
    Point
}
```

An enum becomes a tagged union if ANY variant has a payload. Variants without payloads (like `Point` above) are plain tag-only variants. Payloads and explicit values (`= 5`) are mutually exclusive per variant. String enums and `#flags` enums cannot have payloads.

**Construction:**

Tagged enum values are constructed by calling the variant with arguments:

```gray
mut s Shape = Shape.Circle(3.14)
mut r Shape = Shape.Rect(10.0, 20.0)
mut p Shape = Shape.Point
```

The implicit selector syntax also works with tagged constructors:

```gray
mut s Shape = .Circle(3.14)
```

**Destructuring with `when/is`:**

Use pattern destructuring in `when/is` to extract payload values. Use the fully-qualified form:

```gray
when shape {
    is Shape.Circle(radius) {
        println("Circle with radius ${radius}")
    }
    is Shape.Rect(w, h) {
        println("Rectangle ${w} x ${h}")
    }
    is Shape.Point {
        println("Just a point")
    }
}
```

The implicit selector form (dot-prefix) also works:

```gray
when shape {
    is .Circle(r) { println("radius: ${r}") }
    is .Rect(w, h) { println("${w}x${h}") }
    is .Point { println("point") }
}
```

The number of bindings in a pattern must match the variant's payload count. `#strict` exhaustiveness checking works with tagged enums.

### 3.3 Type Inference

Grayscale is a statically-typed language with type inference. The type of every variable is known at compile time, and explicit type annotations are optional in most contexts when the compiler can determine the type from the initializer.

Type inference works with:

1. **Standalone literals** - The type is inferred from the literal value
2. **Function return values** - The variable's type is inferred from the function's return type
3. **Struct literals** - The type is known from the struct name
4. **Built-in constructors** - `new(Type)` (returns `^Type`) and `copy(value)`
5. **Multiple return values** - Each variable's type is inferred from the corresponding return type

> 💡 **Tip:** Array and map literals do **not** support type inference. You must always provide an explicit type annotation (e.g., `mut arr [int] = {1, 2, 3}`, `mut m map[string:int] = {"a": 1}`).

> ⚠️ **File-scope `const` declarations** of primitive types and arrays require explicit type annotations. Type inference for `const` is only supported inside function bodies. For example, `const MAX_SIZE int = 100` is required at file scope, while `const x = 42` is valid inside a function.

```gray
// Inferred from literals
mut x = 42                    // Inferred: int
mut name = "Alice"            // Inferred: string
mut pi = 3.14                 // Inferred: float
mut flag = true               // Inferred: bool

// Explicit annotations are always accepted
mut y int = 42                // Explicit: int

// Inferred from function return type
do sum(a int, b int) -> int {
    return a + b
}
mut result = sum(1, 2)        // Inferred: int
println(type_of(result))        // Output: int

// Inferred from struct literal
const p = Point{x: 1, y: 2}    // Inferred: Point

// Inferred from built-in constructors
mut val = new(Person)         // Inferred: ^Person (pointer)
mut dup = copy(val)           // Inferred: Person

// Arrays and maps always require explicit type annotations
mut arr [int] = {1, 2, 3}           // Explicit: [int]

// Multiple return values
do divide(a, b int) -> (int, int) {
    return a / b, a % b
}
mut quotient, remainder = divide(10, 3)  // Both inferred: int
```

Explicit type annotations are generally optional but can be used for clarity or documentation. The exceptions are file-scope `const` declarations of primitive types and arrays, which always require explicit type annotations.

> 💡 **Tip:** You don't need to write the type if the compiler can figure it out, but adding it never hurts for readability. At file scope, `const` declarations of primitives and arrays always need the type.

### 3.4 Type Conversions

Explicit type conversions are performed using type constructors:

```gray
mut i int = int('A')       // 65 - char to int (code point)
mut f float = float(42)    // 42.0 - int to float
mut s string = string(123) // "123" - int to string
mut c char = char(65)      // 'A' - int to char
```

Conversions that would lose information or are invalid produce check-time or runtime errors.

#### 3.4.1 The `cast` Keyword

The `cast` keyword provides explicit type conversion for values and arrays:

```gray
mut small u8 = cast(42, u8)
mut truncated int = cast(3.7, int)     // 3
mut text string = cast(123, string)    // "123"
```

For array conversions, `cast` converts each element to the target element type:

```gray
mut ints [int] = {1, 2, 3}
mut bytes [u8] = cast(ints, [u8])
```

Range constraints are enforced at runtime (e.g., `u8` values must be 0-255).

> 💡 **Tip:** `cast` truncates floats to integers, it does not round. `cast(3.9, int)` gives `3`, not `4`.

---

## 4. Variables and Constants

### 4.1 Variable Declarations

Variables are declared using the `mut` keyword:

```gray
mut count int = 0
mut name string = "Alice"
mut items [int] = {1, 2, 3}
```

Variables declared with `mut`:
- Must be initialized at declaration
- Can be reassigned after declaration
- Are scoped to their containing block

### 4.2 Constant Declarations

Constants are declared using the `const` keyword:

```gray
const PI float = 3.14159
const MAX_SIZE int = 100
const origin = Point{x: 0, y: 0}
```

> ⚠️ At file scope, `const` declarations of primitive types (`int`, `float`, `string`, `bool`, `char`, `byte`) and arrays require explicit type annotations. Struct literals and enum values can still be inferred. Inside function bodies, type inference works for all `const` declarations.

### 4.3 Mutability

The `mut` keyword allows modification and **ensures the value itself is mutable**:

```gray
mut arr [int] = {1, 2, 3}
arr[0] = 10  // OK
arr = {4, 5, 6}  // OK
```

When assigning from a function return or other source, `mut` automatically makes the value mutable. There is no need to use `copy()` to obtain a mutable version:

```gray
do get_data() -> [int] {
    return {1, 2, 3}
}

mut arr = get_data()  // arr is mutable
arrays.append(arr, 4)  // OK - can modify directly

const fixed = get_data()  // fixed is immutable
arrays.append(fixed, 4)   // ERROR - cannot modify const
```

### 4.4 Scope

Variables and constants are block-scoped. A block is delimited by braces `{}`.

Inner scopes may declare variables that shadow outer scope variables:

```gray
mut x int = 10
if true {
    mut x int = 20  // Shadows outer x
    // x is 20 here
}
// x is 10 here
```

### 4.5 Return Value Handling

**All return values must be handled.** If a function returns a value, you must either assign it to a variable or explicitly discard it with the blank identifier `_`. Silently ignoring a return value is not permitted.

#### Fallible Functions

Some functions return a `(T, Error)` tuple; these are **fallible functions**. They require destructuring. Assigning the result to a single variable panics at runtime if the function fails; the compiler enforces destructuring to make the choice explicit:

```gray
// Correct: handle the error
mut content, err = io.read_file("data.txt")
if err != nil {
    panic(err)
}

// Correct: explicitly discard the error when you know it won't fail
mut content, _ = io.read_file("data.txt")

// Wrong: single-var assignment from a fallible function panics
mut content = io.read_file("data.txt")  // error: use destructuring for fallible functions
```

#### Blank Identifier

The blank identifier `_` discards a return value you don't need:

```gray
// Discard the second return value
mut value, _ = get_pair()

// Discard multiple values
const _, middle, _ = get_triple()

// Discard error intentionally
mut data, _ = json.decode(raw)
```

The blank identifier:
- Cannot be read from (it discards the value)
- Can be used multiple times in the same assignment
- Works with both `mut` and `const` declarations

> 💡 **Tip:** Use `_` to make it clear you are intentionally ignoring a value. It documents your intent right in the code.

---

## 5. Expressions

### 5.1 Primary Expressions

#### 5.1.1 Array Literals

```gray
{1, 2, 3}
{"a", "b", "c"}
{}   // Empty array (not to be confused with {:} which is an empty map)
```

All elements in an array literal must be the same type. Mixed-type literals are rejected at compile time:

```gray
{1, "two", 3}   // error: mixed types in array literal
```

#### 5.1.2 Map Literals

See [Section 3.2.2](#322-maps) for map literal syntax, including the `{:}` empty map literal.

#### 5.1.3 Struct Literals

```gray
Point{x: 10, y: 20}
Person{name: "Alice", age: 30, active: true}
Point{}  // Zero-initialized
```

### 5.2 Operators

#### 5.2.1 Arithmetic Operators

| Operator | Description | Operand Types | Result Type |
|----------|-------------|---------------|-------------|
| `+` | Addition | `int`, `int` | `int` |
| `+` | Addition | `float`, `float` | `float` |
| `-` | Subtraction | `int`, `int` | `int` |
| `-` | Subtraction | `float`, `float` | `float` |
| `*` | Multiplication | `int`, `int` | `int` |
| `*` | Multiplication | `float`, `float` | `float` |
| `/` | Division | `int`, `int` | `int` (truncated) |
| `/` | Division | `float`, `float` | `float` |
| `%` | Modulo | `int`, `int` | `int` |

Division by zero produces a runtime error.

#### 5.2.2 Comparison Operators

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

Comparison operators return `bool`.

Comparison operators only work on primitive types (numeric kinds, `bool`, `char`, `byte`, `string` for equality, enums) and pointer equality (`==` / `!=` against `nil` or another pointer of the same pointee type). They are not defined on aggregate types:

- Arrays — use `arrays.is_equal(a, b)` for equality.
- Maps — use `maps.is_equal(a, b)` for equality; ordering is not defined on maps.
- Structs — compare individual fields (`a.x == b.x`).
- Pointer arithmetic and ordering are not supported; equality (`==` / `!=`) on pointers is allowed.

#### 5.2.3 Logical Operators

| Operator | Description |
|----------|-------------|
| `&&` | Logical AND (short-circuit) |
| `\|\|` | Logical OR (short-circuit) |
| `!` | Logical NOT |

Logical AND and OR use short-circuit evaluation: the right operand is not evaluated if the result can be determined from the left operand alone.

#### 5.2.4 Membership Operators

| Operator | Description |
|----------|-------------|
| `in` | Membership test |
| `not_in` | Non-membership test |
| `!in` | Non-membership test (shorthand for `not_in`) |

```gray
if 3 in numbers { ... }
if "key" not_in map { ... }
if x in range(0, 10) { ... }
if 10 !in range(0, 10) { ... }  // Shorthand for not_in
```

#### 5.2.5 Assignment Operators

| Operator | Description |
|----------|-------------|
| `=` | Assignment |
| `+=` | Addition assignment |
| `-=` | Subtraction assignment |
| `*=` | Multiplication assignment |
| `/=` | Division assignment |
| `%=` | Modulo assignment |

#### 5.2.6 Increment and Decrement

| Operator | Description |
|----------|-------------|
| `++` | Post-increment |
| `--` | Post-decrement |

```gray
mut x int = 5
x++  // x is now 6
x--  // x is now 5
```

#### 5.2.7 Bitwise Operators

Grayscale uses keyword operators for bitwise operations. Symbol alternatives (`&`, `^`, `|`) are unavailable because `^` is the pointer type and dereference sigil and `&` is the address-of operator.

| Operator | Syntax | Description | Operand Types |
|----------|--------|-------------|---------------|
| `bit_and` | `a bit_and b` | Bitwise AND | `int`, `uint`, `byte`, `char`, sized integer types |
| `bit_or` | `a bit_or b` | Bitwise OR | `int`, `uint`, `byte`, `char`, sized integer types |
| `bit_xor` | `a bit_xor b` | Bitwise XOR | `int`, `uint`, `byte`, `char`, sized integer types |
| `bit_not` | `bit_not a` | Bitwise NOT (complement) | `int`, `uint`, `byte`, `char`, sized integer types |
| `bit_shift_left` | `a bit_shift_left n` | Left shift by `n` bits | `int`, `uint`, `byte`, `char`, sized integer types |
| `bit_shift_right` | `a bit_shift_right n` | Right shift by `n` bits | `int`, `uint`, `byte`, `char`, sized integer types |

`bit_not` is a prefix operator. All others are infix operators. Results have the same type as the operands.

```gray
// Basic operations
mut a int = 0b1010
mut b int = 0b1100

println(a bit_and b)          // 8  (0b1000)
println(a bit_or  b)          // 14 (0b1110)
println(a bit_xor b)          // 6  (0b0110)
println(bit_not a)            // -11 (bitwise complement)
println(1 bit_shift_left  3)  // 8
println(16 bit_shift_right 1) // 8
```

A common use is flag manipulation with named constants:

```gray
const READ  int = 0b001
const WRITE int = 0b010
const EXEC  int = 0b100

mut perms int = READ bit_or WRITE   // set READ and WRITE flags

if perms bit_and READ == READ {
    println("readable")
}
if perms bit_and EXEC == 0 {
    println("not executable")
}

perms = perms bit_xor WRITE         // clear WRITE flag
```

### 5.3 Operator Precedence

From highest to lowest precedence:

1. Parentheses: `()`
2. Prefix/Unary: `!`, `-` (negation), `bit_not`
3. Multiplicative: `*`, `/`, `%`
4. Additive: `+`, `-`
5. Shift: `bit_shift_left`, `bit_shift_right`
6. Membership: `in`, `not_in`
7. Comparison: `<`, `>`, `<=`, `>=`
8. Bitwise: `bit_and`, `bit_or`, `bit_xor`
9. Equality: `==`, `!=`
10. Logical AND: `&&`
11. Logical OR: `||`

### 5.4 Index Expressions

```gray
mut arr [int] = {10, 20, 30}
mut val int = arr[1]  // 20
arr[0] = 100  // Modification

mut str string = "hello"
mut c char = str[0]  // 'h'

mut m map[string:int] = {"a": 1}
mut v int = m["a"]  // 1
```

### 5.5 Member Expressions

```gray
mut p Point = Point{x: 10, y: 20}
mut x int = p.x  // 10
p.y = 30  // Modification

mut status int = Direction.NORTH  // Enum access
```

### 5.6 Call Expressions

```gray
mut sum int = add(1, 2)
mut greeting string = greet("World")
println("Hello!")
```

### 5.7 Range Expressions

```gray
range(0, 10)       // 0, 1, 2, ..., 9  (increment)
range(0, 10, 2)    // 0, 2, 4, 6, 8    (increment)
range(10, 0, -1)   // 10, 9, 8, ..., 1 (decrement)
range(10, 0, -2)   // 10, 8, 6, 4, 2   (decrement)
```

Ranges are inclusive of the start value and exclusive of the end value.

**Step validation rules:**
- Positive step (or omitted): start must be ≤ end
- Negative step: start must be ≥ end (for backwards iteration)
- Zero step: always produces a runtime panic
- Mismatched direction (e.g., `range(0, 10, -1)`) produces a runtime error

---

## 6. Statements

### 6.1 Expression Statements

Any expression can be used as a statement:

```gray
println("Hello")
counter++
do_something()
```

### 6.2 Conditional Statements

#### 6.2.1 If Statements

```gray
if x < 0 {
    println("negative")
} or x == 0 {
    println("zero")
} otherwise {
    println("positive")
}
```

The `or` keyword introduces additional conditions (similar to `else if` in other languages).

The `otherwise` keyword introduces the default case (similar to `else`).

`else` is an alias for `otherwise`. Both are valid, user's choice.

> 💡 **Tip:** `else` and `otherwise` are identical. Pick whichever reads more naturally to you and stick with it.

### 6.3 Loop Statements

#### 6.3.1 For Loops

```gray
for i in range(0, 10) {
    println("${i}")
}

for i int in range(0, 5) {
    // With explicit type
}

for (i in range(0, 10)) {
    // Parentheses optional
}
```

Use the blank identifier `_` to iterate by count without needing the loop variable:

```gray
for _ in range(0, 5) {
    // body runs 5 times; loop counter is discarded
}
```

`_` cannot be read inside the loop body. If you need the counter value, use a named variable instead.

#### 6.3.2 For-Each Loops

```gray
mut items [string] = {"a", "b", "c"}
for_each item in items {
    println(item)
}
```

An optional index variable can precede the value variable, separated by a comma:

```gray
for_each i, item in items {
    println("${i}: ${item}")
}
// Output: 0: a, 1: b, 2: c
```

The index variable is always of type `int` and is zero-based. It works with both arrays and strings:

```gray
for_each i, ch in "hello" {
    println("${i}: ${ch}")
}
```

The blank identifier `_` can be used in either position:

```gray
for_each _, item in items { ... }   // discard index (same as no index)
for_each i, _ in items { ... }     // index only, discard value
```

**Map iteration** is also supported. With two variables, the first is the key and the second is the value:

```gray
mut ages map[string:int] = {"alice": 30, "bob": 25}
for_each k, v in ages {
    println("${k}: ${v}")
}

// Single variable iterates keys only
for_each key in ages {
    println(key)
}
```

Map iteration order is undefined (maps are unordered).

> 💡 **Tip:** Never rely on map iteration order. It is different every run by design.

**Mutation during iteration:**

- **Arrays:** The loop length is captured when `for_each` begins. Appending to the array during iteration is safe; new elements are added to the array but are not visited by the current loop. The full array (including appended elements) is available after the loop ends.
- **Maps:** Modifying a map during `for_each` (inserting or deleting keys) is not allowed and will panic at runtime. Read the map freely, but do not mutate it until the loop completes.

> 💡 **Tip:** You can iterate over an inline array literal directly — no variable needed:
> ```gray
> for_each item in {"a", "b", "c"} {
>     println(item)
> }
> ```
> This works with any element type and supports the index form (`for_each i, item in {...}`) as well.

#### 6.3.3 While Loops

`while` is an alias for `as_long_as`. Both are valid, user's choice.

> 💡 **Tip:** `while` and `as_long_as` are identical. Pick whichever reads more naturally to you and stick with it.

```gray
mut count int = 0
as_long_as count < 10 {
    count++
}

// Equivalent:
while count < 10 {
    count++
}
```

#### 6.3.4 Infinite Loops

The `loop` statement creates an infinite loop that runs until explicitly terminated with `break` or `return`:

```gray
loop {
    mut input string = input()
    if input == "quit" {
        break
    }
    println("You said: ${input}")
}
```

### 6.4 Control Flow Statements

#### 6.4.1 Break

The `break` statement terminates the innermost enclosing loop:

```gray
for i in range(0, 100) {
    if i == 5 {
        break
    }
}
```

#### 6.4.2 Continue

The `continue` statement skips to the next iteration of the innermost enclosing loop:

```gray
for i in range(0, 10) {
    if i % 2 == 0 {
        continue
    }
    println("${i}")  // Prints odd numbers only
}
```

#### 6.4.3 Return

The `return` statement exits the current function, optionally returning a value:

```gray
do add(a int, b int) -> int {
    return a + b
}

do greet() {
    println("Hello")
    return  // Void return
}
```

### 6.5 When Statements (Pattern Matching)

```gray
when x {
    is 1 { println("one") }
    is 2, 3 { println("two or three") }
    is range(4, 10) { println("four to nine") }
    default { println("other") }
}
```

**Allowed condition types:** `int`, `uint`, `string`, `char`, `byte`, `bool`, `float`, and enum types. Float conditions emit a warning about imprecision. Collection types (arrays, maps) are not allowed.

**Strict mode** requires all possible values to be handled:

```gray
#strict
when direction {
    is Direction.NORTH { ... }
    is Direction.EAST { ... }
    is Direction.SOUTH { ... }
    is Direction.WEST { ... }
}
```

When a `when` statement matches on enum values (i.e. one or more `is` branches use `EnumName.VARIANT` patterns) and has no `default` branch, the compiler warns if `#strict` is not present. This warns that exhaustiveness is not being checked. The fix is to either add `#strict` to enforce exhaustive coverage or add a `default` branch. This applies at any nesting depth.

An empty `default {}` branch emits a warning. Unmatched values are silently ignored, which is almost never intentional. Either handle the case or add a comment explaining the intent.

### 6.6 Ensure Statement

The `ensure` statement specifies a function to call when the enclosing function exits (whether normally or via early return):

```gray
do process_file() {
    ensure cleanup()
    // ... do work ...
    if error_condition {
        return  // cleanup() will be called
    }
    // cleanup() will be called when function ends
}
```

### 6.7 Or-Return Statement

The `or_return` keyword provides error propagation shorthand for functions that return `(T, Error)` tuples:

```gray
do load() -> (string, Error) {
    // Bare or_return: propagates the error with zero values
    mut content = read_file("data.txt") or_return
    mut parsed = json.decode(content) or_return
    return parsed, nil
}

// With custom fallback values:
mut content = read_file("data.txt") or_return "", error("failed to load")
```

When the call returns a non-nil error, `or_return` immediately returns from the enclosing function. The bare form returns zero values for non-error slots plus the original error. The enclosing function must have `Error` as its last return type.

---

## 7. Functions

### 7.1 Function Declarations

```gray
do add(a int, b int) -> int {
    return a + b
}

do greet(name string = "World") -> string {
    return "Hello, ${name}!"
}

do process() {
    // Void function (no return type)
}
```

### 7.2 Parameters

#### 7.2.1 Immutable Parameters

By default, parameters are passed by value and cannot modify the caller's variables:

```gray
do double(x int) -> int {
    return x * 2
}

#### 7.2.2 Mutable Parameters

Mutable parameters work with:
- Primitive types
- Struct fields: `increment(point.x)`
- Array elements: `increment(arr[0])`
- Map values: `increment(map["key"])`

#### 7.2.3 Grouped Parameters

Multiple parameters of the same type can be grouped:

```gray
do add(a, b int) -> int {
    return a + b
}

do swap(&a, &b int) {
    mut t int = a
    a = b
    b = t
}
```

#### 7.2.4 Default Parameters

Parameters can have default values. Default values are supported for all primitive types (`int`, `uint`, `float`, `string`, `bool`, `char`):

```gray
do greet(name string = "World") -> string {
    return "Hello, ${name}!"
}

greet()         // "Hello, World!"
greet("Alice")  // "Hello, Alice!"
```

Multiple parameters may have defaults. When calling, arguments fill left-to-right and any remaining parameters use their defaults:

```gray
do connect(host string, port int = 8080, verbose bool = false) {
    if verbose {
        println("Connecting to ${host}:${port}")
    }
}

connect("localhost")              // port=8080, verbose=false
connect("localhost", 3000)        // port=3000, verbose=false
connect("localhost", 3000, true)  // port=3000, verbose=true
```

Default parameters must appear after non-default parameters. A required parameter cannot follow a parameter with a default value:

```gray
do foo(a int = 10, b int) {}   // error: required parameter cannot follow a default parameter
do bar(a int, b int = 10) {}   // OK: required first, then default
```

#### 7.2.5 Named Arguments

When calling a function, arguments can be passed by name using `name: value` syntax. This lets callers provide arguments in any order and skip over defaulted parameters to target specific ones:

```gray
do connect(host string, port int = 8080, verbose bool = false) {
    if verbose {
        println("Connecting to ${host}:${port}")
    }
}

connect(host: "localhost", verbose: true)  // port uses default 8080
connect(verbose: true, host: "localhost")  // same — order doesn't matter
connect("localhost", verbose: true)        // positional + named mix
```

**Rules:**

- Positional arguments must come before named arguments. Once a named argument appears, all remaining arguments must also be named:

```gray
add(1, b: 2)     // OK: positional first, then named
add(a: 1, 2)     // error: positional argument after named argument
```

- Named arguments must match a parameter name in the function signature exactly. Unknown names are rejected:

```gray
do add(a int, b int) -> int { return a + b }

add(a: 1, c: 2)  // error: unknown parameter name 'c' in call to 'add'
```

- A parameter cannot be provided both positionally and by name:

```gray
add(1, a: 2)      // error: parameter 'a' is already provided positionally
```

- Named arguments work with struct functions. For instance dispatch, the self parameter is implicit — only name the non-self parameters:

```gray
const Vec struct {
    x int
    y int

    do scale(self Vec, factor int) -> Vec {
        return Vec{x: self.x * factor, y: self.y * factor}
    }
}

mut v = Vec{x: 2, y: 3}
mut scaled = v.scale(factor: 5)        // instance dispatch: name non-self params
mut also = Vec.scale(self: v, factor: 5)  // static dispatch: name all params
```

- Named arguments are **not supported** for built-in functions (`println`, `len`, `cast`, etc.) or standard library module functions (`strings.to_upper`, `math.sqrt`, etc.):

```gray
println(value: "hello")           // error: named arguments not supported
strings.to_upper(s: "hello")      // error: named arguments not supported
```

### 7.3 Return Types

#### 7.3.1 Single Return Value

```gray
do square(x int) -> int {
    return x * x
}
```

> 💡 **Tip:** If a function returns `^Type`, you can dereference at the call site with `^` to get the value directly without storing the pointer:
> ```gray
> do something() -> ^Foo {
>     mut f = new(Foo)
>     return f
> }
>
> do main() {
>     mut f = something()^   // dereference at call site, f is Foo, not ^Foo
>     println(f)
> }
> ```

#### 7.3.2 Multiple Return Values

```gray
do divide(a, b int) -> (int, int) {
    return a / b, a % b
}

mut quotient, remainder = divide(17, 5)
```

#### 7.3.3 Error Returns

```gray
do parse(s string) -> (int, Error) {
    if s == "" {
        return 0, error("empty string")
    }
    return 42, nil
}

mut value, err = parse("test")
if err != nil {
    // Handle error
}
```

#### 7.3.4 Named Return Values

Return values can be given names to document what each position in the return tuple represents. Named return values are **labels only**; they do not implicitly declare variables in the function body. The programmer must explicitly declare any variables they use:

```gray
do divide(a, b int) -> (quotient int, remainder int) {
    mut quotient int = a / b
    mut remainder int = a % b
    return quotient, remainder
}

mut q, r = divide(17, 5)  // q=3, r=2
```

Named returns support grouped types (multiple names sharing one type):

```gray
do get_info() -> (name, city string, age int) {
    mut name string = "Alice"
    mut city string = "NYC"
    mut age int = 30
    return name, city, age
}
```

Named return values must be enclosed in parentheses. The names serve as documentation for callers and tooling (e.g., `gray doc`) but have no effect on the function's scope or variable declarations.

**Restriction:** Wildcard types (`?`) cannot be used in named return positions. Since `?` resolves to a different concrete type at each call site, the name adds no useful documentation. Use an unnamed return instead:

```gray
// Error: wildcard type '?' cannot be named
do first(arr [?]) -> (result ?) { ... }

// OK: unnamed wildcard return
do first(arr [?]) -> ? { ... }
```

#### 7.3.5 Void Functions

Functions without a return type return no value:

```gray
do print_greeting() {
    println("Hello!")
}
```

### 7.4 Visibility

By default, all functions and constants are public. The `private` keyword restricts access to the declaring module:

```gray
// mathlib/mathlib.gray — module name comes from directory

private const MAX_ITERATIONS int = 1000

private do validate(n int) -> bool {
    return n > 0
}

do factorial(n int) -> int {
    // Can call private members within the same module
    if !validate(n) { return 1 }
    // ...
}
```

Private members cannot be accessed from other modules:

```gray
import "./mathlib"
mathlib.factorial(5)          // OK - public
// mathlib.validate(5)        // error: private function
// mathlib.MAX_ITERATIONS     // error: private constant
```

### 7.5 Attributes

Attributes are annotations prefixed with `#` that modify declaration behavior. Attributes are placed on the line(s) immediately before a declaration, one attribute per line:

```gray
#doc("A person with a name and age")
#json
const Person struct {
    name string
    age int
}
```

**Rules:**

- One attribute per line, stacked before the declaration. Same-line multi-attribute (`#doc("x") #json`) is not supported.
- Order is irrelevant. `#doc` then `#json` and `#json` then `#doc` produce identical results.
- Blank lines between attributes and the declaration are allowed.
- Each attribute applies to the immediately following declaration only. It does not skip ahead to find a compatible declaration further down the file.
- Misapplied attributes are rejected. For example, `#json` on a function produces an error; `#json` can only be applied to struct declarations.

#### Available Attributes

| Attribute | Applies To | Description |
|-----------|------------|-------------|
| `#doc("...")` | functions, structs, enums | Documentation metadata, used by `gray doc` |
| `#json` | structs | Enables JSON serialization for the struct |
| `#flags` | enums | Marks enum as a bitflag set (values are powers of 2) |
| `#strict` | `when` blocks | Requires all enum variants to be handled |

#### 7.5.1 `#doc` Attribute

The `#doc` attribute adds documentation metadata to functions, structs, and enums. Used by the `gray doc` command to generate documentation.

```gray
#doc("Adds two integers and returns the sum")
do add(a int, b int) -> int {
    return a + b
}

#doc("Represents a 2D point")
const Point struct {
    x int
    y int
}
```

#### 7.5.2 `#json` Attribute

The `#json` attribute marks a struct for JSON serialization and deserialization. The compiler generates all marshaling and unmarshaling code automatically, with no field tags, no manual encoding/decoding calls, and no error juggling at every step. Just annotate the struct and use `json.parse()` / `json.stringify()`.

```gray
import @json

#json
const User struct {
    name string
    age int
    active bool
}

do main() {
    // Parse a JSON string directly into a typed struct
    mut u User = json.parse("{\"name\": \"Alice\", \"age\": 25, \"active\": true}")
    println(u.name)            // Alice

    // Serialize back to JSON, fields are mapped automatically
    println(json.stringify(u)) // {"name":"Alice","age":25,"active":true}
}
```

`json.parse()` returns a fully typed struct (or array of structs), and `json.stringify()` accepts any `#json` struct and returns a string. The compiler knows the struct layout at compile time, so it generates field-by-field serialization code directly with no reflection, no runtime schema lookup, and no intermediate map step.

**Rules:**

- Field names in the JSON must match the struct field names exactly.
- Without `#json`, the struct has no serialization machinery and `json.parse()` / `json.stringify()` will fail.
- Supported field types: `int`, `uint`, `float`, `string`, `bool`. Nested `#json` structs and arrays of `#json` structs are also supported.

### 7.6 Function References

A function reference is a value that holds a pointer to a named function. Function references are created with `()` prefix syntax or `ref()` and must always be bound to a `const`:

```gray
do double(n int) -> int { return n * 2 }

// ()func_name: implicit syntax (type is inferred)
const f = ()double

// ref(func_name): explicit syntax (identical result)
const g = ref(double)

// Optional explicit type annotation
const h func(int) -> int = ()double
```

`mut` is rejected for func reference variables; func references are compile-time aliases, not mutable state.

#### 7.6.1 Calling Through a Reference

Call a func reference variable the same way you call a function:

```gray
const f = ()double
f(5)          // 10, call through variable
()double(5)   // 10, inline: create reference and call immediately
```

`()f` and `()f(5)` are always rejected. The `()` prefix means "create a reference to the named function declaration", not "dispatch through a variable `f`". Since `f` is a variable (not a function declaration), both forms are compile errors. `f(5)` is the only valid dispatch syntax for a func reference variable.

#### 7.6.2 Func as a Parameter Type

When a function accepts another function as an argument, declare the parameter with a full typed `func` signature. This enables the compiler to check argument and return types at call sites:

```gray
// Single param
do apply(x int, f func(int) -> int) -> int {
    return f(x)
}

// Multiple params
do combine(a int, b string, f func(int, string) -> bool) -> bool {
    return f(a, b)
}

// No params
do run(f func() -> int) -> int {
    return f()
}

// No return value
do each(arr [int], f func(int)) {
    for_each v in arr { f(v) }
}

do double(n int) -> int { return n * 2 }
do main() {
    println(apply(5, ()double))    // 10
    println(apply(5, ref(double))) // 10, ref() is equivalent
    const f = ()double
    println(apply(5, f))           // 10, pass a variable
}
```

Inside the function body, call through the parameter the same way: `f(x)`.

#### 7.6.3 Func References in Composite Types

Bare `func` is a valid type in arrays and maps. Elements are untyped function pointers; the cast is reconstructed from context at each call site:

```gray
import @arrays

do double(n int) -> int { return n * 2 }
do triple(n int) -> int { return n * 3 }

// Dynamic array of func refs
mut arr [func] = {}
arrays.append(arr, ()double)
arr[0](5)   // 10

// Fixed-size array of func refs
const fns [func, 2] = {()double, ()triple}
fns[0](5)   // 10
fns[1](5)   // 15

// Map with func values
mut m map[string:func] = {:}
m["dbl"]  = ()double
m["trpl"] = ()triple
m["dbl"](5)   // 10
m["trpl"](5)  // 15
```

Typed func signatures as an array element type (e.g. `[func(int)->int]`) are not allowed. Use `[func]` or `[func, N]` instead.

#### 7.6.4 Func Fields in Structs

Struct fields can hold func references. A typed `func` signature is required for struct field types — bare `func` is not allowed. This ensures compile-time argument checking:

```gray
const Wrapper struct {
    f func(int) -> int
}

do double(n int) -> int { return n * 2 }
do main() {
    // Struct literal
    const w = Wrapper{f: ()double}
    w.f(5)   // 10

    // Pointer instance (new())
    mut w2 = new(Wrapper)
    w2.f = ()double
    w2.f(5)  // 10
}
```

#### 7.6.5 Comparing Func References

Func references compare by pointer equality. Two references to the same function are equal; references to different functions are not:

```gray
const f = ()double
const g = ()double
const h = ()triple
if f == g { println("same") }       // same, both point to double
if f != h { println("different") }  // different
```

#### 7.6.6 Restrictions

> 💡 **Tip:** `()` and `ref()` only work with functions you declared with `do`. Built-in functions (`println`, `panic`, `copy`, etc.) and stdlib functions (`arrays.append`, `strings.contains`, etc.) cannot be turned into func references. Wrap them in your own `do` function if you need to pass them as a callback.

| Operation | Result |
|-----------|--------|
| `mut f = ()double` | ❌ must use `const` |
| `println(f)` | ❌ func refs are not printable |
| `copy(f)` | ❌ func refs cannot be copied |
| `const f = get_fn()` | ❌ cannot assign func-type return value; use `()func_name` |
| `get_fn()(5)` | ❌ cannot call a function's return value directly |
| `[func(int)->int]` | ❌ typed func signature as array type; use `[func]` or `[func, N]` |
| `()println` / `ref(println)` | ❌ builtin and stdlib functions cannot be referenced |
| `()f` (f is a variable) | ❌ `()` only works with named function declarations, not variables |
| `()f(5)` (f is a variable) | ❌ same restriction; `f(5)` is the only valid call syntax |

Rules:
- No anonymous functions or lambdas; every reference points to a named function declaration
- `const` only — func references cannot be declared `mut`
- The typed signature (e.g. `func(int) -> int`) must match exactly; param types and return type must all agree
- Each parameter in a `func` signature is listed as its own type: `func(int, string) -> bool`. Grouped-type shorthand is not supported inside `func` signatures
- Default parameter values inside `func` signatures are not supported
- References work with top-level and struct-namespaced functions

### 7.7 Struct-Namespaced Functions

Functions can be declared inside struct blocks as namespaced free functions:

```gray
const Point struct {
    x int
    y int

    do create(x int, y int) -> Point {
        return Point{x: x, y: y}
    }

    do distance(a Point, b Point) -> float {
        return math.sqrt(math.pow(float(a.x - b.x), 2) + math.pow(float(a.y - b.y), 2))
    }

    private do validate(p Point) -> bool {
        return p.x >= 0 && p.y >= 0
    }
}

// Called as Type.func()
mut p = Point.create(3, 4)
mut d = Point.distance(p1, p2)
```

Rules:
- No implicit `self` or `this` — every parameter is explicit
- `private` restricts access to other functions in the same struct
- Called as `StructName.func_name(args...)`
- Cross-module: `module.StructName.func_name(args...)`
- Module-qualified types can be used in variable declarations, parameters, and return types: `mut p module.Point`

#### Instance Dispatch

When a struct function takes the struct (or a pointer to it) as its first parameter, callers can use the instance form `instance.func(...)` instead of writing the type name. The compiler rewrites the call as `Type.func(instance, ...)`:

```gray
const Vec struct {
    x int
    y int

    do len_sq(v Vec) -> int {
        return v.x * v.x + v.y * v.y
    }

    do bump(&v Vec) {
        v.x = v.x + 1
        v.y = v.y + 1
    }
}

mut a Vec = Vec{x: 3, y: 4}
a.len_sq()         // sugar for Vec.len_sq(a)
Vec.len_sq(a)      // still valid

a.bump()           // sugar for Vec.bump(a); '&v' makes it a mutable alias
```

Both `do f(v Vec)` and `do f(&v Vec)` (mutable receiver) and `do f(v ^Vec)` (pointer receiver) participate in instance dispatch. The mutable-receiver form (`&v`) takes the instance by reference and may modify the caller's variable.

Factory-style functions whose first parameter isn't the struct (e.g. `do make(x int) -> Vec`) keep requiring the type-namespaced form (`Vec.make(...)`); there is no instance to bind.

Chained struct function calls (`a.f().g()`) are not supported. Assign each intermediate result to a variable.

### 7.8 Function Scope

All functions in Grayscale are declared at the top level or inside struct blocks. Nested function declarations inside other functions are not permitted. Anonymous functions (lambdas/closures) are not supported.

Storing a reference to an existing function in a local variable is not the same as declaring a function and is perfectly valid:

```gray
do double(n int) -> int { return n * 2 }

do main() {
    const f func(int) -> int = ()double  // valid: f is a variable, not a function declaration
    println(f(5))                         // 10
}
```

### 7.9 Wildcard Types (`?`)

The `?` type is a wildcard placeholder that enables generic-style functions. When used in a function's parameter types, `?` is bound to the concrete type of the argument at each call site. The return type can also use `?` to propagate the bound type.

```gray
do identity(x ?) -> ? {
    return x
}

mut a = identity(42)        // ? binds to int, returns int
mut b = identity("hello")   // ? binds to string, returns string
```

All `?` placeholders in a function signature bind to the same concrete type:

```gray
do pick_first(a ?, b ?) -> ? {
    return a
}

pick_first(1, 2)          // OK, both args are int, ? binds to int
pick_first(1, "hello")    // Error: conflicting bindings for ?
```

Wildcard types also work with composite types in parameters and returns:

```gray
do first(arr [?]) -> ? {
    return arr[0]
}

mut x = first({1, 2, 3})      // ? binds to int
mut y = first({"a", "b"})     // ? binds to string
```

#### Where `?` is allowed

`?` is **only** valid in function parameter types and return types. It is rejected everywhere else:

| Usage | Result |
|-------|--------|
| Function parameter type | Allowed |
| Function return type | Allowed (must have at least one `?` parameter) |
| Variable declaration (`mut x ?`) | Rejected |
| Struct field type | Rejected |
| Array type in variable (`[?]`) | Rejected |
| Map type in variable (`map[string:?]`) | Rejected |
| `new(?)` | Rejected |
| Named return type (`-> (name ?)`) | Rejected; use unnamed `-> (?)` instead |

#### Binding rules

- The concrete type is inferred from the first argument that corresponds to a `?` parameter
- All subsequent `?` parameters and the return type must be consistent with that binding
- If the return type uses `?`, at least one parameter must also use `?` to provide the binding

### 7.10 Type Parameters (`<?>`)

The `<?>` annotation allows a function parameter to accept a struct type name rather than a value. This enables reusable constructors and type-aware utility functions.

```gray
const Point struct {
    x int
    y int
}

do make(T <?>) -> ^? {
    return new(T)
}

mut p = make(Point)    // allocates a new Point, returns ^Point
```

The type parameter `T` is resolved at each call site using the same monomorphization pipeline as value wildcards (`?`). The compiler generates a specialized function for each concrete type used.

#### Where `T` can be used inside the function body

A type parameter name is valid in these positions:

| Usage | Example | Result |
|-------|---------|--------|
| `new(T)` | `new(T)` | Heap-allocates an instance of `T` |
| Struct literal | `T{x: 1, y: 2}` | Constructs a stack instance of `T` |
| `size_of(T)` | `size_of(T)` | Returns the size of `T` in bytes |

#### Return type inference

The return type uses `?` the same way as value wildcards. `-> ^?` resolves to a pointer to the type argument, `-> ?` resolves to the type argument itself:

```gray
do make(T <?>) -> ^? {
    return new(T)
}

do make_stack(T <?>) -> ? {
    return T{}
}

mut p = make(Point)          // -> ^Point
mut s = make_stack(Point)    // -> Point
```

#### Restrictions

**No mixing type and value parameters (E2087):**

Type parameters and value parameters cannot appear in the same function signature:

```gray
do bad(T <?>, x int) -> ^? {     // Error E2087
    return new(T)
}
```

**Only struct types allowed (E3127, E3128):**

Only struct type names may be passed as type arguments. Enums, primitives, and expressions are rejected:

```gray
const Color enum { RED, GREEN, BLUE }

mut p = make(Point)    // OK — Point is a struct
mut c = make(Color)    // Error E3127 — Color is not a struct
mut x = make(int)      // Error E3127 — int is not a struct
mut y = make(1 + 2)    // Error E3128 — not a type name
```

---

## 8. Modules

### 8.1 Module Identity

Module identity is determined by the filesystem; there are no `module` declarations. A file's module name is its filename minus the `.gray` extension. A directory's module name is its directory name.

```
project/
  main.gray              ← entry point
  helpers.gray           ← module "helpers"
  utils.gray             ← module "utils"
  models/
    user.gray            ← ┐
    task.gray            ← ┘ merge into module "models"
    internal/
      cache.gray         ← separate module "internal"
```

Directory imports merge all top-level `.gray` files in that directory into a single namespace under the directory's name. Subdirectories are **not** included; they are separate modules that must be imported independently. Hidden files (names starting with `.`) are excluded from directory scans.

All relative import paths are resolved relative to the **entry point file's directory**, not the importing file's directory. This means a file inside `models/` that uses `import "./shared.gray"` resolves relative to the project root (where `main.gray` lives), not relative to `models/`.

### 8.2 Imports

**Standard library imports** are prefixed with `@`:

```gray
import @arrays, @maps, @strings
```

**Local imports** use relative string paths. The compiler resolves them in order:

1. If the path ends in `.gray`, import that file directly.
2. If the path has no extension, try appending `.gray`. If a file exists, import it.
3. If the path (without extension) is a directory, scan it for all `.gray` files and merge them into one module.
4. If none of the above match, the import is rejected as unresolvable.

```gray
import "./helpers.gray"      // explicit file import
import "./helpers"          // resolves to helpers.gray (file) or helpers/ (directory)
import "./models"           // models/ directory, all .gray files merge
```

When both `helpers.gray` and a `helpers/` directory exist, the file takes priority.

If a directory contains no `.gray` files, it is an error.

**Import aliasing:**

```gray
import m @math              // use m.sqrt() instead of math.sqrt()
import mymod "./server"     // use mymod.handle() instead of server.handle()
```

**Multiple imports** can be comma-separated:

```gray
import @math, "./helpers", "./models"
```

**Collision detection:** If two different imports resolve to the same module name, it is an error. The user must alias one to disambiguate:

```gray
// Error: both resolve to module name "utils"
import "./utils", "./lib/utils"

// Fix: alias one
import "./utils", lib_utils "./lib/utils"
```

**Directory import semantics:**

When a directory is imported, all `.gray` files within it are merged into a single module namespace (the directory name). The following rules apply:

- Files within the directory may import each other via relative paths (e.g., `import "./types.gray"`). These sibling cross-references are resolved internally and do not create separate namespaces; all symbols remain under the directory's namespace.
- Transitive imports inside directory files resolve relative to the importing file's location, not the entry file.
- If a file inside a directory imports its own parent directory (self-referential import), it is rejected.
- If a directory import is followed by a direct import of a file already in that directory, the compiler warns that the import is redundant; the directory namespace should be used instead.
- If two files in a directory declare the same symbol name, it is a collision error.

**Deduplication:** If the same file is imported multiple times (e.g., directly by main and transitively through another import), it is only processed once. No error is emitted.

### 8.3 Combined Import and Use

The `import and use` syntax combines importing and using in a single statement:

```gray
import and use @arrays
```

This is equivalent to:

```gray
import @arrays
using arrays
```

Multiple modules can be combined:

```gray
import and use @arrays, @strings
```

### 8.4 Using Declaration

The `using` declaration brings module members into scope for unqualified access. It can be placed at file scope or function scope:

**File scope:** all functions in the file can use unqualified access:

```gray
import @strings
using strings

do main() {
    println(to_upper("hello"))  // OK, strings is in file scope
}

do shout(s string) -> string {
    return to_upper(s)          // OK, same file scope
}
```

**Function scope:** only that function can use unqualified access:

```gray
import @strings

do main() {
    using strings
    println(to_upper("hello"))  // OK, strings is in scope here
}

do shout(s string) -> string {
    return strings.to_upper(s)  // must qualify; using is not in scope here
}
```

Multiple modules can be listed:

```gray
using arrays, strings
```

### 8.5 Module Member Access

Without `using`, module members are accessed with dot notation:

```gray
import @math
math.sqrt(16.0)
```

With `using`:

```gray
import @math
using math
sqrt(16.0)
```

### 8.6 C Interop

Grayscale can import C headers and call C functions directly using the `c` prefix:

#### Importing C Headers

```gray
import c"stdio.h"           // system header → #include <stdio.h>
import c"./mylib.h"         // local header  → #include "./mylib.h"
```

System headers (no `./` or `../` prefix) emit angle-bracket includes. Local headers emit quoted includes. Multiple C imports can be comma-separated:

```gray
import c"stdio.h", c"stdlib.h", c"string.h"
```

C imports can be mixed with Grayscale imports on separate lines:

```gray
import @math
import c"stdio.h"
```

#### Calling C Functions

All C functions are accessed via the `c.` prefix:

```gray
import c"stdio.h"

do main() {
    c.puts("hello from C")
    c.printf("value: %d\n", 42)
}
```

#### Accessing C Constants and Macros

C constants and macros are accessed with the same `c.` prefix:

```gray
import c"stdio.h"

do main() {
    println(c.EOF)              // -1
    println(c.EXIT_SUCCESS)     // 0
}
```

#### Type Mapping

| Grayscale type | C type | Notes |
|---|---|---|
| `int` | `int64_t` | Use `i32` for C `int` |
| `uint` | `uint64_t` | Use `u32` for C `unsigned int` |
| `i8`, `i16`, `i32`, `i64` | `int8_t`, `int16_t`, `int32_t`, `int64_t` | Exact match |
| `u8`, `u16`, `u32`, `u64` | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` | Exact match |
| `float` | `double` | Use `f32` for C `float` |
| `bool` | `bool` | Exact match |
| `byte` | `uint8_t` | Exact match |
| `char` | `int32_t` | Grayscale uses 32-bit for Unicode |
| `string` | `char*` | Auto-converted when passed to C functions |
| `^T` | `T*` | Direct pointer mapping |

**String conversion:** Grayscale strings are automatically converted to `char*` when passed to C functions. To convert a C `char*` return value back to a Grayscale string, use the `c_string()` builtin:

```gray
import c"stdlib.h"

do main() {
    mut home string = c_string(c.getenv("HOME"))
    println(home)
}
```

**Return types:** C function return types are inferred by the C compiler. If Grayscale needs to know the type (e.g., for `println`), add a type annotation:

```gray
import c"math.h"

do main() {
    mut x float = c.sqrt(2.0)    // type annotation needed
    println(x)                    // prints 1.4142135623730951
}
```

#### Restrictions

The following Grayscale types cannot be passed to C functions:

- `i128`, `i256`, `u128`, `u256` — C has no 128/256-bit integer types
- Arrays and maps — Grayscale-specific types with no C equivalent
- Grayscale structs — pass individual fields instead

C structs returned from C functions can be passed back to other C functions via `__auto_type` inference.

#### Reserved Name

The module name `c` is reserved for C interop. Files named `c.gray` must use an explicit alias:

```gray
import myc "./c.gray"    // OK, aliased
import "./c.gray"         // Error: 'c' is reserved
```

---

## 9. Standard Library

The Grayscale standard library consists of 27 modules plus built-in functions that require no import.

### 9.1 Built-in Functions

Built-in functions are always available without importing any module.

#### Output Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `println` | `(value T)` | Print value with newline. Accepts any type. |
| `print` | `(value T)` | Print value without newline. Accepts any type. |
| `eprintln` | `(value T)` | Print to stderr with newline. Accepts any type. |
| `eprint` | `(value T)` | Print to stderr without newline. Accepts any type. |

All types are printable: `string`, `int`, `float`, `bool`, arrays, maps, structs, and pointers.

#### Input Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `input` | `() -> string` | Read line from stdin |

#### Wide Integer Conversions

| Function | Description |
|----------|-------------|
| `i128`, `i256` | Convert to wide signed integers (struct-based, 128/256-bit) |
| `u128`, `u256` | Convert to wide unsigned integers (struct-based, 128/256-bit) |

#### Utility Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `(collection) -> int` | Length of array, map, or string (byte length for strings, not character count) |
| `type_of` | `(value T) -> string` | Returns the Grayscale type name as a string (e.g. `"int"`, `"uint"`, `"float"`, `"string"`, `"i128"`, `"u256"`). Accepts any type. |
| `size_of` | `(Type) -> int` | Size of type in bytes |
| `copy` | `(value T) -> T` | Create deep copy. Accepts any type. |
| `new` | `(Type) -> ^Type` | Allocate zero-initialized struct on arena |
| `ref` | `(variable T) -> ref<T>` | Create a transparent reference (alias) to a variable. Reads and writes through the reference affect the original. Mutability is determined by the declaration (`mut` or `const`). |
| `addr` | `(variable) -> ^T` | Get memory address of a variable |
| `error` | `(message string) -> Error` | Create error value |
| `assert` | `(condition bool [, message string])` | Terminate with `P0075` if condition is false. Message is optional. |
| `panic` | `(message string)` | Terminate with error message |
| `exit` | `(code int)` | Exit program with code |
| `range` | `(start int, end int [, step int]) -> Range` | Create integer range |
| `cast` | `(value T, Type) -> Type` | Explicit type conversion |
| `to_char` | `(s string, index int) -> int` | Return the Unicode codepoint at character position `index` (not byte position). Panics if index is out of bounds. |
| `char_count` | `(s string) -> int` | Return the number of Unicode characters (codepoints) in a string. Unlike `len()`, which returns byte count, `char_count()` counts decoded UTF-8 characters. |
| `c_string` | `(ptr ^u8) -> string` | Convert a C `char*` return value to a Grayscale string (for C interop) |
| `embed` | `(path string) -> string` | Read a file at compile time and return its contents as a string literal baked into the binary |

**Reference behavior with `ref()`:**

The `ref()` function creates a reference to an existing value. The mutability of the reference depends on the variable declaration:

```gray
mut arr [int] = {1, 2, 3}

// mut ref is mutable - can modify through the reference
mut r1 = ref(arr)
arrays.append(r1, 4)  // OK - modifies arr

// const ref is read-only - can read but not modify
const r2 = ref(arr)
mut val = r2[0]      // OK - can read
arrays.append(r2, 5)  // ERROR - cannot modify through const ref

// const ref sees changes made to the original
arrays.append(arr, 6)
println(r2[4])        // Prints 6 - r2 sees the change
```

**Mutability rules:**

| Reference declaration | Source | Allowed? |
|-----------------------|--------|----------|
| `mut r = ref(x)`      | `mut`   | yes |
| `const r = ref(x)`    | `mut`   | yes, read-only view of a mutable source |
| `const r = ref(x)`    | `const` | yes |
| `mut r = ref(x)`      | `const` | **no**; you cannot get a mutable reference to a const source. Use `copy(x)` to obtain an independent mutable instance. |

**Argument requirement:** `ref()` requires a variable, struct field, array index, or pointer dereference; anything with a stable address. Literals, call results, and arithmetic expressions are rejected. The same rule applies to `addr()`, and the check recurses through member/index chains, so `ref(some_call().field)` and `addr(arr[0])` are validated end-to-end.

**`assert()` — runtime assertion**

`assert()` checks a condition at runtime. If the condition is `false`, the program terminates immediately with error code `P0075` and prints `"panic[P0075]: assertion failed"` to stderr. An optional second argument provides a message appended to the output.

```gray
assert(x > 0, "x must be positive")
assert(len(items) > 0, "list cannot be empty")
assert(connected)  // message is optional
```

`assert()` is a global builtin — no import required.

**Rules:**
- The condition must be a `bool`. Passing a non-bool is a compile-time error (E3001).
- The optional message must be a `string`. Passing any other type is a compile-time error (E3001).
- If the condition is `true`, the program continues normally. `assert()` has no return value.

**Runtime error code:** `P0075`

#### Sleep Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sleep_s` | `(seconds int)` | Sleep for seconds |
| `sleep_ms` | `(ms int)` | Sleep for milliseconds |
| `sleep_ns` | `(ns int)` | Sleep for nanoseconds |

#### Compile-time Functions

**`embed(path string) -> string`**

`embed()` reads a file from disk at **compile time** and bakes its entire contents into the binary as a string literal. The resulting value is available as a `string` at runtime with no file I/O overhead.

The path is resolved relative to the directory of the source file containing the `embed()` call. Absolute paths are also accepted. The argument must be a string literal; variables and expressions are rejected at compile time. If the file does not exist or cannot be read when the compiler runs, it is a compile-time error.

`embed()` is valid at file scope (as a `const` initializer) or inside a function body.

```gray
// Embed a file at file scope, baked into the binary at compile time
const LICENSE string = embed("../../LICENSE")
const DEFAULT_CONFIG string = embed("config/defaults.json")

do main() {
    // Also valid inside a function
    const shader string = embed("shaders/vertex.glsl")
    println(LICENSE)
}
```

> 💡 **Tip:** The embedded file is read once during compilation. Changes to the file after compilation have no effect on the binary. The compiler resolves the path relative to the `.gray` source file, not the current working directory when running `grayc`.

### 9.2 Arrays Module (`@arrays`)

#### Query Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_empty` | `(arr [T]) -> bool` | Check if array is empty |
| `contains` | `(arr [T], value T) -> bool` | Check if value exists |
| `index_of` | `(arr [T], value T) -> int` | First index of value (-1 if not found) |
| `count` | `(arr [T], value T) -> int` | Count occurrences of value |
| `is_equal` | `(a [T], b [T]) -> bool` | Structural equality. Compares length first, then elements. `T` must be a primitive (`int`, `uint`, `float`, `bool`, `char`, `byte`, sized variants) or `string`; arrays of nested composites are rejected at compile time. |

The `==` and `!=` operators on arrays are not allowed; use `arrays.is_equal(a, b)` for equality.

#### Access Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_first` | `(arr [T]) -> T` | Return first element (panic if empty) |
| `get_last` | `(arr [T]) -> T` | Return last element (panic if empty) |

#### Modification Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `append` | `(&arr [T], value T)` | Append element |
| `prepend` | `(&arr [T], value T)` | Insert value at front |
| `insert_at` | `(&arr [T], index int, value T)` | Insert at index |
| `remove` | `(&arr [T], value T)` | Remove first occurrence of value |
| `remove_at` | `(&arr [T], index int)` | Remove element at index |
| `remove_first` | `(&arr [T]) -> T` | Remove and return first element (panic if empty) |
| `remove_last` | `(&arr [T]) -> T` | Remove and return last element (panic if empty) |
| `clear` | `(&arr [T])` | Remove all elements |
| `fill` | `(&arr [T], value T, count int)` | Fill array with N copies of value |
| `sort_asc` | `(&arr [T])` | Sort ascending in-place |
| `sort_desc` | `(&arr [T])` | Sort descending in-place |

#### Transformation Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `reverse` | `(arr [T]) -> [T]` | Return reversed copy |
| `slice` | `(arr [T], start int, end int) -> [T]` | Return slice |
| `concat` | `(a [T], b [T]) -> [T]` | Concatenate two arrays |
| `deduplicate` | `(arr [T]) -> [T]` | Remove duplicate values |
| `flatten` | `(arr [[T]]) -> [T]` | Flatten one level of nesting |
| `split_every` | `(arr [T], size int) -> [[T]]` | Split into sub-arrays of given size |
| `pair` | `(a [T], b [T]) -> [[T]]` | Pair elements from two arrays |

#### Computation Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_sum` | `(arr [T]) -> T` | Sum all elements. Accepts int, float, or any sized integer/float type. |
| `get_min` | `(arr [T]) -> T` | Minimum element |
| `get_max` | `(arr [T]) -> T` | Maximum element |

#### Higher-Order Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `map` | `(arr [T], ()transform) -> [T]` | Returns a new array with `transform` applied to each element. `transform` must be `(T) -> T`. |
| `filter` | `(arr [T], ()predicate) -> [T]` | Returns a new array containing only elements for which `predicate` returns true. `predicate` must be `(T) -> bool`. |
| `reduce` | `(arr [T], initial T, ()accumulator) -> T` | Reduces the array to a single value by applying `accumulator(acc, element)` for each element, starting with `initial`. `accumulator` must be `(T, T) -> T`. |
| `any` | `(arr [T], ()predicate) -> bool` | Returns true if at least one element satisfies `predicate`. `predicate` must be `(T) -> bool`. Returns false on an empty array. |
| `all` | `(arr [T], ()predicate) -> bool` | Returns true if every element satisfies `predicate`. `predicate` must be `(T) -> bool`. Returns true on an empty array. |

### 9.3 Strings Module (`@strings`)

#### Case Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `to_upper` | `(s string) -> string` | Convert to uppercase |
| `to_lower` | `(s string) -> string` | Convert to lowercase |

#### Access Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `char_at` | `(s string, index int) -> char` | Character at byte index; panics if out of bounds |

#### Query Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_empty` | `(s string) -> bool` | Check if string is empty (length zero; does not trim) |
| `contains` | `(s string, sub string) -> bool` | Check if contains substring |
| `starts_with` | `(s string, prefix string) -> bool` | Check prefix |
| `ends_with` | `(s string, suffix string) -> bool` | Check suffix |
| `index_of` | `(s string, sub string) -> int` | First index of substring |
| `last_index_of` | `(s string, sub string) -> int` | Last index of substring (-1 if not found) |
| `count` | `(s string, sub string) -> int` | Count occurrences |

#### Classification Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_alpha` | `(c char) -> bool` | True if c is an ASCII letter (a-z, A-Z) |
| `is_digit` | `(c char) -> bool` | True if c is a decimal digit (0-9) |
| `is_alnum` | `(c char) -> bool` | True if c is a letter or digit |
| `is_whitespace` | `(c char) -> bool` | True if c is space, tab, newline, or carriage return |
| `is_upper` | `(c char) -> bool` | True if c is an uppercase letter (A-Z) |
| `is_lower` | `(c char) -> bool` | True if c is a lowercase letter (a-z) |

#### Transformation Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `trim` | `(s string) -> string` | Trim whitespace |
| `trim_left` | `(s string) -> string` | Trim left whitespace |
| `trim_right` | `(s string) -> string` | Trim right whitespace |
| `remove_prefix` | `(s string, prefix string) -> string` | Remove prefix if present, otherwise return unchanged |
| `remove_suffix` | `(s string, suffix string) -> string` | Remove suffix if present, otherwise return unchanged |
| `replace` | `(s string, old string, new string) -> string` | Replace all occurrences |
| `repeat` | `(s string, count int) -> string` | Repeat string |
| `reverse` | `(s string) -> string` | Reverse string |

#### Conversion Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `split` | `(s string, sep string) -> [string]` | Split into array |
| `join` | `(arr [string], sep string) -> string` | Join array |
| `slice` | `(s string, start int, end int) -> string` | Extract substring |

#### Conversion Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `to_chars` | `(s string) -> [char]` | Convert string to char array |
| `from_chars` | `(chars [char]) -> string` | Convert char array to string |

### 9.4 Maps Module (`@maps`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_empty` | `(m map[K:V]) -> bool` | Check if map is empty |
| `has_key` | `(m map[K:V], key K) -> bool` | Check if key exists |
| `contains_value` | `(m map[K:V], value V) -> bool` | Check if any entry has the given value |
| `get_keys` | `(m map[K:V]) -> [K]` | Get all keys as an array |
| `get_values` | `(m map[K:V]) -> [V]` | Get all values as an array |
| `get_or_default` | `(m map[K:V], key K, default V) -> V` | Get value or return default if key missing |
| `remove_key` | `(&m map[K:V], key K)` | Remove key-value pair; does nothing if key absent |
| `clear` | `(&m map[K:V])` | Remove all entries |
| `merge` | `(m1 map[K:V], m2 map[K:V]) -> map[K:V]` | Combine two maps (m2 overwrites on conflict) |
| `is_equal` | `(a map[K:V], b map[K:V]) -> bool` | Structural equality. Compares counts, then iterates the first map's entries in insertion order looking each key up in the second. `K` and `V` must each be a primitive (`int`, `uint`, `float`, `bool`, `char`, `byte`, sized variants) or `string`; maps with nested-composite values are rejected at compile time. |

The `==` and `!=` operators on maps are not allowed; use `maps.is_equal(a, b)` for equality. Maps have no defined ordering, so `<` / `<=` / `>` / `>=` on maps is also rejected.

Use `len(m)` to get the number of entries (builtin, no import needed).

### 9.5 Math Module (`@math`)

Unless noted otherwise, all math functions accept `int`, `float`, and sized numeric types (`i8`–`i64`, `u8`–`u64`, `f32`, `f64`). Functions that return the same type as their input are marked with `-> T` below; others specify their return type explicitly.

#### Basic Arithmetic

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `(n T) -> T` | Absolute value |
| `neg` | `(n T) -> T` | Negation |
| `sign` | `(n T) -> int` | Sign (-1, 0, or 1) |

#### Min/Max/Clamp

| Function | Signature | Description |
|----------|-----------|-------------|
| `min` | `(a T, b T) -> T` | Minimum of two values |
| `max` | `(a T, b T) -> T` | Maximum of two values |
| `clamp` | `(value T, min T, max T) -> T` | Clamp value to range [min, max] |

#### Rounding

| Function | Signature | Description |
|----------|-----------|-------------|
| `floor` | `(n T) -> float` | Round down to nearest integer (returns float) |
| `ceil` | `(n T) -> float` | Round up to nearest integer (returns float) |
| `round` | `(n T) -> float` | Round to nearest integer (returns float) |
| `trunc` | `(n T) -> float` | Truncate toward zero (returns float) |

#### Powers and Roots

| Function | Signature | Description |
|----------|-----------|-------------|
| `pow` | `(base T, exp T) -> float` | Raise base to exponent |
| `sqrt` | `(n T) -> float` | Square root |
| `cbrt` | `(n T) -> float` | Cube root |
| `hypot` | `(x T, y T) -> float` | Hypotenuse (sqrt(x^2 + y^2)) |
| `exp` | `(n T) -> float` | e raised to the power n |
| `exp2` | `(n T) -> float` | 2 raised to the power n |

#### Logarithms

| Function | Signature | Description |
|----------|-----------|-------------|
| `log` | `(n T) -> float` | Natural logarithm |
| `log2` | `(n T) -> float` | Base 2 logarithm |
| `log10` | `(n T) -> float` | Base 10 logarithm |
| `log_base` | `(value T, base T) -> float` | Logarithm with custom base |

#### Trigonometry

| Function | Signature | Description |
|----------|-----------|-------------|
| `sin` | `(rad T) -> float` | Sine |
| `cos` | `(rad T) -> float` | Cosine |
| `tan` | `(rad T) -> float` | Tangent |
| `asin` | `(n T) -> float` | Arc sine |
| `acos` | `(n T) -> float` | Arc cosine |
| `atan` | `(n T) -> float` | Arc tangent |
| `atan2` | `(y T, x T) -> float` | Arc tangent of y/x |
| `sinh` | `(n T) -> float` | Hyperbolic sine |
| `cosh` | `(n T) -> float` | Hyperbolic cosine |
| `tanh` | `(n T) -> float` | Hyperbolic tangent |
| `deg_to_rad` | `(deg T) -> float` | Degrees to radians |
| `rad_to_deg` | `(rad T) -> float` | Radians to degrees |

#### Statistical

| Function | Signature | Description |
|----------|-----------|-------------|
| `factorial` | `(n int) -> int` | Factorial (n must be non-negative) |
| `gcd` | `(a int, b int) -> int` | Greatest common divisor |
| `lcm` | `(a int, b int) -> int` | Least common multiple |

#### Number Properties

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_prime` | `(n int) -> bool` | Check if prime |
| `is_even` | `(n int) -> bool` | Check if even |
| `is_odd` | `(n int) -> bool` | Check if odd |
| `is_infinite` | `(n float) -> bool` | Check if infinite |
| `is_nan` | `(n float) -> bool` | Check if NaN |
| `is_finite` | `(n float) -> bool` | Check if finite (not infinite or NaN) |

#### Utility

| Function | Signature | Description |
|----------|-----------|-------------|
| `lerp` | `(a T, b T, t T) -> float` | Linear interpolation between a and b by factor t |
| `distance` | `(x1 T, y1 T, x2 T, y2 T) -> float` | Euclidean distance between two 2D points |

#### Constants

- `PI` - Pi (3.14159...)
- `E` - Euler's number (2.71828...)
- `PHI` - Golden ratio (1.61803...)
- `SQRT2` - Square root of 2
- `LN2` - Natural log of 2
- `LN10` - Natural log of 10
- `TAU` - Tau (2*Pi)
- `INF` - Positive infinity
- `NEG_INF` - Negative infinity
- `EPSILON` - Smallest representable float difference

### 9.6 Time Module (`@time`)

#### Current Time

| Function | Signature | Description |
|----------|-----------|-------------|
| `now` | `() -> int` | Current Unix timestamp (seconds) |
| `now_ms` | `() -> int` | Current Unix timestamp (milliseconds) |
| `now_ns` | `() -> int` | Current Unix timestamp (nanoseconds) |

#### Time Components

| Function | Signature | Description |
|----------|-----------|-------------|
| `year` | `(timestamp int) -> int` | Get year |
| `month` | `(timestamp int) -> int` | Get month (1-12) |
| `day` | `(timestamp int) -> int` | Get day of month |
| `hour` | `(timestamp int) -> int` | Get hour |
| `minute` | `(timestamp int) -> int` | Get minute |
| `second` | `(timestamp int) -> int` | Get second |
| `weekday` | `(timestamp int) -> int` | Get day of week (0=Sunday) |

#### Formatting

| Function | Signature | Description |
|----------|-----------|-------------|
| `format` | `(format string, timestamp int) -> string` | Format time |
| `to_iso` | `(timestamp int) -> string` | ISO 8601 string |
| `date` | `(timestamp int) -> string` | Date (YYYY-MM-DD) |
| `to_clock` | `(timestamp int) -> string` | Time (HH:MM:SS) |

#### Arithmetic

| Function | Signature | Description |
|----------|-----------|-------------|
| `diff` | `(t1 int, t2 int) -> int` | Difference in seconds (t2 - t1); negative if t1 is after t2 |

#### Performance Timing

| Function | Signature | Description |
|----------|-----------|-------------|
| `tick` | `() -> int` | High-resolution timestamp in nanoseconds |
| `elapsed_ms` | `(start_tick int) -> int` | Milliseconds elapsed since a tick |

### 9.7 Random Module (`@random`)

Some random functions accept a variable number of arguments (e.g., `rand_int` with 1 or 2 args). This is not general function overloading; it is special-case codegen dispatching within the stdlib only.

| Function | Signature | Description |
|----------|-----------|-------------|
| `rand_float` | `() -> float` | Random float [0.0, 1.0) |
| `rand_float` | `(min float, max float) -> float` | Random float [min, max) |
| `rand_int` | `(max int) -> int` | Random int [0, max) |
| `rand_int` | `(min int, max int) -> int` | Random int [min, max) |
| `rand_bool` | `() -> bool` | Random boolean |
| `rand_byte` | `() -> byte` | Random byte [0, 255] |
| `rand_char` | `() -> char` | Random printable char |
| `rand_char` | `(min char, max char) -> char` | Random char in range |
| `choice` | `(arr [T]) -> T` | Random element from array |
| `shuffle` | `(arr [T]) -> [T]` | Return shuffled copy |
| `sample` | `(arr [T], n int) -> [T]` | Return n unique random elements |
| `seed` | `(value int)` | Seed the random number generator |

### 9.8 JSON Module (`@json`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `decode` | `(text string) -> map[string:string]` | Decode JSON string to map |
| `parse` | `(text string) -> T` | Parse JSON into a `#json` struct (context-dependent) |
| `encode` | `(value T) -> string` | Encode to JSON string. Accepts int, float, bool, string, map, array. |
| `stringify` | `(value T) -> string` | Encode to JSON string (alias for encode, supports `#json` structs) |
| `pretty_print` | `(m map, indent int) -> string` | Pretty-print a map as indented JSON |
| `is_valid` | `(text string) -> bool` | Check if valid JSON |

Error-returning variant: `decode`

### 9.9 IO Module (`@io`)

#### File Reading

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `(path string) -> string` | Read entire file as a string |
| `read_bytes` | `(path string) -> [byte]` | Read entire file as a byte array |
| `read_lines` | `(path string) -> [string]` | Read file and split into lines (strips `\r\n`) |

#### File Writing

| Function | Signature | Description |
|----------|-----------|-------------|
| `write_file` | `(path string, content string) -> bool` | Write file |
| `append_file` | `(path string, content string) -> bool` | Append to file |
| `write_bytes` | `(path string, data [byte]) -> bool` | Write byte array to file |
| `append_bytes` | `(path string, data [byte]) -> bool` | Append byte array to file |

#### File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_exists` | `(path string) -> bool` | Check if file exists |
| `is_file` | `(path string) -> bool` | Check if path is a regular file |
| `is_directory` | `(path string) -> bool` | Check if path is a directory |
| `file_size` | `(path string) -> int` | Get file size in bytes; returns `-1` on error |
| `delete_file` | `(path string) -> bool` | Delete file |
| `rename_file` | `(old_path string, new_path string) -> bool` | Rename file |
| `copy_file` | `(src string, dst string) -> bool` | Copy file |
| `move_file` | `(src string, dst string) -> bool` | Move file |

#### Directory Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `list_dir` | `(path string) -> [string]` | List directory entries |
| `make_dir` | `(path string) -> bool` | Create directory |
| `make_dir_all` | `(path string) -> bool` | Create directory and all parents |
| `remove_dir` | `(path string) -> bool` | Remove empty directory |
| `remove_dir_all` | `(path string) -> bool` | Remove directory and all contents |
| `walk` | `(path string) -> [string]` | Recursively list all files |
| `glob` | `(pattern string) -> [string]` | Match files by glob pattern; returns empty array on no match |

#### Temporary Files

| Function | Signature | Description |
|----------|-----------|-------------|
| `temp_file` | `() -> string` | Create a temporary file; returns its path. Automatically deleted on exit |
| `temp_dir` | `() -> string` | Create a temporary directory; returns its path. Automatically deleted on exit |

#### Path Manipulation

| Function | Signature | Description |
|----------|-----------|-------------|
| `path_join` | `(parts [string]) -> string` | Join path segments; an absolute segment replaces the accumulated path |
| `dirname` | `(path string) -> string` | Parent directory of path |
| `basename` | `(path string) -> string` | Filename component of path |
| `extension` | `(path string) -> string` | File extension including dot (e.g. `".txt"`); empty string if none |
| `is_absolute` | `(path string) -> bool` | Check if path is absolute |
| `normalize` | `(path string) -> string` | Clean and normalize path |

```gray
mut p string = io.path_join({"/home", "user", "docs"})  // "/home/user/docs"
mut q string = io.path_join({"a/b", "/abs"})            // "/abs", absolute replaces
```

#### Error-Returning Variants

Most functions that can fail have an error-returning variant usable via multi-variable destructuring. The plain form panics on hard errors; the error form returns `(T, Error)`.

| Function | Error-returning variant returns |
|----------|---------------------------------|
| `read_file` | `(string, Error)` |
| `read_bytes` | `([byte], Error)` |
| `read_lines` | `([string], Error)` |
| `file_size` | `(int, Error)` |
| `write_file` | `(bool, Error)` |
| `append_file` | `(bool, Error)` |
| `delete_file` | `(bool, Error)` |
| `rename_file` | `(bool, Error)` |
| `copy_file` | `(bool, Error)` |
| `move_file` | `(bool, Error)` |
| `list_dir` | `([string], Error)` |
| `make_dir` | `(bool, Error)` |
| `make_dir_all` | `(bool, Error)` |
| `remove_dir` | `(bool, Error)` |
| `remove_dir_all` | `(bool, Error)` |
| `walk` | `([string], Error)` |
| `glob` | `([string], Error)` |
| `write_bytes` | `(bool, Error)` |
| `append_bytes` | `(bool, Error)` |
| `temp_file` | `(string, Error)` |
| `temp_dir` | `(string, Error)` |

```gray
// Always use destructuring; single-variable assignment is a compile error
mut content, err = io.read_file("data.txt")
if err != nil {
    println("read failed: ${err.message}")
}

// Discard the error with _
mut content, _ = io.read_file("data.txt")

mut lines, err = io.read_lines("data.txt")
for_each line in lines {
    println(line)
}

mut sz, err = io.file_size("data.txt")
```

#### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `O_RDONLY` | `0` | Open for reading only |
| `O_WRONLY` | `1` | Open for writing only |
| `O_RDWR` | `2` | Open for reading and writing |

#### Path Resolution

All relative paths passed to `@io` functions (and any other stdlib function that accepts a file path, including `csv.read_file`, `csv.write_file`, and `sqlite.open`) are resolved relative to the **current working directory** of the process, the directory from which the program was launched. They are not resolved relative to the source file that contains the call.

```gray
// Given project layout:
//   project/
//     main.gray
//     src/cli.gray
//     data/config.json

// Running from project/:  gray main.gray
// All of these resolve from project/, regardless of which .gray file calls them:
io.read_file("data/config.json")      // project/data/config.json
io.read_file("./data/config.json")    // same thing
io.read_file("/etc/hosts")            // absolute path, unaffected by cwd
```

### 9.10 OS Module (`@os`)

#### Environment Variables

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_env` | `(name string) -> string` | Get environment variable |
| `set_env` | `(name string, value string)` | Set environment variable |
| `unset_env` | `(name string)` | Remove environment variable |

#### System Information

| Function | Signature | Description |
|----------|-----------|-------------|
| `args` | `() -> [string]` | Get command-line arguments |
| `current_dir` | `() -> string` | Get current working directory |
| `hostname` | `() -> string` | Get machine hostname |
| `pid` | `() -> int` | Get process ID |
| `current_os` | `() -> int` | Get current OS as an int matching the constants below (`MAC_OS`, `LINUX`, `WINDOWS`, `OTHER`) |
| `arch` | `() -> string` | Get CPU architecture |

#### Process Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `exec` | `(cmd string, args [string]) -> (int, string, string, bool)` | Run a subprocess. Returns `(exit_code, stdout, stderr, ok)`. `ok` is `false` if the process could not be launched. stdout and stderr are captured. POSIX only. |

```gray
mut code, stdout, stderr, ok = os.exec("ls", {"-l", "/tmp"})
if !ok {
    println("failed to launch")
}
if ok && code != 0 {
    println("exited with error: ${stderr}")
}
```

#### Constants

- `MAC_OS` = 0
- `LINUX` = 1
- `WINDOWS` = 2
- `OTHER` = 3

### 9.11 HTTP Module (`@http`)

HTTP client for making requests. Currently supports HTTP only.

#### Request Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `get` | `(url string, headers map[string:string]) -> HttpResponse` | GET request |
| `post` | `(url string, body string, headers map[string:string]) -> HttpResponse` | POST request |
| `put` | `(url string, body string, headers map[string:string]) -> HttpResponse` | PUT request |
| `patch` | `(url string, body string, headers map[string:string]) -> HttpResponse` | PATCH request |
| `delete` | `(url string, headers map[string:string]) -> HttpResponse` | DELETE request |
| `head` | `(url string, headers map[string:string]) -> HttpResponse` | HEAD request |

Error-returning variants: `get`, `post`, `put`, `delete`, `head`, `patch`

#### HttpResponse Type

The `HttpResponse` struct is available when either `@http` or `@server` is imported.

- `status int` - HTTP status code
- `body string` - Response body
- `headers map` - Response headers

### 9.12 Crypto Module (`@crypto`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `sha256` | `(data string) -> string` | SHA-256 hash (hex) |
| `md5` | `(data string) -> string` | MD5 hash (hex) |
| `random_hex` | `(length int) -> string` | Cryptographically secure random hex string |

### 9.13 Encoding Module (`@encoding`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `base64_encode` | `(s string) -> string` | Encode to base64 |
| `base64_decode` | `(s string) -> string` | Decode from base64 |
| `hex_encode` | `(s string) -> string` | Encode to hex |
| `hex_decode` | `(s string) -> string` | Decode from hex |
| `url_encode` | `(s string) -> string` | URL percent-encode |
| `url_decode` | `(s string) -> string` | URL percent-decode |

### 9.14 UUID Module (`@uuid`)

UUID is a struct type wrapping a canonical 36-character hyphenated string. All generator and parse functions return `UUID`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `generate` | `() -> UUID` | Generate UUID v4 (hyphenated, 36 chars) |
| `generate_hyphenated` | `() -> UUID` | Alias for `generate` |
| `generate_random` | `() -> UUID` | RFC 4122 v4 (random), hyphenated, lowercase |
| `generate_time_ordered` | `() -> UUID` | RFC 9562 v7 (time-ordered), hyphenated, lowercase. Sorts by creation time |
| `generate_compact` | `(id UUID) -> string` | Strip hyphens from a UUID, returning a 32-char hex string |
| `parse` | `(s string) -> UUID` | Validate and normalize a 36-char hyphenated UUID to lowercase. Panics on invalid input — gate with `is_valid()` for a non-panicking check |
| `to_string` | `(id UUID) -> string` | Convert UUID to its 36-char hyphenated string representation |
| `is_valid` | `(s string) -> bool` | Validate UUID format |

| Constant | Type | Value |
|----------|------|-------|
| `NIL_UUID` | `UUID` | All-zero UUID (`00000000-0000-0000-0000-000000000000`) |

UUID randomness comes from `getentropy()` (macOS, BSDs, glibc 2.25+) with a fallback to `/dev/urandom`, suitable for security-sensitive identifiers.

### 9.15 Bytes Module (`@bytes`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `from_string` | `(s string) -> [byte]` | Create from UTF-8 string |
| `from_hex` | `(hex string) -> [byte]` | Decode hex string |
| `from_base64` | `(b64 string) -> [byte]` | Decode base64 string |
| `to_string` | `(bytes [byte]) -> string` | Convert to UTF-8 string |
| `to_hex` | `(bytes [byte]) -> string` | Encode to hex string |
| `to_base64` | `(bytes [byte]) -> string` | Encode to base64 string |

### 9.16 Binary Module (`@binary`)

Binary encoding/decoding for integers and floats in little-endian (le) and big-endian (be) formats.

#### 8-bit

| Function | Description |
|----------|-------------|
| `encode_i8`, `decode_i8` | Signed 8-bit |
| `encode_u8`, `decode_u8` | Unsigned 8-bit |

#### 16-bit

| Function | Description |
|----------|-------------|
| `encode_i16_le`, `encode_i16_be`, `decode_i16_le`, `decode_i16_be` | Signed 16-bit |
| `encode_u16_le`, `encode_u16_be`, `decode_u16_le`, `decode_u16_be` | Unsigned 16-bit |

#### 32-bit

| Function | Description |
|----------|-------------|
| `encode_i32_le`, `encode_i32_be`, `decode_i32_le`, `decode_i32_be` | Signed 32-bit |
| `encode_u32_le`, `encode_u32_be`, `decode_u32_le`, `decode_u32_be` | Unsigned 32-bit |

#### 64-bit

| Function | Description |
|----------|-------------|
| `encode_i64_le`, `encode_i64_be`, `decode_i64_le`, `decode_i64_be` | Signed 64-bit |
| `encode_u64_le`, `encode_u64_be`, `decode_u64_le`, `decode_u64_be` | Unsigned 64-bit |

#### 128-bit

| Function | Description |
|----------|-------------|
| `encode_i128_le`, `encode_i128_be`, `decode_i128_le`, `decode_i128_be` | Signed 128-bit |
| `encode_u128_le`, `encode_u128_be`, `decode_u128_le`, `decode_u128_be` | Unsigned 128-bit |

#### 256-bit

| Function | Description |
|----------|-------------|
| `encode_i256_le`, `encode_i256_be`, `decode_i256_le`, `decode_i256_be` | Signed 256-bit |
| `encode_u256_le`, `encode_u256_be`, `decode_u256_le`, `decode_u256_be` | Unsigned 256-bit |

#### Floats

| Function | Description |
|----------|-------------|
| `encode_f32_le`, `encode_f32_be`, `decode_f32_le`, `decode_f32_be` | 32-bit float |
| `encode_f64_le`, `encode_f64_be`, `decode_f64_le`, `decode_f64_be` | 64-bit float |

### 9.17 SQLite Module (`@sqlite`)

SQLite database access for persistent storage.

| Function | Signature | Description |
|----------|-----------|-------------|
| `open` | `(path string) -> (Database, Error)` | Open or create a SQLite database |
| `close` | `(db Database)` | Close database connection |
| `exec` | `(db Database, sql string) -> (bool, Error)` | Execute a SQL statement (no rows returned) |
| `query` | `(db Database, sql string) -> ([map[string:string]], Error)` | Execute a SELECT query, returns array of row maps |

Error-returning variants: `open`, `exec`, `query` — always use destructuring.

> 💡 **Tip:** Parameterized queries (`?` placeholders) are not supported. Build your SQL strings directly. Always sanitize any user-supplied values before interpolating them into SQL.

```gray
import @sqlite

mut db, err = sqlite.open("myapp.db")
if err != nil { println("open failed: ${err}") }

mut ok, _ = sqlite.exec(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT)")
mut ok, _ = sqlite.exec(db, "INSERT INTO users (name) VALUES ('Alice')")

mut rows, err = sqlite.query(db, "SELECT * FROM users")
if err != nil { println("query failed: ${err}") }
for_each row in rows {
    println(row)
}

sqlite.close(db)
```

### 9.18 Server Module (`@server`)

An HTTP server module with dynamic handlers and path parameters.

#### Routing

| Function | Signature | Description |
|----------|-----------|-------------|
| `add_router` | `() -> Router` | Create a new router |
| `add_route` | `(router Router, method string, path string, ()handler)` | Add a route with handler function |
| `listen` | `(router Router, port int)` | Start HTTP server on port (blocks until killed) |
| `cors` | `(router Router, origin string)` | Enable CORS with the given origin |
| `use` | `(router Router, ()middleware)` | Register a middleware function |

#### Response Builders

| Function | Signature | Description |
|----------|-----------|-------------|
| `text` | `(status int, body string) -> HttpResponse` | Create text/plain response |
| `json` | `(status int, data) -> HttpResponse` | Create application/json response |
| `html` | `(status int, body string) -> HttpResponse` | Create text/html response |
| `redirect` | `(status int, url string) -> HttpResponse` | Create redirect response |

#### Request Type

The `HttpRequest` type is only available when `@server` is imported. Importing `@http` alone does not provide this type.

Every handler receives an `HttpRequest` struct:

| Field | Type | Description |
|-------|------|-------------|
| `method` | `string` | HTTP method (GET, POST, etc.) |
| `path` | `string` | Request path |
| `query` | `map[string:string]` | Query parameters |
| `headers` | `map[string:string]` | Request headers |
| `params` | `map[string:string]` | Path parameters (from `:param` segments) |
| `body` | `string` | Raw request body |

```gray
import @server

do home(req HttpRequest) -> HttpResponse {
    return server.text(200, "Welcome!")
}

do get_user(req HttpRequest) -> HttpResponse {
    mut id = req.params["id"]
    return server.json(200, {"id": id})
}

do main() {
    mut r = server.add_router()
    server.cors(r, "*")
    server.add_route(r, "GET", "/", ()home)
    server.add_route(r, "GET", "/users/:id", ()get_user)
    server.listen(r, 8080)
}
```

### 9.19 Regex Module (`@regex`)

Regular expression operations using POSIX extended regex syntax.

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_valid` | `(pattern string) -> bool` | Check if pattern is valid regex |
| `is_match` | `(pattern string, text string) -> bool` | Check if pattern matches text |
| `find` | `(pattern string, text string) -> string` | First match |
| `find_all` | `(pattern string, text string) -> [string]` | All matches |
| `replace` | `(pattern string, text string, replacement string) -> string` | Replace matches |
| `split` | `(pattern string, text string) -> [string]` | Split by pattern |

Error-returning variants: `find`, `find_all`, `replace`, `split`

### 9.20 CSV Module (`@csv`)

Reading and writing CSV (Comma-Separated Values) data.

| Function | Signature | Description |
|----------|-----------|-------------|
| `parse` | `(csv_string string) -> [[string]]` | Parse CSV string to 2D array |
| `encode` | `(data [[string]]) -> string` | Encode 2D array to CSV string |
| `read_file` | `(path string) -> ([[string]], Error)` | Read and parse CSV file — always use destructuring |
| `write_file` | `(path string, data [[string]]) -> (bool, Error)` | Write 2D array to CSV file — always use destructuring |
| `headers` | `(data [[string]]) -> [string]` | Extract header row from parsed CSV data |

### 9.21 Net Module (`@net`)

TCP sockets and DNS resolution.

| Function | Signature | Description |
|----------|-----------|-------------|
| `connect` | `(host string, port int) -> Socket` | Connect to a remote host |
| `listen` | `(port int) -> Listener` | Listen for incoming connections on a port |
| `accept` | `(listener Listener) -> Socket` | Accept an incoming connection |
| `send` | `(sock Socket, data string) -> int` | Send data over a socket, returns bytes sent |
| `receive` | `(sock Socket, max_bytes int) -> string` | Receive up to `max_bytes` bytes from a socket |
| `close` | `(sock Socket)` | Close a socket or listener |
| `set_timeout` | `(sock Socket, ms int)` | Set read/write timeout in milliseconds |
| `resolve` | `(hostname string) -> string` | Resolve a hostname to an IP address |

Error-returning variants: `connect`, `listen`, `accept`, `send`, `receive`, `resolve`

### 9.22 Threads Module (`@threads`)

Thread lifecycle management. Compiler-only feature; requires POSIX threads.

| Function | Signature | Description |
|----------|-----------|-------------|
| `spawn` | `(()func) -> Thread` | Spawn a new thread running `func` |
| `spawn` | `(()func, arg int) -> Thread` | Spawn a new thread running `func` with an int argument |
| `join` | `(t Thread)` | Wait for a thread to finish |
| `detach` | `(t Thread)` | Release ownership; the thread runs independently. After detach the handle must not be joined or queried |
| `is_alive` | `(t Thread) -> bool` | True while the thread's body has not returned. Not valid after `detach` or `join` |
| `get_id` | `() -> int` | Get the current thread's ID |
| `current` | `() -> int` | Same as `get_id`; alternate spelling |
| `yield` | `()` | Hint the scheduler to run another runnable thread |
| `sleep` | `(ms int)` | Sleep the current thread for `ms` milliseconds |
| `thread_count` | `() -> int` | Number of live threads spawned through this module (excludes main and non-Grayscale threads) |

### 9.23 Sync Module (`@sync`)

Synchronization primitives for thread-safe access to shared data. Compiler-only feature; requires POSIX threads.

| Function | Signature | Description |
|----------|-----------|-------------|
| `mutex` | `() -> Mutex` | Create a new mutex |
| `lock` | `(m Mutex)` | Acquire a mutex |
| `unlock` | `(m Mutex)` | Release a mutex |
| `try_lock` | `(m Mutex)` | Try to acquire a mutex (non-blocking) |
| `destroy` | `(m Mutex)` | Destroy a mutex |

### 9.24 Channels Module (`@channels`)

Message passing between threads. Compiler-only feature; requires POSIX threads.

| Function | Signature | Description |
|----------|-----------|-------------|
| `open` | `(capacity int) -> Channel` | Create a buffered channel |
| `send` | `(ch Channel, value)` | Send a value into a channel |
| `receive` | `(ch Channel) -> T` | Receive a value from a channel |
| `close` | `(ch Channel)` | Close a channel |
| `try_send` | `(ch Channel, value int) -> bool` | Non-blocking send; returns false if full |
| `try_receive` | `(ch Channel) -> (int, bool)` | Non-blocking receive; returns value and success |

### 9.25 Memory Module (`@mem`)

Arena-based memory allocation. Compiler-only feature.

| Function | Signature | Description |
|----------|-----------|-------------|
| `arena` | `(size int) -> Arena` | Create an arena with the given byte capacity |
| `destroy` | `(arena Arena)` | Destroy an arena and free its memory |
| `reset` | `(arena Arena)` | Reset an arena, reclaiming all allocations without freeing |
| `usage` | `(arena Arena) -> int` | Return the number of bytes currently used |
| `init` | `(arena Arena, Type) -> ^Type` | Allocate a zero-initialized value of `Type` in the arena |
| `alloc` | `(arena Arena, value T) -> ^T` | Allocate a copy of value in the arena |
| `make` | `(arena Arena, Type) -> ^Type` | Allocate zero-initialized value of Type in arena |
| `free` | `(arena Arena)` | Free an arena (alias for destroy) |
| `size_of` | `(Type) -> int` | Size of type in bytes |
| `raw_copy` | `(dest ptr, src ptr, n int)` | Copy `n` bytes from `src` to `dest` |
| `zero` | `(p ptr, n int)` | Zero out `n` bytes at `p` |
| `fill` | `(p ptr, value int, n int)` | Set `n` bytes at `p` to `value` |

### 9.26 Atomic Module (`@atomic`)

Lock-free atomic operations backed by hand-written assembly (ARM64 and x86_64). Compiler-only feature.

#### 64-bit Atomics

All pointer arguments must be `^int` (pointer to int).

| Function | Signature | Description |
|----------|-----------|-------------|
| `load` | `(ptr ^int) -> int` | Atomically load a value |
| `store` | `(ptr ^int, val int)` | Atomically store a value |
| `add` | `(ptr ^int, val int) -> int` | Atomic add; returns previous value |
| `sub` | `(ptr ^int, val int) -> int` | Atomic subtract; returns previous value |
| `exchange` | `(ptr ^int, val int) -> int` | Atomic swap; returns previous value |
| `cas` | `(ptr ^int, expected int, desired int) -> bool` | Compare-and-swap; returns true on success |
| `and` | `(ptr ^int, val int) -> int` | Atomic bitwise AND; returns previous value |
| `or` | `(ptr ^int, val int) -> int` | Atomic bitwise OR; returns previous value |
| `xor` | `(ptr ^int, val int) -> int` | Atomic bitwise XOR; returns previous value |

#### Spinlock

| Function | Signature | Description |
|----------|-----------|-------------|
| `spinlock` | `() -> SpinLock` | Create a new spinlock |
| `spin_lock` | `(lk SpinLock)` | Acquire a spinlock (spins until acquired) |
| `spin_trylock` | `(lk SpinLock) -> bool` | Try to acquire; returns true if acquired |
| `spin_unlock` | `(lk SpinLock)` | Release a spinlock |

#### Memory Barrier

| Function | Signature | Description |
|----------|-----------|-------------|
| `fence` | `()` | Full memory barrier (sequential consistency) |

### 9.27 Fmt Module (`@fmt`)

Formatted output and string formatting functions.

#### Formatted Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `printf` | `(format string, ...args T)` | Print formatted string to stdout |
| `printfln` | `(format string, ...args T)` | Print formatted string to stdout with trailing newline |
| `eprintf` | `(format string, ...args T)` | Print formatted string to stderr |
| `eprintfln` | `(format string, ...args T)` | Print formatted string to stderr with trailing newline |
| `sprintf` | `(format string, ...args T) -> string` | Return formatted string |
| `sprintfln` | `(format string, ...args T) -> string` | Return formatted string with trailing newline |
| `format` | `(format string, ...args T) -> string` | Return formatted string |

> 💡 **Tip:** `eprintln` and `eprint` are builtins, not fmt module functions. Use them without an import.

#### Format Specifiers

Format strings use C-style `%` specifiers:

| Specifier | Type | Description |
|-----------|------|-------------|
| `%d`, `%i` | `int` | Signed decimal integer |
| `%u` | `uint` | Unsigned decimal integer |
| `%f` | `float` | Decimal floating-point |
| `%e` | `float` | Scientific notation |
| `%g` | `float` | Shorter of `%f` or `%e` |
| `%s` | `string` | String |
| `%c` | `char` | Single character |
| `%x` | `int` | Hexadecimal (lowercase) |
| `%o` | `int` | Octal |
| `%%` | — | Literal `%` |

Width, precision, and flags (`-`, `+`, `0`, `#`) follow standard C printf conventions. `%d` and `%u` are automatically widened to `%lld`/`%llu` for Grayscale's 64-bit integer types. Composite types (structs, arrays, maps) are not supported — use `println` for those.

```gray
import @fmt

mut score int = 42
mut x int = 7
fmt.printf("%-10s %5d\n", "score", score)  // "score         42"
fmt.printf("%08.2f\n", 3.14159)            // "00003.14"
mut s string = fmt.sprintf("x = %d", x)   // "x = 7"
```

#### Padding

| Function | Signature | Description |
|----------|-----------|-------------|
| `pad_left` | `(s string, width int, ch char) -> string` | Pad string on the left to `width` with `ch` |
| `pad_right` | `(s string, width int, ch char) -> string` | Pad string on the right to `width` with `ch` |
| `center` | `(s string, width int, ch char) -> string` | Center string within `width`, padding both sides with `ch` |

#### Number Formatting

| Function | Signature | Description |
|----------|-----------|-------------|
| `int_to_hex` | `(n int) -> string` | Format integer as lowercase hexadecimal (no `0x` prefix) |
| `int_to_binary` | `(n int) -> string` | Format integer as binary |
| `int_to_octal` | `(n int) -> string` | Format integer as octal |
| `float_fixed` | `(f float, decimals int) -> string` | Format float with fixed decimal places |
| `float_sci` | `(f float) -> string` | Format float in scientific notation |

Accepts `string`, `int`, `float`, and `bool` arguments for formatted output functions. Composite types (structs, arrays, maps) are not supported. Use `println` for printing composite types.

### 9.28 Strconv Module (`@strconv`)

String-to-type and type-to-string conversion functions with proper error handling.

#### Fallible Conversions (string → type)

| Function | Signature | Description |
|----------|-----------|-------------|
| `to_int` | `(s string, base int = 10) -> (int, Error)` | Parse string as signed integer in given base |
| `to_uint` | `(s string, base int = 10) -> (uint, Error)` | Parse string as unsigned integer in given base |
| `to_float` | `(s string) -> (float, Error)` | Parse string as floating-point number |
| `to_bool` | `(s string) -> (bool, Error)` | Parse string as boolean |

**Behavior:**
- When assigned to a single variable (`mut n int = strconv.to_int("42")`), panics on invalid input.
- When destructured (`mut n, err = strconv.to_int("42")`), returns an Error instead of panicking.

**`to_int` / `to_uint` rules:**
- The `base` parameter must be an integer between **2 and 36** (inclusive). Invalid bases produce a compile-time error when passed as a literal, or a runtime panic when passed as a variable.
- Leading/trailing whitespace is **not** tolerated; the entire string must be a valid representation.
- `to_uint` rejects strings containing a `-` sign (returns error or panics).
- For bases > 10, letters `A`–`Z` (case-insensitive) represent digits 10–35.

**`to_float` rules:**
- Accepts standard decimal notation (e.g. `"3.14"`, `"-0.5"`, `"100"`).
- Accepts `"inf"`, `"infinity"`, and `"nan"` (case-insensitive), returning `±Inf` / `NaN` respectively. This matches the underlying `strtod` semantics used by the implementation.
- Does **not** accept hex floats.

**`to_bool` rules:**
- Accepts `"true"` and `"false"` (case-insensitive: `"TRUE"`, `"True"`, `"FALSE"`, etc. are valid).
- All other strings produce an error or panic. There is no implicit truthiness; `"1"`, `"0"`, `"yes"`, `"no"` are **not** valid.

#### Type-to-String Conversions (type → string)

| Function | Signature | Description |
|----------|-----------|-------------|
| `from_int` | `(n int) -> string` | Convert integer to decimal string |
| `from_uint` | `(n uint) -> string` | Convert unsigned integer to decimal string |
| `from_float` | `(f float) -> string` | Convert float to string (shortest representation) |
| `from_bool` | `(b bool) -> string` | Convert boolean to `"true"` or `"false"` |

These functions never fail.

#### Query Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `is_numeric` | `(s string) -> bool` | Returns true if string is a valid numeric representation (integer or decimal) |
| `is_integer` | `(s string) -> bool` | Returns true if string is a valid integer (digits only, optional leading sign) |

**`is_numeric` rules:**
- Accepts optional leading `+` or `-`, followed by digits with at most one `.` decimal point.
- Empty strings return `false`.
- Does not accept scientific notation, hex prefixes, or whitespace.

**`is_integer` rules:**
- Accepts optional leading `+` or `-`, followed by one or more digits (`0`–`9`).
- Empty strings return `false`.
- Does not validate whether the value fits in an `int` or `uint`.

#### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BASE_2` | `2` | Binary |
| `BASE_8` | `8` | Octal |
| `BASE_10` | `10` | Decimal (default) |
| `BASE_16` | `16` | Hexadecimal |
| `BASE_36` | `36` | Base-36 (digits + full alphabet) |

Constants can be used qualified (`strconv.BASE_16`) or bare after `import and use @strconv`. Any integer value between 2 and 36 is also accepted directly as the base argument.

---

## 10. Error Handling

### 10.1 Error Type

The `Error` type represents an error condition. Errors are created with the `error()` function:

```gray
mut err Error = error("something went wrong")
```

### 10.2 Error Returns

Functions that may fail return a `(T, Error)` tuple; these are fallible functions. Destructuring is required; single-var assignment from a fallible function is a compile error. See [Section 4.5](#45-return-value-handling) for the full rules.

Functions that may fail conventionally return a tuple with the result and an Error:

```gray
do read_file(path string) -> (string, Error) {
    if !file_exists(path) {
        return "", error("file not found")
    }
    return contents, nil
}
```

### 10.3 Error Checking

Errors are checked by comparing to `nil`:

```gray
mut content, err = read_file("data.txt")
if err != nil {
    println("Error: ${err}")
    return
}
// Use content
```

### 10.4 Runtime Errors

Certain operations produce runtime errors that terminate program execution:

- Division by zero (int or float)
- Array index out of bounds
- Map key not found
- Invalid type conversion

Runtime errors include location information (file, line, column).

---

## 11. Memory Model

### 11.1 Automatic Scope-Based Arena Management (ASBAM)

Grayscale uses **Automatic Scope-Based Arena Management (ASBAM)**, a memory management model that combines arena allocators with scope-driven lifecycle control and automatic escape detection. There is no garbage collector, no reference counting, and no ownership annotations. The compiler infers everything from scope structure.

ASBAM is built on four principles:

1. **Arena allocation** — memory is allocated from arena regions and freed in bulk, not per-object. There is no per-allocation overhead.
2. **Scope-driven lifecycle** — every scope boundary (function, loop iteration, conditional block) defines a memory region. When the scope ends, its region is reclaimed in a single operation.
3. **Automatic escape detection** — when a value created inside a scope is stored somewhere that outlives that scope, the compiler automatically deep-copies it to the outer scope's arena before the inner arena is reclaimed.
4. **Dual-arena separation** — two arenas serve distinct roles: a default arena for scope-bound temporaries, and a heap arena for persistent allocations via `new()`.

When a block of code ends, whether a function body, a loop iteration, or a conditional block, any memory it created is freed. If a value needs to survive because it escapes the scope, ASBAM handles it automatically.

```gray
do process(name string) {
    mut upper = strings.to_upper(name)    // allocated in this scope
    mut parts = strings.split(upper, ",") // allocated in this scope
    println(parts[0])
}
// function ends -> upper and parts are freed
```

No imports, no annotations, no cleanup calls.

When a value is stored somewhere that outlives the current scope, Grayscale copies it to the outer scope:

```gray
mut results [string] = {}

for_each line in lines {
    mut upper = strings.to_upper(line)
    arrays.append(results, upper)         // upper escapes into results
}
// results lives until its scope ends
// each iteration's other temporaries are freed
```

### 11.2 Reference Semantics

Composite types (arrays, maps) have reference semantics for assignment but value semantics for function parameters (unless the parameter is declared mutable).

### 11.3 Deep Copy

The `copy()` function creates a deep copy of any value, including nested structures:

```gray
mut original = Person{name: "Alice", age: 30}
mut duplicate = copy(original)
duplicate.age = 31  // original.age is still 30
```

### 11.4 Zero Values

The `new()` function allocates a zero-initialized struct in the current scope and returns a pointer to it:

| Type | Zero Value |
|------|------------|
| `int/uint` | `0` |
| `float` | `0.0` |
| `string` | `""` |
| `bool` | `false` |
| `char` | `'\0'` |
| `byte` | `0` |
| `map[K:V]` | `{}` |
| struct | All fields zero-initialized |

### 11.5 Scoped Blocks

Three block types create memory scopes:

- **Function bodies:** temporaries freed on return, return values survive by copying to the caller's scope
- **Loop iterations** (`for`, `for_each`, `as_long_as`): each iteration's temporaries freed, values that escape into outer-scope containers survive
- **Conditional blocks** (`if`, `or`, `otherwise`): temporaries freed on block exit, values assigned to outer-scope variables survive

Nested scopes work correctly; a loop inside an if inside a function creates three scope levels, each cleaning up independently.

### 11.6 Manual Control

The `@mem` module provides explicit arena control for power users who need it:

```gray
import @mem

mut scratch = mem.arena(4096)
mut node = mem.init(scratch, Node)
// ... use node ...
mem.reset(scratch)
mem.destroy(scratch)
```

Most users never import the `@mem` module. ASBAM handles their allocations.

### 11.7 Memory Safety

Grayscale is **memory safe by default**. ASBAM prevents common memory errors automatically, and the compiler catches several more at compile time. Memory safety is not unconditionally guaranteed — opting into the `@mem` module or unsynchronized threading introduces hazards that the programmer is responsible for. But for programs that stay within Grayscale's defaults, memory safety holds without annotations or manual management.

**Compile-time checked:**

| Hazard | Grayscale Behavior |
|--------|-------------|
| Returning address of local variable | `addr()` of a local cannot appear in a return statement |
| Cross-scope pointer assignment | Warning when a pointer in an outer scope is assigned from `addr()` of a value in an inner scope |
| Double-free on `@mem` arenas | Straight-line double `mem.destroy()` on the same variable is rejected |

**Prevented by ASBAM:**

| Hazard | How |
|--------|-----|
| Memory leaks in long-running programs | Scopes free allocations on exit |
| Use-after-free (common case) | Out-of-scope values can't be named — if you can't reach it, it's freed |
| Dangling returns (common case) | Return values are copied to the caller's scope — the data moves, the pointer stays valid |
| Loop memory accumulation | Each iteration is a scope; temporaries cleaned up on iteration end |

**Runtime-checked (safe by default):**

| Hazard | Grayscale Behavior |
|--------|-------------|
| Nil pointer dereference | Runtime panic |
| Array out-of-bounds | Runtime panic |
| Map key not found | Runtime panic |
| Division by zero | Runtime panic |
| Integer overflow | Runtime panic (checked arithmetic) |
| Stack overflow (deep recursion) | Detected and reported |
| Double-free on `@mem` arenas (conditional/cross-function) | Runtime panic |

**Not checked (programmer responsibility):**

| Hazard | When It Can Happen |
|--------|-------------------|
| Use-after-free (`@mem` only) | Holding a pointer to `@mem` arena memory after `mem.destroy()` |
| Data races | Multiple threads accessing shared data without `sync.lock()` |
| Pointer arithmetic | Not supported in the language (disallowed by design) |

For most Grayscale programs, those that don't use the `@mem` module, raw pointers, or threading, ASBAM combined with compile-time checks and runtime panics provides practical safety without annotations or manual memory management.

### 11.8 Under the Hood

ASBAM is implemented using arena allocators. An arena is a block of memory that grows as needed and is freed all at once. There is no per-object deallocation — when a scope ends, its entire arena is discarded in a single O(1) operation.

#### Dual-Arena Architecture

Every Grayscale program starts with two arenas, and each thread gets its own independent pair:

- **The default arena** — used by all runtime allocations: strings, arrays, maps, and temporaries. This is the arena that scopes swap in and out. When a scope ends, this arena is either watermark-reset (void functions, blocks) or destroyed entirely (non-void functions).
- **The heap arena** — used exclusively by `new()`. It lives for the entire program and is never swapped or reset. This is why pointers returned by `new()` are always valid until the program exits.

The per-thread isolation means multithreaded programs never contend over memory allocation.

#### How Scopes Work

When a non-void function is called, a fresh arena is created and set as the active arena. All allocations inside that function go to this new arena. When the function returns, the return value is copied to the caller's arena, and the function's arena is destroyed.

Void functions use a cheaper strategy: they save a watermark on the current arena and reset to that point on exit, reclaiming memory without creating or destroying anything.

Functions that accept mutable reference parameters (`&`) skip scoping entirely and run directly in the caller's arena. This is necessary because writes to passed arrays or maps must survive the function call.

#### Block and Loop Scopes

Conditional blocks (`if`, `otherwise`) and loop iterations (`for`, `for_each`) each get their own arena. For loops, a new arena is created and destroyed on **every iteration**, which is why temporaries inside a loop never accumulate regardless of how many iterations run.

When a value created inside a scoped block needs to survive — for example, appending to an outer array inside a loop — Grayscale automatically copies it to the outer scope's arena before the inner arena is destroyed.

#### Early Exits

When `return`, `break`, or `continue` exits through nested scopes, the runtime unwinds all live arenas in reverse order. A `break` inside an `if` inside a loop will clean up the if-block arena and the loop iteration arena before jumping out. This ensures no memory is leaked regardless of control flow.

#### Growth

Arenas start at a fixed size but are not limited by it. If an allocation exceeds the remaining space, the arena chains a new, larger block automatically. An arena never fails due to its initial size being too small.

---

## 12. Program Execution

### 12.1 Program Structure

An Grayscale program consists of one or more source files that are compiled to a native binary by the Grayscale compiler. Each file may contain:

1. Import declarations
2. Using declarations
3. Top-level declarations (functions, structs, enums, constants)

### 12.2 Entry Point

Every Grayscale program must define a `main` function with no parameters and no return value:

```gray
do main() {
    // Program starts here
}
```

The `main` function is not called explicitly; it is invoked automatically when the program runs.

`main()` exits when control reaches its closing brace. An explicit `return` statement inside `main()` is not allowed. To terminate early, branch the remaining work behind an `if`/`otherwise`, or call `exit(code)` to end the program with a specific status.

### 12.3 Evaluation Order

Expressions are evaluated left-to-right. Function arguments are evaluated before the function is called.

Short-circuit evaluation applies to `&&` and `||`:

```gray
// If a is false, b() is not called
if a && b() { ... }

// If a is true, b() is not called
if a || b() { ... }
```

### 12.4 Program Termination

A program terminates when:

1. The `main` function returns
2. A runtime error occurs

---

## 13. CLI Commands

The `gray` command-line tool provides the following commands:

| Command | Description |
|---------|-------------|
| `gray <file.gray>` | Compile and run a source file |
| `gray build <file.gray>` | Compile to a distributable binary |
| `gray check <file.gray>` | Type-check without compiling |
| `gray watch <file.gray>` | Watch for changes and re-run on save |
| `gray fmt <path>` | Format source files |
| `gray doc <path>` | Generate documentation from `#doc` attributes |
| `gray new <name>` | Scaffold a new project |
| `gray man <name>` | Show documentation for builtins, stdlib, and language reference |
| `gray report` | Print system info for bug reports |
| `gray update` | Check for updates and upgrade |
| `gray install <version>` | Install a specific version by exact semver |
| `gray version` | Show version information |

### Global Flags

These flags are available on `gray <file>`, `build`, `check`, and `watch`:

| Flag | Description |
|------|-------------|
| `-q, --quiet <codes>` | Suppress warnings. Use `all` to suppress all, or a comma-separated list of codes (e.g. `W1001,W1003`). |
| `--no-color` | Disable colored diagnostic output. |

### 13.1 `gray <file.gray>`

Compile and run a source file in one step.

```
gray <file.gray> [flags] [-- args...]
```

Arguments after `--` are forwarded to the compiled program.

```bash
gray main.gray
gray main.gray -q all
gray main.gray -- --port 8080
```

### 13.2 `gray build`

Compile a source file to a native binary.

```
gray build <file.gray> [flags]
```

| Flag | Description |
|------|-------------|
| `-o, --output <name>` | Output binary name. Defaults to the input filename without `.gray`. |
| `--emit-c` | Emit the generated C source to a file without compiling to a binary. No binary is produced. Uses `-o` for the output path, or defaults to `<input>.c` (e.g., `main.gray` → `main.c`). |
| `--time` | Show compilation timing. |
| `-q, --quiet <codes>` | Suppress warnings. |
| `--no-color` | Disable colored output. |

```bash
gray build main.gray -o myapp
gray build main.gray --emit-c
gray build main.gray --emit-c -o output.c
gray build main.gray --time -q all
```

### 13.3 `gray check`

Type-check a file or project without compiling. Returns a non-zero exit code if errors are found.

```
gray check <file.gray | directory> [flags]
```

| Flag | Description |
|------|-------------|
| `-q, --quiet <codes>` | Suppress warnings. |

```bash
gray check main.gray
gray check src/
```

### 13.4 `gray watch`

Watch a file or directory for changes and re-run on save. Automatically discovers and watches imported files.

```
gray watch <file.gray | directory> [flags]
```

| Flag | Description |
|------|-------------|
| `-q, --quiet <codes>` | Suppress warnings. |
| `--no-color` | Disable colored output. |

When watching a directory, Grayscale finds the file containing `main()` and watches all `.gray` files in the directory.

```bash
gray watch main.gray
gray watch src/
```

### 13.5 `gray fmt`

Format `.gray` source files in place. Normalizes indentation to 4 spaces, removes trailing whitespace, ensures a final newline, and collapses runs of more than 2 blank lines.

```
gray fmt <path> [flags]
```

| Flag | Description |
|------|-------------|
| `--check` | Check formatting without modifying files. Exits non-zero if any file would change. Intended for CI. |

Supported path patterns:

| Pattern | Scope |
|---------|-------|
| `file.gray` | Single file |
| `dir` | All `.gray` files in directory (non-recursive) |
| `./...` or `dir/...` | Recursive walk for all `.gray` files |

```bash
gray fmt main.gray
gray fmt src/
gray fmt ./...
gray fmt --check ./...
```

### 13.6 `gray doc`

Generate markdown documentation from `#doc` attributes in source files.

```
gray doc <path> [flags]
```

| Flag | Description |
|------|-------------|
| `-o, --output <path>` | Output file path. Defaults to `DOCS.md`. |

Supports the same path patterns as `gray fmt` (single file, directory, `./...` recursive).

```bash
gray doc main.gray
gray doc ./...
gray doc src/ -o API.md
```

### 13.7 `gray new`

Scaffold a new Grayscale project.

```
gray new [project-name] [flags]
```

| Flag | Description |
|------|-------------|
| `-t, --template <name>` | Template to use. One of: `basic`, `cli`, `lib`, `multi`, `server`, `client`. Defaults to `basic`. |
| `-s, --server-type <type>` | `minimal` or `normal`. Only applies to `server` and `client` templates. Defaults to `normal`. |
| `-c, --comments` | Include a quick-reference comment block in the entry file. |
| `-f, --force` | Overwrite an existing directory. |

Running `gray new` with no arguments enters interactive mode, which prompts for the project name, template, and options.

```bash
gray new myapp
gray new myapp -t cli
gray new myapi -t server -s minimal
gray new myapp -t basic -c
gray new                          # interactive mode
```

### 13.8 `gray man`

Show documentation for builtin functions, stdlib modules, stdlib types, and language reference (keywords, types, symbols, attributes).

```
gray man [name]
```

| Argument | Result |
|----------|--------|
| *(none)* | Show usage and list available stdlib modules. |
| `builtins` | List all builtin functions by category. |
| `<module>` | List all functions and types in a stdlib module. |
| `<name>` | Show documentation for a specific function or type. |
| `<module>.<name>` | Qualified lookup to resolve ambiguity. |
| `lang` | Overview of all language reference categories. |
| `keywords` | List all keywords by category. |
| `types` | List all types by category. |
| `symbols` | List all symbols. |
| `attributes` | List all attributes. |

```bash
gray man
gray man builtins
gray man math
gray man println
gray man strings.contains
gray man lang
gray man keywords
gray man struct
gray man i8
gray man flags
```

> 💡 **Tip:** Do not include `()` in the name — the shell interprets bare parentheses as a function definition before `gray` sees them. Use the name alone: `gray man println`, not `gray man println()`.

> 💡 **Tip:** For attributes, omit the `#` prefix — the shell treats `#` as a comment. Use `gray man flags`, not `gray man #flags`.

### 13.9 `gray report`

Print system information for filing bug reports.

```
gray report
```

Output includes Grayscale version, commit hash, OS, CPU, RAM, C compiler version, and target triple.

### 13.10 `gray update`

Check for updates and upgrade to a newer version.

```
gray update [flags]
```

| Flag | Description |
|------|-------------|
| `--pre` | Install the latest pre-release (alpha/beta/rc) instead of latest stable. |

```bash
gray update
gray update --pre
```

### 13.11 `gray install`

Install a specific Grayscale version by exact semver, replacing the current installation. Supports downgrades and pre-release tags.

```
gray install <version>
```

```bash
gray install 3.0.0
gray install 3.1.0-beta.2
```

### 13.12 `gray version`

Show the installed version, build commit, build timestamp, and whether newer versions are available.

```
gray version
```

---

*This document is the authoritative specification for the Grayscale programming language.*
