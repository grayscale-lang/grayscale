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

/* Built-in type singletons */
GrayType TYPE_VOID    = {TK_VOID,   "void",   NULL, NULL, NULL, NULL};
GrayType TYPE_INT     = {TK_INT,    "int",    NULL, NULL, NULL, NULL};
GrayType TYPE_UINT    = {TK_UINT,   "uint",   NULL, NULL, NULL, NULL};
GrayType TYPE_FLOAT   = {TK_FLOAT,  "float",  NULL, NULL, NULL, NULL};
GrayType TYPE_BOOL    = {TK_BOOL,   "bool",   NULL, NULL, NULL, NULL};
GrayType TYPE_CHAR    = {TK_CHAR,   "char",   NULL, NULL, NULL, NULL};
GrayType TYPE_BYTE    = {TK_BYTE,   "byte",   NULL, NULL, NULL, NULL};
GrayType TYPE_STRING  = {TK_STRING, "string", NULL, NULL, NULL, NULL};
GrayType TYPE_NIL     = {TK_NIL,    "nil",    NULL, NULL, NULL, NULL};
GrayType TYPE_UNKNOWN = {TK_UNKNOWN,"unknown",NULL, NULL, NULL, NULL};

#define TYPE_POOL_CAPACITY 4096

/* Pool for dynamically created types (leak on exit, fine for a compiler) */
static GrayType type_pool[TYPE_POOL_CAPACITY];
static int type_pool_count = 0;

GrayType *type_alloc(void) {
    if (type_pool_count >= TYPE_POOL_CAPACITY) {
        fprintf(stderr, "error: type pool exhausted (%d types); please report this bug\n", TYPE_POOL_CAPACITY);
        exit(1);
    }
    return &type_pool[type_pool_count++];
}

/* Hash index for O(1) pool_find. Must be a power of 2 and at least 2x
 * TYPE_POOL_CAPACITY to keep load factor below 50%. */
#define TYPE_HASH_CAP 8192

typedef struct {
    TypeKind kind;
    const char *name;  /* points into the pool entry's name — not a copy */
    GrayType    *type;   /* NULL = empty slot */
} TypeHashEntry;

static TypeHashEntry type_hash_table[TYPE_HASH_CAP];

static uint32_t type_hash(TypeKind kind, const char *name) {
    uint32_t h = 5381u ^ ((uint32_t)kind * 2654435761u);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        h = h * 33u ^ (uint32_t)*p;
    return h;
}

/* Return an existing pool entry matching kind+name, or NULL if not found. */
static GrayType *pool_find(TypeKind kind, const char *name) {
    if (!name) return NULL;
    uint32_t mask = TYPE_HASH_CAP - 1;
    uint32_t idx  = type_hash(kind, name) & mask;
    for (;;) {
        TypeHashEntry *e = &type_hash_table[idx];
        if (!e->type) return NULL;
        if (e->kind == kind && strcmp(e->name, name) == 0) return e->type;
        idx = (idx + 1) & mask;
    }
}

