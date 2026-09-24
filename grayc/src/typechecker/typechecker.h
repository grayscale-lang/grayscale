/*
 * typechecker.h — Public interface for the Grayscale type checker, defining
 * the TypeChecker context, type table, struct/function signature registries,
 * and the entry points for semantic analysis of AST programs.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_TYPECHECKER_H
#define GRAYC_TYPECHECKER_H

#include "types.h"
#include "scope.h"
#include "module_table.h"
#include "../parser/ast.h"
#include "../util/error.h"
#include "../util/arena.h"

/* A constant number folded at full width. An integer is a sign and a
 * 320-bit magnitude: room for every value an integer type holds
 * (-2^255 .. 2^256-1) plus headroom for intermediate results. */
#define LITERAL_LIMBS 10

typedef struct {
    bool is_decimal;
    bool is_too_large;                     /* an integer step reached 2^320, or a
                                         * typed step overflowed its type */
    bool is_negative;
    uint32_t magnitude[LITERAL_LIMBS];  /* little-endian 32-bit limbs */
    double decimal;
} LiteralValue;

/* Type annotation table: maps AST node pointers to resolved types.
 * Uses open-addressing hash table with pointer hashing for O(1) lookup. */
#define TYPE_TABLE_INITIAL_CAPACITY 256

typedef struct {
    AstNode **nodes;    /* hash buckets — NULL = empty slot */
    GrayType **types;     /* parallel type array */
    int count;          /* number of entries */
    int capacity;            /* always a power of 2 */
} TypeTable;

typedef struct {
    /* Struct field info for field type lookup */
    const char *struct_name;   /* flattened lookup key (may be module-prefixed) */
    const char *display_name;  /* user-facing name — for diagnostics, never the prefixed key */
    const char **field_names;
    GrayType **field_types;
    int field_count;
    bool is_deprecated;          /* true if declared with #deprecated attribute */
    const char *deprecated_message; /* NULL if bare #deprecated */
} StructInfo;

/* Sentinels for FunctionSignature.parameter_escape_into[]. Non-negative values are
 * parameter indices. */
#define PARAMETER_ESCAPE_NONE   ((signed char)-1)
#define PARAMETER_ESCAPE_GLOBAL ((signed char)-2)

/* Pointer checker: the lifetime state of one @mem arena variable within the
 * function currently being checked. */
typedef struct {
    const char *name;      /* arena variable name as written */
    int  epoch;            /* incremented by each mem.reset() on this arena */
    bool is_destroyed;        /* mem.destroy() has run on this arena */
    const char *end_file;  /* where the destroy/reset that ended it sits */
    int  end_line;
    bool was_ended_by_reset;    /* true: last lifetime event was reset, not destroy */
    /* An `ensure mem.destroy(a)` was seen: the arena WILL be destroyed once
     * the function returns, no matter what runs between here and then, so a
     * later explicit mem.destroy(a) (or another ensure mem.destroy(a)) is a
     * genuine double-free (P0002 at runtime) — but unlike `destroyed`, this
     * does NOT make an ordinary use of the arena elsewhere in the function
     * an error; the deferred destroy hasn't actually run yet. */
    bool is_ensure_destroy_pending;
    /* Pre-marked destroyed by pointer_checker_premark_loop_body(): some statement later in
     * this loop body's source text destroys the arena, so a dereference
     * appearing earlier in the text is still unsafe on any iteration after
     * the one whose destroy runs. Deliberately kept separate from
     * `destroyed`: it must make an early dereference in the body an error
     * (checked wherever `destroyed` is checked for that purpose), but must
     * NOT make the loop body's own (real, single, textually-later) destroy
     * statement look like a double-free of itself — that statement is the
     * one this flag exists to warn about, not a repeat of it. Only
     * pointer_checker_check_mem_deref consults this; the double-destroy guards
     * (pointer_checker_apply_arena_lifecycle, pointer_checker_apply_ensure_mem_call) deliberately do
     * not, and rely on the loop body's real statements — via the same
     * branch-join machinery used everywhere else — to set `destroyed` for
     * real once actually walked. */
    bool is_premarked_destroyed;
    /* Some statement anywhere in the current function destroys or resets this
     * arena. Consulted only by the escape checks (E3169): a pointer that
     * leaves the function via `return` or a store into caller-visible memory
     * must stay valid for the caller, so it must not root at an arena this
     * function ever tears down — regardless of statement order. Ordinary
     * in-function use is unaffected; that is what the flow-sensitive
     * `destroyed` / epoch state is for. */
    bool is_destroyed_in_function;
} ArenaLifetime;

