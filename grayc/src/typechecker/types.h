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

typedef enum {
    TK_VOID,
    TK_INT,
    TK_UINT,    /* unsigned integer types: uint, u8, u16, u32, u64, u128 */
    TK_FLOAT,
    TK_BOOL,
    TK_CHAR,
    TK_BYTE,
    TK_STRING,
    TK_ARRAY,
    TK_MAP,
    TK_STRUCT,
    TK_ENUM,
    TK_POINTER,
    TK_ERROR,
    TK_FUNCTION,
    TK_NIL,
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
    const char *name;           /* "int", "string", "Person", "[int]", etc. */
    const char *element_type;   /* For arrays: element type name */
    const char *key_type;       /* For maps: key type name */
    const char *value_type;     /* For maps: value type name */
    GrayFuncSig *func_sig;        /* For TK_FUNCTION: parsed signature */
} GrayType;

/* Built-in type singletons */
extern GrayType TYPE_VOID;
extern GrayType TYPE_INT;
extern GrayType TYPE_UINT;
extern GrayType TYPE_FLOAT;
extern GrayType TYPE_BOOL;
extern GrayType TYPE_CHAR;
extern GrayType TYPE_BYTE;
extern GrayType TYPE_STRING;
extern GrayType TYPE_NIL;
extern GrayType TYPE_UNKNOWN;

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
bool type_equals(GrayType *left, GrayType *right);
const char *type_name(GrayType *type);

/* Resolve a type name string to an GrayType */
GrayType *type_from_name(const char *name);

/* Free all heap strings owned by pool entries and reset the pool */
void type_pool_reset(void);

/* --- Type-name string predicates --- */

static inline bool is_unsigned_type(const char *tn) {
    if (!tn) return false;
    return strcmp(tn, "uint") == 0 || strcmp(tn, "u8") == 0 ||
           strcmp(tn, "u16") == 0 || strcmp(tn, "u32") == 0 ||
           strcmp(tn, "u64") == 0 || strcmp(tn, "u128") == 0 ||
           strcmp(tn, "u256") == 0 || strcmp(tn, "byte") == 0;
}

static inline bool is_signed_int_type(const char *tn) {
    if (!tn) return false;
    return strcmp(tn, "int") == 0 || strcmp(tn, "i8") == 0 ||
           strcmp(tn, "i16") == 0 || strcmp(tn, "i32") == 0 ||
           strcmp(tn, "i64") == 0 || strcmp(tn, "i128") == 0 ||
           strcmp(tn, "i256") == 0;
}

static inline bool is_any_int_type(const char *tn) {
    return is_signed_int_type(tn) || is_unsigned_type(tn);
}

static inline bool is_bigint_type(const char *tn) {
    if (!tn) return false;
    return strcmp(tn, "i128") == 0 || strcmp(tn, "u128") == 0 ||
           strcmp(tn, "i256") == 0 || strcmp(tn, "u256") == 0;
}

#endif
