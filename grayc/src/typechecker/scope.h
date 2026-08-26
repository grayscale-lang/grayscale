/*
 * scope.h — Declares the Symbol, Scope, and hash-table structures that back
 * lexical scoping and variable resolution during type checking.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_SCOPE_H
#define GRAYC_SCOPE_H

#include "types.h"
#include <stdint.h>

typedef struct {
    const char *name;
    GrayType *type;
    const char *declared_type; /* original declared type name (e.g., "uint", "i8") */
    bool mutable;
    bool is_ref;         /* true if created via ref() — transparent reference */
    bool const_source;   /* true if pointer was taken from a const variable via addr() */
    /* Lifetime origin of a pointer value: the depth of the scope declaring
     * the variable whose address this pointer holds, biased by +1 so that 0
     * means "no tracked origin", plus that variable's name for diagnostics.
     * Set at addr()/raw() and propagated through pointer assignment so the
     * escape checks (E3063, E3097) survive laundering through intermediate
     * pointer variables. */
    int origin_depth;
    const char *origin_name;
    bool used;           /* true if variable was read */
    int def_line;        /* line where variable was defined */
    int def_column;      /* column where variable was defined */
    GrayType **ret_types;  /* for multi-return temps: all return types */
    int ret_count;       /* number of return types */
    bool ret_types_owned;  /* true if ret_types was xmalloc'd by the typechecker */
    const char *func_ref_name; /* for func-typed vars: name of the referenced
                                  function, used for call-site arity/type
                                  validation (NULL if not assigned from a
                                  static func ref) */
    /* For [func] array vars initialised with a literal of func refs
     *per-element referenced function names, length == the
     * literal's count. NULL entries mark elements that aren't a
     * static func ref. Used by call-site type inference on
     * arr[const](...) expressions so struct return types survive the
     * trip through a type-erased void* array. */
    const char **func_array_refs;
    int func_array_ref_count;
} Symbol;

typedef struct {
    const char *name;  /* NULL = empty slot */
    int idx;           /* index into Scope.symbols[] */
} ScopeHashEntry;

typedef struct Scope {
    struct Scope *parent;
    int depth;         /* 0 for the root scope, parent->depth + 1 otherwise */
    Symbol *symbols;
    int count;
    int cap;
    ScopeHashEntry *hash;  /* open-addressing hash table; NULL until first define */
    int hash_cap;          /* always a power of 2 */
} Scope;

/* djb2 string hash, shared with the module symbol table. */
uint32_t scope_str_hash(const char *string);

Scope *scope_create(Scope *parent);
void scope_destroy(Scope *scope);
void scope_define(Scope *scope, const char *name, GrayType *type, bool mutable);
Symbol *scope_lookup(Scope *scope, const char *name);
Symbol *scope_lookup_local(Scope *scope, const char *name);

#endif
