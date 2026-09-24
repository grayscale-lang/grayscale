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

#define SCOPE_INITIAL_CAPACITY 8

uint32_t scope_string_hash(const char *string) {
    uint32_t hash = 5381u;
    for (const unsigned char *cursor = (const unsigned char *)string; *cursor; cursor++)
        hash = hash * 33u ^ (uint32_t)*cursor;
    /* Every caller consumes this as `hash & (capacity - 1)`, and djb2's low bits are
     * barely mixed: sequential identifiers (x0, x1, v0..) land in long runs
     * that linear probing then walks, 10+ strcmps per lookup. Fold the
     * well-mixed high bits down with an integer finalizer first. */
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return hash;
}

static void scope_hash_insert(Scope *scope, int index) {
    uint32_t mask = (uint32_t)(scope->hash_capacity - 1);
    uint32_t slot = scope_string_hash(scope->symbols[index].name) & mask;
    for (;;) {
        if (!scope->hash[slot].name) {
            scope->hash[slot].name = scope->symbols[index].name;
            scope->hash[slot].index  = index;
            return;
        }
        slot = (slot + 1) & mask;
    }
}

static void scope_hash_rebuild(Scope *scope, int new_capacity) {
    free(scope->hash);
    scope->hash = xcalloc((size_t)new_capacity, sizeof(ScopeHashEntry));
    scope->hash_capacity = new_capacity;
    for (int i = 0; i < scope->count; i++)
        scope_hash_insert(scope, i);
}

Scope *scope_create(Scope *parent) {
    Scope *scope = xmalloc(sizeof(Scope));
    memset(scope, 0, sizeof(Scope));
    scope->parent = parent;
    scope->depth = parent ? parent->depth + 1 : 0;
    return scope;
}

void scope_define(Scope *scope, const char *name, GrayType *type, bool is_mutable) {
    /* Check for redefinition in current scope */
    Symbol *existing = scope_lookup_local(scope, name);
    if (existing) {
        existing->type = type;
        existing->is_mutable = is_mutable;
        return;
    }

    if (scope->count >= scope->capacity) {
        scope->capacity = scope->capacity ? scope->capacity * 2 : SCOPE_INITIAL_CAPACITY;
        scope->symbols = xrealloc(scope->symbols, sizeof(Symbol) * scope->capacity);
    }

    /* Grow hash table before adding the new symbol so it always has room. */
    int new_count = scope->count + 1;
    if (!scope->hash || new_count * 2 > scope->hash_capacity) {
        int new_hash_capacity = scope->hash_capacity ? scope->hash_capacity * 2 : 16;
        while (new_count * 2 > new_hash_capacity) new_hash_capacity *= 2;
        scope_hash_rebuild(scope, new_hash_capacity);
    }

    Symbol *symbol = &scope->symbols[scope->count++];
    memset(symbol, 0, sizeof(Symbol));
    symbol->name = name;
    symbol->type = type;
    symbol->is_mutable = is_mutable;
    scope_hash_insert(scope, scope->count - 1);
}

Symbol *scope_lookup(Scope *scope, const char *name) {
    for (Scope *current = scope; current; current = current->parent) {
        Symbol *symbol = scope_lookup_local(current, name);
        if (symbol) return symbol;
    }
    return NULL;
}

void scope_destroy(Scope *scope) {
    if (!scope) return;
    for (int i = 0; i < scope->count; i++) {
        if (scope->symbols[i].are_return_types_owned)
            free(scope->symbols[i].return_types);
    }
    free(scope->symbols);
    free(scope->hash);
    free(scope);
}

Symbol *scope_lookup_local(Scope *scope, const char *name) {
    if (scope->hash) {
        uint32_t mask = (uint32_t)(scope->hash_capacity - 1);
        uint32_t slot = scope_string_hash(name) & mask;
        for (;;) {
            ScopeHashEntry *entry = &scope->hash[slot];
            if (!entry->name) return NULL;
            if (strcmp(entry->name, name) == 0) return &scope->symbols[entry->index];
            slot = (slot + 1) & mask;
        }
    }
    return NULL;
}
