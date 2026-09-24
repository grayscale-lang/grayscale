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

#define MODULE_HASH_INITIAL_CAPACITY 16

/* --- Shared open-addressing helpers ---
 *
 * Both the module table (module name -> ModuleScope) and each module scope
 * (declaration name -> DeclarationEntry) are name -> index maps over a dense array,
 * so they share one probe. Capacity is always a power of two and the load
 * factor is held at or below one half, which linear probing needs in order to
 * terminate on a free slot. */

static void hash_place(ModuleHashEntry *hash, int capacity, const char *name, int index) {
    uint32_t mask = (uint32_t)(capacity - 1);
    uint32_t slot = scope_string_hash(name) & mask;
    for (;;) {
        if (!hash[slot].name) {
            hash[slot].name = name;
            hash[slot].index = index;
            return;
        }
        slot = (slot + 1) & mask;
    }
}

/* Index of `name`, or -1 when absent. */
static int hash_find(const ModuleHashEntry *hash, int capacity, const char *name) {
    if (!hash) return -1;
    uint32_t mask = (uint32_t)(capacity - 1);
    uint32_t slot = scope_string_hash(name) & mask;
    for (;;) {
        const ModuleHashEntry *slot_entry = &hash[slot];
        if (!slot_entry->name) return -1;
        if (strcmp(slot_entry->name, name) == 0) return slot_entry->index;
        slot = (slot + 1) & mask;
    }
}

static ModuleHashEntry *hash_allocate(Arena *arena, int capacity) {
    ModuleHashEntry *hash = arena_allocate(arena, sizeof(ModuleHashEntry) * (size_t)capacity);
    memset(hash, 0, sizeof(ModuleHashEntry) * (size_t)capacity);
    return hash;
}

/* Grow the hash to hold new_count entries at a load factor of one half. */
static int hash_target_capacity(int current_capacity, int new_count) {
    int capacity = current_capacity ? current_capacity : MODULE_HASH_INITIAL_CAPACITY;
    while (new_count * 2 > capacity) capacity *= 2;
    return capacity;
}

/* --- Module table --- */

ModuleTable *module_table_create(Arena *arena) {
    ModuleTable *table = arena_allocate(arena, sizeof(ModuleTable));
    memset(table, 0, sizeof(ModuleTable));
    table->arena = arena;
    return table;
}

ModuleScope *module_table_find(ModuleTable *table, const char *module_name) {
    if (!table || !module_name) return NULL;
    int index = hash_find(table->hash, table->hash_capacity, module_name);
    return index < 0 ? NULL : table->modules[index];
}

ModuleScope *module_table_scope(ModuleTable *table, const char *module_name,
                                bool is_entry) {
    ModuleScope *existing = module_table_find(table, module_name);
    if (existing) return existing;

    Arena *arena = table->arena;
    ModuleScope *scope = arena_allocate(arena, sizeof(ModuleScope));
    memset(scope, 0, sizeof(ModuleScope));
    scope->name = arena_copy_string(arena, module_name);
    scope->is_entry = is_entry;

    ARENA_GROW(arena, table->modules, table->count, table->capacity);

    int new_count = table->count + 1;
    if (!table->hash || new_count * 2 > table->hash_capacity) {
        table->hash_capacity = hash_target_capacity(table->hash_capacity, new_count);
        table->hash = hash_allocate(arena, table->hash_capacity);
        for (int i = 0; i < table->count; i++)
            hash_place(table->hash, table->hash_capacity, table->modules[i]->name, i);
    }

    table->modules[table->count] = scope;
    hash_place(table->hash, table->hash_capacity, scope->name, table->count);
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
    if (hash_find(table->file_hash, table->file_hash_capacity, file) >= 0) return;

    if (table->file_count >= table->file_capacity) {
        table->file_capacity = GROW_NEXT_CAPACITY(table->file_capacity);
        ARENA_GROW_TO(arena, table->file_paths, table->file_count, table->file_capacity);
        ARENA_GROW_TO(arena, table->file_modules, table->file_count, table->file_capacity);
    }

    int new_count = table->file_count + 1;
    if (!table->file_hash || new_count * 2 > table->file_hash_capacity) {
        table->file_hash_capacity = hash_target_capacity(table->file_hash_capacity, new_count);
        table->file_hash = hash_allocate(arena, table->file_hash_capacity);
        for (int i = 0; i < table->file_count; i++)
            hash_place(table->file_hash, table->file_hash_capacity, table->file_paths[i], i);
    }

    table->file_paths[table->file_count] = arena_copy_string(arena, file);
    table->file_modules[table->file_count] = module_table_find(table, module_name)->name;
    hash_place(table->file_hash, table->file_hash_capacity,
               table->file_paths[table->file_count], table->file_count);
    table->file_count++;
}

