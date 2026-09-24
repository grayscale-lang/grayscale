/*
 * types.c — Defines built-in type singletons and provides constructors for
 * composite types such as arrays, maps, pointers, and function signatures.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "types.h"
#include "../util/constants.h"
#include "../util/xalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

/* Built-in type singletons */
GrayType TYPE_VOID    = {TYPE_KIND_VOID,   "void",   NULL, NULL, NULL, NULL, false};
GrayType TYPE_I64     = {TYPE_KIND_SIGNED_INTEGER,    "i64",    NULL, NULL, NULL, NULL, false};
GrayType TYPE_U64     = {TYPE_KIND_UNSIGNED_INTEGER,   "u64",    NULL, NULL, NULL, NULL, false};
GrayType TYPE_F64     = {TYPE_KIND_FLOATING_POINT,  "f64",    NULL, NULL, NULL, NULL, false};
GrayType TYPE_BOOL    = {TYPE_KIND_BOOL,   "bool",   NULL, NULL, NULL, NULL, false};
GrayType TYPE_CHAR    = {TYPE_KIND_CHAR,   "char",   NULL, NULL, NULL, NULL, false};
GrayType TYPE_U8      = {TYPE_KIND_UNSIGNED_INTEGER,   "u8",     NULL, NULL, NULL, NULL, false};
GrayType TYPE_STRING  = {TYPE_KIND_STRING, "string", NULL, NULL, NULL, NULL, false};
GrayType TYPE_NIL     = {TYPE_KIND_NIL,    "nil",    NULL, NULL, NULL, NULL, false};
GrayType TYPE_UNKNOWN = {TYPE_KIND_UNKNOWN,"unknown",NULL, NULL, NULL, NULL, false};
GrayType TYPE_C_FUNCTION  = {TYPE_KIND_C_FUNCTION, "a C interop value", NULL, NULL, NULL, NULL, false};
GrayType TYPE_LITERAL_INTEGER     = {TYPE_KIND_LITERAL, "integer literal", NULL, NULL, NULL, NULL, false};
GrayType TYPE_LITERAL_DECIMAL = {TYPE_KIND_LITERAL, "decimal literal", NULL, NULL, NULL, NULL, true};

/* Pool for dynamically created types — one entry per distinct composite type
 * (struct, enum, pointer, array, map, function signature), accumulated for the
 * whole compile. A GrayType* handed out here is cached throughout the checker
 * and codegen, so entries must never move: the pool is a linked list of fixed
 * blocks, not one growable array. Heap-allocated names/sigs are released by
 * type_pool_reset() during typechecker teardown. */
#define TYPE_POOL_BLOCK_SIZE 1024

typedef struct TypePoolBlock {
    struct TypePoolBlock *next;
    GrayType types[TYPE_POOL_BLOCK_SIZE];
} TypePoolBlock;

static TypePoolBlock *type_pool_head = NULL;
static TypePoolBlock *type_pool_tail = NULL;
static int type_pool_count = 0;   /* total entries across every block */
static int type_pool_fill  = 0;   /* entries used in the tail block */

GrayType *type_allocate(void) {
    if (!type_pool_tail || type_pool_fill == TYPE_POOL_BLOCK_SIZE) {
        TypePoolBlock *block = xcalloc(1, sizeof(TypePoolBlock));
        if (type_pool_tail) type_pool_tail->next = block;
        else type_pool_head = block;
        type_pool_tail = block;
        type_pool_fill = 0;
    }
    type_pool_count++;
    return &type_pool_tail->types[type_pool_fill++];
}

/* Hash index for O(1) pool_find. Power-of-two capacity, doubled and rehashed
 * before the load factor reaches 50% — linear probing needs that headroom to
 * terminate on a free slot. */
#define TYPE_HASH_INITIAL_CAPACITY 8192

typedef struct {
    TypeKind kind;
    const char *name;  /* points into the pool entry's name — not a copy */
    GrayType    *type;   /* NULL = empty slot */
} TypeHashEntry;

static TypeHashEntry *type_hash_table = NULL;
static uint32_t type_hash_capacity = 0;

