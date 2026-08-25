/*
 * module_table.c — Implements the per-module symbol table and the two
 * resolvers (qualified and unqualified) that every module-aware lookup in the
 * compiler goes through.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "module_table.h"
#include "scope.h"
#include "../util/constants.h"
#include "../util/xalloc.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MODULE_HASH_INIT_CAP 16

/* --- Shared open-addressing helpers ---
 *
 * Both the module table (module name -> ModuleScope) and each module scope
 * (declaration name -> DeclEntry) are name -> index maps over a dense array,
 * so they share one probe. Capacity is always a power of two and the load
 * factor is held at or below one half, which linear probing needs in order to
 * terminate on a free slot. */

static void hash_place(ModuleHashEntry *hash, int cap, const char *name, int idx) {
    uint32_t mask = (uint32_t)(cap - 1);
    uint32_t h = scope_str_hash(name) & mask;
    for (;;) {
        if (!hash[h].name) {
            hash[h].name = name;
            hash[h].idx = idx;
            return;
        }
        h = (h + 1) & mask;
    }
}

/* Index of `name`, or -1 when absent. */
static int hash_find(const ModuleHashEntry *hash, int cap, const char *name) {
    if (!hash) return -1;
    uint32_t mask = (uint32_t)(cap - 1);
    uint32_t h = scope_str_hash(name) & mask;
    for (;;) {
        const ModuleHashEntry *e = &hash[h];
        if (!e->name) return -1;
        if (strcmp(e->name, name) == 0) return e->idx;
        h = (h + 1) & mask;
    }
}

static ModuleHashEntry *hash_alloc(Arena *arena, int cap) {
    ModuleHashEntry *hash = arena_alloc(arena, sizeof(ModuleHashEntry) * (size_t)cap);
    memset(hash, 0, sizeof(ModuleHashEntry) * (size_t)cap);
    return hash;
}

/* Grow the hash to hold new_count entries at a load factor of one half. */
static int hash_target_cap(int cur_cap, int new_count) {
    int cap = cur_cap ? cur_cap : MODULE_HASH_INIT_CAP;
    while (new_count * 2 > cap) cap *= 2;
    return cap;
}

/* --- Module table --- */

ModuleTable *module_table_create(Arena *arena) {
    ModuleTable *table = arena_alloc(arena, sizeof(ModuleTable));
    memset(table, 0, sizeof(ModuleTable));
    table->arena = arena;
    return table;
}

ModuleScope *module_table_find(ModuleTable *table, const char *module_name) {
    if (!table || !module_name) return NULL;
    int idx = hash_find(table->hash, table->hash_cap, module_name);
    return idx < 0 ? NULL : table->modules[idx];
}

ModuleScope *module_table_scope(ModuleTable *table, const char *module_name,
                                bool is_entry) {
    ModuleScope *existing = module_table_find(table, module_name);
    if (existing) return existing;

    Arena *arena = table->arena;
    ModuleScope *scope = arena_alloc(arena, sizeof(ModuleScope));
    memset(scope, 0, sizeof(ModuleScope));
    scope->name = arena_copy_string(arena, module_name);
    scope->is_entry = is_entry;

    ARENA_GROW(arena, table->modules, table->count, table->cap);

    int new_count = table->count + 1;
    if (!table->hash || new_count * 2 > table->hash_cap) {
        table->hash_cap = hash_target_cap(table->hash_cap, new_count);
        table->hash = hash_alloc(arena, table->hash_cap);
        for (int i = 0; i < table->count; i++)
            hash_place(table->hash, table->hash_cap, table->modules[i]->name, i);
    }

    table->modules[table->count] = scope;
    hash_place(table->hash, table->hash_cap, scope->name, table->count);
    table->count++;
    return scope;
}

/* --- File -> module --- */

void module_table_map_file(ModuleTable *table, const char *file,
                           const char *module_name, bool is_entry) {
    if (!table) return;
    if (is_entry) module_name = MODULE_ENTRY_NAME;
    if (!module_name) return;

    module_table_scope(table, module_name, is_entry);
    if (is_entry) table->entry_module = module_table_find(table, module_name)->name;
    if (!file) return;

    Arena *arena = table->arena;
    if (hash_find(table->file_hash, table->file_hash_cap, file) >= 0) return;

    if (table->file_count >= table->file_cap) {
        table->file_cap = GROW_NEXT_CAP(table->file_cap);
        ARENA_GROW_TO(arena, table->file_paths, table->file_count, table->file_cap);
        ARENA_GROW_TO(arena, table->file_modules, table->file_count, table->file_cap);
    }

    int new_count = table->file_count + 1;
    if (!table->file_hash || new_count * 2 > table->file_hash_cap) {
        table->file_hash_cap = hash_target_cap(table->file_hash_cap, new_count);
        table->file_hash = hash_alloc(arena, table->file_hash_cap);
        for (int i = 0; i < table->file_count; i++)
            hash_place(table->file_hash, table->file_hash_cap, table->file_paths[i], i);
    }

    table->file_paths[table->file_count] = arena_copy_string(arena, file);
    table->file_modules[table->file_count] = module_table_find(table, module_name)->name;
    hash_place(table->file_hash, table->file_hash_cap,
               table->file_paths[table->file_count], table->file_count);
    table->file_count++;
}