const char *module_table_module_for_file(ModuleTable *table, const char *file) {
    if (!table) return NULL;
    if (file) {
        int index = hash_find(table->file_hash, table->file_hash_capacity, file);
        if (index >= 0) return table->file_modules[index];
    }
    return table->entry_module;
}

/* --- Node -> declaration index --- */

static void node_index_place(ModuleNodeEntry *index, int capacity,
                             const AstNode *node, DeclarationEntry *entry) {
    uint32_t mask = (uint32_t)(capacity - 1);
    /* Pointer hash: the low bits of an allocation address carry no entropy,
     * so mix the whole value down first. */
    uint32_t slot = (uint32_t)(((uintptr_t)node >> 4) * 2654435761u) & mask;
    for (;;) {
        if (!index[slot].node) {
            index[slot].node = node;
            index[slot].entry = entry;
            return;
        }
        slot = (slot + 1) & mask;
    }
}

static void node_index_add(ModuleTable *table, const AstNode *node, DeclarationEntry *entry) {
    if (!node) return;
    int new_count = table->node_count + 1;
    if (!table->node_index || new_count * 2 > table->node_hash_capacity) {
        int capacity = hash_target_capacity(table->node_hash_capacity, new_count);
        ModuleNodeEntry *fresh = arena_allocate(table->arena, sizeof(ModuleNodeEntry) * (size_t)capacity);
        memset(fresh, 0, sizeof(ModuleNodeEntry) * (size_t)capacity);
        for (int i = 0; i < table->node_hash_capacity; i++) {
            if (table->node_index[i].node)
                node_index_place(fresh, capacity, table->node_index[i].node, table->node_index[i].entry);
        }
        table->node_index = fresh;
        table->node_hash_capacity = capacity;
    }
    node_index_place(table->node_index, table->node_hash_capacity, node, entry);
    table->node_count++;
}

DeclarationEntry *module_table_entry_for_node(ModuleTable *table, const AstNode *node) {
    if (!table || !node || !table->node_index) return NULL;
    uint32_t mask = (uint32_t)(table->node_hash_capacity - 1);
    uint32_t slot = (uint32_t)(((uintptr_t)node >> 4) * 2654435761u) & mask;
    for (;;) {
        ModuleNodeEntry *slot_entry = &table->node_index[slot];
        if (!slot_entry->node) return NULL;
        if (slot_entry->node == node) return slot_entry->entry;
        slot = (slot + 1) & mask;
    }
}

/* --- Mangled name -> entry --- */

