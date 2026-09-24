/*
 * module_table.h — Declares the per-module symbol table: one ModuleScope per
 * module (the entry file included), each holding the declarations written in
 * that module under the names they were written with. Replaces the string
 * mangling that previously encoded module membership into identifiers.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_MODULE_TABLE_H
#define GRAYC_MODULE_TABLE_H

#include "types.h"
#include "../parser/ast.h"
#include "../util/arena.h"

/* The entry file's module is keyed under a name no import can produce. An
 * imported module whose basename matches the entry file's therefore stays a
 * separate scope, and no qualifier can ever name the entry module — which
 * Grayscale has no syntax for in the first place. */
#define MODULE_ENTRY_NAME ""

typedef enum {
    DECLARATION_STRUCT,
    DECLARATION_ENUM,
    DECLARATION_FUNCTION,
    DECLARATION_ALIAS,
    DECLARATION_CONST,
} DeclarationKind;

typedef enum {
    VISIBILITY_PUBLIC,
    VISIBILITY_PRIVATE,
} Visibility;

/* One top-level declaration, keyed in its owning module under the name as
 * written in source. Entries are allocated individually from the compiler
 * arena so a DeclarationEntry* cached on an AST node stays valid as the owning
 * module grows. */
typedef struct DeclarationEntry_ {
    DeclarationKind kind;
    const char *name;         /* as written in source — never mangled */
    const char *module_name;  /* owning module; back-pointer for mangling */
    bool is_module_entry;     /* owning module is the entry file: emits unprefixed */
    AstNode *ast_node;        /* original, unrenamed declaration node */
    const char *origin_file;
    int origin_line;
    Visibility visibility;
    /* Declared by the compiler rather than by Grayscale source: a stdlib
     * function or opaque type. It resolves like anything else, but its C name
     * is produced by the stdlib emitter, not by mangling this entry. */
    bool is_external;
    /* Where this declaration's details live in the registry for its kind
     * (struct fields, function signature, enum variants), or -1 before they
     * are registered. Resolving a name yields the entry, and the entry yields
     * the details directly — no second lookup keyed by a mangled string. */
    int registry_index;
    /* The mangled C name, arena-allocated on first request and reused after.
     * The spelling is fixed by kind/name/module, so every later resolution of
     * this declaration returns the same pointer instead of a fresh copy. */
    const char *cached_mangled_name;
} DeclarationEntry;

typedef struct {
    const char *name;  /* NULL = empty slot */
    int index;         /* index into the owning array */
} ModuleHashEntry;

/* Reverse index: declaration node -> its entry. Lets a later phase recover a
 * declaration's module from the node alone, without a name to look up. */
typedef struct {
    const AstNode *node;  /* NULL = empty slot */
    DeclarationEntry *entry;
} ModuleNodeEntry;

/* The declarations of one module. Every .gray file of a directory-merged
 * module inserts into the same ModuleScope, so sibling lookups are ordinary
 * same-module lookups. */
typedef struct {
    const char *name;
    bool is_entry;     /* the entry file's module — its symbols emit unprefixed */
    DeclarationEntry **entries;
    int count;
    int capacity;
    ModuleHashEntry *hash;  /* open addressing; NULL until first insert */
    int hash_capacity;           /* always a power of 2 */
} ModuleScope;

typedef struct {
    Arena *arena;
    const char *entry_module;  /* the entry file's module; NULL until mapped */
    ModuleScope **modules;
    int count;
    int capacity;
    ModuleHashEntry *hash;
    int hash_capacity;

    /* import alias -> real module name */
    const char **alias_names;
    const char **alias_modules;
    int alias_count;
    int alias_capacity;

    /* source file -> owning module. A declaration belongs to the module of
     * the file it was written in, which is what makes every .gray file of a
     * directory-merged module land in one ModuleScope. */
    ModuleNodeEntry *node_index;
    int node_count;
    int node_hash_capacity;

    /* Mangled C name -> entry. One index replacing the per-registry sorted
     * name arrays each phase used to keep. */
    ModuleHashEntry *mangled_index;
    DeclarationEntry **mangled_entries;
    int mangled_count;
    int mangled_capacity;
    int mangled_hash_capacity;

    const char **file_paths;
    const char **file_modules;
    ModuleHashEntry *file_hash;
    int file_count;
    int file_capacity;
    int file_hash_capacity;
} ModuleTable;

/* Where a name is being resolved from. `module` scopes an unqualified
 * lookup; `file` decides visibility, because a private declaration is
 * private to the file that declares it, not to its module — a directory
 * module's files do not see each other's private declarations. */
typedef struct {
    const char *module;
    const char *file;
    const char **using_modules;
    int using_count;
} ResolveScope;

/* Why a qualified lookup failed. Callers need the distinction to pick between
 * "no such module", "no such member", and the private-access diagnostics. */
typedef enum {
    RESOLVE_OK,
    RESOLVE_NO_MODULE,  /* the qualifier names no known module or alias */
    RESOLVE_NO_DECLARATION,    /* the module exists but declares no such name */
    RESOLVE_PRIVATE,    /* declared, but private to a different module */
} ResolveStatus;

