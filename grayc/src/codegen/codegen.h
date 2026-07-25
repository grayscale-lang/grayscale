/*
 * codegen.h — Public interface for C code generation, defining the Codegen
 * context and the entry point for translating a type-checked AST into C.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_CODEGEN_H
#define GRAYC_CODEGEN_H

#include "../parser/ast.h"
#include "../util/buf.h"
#include "../typechecker/typechecker.h"

/* Per-scope scratch arena identifier pair tracked by codegen so that
 * early-exit paths can unwind every live scratch arena innermost-first. */
typedef struct {
    char arena_var[32];
    char saved_var[32];
} ScopeArena;

typedef struct {
    Buf output;
    Buf global_init;    /* Deferred initialization for file-scope arrays */
    int indent;
    bool has_mem;       /* Whether @mem was imported */
    bool has_fmt;       /* Whether @fmt was imported */
    const char *file;

    /* Track declared type names for codegen */
    const char **enum_names;
    bool *enum_is_string;
    bool *enum_is_tagged;    /* parallel: true if tagged union enum */
    AstNode **enum_decls;    /* parallel: AST nodes for payload type lookup */
    int enum_count;
    int enum_cap;

    /* Current function context (for multi-return, ensure) */
    AstNode *current_func;

    /* loop scoping depth — when > 0, codegen is inside a
     * scoped loop and container mutations need escape-copy logic. */
    int loop_scope_depth;

    /* All function declarations (for mutable param lookup at call sites) */
    AstNode **all_funcs;
    int func_count;
    int func_cap;

    /* Sorted view of all_funcs by func_decl.name, built lazily for
     * O(log n) find_func lookups. all_funcs itself stays in insertion
     * order because emission and several prefix-match scans depend on it. */
    AstNode **funcs_by_name;
    bool funcs_by_name_built;

    /* Type table from type checker (for type-aware codegen) */
    TypeTable *type_table;

    /* Current var decl context (for context-aware call emission) */
    const char *current_var_name;
    const char *current_var_type;

    /* Ref variables (transparent references from ref()) */
    const char **ref_vars;
    int ref_var_count;
    int ref_var_cap;

    /* Track declared bigint variable types (name → type_name) */
    const char **bigint_var_names;
    const char **bigint_var_types;
    int bigint_var_count;
    int bigint_var_cap;

    /* Struct declarations for composite printing */
    AstNode **struct_decls;
    int struct_decl_count;
    int struct_decl_cap;

    /* Lazy index of (field_name, struct_name) for func-typed struct fields,
     * built on first lookup. Lets the member-call fallback heuristic skip
     * the O(struct_count * field_count) scan over non-func fields. */
    struct {
        const char *field_name;
        const char *struct_name;
    } *func_field_index;
    int func_field_count;
    bool func_field_index_built;

    /* Modules brought into scope via 'using' or 'import and use' */
    const char **using_modules;
    int using_module_count;
    int using_module_cap;

    /* Import alias → module name mapping */
    const char **alias_names;
    const char **alias_modules;
    int alias_count;
    int alias_cap;

    /* All imported module names (for module detection in member expressions) */
    const char **imported_modules;
    int imported_module_count;
    int imported_module_cap;

    /* C interop headers from import c"header.h" */
    const char **c_headers;
    int c_header_count;
    int c_header_cap;
    bool has_c_imports;

    /* Active wildcard binding (). Set while emitting a specialised
     * instantiation of a generic function so type-string lookups can
     * substitute "?" with a concrete type name, and so the mangled
     * function name can be appended at call sites. NULL outside a
     * generic instantiation. */
    const char *wildcard_binding;

    /* Side channel for typed-func call-through: when the callee is a
     * variable typed func(...), the cast emitter stashes the parsed
     * signature here so the arg-emission loop can pick up &-mutability
     * even when target_func (the AST decl) is unknown. Reset to NULL
     * after each call. */
    void *pending_call_typed_sig;

    /* True while emitting the initializer of a file-scope const declaration.
     * Prevents runtime overflow-check wrappers (gray_add_check etc.) from being
     * emitted as C file-scope initializers, which C does not allow. */
    bool in_const_decl;

    /* Stack of open per-scope scratch arenas (if / for_each / while /
     * loop). Each entry holds the exact C identifiers emitted at scope
     * entry so any early-exit path (return, or_return-desugared return)
     * can unwind every live scratch arena innermost-first before the
     * function-arena cleanup. Without this, nested scratch arenas leak
     * on early return. */
    ScopeArena *scope_arenas;
    int scope_arena_count;
    int scope_arena_cap;

    /* Stack of active for_each iteration guards. Each entry holds the C
     * expression whose .iterating counter was incremented. On early return,
     * every live guard must be decremented before leaving the function. */
    char **iter_guards;
    int iter_guard_count;
    int iter_guard_cap;

    /* Heap-allocated "StructName_funcName" strings patched into AST nodes.
     * Tracked here so codegen_destroy() can free them. */
    char **ns_func_names;
    int ns_func_name_count;
    int ns_func_name_cap;
} CodeGen;

CodeGen codegen_create(const char *file);
void codegen_generate(CodeGen *codegen, AstNode *program);
const char *codegen_result(CodeGen *codegen);
void codegen_destroy(CodeGen *codegen);

#endif
