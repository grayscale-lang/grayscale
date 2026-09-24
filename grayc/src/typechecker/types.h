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
    TK_VOID,
    TK_INT,
    TK_UINT,    /* unsigned integer types: u8, u16, u32, u64, u128, u256 */
    TK_FLOAT,
    TK_BOOL,
    TK_CHAR,
    TK_STRING,
    TK_ARRAY,
    TK_MAP,
    TK_STRUCT,
    TK_ENUM,
    TK_POINTER,
    TK_ERROR,
    TK_FUNCTION,
    TK_NIL,
    /* The result of an `extern.` C function call. Its real type is known only
     * to the C compiler, so it is deliberately incompatible with every
     * Grayscale type: it may only be consumed by an explicitly type-annotated
     * declaration, another `extern.` call, `c_string()`, or `cast()`. Distinct
     * from TK_UNKNOWN so the type-compatibility checks (which skip TK_UNKNOWN
     * for error recovery) still run against it. */
    TK_C_FUNC,
    /* A number literal, or an expression built only from number literals,
     * that has not taken a type yet. It takes one from the slot it is stored
     * into, the other operand of a binary operator, or a cast; otherwise it
     * becomes i64 (f64 when is_decimal) when its statement finishes. No
     * expression keeps this type once typechecking is done. */
    TK_LITERAL,
    TK_UNKNOWN,
} TypeKind;

/* Signature payload for TK_FUNCTION (typed function references).
 * param_types[i] is the Grayscale type-name string for parameter i (no `&` prefix);
 * param_mutable[i] tracks the `&` flag separately. return_count==0 means
 * void. The strings are owned elsewhere (parser arena). */
typedef struct {
    int param_count;
    const char **param_types;
    bool *param_mutable;
    int return_count;
    const char **return_types;
} GrayFuncSig;

typedef struct GrayType {
    TypeKind kind;
    const char *name;           /* "i64", "string", "Person", "[i64]", etc. */
    const char *element_type;   /* For arrays: element type name */
    const char *key_type;       /* For maps: key type name */
    const char *value_type;     /* For maps: value type name */
    GrayFuncSig *func_sig;        /* For TK_FUNCTION: parsed signature */
    bool is_decimal;            /* For TK_LITERAL: written with a decimal point or exponent */
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
extern GrayType TYPE_C_FUNC;
extern GrayType TYPE_LITERAL_INT;
extern GrayType TYPE_LITERAL_DECIMAL;

/* Type constructors */
GrayType *type_array(const char *elem_type);
GrayType *type_struct(const char *name);
GrayType *type_enum(const char *name);
GrayType *type_pointer(const char *pointee_type);

/* Allocate a new type (from internal pool) */
GrayType *type_alloc(void);

/* Type queries */
bool type_is_numeric(GrayType *type);
bool type_is_integer(GrayType *type);
const char *type_name(GrayType *type);

/* How a value of one type converts to another. This and
 * type_binary_result() are the only definition of the sized-type rules. */
typedef enum {
    CONV_SAME,        /* the same type */
    CONV_WIDEN,       /* every value fits: i32 -> i64, u8 -> i16, f32 -> f64 */
    CONV_NARROW,      /* a wider type: i64 -> i32, u64 -> u8, i64 -> u8, f64 -> f32 */
    CONV_SIGN_CROSS,  /* signed <-> unsigned of the same or greater width: i64 -> u64 */
    CONV_MISMATCH,    /* no implicit conversion */
} Conversion;

/* The conversion of a value of type `from` into a slot of type `to`. Neither
 * may be TK_LITERAL: a literal takes its slot's type instead of converting. */
Conversion type_conversion(GrayType *from, GrayType *to);

/* The type of `left op right` for numeric operands (integer, float, or
 * TK_LITERAL), or NULL when the pair needs a cast. A comparison yields bool.
 * When both operands are literals the result is a literal. */
GrayType *type_binary_result(TokenType op, GrayType *left, GrayType *right);

/* True for the integer and float kinds a sized-type rule applies to. */
static inline bool type_kind_is_number(TypeKind k) {
    return k == TK_INT || k == TK_UINT || k == TK_FLOAT;
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

static inline bool is_unsigned_type(const char *tn) {
    if (!tn || tn[0] != 'u') return false;
    return strcmp(tn, "u8") == 0 ||
           strcmp(tn, "u16") == 0 || strcmp(tn, "u32") == 0 ||
           strcmp(tn, "u64") == 0 || strcmp(tn, "u128") == 0 ||
           strcmp(tn, "u256") == 0;
}

static inline bool is_signed_int_type(const char *tn) {
    if (!tn || tn[0] != 'i') return false;
    return strcmp(tn, "i8") == 0 ||
           strcmp(tn, "i16") == 0 || strcmp(tn, "i32") == 0 ||
           strcmp(tn, "i64") == 0 || strcmp(tn, "i128") == 0 ||
           strcmp(tn, "i256") == 0;
}

static inline bool is_any_int_type(const char *tn) {
    return is_signed_int_type(tn) || is_unsigned_type(tn);
}

static inline bool is_bigint_type(const char *tn) {
    if (!tn || (tn[0] != 'i' && tn[0] != 'u')) return false;
    return strcmp(tn, "i128") == 0 || strcmp(tn, "u128") == 0 ||
           strcmp(tn, "i256") == 0 || strcmp(tn, "u256") == 0;
}

#endif
