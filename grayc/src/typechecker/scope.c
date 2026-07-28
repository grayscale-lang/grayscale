/*
 * scope.c — Manages lexical scopes and the symbol table, providing variable
 * declaration, lookup, and scope push/pop operations for the type checker.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "scope.h"
#include "../util/xalloc.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SCOPE_INITIAL_CAP 8

static uint32_t scope_str_hash(const char *string) {
    uint32_t h = 5381u;
    for (const unsigned char *p = (const unsigned char *)string; *p; p++)
        h = h * 33u ^ (uint32_t)*p;
    return h;
}

static void scope_hash_insert(Scope *scope, int idx) {
    uint32_t mask = (uint32_t)(scope->hash_cap - 1);
    uint32_t h = scope_str_hash(scope->symbols[idx].name) & mask;
    for (;;) {
        if (!scope->hash[h].name) {
            scope->hash[h].name = scope->symbols[idx].name;
            scope->hash[h].idx  = idx;
            return;
        }
        h = (h + 1) & mask;
    }
}

static void scope_hash_rebuild(Scope *scope, int new_cap) {
    free(scope->hash);
    scope->hash = xcalloc((size_t)new_cap, sizeof(ScopeHashEntry));
    scope->hash_cap = new_cap;
    for (int i = 0; i < scope->count; i++)
        scope_hash_insert(scope, i);
}

Scope *scope_create(Scope *parent) {
    Scope *scope = xmalloc(sizeof(Scope));
    memset(scope, 0, sizeof(Scope));
    scope->parent = parent;
    return scope;
}

void scope_define(Scope *scope, const char *name, GrayType *type, bool mutable) {
    /* Check for redefinition in current scope */
    Symbol *existing = scope_lookup_local(scope, name);
    if (existing) {
        existing->type = type;
        existing->mutable = mutable;
        return;
    }

    if (scope->count >= scope->cap) {
        scope->cap = scope->cap ? scope->cap * 2 : SCOPE_INITIAL_CAP;
        scope->symbols = xrealloc(scope->symbols, sizeof(Symbol) * scope->cap);
    }

    /* Grow hash table before adding the new symbol so it always has room. */
    int new_count = scope->count + 1;
    if (!scope->hash || new_count * 2 > scope->hash_cap) {
        int new_hash_cap = scope->hash_cap ? scope->hash_cap * 2 : 16;
        while (new_count * 2 > new_hash_cap) new_hash_cap *= 2;
        scope_hash_rebuild(scope, new_hash_cap);
    }

    Symbol *sym = &scope->symbols[scope->count++];
    memset(sym, 0, sizeof(Symbol));
    sym->name = name;
    sym->type = type;
    sym->mutable = mutable;
    scope_hash_insert(scope, scope->count - 1);
}

Symbol *scope_lookup(Scope *scope, const char *name) {
    for (Scope *cur = scope; cur; cur = cur->parent) {
        Symbol *sym = scope_lookup_local(cur, name);
        if (sym) return sym;
    }
    return NULL;
}

void scope_destroy(Scope *scope) {
    if (!scope) return;
    for (int i = 0; i < scope->count; i++) {
        if (scope->symbols[i].ret_types_owned)
            free(scope->symbols[i].ret_types);
    }
    free(scope->symbols);
    free(scope->hash);
    free(scope);
}

Symbol *scope_lookup_local(Scope *scope, const char *name) {
    if (scope->hash) {
        uint32_t mask = (uint32_t)(scope->hash_cap - 1);
        uint32_t h = scope_str_hash(name) & mask;
        for (;;) {
            ScopeHashEntry *e = &scope->hash[h];
            if (!e->name) return NULL;
            if (strcmp(e->name, name) == 0) return &scope->symbols[e->idx];
            h = (h + 1) & mask;
        }
    }
    return NULL;
}