/* Insert a newly created type into the hash index. */
static void pool_insert(TypeKind kind, const char *name, GrayType *type) {
    uint32_t mask = TYPE_HASH_CAP - 1;
    uint32_t idx  = type_hash(kind, name) & mask;
    for (;;) {
        TypeHashEntry *e = &type_hash_table[idx];
        if (!e->type) {
            e->kind = kind;
            e->name = name;
            e->type = type;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

GrayType *type_array(const char *elem_type) {
    GrayType *existing = pool_find(TK_ARRAY, elem_type);
    if (existing) return existing;
    GrayType *type = type_alloc();
    type->kind = TK_ARRAY;
    const char *dup = strdup(elem_type);
    type->element_type = dup;
    type->name = dup;
    pool_insert(TK_ARRAY, type->name, type);
    return type;
}

GrayType *type_struct(const char *name) {
    GrayType *existing = pool_find(TK_STRUCT, name);
    if (existing) return existing;
    GrayType *type = type_alloc();
    type->kind = TK_STRUCT;
    type->name = strdup(name);
    pool_insert(TK_STRUCT, type->name, type);
    return type;
}

GrayType *type_enum(const char *name) {
    GrayType *existing = pool_find(TK_ENUM, name);
    if (existing) return existing;
    GrayType *type = type_alloc();
    type->kind = TK_ENUM;
    type->name = strdup(name);
    pool_insert(TK_ENUM, type->name, type);
    return type;
}

GrayType *type_pointer(const char *pointee_type) {
    GrayType *existing = pool_find(TK_POINTER, pointee_type);
    if (existing) return existing;
    GrayType *type = type_alloc();
    type->kind = TK_POINTER;
    const char *dup = strdup(pointee_type);
    type->element_type = dup;
    type->name = dup;
    pool_insert(TK_POINTER, type->name, type);
    return type;
}

/* Find the index of the matching ')' for the '(' at start. Tracks nesting
 * so that nested func(...) types (e.g. func(func(int)->int)->bool) work.
 * Returns -1 if not balanced. */
static int find_matching_paren(const char *s, int start) {
    int depth = 0;
    for (int i = start; s[i]; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/* Split src[start..end-1] on commas at top-level depth, dup each segment.
 * Sets *out_count and allocates *out_arr (caller frees the array but the
 * dup'd strings live with the type pool — they're leaked at exit). */
static void split_top_commas(const char *src, int start, int end,
                              int *out_count, char ***out_arr) {
    *out_count = 0;
    *out_arr = NULL;
    if (end <= start) return;
    int cap = 4;
    char **arr = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    int n = 0;
    int depth = 0;
    int seg_start = start;
    for (int i = start; i <= end; i++) {
        char c = (i < end) ? src[i] : ',';
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') depth--;
        if (depth == 0 && (c == ',' || i == end)) {
            int seg_len = i - seg_start;
            char *seg = (char *)xmalloc((size_t)seg_len + 1);
            memcpy(seg, src + seg_start, (size_t)seg_len);
            seg[seg_len] = '\0';
            if (n == cap) {
                cap *= 2;
                arr = (char **)xrealloc(arr, sizeof(char *) * (size_t)cap);
            }
            arr[n++] = seg;
            seg_start = i + 1;
        }
    }
    *out_count = n;
    *out_arr = arr;
}

/* Parse "func(p1,&p2,...)->R" or "func(...)->()" or "func(...)->(R1,R2)"
 * into an GrayFuncSig. Returns NULL if the string isn't well-formed. */
static GrayFuncSig *parse_func_sig(const char *name) {
    if (strncmp(name, "func(", 5) != 0) return NULL;
    int rparen = find_matching_paren(name, 4);
    if (rparen < 0) return NULL;
    GrayFuncSig *sig = (GrayFuncSig *)xmalloc(sizeof(GrayFuncSig));
    sig->param_count = 0;
    sig->param_types = NULL;
    sig->param_mutable = NULL;
    sig->return_count = 0;
    sig->return_types = NULL;

    /* Params */
    char **raw_params = NULL;
    int raw_count = 0;
    split_top_commas(name, 5, rparen, &raw_count, &raw_params);
    sig->param_count = raw_count;
    if (raw_count > 0) {
        sig->param_types = (const char **)xmalloc(sizeof(char *) * (size_t)raw_count);
        sig->param_mutable = (bool *)xmalloc(sizeof(bool) * (size_t)raw_count);
        for (int i = 0; i < raw_count; i++) {
            const char *p = raw_params[i];
            if (p[0] == '&') {
                sig->param_mutable[i] = true;
                sig->param_types[i] = strdup(p + 1);
            } else {
                sig->param_mutable[i] = false;
                sig->param_types[i] = strdup(p);
            }
            free(raw_params[i]);
        }
    }
    free(raw_params);

    /* Return */
    const char *after = name + rparen + 1;
    if (after[0] == '-' && after[1] == '>') {
        const char *ret = after + 2;
        if (ret[0] == '(') {
            int ret_len = (int)strlen(ret);
            if (ret[ret_len - 1] != ')') return sig; /* malformed; keep params */
            char **rets = NULL;
            int rcount = 0;
            split_top_commas(ret, 1, ret_len - 1, &rcount, &rets);
            sig->return_count = rcount;
            if (rcount > 0) {
                sig->return_types = (const char **)xmalloc(sizeof(char *) * (size_t)rcount);
                for (int i = 0; i < rcount; i++) {
                    sig->return_types[i] = rets[i];
                }
            }
            free(rets);
        } else if (strcmp(ret, "void") != 0) {
            sig->return_count = 1;
            sig->return_types = (const char **)xmalloc(sizeof(char *));
            sig->return_types[0] = strdup(ret);
        }
    }
    return sig;
}

void type_pool_reset(void) {
    for (int i = 0; i < type_pool_count; i++) {
        GrayType *type = &type_pool[i];
        switch (type->kind) {
        case TK_STRUCT:
        case TK_ENUM:
        case TK_ARRAY:
        case TK_POINTER:
            /* For ARRAY and POINTER, type->name == type->element_type (same heap
             * pointer); free once via type->name. */
            free((char *)type->name);
            break;
        case TK_MAP:
            free((char *)type->name);
            free((char *)type->key_type);
            free((char *)type->value_type);
            break;
        case TK_FUNCTION:
            free((char *)type->name);
            if (type->func_sig) {
                for (int j = 0; j < type->func_sig->param_count; j++)
                    free((char *)type->func_sig->param_types[j]);
                free(type->func_sig->param_types);
                free(type->func_sig->param_mutable);
                for (int j = 0; j < type->func_sig->return_count; j++)
                    free((char *)type->func_sig->return_types[j]);
                free(type->func_sig->return_types);
                free(type->func_sig);
            }
            break;
        default:
            /* Builtin non-singleton pool entries: type->name is either a
             * string literal (TK_ERROR → "Error", TK_UNKNOWN → "func")
             * or a strdup'd name (i8, f32, u64, …).  Skip literal kinds. */
            if (type->kind != TK_ERROR && type->kind != TK_UNKNOWN)
                free((char *)type->name);
            break;
        }
    }
    type_pool_count = 0;
    memset(type_hash_table, 0, sizeof(type_hash_table));
}

bool type_is_numeric(GrayType *type) {
    return type->kind == TK_INT || type->kind == TK_UINT || type->kind == TK_FLOAT ||
           type->kind == TK_CHAR || type->kind == TK_BYTE;
}

bool type_is_integer(GrayType *type) {
    return type->kind == TK_INT || type->kind == TK_UINT ||
           type->kind == TK_CHAR || type->kind == TK_BYTE;
}

const char *type_name(GrayType *type) {
    if (!type) return "unknown";
    /* Pointer types store the bare pointee in type->name (and type->element_type).
     * Render them with the leading '^' so error messages match source syntax.
     * A small ring of static buffers lets callers chain type_name() calls
     * inside one snprintf() without clobbering the previous result. */
    if (type->kind == TK_POINTER && type->name) {
        static char bufs[4][TYPE_NAME_MAX];
        static int slot = 0;
        char *out = bufs[slot];
        slot = (slot + 1) & 3;
        snprintf(out, sizeof(bufs[0]), "^%s", type->name);
        return out;
    }
    if (type->kind == TK_ARRAY && type->element_type) {
        static char bufs[4][TYPE_NAME_MAX];
        static int slot = 0;
        char *out = bufs[slot];
        slot = (slot + 1) & 3;
        snprintf(out, sizeof(bufs[0]), "[%s]", type->element_type);
        return out;
    }
    if (type->kind == TK_MAP && type->key_type && type->value_type) {
        static char bufs[4][TYPE_NAME_MAX];
        static int slot = 0;
        char *out = bufs[slot];
        slot = (slot + 1) & 3;
        snprintf(out, sizeof(bufs[0]), "map[%s:%s]", type->key_type, type->value_type);
        return out;
    }
    return type->name;
}

/* Builtin type-name dispatch table. MUST stay sorted lexicographically by
 * name (uppercase precedes lowercase in ASCII), validated by bsearch. */
typedef struct {
    const char *name;
    GrayType *singleton;   /* non-NULL: return this singleton directly */
    int alloc_kind;      /* used when singleton is NULL: pool-alloc with this kind */
    const char *alloc_name; /* if non-NULL, set type->name to this literal instead of strdup(name) */
} BuiltinTypeEntry;

static BuiltinTypeEntry builtin_types[] = {
    { "Error",  NULL,         TK_ERROR,   "Error" },
    { "bool",   &TYPE_BOOL,   0, NULL },
    { "byte",   &TYPE_BYTE,   0, NULL },
    { "char",   &TYPE_CHAR,   0, NULL },
    { "f32",    NULL,         TK_FLOAT,   NULL },
    { "f64",    NULL,         TK_FLOAT,   NULL },
    { "float",  &TYPE_FLOAT,  0, NULL },
    { "func",   NULL,         TK_UNKNOWN, "func" },
    { "i128",   NULL,         TK_INT,     NULL },
    { "i16",    NULL,         TK_INT,     NULL },
    { "i256",   NULL,         TK_INT,     NULL },
    { "i32",    NULL,         TK_INT,     NULL },
    { "i64",    NULL,         TK_INT,     NULL },
    { "i8",     NULL,         TK_INT,     NULL },
    { "int",    &TYPE_INT,    0, NULL },
    { "nil",    &TYPE_NIL,    0, NULL },
    { "string", &TYPE_STRING, 0, NULL },
    { "u128",   NULL,         TK_UINT,    NULL },
    { "u16",    NULL,         TK_UINT,    NULL },
    { "u256",   NULL,         TK_UINT,    NULL },
    { "u32",    NULL,         TK_UINT,    NULL },
    { "u64",    NULL,         TK_UINT,    NULL },
    { "u8",     NULL,         TK_UINT,    NULL },
    { "uint",   &TYPE_UINT,   0, NULL },
    { "void",   &TYPE_VOID,   0, NULL },
};

#define BUILTIN_TYPES_COUNT (sizeof(builtin_types) / sizeof(builtin_types[0]))

static int builtin_type_compare(const void *a, const void *b) {
    return strcmp(((const BuiltinTypeEntry *)a)->name,
                  ((const BuiltinTypeEntry *)b)->name);
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
        GrayType *existing = pool_find(TK_FUNCTION, name);
        if (existing) return existing;
        GrayType *type = type_alloc();
        type->kind = TK_FUNCTION;
        type->name = strdup(name);
        type->func_sig = parse_func_sig(name);
        pool_insert(TK_FUNCTION, type->name, type);
        return type;
    }

    BuiltinTypeEntry key = { name, NULL, 0, NULL };
    BuiltinTypeEntry *hit = bsearch(&key, builtin_types, BUILTIN_TYPES_COUNT,
                                    sizeof(BuiltinTypeEntry), builtin_type_compare);
    if (hit) {
        if (hit->singleton) return hit->singleton;
        const char *resolved_name = hit->alloc_name ? hit->alloc_name : name;
        GrayType *existing = pool_find(hit->alloc_kind, resolved_name);
        if (existing) return existing;
        GrayType *type = type_alloc();
        type->kind = hit->alloc_kind;
        type->name = hit->alloc_name ? hit->alloc_name : strdup(name);
        pool_insert(type->kind, type->name, type);
        return type;
    }

    /* Pointer type: ^int, ^Person, etc. */
    if (name[0] == '^') {
        return type_pointer(name + 1);
    }

    /* Array type: [int], [string], [int,3], etc. */
    if (name[0] == '[') {
        size_t len = strlen(name);
        if (len > 2 && name[len - 1] == ']') {
            char *elem = xmalloc(len - 1);
            memcpy(elem, name + 1, len - 2);
            elem[len - 2] = '\0';
            /* Strip ",N" suffix for fixed-size arrays like [string,3] */
            char *comma = strchr(elem, ',');
            if (comma) *comma = '\0';
            GrayType *arr = type_array(elem);
            free(elem);
            return arr;
        }
    }

    /* Map type: map[K:V] */
    if (strncmp(name, "map[", 4) == 0) {
        GrayType *existing = pool_find(TK_MAP, name);
        if (existing) return existing;
        GrayType *type = type_alloc();
        type->kind = TK_MAP;
        type->name = strdup(name);
        /* Parse key:value types from "map[string:int]" */
        const char *start = name + 4;
        const char *colon = strchr(start, ':');
        if (colon) {
            size_t klen = (size_t)(colon - start);
            char *key_str = xmalloc(klen + 1);
            memcpy(key_str, start, klen);
            key_str[klen] = '\0';
            type->key_type = key_str;

            const char *vstart = colon + 1;
            size_t vlen = strlen(vstart);
            if (vlen > 0 && vstart[vlen - 1] == ']') vlen--;
            char *val = xmalloc(vlen + 1);
            memcpy(val, vstart, vlen);
            val[vlen] = '\0';
            type->value_type = val;
        }
        pool_insert(TK_MAP, type->name, type);
        return type;
    }

    /* Qualified stdlib type: mod.Type. User modules are resolved against the
     * symbol table before reaching here, so anything still carrying a dot
     * belongs to the stdlib, which keeps its own registries and names its
     * opaque types unqualified (channels.Channel is the type Channel). */
    {
        const char *dot = strchr(name, '.');
        if (dot && dot[1] >= 'A' && dot[1] <= 'Z') {
            GrayType *existing = pool_find(TK_ENUM, dot + 1);
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
        GrayType *existing = pool_find(TK_ENUM, name);
        if (existing) return existing;
        return type_struct(name);
    }
    {
        /* A module-mangled user type: mod_Name. */
        const char *us = strchr(name, '_');
        if (us && us[1] >= 'A' && us[1] <= 'Z') {
            GrayType *existing = pool_find(TK_ENUM, name);
            if (existing) return existing;
            return type_struct(name);
        }
    }

    return &TYPE_UNKNOWN;
}
