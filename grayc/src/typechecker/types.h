/*
 * types.h — Core type representation for the Grayscale type system, declaring
 * TypeKind, the GrayType struct, and the built-in type singletons used by the
 * type checker and codegen.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_TYPES_H
#define GRAYC_TYPES_H

#include <stdbool.h>
#include <string.h>
#include "../lexer/token.h"

typedef enum {
    TYPE_KIND_VOID,
    TYPE_KIND_SIGNED_INTEGER,
    TYPE_KIND_UNSIGNED_INTEGER,    /* unsigned integer types: u8, u16, u32, u64, u128, u256 */
    TYPE_KIND_FLOATING_POINT,
    TYPE_KIND_BOOL,
    TYPE_KIND_CHAR,
    TYPE_KIND_STRING,
    TYPE_KIND_ARRAY,
    TYPE_KIND_MAP,
    TYPE_KIND_STRUCT,
    TYPE_KIND_ENUM,
    TYPE_KIND_POINTER,
    TYPE_KIND_ERROR,
    TYPE_KIND_FUNCTION,
    TYPE_KIND_NIL,
    /* The result of an `extern.` C function call. Its real type is known only
     * to the C compiler, so it is deliberately incompatible with every
     * Grayscale type: it may only be consumed by an explicitly type-annotated
     * declaration, another `extern.` call, `c_string()`, or `cast()`. Distinct
     * from TYPE_KIND_UNKNOWN so the type-compatibility checks (which skip TYPE_KIND_UNKNOWN
     * for error recovery) still run against it. */
    TYPE_KIND_C_FUNCTION,
    /* A number literal, or an expression built only from number literals,
     * that has not taken a type yet. It takes one from the slot it is stored
     * into, the other operand of a binary operator, or a cast; otherwise it
     * becomes i64 (f64 when is_decimal) when its statement finishes. No
     * expression keeps this type once typechecking is done. */
    TYPE_KIND_LITERAL,
    TYPE_KIND_UNKNOWN,
} TypeKind;

/* Signature payload for TYPE_KIND_FUNCTION (typed function references).
 * parameter_types[i] is the Grayscale type-name string for parameter i (no `&` prefix);
 * is_parameter_mutable[i] tracks the `&` flag separately. return_count==0 means
 * void. The strings are owned elsewhere (parser arena). */
typedef struct {
    int parameter_count;
    const char **parameter_types;
    bool *is_parameter_mutable;
    int return_count;
    const char **return_types;
} GrayFunctionSignature;

typedef struct GrayType {
    TypeKind kind;
    const char *name;           /* "i64", "string", "Person", "[i64]", etc. */
    const char *element_type;   /* For arrays: element type name */
    const char *key_type;       /* For maps: key type name */
    const char *value_type;     /* For maps: value type name */
    GrayFunctionSignature *function_signature; /* For TYPE_KIND_FUNCTION: parsed signature */
    bool is_decimal;            /* For TYPE_KIND_LITERAL: written with a decimal point or exponent */
} GrayType;

/* Built-in type singletons */
extern GrayType TYPE_VOID;
extern GrayType TYPE_I64;
extern GrayType TYPE_U64;
extern GrayType TYPE_F64;
extern GrayType TYPE_BOOL;
extern GrayType TYPE_CHAR;
extern GrayType TYPE_U8;
extern GrayType TYPE_STRING;
extern GrayType TYPE_NIL;
extern GrayType TYPE_UNKNOWN;
extern GrayType TYPE_C_FUNCTION;
extern GrayType TYPE_LITERAL_INTEGER;
extern GrayType TYPE_LITERAL_DECIMAL;

/* Type constructors */
GrayType *type_array(const char *element_type);
GrayType *type_struct(const char *name);
GrayType *type_enum(const char *name);
GrayType *type_pointer(const char *pointee_type);

/* Allocate a new type (from internal pool) */
GrayType *type_allocate(void);

/* Type queries */
bool type_is_numeric(GrayType *type);
bool type_is_integer(GrayType *type);
const char *type_name(GrayType *type);

/* How a value of one type converts to another. This and
 * type_binary_result() are the only definition of the sized-type rules. */
typedef enum {
    CONVERSION_SAME,        /* the same type */
    CONVERSION_WIDEN,       /* every value fits: i32 -> i64, u8 -> i16, f32 -> f64 */
    CONVERSION_NARROW,      /* a wider type: i64 -> i32, u64 -> u8, i64 -> u8, f64 -> f32 */
    CONVERSION_SIGN_CROSS,  /* signed <-> unsigned of the same or greater width: i64 -> u64 */
    CONVERSION_MISMATCH,    /* no implicit conversion */
} Conversion;

/* The conversion of a value of type `source` into a slot of type `target`. Neither
 * may be TYPE_KIND_LITERAL: a literal takes its slot's type instead of converting. */
Conversion type_conversion(GrayType *source, GrayType *target);

/* The type of `left operator right` for numeric operands (integer, floating-point, or
 * TYPE_KIND_LITERAL), or NULL when the pair needs a cast. A comparison yields bool.
 * When both operands are literals the result is a literal. */
GrayType *type_binary_result(TokenType operator, GrayType *left, GrayType *right);

/* True for the integer and floating-point kinds a sized-type rule applies to. */
static inline bool type_kind_is_number(TypeKind kind) {
    return kind == TYPE_KIND_SIGNED_INTEGER || kind == TYPE_KIND_UNSIGNED_INTEGER || kind == TYPE_KIND_FLOATING_POINT;
}

/* Resolve a type name string to an GrayType */
GrayType *type_from_name(const char *name);

/* Return true if name matches a builtin type keyword (i64, string, etc.) */
bool is_builtin_type_name(const char *name);

/* Free all heap strings owned by pool entries and reset the pool */
void type_pool_reset(void);

/* --- Type-name string predicates --- */

/* Every named integer type is an i/u followed by a width. Gate on the first
 * character so a name that is none of them (a struct name, "string", "f64")
 * costs one comparison instead of the whole ladder. */

static inline bool is_unsigned_integer_type_name(const char *type_name) {
    if (!type_name || type_name[0] != 'u') return false;
    return strcmp(type_name, "u8") == 0 ||
           strcmp(type_name, "u16") == 0 || strcmp(type_name, "u32") == 0 ||
           strcmp(type_name, "u64") == 0 || strcmp(type_name, "u128") == 0 ||
           strcmp(type_name, "u256") == 0;
}

static inline bool is_signed_integer_type_name(const char *type_name) {
    if (!type_name || type_name[0] != 'i') return false;
    return strcmp(type_name, "i8") == 0 ||
           strcmp(type_name, "i16") == 0 || strcmp(type_name, "i32") == 0 ||
           strcmp(type_name, "i64") == 0 || strcmp(type_name, "i128") == 0 ||
           strcmp(type_name, "i256") == 0;
}

static inline bool is_integer_type_name(const char *type_name) {
    return is_signed_integer_type_name(type_name) || is_unsigned_integer_type_name(type_name);
}

/* A wide integer is a 128- or 256-bit integer type: i128, u128, i256, u256. */
static inline bool is_wide_integer_type_name(const char *type_name) {
    if (!type_name || (type_name[0] != 'i' && type_name[0] != 'u')) return false;
    return strcmp(type_name, "i128") == 0 || strcmp(type_name, "u128") == 0 ||
           strcmp(type_name, "i256") == 0 || strcmp(type_name, "u256") == 0;
}

#endif