static void mangled_index_add(ModuleTable *table, DeclarationEntry *entry) {
    char name_buffer[MESSAGE_BUFFER_SIZE];
    const char *mangled = module_mangle_into(entry, name_buffer, sizeof(name_buffer));
    if (!mangled) return;
    if (hash_find(table->mangled_index, table->mangled_hash_capacity, mangled) >= 0) return;

    Arena *arena = table->arena;
    if (table->mangled_count >= table->mangled_capacity) {
        table->mangled_capacity = GROW_NEXT_CAPACITY(table->mangled_capacity);
        ARENA_GROW_TO(arena, table->mangled_entries, table->mangled_count, table->mangled_capacity);
    }
    int new_count = table->mangled_count + 1;
    if (!table->mangled_index || new_count * 2 > table->mangled_hash_capacity) {
        table->mangled_hash_capacity = hash_target_capacity(table->mangled_hash_capacity, new_count);
        table->mangled_index = hash_allocate(arena, table->mangled_hash_capacity);
        for (int i = 0; i < table->mangled_count; i++) {
            char rehash_name_buffer[MESSAGE_BUFFER_SIZE];
            hash_place(table->mangled_index, table->mangled_hash_capacity,
                       arena_copy_string(arena,
                           module_mangle_into(table->mangled_entries[i], rehash_name_buffer, sizeof(rehash_name_buffer))), i);
        }
    }
    table->mangled_entries[table->mangled_count] = entry;
    hash_place(table->mangled_index, table->mangled_hash_capacity,
               arena_copy_string(arena, mangled), table->mangled_count);
    table->mangled_count++;
}

DeclarationEntry *module_table_find_mangled(ModuleTable *table, const char *mangled) {
    if (!table || !mangled) return NULL;
    int index = hash_find(table->mangled_index, table->mangled_hash_capacity, mangled);
    return index < 0 ? NULL : table->mangled_entries[index];
}

DeclarationEntry *module_table_declare_synthetic(ModuleTable *table, const char *module_name,
                                          DeclarationKind kind, const char *name,
                                          const char *origin_file) {
    if (!table) return NULL;
    ModuleScope *scope = module_table_scope(table, module_name ? module_name : MODULE_ENTRY_NAME,
                                            !module_name || !*module_name);
    return module_scope_define(table, scope, kind, name, NULL, origin_file, 0, VISIBILITY_PUBLIC);
}

/* --- Module scopes --- */

DeclarationEntry *module_scope_lookup(ModuleScope *scope, const char *name) {
    if (!scope || !name) return NULL;
    int index = hash_find(scope->hash, scope->hash_capacity, name);
    return index < 0 ? NULL : scope->entries[index];
}

DeclarationEntry *module_scope_define(ModuleTable *table, ModuleScope *scope,
                               DeclarationKind kind, const char *name,
                               AstNode *ast_node,
                               const char *origin_file, int origin_line,
                               Visibility visibility) {
    DeclarationEntry *existing = module_scope_lookup(scope, name);
    if (existing) return existing;

    Arena *arena = table->arena;
    DeclarationEntry *entry = arena_allocate(arena, sizeof(DeclarationEntry));
    memset(entry, 0, sizeof(DeclarationEntry));
    entry->kind = kind;
    entry->name = arena_copy_string(arena, name);
    entry->module_name = scope->name;
    entry->is_module_entry = scope->is_entry;
    entry->ast_node = ast_node;
    entry->origin_file = origin_file;
    entry->origin_line = origin_line;
    entry->visibility = visibility;
    entry->registry_index = -1;
    entry->is_external = false;

    ARENA_GROW(arena, scope->entries, scope->count, scope->capacity);

    int new_count = scope->count + 1;
    if (!scope->hash || new_count * 2 > scope->hash_capacity) {
        scope->hash_capacity = hash_target_capacity(scope->hash_capacity, new_count);
        scope->hash = hash_allocate(arena, scope->hash_capacity);
        for (int i = 0; i < scope->count; i++)
            hash_place(scope->hash, scope->hash_capacity, scope->entries[i]->name, i);
    }

    scope->entries[scope->count] = entry;
    hash_place(scope->hash, scope->hash_capacity, entry->name, scope->count);
    scope->count++;
    node_index_add(table, ast_node, entry);
    mangled_index_add(table, entry);
    return entry;
}

/* --- Aliases --- */