const char *module_table_module_for_file(ModuleTable *table, const char *file) {
    if (!table) return NULL;
    if (file) {
        int idx = hash_find(table->file_hash, table->file_hash_cap, file);
        if (idx >= 0) return table->file_modules[idx];
    }
    return table->entry_module;
}

/* --- Node -> declaration index --- */

static void node_index_place(ModuleNodeEntry *index, int cap,
                             const AstNode *node, DeclEntry *entry) {
    uint32_t mask = (uint32_t)(cap - 1);
    /* Pointer hash: the low bits of an allocation address carry no entropy,
     * so mix the whole value down first. */
    uint32_t h = (uint32_t)(((uintptr_t)node >> 4) * 2654435761u) & mask;
    for (;;) {
        if (!index[h].node) {
            index[h].node = node;
            index[h].entry = entry;
            return;
        }
        h = (h + 1) & mask;
    }
}

static void node_index_add(ModuleTable *table, const AstNode *node, DeclEntry *entry) {
    if (!node) return;
    int new_count = table->node_count + 1;
    if (!table->node_index || new_count * 2 > table->node_hash_cap) {
        int cap = hash_target_cap(table->node_hash_cap, new_count);
        ModuleNodeEntry *fresh = arena_alloc(table->arena, sizeof(ModuleNodeEntry) * (size_t)cap);
        memset(fresh, 0, sizeof(ModuleNodeEntry) * (size_t)cap);
        for (int i = 0; i < table->node_hash_cap; i++) {
            if (table->node_index[i].node)
                node_index_place(fresh, cap, table->node_index[i].node, table->node_index[i].entry);
        }
        table->node_index = fresh;
        table->node_hash_cap = cap;
    }
    node_index_place(table->node_index, table->node_hash_cap, node, entry);
    table->node_count++;
}

DeclEntry *module_table_entry_for_node(ModuleTable *table, const AstNode *node) {
    if (!table || !node || !table->node_index) return NULL;
    uint32_t mask = (uint32_t)(table->node_hash_cap - 1);
    uint32_t h = (uint32_t)(((uintptr_t)node >> 4) * 2654435761u) & mask;
    for (;;) {
        ModuleNodeEntry *e = &table->node_index[h];
        if (!e->node) return NULL;
        if (e->node == node) return e->entry;
        h = (h + 1) & mask;
    }
}

/* --- Module scopes --- */

DeclEntry *module_scope_lookup(ModuleScope *scope, const char *name) {
    if (!scope || !name) return NULL;
    int idx = hash_find(scope->hash, scope->hash_cap, name);
    return idx < 0 ? NULL : scope->entries[idx];
}

DeclEntry *module_scope_define(ModuleTable *table, ModuleScope *scope,
                               DeclKind kind, const char *name,
                               AstNode *ast_node, GrayType *gray_type,
                               const char *origin_file, int origin_line,
                               Visibility visibility) {
    DeclEntry *existing = module_scope_lookup(scope, name);
    if (existing) return existing;

    Arena *arena = table->arena;
    DeclEntry *entry = arena_alloc(arena, sizeof(DeclEntry));
    memset(entry, 0, sizeof(DeclEntry));
    entry->kind = kind;
    entry->name = arena_copy_string(arena, name);
    entry->module_name = scope->name;
    entry->module_is_entry = scope->is_entry;
    entry->ast_node = ast_node;
    entry->gray_type = gray_type;
    entry->origin_file = origin_file;
    entry->origin_line = origin_line;
    entry->visibility = visibility;

    ARENA_GROW(arena, scope->entries, scope->count, scope->cap);

    int new_count = scope->count + 1;
    if (!scope->hash || new_count * 2 > scope->hash_cap) {
        scope->hash_cap = hash_target_cap(scope->hash_cap, new_count);
        scope->hash = hash_alloc(arena, scope->hash_cap);
        for (int i = 0; i < scope->count; i++)
            hash_place(scope->hash, scope->hash_cap, scope->entries[i]->name, i);
    }

    scope->entries[scope->count] = entry;
    hash_place(scope->hash, scope->hash_cap, entry->name, scope->count);
    scope->count++;
    node_index_add(table, ast_node, entry);
    return entry;
}