static uint32_t type_hash(TypeKind kind, const char *name) {
    uint32_t hash = 5381u ^ ((uint32_t)kind * 2654435761u);
    for (const unsigned char *cursor = (const unsigned char *)name; *cursor; cursor++)
        hash = hash * 33u ^ (uint32_t)*cursor;
    /* djb2's low bits barely move between similar names ([i64], [i8], [i16]..),
     * and pool_find masks to the low bits then linear-probes. Finalize first so
     * the high bits (which are well mixed) reach the bucket index. */
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash;
}

/* Return an existing pool entry matching kind+name, or NULL if not found. */
static GrayType *pool_find(TypeKind kind, const char *name) {
    if (!name || !type_hash_table) return NULL;
    uint32_t mask = type_hash_capacity - 1;
    uint32_t slot  = type_hash(kind, name) & mask;
    for (;;) {
        TypeHashEntry *entry = &type_hash_table[slot];
        if (!entry->type) return NULL;
        if (entry->kind == kind && strcmp(entry->name, name) == 0) return entry->type;
        slot = (slot + 1) & mask;
    }
}

/* Place an entry into `table` by open addressing. Only ever called with a table
 * known to have a free slot, so the probe terminates. */
static void type_hash_place(TypeHashEntry *table, uint32_t capacity,
                            TypeKind kind, const char *name, GrayType *type) {
    uint32_t mask = capacity - 1;
    uint32_t slot  = type_hash(kind, name) & mask;
    for (;;) {
        TypeHashEntry *entry = &table[slot];
        if (!entry->type) {
            entry->kind = kind;
            entry->name = name;
            entry->type = type;
            return;
        }
        slot = (slot + 1) & mask;
    }
}

/* Double the hash index (or create it) and reinsert every live entry. */
static void type_hash_grow(void) {
    uint32_t old_capacity = type_hash_capacity;
    TypeHashEntry *old_table = type_hash_table;
    uint32_t new_capacity = old_capacity ? old_capacity * 2u : TYPE_HASH_INITIAL_CAPACITY;

    type_hash_table = xcalloc((size_t)new_capacity, sizeof(TypeHashEntry));
    type_hash_capacity = new_capacity;

    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_table[i].type)
            type_hash_place(type_hash_table, new_capacity,
                            old_table[i].kind, old_table[i].name, old_table[i].type);
    }
    free(old_table);
}

/* Insert a newly created type into the hash index. */
static void pool_insert(TypeKind kind, const char *name, GrayType *type) {
    /* Keep the load factor below 50% so pool_find's linear probe terminates. */
    if ((uint32_t)type_pool_count * 2u >= type_hash_capacity)
        type_hash_grow();
    type_hash_place(type_hash_table, type_hash_capacity, kind, name, type);
}

GrayType *type_array(const char *element_type) {
    GrayType *existing = pool_find(TYPE_KIND_ARRAY, element_type);
    if (existing) return existing;
    GrayType *type = type_allocate();
    type->kind = TYPE_KIND_ARRAY;
    const char *name_copy = strdup(element_type);
    type->element_type = name_copy;
    type->name = name_copy;
    pool_insert(TYPE_KIND_ARRAY, type->name, type);
    return type;
}

GrayType *type_struct(const char *name) {
    GrayType *existing = pool_find(TYPE_KIND_STRUCT, name);
    if (existing) return existing;
    GrayType *type = type_allocate();
    type->kind = TYPE_KIND_STRUCT;
    type->name = strdup(name);
    pool_insert(TYPE_KIND_STRUCT, type->name, type);
    return type;
}

GrayType *type_enum(const char *name) {
    GrayType *existing = pool_find(TYPE_KIND_ENUM, name);
    if (existing) return existing;
    GrayType *type = type_allocate();
    type->kind = TYPE_KIND_ENUM;
    type->name = strdup(name);
    pool_insert(TYPE_KIND_ENUM, type->name, type);
    return type;
}

GrayType *type_pointer(const char *pointee_type) {
    GrayType *existing = pool_find(TYPE_KIND_POINTER, pointee_type);
    if (existing) return existing;
    GrayType *type = type_allocate();
    type->kind = TYPE_KIND_POINTER;
    const char *name_copy = strdup(pointee_type);
    type->element_type = name_copy;
    type->name = name_copy;
    pool_insert(TYPE_KIND_POINTER, type->name, type);
    return type;
}