ModuleTable *module_table_create(Arena *arena);

/* Record which module a source file belongs to, creating the module's scope.
 * `is_entry` marks the entry file, whose module emits unprefixed symbols and
 * is keyed under MODULE_ENTRY_NAME regardless of `module_name`. */
void module_table_map_file(ModuleTable *table, const char *file,
                           const char *module_name, bool is_entry);

/* The module a file belongs to, falling back to the entry module for files
 * that were never mapped (synthetic nodes carry no usable path). */
const char *module_table_module_for_file(ModuleTable *table, const char *file);

/* Get the named module's scope, creating it if absent. `is_entry` is only
 * honored on creation. */
ModuleScope *module_table_scope(ModuleTable *table, const char *module_name,
                                bool is_entry);

/* Get the named module's scope, or NULL if it has none. */
ModuleScope *module_table_find(ModuleTable *table, const char *module_name);

/* Insert a declaration into a module. Returns the stored entry, or the
 * existing entry (unmodified) if `name` is already declared in that module. */
DeclarationEntry *module_scope_define(ModuleTable *table, ModuleScope *scope,
                               DeclarationKind kind, const char *name,
                               AstNode *ast_node,
                               const char *origin_file, int origin_line,
                               Visibility visibility);

/* Look a name up in one module, ignoring visibility. */
DeclarationEntry *module_scope_lookup(ModuleScope *scope, const char *name);

/* The entry declared by this AST node, or NULL. The module a declaration
 * belongs to is a property of the declaration, so a phase holding the node
 * needs no name and no file of its own to recover it. */
DeclarationEntry *module_table_entry_for_node(ModuleTable *table, const AstNode *node);

/* The entry whose mangled C name is `mangled`, or NULL. For lookups that
 * already hold the emitted name rather than the name as written. */
DeclarationEntry *module_table_find_mangled(ModuleTable *table, const char *mangled);

/* Declare something that has no source declaration node of its own — a
 * struct function, namespaced under its struct, or a compiler-provided type.
 * It joins the mangled index so it resolves like anything else. */
DeclarationEntry *module_table_declare_synthetic(ModuleTable *table, const char *module_name,
                                          DeclarationKind kind, const char *name,
                                          const char *origin_file);

void module_table_add_alias(ModuleTable *table, const char *alias,
                            const char *module_name);

/* Map an import alias to the module it names. Returns `alias` unchanged when
 * it is not an alias, so the result is always usable as a module name. */
const char *module_table_resolve_alias(ModuleTable *table, const char *alias);

/* Resolve `module_or_alias.name` as seen from `scope`, applying the
 * visibility rule. `out_status` may be NULL. On RESOLVE_PRIVATE the entry is
 * still returned so the caller can report where it was declared. */
DeclarationEntry *module_resolve_qualified(ModuleTable *table,
                                    const ResolveScope *scope,
                                    const char *module_or_alias,
                                    const char *name,
                                    ResolveStatus *out_status);

/* Resolve a bare `name`: the scope's own module first, then each `using`'d
 * module in declared order. A name found in more than one using'd module is
 * ambiguous — *out_ambiguous_with receives the second module's name and the
 * result is NULL. Pass NULL for out_ambiguous_with to take the first match. */
DeclarationEntry *module_resolve_unqualified(ModuleTable *table,
                                      const ResolveScope *scope,
                                      const char *name,
                                      const char **out_ambiguous_with);

/* Is `entry` reachable from `scope`? The single visibility rule. */
bool is_module_declaration_visible(const ResolveScope *scope, const DeclarationEntry *entry);

/* Resolve a name as written in source — "lib.Score" or a bare "Score" — as
 * seen from inside `current_module`. A qualified name goes to the module its
 * qualifier names; a bare name tries the current module first, then each
 * using'd module in declared order. */
DeclarationEntry *module_resolve_written(ModuleTable *table, const ResolveScope *scope,
                                  const char *written);

/* The mangled spelling of a written type name, with every leaf identifier
 * resolved through the symbol table. Handles the composite spellings —
 * [T], [T,N], ^T, map[K:V], and nestings of them — so that a type annotation
 * is rewritten in exactly one place instead of at each site that inspects it.
 * Returns `written` unchanged when nothing in it resolves to a declaration. */
const char *module_resolve_type_name(ModuleTable *table, const ResolveScope *scope,
                                     const char *written);

/* The C symbol name for a declaration: "mod_Name", or "Name" for the entry
 * module. The single point at which module membership becomes a string.
 *
 * module_mangle returns an arena-allocated name that outlives the call,
 * caching it on the entry so repeat calls share one copy; module_mangle_into
 * writes to a caller buffer, for lookup keys that do not outlive the call.
 * Both return their result. */
const char *module_mangle(ModuleTable *table, DeclarationEntry *entry);
const char *module_mangle_into(const DeclarationEntry *entry, char *name_buffer, size_t buffer_length);

/* Split "lib.Score" into ("lib", "Score"). Returns false when `spelling` has
 * no dot, leaving *out_module NULL and *out_name == spelling. */
bool module_split_qualified(Arena *arena, const char *spelling,
                            const char **out_module, const char **out_name);

#endif
