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
    char arena_variable[32];
    char saved_variable[32];
} ScopeArena;

typedef struct {
    StringBuffer output;
    StringBuffer global_initializer; /* Deferred initialization for file-scope arrays */
    int indent;
    bool has_mem;       /* Whether @mem was imported */
    bool has_fmt;       /* Whether @fmt was imported */
    /* Whether the generated C needs a stdlib collection header — set when the
     * `in` operator lowers to that module's helper, or the module is imported.
     * Consulted after body emission to splice the #include into the preamble. */
    bool needs_arrays_header;
    bool needs_maps_header;
    bool needs_strings_header;
    const char *file;
    char *file_owned; /* normalized copy backing `file`; freed by codegen_destroy */

    /* Track declared type names for codegen */
    const char **enum_names;
    bool *is_enum_string;
    bool *is_enum_tagged;    /* parallel: true if tagged union enum */
    AstNode **enum_declarations;    /* parallel: AST nodes for payload type lookup */
    int enum_count;
    int enum_capacity;

    /* Current function context (for multi-return, ensure) */
    AstNode *current_function;

    /* Number of top-level defer/ensure statements whose source position codegen
     * has already passed in the current function. A return only runs the
     * defer/ensure statements that control flow has actually reached. */
    int ensure_reached;

    /* loop scoping depth — when > 0, codegen is inside a
     * scoped loop and container mutations need escape-copy logic. */
    int loop_scope_depth;

    /* True while emitting the body of a loop that opens no iteration arena,
     * so break/continue have no arena pointer to restore. */
    bool is_in_no_arena_loop;

    /* Non-zero while function_uses_watermark scans a body: calls are not
     * treated as allocation-free there, which stops mutually recursive
     * functions from recursing through the analysis. */
    int watermark_probe;

    /* All function declarations (for mutable param lookup at call sites) */
    AstNode **all_functions;
    int function_count;
    int function_capacity;

    /* Sorted view of all_functions by function_declaration.name, built lazily for
     * O(log n) find_function lookups. all_functions itself stays in insertion
     * order because emission and several prefix-match scans depend on it. */
    AstNode **functions_by_name;
    bool is_functions_by_name_built;

    /* Type table from type checker (for type-aware codegen) */
    TypeTable *type_table;

    /* Module symbol table from the type checker. Module membership and the
     * mangled name of any declaration come from here — codegen does not
     * re-derive either. */
    ModuleTable *modules;

    /* The module whose file is currently being emitted. A name written bare
     * inside a module body resolves against this, the way the type checker
     * resolves one against the file being checked. */
    const char *current_module;

    /* The file that module's declarations are being emitted from. Visibility
     * is file-scoped, so resolution needs it alongside the module. */
    const char *current_file;

    /* Current var decl context (for context-aware call emission) */
    const char *current_variable_name;
    const char *current_variable_type;

    /* Ref variables (transparent references from ref()) */
    const char **reference_variables;
    int reference_variable_count;
    int reference_variable_capacity;

    /* Raw pointer variables (from raw()) — dereference skips nil check.
     * Stored as a stack: most recent entry for a name wins.  Entries
     * with is_raw=false act as overrides (e.g. addr() shadowing raw()). */
    struct { const char *name; bool is_raw; } *raw_variables;
    int raw_variable_count;
    int raw_variable_capacity;

    /* Heap-allocated pointer variables (from new()) — field container
     * (map/array/string/struct) reassignment through these pointers must
     * escape to gray_heap_arena so the value outlives the current
     * function's own scoped arena. Same shadow-stack shape as raw_variables. */
    struct { const char *name; bool is_heap; } *heap_variables;
    int heap_variable_count;
    int heap_variable_capacity;

    /* @mem-arena-tracked pointer variables (from mem.init()/mem.alloc()) —
     * dereference emits a live check against the arena expression that
     * produced them, so a use after that arena was destroyed/reset panics
     * (P0105) instead of silently reading freed memory. arena_expr is NULL
     * for an unregister entry (shadowing/reassignment from a non-mem
     * source). Same shadow-stack shape as raw_variables/heap_variables. Only
     * registered when the arena argument is a side-effect-free, safely
     * re-evaluable expression (see is_stable_arena_expr in codegen.c) —
     * the same expression is re-emitted at every dereference site. */
    struct { const char *name; AstNode *arena_expression; } *mem_variables;
    int mem_variable_count;
    int mem_variable_capacity;

    /* Track declared wide integer variable types (name → type_name) */
    const char **wide_integer_variable_names;
    const char **wide_integer_variable_types;
    int wide_integer_variable_count;
    int wide_integer_variable_capacity;

    /* Struct declarations for composite printing */
    AstNode **struct_declarations;
    int struct_declaration_count;
    int struct_declaration_capacity;

    /* Lazy index of (field_name, struct_name) for func-typed struct fields,
     * built on first lookup. Lets the member-call fallback heuristic skip
     * the O(struct_count * field_count) scan over non-func fields. */
    struct {
        const char *field_name;
        const char *struct_name;
    } *function_field_index;
    int function_field_count;
    bool is_function_field_index_built;

    /* Modules brought into scope via 'using' or 'import and use' */
    const char **using_modules;
    int using_module_count;
    int using_module_capacity;

    /* All imported module names (for module detection in member expressions) */
    const char **imported_modules;
    int imported_module_count;
    int imported_module_capacity;

    /* C interop headers from extern import "header.h". is_c_header_local[i]
     * says whether c_headers[i] is a "./x.h"/"../x.h" header — resolved to
     * its canonical absolute path by the time it lands here, so it no longer
     * carries the "./" spelling that would otherwise mark it — and so must
     * be emitted as a quoted #include rather than an angle-bracket one. */
    const char **c_headers;
    bool *is_c_header_local;
    int c_header_count;
    int c_header_capacity;
    bool has_c_imports;

    /* Type alias registry (alias Name = Type) — collected from AST */
    const char **type_alias_names;
    const char **type_alias_targets;
    int type_alias_count;
    int type_alias_capacity;

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
    void *pending_call_typed_signature;

    /* True while emitting the initializer of a file-scope const declaration.
     * Prevents runtime overflow-check wrappers (gray_add_check etc.) from being
     * emitted as C file-scope initializers, which C does not allow. */
    bool is_in_const_declaration;

    /* Arena growth limit in bytes (0 = use 1 GB default) */
    size_t arena_limit;

    /* --test mode: emit a test runner main() that calls every #test
     * function; in normal builds #test functions are not emitted at all. */
    bool is_test_mode;

    /* Monotonic counter for generating unique temporary variable names.
     * Every emitter that needs a unique C identifier draws from this
     * single counter via codegen_next_id(). */
    int temporary_counter;

    /* Stack of open per-scope scratch arenas (if / for_each / while /
     * loop). Each entry holds the exact C identifiers emitted at scope
     * entry so any early-exit path (return, or_return-desugared return)
     * can unwind every live scratch arena innermost-first before the
     * function-arena cleanup. Without this, nested scratch arenas leak
     * on early return. */
    ScopeArena *scope_arenas;
    int scope_arena_count;
    int scope_arena_capacity;

    /* Stack of active for_each iteration guards. Each entry holds the C
     * expression whose .iterating counter was incremented. On early return,
     * every live guard must be decremented before leaving the function. */
    char **iteration_guards;
    int iteration_guard_count;
    int iteration_guard_capacity;

    /* Heap-allocated "StructName_funcName" strings patched into AST nodes.
     * Tracked here so codegen_destroy() can free them. */
    char **namespaced_function_names;
    int namespaced_function_name_count;
    int namespaced_function_name_capacity;

    /* #line directive emission (maps generated C back to the .gray source
     * for cc diagnostics, sanitizers, and gcov). On by default; a raw-C
     * debugging mode can turn it off to read the generated C's own line
     * numbers instead. last_line_directive_file/_line track the .gray
     * location the last directive pointed at, so a run of statements on
     * the same source line emits one directive, not one per statement. */
    bool should_emit_line_directives;
    char *last_line_directive_file;
    int last_line_directive_line;
} CodeGen;

CodeGen codegen_create(const char *file);
void codegen_generate(CodeGen *codegen, AstNode *program);
const char *codegen_result(CodeGen *codegen);
void codegen_destroy(CodeGen *codegen);

#endif