typedef struct {
    /* Function signature for call type checking */
    const char *name;
    GrayType **parameter_types;
    int parameter_count;
    GrayType **return_types;
    int return_count;
    bool was_used;          /* true if function was called */
    int definition_line;       /* line where function was declared */
    bool is_private;    /* true if declared with 'private' keyword */
    bool is_discard;    /* true if declared with #discard attribute */
    bool is_deprecated;    /* true if declared with #deprecated attribute */
    const char *deprecated_message; /* NULL if bare #deprecated */

    /* Wildcard type support .
     * A function is "generic" if any of its param or return type strings
     * contain a '?'. Generic functions are instantiated per call site:
     * at each call the wildcard is bound to a concrete type derived from
     * the call's arguments, and an entry is appended to `instantiations`.
     * Codegen emits one specialized C function per unique instantiation. */
    bool is_generic;
    AstNode *declaration;                /* source NODE_FUNCTION_DECLARATION for body lookup */

    /* Pointer-escape summary, filled lazily by ensure_escape_summary().
     * escape_state: 0 = not computed, 1 = in progress, 2 = done.
     *
     * returns_parameter_address: bit i set if a return value of this function may be
     * the address of parameter i — returned directly, as addr()/raw() of its
     * pointee, buried in a returned struct field, or forwarded through another
     * summarised call.
     *
     * parameter_escape_into[i]: where an address reaching this function through
     * parameter i ends up living — PARAMETER_ESCAPE_NONE, PARAMETER_ESCAPE_GLOBAL, or
     * the index of the parameter whose container/aggregate receives it (a
     * by-reference container/struct parameter, a module-level variable, a
     * stdlib container insert, or transitively another escaping call).
     *
     * Together these let the return and assignment escape checks (E3162,
     * E3163) follow an address through a function call and through a helper
     * that stashes it in caller-visible memory. */
    unsigned char escape_state;
    unsigned long long returns_parameter_address;
    signed char parameter_escape_into[64];
    /* parameter_escape_global_name[i]: when parameter_escape_into[i] is
     * PARAMETER_ESCAPE_GLOBAL because parameter i's address is stored into a
     * *named* module-level variable, that variable's name, so the E3163 at
     * the call site can name the real destination. NULL when the
     * global-lifetime sink has no single name (a forward through a
     * func-typed parameter or an opaque indirect call). */
    const char *parameter_escape_global_name[64];
    /* parameter_escape_via_function[i] / parameter_escape_via_position[i]: when parameter_escape_into[i]
     * is PARAMETER_ESCAPE_GLOBAL *solely* because parameter i's address is forwarded
     * as argument `parameter_escape_via_position[i]` of an indirect call through this
     * function's own func-typed parameter `parameter_escape_via_function[i]`, that
     * parameter's index (-1 otherwise). A call site that passes a statically
     * known function for that parameter judges the escape by that function's
     * own summary instead of assuming the worst. */
    signed char parameter_escape_via_function[64];
    signed char parameter_escape_via_position[64];
    /* passes_parameter_to_extern: bit i set if parameter i's address may reach
     * an extern.func() call as an argument — directly, or forwarded through
     * another summarised call that itself passes one of ITS parameters into
     * extern. Lets the E3154 stack-address-to-C guard follow an address
     * through a pointer-parameter wrapper function the same way
     * returns_parameter_address lets it follow one through a `return`. */
    unsigned long long passes_parameter_to_extern;
    /* writes_through_parameter: bit i set if this function assigns through pointer
     * parameter i — `p^ = v`, `p^.f = v`, `p.f = v` — directly, through a
     * local copy of the pointer, or forwarded to another summarised call that
     * writes through its own parameter. Lets a call site refuse a pointer to
     * a const-declared variable for a callee that would modify it. */
    unsigned long long writes_through_parameter;
    /* can_return_const_pointer: this function can return a pointer to a
     * module-level const-declared variable (`return addr(DEFAULTS)`), or the
     * result of another function that does. */
    bool can_return_const_pointer;

    /* Pointer checker: cross-function @mem summary, filled lazily by
     * pointer_checker_ensure_mem_summary(). mem_state: 0 = not computed, 1 = in progress,
     * 2 = done.
     *
     * destroys_parameter_arena / resets_parameter_arena: bit i set if some path
     * through this function's body calls mem.destroy() / mem.reset() on the
     * @mem arena named by parameter i — directly, or forwarded through
     * another summarised call (do outer(a Arena) { helper(a) } where helper
     * destroys its own parameter 0). Lets a call site apply the same effect
     * to the caller's arena state that a direct mem.destroy(a)/mem.reset(a)
     * would (E3164/E3165/E3166), closing the "across a function call" gap
     * STANDARD 11.7 documents as unchecked. */
    unsigned char mem_state;
    unsigned long long destroys_parameter_arena;
    unsigned long long resets_parameter_arena;
    /* mem_parameter_field[i]: when destroys_parameter_arena or resets_parameter_arena
     * has bit i set because of a mem.destroy()/mem.reset() reaching the
     * arena through a *field* of parameter i (`mem.destroy(h.a)` where h is
     * parameter i) rather than the parameter itself, the field-path suffix
     * (".a", ".inner.a") to append to whatever path the caller's own
     * argument for parameter i resolves to. NULL when the bit is set by a
     * bare-parameter arena (the parameter itself names the handle). */
    const char *mem_parameter_field[64];

    /* returns_parameter_mem_allocation[i] / returns_parameter_mem_allocation_field[i]: bit i
     * set if some `return` in this function's body yields a @mem pointer
     * allocated from the arena named by parameter i — directly (the return
     * value itself is the pointer: mem.alloc(a,x), a local initialised from
     * one, or forwarded through another summarised call's own _direct bit)
     * or _field (the pointer is buried in a struct/array/map literal the
     * function returns, or forwarded through another call's own _field
     * bit). Lets a call site (pointer_checker_bind_mem_pointer, via
     * pointer_checker_mem_pointer_in_expr) bind the result — as mem_arena or
     * field_mem_arena respectively — to the *caller's* arena variable at
     * that parameter position, the same way returns_parameter_address lets a
     * return value's escape origin follow a pointer parameter through a
     * call. Computed alongside destroys_parameter_arena in the same
     * pointer_checker_mem_walk()/pointer_checker_return_stmt_mem_bits() pass. */
    unsigned long long returns_parameter_mem_allocation;
    unsigned long long returns_parameter_mem_allocation_field;

    const char **instantiations;  /* concrete type each call bound `?` to */
    AstNode **instantiation_calls;/* parallel: originating call-site node */
    int instantiation_count;
    int instantiation_capacity;
} FunctionSignature;

