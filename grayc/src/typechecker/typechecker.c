/*
 * typechecker.c — Walks the AST to resolve expression types, enforce type
 * correctness, and build a type table that codegen can query at emission time.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 *
 * Contributors:
 *  - @mvanhorn
 */

#include "typechecker.h"
#include "../util/constants.h"
#include "../util/platform.h"
#include "../util/reserved.h"
#include "../util/xalloc.h"
#include "../util/error_code_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>

#define MAX_STRUCT_DEPTH 32

/* True when `path` is `dir` itself or sits underneath it. Both arguments must
 * already be canonicalized. Used to keep embed() from reaching outside the
 * source tree. */
static bool path_within_dir(const char *path, const char *dir) {
    size_t dir_len = strlen(dir);
    if (strlen(path) < dir_len) return false;
    if (path[dir_len] != '\0' && !gray_is_path_sep(path[dir_len])) return false;
    char prefix[4096];
    if (dir_len >= sizeof(prefix)) return false;
    memcpy(prefix, path, dir_len);
    prefix[dir_len] = '\0';
    return gray_path_equal(prefix, dir);
}

/* Helper: get the source file from an AST node's token, falling back to checker->file.
 * Imported nodes carry their original file path in token.file; main-file nodes have NULL. */
#define NODE_FILE(checker, n) ((n)->token.file ? (n)->token.file : (checker)->file)

/* Check if using_modules[using_index] is accessible from the current file being checked.
 * A using-module entry is accessible only if it was declared in the same file
 * that is currently being validated (prevents transitive import type leaking). */
static inline bool using_module_accessible(TypeChecker *checker, int using_index) {
    const char *using_file = checker->using_module_files ? checker->using_module_files[using_index] : NULL;
    const char *check_file = checker->current_check_file;
    /* Both NULL — both from main file context */
    if (!using_file && !check_file) return true;
    /* Both set — compare paths */
    if (using_file && check_file && strcmp(using_file, check_file) == 0) return true;
    return false;
}

/* Mark an imported module as used by name. Returns true if found. */
static bool mark_import_used(TypeChecker *checker, const char *mod_name) {
    for (int mi = 0; mi < checker->import_count; mi++) {
        if (strcmp(checker->imported_modules[mi], mod_name) == 0) {
            checker->import_used[mi] = true;
            return true;
        }
    }
    return false;
}

/* Helper: get the user-facing display name for a declaration.
 * Import merging prefixes names (e.g. foo → mod_foo) but errors should
 * show the original name the user wrote. */
#define FUNC_DISPLAY_NAME(n) ((n)->data.func_decl.original_name ? (n)->data.func_decl.original_name : (n)->data.func_decl.name)
#define VAR_DISPLAY_NAME(n)  ((n)->data.var_decl.original_name  ? (n)->data.var_decl.original_name  : (n)->data.var_decl.name)
#define STRUCT_DISPLAY_NAME(n) ((n)->data.struct_decl.original_name ? (n)->data.struct_decl.original_name : (n)->data.struct_decl.name)
#define ENUM_DISPLAY_NAME(n) ((n)->data.enum_decl.original_name ? (n)->data.enum_decl.original_name : (n)->data.enum_decl.name)

/* --- Type Table (open-addressing hash, pointer keys) --- */

static uint32_t hash_ptr(const void *ptr) {
    uintptr_t hash_value = (uintptr_t)ptr;
    /* Fibonacci hashing; good distribution for pointer alignment */
    hash_value = ((hash_value >> 4) ^ hash_value) * 0x9E3779B9U;
    return (uint32_t)(hash_value ^ (hash_value >> 16));
}

static TypeTable *typetable_create(void) {
    TypeTable *table = xcalloc(1, sizeof(TypeTable));
    table->cap = TYPETABLE_INIT_CAP;
    table->nodes = xcalloc((size_t)table->cap, sizeof(AstNode *));
    table->types = xcalloc((size_t)table->cap, sizeof(GrayType *));
    return table;
}

static void typetable_set(TypeTable *table, AstNode *node, GrayType *type);

static void typetable_grow(TypeTable *table) {
    int old_cap = table->cap;
    AstNode **old_nodes = table->nodes;
    GrayType **old_types = table->types;

    table->cap = old_cap * 2;
    table->nodes = xcalloc((size_t)table->cap, sizeof(AstNode *));
    table->types = xcalloc((size_t)table->cap, sizeof(GrayType *));
    table->count = 0;

    for (int i = 0; i < old_cap; i++) {
        if (old_nodes[i]) {
            typetable_set(table, old_nodes[i], old_types[i]);
        }
    }
    free(old_nodes);
    free(old_types);
}

static void typetable_set(TypeTable *table, AstNode *node, GrayType *type) {
    /* Grow at 70% load factor */
    if (table->count * 10 >= table->cap * 7) {
        typetable_grow(table);
    }

    uint32_t mask = (uint32_t)(table->cap - 1);
    uint32_t idx = hash_ptr(node) & mask;

    for (;;) {
        if (!table->nodes[idx]) {
            /* Empty slot; insert */
            table->nodes[idx] = node;
            table->types[idx] = type;
            table->count++;
            return;
        }
        if (table->nodes[idx] == node) {
            /* Already present; update */
            table->types[idx] = type;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

GrayType *typetable_get(TypeTable *table, AstNode *node) {
    if (!table || !table->nodes) return NULL;

    uint32_t mask = (uint32_t)(table->cap - 1);
    uint32_t idx = hash_ptr(node) & mask;

    for (;;) {
        if (!table->nodes[idx]) return NULL;
        if (table->nodes[idx] == node) return table->types[idx];
        idx = (idx + 1) & mask;
    }
}

/* Shared helpers for sorted-string-set lookups. */
static int string_pointer_compare(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static bool string_set_contains(const char *const *sorted, int n, const char *name) {
    return bsearch(&name, sorted, (size_t)n, sizeof(const char *), string_pointer_compare) != NULL;
}


/* Forward declaration — resolve_expression is defined later but needed by helper
 * routines and stdlib arg-type validation. */
static GrayType *resolve_expression(TypeChecker *checker, AstNode *node);

/* Forward declarations — pointer checker @mem lifetime helpers, defined near
 * check_expr_stmt but hooked into expression resolution and var-decl. */
static void pc_apply_mem_call(TypeChecker *checker, AstNode *call, AstNode *at,
                              const char *bind_name);
static void pc_bind_mem_pointer(TypeChecker *checker, Symbol *sym, AstNode *value);
static void pc_check_mem_deref(TypeChecker *checker, AstNode *ptr_expr, AstNode *at);
static bool pc_mem_pointer_in_expr(TypeChecker *checker, AstNode *value,
                                   const char **out_arena, int *out_epoch,
                                   bool *out_via_field);
static bool pc_is_mem_call(TypeChecker *checker, AstNode *call,
                           const char **out_fn, const char **out_arena);
static void pc_apply_arena_lifecycle(TypeChecker *checker, const char *arena_name,
                                     bool is_destroy, AstNode *at, const char *disp);
static const char *pc_arena_path_key(TypeChecker *checker, AstNode *expr);

/* Return the user-facing display string for an operator TokenType.
 * Used in error messages that embed the operator name. */
static const char *operator_display_name(TokenType op) {
    switch (op) {
    case TOK_PLUS: return "+";
    case TOK_MINUS: return "-";
    case TOK_ASTERISK: return "*";
    case TOK_SLASH: return "/";
    case TOK_PERCENT: return "%";
    case TOK_EQ: return "==";
    case TOK_NOT_EQ: return "!=";
    case TOK_LT: return "<";
    case TOK_GT: return ">";
    case TOK_LT_EQ: return "<=";
    case TOK_GT_EQ: return ">=";
    case TOK_AND: return "&&";
    case TOK_OR: return "||";
    case TOK_BANG: return "!";
    case TOK_BIT_AND: return "bit_and";
    case TOK_BIT_OR: return "bit_or";
    case TOK_BIT_XOR: return "bit_xor";
    case TOK_BIT_NOT: return "bit_not";
    case TOK_BIT_SHIFT_LEFT: return "bit_shift_left";
    case TOK_BIT_SHIFT_RIGHT: return "bit_shift_right";
    case TOK_INCREMENT: return "++";
    case TOK_DECREMENT: return "--";
    case TOK_CARET: return "^";
    case TOK_ASSIGN: return "=";
    case TOK_PLUS_ASSIGN: return "+=";
    case TOK_MINUS_ASSIGN: return "-=";
    case TOK_ASTERISK_ASSIGN: return "*=";
    case TOK_SLASH_ASSIGN: return "/=";
    case TOK_PERCENT_ASSIGN: return "%=";
    case TOK_IN: return "in";
    case TOK_NOT_IN: return "not_in";
    default: return "?";
    }
}

/* Forward declarations for type-name checks used by is_assignment_target */
static bool is_struct_name(TypeChecker *checker, const char *name);
static bool is_enum_name(TypeChecker *checker, const char *name);
static const char *checker_resolve_decl_into(TypeChecker *checker, const char *written,
                                             char *buf, size_t buflen);
static void checker_refresh_using(TypeChecker *checker);
static ResolveScope checker_scope(TypeChecker *checker);
static bool type_arg_names_a_type(TypeChecker *checker, const char *name);
static DeclEntry *checker_resolve_entry(TypeChecker *checker, const char *written);
static GrayType *resolve_func_ref(TypeChecker *checker, AstNode *node);
static bool ref_names_function(TypeChecker *checker, AstNode *arg);

/* True if the expression is an assignment target (something with a stable
 * address): a variable, a field of an assignment target, an index into an
 * assignment target, or a pointer dereference. Used by addr(), raw(), and
 * ref() to reject literals, call results, arithmetic expressions, and type
 * names — none of which have an address to take. */
static bool is_assignment_target(TypeChecker *checker, AstNode *e) {
    if (!e) return false;
    switch (e->kind) {
    case NODE_LABEL: {
        const char *name = e->data.label.value;
        if (is_builtin_type_name(name)) return false;
        if (is_struct_name(checker, name)) return false;
        if (is_enum_name(checker, name)) return false;
        return true;
    }
    case NODE_MEMBER_EXPR:  return is_assignment_target(checker, e->data.member.object);
    case NODE_INDEX_EXPR:   return is_assignment_target(checker, e->data.index_expr.left);
    case NODE_POSTFIX_EXPR:
        /* p^ (dereference) is an assignment target */
        return e->data.postfix.op == TOK_CARET;
    default:                return false;
    }
}

/* Walk an assignment target expression chain and return the root variable name.
 * Returns NULL if the chain passes through a pointer dereference (^),
 * because the pointed-to memory is independent of the pointer variable's
 * mutability. */
static const char *assignment_target_root_name(AstNode *e) {
    if (!e) return NULL;
    switch (e->kind) {
    case NODE_LABEL:       return e->data.label.value;
    case NODE_MEMBER_EXPR: return assignment_target_root_name(e->data.member.object);
    case NODE_INDEX_EXPR:  return assignment_target_root_name(e->data.index_expr.left);
    case NODE_POSTFIX_EXPR:
        if (e->data.postfix.op == TOK_CARET)
            return NULL; /* pointer deref: memory is not the const variable */
        return NULL;
    default: return NULL;
    }
}

/* Depth of the scope declaring `name`, biased by +1 so 0 means "not found".
 * Deeper means shorter-lived: a pointer may only hold an address whose
 * origin depth is at most the pointer's own. */
static int symbol_scope_depth(Scope *scope, const char *name) {
    for (Scope *cur = scope; cur; cur = cur->parent)
        if (scope_lookup_local(cur, name)) return cur->depth + 1;
    return 0;
}

/* Lifetime origin of a call result whose value is forwarded from one of the
 * call's arguments (per the callee's returns_param_addr summary). Defined
 * below register_func; forward-declared here for pointer_origin_of. */
static int call_result_origin(TypeChecker *checker, AstNode *call,
                              const char **out_name);

/* Deepest origin among a call's own arguments — the conservative fallback
 * for a call this checker cannot analyze precisely: a stdlib module
 * function or bare builtin, neither of which has a FuncSig or a computed
 * escape summary the way a user function does. Only meaningful when the
 * caller has already confirmed there is no such summary to fall back on
 * instead. Defined below expression_origin; forward-declared here for
 * pointer_origin_of and container_literal_origin. */
static int stdlib_call_arg_origin(TypeChecker *checker, AstNode *call,
                                  const char **out_name);

/* The user-defined function `call` targets, or NULL for anything this
 * checker keeps no FuncSig for (a stdlib module call, a bare builtin).
 * Defined further down; forward-declared here for pointer_origin_of. */
static FuncSig *resolve_call_sig(TypeChecker *checker, AstNode *call);

/* If `value` produces a pointer with a known lifetime origin, report that
 * origin's scope depth and name. Covers the direct form addr(x)/raw(x), a
 * pointer variable that already carries an origin, and a call result that
 * forwards one of its pointer arguments — so an address that is laundered
 * through any number of intermediates or through a function call stays
 * tracked. */
static int pointer_origin_of(TypeChecker *checker, AstNode *value,
                             const char **out_name) {
    if (!value) return 0;
    if (value->kind == NODE_CALL_EXPR &&
        value->data.call.function->kind == NODE_LABEL &&
        value->data.call.arg_count == 1 &&
        (strcmp(value->data.call.function->data.label.value, "addr") == 0 ||
         strcmp(value->data.call.function->data.label.value, "raw") == 0 ||
         strcmp(value->data.call.function->data.label.value, "ref") == 0)) {
        const char *root = assignment_target_root_name(value->data.call.args[0]);
        if (!root) return 0;
        int depth = symbol_scope_depth(checker->current_scope, root);
        if (depth) *out_name = root;
        return depth;
    }
    if (value->kind == NODE_LABEL) {
        Symbol *src = scope_lookup(checker->current_scope, value->data.label.value);
        if (src && src->origin_depth) {
            *out_name = src->origin_name;
            return src->origin_depth;
        }
        /* A bare struct/array/map variable, taken as a whole (not indexed
         * into): its own buried origin, from when it was declared or last
         * assigned a literal/copy() holding addr(local). Unlike the indexed
         * read below, no TK_POINTER guard is needed — the origin belongs to
         * something inside the aggregate, not to `value` itself. */
        if (src && src->field_origin_depth) {
            *out_name = src->field_origin_name;
            return src->field_origin_depth;
        }
    }
    /* An element/field read *out of* a container that was built from a
     * literal holding addr(local) — `a[0]`, `b.p`. The container's buried
     * origin travels with the value that comes back out, but only when the
     * slot is itself a pointer; a copied scalar field is safe. */
    if (value->kind == NODE_INDEX_EXPR || value->kind == NODE_MEMBER_EXPR) {
        const char *root = assignment_target_root_name(value);
        if (root) {
            Symbol *s = scope_lookup(checker->current_scope, root);
            if (s && s->field_origin_depth) {
                GrayType *vt = resolve_expression(checker, value);
                if (vt && vt->kind == TK_POINTER) {
                    *out_name = s->field_origin_name;
                    return s->field_origin_depth;
                }
            }
        }
    }
    if (value->kind == NODE_CALL_EXPR) {
        int r = call_result_origin(checker, value, out_name);
        /* call_result_origin comes back 0 both when a resolved user
         * function's summary says nothing escapes, and when there is no
         * FuncSig to summarise at all (a stdlib module call, a bare
         * builtin). Only the latter falls back to the conservative
         * arguments-based guess — resolve_call_sig() tells them apart. */
        if (!r && !resolve_call_sig(checker, value))
            r = stdlib_call_arg_origin(checker, value, out_name);
        return r;
    }
    return 0;
}

/* True if the access path contains a map index, e.g. ref(m["k"]) or
 * ref(m["k"].field) or ref(rows[i].cells["k"]). Map values relocate on
 * rehash so a pointer to one is unsafe. */
static bool path_contains_map_index(TypeChecker *checker, AstNode *e) {
    if (!e) return false;
    switch (e->kind) {
    case NODE_MEMBER_EXPR:
        return path_contains_map_index(checker, e->data.member.object);
    case NODE_INDEX_EXPR: {
        GrayType *left_t = resolve_expression(checker, e->data.index_expr.left);
        if (left_t && left_t->kind == TK_MAP) return true;
        return path_contains_map_index(checker, e->data.index_expr.left);
    }
    case NODE_POSTFIX_EXPR:
        return path_contains_map_index(checker, e->data.postfix.left);
    default:
        return false;
    }
}

/* --- Struct info helpers --- */

/* Is this type name already taken by a different declaration? The registries
 * fill one kind at a time, so a struct/enum collision is visible in the
 * symbol table before either registry can see both halves of it. */
static bool type_name_already_declared(TypeChecker *checker, const char *name,
                                       const AstNode *stmt) {
    DeclEntry *entry = checker_resolve_entry(checker, name);
    return entry && entry->ast_node && entry->ast_node != stmt &&
           (entry->kind == DECL_STRUCT || entry->kind == DECL_ENUM ||
            entry->kind == DECL_ALIAS);
}

/* Anything registered without a source declaration of its own — a compiler
 * provided type, a stdlib opaque type — still needs an entry, so that every
 * lookup goes through the symbol table and none through a name array. */
static void adopt_registration(TypeChecker *checker, DeclKind kind,
                               const char *name, int index) {
    if (!checker->modules) return;
    DeclEntry *existing = module_table_find_mangled(checker->modules, name);
    if (existing) {
        /* A stdlib opaque type is declared by its module before its details
         * are registered here. Point the existing entry at them rather than
         * leaving it detail-less and shadowing this registration. */
        if (existing->kind == kind && existing->registry_index < 0)
            existing->registry_index = index;
        return;
    }
    DeclEntry *entry = module_table_declare_synthetic(checker->modules, NULL, kind,
                                                      name, checker->file);
    if (entry) entry->registry_index = index;
}

/* The unprefixed alias `using` publishes for a declaration stands for that
 * declaration, so it has to carry its visibility. Left public, an alias for a
 * private struct or enum was reachable by a bare name from a file that cannot
 * name it at all — which is how `private` stopped meaning anything on a type
 * once its file was used. */
static void inherit_decl_visibility(TypeChecker *checker, const char *from, const char *to) {
    if (!checker->modules) return;
    DeclEntry *src = module_table_find_mangled(checker->modules, from);
    DeclEntry *dst = module_table_find_mangled(checker->modules, to);
    if (!src || !dst || src == dst) return;
    dst->visibility = src->visibility;
    dst->origin_file = src->origin_file;
}

static void register_struct(TypeChecker *checker, const char *name,
    const char *display_name,
    const char **field_names, GrayType **field_types, int field_count) {
    GROW_ARRAY(checker->structs, checker->struct_count, checker->struct_cap);
    adopt_registration(checker, DECL_STRUCT, name, checker->struct_count);
    StructInfo *si = &checker->structs[checker->struct_count++];
    si->struct_name = name;
    si->display_name = display_name ? display_name : name;
    si->field_names = field_names;
    si->field_types = field_types;
    si->field_count = field_count;
    si->is_deprecated = false;
    si->deprecated_message = NULL;
}


/* The lookup helpers below take a name as written — a bare `Foo` inside its
 * own module, a `using`'d `Foo`, or a qualified `lib.Foo` — and resolve it to
 * the registry spelling before looking it up. Doing it here rather than at
 * each call site is what lets a reference stay as written all the way from
 * the parser. Resolution is idempotent: a name that names no declaration,
 * an already-mangled key included, comes back unchanged. */
static StructInfo *find_struct(TypeChecker *checker, const char *name) {
    DeclEntry *entry = checker_resolve_entry(checker, name);
    if (!entry) entry = module_table_find_mangled(checker->modules, name);
    if (entry && entry->kind == DECL_STRUCT && entry->registry_index >= 0 &&
        entry->registry_index < checker->struct_count)
        return &checker->structs[entry->registry_index];
    return NULL;
}

static bool is_struct_name(TypeChecker *checker, const char *name) {
    return find_struct(checker, name) != NULL;
}



/* Returns the original index of the named enum via O(log n) bsearch, or -1. */
static int find_enum_index(TypeChecker *checker, const char *name) {
    DeclEntry *entry = checker_resolve_entry(checker, name);
    if (!entry) entry = module_table_find_mangled(checker->modules, name);
    if (entry && entry->kind == DECL_ENUM && entry->registry_index >= 0 &&
        entry->registry_index < checker->enum_count)
        return entry->registry_index;
    return -1;
}

static bool is_enum_name(TypeChecker *checker, const char *name) {
    return find_enum_index(checker, name) >= 0;
}

/* Best-effort unqualified form of a type name that has no registry entry
 * to recover a proper display name from — e.g. an undefined type, where
 * read_type_name() mangled a written "mod.Type" into "mod_Type" and
 * there's no struct/enum to look the original spelling up on. Mirrors the
 * module-prefix heuristic already used to resolve module-prefixed lookups
 * in typechecker_type_from_name(). */
static const char *unqualified_display_name(const char *name) {
    if (!name) return name;
    /* As written: mod.Type. */
    const char *dot = strchr(name, '.');
    if (dot && dot[1] >= 'A' && dot[1] <= 'Z') return dot + 1;
    /* As mangled: mod_Type. Split at the last '_' so a module name that
     * contains one keeps its whole prefix. */
    const char *us = strrchr(name, '_');
    if (us && us[1] >= 'A' && us[1] <= 'Z') return us + 1;
    return name;
}

/* The name the programmer wrote for a struct, never the module-prefixed
 * lookup key. Diagnostics and namespace-collision checks must use this. */
static const char *struct_display_name(TypeChecker *checker, const char *name) {
    StructInfo *si = find_struct(checker, name);
    return si ? si->display_name : unqualified_display_name(name);
}

/* As struct_display_name, for enums. */
static const char *enum_display_name(TypeChecker *checker, const char *name) {
    int i = find_enum_index(checker, name);
    if (i < 0) return unqualified_display_name(name);
    return checker->enum_display_names[i] ? checker->enum_display_names[i] : checker->enum_names[i];
}

/* Type identity is the resolved declaration, not the spelling of the
 * reference. Both names arrive already mapped onto their registry spelling,
 * so lib_Foo and objects_Foo are the different types they are — comparing
 * display names made every module's `Foo` the same `Foo`, and let one
 * module's struct be passed where another's was expected. */
static bool typechecker_same_struct_type(TypeChecker *checker, const char *a, const char *b) {
    (void)checker;
    return a == b || strcmp(a, b) == 0;
}
static bool typechecker_enum_is_error_code(TypeChecker *checker, const char *name);
static bool typechecker_same_enum_type(TypeChecker *checker, const char *a, const char *b) {
    if (a == b || strcmp(a, b) == 0) return true;
    /* A #error_code enum value is interchangeable with an ErrorCode value, but
     * only with ErrorCode — each concrete #error_code enum stays its own type,
     * so NetErr and DbErr do not unify with each other (STANDARD 10.5). */
    if (!typechecker_enum_is_error_code(checker, a) ||
        !typechecker_enum_is_error_code(checker, b)) return false;
    return strcmp(a, "ErrorCode") == 0 || strcmp(b, "ErrorCode") == 0;
}
/* Compare array element type names accounting for module-prefixed struct
 * aliases (e.g. "Item" vs "utils_Item" should match). */
static bool typechecker_same_array_element(TypeChecker *checker, const char *a, const char *b) {
    if (strcmp(a, b) == 0) return true;
    GrayType *at = type_from_name(a);
    GrayType *bt = type_from_name(b);
    if (at && bt && at->kind == TK_STRUCT && bt->kind == TK_STRUCT)
        return typechecker_same_struct_type(checker, a, b);
    if (at && bt && at->kind == TK_ENUM && bt->kind == TK_ENUM)
        return typechecker_same_enum_type(checker, a, b);
    return false;
}

/* Returns true if the named enum is string-backed. */
static bool typechecker_enum_is_string(TypeChecker *checker, const char *name) {
    int i = find_enum_index(checker, name);
    return i >= 0 && checker->enum_is_string[i];
}

/* Returns true if the named enum is a tagged enum (has payload variants). */
static bool typechecker_enum_is_tagged(TypeChecker *checker, const char *name) {
    int i = find_enum_index(checker, name);
    return i >= 0 && checker->enum_is_tagged[i];
}

static bool typechecker_enum_is_flags(TypeChecker *checker, const char *name) {
    int i = find_enum_index(checker, name);
    return i >= 0 && checker->enum_is_flags[i];
}

/* True for the builtin open enum "ErrorCode" and for any user enum carrying
 * #error_code. Values of these enums share one variant/value space. */
static bool typechecker_enum_is_error_code(TypeChecker *checker, const char *name) {
    if (!name) return false;
    if (strcmp(name, "ErrorCode") == 0) return true;
    for (int i = 0; i < checker->error_code_enum_count; i++) {
        if (strcmp(checker->error_code_enum_names[i], name) == 0) return true;
    }
    return false;
}

/* How many declared types go by this user-facing name. Two means a message
 * printing the bare name cannot tell them apart. */
static int display_name_count(TypeChecker *checker, const char *display) {
    int count = 0;
    for (int i = 0; i < checker->struct_count; i++) {
        const char *d = checker->structs[i].display_name
            ? checker->structs[i].display_name : checker->structs[i].struct_name;
        if (d && strcmp(d, display) == 0) count++;
    }
    for (int i = 0; i < checker->enum_count; i++) {
        const char *d = checker->enum_display_names[i]
            ? checker->enum_display_names[i] : checker->enum_names[i];
        if (d && strcmp(d, display) == 0) count++;
    }
    return count;
}

/* The name a diagnostic gives a declared type: as written, qualified by its
 * module when another declaration goes by the same name. Two modules' `Color`
 * are different types, and "cannot assign Color to Color" tells the reader
 * nothing about which is which. `key` is the registry spelling, `display` the
 * name looked up from it — equal pointers mean no declaration was found. */
static const char *qualify_ambiguous_name(TypeChecker *checker,
                                          const char *key, const char *display) {
    if (display == key || display_name_count(checker, display) < 2) return display;
    DeclEntry *entry = checker_resolve_entry(checker, key);
    if (!entry) entry = module_table_find_mangled(checker->modules, key);
    if (!entry || !entry->module_name || !entry->module_name[0]) return display;
    static char qual_bufs[4][TYPE_NAME_MAX];
    static int qual_slot = 0;
    char *out = qual_bufs[qual_slot];
    qual_slot = (qual_slot + 1) & 3;
    snprintf(out, sizeof(qual_bufs[0]), "%s.%s", entry->module_name, display);
    return out;
}

/* A type name fit to print in a diagnostic — for struct/enum types this
 * is the user-facing name, never the module-prefixed lookup key. Composite
 * types (pointers, arrays, maps) recurse into their inner types so that
 * e.g. ^myutils_Thing displays as ^Thing. */
static const char *type_display_name(TypeChecker *checker, GrayType *t) {
    if (!t) return type_name(t);
    if (t->kind == TK_STRUCT && t->name)
        return qualify_ambiguous_name(checker, t->name, struct_display_name(checker, t->name));
    if (t->kind == TK_ENUM && t->name)
        return qualify_ambiguous_name(checker, t->name, enum_display_name(checker, t->name));
    if (t->kind == TK_POINTER && t->name) {
        const char *inner = struct_display_name(checker, t->name);
        if (inner == t->name) inner = enum_display_name(checker, t->name);
        if (inner != t->name) {
            static char ptr_bufs[4][TYPE_NAME_MAX];
            static int ptr_slot = 0;
            char *out = ptr_bufs[ptr_slot];
            ptr_slot = (ptr_slot + 1) & 3;
            snprintf(out, sizeof(ptr_bufs[0]), "^%s",
                qualify_ambiguous_name(checker, t->name, inner));
            return out;
        }
    }
    if (t->kind == TK_ARRAY && t->element_type) {
        const char *inner = struct_display_name(checker, t->element_type);
        if (inner == t->element_type) inner = enum_display_name(checker, t->element_type);
        if (inner != t->element_type) {
            static char arr_bufs[4][TYPE_NAME_MAX];
            static int arr_slot = 0;
            char *out = arr_bufs[arr_slot];
            arr_slot = (arr_slot + 1) & 3;
            snprintf(out, sizeof(arr_bufs[0]), "[%s]",
                qualify_ambiguous_name(checker, t->element_type, inner));
            return out;
        }
    }
    return type_name(t);
}

/* Resolve an import alias to the actual module name */
static const char *typechecker_resolve_alias(TypeChecker *checker, const char *name) {
    return module_table_resolve_alias(checker->modules, name);
}

/* Position of the first `sep` in `s` that sits at bracket/paren nesting
 * depth 0, or NULL if there is none. Used to split composite type strings
 * (`map[K:V]`, `func(P,...)->R`, `[Elem,N]`) without tripping on separators
 * inside nested composites. */
static char *alias_top_level_sep(char *s, char sep) {
    int depth = 0;
    for (char *p = s; *p; p++) {
        if (*p == '[' || *p == '(') depth++;
        else if (*p == ']' || *p == ')') depth--;
        else if (*p == sep && depth == 0) return p;
    }
    return NULL;
}

static const char *resolve_type_alias(TypeChecker *checker, const char *name);

/* Resolve every top-level comma-separated type name in `list` (used as a
 * mutable scratch buffer), writing the rebuilt list into `out`. A leading
 * `&` on a segment (a mutable func parameter) is preserved. Returns 1 when
 * at least one element resolved to a different name, 0 when nothing
 * changed, -1 on buffer overflow (`out` unusable). */
static int alias_resolve_type_list(TypeChecker *checker, char *list,
                                   char *out, size_t out_sz) {
    size_t pos = 0;
    bool changed = false;
    char *seg = list;
    out[0] = '\0';
    while (seg && *seg) {
        char *comma = alias_top_level_sep(seg, ',');
        if (comma) *comma = '\0';
        const char *amp = "";
        char *tp = seg;
        if (*tp == '&') { amp = "&"; tp++; }
        const char *rp = resolve_type_alias(checker, tp);
        if (rp != tp) changed = true;
        int n = snprintf(out + pos, out_sz - pos, "%s%s%s",
                         pos ? "," : "", amp, rp);
        if (n < 0 || (size_t)n >= out_sz - pos) return -1;
        pos += (size_t)n;
        seg = comma ? comma + 1 : NULL;
    }
    return changed ? 1 : 0;
}

/* Resolve a type alias name to the underlying type name.
 * Follows chains transitively with a depth limit of 32.
 * Handles pointer (^Name), array ([Name] / [Name,N]), map (map[K:V]) and
 * func signature (func(P,...)->R) types, recursing into every type slot.
 * Returns the original name if it is not an alias. */
static const char *resolve_type_alias(TypeChecker *checker, const char *name) {
    if (!name) return name;

    /* Handle pointer types: ^Alias → ^Resolved */
    if (name[0] == '^') {
        const char *inner = resolve_type_alias(checker, name + 1);
        if (inner != name + 1) {
            size_t len = strlen(inner) + 2;
            char *buf = arena_alloc(checker->arena, len);
            snprintf(buf, len, "^%s", inner);
            return buf;
        }
        return name;
    }

    /* Handle array types: [Alias] and fixed-size [Alias,N] → [Resolved(,N)] */
    if (name[0] == '[') {
        size_t nlen = strlen(name);
        if (nlen > 2 && name[nlen - 1] == ']') {
            /* Extract inner type name (skip [ and ]) */
            char inner_buf[256];
            size_t inner_len = nlen - 2;
            if (inner_len < sizeof(inner_buf)) {
                memcpy(inner_buf, name + 1, inner_len);
                inner_buf[inner_len] = '\0';
                /* Fixed-size array: split off the trailing ",N" size. */
                char *size_comma = alias_top_level_sep(inner_buf, ',');
                const char *size_suffix = "";
                char size_buf[64];
                if (size_comma) {
                    snprintf(size_buf, sizeof(size_buf), ",%s", size_comma + 1);
                    size_suffix = size_buf;
                    *size_comma = '\0';
                }
                const char *resolved_inner = resolve_type_alias(checker, inner_buf);
                if (resolved_inner != inner_buf) {
                    size_t rlen = strlen(resolved_inner) + strlen(size_suffix) + 3;
                    char *buf = arena_alloc(checker->arena, rlen);
                    snprintf(buf, rlen, "[%s%s]", resolved_inner, size_suffix);
                    return buf;
                }
            }
        }
        return name;
    }

    /* Handle map types: map[K:V] → map[Resolved:Resolved] */
    if (strncmp(name, "map[", 4) == 0) {
        size_t nlen = strlen(name);
        if (nlen > 5 && name[nlen - 1] == ']') {
            char inner_buf[256];
            size_t inner_len = nlen - 5;
            if (inner_len < sizeof(inner_buf)) {
                memcpy(inner_buf, name + 4, inner_len);
                inner_buf[inner_len] = '\0';
                char *colon = alias_top_level_sep(inner_buf, ':');
                if (colon) {
                    *colon = '\0';
                    const char *k = resolve_type_alias(checker, inner_buf);
                    const char *v = resolve_type_alias(checker, colon + 1);
                    if (k != inner_buf || v != colon + 1) {
                        size_t blen = strlen(k) + strlen(v) + 7;
                        char *buf = arena_alloc(checker->arena, blen);
                        snprintf(buf, blen, "map[%s:%s]", k, v);
                        return buf;
                    }
                }
            }
        }
        return name;
    }

    /* Handle func signatures: func(P,...)->R → func(Resolved,...)->Resolved */
    if (strncmp(name, "func(", 5) == 0) {
        int depth = 0;
        const char *close = NULL;
        for (const char *p = name + 4; *p; p++) {
            if (*p == '(') depth++;
            else if (*p == ')' && --depth == 0) { close = p; break; }
        }
        if (close) {
            size_t plen = (size_t)(close - (name + 5));
            const char *tail = close + 1;
            const char *ret = (strncmp(tail, "->", 2) == 0) ? tail + 2 : NULL;
            char params[256];
            char rebuilt[256];
            if (plen < sizeof(params)) {
                memcpy(params, name + 5, plen);
                params[plen] = '\0';
                int params_changed =
                    alias_resolve_type_list(checker, params, rebuilt, sizeof(rebuilt));
                const char *rret = ret ? resolve_type_alias(checker, ret) : NULL;
                bool ret_changed = (ret && rret != ret);
                if (params_changed >= 0 && (params_changed == 1 || ret_changed)) {
                    size_t blen = strlen(rebuilt) + (rret ? strlen(rret) : 0) + 10;
                    char *buf = arena_alloc(checker->arena, blen);
                    if (rret) snprintf(buf, blen, "func(%s)->%s", rebuilt, rret);
                    else snprintf(buf, blen, "func(%s)", rebuilt);
                    return buf;
                }
            }
        }
        return name;
    }

    for (int depth = 0; depth < 32; depth++) {
        bool found = false;
        for (int i = 0; i < checker->type_alias_count; i++) {
            if (strcmp(checker->type_alias_names[i], name) == 0) {
                name = checker->type_alias_targets[i];
                found = true;
                break;
            }
        }
        if (!found) break;
        /* A target that is itself a container — alias Counts = [Count],
         * alias M = map[string:I] — has to go back through the branches
         * above, or the alias nested inside it never gets followed. */
        if (name[0] == '^' || name[0] == '[' ||
            strncmp(name, "map[", 4) == 0 || strncmp(name, "func(", 5) == 0)
            return resolve_type_alias(checker, name);
    }
    return name;
}

/* Format a diagnostic message string into the arena (only called in error paths). */
static char *typechecker_format(TypeChecker *checker, const char *fmt, ...) {
    char buffer[MSG_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    return arena_copy_string(checker->arena, buffer);
}

/* Diagnostic emit helpers for the type-mismatch family. Each pins one code so
 * the situation stays 1:1 with its diagnostic (see scripts/check_error_codes.gray)
 * and spares the caller the repeated file/line/column boilerplate. `msg` is a
 * fully-built, arena-owned string (typically from typechecker_format). */
static void tc_err_assign_type(TypeChecker *checker, AstNode *node, char *msg) {
    diagnostic_error_message(checker->diag, "E3001", msg,
        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
}

static void tc_err_arg_type(TypeChecker *checker, AstNode *arg_node, char *msg) {
    diagnostic_error_message(checker->diag, "E5026", msg,
        NODE_FILE(checker, arg_node), arg_node->token.line, arg_node->token.column, 0);
}

static void tc_err_arity(TypeChecker *checker, AstNode *node, char *msg) {
    diagnostic_error_message(checker->diag, "E5008", msg,
        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
}

static AstNode *find_struct_in_program(AstNode *program, const char *name);

static GrayType *struct_field_type(TypeChecker *checker, const char *struct_name, const char *field) {
    StructInfo *si = find_struct(checker, struct_name);
    /* : for mangled generic struct names (Pair__int), fall back
     * to the base name (Pair) since fields are registered there.
     * When the field type is "?", substitute the concrete binding
     * extracted from the mangled suffix. */
    const char *generic_binding = NULL;
    if (!si && struct_name) {
        const char *dunder = strstr(struct_name, "__");
        if (dunder) {
            char base[MSG_BUF_SIZE];
            size_t n = (size_t)(dunder - struct_name);
            if (n < sizeof(base)) {
                memcpy(base, struct_name, n);
                base[n] = '\0';
                si = find_struct(checker, base);
                generic_binding = dunder + 2;
            }
        }
    }
    if (!si) return &TYPE_UNKNOWN;
    for (int i = 0; i < si->field_count; i++) {
        if (strcmp(si->field_names[i], field) == 0) {
            /* : if the field type is ? (registered as TK_UNKNOWN)
             * and we have a generic binding from the mangled name,
             * substitute to the concrete type. Check the raw decl
             * type_name since the resolved GrayType lost the "?" marker. */
            if (generic_binding && si->field_types[i]->kind == TK_UNKNOWN) {
                /* Find the raw struct decl to check the field type_name */
                if (checker->program) {
                    AstNode *decl = find_struct_in_program(checker->program,
                        struct_name); /* try mangled first */
                    if (!decl) {
                        /* Extract base name and try again */
                        const char *dd = strstr(struct_name, "__");
                        if (dd) {
                            char bname[MSG_BUF_SIZE];
                            size_t bn = (size_t)(dd - struct_name);
                            if (bn < sizeof(bname)) {
                                memcpy(bname, struct_name, bn);
                                bname[bn] = '\0';
                                decl = find_struct_in_program(checker->program, bname);
                            }
                        }
                    }
                    if (decl) {
                        for (int field_index = 0; field_index < decl->data.struct_decl.field_count; field_index++) {
                            if (strcmp(decl->data.struct_decl.fields[field_index].name, field) == 0 &&
                                decl->data.struct_decl.fields[field_index].type_name &&
                                strchr(decl->data.struct_decl.fields[field_index].type_name, '?')) {
                                return type_from_name(generic_binding);
                            }
                        }
                    }
                }
            }
            return si->field_types[i];
        }
    }
    return &TYPE_UNKNOWN;
}

/* --- Function signature helpers --- */

static bool type_name_has_wildcard(const char *tn) {
    if (!tn) return false;
    for (const char *c = tn; *c; c++) {
        if (*c == '?') return true;
    }
    return false;
}

static void register_func(TypeChecker *checker, const char *name,
    GrayType **param_types, int param_count,
    GrayType **return_types, int return_count) {
    GROW_ARRAY(checker->funcs, checker->func_count, checker->func_cap);
    adopt_registration(checker, DECL_FUNC, name, checker->func_count);
    FuncSig *fs = &checker->funcs[checker->func_count++];
    fs->name = name;
    fs->param_types = param_types;
    fs->param_count = param_count;
    fs->return_types = return_types;
    fs->return_count = return_count;
    fs->used = false;
    fs->def_line = 0;
    fs->is_private = false;
    fs->is_generic = false;
    fs->is_discard = false;
    fs->decl = NULL;
    fs->escape_state = 0;
    fs->returns_param_addr = 0;
    memset(fs->param_escape_into, PARAM_ESCAPE_NONE, sizeof fs->param_escape_into);
    fs->mem_state = 0;
    fs->destroys_param_arena = 0;
    fs->resets_param_arena = 0;
    memset(fs->mem_param_field, 0, sizeof fs->mem_param_field);
    fs->returns_param_mem_alloc = 0;
    fs->returns_param_mem_alloc_field = 0;
    fs->instantiations = NULL;
    fs->instantiation_calls = NULL;
    fs->instantiation_count = 0;
    fs->instantiation_cap = 0;
}

/* Substitute '?' with `concrete` in a type string and return a heap copy.
 * Returns a strdup of the original if no wildcard is present. */
static char *substitute_wildcard(const char *source, const char *concrete) {
    if (!source) return NULL;
    if (!type_name_has_wildcard(source)) return strdup(source);
    size_t concrete_len = strlen(concrete);
    size_t len = 0;
    for (const char *cursor = source; *cursor; cursor++) len += (*cursor == '?') ? concrete_len : 1;
    char *output = xmalloc(len + 1);
    char *write_ptr = output;
    for (const char *cursor = source; *cursor; cursor++) {
        if (*cursor == '?') { memcpy(write_ptr, concrete, concrete_len); write_ptr += concrete_len; }
        else *write_ptr++ = *cursor;
    }
    *write_ptr = '\0';
    return output;
}

/* Find the top-level ':' inside a "map[K:V]" type string, skipping nested
 * brackets. Sets key_out/key_len and val_out/val_len on success. */
static bool parse_map_key_value(const char *tn,
                              const char **key_out, size_t *key_len,
                              const char **val_out, size_t *val_len) {
    if (strncmp(tn, "map[", 4) != 0) return false;
    const char *start = tn + 4;
    int depth = 0;
    const char *colon = NULL;
    for (const char *c = start; *c && !(depth == 0 && *c == ']'); c++) {
        if      (*c == '[')                   depth++;
        else if (*c == ']')                   depth--;
        else if (*c == ':' && depth == 0) { colon = c; break; }
    }
    if (!colon) return false;
    *key_out = start;
    *key_len = (size_t)(colon - start);
    *val_out = colon + 1;
    const char *end = tn + strlen(tn) - 1;
    if (*end != ']') return false;
    *val_len = (size_t)(end - *val_out);
    return true;
}

/* Recursive string-based wildcard unifier.
 * Matches param_tn (containing '?') against the concrete type string
 * arg_tn and returns a heap-allocated string for the type '?' binds to,
 * or NULL on shape mismatch. Recurses into arrays and maps so nested
 * composites like [[?]], [map[string:?]], and map[string:[?]] are handled.
 *
 * Examples:
 *   "?"               vs "int"            -> "int"
 *   "[?]"             vs "[string]"       -> "string"
 *   "[[?]]"           vs "[[int]]"        -> "int"
 *   "[map[string:?]]" vs "[map[string:int]]" -> "int"
 *   "map[string:[?]]" vs "map[string:[int]]" -> "int"
 *   "map[?:?]"        vs "map[int:int]"   -> "int"    (K==V required)
 */
static char *bind_wildcard_string(const char *param_tn, const char *arg_tn) {
    if (!param_tn || !arg_tn) return NULL;

    if (strcmp(param_tn, "?") == 0) {
        if (strcmp(arg_tn, "unknown") == 0) return NULL;
        return strdup(arg_tn);
    }

    size_t plen = strlen(param_tn);
    size_t alen = strlen(arg_tn);

    /* Array: both must start/end with brackets; recurse into element types */
    if (param_tn[0] == '[' && arg_tn[0] == '[') {
        if (plen < 3 || alen < 3) return NULL;
        if (param_tn[plen - 1] != ']' || arg_tn[alen - 1] != ']') return NULL;
        char *p_inner = gray_strndup(param_tn + 1, plen - 2);
        char *a_inner = gray_strndup(arg_tn + 1, alen - 2);
        char *result = bind_wildcard_string(p_inner, a_inner);
        free(p_inner);
        free(a_inner);
        return result;
    }

    /* Map: both must be map types; recurse into whichever slot carries '?' */
    if (strncmp(param_tn, "map[", 4) == 0 && strncmp(arg_tn, "map[", 4) == 0) {
        const char *param_key, *param_val, *arg_key, *arg_val;
        size_t param_key_len, param_val_len, arg_key_len, arg_val_len;
        if (!parse_map_key_value(param_tn, &param_key, &param_key_len, &param_val, &param_val_len)) return NULL;
        if (!parse_map_key_value(arg_tn,   &arg_key, &arg_key_len, &arg_val, &arg_val_len)) return NULL;

        bool param_key_has_wildcard = false;
        for (size_t i = 0; i < param_key_len; i++) if (param_key[i] == '?') { param_key_has_wildcard = true; break; }
        bool param_value_has_wildcard = false;
        for (size_t i = 0; i < param_val_len; i++) if (param_val[i] == '?') { param_value_has_wildcard = true; break; }

        if (!param_key_has_wildcard && !param_value_has_wildcard) return NULL;

        /* Concrete slots must match the argument's corresponding slot exactly */
        if (!param_key_has_wildcard && (arg_key_len != param_key_len || memcmp(arg_key, param_key, param_key_len) != 0)) return NULL;
        if (!param_value_has_wildcard && (arg_val_len != param_val_len || memcmp(arg_val, param_val, param_val_len) != 0)) return NULL;

        if (param_key_has_wildcard && param_value_has_wildcard) {
            /* A single '?' binding must satisfy both slots: require arg K == V */
            if (arg_key_len != arg_val_len || memcmp(arg_key, arg_val, arg_key_len) != 0) return NULL;
        }

        char *result;
        if (param_key_has_wildcard) {
            char *p = gray_strndup(param_key, param_key_len);
            char *a = gray_strndup(arg_key, arg_key_len);
            result = bind_wildcard_string(p, a);
            free(p); free(a);
        } else {
            char *p = gray_strndup(param_val, param_val_len);
            char *a = gray_strndup(arg_val, arg_val_len);
            result = bind_wildcard_string(p, a);
            free(p); free(a);
        }
        return result;
    }

    return NULL;
}

/* Derive the concrete type that '?' binds to given the parameter's type
 * string and the resolved argument GrayType. Delegates to bind_wildcard_string
 * so composite nesting (arrays-of-arrays, maps-of-arrays, etc.) is handled
 * recursively. Caller owns the returned string. */
static char *bind_wildcard(const char *param_tn, GrayType *arg_t) {
    if (!param_tn || !arg_t) return NULL;
    return bind_wildcard_string(param_tn, type_name(arg_t));
}

/* Mark a just-registered FuncSig as generic if any of the declared
 * parameter or return type strings on `decl` contains a '?'. Stores
 * `decl` on the sig so later call-site instantiation can walk the
 * original type_name strings for substitution. */
static void finalize_generic_signature(FuncSig *fs, AstNode *decl) {
    fs->decl = decl;
    if (!decl || decl->kind != NODE_FUNC_DECL) return;
    for (int i = 0; i < decl->data.func_decl.param_count; i++) {
        if (type_name_has_wildcard(decl->data.func_decl.params[i].type_name)) {
            fs->is_generic = true;
            return;
        }
    }
    for (int i = 0; i < decl->data.func_decl.return_type_count; i++) {
        if (type_name_has_wildcard(decl->data.func_decl.return_types[i])) {
            fs->is_generic = true;
            return;
        }
    }
}

/* Record a concrete instantiation of a generic function. Returns true
 * if this is a new instantiation (i.e. codegen hasn't seen it yet),
 * false if the same concrete binding was already recorded. Also
 * mirrors the entry onto the source AST func_decl so codegen can
 * enumerate instantiations without needing the FuncSig table. */
static bool record_instantiation(FuncSig *fs, const char *concrete,
                                  AstNode *call_site) {
    if (!fs || !concrete) return false;
    /* : reject "unknown" as a concrete binding. This comes from
     * the main-pass walk of a generic body where the inner call's
     * arguments are still `?` (TK_UNKNOWN). The real bindings are
     * recorded during the slice-4 re-check pass once the outer
     * function's parameters are rebound to concrete types. */
    if (strcmp(concrete, "unknown") == 0 || strcmp(concrete, "?") == 0) return false;
    for (int i = 0; i < fs->instantiation_count; i++) {
        if (strcmp(fs->instantiations[i], concrete) == 0) return false;
    }
    if (fs->instantiation_count >= fs->instantiation_cap) {
        fs->instantiation_cap = fs->instantiation_cap ? fs->instantiation_cap * 2 : 4;
        fs->instantiations = xrealloc(fs->instantiations,
            sizeof(const char *) * fs->instantiation_cap);
        fs->instantiation_calls = xrealloc(fs->instantiation_calls,
            sizeof(AstNode *) * fs->instantiation_cap);
    }
    char *stored = strdup(concrete);
    fs->instantiations[fs->instantiation_count] = stored;
    fs->instantiation_calls[fs->instantiation_count] = call_site;
    fs->instantiation_count++;

    if (fs->decl && fs->decl->kind == NODE_FUNC_DECL) {
        int n = fs->decl->data.func_decl.instantiation_count;
        fs->decl->data.func_decl.instantiations = xrealloc(
            (void *)fs->decl->data.func_decl.instantiations,
            sizeof(const char *) * (size_t)(n + 1));
        fs->decl->data.func_decl.instantiations[n] = stored;
        fs->decl->data.func_decl.instantiation_count = n + 1;
    }
    return true;
}


static FuncSig *find_func(TypeChecker *checker, const char *name) {
    DeclEntry *entry = checker_resolve_entry(checker, name);
    if (!entry) entry = module_table_find_mangled(checker->modules, name);
    if (entry && entry->kind == DECL_FUNC && entry->registry_index >= 0 &&
        entry->registry_index < checker->func_count)
        return &checker->funcs[entry->registry_index];
    return NULL;
}

/* The flat-registry key for the member `name` of module `mod`, written into
 * `buf`. Derived from the declaration the symbol table resolves to, so the
 * module name comes from where the declaration actually lives rather than
 * from the spelling of the reference.
 *
 * Transitional: the stdlib keeps its own registries and is not in the table,
 * so a module the table does not hold still falls back to the string form.
 * The registries remain the authority on what a key names — this decides only
 * how the key is spelled. */
static const char *module_member_key(TypeChecker *checker, const char *mod,
                                     const char *name, char *buf, size_t buflen) {
    ResolveScope scope = checker_scope(checker);
    DeclEntry *entry = module_resolve_qualified(checker->modules, &scope, mod, name, NULL);
    if (entry) return module_mangle_into(entry, buf, buflen);
    snprintf(buf, buflen, "%s_%s",
             module_table_resolve_alias(checker->modules, mod), name);
    return buf;
}

/* The signature of `mod.name`, or NULL. */
static FuncSig *find_module_func(TypeChecker *checker, const char *mod,
                                 const char *name) {
    char key[MSG_BUF_SIZE];
    return find_func(checker, module_member_key(checker, mod, name, key, sizeof(key)));
}

/* The user-defined function a call targets, or NULL (builtins and stdlib
 * keep their own registries and are not in the FuncSig table). */
static FuncSig *resolve_call_sig(TypeChecker *checker, AstNode *call) {
    if (!call || call->kind != NODE_CALL_EXPR) return NULL;
    AstNode *fn = call->data.call.function;
    if (!fn) return NULL;
    if (fn->kind == NODE_LABEL) {
        FuncSig *direct = find_func(checker, fn->data.label.value);
        if (direct) return direct;
        /* A call through a func-ref variable (`const f = ()target; f(...)`,
         * or `ref(target)`) dispatches to `target`, not to a function named
         * "f" — resolve through Symbol.func_ref_name so the escape and @mem
         * summaries still apply across the indirection. A func-ref call
         * passes its arguments 1:1 (no implicit receiver is inserted), so
         * the resolved callee's param indices line up with this call's
         * arg indices exactly as they would for a direct call. */
        Symbol *sym = scope_lookup(checker->current_scope, fn->data.label.value);
        if (sym && sym->func_ref_name)
            return find_func(checker, sym->func_ref_name);
        return NULL;
    }
    if (fn->kind == NODE_MEMBER_EXPR &&
        fn->data.member.object &&
        fn->data.member.object->kind == NODE_LABEL)
        return find_module_func(checker, fn->data.member.object->data.label.value,
                                fn->data.member.member);
    return NULL;
}

static unsigned long long returns_param_address(TypeChecker *checker, FuncSig *fs);
static void ensure_escape_summary(TypeChecker *checker, FuncSig *fs);

/* The value a local named `name` was declared with, searched anywhere in
 * `node`, or NULL. Lets the param-bit walk follow `mut b = Box{p: q};
 * return b`. Local declarations form a DAG (Grayscale forbids forward
 * references), so following initialisers terminates. */
static AstNode *local_initializer(AstNode *node, const char *name) {
    if (!node || !name) return NULL;
    switch (node->kind) {
    case NODE_VAR_DECL:
        if (node->data.var_decl.name &&
            strcmp(node->data.var_decl.name, name) == 0)
            return node->data.var_decl.value;
        return local_initializer(node->data.var_decl.value, name);
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++) {
            AstNode *r = local_initializer(node->data.block.stmts[i], name);
            if (r) return r;
        }
        return NULL;
    case NODE_IF_STMT: {
        AstNode *r = local_initializer(node->data.if_stmt.consequence, name);
        return r ? r : local_initializer(node->data.if_stmt.alternative, name);
    }
    case NODE_WHEN_STMT: {
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            AstNode *r = local_initializer(node->data.when_stmt.cases[i].body, name);
            if (r) return r;
        }
        return local_initializer(node->data.when_stmt.default_body, name);
    }
    case NODE_FOR_STMT:      return local_initializer(node->data.for_stmt.body, name);
    case NODE_FOR_EACH_STMT: return local_initializer(node->data.for_each.body, name);
    case NODE_WHILE_STMT:    return local_initializer(node->data.while_stmt.body, name);
    case NODE_LOOP_STMT:     return local_initializer(node->data.loop_stmt.body, name);
    default:                 return NULL;
    }
}

/* The function a func-ref initializer names — `()target` or `ref(target)` —
 * or NULL if `value` is neither. */
static const char *func_ref_target_name(AstNode *value) {
    if (!value) return NULL;
    if (value->kind == NODE_FUNC_REF) {
        AstNode *target = value->data.func_ref.function;
        return (target && target->kind == NODE_LABEL) ? target->data.label.value : NULL;
    }
    if (value->kind == NODE_CALL_EXPR && value->data.call.function &&
        value->data.call.function->kind == NODE_LABEL &&
        strcmp(value->data.call.function->data.label.value, "ref") == 0 &&
        value->data.call.arg_count == 1 &&
        value->data.call.args[0]->kind == NODE_LABEL)
        return value->data.call.args[0]->data.label.value;
    return NULL;
}

/* resolve_call_sig(), extended to see through a call to a func-ref variable
 * declared inside `body` — `const f = ()target; f(...)` or `const f =
 * ref(target); f(...)`. resolve_call_sig()'s own func-ref branch depends on
 * a live checker->current_scope, which the structural summary walks
 * (escape_walk, return_expr_param_bits, pc_mem_walk) never set: they run
 * detached from scope by design, over whichever function's AST they were
 * asked to summarise, which is routinely a different function from whatever
 * is actually being type-checked at the moment the summary is first
 * requested. `body` — the summarised function's own body, which every one
 * of those walks already has on hand — lets local_initializer() recover the
 * same answer structurally, the same way it already recovers a local
 * variable's initializer for this walk without touching scope. */
static FuncSig *resolve_call_sig_in_body(TypeChecker *checker, AstNode *body,
                                         AstNode *call) {
    FuncSig *direct = resolve_call_sig(checker, call);
    if (direct) return direct;
    AstNode *fn = call->data.call.function;
    if (!fn || fn->kind != NODE_LABEL) return NULL;
    const char *target = func_ref_target_name(
        local_initializer(body, fn->data.label.value));
    return target ? find_func(checker, target) : NULL;
}

/* True if `node` constructs a tagged-enum variant — `Enum.Variant(args)` or
 * the implicit-selector form `.Variant(args)` — rather than calling a real
 * function. Both compile to a NODE_CALL_EXPR whose function resolves to no
 * FuncSig, so origin tracking (return_expr_param_bits, container_literal_
 * origin) must recognize the shape directly, the same way it already walks
 * into a struct/array/map literal, or an addr(local) buried in the payload
 * escapes undetected (a tagged-enum return/store is a value carrier exactly
 * like those literals). */
static bool is_tagged_enum_variant_call(TypeChecker *checker, AstNode *node) {
    if (!node || node->kind != NODE_CALL_EXPR) return false;
    AstNode *fn = node->data.call.function;
    if (!fn) return false;
    if (fn->kind == NODE_IMPLICIT_ENUM) return true;
    const char *qual = ast_member_qualifier(fn);
    return qual && is_enum_name(checker, resolve_type_alias(checker, qual));
}

/* Structural: bitmask of `fs`'s parameters whose address `node` may carry —
 * the parameter named directly, addr()/raw() of a parameter's pointee, a
 * field/element read through a parameter, a value buried in a struct or
 * array literal, a local initialised from any of those, or a value
 * forwarded through another summarised call. Runs while the summary is
 * being built, so it looks only at the AST, never at scope.
 *
 * A bare `p^` yields the pointee *value*, so it carries nothing; `p^.field`
 * reaches into the pointee and can. */
static unsigned long long return_expr_param_bits(TypeChecker *checker,
                                                 FuncSig *fs, AstNode *node) {
    if (!node || !fs->decl) return 0;
    switch (node->kind) {
    case NODE_LABEL: {
        int pc = fs->decl->data.func_decl.param_count;
        for (int i = 0; i < pc && i < 64; i++) {
            const char *pn = fs->decl->data.func_decl.params[i].name;
            if (pn && strcmp(pn, node->data.label.value) == 0)
                return 1ull << i;
        }
        AstNode *init = local_initializer(fs->decl->data.func_decl.body,
                                          node->data.label.value);
        if (init && init != node)
            return return_expr_param_bits(checker, fs, init);
        return 0;
    }
    case NODE_MEMBER_EXPR: {
        AstNode *o = node->data.member.object;
        while (o && o->kind == NODE_POSTFIX_EXPR &&
               o->data.postfix.op == TOK_CARET)
            o = o->data.postfix.left;
        return return_expr_param_bits(checker, fs, o);
    }
    case NODE_INDEX_EXPR: {
        AstNode *l = node->data.index_expr.left;
        while (l && l->kind == NODE_POSTFIX_EXPR &&
               l->data.postfix.op == TOK_CARET)
            l = l->data.postfix.left;
        return return_expr_param_bits(checker, fs, l);
    }
    case NODE_POSTFIX_EXPR:
        if (node->data.postfix.op == TOK_CARET) return 0;
        return return_expr_param_bits(checker, fs, node->data.postfix.left);
    case NODE_STRUCT_VALUE: {
        unsigned long long out = 0;
        for (int i = 0; i < node->data.struct_value.count; i++)
            out |= return_expr_param_bits(checker, fs,
                                          node->data.struct_value.field_values[i]);
        return out;
    }
    case NODE_ARRAY_VALUE: {
        unsigned long long out = 0;
        for (int i = 0; i < node->data.array_value.count; i++)
            out |= return_expr_param_bits(checker, fs,
                                          node->data.array_value.elements[i]);
        return out;
    }
    case NODE_MAP_VALUE: {
        unsigned long long out = 0;
        for (int i = 0; i < node->data.map_value.count; i++) {
            out |= return_expr_param_bits(checker, fs, node->data.map_value.keys[i]);
            out |= return_expr_param_bits(checker, fs, node->data.map_value.values[i]);
        }
        return out;
    }
    case NODE_CALL_EXPR: {
        AstNode *f = node->data.call.function;
        if (f && f->kind == NODE_LABEL && node->data.call.arg_count == 1 &&
            (strcmp(f->data.label.value, "addr") == 0 ||
             strcmp(f->data.label.value, "raw") == 0 ||
             strcmp(f->data.label.value, "copy") == 0))
            return return_expr_param_bits(checker, fs, node->data.call.args[0]);
        if (is_tagged_enum_variant_call(checker, node)) {
            unsigned long long out = 0;
            for (int i = 0; i < node->data.call.arg_count; i++)
                out |= return_expr_param_bits(checker, fs, node->data.call.args[i]);
            return out;
        }
        FuncSig *callee = resolve_call_sig_in_body(checker, fs->decl->data.func_decl.body, node);
        if (callee == fs) return 0;
        if (!callee) {
            /* A stdlib module call or bare builtin: no FuncSig, so no
             * returns_param_addr summary to consult — same gap
             * stdlib_call_arg_origin backstops for pointer_origin_of /
             * container_literal_origin, in this function's own bitmask
             * terms. Gated the same way: only when the call's own result
             * could structurally carry a pointer.
             *
             * A summary is computed lazily, on demand from whichever call
             * site first needs it — e.g. from a caller textually earlier in
             * the file, whose own body is what's actively being checked and
             * whose scope is therefore live on checker->current_scope. This
             * walk is structural precisely so it never needs a live scope
             * for *this* function's own body; calling resolve_expression()
             * here would resolve `node` (e.g. a func-typed-parameter call
             * `f(p)`) against the wrong function's scope and misreport `f`
             * and `p` as undefined. typetable_get() is a plain lookup: it
             * returns the type if this exact node was already resolved
             * during a properly-scoped pass over this function's own body,
             * and NULL — a safe "unknown, no bit set" — otherwise. */
            GrayType *rt = typetable_get(checker->type_table, node);
            if (!rt || !(rt->kind == TK_POINTER || rt->kind == TK_STRUCT ||
                         rt->kind == TK_ARRAY || rt->kind == TK_MAP))
                return 0;
            unsigned long long out = 0;
            for (int i = 0; i < node->data.call.arg_count; i++)
                out |= return_expr_param_bits(checker, fs, node->data.call.args[i]);
            return out;
        }
        unsigned long long callee_bits = returns_param_address(checker, callee);
        unsigned long long out = 0;
        for (int i = 0; i < callee->param_count &&
                        i < node->data.call.arg_count && i < 64; i++)
            if (callee_bits & (1ull << i))
                out |= return_expr_param_bits(checker, fs, node->data.call.args[i]);
        return out;
    }
    default:
        return 0;
    }
}

/* OR together the param bits of every `return` reachable in a statement
 * subtree, without descending into a nested function declaration. */
static unsigned long long return_stmt_param_bits(TypeChecker *checker,
                                                 FuncSig *fs, AstNode *node) {
    if (!node) return 0;
    unsigned long long bits = 0;
    switch (node->kind) {
    case NODE_RETURN_STMT:
        for (int i = 0; i < node->data.return_stmt.count; i++)
            bits |= return_expr_param_bits(checker, fs,
                                           node->data.return_stmt.values[i]);
        break;
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++)
            bits |= return_stmt_param_bits(checker, fs, node->data.block.stmts[i]);
        break;
    case NODE_IF_STMT:
        bits |= return_stmt_param_bits(checker, fs, node->data.if_stmt.consequence);
        bits |= return_stmt_param_bits(checker, fs, node->data.if_stmt.alternative);
        break;
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++)
            bits |= return_stmt_param_bits(checker, fs,
                                           node->data.when_stmt.cases[i].body);
        bits |= return_stmt_param_bits(checker, fs, node->data.when_stmt.default_body);
        break;
    case NODE_FOR_STMT:
        bits |= return_stmt_param_bits(checker, fs, node->data.for_stmt.body);
        break;
    case NODE_FOR_EACH_STMT:
        bits |= return_stmt_param_bits(checker, fs, node->data.for_each.body);
        break;
    case NODE_WHILE_STMT:
        bits |= return_stmt_param_bits(checker, fs, node->data.while_stmt.body);
        break;
    case NODE_LOOP_STMT:
        bits |= return_stmt_param_bits(checker, fs, node->data.loop_stmt.body);
        break;
    default:
        break;
    }
    return bits;
}

/* --- param_escape_into: where a parameter's address ends up living --- */

/* Root name of an assignment/insert target, seeing *through* a pointer
 * dereference (unlike assignment_target_root_name, which stops at `^`):
 * writing to `dst^.field` stores into the pointee, which for a pointer
 * parameter is the caller's memory. */
static const char *escape_root_name(AstNode *e) {
    while (e) {
        switch (e->kind) {
        case NODE_LABEL:       return e->data.label.value;
        case NODE_MEMBER_EXPR: e = e->data.member.object; break;
        case NODE_INDEX_EXPR:  e = e->data.index_expr.left; break;
        case NODE_POSTFIX_EXPR:
            if (e->data.postfix.op != TOK_CARET) return NULL;
            e = e->data.postfix.left;
            break;
        default: return NULL;
        }
    }
    return NULL;
}

/* Is `name` declared as a local anywhere in `node`? */
static bool declared_in_subtree(AstNode *node, const char *name) {
    if (!node || !name) return false;
    switch (node->kind) {
    case NODE_VAR_DECL:
        if (node->data.var_decl.name &&
            strcmp(node->data.var_decl.name, name) == 0)
            return true;
        return declared_in_subtree(node->data.var_decl.value, name);
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++)
            if (declared_in_subtree(node->data.block.stmts[i], name)) return true;
        return false;
    case NODE_IF_STMT:
        return declared_in_subtree(node->data.if_stmt.consequence, name) ||
               declared_in_subtree(node->data.if_stmt.alternative, name);
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++)
            if (declared_in_subtree(node->data.when_stmt.cases[i].body, name)) return true;
        return declared_in_subtree(node->data.when_stmt.default_body, name);
    case NODE_FOR_STMT:
        if (node->data.for_stmt.var_name &&
            strcmp(node->data.for_stmt.var_name, name) == 0) return true;
        return declared_in_subtree(node->data.for_stmt.body, name);
    case NODE_FOR_EACH_STMT:
        if ((node->data.for_each.var_name &&
             strcmp(node->data.for_each.var_name, name) == 0) ||
            (node->data.for_each.index_name &&
             strcmp(node->data.for_each.index_name, name) == 0)) return true;
        return declared_in_subtree(node->data.for_each.body, name);
    case NODE_WHILE_STMT: return declared_in_subtree(node->data.while_stmt.body, name);
    case NODE_LOOP_STMT:  return declared_in_subtree(node->data.loop_stmt.body, name);
    default: return false;
    }
}

/* Is `name` a module-level variable of the program being checked? Only
 * returns true when it can prove it, so an unknown name is treated as a
 * local (a conservative miss, never a false E3163). */
static bool is_module_level_var(TypeChecker *checker, const char *name) {
    if (!checker->program || !name ||
        checker->program->kind != NODE_PROGRAM) return false;
    for (int i = 0; i < checker->program->data.program.stmt_count; i++) {
        AstNode *s = checker->program->data.program.stmts[i];
        if (s && s->kind == NODE_VAR_DECL && s->data.var_decl.name &&
            strcmp(s->data.var_decl.name, name) == 0)
            return true;
    }
    return false;
}

/* Classify a store target's root: a parameter index, PARAM_ESCAPE_GLOBAL, or
 * PARAM_ESCAPE_NONE (a local — an in-function concern the direct E3163
 * checks already cover). */
static signed char escape_dest_for_root(TypeChecker *checker, FuncSig *fs,
                                        const char *root, AstNode *body) {
    if (!root) return PARAM_ESCAPE_NONE;
    int pc = fs->decl->data.func_decl.param_count;
    for (int i = 0; i < pc && i < 64; i++) {
        const char *pn = fs->decl->data.func_decl.params[i].name;
        if (pn && strcmp(pn, root) == 0) return (signed char)i;
    }
    if (declared_in_subtree(body, root)) return PARAM_ESCAPE_NONE;
    if (is_module_level_var(checker, root)) return PARAM_ESCAPE_GLOBAL;
    return PARAM_ESCAPE_NONE;
}

/* stdlib functions that store an argument into a container argument. */
typedef struct {
    const char *mod;
    const char *fn;
    int value_arg;
    int container_arg;
} ContainerSink;

static const ContainerSink container_sinks[] = {
    {"arrays", "append",    1, 0},
    {"arrays", "prepend",   1, 0},
    {"arrays", "insert_at", 2, 0},
    {"arrays", "fill",      2, 0},
};

static const ContainerSink *find_container_sink(TypeChecker *checker, AstNode *call) {
    AstNode *fn = call->data.call.function;
    if (!fn || fn->kind != NODE_MEMBER_EXPR) return NULL;
    const char *qual = ast_member_qualifier(fn);
    if (!qual) return NULL;
    const char *mod = typechecker_resolve_alias(checker, qual);
    const char *mfn = fn->data.member.member;
    if (!mod || !mfn) return NULL;
    for (size_t i = 0; i < sizeof container_sinks / sizeof container_sinks[0]; i++)
        if (strcmp(container_sinks[i].mod, mod) == 0 &&
            strcmp(container_sinks[i].fn, mfn) == 0)
            return &container_sinks[i];
    return NULL;
}

static void record_param_escape(FuncSig *fs, unsigned long long bits,
                                signed char dest) {
    for (int i = 0; i < fs->param_count && i < 64; i++) {
        if (!(bits & (1ull << i))) continue;
        if (fs->param_escape_into[i] == PARAM_ESCAPE_NONE ||
            dest == PARAM_ESCAPE_GLOBAL)
            fs->param_escape_into[i] = dest;
    }
}

/* Scan a function body for places a parameter's address is stored into
 * caller-visible memory: an assignment whose target roots at another
 * parameter or a module-level variable, a stdlib container insert, or a
 * call that itself escapes the argument. */
static void escape_walk(TypeChecker *checker, FuncSig *fs, AstNode *body,
                        AstNode *node) {
    if (!node) return;
    switch (node->kind) {
    case NODE_ASSIGN_STMT: {
        signed char dest = escape_dest_for_root(checker, fs,
            escape_root_name(node->data.assign.target), body);
        if (dest != PARAM_ESCAPE_NONE)
            record_param_escape(fs, return_expr_param_bits(checker, fs,
                node->data.assign.value), dest);
        escape_walk(checker, fs, body, node->data.assign.value);
        break;
    }
    case NODE_CALL_EXPR: {
        const ContainerSink *sink = find_container_sink(checker, node);
        if (sink && node->data.call.arg_count > sink->value_arg &&
            node->data.call.arg_count > sink->container_arg) {
            signed char dest = escape_dest_for_root(checker, fs,
                escape_root_name(node->data.call.args[sink->container_arg]), body);
            if (dest != PARAM_ESCAPE_NONE)
                record_param_escape(fs, return_expr_param_bits(checker, fs,
                    node->data.call.args[sink->value_arg]), dest);
        }
        FuncSig *callee = resolve_call_sig_in_body(checker, body, node);
        if (callee && callee != fs) {
            ensure_escape_summary(checker, callee);
            for (int k = 0; k < callee->param_count &&
                            k < node->data.call.arg_count && k < 64; k++) {
                signed char cdest = callee->param_escape_into[k];
                if (cdest == PARAM_ESCAPE_NONE) continue;
                unsigned long long bits = return_expr_param_bits(checker, fs,
                    node->data.call.args[k]);
                if (!bits) continue;
                if (cdest == PARAM_ESCAPE_GLOBAL) {
                    record_param_escape(fs, bits, PARAM_ESCAPE_GLOBAL);
                } else if (cdest < node->data.call.arg_count) {
                    signed char dest = escape_dest_for_root(checker, fs,
                        escape_root_name(node->data.call.args[cdest]), body);
                    if (dest != PARAM_ESCAPE_NONE)
                        record_param_escape(fs, bits, dest);
                }
            }
        }
        for (int i = 0; i < node->data.call.arg_count; i++)
            escape_walk(checker, fs, body, node->data.call.args[i]);
        break;
    }
    case NODE_VAR_DECL:
        escape_walk(checker, fs, body, node->data.var_decl.value);
        break;
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++)
            escape_walk(checker, fs, body, node->data.block.stmts[i]);
        break;
    case NODE_IF_STMT:
        escape_walk(checker, fs, body, node->data.if_stmt.consequence);
        escape_walk(checker, fs, body, node->data.if_stmt.alternative);
        break;
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++)
            escape_walk(checker, fs, body, node->data.when_stmt.cases[i].body);
        escape_walk(checker, fs, body, node->data.when_stmt.default_body);
        break;
    case NODE_FOR_STMT:
        escape_walk(checker, fs, body, node->data.for_stmt.body);
        break;
    case NODE_FOR_EACH_STMT:
        escape_walk(checker, fs, body, node->data.for_each.body);
        break;
    case NODE_WHILE_STMT:
        escape_walk(checker, fs, body, node->data.while_stmt.body);
        break;
    case NODE_LOOP_STMT:
        escape_walk(checker, fs, body, node->data.loop_stmt.body);
        break;
    case NODE_EXPR_STMT:
        escape_walk(checker, fs, body, node->data.expr_stmt.expr);
        break;
    case NODE_ENSURE_STMT:
        escape_walk(checker, fs, body, node->data.ensure_stmt.expr);
        break;
    case NODE_RETURN_STMT:
        for (int i = 0; i < node->data.return_stmt.count; i++)
            escape_walk(checker, fs, body, node->data.return_stmt.values[i]);
        break;
    default:
        break;
    }
}

/* Lazily compute and memoise `fs`'s escape summary (returns_param_addr and
 * param_escape_into). A function caught mid-computation (recursion) is left
 * with whatever partial summary it has — conservative, never a false
 * positive. */
static void ensure_escape_summary(TypeChecker *checker, FuncSig *fs) {
    if (!fs || fs->escape_state != 0) return;
    fs->escape_state = 1;
    fs->returns_param_addr = 0;
    memset(fs->param_escape_into, PARAM_ESCAPE_NONE, sizeof fs->param_escape_into);
    AstNode *body = (fs->decl && fs->decl->kind == NODE_FUNC_DECL)
                    ? fs->decl->data.func_decl.body : NULL;
    if (body && fs->decl->data.func_decl.param_count <= 64) {
        /* returns_param_addr matters only when the return value can carry a
         * pointer: a pointer, or an aggregate that may hold one. A `?` return
         * slot resolves to TK_UNKNOWN here — there is no call-site binding
         * yet to give it a concrete kind — but real call sites bind it to
         * pointer/aggregate types constantly, so check the slot's declared
         * name for a wildcard too; otherwise a wildcard-return function's
         * summary is never computed and forwarding a pointer through it
         * hides the escape from both E3162 and E3163. */
        bool escapable_ret = false;
        int decl_ret_count = fs->decl->data.func_decl.return_type_count;
        for (int i = 0; i < fs->return_count; i++) {
            GrayType *rt = fs->return_types[i];
            if (rt && (rt->kind == TK_POINTER || rt->kind == TK_STRUCT ||
                       rt->kind == TK_ARRAY || rt->kind == TK_MAP))
                escapable_ret = true;
            else if (i < decl_ret_count &&
                     type_name_has_wildcard(fs->decl->data.func_decl.return_types[i]))
                escapable_ret = true;
        }
        if (escapable_ret)
            fs->returns_param_addr = return_stmt_param_bits(checker, fs, body);
        escape_walk(checker, fs, body, body);
    }
    fs->escape_state = 2;
}

static unsigned long long returns_param_address(TypeChecker *checker, FuncSig *fs) {
    if (!fs) return 0;
    ensure_escape_summary(checker, fs);
    return fs->returns_param_addr;
}

/* --- pc_mem_walk: cross-function @mem summary --- */

static void pc_ensure_mem_summary(TypeChecker *checker, FuncSig *fs);

/* Which of fs's own parameters `key` is rooted at, by index, and the
 * field-path suffix beyond that parameter (NULL if key IS the bare
 * parameter itself, e.g. key "h.a" against parameter "h" yields suffix
 * ".a"). -1 if key doesn't root at any parameter of fs — a global, an
 * unrelated local, or simply not a match. */
static int pc_mem_param_index_for_key(FuncSig *fs, const char *key, const char **out_suffix) {
    if (!fs->decl || !key) return -1;
    int pc = fs->decl->data.func_decl.param_count;
    for (int i = 0; i < pc && i < 64; i++) {
        const char *pn = fs->decl->data.func_decl.params[i].name;
        if (!pn) continue;
        size_t pnlen = strlen(pn);
        if (strcmp(pn, key) == 0) { *out_suffix = NULL; return i; }
        if (strncmp(key, pn, pnlen) == 0 && key[pnlen] == '.') {
            *out_suffix = key + pnlen;
            return i;
        }
    }
    return -1;
}

/* The path key an arena reference resolves to at a call site, given a
 * callee's arena-carrying parameter and its recorded field suffix (NULL for
 * a bare parameter). `arg` is this call's own argument expression for that
 * parameter — pc_arena_path_key() resolves it whether it's itself a bare
 * name or already a field chain, so a suffix composes through any depth of
 * forwarding (`cleanup(h)` where cleanup destroys `h.a`, called as
 * `cleanup(outer.h)`, yields "outer.h.a"). */
static const char *pc_mem_forward_key(TypeChecker *checker, AstNode *arg, const char *suffix) {
    const char *base = pc_arena_path_key(checker, arg);
    if (!base) return NULL;
    if (!suffix) return base;
    char buf[MSG_BUF_SIZE];
    snprintf(buf, sizeof(buf), "%s%s", base, suffix);
    return arena_copy_string(checker->arena, buf);
}

/* Scan `fs`'s body for mem.destroy()/mem.reset() calls on one of its own
 * parameters, and for calls to other (already-summarised) functions that
 * destroy/reset one of *their* parameters when the corresponding argument
 * here is one of `fs`'s own parameters — so the effect forwards through a
 * wrapper like `do outer(a Arena) { helper(a) }`. Structural, like
 * escape_walk: looks only at the AST while the summary is being built. */
static void pc_mem_walk(TypeChecker *checker, FuncSig *fs, AstNode *node) {
    if (!node || !fs->decl) return;
    switch (node->kind) {
    case NODE_CALL_EXPR: {
        const char *fn = NULL, *arena = NULL;
        if (pc_is_mem_call(checker, node, &fn, &arena) && arena &&
            (strcmp(fn, "destroy") == 0 || strcmp(fn, "reset") == 0)) {
            const char *suffix = NULL;
            int i = pc_mem_param_index_for_key(fs, arena, &suffix);
            if (i >= 0) {
                if (strcmp(fn, "destroy") == 0) fs->destroys_param_arena |= 1ull << i;
                else fs->resets_param_arena |= 1ull << i;
                if (suffix) fs->mem_param_field[i] = suffix;
            }
        } else {
            FuncSig *callee = resolve_call_sig_in_body(checker,
                fs->decl->data.func_decl.body, node);
            if (callee && callee != fs) {
                pc_ensure_mem_summary(checker, callee);
                for (int k = 0; k < callee->param_count &&
                                k < node->data.call.arg_count && k < 64; k++) {
                    unsigned long long keffect =
                        (callee->destroys_param_arena | callee->resets_param_arena) &
                        (1ull << k);
                    if (!keffect) continue;
                    const char *key = pc_mem_forward_key(checker,
                        node->data.call.args[k], callee->mem_param_field[k]);
                    if (!key) continue;
                    const char *suffix = NULL;
                    int i = pc_mem_param_index_for_key(fs, key, &suffix);
                    if (i < 0) continue;
                    if (callee->destroys_param_arena & (1ull << k))
                        fs->destroys_param_arena |= 1ull << i;
                    if (callee->resets_param_arena & (1ull << k))
                        fs->resets_param_arena |= 1ull << i;
                    if (suffix) fs->mem_param_field[i] = suffix;
                }
            }
        }
        for (int i = 0; i < node->data.call.arg_count; i++)
            pc_mem_walk(checker, fs, node->data.call.args[i]);
        break;
    }
    case NODE_VAR_DECL:
        pc_mem_walk(checker, fs, node->data.var_decl.value);
        break;
    case NODE_ASSIGN_STMT:
        pc_mem_walk(checker, fs, node->data.assign.value);
        break;
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++)
            pc_mem_walk(checker, fs, node->data.block.stmts[i]);
        break;
    case NODE_IF_STMT:
        pc_mem_walk(checker, fs, node->data.if_stmt.consequence);
        pc_mem_walk(checker, fs, node->data.if_stmt.alternative);
        break;
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++)
            pc_mem_walk(checker, fs, node->data.when_stmt.cases[i].body);
        pc_mem_walk(checker, fs, node->data.when_stmt.default_body);
        break;
    case NODE_FOR_STMT:      pc_mem_walk(checker, fs, node->data.for_stmt.body); break;
    case NODE_FOR_EACH_STMT: pc_mem_walk(checker, fs, node->data.for_each.body); break;
    case NODE_WHILE_STMT:    pc_mem_walk(checker, fs, node->data.while_stmt.body); break;
    case NODE_LOOP_STMT:     pc_mem_walk(checker, fs, node->data.loop_stmt.body); break;
    case NODE_EXPR_STMT:     pc_mem_walk(checker, fs, node->data.expr_stmt.expr); break;
    case NODE_ENSURE_STMT:   pc_mem_walk(checker, fs, node->data.ensure_stmt.expr); break;
    case NODE_RETURN_STMT:
        for (int i = 0; i < node->data.return_stmt.count; i++)
            pc_mem_walk(checker, fs, node->data.return_stmt.values[i]);
        break;
    default:
        break;
    }
}

/* bitmask: bit i set if return-value expression `node` yields a @mem pointer
 * allocated from the arena named by `fs`'s parameter i — mem.alloc(a, x) /
 * mem.init(a, T) directly, through a local variable declared from one, or
 * forwarded through another summarised call's own returns_param_mem_alloc.
 * Mirrors return_expr_param_bits' shape, for arena allocation instead of
 * pointer escape. */
/* Fills *out_direct (bit i: node itself is a @mem pointer allocated from
 * parameter i's arena) and *out_field (bit i: node is an aggregate — a
 * struct/array/map literal, or a call forwarding one — with such a pointer
 * buried inside it) for a return-value expression. A bit is set in at most
 * one of the two; mirrors the mem_arena / field_mem_arena split
 * pc_bind_mem_pointer() records on a Symbol, but for a FuncSig's summary of
 * its own return value instead. */
static void pc_return_expr_mem_bits(TypeChecker *checker, FuncSig *fs, AstNode *node,
                                    unsigned long long *out_direct,
                                    unsigned long long *out_field) {
    if (!node || !fs->decl) return;
    if (node->kind == NODE_LABEL) {
        AstNode *init = local_initializer(fs->decl->data.func_decl.body,
                                          node->data.label.value);
        if (init && init != node)
            pc_return_expr_mem_bits(checker, fs, init, out_direct, out_field);
        return;
    }
    int pc = fs->decl->data.func_decl.param_count;
    switch (node->kind) {
    case NODE_STRUCT_VALUE:
        for (int i = 0; i < node->data.struct_value.count; i++) {
            unsigned long long d = 0, f = 0;
            pc_return_expr_mem_bits(checker, fs,
                node->data.struct_value.field_values[i], &d, &f);
            *out_field |= d | f;
        }
        return;
    case NODE_ARRAY_VALUE:
        for (int i = 0; i < node->data.array_value.count; i++) {
            unsigned long long d = 0, f = 0;
            pc_return_expr_mem_bits(checker, fs,
                node->data.array_value.elements[i], &d, &f);
            *out_field |= d | f;
        }
        return;
    case NODE_MAP_VALUE:
        for (int i = 0; i < node->data.map_value.count; i++) {
            unsigned long long d = 0, f = 0;
            pc_return_expr_mem_bits(checker, fs, node->data.map_value.values[i], &d, &f);
            *out_field |= d | f;
        }
        return;
    default:
        break;
    }
    if (node->kind != NODE_CALL_EXPR) return;
    const char *fn = NULL, *arena = NULL;
    if (pc_is_mem_call(checker, node, &fn, &arena) && arena &&
        (strcmp(fn, "init") == 0 || strcmp(fn, "alloc") == 0)) {
        for (int i = 0; i < pc && i < 64; i++) {
            const char *pn = fs->decl->data.func_decl.params[i].name;
            if (pn && strcmp(pn, arena) == 0) *out_direct |= 1ull << i;
        }
        return;
    }
    FuncSig *callee = resolve_call_sig_in_body(checker, fs->decl->data.func_decl.body, node);
    if (!callee || callee == fs) return;
    pc_ensure_mem_summary(checker, callee);
    for (int k = 0; k < callee->param_count &&
                    k < node->data.call.arg_count && k < 64; k++) {
        unsigned long long callee_bit = 1ull << k;
        if (!((callee->returns_param_mem_alloc | callee->returns_param_mem_alloc_field) &
              callee_bit))
            continue;
        AstNode *arg = node->data.call.args[k];
        if (arg->kind != NODE_LABEL) continue;
        bool via_field = (callee->returns_param_mem_alloc_field & callee_bit) != 0;
        for (int i = 0; i < pc && i < 64; i++) {
            const char *pn = fs->decl->data.func_decl.params[i].name;
            if (!pn || strcmp(pn, arg->data.label.value) != 0) continue;
            if (via_field) *out_field |= 1ull << i;
            else *out_direct |= 1ull << i;
        }
    }
}

/* OR together pc_return_expr_mem_bits() over every `return` reachable in a
 * statement subtree. Mirrors return_stmt_param_bits. */
static void pc_return_stmt_mem_bits(TypeChecker *checker, FuncSig *fs, AstNode *node,
                                    unsigned long long *out_direct,
                                    unsigned long long *out_field) {
    if (!node) return;
    switch (node->kind) {
    case NODE_RETURN_STMT:
        for (int i = 0; i < node->data.return_stmt.count; i++)
            pc_return_expr_mem_bits(checker, fs, node->data.return_stmt.values[i],
                                    out_direct, out_field);
        break;
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++)
            pc_return_stmt_mem_bits(checker, fs, node->data.block.stmts[i],
                                    out_direct, out_field);
        break;
    case NODE_IF_STMT:
        pc_return_stmt_mem_bits(checker, fs, node->data.if_stmt.consequence, out_direct, out_field);
        pc_return_stmt_mem_bits(checker, fs, node->data.if_stmt.alternative, out_direct, out_field);
        break;
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++)
            pc_return_stmt_mem_bits(checker, fs, node->data.when_stmt.cases[i].body,
                                    out_direct, out_field);
        pc_return_stmt_mem_bits(checker, fs, node->data.when_stmt.default_body, out_direct, out_field);
        break;
    case NODE_FOR_STMT:
        pc_return_stmt_mem_bits(checker, fs, node->data.for_stmt.body, out_direct, out_field);
        break;
    case NODE_FOR_EACH_STMT:
        pc_return_stmt_mem_bits(checker, fs, node->data.for_each.body, out_direct, out_field);
        break;
    case NODE_WHILE_STMT:
        pc_return_stmt_mem_bits(checker, fs, node->data.while_stmt.body, out_direct, out_field);
        break;
    case NODE_LOOP_STMT:
        pc_return_stmt_mem_bits(checker, fs, node->data.loop_stmt.body, out_direct, out_field);
        break;
    default:
        break;
    }
}

/* Lazily compute and memoise `fs`'s cross-function @mem summary. Mirrors
 * ensure_escape_summary: a function caught mid-computation (recursion) is
 * left with whatever partial summary it has — conservative, never a false
 * negative turned into a crash, just a possibly-missed forwarding case. */
static void pc_ensure_mem_summary(TypeChecker *checker, FuncSig *fs) {
    if (!fs || fs->mem_state != 0) return;
    fs->mem_state = 1;
    fs->destroys_param_arena = 0;
    fs->resets_param_arena = 0;
    memset(fs->mem_param_field, 0, sizeof fs->mem_param_field);
    fs->returns_param_mem_alloc = 0;
    fs->returns_param_mem_alloc_field = 0;
    AstNode *body = (fs->decl && fs->decl->kind == NODE_FUNC_DECL)
                    ? fs->decl->data.func_decl.body : NULL;
    if (body && fs->decl->data.func_decl.param_count <= 64) {
        pc_mem_walk(checker, fs, body);
        pc_return_stmt_mem_bits(checker, fs, body,
            &fs->returns_param_mem_alloc, &fs->returns_param_mem_alloc_field);
    }
    fs->mem_state = 2;
}

static int call_result_origin(TypeChecker *checker, AstNode *call,
                              const char **out_name) {
    FuncSig *fs = resolve_call_sig(checker, call);
    if (!fs) return 0;
    unsigned long long bits = returns_param_address(checker, fs);
    if (!bits) return 0;
    int best = 0;
    const char *best_name = NULL;
    for (int i = 0; i < fs->param_count &&
                    i < call->data.call.arg_count && i < 64; i++) {
        if (!(bits & (1ull << i))) continue;
        const char *nm = NULL;
        int d = pointer_origin_of(checker, call->data.call.args[i], &nm);
        if (d > best) { best = d; best_name = nm; }
    }
    if (best) *out_name = best_name;
    return best;
}

static int container_literal_origin(TypeChecker *checker, AstNode *node,
                                    const char **out_name);

/* Deepest lifetime origin an expression carries: a tracked pointer (the
 * direct addr()/raw() form, one laundered through a variable, or one read
 * back out of a container) combined with an address buried inside a
 * struct / array / map literal. */
static int expression_origin(TypeChecker *checker, AstNode *value,
                             const char **out_name) {
    const char *na = NULL, *nb = NULL;
    int a = pointer_origin_of(checker, value, &na);
    int b = container_literal_origin(checker, value, &nb);
    if (b > a) { *out_name = nb; return b; }
    if (a) { *out_name = na; return a; }
    return 0;
}

/* Deepest origin among a call's own arguments — see the forward declaration
 * near call_result_origin for when this applies. Gated on the call's own
 * result type being able to structurally carry a pointer (a pointer, or a
 * struct/array/map that might hold one buried inside): arrays.get_first(arr)
 * needs this (returns ^T, forwarded from arr), arrays.contains(arr, x) does
 * not (returns bool — no argument's address could possibly be smuggled out
 * through it, whatever arr holds). Without the gate, any stdlib call taking
 * an address-carrying argument would be flagged regardless of what it
 * actually returns. */
static int stdlib_call_arg_origin(TypeChecker *checker, AstNode *call,
                                  const char **out_name) {
    GrayType *rt = resolve_expression(checker, call);
    if (!rt || !(rt->kind == TK_POINTER || rt->kind == TK_STRUCT ||
                 rt->kind == TK_ARRAY || rt->kind == TK_MAP))
        return 0;
    int best = 0;
    const char *best_name = NULL;
    for (int i = 0; i < call->data.call.arg_count; i++) {
        const char *nm = NULL;
        int d = expression_origin(checker, call->data.call.args[i], &nm);
        if (d > best) { best = d; best_name = nm; }
    }
    if (best) *out_name = best_name;
    return best;
}

/* Deepest pointer-origin buried in a struct / array / map literal, or 0.
 * A container built with `Field{p: addr(local)}`, `{addr(local)}`, or
 * `{"k": addr(local)}` carries the pointee's lifetime even though the
 * address sits inside an element. Recurses through nested literals. */
static int container_literal_origin(TypeChecker *checker, AstNode *node,
                                    const char **out_name) {
    if (!node) return 0;
    int best = 0;
    const char *best_name = NULL;
    switch (node->kind) {
    case NODE_STRUCT_VALUE:
        for (int i = 0; i < node->data.struct_value.count; i++) {
            const char *nm = NULL;
            int d = expression_origin(checker,
                        node->data.struct_value.field_values[i], &nm);
            if (d > best) { best = d; best_name = nm; }
        }
        break;
    case NODE_ARRAY_VALUE:
        for (int i = 0; i < node->data.array_value.count; i++) {
            const char *nm = NULL;
            int d = expression_origin(checker,
                        node->data.array_value.elements[i], &nm);
            if (d > best) { best = d; best_name = nm; }
        }
        break;
    case NODE_MAP_VALUE:
        for (int i = 0; i < node->data.map_value.count; i++) {
            const char *nm = NULL;
            int d = expression_origin(checker, node->data.map_value.values[i], &nm);
            if (d > best) { best = d; best_name = nm; }
            nm = NULL;
            d = expression_origin(checker, node->data.map_value.keys[i], &nm);
            if (d > best) { best = d; best_name = nm; }
        }
        break;
    case NODE_CALL_EXPR:
        if (is_tagged_enum_variant_call(checker, node)) {
            /* Enum.Variant(args) / .Variant(args): the payload is a value
             * carrier exactly like a struct/array/map literal, but resolves
             * to no FuncSig so it needs its own recognizer rather than
             * falling into a normal call's lookup — the enum's own type is
             * TK_ENUM, which the general stdlib_call_arg_origin fallback
             * below does not treat as capable of carrying a pointer. */
            for (int i = 0; i < node->data.call.arg_count; i++) {
                const char *nm = NULL;
                int d = expression_origin(checker, node->data.call.args[i], &nm);
                if (d > best) { best = d; best_name = nm; }
            }
        } else {
            /* An ordinary call whose result may carry one of its own
             * arguments' address. A resolved user function keeps its own
             * precise, computed returns_param_addr summary (call_result_
             * origin — pointer_origin_of already returns the same thing for
             * a bare `p = f(...)`). Anything without a FuncSig — a stdlib
             * module call (arrays.get_first, maps.get_values, ...) or a
             * builtin (copy()) — falls back to the conservative
             * arguments-based guess instead, since none of those get an
             * escape summary to consult. Recorded here too so a multi-return
             * temp's per-slot reads see it: `p, n = f(...)` desugars to
             * `_tmp = f(...); p = _tmp.v0; n = _tmp.v1`, and the second
             * statement's value is a NODE_MEMBER_EXPR whose origin lookup
             * (pointer_origin_of's NODE_MEMBER_EXPR case) consults field_
             * origin_depth — this function's result — not origin_depth,
             * which only the first statement's plain call expression sets. */
            best = call_result_origin(checker, node, &best_name);
            if (!best && !resolve_call_sig(checker, node))
                best = stdlib_call_arg_origin(checker, node, &best_name);
        }
        break;
    default:
        return 0;
    }
    if (best) *out_name = best_name;
    return best;
}

/* The name the programmer wrote, never the module-prefixed internal one.
 * FuncSig.name is the flattened lookup key (e.g. "mod_size" for `size`
 * imported from module `mod`); the user-facing name lives on the decl.
 * Diagnostics and namespace-collision checks must use this, not .name.
 *
 * The declaration node's own .name is the name as written — merging an
 * import no longer rewrites it, so original_name stays NULL and is only a
 * fallback for whatever still sets it. Reading .name is what keeps a
 * mangled key out of user-facing text. */
static const char *func_display_name(const FuncSig *fs) {
    if (fs && fs->decl && fs->decl->kind == NODE_FUNC_DECL) {
        return FUNC_DISPLAY_NAME(fs->decl);
    }
    return fs ? fs->name : "";
}

/* E3163 ("a helper's summary escapes one of its own parameters" — into a
 * global, another parameter's reachable storage, or a container sink) plus
 * the @mem arena-lifecycle propagation, for a call to `csig` against `node`'s
 * current arguments. `reported_arg` is the bitmask of argument positions an
 * earlier direct addr()/raw()-alongside-outer-arg heuristic already reported,
 * so this does not double-report them.
 *
 * A shared function rather than inline in resolve_call_expr because instance
 * dispatch (s.stash(addr(y))) needs it run a second time, later: at the point
 * resolve_call_expr's own copy of this check runs, resolve_call_sig() cannot
 * find a FuncSig for the call — the callee is still spelled with the instance
 * as the member-expr's object, not the struct's type name, and self has not
 * yet been prepended into args[0]. That rewrite (retarget_member_object() +
 * the self-arg splice) happens later in the same function, inside
 * resolve_struct_or_module_call(). Without a second call from there, every
 * escape/@mem check silently no-ops for instance-dispatch struct functions —
 * the language's most common calling convention for the struct-function
 * feature. (Static dispatch, Struct.func(self: s, ...), needs no second call:
 * the object already names the struct and self is already an explicit
 * argument, so the first, early check already sees the right shape.) */
static void apply_call_param_escape_and_mem_effects(TypeChecker *checker,
    AstNode *node, FuncSig *csig, unsigned long long reported_arg) {
    if (!csig) return;
    int argc = node->data.call.arg_count;
    ensure_escape_summary(checker, csig);
    for (int a = 0; a < argc && a < csig->param_count && a < 64; a++) {
        signed char pe = csig->param_escape_into[a];
        if (pe == PARAM_ESCAPE_NONE) continue;
        if ((reported_arg >> a) & 1) continue;
        AstNode *arg = node->data.call.args[a];
        const char *onm = NULL;
        int od = expression_origin(checker, arg, &onm);
        if (od <= 0) continue;
        int sink_depth;
        const char *sink_name;
        if (pe == PARAM_ESCAPE_GLOBAL) {
            sink_depth = 0;
            sink_name = onm;
        } else {
            const char *droot = (pe < argc)
                ? assignment_target_root_name(node->data.call.args[pe])
                : NULL;
            sink_depth = droot
                ? symbol_scope_depth(checker->current_scope, droot) : 0;
            sink_name = droot ? droot : onm;
        }
        if (od > sink_depth)
            diagnostic_error_code_formatted(checker->diag, "E3163",
                NODE_FILE(checker, arg), arg->token.line,
                arg->token.column, 0, sink_name, onm);
    }
    /* Pointer checker: a call to a helper that destroys/resets one of its own
     * @mem arena parameters — the parameter itself, or a field of it
     * (csig->mem_param_field) — applies that same effect to the caller's
     * arena state here, at the call site — closing the "across a function
     * call" gap in E3164/E3165/E3166. Only an argument pc_arena_path_key()
     * can name (a bare variable, or a field-access chain) is traced;
     * anything else is left alone. */
    pc_ensure_mem_summary(checker, csig);
    unsigned long long mem_effect =
        csig->destroys_param_arena | csig->resets_param_arena;
    for (int a = 0; a < argc && a < csig->param_count && a < 64 && mem_effect; a++) {
        if (!(mem_effect & (1ull << a))) continue;
        const char *key = pc_mem_forward_key(checker, node->data.call.args[a],
                                             csig->mem_param_field[a]);
        if (!key) continue;
        bool destroys = (csig->destroys_param_arena & (1ull << a)) != 0;
        pc_apply_arena_lifecycle(checker, key, destroys,
                                 node, func_display_name(csig));
    }
}

/* --- #deprecated warning helpers ---
 * Fire W3007 at every genuine reference site (not internal lookup/diagnostic
 * helper calls). Each caller is responsible for only calling these from a
 * spot that represents a real user reference — see call sites. */

static void warn_deprecated(TypeChecker *checker, AstNode *node, const char *kind,
    const char *display_name, const char *message) {
    char *msg = message
        ? typechecker_format(checker, "%s '%s' is deprecated: %s", kind, display_name, message)
        : typechecker_format(checker, "%s '%s' is deprecated", kind, display_name);
    diagnostic_warning_message(checker->diag, "W3007", msg,
        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
}

/* Exempt a deprecated function's own recursive calls to itself (pointer
 * identity on FuncSig.decl, not name — struct functions are registered
 * under a prefixed lookup key that never matches the plain AST name). */
static void warn_if_func_deprecated(TypeChecker *checker, AstNode *node, FuncSig *sig) {
    if (!sig || !sig->is_deprecated) return;
    if (checker->current_func_decl && sig->decl == checker->current_func_decl) return;
    /* func_display_name() only strips module-import prefixes via
     * original_name; struct-scoped functions are registered under a
     * "StructName_func" key with no original_name set, so it would
     * leak the prefixed key. FUNC_DISPLAY_NAME reads the AST node's
     * own (never-prefixed) .name field instead. */
    const char *display = sig->decl ? FUNC_DISPLAY_NAME(sig->decl) : func_display_name(sig);
    warn_deprecated(checker, node, "function", display, sig->deprecated_message);
}

/* E5047: a #test function is an entry point for `gray test`; a normal build
 * strips it, so any call or reference from other code would dangle. Called
 * from every path that resolves a bare/qualified function to a FuncSig. */
static void reject_test_fn_reference(TypeChecker *checker, AstNode *node, FuncSig *sig) {
    if (!sig || !sig->decl || sig->decl->kind != NODE_FUNC_DECL) return;
    if (!sig->decl->data.func_decl.is_test) return;
    diagnostic_error_code_formatted(checker->diag, "E5047",
        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
        func_display_name(sig));
}

/* Exempt references to a deprecated struct's own type from within its own
 * struct-functions (same mechanism as the existing private-field-access
 * check: checker->current_struct_name, set/restored around struct-function
 * body typechecking). Does NOT cascade to that struct's struct-function
 * calls — those are independent and only warn if separately deprecated. */
static void warn_if_struct_deprecated(TypeChecker *checker, AstNode *node, StructInfo *si) {
    if (!si || !si->is_deprecated) return;
    if (checker->current_struct_name && strcmp(checker->current_struct_name, si->struct_name) == 0) return;
    warn_deprecated(checker, node, "struct", si->display_name, si->deprecated_message);
}

/* Enums have no executable body that could reference themselves, so no
 * self-reference exemption is needed here. */
static void warn_if_enum_deprecated(TypeChecker *checker, AstNode *node, int enum_index) {
    if (enum_index < 0 || !checker->enum_is_deprecated[enum_index]) return;
    warn_deprecated(checker, node, "enum", checker->enum_display_names[enum_index],
        checker->enum_deprecated_messages[enum_index]);
}

/* Check a declared type-name string (var-decl / param / return / field
 * annotation) against the struct and enum registries and warn if it
 * names a deprecated one. Bare names only — [Old]/^Old wrappers are not
 * unwrapped, matching the scope of this pass.
 *
 * self_struct_name: pass the enclosing struct's own name when checking a
 * struct-scoped function's param/return types, so a deprecated struct's
 * own `self StructName` parameter is exempt the same way its body is
 * (checker->current_struct_name isn't set yet at signature-registration
 * time, so warn_if_struct_deprecated's own self-check can't catch this —
 * it only guards references from inside an already-typechecked body). */
static void warn_if_type_name_deprecated(TypeChecker *checker, AstNode *node, const char *type_name,
    const char *self_struct_name) {
    if (!type_name) return;
    if (self_struct_name && strcmp(type_name, self_struct_name) == 0) return;
    StructInfo *tn_si = find_struct(checker, type_name);
    if (tn_si) {
        warn_if_struct_deprecated(checker, node, tn_si);
        return;
    }
    int tn_ei = find_enum_index(checker, type_name);
    if (tn_ei >= 0) warn_if_enum_deprecated(checker, node, tn_ei);
}

/* --- Stdlib argument kind validation --- */

typedef enum {
    ARG_STRING, ARG_INT, ARG_FLOAT, ARG_BOOL, ARG_ARRAY, ARG_MAP, ARG_ANY, ARG_NUMBER, ARG_CHAR, ARG_CHANNEL,
    ARG_BUILDER,
    /* A type name, not a value. Declaring the position here is what keeps
     * it out of value resolution — see arg_is_type_position(). */
    ARG_TYPE
} ExpectedArgKind;

static bool arg_kind_matches(ExpectedArgKind expected, GrayType *actual) {
    if (!actual || actual->kind == TK_UNKNOWN) return true; /* can't validate */
    switch (expected) {
    case ARG_STRING: return actual->kind == TK_STRING;
    case ARG_INT:    return actual->kind == TK_INT || actual->kind == TK_UINT ||
                            actual->kind == TK_BYTE;
    case ARG_FLOAT:  return actual->kind == TK_FLOAT;
    case ARG_BOOL:   return actual->kind == TK_BOOL;
    case ARG_ARRAY:  return actual->kind == TK_ARRAY;
    case ARG_MAP:    return actual->kind == TK_MAP;
    case ARG_ANY:    return true;
    case ARG_NUMBER: return actual->kind == TK_INT || actual->kind == TK_UINT ||
                            actual->kind == TK_BYTE || actual->kind == TK_FLOAT;
    case ARG_CHAR:   return actual->kind == TK_CHAR;
    case ARG_CHANNEL: return actual->kind == TK_STRUCT &&
                             actual->name && strcmp(actual->name, "Channel") == 0;
    case ARG_BUILDER: return actual->kind == TK_STRUCT &&
                             actual->name && strcmp(actual->name, "Builder") == 0;
    /* Validated by name, never by resolved type — the argument is a type
     * name and is never resolved as a value. */
    case ARG_TYPE:   return true;
    }
    return true;
}

static const char *expected_kind_name(ExpectedArgKind kind) {
    switch (kind) {
    case ARG_STRING: return "string";
    case ARG_INT:    return "int";
    case ARG_FLOAT:  return "float";
    case ARG_BOOL:   return "bool";
    case ARG_ARRAY:  return "array";
    case ARG_MAP:    return "map";
    case ARG_ANY:    return "any";
    case ARG_NUMBER: return "number";
    case ARG_CHAR:   return "char";
    case ARG_CHANNEL: return "Channel";
    case ARG_BUILDER: return "Builder";
    case ARG_TYPE:   return "a type name";
    }
    return "unknown";
}

/* --- Unified stdlib function metadata registry ---
 * Single table that consolidates fallibility, arg count range, and
 * per-argument type expectations for every stdlib function. */

typedef enum {
    FT_NONE = -1,
    FT_BOOL, FT_INT, FT_UINT, FT_FLOAT, FT_STRING,
    FT_ARRAY_STRING, FT_NESTED_ARRAY_STRING, FT_ARRAY_BYTE, FT_ARRAY_MAP,
    FT_STRUCT_DATABASE, FT_STRUCT_SOCKET, FT_STRUCT_LISTENER,
    FT_STRUCT_HTTP_RESPONSE, FT_STRUCT_MAP,
} FallibleType;

#define STDLIB_MAX_ARG_CHECKS 4

typedef struct {
    const char *mod;
    const char *fn;
    int min_args;
    int max_args;
    bool fallible;
    FallibleType success_type;
    int arg_type_count;
    struct { int index; ExpectedArgKind kind; } arg_types[STDLIB_MAX_ARG_CHECKS];
    const char *return_type;  /* type name string, or NULL for context-dependent */
} StdlibFuncMeta;

static const StdlibFuncMeta stdlib_func_meta[] = {
    /* arrays */
    {"arrays", "all",          2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "bool"},
    {"arrays", "any",          2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "bool"},
    {"arrays", "append",       2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "clear",        1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "concat",       2, 2, false, FT_NONE, 2, {{0, ARG_ARRAY}, {1, ARG_ARRAY}}, NULL},
    {"arrays", "contains",     2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "bool"},
    {"arrays", "count",        2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"arrays", "deduplicate",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "fill",         3, 3, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "filter",       2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "flatten",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "get_first",    1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "get_last",     1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "get_max",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"arrays", "get_min",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"arrays", "get_sum",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"arrays", "index_of",     2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"arrays", "insert_at",    3, 3, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "is_empty",     1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "bool"},
    {"arrays", "is_equal",     2, 2, false, FT_NONE, 2, {{0, ARG_ARRAY}, {1, ARG_ARRAY}}, "bool"},
    {"arrays", "map",          2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "pair",         2, 2, false, FT_NONE, 2, {{0, ARG_ARRAY}, {1, ARG_ARRAY}}, "[[int]]"},
    {"arrays", "prepend",      2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "reduce",       3, 3, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "remove",       2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "remove_at",    2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "remove_first", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "remove_last",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "reverse",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "slice",        3, 3, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"arrays", "sort_asc",     1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "sort_desc",    1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "void"},
    {"arrays", "split_every",  2, 2, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "[[int]]"},
    /* atomic */
    {"atomic", "add",              2, 2, false, FT_NONE, 0, {{0}},"int"},
    {"atomic", "and",              2, 2, false, FT_NONE, 0, {{0}},"int"},
    {"atomic", "cas",              3, 3, false, FT_NONE, 0, {{0}},"bool"},
    {"atomic", "exchange",         2, 2, false, FT_NONE, 0, {{0}},"int"},
    {"atomic", "fence",            0, 0, false, FT_NONE, 0, {{0}},"void"},
    {"atomic", "load",             1, 1, false, FT_NONE, 0, {{0}},"int"},
    {"atomic", "or",               2, 2, false, FT_NONE, 0, {{0}},"int"},
    {"atomic", "spin_lock",        1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"atomic", "spin_trylock",     1, 1, false, FT_NONE, 0, {{0}},"bool"},
    {"atomic", "spin_unlock",      1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"atomic", "spinlock",         0, 0, false, FT_NONE, 0, {{0}},"SpinLock"},
    {"atomic", "spinlock_destroy", 1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"atomic", "store",            2, 2, false, FT_NONE, 0, {{0}},"void"},
    {"atomic", "sub",              2, 2, false, FT_NONE, 0, {{0}},"int"},
    {"atomic", "xor",              2, 2, false, FT_NONE, 0, {{0}},"int"},
    /* binary */
    {"binary", "decode_f32_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "float"},
    {"binary", "decode_f32_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "float"},
    {"binary", "decode_f64_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "float"},
    {"binary", "decode_f64_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "float"},
    {"binary", "decode_i128_be", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "i128"},
    {"binary", "decode_i128_le", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "i128"},
    {"binary", "decode_i16_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_i16_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_i256_be", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "i256"},
    {"binary", "decode_i256_le", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "i256"},
    {"binary", "decode_i32_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_i32_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_i64_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_i64_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_i8",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u128_be", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "u128"},
    {"binary", "decode_u128_le", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "u128"},
    {"binary", "decode_u16_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u16_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u256_be", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "u256"},
    {"binary", "decode_u256_le", 1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "u256"},
    {"binary", "decode_u32_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u32_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u64_be",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u64_le",  1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "decode_u8",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "int"},
    {"binary", "encode_f32_be",  1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "[byte]"},
    {"binary", "encode_f32_le",  1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "[byte]"},
    {"binary", "encode_f64_be",  1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "[byte]"},
    {"binary", "encode_f64_le",  1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "[byte]"},
    {"binary", "encode_i128_be", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i128_le", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i16_be",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i16_le",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i256_be", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i256_le", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i32_be",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i32_le",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i64_be",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i64_le",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_i8",      1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u128_be", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u128_le", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u16_be",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u16_le",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u256_be", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u256_le", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u32_be",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u32_le",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u64_be",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u64_le",  1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    {"binary", "encode_u8",      1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "[byte]"},
    /* encoding — byte conversion (formerly @bytes) */
    {"encoding", "from_base64", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "[byte]"},
    {"encoding", "from_hex",    1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "[byte]"},
    {"encoding", "from_string", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "[byte]"},
    {"encoding", "to_base64",   1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "string"},
    {"encoding", "to_hex",      1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "string"},
    {"encoding", "to_string",   1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "string"},
    /* channels */
    {"channels", "close",       1, 1, false, FT_NONE, 1, {{0, ARG_CHANNEL}}, "void"},
    {"channels", "open",        1, 1, false, FT_NONE, 0, {{0}},"Channel"},
    {"channels", "receive",     1, 1, false, FT_NONE, 1, {{0, ARG_CHANNEL}}, "int"},
    {"channels", "send",        2, 2, false, FT_NONE, 2, {{0, ARG_CHANNEL}, {1, ARG_INT}}, "void"},
    {"channels", "try_receive", 1, 1, false, FT_NONE, 1, {{0, ARG_CHANNEL}}, "int"},
    {"channels", "try_send",    2, 2, false, FT_NONE, 2, {{0, ARG_CHANNEL}, {1, ARG_INT}}, "bool"},
    /* chars */
    {"chars", "to_lower", 1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "char"},
    {"chars", "to_upper", 1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "char"},
    /* crypto */
    {"crypto", "md5",        1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"crypto", "random_hex", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"crypto", "sha256",     1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    /* csv */
    {"csv", "encode",     1, 1, false, FT_NONE,                1, {{0, ARG_ARRAY}}, "string"},
    {"csv", "headers",    1, 1, false, FT_NONE,                1, {{0, ARG_ARRAY}}, "[string]"},
    {"csv", "parse",      1, 1, false, FT_NONE,                1, {{0, ARG_STRING}}, "[[string]]"},
    {"csv", "read_file",  1, 1, true,  FT_NESTED_ARRAY_STRING, 1, {{0, ARG_STRING}}, "[[string]]"},
    {"csv", "write_file", 2, 2, true,  FT_BOOL,                2, {{0, ARG_STRING}, {1, ARG_ARRAY}}, "bool"},
    /* encoding */
    {"encoding", "base64_decode", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"encoding", "base64_encode", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"encoding", "hex_decode",    1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"encoding", "hex_encode",    1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"encoding", "url_decode",    1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"encoding", "url_encode",    1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    /* fmt */
    {"fmt", "center",        3, 3,  false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_INT}, {2, ARG_CHAR}}, "string"},
    {"fmt", "eprintf",       1, 99, false, FT_NONE, 1, {{0, ARG_STRING}}, "void"},
    {"fmt", "eprintfln",     1, 99, false, FT_NONE, 1, {{0, ARG_STRING}}, "void"},
    {"fmt", "float_fixed",   2, 2,  false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_INT}}, "string"},
    {"fmt", "float_sci",     1, 1,  false, FT_NONE, 1, {{0, ARG_NUMBER}}, "string"},
    {"fmt", "int_to_binary", 1, 1,  false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"fmt", "int_to_hex",    1, 1,  false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"fmt", "int_to_octal",  1, 1,  false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"fmt", "pad_left",      3, 3,  false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_INT}, {2, ARG_CHAR}}, "string"},
    {"fmt", "pad_right",     3, 3,  false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_INT}, {2, ARG_CHAR}}, "string"},
    {"fmt", "printf",        1, 99, false, FT_NONE, 1, {{0, ARG_STRING}}, "void"},
    {"fmt", "printfln",      1, 99, false, FT_NONE, 1, {{0, ARG_STRING}}, "void"},
    {"fmt", "sprintf",       1, 99, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"fmt", "sprintfln",     1, 99, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    /* http */
    {"http", "delete", 2, 2, true, FT_STRUCT_HTTP_RESPONSE, 2, {{0, ARG_STRING}, {1, ARG_MAP}}, "HttpResponse"},
    {"http", "get",    2, 2, true, FT_STRUCT_HTTP_RESPONSE, 2, {{0, ARG_STRING}, {1, ARG_MAP}}, "HttpResponse"},
    {"http", "head",   2, 2, true, FT_STRUCT_HTTP_RESPONSE, 2, {{0, ARG_STRING}, {1, ARG_MAP}}, "HttpResponse"},
    {"http", "patch",  3, 3, true, FT_STRUCT_HTTP_RESPONSE, 3, {{0, ARG_STRING}, {1, ARG_STRING}, {2, ARG_MAP}}, "HttpResponse"},
    {"http", "post",   3, 3, true, FT_STRUCT_HTTP_RESPONSE, 3, {{0, ARG_STRING}, {1, ARG_STRING}, {2, ARG_MAP}}, "HttpResponse"},
    {"http", "put",    3, 3, true, FT_STRUCT_HTTP_RESPONSE, 3, {{0, ARG_STRING}, {1, ARG_STRING}, {2, ARG_MAP}}, "HttpResponse"},
    /* io */
    {"io", "append_bytes",   2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_ARRAY}}, "bool"},
    {"io", "append_file",    2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"io", "basename",       1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "string"},
    {"io", "copy_file",      2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"io", "delete_file",    1, 1, true,  FT_BOOL,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "dirname",        1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "string"},
    {"io", "extension",      1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "string"},
    {"io", "file_exists",    1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "file_size",      1, 1, true,  FT_INT,          1, {{0, ARG_STRING}}, "int"},
    {"io", "glob",           1, 1, true,  FT_ARRAY_STRING, 1, {{0, ARG_STRING}}, "[string]"},
    {"io", "is_absolute",    1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "is_directory",   1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "is_file",        1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "list_dir",       1, 1, true,  FT_ARRAY_STRING, 1, {{0, ARG_STRING}}, "[string]"},
    {"io", "make_dir",       1, 1, true,  FT_BOOL,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "make_dir_all",   1, 1, true,  FT_BOOL,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "move_file",      2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"io", "normalize",      1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "string"},
    {"io", "path_join",      1, 1, false, FT_NONE,         1, {{0, ARG_ARRAY}}, "string"},
    {"io", "read_bytes",     1, 1, true,  FT_ARRAY_BYTE,   1, {{0, ARG_STRING}}, "[byte]"},
    {"io", "read_file",      1, 1, true,  FT_STRING,       1, {{0, ARG_STRING}}, "string"},
    {"io", "read_lines",     1, 2, true,  FT_ARRAY_STRING, 2, {{0, ARG_STRING}, {1, ARG_INT}}, "[string]"},
    {"io", "remove_dir",     1, 1, true,  FT_BOOL,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "remove_dir_all", 1, 1, true,  FT_BOOL,         1, {{0, ARG_STRING}}, "bool"},
    {"io", "rename_file",    2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"io", "temp_dir",       0, 0, true,  FT_STRING,       0, {{0}},"string"},
    {"io", "temp_file",      0, 0, true,  FT_STRING,       0, {{0}},"string"},
    {"io", "walk",           1, 1, true,  FT_ARRAY_STRING, 1, {{0, ARG_STRING}}, "[string]"},
    {"io", "write_bytes",    2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_ARRAY}}, "bool"},
    {"io", "write_file",     2, 2, true,  FT_BOOL,         2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    /* json */
    {"json", "decode",       1, 1, true,  FT_STRUCT_MAP, 1, {{0, ARG_STRING}}, "map[string:string]"},
    {"json", "encode",       1, 1, false, FT_NONE,       0, {{0}},"string"},
    {"json", "is_valid",     1, 1, false, FT_NONE,       1, {{0, ARG_STRING}}, "bool"},
    {"json", "parse",        1, 1, false, FT_NONE,       1, {{0, ARG_STRING}}, NULL},
    {"json", "pretty_print", 2, 2, false, FT_NONE,       2, {{0, ARG_MAP}, {1, ARG_INT}}, "string"},
    {"json", "stringify",    1, 1, false, FT_NONE,       0, {{0}},"string"},
    /* maps */
    {"maps", "clear",          1, 1, false, FT_NONE, 1, {{0, ARG_MAP}}, "void"},
    {"maps", "contains_value", 2, 2, false, FT_NONE, 1, {{0, ARG_MAP}}, "bool"},
    {"maps", "get_keys",       1, 1, false, FT_NONE, 1, {{0, ARG_MAP}}, NULL},
    {"maps", "get_or_default", 3, 3, false, FT_NONE, 1, {{0, ARG_MAP}}, NULL},
    {"maps", "get_values",     1, 1, false, FT_NONE, 1, {{0, ARG_MAP}}, NULL},
    {"maps", "has_key",        2, 2, false, FT_NONE, 1, {{0, ARG_MAP}}, "bool"},
    {"maps", "is_empty",       1, 1, false, FT_NONE, 1, {{0, ARG_MAP}}, "bool"},
    {"maps", "is_equal",       2, 2, false, FT_NONE, 2, {{0, ARG_MAP}, {1, ARG_MAP}}, "bool"},
    {"maps", "merge",          2, 2, false, FT_NONE, 2, {{0, ARG_MAP}, {1, ARG_MAP}}, NULL},
    {"maps", "remove_key",     2, 2, false, FT_NONE, 1, {{0, ARG_MAP}}, "void"},
    /* math */
    {"math", "abs",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, NULL},
    {"math", "acos",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "asin",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "atan",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "atan2",       2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"math", "cbrt",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "ceil",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "clamp",       3, 3, false, FT_NONE, 3, {{0, ARG_NUMBER}, {1, ARG_NUMBER}, {2, ARG_NUMBER}}, NULL},
    {"math", "copysign",    2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"math", "cos",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "cosh",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "deg_to_rad",  1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "distance",    4, 4, false, FT_NONE, 4, {{0, ARG_NUMBER}, {1, ARG_NUMBER}, {2, ARG_NUMBER}, {3, ARG_NUMBER}}, "float"},
    {"math", "exp",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "exp2",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "factorial",   1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"math", "floor",       1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "fma",         3, 3, false, FT_NONE, 3, {{0, ARG_NUMBER}, {1, ARG_NUMBER}, {2, ARG_NUMBER}}, "float"},
    {"math", "gcd",         2, 2, false, FT_NONE, 2, {{0, ARG_INT}, {1, ARG_INT}}, "int"},
    {"math", "hypot",       2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"math", "is_even",     1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "bool"},
    {"math", "is_finite",   1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "bool"},
    {"math", "is_infinite", 1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "bool"},
    {"math", "is_nan",      1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "bool"},
    {"math", "is_odd",      1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "bool"},
    {"math", "is_power_of_two", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "bool"},
    {"math", "is_prime",    1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "bool"},
    {"math", "lcm",         2, 2, false, FT_NONE, 2, {{0, ARG_INT}, {1, ARG_INT}}, "int"},
    {"math", "lerp",        3, 3, false, FT_NONE, 3, {{0, ARG_NUMBER}, {1, ARG_NUMBER}, {2, ARG_NUMBER}}, "float"},
    {"math", "log",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "log10",       1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "log2",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "log_base",    2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"math", "max",         2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, NULL},
    {"math", "min",         2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, NULL},
    {"math", "mod",         2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"math", "modf",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "neg",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, NULL},
    {"math", "next_power_of_two", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"math", "pow",         2, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"math", "rad_to_deg",  1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "round",       1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "sign",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "int"},
    {"math", "sin",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "sinh",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "sqrt",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "tan",         1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "tanh",        1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    {"math", "trunc",       1, 1, false, FT_NONE, 1, {{0, ARG_NUMBER}}, "float"},
    /* mem */
    {"mem", "alloc",    2, 2, false, FT_NONE, 0, {{0}},NULL},
    {"mem", "arena",    1, 1, false, FT_NONE, 0, {{0}},"Arena"},
    {"mem", "destroy",  1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"mem", "fill",     3, 3, false, FT_NONE, 0, {{0}},"void"},
    {"mem", "init",     2, 2, false, FT_NONE, 1, {{1, ARG_TYPE}},NULL},
    {"mem", "raw_copy", 3, 3, false, FT_NONE, 0, {{0}},"void"},
    {"mem", "reset",    1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"mem", "usage",    1, 1, false, FT_NONE, 0, {{0}},"int"},
    {"mem", "zero",     2, 2, false, FT_NONE, 0, {{0}},"void"},
    /* net */
    {"net", "accept",      1, 1, true,  FT_STRUCT_SOCKET,   0, {{0}},"Socket"},
    {"net", "close",       1, 1, false, FT_NONE,            0, {{0}},"void"},
    {"net", "connect",     2, 2, true,  FT_STRUCT_SOCKET,   2, {{0, ARG_STRING}, {1, ARG_INT}}, "Socket"},
    {"net", "listen",      1, 2, true,  FT_STRUCT_LISTENER, 1, {{1, ARG_INT}}, "Listener"},
    {"net", "receive",     2, 2, true,  FT_STRING,          1, {{1, ARG_INT}}, "string"},
    {"net", "resolve",     1, 1, true,  FT_STRING,          1, {{0, ARG_STRING}}, "string"},
    {"net", "send",        2, 2, true,  FT_INT,             1, {{1, ARG_STRING}}, "int"},
    {"net", "set_timeout", 2, 2, false, FT_NONE,            1, {{1, ARG_INT}}, "void"},
    /* os */
    {"os", "arch",        0, 0, false, FT_NONE, 0, {{0}},"string"},
    {"os", "args",        0, 0, false, FT_NONE, 0, {{0}},"[string]"},
    {"os", "current_dir", 0, 0, false, FT_NONE, 0, {{0}},"string"},
    {"os", "current_os",  0, 0, false, FT_NONE, 0, {{0}},"Platform"},
    {"os", "exec",        2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_ARRAY}}, "bool"},
    {"os", "get_env",     1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"os", "hostname",    0, 0, false, FT_NONE, 0, {{0}},"string"},
    {"os", "pid",         0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"os", "set_env",     2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "void"},
    {"os", "unset_env",   1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "void"},
    /* random */
    {"random", "choice",     1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    {"random", "rand_bool",  0, 0, false, FT_NONE, 0, {{0}},"bool"},
    {"random", "rand_byte",  0, 0, false, FT_NONE, 0, {{0}},"byte"},
    {"random", "rand_char",  0, 2, false, FT_NONE, 2, {{0, ARG_CHAR}, {1, ARG_CHAR}}, "char"},
    {"random", "rand_float", 0, 2, false, FT_NONE, 2, {{0, ARG_NUMBER}, {1, ARG_NUMBER}}, "float"},
    {"random", "rand_int",   1, 2, false, FT_NONE, 2, {{0, ARG_INT}, {1, ARG_INT}}, "int"},
    {"random", "sample",     2, 2, false, FT_NONE, 2, {{0, ARG_ARRAY}, {1, ARG_INT}}, NULL},
    {"random", "seed",       1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "void"},
    {"random", "shuffle",    1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, NULL},
    /* regex */
    {"regex", "find",     2, 2, true,  FT_STRING,       2, {{0, ARG_STRING}, {1, ARG_STRING}}, "string"},
    {"regex", "find_all", 2, 2, true,  FT_ARRAY_STRING, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "[string]"},
    {"regex", "is_match", 2, 2, false, FT_NONE,         2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"regex", "is_valid", 1, 1, false, FT_NONE,         1, {{0, ARG_STRING}}, "bool"},
    {"regex", "replace",  3, 3, true,  FT_STRING,       3, {{0, ARG_STRING}, {1, ARG_STRING}, {2, ARG_STRING}}, "string"},
    {"regex", "split",    2, 2, true,  FT_ARRAY_STRING, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "[string]"},
    /* runtime */
    {"runtime", "alloc_count",  0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "arena_blocks", 0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "arena_limit",  0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "arena_usage",  0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "call_depth",   0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "call_limit",   0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "heap_blocks",  0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "heap_usage",   0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "peak_usage",   0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "total_usage",  0, 0, false, FT_NONE, 0, {{0}}, "int"},
    {"runtime", "uptime",       0, 0, false, FT_NONE, 0, {{0}}, "float"},
    {"runtime", "version",      0, 0, false, FT_NONE, 0, {{0}}, "string"},
    /* server */
    {"server", "add_route",  4, 4, false, FT_NONE, 0, {{0}},"void"},
    {"server", "add_router", 0, 0, false, FT_NONE, 0, {{0}},"Router"},
    {"server", "cors",       2, 2, false, FT_NONE, 0, {{0}},"void"},
    {"server", "html",       2, 2, false, FT_NONE, 1, {{1, ARG_STRING}}, "HttpResponse"},
    {"server", "json",       2, 2, false, FT_NONE, 1, {{1, ARG_STRING}}, "HttpResponse"},
    {"server", "listen",     2, 3, false, FT_NONE, 2, {{1, ARG_INT}, {2, ARG_STRING}}, "void"},
    {"server", "redirect",   2, 2, false, FT_NONE, 1, {{1, ARG_STRING}}, "HttpResponse"},
    {"server", "text",       2, 2, false, FT_NONE, 1, {{1, ARG_STRING}}, "HttpResponse"},
    {"server", "use",        2, 2, false, FT_NONE, 0, {{0}},"void"},
    /* sqlite */
    {"sqlite", "close",        1, 1,  false, FT_NONE,            0, {{0}},"void"},
    {"sqlite", "exec",         2, 99, true,  FT_BOOL,            0, {{0}},"bool"},
    {"sqlite", "exec_params",  3, 3,  true,  FT_BOOL,            1, {{2, ARG_ARRAY}}, "bool"},
    {"sqlite", "open",         1, 1,  true,  FT_STRUCT_DATABASE,  1, {{0, ARG_STRING}}, "Database"},
    {"sqlite", "query",        2, 99, true,  FT_ARRAY_MAP,       0, {{0}},"[map[string:string]]"},
    {"sqlite", "query_params", 3, 3,  true,  FT_ARRAY_MAP,       1, {{2, ARG_ARRAY}}, "[map[string:string]]"},
    /* strconv */
    {"strconv", "format_int",  2, 2, false, FT_NONE,  2, {{0, ARG_INT}, {1, ARG_INT}}, "string"},
    {"strconv", "format_uint", 2, 2, false, FT_NONE,  2, {{0, ARG_INT}, {1, ARG_INT}}, "string"},
    {"strconv", "from_bool",  1, 1, false, FT_NONE,  1, {{0, ARG_BOOL}}, "string"},
    {"strconv", "from_float", 1, 1, false, FT_NONE,  1, {{0, ARG_FLOAT}}, "string"},
    {"strconv", "from_int",   1, 1, false, FT_NONE,  1, {{0, ARG_INT}}, "string"},
    {"strconv", "from_uint",  1, 1, false, FT_NONE,  1, {{0, ARG_INT}}, "string"},
    {"strconv", "is_integer", 1, 1, false, FT_NONE,  1, {{0, ARG_STRING}}, "bool"},
    {"strconv", "is_numeric", 1, 1, false, FT_NONE,  1, {{0, ARG_STRING}}, "bool"},
    {"strconv", "quote",      1, 1, false, FT_NONE,   1, {{0, ARG_STRING}}, "string"},
    {"strconv", "to_bool",    1, 1, true,  FT_BOOL,  1, {{0, ARG_STRING}}, "bool"},
    {"strconv", "to_float",   1, 1, true,  FT_FLOAT, 1, {{0, ARG_STRING}}, "float"},
    {"strconv", "to_int",     1, 2, true,  FT_INT,   2, {{0, ARG_STRING}, {1, ARG_INT}}, "int"},
    {"strconv", "to_uint",    1, 2, true,  FT_UINT,  2, {{0, ARG_STRING}, {1, ARG_INT}}, "uint"},
    {"strconv", "unquote",    1, 1, true,  FT_STRING, 1, {{0, ARG_STRING}}, "string"},
    /* strings */
    {"strings", "append_char",   2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_CHAR}}, "string"},
    {"strings", "build",              1, 1, false, FT_NONE, 1, {{0, ARG_BUILDER}}, "string"},
    {"strings", "builder",            0, 0, false, FT_NONE, 0, {{0}}, "Builder"},
    {"strings", "builder_append",      2, 2, false, FT_NONE, 2, {{0, ARG_BUILDER}, {1, ARG_STRING}}, "void"},
    {"strings", "builder_append_bytes", 2, 2, false, FT_NONE, 2, {{0, ARG_BUILDER}, {1, ARG_ARRAY}}, "void"},
    {"strings", "builder_append_char", 2, 2, false, FT_NONE, 2, {{0, ARG_BUILDER}, {1, ARG_CHAR}}, "void"},
    {"strings", "builder_append_int",  2, 2, false, FT_NONE, 2, {{0, ARG_BUILDER}, {1, ARG_INT}}, "void"},
    {"strings", "builder_append_line", 2, 2, false, FT_NONE, 2, {{0, ARG_BUILDER}, {1, ARG_STRING}}, "void"},
    {"strings", "builder_clear",       1, 1, false, FT_NONE, 1, {{0, ARG_BUILDER}}, "void"},
    {"strings", "builder_len",         1, 1, false, FT_NONE, 1, {{0, ARG_BUILDER}}, "int"},
    {"strings", "builder_reserve",     2, 2, false, FT_NONE, 2, {{0, ARG_BUILDER}, {1, ARG_INT}}, "void"},
    {"strings", "char_at",       2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_INT}}, "char"},
    {"strings", "compare",       2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "int"},
    {"strings", "contains",      2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"strings", "contains_any",  2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"strings", "count",         2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "int"},
    {"strings", "ends_with",     2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"strings", "equal_fold",    2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"strings", "from_chars",    1, 1, false, FT_NONE, 1, {{0, ARG_ARRAY}}, "string"},
    {"strings", "index_of",      2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "int"},
    {"strings", "insert_char_at", 3, 3, false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_INT}, {2, ARG_CHAR}}, "string"},
    {"strings", "is_alnum",      1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "bool"},
    {"strings", "is_alpha",      1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "bool"},
    {"strings", "is_digit",      1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "bool"},
    {"strings", "is_empty",      1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "bool"},
    {"strings", "is_lower",      1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "bool"},
    {"strings", "is_upper",      1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "bool"},
    {"strings", "is_whitespace", 1, 1, false, FT_NONE, 1, {{0, ARG_CHAR}}, "bool"},
    {"strings", "join",          2, 2, false, FT_NONE, 2, {{0, ARG_ARRAY}, {1, ARG_STRING}}, "string"},
    {"strings", "last_index_of", 2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "int"},
    {"strings", "prepend_char",  2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_CHAR}}, "string"},
    {"strings", "remove_at",     2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_INT}}, "string"},
    {"strings", "remove_prefix", 2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "string"},
    {"strings", "remove_suffix", 2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "string"},
    {"strings", "repeat",        2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_INT}}, "string"},
    {"strings", "replace",       3, 3, false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_STRING}, {2, ARG_STRING}}, "string"},
    {"strings", "reverse",       1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "set_char_at",   3, 3, false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_INT}, {2, ARG_CHAR}}, "string"},
    {"strings", "slice",         3, 3, false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_INT}, {2, ARG_INT}}, "string"},
    {"strings", "split",         2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "[string]"},
    {"strings", "split_n",       3, 3, false, FT_NONE, 3, {{0, ARG_STRING}, {1, ARG_STRING}, {2, ARG_INT}}, "[string]"},
    {"strings", "split_whitespace", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "[string]"},
    {"strings", "starts_with",   2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_STRING}}, "bool"},
    {"strings", "to_camel_case", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "to_chars",      1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "[char]"},
    {"strings", "to_lower",      1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "to_snake_case", 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "to_title",      1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "to_upper",      1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "trim",          1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "trim_left",     1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    {"strings", "trim_right",    1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "string"},
    /* sync */
    {"sync", "destroy",  1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"sync", "lock",     1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"sync", "mutex",    0, 0, false, FT_NONE, 0, {{0}},"Mutex"},
    {"sync", "try_lock", 1, 1, false, FT_NONE, 0, {{0}},"bool"},
    {"sync", "unlock",   1, 1, false, FT_NONE, 0, {{0}},"void"},
    /* threads */
    {"threads", "detach",       1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"threads", "get_id",       0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"threads", "is_alive",     1, 1, false, FT_NONE, 0, {{0}},"bool"},
    {"threads", "join",         1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"threads", "sleep",        1, 1, false, FT_NONE, 0, {{0}},"void"},
    {"threads", "spawn",        1, 2, false, FT_NONE, 0, {{0}},"Thread"},
    {"threads", "spawn_arg",    2, 2, false, FT_NONE, 0, {{0}},"Thread"},
    {"threads", "thread_count", 0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"threads", "yield",        0, 0, false, FT_NONE, 0, {{0}},"void"},
    /* time */
    {"time", "date",       1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"time", "day",        1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "diff",       2, 2, false, FT_NONE, 2, {{0, ARG_INT}, {1, ARG_INT}}, "int"},
    {"time", "elapsed_ms", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "format",     2, 2, false, FT_NONE, 2, {{0, ARG_STRING}, {1, ARG_INT}}, "string"},
    {"time", "hour",       1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "is_leap_year", 1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "bool"},
    {"time", "minute",     1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "month",      1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "now",        0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"time", "now_ms",     0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"time", "now_ns",     0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"time", "parse",      2, 2, true,  FT_INT,  2, {{0, ARG_STRING}, {1, ARG_STRING}}, "int"},
    {"time", "second",     1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "since",      1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "tick",       0, 0, false, FT_NONE, 0, {{0}},"int"},
    {"time", "to_clock",   1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"time", "to_iso",     1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "string"},
    {"time", "weekday",    1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    {"time", "year",       1, 1, false, FT_NONE, 1, {{0, ARG_INT}}, "int"},
    /* uuid */
    {"uuid", "generate",              0, 0, false, FT_NONE, 0, {{0}},"UUID"},
    {"uuid", "generate_compact",      1, 1, false, FT_NONE, 0, {{0}},"string"},
    {"uuid", "generate_random",       0, 0, false, FT_NONE, 0, {{0}},"UUID"},
    {"uuid", "generate_time_ordered", 0, 0, false, FT_NONE, 0, {{0}},"UUID"},
    {"uuid", "is_valid",              1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "bool"},
    {"uuid", "parse",                 1, 1, false, FT_NONE, 1, {{0, ARG_STRING}}, "UUID"},
    {"uuid", "to_string",             1, 1, false, FT_NONE, 0, {{0}},"string"},
};

static int stdlib_meta_compare(const void *a, const void *b) {
    const StdlibFuncMeta *entry_a = *(const StdlibFuncMeta *const *)a;
    const StdlibFuncMeta *entry_b = *(const StdlibFuncMeta *const *)b;
    int r = strcmp(entry_a->mod, entry_b->mod);
    return r != 0 ? r : strcmp(entry_a->fn, entry_b->fn);
}

#define STDLIB_META_N (int)(sizeof(stdlib_func_meta) / sizeof(stdlib_func_meta[0]))
static const StdlibFuncMeta *stdlib_meta_sorted[STDLIB_META_N];

static const StdlibFuncMeta *find_stdlib_meta(const char *mod, const char *fn) {
    StdlibFuncMeta key = { .mod = mod, .fn = fn };
    const StdlibFuncMeta *key_ptr = &key;
    const StdlibFuncMeta **hit = bsearch(&key_ptr, stdlib_meta_sorted, STDLIB_META_N,
        sizeof(const StdlibFuncMeta *), stdlib_meta_compare);
    return hit ? *hit : NULL;
}

bool stdlib_has_func(const char *mod, const char *fn) {
    for (int i = 0; i < STDLIB_META_N; i++) {
        if (strcmp(stdlib_func_meta[i].mod, mod) == 0 &&
            strcmp(stdlib_func_meta[i].fn, fn) == 0)
            return true;
    }
    return false;
}

static GrayType *resolve_return_type(const char *rt) {
    if (!rt) return NULL;
    if (strcmp(rt, "void") == 0) return &TYPE_VOID;
    return type_from_name(rt);
}

/* Returns true if (mod, fn) is a fallible stdlib function.
 * Pass mod=NULL to check by fn name alone (any module). */
static bool typechecker_is_fallible_stdlib(const char *mod, const char *fn) {
    if (!mod) {
        for (int i = 0; i < STDLIB_META_N; i++) {
            if (stdlib_func_meta[i].fallible && strcmp(fn, stdlib_func_meta[i].fn) == 0)
                return true;
        }
        return false;
    }
    const StdlibFuncMeta *m = find_stdlib_meta(mod, fn);
    return m && m->fallible;
}

static GrayType *typechecker_get_fallible_stdlib_type(const char *mod, const char *fn) {
    const StdlibFuncMeta *m = find_stdlib_meta(mod, fn);
    if (!m || !m->fallible) return NULL;
    switch (m->success_type) {
    case FT_NONE:                return NULL;
    case FT_BOOL:                return &TYPE_BOOL;
    case FT_INT:                 return &TYPE_INT;
    case FT_UINT:                return &TYPE_UINT;
    case FT_FLOAT:               return &TYPE_FLOAT;
    case FT_STRING:              return &TYPE_STRING;
    case FT_ARRAY_STRING:        return type_array("string");
    case FT_NESTED_ARRAY_STRING: return type_array("[string]");
    case FT_ARRAY_BYTE:          return type_array("byte");
    case FT_ARRAY_MAP:           return type_array("map[string:string]");
    case FT_STRUCT_DATABASE:     return type_struct("Database");
    case FT_STRUCT_SOCKET:       return type_struct("Socket");
    case FT_STRUCT_LISTENER:     return type_struct("Listener");
    case FT_STRUCT_HTTP_RESPONSE:return type_struct("HttpResponse");
    case FT_STRUCT_MAP:          return type_from_name("map[string:string]");
    }
    return NULL;
}

static void typechecker_check_stdlib_arg_count(TypeChecker *checker, const char *mod,
    const char *fn, AstNode *node)
{
    const StdlibFuncMeta *m = find_stdlib_meta(mod, fn);
    if (!m) return;

    int nargs = node->data.call.arg_count;
    if (nargs < m->min_args || nargs > m->max_args) {
        char *msg = NULL;
        if (m->min_args == m->max_args) {
            msg = typechecker_format(checker,
                "function '%s.%s' expects %d argument(s), got %d",
                mod, fn, m->min_args, nargs);
        } else {
            msg = typechecker_format(checker,
                "function '%s.%s' expects %d to %d argument(s), got %d",
                mod, fn, m->min_args, m->max_args, nargs);
        }
        tc_err_arity(checker, node, msg);
    }
}

/* Compile-time validation for strconv base parameter.
 * When the second arg to to_int/to_uint/format_int/format_uint is a literal
 * integer, verify it's in the valid range [2, 36]. */
static void typechecker_check_strconv_base(TypeChecker *checker, const char *mod,
    const char *fn, AstNode *node)
{
    if (strcmp(mod, "strconv") != 0) return;
    if (strcmp(fn, "to_int") != 0 && strcmp(fn, "to_uint") != 0 &&
        strcmp(fn, "format_int") != 0 && strcmp(fn, "format_uint") != 0) return;
    if (node->data.call.arg_count < 2) return;
    AstNode *base_arg = node->data.call.args[1];
    if (base_arg->kind != NODE_INT_VALUE) return;
    int64_t base = base_arg->data.int_value.value;
    if (base < 2 || base > 36) {
        char *msg;
        msg = typechecker_format(checker,
            "invalid base %lld for 'strconv.%s'; base must be between 2 and 36",
            (long long)base, fn);
        diagnostic_error_message(checker->diag, "E5009", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
}

/* Compile-time validation for io.read_lines' optional line limit.
 * A literal negative limit is rejected outright rather than clamped. */
static void typechecker_check_io_read_lines_limit(TypeChecker *checker, const char *mod,
    const char *fn, AstNode *node)
{
    if (strcmp(mod, "io") != 0 || strcmp(fn, "read_lines") != 0) return;
    if (node->data.call.arg_count < 2) return;
    AstNode *limit_arg = node->data.call.args[1];
    int64_t limit;
    if (limit_arg->kind == NODE_INT_VALUE) {
        limit = limit_arg->data.int_value.value;
    } else if (limit_arg->kind == NODE_PREFIX_EXPR &&
               limit_arg->data.prefix.op == TOK_MINUS &&
               limit_arg->data.prefix.right &&
               limit_arg->data.prefix.right->kind == NODE_INT_VALUE) {
        limit = -limit_arg->data.prefix.right->data.int_value.value;
    } else {
        return;
    }
    if (limit < 0) {
        diagnostic_error_code(checker->diag, "E3150",
            NODE_FILE(checker, node), limit_arg->token.line, limit_arg->token.column, 0);
    }
}

static void typechecker_check_stdlib_arg_types(TypeChecker *checker, const char *mod,
    const char *fn, AstNode *node)
{
    const StdlibFuncMeta *m = find_stdlib_meta(mod, fn);
    if (!m || m->arg_type_count == 0) return;

    for (int i = 0; i < m->arg_type_count; i++) {
        int idx = m->arg_types[i].index;
        if (idx < node->data.call.arg_count) {
            /* A type position is validated by name. Resolving it as a value
             * would report the type name as one (E3100); skipping it without
             * validating would put a mistyped name into the generated C. */
            if (m->arg_types[i].kind == ARG_TYPE) {
                AstNode *arg = node->data.call.args[idx];
                if (arg->kind != NODE_LABEL ||
                    !type_arg_names_a_type(checker, arg->data.label.value)) {
                    char *msg = typechecker_format(checker,
                        "'%s.%s()' expects a type name as argument %d",
                        mod, fn, idx + 1);
                    diagnostic_error_message(checker->diag, "E4016", msg,
                        NODE_FILE(checker, arg), arg->token.line, arg->token.column, 0);
                }
                continue;
            }
            GrayType *arg_t = resolve_expression(checker, node->data.call.args[idx]);
            if (!arg_kind_matches(m->arg_types[i].kind, arg_t)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s.%s()' expects %s as argument %d, got '%s'",
                    mod, fn, expected_kind_name(m->arg_types[i].kind), idx + 1, type_name(arg_t));
                diagnostic_error_message(checker->diag, "E5026", msg,
                    NODE_FILE(checker, node->data.call.args[idx]),
                    node->data.call.args[idx]->token.line,
                    node->data.call.args[idx]->token.column, 0);
            }
        }
    }
}

/* --- "Did you mean?" suggestion helper --- */

static int levenshtein(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la == 0) return lb;
    if (lb == 0) return la;
    int stack_row[256];
    int *row = lb < 256 ? stack_row : xmalloc(sizeof(int) * (lb + 1));
    for (int j = 0; j <= lb; j++) row[j] = j;
    for (int i = 1; i <= la; i++) {
        int prev = row[0];
        row[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del = row[j] + 1;
            int ins = row[j-1] + 1;
            int sub = prev + cost;
            prev = row[j];
            row[j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
    }
    int result = row[lb];
    if (row != stack_row) free(row);
    return result;
}

/* Find closest matching name from scope variables, functions, and builtins */
static const char *suggest_similar_name(TypeChecker *checker, const char *name) {
    const char *best = NULL;
    int best_dist = 3; /* max distance for suggestions */

    /* Check scope variables */
    for (Scope *s = checker->current_scope; s; s = s->parent) {
        for (int i = 0; i < s->count; i++) {
            int d = levenshtein(name, s->symbols[i].name);
            if (d > 0 && d < best_dist) {
                best_dist = d;
                best = s->symbols[i].name;
            }
        }
    }

    /* Check registered functions */
    for (int i = 0; i < checker->func_count; i++) {
        int d = levenshtein(name, checker->funcs[i].name);
        if (d > 0 && d < best_dist) {
            best_dist = d;
            best = checker->funcs[i].name;
        }
    }

    return best;
}

/* --- Builtin name check --- */

static bool typechecker_is_imported_module(TypeChecker *checker, const char *name) {
    for (int i = 0; i < checker->import_count; i++) {
        if (strcmp(checker->imported_modules[i], name) == 0) return true;
    }
    return false;
}

/* Nearest imported module name to `name`, or NULL. Kept separate from
 * suggest_similar_name() so a variable typo is never answered with a module. */
static const char *suggest_similar_module(TypeChecker *checker, const char *name) {
    const char *best = NULL;
    int best_dist = 3;
    for (int i = 0; i < checker->import_count; i++) {
        int d = levenshtein(name, checker->imported_modules[i]);
        if (d > 0 && d < best_dist) {
            best_dist = d;
            best = checker->imported_modules[i];
        }
    }
    return best;
}

/* Nearest function within `mod`, or NULL. Module functions are registered
 * under <mod>_<func>, so the comparison is against the suffix — matching the
 * mangled key would put every candidate out of Levenshtein range. */
static const char *suggest_similar_module_func(TypeChecker *checker,
    const char *mod, const char *fname) {
    const char *best = NULL;
    int best_dist = 3;
    size_t mod_len = strlen(mod);
    for (int i = 0; i < checker->func_count; i++) {
        const char *reg = checker->funcs[i].name;
        if (strncmp(reg, mod, mod_len) != 0 || reg[mod_len] != '_') continue;
        const char *bare = reg + mod_len + 1;
        int d = levenshtein(fname, bare);
        if (d > 0 && d < best_dist) {
            best_dist = d;
            best = bare;
        }
    }
    return best;
}

static bool typechecker_is_stdlib_import(TypeChecker *checker, const char *name) {
    for (int i = 0; i < checker->import_count; i++) {
        if (strcmp(checker->imported_modules[i], name) == 0)
            return checker->import_is_stdlib[i];
    }
    return false;
}

/* Owning module for a stdlib opaque type's bare name (Thread, Mutex, ...),
 * or NULL if bare_name isn't one. Shared by is_stdlib_opaque_type_available()
 * and typechecker_mark_type_module_used() so both agree on the mapping. */
/* The stdlib's opaque types and the module each belongs to. One list, so
 * resolution and registration cannot disagree about it. */
static const struct { const char *type; const char *mod; } stdlib_opaque_map[] = {
        {"Arena",        "mem"},
        {"Builder",      "strings"},
        {"Thread",       "threads"},
        {"Mutex",        "sync"},
        {"SpinLock",     "atomic"},
        {"Channel",      "channels"},
        {"Socket",       "net"},
        {"Listener",     "net"},
        {"Database",     "sqlite"},
        {"Router",       "server"},
        {"HttpRequest",  "server"},
        {"HttpResponse", "http"},
        {"UUID",         "uuid"},
        {NULL, NULL}
};

/* Enums a stdlib module exposes: closed sets of named int values. A variant is
 * reachable both as `module.VARIANT` and as `EnumName.VARIANT`; its value is its
 * position. Reserved (E3099) only while the owning module is imported. */
static const struct {
    const char *name; const char *mod;
    const char *variants[6]; int count;
} stdlib_enum_map[] = {
    {"OpenFlag", "io", {"O_RDONLY", "O_WRONLY", "O_RDWR"}, 3},
    {"Platform", "os", {"MAC_OS", "LINUX", "WINDOWS", "OTHER"}, 4},
    {NULL, NULL, {NULL}, 0}
};

/* Module that owns a stdlib-provided enum name, or NULL. */
static const char *stdlib_enum_module(const char *bare_name) {
    for (int i = 0; stdlib_enum_map[i].name; i++) {
        if (strcmp(bare_name, stdlib_enum_map[i].name) == 0) return stdlib_enum_map[i].mod;
    }
    return NULL;
}

static const char *stdlib_opaque_module(const char *bare_name) {
    for (int i = 0; stdlib_opaque_map[i].type; i++) {
        if (strcmp(bare_name, stdlib_opaque_map[i].type) == 0) return stdlib_opaque_map[i].mod;
    }
    return NULL;
}

/* If type_name is module-prefixed (e.g. "T.Query" or "T_Query"), mark that
 * module used. A type name reaches here as written or already mangled, so we
 * split on whichever separator comes first and check whether the prefix is a
 * known import. A bare stdlib opaque type name (Thread, Mutex, ...) reached
 * via `using`/`import and use` has no prefix to split on, so it's checked
 * against the opaque map instead. */
static void typechecker_mark_type_module_used(TypeChecker *checker, const char *type_name);

/* As typechecker_mark_type_module_used, for a name that is a slice of a
 * larger type string rather than one of its own. */
static void typechecker_mark_type_module_used_n(TypeChecker *checker,
                                                const char *type_name, size_t len) {
    char buffer[TYPE_NAME_MAX];
    if (len == 0 || len >= sizeof(buffer)) return;
    memcpy(buffer, type_name, len);
    buffer[len] = '\0';
    typechecker_mark_type_module_used(checker, buffer);
}

static void typechecker_mark_type_module_used(TypeChecker *checker, const char *type_name) {
    if (!type_name) return;
    /* A module named inside a pointer, array or map type is named just as
     * much as a bare one. The prefix scan below reads from the first
     * character, so "[mod_Item]" measured a prefix of "[mod" and matched no
     * import — and the file was told an import it uses is unused. */
    if (type_name[0] == '^') {
        typechecker_mark_type_module_used(checker, type_name + 1);
        return;
    }
    {
        const char *key, *value;
        size_t key_len, value_len;
        if (parse_map_key_value(type_name, &key, &key_len, &value, &value_len)) {
            typechecker_mark_type_module_used_n(checker, key, key_len);
            typechecker_mark_type_module_used_n(checker, value, value_len);
            return;
        }
    }
    if (type_name[0] == '[') {
        size_t len = strlen(type_name);
        if (len < 3 || type_name[len - 1] != ']') return;
        const char *element = type_name + 1;
        size_t element_len = len - 2;
        /* A fixed-size array carries its length: [T,8]. */
        int depth = 0;
        for (size_t i = 0; i < element_len; i++) {
            if (element[i] == '[') depth++;
            else if (element[i] == ']') depth--;
            else if (element[i] == ',' && depth == 0) { element_len = i; break; }
        }
        typechecker_mark_type_module_used_n(checker, element, element_len);
        return;
    }
    const char *separator = strpbrk(type_name, "._");
    if (!separator || separator == type_name) {
        const char *mod = stdlib_opaque_module(type_name);
        if (!mod) return;
        for (int mi = 0; mi < checker->import_count; mi++) {
            if (strcmp(checker->imported_modules[mi], mod) == 0) {
                checker->import_used[mi] = true;
                return;
            }
        }
        return;
    }
    size_t prefix_len = (size_t)(separator - type_name);
    for (int mi = 0; mi < checker->import_count; mi++) {
        if (strlen(checker->imported_modules[mi]) == prefix_len &&
            strncmp(checker->imported_modules[mi], type_name, prefix_len) == 0) {
            checker->import_used[mi] = true;
            return;
        }
    }
}

static bool typechecker_is_builtin(const char *name) {
    static const char *const builtins[] = {
        "addr", "assert", "bool", "byte", "c_string", "cast",
        "char", "char_count", "copy", "embed", "eprint", "eprintln",
        "error", "exit", "f32", "f64", "fields", "float", "flush", "here",
        "i128", "i16", "i256", "i32", "i64", "i8",
        "input", "int", "len", "new", "panic", "print", "println",
        "range", "raw", "ref", "size_of", "sleep_ms", "sleep_ns", "sleep_s",
        "string", "system", "to_char", "type_of",
        "u128", "u16", "u256", "u32", "u64", "u8", "uint",
    };
    return string_set_contains(builtins, (int)(sizeof(builtins)/sizeof(builtins[0])), name);
}

/* Check whether any arg_names entry is non-NULL (i.e. call has named args). */
static bool typechecker_has_named_arguments(AstNode *node) {
    if (!node->data.call.arg_names) return false;
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (node->data.call.arg_names[i]) return true;
    }
    return false;
}

/* Resolve named arguments: validate names, check ordering, and reorder
 * args in-place so downstream code sees normal positional args. */
static void typechecker_resolve_named_arguments(TypeChecker *checker, AstNode *node,
                                  AstNode *func_decl, const char *display_name) {
    if (!node->data.call.arg_names) return;

    const char **arg_names = node->data.call.arg_names;
    int arg_count = node->data.call.arg_count;

    /* Quick check: any named args at all? */
    bool any_named = false;
    for (int i = 0; i < arg_count; i++) {
        if (arg_names[i]) { any_named = true; break; }
    }
    if (!any_named) return;

    /* E5033: positional-before-named check */
    bool seen_named = false;
    for (int i = 0; i < arg_count; i++) {
        if (arg_names[i]) {
            seen_named = true;
        } else if (seen_named) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "positional argument after named argument in call to '%s'",
                display_name);
            diagnostic_error_message(checker->diag, "E5033", msg,
                NODE_FILE(checker, node), node->data.call.args[i]->token.line,
                node->data.call.args[i]->token.column, 0);
            return;
        }
    }

    if (!func_decl || func_decl->kind != NODE_FUNC_DECL) return;

    int param_count = func_decl->data.func_decl.param_count;
    Param *params = func_decl->data.func_decl.params;

    /* Build reordered args array sized to param_count. */
    size_t new_args_size = sizeof(AstNode *) * (size_t)(param_count > 0 ? param_count : 1);
    AstNode **new_args = arena_alloc(checker->arena, new_args_size);
    memset(new_args, 0, new_args_size);

    /* Copy positional args into their slots */
    int positional_count = 0;
    for (int i = 0; i < arg_count; i++) {
        if (!arg_names[i]) {
            if (i < param_count) {
                new_args[i] = node->data.call.args[i];
            }
            positional_count++;
        } else {
            break; /* positional args are contiguous at the front */
        }
    }

    /* Place named args by matching param names */
    for (int i = positional_count; i < arg_count; i++) {
        const char *name = arg_names[i];
        int slot = -1;
        for (int parameter_index = 0; parameter_index < param_count; parameter_index++) {
            if (strcmp(params[parameter_index].name, name) == 0) {
                slot = parameter_index;
                break;
            }
        }
        if (slot < 0) {
            /* E5031: unknown parameter name */
            char *msg = NULL;
            msg = typechecker_format(checker,
                "unknown parameter name '%s' in call to '%s'",
                name, display_name);
            diagnostic_error_message(checker->diag, "E5031", msg,
                NODE_FILE(checker, node), node->data.call.args[i]->token.line,
                node->data.call.args[i]->token.column, 0);
            return;
        }
        if (new_args[slot]) {
            /* E5032: already filled by a positional arg */
            char *msg = NULL;
            msg = typechecker_format(checker,
                "parameter '%s' is already provided positionally (argument %d) in call to '%s'",
                name, slot + 1, display_name);
            diagnostic_error_message(checker->diag, "E5032", msg,
                NODE_FILE(checker, node), node->data.call.args[i]->token.line,
                node->data.call.args[i]->token.column, 0);
            return;
        }
        new_args[slot] = node->data.call.args[i];
    }

    /* Count how many slots are actually filled (positional + named).
     * Unfilled slots will be handled by the existing default-value logic. */
    int filled = 0;
    for (int i = param_count - 1; i >= 0; i--) {
        if (new_args[i]) { filled = i + 1; break; }
    }
    /* Ensure at least all positional + named are accounted for */
    if (filled < positional_count) filled = positional_count;

    /* Fill interior gaps with default value AST nodes from the function
     * declaration so codegen doesn't encounter NULL arg pointers. */
    for (int i = 0; i < filled; i++) {
        if (!new_args[i] && i < param_count && params[i].default_value) {
            new_args[i] = params[i].default_value;
        }
    }

    /* Replace the node's args in-place */
    node->data.call.args = new_args;
    node->data.call.arg_count = filled;
    node->data.call.arg_names = NULL; /* now positional */
}


/* : stdlib constants reachable via `import and use` / `using`. */
typedef struct {
    const char *name;
    const char *mod;
    TypeKind return_kind;
    const char *struct_name; /* referenced type name for TK_STRUCT / TK_ENUM constants; NULL otherwise */
} UsingConst;
static GrayType *stdlib_const_type(int index);

static const UsingConst _using_consts[] = {
    {"PI","math",TK_FLOAT,NULL},{"E","math",TK_FLOAT,NULL},{"TAU","math",TK_FLOAT,NULL},
    {"PHI","math",TK_FLOAT,NULL},{"SQRT2","math",TK_FLOAT,NULL},{"LN2","math",TK_FLOAT,NULL},
    {"LN10","math",TK_FLOAT,NULL},{"INF","math",TK_FLOAT,NULL},{"NEG_INF","math",TK_FLOAT,NULL},
    {"EPSILON","math",TK_FLOAT,NULL},
    {"MAX_INT","math",TK_INT,NULL},{"MIN_INT","math",TK_INT,NULL},
    {"MAX_FLOAT","math",TK_FLOAT,NULL},{"MIN_FLOAT","math",TK_FLOAT,NULL},
    {"MAC_OS","os",TK_ENUM,"Platform"},{"LINUX","os",TK_ENUM,"Platform"},{"WINDOWS","os",TK_ENUM,"Platform"},{"OTHER","os",TK_ENUM,"Platform"},
    {"O_RDONLY","io",TK_ENUM,"OpenFlag"},{"O_WRONLY","io",TK_ENUM,"OpenFlag"},{"O_RDWR","io",TK_ENUM,"OpenFlag"},
    {"BASE_2","strconv",TK_INT,NULL},{"BASE_8","strconv",TK_INT,NULL},{"BASE_10","strconv",TK_INT,NULL},
    {"BASE_16","strconv",TK_INT,NULL},{"BASE_36","strconv",TK_INT,NULL},
    {"NIL_UUID","uuid",TK_STRUCT,"UUID"},
    {NULL,NULL,TK_UNKNOWN,NULL}
};

/* The type of a stdlib constant, from the one table that describes them. */
static GrayType *stdlib_const_type(int index) {
    switch (_using_consts[index].return_kind) {
    case TK_FLOAT:  return &TYPE_FLOAT;
    case TK_INT:    return &TYPE_INT;
    case TK_STRING: return &TYPE_STRING;
    case TK_STRUCT: return _using_consts[index].struct_name
                        ? type_struct(_using_consts[index].struct_name) : &TYPE_UNKNOWN;
    case TK_ENUM:   return _using_consts[index].struct_name
                        ? type_enum(_using_consts[index].struct_name) : &TYPE_UNKNOWN;
    default:        return &TYPE_UNKNOWN;
    }
}

/* Stdlib functions that return more than one value without being fallible.
 * Fallible functions carry their (T, Error) shape through stdlib_func_meta
 * instead, so they are deliberately absent here. */
#define STDLIB_MAX_RETURN_SLOTS 4
typedef struct {
    const char *mod;
    const char *fn;
    int count;
    GrayType *slots[STDLIB_MAX_RETURN_SLOTS];
} StdlibMultiReturn;

static const StdlibMultiReturn _stdlib_multi_returns[] = {
    {"channels", "try_receive", 2, {&TYPE_INT, &TYPE_BOOL}},
    {"math",     "modf",        2, {&TYPE_FLOAT, &TYPE_FLOAT}},
    {"os",       "exec",        4, {&TYPE_INT, &TYPE_STRING, &TYPE_STRING, &TYPE_BOOL}},
    {NULL, NULL, 0, {NULL}}
};

static const StdlibMultiReturn *find_stdlib_multi_return(const char *mod, const char *fn) {
    if (!mod || !fn) return NULL;
    for (int i = 0; _stdlib_multi_returns[i].mod; i++) {
        if (strcmp(_stdlib_multi_returns[i].mod, mod) == 0 &&
            strcmp(_stdlib_multi_returns[i].fn, fn) == 0)
            return &_stdlib_multi_returns[i];
    }
    return NULL;
}

/* Resolve a bare call name to the stdlib module supplying it through
 * `import and use` / `using`, or NULL when no using-module provides it. */
static const char *find_using_stdlib_module(TypeChecker *checker, const char *fn) {
    if (!fn) return NULL;
    for (int i = 0; i < checker->using_module_count; i++) {
        if (!using_module_accessible(checker, i)) continue;
        const char *real_mod = typechecker_resolve_alias(checker, checker->using_modules[i]);
        if (find_stdlib_meta(real_mod, fn)) return real_mod;
    }
    return NULL;
}

static void set_temp_return_slots(TypeChecker *checker, const char *tmp_name,
                                  GrayType **slots, int count) {
    Symbol *sym = scope_lookup_local(checker->current_scope, tmp_name);
    if (!sym) { free(slots); return; }
    sym->ret_types = slots;
    sym->ret_count = count;
    sym->ret_types_owned = true;
}

/* Give a destructuring temp its slot types for a stdlib call, covering both
 * the fallible (T, Error) shape and the non-fallible multi-value functions.
 * Returns true when slots were applied. */
static bool apply_stdlib_call_returns(TypeChecker *checker, const char *tmp_name,
                                      const char *mod, const char *fn) {
    const StdlibMultiReturn *mr = find_stdlib_multi_return(mod, fn);
    if (mr) {
        GrayType **rt = xmalloc(sizeof(GrayType *) * (size_t)mr->count);
        for (int i = 0; i < mr->count; i++) rt[i] = mr->slots[i];
        set_temp_return_slots(checker, tmp_name, rt, mr->count);
        return true;
    }
    if (typechecker_is_fallible_stdlib(mod, fn)) {
        GrayType *primary = typechecker_get_fallible_stdlib_type(mod, fn);
        if (primary) {
            GrayType **rt = xmalloc(sizeof(GrayType *) * 2);
            rt[0] = primary;
            rt[1] = type_from_name("Error");
            set_temp_return_slots(checker, tmp_name, rt, 2);
            return true;
        }
    }
    return false;
}

/* Find the index of a module name in checker->imported_modules[], or -1. */
static int typechecker_find_import_index(TypeChecker *checker, const char *mod) {
    const char *real = typechecker_resolve_alias(checker, mod);
    for (int mi = 0; mi < checker->import_count; mi++) {
        if (strcmp(checker->imported_modules[mi], mod) == 0 ||
            strcmp(checker->imported_modules[mi], real) == 0)
            return mi;
    }
    return -1;
}

/* Single-pass lookup: marks import used and returns the type (NULL = not found). */
static GrayType *typechecker_lookup_using_constant(TypeChecker *checker, const char *name) {
    for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
        if (!using_module_accessible(checker, using_index)) continue;
        const char *real_mod = typechecker_resolve_alias(checker, checker->using_modules[using_index]);
        for (int const_index = 0; _using_consts[const_index].name; const_index++) {
            if (strcmp(name, _using_consts[const_index].name) == 0 &&
                strcmp(real_mod, _using_consts[const_index].mod) == 0) {
                int mi = checker->using_module_import_indices[using_index];
                if (mi >= 0) checker->import_used[mi] = true;
                return stdlib_const_type(const_index);
            }
        }
    }
    return NULL;
}

/* --- Enum helpers --- */

static void register_enum(TypeChecker *checker, const char *name,
    const char *display_name, bool is_string,
    const char **values, int value_count,
    const char ***payload_types, int *payload_counts, bool is_tagged,
    bool is_flags, bool is_deprecated, const char *deprecated_message) {
    adopt_registration(checker, DECL_ENUM, name, checker->enum_count);
    if (checker->enum_count >= checker->enum_cap) {
        checker->enum_cap = checker->enum_cap ? checker->enum_cap * 2 : 8;
        checker->enum_names = xrealloc(checker->enum_names, sizeof(const char *) * checker->enum_cap);
        checker->enum_display_names = xrealloc(checker->enum_display_names, sizeof(const char *) * checker->enum_cap);
        checker->enum_is_string = xrealloc(checker->enum_is_string, sizeof(bool) * checker->enum_cap);
        checker->enum_values = xrealloc(checker->enum_values, sizeof(const char **) * checker->enum_cap);
        checker->enum_value_counts = xrealloc(checker->enum_value_counts, sizeof(int) * checker->enum_cap);
        checker->enum_payload_types = xrealloc(checker->enum_payload_types, sizeof(const char ***) * checker->enum_cap);
        checker->enum_payload_counts = xrealloc(checker->enum_payload_counts, sizeof(int *) * checker->enum_cap);
        checker->enum_is_tagged = xrealloc(checker->enum_is_tagged, sizeof(bool) * checker->enum_cap);
        checker->enum_is_flags = xrealloc(checker->enum_is_flags, sizeof(bool) * checker->enum_cap);
        checker->enum_is_deprecated = xrealloc(checker->enum_is_deprecated, sizeof(bool) * checker->enum_cap);
        checker->enum_deprecated_messages = xrealloc(checker->enum_deprecated_messages, sizeof(const char *) * checker->enum_cap);
    }
    checker->enum_names[checker->enum_count] = name;
    checker->enum_display_names[checker->enum_count] = display_name ? display_name : name;
    checker->enum_is_string[checker->enum_count] = is_string;
    checker->enum_values[checker->enum_count] = values;
    checker->enum_value_counts[checker->enum_count] = value_count;
    checker->enum_payload_types[checker->enum_count] = payload_types;
    checker->enum_payload_counts[checker->enum_count] = payload_counts;
    checker->enum_is_tagged[checker->enum_count] = is_tagged;
    checker->enum_is_flags[checker->enum_count] = is_flags;
    checker->enum_is_deprecated[checker->enum_count] = is_deprecated;
    checker->enum_deprecated_messages[checker->enum_count] = deprecated_message;
    checker->enum_count++;
}

/* Stdlib opaque types (Thread, Mutex, ...) are only valid when their owning
 * module is imported. bare_name must already have any module prefix
 * stripped (e.g. "Thread", not "threads_Thread"). */
static bool is_stdlib_opaque_type_available(TypeChecker *checker, const char *bare_name) {
    const char *mod = stdlib_opaque_module(bare_name);
    return mod && typechecker_is_imported_module(checker, mod);
}

/* Resolve a type name, returning TK_ENUM for known enum names instead of
 * the default TK_STRUCT that type_from_name() produces for uppercase names. */
/* Report `mod.name` as inaccessible if the symbol table says so, and return
 * whether it did. The visibility rule itself lives in the table; this is only
 * the diagnostic each call site used to hand-roll — E4015 for a function,
 * variable or constant, E4021 for a type alias. */
static bool reject_if_private(TypeChecker *checker, AstNode *node,
                              const char *mod, const char *name);

/* The scope every resolution in this file happens in. */
static ResolveScope checker_scope(TypeChecker *checker) {
    checker_refresh_using(checker);
    ResolveScope scope;
    scope.module = module_table_module_for_file(checker->modules, checker->current_check_file);
    scope.file = checker->current_check_file ? checker->current_check_file : checker->file;
    scope.using_modules = checker->using_visible;
    scope.using_count = checker->using_visible_count;
    return scope;
}

/* Refresh the visible `using` list for the current file. */
static void checker_refresh_using(TypeChecker *checker) {
    if (checker->using_visible_file == checker->current_check_file &&
        checker->using_visible_stamp == checker->using_module_count)
        return;
    checker->using_visible_count = 0;
    for (int i = 0; i < checker->using_module_count; i++) {
        if (!using_module_accessible(checker, i)) continue;
        if (checker->using_visible_count >= checker->using_visible_cap) {
            checker->using_visible_cap = GROW_NEXT_CAP(checker->using_visible_cap);
            checker->using_visible = xrealloc(checker->using_visible,
                sizeof(const char *) * (size_t)checker->using_visible_cap);
        }
        checker->using_visible[checker->using_visible_count++] = checker->using_modules[i];
    }
    checker->using_visible_file = checker->current_check_file;
    checker->using_visible_stamp = checker->using_module_count;
}

/* Resolve a reference and leave the answer on the node, so codegen reads it
 * instead of resolving the same name again. */
static DeclEntry *checker_cache_resolution(TypeChecker *checker, AstNode *node,
                                           const char *written);

/* The declaration a written name refers to, or NULL. */
static DeclEntry *checker_resolve_entry(TypeChecker *checker, const char *written) {
    if (!written || !checker->modules) return NULL;
    ResolveScope scope = checker_scope(checker);
    DeclEntry *entry = module_resolve_written(checker->modules, &scope, written);
    /* A bare name that reached its declaration through `using` names a member
     * of that module exactly as a qualified reference does, so it counts as
     * using the import. Only qualified access marked it before, and a file
     * that referred to an imported type by its bare name was told the import
     * was never used. Resolving inside the declaring module is not a use. */
    if (entry && entry->module_name && entry->module_name[0] &&
        (!scope.module || strcmp(entry->module_name, scope.module) != 0)) {
        mark_import_used(checker, entry->module_name);
    }
    return entry;
}

static DeclEntry *checker_cache_resolution(TypeChecker *checker, AstNode *node,
                                           const char *written) {
    DeclEntry *entry = checker_resolve_entry(checker, written);
    if (entry && node) node->resolved_decl = entry;
    return entry;
}

static const char *checker_resolve_decl_into(TypeChecker *checker, const char *written,
                                             char *buf, size_t buflen) {
    if (!written || !checker->modules) return written;
    ResolveScope scope = checker_scope(checker);
    DeclEntry *entry = module_resolve_written(checker->modules, &scope, written);
    return entry ? module_mangle_into(entry, buf, buflen) : written;
}

static bool reject_if_private(TypeChecker *checker, AstNode *node,
                              const char *mod, const char *name) {
    if (!checker->modules || !mod || !name) return false;
    ResolveScope scope = checker_scope(checker);
    ResolveStatus status;
    DeclEntry *entry = module_resolve_qualified(checker->modules, &scope, mod, name, &status);
    if (status != RESOLVE_PRIVATE) return false;
    const char *code = entry->kind == DECL_ALIAS ? "E4021" : "E4015";
    diagnostic_error_code_formatted(checker->diag, code,
        NODE_FILE(checker, node), node->token.line, node->token.column, 0, name);
    return true;
}

/* Does `name` name a module-level constant or variable visible from here?
 * Scope symbols for those are bound during the statement walk, but a struct
 * field's default value is checked while declarations are still being
 * registered — so the symbol may not exist yet even though the declaration
 * does. The symbol table is complete by then, so it is what decides. */
static bool module_declares_const(TypeChecker *checker, const char *name) {
    if (!checker->modules) return false;
    ResolveScope scope = checker_scope(checker);
    DeclEntry *entry = module_resolve_written(checker->modules, &scope, name);
    return entry && entry->kind == DECL_CONST;
}

/* The registry spelling of an enum name, as an arena string safe to hand to
 * type_enum(), whose result outlives the call. */
static const char *checker_resolve_enum_key(TypeChecker *checker, const char *written) {
    char buf[MSG_BUF_SIZE];
    const char *key = checker_resolve_decl_into(checker, written, buf, sizeof(buf));
    return key == written ? written : arena_copy_string(checker->arena, key);
}

/* The registry spelling of a type name as written in the current file. */
static const char *checker_resolve_type_name(TypeChecker *checker, const char *written) {
    if (!written || !checker->modules) return written;
    ResolveScope scope = checker_scope(checker);
    return module_resolve_type_name(checker->modules, &scope, written);
}

/* Does a written type annotation name a type that does not exist? An
 * annotation resolves to TK_UNKNOWN either because the name is undefined or
 * because it is the generic wildcard, whose type comes from the call site
 * that binds it, or the bare `func`, an untyped function reference that
 * codegen stores as void *. Everything else that types as unknown is a name
 * for nothing.
 *
 * The name's casing has nothing to do with it. Every E4016 site used to
 * require an initial capital, so a lowercase spelling skipped the check
 * outright and `x zag` typechecked clean and failed in the C compiler. */
static bool type_name_is_undefined(const char *written, const GrayType *resolved) {
    if (!written || !resolved || resolved->kind != TK_UNKNOWN) return false;
    return !type_name_has_wildcard(written);
}

/* Do two function types disagree? A typed reference carries its signature in
 * its canonical encoded name — "func(int)->int" — so any difference between
 * two of those is a mismatch. The bare `func` names no signature: it is
 * every function type at once, and matches all of them. */
static bool func_types_mismatch(const GrayType *a, const GrayType *b) {
    if (!a || !b || a->kind != TK_FUNCTION || b->kind != TK_FUNCTION) return false;
    if (!a->name || !b->name) return false;
    if (strcmp(a->name, "func") == 0 || strcmp(b->name, "func") == 0) return false;
    return strcmp(a->name, b->name) != 0;
}

static GrayType *typechecker_type_from_name(TypeChecker *checker, const char *name);
static Symbol *checker_lookup_symbol(TypeChecker *checker, const char *name);

/* Decompose a written container type into the type names it is built from:
 * the pointee of ^T, the element of [T] or [T,N], the key and value of
 * map[K:V]. Returns true when `written` is a container spelling — *out_count
 * is then how many component names were written, which is zero for a
 * malformed one. A leaf name returns false.
 *
 * The separator scans run at bracket depth zero, so a component that is
 * itself a container — [map[string:int],3] — splits where it should. */
static bool type_name_components(const char *written, char out[2][MSG_BUF_SIZE],
                                 int *out_count) {
    *out_count = 0;
    if (!written || !*written) return false;

    if (written[0] == '^') {
        const char *pointee = written;
        while (*pointee == '^') pointee++;
        if (*pointee) {
            snprintf(out[0], MSG_BUF_SIZE, "%s", pointee);
            *out_count = 1;
        }
        return true;
    }

    if (written[0] == '[') {
        size_t len = strlen(written);
        if (len > 2 && written[len - 1] == ']') {
            char elem[MSG_BUF_SIZE];
            size_t element_length = len - 2;
            if (element_length >= MSG_BUF_SIZE) element_length = MSG_BUF_SIZE - 1;
            memcpy(elem, written + 1, element_length);
            elem[element_length] = '\0';
            /* Drop the ,N of a fixed-size array; a comma nested inside the
             * element type is not that separator. */
            int depth = 0;
            for (int i = 0; elem[i]; i++) {
                if (elem[i] == '[') depth++;
                else if (elem[i] == ']') depth--;
                else if (elem[i] == ',' && depth == 0) { elem[i] = '\0'; break; }
            }
            snprintf(out[0], MSG_BUF_SIZE, "%s", elem);
            *out_count = 1;
        }
        return true;
    }

    if (strncmp(written, "map[", 4) == 0) {
        size_t len = strlen(written);
        if (len > 5 && written[len - 1] == ']') {
            char inner[MSG_BUF_SIZE];
            size_t inner_len = len - 5;  /* skip "map[" and "]" */
            if (inner_len >= MSG_BUF_SIZE) inner_len = MSG_BUF_SIZE - 1;
            memcpy(inner, written + 4, inner_len);
            inner[inner_len] = '\0';
            int depth = 0, colon_pos = -1;
            for (int i = 0; inner[i]; i++) {
                if (inner[i] == '[') depth++;
                else if (inner[i] == ']') depth--;
                else if (inner[i] == ':' && depth == 0) { colon_pos = i; break; }
            }
            if (colon_pos >= 0) {
                inner[colon_pos] = '\0';
                snprintf(out[0], MSG_BUF_SIZE, "%s", inner);
                snprintf(out[1], MSG_BUF_SIZE, "%s", inner + colon_pos + 1);
                *out_count = 2;
            }
        }
        return true;
    }

    return false;
}

/* The first leaf of a written type that names no type, copied into `buf`, or
 * NULL when every leaf resolves.
 *
 * A container types as TK_ARRAY or TK_MAP whatever its elements name, so
 * testing an annotation as a whole never looks inside it and `x [zag]`
 * reached the C compiler. Naming the offending leaf rather than the whole
 * spelling is also what the reader needs. */
static const char *undefined_type_leaf(TypeChecker *checker, const char *written,
                                       char *buf, size_t buflen) {
    if (!written || !*written) return NULL;

    char parts[2][MSG_BUF_SIZE];
    int part_count = 0;
    if (type_name_components(written, parts, &part_count)) {
        for (int i = 0; i < part_count; i++) {
            const char *bad = undefined_type_leaf(checker, parts[i], buf, buflen);
            if (bad) return bad;
        }
        return NULL;
    }

    /* A user module's member is resolved against the symbol table, so a dot
     * that survives resolution names nothing there. Only the stdlib carries a
     * qualifier this far down, and type_from_name() reads anything still
     * dotted as one of its types — which is how `lib.Nope` typed as a struct
     * and was reported as a mismatch rather than as a name for nothing. */
    {
        const char *resolved = checker_resolve_type_name(checker, written);
        const char *dot = resolved ? strchr(resolved, '.') : NULL;
        if (dot) {
            char mod[MSG_BUF_SIZE];
            size_t mlen = (size_t)(dot - resolved);
            if (mlen < sizeof(mod)) {
                memcpy(mod, resolved, mlen);
                mod[mlen] = '\0';
                if (!is_stdlib_module_name(typechecker_resolve_alias(checker, mod))) {
                    snprintf(buf, buflen, "%s", resolved);
                    return buf;
                }
            }
        }
    }

    if (!type_name_is_undefined(written, typechecker_type_from_name(checker, written)))
        return NULL;
    snprintf(buf, buflen, "%s", written);
    return buf;
}

/* The private declaration a written type name reaches, or NULL. Mirrors
 * undefined_type_leaf: every leaf of a container or pointer spelling is
 * checked, and the answer is returned rather than reported, because the
 * name-to-type query has no source location of its own to report against. */
static DeclEntry *private_type_leaf(TypeChecker *checker, const char *written) {
    if (!written || !*written || !checker->modules) return NULL;

    char parts[2][MSG_BUF_SIZE];
    int part_count = 0;
    if (type_name_components(written, parts, &part_count)) {
        for (int i = 0; i < part_count; i++) {
            DeclEntry *bad = private_type_leaf(checker, parts[i]);
            if (bad) return bad;
        }
        return NULL;
    }

    ResolveScope scope = checker_scope(checker);
    DeclEntry *entry = module_resolve_written(checker->modules, &scope, written);
    return (entry && !module_decl_visible(&scope, entry)) ? entry : NULL;
}

/* The private struct, enum or alias a written type name reaches, at any depth,
 * or NULL. Unlike private_type_leaf() this ignores visibility from here: an
 * alias declared in the same file as its target always sees it, and that file
 * is exactly where a re-export starts. */
static DeclEntry *private_target_leaf(TypeChecker *checker, const char *written) {
    if (!written || !*written || !checker->modules) return NULL;

    char parts[2][MSG_BUF_SIZE];
    int part_count = 0;
    if (type_name_components(written, parts, &part_count)) {
        for (int i = 0; i < part_count; i++) {
            DeclEntry *bad = private_target_leaf(checker, parts[i]);
            if (bad) return bad;
        }
        return NULL;
    }

    ResolveScope scope = checker_scope(checker);
    DeclEntry *entry = module_resolve_written(checker->modules, &scope, written);
    if (!entry || entry->visibility != VIS_PRIVATE) return NULL;
    return (entry->kind == DECL_STRUCT || entry->kind == DECL_ENUM ||
            entry->kind == DECL_ALIAS) ? entry : NULL;
}

/* Naming a private declaration from outside the file that declares it is an
 * error whatever the declaration is; the kind only picks which code to
 * report. Called once per written annotation, against the node that wrote it. */
static void reject_private_type(TypeChecker *checker, AstNode *node, const char *written) {
    DeclEntry *entry = private_type_leaf(checker, written);
    if (!entry) return;
    diagnostic_error_code_formatted(checker->diag,
        entry->kind == DECL_ALIAS ? "E4021" : "E4015",
        NODE_FILE(checker, node), node->token.line, node->token.column, 0, entry->name);
}

/* True when a written type spells 'Error' as an array element or a map
 * key/value, at any nesting depth. A bare Error scalar (local, param,
 * struct field) is fine — codegen represents it as GrayError* — but no
 * container element type maps to it, so [Error] / map[string:Error] leak
 * a C type error from the constructor. `in_container` tracks whether the
 * recursion has passed through an array or map spelling. */
static bool error_type_in_container(const char *written, bool in_container) {
    if (!written || !*written) return false;
    if (in_container &&
        (strcmp(written, "Error") == 0 || strcmp(written, "error") == 0))
        return true;
    bool arr_or_map = written[0] == '[' || strncmp(written, "map[", 4) == 0;
    char parts[2][MSG_BUF_SIZE];
    int part_count = 0;
    if (type_name_components(written, parts, &part_count)) {
        for (int i = 0; i < part_count; i++)
            if (error_type_in_container(parts[i], in_container || arr_or_map))
                return true;
    }
    return false;
}

/* E3153: reject Error nested in an array or map. Called once per written
 * annotation, alongside reject_private_type. Resolves type aliases first so
 * `alias ErrBag = [Error]` cannot hide the [Error] spelling from the
 * literal-string match in error_type_in_container. */
static void reject_error_in_container(TypeChecker *checker, AstNode *node,
                                      const char *written) {
    if (error_type_in_container(resolve_type_alias(checker, written), false)) {
        diagnostic_error_code(checker->diag, "E3153",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
}

static GrayType *typechecker_type_from_name(TypeChecker *checker, const char *name) {
    /* Map the name as written — "lib.Score", or a bare "Score" naming this
     * module's own or a using'd declaration — onto the registry spelling. */
    name = checker_resolve_type_name(checker, name);
    /* Resolve type aliases before any type lookup. */
    if (name) name = resolve_type_alias(checker, name);
    if (name && is_enum_name(checker, name)) return type_enum(name);
    /* A registered struct is a struct, whatever its registry spelling looks
     * like. type_from_name() has to guess from the spelling, and its guess
     * for a mangled name splits at the first '_' — which loses the type in a
     * module whose own name contains one (foo_bar_Baz). */
    if (name && is_struct_name(checker, name)) return type_struct(name);
    GrayType *resolved_type = type_from_name(name);
    /* : try prefixed type names from using-modules so bare
     * "Point" resolves to "shapes_Point" when shapes is using'd.
     * type_from_name returns TK_STRUCT for any capitalized name
     * even if the struct isn'resolved_type registered, so check is_struct_name
     * to see if the bare name actually exists before giving up. */
    if (name && name[0] >= 'A' && name[0] <= 'Z' &&
        !is_struct_name(checker, name) && !is_enum_name(checker, name)) {
        for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
            if (!using_module_accessible(checker, using_index)) continue;
            char prefixed[MSG_BUF_SIZE];
            module_member_key(checker, checker->using_modules[using_index], name,
                              prefixed, sizeof(prefixed));
            if (is_enum_name(checker, prefixed)) return type_enum(prefixed);
            if (is_struct_name(checker, prefixed)) return type_struct(prefixed);
        }
        /* : after registration completes, reject uppercase names that
         * aren'resolved_type registered as structs or enums. During registration we must
         * allow forward references, so only enforce this in later passes.
         * Exempt built-in types that are mapped directly in codegen without
         * struct registration (e.g. Error → GrayError*). */
        if (!checker->registering) {
            /* Error is always available (no import needed). */
            if (strcmp(name, "Error") == 0) { /* allow */ }
            /* Stdlib opaque types are valid only when their module is imported. */
            else if (!is_stdlib_opaque_type_available(checker, name)) {
                return &TYPE_UNKNOWN;
            }
        }
    }
    return resolved_type;
}

/* --- Type signedness helpers --- */

/* Check if a TypeKind is any integer type (signed or unsigned) */
static bool is_int_kind(TypeKind k) {
    return k == TK_INT || k == TK_UINT || k == TK_BYTE;
}

/* Returns true if src can be assigned to dest under the standard coercion rules.
 * Does NOT cover context-specific exceptions (nil→ptr, ref→ptr, struct↔int)
 * which callers handle separately. */
static bool types_assignable(TypeChecker *checker, GrayType *dest, GrayType *src) {
    if (!dest || !src) return false;
    /* Pointer types must match element types (^int != ^float) */
    if (dest->kind == TK_POINTER && src->kind == TK_POINTER) {
        if (dest->element_type && src->element_type)
            return strcmp(dest->element_type, src->element_type) == 0;
        return true; /* unknown element type: allow */
    }
    /* Struct types must match by name (Point != Size).
     * Use cross-module-aware comparison so "Item" matches "types_Item". */
    if (dest->kind == TK_STRUCT && src->kind == TK_STRUCT) {
        if (dest->name && src->name)
            return typechecker_same_struct_type(checker, dest->name, src->name);
        return true;
    }
    /* Enum types must match by name (Color != Dir).
     * Use cross-module-aware comparison so "Status" matches "types_Status". */
    if (dest->kind == TK_ENUM && src->kind == TK_ENUM) {
        if (dest->name && src->name)
            return typechecker_same_enum_type(checker, dest->name, src->name);
        return true;
    }
    /* Array types must match element types ([int] != [string]) */
    if (dest->kind == TK_ARRAY && src->kind == TK_ARRAY) {
        if (dest->element_type && src->element_type) {
            if (strcmp(dest->element_type, src->element_type) == 0) return true;
            /* Cross-module struct/enum names (e.g. "Item" vs "types_Item") */
            if (typechecker_same_array_element(checker, dest->element_type, src->element_type))
                return true;
            /* Resolve element types and compare recursively (e.g. int→i128, int→float) */
            GrayType *dest_et = typechecker_type_from_name(checker, dest->element_type);
            GrayType *src_et = typechecker_type_from_name(checker, src->element_type);
            if (dest_et->kind != TK_UNKNOWN && src_et->kind != TK_UNKNOWN)
                return types_assignable(checker, dest_et, src_et);
        }
        return true; /* unknown element type: allow */
    }
    if (dest->kind == src->kind) return true;
    /* Int-family interop (byte ↔ uint excluded) */
    if (is_int_kind(dest->kind) && is_int_kind(src->kind) &&
        !((dest->kind == TK_BYTE && src->kind == TK_UINT) ||
          (dest->kind == TK_UINT && src->kind == TK_BYTE)))
        return true;
    /* Enum → int (enums are int-backed) */
    if (is_int_kind(dest->kind) && src->kind == TK_ENUM) return true;
    /* Int → float coercion */
    if (dest->kind == TK_FLOAT && is_int_kind(src->kind)) return true;
    /* String enum → string */
    if (dest->kind == TK_STRING && src->kind == TK_ENUM &&
        typechecker_enum_is_string(checker, src->name))
        return true;
    return false;
}

/* Rank for named integer types; 0 = not a named integer type.
 * Used to detect narrowing (declared rank < value rank). */
static int int_type_name_rank(const char *n) {
    if (!n) return 0;
    if (strcmp(n, "i8")   == 0 || strcmp(n, "u8")   == 0 || strcmp(n, "byte") == 0) return 1;
    if (strcmp(n, "i16")  == 0 || strcmp(n, "u16")  == 0) return 2;
    if (strcmp(n, "i32")  == 0 || strcmp(n, "u32")  == 0) return 3;
    if (strcmp(n, "i64")  == 0 || strcmp(n, "u64")  == 0 ||
        strcmp(n, "int")  == 0 || strcmp(n, "uint") == 0) return 4;
    if (strcmp(n, "i128") == 0 || strcmp(n, "u128") == 0) return 5;
    if (strcmp(n, "i256") == 0 || strcmp(n, "u256") == 0) return 6;
    return 0;
}

/* True when an unsigned value of type `src_tn` is representable in the signed
 * type `dest_tn` purely by width — a value-preserving widening that needs no
 * cast (uint -> i128, byte -> int). A same-width or narrower crossing still
 * reinterprets or truncates and requires an explicit cast. */
static bool unsigned_widens_to_signed(const char *dest_tn, const char *src_tn) {
    int dr = int_type_name_rank(dest_tn);
    int sr = int_type_name_rank(src_tn);
    return dr > 0 && sr > 0 && dr > sr;
}

/* --- Literal value extraction --- */

/* Try to extract a compile-time integer value from a literal expression.
 * Handles: NODE_INT_VALUE, PREFIX(-) NODE_INT_VALUE, and simple constant
 * binary expressions on literals. Returns true if a value was extracted. */
static bool try_get_literal_int(AstNode *node, int64_t *out) {
    if (!node) return false;
    if (node->kind == NODE_INT_VALUE) {
        *out = node->data.int_value.value;
        return true;
    }
    if (node->kind == NODE_PREFIX_EXPR && node->data.prefix.op == TOK_MINUS &&
        node->data.prefix.right && node->data.prefix.right->kind == NODE_INT_VALUE) {
        *out = (int64_t)(0u - (uint64_t)node->data.prefix.right->data.int_value.value);
        return true;
    }
    /* Simple constant folding for literal +, -, *, /. Every operation is
     * guarded against int64 overflow/UB (signed overflow, and the one
     * division that traps: INT64_MIN / -1) — on overflow the expression
     * is simply treated as non-foldable rather than invoking undefined
     * behavior in the compiler's own arithmetic. */
    if (node->kind == NODE_INFIX_EXPR) {
        int64_t left_value, right_value, result;
        if (try_get_literal_int(node->data.infix.left, &left_value) &&
            try_get_literal_int(node->data.infix.right, &right_value)) {
            if (node->data.infix.op == TOK_PLUS &&
                !__builtin_add_overflow(left_value, right_value, &result)) { *out = result; return true; }
            if (node->data.infix.op == TOK_MINUS &&
                !__builtin_sub_overflow(left_value, right_value, &result)) { *out = result; return true; }
            if (node->data.infix.op == TOK_ASTERISK &&
                !__builtin_mul_overflow(left_value, right_value, &result)) { *out = result; return true; }
            if (node->data.infix.op == TOK_SLASH && right_value != 0 &&
                !(left_value == INT64_MIN && right_value == -1)) { *out = left_value / right_value; return true; }
        }
    }
    return false;
}

/* A non-literal integer value used where the other signedness is expected
 * reinterprets the bits — a negative becomes a huge positive, a large unsigned
 * becomes negative — and needs an explicit cast in either direction. var-decl,
 * reassignment, and return check this for their own value position; this
 * consolidates it so call arguments, array elements, struct fields, and map
 * values are covered too. Literals are skipped — their value is range-checked
 * separately (a negative literal to a uint is its own error). `pos` anchors the
 * diagnostic. E3019 covers a signedness crossing in either direction
 * (matching what reassignment already emits for each direction). A
 * value-preserving unsigned -> wider-signed widening is left implicit. */
static void check_signedness_crossing(TypeChecker *checker,
                                      const char *expected_tn,
                                      AstNode *value, GrayType *value_t,
                                      AstNode *pos) {
    if (!expected_tn || !value_t || !value_t->name || !pos) return;
    int64_t lit;
    if (value && try_get_literal_int(value, &lit)) return;
    if (is_unsigned_type(expected_tn) && is_signed_int_type(value_t->name)) {
        diagnostic_error_code_formatted(checker->diag, "E3019",
            NODE_FILE(checker, pos), pos->token.line, pos->token.column, 0,
            value_t->name, expected_tn);
    } else if (is_signed_int_type(expected_tn) && is_unsigned_type(value_t->name) &&
               !unsigned_widens_to_signed(expected_tn, value_t->name)) {
        diagnostic_error_message(checker->diag, "E3019",
            typechecker_format(checker,
                "type mismatch: cannot assign unsigned type '%s' to signed type '%s'; use cast(value, %s) to convert explicitly",
                value_t->name, expected_tn, expected_tn),
            NODE_FILE(checker, pos), pos->token.line, pos->token.column, 0);
    }
}

/* Register a file-scope const integer value for later constant folding. */
static void typechecker_register_const_int(TypeChecker *checker, const char *name, int64_t value) {
    if (checker->const_int_count >= checker->const_int_cap) {
        checker->const_int_cap = checker->const_int_cap ? checker->const_int_cap * 2 : 8;
        checker->const_int_names = xrealloc(checker->const_int_names,
            sizeof(const char *) * (size_t)checker->const_int_cap);
        checker->const_int_values = xrealloc(checker->const_int_values,
            sizeof(int64_t) * (size_t)checker->const_int_cap);
    }
    checker->const_int_names[checker->const_int_count] = name;
    checker->const_int_values[checker->const_int_count] = value;
    checker->const_int_count++;
}

/* Resolve a non-numeric array size identifier in a fixed-size array type
 * string like "[int,SIZE]".  If the size field is already numeric this is
 * a no-op.  Otherwise the name is looked up in const_int_names/values and
 * the type string on the var_decl node is rewritten to its numeric form
 * so that downstream code (E3052/W3003, codegen extract_array_size) sees
 * only numeric size strings.
 *
 * Emits E3125 if the identifier is not a known const int.
 * Emits E3126 if the resolved value is <= 0. */
static void typechecker_resolve_array_size(TypeChecker *checker, AstNode *node) {
    const char *tn = node->data.var_decl.type_name;
    /* Find the top-level comma separating element type from size. */
    const char *size_comma = NULL;
    int depth = 0;
    for (const char *c = tn; *c; c++) {
        if (*c == '(' || *c == '[') depth++;
        else if (*c == ')' || *c == ']') depth--;
        else if (*c == ',' && depth == 1) { size_comma = c; break; }
    }
    if (!size_comma) return; /* no size field */

    /* Extract the size substring: after comma, before closing ']' */
    const char *size_start = size_comma + 1;
    const char *rbracket = strrchr(tn, ']');
    if (!rbracket || rbracket <= size_start) return;
    size_t sz_len = (size_t)(rbracket - size_start);
    char size_buf[256];
    if (sz_len >= sizeof(size_buf)) return;
    memcpy(size_buf, size_start, sz_len);
    size_buf[sz_len] = '\0';

    /* If already numeric, nothing to resolve. */
    char *end_pointer = NULL;
    long val = strtol(size_buf, &end_pointer, 10);
    if (end_pointer && *end_pointer == '\0') {
        (void)val;
        return;
    }

    /* Look up the identifier in the const int table. */
    bool found = false;
    int64_t resolved = 0;
    for (int i = 0; i < checker->const_int_count; i++) {
        if (strcmp(checker->const_int_names[i], size_buf) == 0) {
            resolved = checker->const_int_values[i];
            found = true;
            break;
        }
    }
    if (!found) {
        char *msg = typechecker_format(checker,
            "'%s' is not a compile-time integer constant; array size must be a const int/uint value",
            size_buf);
        diagnostic_error_message(checker->diag, "E3125", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        return;
    }
    if (resolved <= 0) {
        char *msg = typechecker_format(checker,
            "array size must be greater than zero; '%s' resolves to %d",
            size_buf, (int)resolved);
        diagnostic_error_message(checker->diag, "E3126", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        return;
    }

    /* Rewrite the type string with the resolved numeric value.
     * e.g. "[int,SIZE]" → "[int,5]" */
    size_t prefix_len = (size_t)(size_comma + 1 - tn);
    char num_buf[32];
    int num_len = snprintf(num_buf, sizeof(num_buf), "%d", (int)resolved);
    size_t new_len = prefix_len + (size_t)num_len + 2; /* +1 for ']' +1 for '\0' */
    char *new_tn = arena_alloc(checker->arena, new_len);
    memcpy(new_tn, tn, prefix_len);
    memcpy(new_tn + prefix_len, num_buf, (size_t)num_len);
    new_tn[prefix_len + (size_t)num_len] = ']';
    new_tn[prefix_len + (size_t)num_len + 1] = '\0';
    node->data.var_decl.type_name = new_tn;
}

/* Try to evaluate node as a compile-time integer constant.
 * Handles integer literals, negated literals, label references to known
 * file-scope const ints, and infix arithmetic.
 *
 * Returns true if the expression folded to *out with no overflow.
 * Returns false if:
 *   - any operand is not a known constant (*overflowed unchanged), or
 *   - arithmetic overflowed int64 (*overflowed set to true).
 */
static bool typechecker_fold_const_int(TypeChecker *checker, AstNode *node,
                               int64_t *out, bool *overflowed) {
    if (!node) return false;
    if (node->kind == NODE_INT_VALUE) {
        /* overflow_u64 means the literal exceeds uint64 range entirely;
         * overflow alone only means it exceeds int64 range (still valid
         * for unsigned Grayscale types).  Only treat overflow_u64 as a hard
         * failure here since we fold using the raw int64 bit pattern. */
        if (node->data.int_value.overflow_u64) { *overflowed = true; return false; }
        *out = node->data.int_value.value;
        return true;
    }
    if (node->kind == NODE_PREFIX_EXPR && node->data.prefix.op == TOK_MINUS &&
        node->data.prefix.right && node->data.prefix.right->kind == NODE_INT_VALUE) {
        if (node->data.prefix.right->data.int_value.overflow_u64) { *overflowed = true; return false; }
        *out = (int64_t)(0u - (uint64_t)node->data.prefix.right->data.int_value.value);
        return true;
    }
    if (node->kind == NODE_LABEL) {
        const char *name = node->data.label.value;
        for (int i = 0; i < checker->const_int_count; i++) {
            if (strcmp(checker->const_int_names[i], name) == 0) {
                *out = checker->const_int_values[i];
                return true;
            }
        }
        return false;
    }
    if (node->kind == NODE_INFIX_EXPR) {
        int64_t left_value, right_value;
        bool left_overflowed = false, right_overflowed = false;
        bool left_ok = typechecker_fold_const_int(checker, node->data.infix.left, &left_value, &left_overflowed);
        bool right_ok = typechecker_fold_const_int(checker, node->data.infix.right, &right_value, &right_overflowed);
        if (!left_ok || !right_ok) {
            if (left_overflowed || right_overflowed) *overflowed = true;
            return false;
        }
        TokenType op = node->data.infix.op;
        int64_t result;
        if (op == TOK_PLUS) {
            if (__builtin_add_overflow(left_value, right_value, &result)) { *overflowed = true; return false; }
            *out = result; return true;
        }
        if (op == TOK_MINUS) {
            if (__builtin_sub_overflow(left_value, right_value, &result)) { *overflowed = true; return false; }
            *out = result; return true;
        }
        if (op == TOK_ASTERISK) {
            if (__builtin_mul_overflow(left_value, right_value, &result)) { *overflowed = true; return false; }
            *out = result; return true;
        }
        /* INT64_MIN / -1 (and the equivalent %) is the one division C
         * leaves undefined at the int64 boundary — it traps (SIGFPE) on
         * x86-64. Treat it as overflow rather than performing it. */
        bool div_by_min_neg_one = left_value == INT64_MIN && right_value == -1;
        if (op == TOK_SLASH && right_value != 0) {
            if (div_by_min_neg_one) { *overflowed = true; return false; }
            *out = left_value / right_value; return true;
        }
        if (op == TOK_PERCENT && right_value != 0) {
            if (div_by_min_neg_one) { *overflowed = true; return false; }
            *out = left_value % right_value; return true;
        }
    }
    return false;
}

/* Extract a compile-time numeric value from an argument: a float literal or an
 * integer constant (literal, const, folded arithmetic), each optionally
 * negated. Returns false when the argument is not a decidable constant. */
static bool typechecker_const_number(TypeChecker *checker, AstNode *n, double *out) {
    if (!n) return false;
    if (n->kind == NODE_FLOAT_VALUE) { *out = n->data.float_value.value; return true; }
    if (n->kind == NODE_PREFIX_EXPR && n->data.prefix.op == TOK_MINUS &&
        n->data.prefix.right && n->data.prefix.right->kind == NODE_FLOAT_VALUE) {
        *out = -n->data.prefix.right->data.float_value.value;
        return true;
    }
    int64_t iv; bool overflowed = false;
    if (typechecker_fold_const_int(checker, n, &iv, &overflowed) && !overflowed) {
        *out = (double)iv;
        return true;
    }
    return false;
}

/* Reject a stdlib call whose compile-time-constant argument is provably outside
 * the domain the implementation guards with a runtime panic. The runtime checks
 * (P0064-P0070, P0072, P0106, P0051, P0063) stay in place for every
 * non-constant argument; this only moves the decidable cases to compile time.
 * Precedent: typechecker_check_strconv_base / E5009. Emits E5045. */
static void typechecker_check_const_domain(TypeChecker *checker, const char *mod,
    const char *fn, AstNode *node)
{
    int argc = node->data.call.arg_count;

    /* Integer domains, [lo, hi] inclusive. */
    static const struct {
        const char *mod, *fn;
        int arg;
        int64_t lo, hi;
        const char *needs;
    } int_dom[] = {
        {"math",    "factorial",         0, 0,         INT64_MAX,         "a non-negative integer"},
        {"math",    "next_power_of_two", 0, INT64_MIN, (int64_t)1 << 62,  "a value whose next power of two fits in int"},
        {"strings", "repeat",            1, 0,         INT64_MAX,         "a non-negative count"},
        {"crypto",  "random_hex",        0, 0,         INT64_MAX,         "a non-negative length"},
        {"random",  "sample",            1, 0,         INT64_MAX,         "a non-negative count"},
    };
    for (size_t i = 0; i < sizeof(int_dom) / sizeof(int_dom[0]); i++) {
        if (strcmp(mod, int_dom[i].mod) != 0 || strcmp(fn, int_dom[i].fn) != 0) continue;
        if (int_dom[i].arg >= argc) return;
        int64_t v; bool overflowed = false;
        if (!typechecker_fold_const_int(checker, node->data.call.args[int_dom[i].arg], &v, &overflowed)
            || overflowed) return;
        if (v < int_dom[i].lo || v > int_dom[i].hi) {
            AstNode *a = node->data.call.args[int_dom[i].arg];
            char *msg = typechecker_format(checker, "'%s.%s' requires %s; got %lld",
                mod, fn, int_dom[i].needs, (long long)v);
            diagnostic_error_message(checker->diag, "E5045", msg,
                NODE_FILE(checker, node), a->token.line, a->token.column, 0);
        }
        return;
    }

    /* Float domains for the math functions that accept ARG_NUMBER. */
    if (strcmp(mod, "math") != 0 || argc < 1) return;
    double x;
    if (!typechecker_const_number(checker, node->data.call.args[0], &x)) return;
    const char *needs = NULL;
    if (strcmp(fn, "sqrt") == 0) { if (x < 0.0) needs = "a non-negative number"; }
    else if (strcmp(fn, "log") == 0 || strcmp(fn, "log2") == 0 || strcmp(fn, "log10") == 0) {
        if (x <= 0.0) needs = "a positive number";
    }
    else if (strcmp(fn, "asin") == 0 || strcmp(fn, "acos") == 0) {
        if (x < -1.0 || x > 1.0) needs = "a value in [-1, 1]";
    }
    if (needs) {
        AstNode *a = node->data.call.args[0];
        char *msg = typechecker_format(checker, "'math.%s' requires %s; got %g", fn, needs, x);
        diagnostic_error_message(checker->diag, "E5045", msg,
            NODE_FILE(checker, node), a->token.line, a->token.column, 0);
    }
}

/* Like try_get_literal_int, but also reports whether the literal is a
 * genuinely negative value, as opposed to a large non-negative magnitude in
 * (INT64_MAX, UINT64_MAX] whose int64 bit pattern only looks negative. A bare
 * NODE_INT_VALUE always holds a magnitude (the parser stores the `-` sign as a
 * prefix expression), so only folded expressions and unary-minus can be
 * negative. */
static bool try_get_signed_literal_int(AstNode *node, int64_t *out, bool *is_negative) {
    if (!try_get_literal_int(node, out)) return false;
    *is_negative = (node && node->kind == NODE_INT_VALUE) ? false : (*out < 0);
    return true;
}

/* Check if a literal integer value fits in the declared sized type.
 * Returns true if an error was emitted.
 *
 * value carries the parsed bit pattern; value_is_negative distinguishes a
 * true negative from a non-negative magnitude whose top bit is set (a literal
 * above INT64_MAX). For unsigned targets the magnitude is compared as
 * uint64_t so UINT64_MAX itself is accepted by uint/u64. */
static bool check_integer_range(DiagnosticList *diag, const char *file,
    int line, int col, const char *type_name_str, int64_t value,
    bool value_is_negative) {
    int64_t min_val = 0, max_val = 0;
    bool is_unsigned = false;
    bool is_u64 = false;

    if (strcmp(type_name_str, "i8") == 0)        { min_val = -128; max_val = 127; }
    else if (strcmp(type_name_str, "i16") == 0)   { min_val = -32768; max_val = 32767; }
    else if (strcmp(type_name_str, "i32") == 0)   { min_val = -2147483648LL; max_val = 2147483647; }
    else if (strcmp(type_name_str, "u8") == 0)    { min_val = 0; max_val = 255; is_unsigned = true; }
    else if (strcmp(type_name_str, "u16") == 0)   { min_val = 0; max_val = 65535; is_unsigned = true; }
    else if (strcmp(type_name_str, "u32") == 0)   { min_val = 0; max_val = 4294967295LL; is_unsigned = true; }
    else if (strcmp(type_name_str, "u64") == 0)   { is_unsigned = true; is_u64 = true; }
    else if (strcmp(type_name_str, "uint") == 0)  { is_unsigned = true; is_u64 = true; }
    else if (strcmp(type_name_str, "byte") == 0)  { min_val = 0; max_val = 255; is_unsigned = true; }
    else return false; /* not a range-checked type */

    bool out_of_range;
    if (is_u64) {
        out_of_range = value_is_negative; /* 0..UINT64_MAX all fit */
    } else if (value_is_negative) {
        out_of_range = is_unsigned || value < min_val;
    } else {
        out_of_range = (uint64_t)value > (uint64_t)max_val;
    }
    if (!out_of_range) return false;

    char valbuf[24];
    if (value_is_negative)
        snprintf(valbuf, sizeof(valbuf), "%lld", (long long)value);
    else
        snprintf(valbuf, sizeof(valbuf), "%llu", (unsigned long long)value);

    char range_hi[24];
    if (is_u64)
        snprintf(range_hi, sizeof(range_hi), "%llu", (unsigned long long)UINT64_MAX);
    else
        snprintf(range_hi, sizeof(range_hi), "%lld", (long long)max_val);

    char msg[MSG_BUF_SIZE];
    if (is_unsigned && value_is_negative) {
        snprintf(msg, sizeof(msg),
            "value %s is out of range for type '%s'; unsigned types cannot hold negative values (valid range: 0 to %s)",
            valbuf, type_name_str, range_hi);
    } else {
        snprintf(msg, sizeof(msg),
            "value %s is out of range for type '%s' (valid range: %lld to %s)",
            valbuf, type_name_str, (long long)min_val, range_hi);
    }
    diagnostic_error_message(diag, "E3036", strdup(msg), file, line, col, 0);
    return true;
}

/* --- Expression type resolution --- */

/* : shared void-expression guard. Emits E3038 at `expr` when `t`
 * is TK_VOID. `context` is a short phrase describing what the
 * position wants ("println argument", "map value", "binary operand",
 * etc.). If `expr` is a direct call to a named function, the error
 * quotes the function name; otherwise it falls back to a generic
 * "void expression" wording. Caller-suppliable context keeps each
 * diagnostic site self-describing without a zillion format strings. */
static AstNode *find_struct_in_program(AstNode *program, const char *name);

static void reject_void_in_context(TypeChecker *checker, AstNode *expr,
                                    GrayType *t, const char *context) {
    if (!t || t->kind != TK_VOID || !expr) return;
    char *msg = NULL;
    if (expr->kind == NODE_CALL_EXPR && expr->data.call.function &&
        expr->data.call.function->kind == NODE_LABEL) {
        msg = typechecker_format(checker,
            "cannot use void function '%s' as %s; the function does not return a value",
            expr->data.call.function->data.label.value, context);
    } else {
        msg = typechecker_format(checker,
            "cannot use void expression as %s; the expression does not produce a value",
            context);
    }
    diagnostic_error_message(checker->diag, "E3038", msg,
        NODE_FILE(checker, expr), expr->token.line, expr->token.column, 0);
}

/* Classify a call whose result is multiple values and emit the right
 * diagnostic anchored at `at`: E3040 for a user-defined multi-return or a
 * non-fallible multi-value stdlib function, E3089 for a fallible stdlib
 * function's (T, Error). No-op for a single-value call. Shared by the
 * var-decl single-variable check and the single-value position guard. */
static void reject_multi_value_call(TypeChecker *checker, AstNode *call_expr,
                                    AstNode *at) {
    if (!call_expr || call_expr->kind != NODE_CALL_EXPR) return;
    AstNode *fn = call_expr->data.call.function;
    if (!fn) return;
    const char *name = NULL;
    const char *mod = NULL;
    FuncSig *sig = NULL;
    if (fn->kind == NODE_LABEL) {
        name = fn->data.label.value;
        sig = find_func(checker, name);
        /* A bare call carries no module qualifier; resolve it through the
         * using-modules so the stdlib checks below consult the module that
         * actually supplies it. */
        if (!sig) mod = find_using_stdlib_module(checker, name);
    } else if (ast_member_qualifier(fn)) {
        const char *mod_raw = ast_member_qualifier(fn);
        mod = typechecker_resolve_alias(checker, mod_raw);
        name = fn->data.member.member;
        sig = find_module_func(checker, mod_raw, name);
    }
    const char *file = NODE_FILE(checker, at);
    int line = at->token.line;
    int col = at->token.column;
    if (sig && sig->return_count > 1) {
        diagnostic_error_code_formatted(checker->diag, "E3040", file, line, col, 0,
            name, sig->return_count, name);
    } else if (name && !sig) {
        if (typechecker_is_fallible_stdlib(mod, name)) {
            diagnostic_error_code_formatted(checker->diag, "E3089", file, line, col, 0,
                name, name, name);
        } else {
            const StdlibMultiReturn *mr = find_stdlib_multi_return(mod, name);
            if (mr) {
                diagnostic_error_code_formatted(checker->diag, "E3040", file, line, col, 0,
                    name, mr->count, name);
            }
        }
    }
}

/* E3040/E3089: reject a multi-value call in a single-value position (call
 * argument, operand, array element, map value, return/if/when position). */
static void reject_multi_return_in_single_position(TypeChecker *checker, AstNode *expr) {
    reject_multi_value_call(checker, expr, expr);
}

/* : emit E4005 at a stdlib call site where the function name
 * isn't recognized. Shared between every module dispatch branch that
 * has a fallthrough "unknown function" else. Without this, typing
 * `strings.totally_fake_function()` silently types as `string` (or
 * whatever the module's default-else set) and cascades into a
 * misleading downstream "cannot assign string to int" diagnostic,
 * hiding the real bug. */
static void emit_unknown_stdlib_function(TypeChecker *checker, const char *mod,
                                    const char *mfn, AstNode *node) {
    diagnostic_error_code_formatted(checker->diag, "E4005", NODE_FILE(checker, node), node->token.line, node->token.column, 0, mod, mfn);
}

/* Resolve .VARIANT implicit enum selector using expected type context.
 * Sets node->data.implicit_enum.resolved_enum on success. */
static GrayType *resolve_implicit_enum(TypeChecker *checker, AstNode *node) {
    const char *variant = node->data.implicit_enum.variant;
    GrayType *expected = checker->expected_type;

    /* No type context — emit E3110 */
    if (!expected || expected->kind != TK_ENUM || !expected->name) {
        diagnostic_error_code_formatted(checker->diag, "E3110",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            variant, variant);
        return &TYPE_UNKNOWN;
    }

    const char *enum_name = expected->name;

    /* Validate the variant exists in the enum */
    bool found = false;
    {
        /* find_enum_index resolves the name as written; a linear scan over
         * enum_names compares against the registry spelling directly and
         * misses a bare name inside its own module. */
        int enum_index = find_enum_index(checker, enum_name);
        if (enum_index >= 0) {
            for (int variant_index = 0; variant_index < checker->enum_value_counts[enum_index]; variant_index++) {
                if (strcmp(checker->enum_values[enum_index][variant_index], variant) == 0) {
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        diagnostic_error_code_formatted(checker->diag, "E3047",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            enum_name, variant);
        return &TYPE_UNKNOWN;
    }

    /* Resolve: write the enum name onto the node for codegen */
    node->data.implicit_enum.resolved_enum = enum_name;
    return type_enum(enum_name);
}

/* Point a member expression's object at `type_name`. This is the
 * instance-dispatch rewrite: `v.f()` becomes `Type.f()` so that by the time
 * codegen sees it, it is indistinguishable from a call written that way. An
 * object that is not a plain name — the explicit deref in `p^.f()` — is
 * replaced by one rather than edited. */
static void retarget_member_object(AstNode *member, const char *type_name) {
    if (ast_member_qualifier(member)) {
        member->data.member.object->data.label.value = strdup(type_name);
        return;
    }
    AstNode *label = xcalloc(1, sizeof(AstNode));
    label->kind = NODE_LABEL;
    label->token = member->data.member.object->token;
    label->data.label.value = strdup(type_name);
    member->data.member.object = label;
}

/* E3027: validate that an argument passed to a mutable (&) parameter is a
 * mutable assignment target. Emits a diagnostic and returns if the argument
 * is a constant variable, an enum constant, or a literal/expression. */
static void check_mutable_arg(TypeChecker *checker, AstNode *arg,
                               const char *param_desc, const char *func_display) {
    const char *qualifier = ast_member_qualifier(arg);
    if (arg->kind == NODE_LABEL) {
        Symbol *sym = scope_lookup(checker->current_scope, arg->data.label.value);
        if (sym && !sym->mutable) {
            char *msg = typechecker_format(checker,
                "cannot pass constant '%s' to %s of '%s'",
                sym->name, param_desc, func_display);
            diagnostic_error_message(checker->diag, "E3027", msg,
                NODE_FILE(checker, arg), arg->token.line, arg->token.column, 0);
        }
    } else if (qualifier && is_enum_name(checker, qualifier)) {
        char *msg = typechecker_format(checker,
            "cannot pass enum constant to %s of '%s'; expected a mutable variable",
            param_desc, func_display);
        diagnostic_error_message(checker->diag, "E3027", msg,
            NODE_FILE(checker, arg), arg->token.line, arg->token.column, 0);
    } else if (arg->kind != NODE_MEMBER_EXPR &&
               arg->kind != NODE_INDEX_EXPR &&
               arg->kind != NODE_PREFIX_EXPR) {
        char *msg = typechecker_format(checker,
            "cannot pass a literal or expression to %s of '%s'; expected a mutable variable",
            param_desc, func_display);
        diagnostic_error_message(checker->diag, "E3027", msg,
            NODE_FILE(checker, arg), arg->token.line, arg->token.column, 0);
    }
}

static GrayType *resolve_stdlib_call(TypeChecker *checker, AstNode *node, const char *mod, const char *mfn) {
    GrayType *result = &TYPE_UNKNOWN;
    /* E5034: named arguments are not supported for stdlib functions */
    if (typechecker_has_named_arguments(node)) {
        char fname[MSG_BUF_SIZE];
        snprintf(fname, sizeof(fname), "%s.%s", mod, mfn);
        char *msg = NULL;
        msg = typechecker_format(checker,
            "named arguments are not supported for builtin function '%s'",
            fname);
        diagnostic_error_message(checker->diag, "E5034", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Validate argument count for stdlib function calls */
    typechecker_check_stdlib_arg_count(checker, mod, mfn, node);
    typechecker_check_stdlib_arg_types(checker, mod, mfn, node);
    typechecker_check_strconv_base(checker, mod, mfn, node);
    typechecker_check_io_read_lines_limit(checker, mod, mfn, node);
    typechecker_check_const_domain(checker, mod, mfn, node);
    /* Table-driven return type resolution: O(log n) bsearch */
    const StdlibFuncMeta *meta = find_stdlib_meta(mod, mfn);
    if (meta && meta->return_type) {
        result = resolve_return_type(meta->return_type);
    }
    /* Context-dependent return types (meta->return_type == NULL) */
    if (strcmp(mod, "mem") == 0) {
        if (strcmp(mfn, "init") == 0 && node->data.call.arg_count == 2) {
            AstNode *type_arg = node->data.call.args[1];
            /* Point at what the name reaches: an alias bound as itself gave a
             * pointer type that no annotation of the underlying type accepts,
             * and codegen has no C type named after the alias. A name that
             * reaches no type was already reported; stay unknown so the
             * assignment does not report it a second time. */
            if (type_arg->kind == NODE_LABEL &&
                type_arg_names_a_type(checker, type_arg->data.label.value)) {
                const char *target = resolve_type_alias(checker,
                    checker_resolve_type_name(checker, type_arg->data.label.value));
                if (target && strcmp(target, type_arg->data.label.value) != 0)
                    type_arg->data.label.value = target;
                result = type_pointer(type_arg->data.label.value);
            } else {
                result = &TYPE_UNKNOWN;
            }
        } else if (strcmp(mfn, "alloc") == 0 && node->data.call.arg_count == 2) {
            /* alloc(a Arena, value T) -> ^T. Typing the call as T instead
             * disagreed with the pointer codegen emits, so reading the result
             * produced C that does not compile, and the documented `mut p ^int
             * = mem.alloc(a, 42)` was rejected outright. */
            GrayType *value_t = resolve_expression(checker, node->data.call.args[1]);
            const char *value_tn = value_t ? type_name(value_t) : NULL;
            result = (value_t && value_t->kind != TK_UNKNOWN && value_tn)
                ? type_pointer(value_tn) : &TYPE_UNKNOWN;
        }
    } else if (strcmp(mod, "maps") == 0) {
        if (strcmp(mfn, "get_keys") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *map_t = resolve_expression(checker, node->data.call.args[0]);
                result = type_array(map_t && map_t->key_type ? map_t->key_type : "string");
            } else result = type_array("string");
        } else if (strcmp(mfn, "get_values") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *map_t = resolve_expression(checker, node->data.call.args[0]);
                result = type_array(map_t && map_t->value_type ? map_t->value_type : "string");
            } else result = type_array("string");
        } else if (strcmp(mfn, "merge") == 0) {
            if (node->data.call.arg_count > 0) {
                result = resolve_expression(checker, node->data.call.args[0]);
            } else result = &TYPE_UNKNOWN;
        } else if (strcmp(mfn, "get_or_default") == 0) {
            /* get_or_default(m, key, default) -> V. Take V from the map's
             * declared value type; the default argument's type can be a looser
             * literal (e.g. a bare int where V is i128 or float). */
            GrayType *map_t = node->data.call.arg_count >= 1
                ? resolve_expression(checker, node->data.call.args[0]) : NULL;
            if (map_t && map_t->kind == TK_MAP && map_t->value_type) {
                result = typechecker_type_from_name(checker, map_t->value_type);
            } else if (node->data.call.arg_count >= 3) {
                result = resolve_expression(checker, node->data.call.args[2]);
            } else result = &TYPE_UNKNOWN;
        }
        /* E12001: maps functions require map argument */
        if (node->data.call.arg_count > 0) {
            AstNode *arg0 = node->data.call.args[0];
            GrayType *arg0_t = resolve_expression(checker, arg0);
            if (arg0_t && arg0_t->kind == TK_ARRAY) {
                diagnostic_error_code_formatted(checker->diag, "E12001", NODE_FILE(checker, arg0), arg0->token.line, arg0->token.column, 0, mfn);
            }
        }
        if (strcmp(mfn, "is_equal") == 0 && node->data.call.arg_count >= 2) {
            AstNode *a0 = node->data.call.args[0];
            AstNode *a1 = node->data.call.args[1];
            GrayType *t0 = typetable_get(checker->type_table, a0);
            GrayType *t1 = typetable_get(checker->type_table, a1);
            if (t0 && t1 && t0->kind == TK_MAP && t1->kind == TK_MAP) {
                bool key_match = t0->key_type && t1->key_type &&
                    strcmp(t0->key_type, t1->key_type) == 0;
                bool val_match = t0->value_type && t1->value_type &&
                    strcmp(t0->value_type, t1->value_type) == 0;
                if (!key_match || !val_match) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "type mismatch: cannot compare map[%s:%s] with map[%s:%s]",
                        t0->key_type ? t0->key_type : "?",
                        t0->value_type ? t0->value_type : "?",
                        t1->key_type ? t1->key_type : "?",
                        t1->value_type ? t1->value_type : "?");
                    diagnostic_error_message(checker->diag, "E3156", msg,
                        NODE_FILE(checker, a1), a1->token.line, a1->token.column, 0);
                }
                const char *bad_member = NULL;
                if (t0->value_type) {
                    GrayType *vt = type_from_name(t0->value_type);
                    if (vt->kind == TK_ARRAY || vt->kind == TK_MAP || vt->kind == TK_STRUCT)
                        bad_member = t0->value_type;
                }
                if (bad_member) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "maps.is_equal does not support maps with %s values; only primitive and string element types are supported",
                        bad_member);
                    tc_err_arg_type(checker, a0, msg);
                }
            }
        }
        if (strcmp(mfn, "contains_value") == 0 && node->data.call.arg_count >= 1) {
            AstNode *a0 = node->data.call.args[0];
            GrayType *t0 = typetable_get(checker->type_table, a0);
            if (t0 && t0->kind == TK_MAP && t0->value_type) {
                GrayType *vt = type_from_name(t0->value_type);
                if (vt->kind == TK_ARRAY || vt->kind == TK_MAP || vt->kind == TK_STRUCT) {
                    diagnostic_error_code_formatted(checker->diag, "E12007",
                        NODE_FILE(checker, a0), a0->token.line, a0->token.column, 0,
                        t0->value_type);
                }
            }
        }
        /* E5007: mutating map functions on const map */
        if ((strcmp(mfn, "clear") == 0 || strcmp(mfn, "remove_key") == 0) &&
            node->data.call.arg_count > 0) {
            AstNode *arg0 = node->data.call.args[0];
            if (arg0->kind == NODE_LABEL) {
                Symbol *sym = scope_lookup(checker->current_scope, arg0->data.label.value);
                if (sym && !sym->mutable) {
                    diagnostic_error_code_formatted(checker->diag, "E5007",
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                        "map", arg0->data.label.value);
                }
            }
        }
    } else if (strcmp(mod, "math") == 0) {
        /* abs/neg/min/max/clamp: return type matches argument type */
        if (strcmp(mfn, "abs") == 0 || strcmp(mfn, "neg") == 0 ||
            strcmp(mfn, "min") == 0 || strcmp(mfn, "max") == 0 ||
            strcmp(mfn, "clamp") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *arg_t = resolve_expression(checker, node->data.call.args[0]);
                result = (arg_t && arg_t->kind == TK_FLOAT) ? &TYPE_FLOAT : &TYPE_INT;
            } else {
                result = &TYPE_INT;
            }
        }
    } else if (strcmp(mod, "random") == 0) {
        if (strcmp(mfn, "shuffle") == 0 || strcmp(mfn, "sample") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *arr_t = resolve_expression(checker, node->data.call.args[0]);
                if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type)
                    result = type_array(arr_t->element_type);
                else
                    result = type_array("int");
            } else {
                result = type_array("int");
            }
        } else if (strcmp(mfn, "choice") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *arr_t = resolve_expression(checker, node->data.call.args[0]);
                if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type)
                    result = type_from_name(arr_t->element_type);
                else
                    result = &TYPE_INT;
            } else {
                result = &TYPE_INT;
            }
        }
    } else if (strcmp(mod, "arrays") == 0) {
        /* Context-dependent array return types */
        if (strcmp(mfn, "reverse") == 0 || strcmp(mfn, "slice") == 0 ||
            strcmp(mfn, "concat") == 0 || strcmp(mfn, "deduplicate") == 0 ||
            strcmp(mfn, "map") == 0 || strcmp(mfn, "filter") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *arr_t = resolve_expression(checker, node->data.call.args[0]);
                result = (arr_t && arr_t->element_type) ? type_array(arr_t->element_type) : type_array("int");
            } else {
                result = type_array("int");
            }
        } else if (strcmp(mfn, "flatten") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *arr_t = resolve_expression(checker, node->data.call.args[0]);
                if (arr_t && arr_t->element_type) {
                    GrayType *inner = type_from_name(arr_t->element_type);
                    if (inner && inner->kind == TK_ARRAY && inner->element_type)
                        result = type_array(inner->element_type);
                    else
                        result = type_array(arr_t->element_type);
                } else {
                    result = type_array("int");
                }
            } else {
                result = type_array("int");
            }
        } else if (strcmp(mfn, "get_first") == 0 || strcmp(mfn, "get_last") == 0 ||
                   strcmp(mfn, "remove_last") == 0 || strcmp(mfn, "remove_first") == 0 ||
                   strcmp(mfn, "reduce") == 0) {
            if (node->data.call.arg_count > 0) {
                GrayType *arr_t = resolve_expression(checker, node->data.call.args[0]);
                result = (arr_t && arr_t->element_type) ? type_from_name(arr_t->element_type) : &TYPE_INT;
            } else {
                result = &TYPE_INT;
            }
        }
        /* E5007: mutating array functions on const array */
        if ((strcmp(mfn, "append") == 0 || strcmp(mfn, "insert_at") == 0 ||
             strcmp(mfn, "remove") == 0 || strcmp(mfn, "remove_at") == 0 ||
             strcmp(mfn, "remove_last") == 0 ||
             strcmp(mfn, "remove_first") == 0 || strcmp(mfn, "prepend") == 0 ||
             strcmp(mfn, "fill") == 0 ||
             strcmp(mfn, "sort_asc") == 0 || strcmp(mfn, "sort_desc") == 0 ||
             strcmp(mfn, "clear") == 0) &&
            node->data.call.arg_count > 0) {
            AstNode *arg0 = node->data.call.args[0];
            if (arg0->kind == NODE_LABEL) {
                Symbol *sym = scope_lookup(checker->current_scope, arg0->data.label.value);
                if (sym && !sym->mutable) {
                    diagnostic_error_code_formatted(checker->diag, "E5007",
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                        "array", arg0->data.label.value);
                }
            }
        }
        /* E5026: arrays.append/prepend/insert_at element type mismatch */
        {
            AstNode *val_node = NULL;
            const char *op_name = NULL;
            if ((strcmp(mfn, "append") == 0 || strcmp(mfn, "prepend") == 0) &&
                node->data.call.arg_count >= 2) {
                val_node = node->data.call.args[1];
                op_name = mfn;
            } else if (strcmp(mfn, "insert_at") == 0 && node->data.call.arg_count >= 3) {
                val_node = node->data.call.args[2];
                op_name = "insert_at";
            }
            if (val_node && op_name) {
                AstNode *arr_arg = node->data.call.args[0];
                GrayType *arr_t = typetable_get(checker->type_table, arr_arg);
                if (!arr_t) arr_t = resolve_expression(checker, arr_arg);
                GrayType *val_t = resolve_expression(checker, val_node);
                if (arr_t && arr_t->kind != TK_ARRAY && arr_t->kind != TK_UNKNOWN) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "'arrays.%s()' expects an array as the first argument, got '%s'",
                        op_name, type_name(arr_t));
                    tc_err_arg_type(checker, arr_arg, msg);
                } else if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type &&
                    val_t && val_t->kind != TK_UNKNOWN) {
                    GrayType *elem_t = type_from_name(arr_t->element_type);
                    if (elem_t->kind != TK_UNKNOWN && elem_t->kind != val_t->kind &&
                        !(is_int_kind(elem_t->kind) && is_int_kind(val_t->kind))) {
                        char *msg = NULL;
                        msg = typechecker_format(checker,
                            "type mismatch in 'arrays.%s()'; cannot add '%s' to array of '%s'",
                            op_name, type_name(val_t), arr_t->element_type);
                        tc_err_arg_type(checker, val_node, msg);
                    } else if (is_int_kind(elem_t->kind) && is_int_kind(val_t->kind)) {
                        /* A narrow element slot must reject an oversized value the
                         * same way `xs[i] = value` does: E3036 for an out-of-range
                         * literal, E3019 for a signedness crossing. */
                        int64_t lit_val;
                        bool lit_neg;
                        if (try_get_signed_literal_int(val_node, &lit_val, &lit_neg)) {
                            check_integer_range(checker->diag, NODE_FILE(checker, val_node),
                                val_node->token.line, val_node->token.column,
                                arr_t->element_type, lit_val, lit_neg);
                        }
                        check_signedness_crossing(checker, arr_t->element_type,
                            val_node, val_t, val_node);
                    }
                }
            }
        }
        /* E5026: arrays.remove_at/insert_at index must be int */
        if ((strcmp(mfn, "remove_at") == 0 && node->data.call.arg_count >= 2) ||
            (strcmp(mfn, "insert_at") == 0 && node->data.call.arg_count >= 2)) {
            AstNode *idx_node = node->data.call.args[1];
            GrayType *idx_t = resolve_expression(checker, idx_node);
            if (idx_t && idx_t->kind != TK_UNKNOWN && !is_int_kind(idx_t->kind)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'arrays.%s()' expects an int index, got '%s'",
                    mfn, type_name(idx_t));
                tc_err_arg_type(checker, idx_node, msg);
            }
        }
        /* E9002: arrays.sum/min/max require numeric array */
        if ((strcmp(mfn, "sum") == 0 || strcmp(mfn, "min") == 0 ||
             strcmp(mfn, "max") == 0 || strcmp(mfn, "get_sum") == 0 ||
             strcmp(mfn, "get_min") == 0 || strcmp(mfn, "get_max") == 0) &&
            node->data.call.arg_count > 0) {
            AstNode *arg0 = node->data.call.args[0];
            GrayType *arr_t = resolve_expression(checker, arg0);
            if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type) {
                GrayType *elem_t = type_from_name(arr_t->element_type);
                if (elem_t->kind == TK_STRING || elem_t->kind == TK_BOOL) {
                    diagnostic_error_code_formatted(checker->diag, "E9002", NODE_FILE(checker, arg0), arg0->token.line, arg0->token.column, 0, mfn, arr_t->element_type);
                }
            }
        }
        /* E5026: arrays.concat element type mismatch */
        if (strcmp(mfn, "concat") == 0 && node->data.call.arg_count >= 2) {
            AstNode *a0 = node->data.call.args[0];
            AstNode *a1 = node->data.call.args[1];
            GrayType *t0 = typetable_get(checker->type_table, a0);
            GrayType *t1 = typetable_get(checker->type_table, a1);
            if (t0 && t1 && t0->kind == TK_ARRAY && t1->kind == TK_ARRAY &&
                t0->element_type && t1->element_type &&
                strcmp(t0->element_type, t1->element_type) != 0) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "type mismatch: cannot concat array of %s with array of %s",
                    t0->element_type, t1->element_type);
                tc_err_arg_type(checker, a1, msg);
            }
        }
        if (strcmp(mfn, "is_equal") == 0 && node->data.call.arg_count >= 2) {
            AstNode *a0 = node->data.call.args[0];
            AstNode *a1 = node->data.call.args[1];
            GrayType *t0 = typetable_get(checker->type_table, a0);
            GrayType *t1 = typetable_get(checker->type_table, a1);
            if (t0 && t1 && t0->kind == TK_ARRAY && t1->kind == TK_ARRAY &&
                t0->element_type && t1->element_type &&
                strcmp(t0->element_type, t1->element_type) != 0) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "type mismatch: cannot compare array of %s with array of %s",
                    t0->element_type, t1->element_type);
                diagnostic_error_message(checker->diag, "E3156", msg,
                    NODE_FILE(checker, a1), a1->token.line, a1->token.column, 0);
            }
            if (t0 && t0->kind == TK_ARRAY && t0->element_type) {
                GrayType *et = type_from_name(t0->element_type);
                if (et->kind == TK_ARRAY || et->kind == TK_MAP || et->kind == TK_STRUCT) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "arrays.is_equal does not support arrays of %s; only primitive and string element types are supported",
                        t0->element_type);
                    tc_err_arg_type(checker, a0, msg);
                }
            }
        }
        if (strcmp(mfn, "contains") == 0 && node->data.call.arg_count >= 1) {
            AstNode *a0 = node->data.call.args[0];
            GrayType *t0 = typetable_get(checker->type_table, a0);
            if (t0 && t0->kind == TK_ARRAY && t0->element_type) {
                GrayType *et = type_from_name(t0->element_type);
                if (et->kind == TK_ARRAY || et->kind == TK_MAP || et->kind == TK_STRUCT) {
                    diagnostic_error_code_formatted(checker->diag, "E9006",
                        NODE_FILE(checker, a0), a0->token.line, a0->token.column, 0,
                        t0->element_type);
                }
            }
        }
        /* E9003/E9004: map/filter/reduce callback validation */
        if ((strcmp(mfn, "map") == 0 || strcmp(mfn, "filter") == 0 ||
             strcmp(mfn, "reduce") == 0 ||
             strcmp(mfn, "any") == 0 || strcmp(mfn, "all") == 0) && node->data.call.arg_count >= 2) {
            int cb_idx = (strcmp(mfn, "reduce") == 0) ? 2 : 1;
            if (cb_idx < node->data.call.arg_count) {
                AstNode *cb_arg = node->data.call.args[cb_idx];
                if (cb_arg->kind != NODE_FUNC_REF &&
                    !(cb_arg->kind == NODE_CALL_EXPR &&
                      cb_arg->data.call.function->kind == NODE_LABEL &&
                      strcmp(cb_arg->data.call.function->data.label.value, "ref") == 0)) {
                    diagnostic_error_code_formatted(checker->diag, "E9003",
                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                        mfn);
                } else {
                    const char *ref_name = NULL;
                    if (cb_arg->kind == NODE_FUNC_REF &&
                        cb_arg->data.func_ref.function->kind == NODE_LABEL) {
                        ref_name = cb_arg->data.func_ref.function->data.label.value;
                    }
                    if (ref_name) {
                        FuncSig *cb_fs = find_func(checker, ref_name);
                        if (cb_fs) {
                            AstNode *arr_arg = node->data.call.args[0];
                            GrayType *arr_t = typetable_get(checker->type_table, arr_arg);
                            if (!arr_t) arr_t = resolve_expression(checker, arr_arg);
                            const char *elem_tn = (arr_t && arr_t->element_type) ? arr_t->element_type : NULL;

                            if (strcmp(mfn, "map") == 0) {
                                if (cb_fs->param_count != 1) {
                                    char *msg = NULL;
                                    msg = typechecker_format(checker,
                                        "map callback must take 1 parameter, got %d",
                                        cb_fs->param_count);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                } else if (cb_fs->return_count < 1) {
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, "map callback must return a value");
                                } else if (elem_tn && cb_fs->param_types[0] &&
                                           strcmp(type_name(cb_fs->param_types[0]), elem_tn) != 0) {
                                    char *msg = typechecker_format(checker,
                                        "map callback takes '%s' but array element type is '%s'",
                                        type_name(cb_fs->param_types[0]), elem_tn);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                } else if (elem_tn && cb_fs->return_count >= 1 &&
                                           cb_fs->return_types[0] &&
                                           strcmp(type_name(cb_fs->return_types[0]), elem_tn) != 0) {
                                    char *msg = typechecker_format(checker,
                                        "map callback must return the same type as the array element type (%s), got '%s'",
                                        elem_tn, type_name(cb_fs->return_types[0]));
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                }
                            } else if (strcmp(mfn, "filter") == 0 ||
                                       strcmp(mfn, "any") == 0 ||
                                       strcmp(mfn, "all") == 0) {
                                if (cb_fs->param_count != 1) {
                                    char *msg = NULL;
                                    msg = typechecker_format(checker,
                                        "%s callback must take 1 parameter, got %d",
                                        mfn, cb_fs->param_count);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                } else if (cb_fs->return_count < 1 ||
                                           cb_fs->return_types[0]->kind != TK_BOOL) {
                                    char *msg = NULL;
                                    msg = typechecker_format(checker,
                                        "%s callback must return bool", mfn);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                } else if (elem_tn && cb_fs->param_types[0] &&
                                           strcmp(type_name(cb_fs->param_types[0]), elem_tn) != 0) {
                                    char *msg = typechecker_format(checker,
                                        "%s callback takes '%s' but array element type is '%s'",
                                        mfn, type_name(cb_fs->param_types[0]), elem_tn);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                }
                            } else { /* reduce */
                                if (cb_fs->param_count != 2) {
                                    char *msg = NULL;
                                    msg = typechecker_format(checker,
                                        "reduce callback must take 2 parameters, got %d",
                                        cb_fs->param_count);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                } else if (cb_fs->return_count < 1) {
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, "reduce callback must return a value");
                                } else if (elem_tn && cb_fs->param_types[1] &&
                                           strcmp(type_name(cb_fs->param_types[1]), elem_tn) != 0) {
                                    char *msg = typechecker_format(checker,
                                        "reduce callback's element parameter takes '%s' but array element type is '%s'",
                                        type_name(cb_fs->param_types[1]), elem_tn);
                                    diagnostic_error_code_formatted(checker->diag, "E9004",
                                        NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                        mfn, msg);
                                } else if (cb_fs->return_count >= 1 && cb_fs->return_types[0] &&
                                           node->data.call.arg_count > 1) {
                                    AstNode *init_arg = node->data.call.args[1];
                                    GrayType *init_t = typetable_get(checker->type_table, init_arg);
                                    if (!init_t) init_t = resolve_expression(checker, init_arg);
                                    if (init_t) {
                                        const char *init_tn = type_name(init_t);
                                        const char *ret_tn = type_name(cb_fs->return_types[0]);
                                        if (init_tn && ret_tn && strcmp(ret_tn, init_tn) != 0) {
                                            char *msg = typechecker_format(checker,
                                                "reduce callback must return the same type as the accumulator (%s)",
                                                init_tn);
                                            diagnostic_error_code_formatted(checker->diag, "E9004",
                                                NODE_FILE(checker, cb_arg), cb_arg->token.line, cb_arg->token.column, 0,
                                                mfn, msg);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        /* All arrays.* functions expect an array as the first argument */
        if (node->data.call.arg_count > 0) {
            AstNode *arg0 = node->data.call.args[0];
            GrayType *arg0_t = resolve_expression(checker, arg0);
            if (arg0_t && arg0_t->kind != TK_ARRAY && arg0_t->kind != TK_UNKNOWN) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'arrays.%s()' expects an array as the first argument, got '%s'",
                    mfn, type_name(arg0_t));
                tc_err_arg_type(checker, arg0, msg);
            }
        }
    } else if (strcmp(mod, "strings") == 0) {
        /* E5007: mutating a string builder reached through an immutable binding.
         * The builder's buffer grows into the arena that owns the binding, so a
         * non-'mut' parameter would have its growth freed when the callee returns
         * (its scope watermark unwinds). Same rule as arrays.append — a wrapper
         * that appends must take '&b Builder'. */
        if ((strcmp(mfn, "builder_reserve") == 0 || strcmp(mfn, "builder_append") == 0 ||
             strcmp(mfn, "builder_append_char") == 0 || strcmp(mfn, "builder_append_bytes") == 0 ||
             strcmp(mfn, "builder_append_int") == 0 || strcmp(mfn, "builder_append_line") == 0 ||
             strcmp(mfn, "builder_clear") == 0) &&
            node->data.call.arg_count > 0) {
            AstNode *arg0 = node->data.call.args[0];
            if (arg0->kind == NODE_LABEL) {
                Symbol *sym = scope_lookup(checker->current_scope, arg0->data.label.value);
                if (sym && !sym->mutable) {
                    diagnostic_error_code_formatted(checker->diag, "E5007",
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                        "string builder", arg0->data.label.value);
                }
            }
        }
        /* E7004: strings.repeat() second arg must be integer */
        if (strcmp(mfn, "repeat") == 0 && node->data.call.arg_count >= 2) {
            GrayType *count_t = typetable_get(checker->type_table, node->data.call.args[1]);
            if (count_t && count_t->kind == TK_FLOAT) {
                diagnostic_error_message(checker->diag, "E7004",
                    "'strings.repeat()' count must be an integer, not a float",
                    NODE_FILE(checker, node->data.call.args[1]), node->data.call.args[1]->token.line,
                    node->data.call.args[1]->token.column, 0);
            }
        }
        /* E7004: strings.slice() bounds must be integers */
        if (strcmp(mfn, "slice") == 0 && node->data.call.arg_count >= 3) {
            for (int slice_index = 1; slice_index <= 2 && slice_index < node->data.call.arg_count; slice_index++) {
                GrayType *bt = typetable_get(checker->type_table, node->data.call.args[slice_index]);
                if (bt && bt->kind == TK_FLOAT) {
                    diagnostic_error_message(checker->diag, "E7004",
                        "'strings.slice()' bounds must be integers, not floats",
                        NODE_FILE(checker, node->data.call.args[slice_index]), node->data.call.args[slice_index]->token.line,
                        node->data.call.args[slice_index]->token.column, 0);
                }
            }
        }
    } else if (strcmp(mod, "threads") == 0) {
        if ((strcmp(mfn, "spawn") == 0 || strcmp(mfn, "spawn_arg") == 0) &&
            node->data.call.arg_count >= 1) {
            AstNode *arg0 = node->data.call.args[0];
            if (arg0->kind != NODE_FUNC_REF &&
                !(arg0->kind == NODE_CALL_EXPR &&
                  arg0->data.call.function->kind == NODE_LABEL &&
                  strcmp(arg0->data.call.function->data.label.value, "ref") == 0)) {
                diagnostic_error_message(checker->diag, "E7006",
                    "'threads.spawn()' requires a function reference; use '()func_name' or 'ref(func_name)'",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    } else if (strcmp(mod, "net") == 0) {
        /* E5026: functions that take a socket/listener as first arg */
        if (strcmp(mfn, "send") == 0 || strcmp(mfn, "receive") == 0 ||
            strcmp(mfn, "close") == 0 || strcmp(mfn, "set_timeout") == 0 ||
            strcmp(mfn, "accept") == 0) {
            if (node->data.call.arg_count >= 1) {
                GrayType *arg1_type = resolve_expression(checker, node->data.call.args[0]);
                if (arg1_type && arg1_type->kind != TK_STRUCT) {
                    const char *expected = strcmp(mfn, "accept") == 0
                        ? "Listener" : "Socket";
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "'net.%s()' expects a '%s' as the first argument, got '%s'",
                        mfn, expected,
                        arg1_type->name ? arg1_type->name : "non-struct type");
                    diagnostic_error_message(checker->diag, "E5026", msg,
                        NODE_FILE(checker, node), node->data.call.args[0]->token.line,
                        node->data.call.args[0]->token.column, 0);
                }
            }
        }
    } else if (strcmp(mod, "csv") == 0) {
        /* E5026: csv.write_file second arg must be an array */
        if (strcmp(mfn, "write_file") == 0 && node->data.call.arg_count >= 2) {
            GrayType *arg2_type = resolve_expression(checker, node->data.call.args[1]);
            if (arg2_type && arg2_type->kind == TK_STRING) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'csv.%s()' expects an array as the second argument, got string",
                    mfn);
                diagnostic_error_message(checker->diag, "E5026", msg,
                    NODE_FILE(checker, node), node->data.call.args[1]->token.line,
                    node->data.call.args[1]->token.column, 0);
            }
        }
    } else if (strcmp(mod, "fmt") == 0) {
        /* Validate printf/sprintf/format: literal format string + directive types */
        {
            bool is_fmt_fn = strcmp(mfn, "printf") == 0 ||
                             strcmp(mfn, "printfln") == 0 ||
                             strcmp(mfn, "eprintf") == 0 ||
                             strcmp(mfn, "eprintfln") == 0 ||
                             strcmp(mfn, "sprintf") == 0 ||
                             strcmp(mfn, "sprintfln") == 0;
            if (is_fmt_fn && node->data.call.arg_count >= 1) {
                AstNode *fmt_arg = node->data.call.args[0];
                if (fmt_arg->kind != NODE_STRING_VALUE) {
                    diagnostic_error_code_formatted(checker->diag, "E3086",
                        NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                        fmt_arg->token.column, 0, mfn);
                } else {
                    /* Walk format string, validate each directive against arg type */
                    const char *fstr = fmt_arg->data.string_value.value;
                    const char *p = fstr;
                    int di = 1;
                    int num_directives = 0;
                    while (*p) {
                        if (*p != '%') { p++; continue; }
                        p++;
                        if (!*p) {
                            /* Dangling % at end of format string */
                            diagnostic_error_code_formatted(checker->diag, "E3106",
                                NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                                fmt_arg->token.column, 0, mfn);
                            break;
                        }
                        if (*p == '%') { p++; continue; }
                        if (*p == 'n') {
                            diagnostic_error_code_formatted(checker->diag, "E3087",
                                NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                                fmt_arg->token.column, 0);
                            break;
                        }
                        /* Skip flags, width, precision, length modifier */
                        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') p++;
                        while (*p >= '0' && *p <= '9') p++;
                        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
                        if (*p == 'h') { p++; if (*p == 'h') p++; }
                        else if (*p == 'l') { p++; if (*p == 'l') p++; }
                        else if (*p == 'L') p++;
                        char spec = *p ? *p++ : 0;
                        if (!spec) {
                            diagnostic_error_code_formatted(checker->diag, "E3106",
                                NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                                fmt_arg->token.column, 0, mfn);
                            break;
                        }
                        num_directives++;
                        /* Reject unknown format directives */
                        bool known = false;
                        switch (spec) {
                        case 'd': case 'i': case 'u':
                        case 'x': case 'X': case 'o':
                        case 'f': case 'g': case 'e': case 'G': case 'E':
                        case 's': case 'c': case 'b':
                            known = true;
                            break;
                        default:
                            known = false;
                            break;
                        }
                        if (!known) {
                            diagnostic_error_code_formatted(checker->diag, "E3105",
                                NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                                fmt_arg->token.column, 0, mfn, spec);
                            di++;
                            continue;
                        }
                        if (di >= node->data.call.arg_count) { di++; continue; }
                        AstNode *darg = node->data.call.args[di];
                        GrayType *dt = resolve_expression(checker, darg);
                        di++;
                        if (!dt) continue;
                        const char *expected = NULL;
                        bool ok = false;
                        switch (spec) {
                        case 'd': case 'i':
                            expected = "int or char";
                            ok = dt->kind == TK_INT || dt->kind == TK_CHAR || dt->kind == TK_BYTE ||
                                 (dt->name && is_bigint_type(dt->name));
                            break;
                        case 'u':
                            expected = "uint";
                            ok = dt->kind == TK_UINT || dt->kind == TK_BYTE ||
                                 (dt->name && is_bigint_type(dt->name));
                            break;
                        case 'x': case 'X': case 'o':
                            expected = "int or uint";
                            ok = dt->kind == TK_INT || dt->kind == TK_UINT || dt->kind == TK_BYTE;
                            break;
                        case 'f': case 'g': case 'e': case 'G': case 'E':
                            expected = "float";
                            ok = dt->kind == TK_FLOAT;
                            break;
                        case 's':
                            expected = "string";
                            ok = dt->kind == TK_STRING;
                            break;
                        case 'c':
                            expected = "char";
                            ok = dt->kind == TK_CHAR ||
                                 (dt->kind == TK_INT && !(dt->name && is_bigint_type(dt->name)));
                            break;
                        case 'b':
                            expected = "bool";
                            ok = dt->kind == TK_BOOL;
                            break;
                        default:
                            ok = true;
                            break;
                        }
                        if (!ok && expected) {
                            char spec_str[2] = { spec, '\0' };
                            diagnostic_error_code_formatted(checker->diag, "E3088",
                                NODE_FILE(checker, darg), darg->token.line,
                                darg->token.column, 0,
                                mfn, spec_str, expected, di - 1,
                                type_name(dt));
                        }
                    }
                    /* Check argument count vs directive count */
                    int num_args = node->data.call.arg_count - 1;
                    if (num_args < num_directives) {
                        diagnostic_error_code_formatted(checker->diag, "E3107",
                            NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                            fmt_arg->token.column, 0,
                            mfn, num_directives, num_args);
                    } else if (num_args > num_directives) {
                        diagnostic_error_code_formatted(checker->diag, "E3108",
                            NODE_FILE(checker, fmt_arg), fmt_arg->token.line,
                            fmt_arg->token.column, 0,
                            mfn, num_directives, num_args);
                    }
                }
            }
        }
        /* Validate that non-format args are primitive types */
        for (int argument_index = 1; argument_index < node->data.call.arg_count; argument_index++) {
            GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
            if (arg_t && (arg_t->kind == TK_STRUCT || arg_t->kind == TK_ARRAY ||
                          arg_t->kind == TK_MAP || arg_t->kind == TK_POINTER)) {
                /* Build a readable type name */
                char tn[TYPE_NAME_MAX];
                if (arg_t->kind == TK_ARRAY && arg_t->element_type)
                    snprintf(tn, sizeof(tn), "[%s]", arg_t->element_type);
                else if (arg_t->kind == TK_MAP)
                    snprintf(tn, sizeof(tn), "map[%s:%s]",
                        arg_t->key_type ? arg_t->key_type : "?",
                        arg_t->value_type ? arg_t->value_type : "?");
                else if (arg_t->kind == TK_POINTER && arg_t->element_type)
                    snprintf(tn, sizeof(tn), "^%s", arg_t->element_type);
                else {
                    strncpy(tn, type_name(arg_t), sizeof(tn) - 1);
                    tn[sizeof(tn) - 1] = '\0';
                }
                diagnostic_error_code_formatted(checker->diag, "E3017", NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                    node->data.call.args[argument_index]->token.column, 0, mfn, tn);
            }
        }
    }

    if (!meta) {
        emit_unknown_stdlib_function(checker, mod, mfn, node);
        return &TYPE_UNKNOWN;
    }

    return result;
}

/* Does `name` name a type that exists? A <?> argument has to name one: value
 * resolution is skipped for type arguments, so this is the only thing standing
 * between a mistyped name and codegen. type_from_name() reads any capitalized
 * name as a struct, so it is never asked about the written name on its own —
 * only about a builtin spelling, or about what an alias reached. */
static bool type_arg_names_a_type(TypeChecker *checker, const char *name) {
    if (!name) return false;
    if (is_struct_name(checker, name) || is_enum_name(checker, name)) return true;
    if (typechecker_is_builtin(name) && type_from_name(name) != &TYPE_UNKNOWN)
        return true;
    const char *resolved = resolve_type_alias(checker,
        checker_resolve_type_name(checker, name));
    if (!resolved || strcmp(resolved, name) == 0) return false;
    return is_struct_name(checker, resolved) || is_enum_name(checker, resolved) ||
           type_from_name(resolved) != &TYPE_UNKNOWN;
}

/* Generic-call handling shared by every call spelling: bind the wildcard or
 * type parameter from the arguments, validate a type argument (E3127/E3128),
 * record the instantiation so codegen emits the specialization, and produce
 * the substituted return type.
 *
 * The module-qualified path had none of this, so `mod.generic(int)` accepted
 * an invalid type argument in silence and `mod.generic(T)` mangled a call to
 * a specialization that was never emitted. Sharing one implementation is what
 * keeps the qualified and bare spellings reporting identically.
 *
 * Returns the concrete return type, or NULL when the call is not generic or
 * nothing could be bound. *is_generic_out reports whether the callee is
 * generic at all, which callers use to suppress the scalar arg/param check. */
static GrayType *resolve_generic_call(TypeChecker *checker, AstNode *node,
    FuncSig *sig, const char *function_name, bool *is_generic_out) {
    char *generic_binding = NULL;
    GrayType *generic_return_t = NULL;
    bool is_generic_call = sig->is_generic && sig->decl &&
        sig->decl->kind == NODE_FUNC_DECL;
    if (is_generic_call) {
        int clamped_argument_count = node->data.call.arg_count < sig->decl->data.func_decl.param_count
            ? node->data.call.arg_count : sig->decl->data.func_decl.param_count;
        for (int argument_index = 0; argument_index < clamped_argument_count; argument_index++) {
            const char *ptn = sig->decl->data.func_decl.params[argument_index].type_name;
            /* Type parameter (<?>) — binding comes from the label
             * (a struct name), not from resolve_expression. */
            if (sig->decl->data.func_decl.params[argument_index].is_type_param) {
                AstNode *targ = node->data.call.args[argument_index];
                /* A module-qualified type name (mod.Type) parses as a member
                 * expression, not a label. Collapse it to a label carrying the
                 * dotted spelling so the type-name resolution below handles it
                 * exactly as the bare form. */
                if (targ->kind == NODE_MEMBER_EXPR && ast_member_qualifier(targ) &&
                    targ->data.member.member) {
                    char qualified[MSG_BUF_SIZE];
                    snprintf(qualified, sizeof(qualified), "%s.%s",
                        ast_member_qualifier(targ), targ->data.member.member);
                    if (type_arg_names_a_type(checker, qualified)) {
                        targ->kind = NODE_LABEL;
                        targ->data.label.value = arena_copy_string(checker->arena, qualified);
                    }
                }
                if (targ->kind != NODE_LABEL) {
                    diagnostic_error_code(checker->diag, "E3128",
                        NODE_FILE(checker, node->data.call.args[argument_index]),
                        node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0);
                    continue;
                }
                const char *arg_label = node->data.call.args[argument_index]->data.label.value;
                /* Type parameter forwarding: rewrite T → "?" so
                 * codegen can substitute, same as new(T). */
                if (checker->type_param_name &&
                    strcmp(arg_label, checker->type_param_name) == 0) {
                    node->data.call.args[argument_index]->data.label.value = "?";
                    arg_label = "?";
                }
                if (strcmp(arg_label, "?") == 0) {
                    if (checker->type_param_binding) {
                        arg_label = checker->type_param_binding;
                    } else {
                        /* First pass — no binding yet, accept and
                         * propagate the wildcard. */
                        if (!generic_binding) generic_binding = (char *)"?";
                        continue;
                    }
                }
                /* Must not be a variable in scope */
                if (scope_lookup(checker->current_scope, arg_label)) {
                    diagnostic_error_code(checker->diag, "E3128",
                        NODE_FILE(checker, node->data.call.args[argument_index]),
                        node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0);
                    continue;
                }
                /* Must name a type. Which types the function actually accepts
                 * is decided by its body: a `T{...}` literal narrows it to
                 * structs, and reports E3127 where the literal is written. */
                if (!type_arg_names_a_type(checker, arg_label)) {
                    diagnostic_error_code_formatted(checker->diag, "E4016",
                        NODE_FILE(checker, node->data.call.args[argument_index]),
                        node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0,
                        arg_label);
                    continue;
                }
                /* Bind what the name reaches, not the alias spelling. The
                 * instantiation is mangled and substituted by binding, so an
                 * alias bound as itself gave a return type no annotation of
                 * the underlying type would accept. The argument label is
                 * rewritten with it, or codegen would mangle the call site
                 * against a specialization that is never emitted. */
                {
                    const char *target = resolve_type_alias(checker,
                        checker_resolve_type_name(checker, arg_label));
                    if (target && strcmp(target, arg_label) != 0) {
                        arg_label = target;
                        node->data.call.args[argument_index]->data.label.value = target;
                    }
                }
                if (!generic_binding) {
                    generic_binding = (char *)arg_label;
                }
                continue;
            }
            GrayType *at = resolve_expression(checker, node->data.call.args[argument_index]);
            if (!type_name_has_wildcard(ptn)) continue;
            char *bound = bind_wildcard(ptn, at);
            if (!bound) {
                /* arg is TK_UNKNOWN: we are inside a generic
                 * function body during the main pass and the
                 * outer param hasn't been bound yet. The
                 * re-check pass will validate with concrete
                 * types — skip the false-positive here. */
                if (at->kind == TK_UNKNOWN) continue;
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot infer wildcard type '%s' from argument %d of '%s' (got %s)",
                    ptn, argument_index + 1, function_name, type_name(at));
                diagnostic_error_message(checker->diag, "E3159", msg,
                    NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                    node->data.call.args[argument_index]->token.column, 0);
                continue;
            }
            if (!generic_binding) {
                generic_binding = bound;
            } else if (strcmp(generic_binding, bound) != 0) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "wildcard type conflict in '%s': '?' was bound to %s, but argument %d is %s",
                    function_name, generic_binding, argument_index + 1, bound);
                diagnostic_error_message(checker->diag, "E3159", msg,
                    NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                    node->data.call.args[argument_index]->token.column, 0);
                free(bound);
            } else {
                free(bound);
            }
        }
        if (generic_binding) {
            record_instantiation(sig, generic_binding, node);
            if (sig->decl->data.func_decl.return_type_count > 0) {
                char *rt_str = substitute_wildcard(
                    sig->decl->data.func_decl.return_types[0],
                    generic_binding);
                generic_return_t = type_from_name(rt_str);
                /* rt_str is owned by type_from_name on the
                 * heap path; leak is fine at compile time. */
            } else {
                generic_return_t = &TYPE_VOID;
            }
        }
    }
    if (is_generic_out) *is_generic_out = is_generic_call;
    return generic_return_t;
}

/* Does a struct function's first parameter name the struct it is namespaced
 * under — the test for instance dispatch? The parameter type is written as it
 * appears inside the declaring module, while `struct_name` is the registry
 * key: `Msg` against `msg_Msg` for an imported struct. So the written name is
 * resolved in the declaring file's scope before the two are compared. */
static bool self_param_names_struct(TypeChecker *checker, AstNode *decl,
                                    const char *p0_tn, const char *struct_name) {
    if (!p0_tn || !struct_name) return false;
    if (strcmp(p0_tn, struct_name) == 0) return true;
    if (!checker->modules || !decl) return false;
    ResolveScope scope;
    scope.module = module_table_module_for_file(checker->modules, decl->token.file);
    scope.file = decl->token.file ? decl->token.file : checker->file;
    scope.using_modules = NULL;
    scope.using_count = 0;
    DeclEntry *entry = module_resolve_written(checker->modules, &scope, p0_tn);
    if (!entry) return false;
    char key[MSG_BUF_SIZE];
    const char *mangled = module_mangle_into(entry, key, sizeof(key));
    return mangled && strcmp(mangled, struct_name) == 0;
}

static GrayType *resolve_struct_or_module_call(TypeChecker *checker, AstNode *node, const char *mod, const char *mfn, const char *mod_raw, AstNode *fn) {
    GrayType *result = &TYPE_UNKNOWN;
    if (is_struct_name(checker, mod)) {
        /* Struct-namespaced function call: Type.func(); look up return type */
        const char *display_mod = struct_display_name(checker, mod);
        char prefixed[MSG_BUF_SIZE];
        {
            char sk[MSG_BUF_SIZE];
            snprintf(prefixed, sizeof(prefixed), "%s_%s",
                checker_resolve_decl_into(checker, mod, sk, sizeof(sk)), mfn);
        }
        FuncSig *sig = find_func(checker, prefixed);
        if (sig) {
            sig->used = true;
            warn_if_func_deprecated(checker, node, sig);
            /* Resolve named arguments for struct static calls */
            if (sig->decl) {
                char display[MSG_BUF_SIZE];
                snprintf(display, sizeof(display), "%s.%s", display_mod, mfn);
                typechecker_resolve_named_arguments(checker, node, sig->decl, display);
            }
            /* E4017: private struct function called from outside the struct */
            if (sig->is_private &&
                !(checker->current_struct_name && strcmp(checker->current_struct_name, mod) == 0)) {
                diagnostic_error_code_formatted(checker->diag, "E4017", NODE_FILE(checker, node),
                    node->token.line, node->token.column, 0, display_mod, mfn);
            }
            /* Wildcard (generic) struct function: record instantiation
             * and substitute the concrete return type. */
            if (sig->is_generic && sig->decl &&
                sig->decl->kind == NODE_FUNC_DECL) {
                char *binding = NULL;
                int clamped_argument_count = node->data.call.arg_count < sig->decl->data.func_decl.param_count
                    ? node->data.call.arg_count : sig->decl->data.func_decl.param_count;
                for (int argument_index = 0; argument_index < clamped_argument_count && !binding; argument_index++) {
                    const char *ptn = sig->decl->data.func_decl.params[argument_index].type_name;
                    GrayType *at = resolve_expression(checker, node->data.call.args[argument_index]);
                    if (!type_name_has_wildcard(ptn)) continue;
                    binding = bind_wildcard(ptn, at);
                }
                if (binding) {
                    record_instantiation(sig, binding, node);
                    if (sig->decl->data.func_decl.return_type_count > 0) {
                        char *rt = substitute_wildcard(
                            sig->decl->data.func_decl.return_types[0], binding);
                        result = type_from_name(rt);
                    } else {
                        result = &TYPE_VOID;
                    }
                    free(binding);
                } else {
                    result = sig->return_count > 0 ? sig->return_types[0] : &TYPE_VOID;
                }
            } else {
                result = sig->return_count > 0 ? sig->return_types[0] : &TYPE_VOID;
            }
            /* E5008: check argument count, accounting for default params */
            {
                int min_params = sig->param_count;
                if (sig->decl && sig->decl->kind == NODE_FUNC_DECL) {
                    min_params = 0;
                    for (int parameter_index = 0; parameter_index < sig->decl->data.func_decl.param_count; parameter_index++) {
                        if (!sig->decl->data.func_decl.params[parameter_index].default_value)
                            min_params++;
                    }
                }
                if (node->data.call.arg_count < min_params ||
                    node->data.call.arg_count > sig->param_count) {
                    char *msg = NULL;
                    if (min_params == sig->param_count) {
                        msg = typechecker_format(checker,
                            "function '%s.%s' expects %d argument(s), got %d",
                            display_mod, mfn, sig->param_count, node->data.call.arg_count);
                    } else {
                        msg = typechecker_format(checker,
                            "function '%s.%s' expects %d-%d argument(s), got %d",
                            display_mod, mfn, min_params, sig->param_count, node->data.call.arg_count);
                    }
                    tc_err_arity(checker, node, msg);
                }
            }
            /* Check argument types */
            int check_count = node->data.call.arg_count < sig->param_count
                ? node->data.call.arg_count : sig->param_count;
            for (int argument_index = 0; argument_index < check_count; argument_index++) {
                GrayType *param_t = sig->param_types[argument_index];
                /* Set expected_type for implicit enum resolution */
                GrayType *saved_expected_m = checker->expected_type;
                if (param_t && param_t->kind == TK_ENUM && param_t->name)
                    checker->expected_type = param_t;
                GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
                checker->expected_type = saved_expected_m;
                if (arg_t->kind != TK_UNKNOWN && param_t->kind != TK_UNKNOWN &&
                    !types_assignable(checker, param_t, arg_t) &&
                    !(param_t->kind == TK_ENUM && is_int_kind(arg_t->kind)) &&
                    !(param_t->kind == TK_STRUCT && is_int_kind(arg_t->kind)) &&
                    !(is_int_kind(param_t->kind) && arg_t->kind == TK_BOOL) &&
                    !(arg_t->kind == TK_NIL &&
                      (param_t->kind == TK_POINTER || param_t->kind == TK_ERROR))) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s.%s': expected %s, got %s",
                        argument_index + 1, display_mod, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                    tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                }
                /* Enum-to-enum: kinds both TK_ENUM but different names */
                if (arg_t->kind == TK_ENUM && param_t->kind == TK_ENUM &&
                    arg_t->name && param_t->name &&
                    !typechecker_same_enum_type(checker, arg_t->name, param_t->name)) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s.%s': expected enum '%s', got enum '%s'",
                        argument_index + 1, display_mod, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                    tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                }
                /* Struct-to-struct: kinds both TK_STRUCT but different names */
                if (arg_t->kind == TK_STRUCT && param_t->kind == TK_STRUCT &&
                    arg_t->name && param_t->name &&
                    !typechecker_same_struct_type(checker, arg_t->name, param_t->name)) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s.%s': expected struct '%s', got struct '%s'",
                        argument_index + 1, display_mod, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                    tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                }
                /* Pointer-to-pointer: pointee types differ */
                if (arg_t->kind == TK_POINTER && param_t->kind == TK_POINTER &&
                    arg_t->name && param_t->name &&
                    !typechecker_same_struct_type(checker, arg_t->name, param_t->name)) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s.%s': expected '%s', got '%s'",
                        argument_index + 1, display_mod, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                    tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                }
                /* E3027: non-assignable or const passed to mutable (&) param.
                 * Struct functions live inside NODE_STRUCT_DECL, not as
                 * top-level stmts, so scan struct declarations. */
                {
                    AstNode *arg = node->data.call.args[argument_index];
                    AstNode *found_declaration = NULL;
                    for (int field_index = 0; field_index < checker->program->data.program.stmt_count && !found_declaration; field_index++) {
                        AstNode *stmt = checker->program->data.program.stmts[field_index];
                        if (stmt->kind == NODE_STRUCT_DECL &&
                            strcmp(stmt->data.struct_decl.name, mod) == 0) {
                            for (int sfi = 0; sfi < stmt->data.struct_decl.func_count; sfi++) {
                                AstNode *sf = stmt->data.struct_decl.funcs[sfi].func_decl;
                                if (sf && sf->kind == NODE_FUNC_DECL &&
                                    strcmp(sf->data.func_decl.name, mfn) == 0 &&
                                    argument_index < sf->data.func_decl.param_count &&
                                    sf->data.func_decl.params[argument_index].mutable) {
                                    found_declaration = sf;
                                    break;
                                }
                            }
                        }
                    }
                    if (found_declaration) {
                        char fn_display[MSG_BUF_SIZE];
                        snprintf(fn_display, sizeof(fn_display), "%s.%s", display_mod, mfn);
                        char param_desc[MSG_BUF_SIZE];
                        snprintf(param_desc, sizeof(param_desc), "mutable parameter '%s'",
                            found_declaration->data.func_decl.params[argument_index].name);
                        check_mutable_arg(checker, arg, param_desc, fn_display);
                    }
                }
            }
        } else {
            /* E4018: struct has no function with this name */
            diagnostic_error_code_formatted(checker->diag, "E4018", NODE_FILE(checker, node),
                node->token.line, node->token.column, 0, display_mod, mfn);
            result = &TYPE_VOID;
        }
    } else {
        /* Try user-defined module */
        FuncSig *sig = find_module_func(checker, mod, mfn);
        if (sig && reject_if_private(checker, node, mod, mfn)) {
            /* diagnostic already reported */
        } else if (sig) {
            sig->used = true;
            warn_if_func_deprecated(checker, node, sig);
            /* Resolve named arguments for user-module function calls */
            char display[MSG_BUF_SIZE];
            snprintf(display, sizeof(display), "%s.%s", mod, mfn);
            if (sig->decl) {
                typechecker_resolve_named_arguments(checker, node, sig->decl, display);
            }
            /* Argument count, defaulted parameters accounted for. The bare
             * and struct-namespaced spellings both check this; a
             * module-qualified call did not, so too few arguments reached
             * codegen and came back as a C compiler error. */
            {
                int min_args = sig->param_count;
                if (sig->decl && sig->decl->kind == NODE_FUNC_DECL) {
                    min_args = 0;
                    for (int parameter_index = 0; parameter_index < sig->decl->data.func_decl.param_count; parameter_index++) {
                        if (!sig->decl->data.func_decl.params[parameter_index].default_value)
                            min_args++;
                    }
                }
                if (node->data.call.arg_count < min_args ||
                    node->data.call.arg_count > sig->param_count) {
                    char *msg = NULL;
                    if (min_args == sig->param_count) {
                        msg = typechecker_format(checker,
                            "function '%s' expects %d argument(s), got %d",
                            display, sig->param_count, node->data.call.arg_count);
                    } else {
                        msg = typechecker_format(checker,
                            "function '%s' expects %d-%d argument(s), got %d",
                            display, min_args, sig->param_count, node->data.call.arg_count);
                    }
                    tc_err_arity(checker, node, msg);
                }
            }
            /* A generic callee needs the same binding, validation and
             * instantiation recording the bare-call spelling gets; without
             * it an invalid type argument passed silently and codegen
             * mangled a call to a specialization it never emitted. */
            bool mod_is_generic = false;
            GrayType *mod_generic_ret = resolve_generic_call(checker, node, sig,
                display, &mod_is_generic);
            if (mod_is_generic && mod_generic_ret) {
                result = mod_generic_ret;
            } else if (sig->return_count > 0) {
                result = sig->return_types[0];
            } else {
                result = &TYPE_VOID;
            }
            /* Argument types. A module-qualified call took the callee's
             * return type and nothing else: what it was passed went
             * unchecked, so a mismatch — two modules' same-named but
             * distinct structs among them — reached codegen and came back
             * as a C compiler error. A generic callee is already unified
             * against its arguments above. */
            if (!mod_is_generic) {
                int check_count = node->data.call.arg_count < sig->param_count
                    ? node->data.call.arg_count : sig->param_count;
                for (int argument_index = 0; argument_index < check_count; argument_index++) {
                    AstNode *arg = node->data.call.args[argument_index];
                    GrayType *param_t = sig->param_types[argument_index];
                    /* Set expected_type for implicit enum resolution */
                    GrayType *saved_expected_u = checker->expected_type;
                    if (param_t && param_t->kind == TK_ENUM && param_t->name)
                        checker->expected_type = param_t;
                    GrayType *arg_t = resolve_expression(checker, arg);
                    checker->expected_type = saved_expected_u;
                    if (!arg_t || !param_t ||
                        arg_t->kind == TK_UNKNOWN || param_t->kind == TK_UNKNOWN ||
                        types_assignable(checker, param_t, arg_t) ||
                        (param_t->kind == TK_ENUM && is_int_kind(arg_t->kind)) ||
                        (param_t->kind == TK_STRUCT && is_int_kind(arg_t->kind)) ||
                        (is_int_kind(param_t->kind) && arg_t->kind == TK_BOOL) ||
                        (arg_t->kind == TK_NIL &&
                         (param_t->kind == TK_POINTER || param_t->kind == TK_ERROR)))
                        continue;
                    tc_err_arg_type(checker, arg, typechecker_format(checker, "argument %d of '%s': expected %s, got %s", argument_index + 1, display, type_display_name(checker, param_t), type_display_name(checker, arg_t)));
                }
            }
        } else {
            /* Check if 'mod' is a variable with a struct type —
             * user wrote instance.func() instead of Type.func(). */
            Symbol *sym = scope_lookup(checker->current_scope, mod_raw);
            if (sym && sym->type && (sym->type->kind == TK_STRUCT ||
                sym->type->kind == TK_POINTER)) {
                const char *struct_name = sym->type->kind == TK_POINTER
                    ? sym->type->element_type : sym->type->name;
                const char *display_sname = struct_display_name(checker, struct_name);
                /* : check if `mfn` is a data field of type func
                 * before trying struct-function dispatch. A func-typed
                 * field should be called as a function pointer, not
                 * mistaken for a struct function. The bare "func" type
                 * was deprecated; modern typed function refs are
                 * encoded as "func(...)->R" with kind TK_FUNCTION. */
                GrayType *field_t = struct_field_type(checker, struct_name, mfn);
                if (field_t && field_t->kind == TK_FUNCTION) {
                    /* This is a func-typed data field. Accept the call
                     *; codegen will emit it as a function-pointer
                     * call through the field access. Resolve the
                     * return type from the encoded signature so
                     * downstream uses get a typed value. */
                    sym->used = true;
                    result = (field_t->func_sig &&
                              field_t->func_sig->return_count > 0 &&
                              field_t->func_sig->return_types &&
                              field_t->func_sig->return_types[0])
                        ? type_from_name(field_t->func_sig->return_types[0])
                        : &TYPE_UNKNOWN;
                    return result;
                }
                char sfn[MSG_BUF_SIZE];
                {
                    char sk[MSG_BUF_SIZE];
                    snprintf(sfn, sizeof(sfn), "%s_%s",
                        checker_resolve_decl_into(checker, struct_name, sk, sizeof(sk)), mfn);
                }
                FuncSig *ssig = find_func(checker, sfn);
                /* Auto-dispatch instance.func() → Type.func(instance)
                 * whenever the struct function takes the struct (or a
                 * pointer to it) as its first parameter. This covers
                 * both `do bar(self Foo)` and `do bar(&self Foo)`, and
                 * lets users call struct functions on instances without
                 * having to write the type name at every call site.
                 * Factory-style functions (e.g. `do make(x int) -> Foo`)
                 * whose first param isn't a Foo continue to require
                 * explicit `Foo.make(...)` since there's no instance
                 * to bind. */
                bool is_self_func = false;
                if (ssig && ssig->decl && ssig->decl->kind == NODE_FUNC_DECL &&
                    ssig->decl->data.func_decl.param_count > 0) {
                    const char *p0_tn = ssig->decl->data.func_decl.params[0].type_name;
                    if (p0_tn) {
                        is_self_func =
                            self_param_names_struct(checker, ssig->decl, p0_tn, struct_name) ||
                            (p0_tn[0] == '^' &&
                             self_param_names_struct(checker, ssig->decl, p0_tn + 1, struct_name));
                    }
                }
                if (is_self_func) {
                    /* E4017: private struct function via instance dispatch */
                    if (ssig->is_private &&
                        !(checker->current_struct_name && strcmp(checker->current_struct_name, struct_name) == 0)) {
                        diagnostic_error_code_formatted(checker->diag, "E4017", NODE_FILE(checker, node),
                            node->token.line, node->token.column, 0, display_sname, mfn);
                    }
                    /* Resolve named arguments before AST rewrite.
                     * User-visible params start at index 1 (after self),
                     * so build a temporary shifted decl view. Named
                     * args target param names excluding the self param. */
                    if (ssig->decl && ssig->decl->kind == NODE_FUNC_DECL &&
                        typechecker_has_named_arguments(node)) {
                        AstNode *sdecl = ssig->decl;
                        /* Create a temporary fake decl with params shifted
                         * to skip the self parameter (param[0]). */
                        AstNode tmp_decl = *sdecl;
                        int orig_pc = sdecl->data.func_decl.param_count;
                        if (orig_pc > 1) {
                            tmp_decl.data.func_decl.params = &sdecl->data.func_decl.params[1];
                            tmp_decl.data.func_decl.param_count = orig_pc - 1;
                        } else {
                            tmp_decl.data.func_decl.param_count = 0;
                        }
                        char display[MSG_BUF_SIZE];
                        snprintf(display, sizeof(display), "%s.%s", display_sname, mfn);
                        typechecker_resolve_named_arguments(checker, node, &tmp_decl, display);
                    }
                    /* Rewrite the call AST: change the member-expr
                     * object from the instance label to the type
                     * name, and prepend the instance as arg[0].
                     * When the object is an explicit deref (p^.func),
                     * replace it with a plain label node first. */
                    retarget_member_object(fn, struct_name);
                    int orig_count = node->data.call.arg_count;
                    AstNode **new_args = xmalloc(sizeof(AstNode *) * (orig_count + 1));
                    AstNode *self_arg = xcalloc(1, sizeof(AstNode));
                    self_arg->kind = NODE_LABEL;
                    self_arg->token = node->token;
                    self_arg->data.label.value = strdup(mod_raw);
                    /* Auto-deref: receiver is a pointer but param expects
                     * the struct value — wrap self in a deref (p^).
                     * For &self (mutable), codegen cancels the deref
                     * with the address-of, emitting just the pointer. */
                    const char *p0_tn = ssig->decl->data.func_decl.params[0].type_name;
                    if (sym->type->kind == TK_POINTER &&
                        self_param_names_struct(checker, ssig->decl, p0_tn, struct_name)) {
                        AstNode *deref = xcalloc(1, sizeof(AstNode));
                        deref->kind = NODE_POSTFIX_EXPR;
                        deref->token = node->token;
                        deref->data.postfix.left = self_arg;
                        deref->data.postfix.op = TOK_CARET;
                        new_args[0] = deref;
                    } else {
                        new_args[0] = self_arg;
                    }
                    for (int argument_index = 0; argument_index < orig_count; argument_index++) {
                        new_args[argument_index + 1] = node->data.call.args[argument_index];
                    }
                    node->data.call.args = new_args;
                    node->data.call.arg_count = orig_count + 1;
                    /* Mark the function used and resolve return type */
                    ssig->used = true;
                    sym->used = true;
                    warn_if_func_deprecated(checker, node, ssig);
                    if (ssig->is_generic && ssig->decl &&
                        ssig->decl->kind == NODE_FUNC_DECL) {
                        char *binding = NULL;
                        int clamped_argument_count = node->data.call.arg_count < ssig->decl->data.func_decl.param_count
                            ? node->data.call.arg_count : ssig->decl->data.func_decl.param_count;
                        for (int argument_index = 0; argument_index < clamped_argument_count && !binding; argument_index++) {
                            const char *ptn = ssig->decl->data.func_decl.params[argument_index].type_name;
                            GrayType *at = resolve_expression(checker, node->data.call.args[argument_index]);
                            if (!type_name_has_wildcard(ptn)) continue;
                            binding = bind_wildcard(ptn, at);
                        }
                        if (binding) {
                            record_instantiation(ssig, binding, node);
                            if (ssig->decl->data.func_decl.return_type_count > 0) {
                                char *rt = substitute_wildcard(
                                    ssig->decl->data.func_decl.return_types[0], binding);
                                result = type_from_name(rt);
                            } else {
                                result = &TYPE_VOID;
                            }
                            free(binding);
                        } else {
                            result = ssig->return_count > 0 ? ssig->return_types[0] : &TYPE_VOID;
                        }
                    } else {
                        result = ssig->return_count > 0 ? ssig->return_types[0] : &TYPE_VOID;
                    }
                    /* E5008: validate argument count (after AST rewrite
                     * which prepended self as arg[0]). Both arg_count
                     * and param_count include self, so they compare
                     * directly. Display counts subtract 1 to hide self. */
                    {
                        int min_params = ssig->param_count;
                        if (ssig->decl && ssig->decl->kind == NODE_FUNC_DECL) {
                            min_params = 0;
                            for (int parameter_index = 0; parameter_index < ssig->decl->data.func_decl.param_count; parameter_index++) {
                                if (!ssig->decl->data.func_decl.params[parameter_index].default_value)
                                    min_params++;
                            }
                        }
                        if (node->data.call.arg_count < min_params ||
                            node->data.call.arg_count > ssig->param_count) {
                            int display_got = node->data.call.arg_count - 1;
                            int display_max = ssig->param_count - 1;
                            int display_min = min_params - 1;
                            char emsg[MSG_BUF_SIZE];
                            if (display_min == display_max) {
                                snprintf(emsg, sizeof(emsg),
                                    "function '%s.%s' expects %d argument(s), got %d",
                                    display_sname, mfn, display_max, display_got);
                            } else {
                                snprintf(emsg, sizeof(emsg),
                                    "function '%s.%s' expects %d-%d argument(s), got %d",
                                    display_sname, mfn, display_min, display_max, display_got);
                            }
                            tc_err_arity(checker, node, arena_copy_string(checker->arena, emsg));
                        }
                    }
                    /* Validate argument types (after AST rewrite) */
                    {
                        int check_count = node->data.call.arg_count < ssig->param_count
                            ? node->data.call.arg_count : ssig->param_count;
                        for (int argument_index = 0; argument_index < check_count; argument_index++) {
                            GrayType *param_t = ssig->param_types[argument_index];
                            /* Set expected_type for implicit enum resolution */
                            GrayType *saved_expected_s = checker->expected_type;
                            if (param_t && param_t->kind == TK_ENUM && param_t->name)
                                checker->expected_type = param_t;
                            GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
                            checker->expected_type = saved_expected_s;
                            if (arg_t && param_t &&
                                arg_t->kind != TK_UNKNOWN && param_t->kind != TK_UNKNOWN &&
                                !types_assignable(checker, param_t, arg_t) &&
                                !(param_t->kind == TK_ENUM && is_int_kind(arg_t->kind)) &&
                                !(param_t->kind == TK_STRUCT && is_int_kind(arg_t->kind)) &&
                                !(is_int_kind(param_t->kind) && arg_t->kind == TK_BOOL) &&
                                !(arg_t->kind == TK_NIL &&
                                  (param_t->kind == TK_POINTER || param_t->kind == TK_ERROR))) {
                                char amsg[MSG_BUF_SIZE];
                                snprintf(amsg, sizeof(amsg),
                                    "argument %d of '%s.%s': expected %s, got %s",
                                    argument_index + 1, display_sname, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                                tc_err_arg_type(checker, node->data.call.args[argument_index], arena_copy_string(checker->arena, amsg));
                            }
                            /* Struct-to-struct: kinds both TK_STRUCT but different names */
                            if (arg_t && param_t &&
                                arg_t->kind == TK_STRUCT && param_t->kind == TK_STRUCT &&
                                arg_t->name && param_t->name &&
                                !typechecker_same_struct_type(checker, arg_t->name, param_t->name)) {
                                char smsg[MSG_BUF_SIZE];
                                snprintf(smsg, sizeof(smsg),
                                    "argument %d of '%s.%s': expected struct '%s', got struct '%s'",
                                    argument_index + 1, display_sname, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                                tc_err_arg_type(checker, node->data.call.args[argument_index], arena_copy_string(checker->arena, smsg));
                            }
                            /* Pointer-to-pointer: pointee types differ */
                            if (arg_t && param_t &&
                                arg_t->kind == TK_POINTER && param_t->kind == TK_POINTER &&
                                arg_t->name && param_t->name &&
                                !typechecker_same_struct_type(checker, arg_t->name, param_t->name)) {
                                char pmsg[MSG_BUF_SIZE];
                                snprintf(pmsg, sizeof(pmsg),
                                    "argument %d of '%s.%s': expected '%s', got '%s'",
                                    argument_index + 1, display_sname, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                                tc_err_arg_type(checker, node->data.call.args[argument_index], arena_copy_string(checker->arena, pmsg));
                            }
                        }
                    }
                    /* E3027: non-assignable or const passed to mutable (&) param
                     * in instance dispatch. After the AST rewrite, arg[0]
                     * is self; user-visible args start at index 1. */
                    if (ssig->decl && ssig->decl->kind == NODE_FUNC_DECL) {
                        int parameter_count = ssig->decl->data.func_decl.param_count;
                        int clamped_argument_count = node->data.call.arg_count < parameter_count ? node->data.call.arg_count : parameter_count;
                        for (int argument_index = 0; argument_index < clamped_argument_count; argument_index++) {
                            if (!ssig->decl->data.func_decl.params[argument_index].mutable)
                                continue;
                            AstNode *arg = node->data.call.args[argument_index];
                            if (argument_index == 0) {
                                /* Self arg: synthetic NODE_LABEL with value mod_raw */
                                Symbol *self_sym = scope_lookup(checker->current_scope, mod_raw);
                                if (self_sym && !self_sym->mutable) {
                                    char emsg[MSG_BUF_SIZE];
                                    snprintf(emsg, sizeof(emsg),
                                        "cannot call '%s.%s' on constant '%s'; function requires a mutable ('&') self parameter",
                                        display_sname, mfn, self_sym->name);
                                    diagnostic_error_message(checker->diag, "E3027", arena_copy_string(checker->arena, emsg),
                                        NODE_FILE(checker, node), node->token.line,
                                        node->token.column, 0);
                                }
                            } else {
                                /* User-visible args */
                                char fn_display[MSG_BUF_SIZE];
                                snprintf(fn_display, sizeof(fn_display), "%s.%s", display_sname, mfn);
                                char param_desc[MSG_BUF_SIZE];
                                snprintf(param_desc, sizeof(param_desc), "mutable parameter '%s'",
                                    ssig->decl->data.func_decl.params[argument_index].name);
                                check_mutable_arg(checker, arg, param_desc, fn_display);
                            }
                        }
                    }
                    /* E3163 / @mem effects: resolve_call_expr's own copy of
                     * this check runs before dispatch, so resolve_call_sig()
                     * could not yet see this call — the object was still the
                     * instance label, not the struct name, and self had not
                     * been prepended into args[0]. Now that the rewrite above
                     * has happened, apply it directly against ssig. */
                    apply_call_param_escape_and_mem_effects(checker, node, ssig, 0);
                } else if (ssig) {
                    /* Non-self struct function called on an instance.
                     * Rewrite the AST so the member-expr object uses
                     * the struct type name instead of the instance
                     * name.  Do NOT prepend self as arg[0] — the
                     * function doesn't take a self parameter. This
                     * makes the call identical to Type.func() by the
                     * time codegen sees it. */
                    /* E4017: private struct function via instance dispatch */
                    if (ssig->is_private &&
                        !(checker->current_struct_name && strcmp(checker->current_struct_name, struct_name) == 0)) {
                        diagnostic_error_code_formatted(checker->diag, "E4017", NODE_FILE(checker, node),
                            node->token.line, node->token.column, 0, display_sname, mfn);
                    }
                    /* Resolve named arguments if present */
                    if (ssig->decl && ssig->decl->kind == NODE_FUNC_DECL &&
                        typechecker_has_named_arguments(node)) {
                        char display[MSG_BUF_SIZE];
                        snprintf(display, sizeof(display), "%s.%s", display_sname, mfn);
                        typechecker_resolve_named_arguments(checker, node, ssig->decl, display);
                    }
                    retarget_member_object(fn, struct_name);
                    ssig->used = true;
                    sym->used = true;
                    warn_if_func_deprecated(checker, node, ssig);
                    result = ssig->return_count > 0 ? ssig->return_types[0] : &TYPE_VOID;
                    /* Validate argument count */
                    {
                        int min_params = ssig->param_count;
                        if (ssig->decl && ssig->decl->kind == NODE_FUNC_DECL) {
                            min_params = 0;
                            for (int parameter_index = 0; parameter_index < ssig->decl->data.func_decl.param_count; parameter_index++) {
                                if (!ssig->decl->data.func_decl.params[parameter_index].default_value)
                                    min_params++;
                            }
                        }
                        if (node->data.call.arg_count < min_params ||
                            node->data.call.arg_count > ssig->param_count) {
                            char emsg[MSG_BUF_SIZE];
                            if (min_params == ssig->param_count) {
                                snprintf(emsg, sizeof(emsg),
                                    "function '%s.%s' expects %d argument(s), got %d",
                                    display_sname, mfn, ssig->param_count, node->data.call.arg_count);
                            } else {
                                snprintf(emsg, sizeof(emsg),
                                    "function '%s.%s' expects %d-%d argument(s), got %d",
                                    display_sname, mfn, min_params, ssig->param_count, node->data.call.arg_count);
                            }
                            tc_err_arity(checker, node, arena_copy_string(checker->arena, emsg));
                        }
                    }
                    /* Validate argument types */
                    {
                        int check_count = node->data.call.arg_count < ssig->param_count
                            ? node->data.call.arg_count : ssig->param_count;
                        for (int argument_index = 0; argument_index < check_count; argument_index++) {
                            GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
                            GrayType *param_t = ssig->param_types[argument_index];
                            if (arg_t && param_t &&
                                arg_t->kind != TK_UNKNOWN && param_t->kind != TK_UNKNOWN &&
                                !types_assignable(checker, param_t, arg_t) &&
                                !(param_t->kind == TK_ENUM && is_int_kind(arg_t->kind)) &&
                                !(param_t->kind == TK_STRUCT && is_int_kind(arg_t->kind)) &&
                                !(is_int_kind(param_t->kind) && arg_t->kind == TK_BOOL) &&
                                !(arg_t->kind == TK_NIL &&
                                  (param_t->kind == TK_POINTER || param_t->kind == TK_ERROR))) {
                                char amsg[MSG_BUF_SIZE];
                                snprintf(amsg, sizeof(amsg),
                                    "argument %d of '%s.%s': expected %s, got %s",
                                    argument_index + 1, display_sname, mfn, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                                tc_err_arg_type(checker, node->data.call.args[argument_index], arena_copy_string(checker->arena, amsg));
                            }
                        }
                    }
                    /* E3027: non-assignable or const passed to mutable (&) param
                     * in non-self instance dispatch. Args and params are 1:1
                     * (no self prepended). */
                    if (ssig->decl && ssig->decl->kind == NODE_FUNC_DECL) {
                        int parameter_count = ssig->decl->data.func_decl.param_count;
                        int clamped = node->data.call.arg_count < parameter_count
                            ? node->data.call.arg_count : parameter_count;
                        for (int argument_index = 0; argument_index < clamped; argument_index++) {
                            if (!ssig->decl->data.func_decl.params[argument_index].mutable)
                                continue;
                            char fn_display[MSG_BUF_SIZE];
                            snprintf(fn_display, sizeof(fn_display), "%s.%s", display_sname, mfn);
                            char param_desc[MSG_BUF_SIZE];
                            snprintf(param_desc, sizeof(param_desc), "mutable parameter '%s'",
                                ssig->decl->data.func_decl.params[argument_index].name);
                            check_mutable_arg(checker, node->data.call.args[argument_index],
                                param_desc, fn_display);
                        }
                    }
                    /* E3163 / @mem effects — see the is_self_func branch
                     * above for why this must run here rather than in
                     * resolve_call_expr's own copy of the check. */
                    apply_call_param_escape_and_mem_effects(checker, node, ssig, 0);
                } else {
                    diagnostic_error_code_formatted(checker->diag, "E4018",
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                        display_sname, mfn);
                    result = &TYPE_UNKNOWN;
                }
            } else if (sym && sym->type &&
                       sym->type->kind != TK_UNKNOWN && sym->type->kind != TK_VOID) {
                char *msg = typechecker_format(checker,
                    "type '%s' does not support function calls via dot notation",
                    type_name(sym->type));
                diagnostic_error_message(checker->diag, "E3013", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                result = &TYPE_UNKNOWN;
            } else if (sym) {
                /* A variable whose type never resolved; whatever went wrong
                 * with it was already reported. Stay quiet so this call does
                 * not add a second, less useful error on top. */
                result = &TYPE_UNKNOWN;
            } else {
                /* Nothing in the chain matched. Resolving to void here made a
                 * typo indistinguishable from a genuine void call: the result
                 * being used drew a false E3038 blaming the callee, and the
                 * result being discarded reached the C compiler as a call to
                 * an undeclared function. Name the real problem instead. */
                if (typechecker_is_imported_module(checker, mod)) {
                    const char *near = suggest_similar_module_func(checker, mod, mfn);
                    if (near) {
                        char help[MSG_BUF_SIZE];
                        snprintf(help, sizeof(help), "did you mean '%s.%s'?", mod_raw, near);
                        diagnostic_error_code_formatted_help(checker->diag, "E4023",
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                            arena_copy_string(checker->arena, help), mod_raw, mfn);
                    } else {
                        diagnostic_error_code_formatted(checker->diag, "E4023",
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                            mod_raw, mfn);
                    }
                } else {
                    /* The module name is the mistake, so point at it rather
                     * than at the call's parenthesis. */
                    AstNode *at = (fn && fn->kind == NODE_MEMBER_EXPR &&
                                   fn->data.member.object) ? fn->data.member.object : node;
                    const char *near = suggest_similar_module(checker, mod_raw);
                    if (near) {
                        char help[MSG_BUF_SIZE];
                        snprintf(help, sizeof(help), "did you mean '%s'?", near);
                        diagnostic_error_code_formatted_help(checker->diag, "E6010",
                            NODE_FILE(checker, at), at->token.line, at->token.column, 0,
                            arena_copy_string(checker->arena, help), mod_raw);
                    } else {
                        diagnostic_error_code_formatted(checker->diag, "E6010",
                            NODE_FILE(checker, at), at->token.line, at->token.column, 0,
                            mod_raw);
                    }
                }
                result = &TYPE_UNKNOWN;
            }
        }
    }
    return result;
}

/* The written type name a size_of() argument spells, or NULL when the
 * argument is a value expression. A bare name arrives as a label; a
 * module-qualified one arrives as a member expression, which no later phase
 * ever read as a type — so `size_of(mod.T)` reached codegen with nothing to
 * emit and the generated C said sizeof(unknown). */
static const char *size_of_type_spelling(TypeChecker *checker, AstNode *arg) {
    if (!arg) return NULL;
    if (arg->kind == NODE_LABEL) {
        /* A name bound to a variable is a value, not a type. */
        if (scope_lookup(checker->current_scope, arg->data.label.value)) return NULL;
        return arg->data.label.value;
    }
    if (arg->kind == NODE_MEMBER_EXPR && arg->data.member.object &&
        arg->data.member.object->kind == NODE_LABEL &&
        typechecker_is_imported_module(checker, arg->data.member.object->data.label.value)) {
        char buf[MSG_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%s.%s",
                 arg->data.member.object->data.label.value, arg->data.member.member);
        return arena_copy_string(checker->arena, buf);
    }
    return NULL;
}

/* True when an array-type spelling carries a ",N" size at bracket depth 1
 * ([int,4], [int,N]) — a fixed-size array, whose storage never moves. A
 * bare [int] is dynamic: its backing store is reallocated on grow. */
static bool array_spelling_is_fixed(const char *s) {
    if (!s || s[0] != '[') return false;
    int depth = 0;
    for (const char *c = s; *c; c++) {
        if (*c == '[') depth++;
        else if (*c == ']') { if (--depth == 0) break; }
        else if (*c == ',' && depth == 1) return true;
    }
    return false;
}

/* True when `left` indexes a dynamic '[T]' array. A dynamic array's
 * backing store relocates on grow (append / prepend / insert_at), so a
 * raw pointer to an element dangles after any such call — the same
 * hazard the map-index guard covers. Fixed-size '[T,N]' storage never
 * moves. Only a plain variable reference is inspected for its written
 * type; any other index target is treated as dynamic. */
static bool array_index_left_is_dynamic(TypeChecker *checker, AstNode *left) {
    GrayType *lt = resolve_expression(checker, left);
    if (!lt || lt->kind != TK_ARRAY) return false;
    if (left && left->kind == NODE_LABEL) {
        Symbol *sym = scope_lookup(checker->current_scope, left->data.label.value);
        if (!sym) {
            char key[MSG_BUF_SIZE];
            DeclEntry *entry = checker_resolve_entry(checker, left->data.label.value);
            if (entry)
                sym = scope_lookup(checker->current_scope,
                                   module_mangle_into(entry, key, sizeof(key)));
        }
        if (sym && array_spelling_is_fixed(sym->declared_type)) return false;
    }
    return true;
}

/* True if the access path contains an index into a dynamic '[T]' array,
 * e.g. addr(a[i]) or ref(rows[i].cells[j]). Mirrors
 * path_contains_map_index. */
static bool path_contains_dynamic_array_index(TypeChecker *checker, AstNode *e) {
    if (!e) return false;
    switch (e->kind) {
    case NODE_MEMBER_EXPR:
        return path_contains_dynamic_array_index(checker, e->data.member.object);
    case NODE_INDEX_EXPR:
        if (array_index_left_is_dynamic(checker, e->data.index_expr.left))
            return true;
        return path_contains_dynamic_array_index(checker, e->data.index_expr.left);
    case NODE_POSTFIX_EXPR:
        return path_contains_dynamic_array_index(checker, e->data.postfix.left);
    default:
        return false;
    }
}

/* True if the access path indexes into a string, e.g. addr(s[i]). A string
 * byte is a computed value widened to a codepoint (strings are immutable),
 * not an addressable slot. Mirrors path_contains_dynamic_array_index. */
static bool path_contains_string_index(TypeChecker *checker, AstNode *e) {
    if (!e) return false;
    switch (e->kind) {
    case NODE_MEMBER_EXPR:
        return path_contains_string_index(checker, e->data.member.object);
    case NODE_INDEX_EXPR: {
        GrayType *lt = resolve_expression(checker, e->data.index_expr.left);
        if (lt && lt->kind == TK_STRING) return true;
        return path_contains_string_index(checker, e->data.index_expr.left);
    }
    case NODE_POSTFIX_EXPR:
        return path_contains_string_index(checker, e->data.postfix.left);
    default:
        return false;
    }
}

static GrayType *resolve_builtin_call(TypeChecker *checker, AstNode *node, const char *function_name) {
    GrayType *result = &TYPE_UNKNOWN;
    if (typechecker_is_builtin(function_name)) {
        /* E5034: named arguments are not supported for builtins */
        if (typechecker_has_named_arguments(node)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "named arguments are not supported for builtin function '%s'",
                function_name);
            diagnostic_error_message(checker->diag, "E5034", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* Check built-in functions first */
    if (strcmp(function_name, "addr") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        /* addr() requires an assignment target. is_assignment_target recurses into
         * member/index chains so 'addr(some_call().field)' is
         * rejected at typecheck instead of leaking an
         * '&(rvalue)' to clang. */
        if (path_contains_map_index(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3161",
                "'addr()' cannot take the address of a map index expression; map values may relocate on rehash",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (path_contains_dynamic_array_index(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3161",
                "'addr()' cannot take the address of a dynamic array index expression; the backing store may relocate when the array grows",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (path_contains_string_index(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3161",
                "'addr()' cannot take the address of a string index expression; string bytes are immutable",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (!is_assignment_target(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3012",
                "'addr()' requires a variable, field, or index expression; cannot take address of a literal or expression",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        GrayType *arg_t = resolve_expression(checker, arg);
        result = type_pointer(type_name(arg_t));
    } else if (strcmp(function_name, "raw") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        if (path_contains_map_index(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3161",
                "'raw()' cannot take the address of a map index expression; map values may relocate on rehash",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (path_contains_dynamic_array_index(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3161",
                "'raw()' cannot take the address of a dynamic array index expression; the backing store may relocate when the array grows",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (path_contains_string_index(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3161",
                "'raw()' cannot take the address of a string index expression; string bytes are immutable",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (!is_assignment_target(checker, arg)) {
            diagnostic_error_message(checker->diag, "E3012",
                "'raw()' requires a variable, field, or index expression; cannot take address of a literal or expression",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        GrayType *arg_t = resolve_expression(checker, arg);
        result = type_pointer(type_name(arg_t));
    } else if (strcmp(function_name, "ref") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        /* ref(func_name) returns func type; resolve the
         * function lookup BEFORE calling resolve_expression on the
         * label, otherwise the E3031 "bare function name as
         * value" check fires and rejects a legitimate
         * use ( follow-up). */
        if (arg->kind == NODE_MEMBER_EXPR && ref_names_function(checker, arg)) {
            /* ref(name) and ()name are the same thing written two ways, so a
             * qualified argument becomes the func-reference node the other
             * spelling parses to. Left as a call it was checked as a pointer
             * to a value: 'mod.func' has none, and the reference came out a
             * ^unknown that could not be called. */
            node->kind = NODE_FUNC_REF;
            node->data.func_ref.function = arg;
            result = resolve_func_ref(checker, node);
        } else if (arg->kind == NODE_LABEL &&
            find_func(checker, arg->data.label.value)) {
            FuncSig *rfs = find_func(checker, arg->data.label.value);
            if (rfs) rfs->used = true;
            warn_if_func_deprecated(checker, node, rfs);
            reject_test_fn_reference(checker, node, rfs);
            /* Build typed function reference: "func(int,string)->int" */
            char sig[MSG_BUF_SIZE];
            int pos = 0;
            pos += snprintf(sig + pos, sizeof(sig) - pos, "func(");
            for (int i = 0; i < rfs->param_count; i++) {
                if (i > 0) pos += snprintf(sig + pos, sizeof(sig) - pos, ",");
                pos += snprintf(sig + pos, sizeof(sig) - pos, "%s", type_name(rfs->param_types[i]));
            }
            pos += snprintf(sig + pos, sizeof(sig) - pos, ")");
            if (rfs->return_count > 0) {
                pos += snprintf(sig + pos, sizeof(sig) - pos, "->%s", type_name(rfs->return_types[0]));
            }
            result = type_from_name(sig);
        } else {
            /* Same assignment target requirement as addr(): reject literals,
             * call results, arithmetic expressions — anything
             * without a stable address. Without this, ref(42) and
             * ref(some_call()) leaked '&42' / '&(rvalue)' to clang
             * and produced opaque generated-C errors. */
            if (path_contains_map_index(checker, arg)) {
                diagnostic_error_message(checker->diag, "E3161",
                    "'ref()' cannot take a reference to a map index expression; map values may relocate on rehash",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            if (path_contains_dynamic_array_index(checker, arg)) {
                diagnostic_error_message(checker->diag, "E3161",
                    "'ref()' cannot take a reference to a dynamic array index expression; the backing store may relocate when the array grows",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            if (path_contains_string_index(checker, arg)) {
                diagnostic_error_message(checker->diag, "E3161",
                    "'ref()' cannot take a reference to a string index expression; string bytes are immutable",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            if (!is_assignment_target(checker, arg)) {
                diagnostic_error_message(checker->diag, "E3012",
                    "'ref()' requires a variable, field, or index expression; cannot take a reference to a literal, call result, or expression",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            /* Build a pointer type that preserves the full source type.
             * For arrays, type_name returns the element type ("int"),
             * so reconstruct the full name ("[int]"). */
            GrayType *arg_t = resolve_expression(checker, arg);
            const char *pointee_name = type_name(arg_t);
            if (arg_t->kind == TK_ARRAY) {
                char buffer[MSG_BUF_SIZE];
                snprintf(buffer, sizeof(buffer), "[%s]", arg_t->element_type);
                pointee_name = strdup(buffer);
            } else if (arg_t->kind == TK_MAP) {
                pointee_name = strdup(arg_t->name);
            }
            result = type_pointer(pointee_name);
        }
    } else if (strcmp(function_name, "len") == 0) {
        /* E7015 (): len() only works on string / array / map.
         * Codegen blindly emits '.len' on the receiver, which
         * works for the runtime's GrayArray / GrayMap / GrayString
         * structs but bombs with a raw clang "no member 'len'"
         * on anything else; structs, enums, primitives,
         * pointers, func refs, errors. Validate the argument
         * type here instead of letting it leak. */
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'len()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        } else {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            reject_void_in_context(checker, node->data.call.args[0], at,
                "'len()' argument");
            if (at && at->kind != TK_UNKNOWN && at->kind != TK_VOID &&
                at->kind != TK_STRING && at->kind != TK_ARRAY &&
                at->kind != TK_MAP) {
                diagnostic_error_code_formatted(checker->diag, "E7015", NODE_FILE(checker, node->data.call.args[0]), node->data.call.args[0]->token.line,
                    node->data.call.args[0]->token.column, 0, type_name(at));
            }
        }
        result = &TYPE_INT;
    } else if (strcmp(function_name, "type_of") == 0) {
        /* E5008: type_of() requires exactly 1 argument */
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'type_of()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
            result = &TYPE_STRING;
            return result;
        }
        /* E3084: type_of() with a type name instead of a value */
        if (node->data.call.arg_count > 0) {
            AstNode *arg = node->data.call.args[0];
            if (arg->kind == NODE_LABEL) {
                const char *aname = arg->data.label.value;
                Symbol *sym = scope_lookup(checker->current_scope, aname);
                if (!sym && (is_struct_name(checker, aname) || is_enum_name(checker, aname))) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "'type_of()' expects a value, not a type name '%s'; use 'type_of(instance)' instead",
                        aname);
                    diagnostic_error_message(checker->diag, "E3084", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                    result = &TYPE_STRING;
                    return result;
                }
            }
        }
        /* E3038: type_of() on void function result. A bare builtin type name
         * is answered from the name itself — resolving it as an expression
         * would report it as a type name used as a value. */
        if (node->data.call.arg_count > 0) {
            AstNode *arg = node->data.call.args[0];
            if (arg->kind == NODE_LABEL && typechecker_is_builtin(arg->data.label.value)) {
                GrayType *bt = type_from_name(arg->data.label.value);
                if (bt->kind != TK_UNKNOWN) {
                    typetable_set(checker->type_table, arg, bt);
                    result = &TYPE_STRING;
                    return result;
                }
            }
            GrayType *arg_t = resolve_expression(checker, node->data.call.args[0]);
            if (arg_t->kind == TK_VOID) {
                diagnostic_error_message(checker->diag, "E3038",
                    "cannot use 'type_of()' on a void function; the function does not return a value",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        result = &TYPE_STRING;
    } else if (strcmp(function_name, "fields") == 0) {
        /* E5008: fields() requires exactly 1 argument */
        if (node->data.call.arg_count != 1) {
            char *msg = typechecker_format(checker,
                "'fields()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
            result = type_array("string");
            return result;
        }
        /* E5043: reject bare type names (fields() needs an instance) */
        if (node->data.call.args[0]->kind == NODE_LABEL) {
            const char *aname = node->data.call.args[0]->data.label.value;
            Symbol *sym = scope_lookup(checker->current_scope, aname);
            /* An alias names the same type its target does, so the registries
             * have to be consulted with the resolved name — `alias Vec2 =
             * Point` is a type name here just as much as `Point` is. The
             * diagnostic still names the spelling the programmer wrote. */
            const char *resolved = resolve_type_alias(checker,
                checker_resolve_type_name(checker, aname));
            if (!sym && (is_struct_name(checker, resolved) || is_enum_name(checker, resolved))) {
                char *msg = typechecker_format(checker,
                    "'fields()' requires a struct instance, not a type name '%s'; use 'fields(instance)' instead",
                    aname);
                diagnostic_error_message(checker->diag, "E5043", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                result = type_array("string");
                return result;
            }
        }
        GrayType *arg_t = resolve_expression(checker, node->data.call.args[0]);
        /* Auto-deref pointers to structs */
        if (arg_t && arg_t->kind == TK_POINTER && arg_t->element_type) {
            if (is_struct_name(checker, arg_t->element_type)) {
                arg_t = typechecker_type_from_name(checker, arg_t->element_type);
            }
        }
        /* E5043: fields() requires a struct */
        if (arg_t && arg_t->kind != TK_STRUCT && arg_t->kind != TK_UNKNOWN) {
            char *msg = typechecker_format(checker,
                "'fields()' requires a struct instance, got '%s'",
                type_name(arg_t));
            diagnostic_error_message(checker->diag, "E5043", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        result = type_array("string");
    } else if (strcmp(function_name, "size_of") == 0) {
        if (node->data.call.arg_count != 1) {
            char *msg = typechecker_format(checker,
                "'size_of()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
            result = &TYPE_INT;
            return result;
        }
        /* Rewrite size_of(T) → size_of(?) when T is a type param */
        if (node->data.call.arg_count == 1 &&
            node->data.call.args[0]->kind == NODE_LABEL &&
            checker->type_param_name &&
            strcmp(node->data.call.args[0]->data.label.value,
                   checker->type_param_name) == 0) {
            node->data.call.args[0]->data.label.value = "?";
        }
        {
            AstNode *arg = node->data.call.args[0];
            const char *written = size_of_type_spelling(checker, arg);
            if (written && strcmp(written, "?") != 0) {
                /* A container spelling types as TK_ARRAY or TK_MAP whatever
                 * its parts name, so the check has to look at every leaf. */
                char leaf[MSG_BUF_SIZE];
                const char *undefined = undefined_type_leaf(checker, written, leaf, sizeof(leaf));
                if (undefined) {
                    char *msg = typechecker_format(checker,
                        "undefined type '%s'; check the spelling or import the module that defines it",
                        unqualified_display_name(undefined));
                    diagnostic_error_message(checker->diag, "E4016", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                } else {
                    /* Normalize every spelling to the one shape codegen's type
                     * path understands: a label holding the registry name. */
                    const char *resolved = resolve_type_alias(checker,
                        checker_resolve_type_name(checker, written));
                    if (arg->kind != NODE_LABEL || strcmp(resolved, written) != 0) {
                        AstNode *label = ast_alloc(checker->arena, NODE_LABEL, arg->token);
                        label->data.label.value = resolved;
                        node->data.call.args[0] = label;
                    }
                }
            }
        }
        result = &TYPE_INT;
    } else if (strcmp(function_name, "to_char") == 0) {
        if (node->data.call.arg_count != 2) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'to_char()' expects 2 arguments (string, index), got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        } else {
            GrayType *arg0 = resolve_expression(checker, node->data.call.args[0]);
            GrayType *arg1 = resolve_expression(checker, node->data.call.args[1]);
            if (arg0->kind != TK_STRING) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'to_char()' first argument must be a string, got '%s'",
                    type_name(arg0));
                tc_err_arg_type(checker, node, msg);
            }
            if (arg1->kind != TK_INT && arg1->kind != TK_UINT) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'to_char()' second argument must be int or uint, got '%s'",
                    type_name(arg1));
                tc_err_arg_type(checker, node, msg);
            } else {
                /* Constant index provably out of bounds — gray_builtin_to_char
                 * panics P0049 (negative) / P0050 (past end). Dynamic indices
                 * stay runtime-checked. The upper bound is only known when the
                 * string is a plain literal (no escapes to expand). */
                int64_t idx; bool ov = false;
                if (typechecker_fold_const_int(checker, node->data.call.args[1], &idx, &ov) && !ov) {
                    int64_t chars = -1;
                    AstNode *s = node->data.call.args[0];
                    if (s->kind == NODE_STRING_VALUE && s->data.string_value.value &&
                        !strchr(s->data.string_value.value, '\\')) {
                        chars = 0;
                        for (const unsigned char *p = (const unsigned char *)s->data.string_value.value;
                             *p; p++)
                            if ((*p & 0xC0) != 0x80) chars++;
                    }
                    if (idx < 0 || (chars >= 0 && idx >= chars)) {
                        AstNode *a = node->data.call.args[1];
                        char *msg = chars >= 0
                            ? typechecker_format(checker,
                                "'to_char()' index %lld is out of bounds for a string of %lld characters",
                                (long long)idx, (long long)chars)
                            : typechecker_format(checker,
                                "'to_char()' index cannot be negative; got %lld", (long long)idx);
                        diagnostic_error_message(checker->diag, "E5045", msg,
                            NODE_FILE(checker, node), a->token.line, a->token.column, 0);
                    }
                }
            }
        }
        result = &TYPE_CHAR;
    } else if (strcmp(function_name, "char_count") == 0) {
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'char_count()' expects 1 argument (string), got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        } else {
            GrayType *arg0 = resolve_expression(checker, node->data.call.args[0]);
            if (arg0->kind != TK_STRING) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'char_count()' argument must be a string, got '%s'",
                    type_name(arg0));
                tc_err_arg_type(checker, node, msg);
            }
        }
        result = &TYPE_INT;
    } else if (strcmp(function_name, "c_string") == 0) {
        if (node->data.call.arg_count != 1) {
            char *msg = typechecker_format(checker,
                "'c_string()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
            result = &TYPE_STRING;
            return result;
        }
        if (node->data.call.arg_count >= 1) {
            GrayType *arg0 = resolve_expression(checker, node->data.call.args[0]);
            if (arg0 && arg0->kind != TK_POINTER && arg0->kind != TK_UNKNOWN) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'c_string()' requires a raw C pointer; '%s' is not a pointer type. "
                    "'c_string()' is only valid with values from C interop ('extern import \"header.h\"')",
                    type_name(arg0));
                diagnostic_error_message(checker->diag, "E3083", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        result = &TYPE_STRING;
    } else if (strcmp(function_name, "input") == 0) {
        result = &TYPE_STRING;
    } else if (strcmp(function_name, "here") == 0) {
        if (node->data.call.arg_count != 0) {
            diagnostic_error_code(checker->diag, "E5014",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        result = type_struct("SourceLocation");
    } else if (strcmp(function_name, "embed") == 0) {
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'embed()' takes exactly 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        } else {
            AstNode *arg = node->data.call.args[0];
            if (arg->kind != NODE_STRING_VALUE) {
                diagnostic_error_code(checker->diag, "E5017",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            } else {
                /* Resolve file path relative to source file directory */
                const char *embed_path = arg->data.string_value.value;
                char resolved[4096];
                const char *source_file = NODE_FILE(checker, node);
                /* Length of source_file's directory prefix, including the
                 * trailing separator; 0 when the path has no directory part. */
                size_t src_dir_len =
                    source_file ? (size_t)(gray_path_basename(source_file) - source_file) : 0;
                if (!gray_path_is_absolute(embed_path) && src_dir_len > 0) {
                    snprintf(resolved, sizeof(resolved), "%.*s%s",
                        (int)src_dir_len, source_file, embed_path);
                } else {
                    snprintf(resolved, sizeof(resolved), "%s", embed_path);
                }
                /* Reject path traversal outside the source directory */
                char real_embed[4096];
                char real_src_dir[4096];
                if (gray_realpath_into(resolved, real_embed, sizeof(real_embed))) {
                    bool escaped = true;
                    if (source_file) {
                        bool have_dir;
                        if (src_dir_len > 0) {
                            char src_dir[4096];
                            snprintf(src_dir, sizeof(src_dir), "%.*s",
                                (int)(src_dir_len - 1), source_file);
                            have_dir =
                                gray_realpath_into(src_dir, real_src_dir, sizeof(real_src_dir));
                        } else {
                            /* source file has no directory component — cwd is the root */
                            have_dir = gray_realpath_into(".", real_src_dir, sizeof(real_src_dir));
                        }
                        if (have_dir && path_within_dir(real_embed, real_src_dir)) escaped = false;
                    }
                    if (escaped) {
                        diagnostic_error_code(checker->diag, "E5027",
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                        goto embed_done;
                    }
                }
                FILE *ef = fopen(resolved, "r");
                if (!ef) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "'embed()' cannot open '%s': file not found or unreadable",
                        embed_path);
                    diagnostic_error_message(checker->diag, "E5018", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                } else {
                    fclose(ef);
                }
                embed_done:;
            }
        }
        result = &TYPE_STRING;
    } else if (strcmp(function_name, "error") == 0) {
        /* Forms: error(msg), error(code), error(code, msg). code is an
         * ErrorCode; a bare message defaults its code to .Unknown. */
        int argc = node->data.call.arg_count;
        if (argc < 1 || argc > 2) {
            diagnostic_error_code_formatted(checker->diag, "E5048",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                argc == 0 ? "no arguments" : "too many arguments");
            result = type_from_name("Error");
            return result;
        }
        AstNode *earg = node->data.call.args[0];
        GrayType *saved_expected = checker->expected_type;
        checker->expected_type = type_from_name("ErrorCode");
        GrayType *eat = resolve_expression(checker, earg);
        checker->expected_type = saved_expected;
        bool a0_is_code = eat && eat->kind == TK_ENUM && eat->name &&
            typechecker_enum_is_error_code(checker, eat->name);
        bool a0_is_string = eat && eat->kind == TK_STRING;
        /* C interop values (extern.SYMBOL) resolve to TK_UNKNOWN, which the
         * checks below carve out. Left alone, error(extern.EXIT_FAILURE, ...)
         * lowers to gray_error_new(arena, (int64_t)(EXIT_FAILURE), ...) and
         * the constant's raw value is reinterpreted as an ErrorCode slot. */
        bool a0_is_extern = earg->kind == NODE_MEMBER_EXPR &&
            ast_member_qualifier(earg) &&
            strcmp(ast_member_qualifier(earg), "extern") == 0;
        if (a0_is_extern) {
            char *got = typechecker_format(checker,
                "'extern.%s', a C interop constant, as the code",
                earg->data.member.member);
            diagnostic_error_code_formatted(checker->diag, "E5048",
                NODE_FILE(checker, earg), earg->token.line, earg->token.column, 0, got);
        }
        if (argc == 1) {
            if (!a0_is_extern && !a0_is_code && !a0_is_string && eat && eat->kind != TK_UNKNOWN) {
                diagnostic_error_code_formatted(checker->diag, "E5044",
                    NODE_FILE(checker, earg), earg->token.line, earg->token.column, 0,
                    type_display_name(checker, eat));
            }
        } else {
            /* error(code, msg) */
            if (!a0_is_extern && !a0_is_code && eat && eat->kind != TK_UNKNOWN) {
                diagnostic_error_code_formatted(checker->diag, "E5048",
                    NODE_FILE(checker, earg), earg->token.line, earg->token.column, 0,
                    "a message with no code");
            }
            AstNode *marg = node->data.call.args[1];
            GrayType *mat = resolve_expression(checker, marg);
            if (mat && mat->kind != TK_STRING && mat->kind != TK_UNKNOWN) {
                diagnostic_error_code_formatted(checker->diag, "E5044",
                    NODE_FILE(checker, marg), marg->token.line, marg->token.column, 0,
                    type_display_name(checker, mat));
            }
        }
        result = type_from_name("Error");
    } else if (strcmp(function_name, "println") == 0 || strcmp(function_name, "eprintln") == 0) {
        /* println/eprintln accept 0 or 1 arguments */
        if (node->data.call.arg_count > 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'%s()' expects 0 or 1 argument(s), got %d",
                function_name, node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        }
        if (node->data.call.arg_count >= 1) {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            if (at->kind == TK_FUNCTION) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot pass a func reference to '%s()'; func references are not printable values",
                    function_name);
                diagnostic_error_message(checker->diag, "E5028", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            if (at->kind == TK_ENUM && at->name && typechecker_enum_is_tagged(checker, at->name)) {
                diagnostic_error_code_formatted(checker->diag, "E5038",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    enum_display_name(checker, at->name), function_name);
            }
            char context[TYPE_NAME_MAX];
            snprintf(context, sizeof(context), "'%s()' argument", function_name);
            reject_void_in_context(checker, node->data.call.args[0], at, context);
        }
        result = &TYPE_VOID;
    } else if (strcmp(function_name, "print") == 0 || strcmp(function_name, "eprint") == 0) {
        /* print/eprint accept exactly 1 argument */
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'%s()' expects 1 argument, got %d",
                function_name, node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        }
        if (node->data.call.arg_count >= 1) {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            if (at->kind == TK_FUNCTION) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot pass a func reference to '%s()'; func references are not printable values",
                    function_name);
                diagnostic_error_message(checker->diag, "E5028", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            if (at->kind == TK_ENUM && at->name && typechecker_enum_is_tagged(checker, at->name)) {
                diagnostic_error_code_formatted(checker->diag, "E5038",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    enum_display_name(checker, at->name), function_name);
            }
            char context[TYPE_NAME_MAX];
            snprintf(context, sizeof(context), "'%s()' argument", function_name);
            reject_void_in_context(checker, node->data.call.args[0], at, context);
        }
        result = &TYPE_VOID;
    } else if (strcmp(function_name, "flush") == 0) {
        if (node->data.call.arg_count != 0) {
            char *msg = typechecker_format(checker,
                "'flush()' expects 0 arguments, got %d", node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        }
        result = &TYPE_VOID;
    } else if (strcmp(function_name, "exit") == 0 || strcmp(function_name, "panic") == 0 ||
               strcmp(function_name, "assert") == 0 ||
               strcmp(function_name, "sleep_s") == 0 ||
               strcmp(function_name, "sleep_ms") == 0 ||
               strcmp(function_name, "sleep_ns") == 0) {
        /* Validate argument types for these builtins */
        if (strcmp(function_name, "exit") == 0 && node->data.call.arg_count >= 1) {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            if (at->kind != TK_UNKNOWN && !is_int_kind(at->kind)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'exit()' expects an integer argument, got '%s'", type_name(at));
                tc_err_arg_type(checker, node, msg);
            }
        } else if (strcmp(function_name, "panic") == 0 && node->data.call.arg_count >= 1) {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            if (at->kind != TK_UNKNOWN && at->kind != TK_STRING) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'panic()' expects a string argument, got '%s'", type_name(at));
                tc_err_arg_type(checker, node, msg);
            }
        } else if (strcmp(function_name, "assert") == 0 && node->data.call.arg_count >= 1) {
            GrayType *cond_t = resolve_expression(checker, node->data.call.args[0]);
            if (cond_t->kind != TK_UNKNOWN && cond_t->kind != TK_BOOL) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'assert()' condition must be a bool, got '%s'", type_name(cond_t));
                tc_err_arg_type(checker, node, msg);
            }
            if (node->data.call.arg_count >= 2) {
                GrayType *msg_t = resolve_expression(checker, node->data.call.args[1]);
                if (msg_t->kind != TK_UNKNOWN && msg_t->kind != TK_STRING) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "'assert()' message must be a string, got '%s'", type_display_name(checker, msg_t));
                    tc_err_arg_type(checker, node, msg);
                }
            }
        } else if ((strcmp(function_name, "sleep_s") == 0 || strcmp(function_name, "sleep_ms") == 0 ||
                    strcmp(function_name, "sleep_ns") == 0) && node->data.call.arg_count >= 1) {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            if (at->kind != TK_UNKNOWN && !is_int_kind(at->kind)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s()' expects an integer argument, got '%s'", function_name, type_name(at));
                tc_err_arg_type(checker, node, msg);
            }
        }
        result = &TYPE_VOID;
    } else if (strcmp(function_name, "system") == 0) {
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'system()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
        } else {
            GrayType *at = resolve_expression(checker, node->data.call.args[0]);
            if (at->kind != TK_UNKNOWN && at->kind != TK_STRING) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'system()' expects a string argument, got '%s'", type_name(at));
                tc_err_arg_type(checker, node, msg);
            }
        }
        result = &TYPE_INT;
    } else if (strcmp(function_name, "copy") == 0 && node->data.call.arg_count == 1) {
        result = resolve_expression(checker, node->data.call.args[0]);
        if (result->kind == TK_FUNCTION) {
            diagnostic_error_message(checker->diag, "E5029",
                arena_copy_string(checker->arena, "'copy()' cannot be used on a func reference; func references are compile-time aliases, not copyable values"),
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            result = &TYPE_UNKNOWN;
        } else if (result->kind == TK_POINTER) {
            diagnostic_error_code(checker->diag, "E5037",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            result = &TYPE_UNKNOWN;
        }
    } else if (strcmp(function_name, "char") == 0) {
        /* E5008: char() requires exactly 1 argument */
        if (node->data.call.arg_count != 1) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'char()' expects 1 argument, got %d",
                node->data.call.arg_count);
            tc_err_arity(checker, node, msg);
            result = &TYPE_CHAR;
            return result;
        }
        /* E7014: char() with negative integer */
        int64_t lit_val;
        if (try_get_literal_int(node->data.call.args[0], &lit_val) && lit_val < 0) {
            diagnostic_error_code_formatted(checker->diag, "E7014", NODE_FILE(checker, node), node->token.line, node->token.column, 0, (long long)lit_val);
        }
        /* E5026: char() converts an integer codepoint; reject any non-integer
         * argument (a length-1 string still reaches codegen otherwise and
         * emits (int32_t)(GrayString), which cc rejects). */
        GrayType *char_arg_t = resolve_expression(checker, node->data.call.args[0]);
        if (!is_int_kind(char_arg_t->kind) && char_arg_t->kind != TK_CHAR) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'char()' expects an integer codepoint, got %s",
                type_name(char_arg_t));
            tc_err_arg_type(checker, node, msg);
        }
        result = &TYPE_CHAR;
    } else if ((strcmp(function_name, "int") == 0 ||
                strcmp(function_name, "uint") == 0 ||
                strcmp(function_name, "byte") == 0 ||
                is_bigint_type(function_name)) &&
               node->data.call.arg_count == 1) {
        /* E3043: validate source type is convertible to numeric */
        GrayType *src_t = resolve_expression(checker, node->data.call.args[0]);
        if (src_t->kind == TK_ARRAY || src_t->kind == TK_MAP ||
            src_t->kind == TK_STRUCT || src_t->kind == TK_POINTER) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot convert %s to %s; only numeric types, strings, and bools can be converted",
                type_name(src_t), function_name);
            diagnostic_error_help(checker->diag, "E3043", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                "only numeric, enum, and string conversions are supported");
        }
        if (strcmp(function_name, "byte") == 0)
            result = &TYPE_BYTE;
        else if (is_unsigned_type(function_name))
            result = &TYPE_UINT;
        else
            result = &TYPE_INT;
    } else if (strcmp(function_name, "string") == 0 && node->data.call.arg_count == 1) {
        /* E3043: validate source type is convertible to string */
        GrayType *src_t = resolve_expression(checker, node->data.call.args[0]);
        if (src_t->kind == TK_STRING) {
            diagnostic_error_message(checker->diag, "E3043",
                arena_copy_string(checker->arena, "cannot convert string to string; value is already a string"),
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        } else if (src_t->kind == TK_ARRAY || src_t->kind == TK_MAP ||
                   src_t->kind == TK_STRUCT || src_t->kind == TK_POINTER) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot convert %s to string; use string interpolation or access individual elements",
                type_name(src_t));
            diagnostic_error_message(checker->diag, "E3043", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        result = &TYPE_STRING;
    } else if (strcmp(function_name, "float") == 0 && node->data.call.arg_count == 1) {
        /* E3043: validate source type is convertible to float */
        GrayType *src_t = resolve_expression(checker, node->data.call.args[0]);
        if (src_t->kind == TK_ARRAY || src_t->kind == TK_MAP ||
            src_t->kind == TK_STRUCT || src_t->kind == TK_POINTER ||
            src_t->kind == TK_BOOL) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot convert %s to float; only numeric types and strings can be converted",
                type_name(src_t));
            diagnostic_error_message(checker->diag, "E3043", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        result = &TYPE_FLOAT;
    } else if (strcmp(function_name, "bool") == 0 && node->data.call.arg_count == 1) {
        GrayType *src_t = resolve_expression(checker, node->data.call.args[0]);
        if (src_t->kind == TK_ARRAY || src_t->kind == TK_MAP ||
            src_t->kind == TK_STRUCT || src_t->kind == TK_POINTER ||
            src_t->kind == TK_STRING) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot convert %s to bool; only numeric types and bools can be converted",
                type_name(src_t));
            diagnostic_error_message(checker->diag, "E3043", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        result = &TYPE_BOOL;
    } else if ((strcmp(function_name, "i8") == 0 || strcmp(function_name, "i16") == 0 ||
                strcmp(function_name, "i32") == 0 || strcmp(function_name, "i64") == 0 ||
                strcmp(function_name, "u8") == 0 || strcmp(function_name, "u16") == 0 ||
                strcmp(function_name, "u32") == 0 || strcmp(function_name, "u64") == 0 ||
                strcmp(function_name, "f32") == 0 || strcmp(function_name, "f64") == 0)) {
        diagnostic_error_code_formatted(checker->diag, "E5036", NODE_FILE(checker, node),
            node->token.line, node->token.column, 0, function_name, function_name);
        result = type_from_name(function_name);
    } else if (typechecker_is_builtin(function_name)) {
        /* Builtin name matched but arg count was wrong — the specific handler
         * above requires a certain number of arguments and this call didn't
         * satisfy it.  Emit E5008 instead of falling through to the
         * user-defined function path, which would leak a C compiler error. */
        char *msg = typechecker_format(checker,
            "'%s()' expects 1 argument, got %d",
            function_name, node->data.call.arg_count);
        tc_err_arity(checker, node, msg);
        GrayType *bt = type_from_name(function_name);
        result = (bt != &TYPE_UNKNOWN) ? bt : &TYPE_UNKNOWN;
    } else {
        return NULL;
    }
    return result;
}

static GrayType *resolve_direct_call(TypeChecker *checker, AstNode *node, const char *function_name) {
    GrayType *result = &TYPE_UNKNOWN;
    FuncSig *sig = find_func(checker, function_name);
    /* A bare name inside a struct function body falls back to the enclosing
     * struct's namespace, where struct functions are registered as
     * <Struct>_<func>. Top-level functions still win (E4022 rejects a struct
     * function that shares a name with one), so this only fills in where the
     * lookup would otherwise fail. Rewriting the label to the registered name
     * lets codegen resolve it exactly, without guessing at the owning struct. */
    if (!sig && checker->current_struct_name) {
        AstNode *fn = node->data.call.function;
        if (fn && fn->kind == NODE_LABEL) {
            char mangled[MSG_BUF_SIZE];
            {
                char sk[MSG_BUF_SIZE];
                snprintf(mangled, sizeof(mangled), "%s_%s",
                    checker_resolve_decl_into(checker, checker->current_struct_name, sk, sizeof(sk)),
                    function_name);
            }
            sig = find_func(checker, mangled);
            if (sig) {
                fn->data.label.value = arena_copy_string(checker->arena, mangled);
                function_name = fn->data.label.value;
            }
        }
    }
    if (sig) {
        sig->used = true;
        warn_if_func_deprecated(checker, node, sig);
        reject_test_fn_reference(checker, node, sig);
        /* Use the user-facing name in error messages, never
         * the module-prefixed internal key. */
        function_name = func_display_name(sig);
        /* : if this bare name is a using-module alias,
         * also mark the prefixed sig + import as used so
         * W1002/W1003 don't fire on the source. */
        for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
            if (!using_module_accessible(checker, using_index)) continue;
            char pfx[MSG_BUF_SIZE];
            module_member_key(checker, checker->using_modules[using_index],
                              function_name, pfx, sizeof(pfx));
            FuncSig *psig = find_func(checker, pfx);
            if (psig) {
                psig->used = true;
                mark_import_used(checker, checker->using_modules[using_index]);
                mark_import_used(checker,
                    typechecker_resolve_alias(checker, checker->using_modules[using_index]));
                break;
            }
        }
        /* Resolve named arguments before checking counts/types */
        if (sig->decl) {
            typechecker_resolve_named_arguments(checker, node, sig->decl, function_name);
        }
        /* Check argument count; account for default parameters */
        int min_args = sig->param_count;
        if (sig->decl && sig->decl->kind == NODE_FUNC_DECL) {
            min_args = 0;
            for (int parameter_index = 0; parameter_index < sig->decl->data.func_decl.param_count; parameter_index++) {
                if (!sig->decl->data.func_decl.params[parameter_index].default_value)
                    min_args++;
            }
        }
        if (node->data.call.arg_count < min_args ||
            node->data.call.arg_count > sig->param_count) {
            char *msg = NULL;
            if (min_args == sig->param_count) {
                msg = typechecker_format(checker,
                    "function '%s' expects %d argument(s), got %d",
                    function_name, sig->param_count, node->data.call.arg_count);
            } else {
                msg = typechecker_format(checker,
                    "function '%s' expects %d-%d argument(s), got %d",
                    function_name, min_args, sig->param_count, node->data.call.arg_count);
            }
            tc_err_arity(checker, node, msg);
        }
        /* Generic (wildcard) dispatch: unify each '?' parameter
         * against the corresponding argument to derive a single
         * concrete binding T, record the instantiation, and
         * substitute T into the return type. Skip the normal
         * per-arg check below since '?' would otherwise collapse
         * to TK_UNKNOWN and produce no useful errors. */
        bool is_generic_call = false;
        GrayType *generic_return_t = resolve_generic_call(checker, node, sig,
            function_name, &is_generic_call);

        /* Check argument types */
        int check_count = node->data.call.arg_count < sig->param_count
            ? node->data.call.arg_count : sig->param_count;
        for (int argument_index = 0; argument_index < check_count; argument_index++) {
            /* Skip type parameters — no value to type-check */
            if (sig->decl && sig->decl->kind == NODE_FUNC_DECL &&
                argument_index < sig->decl->data.func_decl.param_count &&
                sig->decl->data.func_decl.params[argument_index].is_type_param)
                continue;
            GrayType *param_t = sig->param_types[argument_index];
            /* Set expected_type for implicit enum resolution */
            GrayType *saved_expected = checker->expected_type;
            if (param_t && param_t->kind == TK_ENUM && param_t->name)
                checker->expected_type = param_t;
            GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
            checker->expected_type = saved_expected;
            if (is_generic_call) {
                /* Generic branch already handled unification;
                 * suppress the scalar param/arg comparison which
                 * would compare against TK_UNKNOWN. */
                continue;
            }
            if (arg_t->kind != TK_UNKNOWN && param_t->kind != TK_UNKNOWN &&
                !types_assignable(checker, param_t, arg_t) &&
                !(param_t->kind == TK_ENUM && is_int_kind(arg_t->kind)) &&
                !(param_t->kind == TK_STRUCT && is_int_kind(arg_t->kind)) &&
                !(is_int_kind(param_t->kind) && arg_t->kind == TK_BOOL) &&
                /* nil is a valid value for pointer and Error parameters */
                !(arg_t->kind == TK_NIL &&
                  (param_t->kind == TK_POINTER || param_t->kind == TK_ERROR))) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "argument %d of '%s': expected %s, got %s",
                    argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
            }
            /* Enum-to-enum: kinds both TK_ENUM but different names */
            if (arg_t->kind == TK_ENUM && param_t->kind == TK_ENUM &&
                arg_t->name && param_t->name &&
                !typechecker_same_enum_type(checker, arg_t->name, param_t->name)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "argument %d of '%s': expected enum '%s', got enum '%s'",
                    argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
            }
            /* Struct-to-struct: kinds both TK_STRUCT but different names */
            if (arg_t->kind == TK_STRUCT && param_t->kind == TK_STRUCT &&
                arg_t->name && param_t->name &&
                !typechecker_same_struct_type(checker, arg_t->name, param_t->name)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "argument %d of '%s': expected struct '%s', got struct '%s'",
                    argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
            }
            /* Pointer-to-pointer: pointee types differ (e.g., addr(Color) to ^Point) */
            if (arg_t->kind == TK_POINTER && param_t->kind == TK_POINTER &&
                arg_t->name && param_t->name &&
                !typechecker_same_struct_type(checker, arg_t->name, param_t->name)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "argument %d of '%s': expected '%s', got '%s'",
                    argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
            }
            /* Bigint narrowing in call argument: i128 arg to i64 param, etc. */
            if (arg_t->name && param_t->name) {
                int ar = int_type_name_rank(arg_t->name);
                int pr = int_type_name_rank(param_t->name);
                if (ar >= 5 && pr > 0 && pr < ar) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s': cannot implicitly narrow '%s' to '%s'; use 'cast(value, %s)' to convert explicitly",
                        argument_index + 1, function_name, arg_t->name, param_t->name, param_t->name);
                    diagnostic_error_message(checker->diag, "E3155", msg,
                        NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0);
                }
            }
            /* Array element type mismatch */
            if (arg_t->kind == TK_ARRAY && param_t->kind == TK_ARRAY &&
                arg_t->element_type && param_t->element_type &&
                !typechecker_same_array_element(checker, arg_t->element_type, param_t->element_type)) {
                GrayType *ae = type_from_name(arg_t->element_type);
                GrayType *pe = type_from_name(param_t->element_type);
                if (!(ae && pe && is_int_kind(ae->kind) && is_int_kind(pe->kind))) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s': expected '%s', got '%s'",
                        argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                    tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                }
            }
            /* Map key/value type mismatch */
            if (arg_t->kind == TK_MAP && param_t->kind == TK_MAP) {
                bool key_mismatch = arg_t->key_type && param_t->key_type &&
                    strcmp(arg_t->key_type, param_t->key_type) != 0;
                bool val_mismatch = arg_t->value_type && param_t->value_type &&
                    strcmp(arg_t->value_type, param_t->value_type) != 0;
                if (key_mismatch || val_mismatch) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "argument %d of '%s': expected '%s', got '%s'",
                        argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                    tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                }
            }
            /* E3066: typed-func signatures must match exactly */
            if (func_types_mismatch(arg_t, param_t)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "argument %d of '%s': expected %s, got %s",
                    argument_index + 1, function_name, type_display_name(checker, param_t), type_display_name(checker, arg_t));
                diagnostic_error_message(checker->diag, "E3066", msg,
                    NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                    node->data.call.args[argument_index]->token.column, 0);
            }
            /* E3027: non-assignable or const passed to mutable (&) param */
            {
                AstNode *arg = node->data.call.args[argument_index];
                for (int field_index = 0; field_index < checker->program->data.program.stmt_count; field_index++) {
                    AstNode *stmt = checker->program->data.program.stmts[field_index];
                    if (stmt->kind != NODE_FUNC_DECL ||
                        strcmp(stmt->data.func_decl.name, function_name) != 0 ||
                        argument_index >= stmt->data.func_decl.param_count ||
                        !stmt->data.func_decl.params[argument_index].mutable)
                        continue;
                    char param_desc[MSG_BUF_SIZE];
                    snprintf(param_desc, sizeof(param_desc), "mutable parameter '%stmt'",
                        stmt->data.func_decl.params[argument_index].name);
                    check_mutable_arg(checker, arg, param_desc, function_name);
                    break;
                }
            }
        }
        if (is_generic_call && generic_return_t) {
            result = generic_return_t;
        } else if (sig->return_count > 0) {
            result = sig->return_types[0];
        } else {
            result = &TYPE_VOID;
        }
    } else {
        /* Check if it's a variable holding a function reference */
        Symbol *fn_sym = scope_lookup(checker->current_scope, function_name);
        bool is_typed_func = fn_sym && fn_sym->type && fn_sym->type->kind == TK_FUNCTION;
        bool is_bare_func = fn_sym && fn_sym->type && type_name(fn_sym->type) &&
                            strcmp(type_name(fn_sym->type), "func") == 0;
        if (is_typed_func || is_bare_func) {
            fn_sym->used = true;
            /* Record the variable's type on the call.function
             * label node so codegen can pick up the typed-func
             * signature for cast emission. */
            if (!checker->suppress_typetable_writes) {
                typetable_set(checker->type_table, node->data.call.function, fn_sym->type);
            }
            result = &TYPE_UNKNOWN; /* callable func ref; return type unknown */
            /* If we know which static function this var holds,
             * validate arity + argument types at compile time
             * and propagate the real return type. */
            FuncSig *ref_sig = fn_sym->func_ref_name
                ? find_func(checker, fn_sym->func_ref_name) : NULL;
            if (ref_sig) {
                /* : compute min arity by counting
                 * params without default values. */
                int min_arity = ref_sig->param_count;
                if (ref_sig->decl && ref_sig->decl->kind == NODE_FUNC_DECL) {
                    min_arity = 0;
                    for (int parameter_index = 0; parameter_index < ref_sig->decl->data.func_decl.param_count; parameter_index++) {
                        if (!ref_sig->decl->data.func_decl.params[parameter_index].default_value)
                            min_arity++;
                    }
                }
                int ac = node->data.call.arg_count;
                if (ac < min_arity || ac > ref_sig->param_count) {
                    char *msg = NULL;
                    if (min_arity == ref_sig->param_count) {
                        msg = typechecker_format(checker,
                            "function '%s' expects %d argument(s), got %d",
                            func_display_name(ref_sig), ref_sig->param_count, ac);
                    } else {
                        msg = typechecker_format(checker,
                            "function '%s' expects %d to %d argument(s), got %d",
                            func_display_name(ref_sig), min_arity, ref_sig->param_count, ac);
                    }
                    tc_err_arity(checker, node, msg);
                } else {
                    for (int argument_index = 0; argument_index < ref_sig->param_count; argument_index++) {
                        GrayType *at = resolve_expression(checker, node->data.call.args[argument_index]);
                        GrayType *pt = ref_sig->param_types[argument_index];
                        if (at && pt && at->kind != TK_UNKNOWN &&
                            pt->kind != TK_UNKNOWN &&
                            !types_assignable(checker, pt, at) &&
                            !(at->kind == TK_NIL &&
                              (pt->kind == TK_POINTER || pt->kind == TK_ERROR))) {
                            char *msg = NULL;
                            msg = typechecker_format(checker,
                                "argument %d of '%s': expected %s, got %s",
                                argument_index + 1, func_display_name(ref_sig),
                                type_display_name(checker, pt), type_display_name(checker, at));
                            tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                        }
                        /* Enum-to-enum: different enum types */
                        if (at && pt && at->kind == TK_ENUM && pt->kind == TK_ENUM &&
                            at->name && pt->name &&
                            strcmp(at->name, pt->name) != 0) {
                            char *msg = NULL;
                            msg = typechecker_format(checker,
                                "argument %d of '%s': expected enum '%s', got enum '%s'",
                                argument_index + 1, func_display_name(ref_sig), enum_display_name(checker, pt->name), enum_display_name(checker, at->name));
                            tc_err_arg_type(checker, node->data.call.args[argument_index], msg);
                        }
                        /* E3027: non-assignable or const passed to mutable (&) param */
                        {
                            AstNode *arg = node->data.call.args[argument_index];
                            const char *ref_name = fn_sym->func_ref_name;
                            for (int field_index = 0; field_index < checker->program->data.program.stmt_count; field_index++) {
                                AstNode *stmt = checker->program->data.program.stmts[field_index];
                                if (stmt->kind != NODE_FUNC_DECL ||
                                    strcmp(stmt->data.func_decl.name, ref_name) != 0 ||
                                    argument_index >= stmt->data.func_decl.param_count ||
                                    !stmt->data.func_decl.params[argument_index].mutable)
                                    continue;
                                char param_desc[MSG_BUF_SIZE];
                                snprintf(param_desc, sizeof(param_desc), "mutable parameter '%stmt'",
                                    stmt->data.func_decl.params[argument_index].name);
                                check_mutable_arg(checker, arg, param_desc, func_display_name(ref_sig));
                                break;
                            }
                        }
                    }
                }
                if (ref_sig->return_count > 0)
                    result = ref_sig->return_types[0];
                else
                    result = &TYPE_VOID;
            } else if (is_typed_func && fn_sym->type->func_sig) {
                /* No source FuncSig; validate against the typed-
                 * func signature from the variable's annotated type
                 * (e.g. callback parameters: do f(g func(int)->int)). */
                GrayFuncSig *sig = fn_sym->type->func_sig;
                int ac = node->data.call.arg_count;
                if (ac != sig->param_count) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "function reference '%s' expects %d argument(s), got %d",
                        function_name, sig->param_count, ac);
                    tc_err_arity(checker, node, msg);
                } else {
                    for (int argument_index = 0; argument_index < sig->param_count; argument_index++) {
                        AstNode *arg = node->data.call.args[argument_index];
                        GrayType *at = resolve_expression(checker, arg);
                        GrayType *pt = sig->param_types[argument_index] ? type_from_name(sig->param_types[argument_index]) : NULL;
                        if (at && pt && at->kind != TK_UNKNOWN && pt->kind != TK_UNKNOWN &&
                            !types_assignable(checker, pt, at) &&
                            !(at->kind == TK_NIL &&
                              (pt->kind == TK_POINTER || pt->kind == TK_ERROR))) {
                            char *msg = NULL;
                            msg = typechecker_format(checker,
                                "argument %d of '%s': expected %s, got %s",
                                argument_index + 1, function_name,
                                type_display_name(checker, pt), type_display_name(checker, at));
                            tc_err_arg_type(checker, arg, msg);
                        }
                        /* E3027: `&` param requires an assignment target */
                        if (sig->param_mutable[argument_index]) {
                            char param_desc[MSG_BUF_SIZE];
                            snprintf(param_desc, sizeof(param_desc), "'&' parameter %d", argument_index + 1);
                            check_mutable_arg(checker, arg, param_desc, function_name);
                        }
                    }
                }
                if (sig->return_count > 0 && sig->return_types[0]) {
                    result = type_from_name(sig->return_types[0]);
                } else {
                    result = &TYPE_VOID;
                }
            }
        } else if (fn_sym && fn_sym->type) {
            /* Variable exists but is not a function */
            diagnostic_error_code_formatted(checker->diag, "E3015", NODE_FILE(checker, node), node->token.line, node->token.column, 0, function_name, type_display_name(checker, fn_sym->type));
        } else if (!typechecker_is_builtin(function_name)) {
            /* Check if it's a function from a 'using' module */
            bool found_in_using = false;
            /* Set when the bare name resolves to a stdlib function, so the
             * same signature checks the qualified form gets can run below. */
            const char *using_stdlib_mod = NULL;
            /* Check for math functions whose return type depends on argument */
            if (!found_in_using && (strcmp(function_name, "abs") == 0 || strcmp(function_name, "neg") == 0 ||
                strcmp(function_name, "min") == 0 || strcmp(function_name, "max") == 0 ||
                strcmp(function_name, "clamp") == 0)) {
                for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
                    if (!using_module_accessible(checker, using_index)) continue;
                    const char *real_mod = typechecker_resolve_alias(checker, checker->using_modules[using_index]);
                    if (strcmp(real_mod, "math") == 0) {
                        found_in_using = true;
                        using_stdlib_mod = real_mod;
                        if (node->data.call.arg_count > 0) {
                            GrayType *arg_t = resolve_expression(checker, node->data.call.args[0]);
                            result = (arg_t && arg_t->kind == TK_FLOAT) ? &TYPE_FLOAT : &TYPE_INT;
                        } else {
                            result = &TYPE_INT;
                        }
                        break;
                    }
                }
            }
            /* Random functions whose return type depends on argument */
            if (!found_in_using && (strcmp(function_name, "choice") == 0 ||
                strcmp(function_name, "shuffle") == 0 || strcmp(function_name, "sample") == 0)) {
                for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
                    if (!using_module_accessible(checker, using_index)) continue;
                    const char *real_mod = typechecker_resolve_alias(checker, checker->using_modules[using_index]);
                    if (strcmp(real_mod, "random") == 0) {
                        found_in_using = true;
                        using_stdlib_mod = real_mod;
                        if (node->data.call.arg_count > 0) {
                            GrayType *arr_t = resolve_expression(checker, node->data.call.args[0]);
                            if (strcmp(function_name, "choice") == 0) {
                                result = (arr_t && arr_t->element_type) ? type_from_name(arr_t->element_type) : &TYPE_INT;
                            } else {
                                result = (arr_t && arr_t->element_type) ? type_array(arr_t->element_type) : type_array("int");
                            }
                        } else {
                            result = (strcmp(function_name, "choice") == 0) ? &TYPE_INT : type_array("int");
                        }
                        break;
                    }
                }
            }
            /* Maps functions whose return type depends on map key/value types */
            if (!found_in_using && (strcmp(function_name, "get_keys") == 0 || strcmp(function_name, "get_values") == 0)) {
                for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
                    if (!using_module_accessible(checker, using_index)) continue;
                    const char *real_mod = typechecker_resolve_alias(checker, checker->using_modules[using_index]);
                    if (strcmp(real_mod, "maps") == 0) {
                        found_in_using = true;
                        using_stdlib_mod = real_mod;
                        if (node->data.call.arg_count > 0) {
                            GrayType *map_t = resolve_expression(checker, node->data.call.args[0]);
                            if (strcmp(function_name, "get_keys") == 0)
                                result = type_array(map_t && map_t->key_type ? map_t->key_type : "string");
                            else
                                result = type_array(map_t && map_t->value_type ? map_t->value_type : "string");
                        } else {
                            result = type_array("string");
                        }
                        break;
                    }
                }
            }
            for (int using_index = 0; using_index < checker->using_module_count && !found_in_using; using_index++) {
                if (!using_module_accessible(checker, using_index)) continue;
                const char *umod = checker->using_modules[using_index];
                /* Resolve alias to actual module name */
                const char *real_mod = typechecker_resolve_alias(checker, umod);
                /* 1) Try stdlib metadata table (O(log n) bsearch) */
                const StdlibFuncMeta *umeta = find_stdlib_meta(real_mod, function_name);
                if (umeta) {
                    found_in_using = true;
                    using_stdlib_mod = real_mod;
                    result = umeta->return_type ? resolve_return_type(umeta->return_type) : &TYPE_UNKNOWN;
                }
                /* 2) Try user-defined module */
                if (!found_in_using) {
                    FuncSig *sig = find_module_func(checker, real_mod, function_name);
                    if (sig) {
                        if (reject_if_private(checker, node, real_mod, function_name)) {
                            found_in_using = true;
                        } else {
                            found_in_using = true;
                            if (sig->return_count > 0) {
                                result = sig->return_types[0];
                            } else {
                                result = &TYPE_VOID;
                            }
                            sig->used = true;
                            warn_if_func_deprecated(checker, node, sig);
                        }
                    }
                }
                if (found_in_using) {
                    /* Mark module as used */
                    mark_import_used(checker, umod);
                    mark_import_used(checker, real_mod);
                }
            }
            if (found_in_using) {
                /* Type already set above. A bare name reaching a stdlib
                 * function is the same call as the qualified form, so it
                 * gets the same signature checks — otherwise a wrong
                 * argument count or type here reaches the C compiler. */
                if (using_stdlib_mod) {
                    typechecker_check_stdlib_arg_count(checker, using_stdlib_mod, function_name, node);
                    typechecker_check_stdlib_arg_types(checker, using_stdlib_mod, function_name, node);
                    typechecker_check_strconv_base(checker, using_stdlib_mod, function_name, node);
                    typechecker_check_io_read_lines_limit(checker, using_stdlib_mod, function_name, node);
                    typechecker_check_const_domain(checker, using_stdlib_mod, function_name, node);
                }
            } else {
                /* Check if it's a variable holding a function reference */
                Symbol *fn_sym = scope_lookup(checker->current_scope, function_name);
                if (fn_sym && fn_sym->type && strcmp(type_name(fn_sym->type), "func") == 0) {
                    fn_sym->used = true;
                    result = &TYPE_UNKNOWN;
                    /* Arity/type validation for func refs is done
                     * at the earlier func-var branch; avoid
                     * re-emitting the same diagnostic here. */
                } else {
                    char *msg = NULL;
                    msg = typechecker_format(checker, "undefined function '%s'", function_name);
                    const char *suggestion = suggest_similar_name(checker, function_name);
                    /* Point at the function name, not the ( */
                    AstNode *fn_node = node->data.call.function;
                    int el = fn_node ? fn_node->token.line : node->token.line;
                    int ec = fn_node ? fn_node->token.column : node->token.column;
                    if (suggestion) {
                        char help[MSG_BUF_SIZE];
                        snprintf(help, sizeof(help), "did you mean '%s'?", suggestion);
                        diagnostic_error_help(checker->diag, "E4002", msg,
                            NODE_FILE(checker, node), el, ec, 0, arena_copy_string(checker->arena, help));
                    } else {
                        diagnostic_error_message(checker->diag, "E4002", msg,
                            NODE_FILE(checker, node), el, ec, 0);
                    }
                }
            }
        }
    }
    return result;
}

/* Collapse mod.Enum.Variant(...) to the Enum.Variant(...) shape, with the
 * enum under its module-prefixed key. Tagged enum construction is recognized
 * from a bare enum name, and the imported form is the same construction
 * written with the module in front. */
static void normalize_qualified_enum_call(TypeChecker *checker, AstNode *node) {
    AstNode *fn = node->data.call.function;
    if (!fn || fn->kind != NODE_MEMBER_EXPR) return;
    const char *mod_raw = NULL, *enum_written = NULL;
    if (!ast_member_chain(fn, &mod_raw, &enum_written)) return;

    AstNode *obj = fn->data.member.object;
    char prefixed[MSG_BUF_SIZE];
    module_member_key(checker, mod_raw, enum_written, prefixed, sizeof(prefixed));
    if (!is_enum_name(checker, prefixed)) return;

    mark_import_used(checker, mod_raw);
    mark_import_used(checker, typechecker_resolve_alias(checker, mod_raw));
    obj->kind = NODE_LABEL;
    obj->data.label.value = arena_copy_string(checker->arena, prefixed);
}

/* Does a bare name name a struct or enum type — its own spelling, or the one
 * an alias reaches — and so no value? */
static bool type_name_as_value(TypeChecker *checker, const char *name) {
    if (!name) return false;
    if (is_struct_name(checker, name) || is_enum_name(checker, name)) return true;
    const char *resolved = resolve_type_alias(checker,
        checker_resolve_type_name(checker, name));
    if (!resolved || strcmp(resolved, name) == 0) return false;
    /* Only a name that actually resolved to something else is judged by what
     * it reached: type_from_name() reads any capitalized name as a struct, so
     * asking it about the written name would answer for every undefined one. */
    return is_struct_name(checker, resolved) || is_enum_name(checker, resolved) ||
           type_from_name(resolved)->kind != TK_UNKNOWN;
}

/* The declaration of the function a call names, for the spellings whose
 * signature can be resolved before the arguments are walked: a bare name and a
 * module-qualified one. Returns NULL for anything else, which leaves the
 * argument loop behaving as it did. */
static AstNode *callee_func_decl(TypeChecker *checker, AstNode *node) {
    AstNode *fn = node->data.call.function;
    if (!fn) return NULL;
    FuncSig *sig = NULL;
    if (fn->kind == NODE_LABEL) {
        sig = find_func(checker, fn->data.label.value);
    } else if (fn->kind == NODE_MEMBER_EXPR) {
        const char *qualifier = ast_member_qualifier(fn);
        if (qualifier)
            sig = find_module_func(checker, qualifier, fn->data.member.member);
    }
    return (sig && sig->decl && sig->decl->kind == NODE_FUNC_DECL) ? sig->decl : NULL;
}

/* The builtins that take a type name. Each owns its own type-name diagnostic
 * — size_of() accepts one, type_of() rejects it with E3084 and fields() with
 * E5043 — so the general path must not resolve the argument and report first.
 * Declared here rather than tested inline, so the predicate below is the one
 * place that answers the question. */
static const struct { const char *name; int index; } builtin_type_arg[] = {
    {"size_of", 0}, {"type_of", 0}, {"fields", 0},
};

/* Is argument `index` of this call a type position — a place that takes a type
 * name rather than a value? Answered from where the function is declared: a
 * <?> parameter on a user function, an ARG_TYPE position in the stdlib table,
 * or the builtin table above. A type position must never reach value
 * resolution, which would report the type name as a value (E3100). */
static bool arg_is_type_position(TypeChecker *checker, AstNode *node,
                                 AstNode *callee_decl, int index) {
    if (callee_decl && index < callee_decl->data.func_decl.param_count &&
        callee_decl->data.func_decl.params[index].is_type_param)
        return true;
    AstNode *fn = node->data.call.function;
    if (!fn) return false;
    if (fn->kind == NODE_LABEL) {
        for (size_t i = 0; i < sizeof(builtin_type_arg) / sizeof(builtin_type_arg[0]); i++) {
            if (builtin_type_arg[i].index == index &&
                strcmp(fn->data.label.value, builtin_type_arg[i].name) == 0)
                return true;
        }
        /* A stdlib function reached bare through `using` is the same call as
         * the qualified spelling, so its type positions are the same ones. */
        for (int i = 0; i < checker->using_module_count; i++) {
            if (!using_module_accessible(checker, i)) continue;
            const StdlibFuncMeta *m = find_stdlib_meta(
                typechecker_resolve_alias(checker, checker->using_modules[i]),
                fn->data.label.value);
            if (!m) continue;
            for (int a = 0; a < m->arg_type_count; a++) {
                if (m->arg_types[a].index == index && m->arg_types[a].kind == ARG_TYPE)
                    return true;
            }
        }
        return false;
    }
    if (fn->kind == NODE_MEMBER_EXPR) {
        const char *qualifier = ast_member_base_qualifier(fn);
        if (!qualifier || !typechecker_is_stdlib_import(checker, qualifier)) return false;
        const StdlibFuncMeta *m = find_stdlib_meta(
            typechecker_resolve_alias(checker, qualifier), fn->data.member.member);
        if (!m) return false;
        for (int i = 0; i < m->arg_type_count; i++) {
            if (m->arg_types[i].index == index && m->arg_types[i].kind == ARG_TYPE)
                return true;
        }
    }
    return false;
}

static GrayType *resolve_call_expr(TypeChecker *checker, AstNode *node) {
    GrayType *result = &TYPE_UNKNOWN;
    normalize_qualified_enum_call(checker, node);
    /* Resolve argument types first. Skip the argument of ref()
     * when it's a bare function name; the ref() builtin handler
     * below resolves it specially, and the general resolve_expression
     * path would fire E3031 on the bare name ( follow-up). */
    bool is_ref_call = (node->data.call.function &&
        node->data.call.function->kind == NODE_LABEL &&
        strcmp(node->data.call.function->data.label.value, "ref") == 0);
    AstNode *callee_decl = callee_func_decl(checker, node);
    for (int i = 0; i < node->data.call.arg_count; i++) {
        /* A type argument is validated where the call is dispatched, not here:
         * resolve_generic_call() for a <?> parameter, the stdlib argument
         * check for an ARG_TYPE position, the builtin's own handler for the
         * rest. Resolving one here would report the type name as a value. */
        if (node->data.call.args[i]->kind == NODE_LABEL &&
            arg_is_type_position(checker, node, callee_decl, i))
            continue;
        if (is_ref_call && node->data.call.args[i]->kind == NODE_LABEL &&
            find_func(checker, node->data.call.args[i]->data.label.value)) {
            continue;
        }
        /* Skip implicit enum nodes; they need expected_type context
         * from the function signature, which is resolved later. */
        if (node->data.call.args[i]->kind == NODE_IMPLICIT_ENUM)
            continue;
        GrayType *ai_t = resolve_expression(checker, node->data.call.args[i]);

        /* E3040: a multi-return call cannot appear in single-value
         * argument position. Caller must destructure first. */
        reject_multi_return_in_single_position(checker, node->data.call.args[i]);

        /* E3019: an argument that crosses signedness vs the parameter needs a cast. */
        if (callee_decl && i < callee_decl->data.func_decl.param_count)
            check_signedness_crossing(checker,
                callee_decl->data.func_decl.params[i].type_name,
                node->data.call.args[i], ai_t, node->data.call.args[i]);
    }

    /* E3163: addr() of inner-scope variable passed alongside an outer-scope
     * argument — the classic escape pattern, e.g. arrays.append(outer, addr(x))
     * where x is loop/if/while-local.  We fire only when another argument in
     * the same call comes from an outer scope (scope_lookup_local returns null
     * for it) — that indicates the address is being stored in something that
     * outlives the inner variable's arena. `reported_arg` records which
     * argument positions this heuristic already flagged so the summary-driven
     * check below does not report the same store twice. */
    unsigned long long reported_arg = 0;
    for (int i = 0; i < node->data.call.arg_count && i < 64; i++) {
        AstNode *arg = node->data.call.args[i];
        if (arg->kind == NODE_CALL_EXPR &&
            arg->data.call.function &&
            arg->data.call.function->kind == NODE_LABEL &&
            (strcmp(arg->data.call.function->data.label.value, "addr") == 0 ||
             strcmp(arg->data.call.function->data.label.value, "raw") == 0) &&
            arg->data.call.arg_count == 1 &&
            arg->data.call.args[0]->kind == NODE_LABEL) {
            const char *addr_var = arg->data.call.args[0]->data.label.value;
            if (!scope_lookup_local(checker->current_scope, addr_var)) continue;
            /* Check if any other argument is an outer-scope variable */
            bool has_outer_arg = false;
            for (int j = 0; j < node->data.call.arg_count; j++) {
                if (j == i) continue;
                AstNode *other = node->data.call.args[j];
                if (other->kind == NODE_LABEL) {
                    const char *oname = other->data.label.value;
                    if (!scope_lookup_local(checker->current_scope, oname) &&
                        scope_lookup(checker->current_scope, oname)) {
                        has_outer_arg = true;
                        break;
                    }
                }
            }
            if (has_outer_arg) {
                diagnostic_error_code_formatted(checker->diag, "E3163",
                    NODE_FILE(checker, arg), arg->token.line, arg->token.column, 0,
                    addr_var, addr_var);
                reported_arg |= 1ull << i;
            }
        }
    }

    /* E3163: an inner-scope address that reaches longer-lived memory through
     * a call — stored into a container by a stdlib insert, or stashed into a
     * caller-visible parameter or global by a helper function (its
     * param_escape_into summary). Covers both the inline addr()/raw() form
     * and an address laundered through a variable first. The origin is
     * compared against the lifetime of wherever it lands. */
    {
        int argc = node->data.call.arg_count;
        /* stdlib container inserts (arrays.append/prepend/insert_at/fill) */
        const ContainerSink *sink = find_container_sink(checker, node);
        if (sink && argc > sink->value_arg && argc > sink->container_arg &&
            !((reported_arg >> sink->value_arg) & 1)) {
            const char *dest =
                assignment_target_root_name(node->data.call.args[sink->container_arg]);
            int dest_depth = dest
                ? symbol_scope_depth(checker->current_scope, dest) : 0;
            AstNode *arg = node->data.call.args[sink->value_arg];
            const char *onm = NULL;
            int od = expression_origin(checker, arg, &onm);
            if (od > 0 && od > dest_depth)
                diagnostic_error_code_formatted(checker->diag, "E3163",
                    NODE_FILE(checker, arg), arg->token.line,
                    arg->token.column, 0, dest ? dest : onm, onm);
        }
        /* user helper whose summary escapes one of its parameters. For
         * instance dispatch (s.stash(addr(y))) resolve_call_sig() finds
         * nothing here — dispatch hasn't rewritten the call yet, so the
         * object is still the instance label, not the struct's type name —
         * and apply_call_param_escape_and_mem_effects() is a no-op on a NULL
         * csig; resolve_struct_or_module_call() below calls it a second time
         * once that rewrite has happened. */
        apply_call_param_escape_and_mem_effects(checker,
            node, resolve_call_sig(checker, node), reported_arg);
    }

    /* Resolve function return type */
    AstNode *fn = node->data.call.function;
    const char *function_name = NULL;
    const char *chain_mod = NULL, *chain_type = NULL;

    /* For non-LABEL/non-MEMBER callees (e.g. arr[i]() on a [func] array)
     * the specific branches below won't traverse the function expression,
     * leaving its subtree untyped and breaking codegen paths that rely on
     * typetable lookups. Resolve it here so those subtrees get populated. */
    if (fn && fn->kind != NODE_LABEL && fn->kind != NODE_MEMBER_EXPR) {
        resolve_expression(checker, fn);
    }

    /* E5030: chained call on a function-call result — e.g. get_fn()(5).
     * A call result is never directly callable in Grayscale; func references
     * must be created with ()func_name or ref(func_name) first. */
    if (fn && fn->kind == NODE_CALL_EXPR) {
        AstNode *inner_fn = fn->data.call.function;
        const char *inner_name = NULL;
        char inner_buf[MSG_BUF_SIZE];
        const char *inner_qualifier = ast_member_qualifier(inner_fn);
        if (inner_fn && inner_fn->kind == NODE_LABEL) {
            inner_name = inner_fn->data.label.value;
        } else if (inner_qualifier) {
            snprintf(inner_buf, sizeof(inner_buf), "%s.%s",
                inner_qualifier, inner_fn->data.member.member);
            inner_name = inner_buf;
        }
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot call the return value of '%s' directly; func references must be created with '()func_name' or 'ref(func_name)' before calling",
            inner_name ? inner_name : "function");
        diagnostic_error_message(checker->diag, "E5030", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        result = &TYPE_UNKNOWN;
        return result;
    }

    /* [func] array + constant index + literal-of-func-refs origin:
     * recover the original referenced function's return type so it
     * survives the trip through void* storage (). */
    if (fn && fn->kind == NODE_INDEX_EXPR &&
        fn->data.index_expr.left->kind == NODE_LABEL &&
        fn->data.index_expr.index->kind == NODE_INT_VALUE) {
        const char *arr_name = fn->data.index_expr.left->data.label.value;
        Symbol *arr_sym = scope_lookup(checker->current_scope, arr_name);
        int64_t idx_v = fn->data.index_expr.index->data.int_value.value;
        if (arr_sym && arr_sym->func_array_refs &&
            idx_v >= 0 && idx_v < arr_sym->func_array_ref_count) {
            const char *ref_name = arr_sym->func_array_refs[idx_v];
            if (ref_name) {
                FuncSig *rsig = find_func(checker, ref_name);
                if (rsig && rsig->return_count > 0) {
                    result = rsig->return_types[0];
                    return result;
                }
            }
        }
    }

    /* Fallback for [func(...)->T] arrays: parse the return type from
     * the array's typed element type. Covers dynamically appended
     * refs where func_array_refs isn't populated (). */
    if (fn && fn->kind == NODE_INDEX_EXPR &&
        fn->data.index_expr.left->kind == NODE_LABEL) {
        const char *arr_name = fn->data.index_expr.left->data.label.value;
        Symbol *arr_sym = scope_lookup(checker->current_scope, arr_name);
        if (arr_sym && arr_sym->type && arr_sym->type->kind == TK_ARRAY &&
            arr_sym->type->element_type &&
            strncmp(arr_sym->type->element_type, "func(", 5) == 0) {
            GrayType *elem_t = type_from_name(arr_sym->type->element_type);
            if (elem_t && elem_t->func_sig &&
                elem_t->func_sig->return_count > 0 &&
                elem_t->func_sig->return_types[0]) {
                result = type_from_name(elem_t->func_sig->return_types[0]);
                return result;
            }
        }
    }

    /* Tagged enum construction via implicit selector: .Circle(3.14) */
    if (fn->kind == NODE_IMPLICIT_ENUM) {
        const char *vname = fn->data.implicit_enum.variant;
        /* Resolve enum from expected_type */
        GrayType *et = checker->expected_type;
        if (et && et->kind == TK_ENUM && et->name) {
            fn->data.implicit_enum.resolved_enum = et->name;
            int eidx = -1;
            for (int enum_index = 0; enum_index < checker->enum_count; enum_index++) {
                if (strcmp(checker->enum_names[enum_index], et->name) == 0) { eidx = enum_index; break; }
            }
            if (eidx >= 0) {
                int vidx = -1;
                for (int variant_index = 0; variant_index < checker->enum_value_counts[eidx]; variant_index++) {
                    if (strcmp(checker->enum_values[eidx][variant_index], vname) == 0) { vidx = variant_index; break; }
                }
                if (vidx < 0) {
                    diagnostic_error_code_formatted(checker->diag, "E3047", NODE_FILE(checker, node), node->token.line, node->token.column, 0, et->name, vname);
                } else if (checker->enum_is_tagged[eidx]) {
                    int expected_pc = checker->enum_payload_counts[eidx][vidx];
                    int provided_argument_count = node->data.call.arg_count;
                    if (expected_pc == 0 && provided_argument_count > 0) {
                        diagnostic_error_code_formatted(checker->diag, "E3114", NODE_FILE(checker, node), node->token.line, node->token.column, 0, vname, checker->enum_display_names[eidx]);
                    } else if (expected_pc != provided_argument_count) {
                        diagnostic_error_code_formatted(checker->diag, "E3113", NODE_FILE(checker, node), node->token.line, node->token.column, 0, vname, checker->enum_display_names[eidx], expected_pc, provided_argument_count);
                    } else {
                        for (int argument_index = 0; argument_index < provided_argument_count; argument_index++) {
                            GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
                            GrayType *exp_t = typechecker_type_from_name(checker, checker->enum_payload_types[eidx][vidx][argument_index]);
                            if (arg_t && exp_t &&
                                arg_t->kind != TK_UNKNOWN && exp_t->kind != TK_UNKNOWN &&
                                !types_assignable(checker, exp_t, arg_t)) {
                                diagnostic_error_code_formatted(checker->diag, "E5026", NODE_FILE(checker, node->data.call.args[argument_index]),
                                    node->data.call.args[argument_index]->token.line, node->data.call.args[argument_index]->token.column, 0);
                            }
                        }
                    }
                } else {
                    diagnostic_error_code_formatted(checker->diag, "E3115", NODE_FILE(checker, node), node->token.line, node->token.column, 0, checker->enum_display_names[eidx], vname);
                }
            }
            result = type_enum(et->name);
        } else {
            diagnostic_error_code_formatted(checker->diag, "E3110", NODE_FILE(checker, node), node->token.line, node->token.column, 0, vname, vname);
            result = &TYPE_UNKNOWN;
        }
        return result;
    }

    if (fn->kind == NODE_LABEL) {
        function_name = fn->data.label.value;
    } else if (ast_member_chain(fn, &chain_mod, &chain_type)) {
        /* mod.Struct.func() triple chain: geometry.Vec2.create() */
        const char *mod_name = chain_mod;
        const char *struct_name = chain_type;
        const char *func_name = fn->data.member.member;
        /* Mark module as used */
        mark_import_used(checker, mod_name);
        /* Look up mod_Struct_func */
        char prefixed[MSG_BUF_SIZE];
        snprintf(prefixed, sizeof(prefixed), "%s_%s_%s", mod_name, struct_name, func_name);
        FuncSig *sig = find_func(checker, prefixed);
        if (sig) {
            sig->used = true;
            warn_if_func_deprecated(checker, node, sig);
            /* E4017: private struct function called from outside the struct.
             * The bare and struct-namespaced spellings of this call check it;
             * the module-qualified triple chain did not, so `private` went
             * unenforced across the very boundary it exists to guard. */
            if (sig->is_private) {
                char struct_key[MSG_BUF_SIZE];
                snprintf(struct_key, sizeof(struct_key), "%s_%s", mod_name, struct_name);
                if (!(checker->current_struct_name &&
                      strcmp(checker->current_struct_name, struct_key) == 0)) {
                    diagnostic_error_code_formatted(checker->diag, "E4017",
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                        struct_name, func_name);
                }
            }
            result = sig->return_count > 0 ? sig->return_types[0] : &TYPE_VOID;
            /* Check argument count */
            if (node->data.call.arg_count != sig->param_count) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "function '%s.%s.%s' expects %d argument(s), got %d",
                    mod_name, struct_name, func_name,
                    sig->param_count, node->data.call.arg_count);
                tc_err_arity(checker, node, msg);
            }
            /* E3027: mutable param checks for triple-namespaced calls.
             * Struct functions live inside NODE_STRUCT_DECL, so scan
             * struct declarations in imported modules. */
            int check_count = node->data.call.arg_count < sig->param_count
                ? node->data.call.arg_count : sig->param_count;
            for (int argument_index = 0; argument_index < check_count; argument_index++) {
                /* Give an implicit enum selector (.VARIANT) the parameter's
                 * enum type as context, matching the bare and struct-namespaced
                 * call paths; the triple chain was the one spelling that left
                 * expected_type unset here. */
                GrayType *param_t = sig->param_types ? sig->param_types[argument_index] : NULL;
                GrayType *saved_expected_ms = checker->expected_type;
                if (param_t && param_t->kind == TK_ENUM && param_t->name)
                    checker->expected_type = param_t;
                resolve_expression(checker, node->data.call.args[argument_index]);
                checker->expected_type = saved_expected_ms;
                AstNode *arg = node->data.call.args[argument_index];
                AstNode *found_declaration = NULL;
                for (int field_index = 0; field_index < checker->program->data.program.stmt_count && !found_declaration; field_index++) {
                    AstNode *stmt = checker->program->data.program.stmts[field_index];
                    if (stmt->kind == NODE_STRUCT_DECL &&
                        strcmp(stmt->data.struct_decl.name, struct_name) == 0) {
                        for (int sfi = 0; sfi < stmt->data.struct_decl.func_count; sfi++) {
                            AstNode *sf = stmt->data.struct_decl.funcs[sfi].func_decl;
                            if (sf && sf->kind == NODE_FUNC_DECL &&
                                strcmp(sf->data.func_decl.name, func_name) == 0 &&
                                argument_index < sf->data.func_decl.param_count &&
                                sf->data.func_decl.params[argument_index].mutable) {
                                found_declaration = sf;
                                break;
                            }
                        }
                    }
                }
                if (found_declaration) {
                    char fn_display[MSG_BUF_SIZE];
                    snprintf(fn_display, sizeof(fn_display), "%s.%s.%s", mod_name, struct_name, func_name);
                    char param_desc[MSG_BUF_SIZE];
                    snprintf(param_desc, sizeof(param_desc), "mutable parameter '%s'",
                        found_declaration->data.func_decl.params[argument_index].name);
                    check_mutable_arg(checker, arg, param_desc, fn_display);
                }
            }
            /* Return the signature's type directly. Falling through let a
             * later stage of this function overwrite it, which is why an
             * inferred binding from mod.Struct.func() came out unknown while
             * the annotated spelling of the same call was fine. */
            return result;
        } else {
            result = &TYPE_VOID;
        }
    } else if (ast_member_qualifier(fn) &&
               is_enum_name(checker, resolve_type_alias(checker, ast_member_qualifier(fn)))) {
        /* Tagged enum construction: Shape.Circle(3.14) */
        const char *ename = resolve_type_alias(checker, ast_member_qualifier(fn));
        fn->data.member.object->data.label.value = ename;
        const char *vname = fn->data.member.member;
        int eidx = -1;
        for (int enum_index = 0; enum_index < checker->enum_count; enum_index++) {
            if (strcmp(checker->enum_names[enum_index], ename) == 0) { eidx = enum_index; break; }
        }
        if (eidx >= 0) {
            /* Find variant index */
            int vidx = -1;
            for (int variant_index = 0; variant_index < checker->enum_value_counts[eidx]; variant_index++) {
                if (strcmp(checker->enum_values[eidx][variant_index], vname) == 0) { vidx = variant_index; break; }
            }
            if (vidx < 0) {
                diagnostic_error_code_formatted(checker->diag, "E3047", NODE_FILE(checker, node), node->token.line, node->token.column, 0, ename, vname);
                result = &TYPE_UNKNOWN;
                return result;
            }
            if (!checker->enum_is_tagged[eidx]) {
                /* E3115: plain enum, can't call */
                const char *dname = checker->enum_display_names[eidx];
                diagnostic_error_code_formatted(checker->diag, "E3115", NODE_FILE(checker, node), node->token.line, node->token.column, 0, dname, vname);
                result = type_enum(checker_resolve_enum_key(checker, ename));
                return result;
            }
            int expected_pc = checker->enum_payload_counts[eidx][vidx];
            int provided_argument_count = node->data.call.arg_count;
            if (expected_pc == 0 && provided_argument_count > 0) {
                diagnostic_error_code_formatted(checker->diag, "E3114", NODE_FILE(checker, node), node->token.line, node->token.column, 0, vname, checker->enum_display_names[eidx]);
            } else if (expected_pc != provided_argument_count) {
                diagnostic_error_code_formatted(checker->diag, "E3113", NODE_FILE(checker, node), node->token.line, node->token.column, 0, vname, checker->enum_display_names[eidx], expected_pc, provided_argument_count);
            } else {
                /* Validate each arg type against payload type */
                for (int argument_index = 0; argument_index < provided_argument_count; argument_index++) {
                    GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
                    GrayType *expected_t = typechecker_type_from_name(checker, checker->enum_payload_types[eidx][vidx][argument_index]);
                    if (arg_t && expected_t &&
                        arg_t->kind != TK_UNKNOWN && expected_t->kind != TK_UNKNOWN &&
                        !types_assignable(checker, expected_t, arg_t)) {
                        diagnostic_error_code_formatted(checker->diag, "E5026", NODE_FILE(checker, node->data.call.args[argument_index]),
                            node->data.call.args[argument_index]->token.line, node->data.call.args[argument_index]->token.column, 0);
                    }
                }
            }
            result = type_enum(checker_resolve_enum_key(checker, ename));
        } else {
            result = &TYPE_UNKNOWN;
        }
    } else if (ast_member_base_qualifier(fn)) {
        const char *mod_raw = ast_member_base_qualifier(fn);
        const char *mod = typechecker_resolve_alias(checker, mod_raw);
        /* A struct alias names the struct, so Vec2.f() is Point.f(). Only the
         * import alias was resolved here, so a type alias reached the dispatch
         * below as an unknown module and got E6010. Rewriting the qualifier is
         * what the enum-alias branch above does, and it also hands codegen the
         * struct's own name to mangle the call against. A variable of the same
         * name wins: that spelling is instance dispatch, not a static call. */
        if (!is_struct_name(checker, mod) &&
            !scope_lookup(checker->current_scope, mod_raw) &&
            fn->data.member.object &&
            fn->data.member.object->kind == NODE_LABEL) {
            const char *aliased = resolve_type_alias(checker,
                checker_resolve_type_name(checker, mod));
            if (strcmp(aliased, mod) != 0 && is_struct_name(checker, aliased)) {
                mod = aliased;
                fn->data.member.object->data.label.value = aliased;
            }
        }
        const char *mfn = fn->data.member.member;
        /* Check that module is actually imported */
        bool mod_imported = mark_import_used(checker, mod_raw) ||
                            mark_import_used(checker, mod);
        if (!mod_imported && is_stdlib_module_name(mod)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "module '%s' is not imported; add 'import @%s' at the top of the file",
                mod, mod);
            diagnostic_error_message(checker->diag, "E4001", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* extern.func() without extern import "..."; but only if "extern" isn't a
         * local variable. A variable named `extern` with a struct type
         * should fall through to the struct function dispatch, not
         * be treated as the C interop module (). */
        if (!mod_imported && strcmp(mod, "extern") == 0 &&
            !scope_lookup(checker->current_scope, "extern")) {
            diagnostic_error_message(checker->diag, "E4001",
                "C interop requires a C header import; add extern import \"header.h\" at the top of the file",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            result = &TYPE_UNKNOWN;
            return result;
        }
        /* C interop: extern.func(); skip type checking but reject bigints */
        if (strcmp(mod, "extern") == 0 && mod_imported) {
            /* Mark all C imports as used */
            mark_import_used(checker, "extern");
            /* Validate arguments; reject types that don't translate to C */
            for (int argument_index = 0; argument_index < node->data.call.arg_count; argument_index++) {
                /* E3154: a stack address cannot cross into C. The direct
                 * addr()/ref()/raw() form is rejected when its target is a
                 * local, a parameter, or bound in a nested block of the
                 * current function; the compiler cannot see the C signature
                 * and cannot prove the C side does not retain the pointer.
                 * new() (heap arena) and file-scope variables outlive the
                 * call and are allowed. */
                AstNode *ca = node->data.call.args[argument_index];
                if (checker->current_func_scope_depth > 0 &&
                    ca && ca->kind == NODE_CALL_EXPR &&
                    ca->data.call.function->kind == NODE_LABEL &&
                    ca->data.call.arg_count == 1 &&
                    (strcmp(ca->data.call.function->data.label.value, "addr") == 0 ||
                     strcmp(ca->data.call.function->data.label.value, "ref") == 0 ||
                     strcmp(ca->data.call.function->data.label.value, "raw") == 0)) {
                    const char *root = assignment_target_root_name(ca->data.call.args[0]);
                    int d = root
                        ? symbol_scope_depth(checker->current_scope, root) : 0;
                    if (d > 0 && d >= checker->current_func_scope_depth) {
                        diagnostic_error_code_formatted(checker->diag, "E3154",
                            NODE_FILE(checker, ca), ca->token.line, ca->token.column, 0,
                            root);
                    }
                }
                GrayType *arg_t = resolve_expression(checker, node->data.call.args[argument_index]);
                if (!arg_t || arg_t->kind == TK_UNKNOWN) continue;
                /* Reject bigint types */
                if (arg_t->name &&
                    (strcmp(arg_t->name, "i128") == 0 || strcmp(arg_t->name, "i256") == 0 ||
                     strcmp(arg_t->name, "u128") == 0 || strcmp(arg_t->name, "u256") == 0)) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "cannot pass %s to a C function; C has no 128/256-bit integer types",
                        arg_t->name);
                    diagnostic_error_message(checker->diag, "E3158", msg,
                        NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0);
                }
                /* Reject Grayscale-specific composite types */
                if (arg_t->kind == TK_ARRAY || arg_t->kind == TK_MAP) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "cannot pass %s to a C function; use individual elements instead",
                        arg_t->kind == TK_ARRAY ? "an array" : "a map");
                    diagnostic_error_message(checker->diag, "E3158", msg,
                        NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0);
                }
                /* Reject Grayscale structs (registered in typechecker) */
                if (arg_t->kind == TK_STRUCT && arg_t->name &&
                    is_struct_name(checker, arg_t->name)) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "cannot pass struct '%s' to a C function; pass individual fields instead",
                        type_display_name(checker, arg_t));
                    diagnostic_error_message(checker->diag, "E3158", msg,
                        NODE_FILE(checker, node->data.call.args[argument_index]), node->data.call.args[argument_index]->token.line,
                        node->data.call.args[argument_index]->token.column, 0);
                }
            }
            result = &TYPE_UNKNOWN;
            return result;
        }
        /* Skip stdlib dispatch if this module is a user import, not stdlib.
         * User modules with the same name (e.g., import "./server.gray") must
         * fall through to the user-module handler below. */
        if (typechecker_is_stdlib_import(checker, mod_raw)) {
            result = resolve_stdlib_call(checker, node, mod, mfn);
        } else {
            result = resolve_struct_or_module_call(checker, node, mod, mfn, mod_raw, fn);
        }
        return result;
    }

    /* Member call on expression result: foo().bar() */
    if (fn->kind == NODE_MEMBER_EXPR && !ast_member_qualifier(fn)) {
        GrayType *obj_t = resolve_expression(checker, fn->data.member.object);
        if (obj_t && obj_t->kind != TK_STRUCT && obj_t->kind != TK_POINTER &&
            obj_t->kind != TK_UNKNOWN && obj_t->kind != TK_VOID) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type '%s' does not support function calls via dot notation",
                type_name(obj_t));
            diagnostic_error_message(checker->diag, "E3013", msg,
                NODE_FILE(checker, fn), fn->token.line, fn->token.column, 0);
        } else if (obj_t && (obj_t->kind == TK_STRUCT || obj_t->kind == TK_POINTER)) {
            /* E3075: chaining struct function calls (calling one struct
             * function on the result of another) isn't supported.
             * Assigning the intermediate result to a variable keeps
             * each call site readable and avoids the AST-rewrite
             * gymnastics that fluent-interface chaining would require. */
            diagnostic_error_code_help(checker->diag, "E3075",
                NODE_FILE(checker, fn), fn->token.line, fn->token.column, 0,
                "assign the intermediate result to a variable, then call the next struct function on it");
        }
        result = &TYPE_UNKNOWN;
        return result;
    }

    if (function_name) {
        result = resolve_builtin_call(checker, node, function_name);
        if (!result) {
            result = resolve_direct_call(checker, node, function_name);
        }
    }
    return result;
}

static GrayType *resolve_infix_expr(TypeChecker *checker, AstNode *node) {
    GrayType *result = &TYPE_UNKNOWN;
    TokenType op = node->data.infix.op;
    GrayType *left = resolve_expression(checker, node->data.infix.left);
    /* For == and !=, set expected_type so .VARIANT on the RHS
     * can resolve against the LHS enum type. */
    GrayType *saved_infix_expected = checker->expected_type;
    if ((op == TOK_EQ || op == TOK_NOT_EQ) &&
        left && left->kind == TK_ENUM && left->name)
        checker->expected_type = left;
    GrayType *right = resolve_expression(checker, node->data.infix.right);
    checker->expected_type = saved_infix_expected;

    /* : track whether any op-specific check has rejected the
     * expression so the final result can be collapsed to TK_UNKNOWN
     * instead of one operand's type. Otherwise `mut x int = true +
     * 1` fires E3002 at the '+' and then cascades into E3001 "can't
     * assign bool to int" at the var_decl, where the bool came from
     * the left operand rather than a real result type. */
    bool infix_errored = false;

    /* : void operands never make sense in any binary
     * operator. Check both sides at the kind level before any
     * op-specific diagnostic runs, so `1 + nothing()` and
     * `nothing() == x` report a clean E3038 instead of
     * cascading through the op-specific type checks or leaking
     * to clang. */
    if (left && left->kind == TK_VOID) infix_errored = true;
    if (right && right->kind == TK_VOID) infix_errored = true;
    reject_void_in_context(checker, node->data.infix.left, left, "binary operand");
    reject_void_in_context(checker, node->data.infix.right, right, "binary operand");

    /* E3040: multi-return calls cannot appear as operands */
    reject_multi_return_in_single_position(checker, node->data.infix.left);
    reject_multi_return_in_single_position(checker, node->data.infix.right);

    /* String ordering operators not supported; use strings.compare() */
    if ((left->kind == TK_STRING || right->kind == TK_STRING) &&
        (op == TOK_LT || op == TOK_GT ||
         op == TOK_LT_EQ || op == TOK_GT_EQ)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot use '%s' on strings; use strings.compare() instead", operator_display_name(op));
        diagnostic_error_message(checker->diag, "E3002", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        result = &TYPE_BOOL;
        return result;
    }

    /* E3002: modulo on float */
    if (op == TOK_PERCENT &&
        (left->kind == TK_FLOAT || right->kind == TK_FLOAT)) {
        diagnostic_error_message(checker->diag, "E3002",
            "modulo (%) only works on integers, not floats",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3002: literal divide/modulo by zero (). Catches the
     * statically-detectable case where the RHS is an integer or
     * float literal zero (including a prefix -0). Runtime checks
     * still cover the dynamic case. */
    if (op == TOK_SLASH || op == TOK_PERCENT) {
        AstNode *r = node->data.infix.right;
        bool is_zero = false;
        int64_t iv;
        bool r_is_int_literal = try_get_literal_int(r, &iv);
        if (r_is_int_literal && iv == 0) {
            is_zero = true;
        } else if (r && r->kind == NODE_FLOAT_VALUE &&
                   r->data.float_value.value == 0.0) {
            is_zero = true;
        } else if (r && r->kind == NODE_PREFIX_EXPR &&
                   r->data.prefix.op == TOK_MINUS &&
                   r->data.prefix.right &&
                   r->data.prefix.right->kind == NODE_FLOAT_VALUE &&
                   r->data.prefix.right->data.float_value.value == 0.0) {
            is_zero = true;
        }
        if (is_zero) {
            char *msg;
            msg = typechecker_format(checker,
                "%s by zero; dividing by a literal zero is always invalid",
                op == TOK_PERCENT ? "modulo" : "division");
            diagnostic_error_message(checker->diag, "E3002", msg,
                NODE_FILE(checker, r), r->token.line, r->token.column, 0);
            infix_errored = true;
        } else if (op == TOK_SLASH) {
            /* E3137: INT64_MIN / -1 is the one division C leaves undefined
             * at the int64 boundary — it traps (SIGFPE) on x86-64. Check
             * both literal operands directly rather than relying on
             * try_get_literal_int() to fold the whole division, since that
             * folder now refuses (by design) to perform this division. */
            int64_t lv;
            if (r_is_int_literal && iv == -1 &&
                try_get_literal_int(node->data.infix.left, &lv) && lv == INT64_MIN) {
                diagnostic_error_code_formatted(checker->diag, "E3137",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    (long long)lv, (long long)iv, type_display_name(checker, left));
                infix_errored = true;
            }
        }
    }

    /* E3002: bool used in arithmetic (e.g., 1 + true) */
    if ((left->kind == TK_BOOL || right->kind == TK_BOOL) &&
        (op == TOK_PLUS || op == TOK_MINUS ||
         op == TOK_ASTERISK || op == TOK_SLASH ||
         op == TOK_PERCENT) &&
        left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "invalid operands: cannot use '%s' with %s and %s",
            operator_display_name(op), type_name(left), type_name(right));
        diagnostic_error_message(checker->diag, "E3002", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3002: incompatible bigint operands. Bigint arithmetic and comparison
     * functions (gray_i128_add_checked, gray_i128_eq, etc.) only accept their
     * own struct type. The only cross-type bigint combinations that codegen
     * can handle are i256+i128 and u256+u128 (the narrower type is widened).
     * Everything else — mixed signedness or the wrong direction — leaks a
     * C type error and must be caught here. */
    if (!infix_errored &&
        (op == TOK_PLUS || op == TOK_MINUS ||
         op == TOK_ASTERISK || op == TOK_SLASH || op == TOK_PERCENT ||
         op == TOK_EQ || op == TOK_NOT_EQ ||
         op == TOK_LT || op == TOK_GT ||
         op == TOK_LT_EQ || op == TOK_GT_EQ) &&
        left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN &&
        left->name && right->name &&
        is_bigint_type(left->name) && is_bigint_type(right->name) &&
        strcmp(left->name, right->name) != 0 &&
        !(strcmp(left->name, "i256") == 0 && strcmp(right->name, "i128") == 0) &&
        !(strcmp(left->name, "u256") == 0 && strcmp(right->name, "u128") == 0)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "invalid operands: cannot use '%s' with %s and %s; bigint types must match",
            operator_display_name(op), type_name(left), type_name(right));
        diagnostic_error_message(checker->diag, "E3002", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3002 (): nil in any operator other than equality. `nil
     * == x` and `nil != x` are valid against nullable types (the
     * existing comparison path already validates that); every
     * other operator on nil is nonsense. Catches both
     * `println(nil + 1)` (which leaked straight to clang) and
     * `mut x int = nil + 1` (which was caught by the downstream
     * nil-assignment check with a confusing message). */
    if ((left->kind == TK_NIL || right->kind == TK_NIL) &&
        op != TOK_EQ && op != TOK_NOT_EQ) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot use nil with operator '%s'; nil is only valid for == / != against nullable types (Error, pointers)",
            operator_display_name(op));
        diagnostic_error_message(checker->diag, "E3002", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3092: nil compared to a non-nullable type */
    if (!infix_errored && (op == TOK_EQ || op == TOK_NOT_EQ)) {
        GrayType *non_nil = NULL;
        if (left->kind == TK_NIL && right->kind != TK_UNKNOWN && right->kind != TK_NIL &&
            right->kind != TK_POINTER && right->kind != TK_ERROR)
            non_nil = right;
        else if (right->kind == TK_NIL && left->kind != TK_UNKNOWN && left->kind != TK_NIL &&
            left->kind != TK_POINTER && left->kind != TK_ERROR)
            non_nil = left;
        if (non_nil) {
            diagnostic_error_code_formatted(checker->diag, "E3092", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                type_display_name(checker, non_nil));
            infix_errored = true;
        }
    }

    /* String concatenation with '+': both operands must be strings.
     * string + string yields a new string; a string mixed with any
     * other type (E3048) is rejected so codegen never has to guess. */
    if (op == TOK_PLUS &&
        (left->kind == TK_STRING || right->kind == TK_STRING) &&
        left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN &&
        (left->kind != TK_STRING || right->kind != TK_STRING)) {
        diagnostic_error_code_formatted(checker->diag, "E3048",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            type_display_name(checker, left), type_display_name(checker, right));
        infix_errored = true;
    }

    /* Arithmetic on strings (-, *, /, %, etc.) */
    if ((left->kind == TK_STRING || right->kind == TK_STRING) &&
        op != TOK_PLUS && op != TOK_EQ && op != TOK_NOT_EQ &&
        op != TOK_IN && op != TOK_NOT_IN) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot use '%s' on string type", operator_display_name(op));
        diagnostic_error_message(checker->diag, "E3002", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3093: arithmetic on map, array, or struct */
    if (!infix_errored &&
        (op == TOK_PLUS || op == TOK_MINUS ||
         op == TOK_ASTERISK || op == TOK_SLASH || op == TOK_PERCENT)) {
        GrayType *bad = NULL;
        if (left->kind == TK_MAP || left->kind == TK_ARRAY || left->kind == TK_STRUCT)
            bad = left;
        else if (right->kind == TK_MAP || right->kind == TK_ARRAY || right->kind == TK_STRUCT)
            bad = right;
        if (bad && bad->kind != TK_UNKNOWN) {
            diagnostic_error_code_formatted(checker->diag, "E3093", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                operator_display_name(op), type_display_name(checker, bad));
            infix_errored = true;
        }
    }

    /* E3078: pointer arithmetic is not supported. The spec disallows
     * it (no contiguous-buffer guarantee on '^T'), and without this
     * check 'p + 1' silently emitted C pointer math and produced
     * garbage. Equality (== / !=) against nil or another pointer is
     * still allowed. */
    if ((left->kind == TK_POINTER || right->kind == TK_POINTER) &&
        (op == TOK_PLUS || op == TOK_MINUS ||
         op == TOK_ASTERISK || op == TOK_SLASH || op == TOK_PERCENT)) {
        diagnostic_error_code(checker->diag, "E3078",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3120: pointer ordering comparisons are not supported. STANDARD.md
     * §5.2.2 allows only == and != on pointers; <, >, <=, >= silently
     * fell through to C where cross-allocation ordering is undefined. */
    if (!infix_errored &&
        (left->kind == TK_POINTER || right->kind == TK_POINTER) &&
        (op == TOK_LT || op == TOK_GT ||
         op == TOK_LT_EQ || op == TOK_GT_EQ)) {
        diagnostic_error_code(checker->diag, "E3120",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        infix_errored = true;
    }

    /* E3032: different enum types in comparison — catches both
     * direct enum literals (Color.RED == Dir.NORTH) and variables
     * of different enum types (c == d where c:Color, d:Dir).
     * Use display-name comparison so cross-module aliases unify. */
    if (!infix_errored &&
        (op == TOK_EQ || op == TOK_NOT_EQ) &&
        left->kind == TK_ENUM && right->kind == TK_ENUM &&
        left->name && right->name &&
        !typechecker_same_enum_type(checker, left->name, right->name)) {
        diagnostic_error_code_formatted(checker->diag, "E3032", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            type_display_name(checker, left), type_display_name(checker, right));
        infix_errored = true;
    }

    /* E3124: == / != on tagged enums. Tagged enums are emitted as C
     * structs (union + tag field) and cannot be compared with ==.
     * Reject at the Grayscale level before C is ever invoked. */
    if (!infix_errored &&
        (op == TOK_EQ || op == TOK_NOT_EQ) &&
        left->kind == TK_ENUM && right->kind == TK_ENUM &&
        left->name) {
        int eidx = -1;
        for (int enum_index = 0; enum_index < checker->enum_count; enum_index++)
            if (strcmp(checker->enum_names[enum_index], left->name) == 0) { eidx = enum_index; break; }
        if (eidx >= 0 && checker->enum_is_tagged[eidx]) {
            diagnostic_error_code_formatted(checker->diag, "E3124",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                operator_display_name(op), enum_display_name(checker, left->name));
            infix_errored = true;
        }
    }

    /* E3049: arithmetic and ordering on enum values — catch both
     * direct enum literals (Color.RED + 1) and variables of enum
     * type (c + 1).  Enums only support == and != comparison.
     * Exception: ordering comparisons (< > <= >=) are allowed when
     * one side is an integer variable (user has explicitly unboxed
     * the enum value into an int for numeric comparison). */
    if (!infix_errored &&
        (op == TOK_PLUS || op == TOK_MINUS ||
         op == TOK_ASTERISK || op == TOK_SLASH || op == TOK_PERCENT ||
         op == TOK_LT || op == TOK_GT ||
         op == TOK_LT_EQ || op == TOK_GT_EQ)) {
        bool left_is_enum = (left && left->kind == TK_ENUM);
        bool right_is_enum = (right && right->kind == TK_ENUM);
        bool is_ordering = (op == TOK_LT || op == TOK_GT ||
                            op == TOK_LT_EQ || op == TOK_GT_EQ);
        bool has_int_side = (left && left->kind == TK_INT) ||
                            (right && right->kind == TK_INT);
        if ((left_is_enum || right_is_enum) &&
            !(is_ordering && has_int_side)) {
            diagnostic_error_code_formatted(checker->diag, "E3049", NODE_FILE(checker, node), node->token.line, node->token.column, 0, operator_display_name(op));
            infix_errored = true;
        }
    }

    /* Enum vs integer comparison: reject with a specific diagnostic. */
    if ((op == TOK_EQ || op == TOK_NOT_EQ) &&
        left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN) {
        if (left->kind == TK_ENUM && is_int_kind(right->kind)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot compare enum '%s' with %s; use an enum variant like '%s.VARIANT'",
                type_display_name(checker, left), type_name(right), type_display_name(checker, left));
            diagnostic_error_message(checker->diag, "E3117", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        if (is_int_kind(left->kind) && right->kind == TK_ENUM) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot compare %s with enum '%s'; use an enum variant like '%s.VARIANT'",
                type_name(left), type_display_name(checker, right), type_display_name(checker, right));
            diagnostic_error_message(checker->diag, "E3117", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* Comparison of incompatible types (e.g., int == string) */
    if ((op == TOK_EQ || op == TOK_NOT_EQ) &&
        left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN &&
        left->kind != right->kind && left->kind != TK_NIL && right->kind != TK_NIL &&
        !(is_int_kind(left->kind) && is_int_kind(right->kind)) &&
        !(left->kind == TK_STRUCT && is_int_kind(right->kind)) &&
        !(is_int_kind(left->kind) && right->kind == TK_STRUCT) &&
        !(is_int_kind(left->kind) && right->kind == TK_BOOL) &&
        !(left->kind == TK_BOOL && is_int_kind(right->kind)) &&
        !(left->kind == TK_ENUM && is_int_kind(right->kind)) &&
        !(is_int_kind(left->kind) && right->kind == TK_ENUM) &&
        /* String enums can be compared with string literals */
        !(left->kind == TK_ENUM && right->kind == TK_STRING && typechecker_enum_is_string(checker, left->name)) &&
        !(left->kind == TK_STRING && right->kind == TK_ENUM && typechecker_enum_is_string(checker, right->name))) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot compare %s with %s", type_name(left), type_name(right));
        diagnostic_error_message(checker->diag, "E3156", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }

    /* Pointer-to-pointer: pointee types differ in == / != comparison */
    if ((op == TOK_EQ || op == TOK_NOT_EQ) &&
        left->kind == TK_POINTER && right->kind == TK_POINTER &&
        left->name && right->name &&
        strcmp(left->name, right->name) != 0 &&
        strcmp(left->name, "nil") != 0 && strcmp(right->name, "nil") != 0) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot compare %s with %s", type_name(left), type_name(right));
        diagnostic_error_message(checker->diag, "E3156", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E3074: arrays cannot be compared with == / != directly. The C
     * backend has no structural-equality operator on aggregate types,
     * so this used to slip through to clang. Point users at
     * arrays.is_equal. */
    if ((op == TOK_EQ || op == TOK_NOT_EQ ||
         op == TOK_LT || op == TOK_LT_EQ ||
         op == TOK_GT || op == TOK_GT_EQ) &&
        left->kind == TK_ARRAY && right->kind == TK_ARRAY) {
        diagnostic_error_code_help(checker->diag, "E3074",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            "use 'arrays.is_equal(a, b)' to compare arrays element-by-element");
        infix_errored = true;
    }
    if ((op == TOK_EQ || op == TOK_NOT_EQ ||
         op == TOK_LT || op == TOK_LT_EQ ||
         op == TOK_GT || op == TOK_GT_EQ) &&
        left->kind == TK_MAP && right->kind == TK_MAP) {
        diagnostic_error_code_help(checker->diag, "E3076",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            "use 'maps.is_equal(a, b)' to compare maps for equality");
        infix_errored = true;
    }
    if ((op == TOK_EQ || op == TOK_NOT_EQ ||
         op == TOK_LT || op == TOK_LT_EQ ||
         op == TOK_GT || op == TOK_GT_EQ) &&
        left->kind == TK_STRUCT && right->kind == TK_STRUCT) {
        diagnostic_error_code_help(checker->diag, "E3077",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            "compare individual fields instead, e.g. 'a.x == b.x'");
        infix_errored = true;
    }

    /* E3085: validate type compatibility for in/not_in/!in */
    if ((op == TOK_IN || op == TOK_NOT_IN) &&
        !infix_errored && left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN) {
        bool mismatch = false;
        const char *left_tn = type_name(left);
        const char *right_tn = type_name(right);
        if (right->kind == TK_ARRAY && right->element_type) {
            GrayType *elem = type_from_name(right->element_type);
            if (elem->kind != TK_UNKNOWN &&
                !types_assignable(checker, elem, left) &&
                !(left->name && strcmp(left->name, right->element_type) == 0)) {
                mismatch = true;
            }
        } else if (right->kind == TK_MAP && right->key_type) {
            GrayType *key = type_from_name(right->key_type);
            if (key->kind != TK_UNKNOWN &&
                !types_assignable(checker, key, left) &&
                !(left->name && strcmp(left->name, right->key_type) == 0)) {
                mismatch = true;
            }
        } else if (right->kind == TK_STRING) {
            /* char in string and string in string are valid */
            if (left->kind != TK_CHAR && left->kind != TK_STRING) {
                mismatch = true;
            }
        } else if (right->name && strcmp(right->name, "Range<int>") == 0) {
            /* range() produces Range<int>; only integer types can be checked */
            if (!is_int_kind(left->kind)) {
                mismatch = true;
            }
        } else {
            /* RHS is not a valid target for 'in' */
            diagnostic_error_code_formatted(checker->diag, "E3095", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                right_tn);
            infix_errored = true;
        }
        if (mismatch) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'in' operator type mismatch: cannot check if '%s' is in '%s'",
                left_tn, right_tn);
            diagnostic_error_message(checker->diag, "E3085", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            infix_errored = true;
        }
    }

    /* E3089: binary bitwise operators require integer operands */
    if ((op == TOK_BIT_AND || op == TOK_BIT_OR ||
         op == TOK_BIT_XOR || op == TOK_BIT_SHIFT_LEFT ||
         op == TOK_BIT_SHIFT_RIGHT) &&
        !infix_errored && left->kind != TK_UNKNOWN && right->kind != TK_UNKNOWN) {
        bool left_ok  = is_int_kind(left->kind)  || left->kind  == TK_CHAR
            || (left->kind == TK_ENUM && left->name && typechecker_enum_is_flags(checker, left->name));
        bool right_ok = is_int_kind(right->kind) || right->kind == TK_CHAR
            || (right->kind == TK_ENUM && right->name && typechecker_enum_is_flags(checker, right->name));
        if (!left_ok || !right_ok) {
            diagnostic_error_code_formatted(checker->diag, "E8001",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                operator_display_name(op), type_display_name(checker, left), type_display_name(checker, right));
            infix_errored = true;
        } else {
            result = left;
        }
    }

    if (op == TOK_EQ || op == TOK_NOT_EQ ||
        op == TOK_LT || op == TOK_GT ||
        op == TOK_LT_EQ || op == TOK_GT_EQ ||
        op == TOK_AND || op == TOK_OR ||
        op == TOK_IN || op == TOK_NOT_IN) {
        result = &TYPE_BOOL;
    } else if (left->kind == TK_FLOAT || right->kind == TK_FLOAT) {
        result = &TYPE_FLOAT;
    } else if (left->kind == TK_STRING && right->kind == TK_STRING && op == TOK_PLUS) {
        result = &TYPE_STRING;
    } else if (left->kind == TK_ENUM || right->kind == TK_ENUM) {
        /* #flags enum bitwise ops produce int (combined values
         * don't correspond to a single variant). */
        result = &TYPE_INT;
    } else {
        result = left;
    }
    /* : if any op-level check rejected the expression, drop
     * result to TK_UNKNOWN so downstream var_decl / return / etc.
     * checks that already skip TK_UNKNOWN don't cascade a second
     * diagnostic off one of the (invalid) operand types. */
    if (infix_errored) result = &TYPE_UNKNOWN;
    return result;
}

/* Field of the builtin Error type: .msg / .message -> string, .code ->
 * ErrorCode. Any other name is E3010. Shared by every syntactic position
 * that can yield an Error (bare var, array/map index, call result, field
 * chain) so they resolve identically. */
static GrayType *resolve_error_field(TypeChecker *checker, AstNode *node,
                                     const char *member) {
    if (strcmp(member, "msg") == 0 || strcmp(member, "message") == 0)
        return &TYPE_STRING;
    if (strcmp(member, "code") == 0)
        return type_from_name("ErrorCode");
    diagnostic_error_message(checker->diag, "E3010",
        typechecker_format(checker,
            "Error has no field '%s'; available fields: msg, code", member),
        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    return &TYPE_UNKNOWN;
}

static GrayType *resolve_member_expr(TypeChecker *checker, AstNode *node) {
    GrayType *result = &TYPE_UNKNOWN;
    /* Resolve object type first (sets type table entry for the object) */
    AstNode *obj = node->data.member.object;
    const char *member = node->data.member.member;

    /* Handle mod.Enum.VALUE or mod.Struct.field triple chain */
    const char *mod_name = NULL, *chain_type = NULL;
    if (ast_member_chain(node, &mod_name, &chain_type)) {
        char prefixed_type[MSG_BUF_SIZE];
        module_member_key(checker, mod_name, chain_type,
                          prefixed_type, sizeof(prefixed_type));
        /* Check if it's a module-qualified enum access */
        if (is_enum_name(checker, prefixed_type)) {
            /* Validate variant exists */
            bool member_found = false;
            for (int enum_index = find_enum_index(checker, prefixed_type);
                 enum_index >= 0; enum_index = -1) {
                {
                    for (int variant_index = 0; variant_index < checker->enum_value_counts[enum_index]; variant_index++) {
                        if (strcmp(checker->enum_values[enum_index][variant_index], member) == 0) {
                            member_found = true;
                            break;
                        }
                    }
                    break;
                }
            }
            if (!member_found) {
                diagnostic_error_code_formatted(checker->diag, "E3047", NODE_FILE(checker, node), node->token.line, node->token.column, 0, prefixed_type, member);
            }
            /* Mark module as used */
            mark_import_used(checker, mod_name);
            result = type_enum(prefixed_type);
            return result;
        }
    }

    /* A label naming a type is the qualifier of an enum variant, a struct
     * function, or a module member — all handled below. Resolving it as a
     * value first would report the qualifier as a type name used as one. */
    bool obj_is_type_name = obj->kind == NODE_LABEL &&
        !scope_lookup(checker->current_scope, obj->data.label.value) &&
        type_name_as_value(checker, obj->data.label.value);
    if (!obj_is_type_name) resolve_expression(checker, obj);

    if (obj->kind == NODE_LABEL) {
        const char *obj_name = obj->data.label.value;

        /* Mark module as used (for member access like math.PI) */
        mark_import_used(checker, obj_name);

        /* Stdlib module constant: math.PI, io.O_RDONLY, uuid.NIL_UUID, ...
         * One lookup, from the table that describes them. This used to be a
         * chain of per-module string compares in which `math.<anything>`
         * typed as float, so a misspelled constant became a float rather
         * than an error. */
        {
            ResolveScope cscope = checker_scope(checker);
            DeclEntry *centry = module_resolve_qualified(checker->modules, &cscope,
                obj_name, node->data.member.member, NULL);
            if (centry && centry->external && centry->kind == DECL_CONST &&
                centry->registry_index >= 0) {
                result = stdlib_const_type(centry->registry_index);
                return result;
            }
        }

        /* Check if it's an enum access: Color.RED (also via type alias) */
        const char *resolved_obj = resolve_type_alias(checker, obj_name);
        if (is_enum_name(checker, resolved_obj)) {
            /* Rewrite the label to the resolved enum name for codegen */
            obj->data.label.value = resolved_obj;
            warn_if_enum_deprecated(checker, node, find_enum_index(checker, resolved_obj));
            /* Validate member exists */
            bool member_found = false;
            for (int enum_index = find_enum_index(checker, resolved_obj);
                 enum_index >= 0; enum_index = -1) {
                {
                    for (int variant_index = 0; variant_index < checker->enum_value_counts[enum_index]; variant_index++) {
                        if (strcmp(checker->enum_values[enum_index][variant_index], member) == 0) {
                            member_found = true;
                            break;
                        }
                    }
                    break;
                }
            }
            if (!member_found) {
                diagnostic_error_code_formatted(checker->diag, "E3047", NODE_FILE(checker, node), node->token.line, node->token.column, 0, resolved_obj, member);
            }
            result = type_enum(checker_resolve_enum_key(checker, resolved_obj));
            return result;
        }

        /* C interop constant access: extern.EOF, extern.NULL, etc. */
        if (strcmp(obj_name, "extern") == 0 && typechecker_is_imported_module(checker, "extern")) {
            result = &TYPE_UNKNOWN;
            return result;
        }

        /* Check for user-module constant access: mod.CONST */
        if (typechecker_is_imported_module(checker, obj_name)) {
            char prefixed[MSG_BUF_SIZE];
            module_member_key(checker, obj_name, member, prefixed, sizeof(prefixed));
            Symbol *mod_sym = scope_lookup(checker->current_scope, prefixed);
            if (mod_sym) {
                mod_sym->used = true;
                /* A private variable or constant is as unreachable from
                 * outside its file as a private function. */
                reject_if_private(checker, node, obj_name, member);
                result = mod_sym->type;
                /* Mark module as used */
                mark_import_used(checker, obj_name);
                return result;
            }
        }

        /* Otherwise it's a struct field or multi-return access */
        Symbol *sym = scope_lookup(checker->current_scope, obj_name);
        /* A bare name that names a module-level declaration is bound under
         * that module's spelling, so a reference from inside the module has
         * to resolve the same way — matching the NODE_LABEL value path.
         * Without this, `addr(MODVAR.field)` inside the declaring module
         * types as ^unknown. */
        if (!sym) {
            DeclEntry *entry = checker_cache_resolution(checker, obj, obj_name);
            if (entry) {
                char key[MSG_BUF_SIZE];
                sym = scope_lookup(checker->current_scope,
                                   module_mangle_into(entry, key, sizeof(key)));
            }
        }
        /* or_return propagation guard: `_gray_orN.verr` is the sentinel for
         * "the call's trailing Error slot", whose index is only known now
         * that the temp's return arity is resolved. Rewrite it to the
         * concrete slot so the plain .vN paths below (and codegen) handle
         * it. A source that isn't an (..., Error) tuple is already reported
         * by E3045; leave the access unknown. */
        if (sym && strcmp(member, OR_RETURN_ERR_SLOT) == 0 &&
            strncmp(obj_name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) == 0) {
            if (sym->ret_types && sym->ret_count >= 1 &&
                sym->ret_types[sym->ret_count - 1]->kind == TK_ERROR) {
                char slot[16];
                snprintf(slot, sizeof(slot), "v%d", sym->ret_count - 1);
                member = arena_copy_string(checker->arena, slot);
                node->data.member.member = member;
            } else {
                /* Source doesn't end in Error — E3045 already reports it.
                 * Stay unknown so the guard's synthetic nil-compare and
                 * propagated return don't cascade further diagnostics. */
                return &TYPE_UNKNOWN;
            }
        }
        /* Multi-return .v0/.v1 access takes priority over struct field
         * lookup when the symbol has ret_types set. Without this, stdlib
         * functions returning struct types (Socket, HttpResponse, etc.)
         * would enter struct_field_type() which fails on .v0/.v1. */
        if (sym && sym->ret_types && member[0] == 'v' && member[1] >= '0' && member[1] <= '9') {
            int idx = member[1] - '0';
            if (idx < sym->ret_count) {
                result = sym->ret_types[idx];
            } else {
                diagnostic_error_code_formatted(checker->diag, "E3006", NODE_FILE(checker, node), node->token.line, node->token.column, 0, sym->ret_count, idx + 1);
                result = &TYPE_UNKNOWN;
            }
        } else if (sym && sym->type->kind == TK_STRUCT) {
            result = struct_field_type(checker, sym->type->name, member);
            if (result->kind == TK_UNKNOWN && member[0] != 'v') {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0, struct_display_name(checker, sym->type->name), member);
            }
        } else if (sym && member[0] == 'v' && member[1] >= '0' && member[1] <= '9') {
            /* Multi-return .v0/.v1/.v2 access; use stored return types */
            int idx = member[1] - '0';
            if (sym->ret_types && idx < sym->ret_count) {
                result = sym->ret_types[idx];
            } else if (idx == 0) {
                result = sym->type;
            } else if (sym->ret_types && idx >= sym->ret_count) {
                /* More variables than the function returns */
                diagnostic_error_code_formatted(checker->diag, "E3006", NODE_FILE(checker, node), node->token.line, node->token.column, 0, sym->ret_count, idx + 1);
                result = &TYPE_UNKNOWN;
            } else if (!sym->ret_types && idx > 0) {
                /* Single-return function used in multi-variable assignment */
                diagnostic_error_message(checker->diag, "E3006",
                    "too many variables; the function returns only 1 value",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                result = &TYPE_UNKNOWN;
            } else {
                result = type_from_name("Error"); /* fallback for (T, Error) pattern */
            }
        } else if (sym && sym->type->kind == TK_POINTER) {
            /* Pointer auto-deref field access */
            result = struct_field_type(checker, sym->type->element_type, member);
            if (result->kind == TK_UNKNOWN && member[0] != 'v') {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0, sym->type->element_type, member);
            }
        } else if (sym && sym->type->kind == TK_ERROR) {
            result = resolve_error_field(checker, node, member);
        } else if (sym && sym->type->kind != TK_UNKNOWN &&
                   sym->type->kind != TK_STRUCT && sym->type->kind != TK_ENUM &&
                   sym->type->kind != TK_POINTER &&
                   !(member[0] == 'v' && member[1] >= '0' && member[1] <= '9')) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type '%s' does not support access via dot notation",
                type_name(sym->type));
            diagnostic_error_message(checker->diag, "E3013", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* Struct-namespaced function or enum access: Type.func() / Type.MEMBER */
        if (!sym && is_struct_name(checker, resolved_obj)) {
            obj->data.label.value = resolved_obj;
            /* Check if member is a field; can't access fields on the type itself */
            GrayType *ft = struct_field_type(checker, resolved_obj, member);
            if (ft && ft->kind != TK_UNKNOWN) {
                diagnostic_error_code_formatted(checker->diag, "E3044", NODE_FILE(checker, node), node->token.line, node->token.column, 0, member, resolved_obj);
            } else {
                /* Not a field and not a struct function: the user is accessing
                 * a struct type as if it were an enum (Point.RED). Left
                 * unreported, codegen emitted the name verbatim and the C
                 * compiler failed on an undeclared identifier. */
                char skey[MSG_BUF_SIZE], fkey[MSG_BUF_SIZE];
                snprintf(fkey, sizeof(fkey), "%s_%s",
                    checker_resolve_decl_into(checker, resolved_obj, skey, sizeof(skey)), member);
                if (!find_func(checker, fkey)) {
                    diagnostic_error_code_formatted(checker->diag, "E3141",
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                        struct_display_name(checker, resolved_obj), member);
                }
            }
            result = &TYPE_UNKNOWN;
        }

        /* An imported module whose member matched nothing above: the member
         * does not exist. Leaving the type unknown reported nothing, because
         * the checks downstream skip TK_UNKNOWN to avoid cascading — so an
         * annotated declaration accepted the typo silently and an inferred
         * one surfaced later as an undefined variable, blaming the variable
         * rather than the member. The table holds every member of an
         * imported module, stdlib included, so it settles this. */
        if (!sym && result->kind == TK_UNKNOWN &&
            typechecker_is_imported_module(checker, obj_name) &&
            !is_struct_name(checker, resolved_obj) &&
            !is_enum_name(checker, resolved_obj)) {
            ResolveScope mscope = checker_scope(checker);
            if (!module_resolve_qualified(checker->modules, &mscope, obj_name, member, NULL)) {
                diagnostic_error_code_formatted(checker->diag, "E4024",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    obj_name, member);
            }
        }
    } else if (obj->kind == NODE_MEMBER_EXPR) {
        /* Check for module-qualified enum: lib.Color.RED */
        const char *mod = NULL, *type_n = NULL;
        if (ast_member_chain(node, &mod, &type_n)) {
            if (mod[0] >= 'a' && mod[0] <= 'z' &&
                type_n[0] >= 'A' && type_n[0] <= 'Z') {
                char prefixed[MSG_BUF_SIZE];
                module_member_key(checker, mod, type_n, prefixed, sizeof(prefixed));
                if (is_enum_name(checker, prefixed)) {
                    result = &TYPE_INT;
                    /* Mark module as used */
                    mark_import_used(checker, mod);
                    return result;
                }
            }
        }
        /* Nested member access: a.b.c; resolve a.b first, then look up .c */
        GrayType *obj_t = typetable_get(checker->type_table, obj);
        if (!obj_t) obj_t = resolve_expression(checker, obj);
        if (obj_t && obj_t->kind == TK_STRUCT) {
            result = struct_field_type(checker, obj_t->name, member);
            if (result->kind == TK_UNKNOWN && member[0] != 'v') {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    struct_display_name(checker, obj_t->name), member);
            }
        } else if (obj_t && obj_t->kind == TK_POINTER) {
            /* Auto-deref pointer field: a.next.val where a.next is ^Node */
            result = struct_field_type(checker, obj_t->element_type, member);
            if (result->kind == TK_UNKNOWN && member[0] != 'v') {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    obj_t->element_type, member);
            }
        } else if (obj_t && obj_t->kind == TK_ERROR) {
            result = resolve_error_field(checker, node, member);
        } else if (obj_t && obj_t->kind != TK_UNKNOWN && obj_t->kind != TK_STRUCT) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type '%s' does not support access via dot notation",
                type_name(obj_t));
            diagnostic_error_message(checker->diag, "E3013", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    } else {
        /* Object is an expression (e.g. foo().bar, p^.field); resolve its type */
        GrayType *obj_t = resolve_expression(checker, obj);
        if (obj_t && obj_t->kind == TK_STRUCT) {
            result = struct_field_type(checker, obj_t->name, member);
            if (result->kind == TK_UNKNOWN && member[0] != 'v') {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    struct_display_name(checker, obj_t->name), member);
            }
        } else if (obj_t && obj_t->kind == TK_POINTER && obj_t->element_type) {
            /* Auto-deref pointer from expression (array index, map index, call) */
            result = struct_field_type(checker, obj_t->element_type, member);
            if (result->kind == TK_UNKNOWN && member[0] != 'v') {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    obj_t->element_type, member);
            }
        } else if (obj_t && obj_t->kind == TK_ERROR) {
            /* Error from an expression: errs[0].code, m["k"].msg, f().code */
            result = resolve_error_field(checker, node, member);
        } else if (obj_t && obj_t->kind != TK_UNKNOWN && obj_t->kind != TK_VOID) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type '%s' does not support access via dot notation",
                type_name(obj_t));
            diagnostic_error_message(checker->diag, "E3013", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    return result;
}

static GrayType *resolve_struct_value(TypeChecker *checker, AstNode *node) {
    GrayType *result = &TYPE_UNKNOWN;
    const char *struct_name = node->data.struct_value.name;
    /* Type parameter: rewrite T → "?" so codegen can substitute */
    if (checker->type_param_name && strcmp(struct_name, checker->type_param_name) == 0) {
        node->data.struct_value.name = "?";
        struct_name = "?";
    }
    /* A literal written against a type parameter is what narrows the function
     * to struct arguments — `T{...}` means nothing for an int. The binding is
     * judged as E3127 below rather than as an undefined type, which is what
     * `int` would otherwise be called here. */
    bool name_from_type_param = false;
    if (strcmp(struct_name, "?") == 0) {
        /* During re-check with a binding, validate with concrete struct */
        if (checker->type_param_binding) {
            struct_name = checker->type_param_binding;
            name_from_type_param = true;
        } else {
            /* Main pass — skip field validation, return unknown */
            for (int i = 0; i < node->data.struct_value.count; i++)
                resolve_expression(checker, node->data.struct_value.field_values[i]);
            result = &TYPE_UNKNOWN;
            return result;
        }
    }
    typechecker_mark_type_module_used(checker, struct_name);
    {
        DeclEntry *entry = checker_cache_resolution(checker, node, struct_name);
        if (entry)
            struct_name = arena_copy_string(checker->arena,
                module_mangle(checker->modules, entry));
    }
    /* An alias names the struct it stands for, so Vec{...} builds a Point.
     * Re-point the node at the target too, so codegen emits the struct's own
     * tag instead of the alias spelling, which no C struct is named after. */
    {
        const char *target = resolve_type_alias(checker, struct_name);
        if (target && strcmp(target, struct_name) != 0) {
            struct_name = target;
            node->data.struct_value.name = target;
            node->resolved_decl = NULL;
        }
    }
    StructInfo *si = find_struct(checker, struct_name);
    warn_if_struct_deprecated(checker, node, si);
    /* E4016: reject undefined/unimported struct types in struct literals */
    if (!si && !is_struct_name(checker, struct_name)) {
        if (name_from_type_param) {
            /* E3127: the binding names a real type, just not a struct one. */
            if (!node->data.struct_value.type_param_rejected) {
                node->data.struct_value.type_param_rejected = true;
                diagnostic_error_code_formatted(checker->diag, "E3127",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    checker->type_param_name ? checker->type_param_name : "T",
                    unqualified_display_name(struct_name));
            }
        } else {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "undefined type '%s'; check the spelling or import the module that defines it",
                unqualified_display_name(struct_name));
            diagnostic_error_message(checker->diag, "E4016", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        result = &TYPE_UNKNOWN;
        return result;
    }
    /* E2015: check for duplicate field names in struct literal */
    for (int i = 0; i < node->data.struct_value.count; i++) {
        if (!node->data.struct_value.field_names[i]) continue;
        for (int j = 0; j < i; j++) {
            if (!node->data.struct_value.field_names[j]) continue;
            if (strcmp(node->data.struct_value.field_names[j],
                       node->data.struct_value.field_names[i]) == 0) {
                diagnostic_error_code_formatted(checker->diag, "E2015", NODE_FILE(checker, node), node->token.line, node->token.column, 0, node->data.struct_value.field_names[i]);
                break;
            }
        }
    }
    for (int i = 0; i < node->data.struct_value.count; i++) {
        /* Look up expected field type for implicit enum resolution */
        GrayType *field_expected_t = NULL;
        if (si && node->data.struct_value.field_names[i]) {
            const char *fname_pre = node->data.struct_value.field_names[i];
            for (int j = 0; j < si->field_count; j++) {
                if (strcmp(si->field_names[j], fname_pre) == 0) {
                    field_expected_t = si->field_types[j];
                    break;
                }
            }
        }
        GrayType *saved_sv_expected = checker->expected_type;
        if (field_expected_t)
            checker->expected_type = field_expected_t;
        /* A field value is a single-value position. */
        reject_multi_return_in_single_position(checker, node->data.struct_value.field_values[i]);
        GrayType *val_t = resolve_expression(checker, node->data.struct_value.field_values[i]);
        checker->expected_type = saved_sv_expected;
        /* Validate field exists */
        if (si && node->data.struct_value.field_names[i]) {
            const char *fname = node->data.struct_value.field_names[i];
            bool found = false;
            GrayType *expected_t = NULL;
            for (int j = 0; j < si->field_count; j++) {
                if (strcmp(si->field_names[j], fname) == 0) {
                    found = true;
                    expected_t = si->field_types[j];
                    break;
                }
            }
            if (!found) {
                diagnostic_error_code_formatted(checker->diag, "E3010", NODE_FILE(checker, node), node->token.line, node->token.column, 0, struct_name, fname);
            } else if (expected_t && val_t->kind != TK_UNKNOWN &&
                       expected_t->kind != TK_UNKNOWN &&
                       /* kinds differ, OR both are pointers to different types,
                        * OR both are structs with different names,
                        * OR both are enums with different names */
                       (!types_assignable(checker, expected_t, val_t) ||
                        (expected_t->kind == TK_POINTER &&
                         expected_t->name && val_t->name &&
                         strcmp(expected_t->name, val_t->name) != 0) ||
                        (expected_t->kind == TK_STRUCT && val_t->kind == TK_STRUCT &&
                         expected_t->name && val_t->name &&
                         !typechecker_same_struct_type(checker, expected_t->name, val_t->name)) ||
                        (expected_t->kind == TK_ENUM && val_t->kind == TK_ENUM &&
                         expected_t->name && val_t->name &&
                         !typechecker_same_enum_type(checker, expected_t->name, val_t->name))) &&
                       !(expected_t->kind == TK_ENUM && is_int_kind(val_t->kind)) &&
                       !(expected_t->kind == TK_STRUCT && is_int_kind(val_t->kind)) &&
                       !(is_int_kind(expected_t->kind) && val_t->kind == TK_STRUCT) &&
                       /* nil is a valid value for pointer and Error fields */
                       !(val_t->kind == TK_NIL &&
                         (expected_t->kind == TK_POINTER || expected_t->kind == TK_ERROR))) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "field '%s' of struct '%s': expected %s, got %s",
                    fname, struct_display_name(checker, struct_name), type_display_name(checker, expected_t), type_display_name(checker, val_t));
                diagnostic_error_message(checker->diag, "E3053", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            /* E3019: a field initializer that crosses signedness needs a cast. */
            if (found && expected_t && expected_t->name)
                check_signedness_crossing(checker, expected_t->name,
                    node->data.struct_value.field_values[i], val_t,
                    node->data.struct_value.field_values[i]);
            /* E3036: an out-of-range literal in a narrow field (S{ b: 300 }). */
            if (found && expected_t && expected_t->name) {
                int64_t field_lit;
                bool field_lit_neg;
                AstNode *fv = node->data.struct_value.field_values[i];
                if (try_get_signed_literal_int(fv, &field_lit, &field_lit_neg))
                    check_integer_range(checker->diag, NODE_FILE(checker, fv),
                        fv->token.line, fv->token.column, expected_t->name, field_lit, field_lit_neg);
            }
            /* E3066: func signature mismatch on a struct-literal field. The
             * mismatch check above treats any two func types as assignable, so
             * a reference with the wrong signature only got caught when it was
             * assigned to the field afterward — never when the literal that
             * built the struct supplied it. */
            if (found && func_types_mismatch(expected_t, val_t)) {
                AstNode *value = node->data.struct_value.field_values[i];
                char *msg = typechecker_format(checker,
                    "cannot assign %s to field '%s' of type %s",
                    type_display_name(checker, val_t), fname,
                    type_display_name(checker, expected_t));
                diagnostic_error_message(checker->diag, "E3066", msg,
                    NODE_FILE(checker, value), value->token.line, value->token.column, 0);
            }
        }
    }
    /* : for generic structs, infer the wildcard binding from
     * the field values and record the instantiation on the struct
     * decl so codegen can emit per-binding typedefs. */
    AstNode *sdecl = find_struct_in_program(checker->program, struct_name);
    if (sdecl && sdecl->data.struct_decl.is_generic) {
        const char *binding = NULL;
        for (int i = 0; i < node->data.struct_value.count; i++) {
            const char *fname = node->data.struct_value.field_names[i];
            if (!fname) continue;
            /* Find the field's declared type in the struct decl */
            for (int j = 0; j < sdecl->data.struct_decl.field_count; j++) {
                if (strcmp(sdecl->data.struct_decl.fields[j].name, fname) == 0 &&
                    sdecl->data.struct_decl.fields[j].type_name &&
                    strcmp(sdecl->data.struct_decl.fields[j].type_name, "?") == 0) {
                    GrayType *val_t = typetable_get(checker->type_table,
                        node->data.struct_value.field_values[i]);
                    if (!val_t) val_t = resolve_expression(checker, node->data.struct_value.field_values[i]);
                    if (val_t && val_t->kind != TK_UNKNOWN) {
                        const char *concrete = type_name(val_t);
                        if (!binding) {
                            binding = concrete;
                        } else if (strcmp(binding, concrete) != 0) {
                            char *msg = NULL;
                            msg = typechecker_format(checker,
                                "wildcard type conflict in struct '%s': '?' was bound to %s, but field '%s' is %s",
                                struct_name, binding, fname, concrete);
                            diagnostic_error_message(checker->diag, "E3159", msg,
                                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                        }
                    }
                    break;
                }
            }
        }
        if (binding) {
            node->data.struct_value.wildcard_binding = strdup(binding);
            /* Record instantiation on the struct decl */
            bool already = false;
            for (int ii = 0; ii < sdecl->data.struct_decl.instantiation_count; ii++) {
                if (strcmp(sdecl->data.struct_decl.instantiations[ii], binding) == 0) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                int n = sdecl->data.struct_decl.instantiation_count;
                sdecl->data.struct_decl.instantiations = xrealloc(
                    (void *)sdecl->data.struct_decl.instantiations,
                    sizeof(const char *) * (size_t)(n + 1));
                sdecl->data.struct_decl.instantiations[n] = strdup(binding);
                sdecl->data.struct_decl.instantiation_count = n + 1;
            }
            /* Return mangled struct type */
            char mangled[MSG_BUF_SIZE];
            size_t pos = snprintf(mangled, sizeof(mangled), "%s__", struct_name);
            for (const char *c = binding; *c && pos < sizeof(mangled) - 1; c++) {
                mangled[pos++] = (isalnum((unsigned char)*c) || *c == '_') ? *c : '_';
            }
            mangled[pos] = '\0';
            result = type_struct(strdup(mangled));
        } else {
            result = type_struct(struct_name);
        }
    } else {
        result = type_struct(struct_name);
    }
    return result;
}

/* Does a ref() argument name a function rather than a value? Both spellings
 * of a function reference mean the same thing, but ref() also takes the
 * address of a variable, field or element, so only a name that resolves to a
 * function may be treated as a reference to one. */
static bool ref_names_function(TypeChecker *checker, AstNode *arg) {
    if (arg->kind == NODE_LABEL)
        return find_func(checker, arg->data.label.value) != NULL;
    const char *qualifier = ast_member_qualifier(arg);
    if (!qualifier) return false;
    /* An instance's field, not a module's or struct's function. */
    if (scope_lookup(checker->current_scope, qualifier)) return false;
    if (is_stdlib_module_name(typechecker_resolve_alias(checker, qualifier)))
        return true;  /* rejected as non-first-class, but by resolve_func_ref */
    char key[MSG_BUF_SIZE], resolved[MSG_BUF_SIZE];
    snprintf(key, sizeof(key), "%s_%s",
        checker_resolve_decl_into(checker, qualifier, resolved, sizeof(resolved)),
        arg->data.member.member);
    return find_func(checker, key) != NULL;
}

static GrayType *resolve_func_ref(TypeChecker *checker, AstNode *node) {
    GrayType *result = &TYPE_UNKNOWN;
    /* Validate that the referenced function exists.
     * Builtin and stdlib functions cannot be used as function references. */
    const char *ref_name = NULL;
    const char *ref_struct_name = NULL;  /* struct name for privacy check */
    const char *ref_member_name = NULL;  /* member name for privacy check */
    if (node->data.func_ref.function->kind == NODE_LABEL) {
        const char *lname = node->data.func_ref.function->data.label.value;
        /* Surface 1: ()builtin_name — builtins are not first-class values */
        if (typechecker_is_builtin(lname)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot take a function reference to '%s'; builtin functions are not first-class values", lname);
            diagnostic_error_message(checker->diag, "E4019", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        } else {
            ref_name = lname;
        }
    } else if (node->data.func_ref.function->kind == NODE_MEMBER_EXPR) {
        AstNode *obj = node->data.func_ref.function->data.member.object;
        const char *member = node->data.func_ref.function->data.member.member;
        if (obj->kind == NODE_LABEL) {
            const char *mod_name = typechecker_resolve_alias(checker, obj->data.label.value);
            /* Naming a module member is using the module, whether the name is
             * called or referenced. Only calls marked the import before, so a
             * file whose only use of an import was a func reference was told
             * the import was never used. */
            if (!mark_import_used(checker, obj->data.label.value))
                mark_import_used(checker, mod_name);
            /* Surface 2: ()module.func — stdlib module functions are not first-class values */
            if (is_stdlib_module_name(mod_name)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot take a function reference to '%s.%s'; stdlib functions are not first-class values",
                    obj->data.label.value, member);
                diagnostic_error_message(checker->diag, "E4019", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            } else {
                /* Struct.func → lookup as Struct_func */
                ref_struct_name = obj->data.label.value;
                ref_member_name = member;
                char buffer[MSG_BUF_SIZE];
                {
                    char sk[MSG_BUF_SIZE];
                    snprintf(buffer, sizeof(buffer), "%s_%s",
                        checker_resolve_decl_into(checker, obj->data.label.value, sk, sizeof(sk)),
                        member);
                }
                ref_name = arena_copy_string(checker->arena, buffer);
            }
        }
    }
    FuncSig *ref_sig = ref_name ? find_func(checker, ref_name) : NULL;
    if (ref_sig) {
        ref_sig->used = true;
        warn_if_func_deprecated(checker, node, ref_sig);
        reject_test_fn_reference(checker, node, ref_sig);
        /* E4017: private struct function referenced from outside the struct */
        if (ref_sig->is_private && ref_struct_name &&
            !(checker->current_struct_name &&
              strcmp(checker->current_struct_name, ref_struct_name) == 0)) {
            diagnostic_error_code_formatted(checker->diag, "E4017", NODE_FILE(checker, node),
                node->token.line, node->token.column, 0,
                ref_struct_name, ref_member_name);
        }
    } else if (ref_name) {
        /* Surface 3: using module; ()stdlib_func — check if ref_name is in an active using module */
        bool found_in_using = false;
        for (int using_index = 0; using_index < checker->using_module_count && !found_in_using; using_index++) {
            if (!using_module_accessible(checker, using_index)) continue;
            const char *real_mod = typechecker_resolve_alias(checker, checker->using_modules[using_index]);
            if (is_stdlib_module_name(real_mod) && find_stdlib_meta(real_mod, ref_name)) {
                found_in_using = true;
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot take a function reference to '%s'; stdlib functions are not first-class values",
                    ref_name);
                diagnostic_error_message(checker->diag, "E4019", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        if (!found_in_using) {
            char *msg = NULL;
            msg = typechecker_format(checker, "undefined function '%s' in function reference", ref_name);
            const char *suggestion = suggest_similar_name(checker, ref_name);
            if (suggestion) {
                char help[MSG_BUF_SIZE];
                snprintf(help, sizeof(help), "did you mean '%s'?", suggestion);
                diagnostic_error_help(checker->diag, "E4002", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0, arena_copy_string(checker->arena, help));
            } else {
                diagnostic_error_message(checker->diag, "E4002", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    }
    /* Build a typed-func type from the referenced function's signature.
     * Canonical encoding: "func(p1,&p2,...)" with no "->R" suffix when
     * the function returns nothing; "func(...)->R" for a single return;
     * "func(...)->(R1,R2)" for multi-return. */
    if (ref_sig) {
        char buffer[512];
        size_t buffer_size = sizeof(buffer);
        int buf_len = snprintf(buffer, buffer_size, "func(");
        if ((size_t)buf_len >= buffer_size) buf_len = (int)buffer_size - 1;
        for (int i = 0; i < ref_sig->param_count && (size_t)buf_len < buffer_size - 1; i++) {
            bool mut_p = (ref_sig->decl && ref_sig->decl->kind == NODE_FUNC_DECL &&
                          i < ref_sig->decl->data.func_decl.param_count &&
                          ref_sig->decl->data.func_decl.params[i].mutable);
            /* type_name(), not ->name directly: a pointer/array/map type
             * stores its bare pointee/element/key-value in ->name (^int's
             * ->name is "int") — reading it raw here flattened func(^int)
             * to func(int), so a func-ref call with a pointer argument
             * either leaked a C compiler error or failed a bogus signature
             * check against the flattened type. */
            const char *param_type_name = ref_sig->param_types[i]
                ? type_name(ref_sig->param_types[i]) : "int";
            int written = snprintf(buffer + buf_len, buffer_size - (size_t)buf_len, "%s%s%s",
                i ? "," : "", mut_p ? "&" : "", param_type_name);
            if (written > 0 && (size_t)written < buffer_size - (size_t)buf_len) buf_len += written;
            else { buf_len = (int)buffer_size - 1; break; }
        }
        if ((size_t)buf_len < buffer_size - 1) {
            int written = snprintf(buffer + buf_len, buffer_size - (size_t)buf_len, ")");
            if (written > 0 && (size_t)written < buffer_size - (size_t)buf_len) buf_len += written;
            else buf_len = (int)buffer_size - 1;
        }
        const char *ret0_tn = (ref_sig->return_count == 1 && ref_sig->return_types[0])
            ? type_name(ref_sig->return_types[0]) : NULL;
        if (ret0_tn && strcmp(ret0_tn, "void") != 0 &&
            (size_t)buf_len < buffer_size - 1) {
            int written = snprintf(buffer + buf_len, buffer_size - (size_t)buf_len, "->%s",
                ret0_tn);
            if (written > 0 && (size_t)written < buffer_size - (size_t)buf_len) buf_len += written;
            else buf_len = (int)buffer_size - 1;
        } else if (ref_sig->return_count > 1 && (size_t)buf_len < buffer_size - 1) {
            int written = snprintf(buffer + buf_len, buffer_size - (size_t)buf_len, "->(");
            if (written > 0 && (size_t)written < buffer_size - (size_t)buf_len) buf_len += written;
            else buf_len = (int)buffer_size - 1;
            for (int i = 0; i < ref_sig->return_count && (size_t)buf_len < buffer_size - 1; i++) {
                const char *return_type_name = ref_sig->return_types[i]
                    ? type_name(ref_sig->return_types[i]) : "int";
                written = snprintf(buffer + buf_len, buffer_size - (size_t)buf_len, "%s%s",
                    i ? "," : "", return_type_name);
                if (written > 0 && (size_t)written < buffer_size - (size_t)buf_len) buf_len += written;
                else { buf_len = (int)buffer_size - 1; break; }
            }
            if ((size_t)buf_len < buffer_size - 1) {
                written = snprintf(buffer + buf_len, buffer_size - (size_t)buf_len, ")");
                if (written > 0 && (size_t)written < buffer_size - (size_t)buf_len) buf_len += written;
                else buf_len = (int)buffer_size - 1;
            }
        }
        char *encoded = strdup(buffer);
        result = type_from_name(encoded);
    } else {
        /* Unknown function: fall back to bare-func type
         * so downstream "is this callable" checks don't crash. */
        result = type_from_name("func");
    }
    return result;
}

/* Grayscale type name for an array- or map-literal element, used when an
 * unannotated `mut` array/map infers its element (or K/V) type from the first
 * entry. A wide-integer constructor call (i128(x), u256(x), ...) is resolved
 * as plain int/uint by the expression typechecker, so recover the width from
 * the call itself — otherwise the inferred container is [int] / map[..:int]
 * and the 16/32-byte value is truncated to 8 bytes in codegen. */
static const char *literal_elem_type_name(AstNode *elem, GrayType *resolved) {
    if (elem && elem->kind == NODE_CALL_EXPR &&
        elem->data.call.function->kind == NODE_LABEL &&
        is_bigint_type(elem->data.call.function->data.label.value))
        return elem->data.call.function->data.label.value;
    return resolved ? type_name(resolved) : "unknown";
}

static GrayType *resolve_expression(TypeChecker *checker, AstNode *node) {
    if (!node) return &TYPE_UNKNOWN;

    /* Memoize: if we already resolved this node, return the cached type.
     * This prevents duplicate diagnostics when the same subtree is walked
     * multiple times (e.g. builtin call args resolved by both the general
     * call path and the builtin-specific path).
     *
     * : skip the cache for call expressions during the re-check
     * pass (suppress_typetable_writes is true). The re-check walks
     * generic bodies with concrete parameter bindings; inner calls to
     * other generic functions need to re-run the dispatch to record
     * their concrete instantiations. Without this bypass, the cached
     * TK_UNKNOWN from the main pass short-circuits the resolution and
     * the inner function's binding never gets recorded. */
    GrayType *cached = typetable_get(checker->type_table, node);
    /* : bypass the cache entirely during the re-check pass
     * (suppress_typetable_writes). The re-check walks generic bodies
     * with concrete param bindings; stale TK_UNKNOWN entries from the
     * main pass prevent inner generic calls from resolving their
     * concrete bindings. */
    if (cached && !checker->suppress_typetable_writes)
        return cached;

    GrayType *result = &TYPE_UNKNOWN;

    switch (node->kind) {
    case NODE_INT_VALUE:
        result = &TYPE_INT;
        break;

    case NODE_FLOAT_VALUE:
        result = &TYPE_FLOAT;
        break;

    case NODE_STRING_VALUE:
        result = &TYPE_STRING;
        break;

    case NODE_INTERPOLATED_STRING:
        /* Resolve types of all interpolation parts */
        for (int i = 0; i < node->data.interpolated_string.part_count; i++) {
            AstNode *part = node->data.interpolated_string.parts[i];
            GrayType *pt = resolve_expression(checker, part);
            /* Only check non-literal parts (the ${expr} expressions) */
            if (part->kind == NODE_STRING_VALUE || !pt) continue;
            /* E3040: multi-return calls cannot appear in interpolation */
            reject_multi_return_in_single_position(checker, part);
            /* Interpolation expressions are re-lexed by a sub-lexer on
             * the extracted ${...} text, so part tokens have positions
             * relative to that sub-stream; not the original file.
             * Always anchor diagnostics at the outer string literal's
             * location instead. */
            int line = node->token.line;
            int col = node->token.column;
            bool is_func_type = (pt->kind == TK_FUNCTION);
            if (pt->kind == TK_VOID) {
                diagnostic_error_message(checker->diag, "E3041",
                    "cannot interpolate void expression; the function does not return a value",
                    NODE_FILE(checker, node), line, col, 0);
            } else if (pt->kind == TK_ENUM && pt->name && typechecker_enum_is_tagged(checker, pt->name)) {
                char *msg = typechecker_format(checker,
                    "cannot interpolate tagged enum '%s'; use when/is to destructure the payload first",
                    enum_display_name(checker, pt->name));
                diagnostic_error_message(checker->diag, "E3041", msg,
                    NODE_FILE(checker, node), line, col, 0);
            } else if (pt->kind == TK_ARRAY && pt->element_type &&
                       typechecker_enum_is_tagged(checker, pt->element_type)) {
                char *msg = typechecker_format(checker,
                    "cannot interpolate array of tagged enum '%s'; use when/is to destructure the payload first",
                    enum_display_name(checker, pt->element_type));
                diagnostic_error_message(checker->diag, "E3041", msg,
                    NODE_FILE(checker, node), line, col, 0);
            } else if (pt->kind == TK_MAP && pt->value_type &&
                       typechecker_enum_is_tagged(checker, pt->value_type)) {
                char *msg = typechecker_format(checker,
                    "cannot interpolate map of tagged enum '%s'; use when/is to destructure the payload first",
                    enum_display_name(checker, pt->value_type));
                diagnostic_error_message(checker->diag, "E3041", msg,
                    NODE_FILE(checker, node), line, col, 0);
            } else if (pt->kind == TK_STRUCT ||
                       pt->kind == TK_POINTER ||
                       is_func_type) {
                /* : interpolation codegen only handles scalars,
                 * strings, arrays, and maps. Structs, pointers, and
                 * func references fall through to a `%lld` + long-long
                 * cast in the generated C, which clang rejects. Catch
                 * it here with a targeted E3041 instead of leaking a
                 * raw C error, and nudge the user at the workaround
                 * (interpolate individual fields for structs). */
                char *msg = NULL;
                if (pt->kind == TK_STRUCT && pt->name) {
                    msg = typechecker_format(checker,
                        "cannot interpolate struct value of type '%s'; format fields individually (e.g. \"${v.field}\")",
                        type_display_name(checker, pt));
                } else if (pt->kind == TK_POINTER) {
                    msg = typechecker_format(checker,
                        "cannot interpolate pointer value; dereference with ^ or format the pointee explicitly");
                } else {
                    msg = typechecker_format(checker,
                        "cannot interpolate function reference; call the function or format its result");
                }
                diagnostic_error_message(checker->diag, "E3041", msg,
                    NODE_FILE(checker, node), line, col, 0);
            }
        }
        result = &TYPE_STRING;
        break;

    case NODE_BOOL_VALUE:
        result = &TYPE_BOOL;
        break;

    case NODE_CHAR_VALUE:
        result = &TYPE_CHAR;
        break;

    case NODE_NIL_VALUE:
        result = &TYPE_NIL;
        break;

    case NODE_LABEL: {
        const char *name = node->data.label.value;

        /* Type parameter name (e.g. T) — resolve as unknown during main
         * pass, or as the concrete binding during re-check.
         * Also handle "?" which is the rewritten form of T. */
        if (checker->type_param_name &&
            (strcmp(name, checker->type_param_name) == 0 ||
             strcmp(name, "?") == 0)) {
            if (checker->type_param_binding) {
                result = type_from_name(checker->type_param_binding);
            } else {
                result = &TYPE_UNKNOWN;
            }
            break;
        }

        /* Type names used as values are caught downstream; they won't match
         * any variable in scope, and functions like new(), mem.init(), and casts
         * legitimately take type names as arguments. */

        Symbol *sym = scope_lookup(checker->current_scope, name);
        if (sym) {
            /* Direct hit — a local, a parameter, or a file-scope global.
             * Flag the global case (declared in the root scope, nothing
             * nearer shadowing it) so codegen gives it a collision-proof
             * C name: a bare global can clash with a libc identifier. */
            Scope *decl_scope = checker->current_scope;
            while (decl_scope->parent && !scope_lookup_local(decl_scope, name))
                decl_scope = decl_scope->parent;
            if (decl_scope->parent == NULL)
                node->data.label.refers_to_file_global = true;
        }
        /* A bare name that names a module-level declaration is bound under
         * that module's spelling, so a reference from inside the module has
         * to resolve the same way. */
        if (!sym) {
            DeclEntry *entry = checker_cache_resolution(checker, node, name);
            if (entry) {
                if (entry->module_is_entry)
                    node->data.label.refers_to_file_global = true;
                char key[MSG_BUF_SIZE];
                sym = scope_lookup(checker->current_scope,
                                   module_mangle_into(entry, key, sizeof(key)));
            }
        }
        /* Try using-module-prefixed name if not found */
        bool named_private = false;
        if (!sym) {
            for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
                if (!using_module_accessible(checker, using_index)) continue;
                const char *umod = typechecker_resolve_alias(checker, checker->using_modules[using_index]);
                /* `using` does not bring a private declaration into scope, but
                 * its symbol is still bound under the module's spelling — so
                 * the prefixed lookup below found it and read it, and the bare
                 * name was the one spelling `private` did not stop. */
                if (reject_if_private(checker, node, checker->using_modules[using_index], name)) {
                    mark_import_used(checker, checker->using_modules[using_index]);
                    mark_import_used(checker, umod);
                    named_private = true;
                    break;
                }
                char prefixed[MSG_BUF_SIZE];
                module_member_key(checker, checker->using_modules[using_index], name,
                                  prefixed, sizeof(prefixed));
                sym = scope_lookup(checker->current_scope, prefixed);
                if (sym) {
                    /* Rewrite the label to the prefixed name so codegen finds it */
                    node->data.label.value = strdup(prefixed);
                    /* Mark module as used */
                    mark_import_used(checker, checker->using_modules[using_index]);
                    mark_import_used(checker, umod);
                    break;
                }
            }
        }
        if (named_private) {
            result = &TYPE_UNKNOWN;
        } else if (sym) {
            sym->used = true;
            result = sym->type;
            /* Transparent ref: unwrap pointer to expose underlying type.
             * The codegen auto-derefs ref vars, so the typechecker must
             * see the dereferenced type for indexing, comparison, etc. */
            if (sym->is_ref && result->kind == TK_POINTER && result->element_type) {
                result = type_from_name(result->element_type);
            }
        } else if (find_func(checker, name)) {
            /* Bare function name used as a value (). Call sites
             * inspect node->data.call.function directly and never
             * recurse through resolve_expression for a LABEL callee, and
             * NODE_FUNC_REF reads its inner label without going
             * through here either; so any NODE_LABEL that resolves
             * to a function at this point is an unwrapped reference
             * in a value position and should be rejected. */
            FuncSig *fs = find_func(checker, name);
            if (fs) fs->used = true;
            diagnostic_error_code_formatted(checker->diag, "E3031", NODE_FILE(checker, node), node->token.line, node->token.column, 0, name, name, name);
        } else if (typechecker_lookup_using_constant(checker, name)) {
            result = typechecker_lookup_using_constant(checker, name);
        } else if (typechecker_is_builtin(name)) {
            GrayType *bt = type_from_name(name);
            if (bt != &TYPE_UNKNOWN) {
                /* Builtin type name (int, i128, float, ...) in a value
                 * position. The builtins that take one — size_of(), type_of()
                 * — resolve their own argument, so anything arriving here is
                 * a name used as a value and was emitted into the C. */
                diagnostic_error_code_formatted(checker->diag, "E3100",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0, name);
                result = bt;
            } else {
                /* Bare builtin function name used as a value (e.g. `input`
                 * instead of `input()`). */
                diagnostic_error_code_formatted(checker->diag, "E3031", NODE_FILE(checker, node),
                    node->token.line, node->token.column, 0, name, name, name);
            }
        } else if (!typechecker_is_imported_module(checker, name) &&
                   !module_declares_const(checker, name) &&
                   type_name_as_value(checker, name)) {
            /* E3100: a name that names a type but no value. Reported here
             * rather than at each call site, because a type name reaches a
             * value position in far more places than an argument list — a
             * builtin argument and a plain initializer among them, neither of
             * which checked, so the name was emitted into the generated C and
             * the user saw a C compiler error. */
            const char *resolved = resolve_type_alias(checker,
                checker_resolve_type_name(checker, name));
            /* The way out depends on what the name reaches: a variant for an
             * enum, an instance for a struct, and neither for an alias of a
             * primitive, which gets the bare message. */
            if (is_enum_name(checker, resolved)) {
                char *msg = typechecker_format(checker,
                    "type name '%s' cannot be used as a value; use '%s.VARIANT' to access an enum value",
                    name, name);
                diagnostic_error_message(checker->diag, "E3100", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            } else if (is_struct_name(checker, resolved)) {
                char *msg = typechecker_format(checker,
                    "type name '%s' cannot be used as a value; use '%s{...}' or 'new(%s)' to create an instance",
                    name, name, name);
                diagnostic_error_message(checker->diag, "E3100", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            } else {
                diagnostic_error_code_formatted(checker->diag, "E3100",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0, name);
            }
            result = &TYPE_UNKNOWN;
        } else if (!is_enum_name(checker, name) &&
                   !is_struct_name(checker, name) &&
                   !typechecker_is_imported_module(checker, name) &&
                   !is_enum_name(checker, resolve_type_alias(checker, name)) &&
                   !is_struct_name(checker, resolve_type_alias(checker, name)) &&
                   (checker->in_file_scope_init ||
                    !module_declares_const(checker, name))) {
            /* Check if it looks like a number with a leading underscore */
            if (name[0] == '_' && name[1] >= '0' && name[1] <= '9') {
                diagnostic_error_code_formatted(checker->diag, "E1012", NODE_FILE(checker, node), node->token.line, node->token.column, 0, name + 1);
            } else {
                char *msg = NULL;
                msg = typechecker_format(checker, "undefined variable '%s'", name);
                /* Check if the name matches a named return value */
                bool is_named_return = false;
                const char *nr_type = NULL;
                if (checker->current_has_named_returns && checker->current_return_names) {
                    for (int i = 0; i < checker->current_return_count; i++) {
                        if (checker->current_return_names[i] &&
                            strcmp(checker->current_return_names[i], name) == 0) {
                            is_named_return = true;
                            nr_type = checker->current_return_type_names[i];
                            break;
                        }
                    }
                }
                if (is_named_return) {
                    char help[MSG_BUF_LARGE];
                    snprintf(help, sizeof(help),
                        "'%s' is a named return value in the function signature. Did you forget to declare 'mut %s %s' at function scope?",
                        name, name, nr_type ? nr_type : "?");
                    diagnostic_error_help(checker->diag, "E4001", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0, arena_copy_string(checker->arena, help));
                } else {
                    const char *suggestion = suggest_similar_name(checker, name);
                    if (suggestion) {
                        char help[MSG_BUF_SIZE];
                        snprintf(help, sizeof(help), "did you mean '%s'?", suggestion);
                        diagnostic_error_help(checker->diag, "E4001", msg,
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0, arena_copy_string(checker->arena, help));
                    } else {
                        diagnostic_error_message(checker->diag, "E4001", msg,
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                    }
                }
            }
        }
        break;
    }

    case NODE_PREFIX_EXPR: {
        GrayType *right = resolve_expression(checker, node->data.prefix.right);
        if (node->data.prefix.op == TOK_BANG) {
            if (right->kind != TK_BOOL && right->kind != TK_UNKNOWN) {
                diagnostic_error_code_formatted(checker->diag, "E3090", NODE_FILE(checker, node), node->token.line, node->token.column, 0, type_display_name(checker, right));
            }
            result = &TYPE_BOOL;
        } else if (node->data.prefix.op == TOK_MINUS) {
            if (right->kind != TK_UNKNOWN && !type_is_numeric(right)) {
                diagnostic_error_code_formatted(checker->diag, "E3007", NODE_FILE(checker, node), node->token.line, node->token.column, 0, type_display_name(checker, right));
            } else if (right->kind != TK_UNKNOWN && right->name && is_unsigned_type(right->name)) {
                diagnostic_error_code_formatted(checker->diag, "E3096", NODE_FILE(checker, node), node->token.line, node->token.column, 0, right->name);
            }
            result = right;
        } else if (node->data.prefix.op == TOK_BIT_NOT) {
            /* E3090: bit_not requires an integer operand */
            if (right->kind != TK_UNKNOWN && !is_int_kind(right->kind) && right->kind != TK_CHAR) {
                diagnostic_error_code_formatted(checker->diag, "E8002", NODE_FILE(checker, node), node->token.line, node->token.column, 0, type_display_name(checker, right));
            }
            result = right;
        } else {
            result = right;
        }
        break;
    }

    case NODE_INFIX_EXPR:
        result = resolve_infix_expr(checker, node);
        break;

    case NODE_POSTFIX_EXPR: {
        GrayType *left_t = resolve_expression(checker, node->data.postfix.left);
        if (node->data.postfix.op == TOK_CARET) {
            /* Pointer checker: dereferencing a pointer into a @mem arena that
             * has been destroyed (E3164) or reset (E3165). */
            pc_check_mem_deref(checker, node->data.postfix.left, node);
            if (left_t->kind == TK_POINTER) {
                /* Dereference: ^T^ → T */
                result = typechecker_type_from_name(checker, left_t->element_type);
            } else if (left_t->kind != TK_UNKNOWN) {
                diagnostic_error_code_formatted(checker->diag, "E3016", NODE_FILE(checker, node), node->token.line, node->token.column, 0, type_display_name(checker, left_t));
                result = left_t;
            } else {
                result = left_t;
            }
        } else if (node->data.postfix.op == TOK_INCREMENT ||
                   node->data.postfix.op == TOK_DECREMENT) {
            /* E5015: ++ and -- require a variable, not a literal */
            if (node->data.postfix.left->kind != NODE_LABEL &&
                node->data.postfix.left->kind != NODE_INDEX_EXPR &&
                node->data.postfix.left->kind != NODE_MEMBER_EXPR &&
                !(node->data.postfix.left->kind == NODE_POSTFIX_EXPR &&
                  node->data.postfix.left->data.postfix.op == TOK_CARET)) {
                diagnostic_error_message(checker->diag, "E5015",
                    "++ and -- require a variable; you cannot increment a literal or expression",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            /* ++ and -- only valid on mutable numeric types.
             * Walk nested member/index chains so that e.g. p.x++ is caught. */
            {
                const char *root = assignment_target_root_name(node->data.postfix.left);
                if (root) {
                    Symbol *sym = scope_lookup(checker->current_scope, root);
                    if (sym && !sym->mutable && !(sym->type && sym->type->kind == TK_POINTER))
                        diagnostic_error_code_formatted(checker->diag, "E3005", NODE_FILE(checker, node), node->token.line, node->token.column, 0, root);
                }
            }
            if (left_t->kind != TK_UNKNOWN && !type_is_integer(left_t)) {
                diagnostic_error_code_formatted(checker->diag, "E5023", NODE_FILE(checker, node), node->token.line, node->token.column, 0, operator_display_name(node->data.postfix.op), type_display_name(checker, left_t));
            }
            result = left_t;
        } else {
            result = left_t;
        }
        break;
    }

    case NODE_CALL_EXPR:
        result = resolve_call_expr(checker, node);
        break;

    case NODE_MEMBER_EXPR:
        result = resolve_member_expr(checker, node);
        break;

    case NODE_INDEX_EXPR: {
        /* Neither half of `a[i]` is a multi-value position: a fallible
         * (T, Error) call there drops the Error, and a user multi-return
         * call fails the C compile. */
        reject_multi_return_in_single_position(checker, node->data.index_expr.left);
        reject_multi_return_in_single_position(checker, node->data.index_expr.index);
        GrayType *left = resolve_expression(checker, node->data.index_expr.left);
        /* Propagate map key type as expected_type so .VARIANT resolves */
        GrayType *saved_idx_expected = checker->expected_type;
        if (left->kind == TK_MAP && left->key_type) {
            GrayType *key_t = typechecker_type_from_name(checker, left->key_type);
            if (key_t && key_t->kind == TK_ENUM && key_t->name)
                checker->expected_type = key_t;
        }
        GrayType *idx_t = resolve_expression(checker, node->data.index_expr.index);
        checker->expected_type = saved_idx_expected;
        /* E3003: array index must be integer */
        if (left->kind == TK_ARRAY && idx_t->kind != TK_UNKNOWN &&
            !is_int_kind(idx_t->kind) && idx_t->kind != TK_BYTE) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "array index must be an integer, got %s", type_name(idx_t));
            diagnostic_error_message(checker->diag, "E3003", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* Reject negative literal index at compile time. Applies to
         * arrays and strings (); the analogous runtime panic
         * fires for both, and there's no reason to wait until then
         * when the index is a literal '-N'. */
        if ((left->kind == TK_ARRAY || left->kind == TK_STRING) &&
            node->data.index_expr.index->kind == NODE_PREFIX_EXPR &&
            node->data.index_expr.index->data.prefix.op == TOK_MINUS &&
            node->data.index_expr.index->data.prefix.right->kind == NODE_INT_VALUE) {
            const char *what = left->kind == TK_STRING ? "string" : "array";
            char *msg;
            msg = typechecker_format(checker, "%s index cannot be negative", what);
            diagnostic_error_message(checker->diag, "E3003", msg,
                NODE_FILE(checker, node->data.index_expr.index), node->data.index_expr.index->token.line,
                node->data.index_expr.index->token.column, 0);
        }
        if (left->kind == TK_ARRAY && left->element_type) {
            result = typechecker_type_from_name(checker, left->element_type);
        } else if (left->kind == TK_MAP && left->value_type) {
            result = typechecker_type_from_name(checker, left->value_type);
            /* Check map key type matches. Enum keys are int-backed, so accept
             * int expressions (and enum members, which resolve as int) when
             * the declared key is a user enum name. */
            if (left->key_type && idx_t->kind != TK_UNKNOWN) {
                GrayType *key_t = typechecker_type_from_name(checker, left->key_type);
                bool declared_is_enum = is_enum_name(checker, left->key_type);
                bool compatible =
                    key_t->kind == TK_UNKNOWN ||
                    key_t->kind == idx_t->kind ||
                    (is_int_kind(key_t->kind) && is_int_kind(idx_t->kind)) ||
                    (declared_is_enum && is_int_kind(idx_t->kind));
                if (!compatible) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "map key type mismatch: expected '%s', got '%s'",
                        left->key_type, type_name(idx_t));
                    diagnostic_error_message(checker->diag, "E3156", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                }
            }
        } else if (left->kind == TK_STRING) {
            if (idx_t->kind != TK_UNKNOWN && !is_int_kind(idx_t->kind) && idx_t->kind != TK_BYTE) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "string index must be an integer, got %s", type_name(idx_t));
                diagnostic_error_message(checker->diag, "E3003", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            result = &TYPE_CHAR;
        } else if (left->kind != TK_UNKNOWN) {
            diagnostic_error_code_formatted(checker->diag, "E3008", NODE_FILE(checker, node), node->token.line, node->token.column, 0, type_display_name(checker, left));
        }
        break;
    }

    case NODE_ARRAY_VALUE: {
        /* If expected_type is an array-of-enum, propagate element type for .VARIANT */
        GrayType *saved_arr_expected = checker->expected_type;
        if (checker->expected_type && checker->expected_type->kind == TK_ARRAY &&
            checker->expected_type->element_type) {
            GrayType *elem_t = typechecker_type_from_name(checker, checker->expected_type->element_type);
            if (elem_t && elem_t->kind == TK_ENUM && elem_t->name)
                checker->expected_type = elem_t;
        }
        if (node->data.array_value.count > 0) {
            GrayType *first = resolve_expression(checker, node->data.array_value.elements[0]);
            reject_multi_return_in_single_position(checker, node->data.array_value.elements[0]);
            result = type_array(literal_elem_type_name(node->data.array_value.elements[0], first));
            /* Validate all elements have the same type */
            for (int i = 1; i < node->data.array_value.count; i++) {
                GrayType *element_resolved = resolve_expression(checker, node->data.array_value.elements[i]);
                reject_multi_return_in_single_position(checker, node->data.array_value.elements[i]);
                if (!element_resolved || element_resolved->kind == TK_UNKNOWN || !first || first->kind == TK_UNKNOWN)
                    continue;
                /* Strict type equality for array elements — no coercions */
                bool compatible = (first->kind == element_resolved->kind);
                if (compatible && first->name && element_resolved->name)
                    compatible = strcmp(first->name, element_resolved->name) == 0;
                if (compatible && first->element_type && element_resolved->element_type)
                    compatible = strcmp(first->element_type, element_resolved->element_type) == 0;
                if (!compatible) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "array elements must all be the same type; element %d is '%s' but the array is '%s'",
                        i, type_name(element_resolved), type_name(first));
                    diagnostic_error_message(checker->diag, "E3053", msg,
                        NODE_FILE(checker, node->data.array_value.elements[i]), node->data.array_value.elements[i]->token.line,
                        node->data.array_value.elements[i]->token.column, 0);
                    break;
                }
            }
        }
        checker->expected_type = saved_arr_expected;
        break;
    }

    case NODE_MAP_VALUE: {
        /* If expected_type is a map-of-enum, propagate value type for .VARIANT */
        GrayType *saved_map_expected = checker->expected_type;
        if (checker->expected_type && checker->expected_type->kind == TK_MAP &&
            checker->expected_type->value_type) {
            GrayType *val_t = typechecker_type_from_name(checker, checker->expected_type->value_type);
            if (val_t && val_t->kind == TK_ENUM && val_t->name)
                checker->expected_type = val_t;
        }
        /* Resolve key and value types */
        for (int i = 0; i < node->data.map_value.count; i++) {
            GrayType *kt = resolve_expression(checker, node->data.map_value.keys[i]);
            GrayType *vt = resolve_expression(checker, node->data.map_value.values[i]);
            /* : void can't be a map key or value. */
            reject_void_in_context(checker, node->data.map_value.keys[i], kt, "map key");
            reject_void_in_context(checker, node->data.map_value.values[i], vt, "map value");
            /* E3040: multi-return call in single-value map position */
            reject_multi_return_in_single_position(checker, node->data.map_value.keys[i]);
            reject_multi_return_in_single_position(checker, node->data.map_value.values[i]);
        }
        /* E12006: Check for duplicate keys in map literal */
        for (int i = 0; i < node->data.map_value.count; i++) {
            AstNode *ki = node->data.map_value.keys[i];
            for (int j = i + 1; j < node->data.map_value.count; j++) {
                AstNode *kj = node->data.map_value.keys[j];
                bool dup = false;
                if (ki->kind == NODE_STRING_VALUE && kj->kind == NODE_STRING_VALUE &&
                    strcmp(ki->data.string_value.value, kj->data.string_value.value) == 0) {
                    dup = true;
                } else if (ki->kind == NODE_INT_VALUE && kj->kind == NODE_INT_VALUE &&
                    ki->data.int_value.value == kj->data.int_value.value) {
                    dup = true;
                }
                if (dup) {
                    diagnostic_error_code(checker->diag, "E12006", NODE_FILE(checker, kj), kj->token.line, kj->token.column, 0);
                }
            }
        }
        GrayType *resolved_type = type_alloc();
        resolved_type->kind = TK_MAP;
        resolved_type->name = strdup("map");
        if (node->data.map_value.count > 0) {
            GrayType *kt = typetable_get(checker->type_table, node->data.map_value.keys[0]);
            GrayType *vt = typetable_get(checker->type_table, node->data.map_value.values[0]);
            resolved_type->key_type = strdup(literal_elem_type_name(node->data.map_value.keys[0], kt));
            resolved_type->value_type = strdup(literal_elem_type_name(node->data.map_value.values[0], vt));
        } else if (saved_map_expected && saved_map_expected->kind == TK_MAP &&
                   saved_map_expected->key_type && saved_map_expected->value_type) {
            /* `{:}` carries no pair to infer from — adopt the element types
             * of the context it is written into, so codegen sizes the table
             * from the declared type instead of falling back to string keys
             * and 8-byte values. */
            resolved_type->key_type = strdup(saved_map_expected->key_type);
            resolved_type->value_type = strdup(saved_map_expected->value_type);
        }
        result = resolved_type;
        checker->expected_type = saved_map_expected;
        break;
    }

    case NODE_STRUCT_VALUE:
        result = resolve_struct_value(checker, node);
        break;

    case NODE_RANGE_EXPR: {
        /* Validate range arguments are integer types */
        AstNode *parts[] = { node->data.range_expr.start,
                             node->data.range_expr.end,
                             node->data.range_expr.step };
        const char *labels[] = { "start", "end", "step" };
        for (int return_index = 0; return_index < 3; return_index++) {
            if (!parts[return_index]) continue;
            GrayType *pt = resolve_expression(checker, parts[return_index]);
            if (pt->kind != TK_UNKNOWN && !is_int_kind(pt->kind)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'range()' %s argument must be an integer type, got '%s'",
                    labels[return_index], type_name(pt));
                tc_err_arg_type(checker, parts[return_index], msg);
            }
        }
        GrayType *rt = type_alloc();
        rt->kind = TK_INT;
        rt->name = strdup("Range<int>");
        result = rt;
        break;
    }

    case NODE_IMPLICIT_ENUM:
        result = resolve_implicit_enum(checker, node);
        break;

    case NODE_WHEN_PATTERN:
        /* Pattern nodes are validated in the NODE_WHEN_STMT handler */
        if (node->data.when_pattern.enum_name)
            result = type_enum(node->data.when_pattern.enum_name);
        else
            result = &TYPE_UNKNOWN;
        break;

    case NODE_CAST_EXPR: {
        GrayType *src_t = resolve_expression(checker, node->data.cast.value);
        /* The cast target is a written type name like any annotation, so it
         * goes through the same resolution: as written, then through aliases.
         * Taken literally, `cast(x, I)` where `alias I = int` was read as a
         * cast to an unknown user type and rejected. The diagnostic below
         * still names the spelling the programmer used. */
        const char *written_target = node->data.cast.target_type;
        const char *target = resolve_type_alias(checker,
            checker_resolve_type_name(checker, written_target));
        node->data.cast.target_type = target;
        /* E4016: a target that names no type, at any depth. The allowlist
         * below cannot stand in for this: an undefined capitalized name types
         * as TK_STRUCT and was reported as an unsupported conversion, and an
         * undefined lowercase one types as TK_UNKNOWN, which skips the
         * allowlist entirely and let the written name reach the C compiler. */
        {
            char leaf[MSG_BUF_SIZE];
            const char *undefined = undefined_type_leaf(checker, written_target,
                                                        leaf, sizeof(leaf));
            if (undefined) {
                char *msg = typechecker_format(checker,
                    "undefined type '%s'; check the spelling or import the module that defines it",
                    unqualified_display_name(undefined));
                diagnostic_error_message(checker->diag, "E4016", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                result = &TYPE_UNKNOWN;
                break;
            }
        }
        GrayType *dst_t = type_from_name(target);
        /* Resolve user-defined enum types that type_from_name() can't find */
        if (!dst_t && is_enum_name(checker, target))
            dst_t = type_enum(target);
        /* E3167: cast() cannot reinterpret one pointer type as another. A
         * pointer cast bypasses every lifetime and type guarantee the pointer
         * checker relies on; there is no safe form of it in the language. */
        if (src_t && src_t->kind == TK_POINTER && dst_t && dst_t->kind == TK_POINTER) {
            char src_name[TYPE_NAME_MAX];
            strncpy(src_name, type_name(src_t), sizeof(src_name) - 1);
            src_name[sizeof(src_name) - 1] = '\0';
            diagnostic_error_code_formatted(checker->diag, "E3167",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                src_name, unqualified_display_name(written_target));
            result = dst_t;
            break;
        }
        /* Allowlist-based cast validation */
        if (src_t && src_t->kind != TK_UNKNOWN && dst_t && dst_t->kind != TK_UNKNOWN) {
            bool allowed = false;
            /* Same kind is always allowed (identity cast) */
            if (src_t->kind == dst_t->kind && src_t->kind != TK_ARRAY &&
                src_t->kind != TK_MAP && src_t->kind != TK_STRUCT)
                allowed = true;
            /* Numeric <-> Numeric (int, uint, float, char, byte, sized types) */
            if (type_is_numeric(src_t) && type_is_numeric(dst_t))
                allowed = true;
            /* Bool <-> Numeric */
            if ((src_t->kind == TK_BOOL && type_is_numeric(dst_t)) ||
                (type_is_numeric(src_t) && dst_t->kind == TK_BOOL))
                allowed = true;
            /* String -> numeric: NOT allowed via cast(); use dedicated parsing
             * functions (e.g. strings.parse_int()) instead. cast() is for
             * type reinterpretation, not fallible string parsing. */
            /* Numeric/Bool -> String (stringification) */
            if ((type_is_numeric(src_t) || src_t->kind == TK_BOOL) && dst_t->kind == TK_STRING)
                allowed = true;
            /* String -> String (identity) */
            if (src_t->kind == TK_STRING && dst_t->kind == TK_STRING)
                allowed = true;
            /* A string-backed enum is a GrayString at runtime, not an
             * integer, so the int-backed rules below do not apply to it. */
            bool src_str_enum = src_t->kind == TK_ENUM &&
                typechecker_enum_is_string(checker, src_t->name);
            bool dst_str_enum = dst_t->kind == TK_ENUM &&
                typechecker_enum_is_string(checker, dst_t->name);
            /* Enum -> int/uint (int-backed enums only) */
            if (src_t->kind == TK_ENUM && !src_str_enum &&
                (dst_t->kind == TK_INT || dst_t->kind == TK_UINT))
                allowed = true;
            /* int/uint -> Enum (explicit reinterpretation, int-backed only) */
            if ((src_t->kind == TK_INT || src_t->kind == TK_UINT) &&
                dst_t->kind == TK_ENUM && !dst_str_enum)
                allowed = true;
            /* string <-> string-backed enum */
            if ((src_t->kind == TK_STRING && dst_str_enum) ||
                (src_str_enum && dst_t->kind == TK_STRING))
                allowed = true;
            /* Array -> Array: only when both element types are numeric */
            if (src_t->kind == TK_ARRAY && dst_t->kind == TK_ARRAY) {
                if (src_t->element_type && dst_t->element_type) {
                    GrayType *src_elem = type_from_name(src_t->element_type);
                    GrayType *dst_elem = type_from_name(dst_t->element_type);
                    if (type_is_numeric(src_elem) && type_is_numeric(dst_elem))
                        allowed = true;
                } else {
                    allowed = true;
                }
            }
            if (!allowed) {
                char tn[TYPE_NAME_MAX];
                if (src_t->kind == TK_ARRAY && src_t->element_type)
                    snprintf(tn, sizeof(tn), "[%s]", src_t->element_type);
                else {
                    strncpy(tn, type_name(src_t), sizeof(tn) - 1);
                    tn[sizeof(tn) - 1] = '\0';
                }
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot cast '%s' to '%s'",
                    tn, unqualified_display_name(written_target));
                diagnostic_error_help(checker->diag, "E3043", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    "only primitive-to-primitive casts are supported (e.g. cast(x, int), cast(x, string))");
            }
        }
        result = dst_t;
        break;
    }

    case NODE_NEW_EXPR: {
        const char *new_type = node->data.new_expr.type_name;
        /* Type parameter: rewrite T → "?" so codegen can substitute */
        if (checker->type_param_name && strcmp(new_type, checker->type_param_name) == 0) {
            node->data.new_expr.type_name = "?";
            new_type = "?";
        }
        /* Resolve the name as written, then aliases — the same order a type
         * annotation goes through, so `new(mod.T)` and `^mod.T` agree. */
        new_type = checker_resolve_type_name(checker, new_type);
        new_type = resolve_type_alias(checker, new_type);
        node->data.new_expr.type_name = new_type;
        if (strcmp(new_type, "?") == 0) {
            /* During re-check with a binding, validate the concrete type */
            if (checker->type_param_binding) {
                result = type_pointer(checker->type_param_binding);
            } else {
                result = type_pointer("?");
            }
        } else {
            typechecker_mark_type_module_used(checker, new_type);
            GrayType *nt = type_from_name(new_type);
            bool known = is_struct_name(checker, new_type) ||
                         is_enum_name(checker, new_type) ||
                         (nt->kind != TK_UNKNOWN && nt->kind != TK_STRUCT) ||
                         /* Stdlib opaque types (Thread, Mutex, ...): type_from_name()
                          * normalizes the mangled mod_Type to a bare-name TK_STRUCT,
                          * which the disjunct above deliberately excludes since
                          * type_from_name() also returns TK_STRUCT for any
                          * unregistered capitalized name. Check the opaque
                          * allowlist explicitly instead, same as
                          * typechecker_type_from_name() does elsewhere. */
                         is_stdlib_opaque_type_available(checker, unqualified_display_name(new_type));
            if (!known) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'new()' requires a known type, but '%s' is not defined",
                    unqualified_display_name(new_type));
                diagnostic_error_message(checker->diag, "E3041", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            result = type_pointer(new_type);
        }
        break;
    }

    case NODE_FUNC_REF:
        result = resolve_func_ref(checker, node);
        break;

    default:
        break;
    }

    if (!checker->suppress_typetable_writes) {
        typetable_set(checker->type_table, node, result);
    }
    return result;
}

/* Check if a name uses a reserved prefix that would collide with generated C.
 * Compiler-generated temporaries also start with _gray_, so callers must skip
 * the names they synthesize rather than exempting the prefix here. */
static void check_reserved_name(TypeChecker *checker, const char *name, const char *file, int line, int col) {
    if (!name) return;
    if (strncmp(name, "gray_", 5) == 0 ||
        strncmp(name, GRAY_SYNTH_PREFIX, sizeof(GRAY_SYNTH_PREFIX) - 1) == 0 ||
        strncmp(name, "Gray", 4) == 0) {
        diagnostic_error_code_formatted(checker->diag, "E4006", file, line, col, 0, name);
    }
}

/* Reject `main` as the name of anything but the top-level entry-point
 * function. Without this a `const main`, `mut main`, struct/enum/alias/param
 * named `main` either collides silently or surfaces as a misleading
 * "program has no main() function" (E4005). */
static void check_reserved_main(TypeChecker *checker, const char *name, const char *file, int line, int col) {
    if (name && strcmp(name, "main") == 0) {
        diagnostic_error_code(checker->diag, "E4026", file, line, col, 0);
        checker->main_name_misused = true;
    }
}

/* --- Keyword alias consistency (E2088) --- */

/* One alias family: the spelling that first established the file's dialect.
 * `dialect` is 0 for the canonical spelling and 1 for the alias; the two words
 * of a joint pair (when/is vs switch/case, or/otherwise vs elif/else) share a
 * single tracker and pass the same dialect id, so crossing them is a mix. */
typedef struct {
    const char *form;   /* first keyword form seen (e.g. "while" or "as_long_as") */
    int dialect;
    int line;
    int column;
} AliasFirst;

typedef struct {
    AliasFirst loop;    /* while / as_long_as                     */
    AliasFirst branch;  /* or + otherwise / elif + else   (joint) */
    AliasFirst not_in;  /* !in / not_in                           */
    AliasFirst func;    /* do / fn                                */
    AliasFirst match;   /* when + is / switch + case      (joint) */
    AliasFirst ensure;  /* ensure / defer                         */
} AliasState;

/* Record the spelling that establishes this file's dialect for one alias
 * family, or report E2088 when a later spelling belongs to the other side. */
static void note_alias(TypeChecker *checker, AliasFirst *slot, const char *form,
                       int dialect, int line, int column, const char *file) {
    if (!slot->form) {
        slot->form = form;
        slot->dialect = dialect;
        slot->line = line;
        slot->column = column;
    } else if (slot->dialect != dialect) {
        char *msg = typechecker_format(checker,
            "mixed keyword aliases in the same file; '%s' used here, but '%s' was used on line %d",
            form, slot->form, slot->line);
        diagnostic_error_message(checker->diag, "E2088", msg, file, line, column, 0);
    }
}

/* Match a token spelling against one alias family and record it. Spellings that
 * belong to no pair (a leading 'if', a when case with no keyword) are ignored. */
static void note_alias_form(TypeChecker *checker, AliasFirst *slot,
                            const char *form, const char *canonical, const char *alias,
                            int line, int column, const char *file) {
    if (!form) return;
    if (strcmp(form, canonical) == 0) {
        note_alias(checker, slot, form, 0, line, column, file);
    } else if (strcmp(form, alias) == 0) {
        note_alias(checker, slot, form, 1, line, column, file);
    }
}

static void check_alias_walk(TypeChecker *checker, AstNode *node,
                             AliasState *state, const char *file);

static void check_alias_block(TypeChecker *checker, AstNode *block,
                              AliasState *state, const char *file) {
    if (!block || block->kind != NODE_BLOCK_STMT) return;
    for (int i = 0; i < block->data.block.count; i++) {
        check_alias_walk(checker, block->data.block.stmts[i], state, file);
    }
}

/* Recursively scan an expression tree for the !in/not_in membership operator. */
static void check_alias_expr(TypeChecker *checker, AstNode *expr,
                             AliasState *state, const char *file) {
    if (!expr) return;

    switch (expr->kind) {
    case NODE_INFIX_EXPR: {
        if (expr->data.infix.op == TOK_NOT_IN) {
            note_alias_form(checker, &state->not_in, expr->token.literal,
                            "not_in", "!in",
                            expr->token.line, expr->token.column, file);
        }
        check_alias_expr(checker, expr->data.infix.left, state, file);
        check_alias_expr(checker, expr->data.infix.right, state, file);
        break;
    }
    case NODE_PREFIX_EXPR:
        check_alias_expr(checker, expr->data.prefix.right, state, file);
        break;
    case NODE_POSTFIX_EXPR:
        check_alias_expr(checker, expr->data.postfix.left, state, file);
        break;
    case NODE_CALL_EXPR:
        check_alias_expr(checker, expr->data.call.function, state, file);
        for (int i = 0; i < expr->data.call.arg_count; i++) {
            check_alias_expr(checker, expr->data.call.args[i], state, file);
        }
        break;
    case NODE_INDEX_EXPR:
        check_alias_expr(checker, expr->data.index_expr.left, state, file);
        check_alias_expr(checker, expr->data.index_expr.index, state, file);
        break;
    case NODE_MEMBER_EXPR:
        check_alias_expr(checker, expr->data.member.object, state, file);
        break;
    case NODE_CAST_EXPR:
        check_alias_expr(checker, expr->data.cast.value, state, file);
        break;
    case NODE_RANGE_EXPR:
        check_alias_expr(checker, expr->data.range_expr.start, state, file);
        check_alias_expr(checker, expr->data.range_expr.end, state, file);
        check_alias_expr(checker, expr->data.range_expr.step, state, file);
        break;
    case NODE_ARRAY_VALUE:
        for (int i = 0; i < expr->data.array_value.count; i++) {
            check_alias_expr(checker, expr->data.array_value.elements[i], state, file);
        }
        break;
    case NODE_MAP_VALUE:
        for (int i = 0; i < expr->data.map_value.count; i++) {
            check_alias_expr(checker, expr->data.map_value.keys[i], state, file);
            check_alias_expr(checker, expr->data.map_value.values[i], state, file);
        }
        break;
    case NODE_STRUCT_VALUE:
        for (int i = 0; i < expr->data.struct_value.count; i++) {
            check_alias_expr(checker, expr->data.struct_value.field_values[i], state, file);
        }
        break;
    case NODE_INTERPOLATED_STRING:
        for (int i = 0; i < expr->data.interpolated_string.part_count; i++) {
            check_alias_expr(checker, expr->data.interpolated_string.parts[i], state, file);
        }
        break;
    default:
        break;
    }
}

static void check_alias_walk(TypeChecker *checker, AstNode *node,
                             AliasState *state, const char *file) {
    if (!node) return;

    switch (node->kind) {
    case NODE_WHILE_STMT: {
        note_alias_form(checker, &state->loop, node->token.literal,
                        "as_long_as", "while",
                        node->token.line, node->token.column, file);
        check_alias_expr(checker, node->data.while_stmt.condition, state, file);
        check_alias_block(checker, node->data.while_stmt.body, state, file);
        break;
    }
    case NODE_IF_STMT: {
        /* 'or'/'elif' arrives as a nested if node whose own token is the
         * branch keyword; a leading 'if' matches neither and is skipped. */
        note_alias_form(checker, &state->branch, node->token.literal,
                        "or", "elif",
                        node->token.line, node->token.column, file);
        /* The final branch shares the same tracker: a file that writes 'elif'
         * must close with 'else', and one that writes 'or' must close with
         * 'otherwise'. */
        if (node->data.if_stmt.alternative && node->data.if_stmt.else_token.line > 0) {
            note_alias_form(checker, &state->branch, node->data.if_stmt.else_token.literal,
                            "otherwise", "else",
                            node->data.if_stmt.else_token.line,
                            node->data.if_stmt.else_token.column, file);
        }
        check_alias_expr(checker, node->data.if_stmt.condition, state, file);
        check_alias_block(checker, node->data.if_stmt.consequence, state, file);
        if (node->data.if_stmt.alternative) {
            check_alias_walk(checker, node->data.if_stmt.alternative, state, file);
        }
        break;
    }
    case NODE_BLOCK_STMT:
        check_alias_block(checker, node, state, file);
        break;
    case NODE_FOR_STMT:
        check_alias_expr(checker, node->data.for_stmt.iterable, state, file);
        check_alias_block(checker, node->data.for_stmt.body, state, file);
        break;
    case NODE_FOR_EACH_STMT:
        check_alias_expr(checker, node->data.for_each.collection, state, file);
        check_alias_block(checker, node->data.for_each.body, state, file);
        break;
    case NODE_LOOP_STMT:
        check_alias_block(checker, node->data.loop_stmt.body, state, file);
        break;
    case NODE_FUNC_DECL:
        note_alias_form(checker, &state->func, node->token.literal,
                        "do", "fn",
                        node->token.line, node->token.column, file);
        check_alias_block(checker, node->data.func_decl.body, state, file);
        break;
    case NODE_STRUCT_DECL:
        /* Struct functions are declared with 'do'/'fn' too. */
        for (int i = 0; i < node->data.struct_decl.func_count; i++) {
            check_alias_walk(checker, node->data.struct_decl.funcs[i].func_decl, state, file);
        }
        break;
    case NODE_WHEN_STMT:
        note_alias_form(checker, &state->match, node->token.literal,
                        "when", "switch",
                        node->token.line, node->token.column, file);
        check_alias_expr(checker, node->data.when_stmt.value, state, file);
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            /* Case keywords share the tracker: 'switch' pairs with 'case',
             * 'when' with 'is'. */
            Token kw = node->data.when_stmt.cases[i].kw_token;
            note_alias_form(checker, &state->match, kw.literal,
                            "is", "case", kw.line, kw.column, file);
            check_alias_block(checker, node->data.when_stmt.cases[i].body, state, file);
        }
        if (node->data.when_stmt.default_body) {
            check_alias_block(checker, node->data.when_stmt.default_body, state, file);
        }
        break;
    case NODE_VAR_DECL:
        check_alias_expr(checker, node->data.var_decl.value, state, file);
        break;
    case NODE_ASSIGN_STMT:
        check_alias_expr(checker, node->data.assign.value, state, file);
        break;
    case NODE_RETURN_STMT:
        for (int i = 0; i < node->data.return_stmt.count; i++) {
            check_alias_expr(checker, node->data.return_stmt.values[i], state, file);
        }
        break;
    case NODE_ENSURE_STMT:
        note_alias_form(checker, &state->ensure, node->token.literal,
                        "ensure", "defer",
                        node->token.line, node->token.column, file);
        check_alias_expr(checker, node->data.ensure_stmt.expr, state, file);
        break;
    case NODE_EXPR_STMT:
        check_alias_expr(checker, node->data.expr_stmt.expr, state, file);
        break;
    default:
        break;
    }
}

static void check_keyword_alias_consistency(TypeChecker *checker, AstNode *program) {
    if (!program || program->kind != NODE_PROGRAM) return;

    /* Track per-file state: group statements by source file */
    const char *current_file = NULL;
    AliasState state = {0};

    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (!stmt) continue;

        /* Determine which file this statement belongs to */
        const char *stmt_file = stmt->token.file ? stmt->token.file : checker->file;

        /* When the file changes, reset the alias trackers */
        bool same_file = (current_file == stmt_file) ||
            (current_file && stmt_file && strcmp(current_file, stmt_file) == 0);
        if (!same_file) {
            current_file = stmt_file;
            state = (AliasState){0};
        }

        check_alias_walk(checker, stmt, &state, stmt_file ? stmt_file : checker->file);
    }
}

/* --- Statement checking --- */

static void check_statement(TypeChecker *checker, AstNode *node);

/* Walk an expression tree looking for a function call anywhere inside.
 * Used by the file-scope initializer guard (E5013): runtime calls in
 * a top-level var_decl produce invalid C (free-standing init or
 * non-constant initializer), so the Grayscale side must reject them with
 * a real diagnostic before codegen runs. */
static bool expression_contains_call(AstNode *node) {
    if (!node) return false;
    switch (node->kind) {
    case NODE_CALL_EXPR:
        /* Compile-time builtins and intrinsic operations are allowed in
         * constant initializers and at file scope.  Memory builtins (new,
         * ref, copy, addr) and introspection builtins (len, type_of,
         * size_of, make_size) produce runtime values but are not user-
         * defined function calls — they map directly to C constructs. */
        if (node->data.call.function &&
            node->data.call.function->kind == NODE_LABEL) {
            const char *name = node->data.call.function->data.label.value;
            if (strcmp(name, "embed") == 0 ||
                strcmp(name, "here") == 0 ||
                strcmp(name, "new") == 0 ||
                strcmp(name, "ref") == 0 ||
                strcmp(name, "copy") == 0 ||
                strcmp(name, "addr") == 0 ||
                strcmp(name, "raw") == 0 ||
                strcmp(name, "make_size") == 0 ||
                strcmp(name, "len") == 0 ||
                strcmp(name, "type_of") == 0 ||
                strcmp(name, "size_of") == 0) {
                return false;
            }
        }
        return true;
    case NODE_PREFIX_EXPR:
        return expression_contains_call(node->data.prefix.right);
    case NODE_INFIX_EXPR:
        return expression_contains_call(node->data.infix.left) ||
               expression_contains_call(node->data.infix.right);
    case NODE_POSTFIX_EXPR:
        return expression_contains_call(node->data.postfix.left);
    case NODE_INDEX_EXPR:
        return expression_contains_call(node->data.index_expr.left) ||
               expression_contains_call(node->data.index_expr.index);
    case NODE_MEMBER_EXPR:
        return expression_contains_call(node->data.member.object);
    case NODE_RANGE_EXPR:
        return expression_contains_call(node->data.range_expr.start) ||
               expression_contains_call(node->data.range_expr.end) ||
               expression_contains_call(node->data.range_expr.step);
    case NODE_CAST_EXPR:
        return expression_contains_call(node->data.cast.value);
    case NODE_ARRAY_VALUE:
        for (int i = 0; i < node->data.array_value.count; i++) {
            if (expression_contains_call(node->data.array_value.elements[i])) return true;
        }
        return false;
    case NODE_MAP_VALUE:
        for (int i = 0; i < node->data.map_value.count; i++) {
            if (expression_contains_call(node->data.map_value.keys[i])) return true;
            if (expression_contains_call(node->data.map_value.values[i])) return true;
        }
        return false;
    case NODE_STRUCT_VALUE:
        for (int i = 0; i < node->data.struct_value.count; i++) {
            if (expression_contains_call(node->data.struct_value.field_values[i])) return true;
        }
        return false;
    case NODE_INTERPOLATED_STRING:
        for (int i = 0; i < node->data.interpolated_string.part_count; i++) {
            if (expression_contains_call(node->data.interpolated_string.parts[i])) return true;
        }
        return false;
    default:
        return false;
    }
}

/* Check if ALL paths through a block end in a return statement */
static bool block_has_return(AstNode *node); /* forward declaration */

static bool all_paths_return(AstNode *node) {
    if (!node) return false;
    if (node->kind == NODE_RETURN_STMT) return true;
    if (node->kind == NODE_BLOCK_STMT) {
        /* Last statement must be a return or all-paths-return construct */
        if (node->data.block.count == 0) return false;
        for (int i = 0; i < node->data.block.count; i++) {
            if (node->data.block.stmts[i]->kind == NODE_RETURN_STMT) return true;
        }
        AstNode *last = node->data.block.stmts[node->data.block.count - 1];
        return all_paths_return(last);
    }
    if (node->kind == NODE_IF_STMT) {
        /* Both branches must return */
        if (!node->data.if_stmt.alternative) return false;
        return all_paths_return(node->data.if_stmt.consequence) &&
               all_paths_return(node->data.if_stmt.alternative);
    }
    if (node->kind == NODE_WHEN_STMT) {
        /* A `#strict` enum `when` is exhaustive by the time this runs — any
         * uncovered variant has already produced E3056 — so it terminates
         * when every case body does, with no `default` required. */
        if (!node->data.when_stmt.default_body) {
            if (!node->data.when_stmt.is_strict) return false;
        } else if (!all_paths_return(node->data.when_stmt.default_body)) {
            return false;
        }
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            if (!all_paths_return(node->data.when_stmt.cases[i].body)) return false;
        }
        return true;
    }
    /* loop { ... return ... }; an infinite loop with a return always returns */
    if (node->kind == NODE_LOOP_STMT) {
        return block_has_return(node->data.loop_stmt.body);
    }
    return false;
}

/* Recursively check if an AST node or its children contain a return statement */
static bool block_has_return(AstNode *node) {
    if (!node) return false;
    if (node->kind == NODE_RETURN_STMT) return true;
    if (node->kind == NODE_BLOCK_STMT) {
        for (int i = 0; i < node->data.block.count; i++) {
            if (block_has_return(node->data.block.stmts[i])) return true;
        }
    }
    if (node->kind == NODE_IF_STMT) {
        if (block_has_return(node->data.if_stmt.consequence)) return true;
        if (block_has_return(node->data.if_stmt.alternative)) return true;
    }
    if (node->kind == NODE_WHEN_STMT) {
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            if (block_has_return(node->data.when_stmt.cases[i].body)) return true;
        }
        if (block_has_return(node->data.when_stmt.default_body)) return true;
    }
    /* loop { ... return ... }; loop always executes, so a return inside counts */
    if (node->kind == NODE_LOOP_STMT) {
        if (block_has_return(node->data.loop_stmt.body)) return true;
    }
    /* for/for_each/as_long_as may also contain returns */
    if (node->kind == NODE_FOR_STMT) {
        if (block_has_return(node->data.for_stmt.body)) return true;
    }
    if (node->kind == NODE_FOR_EACH_STMT) {
        if (block_has_return(node->data.for_each.body)) return true;
    }
    if (node->kind == NODE_WHILE_STMT) {
        if (block_has_return(node->data.while_stmt.body)) return true;
    }
    return false;
}

/* E3070: ensure must appear at the top level of a function body. The
 * codegen ensure-cleanup pass only walks the function-body block, so
 * an ensure inside a for/while/if/etc. body would silently never fire.
 * Rather than introducing closure-capture semantics for nested ensures,
 * reject them at the type-check stage with a clear error. */
static void check_no_nested_ensure(TypeChecker *checker, AstNode *node, bool nested) {
    if (!node) return;
    if (node->kind == NODE_ENSURE_STMT && nested) {
        diagnostic_error_code(checker->diag, "E3070", NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        return;
    }
    if (node->kind == NODE_BLOCK_STMT) {
        for (int i = 0; i < node->data.block.count; i++) {
            check_no_nested_ensure(checker, node->data.block.stmts[i], nested);
        }
        return;
    }
    switch (node->kind) {
    case NODE_IF_STMT:
        check_no_nested_ensure(checker, node->data.if_stmt.consequence, true);
        check_no_nested_ensure(checker, node->data.if_stmt.alternative, true);
        break;
    case NODE_FOR_STMT:
        check_no_nested_ensure(checker, node->data.for_stmt.body, true);
        break;
    case NODE_FOR_EACH_STMT:
        check_no_nested_ensure(checker, node->data.for_each.body, true);
        break;
    case NODE_WHILE_STMT:
        check_no_nested_ensure(checker, node->data.while_stmt.body, true);
        break;
    case NODE_LOOP_STMT:
        check_no_nested_ensure(checker, node->data.loop_stmt.body, true);
        break;
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            check_no_nested_ensure(checker, node->data.when_stmt.cases[i].body, true);
        }
        check_no_nested_ensure(checker, node->data.when_stmt.default_body, true);
        break;
    default:
        break;
    }
}

static void check_block(TypeChecker *checker, AstNode *node) {
    if (!node || node->kind != NODE_BLOCK_STMT) return;
    bool seen_return = false;
    for (int i = 0; i < node->data.block.count; i++) {
        AstNode *stmt = node->data.block.stmts[i];
        if (seen_return && stmt) {
            diagnostic_warning_code(checker->diag, "W2003", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            break; /* only warn once per block */
        }
        check_statement(checker, stmt);
        if (stmt && stmt->kind == NODE_RETURN_STMT) {
            seen_return = true;
        }
    }
    /* E3006: multi-var destructuring with fewer variables than return values.
     * Desugared blocks look like: _gray_tmpN = call(); a = _gray_tmpN.v0; b = _gray_tmpN.v1; ...
     * If var_count < ret_count, trailing return values are silently lost. */
    if (node->data.block.count >= 2) {
        AstNode *first = node->data.block.stmts[0];
        if (first && first->kind == NODE_VAR_DECL &&
            strncmp(first->data.var_decl.name, GRAY_SYNTH_TMP, sizeof(GRAY_SYNTH_TMP) - 1) == 0 &&
            first->data.var_decl.value &&
            first->data.var_decl.value->kind == NODE_CALL_EXPR) {
            Symbol *sym = scope_lookup_local(checker->current_scope, first->data.var_decl.name);
            if (sym && sym->ret_types && sym->ret_count > 0) {
                int var_count = node->data.block.count - 1;
                if (var_count < sym->ret_count) {
                    /* Extract function name for the error message */
                    AstNode *call_fn = first->data.var_decl.value->data.call.function;
                    const char *function_name = "function";
                    if (call_fn->kind == NODE_LABEL) {
                        function_name = call_fn->data.label.value;
                    } else if (call_fn->kind == NODE_MEMBER_EXPR &&
                               call_fn->data.member.member) {
                        function_name = call_fn->data.member.member;
                    }
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "'%s' returns %d values but only %d variable(s) provided; "
                        "all return values must be handled (use '_' to discard unwanted values)",
                        function_name, sym->ret_count, var_count);
                    diagnostic_error_message(checker->diag, "E3006", msg,
                        NODE_FILE(checker, first), first->token.line, first->token.column, 0);
                }
            }
        }
    }
    /* E3006/E3040: or_return binding arity. The desugared block is
     *   _gray_orN = call(); if (_gray_orN.verr) { return ... }; a = _gray_orN.v0; ...
     * The trailing Error slot is consumed by the guard, so the user bindings
     * must match the call's non-error return slots exactly. Nothing else checks
     * this, so trailing values were dropped silently and an over-count bound the
     * Error slot to a user variable. */
    if (node->data.block.count >= 3) {
        AstNode *first = node->data.block.stmts[0];
        if (first && first->kind == NODE_VAR_DECL &&
            strncmp(first->data.var_decl.name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) == 0 &&
            first->data.var_decl.value &&
            first->data.var_decl.value->kind == NODE_CALL_EXPR) {
            Symbol *sym = scope_lookup_local(checker->current_scope, first->data.var_decl.name);
            if (sym && sym->ret_types && sym->ret_count >= 2 &&
                sym->ret_types[sym->ret_count - 1]->kind == TK_ERROR) {
                int nonerr_count = sym->ret_count - 1;
                int var_count = node->data.block.count - 2; /* tmp_decl + guard */
                /* `_ = call() or_return` is the whole-result discard form,
                 * equivalent to bare `call() or_return`; it binds nothing. */
                AstNode *b0 = node->data.block.stmts[2];
                bool discard_all = var_count == 1 && b0 &&
                    b0->kind == NODE_VAR_DECL && strcmp(b0->data.var_decl.name, "_") == 0;
                AstNode *call_fn = first->data.var_decl.value->data.call.function;
                const char *function_name = "function";
                if (call_fn->kind == NODE_LABEL) {
                    function_name = call_fn->data.label.value;
                } else if (call_fn->kind == NODE_MEMBER_EXPR &&
                           call_fn->data.member.member) {
                    function_name = call_fn->data.member.member;
                }
                const char *file = NODE_FILE(checker, first);
                int line = first->token.line, col = first->token.column;
                if (discard_all) {
                    /* nothing bound — no arity to check */
                } else if (var_count == 1 && nonerr_count > 1) {
                    diagnostic_error_code_formatted(checker->diag, "E3040", file, line, col, 0,
                        function_name, nonerr_count, function_name);
                } else if (var_count < nonerr_count) {
                    char *msg = typechecker_format(checker,
                        "'%s' returns %d values but only %d variable(s) provided; "
                        "all return values must be handled (use '_' to discard unwanted values)",
                        function_name, nonerr_count, var_count);
                    diagnostic_error_message(checker->diag, "E3006", msg, file, line, col, 0);
                } else if (var_count > nonerr_count) {
                    diagnostic_error_code_formatted(checker->diag, "E3006", file, line, col, 0,
                        nonerr_count, var_count);
                }
            }
        }
    }
}

/* --- check_statement() per-case helpers --- */

/* The literal a scalar type zero-initializes to, for the W1004 message.
 * NULL for container/struct/pointer types (no single literal to show). */
static const char *zero_value_literal(const char *type_name) {
    if (!type_name) return NULL;
    if (is_any_int_type(type_name)) return "0";
    if (strcmp(type_name, "float") == 0) return "0.0";
    if (strcmp(type_name, "string") == 0) return "\"\"";
    if (strcmp(type_name, "bool") == 0) return "false";
    if (strcmp(type_name, "char") == 0) return "'\\0'";
    return NULL;
}

/* The seven primitive value types eligible for `mut` array/map literal type
 * inference (issue #2374). */
static bool typechecker_kind_is_primitive(TypeKind k) {
    return k == TK_INT || k == TK_UINT || k == TK_FLOAT || k == TK_BOOL ||
           k == TK_CHAR || k == TK_BYTE || k == TK_STRING;
}

/* True when `lit` (an already-resolved array or map literal, type `t`) is a
 * non-empty literal whose element types — for a map, both key and value — are
 * all primitive, so its type can be inferred without an annotation. Empty
 * literals and non-primitive elements return false. */
static bool typechecker_literal_type_inferable(TypeChecker *checker, AstNode *lit, GrayType *t) {
    if (!t) return false;
    if (t->kind == TK_ARRAY) {
        if (lit->data.array_value.count == 0 || !t->element_type) return false;
        GrayType *et = typechecker_type_from_name(checker, t->element_type);
        return et && typechecker_kind_is_primitive(et->kind);
    }
    if (t->kind == TK_MAP) {
        if (lit->data.map_value.count == 0 || !t->key_type || !t->value_type) return false;
        GrayType *kt = typechecker_type_from_name(checker, t->key_type);
        GrayType *vt = typechecker_type_from_name(checker, t->value_type);
        return kt && vt && typechecker_kind_is_primitive(kt->kind) &&
               typechecker_kind_is_primitive(vt->kind);
    }
    return false;
}

static void check_var_decl(TypeChecker *checker, AstNode *node) {
    /* Resolve type aliases in the declared type name so downstream
     * checks and codegen see the underlying type. */
    if (node->data.var_decl.type_name) {
        node->data.var_decl.type_name = resolve_type_alias(checker, node->data.var_decl.type_name);
    }
    /* #deprecated: warn when a variable is explicitly annotated with a
     * deprecated struct or enum type. */
    warn_if_type_name_deprecated(checker, node, node->data.var_decl.type_name, NULL);
    /* E5013: file-scope initializers cannot contain function calls.
     * A runtime call as an initializer would either need a module-init
     * function (which Grayscale does not generate) or produce invalid C
     * (non-constant initializer / free-standing statement at file
     * scope). Catch it on the Grayscale side so the user sees a real
     * diagnostic instead of a clang error pointing at the generated C. */
    if (checker->func_depth == 0 && node->data.var_decl.value &&
        expression_contains_call(node->data.var_decl.value)) {
        diagnostic_error_code(checker->diag, "E5013",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E5040: function-scope const initializers cannot contain runtime
     * function calls.  Constants must be compile-time-known. */
    if (checker->func_depth > 0 && !node->data.var_decl.mutable &&
        node->data.var_decl.value &&
        expression_contains_call(node->data.var_decl.value)) {
        diagnostic_error_code(checker->diag, "E5040",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Track const integer values for constant folding in later
     * declarations (e.g. fixed-size array sizes).  Also detect overflow
     * in const arithmetic expressions: codegen emits runtime
     * overflow-check wrappers (gray_add_check etc.) which are not valid
     * as C file-scope initializers.  The typechecker must evaluate and
     * reject overflowing expressions before codegen runs.
     * E5039: constant expression overflows the declared integer type. */
    if (!node->data.var_decl.mutable &&
        node->data.var_decl.type_name && node->data.var_decl.value) {
        const char *type_name_str = node->data.var_decl.type_name;
        /* Track integer types (signed and unsigned) in the const table.
         * Unsigned values that fit in int64_t are stored as-is; this
         * covers practical array-size use cases.  Full uint64 overflow
         * detection is left to a separate check; for now we just ensure
         * the codegen fix applies (in_const_decl suppresses the runtime
         * wrapper). */
        bool is_int_type = is_any_int_type(type_name_str);
        if (is_int_type) {
            int64_t folded = 0;
            bool overflowed = false;
            bool ok = typechecker_fold_const_int(checker, node->data.var_decl.value, &folded, &overflowed);
            if (ok) {
                /* Expression is a valid compile-time constant.  Register
                 * the value so later const declarations can reference it. */
                typechecker_register_const_int(checker, node->data.var_decl.name, folded);
            } else if (overflowed) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "constant expression overflows type '%s'", type_name_str);
                diagnostic_error_message(checker->diag, "E5039",
                    msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    }
    /* E3038: void cannot be used as variable type */
    if (node->data.var_decl.type_name && strcmp(node->data.var_decl.type_name, "void") == 0) {
        diagnostic_error_message(checker->diag, "E3038",
            "'void' cannot be used as a variable type",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E3038: void in array/map types.
     * "void" is legal as a typed-func return type (encoded as
     * "func(...)->void"), so skip the strstr check for those. */
    if (node->data.var_decl.type_name) {
        const char *type_name_str = node->data.var_decl.type_name;
        bool is_typed_func = strncmp(type_name_str, "func(", 5) == 0 ||
                             strncmp(type_name_str, "[func(", 6) == 0;
        if (!is_typed_func && strstr(type_name_str, "void") != NULL && strcmp(type_name_str, "void") != 0) {
            diagnostic_error_message(checker->diag, "E3038",
                "'void' cannot be used as an element type in arrays or maps",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* E3101: func reference variables must use 'const', not 'mut' */
    if (node->data.var_decl.mutable) {
        bool is_func_ref_value = node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_FUNC_REF;
        bool is_func_type = node->data.var_decl.type_name &&
            strncmp(node->data.var_decl.type_name, "func(", 5) == 0;
        if (is_func_ref_value || is_func_type) {
            diagnostic_error_message(checker->diag, "E3101",
                "func reference variables must be declared with 'const', not 'mut'; func references are compile-time aliases",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* E3034: 'any' type is reserved */
    if (node->data.var_decl.type_name && strcmp(node->data.var_decl.type_name, "any") == 0) {
        diagnostic_error_code(checker->diag, "E3034", NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E2038: reserved type name as variable name */
    if (node->data.var_decl.name[0] != '_' &&
        is_reserved_type_name(node->data.var_decl.name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "'%s' is a reserved type name and cannot be used as a variable name",
            VAR_DISPLAY_NAME(node));
        diagnostic_error_message(checker->diag, "E2038", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E5016: builtin function name as variable name */
    if (node->data.var_decl.name[0] != '_' &&
        is_reserved_builtin_func_name(node->data.var_decl.name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "'%s' is a builtin function and cannot be used as a variable name",
            VAR_DISPLAY_NAME(node));
        diagnostic_error_message(checker->diag, "E5016", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E5035: stdlib module name as variable name */
    if (node->data.var_decl.name[0] != '_' &&
        is_stdlib_module_name(node->data.var_decl.name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "'%s' is a standard library module and cannot be used as a variable name",
            VAR_DISPLAY_NAME(node));
        diagnostic_error_message(checker->diag, "E5035", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E4026: 'main' is reserved for the entry-point function */
    check_reserved_main(checker, node->data.var_decl.name,
        NODE_FILE(checker, node), node->token.line, node->token.column);
    /* E3045: or_return on non-error-returning function */
    if (strncmp(node->data.var_decl.name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) == 0 &&
        node->data.var_decl.value && node->data.var_decl.value->kind == NODE_CALL_EXPR) {
        AstNode *call_fn = node->data.var_decl.value->data.call.function;
        const char *call_name = NULL;
        const char *call_qualifier = ast_member_qualifier(call_fn);
        if (call_fn->kind == NODE_LABEL) call_name = call_fn->data.label.value;
        else if (call_qualifier) {
            /* module.func() or Type.func() */
            char prefixed[MSG_BUF_SIZE];
            call_name = arena_copy_string(checker->arena,
                module_member_key(checker, call_qualifier,
                                  call_fn->data.member.member, prefixed, sizeof(prefixed)));
        }
        if (call_name) {
            FuncSig *sig = find_func(checker, call_name);
            if (sig && (sig->return_count < 2 ||
                sig->return_types[sig->return_count - 1]->kind != TK_ERROR)) {
                char display[MSG_BUF_SIZE];
                if (call_qualifier)
                    snprintf(display, sizeof(display), "%s.%s",
                        call_qualifier, call_fn->data.member.member);
                else {
                    strncpy(display, call_name, sizeof(display) - 1);
                    display[sizeof(display) - 1] = '\0';
                }
                diagnostic_error_code_formatted(checker->diag, "E3045", NODE_FILE(checker, node->data.var_decl.value), node->data.var_decl.value->token.line,
                    node->data.var_decl.value->token.column, 0, display);
            }
        }
    }
    /* E3059: maps cannot be declared const */
    if (!node->data.var_decl.mutable && node->data.var_decl.type_name &&
        strncmp(node->data.var_decl.type_name, "map[", 4) == 0) {
        diagnostic_error_code_help(checker->diag, "E3059", NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            "change 'const' to 'mut'; use a struct for fixed key-value data");
    }
    /* const must have a value */
    if (!node->data.var_decl.mutable && !node->data.var_decl.value) {
        diagnostic_error_code_formatted(checker->diag, "E2011", NODE_FILE(checker, node), node->token.line, node->token.column, 0, VAR_DISPLAY_NAME(node));
    }
    /* W1004: a mut-class declaration with a type but no value silently
     * zero-initializes. State the fact so a dropped '= value' is visible.
     * Skip synthetic decls and '_'; guard against the generic re-check
     * pass so it fires once. */
    if (!checker->suppress_typetable_writes &&
        node->data.var_decl.mutable && !node->data.var_decl.value &&
        node->data.var_decl.type_name &&
        strcmp(node->data.var_decl.name, "_") != 0 &&
        strncmp(node->data.var_decl.name, GRAY_SYNTH_TMP, sizeof(GRAY_SYNTH_TMP) - 1) != 0 &&
        strncmp(node->data.var_decl.name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) != 0) {
        const char *zero = zero_value_literal(node->data.var_decl.type_name);
        char *msg = zero
            ? typechecker_format(checker, "'%s' declared with no value — defaults to %s",
                                 VAR_DISPLAY_NAME(node), zero)
            : typechecker_format(checker, "'%s' declared with no value — defaults to its zero value",
                                 VAR_DISPLAY_NAME(node));
        diagnostic_warning_message(checker->diag, "W1004", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Check for type keyword used as value: mut x = int */
    if (node->data.var_decl.value && node->data.var_decl.value->kind == NODE_LABEL) {
        const char *vname = node->data.var_decl.value->data.label.value;
        if (is_reserved_type_name(vname)) {
            diagnostic_error_code_formatted(checker->diag, "E3011", NODE_FILE(checker, node->data.var_decl.value), node->data.var_decl.value->token.line,
                node->data.var_decl.value->token.column, 0, vname, vname);
        }
    }

    /* E3131: file-scope and struct-scope const of primitive types must
     * have an explicit type annotation.  Struct instances and func
     * references are exempt because the type is visible in the
     * expression.  Arrays/maps are already caught by E3050/E3051. */
    if (!node->data.var_decl.mutable && !node->data.var_decl.type_name &&
        node->data.var_decl.value &&
        (checker->func_depth == 0 || checker->current_struct_name != NULL) &&
        strncmp(node->data.var_decl.name, GRAY_SYNTH_TMP, sizeof(GRAY_SYNTH_TMP) - 1) != 0 &&
        strncmp(node->data.var_decl.name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) != 0 &&
        node->data.var_decl.value->kind != NODE_STRUCT_VALUE &&
        node->data.var_decl.value->kind != NODE_FUNC_REF &&
        node->data.var_decl.value->kind != NODE_ARRAY_VALUE &&
        node->data.var_decl.value->kind != NODE_MAP_VALUE) {
        const char *suggested = "<type>";
        switch (node->data.var_decl.value->kind) {
        case NODE_INT_VALUE:    suggested = "int";    break;
        case NODE_FLOAT_VALUE:  suggested = "float";  break;
        case NODE_STRING_VALUE: /* fall through */
        case NODE_INTERPOLATED_STRING: suggested = "string"; break;
        case NODE_CHAR_VALUE:   suggested = "char";   break;
        case NODE_BOOL_VALUE:   suggested = "bool";   break;
        default: break;
        }
        diagnostic_error_code_formatted(checker->diag, "E3131",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            VAR_DISPLAY_NAME(node), suggested);
    }

    /* E3054: mutable array with fixed size */
    /* E3055: const array without fixed size */
    if (node->data.var_decl.type_name && node->data.var_decl.type_name[0] == '[') {
        const char *type_name_str = node->data.var_decl.type_name;
        /* Top-level comma only; commas inside (), [], or func sigs are
         * part of the element type, not the [T,N] size separator. */
        const char *size_comma = NULL;
        int depth = 0;
        for (const char *c = type_name_str; *c; c++) {
            if (*c == '(' || *c == '[') depth++;
            else if (*c == ')' || *c == ']') depth--;
            else if (*c == ',' && depth == 1) { size_comma = c; break; }
        }
        bool has_size = size_comma != NULL;
        /* Resolve const identifier sizes (e.g. "[int,SIZE]" → "[int,5]")
         * before the mut/const checks so downstream code always sees
         * numeric type strings. */
        if (has_size) {
            typechecker_resolve_array_size(checker, node);
            /* Re-read type_name — typechecker_resolve_array_size may have rewritten it. */
            type_name_str = node->data.var_decl.type_name;
        }
        if (node->data.var_decl.mutable && has_size) {
            char *msg = typechecker_format(checker, "mutable array '%s' cannot have a fixed size '%.*s'",
                VAR_DISPLAY_NAME(node), (int)(size_comma - type_name_str), type_name_str);
            diagnostic_error_help(checker->diag, "E3054", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                "use 'const' for fixed-size arrays, or remove the size for a dynamic 'mut' array");
        } else if (!node->data.var_decl.mutable && !has_size) {
            char *msg = typechecker_format(checker, "const array '%s' of type [%.*s] must have a fixed size",
                VAR_DISPLAY_NAME(node), (int)(strlen(type_name_str) - 2), type_name_str + 1);
            diagnostic_error_help(checker->diag, "E3055", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                "add a size: const name [type, N] = {...}, or use 'mut' for a dynamic array");
        }
    }

    /* E4029: private not allowed inside functions */
    if (node->data.var_decl.is_private && checker->func_depth > 0) {
        diagnostic_error_message(checker->diag, "E4029",
            "'private' cannot be used inside a function; it only applies to top-level declarations",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }

    GrayType *declared = node->data.var_decl.type_name
        ? typechecker_type_from_name(checker, node->data.var_decl.type_name)
        : &TYPE_UNKNOWN;
    /* E4016: explicitly annotated type name that doesn't exist, at any depth
     * — the element of an array, either half of a map, a pointee. */
    {
        char leaf[MSG_BUF_SIZE];
        const char *undefined = undefined_type_leaf(checker,
            node->data.var_decl.type_name, leaf, sizeof(leaf));
        if (undefined) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "undefined type '%s'; check the spelling or import the module that defines it",
                unqualified_display_name(undefined));
            diagnostic_error_message(checker->diag, "E4016", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* E4021/E4015: annotated type is private to another file */
    reject_private_type(checker, node, node->data.var_decl.type_name);
    reject_error_in_container(checker, node, node->data.var_decl.type_name);
    typechecker_mark_type_module_used(checker, node->data.var_decl.type_name);

    /* E3057: reject composite types as map keys before downstream checks
     * produce misleading cascades (e.g. struct-literal-in-index-position
     * tripping "no field 'y'"). Enums are allowed; they're int-backed
     * and hash fine. */
    if (declared->kind == TK_MAP && declared->key_type) {
        const char *key_type_name = resolve_type_alias(checker, declared->key_type);
        GrayType *key_resolved = type_from_name(key_type_name);
        const char *bad = NULL;
        if (key_resolved->kind == TK_STRUCT && !is_enum_name(checker, key_type_name))
            bad = "struct";
        else if (key_resolved->kind == TK_ARRAY) bad = "array";
        else if (key_resolved->kind == TK_MAP) bad = "map";
        else if (key_resolved->kind == TK_POINTER) bad = "pointer";
        if (bad) {
            diagnostic_error_code_formatted(checker->diag, "E3057", NODE_FILE(checker, node), node->token.line, node->token.column, 0, key_type_name);
        }
    }

    if (node->data.var_decl.value) {
        /* Set expected_type for implicit enum resolution (.VARIANT) */
        GrayType *saved_expected = checker->expected_type;
        if (declared->kind == TK_ENUM && declared->name)
            checker->expected_type = declared;
        else if (declared->kind == TK_ARRAY && declared->element_type) {
            checker->expected_type = declared;
        } else if (declared->kind == TK_MAP && declared->value_type) {
            GrayType *val_t = typechecker_type_from_name(checker, declared->value_type);
            if (val_t && val_t->kind == TK_ENUM)
                checker->expected_type = declared;
        }
        /* A file-scope initializer is emitted where it is written, so it can
         * only name declarations already in scope at that point. Every earlier
         * file-scope declaration has been defined by the walk that got here, so
         * a name the scope does not hold is one declared further down — and
         * the module registry alone must not vouch for it, or the reference
         * reaches the C compiler as an undeclared identifier. Function bodies
         * are exempt: codegen emits file-scope declarations ahead of them. */
        bool saved_file_scope_init = checker->in_file_scope_init;
        if (checker->func_depth == 0) checker->in_file_scope_init = true;
        GrayType *value_type = resolve_expression(checker, node->data.var_decl.value);
        checker->in_file_scope_init = saved_file_scope_init;
        checker->expected_type = saved_expected;

        /* E3050/E3051: an array or map literal with no type annotation needs
         * one — unless this is a `mut` declaration whose literal is built only
         * from primitives, in which case the element (and, for a map, key and
         * value) types are inferred from the literal (issue #2374). Empty
         * literals and non-primitive elements stay rejected, as do `const`
         * declarations. */
        if (!node->data.var_decl.type_name &&
            strncmp(node->data.var_decl.name, GRAY_SYNTH_TMP, sizeof(GRAY_SYNTH_TMP) - 1) != 0 &&
            strncmp(node->data.var_decl.name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) != 0) {
            AstNode *lit = node->data.var_decl.value;
            bool is_array = lit->kind == NODE_ARRAY_VALUE;
            bool is_map = lit->kind == NODE_MAP_VALUE;
            bool inferred = node->data.var_decl.mutable &&
                typechecker_literal_type_inferable(checker, lit, value_type);
            if ((is_array || is_map) && !inferred) {
                diagnostic_error_code_help(checker->diag, is_array ? "E3050" : "E3051",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    is_array ? "add a type annotation, e.g. 'mut x [int] = {1, 2, 3}'"
                             : "add a type annotation, e.g. 'mut x [string:int] = {\"a\": 1}'");
            } else if (is_map && inferred) {
                /* The inferred map type is taken from the first pair only
                 * (typechecker_literal_type_inferable / NODE_MAP_VALUE). Every
                 * later pair must agree with it: a non-primitive key or value
                 * falls back to E3051, a mismatched primitive gets the same
                 * E3053 the annotated path emits (issue #2374). */
                GrayType *ik = typechecker_type_from_name(checker, value_type->key_type);
                GrayType *iv = typechecker_type_from_name(checker, value_type->value_type);
                for (int mi = 1; mi < lit->data.map_value.count; mi++) {
                    AstNode *kn = lit->data.map_value.keys[mi];
                    AstNode *vn = lit->data.map_value.values[mi];
                    GrayType *kt = resolve_expression(checker, kn);
                    GrayType *vt = resolve_expression(checker, vn);
                    if ((kt && !typechecker_kind_is_primitive(kt->kind)) ||
                        (vt && !typechecker_kind_is_primitive(vt->kind))) {
                        diagnostic_error_code_help(checker->diag, "E3051",
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                            "add a type annotation, e.g. 'mut x [string:int] = {\"a\": 1}'");
                        break;
                    }
                    if (kt && ik && kt->kind != TK_UNKNOWN && ik->kind != TK_UNKNOWN &&
                        !types_assignable(checker, ik, kt)) {
                        char *msg = typechecker_format(checker,
                            "type mismatch in map literal key; expected '%s', got '%s'",
                            type_display_name(checker, ik), type_display_name(checker, kt));
                        diagnostic_error_message(checker->diag, "E3053", msg,
                            NODE_FILE(checker, kn), kn->token.line, kn->token.column, 0);
                    }
                    if (vt && iv && vt->kind != TK_UNKNOWN && iv->kind != TK_UNKNOWN &&
                        !types_assignable(checker, iv, vt)) {
                        char *msg = typechecker_format(checker,
                            "type mismatch in map literal value; expected '%s', got '%s'",
                            type_display_name(checker, iv), type_display_name(checker, vt));
                        diagnostic_error_message(checker->diag, "E3053", msg,
                            NODE_FILE(checker, vn), vn->token.line, vn->token.column, 0);
                    }
                }
            }
        }
        /* : when a func-pointer call returns TK_UNKNOWN but
         * the assignment target has a concrete declared type,
         * push the declared type onto the call node's typetable
         * entry so codegen can derive the correct function-pointer
         * return cast instead of defaulting to int64_t. */
        if (value_type->kind == TK_UNKNOWN && declared->kind != TK_UNKNOWN &&
            declared->kind != TK_VOID &&
            node->data.var_decl.value->kind == NODE_CALL_EXPR) {
            typetable_set(checker->type_table, node->data.var_decl.value, declared);
            value_type = declared;
        }
        /* E3038: cannot assign void function result */
        if (value_type->kind == TK_VOID) {
            diagnostic_error_message(checker->diag, "E3038",
                "cannot assign the result of a void function to a variable",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* E3102: cannot assign a func-type return value to a variable.
         * Func references must be created with ()func_name or ref(func_name).
         * Skip ref() itself — it is the canonical way to create a func reference. */
        if (value_type->kind == TK_FUNCTION &&
            node->data.var_decl.value->kind == NODE_CALL_EXPR &&
            !(node->data.var_decl.value->data.call.function->kind == NODE_LABEL &&
              strcmp(node->data.var_decl.value->data.call.function->data.label.value, "ref") == 0)) {
            AstNode *call_fn = node->data.var_decl.value->data.call.function;
            const char *called = "function";
            char called_buf[MSG_BUF_SIZE];
            const char *called_qualifier = ast_member_qualifier(call_fn);
            if (call_fn->kind == NODE_LABEL) {
                called = call_fn->data.label.value;
            } else if (called_qualifier) {
                snprintf(called_buf, sizeof(called_buf), "%s.%s",
                    called_qualifier, call_fn->data.member.member);
                called = called_buf;
            }
            char *msg = NULL;
            msg = typechecker_format(checker,
                "function '%s' returns a func type; func references cannot be assigned from function return values. Use '()func_name' or 'ref(func_name)' to create a func reference",
                called);
            diagnostic_error_message(checker->diag, "E3102", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* Check for multi-return to single variable
         * (skip if this is part of a multi-var expansion; the value will be a .v0 access) */
        if (node->data.var_decl.value->kind == NODE_CALL_EXPR &&
            strncmp(node->data.var_decl.name, GRAY_SYNTH_TMP, sizeof(GRAY_SYNTH_TMP) - 1) != 0 &&
            strncmp(node->data.var_decl.name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) != 0) {
            reject_multi_value_call(checker, node->data.var_decl.value, node);
        }
        /* Reject nil on non-nullable types */
        if (value_type->kind == TK_NIL && declared->kind != TK_UNKNOWN &&
            declared->kind != TK_ERROR && declared->kind != TK_POINTER &&
            declared->kind != TK_NIL) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot assign nil to '%s'; only Error and pointer types are nullable",
                type_name(declared));
            diagnostic_error_message(checker->diag, "E3157", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* Reject bare 'mut x = nil' with no type context */
        if (value_type->kind == TK_NIL && declared->kind == TK_UNKNOWN) {
            diagnostic_error_message(checker->diag, "E3157",
                "cannot infer type from 'nil'; add a type annotation (e.g., 'mut x Error = nil')",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* E3066: typed-func variable assigned a function reference with a
         * different signature. Both sides are TK_FUNCTION; the canonical
         * encoded names (e.g. "func(int)->int") must match exactly. */
        if (func_types_mismatch(declared, value_type)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot assign %s to variable of type %s",
                type_display_name(checker, value_type), type_display_name(checker, declared));
            diagnostic_error_message(checker->diag, "E3066", msg,
                NODE_FILE(checker, node->data.var_decl.value),
                node->data.var_decl.value->token.line,
                node->data.var_decl.value->token.column, 0);
        }
        /* E3001: \`f func = expr\` requires expr to be a function reference.
         * The generic mismatch check below would catch it too, but only to
         * say the kinds differ; naming the reference form is what the reader
         * needs, so this reports first and suppresses that one. */
        bool func_decl_reported = false;
        if (node->data.var_decl.type_name &&
            strcmp(node->data.var_decl.type_name, "func") == 0 &&
            node->data.var_decl.value) {
            AstNode *v = node->data.var_decl.value;
            bool value_is_func =
                v->kind == NODE_FUNC_REF ||
                value_type->kind == TK_NIL ||
                (value_type->name && strcmp(value_type->name, "func") == 0);
            if (!value_is_func) {
                char *msg = NULL;
                /* If the initializer is a direct call, point the user
                 * at the reference form of the same name; that's
                 * overwhelmingly what they meant. */
                if (v->kind == NODE_CALL_EXPR &&
                    v->data.call.function &&
                    v->data.call.function->kind == NODE_LABEL) {
                    const char *called = v->data.call.function->data.label.value;
                    msg = typechecker_format(checker,
                        "cannot assign %s to 'func'; to store a reference to '%s', use '()%s' (not '%s()')",
                        type_name(value_type), called, called, called);
                } else {
                    msg = typechecker_format(checker,
                        "cannot assign %s to 'func'; func variables hold function references (e.g. '()name')",
                        type_name(value_type));
                }
                tc_err_assign_type(checker, node, msg);
                func_decl_reported = true;
            }
        }
        /* If no declared type, infer from value */
        if (declared->kind == TK_UNKNOWN) {
            declared = value_type;
        } else if (!func_decl_reported &&
                   value_type->kind != TK_UNKNOWN &&
                   value_type->kind != TK_VOID &&
                   value_type->kind != TK_NIL &&
                   !types_assignable(checker, declared, value_type) &&
                   /* Skip mismatch when assigning ref var to ^T pointer */
                   !(declared->kind == TK_POINTER && node->data.var_decl.value &&
                     node->data.var_decl.value->kind == NODE_LABEL &&
                     scope_lookup(checker->current_scope, node->data.var_decl.value->data.label.value) &&
                     scope_lookup(checker->current_scope, node->data.var_decl.value->data.label.value)->is_ref) &&
                   /* Skip mismatch when assigning pointer (addr) to ^T */
                   !(declared->kind == TK_POINTER && value_type->kind == TK_POINTER)) {
            /* Type mismatch */
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type mismatch: cannot assign %s to %s",
                type_display_name(checker, value_type), type_display_name(checker, declared));
            tc_err_assign_type(checker, node, msg);
        }
        /* Pointer-to-pointer: pointee types differ (e.g., ^int assigned from ^string).
         * The outer kind-mismatch guard above short-circuits when both sides are TK_POINTER,
         * so this separate check is required to catch it. Mirrors the call-site check. */
        if (declared && value_type &&
            declared->kind == TK_POINTER && value_type->kind == TK_POINTER &&
            declared->name && value_type->name &&
            strcmp(declared->name, value_type->name) != 0) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type mismatch: cannot assign %s to %s",
                type_display_name(checker, value_type), type_display_name(checker, declared));
            tc_err_assign_type(checker, node, msg);
        }
        /* Bigint narrowing: e.g. i128 → i64, u256 → int.  Both sides share
         * TK_INT/TK_UINT so the kind-equality guard above silently passes
         * them through.  Catch it here by comparing named ranks. */
        if (declared && value_type &&
            declared->name && value_type->name) {
            int declared_rank = int_type_name_rank(declared->name);
            int value_rank = int_type_name_rank(value_type->name);
            if (declared_rank > 0 && value_rank >= 5 && declared_rank < value_rank) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "type mismatch: cannot implicitly narrow %s to %s; use cast(value, %s) to convert explicitly",
                    value_type->name, declared->name, declared->name);
                diagnostic_error_message(checker->diag, "E3155", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        /* Struct-to-struct name mismatch (both TK_STRUCT but different names).
         * : skip when one name is a module-prefixed alias of the
         * other (e.g. "Point" vs "shapes_Point" via import and use). */
        bool struct_alias_match = false;
        if (declared->kind == TK_STRUCT && value_type->kind == TK_STRUCT &&
            declared->name && value_type->name) {
            const char *declared_name = declared->name;
            const char *value_name = value_type->name;
            const char *d_us = strrchr(declared_name, '_');
            const char *v_us = strrchr(value_name, '_');
            if (d_us && strcmp(d_us + 1, value_name) == 0) struct_alias_match = true;
            if (v_us && strcmp(v_us + 1, declared_name) == 0) struct_alias_match = true;
        }
        if (declared->kind == TK_STRUCT && value_type->kind == TK_STRUCT &&
            declared->name && value_type->name &&
            strcmp(declared->name, value_type->name) != 0 &&
            !struct_alias_match) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type mismatch: cannot assign '%s' to '%s'",
                type_display_name(checker, value_type), type_display_name(checker, declared));
            tc_err_assign_type(checker, node, msg);
        }
        /* Enum-to-enum name mismatch (both TK_ENUM but different enum types) */
        if (declared->kind == TK_ENUM && value_type->kind == TK_ENUM &&
            declared->name && value_type->name &&
            !typechecker_same_enum_type(checker, declared->name, value_type->name)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type mismatch: cannot assign enum '%s' to enum '%s'",
                type_display_name(checker, value_type), type_display_name(checker, declared));
            tc_err_assign_type(checker, node, msg);
        }
        /* Array element type mismatch (both TK_ARRAY but different element types) */
        if (declared->kind == TK_ARRAY && value_type->kind == TK_ARRAY &&
            declared->element_type && value_type->element_type &&
            !typechecker_same_array_element(checker, declared->element_type, value_type->element_type)) {
            GrayType *decl_elem = type_from_name(declared->element_type);
            GrayType *val_elem  = type_from_name(value_type->element_type);
            /* Allow int-kind ↔ int-kind, int→float, and skip when either
             * element type is opaque/unknown (e.g. generic stdlib returns) */
            /* Skip function-type arrays: signature strings differ by whitespace */
            bool elem_is_func = strncmp(declared->element_type, "func", 4) == 0;
            if (!elem_is_func && decl_elem && val_elem &&
                decl_elem->kind != TK_UNKNOWN && val_elem->kind != TK_UNKNOWN &&
                !(is_int_kind(decl_elem->kind) && is_int_kind(val_elem->kind)) &&
                !(is_int_kind(decl_elem->kind) && val_elem->kind == TK_STRUCT) &&
                !(decl_elem->kind == TK_FLOAT && is_int_kind(val_elem->kind)) &&
                /* float ↔ f32 ↔ f64 array element coercion, mirroring the
                 * scalar path (`mut x f32 = someFloat` is allowed). */
                !(decl_elem->kind == TK_FLOAT && val_elem->kind == TK_FLOAT)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "type mismatch: cannot assign '%s' to '%s'",
                    type_display_name(checker, value_type), type_display_name(checker, declared));
                tc_err_assign_type(checker, node, msg);
            }
        }
        /* Map key/value type mismatch (both TK_MAP but different key or value types) */
        if (declared->kind == TK_MAP && value_type->kind == TK_MAP) {
            bool key_mismatch = declared->key_type && value_type->key_type &&
                strcmp(declared->key_type, value_type->key_type) != 0;
            bool val_mismatch = declared->value_type && value_type->value_type &&
                strcmp(declared->value_type, value_type->value_type) != 0;
            /* Suppress key/value mismatches caused by int-kind coercion or float coercion */
            if (key_mismatch) {
                GrayType *dk = type_from_name(declared->key_type);
                GrayType *vk = type_from_name(value_type->key_type);
                if (dk && vk && ((is_int_kind(dk->kind) && is_int_kind(vk->kind)) ||
                                (dk->kind == TK_FLOAT && vk->kind == TK_FLOAT) ||
                                (dk->kind == TK_FLOAT && is_int_kind(vk->kind))))
                    key_mismatch = false;
            }
            if (val_mismatch) {
                GrayType *dv = type_from_name(declared->value_type);
                GrayType *vv = type_from_name(value_type->value_type);
                bool val_is_literal = node->data.var_decl.value &&
                    node->data.var_decl.value->kind == NODE_MAP_VALUE;
                if (dv && vv && ((is_int_kind(dv->kind) && is_int_kind(vv->kind)) ||
                                (dv->kind == TK_FLOAT && vv->kind == TK_FLOAT) ||
                                /* int→float coercion only for map literals, not variables */
                                (val_is_literal && dv->kind == TK_FLOAT && is_int_kind(vv->kind))))
                    val_mismatch = false;
            }
            if (key_mismatch || val_mismatch) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "type mismatch: cannot assign '%s' to '%s'",
                    type_display_name(checker, value_type), type_display_name(checker, declared));
                tc_err_assign_type(checker, node, msg);
            }
        }
        /* E3046: literal that exceeds the destination type's range.
         *   overflow_u64 = true  : exceeds UINT64_MAX, never fits a non-bigint
         *   overflow     = true  : exceeds INT64_MAX but fits in UINT64_MAX,
         *                         OK for uint/u64/bigint, error otherwise */
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_INT_VALUE &&
            node->data.var_decl.value->data.int_value.overflow) {
            const char *type_name_str = node->data.var_decl.type_name;
            bool is_bigint = type_name_str && (strcmp(type_name_str, "i128") == 0 || strcmp(type_name_str, "u128") == 0 ||
                                    strcmp(type_name_str, "i256") == 0 || strcmp(type_name_str, "u256") == 0);
            bool is_u64_like = type_name_str && (strcmp(type_name_str, "u64") == 0 || strcmp(type_name_str, "uint") == 0);
            bool exceeds_u64 = node->data.var_decl.value->data.int_value.overflow_u64;
            if (exceeds_u64 && !is_bigint) {
                diagnostic_error_message(checker->diag, "E3046",
                    "integer literal overflows 64-bit integer; max value is 18446744073709551615",
                    NODE_FILE(checker, node->data.var_decl.value), node->data.var_decl.value->token.line,
                    node->data.var_decl.value->token.column, 0);
            } else if (!exceeds_u64 && !is_bigint && !is_u64_like) {
                diagnostic_error_message(checker->diag, "E3046",
                    "integer literal overflows 64-bit integer; max value is 9223372036854775807",
                    NODE_FILE(checker, node->data.var_decl.value), node->data.var_decl.value->token.line,
                    node->data.var_decl.value->token.column, 0);
            }
        }
        /* E3036: Check literal value fits in sized integer type (skip overflowed literals) */
        if (node->data.var_decl.type_name && node->data.var_decl.value) {
            bool val_overflowed = (node->data.var_decl.value->kind == NODE_INT_VALUE &&
                node->data.var_decl.value->data.int_value.overflow);
            int64_t lit_val;
            bool lit_neg;
            if (!val_overflowed && try_get_signed_literal_int(node->data.var_decl.value, &lit_val, &lit_neg)) {
                check_integer_range(checker->diag, NODE_FILE(checker, node),
                    node->token.line, node->token.column,
                    node->data.var_decl.type_name, lit_val, lit_neg);
            }
            /* E3001 (): assigning an array literal `{}` to a map
             * variable falls through the normal type check because the
             * literal has no elements to derive a concrete element type
             * from, and codegen then emits gray_array_new which the C
             * compiler rejects. Point the user at the empty-map form
             * `{:}` before the rest of the var_decl check runs. */
            const char *type_name_str = node->data.var_decl.type_name;
            if (strncmp(type_name_str, "map[", 4) == 0 &&
                node->data.var_decl.value->kind == NODE_ARRAY_VALUE) {
                char *msg = NULL;
                if (node->data.var_decl.value->data.array_value.count == 0) {
                    msg = typechecker_format(checker,
                        "cannot assign array literal '{}' to '%s'; use '{:}' for an empty map",
                        type_name_str);
                } else {
                    msg = typechecker_format(checker,
                        "cannot assign array literal to '%s'; map literals use '{key: value, ...}' syntax",
                        type_name_str);
                }
                tc_err_assign_type(checker, node->data.var_decl.value, msg);
            }
            /* E3026/E3036: Check array literal elements fit in sized element type */
            if (type_name_str[0] == '[' && node->data.var_decl.value->kind == NODE_ARRAY_VALUE) {
                /* Extract element type name from "[byte]", "[i8]", "[u8, 3]", etc. */
                char elem_type[TYPE_NAME_MAX] = {0};
                const char *start = type_name_str + 1;
                /* Find the matching ']' for the outermost array bracket,
                 * skipping nested brackets (e.g. map[K:V], [T]). */
                const char *end = NULL;
                {
                    int depth = 0;
                    for (const char *p = start; *p; p++) {
                        if (*p == '[') depth++;
                        else if (*p == ']') {
                            if (depth == 0) { end = p; break; }
                            depth--;
                        }
                    }
                }
                /* Find the top-level comma (fixed-size separator), ignoring
                 * commas inside nested brackets. */
                const char *comma = NULL;
                {
                    int depth = 0;
                    for (const char *p = start; p < (end ? end : start + strlen(start)); p++) {
                        if (*p == '[') depth++;
                        else if (*p == ']') depth--;
                        else if (*p == ',' && depth == 0) { comma = p; break; }
                    }
                }
                if (end) {
                    int elen = (int)((comma && comma < end ? comma : end) - start);
                    if (elen > 0 && elen < (int)sizeof(elem_type)) {
                        /* trim whitespace */
                        while (elen > 0 && start[elen-1] == ' ') elen--;
                        memcpy(elem_type, start, (size_t)elen);
                        elem_type[elen] = '\0';
                    }
                }
                if (elem_type[0] && !is_bigint_type(elem_type)) {
                    AstNode *arr = node->data.var_decl.value;
                    bool elem_is_u64_like = (strcmp(elem_type, "uint") == 0 || strcmp(elem_type, "u64") == 0);
                    for (int enum_index = 0; enum_index < arr->data.array_value.count; enum_index++) {
                        AstNode *el = arr->data.array_value.elements[enum_index];
                        bool el_overflowed = (el->kind == NODE_INT_VALUE && el->data.int_value.overflow);
                        bool el_overflowed_u64 = (el->kind == NODE_INT_VALUE && el->data.int_value.overflow_u64);
                        /* Element exceeds UINT64_MAX entirely; always an error. */
                        if (el_overflowed_u64) {
                            diagnostic_error_message(checker->diag, "E3046",
                                "integer literal overflows 64-bit integer; max value is 18446744073709551615",
                                NODE_FILE(checker, el), el->token.line, el->token.column, 0);
                            continue;
                        }
                        /* Element exceeds INT64_MAX but fits UINT64_MAX —
                         * fine for u64/uint elements, error for narrower
                         * signed/unsigned and for int. */
                        if (el_overflowed) {
                            if (!elem_is_u64_like) {
                                diagnostic_error_message(checker->diag, "E3046",
                                    "integer literal overflows 64-bit integer; max value is 9223372036854775807",
                                    NODE_FILE(checker, el), el->token.line, el->token.column, 0);
                            }
                            continue;
                        }
                        int64_t ev;
                        bool ev_neg;
                        if (try_get_signed_literal_int(el, &ev, &ev_neg)) {
                            check_integer_range(checker->diag, NODE_FILE(checker, el),
                                el->token.line, el->token.column,
                                elem_type, ev, ev_neg);
                        }
                    }
                }
                /* E3053: element type mismatch in array initializer */
                if (elem_type[0]) {
                    GrayType *expected_et = typechecker_type_from_name(checker, elem_type);
                    AstNode *arr = node->data.var_decl.value;
                    for (int enum_index = 0; enum_index < arr->data.array_value.count; enum_index++) {
                        AstNode *el_node = arr->data.array_value.elements[enum_index];
                        GrayType *actual_et = resolve_expression(checker, el_node);
                        /* E3019: an array element that crosses signedness needs a cast. */
                        check_signedness_crossing(checker, elem_type,
                            el_node, actual_et, el_node);
                        if (actual_et && actual_et->kind != TK_UNKNOWN &&
                            expected_et && expected_et->kind != TK_UNKNOWN &&
                            actual_et->kind != expected_et->kind) {
                            bool compatible = types_assignable(checker, expected_et, actual_et) ||
                                (expected_et->kind == TK_ENUM && is_int_kind(actual_et->kind));
                            if (!compatible) {
                                diagnostic_error_code_formatted(checker->diag, "E3053", NODE_FILE(checker, arr->data.array_value.elements[enum_index]),
                                    arr->data.array_value.elements[enum_index]->token.line,
                                    arr->data.array_value.elements[enum_index]->token.column, 0,
                                    type_display_name(checker, expected_et),
                                    type_display_name(checker, actual_et));
                            }
                        }
                        /* E3053: cross-enum mismatch — both are TK_ENUM but
                         * from different enum types (e.g. Color vs Dir).
                         * The kind-level check above passes since both are
                         * TK_ENUM, so we need a name-level comparison.
                         * Use display-name comparison so cross-module
                         * aliases (e.g. types_Status vs Status) unify. */
                        if (actual_et && expected_et &&
                            actual_et->kind == TK_ENUM && expected_et->kind == TK_ENUM &&
                            actual_et->name && expected_et->name &&
                            !typechecker_same_enum_type(checker, actual_et->name, expected_et->name)) {
                            diagnostic_error_code_formatted(checker->diag, "E3053", NODE_FILE(checker, arr->data.array_value.elements[enum_index]),
                                arr->data.array_value.elements[enum_index]->token.line,
                                arr->data.array_value.elements[enum_index]->token.column, 0,
                                type_display_name(checker, expected_et),
                                type_display_name(checker, actual_et));
                        }
                        /* E3053: cross-pointer mismatch — both are TK_POINTER but
                         * point to different types (e.g. ^int vs ^float). */
                        if (actual_et && expected_et &&
                            actual_et->kind == TK_POINTER && expected_et->kind == TK_POINTER &&
                            actual_et->element_type && expected_et->element_type &&
                            strcmp(actual_et->element_type, expected_et->element_type) != 0) {
                            diagnostic_error_code_formatted(checker->diag, "E3053", NODE_FILE(checker, arr->data.array_value.elements[enum_index]),
                                arr->data.array_value.elements[enum_index]->token.line,
                                arr->data.array_value.elements[enum_index]->token.column, 0,
                                type_display_name(checker, expected_et), type_display_name(checker, actual_et));
                        }
                    }
                }
                /* W3003/E3052: fixed-size array initialization count checks */
                if (comma && comma < end) {
                    int fixed_size = atoi(comma + 1);
                    AstNode *arr = node->data.var_decl.value;
                    if (fixed_size > 0 && arr->data.array_value.count > fixed_size) {
                        diagnostic_error_code_formatted(checker->diag, "E3052", NODE_FILE(checker, node), node->token.line, node->token.column, 0, fixed_size, arr->data.array_value.count);
                    }
                    if (fixed_size > 0 && arr->data.array_value.count < fixed_size) {
                        char *msg = NULL;
                        msg = typechecker_format(checker,
                            "fixed-size array [%s, %d] initialized with only %d of %d elements; remaining will be zero-valued",
                            elem_type[0] ? elem_type : "?", fixed_size,
                            arr->data.array_value.count, fixed_size);
                        diagnostic_warning_message(checker->diag, "W3003", msg,
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                    }
                }
            }
            /* : map literal key/value type mismatch. Parallel
             * to the array E3053 check above; walks NODE_MAP_VALUE
             * pairs and rejects entries whose key or value type
             * doesn't match the declared map's K/V. The void case is
             * already caught in NODE_MAP_VALUE's
             * reject_void_in_context path (); this block
             * covers the non-void-but-wrong-type leak. */
            if (strncmp(type_name_str, "map[", 4) == 0 &&
                node->data.var_decl.value->kind == NODE_MAP_VALUE) {
                const char *mstart = type_name_str + 4;
                const char *mcolon = strchr(mstart, ':');
                const char *mend = strrchr(type_name_str, ']');
                if (mcolon && mend && mend > mcolon) {
                    char key_tn[TYPE_NAME_MAX] = {0};
                    char val_tn[TYPE_NAME_MAX] = {0};
                    size_t klen = (size_t)(mcolon - mstart);
                    size_t vlen = (size_t)(mend - mcolon - 1);
                    if (klen > 0 && klen < sizeof(key_tn) &&
                        vlen > 0 && vlen < sizeof(val_tn)) {
                        memcpy(key_tn, mstart, klen);
                        key_tn[klen] = '\0';
                        memcpy(val_tn, mcolon + 1, vlen);
                        val_tn[vlen] = '\0';
                        GrayType *expected_k = typechecker_type_from_name(checker, key_tn);
                        GrayType *expected_v = typechecker_type_from_name(checker, val_tn);
                        AstNode *mv = node->data.var_decl.value;
                        for (int mi = 0; mi < mv->data.map_value.count; mi++) {
                            AstNode *kn = mv->data.map_value.keys[mi];
                            AstNode *vn = mv->data.map_value.values[mi];
                            GrayType *kt = resolve_expression(checker, kn);
                            GrayType *vt = resolve_expression(checker, vn);
                            /* E3019: a map key or value that crosses signedness needs a cast. */
                            check_signedness_crossing(checker, key_tn, kn, kt, kn);
                            check_signedness_crossing(checker, val_tn, vn, vt, vn);
                            /* E3036: an out-of-range literal key or value ({"a": 300}). */
                            int64_t kv_lit;
                            bool kv_neg;
                            if (try_get_signed_literal_int(kn, &kv_lit, &kv_neg))
                                check_integer_range(checker->diag, NODE_FILE(checker, kn),
                                    kn->token.line, kn->token.column, key_tn, kv_lit, kv_neg);
                            if (try_get_signed_literal_int(vn, &kv_lit, &kv_neg))
                                check_integer_range(checker->diag, NODE_FILE(checker, vn),
                                    vn->token.line, vn->token.column, val_tn, kv_lit, kv_neg);
                            if (kt && kt->kind != TK_UNKNOWN && kt->kind != TK_VOID &&
                                expected_k && expected_k->kind != TK_UNKNOWN &&
                                !types_assignable(checker, expected_k, kt) &&
                                !(expected_k->kind == TK_ENUM && is_int_kind(kt->kind))) {
                                char *msg = NULL;
                                msg = typechecker_format(checker,
                                    "type mismatch in map literal key; expected '%s', got '%s'",
                                    type_display_name(checker, expected_k), type_display_name(checker, kt));
                                diagnostic_error_message(checker->diag, "E3053", msg,
                                    NODE_FILE(checker, kn), kn->token.line, kn->token.column, 0);
                            }
                            /* Enum-to-enum: key types both TK_ENUM but different names */
                            if (expected_k && kt &&
                                expected_k->kind == TK_ENUM && kt->kind == TK_ENUM &&
                                expected_k->name && kt->name &&
                                !typechecker_same_enum_type(checker, expected_k->name, kt->name)) {
                                char *msg = NULL;
                                msg = typechecker_format(checker,
                                    "type mismatch in map literal key; expected enum '%s', got enum '%s'",
                                    type_display_name(checker, expected_k), type_display_name(checker, kt));
                                diagnostic_error_message(checker->diag, "E3053", msg,
                                    NODE_FILE(checker, kn), kn->token.line, kn->token.column, 0);
                            }
                            if (vt && vt->kind != TK_UNKNOWN && vt->kind != TK_VOID &&
                                expected_v && expected_v->kind != TK_UNKNOWN &&
                                !types_assignable(checker, expected_v, vt) &&
                                !(expected_v->kind == TK_ENUM && is_int_kind(vt->kind)) &&
                                !(expected_v->kind == TK_POINTER && vt->kind == TK_POINTER)) {
                                char *msg = NULL;
                                msg = typechecker_format(checker,
                                    "type mismatch in map literal value; expected '%s', got '%s'",
                                    type_display_name(checker, expected_v), type_display_name(checker, vt));
                                diagnostic_error_message(checker->diag, "E3053", msg,
                                    NODE_FILE(checker, vn), vn->token.line, vn->token.column, 0);
                            }
                            /* Enum-to-enum: value types both TK_ENUM but different names */
                            if (expected_v && vt &&
                                expected_v->kind == TK_ENUM && vt->kind == TK_ENUM &&
                                expected_v->name && vt->name &&
                                !typechecker_same_enum_type(checker, expected_v->name, vt->name)) {
                                char *msg = NULL;
                                msg = typechecker_format(checker,
                                    "type mismatch in map literal value; expected enum '%s', got enum '%s'",
                                    type_display_name(checker, expected_v), type_display_name(checker, vt));
                                diagnostic_error_message(checker->diag, "E3053", msg,
                                    NODE_FILE(checker, vn), vn->token.line, vn->token.column, 0);
                            }
                            /* Pointer-to-pointer: pointee types differ in map literal value */
                            if (expected_v && vt &&
                                expected_v->kind == TK_POINTER && vt->kind == TK_POINTER &&
                                expected_v->name && vt->name &&
                                strcmp(expected_v->name, vt->name) != 0) {
                                char *msg = NULL;
                                msg = typechecker_format(checker,
                                    "type mismatch in map literal value; expected '%s', got '%s'",
                                    type_display_name(checker, expected_v), type_display_name(checker, vt));
                                diagnostic_error_message(checker->diag, "E3053", msg,
                                    NODE_FILE(checker, vn), vn->token.line, vn->token.column, 0);
                            }
                        }
                    }
                }
            }
        }
        /* E3019: a declared integer type crossed by the initializer variable's
         * signedness (int x = uint_var, uint x = int_var) needs an explicit cast. */
        if (node->data.var_decl.type_name &&
            node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_LABEL) {
            const char *dest_tn = node->data.var_decl.type_name;
            const char *src_name = node->data.var_decl.value->data.label.value;
            Symbol *src_sym = scope_lookup(checker->current_scope, src_name);
            const char *src_tn = src_sym ? src_sym->declared_type : NULL;
            if (src_tn && is_unsigned_type(dest_tn) && is_signed_int_type(src_tn)) {
                diagnostic_error_code_formatted(checker->diag, "E3019", NODE_FILE(checker, node), node->token.line, node->token.column, 0, src_tn, dest_tn);
            } else if (src_tn && is_signed_int_type(dest_tn) && is_unsigned_type(src_tn) &&
                       !unsigned_widens_to_signed(dest_tn, src_tn)) {
                diagnostic_error_message(checker->diag, "E3019",
                    typechecker_format(checker,
                        "type mismatch: cannot assign unsigned type '%s' to signed type '%s' variable '%s'; use cast(value, %s) to convert explicitly",
                        src_tn, dest_tn, node->data.var_decl.name, dest_tn),
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        } else if (node->data.var_decl.type_name && node->data.var_decl.value &&
                   value_type && value_type->kind != TK_UNKNOWN) {
            /* A non-label initializer (function call, or an expression whose
             * top-level type is a concrete integer) crosses signedness the
             * same way — the NODE_LABEL branch above only covers a bare
             * variable RHS. Literals are range-checked separately and are
             * skipped by check_signedness_crossing. */
            check_signedness_crossing(checker, node->data.var_decl.type_name,
                node->data.var_decl.value, value_type, node->data.var_decl.value);
        }
    }

    /* E3062 (): handle types (channels, mutexes, threads)
     * cannot be declared const; every meaningful operation on
     * them mutates internal state, so const is a semantic lie.
     * Same class as the E3059 map check above. */
    if (!node->data.var_decl.mutable && declared->kind == TK_STRUCT && declared->name) {
        const char *declared_name = declared->name;
        const char *handle_label = NULL;
        if (strcmp(declared_name, "Channel") == 0) handle_label = "channel";
        else if (strcmp(declared_name, "Mutex") == 0) handle_label = "mutex";
        else if (strcmp(declared_name, "Thread") == 0) handle_label = "thread handle";
        else if (strcmp(declared_name, "Builder") == 0) handle_label = "string builder";
        if (handle_label) {
            diagnostic_error_code_formatted(checker->diag, "E3062", NODE_FILE(checker, node), node->token.line, node->token.column, 0, handle_label, handle_label);
        }
    }

    /* W1005: typed blank identifier; _ with explicit type annotation */
    if (strcmp(node->data.var_decl.name, "_") == 0 && node->data.var_decl.type_name) {
        diagnostic_warning_code(checker->diag, "W1005", NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }

    if (strcmp(node->data.var_decl.name, "_") != 0) {
        /* Check for reserved prefix (the parser's own temps are exempt) */
        if (!node->data.var_decl.synthetic) {
            check_reserved_name(checker, node->data.var_decl.name,
                NODE_FILE(checker, node), node->token.line, node->token.column);
        }
        /* Check for redeclaration in same scope */
        Symbol *existing = scope_lookup_local(checker->current_scope,
            node->data.var_decl.name);
        if (existing && existing->def_line != 0) {
            /* def_line == 0 means this was pre-registered in Pass 1.5
             * to allow forward references between global constants;
             * that is not a duplicate declaration. */
            diagnostic_error_code_formatted(checker->diag, "E4003", NODE_FILE(checker, node), node->token.line, node->token.column, 0, VAR_DISPLAY_NAME(node), existing->def_line);
        }
        /* W2002/W2007: check if variable shadows outer scope */
        if (!existing && checker->current_scope->parent) {
            Symbol *outer_sym = scope_lookup(checker->current_scope->parent,
                node->data.var_decl.name);
            if (outer_sym && outer_sym->def_line > 0) {
                /* Check if it's a global (file-scope) variable */
                Scope *outer_scope = checker->current_scope->parent;
                while (outer_scope->parent) {
                    Symbol *s = scope_lookup_local(outer_scope, node->data.var_decl.name);
                    if (s) break;
                    outer_scope = outer_scope->parent;
                }
                bool is_global = (outer_scope->parent == NULL);
                char *msg = NULL;
                if (is_global) {
                    msg = typechecker_format(checker,
                        "variable '%s' shadows a global constant or variable declared on line %d",
                        VAR_DISPLAY_NAME(node), outer_sym->def_line);
                    diagnostic_warning_message(checker->diag, "W2007", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                } else {
                    msg = typechecker_format(checker,
                        "variable '%s' shadows a variable declared on line %d",
                        VAR_DISPLAY_NAME(node), outer_sym->def_line);
                    diagnostic_warning_message(checker->diag, "W2002", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                }
            }
        }
        /* A variable named `main` was already rejected with E4026; the
         * shadow checks below would only pile a second, vaguer error on top. */
        if (strcmp(node->data.var_decl.name, "main") != 0) {
        /* E4012: shadows a type — only when the variable name matches a
         * type usable by that same name. is_struct_name/is_enum_name key
         * on the flattened (module-prefixed) name, so a legitimate local
         * like `mod_Foo` collides with the internal name of `mod.Foo`.
         * Compare display names so that artifact never fires. */
        {
            const char *vname = node->data.var_decl.name;
            const char *vdisplay = VAR_DISPLAY_NAME(node);
            bool shadows_type =
                (is_struct_name(checker, vname) &&
                 strcmp(struct_display_name(checker, vname), vdisplay) == 0) ||
                (is_enum_name(checker, vname) &&
                 strcmp(enum_display_name(checker, vname), vdisplay) == 0);
            if (shadows_type) {
                diagnostic_error_code_formatted(checker->diag, "E4012", NODE_FILE(checker, node), node->token.line, node->token.column, 0, vdisplay);
            }
        }
        /* E4013: shadows a function — only when the variable name
         * matches a function callable by that same name. find_func
         * keys on the flattened (module-prefixed) name, so a legitimate
         * local like `mod_size` collides with the internal name of
         * `mod.size`. Compare display names so that artifact never fires. */
        {
            FuncSig *shadowed = find_func(checker, node->data.var_decl.name);
            if (shadowed && strcmp(func_display_name(shadowed),
                                   VAR_DISPLAY_NAME(node)) == 0) {
                diagnostic_error_code_formatted(checker->diag, "E4013", NODE_FILE(checker, node), node->token.line, node->token.column, 0, VAR_DISPLAY_NAME(node));
            }
        }
        /* E4014: shadows an imported module. imported_modules[] is a single
         * whole-program list covering every file's imports, so it must be
         * filtered to the imports visible in this variable's own file —
         * otherwise a local named after some unrelated file's import gets
         * flagged. token.file is NULL for main-file nodes, matching
         * import_files[]'s own NULL-means-main-file convention. */
        {
            const char *var_file = node->token.file;
            for (int mi = 0; mi < checker->import_count; mi++) {
                const char *imp_file = checker->import_files[mi];
                bool same_file = (!var_file && !imp_file) ||
                    (var_file && imp_file && strcmp(var_file, imp_file) == 0);
                if (!same_file) continue;
                if (strcmp(checker->imported_modules[mi], node->data.var_decl.name) == 0) {
                    diagnostic_error_code_formatted(checker->diag, "E4014", NODE_FILE(checker, node), node->token.line, node->token.column, 0, VAR_DISPLAY_NAME(node));
                    break;
                }
            }
        }
        } /* end: name != "main" */
        if (declared->kind == TK_UNKNOWN &&
            !(node->data.var_decl.value &&
              (node->data.var_decl.value->kind == NODE_CALL_EXPR ||
               node->data.var_decl.value->kind == NODE_MEMBER_EXPR))) {
            /* Don't register variables with unresolved types; an error
               (E3050, E3051, etc.) has already been emitted upstream.
               Skipping scope_define prevents confusing cascading errors.
               Exceptions: func refs, func ref calls (return type unknown),
               member access (the member named the mistake, and leaving the
               variable undeclared turns one error into one per later read),
               and wildcard propagation (value derived from a ?-typed var). */
            bool wildcard_propagation = false;
            if (node->data.var_decl.value) {
                AstNode *val = node->data.var_decl.value;
                /* Direct variable reference: mut tmp = val */
                if (val->kind == NODE_LABEL) {
                    Symbol *src = scope_lookup(checker->current_scope, val->data.label.value);
                    if (src && src->type->kind == TK_UNKNOWN)
                        wildcard_propagation = true;
                }
                /* Array index: mut x = arr[0] where arr is [?] */
                if (val->kind == NODE_INDEX_EXPR && val->data.index_expr.left &&
                    val->data.index_expr.left->kind == NODE_LABEL) {
                    Symbol *src = scope_lookup(checker->current_scope,
                        val->data.index_expr.left->data.label.value);
                    if (src && src->type->kind == TK_ARRAY &&
                        src->type->element_type &&
                        strcmp(src->type->element_type, "?") == 0)
                        wildcard_propagation = true;
                }
            }
            if (!wildcard_propagation) return;
        }
        /* A module-level variable is bound under the name its module gives
         * it, because that is what a reference to it resolves to. Locals have
         * no table entry and keep the name as written. */
        const char *bind_name = node->data.var_decl.name;
        {
            DeclEntry *entry = module_table_entry_for_node(checker->modules, node);
            if (entry && entry->kind == DECL_CONST)
                bind_name = module_mangle(checker->modules, entry);
        }
        scope_define(checker->current_scope, bind_name,
            declared, node->data.var_decl.mutable);
        /* Store definition location and declared type for unused/signedness warnings */
        Symbol *def_sym = scope_lookup_local(checker->current_scope, bind_name);
        if (def_sym) {
            def_sym->declared_type = node->data.var_decl.type_name;
            def_sym->def_line = node->token.line;
            def_sym->def_column = node->token.column;
            /* A new() result (or an alias of one) lives in the heap arena,
             * which outlives every function scope — storing a local's address
             * into one of its pointer fields is an escape (#2650). */
            AstNode *dv = node->data.var_decl.value;
            if (dv && dv->kind == NODE_NEW_EXPR) def_sym->is_heap = true;
            else if (dv && dv->kind == NODE_LABEL) {
                Symbol *src = scope_lookup(checker->current_scope, dv->data.label.value);
                if (src && src->is_heap) def_sym->is_heap = true;
            }
        }

        /* Mark as transparent ref if assigned from ref() */
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_CALL_EXPR) {
            AstNode *fn = node->data.var_decl.value->data.call.function;
            if (fn->kind == NODE_LABEL && strcmp(fn->data.label.value, "ref") == 0) {
                Symbol *sym = scope_lookup_local(checker->current_scope,
                    node->data.var_decl.name);
                if (sym) sym->is_ref = true;
                /* E3079: a mutable reference to a const source is a
                 * contradiction — the source promised immutability and
                 * the reference would let writes through. Allow:
                 *   const r = ref(const_var)   (read-only view)
                 *   const r = ref(mut_var)     (read-only view of mutable)
                 *   mut r   = ref(mut_var)     (full mutable alias)
                 * Reject:
                 *   mut r   = ref(const_var)
                 */
                if (node->data.var_decl.mutable &&
                    node->data.var_decl.value->data.call.arg_count == 1) {
                    AstNode *src = node->data.var_decl.value->data.call.args[0];
                    if (src->kind == NODE_LABEL) {
                        Symbol *src_sym = scope_lookup(checker->current_scope,
                            src->data.label.value);
                        if (src_sym && !src_sym->mutable &&
                            !find_func(checker, src->data.label.value)) {
                            char *msg = NULL;
                            msg = typechecker_format(checker,
                                "cannot take a mutable reference to const variable '%s'; declare '%s' as 'const', or 'copy()' the value to get an independent mutable instance",
                                src->data.label.value,
                                node->data.var_decl.name);
                            diagnostic_error_message(checker->diag, "E3079", msg,
                                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                        }
                    }
                }
            }
            /* Mark const_source when addr() takes a const variable,
             * so writes through the resulting pointer are caught. */
            if (fn->kind == NODE_LABEL && strcmp(fn->data.label.value, "addr") == 0 &&
                node->data.var_decl.value->data.call.arg_count == 1) {
                AstNode *src = node->data.var_decl.value->data.call.args[0];
                const char *root = assignment_target_root_name(src);
                if (root) {
                    /* A module-level declaration is bound under its module's
                     * spelling, so a bare reference from inside the module
                     * misses a plain scope_lookup — and addr() on a const one
                     * left the pointer unmarked, so the write through it was
                     * never caught. */
                    Symbol *src_sym = checker_lookup_symbol(checker, root);
                    if (src_sym && !src_sym->mutable) {
                        Symbol *sym = scope_lookup_local(checker->current_scope,
                            node->data.var_decl.name);
                        if (sym) sym->const_source = true;
                    }
                }
            }
            /* Store multi-return types for temp variables from calls.
             * For generic functions, substitute the wildcard binding
             * so destructured slots get concrete types instead of
             * TK_UNKNOWN; without this, `mut a, b = pair(42)` where
             * pair returns (?, ?) leaves the temp's slot types
             * unknown, the unannotated LHS vars never declare, and
             * subsequent uses error as undefined. */
            if (fn->kind == NODE_LABEL) {
                FuncSig *sig = find_func(checker, fn->data.label.value);
                if (!sig) {
                    /* A bare stdlib call reaching its module through
                     * `import and use` / `using` has no FuncSig to consult. */
                    const char *umod = find_using_stdlib_module(checker, fn->data.label.value);
                    if (umod) {
                        apply_stdlib_call_returns(checker, node->data.var_decl.name,
                                                  umod, fn->data.label.value);
                    }
                }
                if (sig && sig->return_count > 1) {
                    Symbol *sym = scope_lookup_local(checker->current_scope,
                        node->data.var_decl.name);
                    if (sym) {
                        GrayType **slots = sig->return_types;
                        int slot_count = sig->return_count;
                        if (sig->is_generic && sig->decl &&
                            sig->decl->kind == NODE_FUNC_DECL) {
                            /* Bind '?' from the call's args, then
                             * substitute into each return slot. */
                            AstNode *call = node->data.var_decl.value;
                            AstNode *decl = sig->decl;
                            char *binding = NULL;
                            int clamped_argument_count = call->data.call.arg_count <
                                     decl->data.func_decl.param_count
                                ? call->data.call.arg_count
                                : decl->data.func_decl.param_count;
                            for (int argument_index = 0; argument_index < clamped_argument_count && !binding; argument_index++) {
                                const char *ptn =
                                    decl->data.func_decl.params[argument_index].type_name;
                                if (!ptn || !type_name_has_wildcard(ptn)) continue;
                                GrayType *at = resolve_expression(checker, call->data.call.args[argument_index]);
                                binding = bind_wildcard(ptn, at);
                            }
                            if (binding) {
                                int return_count = decl->data.func_decl.return_type_count;
                                GrayType **subbed = xmalloc(sizeof(GrayType *) * (size_t)return_count);
                                for (int return_index = 0; return_index < return_count; return_index++) {
                                    char *sub = substitute_wildcard(
                                        decl->data.func_decl.return_types[return_index], binding);
                                    subbed[return_index] = sub ? type_from_name(sub) : &TYPE_UNKNOWN;
                                }
                                free(binding);
                                slots = subbed;
                                slot_count = return_count;
                            }
                        }
                        sym->ret_types = slots;
                        sym->ret_count = slot_count;
                        sym->ret_types_owned = (slots != sig->return_types);
                    }
                }
            }
            /* Stdlib module calls (mod.func); synthesize (T, Error) return
             * types for fallible functions so multi-var destructuring works. */
            const char *call_qualifier = ast_member_qualifier(fn);
            if (call_qualifier) {
                const char *mfn = fn->data.member.member;
                apply_stdlib_call_returns(checker, node->data.var_decl.name,
                                          call_qualifier, mfn);
            }
            /* User-defined module calls (mod.func); look up the prefixed
             * function signature and propagate multi-return types so
             * destructuring like `mut a, b = mod.func()` works. */
            if (call_qualifier) {
                const char *mfn = fn->data.member.member;
                FuncSig *sig = find_module_func(checker, call_qualifier, mfn);
                if (sig && sig->return_count > 1) {
                    Symbol *sym = scope_lookup_local(checker->current_scope,
                        node->data.var_decl.name);
                    if (sym) {
                        sym->ret_types = sig->return_types;
                        sym->ret_count = sig->return_count;
                    }
                }
            }
            /* Triple-chain calls (mod.Type.func); look up mod_Type_func
             * and propagate multi-return types for destructuring. */
            const char *chain_mod = NULL, *sname = NULL;
            if (ast_member_chain(fn, &chain_mod, &sname)) {
                const char *mod = typechecker_resolve_alias(checker, chain_mod);
                const char *mfn = fn->data.member.member;
                char prefixed[MSG_BUF_SIZE];
                snprintf(prefixed, sizeof(prefixed), "%s_%s_%s", mod, sname, mfn);
                FuncSig *sig = find_func(checker, prefixed);
                if (sig && sig->return_count > 1) {
                    Symbol *sym = scope_lookup_local(checker->current_scope,
                        node->data.var_decl.name);
                    if (sym) {
                        sym->ret_types = sig->return_types;
                        sym->ret_count = sig->return_count;
                    }
                }
            }
        }
        /* Propagate const_source through pointer assignment so that
         * mut q = p inherits the flag when p originated from addr()
         * on a const variable. */
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_LABEL) {
            Symbol *src_sym = scope_lookup(checker->current_scope,
                node->data.var_decl.value->data.label.value);
            if (src_sym && src_sym->const_source) {
                Symbol *dst_sym = scope_lookup_local(checker->current_scope,
                    node->data.var_decl.name);
                if (dst_sym) dst_sym->const_source = true;
            }
        }
        /* Record the lifetime origin of a pointer declared from addr()/raw()
         * or copied from another tracked pointer. A declaration is always
         * legal — the pointer cannot outlive its own scope — but the origin
         * travels with it so later escapes (E3162, E3163) are still caught. */
        {
            Symbol *dst_sym = scope_lookup_local(checker->current_scope,
                node->data.var_decl.name);
            if (dst_sym) {
                const char *origin_name = NULL;
                dst_sym->origin_depth = pointer_origin_of(checker,
                    node->data.var_decl.value, &origin_name);
                dst_sym->origin_name = origin_name;
                const char *field_origin_name = NULL;
                dst_sym->field_origin_depth = container_literal_origin(checker,
                    node->data.var_decl.value, &field_origin_name);
                dst_sym->field_origin_name = field_origin_name;
                /* Pointer checker: a @mem arena handle (mut a = mem.arena(n)),
                 * or a pointer into one (mut p = mem.init(a, T)). */
                pc_apply_mem_call(checker, node->data.var_decl.value, node,
                                  node->data.var_decl.name);
                pc_bind_mem_pointer(checker, dst_sym, node->data.var_decl.value);
            }
        }
        /* Track referenced function for func-typed vars so calls through
         * them can be arity/type-checked at compile time. */
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_FUNC_REF) {
            AstNode *fref = node->data.var_decl.value->data.func_ref.function;
            const char *rname = NULL;
            const char *ref_qualifier = ast_member_qualifier(fref);
            if (fref->kind == NODE_LABEL) {
                rname = fref->data.label.value;
            } else if (ref_qualifier) {
                char buffer[MSG_BUF_SIZE];
                rname = arena_copy_string(checker->arena,
                    module_member_key(checker, ref_qualifier,
                                      fref->data.member.member, buffer, sizeof(buffer)));
            }
            if (rname) {
                Symbol *sym = scope_lookup_local(checker->current_scope,
                    node->data.var_decl.name);
                if (sym) sym->func_ref_name = rname;
            }
        }
        /* ref(func_name) also creates a func reference — capture the
         * referenced function name so call-site validation works. */
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_CALL_EXPR) {
            AstNode *call_fn = node->data.var_decl.value->data.call.function;
            if (call_fn->kind == NODE_LABEL &&
                strcmp(call_fn->data.label.value, "ref") == 0 &&
                node->data.var_decl.value->data.call.arg_count == 1) {
                AstNode *ref_arg = node->data.var_decl.value->data.call.args[0];
                const char *rname = NULL;
                const char *arg_qualifier = ast_member_qualifier(ref_arg);
                if (ref_arg->kind == NODE_LABEL &&
                    find_func(checker, ref_arg->data.label.value)) {
                    rname = ref_arg->data.label.value;
                } else if (arg_qualifier) {
                    char buffer[MSG_BUF_SIZE];
                    module_member_key(checker, arg_qualifier,
                                      ref_arg->data.member.member, buffer, sizeof(buffer));
                    if (find_func(checker, buffer))
                        rname = arena_copy_string(checker->arena, buffer);
                }
                if (rname) {
                    Symbol *sym = scope_lookup_local(checker->current_scope,
                        node->data.var_decl.name);
                    if (sym) sym->func_ref_name = rname;
                }
            }
        }
        /* Per-element tracking for [func] arrays initialised with a
         * literal of func refs (). Preserves each element's
         * originating function name so constant-index calls can
         * recover the real return type (e.g. struct returns) that
         * would otherwise be erased by the void* storage. */
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_ARRAY_VALUE &&
            node->data.var_decl.type_name &&
            (strcmp(node->data.var_decl.type_name, "[func]") == 0 ||
             strncmp(node->data.var_decl.type_name, "[func(", 6) == 0)) {
            AstNode *lit = node->data.var_decl.value;
            int n = lit->data.array_value.count;
            Symbol *sym = scope_lookup_local(checker->current_scope,
                node->data.var_decl.name);
            if (sym && n > 0) {
                sym->func_array_refs = xcalloc((size_t)n, sizeof(const char *));
                sym->func_array_ref_count = n;
                for (int enum_index = 0; enum_index < n; enum_index++) {
                    AstNode *el = lit->data.array_value.elements[enum_index];
                    if (!el || el->kind != NODE_FUNC_REF) continue;
                    AstNode *fref = el->data.func_ref.function;
                    const char *el_qualifier = ast_member_qualifier(fref);
                    if (fref->kind == NODE_LABEL) {
                        sym->func_array_refs[enum_index] = fref->data.label.value;
                    } else if (el_qualifier) {
                        char buffer[MSG_BUF_SIZE];
                        sym->func_array_refs[enum_index] = arena_copy_string(checker->arena,
                            module_member_key(checker, el_qualifier,
                                fref->data.member.member, buffer, sizeof(buffer)));
                    }
                }
            }
        }
    }
}

/* The symbol a bare name binds to. A module-level declaration is bound in
 * scope under its module's spelling, so a bare reference from inside the
 * module resolves through the symbol table first — a plain scope_lookup
 * misses it, and a write then looks like a new local that shadows it. */
static Symbol *checker_lookup_symbol(TypeChecker *checker, const char *name) {
    if (!name) return NULL;
    Symbol *sym = scope_lookup(checker->current_scope, name);
    if (sym) return sym;
    DeclEntry *entry = checker_resolve_entry(checker, name);
    if (!entry) return NULL;
    char key[MSG_BUF_SIZE];
    return scope_lookup(checker->current_scope,
                        module_mangle_into(entry, key, sizeof(key)));
}

/* True when an assignment target writes into memory the caller owns, reached
 * through a parameter of the current function: a &mutable-reference parameter
 * (writes to it or its fields flow back to the caller), or a write *through* a
 * pointer parameter (`out^ = ...`, `out^.f = ...`). Storing a local's address
 * into such a target hands the caller a dangling pointer once this frame is
 * reclaimed (#2650). Reassigning a plain pointer parameter itself
 * (`p = addr(x)`) only rebinds the local copy and is not caught here. */
static bool assign_target_outlives_locals(TypeChecker *checker, AstNode *target) {
    AstNode *fd = checker->current_func_decl;
    if (!fd || fd->kind != NODE_FUNC_DECL || !target) return false;
    AstNode *base = target;
    bool through_deref = false;
    while (base) {
        if (base->kind == NODE_MEMBER_EXPR) base = base->data.member.object;
        else if (base->kind == NODE_INDEX_EXPR) base = base->data.index_expr.left;
        else if (base->kind == NODE_POSTFIX_EXPR && base->data.postfix.op == TOK_CARET) {
            through_deref = true;
            base = base->data.postfix.left;
        } else break;
    }
    if (!base || base->kind != NODE_LABEL) return false;
    const char *root = base->data.label.value;
    for (int i = 0; i < fd->data.func_decl.param_count; i++) {
        Param *p = &fd->data.func_decl.params[i];
        if (!p->name || strcmp(p->name, root) != 0) continue;
        if (p->mutable) return true; /* &ref param: any write reaches the caller */
        if (through_deref) {
            GrayType *pt = typechecker_type_from_name(checker, p->type_name);
            return pt && pt->kind == TK_POINTER;
        }
        return false;
    }
    return false;
}

static void check_assign_stmt(TypeChecker *checker, AstNode *node) {
    /* The right-hand side is a single-value position: a fallible (T, Error)
     * call drops the Error, a user multi-return call fails the C compile. */
    reject_multi_return_in_single_position(checker, node->data.assign.value);

    /* Implicit declaration: x = expr where x is not in scope */
    {
        AstNode *target = node->data.assign.target;
        if (target->kind == NODE_LABEL &&
            node->data.assign.op == TOK_ASSIGN &&
            strcmp(target->data.label.value, "_") != 0) {
            const char *name = target->data.label.value;
            Symbol *sym = checker_lookup_symbol(checker, name);
            if (!sym && !typechecker_is_builtin(name) &&
                !is_struct_name(checker, name) && !is_enum_name(checker, name) &&
                !find_func(checker, name)) {
                /* Resolve RHS to infer type */
                GrayType *val_t = resolve_expression(checker, node->data.assign.value);
                if (val_t && val_t->kind != TK_UNKNOWN && val_t->kind != TK_VOID) {
                    scope_define(checker->current_scope, name, val_t, true);
                    Symbol *new_sym = scope_lookup_local(checker->current_scope, name);
                    if (new_sym) {
                        new_sym->def_line = node->token.line;
                        new_sym->def_column = node->token.column;
                    }
                    node->data.assign.is_decl = true;
                    return; /* done — skip normal assignment validation */
                }
            }
        }
    }

    GrayType *target_t = resolve_expression(checker, node->data.assign.target);
    /* Set expected_type so the value can be resolved against the target:
     * implicit enum selectors (.VARIANT) need the enum, and an empty map
     * literal needs the element types it is being assigned into. */
    GrayType *saved_expected = checker->expected_type;
    if (target_t && ((target_t->kind == TK_ENUM && target_t->name) ||
                     (target_t->kind == TK_MAP && target_t->key_type)))
        checker->expected_type = target_t;
    GrayType *value_t = resolve_expression(checker, node->data.assign.value);
    checker->expected_type = saved_expected;

    /* Pointer checker: `a = mem.arena(n)` re-binds a fresh, live arena handle
     * (a common idiom right after `mem.destroy(a)`); `p = mem.init(a, T)` /
     * `mem.alloc(a, n)` re-binds which arena a pointer variable tracks. */
    if (node->data.assign.target->kind == NODE_LABEL &&
        node->data.assign.op == TOK_ASSIGN) {
        const char *aname = node->data.assign.target->data.label.value;
        pc_apply_mem_call(checker, node->data.assign.value, node, aname);
        Symbol *tsym = scope_lookup(checker->current_scope, aname);
        if (tsym) {
            tsym->mem_arena = NULL;
            pc_bind_mem_pointer(checker, tsym, node->data.assign.value);
        }
    }

    /* An in-range integer literal (or constant-folded literal expression)
     * carries no inherent signedness or width — resolve_expression types it
     * as plain `int`. check_integer_range (E3036) already rejects a value
     * that does not fit the target, so the signed/unsigned and narrowing
     * rules meant for variable sources must not fire for such a literal.
     * A literal that overflows 64 bits is left to those rules (and E3046),
     * so it is still rejected rather than silently truncated. */
    int64_t reassign_lit_val;
    bool value_is_int_literal =
        try_get_literal_int(node->data.assign.value, &reassign_lit_val) &&
        !(node->data.assign.value->kind == NODE_INT_VALUE &&
          node->data.assign.value->data.int_value.overflow);

    /* Compound assignment type validation: x op= y must be valid
     * when x op y would be valid. Mirrors the checks in
     * resolve_infix_expr() for the corresponding binary operator. */
    TokenType aop = node->data.assign.op;
    if (aop == TOK_PLUS_ASSIGN || aop == TOK_MINUS_ASSIGN ||
        aop == TOK_ASTERISK_ASSIGN || aop == TOK_SLASH_ASSIGN ||
        aop == TOK_PERCENT_ASSIGN) {

        /* E3078: pointer arithmetic */
        if (target_t && target_t->kind == TK_POINTER) {
            diagnostic_error_code(checker->diag, "E3078",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }

        if (target_t && value_t &&
            target_t->kind != TK_UNKNOWN && value_t->kind != TK_UNKNOWN) {

            /* E3002: bool in arithmetic */
            if (target_t->kind == TK_BOOL || value_t->kind == TK_BOOL) {
                char *msg = typechecker_format(checker,
                    "invalid operands: cannot use '%s' with %s and %s",
                    operator_display_name(aop), type_name(target_t), type_name(value_t));
                diagnostic_error_message(checker->diag, "E3002", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }

            /* E3048: appending a non-string to a string. Only a string
             * target makes '+=' a concatenation; a non-string target
             * (array, map, struct, int, ...) is left to the arithmetic
             * checks below and the assignment type check, so E3048 does
             * not pile onto E3093 / E3001. */
            if (target_t->kind == TK_STRING && value_t->kind != TK_STRING &&
                aop == TOK_PLUS_ASSIGN) {
                diagnostic_error_code_formatted(checker->diag, "E3048",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    type_display_name(checker, target_t), type_display_name(checker, value_t));
            }

            /* E3002: string in non-plus arithmetic */
            if ((target_t->kind == TK_STRING || value_t->kind == TK_STRING) &&
                aop != TOK_PLUS_ASSIGN) {
                char *msg = typechecker_format(checker,
                    "cannot use '%s' on string type", operator_display_name(aop));
                diagnostic_error_message(checker->diag, "E3002", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }

            /* E3002: modulo on float */
            if (aop == TOK_PERCENT_ASSIGN &&
                (target_t->kind == TK_FLOAT || value_t->kind == TK_FLOAT)) {
                diagnostic_error_message(checker->diag, "E3002",
                    "modulo (%) only works on integers, not floats",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }

            /* E3093: arithmetic on map, array, or struct */
            if (target_t->kind == TK_MAP || target_t->kind == TK_ARRAY ||
                target_t->kind == TK_STRUCT ||
                value_t->kind == TK_MAP || value_t->kind == TK_ARRAY ||
                value_t->kind == TK_STRUCT) {
                GrayType *bad = (target_t->kind == TK_MAP || target_t->kind == TK_ARRAY ||
                                 target_t->kind == TK_STRUCT) ? target_t : value_t;
                diagnostic_error_code_formatted(checker->diag, "E3093",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    operator_display_name(aop), type_display_name(checker, bad));
            }
        }
    }

    /* E6008: reject assignment to stdlib module constants (math.PI = x, etc.) */
    AstNode *target = node->data.assign.target;
    const char *target_qualifier = ast_member_qualifier(target);
    if (target_qualifier) {
        const char *obj = target_qualifier;
        bool is_module = false;
        /* A local or parameter of the same name shadows the module, so the
         * qualifier names a value and the assignment is an ordinary field
         * write — including inside the file whose own module it names. */
        if (!scope_lookup(checker->current_scope, obj)) {
            for (int mi = 0; mi < checker->import_count; mi++) {
                if (strcmp(checker->imported_modules[mi], obj) == 0) { is_module = true; break; }
            }
        }
        if (is_module) {
            /* The member is not necessarily a constant. Stdlib members
             * (math.PI) have no declaration to consult and are constants; a
             * user module's `mut` variable is not, and calling it one
             * described the declaration incorrectly. */
            DeclEntry *entry = target->resolved_decl;
            AstNode *decl = (entry && entry->kind == DECL_CONST) ? entry->ast_node : NULL;
            const char *kind = (decl && decl->data.var_decl.mutable) ? "variable" : "constant";
            diagnostic_error_code_formatted(checker->diag, "E6008",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                obj, target->data.member.member, kind);
        }
    }

    /* E5025: assignment target validation; reject assignment to non-assignable targets */
    if (target->kind != NODE_LABEL &&
        target->kind != NODE_MEMBER_EXPR &&
        target->kind != NODE_INDEX_EXPR &&
        target->kind != NODE_PREFIX_EXPR &&
        target->kind != NODE_POSTFIX_EXPR) {
        diagnostic_error_message(checker->diag, "E5025",
            "cannot assign to this expression; left side of '=' must be a variable, field, or index",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }

    /* Check for assignment to const variable (direct, index, or field).
     * Uses assignment_target_root_name() to walk arbitrarily nested
     * member/index chains so that e.g. o.inner.value = 999 is caught. */
    const char *const_name = NULL;
    {
        const char *root = assignment_target_root_name(target);
        if (root) {
            Symbol *sym = checker_lookup_symbol(checker, root);
            /* p.field on a pointer parameter auto-derefs to p^.field — the
             * pointer itself is not being modified, so don't flag it. */
            if (sym && !sym->mutable && !(sym->type && sym->type->kind == TK_POINTER))
                const_name = root;
        }
    }
    if (const_name) {
        diagnostic_error_code_formatted(checker->diag, "E3005", NODE_FILE(checker, node), node->token.line, node->token.column, 0, const_name);
    }

    /* E3122: cannot modify value through a pointer whose pointee is a
     * const-declared variable (taken via addr()).  Covers p^ = v,
     * p^.field = v, and compound assignments (p^ += v). */
    if (target->kind == NODE_POSTFIX_EXPR &&
        target->data.postfix.op == TOK_CARET &&
        target->data.postfix.left->kind == NODE_LABEL) {
        Symbol *sym = scope_lookup(checker->current_scope,
            target->data.postfix.left->data.label.value);
        if (sym && sym->const_source) {
            diagnostic_error_code_formatted(checker->diag, "E3122",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                target->data.postfix.left->data.label.value);
        }
    } else if (target->kind == NODE_MEMBER_EXPR &&
               target->data.member.object->kind == NODE_POSTFIX_EXPR &&
               target->data.member.object->data.postfix.op == TOK_CARET &&
               target->data.member.object->data.postfix.left->kind == NODE_LABEL) {
        Symbol *sym = scope_lookup(checker->current_scope,
            target->data.member.object->data.postfix.left->data.label.value);
        if (sym && sym->const_source) {
            diagnostic_error_code_formatted(checker->diag, "E3122",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                target->data.member.object->data.postfix.left->data.label.value);
        }
    }

    /* Propagate const_source through pointer reassignment (q = p). */
    if (target->kind == NODE_LABEL && node->data.assign.value &&
        node->data.assign.value->kind == NODE_LABEL) {
        Symbol *src_sym = scope_lookup(checker->current_scope,
            node->data.assign.value->data.label.value);
        if (src_sym && src_sym->const_source) {
            Symbol *dst_sym = scope_lookup(checker->current_scope,
                target->data.label.value);
            if (dst_sym) dst_sym->const_source = true;
        }
    }

    /* E3004: string index assignment is not supported; strings are immutable
     * sequences — individual characters cannot be modified by index.
     * This fires regardless of the assigned value's type. */
    if (target->kind == NODE_INDEX_EXPR) {
        GrayType *indexed_t = resolve_expression(checker, target->data.index_expr.left);
        if (indexed_t && indexed_t->kind == TK_STRING) {
            diagnostic_error_code(checker->diag, "E3004",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }

    /* E3094: array index assignment type mismatch (arr[i] = wrong_type) */
    if (target->kind == NODE_INDEX_EXPR && node->data.assign.value) {
        GrayType *indexed_t = resolve_expression(checker, target->data.index_expr.left);
        if (indexed_t && indexed_t->kind == TK_ARRAY && indexed_t->element_type) {
            GrayType *elem_t = type_from_name(indexed_t->element_type);
            GrayType *val_t = resolve_expression(checker, node->data.assign.value);
            if (val_t && val_t->kind != TK_UNKNOWN && elem_t && elem_t->kind != TK_UNKNOWN &&
                !types_assignable(checker, elem_t, val_t) &&
                !(val_t->kind == TK_FLOAT && is_int_kind(elem_t->kind)) &&
                /* enum array: type_from_name returns TK_STRUCT for enum names */
                !(val_t->kind == TK_ENUM && elem_t->kind == TK_STRUCT &&
                  indexed_t->element_type && is_enum_name(checker, indexed_t->element_type))) {
                diagnostic_error_code_formatted(checker->diag, "E3094",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    type_display_name(checker, val_t), type_display_name(checker, indexed_t));
            }
            /* E3019: assigning a value that crosses signedness into an element needs a cast. */
            check_signedness_crossing(checker, indexed_t->element_type,
                node->data.assign.value, val_t, node->data.assign.value);
            /* E3036: out-of-range literal into a narrow element (xs[0] = 300). */
            int64_t elem_lit;
            bool elem_lit_neg;
            if (try_get_signed_literal_int(node->data.assign.value, &elem_lit, &elem_lit_neg)) {
                check_integer_range(checker->diag, NODE_FILE(checker, node),
                    node->data.assign.value->token.line,
                    node->data.assign.value->token.column,
                    indexed_t->element_type, elem_lit, elem_lit_neg);
            }
        }
        /* E3019: assigning a signed value into an unsigned map value needs a cast. */
        if (indexed_t && indexed_t->kind == TK_MAP && indexed_t->value_type) {
            check_signedness_crossing(checker, indexed_t->value_type,
                node->data.assign.value, value_t, node->data.assign.value);
            /* E3036: out-of-range literal into a narrow map value (m["a"] = 300). */
            int64_t mval_lit;
            bool mval_lit_neg;
            if (try_get_signed_literal_int(node->data.assign.value, &mval_lit, &mval_lit_neg)) {
                check_integer_range(checker->diag, NODE_FILE(checker, node),
                    node->data.assign.value->token.line,
                    node->data.assign.value->token.column,
                    indexed_t->value_type, mval_lit, mval_lit_neg);
            }
        }
    }

    /* E3036 (): range check on reassignment; the var_decl path
     * already catches out-of-range literals at declaration, but
     * reassignment (`x = 300` where x is u8) was unchecked. */
    if (target->kind == NODE_LABEL && node->data.assign.value) {
        Symbol *sym = scope_lookup(checker->current_scope, target->data.label.value);
        if (sym && sym->declared_type) {
            int64_t lit_val;
            bool lit_neg;
            if (try_get_signed_literal_int(node->data.assign.value, &lit_val, &lit_neg)) {
                check_integer_range(checker->diag, NODE_FILE(checker, node),
                    node->data.assign.value->token.line,
                    node->data.assign.value->token.column,
                    sym->declared_type, lit_val, lit_neg);
            }
        }
    }
    /* E3036 (): range check on struct field assignment. */
    if (target->kind == NODE_MEMBER_EXPR &&
        target->data.member.object->kind == NODE_LABEL &&
        node->data.assign.value) {
        Symbol *sym = scope_lookup(checker->current_scope, target->data.member.object->data.label.value);
        if (sym && sym->type && sym->type->kind == TK_STRUCT) {
            GrayType *field_t = struct_field_type(checker, sym->type->name, target->data.member.member);
            if (field_t && field_t->name) {
                int64_t lit_val;
                bool lit_neg;
                if (try_get_signed_literal_int(node->data.assign.value, &lit_val, &lit_neg)) {
                    check_integer_range(checker->diag, NODE_FILE(checker, node),
                        node->data.assign.value->token.line,
                        node->data.assign.value->token.column,
                        field_t->name, lit_val, lit_neg);
                }
            }
        }
    }
    /* Also handle dereferenced pointer field: p^.field = value */
    if (target->kind == NODE_MEMBER_EXPR &&
        target->data.member.object->kind == NODE_POSTFIX_EXPR &&
        target->data.member.object->data.postfix.left->kind == NODE_LABEL &&
        node->data.assign.value) {
        Symbol *sym = scope_lookup(checker->current_scope,
            target->data.member.object->data.postfix.left->data.label.value);
        if (sym && sym->type && sym->type->kind == TK_POINTER && sym->type->element_type) {
            GrayType *field_t = struct_field_type(checker, sym->type->element_type, target->data.member.member);
            if (field_t && field_t->name) {
                int64_t lit_val;
                bool lit_neg;
                if (try_get_signed_literal_int(node->data.assign.value, &lit_val, &lit_neg)) {
                    check_integer_range(checker->diag, NODE_FILE(checker, node),
                        node->data.assign.value->token.line,
                        node->data.assign.value->token.column,
                        field_t->name, lit_val, lit_neg);
                }
            }
        }
    }
    /* Reject integer assigned to enum variable */
    if (target->kind == NODE_LABEL && target_t->kind == TK_ENUM &&
        is_int_kind(value_t->kind)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "cannot assign %s to enum '%s'; use an enum variant like '%s.VARIANT'",
            type_name(value_t), type_display_name(checker, target_t), type_display_name(checker, target_t));
        diagnostic_error_message(checker->diag, "E3118", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Check type mismatch on assignment (only for direct variable targets) */
    if (target->kind == NODE_LABEL) {
        Symbol *sym = scope_lookup(checker->current_scope, target->data.label.value);
        if (sym && sym->type->kind != TK_UNKNOWN && value_t->kind != TK_UNKNOWN &&
            target_t->kind != TK_UNKNOWN &&
            !types_assignable(checker, target_t, value_t) &&
            !(target_t->kind == TK_ENUM && is_int_kind(value_t->kind)) &&
            !(target_t->kind == TK_STRUCT && is_int_kind(value_t->kind)) &&
            !(target_t->kind == TK_POINTER && node->data.assign.value->kind == NODE_LABEL &&
              scope_lookup(checker->current_scope, node->data.assign.value->data.label.value) &&
              scope_lookup(checker->current_scope, node->data.assign.value->data.label.value)->is_ref) &&
            /* nil is a valid value for pointer and Error variables */
            !(value_t->kind == TK_NIL &&
              (target_t->kind == TK_POINTER || target_t->kind == TK_ERROR))) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type mismatch: cannot assign %s to %s variable '%s'",
                type_display_name(checker, value_t), type_display_name(checker, target_t), target->data.label.value);
            tc_err_assign_type(checker, node, msg);
        }
    }
    /* Struct-to-struct name mismatch on direct variable assignment */
    if (target->kind == NODE_LABEL &&
        target_t->kind == TK_STRUCT && value_t->kind == TK_STRUCT &&
        target_t->name && value_t->name &&
        strcmp(target_t->name, value_t->name) != 0) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign '%s' to '%s' variable '%s'",
            type_display_name(checker, value_t), type_display_name(checker, target_t),
            target->data.label.value);
        tc_err_assign_type(checker, node, msg);
    }
    /* Enum-to-enum name mismatch on direct variable assignment */
    if (target->kind == NODE_LABEL &&
        target_t->kind == TK_ENUM && value_t->kind == TK_ENUM &&
        target_t->name && value_t->name &&
        !typechecker_same_enum_type(checker, target_t->name, value_t->name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign enum '%s' to enum '%s' variable '%s'",
            type_display_name(checker, value_t), type_display_name(checker, target_t),
            target->data.label.value);
        tc_err_assign_type(checker, node, msg);
    }
    /* Function-to-function signature mismatch on direct variable assignment */
    if (target->kind == NODE_LABEL && func_types_mismatch(target_t, value_t)) {
        char *msg = typechecker_format(checker,
            "type mismatch: cannot assign '%s' to '%s' variable '%s'",
            type_display_name(checker, value_t), type_display_name(checker, target_t),
            target->data.label.value);
        tc_err_assign_type(checker, node, msg);
    }
    /* E3098: struct-to-struct name mismatch through pointer dereference: v3^ = v2^
     * The NODE_LABEL check above is bypassed when the target is a postfix
     * dereference. resolve_expression already strips the pointer layer, so
     * target_t and value_t are both TK_STRUCT — just compare names. */
    if (target->kind == NODE_POSTFIX_EXPR &&
        target->data.postfix.op == TOK_CARET &&
        target_t && value_t &&
        target_t->kind == TK_STRUCT && value_t->kind == TK_STRUCT &&
        target_t->name && value_t->name &&
        strcmp(target_t->name, value_t->name) != 0) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign '%s' to '%s' through pointer dereference",
            type_display_name(checker, value_t), type_display_name(checker, target_t));
        diagnostic_error_message(checker->diag, "E3098", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* General type mismatch through pointer dereference (e.g. p^ = "hello"
     * where p is ^Foo).  E3098 above catches struct-to-struct name mismatches;
     * this covers all other cross-kind mismatches (struct^ = string, int^ = string, etc.). */
    if (target->kind == NODE_POSTFIX_EXPR &&
        target->data.postfix.op == TOK_CARET &&
        target_t && value_t &&
        target_t->kind != TK_UNKNOWN && value_t->kind != TK_UNKNOWN &&
        !types_assignable(checker, target_t, value_t) &&
        !(value_t->kind == TK_NIL &&
          (target_t->kind == TK_POINTER || target_t->kind == TK_ERROR))) {
        char *msg = typechecker_format(checker,
            "type mismatch: cannot assign %s to %s through pointer dereference",
            type_display_name(checker, value_t), type_display_name(checker, target_t));
        diagnostic_error_message(checker->diag, "E3098", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Pointer-to-pointer: pointee types differ on reassignment (e.g., p = q where ^int ≠ ^string).
     * The outer kind-equality guard short-circuits, so a dedicated check is required. */
    if (target->kind == NODE_LABEL &&
        target_t && value_t &&
        target_t->kind == TK_POINTER && value_t->kind == TK_POINTER &&
        target_t->name && value_t->name &&
        strcmp(target_t->name, value_t->name) != 0) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign %s to %s variable '%s'",
            type_display_name(checker, value_t), type_display_name(checker, target_t), target->data.label.value);
        tc_err_assign_type(checker, node, msg);
    }
    /* Array-to-array: element types differ on reassignment (e.g., [int] = [string]).
     * Both sides are TK_ARRAY so the outer kind-equality guard passes. */
    if (target->kind == NODE_LABEL &&
        target_t && value_t &&
        target_t->kind == TK_ARRAY && value_t->kind == TK_ARRAY &&
        target_t->element_type && value_t->element_type &&
        strcmp(target_t->element_type, value_t->element_type) != 0) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign %s to %s variable '%s'",
            type_display_name(checker, value_t), type_display_name(checker, target_t), target->data.label.value);
        tc_err_assign_type(checker, node, msg);
    }
    /* Map-to-map: key or value types differ on reassignment (e.g., [string:int] = [string:string]).
     * Both sides are TK_MAP so the outer kind-equality guard passes. */
    if (target->kind == NODE_LABEL &&
        target_t && value_t &&
        target_t->kind == TK_MAP && value_t->kind == TK_MAP &&
        target_t->key_type && value_t->key_type &&
        target_t->value_type && value_t->value_type &&
        (strcmp(target_t->key_type, value_t->key_type) != 0 ||
         strcmp(target_t->value_type, value_t->value_type) != 0)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign %s to %s variable '%s'",
            type_display_name(checker, value_t), type_display_name(checker, target_t), target->data.label.value);
        tc_err_assign_type(checker, node, msg);
    }
    /* Integer narrowing on reassignment: u32 → u8, int → i16, i128 → i64, etc.
     * Both sides share TK_INT/TK_UINT so the kind-equality guard passes. */
    if (target->kind == NODE_LABEL && !value_is_int_literal &&
        target_t && value_t &&
        target_t->name && value_t->name) {
        int declared_rank = int_type_name_rank(target_t->name);
        int value_rank = int_type_name_rank(value_t->name);
        if (declared_rank > 0 && value_rank > 0 && declared_rank < value_rank) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "type mismatch: cannot implicitly narrow %s to %s variable '%s'; use cast(value, %s) to convert explicitly",
                value_t->name, target_t->name, target->data.label.value, target_t->name);
            diagnostic_error_message(checker->diag, "E3155", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* E3019: signed-to-unsigned on reassignment (e.g., uint_var = signed_var).
     * Only fires when narrowing did not already catch it (same rank). */
    if (target->kind == NODE_LABEL && !value_is_int_literal &&
        target_t && value_t &&
        target_t->name && value_t->name &&
        is_unsigned_type(target_t->name) &&
        is_signed_int_type(value_t->name) &&
        int_type_name_rank(target_t->name) >= int_type_name_rank(value_t->name)) {
        diagnostic_error_code_formatted(checker->diag, "E3019",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            value_t->name, target_t->name);
    }
    /* Unsigned-to-signed on reassignment (e.g., int_var = uint_var). Fires on a
     * same-width reinterpretation only: narrowing is caught above, and an
     * unsigned value widening into a strictly larger signed type is lossless. */
    if (target->kind == NODE_LABEL &&
        target_t && value_t &&
        target_t->name && value_t->name &&
        is_signed_int_type(target_t->name) &&
        is_unsigned_type(value_t->name) &&
        int_type_name_rank(target_t->name) == int_type_name_rank(value_t->name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot assign unsigned type '%s' to signed type '%s' variable '%s'; use cast(value, %s) to convert explicitly",
            value_t->name, target_t->name, target->data.label.value,
            target_t->name);
        diagnostic_error_message(checker->diag, "E3019", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Float narrowing on reassignment: f64 → f32, float → f32.
     * Both are TK_FLOAT so the kind guard passes. */
    if (target->kind == NODE_LABEL &&
        target_t && value_t &&
        target_t->kind == TK_FLOAT && value_t->kind == TK_FLOAT &&
        target_t->name && value_t->name &&
        strcmp(target_t->name, value_t->name) != 0 &&
        strcmp(target_t->name, "f32") == 0) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "type mismatch: cannot implicitly narrow %s to %s variable '%s'; use cast(value, %s) to convert explicitly",
            value_t->name, target_t->name, target->data.label.value, target_t->name);
        diagnostic_error_message(checker->diag, "E3155", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Check type mismatch on struct field assignment.
     * sym->type may be TK_STRUCT (by-value) or TK_POINTER (from new()),
     * in both cases sym->type->name is the pointee/struct name. */
    if (target->kind == NODE_MEMBER_EXPR && target->data.member.object->kind == NODE_LABEL) {
        Symbol *sym = scope_lookup(checker->current_scope, target->data.member.object->data.label.value);
        if (sym && (sym->type->kind == TK_STRUCT || sym->type->kind == TK_POINTER)) {
            GrayType *field_t = struct_field_type(checker, sym->type->name, target->data.member.member);
            if (field_t->kind != TK_UNKNOWN && value_t->kind != TK_UNKNOWN &&
                /* kinds differ, OR both are pointers/structs to different types */
                (!types_assignable(checker, field_t, value_t) ||
                 (field_t->kind == TK_POINTER &&
                  field_t->name && value_t->name &&
                  strcmp(field_t->name, value_t->name) != 0) ||
                 (field_t->kind == TK_STRUCT && value_t->kind == TK_STRUCT &&
                  field_t->name && value_t->name &&
                  strcmp(field_t->name, value_t->name) != 0)) &&
                /* nil is a valid value for pointer and Error fields */
                !(value_t->kind == TK_NIL &&
                  (field_t->kind == TK_POINTER || field_t->kind == TK_ERROR))) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "type mismatch: cannot assign %s to %s field '%s'",
                    type_display_name(checker, value_t), type_display_name(checker, field_t), target->data.member.member);
                tc_err_assign_type(checker, node, msg);
            }
            /* E3066: func signature mismatch on struct field assignment */
            if (func_types_mismatch(field_t, value_t)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot assign %s to field '%s' of type %s",
                    type_display_name(checker, value_t), target->data.member.member,
                    type_display_name(checker, field_t));
                diagnostic_error_message(checker->diag, "E3066", msg,
                    NODE_FILE(checker, node->data.assign.value),
                    node->data.assign.value->token.line,
                    node->data.assign.value->token.column, 0);
            }
        }
    }
    /* Type mismatch on explicit deref field assignment: p^.field = value */
    if (target->kind == NODE_MEMBER_EXPR &&
        target->data.member.object->kind == NODE_POSTFIX_EXPR &&
        target->data.member.object->data.postfix.op == TOK_CARET) {
        GrayType *obj_t = resolve_expression(checker, target->data.member.object);
        if (obj_t && obj_t->kind == TK_STRUCT && obj_t->name) {
            GrayType *field_t = struct_field_type(checker, obj_t->name, target->data.member.member);
            if (field_t->kind != TK_UNKNOWN && value_t->kind != TK_UNKNOWN &&
                !types_assignable(checker, field_t, value_t) &&
                !(value_t->kind == TK_NIL &&
                  (field_t->kind == TK_POINTER || field_t->kind == TK_ERROR))) {
                char *msg = typechecker_format(checker,
                    "type mismatch: cannot assign %s to %s field '%s'",
                    type_display_name(checker, value_t), type_display_name(checker, field_t), target->data.member.member);
                tc_err_assign_type(checker, node, msg);
            }
        }
    }
    /* E3163: storing a local's address into memory that outlives it, reached
     * through a pointer parameter, a &ref parameter, or a new() heap object's
     * pointer field. The origin travels through intermediate pointers, so
     * `tmp = addr(local); out^ = tmp` is caught too (#2650). */
    bool caller_mem = assign_target_outlives_locals(checker, target);
    if (caller_mem) {
        const char *origin_name = NULL;
        int origin_depth = expression_origin(checker, node->data.assign.value,
                                             &origin_name);
        if (origin_depth > 0 &&
            origin_depth >= checker->current_func_scope_depth) {
            const char *dest = escape_root_name(target);
            diagnostic_error_code_formatted(checker->diag, "E3163",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                dest ? dest : origin_name, origin_name);
        }
    }
    /* E3163: reject storing an address into a pointer that outlives it.
     * The origin travels through intermediate pointers and function calls,
     * so laundering the address (p = tmp where tmp = addr(local), or
     * p = forward(addr(local))) is caught too. */
    if (!caller_mem && target->kind == NODE_LABEL) {
        const char *ptr_name = target->data.label.value;
        const char *origin_name = NULL;
        int origin_depth = expression_origin(checker, node->data.assign.value,
                                             &origin_name);
        if (origin_depth > symbol_scope_depth(checker->current_scope, ptr_name)) {
            diagnostic_error_code_formatted(checker->diag, "E3163",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                ptr_name, origin_name);
        } else {
            Symbol *ptr_sym = scope_lookup(checker->current_scope, ptr_name);
            if (ptr_sym) {
                const char *direct_origin_name = NULL;
                ptr_sym->origin_depth = pointer_origin_of(checker,
                    node->data.assign.value, &direct_origin_name);
                ptr_sym->origin_name = direct_origin_name;
                const char *field_origin_name = NULL;
                ptr_sym->field_origin_depth = container_literal_origin(checker,
                    node->data.assign.value, &field_origin_name);
                ptr_sym->field_origin_name = field_origin_name;
            }
        }
    } else if (!caller_mem &&
               (target->kind == NODE_MEMBER_EXPR || target->kind == NODE_INDEX_EXPR)) {
        /* A struct field or array element as the target: the E3163 guard
         * above never reached these. Compare the stored address's origin
         * against the scope of the root aggregate variable. */
        const char *root = assignment_target_root_name(target);
        if (root) {
            const char *origin_name = NULL;
            int origin_depth = expression_origin(checker, node->data.assign.value,
                                                 &origin_name);
            int root_depth = symbol_scope_depth(checker->current_scope, root);
            if (origin_depth > root_depth) {
                diagnostic_error_code_formatted(checker->diag, "E3163",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    root, origin_name);
            } else if (origin_depth > 0) {
                Symbol *root_sym = scope_lookup(checker->current_scope, root);
                if (root_sym && origin_depth > root_sym->field_origin_depth) {
                    root_sym->field_origin_depth = origin_depth;
                    root_sym->field_origin_name = origin_name;
                }
            }
            /* Pointer checker: `b.p = mem.alloc(a, x)` binds a @mem arena
             * into a field, the same as a struct literal doing it at
             * construction (pc_bind_mem_pointer) — track it on the root
             * aggregate's field_mem_arena so pc_check_mem_deref() catches
             * `b.p^` after `a` is destroyed. Always the field slot here
             * (never root_sym's own mem_arena): the target is a field of
             * root, not root itself. */
            {
                Symbol *root_sym = scope_lookup(checker->current_scope, root);
                const char *marena = NULL;
                int mepoch = 0;
                bool mvia_field = false;
                /* Always the field slot here, regardless of what
                 * pc_mem_pointer_in_expr reports: the assignment target is
                 * root.<field>, not root itself. */
                if (root_sym &&
                    pc_mem_pointer_in_expr(checker, node->data.assign.value,
                                           &marena, &mepoch, &mvia_field)) {
                    root_sym->field_mem_arena = marena;
                    root_sym->field_mem_epoch = mepoch;
                }
            }
        } else {
            /* The chain passes through a `^` (e.g. `n^.link = addr(local)` on a
             * new() heap object). assignment_target_root_name gave up at the
             * deref; record the stored address's origin on the pointer variable
             * so the escape checks catch it when that pointer itself escapes
             * (`return n`) — the write only dangles if the object outlives the
             * local (#2650). */
            const char *deref_root = escape_root_name(target);
            if (deref_root) {
                Symbol *root_sym = scope_lookup(checker->current_scope, deref_root);
                if (root_sym && root_sym->is_heap) {
                    const char *origin_name = NULL;
                    int origin_depth = expression_origin(checker,
                        node->data.assign.value, &origin_name);
                    if (origin_depth > 0 &&
                        origin_depth > root_sym->field_origin_depth) {
                        root_sym->field_origin_depth = origin_depth;
                        root_sym->field_origin_name = origin_name;
                    }
                }
            }
        }
    }
}

/* A returned value whose type is the caller's type argument, or a
 * wildcard-typed parameter. Such a value has no fixed type — it is a Foo only
 * for the one caller that passes Foo — so it cannot satisfy a concrete
 * declared return type. Reports the offending name and what it is; returns
 * NULL when the value's type is fixed. Derefs are transparent: `new(t)^` is
 * the type argument just as `new(t)` is. */
static const char *return_value_is_type_argument(TypeChecker *checker,
    AstNode *value, const char **what, AstNode **at) {
    if (!value) return NULL;
    if (value->kind == NODE_POSTFIX_EXPR && value->data.postfix.op == TOK_CARET)
        return return_value_is_type_argument(checker, value->data.postfix.left, what, at);
    *at = value;
    /* The parser normalises a type-parameter name in a type position to "?",
     * so match either form and report the name the user wrote. */
    const char *tp = checker->type_param_name;
    const char *built = NULL;
    if (value->kind == NODE_NEW_EXPR) built = value->data.new_expr.type_name;
    else if (value->kind == NODE_STRUCT_VALUE) built = value->data.struct_value.name;
    if (built && (type_name_has_wildcard(built) || (tp && strcmp(built, tp) == 0))) {
        *what = "an instance of the type argument";
        return tp ? tp : built;
    }
    /* A parameter declared with a wildcard type (e.g. `v ?`) carries the same
     * contradiction: its type is the argument's, not the declared one. */
    if (value->kind == NODE_LABEL && checker->current_func_decl) {
        AstNode *fd = checker->current_func_decl;
        for (int i = 0; i < fd->data.func_decl.param_count; i++) {
            Param *p = &fd->data.func_decl.params[i];
            if (p->is_type_param || !p->name ||
                strcmp(p->name, value->data.label.value) != 0)
                continue;
            if (type_name_has_wildcard(p->type_name)) {
                *what = "a wildcard-typed value";
                return p->name;
            }
            break;
        }
    }
    return NULL;
}

static void check_return_stmt(TypeChecker *checker, AstNode *node) {
    for (int i = 0; i < node->data.return_stmt.count; i++) {
        /* E3040: multi-return call in single-value return position */
        reject_multi_return_in_single_position(checker, node->data.return_stmt.values[i]);
        /* Set expected_type for implicit enum resolution in return values */
        GrayType *saved_ret_expected = checker->expected_type;
        if (i < checker->current_return_count &&
            checker->current_return_types[i] &&
            checker->current_return_types[i]->kind == TK_ENUM &&
            checker->current_return_types[i]->name)
            checker->expected_type = checker->current_return_types[i];
        resolve_expression(checker, node->data.return_stmt.values[i]);
        checker->expected_type = saved_ret_expected;
    }
    /* main() exits when control reaches the closing brace; an
     * explicit `return` is not allowed. Without this check, codegen
     * emits `gray_scope_restore(_, _scope_mark)` referencing a
     * variable that main never declares, and the C compile fails. */
    if (checker->current_func_is_main) {
        diagnostic_error_help(checker->diag, "E3073",
            arena_copy_string(checker->arena, "'return' is not allowed in main(); main exits when control reaches the closing brace"),
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            "use exit(code) to terminate with a status code");
        return;
    }
    /* E3162: reject returning the address of a local; its memory is freed
     * when the function returns. The origin travels through intermediate
     * pointers, a function call, a struct field, and a container literal
     * (array/map/struct) or an element read back out of one, so
     * `mut p = addr(local); return p`, `return forward(addr(local))`,
     * `return {addr(local)}`, and `mut a [^int] = {addr(local)}; return a[0]`
     * are all caught. */
    for (int i = 0; i < node->data.return_stmt.count; i++) {
        AstNode *return_val = node->data.return_stmt.values[i];
        const char *origin_name = NULL;
        int origin_depth = expression_origin(checker, return_val, &origin_name);
        if (origin_depth > 0 &&
            origin_depth >= checker->current_func_scope_depth) {
            diagnostic_error_code_formatted(checker->diag, "E3162",
                NODE_FILE(checker, node), return_val->token.line,
                return_val->token.column, 0, origin_name, origin_name);
        }
    }
    /* E3071: `return nil` from a function whose return type contains
     * '?' is unsound; nil isn't a value for every binding (int,
     * string, etc.). The codegen would otherwise emit `NULL` and let
     * clang reject the result as an int/struct conversion error.
     * Allow nil in non-primary return slots (e.g. (?, Error)).
     * Skip during the per-instantiation re-check so we only emit once. */
    if (!checker->suppress_typetable_writes &&
        checker->current_return_count > 0 && node->data.return_stmt.count > 0) {
        int n = node->data.return_stmt.count;
        int slots = n < checker->current_return_count ? n : checker->current_return_count;
        for (int i = 0; i < slots; i++) {
            AstNode *return_val = node->data.return_stmt.values[i];
            if (return_val->kind != NODE_NIL_VALUE) continue;
            const char *type_name_str = (i == 0 && checker->current_return_type_names)
                ? checker->current_return_type_names[i] : NULL;
            if (type_name_str && type_name_has_wildcard(type_name_str)) {
                diagnostic_error_code(checker->diag, "E3071", NODE_FILE(checker, return_val), return_val->token.line, return_val->token.column, 0);
            }
        }
    }

    /* E3139: a concrete declared return type is a promise that has to hold for
     * every caller. Returning the caller's type argument breaks it for all but
     * the one caller that happens to pass a matching type. Checked here rather
     * than during monomorphisation so it fires on the declaration alone, even
     * when the function is never called. */
    if (!checker->suppress_typetable_writes &&
        checker->current_return_count > 0 && node->data.return_stmt.count > 0) {
        int n = node->data.return_stmt.count;
        int slots = n < checker->current_return_count ? n : checker->current_return_count;
        for (int i = 0; i < slots; i++) {
            const char *declared = checker->current_return_type_names
                ? checker->current_return_type_names[i] : NULL;
            if (!declared || type_name_has_wildcard(declared)) continue;
            const char *what = NULL;
            AstNode *return_val = node->data.return_stmt.values[i];
            AstNode *at = return_val;
            const char *offender = return_value_is_type_argument(checker, return_val, &what, &at);
            if (!offender) continue;
            diagnostic_error_code_formatted(checker->diag, "E3139",
                NODE_FILE(checker, at), at->token.line,
                at->token.column, 0, what, offender, declared, declared);
        }
    }

    /* E3072: `return nil` from a function returning a non-nullable type
     * (struct, int, string, array, etc.). nil is only valid for pointer
     * and error return types. */
    if (checker->current_return_count > 0 && node->data.return_stmt.count > 0) {
        AstNode *return_val = node->data.return_stmt.values[0];
        if (return_val->kind == NODE_NIL_VALUE) {
            GrayType *expected = checker->current_return_types[0];
            if (expected && expected->kind != TK_POINTER &&
                expected->kind != TK_ERROR && expected->kind != TK_UNKNOWN &&
                expected->kind != TK_NIL && expected->kind != TK_VOID) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "cannot return 'nil' from a function that returns '%s'; nil is only valid for pointer and error types",
                    type_name(expected));
                diagnostic_error_message(checker->diag, "E3072", msg,
                    NODE_FILE(checker, return_val), return_val->token.line, return_val->token.column, 0);
            }
        }
    }

    /* Check return type matches function signature */
    if (checker->current_return_count == 0 && node->data.return_stmt.count > 0) {
        /* Returning a value from a void function; suppress when
         * we've rewritten main()'s declared return type to void
         * after E4008 (). */
        if (!checker->current_main_return_suppressed) {
            diagnostic_error_message(checker->diag, "E3006", "cannot return a value from a void function",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    } else if (checker->current_return_count > 0 && node->data.return_stmt.count == 0 &&
               !checker->current_has_named_returns) {
        /* Bare return in non-void function (without named returns) */
        diagnostic_error_message(checker->diag, "E3006",
            "missing return value; function expects a return value",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    } else if (checker->current_return_count > 0 && node->data.return_stmt.count > 0 &&
               node->data.return_stmt.count != checker->current_return_count) {
        /* E3040: wrong number of return values (skip or_return synthetic returns
         * which have count=1 but the function expects more; that's handled by codegen) */
        bool is_or_return_synthetic = false;
        if (node->data.return_stmt.count == 1 &&
            node->data.return_stmt.values[0]->kind == NODE_MEMBER_EXPR) {
            AstNode *obj = node->data.return_stmt.values[0]->data.member.object;
            if (obj->kind == NODE_LABEL && strncmp(obj->data.label.value, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) == 0) {
                is_or_return_synthetic = true;
            }
        }
        if (!is_or_return_synthetic) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "function expects %d return value(s), got %d",
                checker->current_return_count, node->data.return_stmt.count);
            diagnostic_error_message(checker->diag, "E3040", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    } else if (checker->current_return_count > 0 && node->data.return_stmt.count > 0 &&
               node->data.return_stmt.count == checker->current_return_count) {
        /* Check first return value type (skip for or_return synthetic returns) */
        GrayType *ret_t = resolve_expression(checker, node->data.return_stmt.values[0]);
        GrayType *expected = checker->current_return_types[0];
        /* : same push as var_decl; when a func-pointer call
         * is the return value and the function's declared return
         * type is concrete, push it onto the call node so codegen
         * uses the right function-pointer return cast. */
        if (ret_t->kind == TK_UNKNOWN && expected->kind != TK_UNKNOWN &&
            expected->kind != TK_VOID &&
            node->data.return_stmt.values[0]->kind == NODE_CALL_EXPR) {
            typetable_set(checker->type_table, node->data.return_stmt.values[0], expected);
            ret_t = expected;
        }
        /* One report per mismatch. A struct or enum pair is checked both by
         * types_assignable() and by a display-name comparison — the latter so
         * cross-module aliases (e.g. types_Item vs Item) unify correctly — and
         * either failing is a mismatch. Reporting them separately meant one
         * mistake surfaced twice under the same code with two wordings.
         * The message names the kind for struct and enum types, matching how
         * argument mismatches are worded elsewhere in the checker. */
        if (ret_t->kind != TK_UNKNOWN && expected->kind != TK_UNKNOWN &&
            ret_t->kind != TK_NIL) {
            bool assignable = types_assignable(checker, expected, ret_t);
            bool named_pair = ret_t->name && expected->name &&
                ret_t->kind == expected->kind &&
                (ret_t->kind == TK_STRUCT || ret_t->kind == TK_ENUM);
            bool same_named = true;
            if (named_pair) {
                same_named = ret_t->kind == TK_STRUCT
                    ? typechecker_same_struct_type(checker, ret_t->name, expected->name)
                    : typechecker_same_enum_type(checker, ret_t->name, expected->name);
            }
            if (!assignable || !same_named) {
                const char *kind = "";
                if (named_pair) kind = ret_t->kind == TK_STRUCT ? "struct " : "enum ";
                char *msg = NULL;
                msg = named_pair
                    ? typechecker_format(checker,
                        "return type mismatch: expected %s'%s', got %s'%s'",
                        kind, type_display_name(checker, expected),
                        kind, type_display_name(checker, ret_t))
                    : typechecker_format(checker,
                        "return type mismatch: expected %s, got %s",
                        type_display_name(checker, expected), type_display_name(checker, ret_t));
                diagnostic_error_message(checker->diag, "E5049", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        /* E3066: func signature mismatch in return */
        if (func_types_mismatch(ret_t, expected)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "cannot return %s from function declared to return %s",
                type_display_name(checker, ret_t), type_display_name(checker, expected));
            diagnostic_error_message(checker->diag, "E3066", msg,
                NODE_FILE(checker, node->data.return_stmt.values[0]),
                node->data.return_stmt.values[0]->token.line,
                node->data.return_stmt.values[0]->token.column, 0);
        }
        /* Array element type mismatch in return */
        if (ret_t->kind == TK_ARRAY && expected->kind == TK_ARRAY &&
            ret_t->element_type && expected->element_type &&
            !typechecker_same_array_element(checker, ret_t->element_type, expected->element_type)) {
            GrayType *re = type_from_name(ret_t->element_type);
            GrayType *ee = type_from_name(expected->element_type);
            if (!(re && ee && is_int_kind(re->kind) && is_int_kind(ee->kind))) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "return type mismatch: expected '%s', got '%s'",
                    type_display_name(checker, expected), type_display_name(checker, ret_t));
                diagnostic_error_message(checker->diag, "E5049", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        /* Map key/value type mismatch in return */
        if (ret_t->kind == TK_MAP && expected->kind == TK_MAP) {
            bool key_mismatch = ret_t->key_type && expected->key_type &&
                strcmp(ret_t->key_type, expected->key_type) != 0;
            bool val_mismatch = ret_t->value_type && expected->value_type &&
                strcmp(ret_t->value_type, expected->value_type) != 0;
            if (key_mismatch || val_mismatch) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "return type mismatch: expected '%s', got '%s'",
                    type_display_name(checker, expected), type_display_name(checker, ret_t));
                diagnostic_error_message(checker->diag, "E5049", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        /* : pointer depth mismatch (e.g. returning ^^int
         * from a function declared -> ^int). Both sides are
         * TK_POINTER so the kind check above passes, but the
         * element_type strings differ ("int" vs "^int"). */
        if (ret_t->kind == TK_POINTER && expected->kind == TK_POINTER &&
            ret_t->element_type && expected->element_type &&
            strcmp(ret_t->element_type, expected->element_type) != 0) {
            /* Build human-readable pointer type strings (strip module prefix) */
            const char *exp_inner = struct_display_name(checker, expected->element_type);
            if (exp_inner == expected->element_type) exp_inner = enum_display_name(checker, expected->element_type);
            const char *got_inner = struct_display_name(checker, ret_t->element_type);
            if (got_inner == ret_t->element_type) got_inner = enum_display_name(checker, ret_t->element_type);
            char exp_str[TYPE_NAME_MAX], got_str[TYPE_NAME_MAX];
            snprintf(exp_str, sizeof(exp_str), "^%s", exp_inner);
            snprintf(got_str, sizeof(got_str), "^%s", got_inner);
            char *msg = NULL;
            msg = typechecker_format(checker,
                "return type mismatch: expected '%s', got '%s'",
                exp_str, got_str);
            diagnostic_error_message(checker->diag, "E5049", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* E5024/E5049: a returned variable whose signedness crosses the declared
         * return type needs an explicit cast, in either direction. */
        if (checker->current_return_type_names && checker->current_return_type_names[0] &&
            node->data.return_stmt.values[0]->kind == NODE_LABEL) {
            const char *ret_tn = checker->current_return_type_names[0];
            const char *src_name = node->data.return_stmt.values[0]->data.label.value;
            Symbol *src_sym = scope_lookup(checker->current_scope, src_name);
            const char *src_tn = src_sym ? src_sym->declared_type : NULL;
            if (src_tn && is_unsigned_type(ret_tn) && is_signed_int_type(src_tn)) {
                diagnostic_error_code_formatted(checker->diag, "E5024", NODE_FILE(checker, node), node->token.line, node->token.column, 0, src_tn, ret_tn);
            } else if (src_tn && is_signed_int_type(ret_tn) && is_unsigned_type(src_tn) &&
                       !unsigned_widens_to_signed(ret_tn, src_tn)) {
                diagnostic_error_message(checker->diag, "E5049",
                    typechecker_format(checker,
                        "return type mismatch: cannot return unsigned '%s' as signed '%s'; use cast(value, %s) to convert explicitly",
                        src_tn, ret_tn, ret_tn),
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        /* E3036: an out-of-range integer literal in any return slot
         * (do f() -> byte { return 300 }). */
        if (checker->current_return_type_names) {
            for (int i = 0; i < node->data.return_stmt.count &&
                            i < checker->current_return_count; i++) {
                const char *slot_tn = checker->current_return_type_names[i];
                int64_t ret_lit;
                bool ret_lit_neg;
                if (slot_tn && try_get_signed_literal_int(node->data.return_stmt.values[i], &ret_lit, &ret_lit_neg)) {
                    AstNode *rv = node->data.return_stmt.values[i];
                    check_integer_range(checker->diag, NODE_FILE(checker, rv),
                        rv->token.line, rv->token.column, slot_tn, ret_lit, ret_lit_neg);
                }
            }
        }
        /* Non-primary return slots. Everything above inspects values[0]
         * only; without this a `return 0, NetErr.DNS_FAIL` into a
         * `-> (int, DbErr)` slot passed unchecked and leaked a C type
         * error to the user. Mirrors the primary slot's core check:
         * assignability plus a same-type check for named struct/enum
         * pairs (types_assignable() unifies same-kind enums on its own). */
        for (int i = 1; i < node->data.return_stmt.count &&
                        i < checker->current_return_count; i++) {
            GrayType *slot_ret = resolve_expression(checker, node->data.return_stmt.values[i]);
            GrayType *slot_exp = checker->current_return_types[i];
            if (!slot_ret || !slot_exp) continue;
            if (slot_ret->kind == TK_UNKNOWN || slot_exp->kind == TK_UNKNOWN ||
                slot_ret->kind == TK_NIL) continue;
            bool slot_assignable = types_assignable(checker, slot_exp, slot_ret);
            bool slot_named_pair = slot_ret->name && slot_exp->name &&
                slot_ret->kind == slot_exp->kind &&
                (slot_ret->kind == TK_STRUCT || slot_ret->kind == TK_ENUM);
            bool slot_same_named = true;
            if (slot_named_pair) {
                slot_same_named = slot_ret->kind == TK_STRUCT
                    ? typechecker_same_struct_type(checker, slot_ret->name, slot_exp->name)
                    : typechecker_same_enum_type(checker, slot_ret->name, slot_exp->name);
            }
            if (slot_assignable && slot_same_named) continue;
            const char *slot_kind = slot_named_pair
                ? (slot_ret->kind == TK_STRUCT ? "struct " : "enum ") : "";
            AstNode *sv = node->data.return_stmt.values[i];
            char *msg = slot_named_pair
                ? typechecker_format(checker,
                    "return type mismatch: expected %s'%s', got %s'%s'",
                    slot_kind, type_display_name(checker, slot_exp),
                    slot_kind, type_display_name(checker, slot_ret))
                : typechecker_format(checker,
                    "return type mismatch: expected %s, got %s",
                    type_display_name(checker, slot_exp), type_display_name(checker, slot_ret));
            diagnostic_error_message(checker->diag, "E5049", msg,
                NODE_FILE(checker, sv), sv->token.line, sv->token.column, 0);
        }
        /* E3073: named return variable must be the value returned */
        if (checker->current_has_named_returns && checker->current_return_names) {
            for (int i = 0; i < node->data.return_stmt.count && i < checker->current_return_count; i++) {
                if (!checker->current_return_names[i]) continue;
                AstNode *return_val = node->data.return_stmt.values[i];
                bool is_named_var = (return_val->kind == NODE_LABEL &&
                    strcmp(return_val->data.label.value, checker->current_return_names[i]) == 0);
                if (!is_named_var) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "function must return named variable '%s', not a different expression",
                        checker->current_return_names[i]);
                    diagnostic_error_message(checker->diag, "E3080", msg,
                        NODE_FILE(checker, return_val), return_val->token.line, return_val->token.column, 0);
                }
            }
        }
    }
}

/* ============================================================================
 * Pointer checker — @mem arena lifetime tracking (E3164 / E3165 / E3166)
 *
 * checker->arenas holds one ArenaLifetime per @mem arena variable seen in the
 * function being checked. mem.destroy() marks it destroyed; mem.reset() bumps
 * its epoch. A pointer bound from mem.init()/mem.alloc() records the arena and
 * the epoch-at-binding on its Symbol; dereferencing it after the arena is
 * destroyed is E3164, after a reset past that epoch is E3165. A destroy/reset
 * of an already-destroyed arena is E3166.
 *
 * The state is flow-sensitive: check_if_stmt / check_when_stmt snapshot the
 * array, check each branch from the snapshot, then join — destroyed on ANY
 * path wins, epoch is the max — so a destroy on one branch is seen after the
 * merge. Loop checkers pre-mark any arena a loop body destroys/resets (unless
 * the handle is loop-local), so a use on a later iteration is caught. This
 * replaces the old flat `destroyed_arenas` list, which saw only the direct
 * `mem.destroy(a); mem.destroy(a)` form.
 * ========================================================================== */

static ArenaLifetime *pc_arena_get(TypeChecker *checker, const char *name) {
    for (int i = 0; i < checker->arena_count; i++)
        if (strcmp(checker->arenas[i].name, name) == 0) return &checker->arenas[i];
    return NULL;
}

static ArenaLifetime *pc_arena_ensure(TypeChecker *checker, const char *name) {
    ArenaLifetime *a = pc_arena_get(checker, name);
    if (a) return a;
    GROW_ARRAY(checker->arenas, checker->arena_count, checker->arena_cap);
    a = &checker->arenas[checker->arena_count++];
    memset(a, 0, sizeof *a);
    a->name = name;
    return a;
}

/* True + fills *out_fn / *out_arena when `call` is a @mem lifecycle call on an
 * arena named by a bare variable: mem.arena / mem.init / mem.alloc / mem.reset
 * / mem.destroy, written `mem.fn(a, ...)` or bare `fn(a, ...)` under `using
 * mem`. For mem.arena(size) *out_arena is NULL (arg 0 is a size). */
/* A stable string key for an arena-handle expression: a bare variable name
 * ("a"), or a dotted field-access chain reaching one buried in a struct
 * ("h.a", "h.inner.a"). Used as ArenaLifetime.name and Symbol.mem_arena /
 * field_mem_arena alike — every consumer treats an arena's identity as an
 * opaque string, so a struct-field handle needs nothing more than a key
 * that's stable and distinct from any other arena's. Only a plain field
 * chain is recognized (no array index, no pointer deref) — the common case
 * an arena struct field actually takes; anything else returns NULL rather
 * than risk two different arenas colliding on the same key. */
static const char *pc_arena_path_key(TypeChecker *checker, AstNode *expr) {
    if (!expr) return NULL;
    if (expr->kind == NODE_LABEL) return expr->data.label.value;
    if (expr->kind == NODE_MEMBER_EXPR) {
        const char *base = pc_arena_path_key(checker, expr->data.member.object);
        if (!base || !expr->data.member.member) return NULL;
        char buf[MSG_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%s.%s", base, expr->data.member.member);
        return arena_copy_string(checker->arena, buf);
    }
    return NULL;
}

static bool pc_is_mem_call(TypeChecker *checker, AstNode *call,
                           const char **out_fn, const char **out_arena) {
    if (!call || call->kind != NODE_CALL_EXPR || call->data.call.arg_count < 1)
        return false;
    AstNode *fn = call->data.call.function;
    const char *fname = NULL;
    if (fn->kind == NODE_MEMBER_EXPR && fn->data.member.object &&
        fn->data.member.object->kind == NODE_LABEL &&
        strcmp(fn->data.member.object->data.label.value, "mem") == 0) {
        fname = fn->data.member.member;
    } else if (fn->kind == NODE_LABEL) {
        bool mem_using = false;
        for (int i = 0; i < checker->using_module_count; i++) {
            if (!using_module_accessible(checker, i)) continue;
            if (strcmp(checker->using_modules[i], "mem") == 0) { mem_using = true; break; }
        }
        if (mem_using) fname = fn->data.label.value;
    }
    if (!fname) return false;
    if (strcmp(fname, "arena") != 0 && strcmp(fname, "init") != 0 &&
        strcmp(fname, "alloc") != 0 && strcmp(fname, "reset") != 0 &&
        strcmp(fname, "destroy") != 0)
        return false;
    *out_fn = fname;
    if (strcmp(fname, "arena") == 0) { *out_arena = NULL; return true; }
    const char *key = pc_arena_path_key(checker, call->data.call.args[0]);
    if (!key) return false;
    *out_arena = key;
    return true;
}

/* Apply a @mem lifecycle call's effect to arena state, reporting E3166 for a
 * destroy/reset of an already-destroyed arena. `bind_name` is the variable a
 * mem.arena() result is bound to, or NULL for a bare statement call. */
/* Apply a destroy or reset to arena `arena_name`'s lifetime state, reporting
 * E3166 if it is already destroyed. `disp` is the call spelling for the
 * message (e.g. "mem.destroy", or a callee name for a cross-function
 * destroy applied on the caller's behalf at a call site). Shared by the
 * direct mem.destroy()/mem.reset() form and the cross-function summary. */
static void pc_apply_arena_lifecycle(TypeChecker *checker, const char *arena_name,
                                     bool is_destroy, AstNode *at, const char *disp) {
    ArenaLifetime *a = pc_arena_ensure(checker, arena_name);
    /* A destroy scheduled via `ensure` still runs, at scope exit, no matter
     * what happens between now and then — so an explicit destroy reaching
     * here after one is already pending is a second destroy too, same as
     * a->destroyed. A reset is unaffected: resetting (or re-resetting) an
     * arena whose actual destroy hasn't run yet is safe. */
    if (a->destroyed || (is_destroy && a->ensure_destroy_pending)) {
        diagnostic_error_code_formatted(checker->diag, "E3166",
            NODE_FILE(checker, at), at->token.line, at->token.column, 0,
            disp, arena_name, arena_name);
        return;
    }
    a->end_file = NODE_FILE(checker, at);
    a->end_line = at->token.line;
    if (is_destroy) {
        a->destroyed = true;
        a->end_was_reset = false;
    } else {
        a->epoch++;
        a->end_was_reset = true;
    }
}

/* ensure mem.destroy(a): mark the destroy pending rather than applying it
 * immediately through pc_apply_arena_lifecycle. The arena is not actually
 * freed until this function returns, so an ordinary use of it in the
 * statements that follow (the entire point of scheduling cleanup with
 * `ensure`) must stay legal — only a second destroy attempt on the same
 * arena, explicit or via another ensure, is the genuine double-free E3166
 * exists to catch. */
static void pc_apply_ensure_mem_call(TypeChecker *checker, AstNode *call, AstNode *at) {
    const char *fn = NULL, *arena = NULL;
    if (!pc_is_mem_call(checker, call, &fn, &arena)) return;
    if (!arena || strcmp(fn, "destroy") != 0) return;
    ArenaLifetime *a = pc_arena_ensure(checker, arena);
    const char *disp = (call->data.call.function->kind == NODE_MEMBER_EXPR)
        ? "mem.destroy" : fn;
    if (a->destroyed || a->ensure_destroy_pending) {
        diagnostic_error_code_formatted(checker->diag, "E3166",
            NODE_FILE(checker, at), at->token.line, at->token.column, 0,
            disp, arena, arena);
        return;
    }
    a->ensure_destroy_pending = true;
}

static void pc_apply_mem_call(TypeChecker *checker, AstNode *call, AstNode *at,
                              const char *bind_name) {
    const char *fn = NULL, *arena = NULL;
    if (!pc_is_mem_call(checker, call, &fn, &arena)) return;

    if (strcmp(fn, "arena") == 0) {
        if (bind_name) {
            ArenaLifetime *a = pc_arena_ensure(checker, bind_name);
            a->destroyed = false;
            a->epoch = 0;
        }
        return;
    }
    if (!arena) return;

    if (strcmp(fn, "destroy") == 0 || strcmp(fn, "reset") == 0) {
        const char *disp = (call->data.call.function->kind == NODE_MEMBER_EXPR)
            ? (strcmp(fn, "destroy") == 0 ? "mem.destroy" : "mem.reset")
            : fn;
        pc_apply_arena_lifecycle(checker, arena, strcmp(fn, "destroy") == 0, at, disp);
    }
    /* init / alloc: no lifetime effect; the pointer binding is recorded at the
     * var-decl (pc_bind_mem_pointer). */
}

/* Deepest @mem arena binding an expression carries: a direct mem.init()/
 * mem.alloc() call, a variable already bound to one (directly or via its own
 * field_mem_arena — reading a mem-pointer back out of a tracked aggregate),
 * or a struct/array/map literal with such a value buried in a field or
 * element. Mirrors container_literal_origin()'s walk, for @mem arenas
 * instead of scope depth. Fills out_arena and out_epoch and returns true on
 * a match; leaves them untouched and returns false otherwise. */
static bool pc_mem_pointer_in_expr(TypeChecker *checker, AstNode *value,
                                   const char **out_arena, int *out_epoch,
                                   bool *out_via_field) {
    if (!value) return false;
    const char *fn = NULL, *arena = NULL;
    if (value->kind == NODE_CALL_EXPR &&
        pc_is_mem_call(checker, value, &fn, &arena) && arena &&
        (strcmp(fn, "init") == 0 || strcmp(fn, "alloc") == 0)) {
        ArenaLifetime *a = pc_arena_ensure(checker, arena);
        *out_arena = arena;
        *out_epoch = a->epoch;
        *out_via_field = false;
        return true;
    }
    if (value->kind == NODE_CALL_EXPR) {
        /* A call to a user function that itself returns a @mem pointer
         * forwarded from one of its own arena parameters — directly
         * (`do make(a Arena) -> ^int { return mem.alloc(a, 1) }`) or buried
         * in a returned literal (`do make(a Arena) -> Box { return Box{p:
         * mem.alloc(a, 1)} }`) — called as `p = make(a)` / `b = make(a)`.
         * resolve_call_sig() (not the _in_body variant: this runs during
         * the caller's own live Pass-2 walk, so checker->current_scope is
         * this call's real scope) plus the callee's returns_param_mem_alloc
         * / _field summary say which of *this* call's arguments names the
         * arena and whether it's direct or buried; binding to that
         * argument's own live epoch (not the callee's, which the callee's
         * own frame no longer exists to hold) is what lets a destroy the
         * caller performs after the call still be seen. */
        FuncSig *callee = resolve_call_sig(checker, value);
        if (!callee) return false;
        pc_ensure_mem_summary(checker, callee);
        for (int k = 0; k < callee->param_count &&
                        k < value->data.call.arg_count && k < 64; k++) {
            unsigned long long bit = 1ull << k;
            if (!((callee->returns_param_mem_alloc | callee->returns_param_mem_alloc_field) & bit))
                continue;
            AstNode *arg = value->data.call.args[k];
            if (arg->kind != NODE_LABEL) continue;
            ArenaLifetime *a = pc_arena_get(checker, arg->data.label.value);
            *out_arena = arg->data.label.value;
            *out_epoch = a ? a->epoch : 0;
            *out_via_field = (callee->returns_param_mem_alloc_field & bit) != 0;
            return true;
        }
        return false;
    }
    if (value->kind == NODE_LABEL) {
        Symbol *src = scope_lookup(checker->current_scope, value->data.label.value);
        if (!src) return false;
        if (src->mem_arena) {
            *out_arena = src->mem_arena;
            *out_epoch = src->mem_epoch;
            *out_via_field = false;
            return true;
        }
        if (src->field_mem_arena) {
            *out_arena = src->field_mem_arena;
            *out_epoch = src->field_mem_epoch;
            *out_via_field = true;
            return true;
        }
        return false;
    }
    /* A struct/array/map literal: anything found inside it is buried in a
     * field from the outside, regardless of whether the recursive call
     * itself found a direct or already-field match at the inner level. */
    bool inner_via_field = false;
    switch (value->kind) {
    case NODE_STRUCT_VALUE:
        for (int i = 0; i < value->data.struct_value.count; i++)
            if (pc_mem_pointer_in_expr(checker, value->data.struct_value.field_values[i],
                                       out_arena, out_epoch, &inner_via_field)) {
                *out_via_field = true;
                return true;
            }
        return false;
    case NODE_ARRAY_VALUE:
        for (int i = 0; i < value->data.array_value.count; i++)
            if (pc_mem_pointer_in_expr(checker, value->data.array_value.elements[i],
                                       out_arena, out_epoch, &inner_via_field)) {
                *out_via_field = true;
                return true;
            }
        return false;
    case NODE_MAP_VALUE:
        for (int i = 0; i < value->data.map_value.count; i++)
            if (pc_mem_pointer_in_expr(checker, value->data.map_value.values[i],
                                       out_arena, out_epoch, &inner_via_field)) {
                *out_via_field = true;
                return true;
            }
        return false;
    default:
        return false;
    }
}

/* Record on a freshly declared symbol that it points into a @mem arena
 * (value is itself a mem-bound pointer expression — mem.init()/mem.alloc(),
 * or an alias of an already-tracked pointer), or that it's an aggregate
 * carrying one buried in a field/element (a struct/array/map literal, or a
 * call forwarding one). Which of the two applies comes from
 * pc_mem_pointer_in_expr() itself — not merely value's own AST shape, since
 * a CALL_EXPR can return either a bare pointer or an aggregate. */
static void pc_bind_mem_pointer(TypeChecker *checker, Symbol *sym, AstNode *value) {
    if (!sym || !value) return;
    const char *arena = NULL;
    int epoch = 0;
    bool via_field = false;
    if (!pc_mem_pointer_in_expr(checker, value, &arena, &epoch, &via_field)) return;
    if (via_field) {
        sym->field_mem_arena = arena;
        sym->field_mem_epoch = epoch;
    } else {
        sym->mem_arena = arena;
        sym->mem_epoch = epoch;
    }
}

/* A dereference `ptr_expr^` (as `p^`, `p^.field`, `p^[i]`). If ptr_expr roots
 * at a mem-bound pointer, verify its arena is still alive. */
static void pc_check_mem_deref(TypeChecker *checker, AstNode *ptr_expr, AstNode *at) {
    AstNode *inner = ptr_expr;
    while (inner && inner->kind == NODE_POSTFIX_EXPR &&
           inner->data.postfix.op == TOK_CARET)
        inner = inner->data.postfix.left;
    /* A bare pointer variable (`p^`) is tracked on its own symbol
     * (mem_arena); one reached through a field or element chain (`b.p^`,
     * `arr[i]^`) is tracked on the root aggregate's field_mem_arena — the
     * slot itself carries no symbol of its own to hang the binding on. */
    bool is_bare = inner && inner->kind == NODE_LABEL;
    const char *root = is_bare ? inner->data.label.value
                               : assignment_target_root_name(inner);
    if (!root) return;
    Symbol *sym = scope_lookup(checker->current_scope, root);
    if (!sym) return;
    const char *arena = is_bare ? sym->mem_arena : sym->field_mem_arena;
    int bound_epoch = is_bare ? sym->mem_epoch : sym->field_mem_epoch;
    if (!arena) return;
    ArenaLifetime *a = pc_arena_get(checker, arena);
    if (!a) return;
    if (a->destroyed || a->premarked_destroyed) {
        diagnostic_error_code_formatted(checker->diag, "E3164",
            NODE_FILE(checker, at), at->token.line, at->token.column, 0,
            root, arena);
    } else if (a->epoch > bound_epoch) {
        diagnostic_error_code_formatted(checker->diag, "E3165",
            NODE_FILE(checker, at), at->token.line, at->token.column, 0,
            root, arena);
    }
}

/* --- branch-sensitive snapshot / join --- */

typedef struct { ArenaLifetime *rows; int count; } PcArenaSnap;

static PcArenaSnap pc_snap(TypeChecker *checker) {
    PcArenaSnap s;
    s.count = checker->arena_count;
    s.rows = s.count ? xmalloc(sizeof(ArenaLifetime) * (size_t)s.count) : NULL;
    if (s.count)
        memcpy(s.rows, checker->arenas, sizeof(ArenaLifetime) * (size_t)s.count);
    return s;
}

static void pc_snap_free(PcArenaSnap s) { free(s.rows); }

/* Restore live state to a snapshot. Arena rows are append-only and
 * index-stable within a function, and any handle a branch declared is
 * block-scoped, so truncating back to the snapshot's count is correct. */
static void pc_restore(TypeChecker *checker, PcArenaSnap s) {
    for (int i = 0; i < s.count && i < checker->arena_count; i++)
        checker->arenas[i] = s.rows[i];
    checker->arena_count = s.count;
}

/* Merge `other` into the live state: an arena is destroyed after the join if
 * destroyed on either path; its epoch is the higher of the two. */
static void pc_join(TypeChecker *checker, PcArenaSnap other) {
    for (int i = 0; i < other.count && i < checker->arena_count; i++) {
        ArenaLifetime *live = &checker->arenas[i];
        ArenaLifetime *o = &other.rows[i];
        if (o->destroyed && !live->destroyed) {
            live->destroyed = true;
            live->end_file = o->end_file;
            live->end_line = o->end_line;
            live->end_was_reset = o->end_was_reset;
        }
        if (o->epoch > live->epoch) {
            live->epoch = o->epoch;
            if (!live->destroyed) {
                live->end_file = o->end_file;
                live->end_line = o->end_line;
                live->end_was_reset = o->end_was_reset;
            }
        }
    }
}

/* Pre-mark every arena a loop body destroys or resets, so a dereference on a
 * later iteration is caught. A handle declared inside the body is fresh each
 * iteration and is left alone (the idiomatic per-iteration scratch arena). */
static void pc_premark_loop_body(TypeChecker *checker, AstNode *node, AstNode *body) {
    if (!node) return;
    switch (node->kind) {
    case NODE_CALL_EXPR: {
        const char *fn = NULL, *arena = NULL;
        if (pc_is_mem_call(checker, node, &fn, &arena) && arena &&
            (strcmp(fn, "destroy") == 0 || strcmp(fn, "reset") == 0) &&
            !declared_in_subtree(body, arena)) {
            ArenaLifetime *a = pc_arena_ensure(checker, arena);
            /* premarked_destroyed, not destroyed — see the field comment.
             * The body's own walk (below) applies this destroy for real when
             * it reaches the statement; premarking it here as `destroyed`
             * would make that real statement collide with its own echo. */
            if (strcmp(fn, "destroy") == 0) a->premarked_destroyed = true;
            else a->epoch++;
        } else {
            /* A call to a helper whose own cross-function @mem summary says
             * it destroys/resets an arena passed to it — same cross-
             * function effect pc_mem_walk()/resolve_call_expr() apply at an
             * ordinary (non-loop) call site, needed here too: a loop that
             * hands the arena to a cleanup helper on some iteration is the
             * same hazard as calling mem.destroy(a) directly in the body.
             * Called synchronously from this function's own Pass-2 walk (not
             * a lazily-triggered summary), so checker->current_scope is
             * correctly this function's own — resolve_call_sig() sees
             * through a func-ref call here too. */
            FuncSig *callee = resolve_call_sig(checker, node);
            if (callee) {
                pc_ensure_mem_summary(checker, callee);
                unsigned long long effect =
                    callee->destroys_param_arena | callee->resets_param_arena;
                for (int k = 0; k < callee->param_count &&
                                k < node->data.call.arg_count && k < 64 && effect; k++) {
                    if (!(effect & (1ull << k))) continue;
                    AstNode *arg = node->data.call.args[k];
                    const char *root = (arg->kind == NODE_LABEL)
                        ? arg->data.label.value
                        : assignment_target_root_name(arg);
                    if (!root || declared_in_subtree(body, root)) continue;
                    const char *key = pc_mem_forward_key(checker, arg,
                        callee->mem_param_field[k]);
                    if (!key) continue;
                    ArenaLifetime *a = pc_arena_ensure(checker, key);
                    if (callee->destroys_param_arena & (1ull << k))
                        a->premarked_destroyed = true;
                    else
                        a->epoch++;
                }
            }
        }
        for (int i = 0; i < node->data.call.arg_count; i++)
            pc_premark_loop_body(checker, node->data.call.args[i], body);
        break;
    }
    case NODE_VAR_DECL:
        pc_premark_loop_body(checker, node->data.var_decl.value, body);
        break;
    case NODE_ASSIGN_STMT:
        pc_premark_loop_body(checker, node->data.assign.value, body);
        break;
    case NODE_EXPR_STMT:
        pc_premark_loop_body(checker, node->data.expr_stmt.expr, body);
        break;
    case NODE_BLOCK_STMT:
        for (int i = 0; i < node->data.block.count; i++)
            pc_premark_loop_body(checker, node->data.block.stmts[i], body);
        break;
    case NODE_IF_STMT:
        pc_premark_loop_body(checker, node->data.if_stmt.consequence, body);
        pc_premark_loop_body(checker, node->data.if_stmt.alternative, body);
        break;
    case NODE_WHEN_STMT:
        for (int i = 0; i < node->data.when_stmt.case_count; i++)
            pc_premark_loop_body(checker, node->data.when_stmt.cases[i].body, body);
        pc_premark_loop_body(checker, node->data.when_stmt.default_body, body);
        break;
    case NODE_FOR_STMT:      pc_premark_loop_body(checker, node->data.for_stmt.body, body); break;
    case NODE_FOR_EACH_STMT: pc_premark_loop_body(checker, node->data.for_each.body, body); break;
    case NODE_WHILE_STMT:    pc_premark_loop_body(checker, node->data.while_stmt.body, body); break;
    case NODE_LOOP_STMT:     pc_premark_loop_body(checker, node->data.loop_stmt.body, body); break;
    default: break;
    }
}

static void check_expr_stmt(TypeChecker *checker, AstNode *node) {
    GrayType *expr_t = resolve_expression(checker, node->data.expr_stmt.expr);
    /* E3081: bare function name used as statement without call */
    AstNode *expr = node->data.expr_stmt.expr;
    if (expr && expr->kind == NODE_LABEL) {
        const char *name = expr->data.label.value;
        if (typechecker_is_builtin(name) || find_func(checker, name)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "function '%s' used as a statement without being called; did you mean '%s()'?",
                name, name);
            diagnostic_error_message(checker->diag, "E3081", msg,
                NODE_FILE(checker, expr), expr->token.line, expr->token.column, 0);
        }
    }
    if (expr && expr->kind == NODE_CALL_EXPR && expr_t &&
        expr_t->kind != TK_VOID && expr_t->kind != TK_UNKNOWN) {
        AstNode *fn = expr->data.call.function;
        const char *function_name = NULL;
        if (fn->kind == NODE_LABEL) function_name = fn->data.label.value;
        /* Don't warn for known side-effect functions */
        bool is_side_effect = function_name && (
            strcmp(function_name, "println") == 0 || strcmp(function_name, "print") == 0 ||
            strcmp(function_name, "eprintln") == 0 || strcmp(function_name, "eprint") == 0 ||
            strcmp(function_name, "panic") == 0 || strcmp(function_name, "assert") == 0 ||
            strcmp(function_name, "exit") == 0 || strcmp(function_name, "sleep_s") == 0 ||
            strcmp(function_name, "sleep_ms") == 0 || strcmp(function_name, "sleep_ns") == 0 ||
            strcmp(function_name, "system") == 0);
        /* For member expression calls, check if the return type is void —
         * only warn about non-void return values being discarded */
        if (fn->kind == NODE_MEMBER_EXPR) {
            /* expr_t is already the resolved return type from resolve_expression above.
             * If it's void or unknown, this is a side-effect call; no warning needed. */
            if (expr_t->kind == TK_VOID || expr_t->kind == TK_UNKNOWN) {
                is_side_effect = true;
            } else {
                /* Build display name for the error message */
                const char *obj_name = ast_member_qualifier(fn);
                const char *mem_name = obj_name ? fn->data.member.member : NULL;
                if (obj_name && mem_name && !is_side_effect) {
                    /* Check if struct function has #discard attribute */
                    char prefixed[MSG_BUF_SIZE];
                    module_member_key(checker, obj_name, mem_name, prefixed, sizeof(prefixed));
                    FuncSig *fs = find_func(checker, prefixed);
                    if (!fs || !fs->is_discard) {
                        char full[MSG_BUF_SIZE];
                        const char *display_obj = struct_display_name(checker, obj_name);
                        snprintf(full, sizeof(full), "%s.%s()", display_obj, mem_name);
                        char *msg = typechecker_format(checker, "return value of '%s' is not used", full);
                        diagnostic_error_help(checker->diag, "E5011", msg,
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                            "assign the result to a variable, or use 'mut _ = ...' to discard it");
                    }
                }
                is_side_effect = true; /* already handled */
            }
        }
        if (!is_side_effect && function_name) {
            FuncSig *fs = find_func(checker, function_name);
            if (!fs || !fs->is_discard) {
                char full[MSG_BUF_SIZE];
                snprintf(full, sizeof(full), "%s()", function_name);
                char *msg = typechecker_format(checker, "return value of '%s' is not used", full);
                diagnostic_error_help(checker->diag, "E5011", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    "assign the result to a variable, or use 'mut _ = ...' to discard it");
            }
        }
    }
    /* Pointer checker: a bare @mem lifecycle call as a statement —
     * mem.destroy(a) / mem.reset(a). Updates arena lifetime state and reports
     * E3166 for a repeat destroy/reset. */
    if (expr && expr->kind == NODE_CALL_EXPR)
        pc_apply_mem_call(checker, expr, node, NULL);
}

static void check_if_stmt(TypeChecker *checker, AstNode *node) {
    GrayType *cond_t = resolve_expression(checker, node->data.if_stmt.condition);
    /* E3038 (): void function call as condition. The same check
     * already exists for variable assignment and arithmetic; wire
     * it up for control-flow conditions too. The 'or' branch of an
     * if chain is parsed as a nested NODE_IF_STMT, so this one spot
     * covers 'if' and every subsequent 'or'. */
    if (cond_t && cond_t->kind == TK_VOID) {
        AstNode *c = node->data.if_stmt.condition;
        char *msg = NULL;
        if (c && c->kind == NODE_CALL_EXPR && c->data.call.function &&
            c->data.call.function->kind == NODE_LABEL) {
            msg = typechecker_format(checker,
                "cannot use void function '%s' as condition; 'if' requires a bool expression",
                c->data.call.function->data.label.value);
        } else {
            msg = typechecker_format(checker,
                "cannot use void expression as condition; 'if' requires a bool expression");
        }
        diagnostic_error_message(checker->diag, "E3038", msg,
            NODE_FILE(checker, c), c->token.line, c->token.column, 0);
    }
    /* E3040: multi-return calls cannot be used as if condition */
    reject_multi_return_in_single_position(checker, node->data.if_stmt.condition);
    if (cond_t && cond_t->kind != TK_UNKNOWN &&
        (cond_t->kind == TK_STRING || cond_t->kind == TK_ARRAY ||
         cond_t->kind == TK_MAP   || cond_t->kind == TK_STRUCT ||
         cond_t->kind == TK_POINTER)) {
        AstNode *c = node->data.if_stmt.condition;
        diagnostic_error_code_formatted(checker->diag, "E3091", NODE_FILE(checker, c), c->token.line, c->token.column, 0,
            type_display_name(checker, cond_t));
    }
    Scope *if_outer = checker->current_scope;
    /* Pointer checker: check each arm from the pre-branch arena state, then
     * join — an arena destroyed on either path is destroyed after the merge. */
    PcArenaSnap pc_pre = pc_snap(checker);
    Scope *if_body = scope_create(if_outer);
    checker->current_scope = if_body;
    check_block(checker, node->data.if_stmt.consequence);
    checker->current_scope = if_outer;
    scope_destroy(if_body);
    PcArenaSnap pc_then = pc_snap(checker);
    pc_restore(checker, pc_pre);
    if (node->data.if_stmt.alternative) {
        Scope *else_body = scope_create(if_outer);
        checker->current_scope = else_body;
        check_statement(checker, node->data.if_stmt.alternative);
        checker->current_scope = if_outer;
        scope_destroy(else_body);
    }
    pc_join(checker, pc_then);
    pc_snap_free(pc_then);
    pc_snap_free(pc_pre);
}

static void check_for_stmt(TypeChecker *checker, AstNode *node) {
    Scope *loop_scope = scope_create(checker->current_scope);
    Scope *outer = checker->current_scope;
    checker->current_scope = loop_scope;
    scope_define(loop_scope, node->data.for_stmt.var_name, &TYPE_INT, false);
    resolve_expression(checker, node->data.for_stmt.iterable);
    /* E9005: check range bounds when both bounds and step direction are compile-time known */
    if (node->data.for_stmt.iterable &&
        node->data.for_stmt.iterable->kind == NODE_RANGE_EXPR) {
        AstNode *r = node->data.for_stmt.iterable;
        if (r->data.range_expr.start && r->data.range_expr.end &&
            r->data.range_expr.start->kind == NODE_INT_VALUE &&
            r->data.range_expr.end->kind == NODE_INT_VALUE) {
            /* Skip bounds check when step is a runtime variable — direction is unknown. */
            bool has_neg_step = r->data.range_expr.step &&
                r->data.range_expr.step->kind == NODE_INT_VALUE &&
                r->data.range_expr.step->data.int_value.value < 0;
            bool has_neg_prefix = r->data.range_expr.step &&
                r->data.range_expr.step->kind == NODE_PREFIX_EXPR &&
                r->data.range_expr.step->data.prefix.op == TOK_MINUS;
            bool step_direction_known = !r->data.range_expr.step ||
                (r->data.range_expr.step->kind == NODE_INT_VALUE) ||
                has_neg_prefix;
            if (step_direction_known) {
                int64_t start_val = r->data.range_expr.start->data.int_value.value;
                int64_t end_val = r->data.range_expr.end->data.int_value.value;
                bool negative_step = has_neg_step || has_neg_prefix;
                bool invalid = negative_step ? (start_val < end_val) : (start_val > end_val);
                if (invalid) {
                    char *msg = NULL;
                    if (negative_step) {
                        msg = typechecker_format(checker,
                            "invalid range: start (%lld) must be greater than or equal to end (%lld) for negative step",
                            (long long)start_val, (long long)end_val);
                    } else {
                        msg = typechecker_format(checker,
                            "invalid range: start (%lld) must be less than or equal to end (%lld)",
                            (long long)start_val, (long long)end_val);
                    }
                    diagnostic_error_message(checker->diag, "E9005", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                }
            }
        }
    }
    checker->loop_depth++;
    pc_premark_loop_body(checker, node->data.for_stmt.body, node->data.for_stmt.body);
    check_block(checker, node->data.for_stmt.body);
    checker->loop_depth--;
    checker->current_scope = outer;
    scope_destroy(loop_scope);
}

static void check_for_each_stmt(TypeChecker *checker, AstNode *node) {
    Scope *loop_scope = scope_create(checker->current_scope);
    Scope *outer = checker->current_scope;
    checker->current_scope = loop_scope;

    /* W2002: check if for_each iterator/index variables shadow outer variables */
    {
        const char *var = node->data.for_each.var_name;
        if (var && strcmp(var, "_") != 0) {
            Symbol *outer_sym = scope_lookup(outer, var);
            if (outer_sym) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "for_each variable '%s' shadows a variable declared on line %d",
                    var, outer_sym->def_line);
                diagnostic_warning_message(checker->diag, "W2002", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        const char *idx = node->data.for_each.index_name;
        if (idx && strcmp(idx, "_") != 0) {
            Symbol *outer_sym = scope_lookup(outer, idx);
            if (outer_sym) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "for_each index variable '%s' shadows a variable declared on line %d",
                    idx, outer_sym->def_line);
                diagnostic_warning_message(checker->diag, "W2002", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    }

    /* E3123: both index and value discarded — no collection access occurs */
    {
        const char *var = node->data.for_each.var_name;
        const char *idx = node->data.for_each.index_name;
        if (idx && strcmp(idx, "_") == 0 && var && strcmp(var, "_") == 0) {
            diagnostic_error_code_formatted(checker->diag, "E3123", NODE_FILE(checker, node),
                node->token.line, node->token.column, 0);
            checker->current_scope = outer;
            scope_destroy(loop_scope);
            return;
        }
    }

    /* The collection is a single-value position: a fallible (T, Error) call
     * here drops the Error (the loop just runs over the first value), and a
     * user multi-return call fails the C compile. */
    reject_multi_return_in_single_position(checker, node->data.for_each.collection);

    /* Resolve collection type to determine element type */
    GrayType *coll_t = resolve_expression(checker, node->data.for_each.collection);

    /* E3136: empty inline array literal in for_each has no elements to
     * infer a type from, causing the loop variable to default to unknown. */
    if (node->data.for_each.collection->kind == NODE_ARRAY_VALUE &&
        node->data.for_each.collection->data.array_value.count == 0) {
        diagnostic_error_code(checker->diag, "E3136", NODE_FILE(checker, node->data.for_each.collection),
            node->data.for_each.collection->token.line, node->data.for_each.collection->token.column, 0);
    }

    /* Check that collection is iterable */
    if (coll_t->kind != TK_UNKNOWN && coll_t->kind != TK_ARRAY &&
        coll_t->kind != TK_MAP && coll_t->kind != TK_STRING) {
        diagnostic_error_code_formatted(checker->diag, "E3009", NODE_FILE(checker, node), node->token.line, node->token.column, 0, type_display_name(checker, coll_t));
    }

    if (coll_t->kind == TK_MAP) {
        /* Map iteration: for_each k, v in map OR for_each key in map */
        GrayType *key_t = coll_t->key_type ? type_from_name(coll_t->key_type) : &TYPE_STRING;
        GrayType *val_t = coll_t->value_type ? type_from_name(coll_t->value_type) : &TYPE_UNKNOWN;
        if (node->data.for_each.index_name) {
            /* Two-var: index_name = key, var_name = value */
            scope_define(loop_scope, node->data.for_each.index_name, key_t, false);
            scope_define(loop_scope, node->data.for_each.var_name, val_t, false);
        } else {
            /* One-var: var_name = key */
            scope_define(loop_scope, node->data.for_each.var_name, key_t, false);
        }
    } else {
        /* Array/string iteration */
        GrayType *elem_t = &TYPE_UNKNOWN;
        if (coll_t->kind == TK_ARRAY && coll_t->element_type) {
            elem_t = typechecker_type_from_name(checker, coll_t->element_type);
        } else if (coll_t->kind == TK_STRING) {
            elem_t = &TYPE_CHAR;
        }
        if (node->data.for_each.index_name) {
            scope_define(loop_scope, node->data.for_each.index_name, &TYPE_INT, false);
        }
        scope_define(loop_scope, node->data.for_each.var_name, elem_t, false);
    }

    checker->loop_depth++;
    pc_premark_loop_body(checker, node->data.for_each.body, node->data.for_each.body);
    check_block(checker, node->data.for_each.body);
    checker->loop_depth--;
    checker->current_scope = outer;
    scope_destroy(loop_scope);
}

static void check_while_stmt(TypeChecker *checker, AstNode *node) {
    GrayType *wh_cond_t = resolve_expression(checker, node->data.while_stmt.condition);
    /* E3089/E3040: fallible or multi-return call in single-value condition position. */
    reject_multi_return_in_single_position(checker, node->data.while_stmt.condition);
    /* E3038 (): void function call as 'as_long_as' condition. */
    if (wh_cond_t && wh_cond_t->kind == TK_VOID) {
        AstNode *c = node->data.while_stmt.condition;
        char *msg = NULL;
        if (c && c->kind == NODE_CALL_EXPR && c->data.call.function &&
            c->data.call.function->kind == NODE_LABEL) {
            msg = typechecker_format(checker,
                "cannot use void function '%s' as condition; 'as_long_as' requires a bool expression",
                c->data.call.function->data.label.value);
        } else {
            msg = typechecker_format(checker,
                "cannot use void expression as condition; 'as_long_as' requires a bool expression");
        }
        diagnostic_error_message(checker->diag, "E3038", msg,
            NODE_FILE(checker, c), c->token.line, c->token.column, 0);
    }
    if (wh_cond_t && wh_cond_t->kind != TK_UNKNOWN &&
        (wh_cond_t->kind == TK_STRING || wh_cond_t->kind == TK_ARRAY ||
         wh_cond_t->kind == TK_MAP   || wh_cond_t->kind == TK_STRUCT ||
         wh_cond_t->kind == TK_POINTER)) {
        AstNode *c = node->data.while_stmt.condition;
        diagnostic_error_code_formatted(checker->diag, "E3091", NODE_FILE(checker, c), c->token.line, c->token.column, 0,
            type_display_name(checker, wh_cond_t));
    }
    /* E3129: empty loop body hangs forever at runtime */
    if (node->data.while_stmt.body &&
        node->data.while_stmt.body->data.block.count == 0) {
        diagnostic_error_code(checker->diag, "E3129",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    Scope *wh_outer = checker->current_scope;
    Scope *wh_scope = scope_create(wh_outer);
    checker->current_scope = wh_scope;
    checker->loop_depth++;
    pc_premark_loop_body(checker, node->data.while_stmt.body, node->data.while_stmt.body);
    check_block(checker, node->data.while_stmt.body);
    checker->loop_depth--;
    checker->current_scope = wh_outer;
    scope_destroy(wh_scope);
}

static void check_func_decl(TypeChecker *checker, AstNode *node) {
    /* E2038: reserved type name as function name */
    if (is_reserved_type_name(node->data.func_decl.name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "'%s' is a reserved type name and cannot be used as a function name",
            FUNC_DISPLAY_NAME(node));
        diagnostic_error_message(checker->diag, "E2038", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E5016: builtin function name redeclared */
    if (is_reserved_builtin_func_name(node->data.func_decl.name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "'%s' is a builtin function and cannot be redeclared",
            FUNC_DISPLAY_NAME(node));
        diagnostic_error_message(checker->diag, "E5016", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* E5035: stdlib module name as function name */
    if (is_stdlib_module_name(node->data.func_decl.name)) {
        char *msg = NULL;
        msg = typechecker_format(checker,
            "'%s' is a standard library module and cannot be used as a function name",
            FUNC_DISPLAY_NAME(node));
        diagnostic_error_message(checker->diag, "E5035", msg,
            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
    }
    /* Check for nested function declarations */
    if (checker->func_depth > 0) {
        diagnostic_error_code_formatted(checker->diag, "E2051", NODE_FILE(checker, node), node->token.line, node->token.column, 0, FUNC_DISPLAY_NAME(node));
    }

    Scope *func_scope = scope_create(checker->current_scope);
    Scope *outer = checker->current_scope;
    checker->current_scope = func_scope;
    int saved_func_scope_depth = checker->current_func_scope_depth;
    checker->current_func_scope_depth = func_scope->depth + 1;
    checker->func_depth++;
    checker->arena_count = 0;

    /* Define parameters in function scope, check for duplicates */
    for (int i = 0; i < node->data.func_decl.param_count; i++) {
        Param *p = &node->data.func_decl.params[i];
        /* Type parameter (<?>) — not a variable; just record the name
         * so the body can recognise T in type positions. */
        if (p->is_type_param) {
            checker->type_param_name = p->name;
            continue;
        }
        /* E2038: reserved type name as parameter name */
        if (is_reserved_type_name(p->name)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'%s' is a reserved type name and cannot be used as a parameter name",
                p->name);
            diagnostic_error_message(checker->diag, "E2038", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* E5016: builtin function name as parameter name */
        if (is_reserved_builtin_func_name(p->name)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'%s' is a builtin function and cannot be used as a parameter name",
                p->name);
            diagnostic_error_message(checker->diag, "E5016", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* E5035: stdlib module name as parameter name */
        if (is_stdlib_module_name(p->name)) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "'%s' is a standard library module and cannot be used as a parameter name",
                p->name);
            diagnostic_error_message(checker->diag, "E5035", msg,
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* E4026: 'main' is reserved for the entry-point function */
        check_reserved_main(checker, p->name, NODE_FILE(checker, node), node->token.line, node->token.column);
        /* Check for duplicate parameter name */
        for (int j = 0; j < i; j++) {
            if (strcmp(node->data.func_decl.params[j].name, p->name) == 0) {
                diagnostic_error_code_formatted(checker->diag, "E2012", NODE_FILE(checker, node), node->token.line, node->token.column, 0, p->name);
                break;
            }
        }
        /* W2008: parameter shadows an enum variant name */
        for (int enum_index = 0; enum_index < checker->enum_count; enum_index++) {
            bool found_variant = false;
            for (int variant_index = 0; variant_index < checker->enum_value_counts[enum_index]; variant_index++) {
                if (strcmp(checker->enum_values[enum_index][variant_index], p->name) == 0) {
                    const char *display = checker->enum_display_names[enum_index]
                        ? checker->enum_display_names[enum_index] : checker->enum_names[enum_index];
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "parameter '%s' shadows enum variant '%s.%s'",
                        p->name, display, p->name);
                    diagnostic_warning_message(checker->diag, "W2008", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                    found_variant = true;
                    break;
                }
            }
            if (found_variant) break;
        }
        /* E2039: required param after param with default value */
        if (i > 0 && !p->default_value) {
            bool prev_has_default = false;
            for (int k = 0; k < i; k++) {
                if (node->data.func_decl.params[k].default_value) {
                    prev_has_default = true;
                    break;
                }
            }
            if (prev_has_default) {
                char *msg = typechecker_format(checker, "required parameter '%s' follows a parameter with a default value", p->name);
                diagnostic_error_help(checker->diag, "E2039", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    "move all parameters with default values to the end of the parameter list");
            }
        }
        /* E3119: fixed-size array in function parameter */
        if (p->type_name && p->type_name[0] == '[') {
            const char *tn = p->type_name;
            const char *size_comma = NULL;
            int depth = 0;
            for (const char *c = tn; *c; c++) {
                if (*c == '(' || *c == '[') depth++;
                else if (*c == ')' || *c == ']') depth--;
                else if (*c == ',' && depth == 1) { size_comma = c; break; }
            }
            if (size_comma) {
                char elem[MSG_BUF_SIZE];
                int element_length = (int)(size_comma - tn - 1);
                snprintf(elem, sizeof(elem), "%.*s", element_length, tn + 1);
                char *msg = typechecker_format(checker, "fixed-size array type '%s' is not allowed in function parameter '%s'; use [%s] instead",
                    tn, p->name, elem);
                diagnostic_error_help(checker->diag, "E3119", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    "use a dynamic array type instead, e.g. [int] without a size");
            }
        }
        GrayType *ptype = p->type_name ? typechecker_type_from_name(checker, p->type_name) : &TYPE_UNKNOWN;
        /* E4016: undefined parameter type */
        {
            char leaf[MSG_BUF_SIZE];
            const char *undefined = undefined_type_leaf(checker, p->type_name,
                                                        leaf, sizeof(leaf));
            if (undefined) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "undefined type '%s'; check the spelling or import the module that defines it",
                    unqualified_display_name(undefined));
                diagnostic_error_message(checker->diag, "E4016", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        reject_private_type(checker, node, p->type_name);
        reject_error_in_container(checker, node, p->type_name);
        /* Type inference: if no explicit type annotation, infer from default
         * value when it is an enum member access (e.g. t = Color.RED). */
        if (!p->type_name && p->default_value) {
            GrayType *inferred = resolve_expression(checker, p->default_value);
            if (inferred && inferred->kind == TK_ENUM) {
                ptype = inferred;
            } else {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "parameter '%s' has no type annotation; omitting the type is only allowed when the default value is an enum member (e.g. %s = MyEnum.VALUE)",
                    p->name, p->name);
                diagnostic_error_message(checker->diag, "E3160", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        /* E5026: validate default value type matches parameter type */
        if (p->default_value && p->type_name) {
            /* Set expected_type for implicit enum resolution in default param values */
            GrayType *saved_def_expected = checker->expected_type;
            if (ptype->kind == TK_ENUM && ptype->name)
                checker->expected_type = ptype;
            GrayType *def_t = resolve_expression(checker, p->default_value);
            checker->expected_type = saved_def_expected;
            if (def_t->kind != TK_UNKNOWN && ptype->kind != TK_UNKNOWN &&
                !types_assignable(checker, ptype, def_t) &&
                !(def_t->kind == TK_NIL)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "default value for parameter '%s' has wrong type; expected %s, got %s",
                    p->name, p->type_name, type_name(def_t));
                tc_err_arg_type(checker, p->default_value, msg);
            }
            /* E3036: out-of-range default value for a narrow parameter
             * (do f(b byte = 300)). */
            int64_t def_lit;
            bool def_lit_neg;
            if (try_get_signed_literal_int(p->default_value, &def_lit, &def_lit_neg))
                check_integer_range(checker->diag, NODE_FILE(checker, p->default_value),
                    p->default_value->token.line, p->default_value->token.column,
                    p->type_name, def_lit, def_lit_neg);
        }
        scope_define(func_scope, p->name, ptype, p->mutable);
    }

    /* E3060: wildcard in return type but no wildcard in any parameter.
     * Suppress when every wildcard return is in a named position — E3082
     * handles that case with a more specific message. */
    {
        bool ret_has_wc = false;
        bool all_wc_named = true;
        bool param_has_wc = false;
        for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
            if (type_name_has_wildcard(node->data.func_decl.return_types[i])) {
                ret_has_wc = true;
                if (!node->data.func_decl.return_names ||
                    !node->data.func_decl.return_names[i]) {
                    all_wc_named = false;
                }
            }
        }
        if (ret_has_wc && !all_wc_named) {
            for (int i = 0; i < node->data.func_decl.param_count; i++) {
                if (type_name_has_wildcard(node->data.func_decl.params[i].type_name)) {
                    param_has_wc = true;
                    break;
                }
            }
            if (!param_has_wc) {
                diagnostic_error_code(checker->diag, "E3060", NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    }

    /* Define named return variables in function scope */
    if (node->data.func_decl.return_names) {
        for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
            if (node->data.func_decl.return_names[i]) {
                const char *rn = node->data.func_decl.return_names[i];
                /* E2063: duplicate named return value */
                for (int j = 0; j < i; j++) {
                    if (node->data.func_decl.return_names[j] &&
                        strcmp(node->data.func_decl.return_names[j], rn) == 0) {
                        char *msg = NULL;
                        msg = typechecker_format(checker,
                            "duplicate named return value '%s'", rn);
                        diagnostic_error_message(checker->diag, "E2063", msg,
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                        break;
                    }
                }
                /* E3082: wildcard type '?' in named return position */
                if (i < node->data.func_decl.return_type_count &&
                    node->data.func_decl.return_types[i] &&
                    strcmp(node->data.func_decl.return_types[i], "?") == 0) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "wildcard type '?' cannot be used in named return value '%s'; use an unnamed return instead (e.g. -> (?, int))",
                        rn);
                    diagnostic_error_message(checker->diag, "E3082", msg,
                        NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                }
                /* E2063: named return collides with parameter */
                for (int j = 0; j < node->data.func_decl.param_count; j++) {
                    if (strcmp(node->data.func_decl.params[j].name, rn) == 0) {
                        char *msg = NULL;
                        msg = typechecker_format(checker,
                            "named return value '%s' conflicts with parameter '%s'",
                            rn, rn);
                        diagnostic_error_message(checker->diag, "E2063", msg,
                            NODE_FILE(checker, node), node->token.line, node->token.column, 0);
                        break;
                    }
                }
            }
        }
    }

    /* Save using-module count so function-scoped `using` doesn't
     * leak to subsequent functions (). */
    int prev_using_count = checker->using_module_count;

    /* Track current function return types for return statement checking */
    GrayType **prev_ret = checker->current_return_types;
    const char **prev_ret_names = checker->current_return_type_names;
    int prev_ret_count = checker->current_return_count;
    bool prev_named = checker->current_has_named_returns;

    /* Detect named return values */
    const char **prev_return_names = checker->current_return_names;
    checker->current_has_named_returns = false;
    checker->current_return_names = node->data.func_decl.return_names;
    if (node->data.func_decl.return_names) {
        for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
            if (node->data.func_decl.return_names[i]) {
                checker->current_has_named_returns = true;
                break;
            }
        }
    }

    if (node->data.func_decl.return_type_count > 0) {
        checker->current_return_types = xmalloc(sizeof(GrayType *) * node->data.func_decl.return_type_count);
        checker->current_return_type_names = xmalloc(sizeof(const char *) * node->data.func_decl.return_type_count);
        checker->current_return_count = node->data.func_decl.return_type_count;
        for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
            checker->current_return_types[i] = typechecker_type_from_name(checker, node->data.func_decl.return_types[i]);
            checker->current_return_type_names[i] = node->data.func_decl.return_types[i];
            /* E4016: undefined return type */
            const char *rtn = node->data.func_decl.return_types[i];
            /* E3142: a func return type. STANDARD 7.6.6 — a returned func
             * value cannot be called, assigned, or stored, so a function
             * whose return type is func has no valid use. Reject it at the
             * declaration rather than at every downstream call site. */
            if (rtn && (strcmp(rtn, "func") == 0 || strncmp(rtn, "func(", 5) == 0)) {
                diagnostic_error_code_formatted(checker->diag, "E3142",
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                    FUNC_DISPLAY_NAME(node));
            }
            reject_private_type(checker, node, rtn);
            reject_error_in_container(checker, node, rtn);
            char leaf[MSG_BUF_SIZE];
            const char *undefined = undefined_type_leaf(checker, rtn, leaf, sizeof(leaf));
            if (undefined) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "undefined type '%s'; check the spelling or import the module that defines it",
                    unqualified_display_name(undefined));
                diagnostic_error_message(checker->diag, "E4016", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    } else {
        checker->current_return_types = NULL;
        checker->current_return_type_names = NULL;
        checker->current_return_count = 0;
    }

    /* : main() is always void, and E4008 was already emitted
     * by register_declarations when the user attached a return
     * type. Treat main's effective return type as void for the
     * body walk so downstream "must return a value" (E3024),
     * "cannot return a value from a void function" (E3006), and
     * return-type-mismatch cascades don't fire on top of the
     * E4008 the user is already going to fix. The suppression
     * flag lets individual checks distinguish "real void
     * function" from "main that tried to declare a return type
     * but got rewritten to void"; for the latter, we want
     * silence, not a different cascade. */
    bool main_return_coerced = false;
    if (strcmp(node->data.func_decl.name, "main") == 0 &&
        checker->current_return_count > 0) {
        free(checker->current_return_types);
        free((void *)checker->current_return_type_names);
        checker->current_return_types = NULL;
        checker->current_return_type_names = NULL;
        checker->current_return_count = 0;
        main_return_coerced = true;
    }
    bool saved_main_suppressed = checker->current_main_return_suppressed;
    checker->current_main_return_suppressed = main_return_coerced;
    bool saved_is_main = checker->current_func_is_main;
    checker->current_func_is_main =
        (strcmp(node->data.func_decl.name, "main") == 0);
    AstNode *saved_func_decl = checker->current_func_decl;
    checker->current_func_decl = node;

    check_block(checker, node->data.func_decl.body);

    /* E3070: ensure must be at the function body's top level. */
    check_no_nested_ensure(checker, node->data.func_decl.body, false);

    /* Check for missing return in non-void function (simple: check last statement) */
    if (checker->current_return_count > 0 && node->data.func_decl.body &&
        node->data.func_decl.body->kind == NODE_BLOCK_STMT) {
        AstNode *body = node->data.func_decl.body;
        bool has_return = false;
        /* Recursively check if any statement in the body is a return */
        for (int i = 0; i < body->data.block.count; i++) {
            if (block_has_return(body->data.block.stmts[i])) {
                has_return = true;
                break;
            }
        }
        /* Also check named returns (if return names are set, implicit return is OK) */
        bool has_named_returns = false;
        if (node->data.func_decl.return_names) {
            for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
                if (node->data.func_decl.return_names[i]) {
                    has_named_returns = true;
                    break;
                }
            }
        }
        if (!has_return && !has_named_returns) {
            diagnostic_error_code_formatted(checker->diag, "E3024", NODE_FILE(checker, node), node->token.line, node->token.column, 0, FUNC_DISPLAY_NAME(node));
        } else if (has_return && !has_named_returns &&
                   !all_paths_return(node->data.func_decl.body)) {
            diagnostic_error_code_formatted(checker->diag, "E3035", NODE_FILE(checker, node), node->token.line, node->token.column, 0, FUNC_DISPLAY_NAME(node));
        }
    }

    /* W2011: named return value declared but no matching variable in body */
    if (node->data.func_decl.return_names) {
        for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
            const char *rn = node->data.func_decl.return_names[i];
            if (!rn) continue;
            Symbol *sym = scope_lookup_local(func_scope, rn);
            if (!sym) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "named return value '%s' is declared in the signature but no matching variable exists in the function body",
                    rn);
                diagnostic_warning_message(checker->diag, "W2011", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
    }

    /* Warn about unused variables in this function scope */
    for (int symbol_index = 0; symbol_index < func_scope->count; symbol_index++) {
        Symbol *s = &func_scope->symbols[symbol_index];
        if (!s->used && s->name[0] != '_' && s->def_line > 0) {
            /* Skip function parameters (they have def_line == 0 or from param list) */
            bool is_param = false;
            for (int parameter_index = 0; parameter_index < node->data.func_decl.param_count; parameter_index++) {
                if (strcmp(node->data.func_decl.params[parameter_index].name, s->name) == 0) {
                    is_param = true;
                    break;
                }
            }
            if (!is_param) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "variable '%s' is declared but never used", s->name);
                /* def_line numbers the file holding the function body, which
                 * is not the entry file when the function came from an
                 * import. A local cannot outlive its function, so the
                 * declaration node names the right file. */
                diagnostic_warning_message(checker->diag, "W1001", msg,
                    NODE_FILE(checker, node), s->def_line, s->def_column, 0);
            }
        }
    }

    if (checker->current_return_types) free(checker->current_return_types);
    if (checker->current_return_type_names) free(checker->current_return_type_names);
    checker->current_return_types = prev_ret;
    checker->current_return_type_names = prev_ret_names;
    checker->current_return_count = prev_ret_count;
    checker->current_has_named_returns = prev_named;
    checker->current_return_names = prev_return_names;
    checker->current_main_return_suppressed = saved_main_suppressed;
    checker->current_func_is_main = saved_is_main;
    checker->current_func_scope_depth = saved_func_scope_depth;
    checker->current_func_decl = saved_func_decl;
    checker->using_module_count = prev_using_count;
    checker->type_param_name = NULL;
    checker->type_param_binding = NULL;
    checker->func_depth--;
    checker->current_scope = outer;
    scope_destroy(func_scope);
}

/* E3099: a user type name collides with a stdlib opaque type. codegen maps
 * these names to internal C types (GrayRouter, GrayThread, ...), so the
 * conflict is real only when the owning module is imported and `mod.func()`
 * can return that opaque type under the same name. A name with no owning
 * module (SourceLocation) is produced by a builtin with no import and stays
 * reserved unconditionally. */
static void check_stdlib_opaque_name_collision(TypeChecker *checker, AstNode *node,
                                               const char *name) {
    /* Enums a stdlib module exposes (io.OpenFlag, os.Platform) are reserved the
     * same way opaque types are: only while the owning module is imported. */
    const char *enum_owner = stdlib_enum_module(name);
    if (enum_owner && typechecker_is_imported_module(checker, enum_owner)) {
        diagnostic_error_code_formatted(checker->diag, "E3099",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0, name);
        return;
    }
    if (!is_reserved_stdlib_struct_name(name)) return;
    const char *owner = stdlib_opaque_module(name);
    if (!owner || typechecker_is_imported_module(checker, owner)) {
        diagnostic_error_code_formatted(checker->diag, "E3099",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0, name);
    }
}

static void check_struct_decl(TypeChecker *checker, AstNode *node) {
    /* E4021/E4015: a field's annotated type is private to another file */
    for (int field_index = 0; field_index < node->data.struct_decl.field_count; field_index++) {
        reject_private_type(checker, node, node->data.struct_decl.fields[field_index].type_name);
        reject_error_in_container(checker, node, node->data.struct_decl.fields[field_index].type_name);
    }
    check_stdlib_opaque_name_collision(checker, node, STRUCT_DISPLAY_NAME(node));
    /* E2053: struct inside function */
    if (checker->func_depth > 0) {
        diagnostic_error_code_formatted(checker->diag, "E2053",
            NODE_FILE(checker, node), node->token.line, node->token.column, 0,
            "struct", STRUCT_DISPLAY_NAME(node));
    }
    /* E3103/E3104: #json structs are data-only */
    if (node->data.struct_decl.is_json) {
        /* E6012: the generated serializer helpers call into the json runtime,
         * whose declarations only reach the C output when @json is imported.
         * Without the import the helper references undeclared symbols and the
         * C compiler fails instead of the typechecker. */
        if (!typechecker_is_imported_module(checker, "json")) {
            diagnostic_error_code_formatted_help(checker->diag, "E6012",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                "add 'import @json' to this file", STRUCT_DISPLAY_NAME(node));
        }
        for (int field_index = 0; field_index < node->data.struct_decl.field_count; field_index++) {
            const char *ftype = node->data.struct_decl.fields[field_index].type_name;
            if (ftype && strncmp(ftype, "func", 4) == 0) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "#json struct '%s' cannot have func-typed field '%s'; func references have no JSON representation",
                    STRUCT_DISPLAY_NAME(node),
                    node->data.struct_decl.fields[field_index].name);
                diagnostic_error_message(checker->diag, "E3103", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            if (node->data.struct_decl.fields[field_index].default_value) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "#json struct '%s' cannot have default field values; field '%s' has a default",
                    STRUCT_DISPLAY_NAME(node),
                    node->data.struct_decl.fields[field_index].name);
                diagnostic_error_message(checker->diag, "E3109", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
            /* E3140: field types the JSON serializer codegen cannot marshal.
             * The func-typed case is already reported as E3103 above. */
            if (ftype && strncmp(ftype, "func", 4) != 0 &&
                strcmp(ftype, "int") != 0 && strcmp(ftype, "i64") != 0 &&
                strcmp(ftype, "uint") != 0 && strcmp(ftype, "u64") != 0 &&
                strcmp(ftype, "float") != 0 && strcmp(ftype, "f64") != 0 &&
                strcmp(ftype, "string") != 0 && strcmp(ftype, "bool") != 0) {
                char *msg = typechecker_format(checker,
                    "#json struct '%s' field '%s' has type '%s', which has no JSON representation; "
                    "#json fields must be int, uint, float, string, or bool",
                    STRUCT_DISPLAY_NAME(node),
                    node->data.struct_decl.fields[field_index].name, ftype);
                diagnostic_error_message(checker->diag, "E3140", msg,
                    NODE_FILE(checker, node), node->token.line, node->token.column, 0);
            }
        }
        for (int field_index = 0; field_index < node->data.struct_decl.func_count; field_index++) {
            AstNode *fn = node->data.struct_decl.funcs[field_index].func_decl;
            if (fn && fn->kind == NODE_FUNC_DECL) {
                const char *fname = FUNC_DISPLAY_NAME(fn);
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "#json struct '%s' cannot declare functions; #json structs are data-only — move '%s' to a standalone function",
                    STRUCT_DISPLAY_NAME(node), fname);
                diagnostic_error_message(checker->diag, "E3104", msg,
                    NODE_FILE(checker, node), fn->token.line, fn->token.column, 0);
            }
        }
    }
    /* Type-check struct-namespaced function bodies */
    checker->current_struct_name = node->data.struct_decl.name;
    for (int i = 0; i < node->data.struct_decl.func_count; i++) {
        AstNode *fn = node->data.struct_decl.funcs[i].func_decl;
        if (fn && fn->kind == NODE_FUNC_DECL) {
            check_statement(checker, fn);
        }
    }
    checker->current_struct_name = NULL;
}

/* The enum-variant name a when/is case value selects — plain (`Color.RED`),
 * implicit (`.RED`), or a tagged pattern (`Shape.Circle(r)`). NULL if the case
 * value does not name an enum variant. Uses only parse-time fields, so it is
 * safe to call before the case values are resolved. */
static const char *when_case_variant_name(AstNode *v) {
    if (!v) return NULL;
    if (v->kind == NODE_WHEN_PATTERN) return v->data.when_pattern.variant;
    if (v->kind == NODE_IMPLICIT_ENUM) return v->data.implicit_enum.variant;
    if (v->kind == NODE_MEMBER_EXPR && v->data.member.object &&
        v->data.member.object->kind == NODE_LABEL)
        return v->data.member.member;
    return NULL;
}

/* True when two when/is case values select the same case. Enum variants are
 * compared by name (the subject type already constrains every case to one
 * enum, so `Color.RED` and `.RED` are the same case). */
static bool when_case_values_equal(AstNode *a, AstNode *b) {
    if (!a || !b) return false;
    if (a->kind == NODE_INT_VALUE && b->kind == NODE_INT_VALUE)
        return a->data.int_value.value == b->data.int_value.value;
    if (a->kind == NODE_STRING_VALUE && b->kind == NODE_STRING_VALUE)
        return strcmp(a->data.string_value.value, b->data.string_value.value) == 0;
    const char *va = when_case_variant_name(a);
    const char *vb = when_case_variant_name(b);
    return va && vb && strcmp(va, vb) == 0;
}

static void check_when_stmt(TypeChecker *checker, AstNode *node) {
    /* E3100: a bare struct or enum name is a type, not a value. Without
     * this the subject resolves to the type itself and codegen emits a
     * reference to a name that does not exist in the generated C. */
    AstNode *subject = node->data.when_stmt.value;
    if (subject && subject->kind == NODE_LABEL &&
        !scope_lookup(checker->current_scope, subject->data.label.value)) {
        const char *tn = subject->data.label.value;
        if (is_struct_name(checker, tn)) {
            char *msg = typechecker_format(checker,
                "type name '%s' cannot be used as a value; use '%s{...}' or 'new(%s)' to create an instance",
                tn, tn, tn);
            diagnostic_error_message(checker->diag, "E3100", msg,
                NODE_FILE(checker, subject), subject->token.line, subject->token.column, 0);
            return;
        }
        if (is_enum_name(checker, tn)) {
            char *msg = typechecker_format(checker,
                "type name '%s' cannot be used as a value; use '%s.VARIANT' to access an enum value",
                tn, tn);
            diagnostic_error_message(checker->diag, "E3100", msg,
                NODE_FILE(checker, subject), subject->token.line, subject->token.column, 0);
            return;
        }
    }
    GrayType *when_t = resolve_expression(checker, node->data.when_stmt.value);
    /* W2012: float subjects use bit-equality, which is rarely what the
     * user wants given 0.1 + 0.2 != 0.3. */
    if (when_t && when_t->kind == TK_FLOAT) {
        AstNode *subj = node->data.when_stmt.value;
        diagnostic_warning_code(checker->diag, "W2012", NODE_FILE(checker, subj), subj->token.line, subj->token.column, 0);
    }
    /* E3040: multi-return calls cannot be used as when subject */
    reject_multi_return_in_single_position(checker, node->data.when_stmt.value);
    /* E3121: struct, array, map, and pointer types are not valid when subjects.
     * Null out when_t so subsequent case type checks are skipped. */
    if (when_t && (when_t->kind == TK_STRUCT || when_t->kind == TK_ARRAY ||
                   when_t->kind == TK_MAP || when_t->kind == TK_POINTER)) {
        AstNode *subj = node->data.when_stmt.value;
        diagnostic_error_code_formatted(checker->diag, "E3121", NODE_FILE(checker, subj), subj->token.line, subj->token.column, 0,
            type_display_name(checker, when_t));
        when_t = NULL;
    }
    /* The program-wide ErrorCode enum is open — `when err.code` can never be
     * exhaustive, so it always needs a default (E3149) and is never #strict
     * (E3151); #strict is the more specific violation and wins when both
     * apply. A concrete #error_code enum (PayErr, ...) is a closed set and
     * stays usable as an ordinary when subject (STANDARD 10.5), so this keys
     * on the literal ErrorCode type, not the whole ErrorCode value space. */
    if (when_t && when_t->kind == TK_ENUM && when_t->name &&
        strcmp(when_t->name, "ErrorCode") == 0) {
        AstNode *subj = node->data.when_stmt.value;
        if (node->data.when_stmt.is_strict) {
            diagnostic_error_code(checker->diag, "E3151",
                NODE_FILE(checker, subj), subj->token.line, subj->token.column, 0);
        } else if (!node->data.when_stmt.default_body) {
            diagnostic_error_code(checker->diag, "E3149",
                NODE_FILE(checker, subj), subj->token.line, subj->token.column, 0);
        }
    }

    /* E2043: a case value that repeats an earlier one is dead code. Covers int,
     * string, and enum-variant cases (plain, implicit, and tagged patterns);
     * the tagged-pattern branch below `continue`s before the per-value checks,
     * so this runs as its own pass over every (case, value) pair. */
    for (int i = 0; i < node->data.when_stmt.case_count; i++) {
        for (int j = 0; j < node->data.when_stmt.cases[i].value_count; j++) {
            AstNode *val_i = node->data.when_stmt.cases[i].values[j];
            bool flagged = false;
            for (int pi = 0; pi <= i && !flagged; pi++) {
                int pj_max = (pi == i) ? j : node->data.when_stmt.cases[pi].value_count;
                for (int pj = 0; pj < pj_max; pj++) {
                    if (when_case_values_equal(val_i, node->data.when_stmt.cases[pi].values[pj])) {
                        diagnostic_error_code(checker->diag, "E2043", NODE_FILE(checker, val_i),
                            val_i->token.line, val_i->token.column, 0);
                        flagged = true;
                        break;
                    }
                }
            }
        }
    }

    /* E3001: check type match */
    /* Set expected_type for implicit enum resolution in when/is branches */
    GrayType *saved_when_expected = checker->expected_type;
    if (when_t && when_t->kind == TK_ENUM && when_t->name)
        checker->expected_type = when_t;
    /* Pointer checker: each case body and the default run from the pre-when
     * arena state; the state after the when is the join of every branch (plus
     * the fall-through path when there is no default). */
    PcArenaSnap pc_pre = pc_snap(checker);
    PcArenaSnap pc_merged = { NULL, 0 };
    bool pc_have = false;
    for (int i = 0; i < node->data.when_stmt.case_count; i++) {
        for (int j = 0; j < node->data.when_stmt.cases[i].value_count; j++) {
            AstNode *val_i = node->data.when_stmt.cases[i].values[j];
            /* Handle NODE_WHEN_PATTERN: validate variant + binding count */
            if (val_i->kind == NODE_WHEN_PATTERN) {
                const char *vname = val_i->data.when_pattern.variant;
                const char *ename = NULL;
                if (val_i->data.when_pattern.is_implicit) {
                    if (when_t && when_t->kind == TK_ENUM && when_t->name)
                        ename = when_t->name;
                } else {
                    /* For explicit form Variant(x), resolve from scrutinee type */
                    if (when_t && when_t->kind == TK_ENUM && when_t->name)
                        ename = when_t->name;
                }
                if (ename) {
                    val_i->data.when_pattern.enum_name = ename;
                    int eidx = -1;
                    for (int enum_index = 0; enum_index < checker->enum_count; enum_index++) {
                        if (strcmp(checker->enum_names[enum_index], ename) == 0) { eidx = enum_index; break; }
                    }
                    if (eidx >= 0) {
                        int vidx = -1;
                        for (int variant_index = 0; variant_index < checker->enum_value_counts[eidx]; variant_index++) {
                            if (strcmp(checker->enum_values[eidx][variant_index], vname) == 0) { vidx = variant_index; break; }
                        }
                        if (vidx < 0) {
                            diagnostic_error_code_formatted(checker->diag, "E3047", NODE_FILE(checker, val_i), val_i->token.line, val_i->token.column, 0, ename, vname);
                        } else {
                            int expected_bc = checker->enum_payload_counts[eidx][vidx];
                            int got_bc = val_i->data.when_pattern.binding_count;
                            if (expected_bc != got_bc) {
                                diagnostic_error_code_formatted(checker->diag, "E3116", NODE_FILE(checker, val_i), val_i->token.line, val_i->token.column, 0, vname, expected_bc, got_bc);
                            }
                        }
                    }
                }
                continue;
            }
            GrayType *case_t = resolve_expression(checker, val_i);
            /* Check case value type matches scrutinee; skip range exprs and unknowns */
            if (when_t && case_t &&
                when_t->kind != TK_UNKNOWN && case_t->kind != TK_UNKNOWN &&
                val_i->kind != NODE_RANGE_EXPR &&
                !(val_i->kind == NODE_CALL_EXPR && val_i->data.call.function->kind == NODE_LABEL &&
                  strcmp(val_i->data.call.function->data.label.value, "range") == 0)) {
                /* Mixing an enum with a plain int is a mismatch, matching the
                 * == operator (E3117): an int literal pattern against an enum
                 * subject, or an enum-variant pattern against an int subject,
                 * would otherwise match by raw ordinal with no diagnostic. */
                bool enum_vs_int =
                    (when_t->kind == TK_ENUM && is_int_kind(case_t->kind)) ||
                    (is_int_kind(when_t->kind) && case_t->kind == TK_ENUM);
                bool compat = !enum_vs_int &&
                    (types_assignable(checker, when_t, case_t) ||
                     (when_t->kind == TK_ENUM && case_t->kind == TK_STRING &&
                      typechecker_enum_is_string(checker, when_t->name)));
                if (!compat) {
                    diagnostic_error_code_formatted(checker->diag, "E3018", NODE_FILE(checker, val_i), val_i->token.line, val_i->token.column, 0, type_display_name(checker, when_t), type_display_name(checker, case_t));
                }
            }
        }
        Scope *case_outer = checker->current_scope;
        Scope *case_body = scope_create(case_outer);
        checker->current_scope = case_body;
        /* Introduce pattern bindings into case scope */
        for (int j = 0; j < node->data.when_stmt.cases[i].value_count; j++) {
            AstNode *val_i = node->data.when_stmt.cases[i].values[j];
            if (val_i->kind == NODE_WHEN_PATTERN && val_i->data.when_pattern.enum_name) {
                const char *ename = val_i->data.when_pattern.enum_name;
                const char *vname = val_i->data.when_pattern.variant;
                int eidx = -1;
                for (int enum_index = 0; enum_index < checker->enum_count; enum_index++) {
                    if (strcmp(checker->enum_names[enum_index], ename) == 0) { eidx = enum_index; break; }
                }
                if (eidx >= 0) {
                    int vidx = -1;
                    for (int variant_index = 0; variant_index < checker->enum_value_counts[eidx]; variant_index++) {
                        if (strcmp(checker->enum_values[eidx][variant_index], vname) == 0) { vidx = variant_index; break; }
                    }
                    if (vidx >= 0) {
                        int bc = val_i->data.when_pattern.binding_count;
                        int payload_count = checker->enum_payload_counts[eidx][vidx];
                        int limit = bc < payload_count ? bc : payload_count;
                        for (int bi = 0; bi < limit; bi++) {
                            GrayType *bt = typechecker_type_from_name(checker, checker->enum_payload_types[eidx][vidx][bi]);
                            scope_define(checker->current_scope, val_i->data.when_pattern.bindings[bi], bt, false);
                        }
                    }
                }
            }
        }
        pc_restore(checker, pc_pre);
        check_block(checker, node->data.when_stmt.cases[i].body);
        if (!pc_have) { pc_merged = pc_snap(checker); pc_have = true; }
        else { PcArenaSnap m = pc_merged; pc_join(checker, m);
               pc_merged = pc_snap(checker); pc_snap_free(m); }
        checker->current_scope = case_outer;
        scope_destroy(case_body);
    }
    checker->expected_type = saved_when_expected;
    if (node->data.when_stmt.default_body) {
        Scope *def_outer = checker->current_scope;
        Scope *def_body = scope_create(def_outer);
        checker->current_scope = def_body;
        pc_restore(checker, pc_pre);
        check_block(checker, node->data.when_stmt.default_body);
        if (!pc_have) { pc_merged = pc_snap(checker); pc_have = true; }
        else { PcArenaSnap m = pc_merged; pc_join(checker, m);
               pc_merged = pc_snap(checker); pc_snap_free(m); }
        checker->current_scope = def_outer;
        scope_destroy(def_body);
        /* W3006: empty default branch */
        if (node->data.when_stmt.default_body->data.block.count == 0) {
            diagnostic_warning_message(checker->diag, "W3006",
                "empty default branch in when statement; unmatched values are silently ignored",
                NODE_FILE(checker, node->data.when_stmt.default_body),
                node->data.when_stmt.default_body->token.line,
                node->data.when_stmt.default_body->token.column, 0);
        }
    }
    /* #strict exhaustiveness check for enum types */
    if (node->data.when_stmt.is_strict && !node->data.when_stmt.default_body) {
        /* Infer the enum name from case values (e.g., Color.RED → "Color") */
        const char *enum_name = NULL;
        for (int const_index = 0; const_index < node->data.when_stmt.case_count && !enum_name; const_index++) {
            for (int cj = 0; cj < node->data.when_stmt.cases[const_index].value_count && !enum_name; cj++) {
                AstNode *cv = node->data.when_stmt.cases[const_index].values[cj];
                const char *case_qualifier = ast_member_qualifier(cv);
                if (case_qualifier && is_enum_name(checker, case_qualifier))
                    enum_name = case_qualifier;
                /* Module-qualified: mod.Enum.VARIANT nests the enum one level
                 * deeper, so a bare-label test never saw it and the `when`
                 * was reported as being on a non-enum type. */
                const char *case_mod = NULL, *case_enum = NULL;
                if (ast_member_chain(cv, &case_mod, &case_enum)) {
                    char qualified[MSG_BUF_SIZE];
                    snprintf(qualified, sizeof(qualified), "%s.%s", case_mod, case_enum);
                    if (is_enum_name(checker, qualified))
                        enum_name = arena_copy_string(checker->arena, qualified);
                }
                /* Also infer from resolved implicit enum */
                if (cv->kind == NODE_IMPLICIT_ENUM &&
                    cv->data.implicit_enum.resolved_enum) {
                    enum_name = cv->data.implicit_enum.resolved_enum;
                }
                /* Infer from when pattern */
                if (cv->kind == NODE_WHEN_PATTERN &&
                    cv->data.when_pattern.enum_name) {
                    enum_name = cv->data.when_pattern.enum_name;
                }
            }
        }
        if (enum_name) {
            /* Find the enum's variants */
            int enum_idx = -1;
            for (int enum_index = find_enum_index(checker, enum_name);
                 enum_index >= 0; enum_index = -1) {
                {
                    enum_idx = enum_index;
                    break;
                }
            }
            if (enum_idx >= 0) {
                int variant_count = checker->enum_value_counts[enum_idx];
                const char **variants = checker->enum_values[enum_idx];
                /* Collect covered variants from case branches */
                for (int variant_index = 0; variant_index < variant_count; variant_index++) {
                    bool covered = false;
                    for (int const_index = 0; const_index < node->data.when_stmt.case_count && !covered; const_index++) {
                        for (int cj = 0; cj < node->data.when_stmt.cases[const_index].value_count && !covered; cj++) {
                            AstNode *cv = node->data.when_stmt.cases[const_index].values[cj];
                            /* Match Enum.VARIANT, and the module-qualified
                             * mod.Enum.VARIANT, whose object is itself a
                             * member expression rather than a bare label. */
                            if (cv->kind == NODE_MEMBER_EXPR &&
                                (ast_member_qualifier(cv) || ast_member_chain(cv, NULL, NULL)) &&
                                strcmp(cv->data.member.member, variants[variant_index]) == 0) {
                                covered = true;
                            }
                            /* Match .VARIANT (implicit enum selector) */
                            if (cv->kind == NODE_IMPLICIT_ENUM &&
                                strcmp(cv->data.implicit_enum.variant, variants[variant_index]) == 0) {
                                covered = true;
                            }
                            /* Match when pattern (destructuring) */
                            if (cv->kind == NODE_WHEN_PATTERN &&
                                strcmp(cv->data.when_pattern.variant, variants[variant_index]) == 0) {
                                covered = true;
                            }
                            /* Match bare integer literal (for auto-increment enums) */
                            if (cv->kind == NODE_INT_VALUE &&
                                cv->data.int_value.value == variant_index) {
                                covered = true;
                            }
                        }
                    }
                    if (!covered) {
                        diagnostic_error_code_formatted(checker->diag, "E3056", NODE_FILE(checker, node), node->token.line, node->token.column, 0, enum_name, variants[variant_index]);
                    }
                }
            }
        } else {
            /* #strict on non-enum: just warn that it has no effect without default */
            diagnostic_error_message(checker->diag, "E3056",
                "#strict when on a non-enum type requires a default branch to be exhaustive",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* W3005: when matches on enum values but has no #strict and no default */
    if (!node->data.when_stmt.is_strict && !node->data.when_stmt.default_body) {
        bool has_enum_case = false;
        for (int const_index = 0; const_index < node->data.when_stmt.case_count && !has_enum_case; const_index++) {
            for (int cj = 0; cj < node->data.when_stmt.cases[const_index].value_count && !has_enum_case; cj++) {
                AstNode *cv = node->data.when_stmt.cases[const_index].values[cj];
                if (cv->kind == NODE_IMPLICIT_ENUM || cv->kind == NODE_WHEN_PATTERN) {
                    has_enum_case = true;
                } else if (ast_member_qualifier(cv)) {
                    const char *name = ast_member_qualifier(cv);
                    if (is_enum_name(checker, name)) {
                        has_enum_case = true;
                    } else {
                        for (int using_index = 0; using_index < checker->using_module_count && !has_enum_case; using_index++) {
                            if (!using_module_accessible(checker, using_index)) continue;
                            char prefixed[MSG_BUF_SIZE];
                            module_member_key(checker, checker->using_modules[using_index],
                                              name, prefixed, sizeof(prefixed));
                            if (is_enum_name(checker, prefixed)) has_enum_case = true;
                        }
                    }
                }
            }
        }
        if (has_enum_case) {
            diagnostic_warning_message(checker->diag, "W3005",
                "when statement matches on enum values without #strict and no default; exhaustiveness is not checked",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }
    /* Pointer checker: settle the joined arena state. A when with no default
     * (and not #strict) may match nothing, so the pre-when state is also a
     * possible outcome. */
    if (pc_have && !node->data.when_stmt.is_strict &&
        !node->data.when_stmt.default_body) {
        PcArenaSnap m = pc_merged;
        pc_restore(checker, pc_pre);
        pc_join(checker, m);
        pc_merged = pc_snap(checker);
        pc_snap_free(m);
    }
    if (pc_have) { pc_restore(checker, pc_merged); pc_snap_free(pc_merged); }
    else pc_restore(checker, pc_pre);
    pc_snap_free(pc_pre);
}

static void check_statement(TypeChecker *checker, AstNode *node) {
    if (!node) return;

    /* E2056: executable statements not allowed at file scope */
    if (checker->func_depth == 0) {
        bool is_executable = (node->kind == NODE_ASSIGN_STMT ||
                              node->kind == NODE_IF_STMT ||
                              node->kind == NODE_FOR_STMT ||
                              node->kind == NODE_FOR_EACH_STMT ||
                              node->kind == NODE_WHILE_STMT ||
                              node->kind == NODE_LOOP_STMT ||
                              node->kind == NODE_EXPR_STMT ||
                              node->kind == NODE_RETURN_STMT ||
                              node->kind == NODE_BREAK_STMT ||
                              node->kind == NODE_CONTINUE_STMT ||
                              node->kind == NODE_WHEN_STMT);
        if (is_executable) {
            diagnostic_error_message(checker->diag, "E2056",
                "executable statements are not allowed at file scope; put this inside a function",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
    }

    switch (node->kind) {
    case NODE_VAR_DECL:
        check_var_decl(checker, node);
        break;

    case NODE_ASSIGN_STMT:
        check_assign_stmt(checker, node);
        break;

    case NODE_RETURN_STMT:
        check_return_stmt(checker, node);
        break;

    case NODE_EXPR_STMT:
        check_expr_stmt(checker, node);
        break;

    case NODE_BLOCK_STMT:
        /* Inline blocks (from multi-var expansion) share parent scope.
         * Only control flow blocks (if, for, etc.) create new scopes,
         * and those are handled by their own cases. */
        check_block(checker, node);
        break;

    case NODE_IF_STMT:
        check_if_stmt(checker, node);
        break;

    case NODE_FOR_STMT:
        check_for_stmt(checker, node);
        break;

    case NODE_FOR_EACH_STMT:
        check_for_each_stmt(checker, node);
        break;

    case NODE_WHILE_STMT:
        check_while_stmt(checker, node);
        break;

    case NODE_LOOP_STMT: {
        /* E3129: empty loop body hangs forever at runtime */
        if (node->data.loop_stmt.body &&
            node->data.loop_stmt.body->data.block.count == 0) {
            diagnostic_error_code(checker->diag, "E3129",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        Scope *lp_outer = checker->current_scope;
        Scope *lp_scope = scope_create(lp_outer);
        checker->current_scope = lp_scope;
        checker->loop_depth++;
        pc_premark_loop_body(checker, node->data.loop_stmt.body, node->data.loop_stmt.body);
        check_block(checker, node->data.loop_stmt.body);
        checker->loop_depth--;
        checker->current_scope = lp_outer;
        scope_destroy(lp_scope);
        break;
    }

    case NODE_BREAK_STMT:
    case NODE_CONTINUE_STMT:
        if (checker->loop_depth == 0) {
            diagnostic_error_code(checker->diag, "E2050",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        break;

    case NODE_FUNC_DECL:
        check_func_decl(checker, node);
        break;

    case NODE_IMPORT_STMT:
        if (checker->func_depth > 0) {
            diagnostic_error_code(checker->diag, "E2036", NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        break;

    case NODE_USING_STMT:
        /* Function-scoped using: add modules to the using list so
         * bare-name resolution works for the rest of this scope. */
        for (int j = 0; j < node->data.using_stmt.count; j++) {
            if (checker->using_module_count >= checker->using_module_cap) {
                checker->using_module_cap = checker->using_module_cap ? checker->using_module_cap * 2 : 8;
                checker->using_modules = xrealloc(checker->using_modules,
                    sizeof(const char *) * (size_t)checker->using_module_cap);
                checker->using_module_files = xrealloc(checker->using_module_files,
                    sizeof(const char *) * (size_t)checker->using_module_cap);
                checker->using_module_import_indices = xrealloc(checker->using_module_import_indices,
                    sizeof(int) * (size_t)checker->using_module_cap);
            }
            checker->using_module_files[checker->using_module_count] = node->token.file;
            checker->using_module_import_indices[checker->using_module_count] =
                typechecker_find_import_index(checker, node->data.using_stmt.modules[j]);
            checker->using_modules[checker->using_module_count++] = node->data.using_stmt.modules[j];
        }
        break;

    case NODE_ENSURE_STMT:
        resolve_expression(checker, node->data.ensure_stmt.expr);
        if (node->data.ensure_stmt.expr &&
            node->data.ensure_stmt.expr->kind != NODE_CALL_EXPR) {
            diagnostic_error_code(checker->diag, "E3039", NODE_FILE(checker, node), node->token.line, node->token.column, 0);
        }
        /* Pointer checker: ensure mem.destroy(a) — mark the destroy pending
         * (pc_apply_ensure_mem_call), not applied via pc_apply_mem_call like
         * a bare statement would be: that sets the arena destroyed
         * immediately, which would flag every ordinary use of it for the
         * rest of the function — exactly the pattern `ensure mem.destroy`
         * exists to let the caller write. */
        if (node->data.ensure_stmt.expr &&
            node->data.ensure_stmt.expr->kind == NODE_CALL_EXPR)
            pc_apply_ensure_mem_call(checker, node->data.ensure_stmt.expr, node);
        break;

    case NODE_STRUCT_DECL:
        check_struct_decl(checker, node);
        break;

    case NODE_ENUM_DECL:
        /* E2053: enum inside function */
        if (checker->func_depth > 0) {
            diagnostic_error_code_formatted(checker->diag, "E2053",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                "enum", ENUM_DISPLAY_NAME(node));
        }
        /* E3099: enum name collides with a stdlib opaque type */
        check_stdlib_opaque_name_collision(checker, node, ENUM_DISPLAY_NAME(node));
        break;

    case NODE_ALIAS_DECL:
        /* E2053: alias inside function */
        if (checker->func_depth > 0) {
            diagnostic_error_code_formatted(checker->diag, "E2053",
                NODE_FILE(checker, node), node->token.line, node->token.column, 0,
                "alias", node->data.alias_decl.name);
        }
        break;

    case NODE_WHEN_STMT:
        check_when_stmt(checker, node);
        break;

    default:
        break;
    }
}

/* --- Registration pass --- */


/* : returns true if any NODE_STRUCT_DECL in the program has the given
 * name. Used by pointer-field pointee validation to accept forward
 * references and self-recursion before the struct is registered. */
static bool struct_name_declared(AstNode *program, const char *name) {
    if (!program || !name) return false;
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt && stmt->kind == NODE_STRUCT_DECL && stmt->data.struct_decl.name &&
            strcmp(stmt->data.struct_decl.name, name) == 0)
            return true;
    }
    return false;
}

/* : look up a struct declaration in the program by name. Returns
 * NULL if no struct with the given name exists. Used by the by-value
 * recursion detector below. */
static AstNode *find_struct_in_program(AstNode *program, const char *name) {
    if (!program || !name) return NULL;
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt && stmt->kind == NODE_STRUCT_DECL && stmt->data.struct_decl.name &&
            strcmp(stmt->data.struct_decl.name, name) == 0) {
            return stmt;
        }
    }
    return NULL;
}

/* : depth-first walk of a struct's by-value field graph, testing
 * whether `target` appears transitively. Pointer fields (^T), arrays
 * ([T]), and maps (map[K:V]) are treated as heap-indirected; they
 * break the chain since the inner type lives behind a fat-pointer
 * header, not inline. `visited` is a stack of struct names on the
 * current DFS path used to short-circuit cycles that don't touch
 * `target` directly. */
static bool struct_contains_by_value(TypeChecker *checker, AstNode *program, AstNode *decl,
                                      const char *target,
                                      const char **visited, int *visited_count,
                                      int visited_cap) {
    if (!decl || decl->kind != NODE_STRUCT_DECL) return false;
    for (int i = 0; i < *visited_count; i++) {
        if (strcmp(visited[i], decl->data.struct_decl.name) == 0) return false;
    }
    if (*visited_count >= visited_cap) return false;
    visited[(*visited_count)++] = decl->data.struct_decl.name;

    for (int i = 0; i < decl->data.struct_decl.field_count; i++) {
        const char *ftn = decl->data.struct_decl.fields[i].type_name;
        if (!ftn || !*ftn) continue;
        /* A field whose type is written as an alias is resolved here: the
         * enclosing struct's own fields are rewritten before this walk, but
         * a struct reached transitively is visited with its AST field names
         * still as written, so an alias hid the cycle. */
        ftn = resolve_type_alias(checker, checker_resolve_type_name(checker, ftn));
        /* Pointer, array, map; heap-indirected, size doesn't propagate. */
        if (ftn[0] == '^' || ftn[0] == '[' || strncmp(ftn, "map[", 4) == 0) continue;
        if (strcmp(ftn, target) == 0) {
            (*visited_count)--;
            return true;
        }
        AstNode *child = find_struct_in_program(program, ftn);
        if (child && struct_contains_by_value(checker, program, child, target,
                                               visited, visited_count, visited_cap)) {
            (*visited_count)--;
            return true;
        }
    }
    (*visited_count)--;
    return false;
}

/* Recursively validate that every named type inside a field's type string
 * (including array element types, map key/value types, pointer pointees, and
 * arbitrarily nested combinations) refers to a known type. Emits E4016 for
 * every unknown leaf type found. */
static void validate_field_type_recursive(TypeChecker *checker, AstNode *program,
                                          const char *type_name,
                                          const char *field_name,
                                          AstNode *stmt, bool nested) {
    if (!type_name || !*type_name) return;

    /* Containers recurse on what they are built from: the element of an
     * array, both halves of a map, the pointee of a pointer. */
    {
        char parts[2][MSG_BUF_SIZE];
        int part_count = 0;
        if (type_name_components(type_name, parts, &part_count)) {
            for (int i = 0; i < part_count; i++)
                validate_field_type_recursive(checker, program, parts[i], field_name, stmt, true);
            return;
        }
    }

    /* Leaf: must be a known primitive, enum, struct, or wildcard '?' */
    if (type_name_has_wildcard(type_name)) return;

    /* Naming a type is a use of the module that defines it. Function
     * parameters and return types already mark it; without this a struct
     * field was the one reference that did not, so an import used only for
     * field types warned as unused (W1002). The recursion above has already
     * peeled off arrays, maps and pointers, so this leaf is the bare name
     * the prefix match expects. */
    typechecker_mark_type_module_used(checker, type_name);

    /* Bare func: reject only as a direct scalar field type. As a container
     * element ([func], map[string:func]) it is the documented spelling and
     * works the same as a local of that type. */
    if (!nested && strcmp(type_name, "func") == 0) {
        diagnostic_error_code_help(checker->diag, "E3130",
            NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0,
            "use a typed signature: 'func(params) -> return_type'");
        return;
    }

    GrayType *resolved_type = typechecker_type_from_name(checker, type_name);
    if (resolved_type && resolved_type->kind != TK_STRUCT && resolved_type->kind != TK_UNKNOWN) return;
    if (is_enum_name(checker, type_name)) return;
    if (is_struct_name(checker, type_name)) return;
    if (struct_name_declared(program, type_name)) return;
    /* Forward reference: the flat registries are filled in program order, so
     * a field may name a type registered later or in another module. The
     * symbol table is complete before any of this runs, so it is what says
     * whether the type exists. */
    {
        char buf[MSG_BUF_SIZE];
        if (checker_resolve_decl_into(checker, type_name, buf, sizeof(buf)) != type_name)
            return;
    }

    /* Stdlib opaque struct types are registered after user structs; accept
     * them here so struct fields can reference them without false E4016. */
    if (is_reserved_stdlib_struct_name(type_name)) return;

    char *msg = NULL;
    msg = typechecker_format(checker,
        "field '%s' references undefined type '%s'",
        field_name, type_name);
    diagnostic_error_message(checker->diag, "E4016", msg,
        NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
}

/* ── declaration registration sub-handlers ────────────────────────── */

/* Insert a top-level declaration into its module's scope, keyed by the name
 * as written in source. The module is the one owning the file the declaration
 * was written in, so every file of a directory import lands in one scope.
 *
 * Transitional: the import merge still renames imported declarations, so the
 * source name is `source_name` (the *_DISPLAY_NAME macros) while `stmt` still
 * carries the mangled one. Once the merge stops renaming, the two coincide. */
/* The flat-registry key for a declaration: the mangled name of the entry the
 * symbol table holds for it. `fallback` covers declarations the table has no
 * entry for, which today is none — it is there so a future registration path
 * that runs before the table is populated degrades to the old spelling rather
 * than to a wrong one.
 *
 * This is the point of issue #2485's "mangling becomes a pure function of a
 * resolved declaration": the key no longer comes from a name the import merge
 * rewrote, it comes from where the declaration actually lives. */
static const char *decl_registry_key(TypeChecker *checker, const DeclEntry *entry,
                                     const char *fallback) {
    return entry ? module_mangle(checker->modules, entry) : fallback;
}

static DeclEntry *module_register(TypeChecker *checker, AstNode *stmt,
                                  DeclKind kind, const char *source_name,
                                  bool is_private) {
    const char *module = module_table_module_for_file(checker->modules, stmt->token.file);
    ModuleScope *scope = module_table_find(checker->modules, module);
    if (!scope || !source_name) return NULL;
    return module_scope_define(checker->modules, scope, kind, source_name,
                               stmt, NODE_FILE(checker, stmt),
                               stmt->token.line,
                               is_private ? VIS_PRIVATE : VIS_PUBLIC);
}

/* Put a stdlib module's declarations in the symbol table when it is imported,
 * so `mod.name` resolves through the same path a user module does. Gated on
 * the import: an unimported stdlib module must stay unresolvable, or the
 * unknown-module and unused-import diagnostics lose their basis.
 *
 * These entries are for resolution and visibility only — the C name of a
 * stdlib call comes from its emitter, which is why they are external. */
static void register_stdlib_module(TypeChecker *checker, const char *module) {
    if (!checker->modules || !module) return;
    if (module_table_find(checker->modules, module)) return;

    for (int i = 0; stdlib_opaque_map[i].type; i++) {
        if (strcmp(stdlib_opaque_map[i].mod, module) != 0) continue;
        DeclEntry *entry = module_table_declare_synthetic(checker->modules, module,
            DECL_STRUCT, stdlib_opaque_map[i].type, NULL);
        if (entry) entry->external = true;
    }
    /* Enums a stdlib module exposes (io.OpenFlag, os.Platform). Registered as
     * ordinary enums so EnumName.VARIANT, `when`, and `type_of` all work; the
     * module.VARIANT spelling is typed through _using_consts above. */
    for (int i = 0; stdlib_enum_map[i].name; i++) {
        if (strcmp(stdlib_enum_map[i].mod, module) != 0) continue;
        if (find_enum_index(checker, stdlib_enum_map[i].name) >= 0) continue;
        register_enum(checker, stdlib_enum_map[i].name, stdlib_enum_map[i].name,
            false, (const char **)stdlib_enum_map[i].variants, stdlib_enum_map[i].count,
            NULL, NULL, false, false, false, NULL);
        type_enum(stdlib_enum_map[i].name);
    }
    for (int i = 0; _using_consts[i].name; i++) {
        if (strcmp(_using_consts[i].mod, module) != 0) continue;
        DeclEntry *entry = module_table_declare_synthetic(checker->modules, module,
            DECL_CONST, _using_consts[i].name, NULL);
        if (entry) { entry->external = true; entry->registry_index = i; }
    }
    for (int i = 0; i < STDLIB_META_N; i++) {
        if (strcmp(stdlib_func_meta[i].mod, module) != 0) continue;
        DeclEntry *entry = module_table_declare_synthetic(checker->modules, module,
            DECL_FUNC, stdlib_func_meta[i].fn, NULL);
        if (entry) { entry->external = true; entry->registry_index = i; }
    }
}

static void register_decl_imports(TypeChecker *checker, AstNode *program) {
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind == NODE_IMPORT_STMT) {
            for (int j = 0; j < stmt->data.import_stmt.count; j++) {
                ImportItem *item = &stmt->data.import_stmt.items[j];
                if (item->is_stdlib && item->module && is_stdlib_module_name(item->module))
                    register_stdlib_module(checker, item->module);
                if (item->is_stdlib && item->module && !is_stdlib_module_name(item->module)) {
                    diagnostic_error_code_formatted(checker->diag, "E6001", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, item->module);
                }
                /* Record import for unused-import tracking.
                 * Store the ALIAS as the import name (so using/dot notation uses the alias). */
                if (item->module || item->alias) {
                    if (checker->import_count >= checker->import_cap) {
                        checker->import_cap = checker->import_cap ? checker->import_cap * 2 : 16;
                        checker->imported_modules = xrealloc(checker->imported_modules, sizeof(const char *) * checker->import_cap);
                        checker->import_files = xrealloc(checker->import_files, sizeof(const char *) * checker->import_cap);
                        checker->import_lines = xrealloc(checker->import_lines, sizeof(int) * checker->import_cap);
                        checker->import_used = xrealloc(checker->import_used, sizeof(bool) * checker->import_cap);
                        checker->import_is_stdlib = xrealloc(checker->import_is_stdlib, sizeof(bool) * checker->import_cap);
                    }
                    checker->imported_modules[checker->import_count] = item->alias ? item->alias : item->module;
                    checker->import_files[checker->import_count] = stmt->token.file; /* NULL = main file */
                    checker->import_lines[checker->import_count] = stmt->token.line;
                    checker->import_used[checker->import_count] = item->is_c_import; /* C imports are always "used" */
                    checker->import_is_stdlib[checker->import_count] = item->is_stdlib;
                    checker->import_count++;
                }
                /* Track alias → module mapping */
                module_table_add_alias(checker->modules, item->alias, item->module);
            }
        }
    }
}

static void register_decl_aliases(TypeChecker *checker, AstNode *program) {
    /* Register type aliases BEFORE enums/structs so that alias
     * names can be used in struct fields and enum payloads. */
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind != NODE_ALIAS_DECL) continue;

        checker->current_check_file = stmt->token.file;
        const char *aname = stmt->data.alias_decl.name;
        /* The target is written in the alias's own module, so it has to be
         * mapped onto its registry spelling like any other type annotation.
         * Left as written, a target naming a type in a non-entry module —
         * registered as `mod_Point`, not `Point` — resolved to nothing, and
         * so did every alias chained onto another alias in such a module. */
        const char *target = checker_resolve_type_name(checker,
                                                       stmt->data.alias_decl.target_type);

        /* E4020: duplicate alias name */
        for (int j = 0; j < checker->type_alias_count; j++) {
            if (strcmp(checker->type_alias_names[j], aname) == 0) {
                diagnostic_error_code_formatted(checker->diag, "E4020",
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, aname);
                goto next_alias;
            }
        }
        /* E2038: a built-in type name is not available to redeclare. Struct
         * and enum declarations already reject one; an alias did not, so
         * `alias int = float` silently redefined int for the rest of the file.
         * Skip registration so every later use of the name still means what
         * the language says it means. */
        if (is_reserved_type_name(aname)) {
            char *msg = typechecker_format(checker,
                "'%s' is a reserved type name and cannot be used as an alias name", aname);
            diagnostic_error_message(checker->diag, "E2038", msg,
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            goto next_alias;
        }
        /* E5016: a builtin's name is not available either. Structs, enums,
         * variables, and functions all run this check; the alias declaration
         * was the one site that did not. */
        if (is_reserved_builtin_func_name(aname)) {
            char *msg = typechecker_format(checker,
                "'%s' is a builtin function and cannot be used as an alias name", aname);
            diagnostic_error_message(checker->diag, "E5016", msg,
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            goto next_alias;
        }
        /* E4026: 'main' is reserved for the entry-point function */
        if (strcmp(aname, "main") == 0) {
            check_reserved_main(checker, aname, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column);
            goto next_alias;
        }
        /* E4007: collision with existing struct/enum type */
        if (type_name_already_declared(checker, aname, stmt) ||
            is_struct_name(checker, aname) || is_enum_name(checker, aname)) {
            char *msg = typechecker_format(checker,
                "a type named '%s' is already declared", aname);
            diagnostic_error_message(checker->diag, "E4007", msg,
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E3135: cannot alias wildcard type */
        if (strcmp(target, "?") == 0) {
            diagnostic_error_code_formatted(checker->diag, "E3135",
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, aname);
            goto next_alias;
        }
        /* Register the alias */
        if (checker->type_alias_count >= checker->type_alias_cap) {
            checker->type_alias_cap = checker->type_alias_cap ? checker->type_alias_cap * 2 : 8;
            checker->type_alias_names = xrealloc(checker->type_alias_names, sizeof(const char *) * (size_t)checker->type_alias_cap);
            checker->type_alias_targets = xrealloc(checker->type_alias_targets, sizeof(const char *) * (size_t)checker->type_alias_cap);
            checker->type_alias_nodes = xrealloc(checker->type_alias_nodes, sizeof(AstNode *) * (size_t)checker->type_alias_cap);
        }
        checker->type_alias_targets[checker->type_alias_count] = target;
        checker->type_alias_nodes[checker->type_alias_count] = stmt;
        {
            DeclEntry *entry = module_register(checker, stmt, DECL_ALIAS, aname,
                                               stmt->data.alias_decl.is_private);
            checker->type_alias_names[checker->type_alias_count] =
                decl_registry_key(checker, entry, aname);
        }
        checker->type_alias_count++;

        next_alias:;
    }

    /* Validate alias targets exist and detect cycles (after all aliases registered) */
    for (int i = 0; i < checker->type_alias_count; i++) {
        const char *target = checker->type_alias_targets[i];
        /* Cycle detection: follow the chain and check for revisiting */
        const char *cur = target;
        bool cycle = false;
        for (int depth = 0; depth < 32; depth++) {
            if (strcmp(cur, checker->type_alias_names[i]) == 0) {
                cycle = true;
                break;
            }
            bool found = false;
            for (int j = 0; j < checker->type_alias_count; j++) {
                if (strcmp(checker->type_alias_names[j], cur) == 0) {
                    cur = checker->type_alias_targets[j];
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        if (cycle) {
            AstNode *decl = checker->type_alias_nodes[i];
            diagnostic_error_code_formatted(checker->diag, "E3133",
                NODE_FILE(checker, decl), decl->token.line, decl->token.column, 0,
                decl->data.alias_decl.name);
            continue;
        }
        /* E3132: target type must exist — resolve fully then check.
         * Primitives and collection types are always valid; only uppercase
         * names (structs/enums) need registration. Defer this check until
         * after struct/enum registration so aliases to user-defined types
         * don't false-positive during this early pass. We'll check later. */
    }
}

static void register_decl_enums(TypeChecker *checker, AstNode *program) {
    /* Register enums BEFORE structs so that struct field types
     * referencing enums resolve correctly via typechecker_type_from_name(). */
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind != NODE_ENUM_DECL) continue;

        /* E2038: reserved name for enums */
        const char *en = ENUM_DISPLAY_NAME(stmt);
        if (is_reserved_type_name(stmt->data.enum_decl.name)) {
            char *msg = NULL;
            msg = typechecker_format(checker, "'%s' is a reserved type name and cannot be used as an enum name", en);
            diagnostic_error_message(checker->diag, "E2038", msg, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E5016: builtin function name as enum name */
        if (is_reserved_builtin_func_name(stmt->data.enum_decl.name)) {
            char *msg = NULL;
            msg = typechecker_format(checker, "'%s' is a builtin function and cannot be used as an enum name", en);
            diagnostic_error_message(checker->diag, "E5016", msg, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E5035: stdlib module name as enum name */
        if (is_stdlib_module_name(stmt->data.enum_decl.name)) {
            char *msg = NULL;
            msg = typechecker_format(checker, "'%s' is a standard library module and cannot be used as an enum name", en);
            diagnostic_error_message(checker->diag, "E5035", msg, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E4026: 'main' is reserved for the entry-point function */
        check_reserved_main(checker, stmt->data.enum_decl.name, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column);
        /* E2016: empty enum */
        if (stmt->data.enum_decl.value_count == 0) {
            diagnostic_error_code_formatted(checker->diag, "E2016", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, en);
        }
        /* E3033: check for duplicate enum values */
        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            if (!stmt->data.enum_decl.values[j].value) continue;
            for (int k = 0; k < j; k++) {
                if (!stmt->data.enum_decl.values[k].value) continue;
                if (stmt->data.enum_decl.values[j].value->kind == NODE_INT_VALUE &&
                    stmt->data.enum_decl.values[k].value->kind == NODE_INT_VALUE &&
                    stmt->data.enum_decl.values[j].value->data.int_value.value ==
                    stmt->data.enum_decl.values[k].value->data.int_value.value) {
                    diagnostic_error_code_formatted(checker->diag, "E3033", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, en,
                        stmt->data.enum_decl.values[k].name,
                        stmt->data.enum_decl.values[j].name);
                    break;
                }
            }
        }
        /* E2014: check for duplicate enum variant names */
        /* E2065: check variant name vs enum type name */
        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            const char *vname = stmt->data.enum_decl.values[j].name;
            if (strcmp(vname, stmt->data.enum_decl.name) == 0) {
                diagnostic_error_code_formatted(checker->diag, "E2065", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, vname,
                    en);
            }
            /* E2038: reserved type name as enum variant */
            if (is_reserved_type_name(vname)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s' is a reserved type name and cannot be used as an enum variant name",
                    vname);
                diagnostic_error_message(checker->diag, "E2038", msg,
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            /* E5016: builtin function name as enum variant */
            if (is_reserved_builtin_func_name(vname)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s' is a builtin function and cannot be used as an enum variant name",
                    vname);
                diagnostic_error_message(checker->diag, "E5016", msg,
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            /* E5035: stdlib module name as enum variant */
            if (is_stdlib_module_name(vname)) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s' is a standard library module and cannot be used as an enum variant name",
                    vname);
                diagnostic_error_message(checker->diag, "E5035", msg,
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            for (int k = 0; k < j; k++) {
                if (strcmp(stmt->data.enum_decl.values[k].name, vname) == 0) {
                    diagnostic_error_code_formatted(checker->diag, "E2014", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, vname,
                        en);
                    break;
                }
            }
        }
        /* Detect string enum (auto-detect from values) */
        bool is_str = false;
        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            if (stmt->data.enum_decl.values[j].value &&
                stmt->data.enum_decl.values[j].value->kind == NODE_STRING_VALUE) {
                is_str = true;
                break;
            }
        }
        /* E4007: duplicate enum name. An enum named `main` collides with the
         * entry point, but E4026 already says so — don't pile on. A collision
         * with a stdlib-provided enum (io.OpenFlag, os.Platform) is reported by
         * E3099 instead — that enum is pre-registered, so is_enum_name is true. */
        if (strcmp(stmt->data.enum_decl.name, "main") != 0 &&
            !stdlib_enum_module(stmt->data.enum_decl.name) &&
            (type_name_already_declared(checker, stmt->data.enum_decl.name, stmt) ||
            is_enum_name(checker, stmt->data.enum_decl.name) ||
            is_struct_name(checker, stmt->data.enum_decl.name))) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "a type named '%s' is already declared",
                ENUM_DISPLAY_NAME(stmt));
            diagnostic_error_message(checker->diag, "E4007", msg,
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        int variant_count = stmt->data.enum_decl.value_count;
        const char **vnames = arena_alloc(checker->arena, sizeof(const char *) * (variant_count ? variant_count : 1));
        for (int j = 0; j < variant_count; j++) {
            vnames[j] = stmt->data.enum_decl.values[j].name;
        }
        /* Extract payload info for tagged enums */
        bool has_tagged = stmt->data.enum_decl.is_tagged;
        const char ***pt = NULL;
        int *payload_counts = NULL;
        if (variant_count > 0) {
            pt = arena_alloc(checker->arena, sizeof(const char **) * variant_count);
            payload_counts = arena_alloc(checker->arena, sizeof(int) * variant_count);
            for (int j = 0; j < variant_count; j++) {
                EnumVal *ev = &stmt->data.enum_decl.values[j];
                payload_counts[j] = ev->payload_count;
                if (ev->payload_count > 0) {
                    pt[j] = arena_alloc(checker->arena, sizeof(const char *) * ev->payload_count);
                    for (int k = 0; k < ev->payload_count; k++) {
                        pt[j][k] = ev->payload_types[k];
                    }
                } else {
                    pt[j] = NULL;
                }
            }
        }
        /* E3111: string enum with payloads */
        if (is_str && has_tagged) {
            diagnostic_error_code(checker->diag, "E3111", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E3112: flags enum with payloads */
        if (stmt->data.enum_decl.is_flags && has_tagged) {
            diagnostic_error_code(checker->diag, "E3112", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E3143: flags enum with more than 63 variants. Codegen assigns each
         * unvalued variant `1LL << j`; bit 63 is int64's sign bit and bits
         * 64+ are undefined shifts in C. */
        if (stmt->data.enum_decl.is_flags && variant_count > 63) {
            diagnostic_error_code_formatted(checker->diag, "E3143", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0,
                en, variant_count);
        }
        DeclEntry *entry = module_register(checker, stmt, DECL_ENUM, ENUM_DISPLAY_NAME(stmt),
                            stmt->data.enum_decl.is_private);
        /* A name declared twice in one module resolves to the first
         * declaration; the duplicate must not repoint it at its own
         * registry slot, or the collision it is about to be reported
         * for would go unnoticed. */
        if (entry && entry->kind == DECL_ENUM) entry->registry_index = checker->enum_count;
        register_enum(checker, decl_registry_key(checker, entry, stmt->data.enum_decl.name),
            ENUM_DISPLAY_NAME(stmt), is_str, vnames, variant_count, pt, payload_counts, has_tagged, stmt->data.enum_decl.is_flags, stmt->data.enum_decl.is_deprecated, stmt->data.enum_decl.deprecated_message);
    }
}

/* Assemble the program-wide open ErrorCode enum: the compiler-owned builtin
 * variants first (slots 0..N-1), then the variants of every #error_code enum
 * in source order. Validates each #error_code enum and rejects duplicate
 * variant names across the whole set. Registered as a normal enum named
 * "ErrorCode" so .VARIANT selectors, ==, and `when` all work on it. */
static void register_error_code_set(TypeChecker *checker, AstNode *program) {
    int cap = 32, count = 0;
    const char **names = xmalloc(sizeof(const char *) * cap);
#define GRAY_ERR_ADD(n) names[count++] = #n;
    GRAY_ERROR_CODE_BUILTINS(GRAY_ERR_ADD)
#undef GRAY_ERR_ADD

    int ec_cap = 8;
    checker->error_code_enum_names = xmalloc(sizeof(const char *) * ec_cap);
    checker->error_code_enum_count = 0;

    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind != NODE_ENUM_DECL || !stmt->data.enum_decl.is_error_code) continue;

        const char *en = ENUM_DISPLAY_NAME(stmt);
        bool bad = false;

        if (stmt->data.enum_decl.is_tagged) {
            diagnostic_error_code_formatted(checker->diag, "E3147",
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, en);
            bad = true;
        }
        if (stmt->data.enum_decl.is_flags) {
            diagnostic_error_code_formatted(checker->diag, "E3152",
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, en);
            bad = true;
        }
        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            AstNode *v = stmt->data.enum_decl.values[j].value;
            if (!v) continue;
            if (v->kind == NODE_STRING_VALUE) {
                diagnostic_error_code_formatted(checker->diag, "E3145",
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, en);
                bad = true;
                break;
            }
            diagnostic_error_code_formatted(checker->diag, "E3146",
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0,
                en, stmt->data.enum_decl.values[j].name);
            bad = true;
            break;
        }
        if (bad) continue;

        if (checker->error_code_enum_count >= ec_cap) {
            ec_cap *= 2;
            checker->error_code_enum_names = xrealloc(checker->error_code_enum_names,
                sizeof(const char *) * ec_cap);
        }
        /* Store the registry spelling, not the bare name: an imported
         * #error_code enum reaches typechecker_enum_is_error_code() as its
         * module-prefixed key (errs_ModErr), and a bare "ModErr" here would
         * never match, so cross-file widening to ErrorCode was rejected. */
        const char *saved_check_file = checker->current_check_file;
        checker->current_check_file = stmt->token.file;
        checker->error_code_enum_names[checker->error_code_enum_count++] =
            checker_resolve_enum_key(checker, stmt->data.enum_decl.name);
        checker->current_check_file = saved_check_file;

        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            const char *vn = stmt->data.enum_decl.values[j].name;
            bool dup = false;
            for (int k = 0; k < count; k++) {
                if (strcmp(names[k], vn) == 0) { dup = true; break; }
            }
            if (dup) {
                diagnostic_error_code_formatted(checker->diag, "E3148",
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, vn);
                continue;
            }
            if (count >= cap) {
                cap *= 2;
                names = xrealloc(names, sizeof(const char *) * cap);
            }
            names[count++] = vn;
        }
    }

    const char **owned = arena_alloc(checker->arena, sizeof(const char *) * count);
    for (int k = 0; k < count; k++) owned[k] = names[k];
    free(names);
    register_enum(checker, "ErrorCode", "ErrorCode", false, owned, count,
                  NULL, NULL, false, false, false, NULL);
}

static void register_decl_structs(TypeChecker *checker, AstNode *program) {
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind != NODE_STRUCT_DECL) continue;
        checker->current_check_file = stmt->token.file;

        /* E2067: empty struct */
        if (stmt->data.struct_decl.field_count == 0) {
            diagnostic_error_code_formatted(checker->diag, "E2067", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, STRUCT_DISPLAY_NAME(stmt));
        }
        int field_count = stmt->data.struct_decl.field_count;
        const char **fnames = arena_alloc(checker->arena, sizeof(const char *) * (field_count ? field_count : 1));
        GrayType **ftypes = arena_alloc(checker->arena, sizeof(GrayType *) * (field_count ? field_count : 1));
        for (int j = 0; j < field_count; j++) {
            fnames[j] = stmt->data.struct_decl.fields[j].name;
            ftypes[j] = typechecker_type_from_name(checker, stmt->data.struct_decl.fields[j].type_name);
            warn_if_type_name_deprecated(checker, stmt, stmt->data.struct_decl.fields[j].type_name, NULL);
            /* A field type written as an alias has to be recorded as what the
             * alias stands for. Only ftypes[j] above resolved it; the name left
             * in the AST is what every later reader sees, and codegen builds the
             * field's C type and its member accesses from that name. A field
             * declared `f Handler` was emitted as an untyped `void *` and its
             * call mangled as a struct function, while the same field written
             * `func(int) -> int` compiled correctly. */
            {
                const char *key = checker_resolve_type_name(checker,
                    stmt->data.struct_decl.fields[j].type_name);
                const char *resolved = resolve_type_alias(checker, key);
                if (resolved != key)
                    stmt->data.struct_decl.fields[j].type_name = resolved;
            }
            /* E3038: void field type */
            if (stmt->data.struct_decl.fields[j].type_name &&
                strcmp(stmt->data.struct_decl.fields[j].type_name, "void") == 0) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'void' cannot be used as a struct field type (field '%s')",
                    fnames[j]);
                diagnostic_error_message(checker->diag, "E3038", msg,
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            /* E2066: field name matches struct type name */
            if (strcmp(fnames[j], stmt->data.struct_decl.name) == 0) {
                diagnostic_error_code_formatted(checker->diag, "E2066", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, fnames[j], STRUCT_DISPLAY_NAME(stmt));
            }
            /* E5016: builtin function name as struct field name */
            if (is_reserved_builtin_func_name(fnames[j])) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s' is a builtin function and cannot be used as a struct field name",
                    fnames[j]);
                diagnostic_error_message(checker->diag, "E5016", msg,
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            /* E5035: stdlib module name as struct field name */
            if (is_stdlib_module_name(fnames[j])) {
                char *msg = NULL;
                msg = typechecker_format(checker,
                    "'%s' is a standard library module and cannot be used as a struct field name",
                    fnames[j]);
                diagnostic_error_message(checker->diag, "E5035", msg,
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            /* E3061: struct field cannot be the enclosing struct by value */
            const char *ftn = stmt->data.struct_decl.fields[j].type_name;
            if (ftn && *ftn && ftn[0] != '^' && ftn[0] != '[' &&
                strncmp(ftn, "map[", 4) != 0) {
                bool is_cycle = false;
                const char *self_name = stmt->data.struct_decl.name;
                if (strcmp(ftn, self_name) == 0) {
                    is_cycle = true;
                } else {
                    AstNode *child = find_struct_in_program(program, ftn);
                    if (child) {
                        const char *visited[MAX_STRUCT_DEPTH];
                        int variant_count = 0;
                        is_cycle = struct_contains_by_value(
                            checker, program, child, self_name, visited, &variant_count, 32);
                    }
                }
                if (is_cycle) {
                    char *msg = NULL;
                    const char *display = STRUCT_DISPLAY_NAME(stmt);
                    if (strcmp(ftn, self_name) == 0) {
                        msg = typechecker_format(checker,
                            "struct '%s' cannot contain itself by value; use a pointer field '^%s' for recursive types",
                            display, display);
                    } else {
                        msg = typechecker_format(checker,
                            "struct '%s' cannot contain itself by value through '%s'; break the cycle with a pointer field '^%s'",
                            display, ftn, ftn);
                    }
                    diagnostic_error_message(checker->diag, "E3061", msg,
                        NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
                }
            }
            /* Validate all named types within the field type recursively. */
            if (ftn && *ftn)
                validate_field_type_recursive(checker, program, ftn, fnames[j], stmt, false);
            /* Check for duplicate field names */
            for (int k = 0; k < j; k++) {
                if (strcmp(fnames[k], fnames[j]) == 0) {
                    diagnostic_error_code_formatted(checker->diag, "E2013", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, fnames[j], STRUCT_DISPLAY_NAME(stmt));
                    break;
                }
            }
            /* Resolve implicit enum selectors in field default values */
            if (stmt->data.struct_decl.fields[j].default_value) {
                GrayType *saved_expected = checker->expected_type;
                if (ftypes[j] && ftypes[j]->kind == TK_ENUM && ftypes[j]->name)
                    checker->expected_type = ftypes[j];
                resolve_expression(checker, stmt->data.struct_decl.fields[j].default_value);
                checker->expected_type = saved_expected;
            }
        }
        /* E2037/E2038: reserved name check for structs */
        const char *sn = STRUCT_DISPLAY_NAME(stmt);
        if (is_reserved_type_name(stmt->data.struct_decl.name)) {
            char *msg = NULL;
            msg = typechecker_format(checker, "'%s' is a reserved type name and cannot be used as a struct name", sn);
            diagnostic_error_message(checker->diag, "E2037", msg, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E5016: builtin function name as struct name */
        if (is_reserved_builtin_func_name(stmt->data.struct_decl.name)) {
            char *msg = NULL;
            msg = typechecker_format(checker, "'%s' is a builtin function and cannot be used as a struct name", sn);
            diagnostic_error_message(checker->diag, "E5016", msg, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E5035: stdlib module name as struct name */
        if (is_stdlib_module_name(stmt->data.struct_decl.name)) {
            char *msg = NULL;
            msg = typechecker_format(checker, "'%s' is a standard library module and cannot be used as a struct name", sn);
            diagnostic_error_message(checker->diag, "E5035", msg, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        /* E4026: 'main' is reserved for the entry-point function */
        check_reserved_main(checker, stmt->data.struct_decl.name, NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column);
        /* E4007: duplicate struct name. A struct named `main` collides with
         * the entry point, but E4026 already says so — don't pile on. */
        if (strcmp(stmt->data.struct_decl.name, "main") != 0 &&
            (type_name_already_declared(checker, stmt->data.struct_decl.name, stmt) ||
            is_struct_name(checker, stmt->data.struct_decl.name) ||
            is_enum_name(checker, stmt->data.struct_decl.name))) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "a type named '%s' is already declared",
                STRUCT_DISPLAY_NAME(stmt));
            diagnostic_error_message(checker->diag, "E4007", msg,
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        DeclEntry *entry = module_register(checker, stmt, DECL_STRUCT, STRUCT_DISPLAY_NAME(stmt),
                            stmt->data.struct_decl.is_private);
        /* A name declared twice in one module resolves to the first
         * declaration; the duplicate must not repoint it at its own
         * registry slot, or the collision it is about to be reported
         * for would go unnoticed. */
        if (entry && entry->kind == DECL_STRUCT) entry->registry_index = checker->struct_count;
        register_struct(checker, decl_registry_key(checker, entry, stmt->data.struct_decl.name),
                        STRUCT_DISPLAY_NAME(stmt), fnames, ftypes, field_count);
        checker->structs[checker->struct_count - 1].is_deprecated = stmt->data.struct_decl.is_deprecated;
        checker->structs[checker->struct_count - 1].deprecated_message = stmt->data.struct_decl.deprecated_message;

        /* Detect generic structs (any field with ? in type) */
        stmt->data.struct_decl.is_generic = false;
        stmt->data.struct_decl.instantiations = NULL;
        stmt->data.struct_decl.instantiation_count = 0;
        for (int j = 0; j < field_count; j++) {
            if (stmt->data.struct_decl.fields[j].type_name &&
                strchr(stmt->data.struct_decl.fields[j].type_name, '?')) {
                stmt->data.struct_decl.is_generic = true;
                break;
            }
        }

        /* Register struct-namespaced functions as StructName_funcName */
        for (int j = 0; j < stmt->data.struct_decl.func_count; j++) {
            AstNode *fn = stmt->data.struct_decl.funcs[j].func_decl;
            if (!fn || fn->kind != NODE_FUNC_DECL) continue;
            /* E2037: check for duplicate function names in struct */
            for (int k = 0; k < j; k++) {
                AstNode *prev = stmt->data.struct_decl.funcs[k].func_decl;
                if (prev && prev->kind == NODE_FUNC_DECL &&
                    strcmp(prev->data.func_decl.name, fn->data.func_decl.name) == 0) {
                    char *msg = NULL;
                    msg = typechecker_format(checker,
                        "duplicate function '%s' in struct '%s'",
                        FUNC_DISPLAY_NAME(fn), STRUCT_DISPLAY_NAME(stmt));
                    diagnostic_error_message(checker->diag, "E2037", msg,
                        NODE_FILE(checker, fn), fn->token.line, fn->token.column, 0);
                    break;
                }
            }
            /* E4022: function name conflicts with a top-level function.
             * A bare call inside the struct resolves to the top-level one,
             * leaving the struct function reachable only as Struct.func —
             * a silent shadow, so reject it at the declaration. */
            for (int k = 0; k < program->data.program.stmt_count; k++) {
                AstNode *top = program->data.program.stmts[k];
                if (top->kind != NODE_FUNC_DECL ||
                    strcmp(top->data.func_decl.name, fn->data.func_decl.name) != 0)
                    continue;
                diagnostic_error_code_formatted(checker->diag, "E4022",
                    NODE_FILE(checker, fn), fn->token.line, fn->token.column, 0,
                    STRUCT_DISPLAY_NAME(stmt), FUNC_DISPLAY_NAME(fn), FUNC_DISPLAY_NAME(top));
                break;
            }
            /* E2064: function name conflicts with field name */
            for (int k = 0; k < field_count; k++) {
                if (strcmp(fnames[k], fn->data.func_decl.name) == 0) {
                    diagnostic_error_code_formatted(checker->diag, "E2064", NODE_FILE(checker, fn), fn->token.line, fn->token.column, 0, FUNC_DISPLAY_NAME(fn), fnames[k],
                        STRUCT_DISPLAY_NAME(stmt));
                    break;
                }
            }
            int parameter_count = fn->data.func_decl.param_count;
            GrayType **ptypes = arena_alloc(checker->arena, sizeof(GrayType *) * (parameter_count ? parameter_count : 1));
            for (int k = 0; k < parameter_count; k++) {
                ptypes[k] = typechecker_type_from_name(checker, fn->data.func_decl.params[k].type_name);
                warn_if_type_name_deprecated(checker, fn, fn->data.func_decl.params[k].type_name,
                    stmt->data.struct_decl.name);
            }
            int return_count = fn->data.func_decl.return_type_count;
            GrayType **rtypes = arena_alloc(checker->arena, sizeof(GrayType *) * (return_count ? return_count : 1));
            for (int k = 0; k < return_count; k++) {
                rtypes[k] = typechecker_type_from_name(checker, fn->data.func_decl.return_types[k]);
                warn_if_type_name_deprecated(checker, fn, fn->data.func_decl.return_types[k],
                    stmt->data.struct_decl.name);
            }
            /* Register with prefixed name: StructName_funcName */
            char buffer[MSG_BUF_SIZE];
            /* A struct function is namespaced under its struct, and the
             * struct is namespaced under its module — so the key is built
             * from the struct's registry spelling, not from the name as
             * written in the declaration. */
            {
                DeclEntry *sdecl = module_table_entry_for_node(checker->modules, stmt);
                snprintf(buffer, sizeof(buffer), "%s_%s",
                    sdecl ? sdecl->name : stmt->data.struct_decl.name,
                    fn->data.func_decl.name);
                DeclEntry *fentry = module_table_declare_synthetic(checker->modules,
                    sdecl ? sdecl->module_name : NULL, DECL_FUNC,
                    arena_copy_string(checker->arena, buffer),
                    NODE_FILE(checker, stmt));
                if (fentry) fentry->registry_index = checker->func_count;
                snprintf(buffer, sizeof(buffer), "%s_%s",
                    decl_registry_key(checker, sdecl, stmt->data.struct_decl.name),
                    fn->data.func_decl.name);
            }
            const char *prefixed = arena_copy_string(checker->arena, buffer);
            register_func(checker, prefixed, ptypes, parameter_count, rtypes, return_count);
            checker->funcs[checker->func_count - 1].is_private = fn->data.func_decl.is_private;
            checker->funcs[checker->func_count - 1].is_discard = fn->data.func_decl.is_discard;
            checker->funcs[checker->func_count - 1].is_deprecated = fn->data.func_decl.is_deprecated;
            checker->funcs[checker->func_count - 1].deprecated_message = fn->data.func_decl.deprecated_message;
            if (fn->data.func_decl.is_discard && return_count == 0) {
                diagnostic_error_code_formatted(checker->diag, "E5042",
                    NODE_FILE(checker, fn), fn->token.line, fn->token.column, 0,
                    FUNC_DISPLAY_NAME(fn));
            }
            finalize_generic_signature(&checker->funcs[checker->func_count - 1], fn);
        }
    }
}

static void register_decl_functions(TypeChecker *checker, AstNode *program) {
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind != NODE_FUNC_DECL) continue;
        checker->current_check_file = stmt->token.file;

        int parameter_count = stmt->data.func_decl.param_count;
        GrayType **ptypes = arena_alloc(checker->arena, sizeof(GrayType *) * (parameter_count ? parameter_count : 1));
        for (int j = 0; j < parameter_count; j++) {
            ptypes[j] = typechecker_type_from_name(checker, stmt->data.func_decl.params[j].type_name);
            typechecker_mark_type_module_used(checker, stmt->data.func_decl.params[j].type_name);
            warn_if_type_name_deprecated(checker, stmt, stmt->data.func_decl.params[j].type_name, NULL);
        }

        int return_count = stmt->data.func_decl.return_type_count;
        GrayType **rtypes = arena_alloc(checker->arena, sizeof(GrayType *) * (return_count ? return_count : 1));
        for (int j = 0; j < return_count; j++) {
            rtypes[j] = typechecker_type_from_name(checker, stmt->data.func_decl.return_types[j]);
            typechecker_mark_type_module_used(checker, stmt->data.func_decl.return_types[j]);
            warn_if_type_name_deprecated(checker, stmt, stmt->data.func_decl.return_types[j], NULL);
        }

        /* E4008: main() cannot have parameters or return types */
        if (strcmp(stmt->data.func_decl.name, "main") == 0) {
            if (parameter_count > 0) {
                diagnostic_error_message(checker->diag, "E4008",
                    "'main' function cannot have parameters; main() takes no arguments",
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
            if (return_count > 0) {
                diagnostic_error_message(checker->diag, "E4008",
                    "'main' function cannot have a return type; main() always returns void",
                    NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
            }
        }
        /* E5046: #test functions take no parameters and return nothing */
        if (stmt->data.func_decl.is_test &&
            (parameter_count > 0 || return_count > 0)) {
            diagnostic_error_code_formatted(checker->diag, "E5046",
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0,
                FUNC_DISPLAY_NAME(stmt));
        }
        /* Check for reserved prefix */
        check_reserved_name(checker, stmt->data.func_decl.name,
            NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column);
        /* Check for duplicate function names */
        if (find_func(checker, stmt->data.func_decl.name)) {
            diagnostic_error_code_formatted(checker->diag, "E4004", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, FUNC_DISPLAY_NAME(stmt));
        }
        /* E4007: function name conflicts with a type. `do main()` paired with
         * a type named `main` already produced E4026 on the type — the real
         * entry point is not the problem, so don't flag it here. */
        if (strcmp(stmt->data.func_decl.name, "main") != 0 &&
            (is_struct_name(checker, stmt->data.func_decl.name) ||
            is_enum_name(checker, stmt->data.func_decl.name))) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "function '%s' conflicts with a type of the same name",
                FUNC_DISPLAY_NAME(stmt));
            diagnostic_error_message(checker->diag, "E4007", msg,
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0);
        }
        DeclEntry *entry = module_register(checker, stmt, DECL_FUNC, FUNC_DISPLAY_NAME(stmt),
                                           stmt->data.func_decl.is_private);
        /* A name declared twice in one module resolves to the first
         * declaration; the duplicate must not repoint it at its own
         * registry slot, or the collision it is about to be reported
         * for would go unnoticed. */
        if (entry && entry->kind == DECL_FUNC) entry->registry_index = checker->func_count;
        register_func(checker, decl_registry_key(checker, entry, stmt->data.func_decl.name),
                      ptypes, parameter_count, rtypes, return_count);
        checker->funcs[checker->func_count - 1].is_private = stmt->data.func_decl.is_private;
        checker->funcs[checker->func_count - 1].is_discard = stmt->data.func_decl.is_discard;
        checker->funcs[checker->func_count - 1].is_deprecated = stmt->data.func_decl.is_deprecated;
        checker->funcs[checker->func_count - 1].deprecated_message = stmt->data.func_decl.deprecated_message;
        /* E5042: #discard on a void function is an error */
        if (stmt->data.func_decl.is_discard && return_count == 0) {
            diagnostic_error_code_formatted(checker->diag, "E5042",
                NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0,
                FUNC_DISPLAY_NAME(stmt));
        }
        /* Store line for unused function warning */
        checker->funcs[checker->func_count - 1].def_line = stmt->token.line;
        finalize_generic_signature(&checker->funcs[checker->func_count - 1], stmt);
        /* Also register unprefixed name for internal cross-references. */
        const char *fn = stmt->data.func_decl.name;
        const char *underscore = strchr(fn, '_');
        if (underscore && underscore != fn) {
            const char *unprefixed = underscore + 1;
            if (strcmp(unprefixed, "main") != 0) {
                /* Only register if the prefix matches a known import */
                for (int ii = 0; ii < checker->import_count; ii++) {
                    size_t mod_len = strlen(checker->imported_modules[ii]);
                    if (strncmp(fn, checker->imported_modules[ii], mod_len) == 0 &&
                        fn[mod_len] == '_') {
                        register_func(checker, unprefixed, ptypes, parameter_count, rtypes, return_count);
                        checker->funcs[checker->func_count - 1].is_private = stmt->data.func_decl.is_private;
                        checker->funcs[checker->func_count - 1].is_discard = stmt->data.func_decl.is_discard;
                        checker->funcs[checker->func_count - 1].is_deprecated = stmt->data.func_decl.is_deprecated;
                        checker->funcs[checker->func_count - 1].deprecated_message = stmt->data.func_decl.deprecated_message;
                        checker->funcs[checker->func_count - 1].def_line = 0; /* suppress unused warning */
                        finalize_generic_signature(&checker->funcs[checker->func_count - 1], stmt);
                        break;
                    }
                }
            }
        }
    }
}

/* Module-level constants and variables. These have no flat-array registry of
 * their own — they are ordinary scope symbols — but they are module members,
 * so `mod.NAME` has to resolve to them. */
static void register_decl_consts(TypeChecker *checker, AstNode *program) {
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind != NODE_VAR_DECL || stmt->data.var_decl.synthetic) continue;
        const char *source_name = stmt->data.var_decl.original_name
            ? stmt->data.var_decl.original_name : stmt->data.var_decl.name;
        module_register(checker, stmt, DECL_CONST, source_name,
                        stmt->data.var_decl.is_private);
    }
}

/* Put every top-level declaration into its module's scope before anything
 * resolves a type. The passes below resolve field, parameter, and return
 * types as they register, and a type may name a declaration that appears
 * later in the merged program or in another module entirely — so the symbol
 * table has to be complete first. module_register is idempotent, so the
 * passes can keep calling it for the entry they need. */
static void register_decl_symbols(TypeChecker *checker, AstNode *program) {
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        switch (stmt->kind) {
        case NODE_ALIAS_DECL:
            module_register(checker, stmt, DECL_ALIAS, stmt->data.alias_decl.name,
                            stmt->data.alias_decl.is_private);
            break;
        case NODE_ENUM_DECL:
            module_register(checker, stmt, DECL_ENUM, ENUM_DISPLAY_NAME(stmt),
                            stmt->data.enum_decl.is_private);
            break;
        case NODE_STRUCT_DECL:
            module_register(checker, stmt, DECL_STRUCT, STRUCT_DISPLAY_NAME(stmt),
                            stmt->data.struct_decl.is_private);
            break;
        case NODE_FUNC_DECL:
            module_register(checker, stmt, DECL_FUNC, FUNC_DISPLAY_NAME(stmt),
                            stmt->data.func_decl.is_private);
            break;
        case NODE_VAR_DECL:
            if (!stmt->data.var_decl.synthetic) {
                const char *source_name = stmt->data.var_decl.original_name
                    ? stmt->data.var_decl.original_name : stmt->data.var_decl.name;
                module_register(checker, stmt, DECL_CONST, source_name,
                                stmt->data.var_decl.is_private);
            }
            break;
        default:
            break;
        }
    }
}

/* Record a using entry once per (module, file). */
static void using_add(TypeChecker *checker, const char *module, const char *file) {
    if (!module) return;
    for (int i = 0; i < checker->using_module_count; i++) {
        if (strcmp(checker->using_modules[i], module) != 0) continue;
        const char *f = checker->using_module_files[i];
        if ((!f && !file) || (f && file && strcmp(f, file) == 0)) return;
    }
    if (checker->using_module_count >= checker->using_module_cap) {
        checker->using_module_cap = checker->using_module_cap ? checker->using_module_cap * 2 : 8;
        checker->using_modules = xrealloc(checker->using_modules,
            sizeof(const char *) * (size_t)checker->using_module_cap);
        checker->using_module_files = xrealloc(checker->using_module_files,
            sizeof(const char *) * (size_t)checker->using_module_cap);
        checker->using_module_import_indices = xrealloc(checker->using_module_import_indices,
            sizeof(int) * (size_t)checker->using_module_cap);
    }
    checker->using_module_files[checker->using_module_count] = file;
    checker->using_module_import_indices[checker->using_module_count] =
        typechecker_find_import_index(checker, module);
    checker->using_modules[checker->using_module_count++] = module;
}

/* Collect `using` and `import and use` before anything resolves a name.
 * A bare name in a file that used a module has to resolve while types are
 * being registered, which happens before the statement walk that used to be
 * the only place this list was built. Diagnostics stay in that walk. */
static void register_decl_usings(TypeChecker *checker, AstNode *program) {
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind == NODE_USING_STMT) {
            for (int j = 0; j < stmt->data.using_stmt.count; j++)
                using_add(checker, stmt->data.using_stmt.modules[j], stmt->token.file);
        } else if (stmt->kind == NODE_IMPORT_STMT && stmt->data.import_stmt.auto_use) {
            for (int j = 0; j < stmt->data.import_stmt.count; j++)
                using_add(checker, stmt->data.import_stmt.items[j].module, stmt->token.file);
        }
    }
}

static void mark_collided_func_used(TypeChecker *checker, const DeclEntry *entry) {
    if (entry->kind != DECL_FUNC) return;
    if (entry->registry_index < 0 || entry->registry_index >= checker->func_count) return;
    checker->funcs[entry->registry_index].used = true;
}

/* How a declaration is written from outside its module, for a diagnostic. */
static void decl_qualified_spelling(const DeclEntry *entry, char *buf, size_t buflen) {
    if (entry->module_is_entry || !entry->module_name || !*entry->module_name)
        snprintf(buf, buflen, "%s", entry->name);
    else
        snprintf(buf, buflen, "%s.%s", entry->module_name, entry->name);
}

/* Mangling is `<module>_<name>`, so two modules can produce one compiled name
 * — `foo.bar_baz` and `foo_bar.baz` both mangle to `foo_bar_baz`. The mangled
 * index keeps whichever registered first, and the loser's definition would
 * otherwise reach the C compiler as a redefinition. Report it here instead. */
static void check_mangle_collisions(TypeChecker *checker) {
    ModuleTable *table = checker->modules;
    if (!table) return;
    for (int m = 0; m < table->count; m++) {
        ModuleScope *scope = table->modules[m];
        for (int i = 0; i < scope->count; i++) {
            DeclEntry *entry = scope->entries[i];
            if (entry->external || !entry->ast_node) continue;
            char buf[MSG_BUF_SIZE];
            DeclEntry *winner = module_table_find_mangled(table,
                module_mangle_into(entry, buf, sizeof(buf)));
            if (!winner || winner == entry || winner->external || !winner->ast_node) continue;
            char self_name[MSG_BUF_SIZE], other_name[MSG_BUF_SIZE];
            decl_qualified_spelling(entry, self_name, sizeof(self_name));
            decl_qualified_spelling(winner, other_name, sizeof(other_name));
            diagnostic_error_code_formatted_help(checker->diag, "E6009",
                entry->origin_file, entry->ast_node->token.line,
                entry->ast_node->token.column, 0,
                "rename one of the declarations, or rename one of the modules",
                self_name, other_name);
            /* Both declarations share one entry in the function registry, so
             * a call to either marks only one of them used. Silence the
             * unused warning the other would draw — it is wrong, and the
             * collision error above is the thing to act on. */
            mark_collided_func_used(checker, entry);
            mark_collided_func_used(checker, winner);
        }
    }
}

static void register_declarations(TypeChecker *checker, AstNode *program) {
    checker->registering = true;
    register_decl_symbols(checker, program);
    register_decl_usings(checker, program);
    register_decl_imports(checker, program);
    register_decl_aliases(checker, program);
    register_decl_enums(checker, program);
    register_error_code_set(checker, program);
    register_decl_structs(checker, program);
    register_decl_functions(checker, program);
    register_decl_consts(checker, program);
    check_mangle_collisions(checker);

    /* Validate alias targets exist now that structs/enums are registered */
    for (int i = 0; i < checker->type_alias_count; i++) {
        AstNode *decl = checker->type_alias_nodes[i];
        checker->current_check_file = decl->token.file;
        const char *resolved = resolve_type_alias(checker, checker->type_alias_targets[i]);
        /* E4025: a public alias of a private type re-exports it. `private` on
         * the type is enforced at every use site, and a one-line alias in the
         * same file was the way around all of them. */
        if (!decl->data.alias_decl.is_private) {
            DeclEntry *target_entry = private_target_leaf(checker,
                decl->data.alias_decl.target_type);
            if (target_entry) {
                diagnostic_error_code_formatted(checker->diag, "E4025",
                    NODE_FILE(checker, decl), decl->token.line, decl->token.column, 0,
                    decl->data.alias_decl.name, target_entry->name);
            }
        }
        /* Check if the resolved name is a known type */
        GrayType *resolved_type = type_from_name(resolved);
        if (resolved_type->kind == TK_STRUCT) {
            /* Uppercase name — must be a registered struct or enum */
            if (!is_struct_name(checker, resolved) && !is_enum_name(checker, resolved) &&
                strcmp(resolved, "Error") != 0) {
                diagnostic_error_code_formatted(checker->diag, "E3132",
                    NODE_FILE(checker, decl), decl->token.line, decl->token.column, 0,
                    decl->data.alias_decl.target_type);
            }
        }
    }

    checker->registering = false;
}

/* --- qualified-name resolution ----------------------------------------
 *
 * `mod.Name` parses as a member expression and says nothing about what it
 * refers to. This pass resolves each one through the symbol table, once, and
 * leaves the declaration on the node, so the phases after it — the rest of
 * this file and codegen — read the answer instead of re-deriving it from the
 * qualifier's spelling at every site that needs it.
 *
 * Only a qualifier naming a module resolves. Struct instances, struct types
 * and enum types are written against a bare name too, and none of them is a
 * module member: leaving resolved_decl NULL is what says so. The nested
 * `mod.Type.member` spelling resolves through its object, which is itself a
 * `mod.Type`. */

static void resolve_qualified_member(TypeChecker *checker, AstNode *node) {
    if (node->resolved_decl || !checker->modules) return;
    const char *qualifier = ast_member_qualifier(node);
    if (!qualifier) return;
    if (!module_table_find(checker->modules,
                           module_table_resolve_alias(checker->modules, qualifier)))
        return;
    ResolveScope scope = checker_scope(checker);
    ResolveStatus status;
    DeclEntry *entry = module_resolve_qualified(checker->modules, &scope, qualifier,
                                                node->data.member.member, &status);
    if (status == RESOLVE_OK) node->resolved_decl = entry;
}

static void resolve_qualified_walk(TypeChecker *checker, AstNode *node);

static void resolve_qualified_walk_each(TypeChecker *checker, AstNode **nodes, int count) {
    for (int i = 0; i < count; i++) resolve_qualified_walk(checker, nodes[i]);
}

static void resolve_qualified_walk(TypeChecker *checker, AstNode *node) {
    if (!node) return;
    if (node->token.file) checker->current_check_file = node->token.file;

    switch (node->kind) {
    case NODE_MEMBER_EXPR:
        resolve_qualified_walk(checker, node->data.member.object);
        resolve_qualified_member(checker, node);
        break;
    case NODE_INTERPOLATED_STRING:
        resolve_qualified_walk_each(checker, node->data.interpolated_string.parts,
                                    node->data.interpolated_string.part_count);
        break;
    case NODE_ARRAY_VALUE:
        resolve_qualified_walk_each(checker, node->data.array_value.elements,
                                    node->data.array_value.count);
        break;
    case NODE_MAP_VALUE:
        resolve_qualified_walk_each(checker, node->data.map_value.keys,
                                    node->data.map_value.count);
        resolve_qualified_walk_each(checker, node->data.map_value.values,
                                    node->data.map_value.count);
        break;
    case NODE_STRUCT_VALUE:
        resolve_qualified_walk_each(checker, node->data.struct_value.field_values,
                                    node->data.struct_value.count);
        break;
    case NODE_PREFIX_EXPR:
        resolve_qualified_walk(checker, node->data.prefix.right);
        break;
    case NODE_INFIX_EXPR:
        resolve_qualified_walk(checker, node->data.infix.left);
        resolve_qualified_walk(checker, node->data.infix.right);
        break;
    case NODE_POSTFIX_EXPR:
        resolve_qualified_walk(checker, node->data.postfix.left);
        break;
    case NODE_CALL_EXPR:
        resolve_qualified_walk(checker, node->data.call.function);
        resolve_qualified_walk_each(checker, node->data.call.args,
                                    node->data.call.arg_count);
        break;
    case NODE_INDEX_EXPR:
        resolve_qualified_walk(checker, node->data.index_expr.left);
        resolve_qualified_walk(checker, node->data.index_expr.index);
        break;
    case NODE_RANGE_EXPR:
        resolve_qualified_walk(checker, node->data.range_expr.start);
        resolve_qualified_walk(checker, node->data.range_expr.end);
        resolve_qualified_walk(checker, node->data.range_expr.step);
        break;
    case NODE_CAST_EXPR:
        resolve_qualified_walk(checker, node->data.cast.value);
        break;
    case NODE_FUNC_REF:
        resolve_qualified_walk(checker, node->data.func_ref.function);
        break;
    case NODE_VAR_DECL:
        resolve_qualified_walk(checker, node->data.var_decl.value);
        break;
    case NODE_ASSIGN_STMT:
        resolve_qualified_walk(checker, node->data.assign.target);
        resolve_qualified_walk(checker, node->data.assign.value);
        break;
    case NODE_RETURN_STMT:
        resolve_qualified_walk_each(checker, node->data.return_stmt.values,
                                    node->data.return_stmt.count);
        break;
    case NODE_ENSURE_STMT:
        resolve_qualified_walk(checker, node->data.ensure_stmt.expr);
        break;
    case NODE_EXPR_STMT:
        resolve_qualified_walk(checker, node->data.expr_stmt.expr);
        break;
    case NODE_BLOCK_STMT:
        resolve_qualified_walk_each(checker, node->data.block.stmts,
                                    node->data.block.count);
        break;
    case NODE_IF_STMT:
        resolve_qualified_walk(checker, node->data.if_stmt.condition);
        resolve_qualified_walk(checker, node->data.if_stmt.consequence);
        resolve_qualified_walk(checker, node->data.if_stmt.alternative);
        break;
    case NODE_WHEN_STMT:
        resolve_qualified_walk(checker, node->data.when_stmt.value);
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            resolve_qualified_walk_each(checker, node->data.when_stmt.cases[i].values,
                                        node->data.when_stmt.cases[i].value_count);
            resolve_qualified_walk(checker, node->data.when_stmt.cases[i].body);
        }
        resolve_qualified_walk(checker, node->data.when_stmt.default_body);
        break;
    case NODE_FOR_STMT:
        resolve_qualified_walk(checker, node->data.for_stmt.iterable);
        resolve_qualified_walk(checker, node->data.for_stmt.body);
        break;
    case NODE_FOR_EACH_STMT:
        resolve_qualified_walk(checker, node->data.for_each.collection);
        resolve_qualified_walk(checker, node->data.for_each.body);
        break;
    case NODE_WHILE_STMT:
        resolve_qualified_walk(checker, node->data.while_stmt.condition);
        resolve_qualified_walk(checker, node->data.while_stmt.body);
        break;
    case NODE_LOOP_STMT:
        resolve_qualified_walk(checker, node->data.loop_stmt.body);
        break;
    case NODE_FUNC_DECL:
        for (int i = 0; i < node->data.func_decl.param_count; i++)
            resolve_qualified_walk(checker, node->data.func_decl.params[i].default_value);
        resolve_qualified_walk(checker, node->data.func_decl.body);
        break;
    case NODE_STRUCT_DECL:
        for (int i = 0; i < node->data.struct_decl.field_count; i++)
            resolve_qualified_walk(checker, node->data.struct_decl.fields[i].default_value);
        for (int i = 0; i < node->data.struct_decl.func_count; i++)
            resolve_qualified_walk(checker, node->data.struct_decl.funcs[i].func_decl);
        break;
    case NODE_ENUM_DECL:
        for (int i = 0; i < node->data.enum_decl.value_count; i++)
            resolve_qualified_walk(checker, node->data.enum_decl.values[i].value);
        break;
    case NODE_PROGRAM:
        resolve_qualified_walk_each(checker, node->data.program.stmts,
                                    node->data.program.stmt_count);
        break;
    default:
        break;
    }
}

static void resolve_qualified_names(TypeChecker *checker, AstNode *program) {
    const char *saved_file = checker->current_check_file;
    resolve_qualified_walk(checker, program);
    checker->current_check_file = saved_file;
}

/* --- Public API --- */

static void typetable_free(TypeTable *table) {
    if (!table) return;
    free(table->nodes);
    free(table->types);
    free(table);
}

void typechecker_free(TypeChecker *checker) {
    if (!checker) return;

    for (int i = 0; i < checker->func_count; i++) {
        free(checker->funcs[i].instantiations);
        free(checker->funcs[i].instantiation_calls);
    }
    free(checker->funcs);

    free(checker->structs);

    free(checker->enum_names);
    free(checker->enum_display_names);
    free(checker->enum_is_string);
    free(checker->enum_values);
    free(checker->enum_value_counts);
    free(checker->enum_payload_types);
    free(checker->enum_payload_counts);
    free(checker->enum_is_tagged);
    free(checker->enum_is_flags);
    free(checker->enum_is_deprecated);
    free(checker->enum_deprecated_messages);
    free(checker->error_code_enum_names);

    free(checker->imported_modules);
    free(checker->import_files);
    free(checker->import_lines);
    free(checker->import_used);
    free(checker->import_is_stdlib);

    free(checker->using_modules);
    free(checker->using_module_files);
    free(checker->using_module_import_indices);

    free(checker->arenas);

    free(checker->type_alias_names);
    free(checker->type_alias_targets);
    free(checker->type_alias_nodes);

    free(checker->const_int_names);
    free(checker->const_int_values);

    typetable_free(checker->type_table);
    arena_destroy(checker->arena);
    scope_destroy(checker->current_scope);
    type_pool_reset();

    free(checker);
}

TypeChecker *typechecker_create(DiagnosticList *diag, const char *file) {
    /* Build sorted stdlib lookup tables once before any type-check begins. */
    static bool tables_built = false;
    if (!tables_built) {
        for (int i = 0; i < STDLIB_META_N; i++) stdlib_meta_sorted[i] = &stdlib_func_meta[i];
        qsort(stdlib_meta_sorted, STDLIB_META_N, sizeof(const StdlibFuncMeta *), stdlib_meta_compare);
        tables_built = true;
    }

    TypeChecker *checker = xcalloc(1, sizeof(TypeChecker));
    checker->diag = diag;
    checker->file = file;
    checker->current_scope = scope_create(NULL);
    checker->type_table = typetable_create();
    checker->arena = arena_create(64 * 1024);
    checker->modules = module_table_create(checker->arena);
    return checker;
}

void typechecker_add_file_module(TypeChecker *checker, const char *file,
                                 const char *module_name, bool is_entry) {
    module_table_map_file(checker->modules, file, module_name, is_entry);
}

void typechecker_set_test_mode(TypeChecker *checker, bool enabled) {
    checker->test_mode = enabled;
}

void typechecker_add_module_alias(TypeChecker *checker, const char *alias,
                                  const char *module_name) {
    module_table_add_alias(checker->modules, alias, module_name);
}

void typechecker_check(TypeChecker *checker, AstNode *program) {
    if (!program || program->kind != NODE_PROGRAM) return;

    checker->program = program;

    /* Pass 1: register all type/function declarations */
    register_declarations(checker, program);

    /* SourceLocation is always registered: the here() builtin returns it
     * and is available without any import. */
    {
        const char **fnames = arena_alloc(checker->arena, sizeof(const char *) * 3);
        GrayType **ftypes = arena_alloc(checker->arena, sizeof(GrayType *) * 3);
        fnames[0] = "file"; fnames[1] = "line"; fnames[2] = "column";
        ftypes[0] = &TYPE_STRING; ftypes[1] = &TYPE_INT; ftypes[2] = &TYPE_INT;
        register_struct(checker, "SourceLocation", "SourceLocation", fnames, ftypes, 3);
    }

    /* Register stdlib struct types scoped to their module imports */
    if (typechecker_is_imported_module(checker, "server") || typechecker_is_imported_module(checker, "http")) {
        const char **fnames = arena_alloc(checker->arena, sizeof(const char *) * 3);
        GrayType **ftypes = arena_alloc(checker->arena, sizeof(GrayType *) * 3);
        fnames[0] = "status"; fnames[1] = "body"; fnames[2] = "headers";
        ftypes[0] = &TYPE_INT; ftypes[1] = &TYPE_STRING; ftypes[2] = type_from_name("map[string:string]");
        register_struct(checker, "HttpResponse", "HttpResponse", fnames, ftypes, 3);
    }

    if (typechecker_is_imported_module(checker, "server")) {
        const char **fnames = arena_alloc(checker->arena, sizeof(const char *) * 6);
        GrayType **ftypes = arena_alloc(checker->arena, sizeof(GrayType *) * 6);
        fnames[0] = "method"; fnames[1] = "path"; fnames[2] = "body";
        fnames[3] = "query"; fnames[4] = "headers"; fnames[5] = "params";
        ftypes[0] = &TYPE_STRING; ftypes[1] = &TYPE_STRING; ftypes[2] = &TYPE_STRING;
        ftypes[3] = type_from_name("map[string:string]");
        ftypes[4] = type_from_name("map[string:string]");
        ftypes[5] = type_from_name("map[string:string]");
        register_struct(checker, "HttpRequest", "HttpRequest", fnames, ftypes, 6);
    }

    if (typechecker_is_imported_module(checker, "uuid")) {
        const char **fnames = arena_alloc(checker->arena, sizeof(const char *) * 1);
        GrayType **ftypes = arena_alloc(checker->arena, sizeof(GrayType *) * 1);
        fnames[0] = "value";
        ftypes[0] = &TYPE_STRING;
        register_struct(checker, "UUID", "UUID", fnames, ftypes, 1);
    }

    /* Collect 'using' and 'import and use' module names */
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind == NODE_USING_STMT) {
            for (int j = 0; j < stmt->data.using_stmt.count; j++) {
                /* E2010: check that the module was imported BEFORE this using statement.
                 * For using stmts from imported files, just verify the module exists
                 * as an import (line ordering can't be compared across files). */
                const char *umod = stmt->data.using_stmt.modules[j];
                bool imported_before = false;
                const char *using_src = stmt->token.file;
                for (int mi = 0; mi < checker->import_count; mi++) {
                    if (strcmp(checker->imported_modules[mi], umod) != 0) continue;
                    const char *imp_src = checker->import_files[mi];
                    /* Same file: require import line < using line */
                    bool same_file = (!using_src && !imp_src) ||
                        (using_src && imp_src && strcmp(using_src, imp_src) == 0);
                    if (same_file && checker->import_lines[mi] < stmt->token.line) {
                        imported_before = true;
                        break;
                    }
                    /* Cross-file: the using is from an imported file whose own
                     * import was already injected — just check existence */
                    if (!same_file && using_src) {
                        imported_before = true;
                        break;
                    }
                }
                if (!imported_before) {
                    diagnostic_error_code_formatted(checker->diag, "E2010", NODE_FILE(checker, stmt), stmt->token.line, stmt->token.column, 0, umod, umod);
                }
                if (checker->using_module_count >= checker->using_module_cap) {
                    checker->using_module_cap = checker->using_module_cap ? checker->using_module_cap * 2 : 8;
                    checker->using_modules = xrealloc(checker->using_modules,
                        sizeof(const char *) * (size_t)checker->using_module_cap);
                    checker->using_module_files = xrealloc(checker->using_module_files,
                        sizeof(const char *) * (size_t)checker->using_module_cap);
                    checker->using_module_import_indices = xrealloc(checker->using_module_import_indices,
                        sizeof(int) * (size_t)checker->using_module_cap);
                }
                checker->using_module_files[checker->using_module_count] = stmt->token.file;
                {
                    int mi = typechecker_find_import_index(checker, stmt->data.using_stmt.modules[j]);
                    checker->using_module_import_indices[checker->using_module_count] = mi;
                    if (mi >= 0) checker->import_used[mi] = true;
                }
                checker->using_modules[checker->using_module_count++] = stmt->data.using_stmt.modules[j];
            }
        }
        if (stmt->kind == NODE_IMPORT_STMT && stmt->data.import_stmt.auto_use) {
            for (int j = 0; j < stmt->data.import_stmt.count; j++) {
                ImportItem *item = &stmt->data.import_stmt.items[j];
                if (item->module) {
                    if (checker->using_module_count >= checker->using_module_cap) {
                        checker->using_module_cap = checker->using_module_cap ? checker->using_module_cap * 2 : 8;
                        checker->using_modules = xrealloc(checker->using_modules,
                            sizeof(const char *) * (size_t)checker->using_module_cap);
                        checker->using_module_files = xrealloc(checker->using_module_files,
                            sizeof(const char *) * (size_t)checker->using_module_cap);
                        checker->using_module_import_indices = xrealloc(checker->using_module_import_indices,
                            sizeof(int) * (size_t)checker->using_module_cap);
                    }
                    checker->using_module_files[checker->using_module_count] = stmt->token.file;
                    checker->using_module_import_indices[checker->using_module_count] =
                        typechecker_find_import_index(checker, item->module);
                    checker->using_modules[checker->using_module_count++] = item->module;
                }
            }
        }
    }

    /* Register unprefixed aliases for struct/enum types from 'import and use' modules.
     * Only process using-modules declared in the main file — transitive imports
     * should not leak their unprefixed aliases into the main compilation unit.
     * A private declaration is not part of what `using` brings in: an
     * unprefixed alias for one made it reachable by a bare name from a file
     * that cannot name it at all, which is how `private` on a struct or enum
     * stopped meaning anything across a module boundary. */
    for (int using_index = 0; using_index < checker->using_module_count; using_index++) {
        const char *uf = checker->using_module_files ? checker->using_module_files[using_index] : NULL;
        bool is_main = (!uf && !checker->file) || (uf && checker->file && strcmp(uf, checker->file) == 0);
        if (!is_main) continue;
        const char *umod = checker->using_modules[using_index];
        size_t umod_len = strlen(umod);
        char prefix[TYPE_NAME_MAX];
        snprintf(prefix, sizeof(prefix), "%s_", umod);
        size_t prefix_len = umod_len + 1;
        /* Check structs */
        for (int struct_index = 0; struct_index < checker->struct_count; struct_index++) {
            const char *sn = checker->structs[struct_index].struct_name;
            if (strncmp(sn, prefix, prefix_len) == 0) {
                const char *unprefixed = sn + prefix_len;
                if (!is_struct_name(checker, unprefixed)) {
                    int field_count = checker->structs[struct_index].field_count;
                    const char **fn = arena_alloc(checker->arena, sizeof(const char *) * (field_count ? field_count : 1));
                    GrayType **ft = arena_alloc(checker->arena, sizeof(GrayType *) * (field_count ? field_count : 1));
                    memcpy(fn, checker->structs[struct_index].field_names, sizeof(const char *) * field_count);
                    memcpy(ft, checker->structs[struct_index].field_types, sizeof(GrayType *) * field_count);
                    register_struct(checker, unprefixed, unprefixed, fn, ft, field_count);
                    inherit_decl_visibility(checker, sn, unprefixed);
                    checker->structs[checker->struct_count - 1].is_deprecated = checker->structs[struct_index].is_deprecated;
                    checker->structs[checker->struct_count - 1].deprecated_message = checker->structs[struct_index].deprecated_message;
                }
            }
        }
        /* Check enums */
        for (int enum_index = 0; enum_index < checker->enum_count; enum_index++) {
            const char *en = checker->enum_names[enum_index];
            if (strncmp(en, prefix, prefix_len) == 0) {
                const char *unprefixed = en + prefix_len;
                if (!is_enum_name(checker, unprefixed)) {
                    register_enum(checker, unprefixed, unprefixed, checker->enum_is_string[enum_index],
                        checker->enum_values[enum_index], checker->enum_value_counts[enum_index],
                        checker->enum_payload_types[enum_index], checker->enum_payload_counts[enum_index],
                        checker->enum_is_tagged[enum_index], checker->enum_is_flags[enum_index],
                        checker->enum_is_deprecated[enum_index], checker->enum_deprecated_messages[enum_index]);
                    inherit_decl_visibility(checker, en, unprefixed);
                }
            }
        }
        /* Register unprefixed aliases for struct-namespaced functions.
         * e.g., testmod_Hero_attack → Hero_attack so Hero.attack() works unprefixed */
        int current_func_count = checker->func_count;
        for (int field_index = 0; field_index < current_func_count; field_index++) {
            const char *fn = checker->funcs[field_index].name;
            if (strncmp(fn, prefix, prefix_len) == 0) {
                const char *unprefixed = fn + prefix_len;
                /* Only register if it's a struct-namespaced function (StructName_func)
                 * and not already registered */
                const char *inner_us = strchr(unprefixed, '_');
                if (inner_us && unprefixed[0] >= 'A' && unprefixed[0] <= 'Z' &&
                    !find_func(checker, unprefixed)) {
                    register_func(checker, unprefixed,
                        checker->funcs[field_index].param_types,
                        checker->funcs[field_index].param_count,
                        checker->funcs[field_index].return_types,
                        checker->funcs[field_index].return_count);
                    checker->funcs[checker->func_count - 1].is_discard = checker->funcs[field_index].is_discard;
                    checker->funcs[checker->func_count - 1].is_deprecated = checker->funcs[field_index].is_deprecated;
                    checker->funcs[checker->func_count - 1].deprecated_message = checker->funcs[field_index].deprecated_message;
                    checker->funcs[checker->func_count - 1].def_line = 0;
                }
            }
        }
    }

    /* Resolve every `mod.Name` written in the program into the declaration it
     * names, before anything reads one. */
    resolve_qualified_names(checker, program);

    /* E2088: keyword alias consistency (while/as_long_as, do/fn, when+is/switch+case, etc.) */
    check_keyword_alias_consistency(checker, program);

    /* Pass 2: check all statements */
    const char *prev_file = NULL;
    bool seen_file_decl = false;
    for (int i = 0; i < program->data.program.stmt_count; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        checker->current_check_file = stmt->token.file;
        /* Track per-file: reject imports after non-import declarations */
        if (!prev_file || strcmp(stmt->token.file, prev_file) != 0) {
            prev_file = stmt->token.file;
            seen_file_decl = false;
        }
        if (stmt->kind == NODE_IMPORT_STMT) {
            if (seen_file_decl) {
                diagnostic_error_code(checker->diag, "E2036", NODE_FILE(checker, stmt),
                    stmt->token.line, stmt->token.column, 0);
            }
        } else if (stmt->kind != NODE_USING_STMT) {
            seen_file_decl = true;
        }
        check_statement(checker, stmt);
    }

    /* Pass 3: re-check generic function bodies per instantiation
     * ( slice 4). The main pass walked bodies with '?' as
     * TK_UNKNOWN, so operations the concrete type doesn't support
     * (e.g. `a + b` on strings) slipped through. For every recorded
     * instantiation, rebind the parameters to concrete types and
     * re-run check_block on the body. If any new errors fire, emit
     * a companion diagnostic at the originating call site so the
     * user sees "I asked for this specialisation, and here's what
     * broke inside it".
     *
     * The outer loop repeats until no new instantiations are
     * discovered, which handles type parameter forwarding: when
     * wrap(Point) re-checks and its body calls make(T), that
     * registers make(Point) as a new instantiation to process. */
    bool new_instantiations = true;
    int *inst_cursors = xcalloc((size_t)checker->func_count, sizeof(int));
    while (new_instantiations) {
    new_instantiations = false;
    for (int field_index = 0; field_index < checker->func_count; field_index++) {
        FuncSig *fs = &checker->funcs[field_index];
        if (!fs->is_generic || !fs->decl ||
            fs->decl->kind != NODE_FUNC_DECL ||
            fs->instantiation_count == 0) continue;
        if (inst_cursors[field_index] >= fs->instantiation_count) continue;

        AstNode *decl = fs->decl;
        checker->current_check_file = decl->token.file;
        for (int ii = inst_cursors[field_index]; ii < fs->instantiation_count; ii++) {
            new_instantiations = true;
            const char *concrete = fs->instantiations[ii];
            AstNode *call_site = fs->instantiation_calls[ii];

            Scope *inst_scope = scope_create(checker->current_scope);
            Scope *outer_scope = checker->current_scope;
            checker->current_scope = inst_scope;
            checker->func_depth++;

            for (int parameter_index = 0; parameter_index < decl->data.func_decl.param_count; parameter_index++) {
                Param *p = &decl->data.func_decl.params[parameter_index];
                /* Type parameter — not a variable; set binding for body re-check */
                if (p->is_type_param) {
                    checker->type_param_name = p->name;
                    checker->type_param_binding = concrete;
                    continue;
                }
                char *sub = substitute_wildcard(p->type_name, concrete);
                GrayType *pt = sub ? type_from_name(sub) : &TYPE_UNKNOWN;
                scope_define(inst_scope, p->name, pt, p->mutable);
                /* `sub` leaks on purpose; type_from_name stores the
                 * name pointer for array/map kinds and we need it
                 * alive for the duration of the re-check. Compile-
                 * time allocation; short-lived process. */
            }

            GrayType **prev_ret = checker->current_return_types;
            const char **prev_ret_names = checker->current_return_type_names;
            int prev_ret_count = checker->current_return_count;
            bool prev_named = checker->current_has_named_returns;
            const char **prev_return_names = checker->current_return_names;

            int return_count = decl->data.func_decl.return_type_count;
            GrayType **ret_types = NULL;
            const char **ret_names = NULL;
            if (return_count > 0) {
                ret_types = xmalloc(sizeof(GrayType *) * (size_t)return_count);
                ret_names = xmalloc(sizeof(const char *) * (size_t)return_count);
                for (int return_index = 0; return_index < return_count; return_index++) {
                    char *sub = substitute_wildcard(
                        decl->data.func_decl.return_types[return_index], concrete);
                    ret_types[return_index] = sub ? type_from_name(sub) : &TYPE_UNKNOWN;
                    ret_names[return_index] = decl->data.func_decl.return_types[return_index];
                }
            }
            checker->current_return_types = ret_types;
            checker->current_return_type_names = ret_names;
            checker->current_return_count = return_count;
            checker->current_return_names = decl->data.func_decl.return_names;

            /* Detect and define named return variables in instantiation scope */
            checker->current_has_named_returns = false;
            if (decl->data.func_decl.return_names) {
                for (int return_index = 0; return_index < return_count; return_index++) {
                    if (decl->data.func_decl.return_names[return_index]) {
                        checker->current_has_named_returns = true;
                    }
                }
            }

            /* Save/restore current_struct_name so private-access
             * checks inside struct function bodies see the correct
             * owning struct during re-check (fixes spurious E4017). */
            const char *prev_struct = checker->current_struct_name;
            char sname_buf[MSG_BUF_SIZE];
            checker->current_struct_name = NULL;
            const char *underscore = strchr(fs->name, '_');
            if (underscore) {
                size_t prefix_len = (size_t)(underscore - fs->name);
                memcpy(sname_buf, fs->name, prefix_len);
                sname_buf[prefix_len] = '\0';
                if (is_struct_name(checker, sname_buf))
                    checker->current_struct_name = sname_buf;
            }

            int errs_before = diagnostic_error_count(checker->diag);
            checker->suppress_typetable_writes = true;
            if (decl->data.func_decl.body) {
                check_block(checker, decl->data.func_decl.body);
            }
            checker->suppress_typetable_writes = false;
            int errs_after = diagnostic_error_count(checker->diag);

            checker->current_struct_name = prev_struct;
            checker->current_return_types = prev_ret;
            checker->current_return_type_names = prev_ret_names;
            checker->current_return_count = prev_ret_count;
            checker->current_has_named_returns = prev_named;
            checker->current_return_names = prev_return_names;
            checker->type_param_name = NULL;
            checker->type_param_binding = NULL;
            checker->current_scope = outer_scope;
            checker->func_depth--;
            free(ret_types);
            free(ret_names);
            scope_destroy(inst_scope);

            if (errs_after > errs_before && call_site) {
                diagnostic_error_code_formatted(checker->diag, "E3058", NODE_FILE(checker, call_site), call_site->token.line, call_site->token.column, 0, func_display_name(fs), concrete);
            }
        }
        inst_cursors[field_index] = fs->instantiation_count;
    }
    } /* end while (new_instantiations) */
    free(inst_cursors);

    /* Verify main() exists (not required when building a test runner). A
     * #test-attributed main is stripped from a normal build, so it does not
     * count as an entry point here. */
    FuncSig *main_sig = find_func(checker, "main");
    bool main_is_test = main_sig && main_sig->decl &&
                        main_sig->decl->kind == NODE_FUNC_DECL &&
                        main_sig->decl->data.func_decl.is_test;
    if (!checker->test_mode && !checker->main_name_misused && (!main_sig || main_is_test)) {
        /* Point at the last statement or line 1 if empty */
        int err_line = 1;
        if (program->data.program.stmt_count > 0) {
            AstNode *last = program->data.program.stmts[program->data.program.stmt_count - 1];
            if (last) err_line = last->token.line;
        }
        diagnostic_error_message(checker->diag, "E4005",
            "program has no main() function; every program needs 'do main() { }'",
            checker->file, err_line, 1, 0);
    }

    /* Warn about unused imports (only for the main file; imports from
     * sub-files are the sub-file author's responsibility). */
    for (int i = 0; i < checker->import_count; i++) {
        if (checker->import_files[i] && checker->file &&
            strcmp(checker->import_files[i], checker->file) != 0) continue; /* skip sub-file imports */
        if (!checker->import_used[i]) {
            char *msg = NULL;
            msg = typechecker_format(checker,
                "module '%s' is imported but never used; remove the import or use the module",
                checker->imported_modules[i]);
            diagnostic_warning_message(checker->diag, "W1002", msg,
                checker->file, checker->import_lines[i], 1, 0);
        }
    }

    /* Warn about unused functions (skip main and struct-namespaced) */
    for (int i = 0; i < checker->func_count; i++) {
        FuncSig *fs = &checker->funcs[i];
        bool is_test_fn = fs->decl && fs->decl->kind == NODE_FUNC_DECL &&
                          fs->decl->data.func_decl.is_test;
        if (!fs->used && fs->def_line > 0 &&
            strcmp(fs->name, "main") != 0 &&
            !fs->is_private && !is_test_fn &&
            !(fs->name[0] >= 'A' && fs->name[0] <= 'Z' && strchr(fs->name, '_'))) {
            const char *display = func_display_name(fs);
            char *msg = NULL;
            msg = typechecker_format(checker,
                "function '%s' is declared but never called", display);
            /* def_line numbers the file the function was declared in, which
             * is not the entry file when the function came from an import. */
            diagnostic_warning_message(checker->diag, "W1003", msg,
                fs->decl ? NODE_FILE(checker, fs->decl) : checker->file,
                fs->def_line, 1, 0);
        }
    }
}

ModuleTable *typechecker_get_modules(TypeChecker *checker) {
    return checker->modules;
}

TypeTable *typechecker_get_table(TypeChecker *checker) {
    return checker->type_table;
}