void module_table_add_alias(ModuleTable *table, const char *alias,
                            const char *module_name) {
    if (!alias || !module_name || strcmp(alias, module_name) == 0) return;
    for (int i = 0; i < table->alias_count; i++) {
        if (strcmp(table->alias_names[i], alias) == 0) return;
    }
    if (table->alias_count >= table->alias_capacity) {
        table->alias_capacity = GROW_NEXT_CAPACITY(table->alias_capacity);
        ARENA_GROW_TO(table->arena, table->alias_names, table->alias_count, table->alias_capacity);
        ARENA_GROW_TO(table->arena, table->alias_modules, table->alias_count, table->alias_capacity);
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

bool is_module_declaration_visible(const ResolveScope *scope, const DeclarationEntry *entry) {
    if (!entry || entry->visibility != VISIBILITY_PRIVATE) return true;
    /* Private is private to the declaring file. Two files of one directory
     * module are as much "outside" each other as two separate modules. */
    const char *from = scope ? scope->file : NULL;
    if (!from || !entry->origin_file) return false;
    return strcmp(from, entry->origin_file) == 0;
}

DeclarationEntry *module_resolve_qualified(ModuleTable *table,
                                    const ResolveScope *scope,
                                    const char *module_or_alias,
                                    const char *name,
                                    ResolveStatus *out_status) {
    ResolveStatus status = RESOLVE_NO_MODULE;
    DeclarationEntry *entry = NULL;

    /* The empty name keys the entry module, which no source qualifier can
     * spell. Guarding here keeps a stray empty qualifier from reaching into
     * the entry file's declarations. */
    if (table && module_or_alias && *module_or_alias && name) {
        const char *module_name = module_table_resolve_alias(table, module_or_alias);
        ModuleScope *module_scope = module_table_find(table, module_name);
        if (module_scope) {
            entry = module_scope_lookup(module_scope, name);
            if (!entry) status = RESOLVE_NO_DECLARATION;
            else if (!is_module_declaration_visible(scope, entry)) status = RESOLVE_PRIVATE;
            else status = RESOLVE_OK;
        }
    }

    if (out_status) *out_status = status;
    /* entry is non-NULL exactly for RESOLVE_OK and RESOLVE_PRIVATE. */
    return entry;
}

DeclarationEntry *module_resolve_unqualified(ModuleTable *table,
                                      const ResolveScope *scope,
                                      const char *name,
                                      const char **out_ambiguous_with) {
    if (out_ambiguous_with) *out_ambiguous_with = NULL;
    if (!table || !name || !scope) return NULL;

    /* The current module always wins — a local declaration shadows anything a
     * `using` brought in, and is never ambiguous with it. */
    ModuleScope *own_module = module_table_find(table, scope->module);
    if (own_module) {
        DeclarationEntry *entry = module_scope_lookup(own_module, name);
        if (entry) return entry;
    }

    DeclarationEntry *found = NULL;
    for (int i = 0; i < scope->using_count; i++) {
        const char *module_name = module_table_resolve_alias(table, scope->using_modules[i]);
        ModuleScope *module_scope = module_table_find(table, module_name);
        if (!module_scope) continue;
        DeclarationEntry *entry = module_scope_lookup(module_scope, name);
        if (!entry || !is_module_declaration_visible(scope, entry)) continue;
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

DeclarationEntry *module_resolve_written(ModuleTable *table, const ResolveScope *scope,
                                  const char *written) {
    if (!table || !written || !scope) return NULL;
    /* Leaf names only. A composite spelling — [T], ^T, map[K:V] — has to go
     * through module_resolve_type_name, which takes it apart first; splitting
     * one here on its first '.' would produce nonsense like ("[types",
     * "Item]"). */
    if (strpbrk(written, "[]^:?,")) return NULL;
    const char *dot = strchr(written, '.');
    if (dot) {
        char qualifier[MESSAGE_BUFFER_SIZE];
        size_t qualifier_length = (size_t)(dot - written);
        if (qualifier_length >= sizeof(qualifier)) return NULL;
        memcpy(qualifier, written, qualifier_length);
        qualifier[qualifier_length] = '\0';
        return module_resolve_qualified(table, scope, qualifier, dot + 1, NULL);
    }
    return module_resolve_unqualified(table, scope, written, NULL);
}

/* --- Written type names --- */

/* Index of `character` at bracket depth zero, or -1. */
static int find_at_depth_zero(const char *text, size_t length, char character) {
    int depth = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '[') depth++;
        else if (text[i] == ']') depth--;
        else if (text[i] == character && depth == 0) return (int)i;
    }
    return -1;
}

const char *module_resolve_type_name(ModuleTable *table, const ResolveScope *scope,
                                     const char *written) {
    if (!table || !written) return written;
    size_t length = strlen(written);
    if (length == 0) return written;
    Arena *arena = table->arena;

    /* Array: [T] or [T,N] */
    if (written[0] == '[' && written[length - 1] == ']') {
        size_t inner_length = length - 2;
        char *inner = arena_copy_string_with_length(arena, written + 1, inner_length);
        int comma = find_at_depth_zero(inner, inner_length, ',');
        const char *size_suffix = NULL;
        if (comma >= 0) {
            inner[comma] = '\0';
            size_suffix = inner + comma + 1;
        }
        const char *element_type = module_resolve_type_name(table, scope, inner);
        if (element_type == inner && !size_suffix) return written;
        char name_buffer[MESSAGE_BUFFER_SIZE];
        if (size_suffix) snprintf(name_buffer, sizeof(name_buffer), "[%s,%s]", element_type, size_suffix);
        else             snprintf(name_buffer, sizeof(name_buffer), "[%s]", element_type);
        return arena_copy_string(arena, name_buffer);
    }

    /* Pointer: ^T */
    if (written[0] == '^') {
        const char *pointee = module_resolve_type_name(table, scope, written + 1);
        if (pointee == written + 1) return written;
        char name_buffer[MESSAGE_BUFFER_SIZE];
        snprintf(name_buffer, sizeof(name_buffer), "^%s", pointee);
        return arena_copy_string(arena, name_buffer);
    }

    /* Map: map[K:V] */
    if (length > 5 && strncmp(written, "map[", 4) == 0 && written[length - 1] == ']') {
        size_t inner_length = length - 5;
        char *inner = arena_copy_string_with_length(arena, written + 4, inner_length);
        int colon = find_at_depth_zero(inner, inner_length, ':');
        if (colon < 0) return written; /* malformed; leave alone */
        inner[colon] = '\0';
        const char *key_type_name = inner;
        const char *value_type_name = inner + colon + 1;
        const char *resolved_key = module_resolve_type_name(table, scope, key_type_name);
        const char *resolved_value = module_resolve_type_name(table, scope, value_type_name);
        if (resolved_key == key_type_name && resolved_value == value_type_name) return written;
        char name_buffer[MESSAGE_BUFFER_SIZE];
        snprintf(name_buffer, sizeof(name_buffer), "map[%s:%s]", resolved_key, resolved_value);
        return arena_copy_string(arena, name_buffer);
    }

    /* Typed function reference: func(P1,&P2,...)->R, ->(R1,R2), or a bare
     * func(...) for void. Each parameter and return type is a written type
     * of its own — resolved the same recursive way an array element or map
     * key/value already is above. Without this a module-local struct name
     * inside a callback signature ("func(Rec)->bool") kept its bare spelling
     * while every other appearance of that struct was mangled, so the two
     * spellings of the same struct compared unequal. type_from_name() already
     * knows how to parse the signature into per-component strings (GrayFunctionSignature);
     * reuse that instead of re-parsing it here. */
    if (length > 5 && strncmp(written, "func(", 5) == 0) {
        GrayType *function_type = type_from_name(written);
        if (!function_type || function_type->kind != TYPE_KIND_FUNCTION || !function_type->function_signature) return written;
        GrayFunctionSignature *function_signature = function_type->function_signature;
        bool was_changed = false;
        char parameter_list[MESSAGE_BUFFER_SIZE];
        parameter_list[0] = '\0';
        for (int i = 0; i < function_signature->parameter_count; i++) {
            const char *parameter_type_name = function_signature->parameter_types[i];
            const char *resolved = module_resolve_type_name(table, scope, parameter_type_name);
            if (resolved != parameter_type_name) was_changed = true;
            char piece[MESSAGE_BUFFER_SIZE];
            snprintf(piece, sizeof(piece), "%s%s%s", i ? "," : "",
                function_signature->is_parameter_mutable[i] ? "&" : "", resolved);
            strncat(parameter_list, piece, sizeof(parameter_list) - strlen(parameter_list) - 1);
        }
        char return_list[MESSAGE_BUFFER_SIZE];
        return_list[0] = '\0';
        if (function_signature->return_count == 1) {
            const char *return_type_name = function_signature->return_types[0];
            const char *resolved = module_resolve_type_name(table, scope, return_type_name);
            if (resolved != return_type_name) was_changed = true;
            snprintf(return_list, sizeof(return_list), "%s", resolved);
        } else if (function_signature->return_count > 1) {
            char parts[MESSAGE_BUFFER_SIZE];
            parts[0] = '\0';
            for (int i = 0; i < function_signature->return_count; i++) {
                const char *return_type_name = function_signature->return_types[i];
                const char *resolved = module_resolve_type_name(table, scope, return_type_name);
                if (resolved != return_type_name) was_changed = true;
                char piece[MESSAGE_BUFFER_SIZE];
                snprintf(piece, sizeof(piece), "%s%s", i ? "," : "", resolved);
                strncat(parts, piece, sizeof(parts) - strlen(parts) - 1);
            }
            snprintf(return_list, sizeof(return_list), "(%s)", parts);
        }
        if (!was_changed) return written;
        char name_buffer[MESSAGE_BUFFER_SIZE];
        if (function_signature->return_count > 0) snprintf(name_buffer, sizeof(name_buffer), "func(%s)->%s", parameter_list, return_list);
        else                        snprintf(name_buffer, sizeof(name_buffer), "func(%s)", parameter_list);
        return arena_copy_string(arena, name_buffer);
    }

    /* Leaf. Only type declarations name a type; a function or constant that
     * happens to share the spelling must not capture it. */
    DeclarationEntry *entry = module_resolve_written(table, scope, written);
    if (!entry) return written;
    if (entry->kind != DECLARATION_STRUCT && entry->kind != DECLARATION_ENUM && entry->kind != DECLARATION_ALIAS)
        return written;
    return module_mangle(table, entry);
}

/* --- Mangling --- */

const char *module_mangle_into(const DeclarationEntry *entry, char *name_buffer, size_t buffer_length) {
    if (!entry) return NULL;
    if (entry->is_external) return entry->name;
    if (entry->is_module_entry || !entry->module_name) return entry->name;
    snprintf(name_buffer, buffer_length, "%s_%s", entry->module_name, entry->name);
    return name_buffer;
}

const char *module_mangle(ModuleTable *table, DeclarationEntry *entry) {
    if (!entry) return NULL;
    if (entry->cached_mangled_name) return entry->cached_mangled_name;
    char name_buffer[MESSAGE_BUFFER_SIZE];
    const char *mangled = module_mangle_into(entry, name_buffer, sizeof(name_buffer));
    if (!mangled) return NULL;
    /* module_mangle_into hands back entry->name directly for the unprefixed
     * cases (entry module, external); only a prefixed name is a fresh buffer
     * that needs copying. */
    entry->cached_mangled_name = (mangled == entry->name)
        ? entry->name
        : arena_copy_string(table->arena, mangled);
    return entry->cached_mangled_name;
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