/* One extern.func(...) call or extern.CONST access site, recorded during
 * type checking so main.c can probe the real header through the C compiler
 * (the typechecker has no C header parser) and validate the symbol against
 * it: is_call sites get their argument count checked against the real C
 * signature, and every site (call or constant) gets checked for existence,
 * catching a misspelled C function/constant/macro name. A call whose result
 * is declared or cast to a type also records that type, so the real C return
 * type can be checked against it. */
typedef struct {
    const char *function_name;
    int argument_count;
    bool is_call;
    const char *file;
    int line;
    int column;
    const AstNode *node;       /* the call expression, to attach an assertion */
    GrayType *asserted_type;        /* type the result is declared or cast to, or NULL */
    bool is_asserted_via_cast;    /* cast() converts explicitly; a declaration asserts */
} ExternCallSite;

typedef struct {
    DiagnosticList *diagnostics;
    Scope *current_scope;
    TypeTable *type_table;
    const char *file;

    /* Registered struct types */
    StructInfo *structs;
    int struct_count;
    int struct_capacity;

    /* Registered function signatures */
    FunctionSignature *functions;
    int function_count;
    int function_capacity;

    /* Program AST (for default param lookup) */
    AstNode *program;

    /* Registered enum names */
    const char **enum_names;
    const char **enum_display_names; /* parallel array: user-facing name (never prefixed) */
    bool *is_enum_string; /* parallel array: true if string enum */
    const char ***enum_values; /* parallel array: variant name arrays */
    int *enum_value_counts; /* parallel array: variant counts */
    const char ****enum_payload_types; /* [enum_idx][variant_idx] → type name array */
    int **enum_payload_counts;         /* [enum_idx][variant_idx] → count */
    bool *is_enum_tagged;              /* parallel to enum_names */
    bool *is_enum_flags;               /* parallel to enum_names */
    bool *is_enum_deprecated;          /* parallel to enum_names: #deprecated attribute */
    const char **enum_deprecated_messages; /* parallel to enum_names: NULL if bare #deprecated */
    int enum_count;
    int enum_capacity;

    /* Names of user enums carrying #error_code; their variants join the open
     * builtin ErrorCode enum. "ErrorCode" itself is a normal registered enum. */
    const char **error_code_enum_names;
    int error_code_enum_count;

    /* Control flow tracking */
    int loop_depth;               /* >0 means inside a loop */
    int function_depth;               /* >0 means inside a function body */
    bool is_in_file_scope_initializer;      /* checking a file-scope declaration's initializer */
    GrayType **current_return_types; /* expected return types of current function */
    const char **current_return_type_names; /* raw declared return type names */
    int current_return_count;
    bool has_current_named_returns; /* true if current function uses named return values */
    bool is_current_main_return_suppressed; /*  main() had a declared return
                                          * type, E4008 already fired, we zeroed
                                          * current_return_count so downstream
                                          * "must return" / "return from void"
                                          * checks should also stay quiet */
    int current_function_scope_depth;         /* Scope.depth of the body scope of the
                                           * function being checked, biased by +1
                                           * to match Symbol.origin_depth. A
                                           * pointer origin at or below this depth
                                           * dies when the function returns. */
    bool is_current_function_main;            /* true while typechecking the body of
                                           * main(); used to reject `return` —
                                           * main exits when control reaches
                                           * the closing brace */
    AstNode *current_function_declaration;           /* NODE_FUNCTION_DECLARATION of the function whose
                                           * body is currently being checked, or
                                           * NULL. Compared against FunctionSignature.decl
                                           * (pointer identity, not name — struct
                                           * functions are registered under a
                                           * prefixed lookup key) to exempt a
                                           * #deprecated function's own recursive
                                           * calls from warning on itself. */
    const char **current_return_names; /* named return variable names (NULL entries for unnamed) */

    /* Pass 3 / slice 4: when true, resolve_expr must not write
     * into the type table. Re-checking a generic function body with
     * concretely-bound parameters would otherwise clobber the main
     * pass's type_table entries for the shared body AST and break
     * codegen for other instantiations. */
    bool should_suppress_type_table_writes;

    /* Import tracking for unused import warnings */
    const char **imported_modules;
    const char **import_files;   /* source file each import came from (NULL = main) */
    int *import_lines;
    bool *is_import_used;
    bool *is_import_stdlib;
    int import_count;
    int import_capacity;

    /* Modules brought into scope via 'using' or 'import and use' */
    const char **using_modules;
    const char **using_module_files; /* parallel: source file each using came from (NULL = main) */
    int *using_module_import_indices; /* parallel: index into imported_modules[], or -1 if not found */
    int using_module_count;
    int using_module_capacity;

    /* File currently being validated — used to filter using_modules per-file */
    const char *current_check_file;

    /* The using'd modules visible from current_check_file, in declared order:
     * the search order for an unqualified name. Rebuilt when the file or the
     * using list changes, so name resolution does not re-filter per lookup. */
    const char **using_visible;
    int using_visible_count;
    int using_visible_capacity;
    const char *using_visible_file;
    int using_visible_stamp;

    /* The module owning current_check_file, resolved through the file->module
     * hash. The mapping is fixed before checking begins, so cache it and
     * recompute only when the file under check changes. */
    const char *scope_module_cache;
    const char *scope_module_file;
    bool is_scope_module_valid;

    /* struct-decl name -> the first matching NODE_STRUCT_DECLARATION in the program.
     * The top-level statement list is fixed once checking begins, so this is
     * built once on first use and replaces a linear AST scan. */
    const char **struct_declaration_index_names;
    AstNode **struct_declaration_index_nodes;
    int struct_declaration_index_capacity;
    bool is_struct_declaration_index_built;

    /* Set of top-level NODE_VARIABLE_DECLARATION names in the program. Same rationale as
     * the struct-decl index: the statement list is fixed once checking begins,
     * so this is built once on first use and replaces a per-assignment AST
     * scan in is_module_level_var(). */
    const char **module_variable_index_names;
    int module_variable_index_capacity;
    bool is_module_variable_index_built;

    /* Cache for typechecker_type_from_name (spelling -> resolved type). The
     * module/alias/struct/enum tables it consults are frozen once statement
     * checking begins, so within one file and using-list the answer for a
     * spelling never changes. `active` gates it to that window; the cache is
     * flushed when `file` or `using_count` no longer match the checker. Keys
     * are arena copies of the spelling. */
    const char **type_name_cache_names;
    GrayType **type_name_cache_types;
    int type_name_cache_count;
    int type_name_cache_capacity;
    const char *type_name_cache_file;
    int type_name_cache_using_count;
    bool is_type_name_cache_active;

    /* Pointer checker: per-function @mem arena lifetime state. Tracks, per
     * arena variable in scope, whether mem.destroy() has run and how many
     * times mem.reset() has (the epoch). Flow-sensitive: saved and joined
     * around branches so a destroy on one path is seen after the join.
     * Drives E3164 (use after destroy), E3165 (use after reset), E3166
     * (destroy/reset of an already-destroyed arena). */
    ArenaLifetime *arenas;
    int arena_count;
    int arena_capacity;
    /* True while a structural cross-function @mem summary walk is in progress.
     * That walk runs detached from the summarised function's scope, so arena
     * path keys must be taken literally (a parameter's own name) rather than
     * resolved through checker->current_scope, which belongs to whichever
     * caller triggered the lazy summary. */
    bool is_pointer_checker_in_mem_summary;

    /*  true during register_declarations to allow forward references */
    bool is_registering;

    /* Name of the struct whose function body is currently being checked.
     * NULL when outside a struct function body. Used for private access. */
    const char *current_struct_name;

    /* Expected type for resolving implicit enum selectors (.VARIANT).
     * Set before resolving expressions where the target enum type is
     * known (assignments, function args, when/is, comparisons, returns).
     * Cleared after use to prevent stale context. */
    GrayType *expected_type;

    /* Number literal expressions a context read without giving them a type.
     * Each still untyped when its statement finishes takes its default type
     * (i64, or f64 for a decimal literal) then. */
    AstNode **pending_literals;
    int pending_literal_count;
    int pending_literal_capacity;

    /* Type-level generic parameters (<?> syntax).
     * type_parameter_name is the parameter name (e.g. "T") during body check.
     * type_parameter_binding is the concrete struct name during re-check. */
    const char *type_parameter_name;
    const char *type_parameter_binding;

    /* Arena for diagnostic message strings — replaces per-message strdup */
    Arena *arena;

    /* Per-module symbol table: every top-level declaration keyed by
     * (module, name-as-written). Populated by register_declarations. */
    ModuleTable *modules;

    /* Type alias registry (alias Name = Type) */
    const char **type_alias_names;
    const char **type_alias_targets;
    AstNode **type_alias_nodes;
    int type_alias_count;
    int type_alias_capacity;

    /* Evaluated values of file-scope const integer declarations.
     * Used to constant-fold expressions in subsequent const initializers
     * and to detect overflow before codegen runs. */
    const char **const_integer_names;
    LiteralValue *const_integer_values;
    int const_integer_count;
    int const_integer_capacity;

    /* --test mode: building a test runner, so main() is not required and
     * #test functions are not flagged as unused. */
    bool is_test_mode;

    /* Set when a non-function declaration tried to claim the name `main`
     * (E4026). Suppresses the follow-on "program has no main() function"
     * (E4005), which would otherwise fire because the bad declaration
     * shadowed the real entry point in the symbol table. */
    bool was_main_name_misused;

    /* extern.func(...) call sites seen during type checking. The typechecker
     * cannot see the real C signature (no header parser lives here), so it
     * only records what was written; main.c probes the actual imported
     * header through the C compiler after type checking succeeds and
     * validates argument counts against these recorded sites. */
    ExternCallSite *extern_calls;
    int extern_call_count;
    int extern_call_capacity;

} TypeChecker;