/* Find the index of the ')' that closes the '(' at index 4 — the parameter
 * list of a "func(...)" type string. Tracks nesting so that nested func(...)
 * types (e.g. func(func(i64)->i64)->bool) balance correctly. Returns -1 if
 * not balanced. Precondition: the caller has verified the "func(" prefix. */
static int find_function_signature_close_parenthesis(const char *type_text) {
    int depth = 0;
    for (int i = 4; type_text[i]; i++) {
        if (type_text[i] == '(') depth++;
        else if (type_text[i] == ')') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/* Split src[start..end-1] on commas at top-level depth, dup each segment.
 * Sets *out_count and allocates *out_arr (caller frees the array; the dup'd
 * strings are handed to the type pool and freed by type_pool_reset()). */
static void split_top_commas(const char *source, int start, int end,
                              int *out_count, char ***out_segments) {
    *out_count = 0;
    *out_segments = NULL;
    if (end <= start) return;
    int capacity = 4;
    char **segments = (char **)xmalloc(sizeof(char *) * (size_t)capacity);
    int segment_count = 0;
    int depth = 0;
    int segment_start = start;
    for (int i = start; i <= end; i++) {
        char character = (i < end) ? source[i] : ',';
        if (character == '(' || character == '[') depth++;
        else if (character == ')' || character == ']') depth--;
        if (depth == 0 && (character == ',' || i == end)) {
            int segment_length = i - segment_start;
            char *segment = (char *)xmalloc((size_t)segment_length + 1);
            memcpy(segment, source + segment_start, (size_t)segment_length);
            segment[segment_length] = '\0';
            if (segment_count == capacity) {
                capacity *= 2;
                segments = (char **)xrealloc(segments, sizeof(char *) * (size_t)capacity);
            }
            segments[segment_count++] = segment;
            segment_start = i + 1;
        }
    }
    *out_count = segment_count;
    *out_segments = segments;
}

/* Parse "func(p1,&p2,...)->R" or "func(...)->()" or "func(...)->(R1,R2)"
 * into an GrayFunctionSignature. Returns NULL if the string isn't well-formed. */
static GrayFunctionSignature *parse_function_signature(const char *name) {
    if (strncmp(name, "func(", 5) != 0) return NULL;
    int close_parenthesis_index = find_function_signature_close_parenthesis(name);
    if (close_parenthesis_index < 0) return NULL;
    GrayFunctionSignature *signature = (GrayFunctionSignature *)xmalloc(sizeof(GrayFunctionSignature));
    signature->parameter_count = 0;
    signature->parameter_types = NULL;
    signature->is_parameter_mutable = NULL;
    signature->return_count = 0;
    signature->return_types = NULL;

    /* Params */
    char **raw_parameters = NULL;
    int raw_parameter_count = 0;
    split_top_commas(name, 5, close_parenthesis_index, &raw_parameter_count, &raw_parameters);
    signature->parameter_count = raw_parameter_count;
    if (raw_parameter_count > 0) {
        signature->parameter_types = (const char **)xmalloc(sizeof(char *) * (size_t)raw_parameter_count);
        signature->is_parameter_mutable = (bool *)xmalloc(sizeof(bool) * (size_t)raw_parameter_count);
        for (int i = 0; i < raw_parameter_count; i++) {
            const char *raw_parameter = raw_parameters[i];
            if (raw_parameter[0] == '&') {
                signature->is_parameter_mutable[i] = true;
                signature->parameter_types[i] = strdup(raw_parameter + 1);
            } else {
                signature->is_parameter_mutable[i] = false;
                signature->parameter_types[i] = strdup(raw_parameter);
            }
            free(raw_parameters[i]);
        }
    }
    free(raw_parameters);

    /* Return */
    const char *after = name + close_parenthesis_index + 1;
    if (after[0] == '-' && after[1] == '>') {
        const char *return_text = after + 2;
        if (return_text[0] == '(') {
            int return_text_length = (int)strlen(return_text);
            if (return_text[return_text_length - 1] != ')') return signature; /* malformed; keep params */
            char **return_segments = NULL;
            int return_count_parsed = 0;
            split_top_commas(return_text, 1, return_text_length - 1, &return_count_parsed, &return_segments);
            signature->return_count = return_count_parsed;
            if (return_count_parsed > 0) {
                signature->return_types = (const char **)xmalloc(sizeof(char *) * (size_t)return_count_parsed);
                for (int i = 0; i < return_count_parsed; i++) {
                    signature->return_types[i] = return_segments[i];
                }
            }
            free(return_segments);
        } else if (strcmp(return_text, "void") != 0) {
            signature->return_count = 1;
            signature->return_types = (const char **)xmalloc(sizeof(char *));
            signature->return_types[0] = strdup(return_text);
        }
    }
    return signature;
}

/* Free the heap-owned names/signatures of one pool entry. */
static void type_pool_free_entry(GrayType *type) {
    switch (type->kind) {
    case TYPE_KIND_STRUCT:
    case TYPE_KIND_ENUM:
    case TYPE_KIND_ARRAY:
    case TYPE_KIND_POINTER:
        /* For ARRAY and POINTER, type->name == type->element_type (same heap
         * pointer); free once via type->name. */
        free((char *)type->name);
        break;
    case TYPE_KIND_MAP:
        free((char *)type->name);
        free((char *)type->key_type);
        free((char *)type->value_type);
        break;
    case TYPE_KIND_FUNCTION:
        free((char *)type->name);
        if (type->function_signature) {
            for (int index = 0; index < type->function_signature->parameter_count; index++)
                free((char *)type->function_signature->parameter_types[index]);
            free(type->function_signature->parameter_types);
            free(type->function_signature->is_parameter_mutable);
            for (int index = 0; index < type->function_signature->return_count; index++)
                free((char *)type->function_signature->return_types[index]);
            free(type->function_signature->return_types);
            free(type->function_signature);
        }
        break;
    default:
        /* Builtin non-singleton pool entries (Error, i8, f32, u64, …).
         * type_from_name copies every one of these names, so there is no
         * kind whose name must be left alone. */
        free((char *)type->name);
        break;
    }
}

void type_pool_reset(void) {
    int remaining = type_pool_count;
    TypePoolBlock *block = type_pool_head;
    while (block) {
        int in_block = remaining < TYPE_POOL_BLOCK_SIZE ? remaining : TYPE_POOL_BLOCK_SIZE;
        for (int i = 0; i < in_block; i++)
            type_pool_free_entry(&block->types[i]);
        remaining -= in_block;
        TypePoolBlock *next = block->next;
        free(block);
        block = next;
    }
    type_pool_head = type_pool_tail = NULL;
    type_pool_count = 0;
    type_pool_fill = 0;

    free(type_hash_table);
    type_hash_table = NULL;
    type_hash_capacity = 0;
}

/* Width in bits of a sized integer or floating-point type name; 0 for anything else. */
static int number_width(const char *name) {
    if (!name || (name[0] != 'i' && name[0] != 'u' && name[0] != 'f')) return 0;
    const char *digits = name + 1;
    if (strcmp(digits, "8") == 0)   return 8;
    if (strcmp(digits, "16") == 0)  return 16;
    if (strcmp(digits, "32") == 0)  return 32;
    if (strcmp(digits, "64") == 0)  return 64;
    if (strcmp(digits, "128") == 0) return 128;
    if (strcmp(digits, "256") == 0) return 256;
    return 0;
}

Conversion type_conversion(GrayType *source, GrayType *target) {
    if (!source || !target || !source->name || !target->name) return CONVERSION_MISMATCH;
    if (type_kind_is_number(source->kind) && type_kind_is_number(target->kind)) {
        if (strcmp(source->name, target->name) == 0) return CONVERSION_SAME;
        int from_width = number_width(source->name);
        int to_width = number_width(target->name);
        if (source->kind == TYPE_KIND_FLOATING_POINT) {
            if (target->kind != TYPE_KIND_FLOATING_POINT) return CONVERSION_MISMATCH;
            return to_width > from_width ? CONVERSION_WIDEN : CONVERSION_NARROW;
        }
        /* An integer stored into a floating-point slot has always been accepted. */
        if (target->kind == TYPE_KIND_FLOATING_POINT) return CONVERSION_WIDEN;
        if (to_width < from_width) return CONVERSION_NARROW;
        if (source->kind == target->kind) return CONVERSION_WIDEN;
        /* An unsigned value fits every strictly wider signed type. */
        if (source->kind == TYPE_KIND_UNSIGNED_INTEGER && to_width > from_width) return CONVERSION_WIDEN;
        return CONVERSION_SIGN_CROSS;
    }
    if (source->kind != target->kind) return CONVERSION_MISMATCH;
    if (source->kind == TYPE_KIND_ARRAY)
        return source->element_type && target->element_type &&
               strcmp(source->element_type, target->element_type) == 0 ? CONVERSION_SAME : CONVERSION_MISMATCH;
    if (source->kind == TYPE_KIND_MAP)
        return source->key_type && target->key_type && source->value_type && target->value_type &&
               strcmp(source->key_type, target->key_type) == 0 &&
               strcmp(source->value_type, target->value_type) == 0 ? CONVERSION_SAME : CONVERSION_MISMATCH;
    return strcmp(source->name, target->name) == 0 ? CONVERSION_SAME : CONVERSION_MISMATCH;
}

GrayType *type_binary_result(TokenType operator, GrayType *left, GrayType *right) {
    bool is_comparison = operator == TOKEN_EQUAL || operator == TOKEN_NOT_EQUAL || operator == TOKEN_LESS_THAN ||
                         operator == TOKEN_GREATER_THAN || operator == TOKEN_LESS_THAN_OR_EQUAL || operator == TOKEN_GREATER_THAN_OR_EQUAL;
    bool is_shift = operator == TOKEN_BIT_SHIFT_LEFT || operator == TOKEN_BIT_SHIFT_RIGHT;
    bool is_integers_only = is_shift || operator == TOKEN_BIT_AND || operator == TOKEN_BIT_OR ||
                         operator == TOKEN_BIT_XOR || operator == TOKEN_PERCENT;
    bool is_left_literal = left->kind == TYPE_KIND_LITERAL;
    bool is_right_literal = right->kind == TYPE_KIND_LITERAL;
    bool is_left_floating_point = left->kind == TYPE_KIND_FLOATING_POINT || (is_left_literal && left->is_decimal);
    bool is_right_floating_point = right->kind == TYPE_KIND_FLOATING_POINT || (is_right_literal && right->is_decimal);

    if (is_integers_only && (is_left_floating_point || is_right_floating_point)) return NULL;
    if (is_shift) {
        /* The count can be any integer type; the result is the shifted
         * operand's type. A literal shifted by a typed count still takes its
         * type from context, like the literal alone would. */
        return is_left_literal ? &TYPE_LITERAL_INTEGER : left;
    }
    if (is_left_literal && is_right_literal) {
        if (is_comparison) return &TYPE_BOOL;
        return is_left_floating_point || is_right_floating_point ? &TYPE_LITERAL_DECIMAL : &TYPE_LITERAL_INTEGER;
    }
    if (is_left_literal || is_right_literal) {
        GrayType *typed = is_left_literal ? right : left;
        GrayType *literal = is_left_literal ? left : right;
        if (literal->is_decimal && typed->kind != TYPE_KIND_FLOATING_POINT) return NULL;
        return is_comparison ? &TYPE_BOOL : typed;
    }
    GrayType *common = NULL;
    if (strcmp(left->name, right->name) == 0)
        common = left;
    else if (left->kind == right->kind)
        common = number_width(left->name) >= number_width(right->name) ? left : right;
    if (!common) return NULL;
    return is_comparison ? &TYPE_BOOL : common;
}

bool type_is_numeric(GrayType *type) {
    return type->kind == TYPE_KIND_SIGNED_INTEGER || type->kind == TYPE_KIND_UNSIGNED_INTEGER || type->kind == TYPE_KIND_FLOATING_POINT ||
           type->kind == TYPE_KIND_CHAR;
}

bool type_is_integer(GrayType *type) {
    return type->kind == TYPE_KIND_SIGNED_INTEGER || type->kind == TYPE_KIND_UNSIGNED_INTEGER ||
           type->kind == TYPE_KIND_CHAR;
}

/* Render a composite type name into a small ring of static buffers, so callers
 * can chain several type_name() results inside one snprintf() without a later
 * call clobbering an earlier one. Each slot is bounded by TYPE_NAME_MAX. */
static const char *type_name_format(const char *format, ...) {
    static char buffers[4][TYPE_NAME_MAX];
    static int slot = 0;
    char *rendered = buffers[slot];
    slot = (slot + 1) & 3;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(rendered, sizeof(buffers[0]), format, arguments);
    va_end(arguments);
    return rendered;
}

const char *type_name(GrayType *type) {
    if (!type) return "unknown";
    /* Pointer types store the bare pointee in type->name (and type->element_type).
     * Render them with the leading '^' so error messages match source syntax. */
    if (type->kind == TYPE_KIND_POINTER && type->name) {
        return type_name_format("^%s", type->name);
    }
    if (type->kind == TYPE_KIND_ARRAY && type->element_type) {
        return type_name_format("[%s]", type->element_type);
    }
    if (type->kind == TYPE_KIND_MAP && type->key_type && type->value_type) {
        return type_name_format("map[%s:%s]", type->key_type, type->value_type);
    }
    return type->name;
}

/* Builtin type-name dispatch table. MUST stay sorted lexicographically by
 * name (uppercase precedes lowercase in ASCII), validated by bsearch. */
typedef struct {
    const char *name;
    GrayType *singleton;   /* non-NULL: return this singleton directly */
    int allocation_kind;      /* used when singleton is NULL: pool-alloc with this kind */
    const char *allocation_name; /* if non-NULL, set type->name to this literal instead of strdup(name) */
} BuiltinTypeEntry;

static BuiltinTypeEntry builtin_types[] = {
    { "Error",     NULL,      TYPE_KIND_ERROR,   "Error" },
    { "ErrorCode", NULL,      TYPE_KIND_ENUM,    "ErrorCode" },
    { "bool",   &TYPE_BOOL,   0, NULL },
    { "char",   &TYPE_CHAR,   0, NULL },
    { "f32",    NULL,         TYPE_KIND_FLOATING_POINT,   NULL },
    { "f64",    &TYPE_F64,    0, NULL },
    { "func",   NULL,         TYPE_KIND_FUNCTION, "func" },
    { "i128",   NULL,         TYPE_KIND_SIGNED_INTEGER,     NULL },
    { "i16",    NULL,         TYPE_KIND_SIGNED_INTEGER,     NULL },
    { "i256",   NULL,         TYPE_KIND_SIGNED_INTEGER,     NULL },
    { "i32",    NULL,         TYPE_KIND_SIGNED_INTEGER,     NULL },
    { "i64",    &TYPE_I64,    0, NULL },
    { "i8",     NULL,         TYPE_KIND_SIGNED_INTEGER,     NULL },
    { "nil",    &TYPE_NIL,    0, NULL },
    { "string", &TYPE_STRING, 0, NULL },
    { "u128",   NULL,         TYPE_KIND_UNSIGNED_INTEGER,    NULL },
    { "u16",    NULL,         TYPE_KIND_UNSIGNED_INTEGER,    NULL },
    { "u256",   NULL,         TYPE_KIND_UNSIGNED_INTEGER,    NULL },
    { "u32",    NULL,         TYPE_KIND_UNSIGNED_INTEGER,    NULL },
    { "u64",    &TYPE_U64,    0, NULL },
    { "u8",     &TYPE_U8,     0, NULL },
    { "void",   &TYPE_VOID,   0, NULL },
};

#define BUILTIN_TYPES_COUNT (sizeof(builtin_types) / sizeof(builtin_types[0]))

static int builtin_type_compare(const void *left, const void *right) {
    return strcmp(((const BuiltinTypeEntry *)left)->name,
                  ((const BuiltinTypeEntry *)right)->name);
}

bool is_builtin_type_name(const char *name) {
    if (!name) return false;
    BuiltinTypeEntry key = { name, NULL, 0, NULL };
    return bsearch(&key, builtin_types, BUILTIN_TYPES_COUNT,
                   sizeof(BuiltinTypeEntry), builtin_type_compare) != NULL;
}

GrayType *type_from_name(const char *name) {
    if (!name) return &TYPE_UNKNOWN;

    /* Typed function reference: "func(p1,&p2)->R" — checked before bsearch
     * so "func(...)" doesn't get conflated with the bare "func" entry. */
    if (strncmp(name, "func(", 5) == 0) {
        GrayType *existing = pool_find(TYPE_KIND_FUNCTION, name);
        if (existing) return existing;
        GrayType *type = type_allocate();
        type->kind = TYPE_KIND_FUNCTION;
        type->name = strdup(name);
        type->function_signature = parse_function_signature(name);
        pool_insert(TYPE_KIND_FUNCTION, type->name, type);
        return type;
    }

    BuiltinTypeEntry key = { name, NULL, 0, NULL };
    BuiltinTypeEntry *matching_entry = bsearch(&key, builtin_types, BUILTIN_TYPES_COUNT,
                                    sizeof(BuiltinTypeEntry), builtin_type_compare);
    if (matching_entry) {
        if (matching_entry->singleton) return matching_entry->singleton;
        const char *resolved_name = matching_entry->allocation_name ? matching_entry->allocation_name : name;
        GrayType *existing = pool_find(matching_entry->allocation_kind, resolved_name);
        if (existing) return existing;
        GrayType *type = type_allocate();
        type->kind = matching_entry->allocation_kind;
        /* Always a copy: a pooled entry owns its name whatever its kind, so
         * the teardown below frees uniformly instead of carrying a list of
         * the kinds whose names happen to be literals. */
        type->name = strdup(resolved_name);
        pool_insert(type->kind, type->name, type);
        return type;
    }

    /* Pointer type: ^i64, ^Person, etc. */
    if (name[0] == '^') {
        return type_pointer(name + 1);
    }

    /* Array type: [i64], [string], [i64,3], etc. */
    if (name[0] == '[') {
        size_t length = strlen(name);
        if (length > 2 && name[length - 1] == ']') {
            char *element_type = xmalloc(length - 1);
            memcpy(element_type, name + 1, length - 2);
            element_type[length - 2] = '\0';
            /* Strip ",N" suffix for fixed-size arrays like [string,3] */
            char *comma = strchr(element_type, ',');
            if (comma) *comma = '\0';
            GrayType *array_type = type_array(element_type);
            free(element_type);
            return array_type;
        }
    }

    /* Map type: map[K:V] */
    if (strncmp(name, "map[", 4) == 0) {
        GrayType *existing = pool_find(TYPE_KIND_MAP, name);
        if (existing) return existing;
        GrayType *type = type_allocate();
        type->kind = TYPE_KIND_MAP;
        type->name = strdup(name);
        /* Parse key:value types from "map[string:i64]" */
        const char *start = name + 4;
        const char *colon = strchr(start, ':');
        if (colon) {
            size_t key_length = (size_t)(colon - start);
            char *key_type_text = xmalloc(key_length + 1);
            memcpy(key_type_text, start, key_length);
            key_type_text[key_length] = '\0';
            type->key_type = key_type_text;

            const char *value_start = colon + 1;
            size_t value_length = strlen(value_start);
            if (value_length > 0 && value_start[value_length - 1] == ']') value_length--;
            char *value_type_text = xmalloc(value_length + 1);
            memcpy(value_type_text, value_start, value_length);
            value_type_text[value_length] = '\0';
            type->value_type = value_type_text;
        }
        pool_insert(TYPE_KIND_MAP, type->name, type);
        return type;
    }

    /* Qualified stdlib type: mod.Type. User modules are resolved against the
     * symbol table before reaching here, so anything still carrying a dot
     * belongs to the stdlib, which keeps its own registries and names its
     * opaque types unqualified (channels.Channel is the type Channel). */
    {
        const char *dot = strchr(name, '.');
        if (dot && dot[1] >= 'A' && dot[1] <= 'Z') {
            GrayType *existing = pool_find(TYPE_KIND_ENUM, dot + 1);
            if (existing) return existing;
            return type_struct(dot + 1);
        }
    }

    /* Uppercase names the primitive table did not claim are user-defined
     * types. A qualified name has already been mapped onto its registry
     * spelling by the caller, so there is nothing left to un-guess: this used
     * to split on '.' and on '_Uppercase', and carried a denylist of stdlib
     * opaque type names to undo the second guess. */
    if (name[0] >= 'A' && name[0] <= 'Z') {
        GrayType *existing = pool_find(TYPE_KIND_ENUM, name);
        if (existing) return existing;
        return type_struct(name);
    }
    {
        /* A module-mangled user type: mod_Name. Split at the last '_' so a
         * module name that contains one keeps its whole prefix. */
        const char *underscore = strrchr(name, '_');
        if (underscore && underscore[1] >= 'A' && underscore[1] <= 'Z') {
            GrayType *existing = pool_find(TYPE_KIND_ENUM, name);
            if (existing) return existing;
            return type_struct(name);
        }
    }

    return &TYPE_UNKNOWN;
}