/* --- Aliases --- */

void module_table_add_alias(ModuleTable *table, const char *alias,
                            const char *module_name) {
    if (!alias || !module_name || strcmp(alias, module_name) == 0) return;
    for (int i = 0; i < table->alias_count; i++) {
        if (strcmp(table->alias_names[i], alias) == 0) return;
    }
    if (table->alias_count >= table->alias_cap) {
        table->alias_cap = GROW_NEXT_CAP(table->alias_cap);
        ARENA_GROW_TO(table->arena, table->alias_names, table->alias_count, table->alias_cap);
        ARENA_GROW_TO(table->arena, table->alias_modules, table->alias_count, table->alias_cap);
    }
    table->alias_names[table->alias_count] = arena_copy_string(table->arena, alias);
    table->alias_modules[table->alias_count] = arena_copy_string(table->arena, module_name);
    table->alias_count++;
}

const char *module_table_resolve_alias(ModuleTable *table, const char *alias) {
    if (!table || !alias) return alias;
    for (int i = 0; i < table->alias_count; i++) {
        if (strcmp(table->alias_names[i], alias) == 0) return table->alias_modules[i];
    }
    return alias;
}

/* --- Resolution --- */

DeclEntry *module_resolve_qualified(ModuleTable *table,
                                    const char *current_module,
                                    const char *module_or_alias,
                                    const char *name,
                                    ResolveStatus *out_status) {
    ResolveStatus status = RESOLVE_NO_MODULE;
    DeclEntry *entry = NULL;

    /* The empty name keys the entry module, which no source qualifier can
     * spell. Guarding here keeps a stray empty qualifier from reaching into
     * the entry file's declarations. */
    if (table && module_or_alias && *module_or_alias && name) {
        const char *module_name = module_table_resolve_alias(table, module_or_alias);
        ModuleScope *scope = module_table_find(table, module_name);
        if (scope) {
            entry = module_scope_lookup(scope, name);
            if (!entry) {
                status = RESOLVE_NO_DECL;
            } else if (entry->visibility == VIS_PRIVATE &&
                       (!current_module || strcmp(current_module, entry->module_name) != 0)) {
                status = RESOLVE_PRIVATE;
            } else {
                status = RESOLVE_OK;
            }
        }
    }

    if (out_status) *out_status = status;
    /* entry is non-NULL exactly for RESOLVE_OK and RESOLVE_PRIVATE. */
    return entry;
}

DeclEntry *module_resolve_unqualified(ModuleTable *table,
                                      const char *current_module,
                                      const char **using_modules,
                                      int using_count,
                                      const char *name,
                                      const char **out_ambiguous_with) {
    if (out_ambiguous_with) *out_ambiguous_with = NULL;
    if (!table || !name) return NULL;

    /* The current module always wins — a local declaration shadows anything a
     * `using` brought in, and is never ambiguous with it. */
    ModuleScope *own = module_table_find(table, current_module);
    if (own) {
        DeclEntry *entry = module_scope_lookup(own, name);
        if (entry) return entry;
    }

    DeclEntry *found = NULL;
    for (int i = 0; i < using_count; i++) {
        const char *module_name = module_table_resolve_alias(table, using_modules[i]);
        ModuleScope *scope = module_table_find(table, module_name);
        if (!scope) continue;
        DeclEntry *entry = module_scope_lookup(scope, name);
        if (!entry || entry->visibility == VIS_PRIVATE) continue;
        if (!found) {
            found = entry;
            /* Without an ambiguity report to make, the first match is the
             * answer and the remaining modules need not be searched. */
            if (!out_ambiguous_with) return found;
            continue;
        }
        if (entry != found) {
            *out_ambiguous_with = entry->module_name;
            return NULL;
        }
    }
    return found;
}

DeclEntry *module_resolve_written(ModuleTable *table, const char *current_module,
                                  const char **using_modules, int using_count,
                                  const char *written) {
    if (!table || !written) return NULL;
    /* Leaf names only. A composite spelling — [T], ^T, map[K:V] — has to go
     * through module_resolve_type_name, which takes it apart first; splitting
     * one here on its first '.' would produce nonsense like ("[types",
     * "Item]"). */
    if (strpbrk(written, "[]^:?,")) return NULL;
    const char *dot = strchr(written, '.');
    if (dot) {
        char qualifier[MSG_BUF_SIZE];
        size_t qlen = (size_t)(dot - written);
        if (qlen >= sizeof(qualifier)) return NULL;
        memcpy(qualifier, written, qlen);
        qualifier[qlen] = '\0';
        return module_resolve_qualified(table, current_module, qualifier, dot + 1, NULL);
    }
    return module_resolve_unqualified(table, current_module, using_modules,
                                      using_count, written, NULL);
}

