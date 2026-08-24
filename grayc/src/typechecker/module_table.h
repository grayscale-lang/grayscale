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
    DECL_STRUCT,
    DECL_ENUM,
    DECL_FUNC,
    DECL_ALIAS,
    DECL_CONST,
} DeclKind;

typedef enum {
    VIS_PUBLIC,
    VIS_PRIVATE,
} Visibility;

/* One top-level declaration, keyed in its owning module under the name as
 * written in source. Entries are allocated individually from the compiler
 * arena so a DeclEntry* cached on an AST node stays valid as the owning
 * module grows. */
typedef struct {
    DeclKind kind;
    const char *name;         /* as written in source — never mangled */
    const char *module_name;  /* owning module; back-pointer for mangling */
    bool module_is_entry;     /* owning module is the entry file: emits unprefixed */
    AstNode *ast_node;        /* original, unrenamed declaration node */
    GrayType *gray_type;      /* resolved type, or NULL until types are built */
    const char *origin_file;
    int origin_line;
    Visibility visibility;
} DeclEntry;

typedef struct {
    const char *name;  /* NULL = empty slot */
    int idx;           /* index into the owning array */
} ModuleHashEntry;

/* The declarations of one module. Every .gray file of a directory-merged
 * module inserts into the same ModuleScope, so sibling lookups are ordinary
 * same-module lookups. */
typedef struct {
    const char *name;
    bool is_entry;     /* the entry file's module — its symbols emit unprefixed */
    DeclEntry **entries;
    int count;
    int cap;
    ModuleHashEntry *hash;  /* open addressing; NULL until first insert */
    int hash_cap;           /* always a power of 2 */
} ModuleScope;

typedef struct {
    Arena *arena;
    const char *entry_module;  /* the entry file's module; NULL until mapped */
    ModuleScope **modules;
    int count;
    int cap;
    ModuleHashEntry *hash;
    int hash_cap;

    /* import alias -> real module name */
    const char **alias_names;
    const char **alias_modules;
    int alias_count;
    int alias_cap;

    /* source file -> owning module. A declaration belongs to the module of
     * the file it was written in, which is what makes every .gray file of a
     * directory-merged module land in one ModuleScope. */
    const char **file_paths;
    const char **file_modules;
    ModuleHashEntry *file_hash;
    int file_count;
    int file_cap;
    int file_hash_cap;
} ModuleTable;

/* Why a qualified lookup failed. Callers need the distinction to pick between
 * "no such module", "no such member", and the private-access diagnostics. */
typedef enum {
    RESOLVE_OK,
    RESOLVE_NO_MODULE,  /* the qualifier names no known module or alias */
    RESOLVE_NO_DECL,    /* the module exists but declares no such name */
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
DeclEntry *module_scope_define(ModuleTable *table, ModuleScope *scope,
                               DeclKind kind, const char *name,
                               AstNode *ast_node, GrayType *gray_type,
                               const char *origin_file, int origin_line,
                               Visibility visibility);

/* Look a name up in one module, ignoring visibility. */
DeclEntry *module_scope_lookup(ModuleScope *scope, const char *name);

void module_table_add_alias(ModuleTable *table, const char *alias,
                            const char *module_name);

/* Map an import alias to the module it names. Returns `alias` unchanged when
 * it is not an alias, so the result is always usable as a module name. */
const char *module_table_resolve_alias(ModuleTable *table, const char *alias);

/* Resolve `module_or_alias.name` as seen from `current_module`, applying the
 * visibility rule. `out_status` may be NULL. On RESOLVE_PRIVATE the entry is
 * still returned so the caller can point at its declaration site. */
DeclEntry *module_resolve_qualified(ModuleTable *table,
                                    const char *current_module,
                                    const char *module_or_alias,
                                    const char *name,
                                    ResolveStatus *out_status);

/* Resolve a bare `name`: the current module first, then each `using`'d module
 * in declared order. A name found in more than one using'd module is
 * ambiguous — *out_ambiguous_with receives the second module's name and the
 * result is NULL. Pass NULL for out_ambiguous_with to take the first match. */
DeclEntry *module_resolve_unqualified(ModuleTable *table,
                                      const char *current_module,
                                      const char **using_modules,
                                      int using_count,
                                      const char *name,
                                      const char **out_ambiguous_with);

/* The C symbol name for a declaration: "mod_Name", or "Name" for the entry
 * module. The single point at which module membership becomes a string.
 *
 * module_mangle copies into the table's arena, for names that outlive the
 * call; module_mangle_into writes to a caller buffer, for lookup keys that do
 * not. Both return their result. */
const char *module_mangle(ModuleTable *table, const DeclEntry *entry);
const char *module_mangle_into(const DeclEntry *entry, char *buf, size_t buflen);

/* Split "lib.Score" into ("lib", "Score"). Returns false when `spelling` has
 * no dot, leaving *out_module NULL and *out_name == spelling. */
bool module_split_qualified(Arena *arena, const char *spelling,
                            const char **out_module, const char **out_name);

#endif