/* Create and run the type checker */
TypeChecker *typechecker_create(DiagnosticList *diagnostics, const char *file);

/* Enable --test mode: main() is not required and #test functions are exempt
 * from the unused-function warning. */
void typechecker_set_test_mode(TypeChecker *checker, bool enabled);

/* Record which module a source file belongs to. The import driver calls this
 * for the entry file and every file it pulls in, before typechecker_check. */
void typechecker_add_file_module(TypeChecker *checker, const char *file,
                                 const char *module_name, bool is_entry);

/* Record that `alias` names `module_name`. Used for import aliases and for
 * sibling files of a directory module, which are named as if they were
 * modules but resolve to the directory's module. */
void typechecker_add_module_alias(TypeChecker *checker, const char *alias,
                                  const char *module_name);
void typechecker_check(TypeChecker *checker, AstNode *program);
void typechecker_free(TypeChecker *checker);

/* Query the type table (used by codegen) */
GrayType *type_table_get(TypeTable *table, AstNode *node);

/* Get the type table from the checker */
TypeTable *typechecker_get_table(TypeChecker *checker);

/* extern.func(...) call sites recorded during type checking, for
 * post-typecheck signature validation against the real imported header. */
const ExternCallSite *typechecker_get_extern_calls(TypeChecker *checker, int *count);

/* Get the module symbol table from the checker (used by codegen) */
ModuleTable *typechecker_get_modules(TypeChecker *checker);

/* Check if (module_name, function_name) is a known stdlib function.
 * Used by codegen for unqualified 'using' dispatch. */
bool stdlib_has_function(const char *module_name, const char *function_name);

#endif