/* --- Written type names --- */

/* Index of `ch` at bracket depth zero, or -1. */
static int find_at_depth_zero(const char *s, size_t len, char ch) {
    int depth = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '[') depth++;
        else if (s[i] == ']') depth--;
        else if (s[i] == ch && depth == 0) return (int)i;
    }
    return -1;
}

const char *module_resolve_type_name(ModuleTable *table, const char *current_module,
                                     const char **using_modules, int using_count,
                                     const char *written) {
    if (!table || !written) return written;
    size_t len = strlen(written);
    if (len == 0) return written;
    Arena *arena = table->arena;

    /* Array: [T] or [T,N] */
    if (written[0] == '[' && written[len - 1] == ']') {
        size_t inner_len = len - 2;
        char *inner = arena_copy_string_with_length(arena, written + 1, inner_len);
        int comma = find_at_depth_zero(inner, inner_len, ',');
        const char *size_suffix = NULL;
        if (comma >= 0) {
            inner[comma] = '\0';
            size_suffix = inner + comma + 1;
        }
        const char *elem = module_resolve_type_name(table, current_module,
                                                    using_modules, using_count, inner);
        if (elem == inner && !size_suffix) return written;
        char buf[MSG_BUF_SIZE];
        if (size_suffix) snprintf(buf, sizeof(buf), "[%s,%s]", elem, size_suffix);
        else             snprintf(buf, sizeof(buf), "[%s]", elem);
        return arena_copy_string(arena, buf);
    }

    /* Pointer: ^T */
    if (written[0] == '^') {
        const char *pointee = module_resolve_type_name(table, current_module,
                                                       using_modules, using_count,
                                                       written + 1);
        if (pointee == written + 1) return written;
        char buf[MSG_BUF_SIZE];
        snprintf(buf, sizeof(buf), "^%s", pointee);
        return arena_copy_string(arena, buf);
    }

    /* Map: map[K:V] */
    if (len > 5 && strncmp(written, "map[", 4) == 0 && written[len - 1] == ']') {
        size_t inner_len = len - 5;
        char *inner = arena_copy_string_with_length(arena, written + 4, inner_len);
        int colon = find_at_depth_zero(inner, inner_len, ':');
        if (colon < 0) return written; /* malformed; leave alone */
        inner[colon] = '\0';
        const char *k = inner;
        const char *v = inner + colon + 1;
        const char *rk = module_resolve_type_name(table, current_module,
                                                  using_modules, using_count, k);
        const char *rv = module_resolve_type_name(table, current_module,
                                                  using_modules, using_count, v);
        if (rk == k && rv == v) return written;
        char buf[MSG_BUF_SIZE];
        snprintf(buf, sizeof(buf), "map[%s:%s]", rk, rv);
        return arena_copy_string(arena, buf);
    }

    /* Leaf. Only type declarations name a type; a function or constant that
     * happens to share the spelling must not capture it. */
    DeclEntry *entry = module_resolve_written(table, current_module, using_modules,
                                              using_count, written);
    if (!entry) return written;
    if (entry->kind != DECL_STRUCT && entry->kind != DECL_ENUM && entry->kind != DECL_ALIAS)
        return written;
    return module_mangle(table, entry);
}

/* --- Mangling --- */

const char *module_mangle_into(const DeclEntry *entry, char *buf, size_t buflen) {
    if (!entry) return NULL;
    if (entry->module_is_entry || !entry->module_name) return entry->name;
    snprintf(buf, buflen, "%s_%s", entry->module_name, entry->name);
    return buf;
}

const char *module_mangle(ModuleTable *table, const DeclEntry *entry) {
    char buf[MSG_BUF_SIZE];
    const char *mangled = module_mangle_into(entry, buf, sizeof(buf));
    return mangled ? arena_copy_string(table->arena, mangled) : NULL;
}

bool module_split_qualified(Arena *arena, const char *spelling,
                            const char **out_module, const char **out_name) {
    *out_module = NULL;
    *out_name = spelling;
    if (!spelling) return false;
    const char *dot = strchr(spelling, '.');
    if (!dot) return false;
    *out_module = arena_copy_string_with_length(arena, spelling, (size_t)(dot - spelling));
    *out_name = dot + 1;
    return true;
}
