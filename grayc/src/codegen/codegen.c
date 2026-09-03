/*
 * codegen.c — Walks the typed AST and emits equivalent C source code,
 * handling declarations, control flow, stdlib calls, and memory management.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "codegen.h"
#include "../util/constants.h"
#include "../util/platform.h"
#include "../util/xalloc.h"
#include "../util/reserved.h"
#include "../util/error_code_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#ifndef GRAY_VERSION
#define GRAY_VERSION "unknown"
#endif

#define IF_ARENA_SIZE        4096
#define LOOP_ARENA_SIZE      16384
#define FUNC_ARENA_SIZE      65536
#define OUTPUT_BUF_INITIAL   4096
#define MAX_STRUCT_DECLS     256
#define MAX_MEMBER_CHAIN     32
#define VAR_NAME_BUF         64
#define SHORT_VAR_BUF        32
#define CYCLE_GUARD_DEPTH    64

/* Return the C-syntax string for an operator TokenType. Used when emitting
 * the operator literally into C source code. */
static const char *operator_to_c_string(TokenType op) {
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
    case TOK_BIT_AND: return "&";
    case TOK_BIT_OR: return "|";
    case TOK_BIT_XOR: return "^";
    case TOK_BIT_NOT: return "~";
    case TOK_BIT_SHIFT_LEFT: return "<<";
    case TOK_BIT_SHIFT_RIGHT: return ">>";
    case TOK_INCREMENT: return "++";
    case TOK_DECREMENT: return "--";
    case TOK_ASSIGN: return "=";
    case TOK_PLUS_ASSIGN: return "+=";
    case TOK_MINUS_ASSIGN: return "-=";
    case TOK_ASTERISK_ASSIGN: return "*=";
    case TOK_SLASH_ASSIGN: return "/=";
    case TOK_PERCENT_ASSIGN: return "%=";
    case TOK_CARET: return "^";
    default: return "?";
    }
}

/* Forward declarations */
static void emit_statement(CodeGen *codegen, AstNode *node);
static void emit_expression(CodeGen *codegen, AstNode *node);
static void emit_call_expression(CodeGen *codegen, AstNode *node);
static bool codegen_is_enum(CodeGen *codegen, const char *name);
static bool codegen_enum_is_tagged(CodeGen *codegen, const char *name);
static bool codegen_enum_is_error_code(CodeGen *codegen, const char *name);
static int codegen_enum_index(CodeGen *codegen, const char *name);
static void emit_to_string(CodeGen *codegen, AstNode *arg);
static bool emit_narrowing_cast(CodeGen *codegen, const char *target, AstNode *val, int line);
static AstNode *find_struct_declaration(CodeGen *codegen, const char *name);
static const char *codegen_resolve_type(CodeGen *codegen, const char *written);


/* The C name for a declaration node: the mangled name of the symbol-table
 * entry that declared it. The module a declaration belongs to is a property
 * of the declaration, so this needs no name to look up and no file context.
 *
 * `fallback` covers nodes the table holds no entry for — struct functions,
 * which are namespaced under their struct rather than their module, and
 * generic instantiations, which are mangled by binding. */
/* Track which module's file is being emitted. Every node of a declaration
 * carries the same file, so setting this per statement covers the bodies. */
static void codegen_enter_node(CodeGen *codegen, AstNode *node) {
    if (node && node->token.file && codegen->modules) {
        codegen->current_module =
            module_table_module_for_file(codegen->modules, node->token.file);
        codegen->current_file = node->token.file;
    }
}

/* Is `mod` a module this program imported or used? Only the stdlib reaches
 * this now — a user module is answered by the symbol table, which also gives
 * the declaration's name. The two lists differ only in that `using` may hold
 * an alias, so both are consulted. */
static bool codegen_module_imported(CodeGen *codegen, const char *mod) {
    for (int i = 0; i < codegen->using_module_count; i++)
        if (strcmp(codegen->using_modules[i], mod) == 0) return true;
    for (int i = 0; i < codegen->imported_module_count; i++)
        if (strcmp(codegen->imported_modules[i], mod) == 0) return true;
    return false;
}

/* The scope emission resolves names in. */
static ResolveScope codegen_scope(CodeGen *codegen) {
    ResolveScope scope;
    scope.module = codegen->current_module;
    scope.file = codegen->current_file ? codegen->current_file : codegen->file;
    scope.using_modules = codegen->using_modules;
    scope.using_count = codegen->using_module_count;
    return scope;
}

/* The C-visible spelling of a name as written where it appears: bare inside
 * its own module, reachable through a `using`, or qualified. The single point
 * at which a written name becomes the symbol it names. */
static const char *codegen_resolve_type(CodeGen *codegen, const char *written) {
    if (!codegen || !codegen->modules || !written) return written;
    ResolveScope scope = codegen_scope(codegen);
    return module_resolve_type_name(codegen->modules, &scope, written);
}

/* The name a type is written with, for type_of(): every leaf mapped from the
 * module-mangled registry key a cross-module reference resolves to back to the
 * declaration's own name. Mangling is how declarations are keyed across
 * modules and has no business reaching a program's output. Writes into `buf`
 * and returns it. */
static const char *codegen_written_type_name(CodeGen *codegen, const char *name,
                                             char *buf, size_t buflen) {
    if (!name || !*name) {
        snprintf(buf, buflen, "unknown");
        return buf;
    }
    size_t len = strlen(name);
    char inner[MSG_BUF_SIZE];
    if (len > 1 && name[0] == '^') {
        snprintf(buf, buflen, "^%s",
                 codegen_written_type_name(codegen, name + 1, inner, sizeof(inner)));
        return buf;
    }
    if (len > 2 && name[0] == '[' && name[len - 1] == ']') {
        char elem[MSG_BUF_SIZE];
        snprintf(elem, sizeof(elem), "%.*s", (int)(len - 2), name + 1);
        snprintf(buf, buflen, "[%s]",
                 codegen_written_type_name(codegen, elem, inner, sizeof(inner)));
        return buf;
    }
    if (len > 5 && strncmp(name, "map[", 4) == 0 && name[len - 1] == ']') {
        char pair[MSG_BUF_SIZE];
        snprintf(pair, sizeof(pair), "%.*s", (int)(len - 5), name + 4);
        char *colon = strchr(pair, ':');
        if (colon) {
            *colon = '\0';
            char key[MSG_BUF_SIZE];
            codegen_written_type_name(codegen, pair, key, sizeof(key));
            snprintf(buf, buflen, "map[%s:%s]", key,
                     codegen_written_type_name(codegen, colon + 1, inner, sizeof(inner)));
            return buf;
        }
    }
    DeclEntry *entry = codegen->modules
        ? module_table_find_mangled(codegen->modules, name) : NULL;
    snprintf(buf, buflen, "%s", entry && entry->name ? entry->name : name);
    return buf;
}

static const char *codegen_resolve_decl(CodeGen *codegen, const char *written) {
    if (!codegen || !codegen->modules || !written) return written;
    ResolveScope scope = codegen_scope(codegen);
    DeclEntry *entry = module_resolve_written(codegen->modules, &scope, written);
    return entry ? module_mangle(codegen->modules, entry) : written;
}

/* The declaration a reference node names: the type checker's cached answer
 * when it left one, otherwise resolved here. */
static const char *codegen_resolve_ref(CodeGen *codegen, AstNode *node,
                                       const char *written) {
    if (node && node->resolved_decl)
        return module_mangle(codegen->modules, node->resolved_decl);
    return codegen_resolve_decl(codegen, written);
}

static const char *codegen_decl_name(CodeGen *codegen, AstNode *node,
                                     const char *fallback) {
    /* While a generic instantiation is being emitted the caller has already
     * put the per-binding mangled name in hand (and on the node). That
     * mangling is by type argument, not by module, and wins. */
    if (codegen->wildcard_binding) return fallback;
    DeclEntry *entry = module_table_entry_for_node(codegen->modules, node);
    return entry ? module_mangle(codegen->modules, entry) : fallback;
}

/* --- Helpers --- */

static char *normalize_path_separators(const char *path);

static void emit(CodeGen *codegen, const char *text) {
    append_string_to_buffer(&codegen->output, text);
}

static void emit_formatted(CodeGen *codegen, const char *format, ...) {
    /* Fast path: try a stack buffer first. vsnprintf always returns the full
     * needed length even when truncated, so the slow path below can skip the
     * NULL-buffer measure call and write directly in one vsnprintf. */
    char stack_buf[256];
    va_list args;
    va_start(args, format);
    int formatted_len = vsnprintf(stack_buf, sizeof(stack_buf), format, args);
    va_end(args);

    if (formatted_len < 0) return;

    if (formatted_len < (int)sizeof(stack_buf)) {
        append_bytes_to_buffer(&codegen->output, stack_buf, (size_t)formatted_len);
        return;
    }

    /* Slow path: formatted string exceeds stack buffer.
     * formatted_len is already the exact required length — no second measure needed. */
    size_t req = codegen->output.len + (size_t)formatted_len + 1;
    if (req > codegen->output.cap) {
        size_t new_cap = codegen->output.cap * 2;
        if (new_cap < req) new_cap = req;
        codegen->output.data = xrealloc(codegen->output.data, new_cap);
        codegen->output.cap = new_cap;
    }

    va_start(args, format);
    vsnprintf(codegen->output.data + codegen->output.len, (size_t)formatted_len + 1, format, args);
    va_end(args);
    codegen->output.len += (size_t)formatted_len;
}

static void emit_indent(CodeGen *codegen) {
    append_indent_to_buffer(&codegen->output, codegen->indent);
}

/* Internal compiler error; emit a clear message instead of segfaulting.
 * Used when a type lookup unexpectedly returns NULL. */
static void codegen_internal_error(const char *context, const char *file, int line) {
    fflush(stdout);
    fprintf(stderr, "internal compiler error: %s (at %s:%d)\n"
        "This is a bug in the Grayscale compiler. Please report it.\n",
        context, file ? file : "<unknown>", line);
    exit(1);
}

static int keyword_compare(const void *key, const void *element) {
    return strcmp((const char *)key, *(const char *const *)element);
}

/* Check if a name collides with a C keyword — or an identifier the C
 * standard defines as a macro (stdin/stdout/stderr, errno, EOF): those
 * expand to function calls on some libcs (MinGW), so a local variable
 * with that name is a syntax error there. Sorted for bsearch. */
static bool is_c_keyword(const char *name) {
    static const char *keywords[] = {
        "EOF", "NULL", "auto", "bool", "break", "case", "char", "const",
        "continue", "default", "do", "double", "else", "enum", "errno",
        "extern", "false", "float", "for", "goto", "if", "inline", "int",
        "long", "register", "restrict", "return", "short", "signed", "sizeof",
        "static", "stderr", "stdin", "stdout", "struct", "switch",
        "true", "typedef", "union", "unsigned", "void", "volatile", "while"
    };
    return bsearch(name, keywords, sizeof(keywords) / sizeof(keywords[0]),
                   sizeof(keywords[0]), keyword_compare) != NULL;
}

/* Returns the bit-width rank of a sized integer type name.
 * Higher rank = wider type. Used to pick the wider operand in
 * mixed-width arithmetic so bounds checks fire against the right range. */
static int int_type_rank(const char *type_name) {
    if (!type_name) return 0;
    if (strcmp(type_name, "i8")  == 0 || strcmp(type_name, "u8")   == 0 || strcmp(type_name, "byte") == 0) return 1;
    if (strcmp(type_name, "i16") == 0 || strcmp(type_name, "u16")  == 0) return 2;
    if (strcmp(type_name, "i32") == 0 || strcmp(type_name, "u32")  == 0) return 3;
    if (strcmp(type_name, "i64") == 0 || strcmp(type_name, "u64")  == 0 ||
        strcmp(type_name, "int") == 0 || strcmp(type_name, "uint") == 0) return 4;
    if (strcmp(type_name, "i128") == 0 || strcmp(type_name, "u128") == 0) return 5;
    if (strcmp(type_name, "i256") == 0 || strcmp(type_name, "u256") == 0) return 6;
    return 0;
}

/* Look up sized-integer bounds for overflow checking.
 * Returns true if the type is a sized integer, populating the out params.
 * For unsigned types, *is_unsigned is set and *min_out is NULL. */
static bool sized_int_bounds(const char *type_name,
                             const char **min_out, const char **max_out,
                             bool *is_unsigned) {
    *min_out = NULL; *max_out = NULL; *is_unsigned = false;
    if (!type_name) return false;
    if (strcmp(type_name, "i8") == 0)  { *min_out = "-128"; *max_out = "127"; return true; }
    if (strcmp(type_name, "i16") == 0) { *min_out = "-32768"; *max_out = "32767"; return true; }
    if (strcmp(type_name, "i32") == 0) { *min_out = "-2147483648LL"; *max_out = "2147483647LL"; return true; }
    if (strcmp(type_name, "u8") == 0 || strcmp(type_name, "byte") == 0)  { *is_unsigned = true; *max_out = "255"; return true; }
    if (strcmp(type_name, "u16") == 0) { *is_unsigned = true; *max_out = "65535"; return true; }
    if (strcmp(type_name, "u32") == 0) { *is_unsigned = true; *max_out = "4294967295ULL"; return true; }
    return false;
}

/* Return the runtime overflow-check function name for a compound assignment
 * operator on a sized integer type, or NULL if not applicable. */
static const char *sized_check_func(TokenType op, bool is_unsigned) {
    if (is_unsigned) {
        if (op == TOK_PLUS_ASSIGN)     return "gray_usized_add_check";
        if (op == TOK_MINUS_ASSIGN)    return "gray_usized_sub_check";
        if (op == TOK_ASTERISK_ASSIGN) return "gray_usized_mul_check";
    } else {
        if (op == TOK_PLUS_ASSIGN)     return "gray_sized_add_check";
        if (op == TOK_MINUS_ASSIGN)    return "gray_sized_sub_check";
        if (op == TOK_ASTERISK_ASSIGN) return "gray_sized_mul_check";
    }
    return NULL;
}

/* Emit an overflow-checked compound assignment for a pointer-based target
 * whose C reference string is ref_str (e.g. "*_dp", "_dp->field").
 * Handles +=, -=, *= on sized and plain integer types.
 * Returns true if a checked form was emitted, false otherwise. */
static bool emit_checked_ptr_compound(CodeGen *codegen, AstNode *node,
                                      const char *ref_str) {
    TokenType aop = node->data.assign.op;
    if (aop != TOK_PLUS_ASSIGN && aop != TOK_MINUS_ASSIGN &&
        aop != TOK_ASTERISK_ASSIGN)
        return false;

    GrayType *tgt_t = codegen->type_table
        ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
    if (!tgt_t) return false;

    /* String append: s += t → s = gray_string_concat(arena, s, t). */
    if (aop == TOK_PLUS_ASSIGN && tgt_t->kind == TK_STRING) {
        emit_formatted(codegen, "%s = gray_string_concat(gray_default_arena, %s, ", ref_str, ref_str);
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, ")");
        return true;
    }

    const char *type_name_str = tgt_t->name;

    /* Sized integers (i8/i16/i32/u8/u16/u32): gray_(u)sized_*_check */
    const char *smin = NULL, *smax = NULL;
    bool is_unsigned = false;
    if (type_name_str) sized_int_bounds(type_name_str, &smin, &smax, &is_unsigned);
    if (smax) {
        const char *check_func_name = sized_check_func(aop, is_unsigned);
        if (!check_func_name) return false;
        emit_formatted(codegen, "%s = %s(%s, ", ref_str, check_func_name, ref_str);
        emit_expression(codegen, node->data.assign.value);
        if (is_unsigned)
            emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d)",
                           smax, type_name_str, codegen->file, node->token.line);
        else
            emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d)",
                           smin, smax, type_name_str, codegen->file, node->token.line);
        return true;
    }

    /* Plain int/uint (i64/u64): gray_(u)*_check */
    bool tgt_is_int = (tgt_t->kind == TK_INT || tgt_t->kind == TK_UINT ||
                       tgt_t->kind == TK_BYTE);
    if (!tgt_is_int) return false;

    bool unsigned_op = (tgt_t->kind == TK_UINT || tgt_t->kind == TK_BYTE);
    const char *check_func_name = NULL;
    if (unsigned_op) {
        if (aop == TOK_PLUS_ASSIGN) check_func_name = "gray_uadd_check";
        else if (aop == TOK_MINUS_ASSIGN) check_func_name = "gray_usub_check";
        else if (aop == TOK_ASTERISK_ASSIGN) check_func_name = "gray_umul_check";
    } else {
        if (aop == TOK_PLUS_ASSIGN) check_func_name = "gray_add_check";
        else if (aop == TOK_MINUS_ASSIGN) check_func_name = "gray_sub_check";
        else if (aop == TOK_ASTERISK_ASSIGN) check_func_name = "gray_mul_check";
    }
    if (!check_func_name) return false;

    emit_formatted(codegen, "%s = %s(%s, ", ref_str, check_func_name, ref_str);
    emit_expression(codegen, node->data.assign.value);
    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
    return true;
}

static const char *sanitize_name(const char *name) {
    if (!name || !is_c_keyword(name)) return name;
    static char bufs[4][MSG_BUF_SIZE];
    static int slot_index = 0;
    int i = slot_index++ & 3;
    snprintf(bufs[i], sizeof(bufs[i]), "_gray_%s", name);
    return bufs[i];
}

/* Build a mangled name for a generic instantiation: `base__concrete`
 * with non-alphanumeric characters replaced by underscores so
 * array/map bindings stay legal C identifiers. */
static void mangle_generic_name(char *buf, size_t buf_size, const char *base, const char *concrete) {
    size_t pos = (size_t)snprintf(buf, buf_size, "%s__", base);
    for (const char *ch = concrete; *ch && pos < buf_size - 1; ch++) {
        buf[pos++] = (isalnum((unsigned char)*ch) || *ch == '_') ? *ch : '_';
    }
    buf[pos] = '\0';
}

/* Returns true if the function declaration contains any wildcard ('?')
 * type parameters or return types, indicating a generic function. */
static bool func_is_generic(AstNode *func) {
    for (int i = 0; i < func->data.func_decl.param_count; i++) {
        if (func->data.func_decl.params[i].type_name &&
            strchr(func->data.func_decl.params[i].type_name, '?')) return true;
    }
    for (int i = 0; i < func->data.func_decl.return_type_count; i++) {
        if (func->data.func_decl.return_types[i] &&
            strchr(func->data.func_decl.return_types[i], '?')) return true;
    }
    return false;
}

/* Map Grayscale type name to C type */
/* Return a type string with any '?' replaced by the active wildcard
 * binding. Returns the original pointer if no binding is active or the
 * string has no wildcard. The substituted string lives in a small ring
 * of static buffers so a handful of nested calls can each keep their
 * result alive simultaneously. */
static const char *multi_return_base_name(const char *fn_name);
static const char *multi_return_name(AstNode *func);
static const char *codegen_effective_type_string(CodeGen *codegen, const char *type_name) {
    if (!type_name || !codegen || !codegen->wildcard_binding) return type_name;
    if (!strchr(type_name, '?')) return type_name;
    size_t binding_len = strlen(codegen->wildcard_binding);
    static char bufs[4][TYPE_NAME_MAX];
    static int slot = 0;
    char *out = bufs[slot];
    slot = (slot + 1) & 3;
    char *write_ptr = out;
    char *end = out + TYPE_NAME_MAX - 1;
    for (const char *read_ptr = type_name; *read_ptr && write_ptr < end; read_ptr++) {
        if (*read_ptr == '?') {
            size_t avail = (size_t)(end - write_ptr);
            size_t copy = binding_len < avail ? binding_len : avail;
            memcpy(write_ptr, codegen->wildcard_binding, copy);
            write_ptr += copy;
        } else {
            *write_ptr++ = *read_ptr;
        }
    }
    *write_ptr = '\0';
    return out;
}

/* Recursively derive what '?' binds to given a param type pattern (ptn)
 * containing '?' and a concrete argument type name (atn).
 * Returns malloc'd binding string (caller must free) or NULL on mismatch. */
static char *codegen_bind_wildcard(const char *ptn, const char *atn) {
    if (!ptn || !atn || !strchr(ptn, '?')) return NULL;
    if (strcmp(ptn, "?") == 0) return strdup(atn);
    size_t plen = strlen(ptn);
    size_t alen = strlen(atn);
    /* Array layer: strip matching outer [...] brackets */
    if (plen >= 3 && ptn[0] == '[' && ptn[plen - 1] == ']') {
        if (alen < 3 || atn[0] != '[' || atn[alen - 1] != ']') return NULL;
        char *array_pattern_inner = gray_strndup(ptn + 1, plen - 2);
        char *array_arg_inner = gray_strndup(atn + 1, alen - 2);
        char *result = codegen_bind_wildcard(array_pattern_inner, array_arg_inner);
        free(array_pattern_inner); free(array_arg_inner);
        return result;
    }
    /* Map layer: find top-level ':' in both sides and recurse into wildcard slot */
    if (plen > 4 && strncmp(ptn, "map[", 4) == 0 && ptn[plen - 1] == ']') {
        if (alen <= 4 || strncmp(atn, "map[", 4) != 0 || atn[alen - 1] != ']') return NULL;
        const char *map_pattern_content = ptn + 4; size_t map_pattern_content_len = plen - 5;
        const char *map_arg_content = atn + 4; size_t map_arg_content_len = alen - 5;
        int depth = 0; const char *map_pattern_colon = NULL, *map_arg_colon = NULL;
        for (size_t i = 0; i < map_pattern_content_len; i++) {
            if (map_pattern_content[i] == '[') depth++; else if (map_pattern_content[i] == ']') depth--;
            else if (map_pattern_content[i] == ':' && depth == 0) { map_pattern_colon = map_pattern_content + i; break; }
        }
        depth = 0;
        for (size_t i = 0; i < map_arg_content_len; i++) {
            if (map_arg_content[i] == '[') depth++; else if (map_arg_content[i] == ']') depth--;
            else if (map_arg_content[i] == ':' && depth == 0) { map_arg_colon = map_arg_content + i; break; }
        }
        if (!map_pattern_colon || !map_arg_colon) return NULL;
        char *map_pattern_key = gray_strndup(map_pattern_content, (size_t)(map_pattern_colon - map_pattern_content));
        char *map_pattern_value = gray_strndup(map_pattern_colon + 1, map_pattern_content_len - (size_t)(map_pattern_colon - map_pattern_content) - 1);
        char *map_arg_key = gray_strndup(map_arg_content, (size_t)(map_arg_colon - map_arg_content));
        char *map_arg_value = gray_strndup(map_arg_colon + 1, map_arg_content_len - (size_t)(map_arg_colon - map_arg_content) - 1);
        char *result = NULL;
        if (strchr(map_pattern_key, '?')) result = codegen_bind_wildcard(map_pattern_key, map_arg_key);
        if (!result && strchr(map_pattern_value, '?')) result = codegen_bind_wildcard(map_pattern_value, map_arg_value);
        free(map_pattern_key); free(map_pattern_value); free(map_arg_key); free(map_arg_value);
        return result;
    }
    return NULL;
}

/* Resolve type alias name to underlying type (codegen side).
 * Handles pointer (^Alias) and array ([Alias]) wrappers. */
static const char *resolve_type_alias_codegen(CodeGen *codegen, const char *name) {
    if (!name) return name;

    /* Handle pointer types: ^Alias → ^Resolved */
    if (name[0] == '^') {
        const char *inner = resolve_type_alias_codegen(codegen, name + 1);
        if (inner != name + 1) {
            size_t len = strlen(inner) + 2;
            char *buf = xmalloc(len);
            snprintf(buf, len, "^%s", inner);
            return buf;
        }
        return name;
    }

    for (int depth = 0; depth < 32; depth++) {
        bool found = false;
        for (int i = 0; i < codegen->type_alias_count; i++) {
            if (strcmp(codegen->type_alias_names[i], name) == 0) {
                name = codegen->type_alias_targets[i];
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    return name;
}

static const char *gray_type_to_c_codegen(CodeGen *codegen, const char *type_name) {
    if (!type_name) return "int64_t";

    /* Resolve type aliases before any type mapping */
    if (codegen) {
        /* Resolve the name as written to its module's spelling first — the
         * alias registry is keyed that way, so `Score` inside lib and
         * `lib.Score` outside it both find the same alias. */
        type_name = codegen_resolve_type(codegen, type_name);
        type_name = resolve_type_alias_codegen(codegen, type_name);
    }

    /* if Wildcard type'?' appears in the type
     * string while a generic instantiation is active, rewrite via
     * codegen_effective_type_string and recurse through the normal mapping. */
    if (codegen && codegen->wildcard_binding && strchr(type_name, '?')) {
        const char *sub = codegen_effective_type_string(codegen, type_name);
        const char *saved = codegen->wildcard_binding;
        codegen->wildcard_binding = NULL;
        const char *resolved = gray_type_to_c_codegen(codegen, sub);
        codegen->wildcard_binding = saved;
        return resolved;
    }

    if (strcmp(type_name, "int") == 0)    return "int64_t";
    if (strcmp(type_name, "uint") == 0)   return "uint64_t";
    if (strcmp(type_name, "i8") == 0)     return "int8_t";
    if (strcmp(type_name, "i16") == 0)    return "int16_t";
    if (strcmp(type_name, "i32") == 0)    return "int32_t";
    if (strcmp(type_name, "i64") == 0)    return "int64_t";
    if (strcmp(type_name, "u8") == 0)     return "uint8_t";
    if (strcmp(type_name, "u16") == 0)    return "uint16_t";
    if (strcmp(type_name, "u32") == 0)    return "uint32_t";
    if (strcmp(type_name, "u64") == 0)    return "uint64_t";
    if (strcmp(type_name, "i128") == 0)   return "gray_i128";
    if (strcmp(type_name, "u128") == 0)   return "gray_u128";
    if (strcmp(type_name, "i256") == 0)   return "gray_i256";
    if (strcmp(type_name, "u256") == 0)   return "gray_u256";
    if (strcmp(type_name, "float") == 0)  return "double";
    if (strcmp(type_name, "f32") == 0)    return "float";
    if (strcmp(type_name, "f64") == 0)    return "double";
    if (strcmp(type_name, "bool") == 0)   return "bool";
    if (strcmp(type_name, "char") == 0)   return "int32_t";
    if (strcmp(type_name, "byte") == 0)   return "uint8_t";
    if (strcmp(type_name, "string") == 0) return "GrayString";
    if (strcmp(type_name, "Error") == 0 || strcmp(type_name, "error") == 0) return "GrayError *";
    if (strcmp(type_name, "ErrorCode") == 0) return "GrayErrorCode";
    if (strcmp(type_name, "OpenFlag") == 0) return "GrayEnum_OpenFlag";
    if (strcmp(type_name, "Platform") == 0) return "GrayEnum_Platform";
    /* A user struct/enum that shadows a stdlib opaque type name (Database,
     * Router, Thread, ...) resolves to its own GrayStruct_/GrayEnum_ name, so
     * every emit site agrees. The typechecker (E3099) still blocks declaring
     * one of these names while its owning module is imported. */
    if (codegen && type_name[0] >= 'A' && type_name[0] <= 'Z' &&
        is_reserved_stdlib_struct_name(type_name)) {
        static char user_opaque_buf[MSG_BUF_SIZE];
        if (find_struct_declaration(codegen, type_name)) {
            snprintf(user_opaque_buf, sizeof(user_opaque_buf), "GrayStruct_%s", type_name);
            return user_opaque_buf;
        }
        if (codegen_is_enum(codegen, type_name)) {
            snprintf(user_opaque_buf, sizeof(user_opaque_buf), "GrayEnum_%s", type_name);
            return user_opaque_buf;
        }
    }
    if (strcmp(type_name, "HttpRequest") == 0) return "GrayRequest";
    if (strcmp(type_name, "HttpResponse") == 0) return "GrayResponse";
    /* Stdlib opaque types: scalar path uses __auto_type and never reaches
     * this resolver, but [T] / map[_:T] / struct fields write the type
     * name explicitly and need it mapped here. Without this, the fallback
     * below produces GrayStruct_<Name>, which no header defines, and clang
     * fails on the generated C. */
    if (strcmp(type_name, "Thread") == 0)   return "GrayThread";
    if (strcmp(type_name, "Mutex") == 0)    return "GrayMutex";
    if (strcmp(type_name, "SpinLock") == 0) return "GraySpinLock";
    if (strcmp(type_name, "Channel") == 0)  return "GrayChannel";
    if (strcmp(type_name, "Socket") == 0)   return "GraySocket";
    if (strcmp(type_name, "Listener") == 0) return "GraySocket";
    if (strcmp(type_name, "Database") == 0) return "GraySqlite";
    if (strcmp(type_name, "Router") == 0)   return "GrayRouter";
    if (strcmp(type_name, "UUID") == 0)     return "GrayUUID";
    if (strcmp(type_name, "Arena") == 0)    return "GrayArena *";
    if (strcmp(type_name, "Builder") == 0)  return "GrayStringsBuilder *";
    if (strcmp(type_name, "func") == 0)  return "void *"; /* bare func; cast at call site */
    if (strncmp(type_name, "func(", 5) == 0) return "void *"; /* typed func; same C storage, signature lives in casts */


    /* Pointer type: ^T; use C pointer (ring buffer avoids aliasing on recursion) */
    if (type_name[0] == '^') {
        static char ptrbufs[4][MSG_BUF_SIZE];
        static int ptridx = 0;
        char *buffer = ptrbufs[ptridx++ & 3];
        const char *pointee = gray_type_to_c_codegen(codegen, type_name + 1);
        snprintf(buffer, MSG_BUF_SIZE, "%s *", pointee);
        return buffer;
    }

    /* Array type: [T]; use GrayArray */
    if (type_name[0] == '[') {
        return "GrayArray";
    }

    /* Map type: map[K:V]; use GrayMap */
    if (strncmp(type_name, "map[", 4) == 0) {
        return "GrayMap";
    }

    /* Qualified type name: module.Type. The qualifier used to be stripped,
     * naming the type without saying whose it was; it resolves now — so the
     * split only has to say that the spelling *is* qualified, and hand back
     * the bare half for the unresolvable case. */
    const char *type_qualifier = NULL, *bare_type = NULL;
    (void)type_qualifier;
    if (codegen && codegen->modules &&
        module_split_qualified(codegen->modules->arena, type_name,
                               &type_qualifier, &bare_type)) {
        const char *resolved = codegen_resolve_type(codegen, type_name);
        if (resolved == type_name) {
            /* Unresolvable qualified name: fall back to the bare half so a
             * type the table has not been told about still maps. */
            return gray_type_to_c_codegen(codegen, bare_type);
        }
        /* An alias is erased: once resolved it may name a type alias, whose
         * underlying type is what C sees. */
        const char *unaliased = resolve_type_alias_codegen(codegen, resolved);
        if (unaliased != resolved && strcmp(unaliased, type_name) != 0)
            return gray_type_to_c_codegen(codegen, unaliased);
        static char buffer[MSG_BUF_SIZE];
        if (codegen && codegen_is_enum(codegen, resolved)) {
            snprintf(buffer, sizeof(buffer), "GrayEnum_%s", resolved);
        } else {
            snprintf(buffer, sizeof(buffer), "GrayStruct_%s", resolved);
        }
        return buffer;
    }

    /* If starts with uppercase, it's a user-defined type */
    /* Also handle module-prefixed types: lib_Point, mod_Color */
    bool is_user_type = (type_name[0] >= 'A' && type_name[0] <= 'Z');
    /* A registered declaration is a user type whatever its mangled spelling
     * looks like — the guess below splits at the first '_' and loses the
     * type when the module's own name contains one (foo_bar_Color). */
    if (!is_user_type && codegen)
        is_user_type = find_struct_declaration(codegen, type_name) != NULL ||
                       codegen_is_enum(codegen, type_name);
    if (!is_user_type) {
        const char *us = strrchr(type_name, '_');
        if (us && us[1] >= 'A' && us[1] <= 'Z') is_user_type = true;
    }
    if (is_user_type) {
        static char buffer[MSG_BUF_SIZE];
        const char *resolved = type_name;
        if (codegen && type_name[0] >= 'A' && type_name[0] <= 'Z' && !strchr(type_name, '_')) {
            resolved = codegen_resolve_type(codegen, type_name);
            /* Names the table does not hold — stdlib opaque types — keep the
             * using-module search. */
        }
        /* Module-qualified opaque types: mod_Type -> strip prefix and
         * re-resolve so opaque mappings (Channel->GrayChannel etc.) apply.
         * Skip for known user-defined struct/enum declarations: stripping
         * the module prefix would lose the correct qualification and could
         * resolve to the wrong type when multiple modules define types
         * with the same base name (e.g. geo_Point vs color_Point).
         * Also guard against infinite recursion when the stripped base
         * equals the original type_name. */
        const char *mod_us = strrchr(resolved, '_');
        if (mod_us && mod_us[1] >= 'A' && mod_us[1] <= 'Z') {
            bool is_known_decl = codegen &&
                (find_struct_declaration(codegen, resolved) != NULL ||
                 codegen_is_enum(codegen, resolved));
            if (!is_known_decl) {
                const char *base = mod_us + 1;
                if (strcmp(base, type_name) != 0) {
                    const char *mapped = gray_type_to_c_codegen(codegen, base);
                    if (mapped != base) return mapped;
                }
            }
        }
        if (codegen && codegen_is_enum(codegen, resolved)) {
            snprintf(buffer, sizeof(buffer), "GrayEnum_%s", resolved);
        } else {
            snprintf(buffer, sizeof(buffer), "GrayStruct_%s", resolved);
        }
        return buffer;
    }

    return type_name;
}

static const char *bigint_prefix(const char *type_str);

/* Resolve a Grayscale type to its C type for map key/value storage.
 * Uses gray_type_to_c_codegen for struct/array/map types, hardcoded for primitives.
 * Routes the input through codegen_effective_type_string so '?' inside a generic
 * instantiation resolves to the active wildcard binding. */
static const char *gray_map_element_c_type(CodeGen *codegen, const char *gray_tn) {
    if (!gray_tn) return "int64_t";
    gray_tn = codegen_effective_type_string(codegen, gray_tn);
    /* Func references (bare or typed) are stored as void * in maps, same as
     * in arrays and all other composite types. */
    if (strcmp(gray_tn, "func") == 0 || strncmp(gray_tn, "func(", 5) == 0) return "void *";
    /* A tagged enum is a C struct, not an integer; storing one in a map has
     * to use its real type so the element size and the casts on read match. */
    if (codegen && codegen_is_enum(codegen, gray_tn) &&
        codegen_enum_is_tagged(codegen, gray_tn))
        return gray_type_to_c_codegen(codegen, gray_tn);
    /* Wide integers are TK_INT/TK_UINT in the type system but 16/32-byte
     * structs in C; a map slot must use the struct type so its size and the
     * casts on read match, just like a [i128] array element does. */
    if (is_bigint_type(gray_tn)) return bigint_prefix(gray_tn);
    GrayType *type = type_from_name(gray_tn);
    if (!type) return "int64_t";
    switch (type->kind) {
    /* Honor the annotated width/signedness for sized ints (i8..u64, uint)
     * and f32/f64, just like a scalar or struct field — the slot must match
     * so its size and the casts on read agree. gray_type_to_c_codegen maps
     * int->int64_t and float->double, so the plain forms are unaffected. */
    case TK_FLOAT:   return gray_type_to_c_codegen(codegen, gray_tn);
    case TK_INT:     return gray_type_to_c_codegen(codegen, gray_tn);
    case TK_UINT:    return gray_type_to_c_codegen(codegen, gray_tn);
    case TK_STRING:  return "GrayString";
    case TK_BOOL:    return "bool";
    case TK_CHAR:    return "int32_t";
    case TK_BYTE:    return "uint8_t";
    case TK_ARRAY:   return "GrayArray";
    case TK_MAP:     return "GrayMap";
    case TK_STRUCT:  return gray_type_to_c_codegen(codegen, gray_tn);
    case TK_POINTER: return gray_type_to_c_codegen(codegen, gray_tn);
    default:         return "int64_t";
    }
}

/* Map a C key type back to its GrayMap key-kind macro so the codegen can
 * tag each gray_map_new_kind call. Float keys need this so -0.0/+0.0 and
 * NaN are normalized at lookup time; other 8-byte keys (int, pointer)
 * stay on the bytewise path. */
static const char *gray_map_key_kind_macro(const char *c_key_type) {
    if (!c_key_type) return "GRAY_MAP_KEY_BYTES";
    if (strcmp(c_key_type, "GrayString") == 0) return "GRAY_MAP_KEY_STRING";
    if (strcmp(c_key_type, "double") == 0)   return "GRAY_MAP_KEY_F64";
    if (strcmp(c_key_type, "float") == 0)    return "GRAY_MAP_KEY_F32";
    return "GRAY_MAP_KEY_BYTES";
}

/* --- Deep copy machinery , ) ---
 *
 * A value is "needs-deep-copy" iff reading one C-level copy of it
 * would share mutable backing storage with the source. That covers
 * arrays (GrayArray header aliases data), maps (GrayMap header aliases
 * keys/values/states/order), and any struct that transitively holds a
 * field of either. Pointers are deliberately left to alias; following
 * the pointee would surprise users, loop on cycles, and doesn't match
 * how any real language treats pointer copy.
 *
 * Three mutually-recursive emitters handle each collection kind, and
 * emit_value_deep_copy dispatches based on the Grayscale type string. All of
 * them take a `src_var` naming a C assignable variable holding the source value,
 * and emit a single C expression (usually a GCC statement expression)
 * that evaluates to a fully independent copy. */

static AstNode *find_struct_declaration(CodeGen *codegen, const char *name);

/* Cycle guard for type_needs_deep_copy: tracks struct names currently being
 * visited so circular references (A -> [B] -> B -> A) don't cause infinite
 * recursion and a stack-overflow crash. */
static const char *type_name_deep_copy_visiting[CYCLE_GUARD_DEPTH];
static int type_name_deep_copy_depth = 0;

static bool type_needs_deep_copy(CodeGen *codegen, const char *gray_tn) {
    if (!gray_tn || !*gray_tn) return false;
    if (gray_tn[0] == '[') return true;
    if (strncmp(gray_tn, "map[", 4) == 0) return true;
    if (strcmp(gray_tn, "string") == 0) return true;
    if (gray_tn[0] == '^') return false; /* pointers alias; see header comment */
    AstNode *sdecl = find_struct_declaration(codegen, gray_tn);
    if (!sdecl) return false;
    /* Cycle detection: if we're already visiting this struct, stop. */
    for (int j = 0; j < type_name_deep_copy_depth; j++) {
        if (strcmp(type_name_deep_copy_visiting[j], gray_tn) == 0) return false;
    }
    if (type_name_deep_copy_depth < CYCLE_GUARD_DEPTH) type_name_deep_copy_visiting[type_name_deep_copy_depth++] = gray_tn;
    for (int i = 0; i < sdecl->data.struct_decl.field_count; i++) {
        const char *field_type = sdecl->data.struct_decl.fields[i].type_name;
        if (type_needs_deep_copy(codegen, field_type)) { type_name_deep_copy_depth--; return true; }
    }
    type_name_deep_copy_depth--;
    return false;
}

/* Return a unique integer for temporary variable names, drawn from the
 * codegen-wide counter so every emitter gets a distinct id. */
static int codegen_next_id(CodeGen *codegen) { return codegen->temp_counter++; }

static void emit_value_deep_copy(CodeGen *codegen, const char *gray_tn, const char *src_var);

static void emit_array_deep_copy(CodeGen *codegen, const char *gray_tn, const char *src_var) {
    size_t len = gray_tn ? strlen(gray_tn) : 0;
    if (len < 3 || gray_tn[0] != '[' || gray_tn[len - 1] != ']') {
        emit_formatted(codegen, "gray_array_copy(gray_default_arena, &%s)", src_var);
        return;
    }

    /* Extract element type name from "[T]" (dropping any ",N" sized tail). */
    char elem_tn[MSG_BUF_SIZE];
    size_t elen = len - 2;
    if (elen >= sizeof(elem_tn)) elen = sizeof(elem_tn) - 1;
    memcpy(elem_tn, gray_tn + 1, elen);
    elem_tn[elen] = '\0';
    char *comma = strchr(elem_tn, ',');
    if (comma) *comma = '\0';

    if (!type_needs_deep_copy(codegen, elem_tn)) {
        /* Flat element type; the shallow bulk memcpy in gray_array_copy
         * is already correct. */
        emit_formatted(codegen, "gray_array_copy(gray_default_arena, &%s)", src_var);
        return;
    }

    /* Element needs its own deep copy. Allocate a fresh outer and walk
     * each slot, recursively deep-copying the element in place. */
    /* Snapshot c_elem into a local buffer. gray_type_to_c_codegen returns a
     * pointer into a shared static buffer; the recursive
     * emit_value_deep_copy call below also resolves type names (when
     * the element is a struct with a nested struct field) and would
     * clobber that buffer, leaving c_elem pointing at the inner field's
     * C type by the time we emit the outer cast. */
    char c_elem_buf[MSG_BUF_SIZE];
    {
        const char *c_elem_ptr = gray_type_to_c_codegen(codegen, elem_tn);
        snprintf(c_elem_buf, sizeof(c_elem_buf), "%s", c_elem_ptr ? c_elem_ptr : "");
    }
    const char *c_elem = c_elem_buf;
    int tag = codegen_next_id(codegen);
    emit_formatted(codegen,
        "({ GrayArray _ds%d = %s; "
        "GrayArray _dd%d = gray_array_new(gray_default_arena, sizeof(%s), _ds%d.len); "
        "_dd%d.len = _ds%d.len; "
        "for (int32_t _di%d = 0; _di%d < _ds%d.len; _di%d++) { "
        "%s _de%d = ",
        tag, src_var,
        tag, c_elem, tag,
        tag, tag,
        tag, tag, tag, tag,
        c_elem, tag);

    char inner_var[MSG_BUF_SIZE];
    snprintf(inner_var, sizeof(inner_var),
        "((%s *)_ds%d.data)[_di%d]", c_elem, tag, tag);
    emit_value_deep_copy(codegen, elem_tn, inner_var);

    emit_formatted(codegen,
        "; ((%s *)_dd%d.data)[_di%d] = _de%d; "
        "} _dd%d; })",
        c_elem, tag, tag, tag, tag);
}

static void emit_map_deep_copy(CodeGen *codegen, const char *gray_tn, const char *src_var) {
    /* Parse "map[K:V]" into its two slots. */
    if (!gray_tn || strncmp(gray_tn, "map[", 4) != 0) {
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", src_var);
        return;
    }
    size_t len = strlen(gray_tn);
    if (len < 7 || gray_tn[len - 1] != ']') {
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", src_var);
        return;
    }
    const char *start = gray_tn + 4;
    const char *colon = strchr(start, ':');
    if (!colon) {
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", src_var);
        return;
    }
    char key_tn[TYPE_NAME_MAX];
    char val_tn[MSG_BUF_SIZE];
    size_t klen = (size_t)(colon - start);
    if (klen >= sizeof(key_tn)) klen = sizeof(key_tn) - 1;
    memcpy(key_tn, start, klen);
    key_tn[klen] = '\0';
    size_t vlen = len - 4 - klen - 1 - 1; /* drop "map[", K, ":", "]" */
    if (vlen >= sizeof(val_tn)) vlen = sizeof(val_tn) - 1;
    memcpy(val_tn, colon + 1, vlen);
    val_tn[vlen] = '\0';

    if (!type_needs_deep_copy(codegen, val_tn)) {
        /* Value type is flat; gray_map_copy handles string key deep-copy
         * internally when key_kind == GRAY_MAP_KEY_STRING. */
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", src_var);
        return;
    }

    /* Value type needs recursion. Iterate the source in insertion order,
     * deep-copy each value, and insert into a fresh map. */
    const char *c_key = gray_map_element_c_type(codegen, key_tn);
    const char *c_val = gray_map_element_c_type(codegen, val_tn);
    int tag = codegen_next_id(codegen);
    emit_formatted(codegen,
        "({ GrayMap _ms%d = %s; "
        "GrayMap _md%d = gray_map_new_kind(gray_default_arena, _ms%d.key_size, _ms%d.value_size, "
        "_ms%d.order_len > 4 ? _ms%d.order_len * 2 : 8, _ms%d.key_kind); "
        "for (int32_t _mi%d = 0; _mi%d < _ms%d.order_len; _mi%d++) { "
        "int32_t _mslot%d = _ms%d.order[_mi%d]; if (_mslot%d < 0) continue; "
        "%s _mk%d = *(%s *)gray_map_key_at(&_ms%d, _mslot%d); "
        "%s _mvs%d = *(%s *)gray_map_value_at(&_ms%d, _mslot%d); "
        "%s _mvd%d = ",
        tag, src_var,
        tag, tag, tag, tag, tag, tag,
        tag, tag, tag, tag,
        tag, tag, tag, tag,
        c_key, tag, c_key, tag, tag,
        c_val, tag, c_val, tag, tag,
        c_val, tag);

    char src_val_var[VAR_NAME_BUF];
    snprintf(src_val_var, sizeof(src_val_var), "_mvs%d", tag);
    emit_value_deep_copy(codegen, val_tn, src_val_var);

    /* String keys store a pointer into the source arena — copy the data
     * before inserting so the destination map owns its key strings. */
    if (strcmp(key_tn, "string") == 0) {
        emit_formatted(codegen,
            "; _mk%d = gray_string_new(gray_default_arena, _mk%d.data, _mk%d.len); "
            "gray_map_set(gray_default_arena, &_md%d, &_mk%d, &_mvd%d, __FILE__, __LINE__); "
            "} _md%d; })",
            tag, tag, tag, tag, tag, tag, tag);
    } else {
        emit_formatted(codegen,
            "; gray_map_set(gray_default_arena, &_md%d, &_mk%d, &_mvd%d, __FILE__, __LINE__); "
            "} _md%d; })",
            tag, tag, tag, tag);
    }
}

/* Cycle guard for emit_struct_deep_copy: prevents infinite recursion when
 * struct types reference each other in a cycle (e.g. A has [B], B has A). */
static const char *emit_struct_deep_copy_visiting[CYCLE_GUARD_DEPTH];
static int emit_struct_deep_copy_depth = 0;

static void emit_struct_deep_copy(CodeGen *codegen, const char *struct_tn, const char *src_var) {
    AstNode *sdecl = find_struct_declaration(codegen, struct_tn);
    if (!sdecl) {
        /* No decl info; bitwise copy is the best we can do. */
        emit_formatted(codegen, "%s", src_var);
        return;
    }
    /* Cycle detection: if already emitting a deep copy for this struct type,
     * fall back to a shallow (bitwise) copy to break the cycle. */
    for (int j = 0; j < emit_struct_deep_copy_depth; j++) {
        if (strcmp(emit_struct_deep_copy_visiting[j], struct_tn) == 0) {
            emit_formatted(codegen, "%s", src_var);
            return;
        }
    }
    if (emit_struct_deep_copy_depth < CYCLE_GUARD_DEPTH) emit_struct_deep_copy_visiting[emit_struct_deep_copy_depth++] = struct_tn;
    const char *c_struct = gray_type_to_c_codegen(codegen, struct_tn);
    int tag = codegen_next_id(codegen);
    emit_formatted(codegen,
        "({ %s _ss%d = %s; %s _sd%d = _ss%d; ",
        c_struct, tag, src_var, c_struct, tag, tag);
    for (int i = 0; i < sdecl->data.struct_decl.field_count; i++) {
        StructField *field = &sdecl->data.struct_decl.fields[i];
        if (!field->type_name || !field->name) continue;
        if (!type_needs_deep_copy(codegen, field->type_name)) continue;
        char src_field[MSG_BUF_SIZE];
        snprintf(src_field, sizeof(src_field), "_ss%d.%s", tag, field->name);
        emit_formatted(codegen, "_sd%d.%s = ", tag, field->name);
        emit_value_deep_copy(codegen, field->type_name, src_field);
        emit(codegen, "; ");
    }
    emit_formatted(codegen, "_sd%d; })", tag);
    emit_struct_deep_copy_depth--;
}

static void emit_value_deep_copy(CodeGen *codegen, const char *gray_tn, const char *src_var) {
    if (!type_needs_deep_copy(codegen, gray_tn)) {
        /* Primitive / pointer / scalar struct; C value copy is correct. */
        emit_formatted(codegen, "%s", src_var);
        return;
    }
    if (strcmp(gray_tn, "string") == 0) {
        emit_formatted(codegen, "gray_string_new(gray_default_arena, %s.data, %s.len)", src_var, src_var);
        return;
    }
    if (gray_tn[0] == '[') {
        emit_array_deep_copy(codegen, gray_tn, src_var);
        return;
    }
    if (strncmp(gray_tn, "map[", 4) == 0) {
        emit_map_deep_copy(codegen, gray_tn, src_var);
        return;
    }
    /* Must be a struct that needs recursion. */
    emit_struct_deep_copy(codegen, gray_tn, src_var);
}

/* Entry point used by the three sites that hold an AstNode for the
 * source array (copy() builtin, var_decl copy-on-assign, assignment
 * copy-on-assign). Evaluates the AstNode once into a temp, then hands
 * the temp name to emit_value_deep_copy with a reconstructed "[elem]"
 * type string. */
static void emit_deep_array_copy(CodeGen *codegen, AstNode *src_node, const char *elem_type_name) {
    int tag = codegen_next_id(codegen);
    emit_formatted(codegen, "({ GrayArray _dtop%d = ", tag);
    emit_expression(codegen, src_node);
    emit(codegen, "; ");
    char src_var[SHORT_VAR_BUF];
    snprintf(src_var, sizeof(src_var), "_dtop%d", tag);
    char full_tn[MSG_BUF_SIZE];
    snprintf(full_tn, sizeof(full_tn), "[%s]", elem_type_name ? elem_type_name : "");
    emit_value_deep_copy(codegen, full_tn, src_var);
    emit(codegen, "; })");
}

/* Resolve an import alias to the actual module name, or return the name
 * itself. The mapping is the type checker's, not a second copy built here. */
static const char *resolve_alias(CodeGen *codegen, const char *name) {
    return module_table_resolve_alias(codegen->modules, name);
}

/* Check if a variable name is a mutable parameter in the current function */
static bool is_reference_variable(CodeGen *codegen, const char *name) {
    for (int i = 0; i < codegen->ref_var_count; i++) {
        if (strcmp(codegen->ref_vars[i], name) == 0) return true;
    }
    return false;
}

static void register_reference_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->ref_vars, codegen->ref_var_count, codegen->ref_var_cap);
    codegen->ref_vars[codegen->ref_var_count++] = name;
}

static bool is_raw_variable(CodeGen *codegen, const char *name) {
    /* Search from end: most recent entry for this name wins. */
    for (int i = codegen->raw_var_count - 1; i >= 0; i--) {
        if (strcmp(codegen->raw_vars[i].name, name) == 0)
            return codegen->raw_vars[i].is_raw;
    }
    return false;
}

static void register_raw_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->raw_vars, codegen->raw_var_count, codegen->raw_var_cap);
    codegen->raw_vars[codegen->raw_var_count].name = name;
    codegen->raw_vars[codegen->raw_var_count].is_raw = true;
    codegen->raw_var_count++;
}

static void unregister_raw_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->raw_vars, codegen->raw_var_count, codegen->raw_var_cap);
    codegen->raw_vars[codegen->raw_var_count].name = name;
    codegen->raw_vars[codegen->raw_var_count].is_raw = false;
    codegen->raw_var_count++;
}

/* Was this variable last assigned the result of new()? Its pointee lives in
 * gray_heap_arena, so replacing a container field through it must also
 * target gray_heap_arena rather than the current function's scoped arena. */
static bool is_heap_variable(CodeGen *codegen, const char *name) {
    for (int i = codegen->heap_var_count - 1; i >= 0; i--) {
        if (strcmp(codegen->heap_vars[i].name, name) == 0)
            return codegen->heap_vars[i].is_heap;
    }
    return false;
}

static void register_heap_variable(CodeGen *codegen, const char *name, bool is_heap) {
    GROW_ARRAY(codegen->heap_vars, codegen->heap_var_count, codegen->heap_var_cap);
    codegen->heap_vars[codegen->heap_var_count].name = name;
    codegen->heap_vars[codegen->heap_var_count].is_heap = is_heap;
    codegen->heap_var_count++;
}

/* True when value is a direct new(...) expression — the only construct
 * that hands back a pointer into gray_heap_arena. */
static bool is_new_call(AstNode *value) {
    return value && value->kind == NODE_NEW_EXPR;
}

/* Returns true if the named enum is string-backed.
 * enum_names is sorted after the init pass, so we use bsearch. */
static bool codegen_enum_is_string(CodeGen *codegen, const char *name) {
    if (!name) return false;
    const char **hit = bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                               sizeof(const char *), keyword_compare);
    if (hit) return codegen->enum_is_string[hit - codegen->enum_names];
    return false;
}

/* Register a bigint variable's declared type name */
static void register_bigint_variable(CodeGen *codegen, const char *name, const char *type_name) {
    if (codegen->bigint_var_count >= codegen->bigint_var_cap) {
        codegen->bigint_var_cap = codegen->bigint_var_cap ? codegen->bigint_var_cap * 2 : 8;
        codegen->bigint_var_names = xrealloc(codegen->bigint_var_names, sizeof(const char *) * codegen->bigint_var_cap);
        codegen->bigint_var_types = xrealloc(codegen->bigint_var_types, sizeof(const char *) * codegen->bigint_var_cap);
    }
    codegen->bigint_var_names[codegen->bigint_var_count] = name;
    codegen->bigint_var_types[codegen->bigint_var_count] = type_name;
    codegen->bigint_var_count++;
}

/* Look up a variable's bigint type name, or NULL if not bigint */
static const char *lookup_bigint_variable(CodeGen *codegen, const char *name) {
    for (int i = codegen->bigint_var_count - 1; i >= 0; i--) {
        if (strcmp(codegen->bigint_var_names[i], name) == 0) return codegen->bigint_var_types[i];
    }
    return NULL;
}

/* Get the bigint type prefix for a given type name (e.g., "i128" → "gray_i128") */
static const char *bigint_prefix(const char *type_str) {
    if (strcmp(type_str, "i128") == 0) return "gray_i128";
    if (strcmp(type_str, "u128") == 0) return "gray_u128";
    if (strcmp(type_str, "i256") == 0) return "gray_i256";
    if (strcmp(type_str, "u256") == 0) return "gray_u256";
    return NULL;
}

/* Canonical wide-integer type name as a string literal, or NULL if not one.
 * Use this (not the caller's string) when registering a bigint binding, so the
 * stored pointer has static lifetime. */
static const char *bigint_type_name(const char *type_str) {
    if (!type_str) return NULL;
    if (strcmp(type_str, "i128") == 0) return "i128";
    if (strcmp(type_str, "u128") == 0) return "u128";
    if (strcmp(type_str, "i256") == 0) return "i256";
    if (strcmp(type_str, "u256") == 0) return "u256";
    return NULL;
}

/* Resolve the bigint type name for an expression (checks labels against tracked vars) */
static const char *resolve_bigint_type(CodeGen *codegen, AstNode *node) {
    if (!node) return NULL;
    if (node->kind == NODE_LABEL) {
        return lookup_bigint_variable(codegen, node->data.label.value);
    }
    /* If the node is a call to a bigint cast function */
    if (node->kind == NODE_CALL_EXPR && node->data.call.function->kind == NODE_LABEL) {
        const char *function_name = node->data.call.function->data.label.value;
        if (is_bigint_type(function_name)) return function_name;
    }
    /* cast(expr, i128/u128/i256/u256) — the result is already the target bigint */
    if (node->kind == NODE_CAST_EXPR && is_bigint_type(node->data.cast.target_type))
        return node->data.cast.target_type;
    /* -bigint_var — the result is still the same bigint type */
    if (node->kind == NODE_PREFIX_EXPR && node->data.prefix.op == TOK_MINUS)
        return resolve_bigint_type(codegen, node->data.prefix.right);
    /* If this is an infix expression, check left operand.
     * Operators that yield bool never yield a bigint, whatever their operands
     * are — mirrors the bool-result set in resolve_infix_expression. */
    if (node->kind == NODE_INFIX_EXPR) {
        TokenType op = node->data.infix.op;
        if (op == TOK_EQ || op == TOK_NOT_EQ ||
            op == TOK_LT || op == TOK_GT ||
            op == TOK_LT_EQ || op == TOK_GT_EQ ||
            op == TOK_AND || op == TOK_OR ||
            op == TOK_IN || op == TOK_NOT_IN)
            return NULL;
        const char *left_type = resolve_bigint_type(codegen, node->data.infix.left);
        if (left_type) return left_type;
        return resolve_bigint_type(codegen, node->data.infix.right);
    }
    /* Struct field access a.val — check the resolved field type from the type table */
    if (node->kind == NODE_MEMBER_EXPR) {
        GrayType *field_t = codegen->type_table
            ? typetable_get(codegen->type_table, node) : NULL;
        if (field_t && field_t->name && is_bigint_type(field_t->name))
            return field_t->name;
    }
    /* Pointer dereference p^ — check whether the pointee type is bigint */
    if (node->kind == NODE_POSTFIX_EXPR && node->data.postfix.op == TOK_CARET) {
        GrayType *ptr_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.postfix.left) : NULL;
        if (ptr_t && ptr_t->kind == TK_POINTER && ptr_t->element_type &&
            is_bigint_type(ptr_t->element_type))
            return ptr_t->element_type;
    }
    /* Array indexing arr[i] and user-function calls: the typechecker records
     * the resolved element / return type, so trust that when it is bigint. */
    if (node->kind == NODE_INDEX_EXPR || node->kind == NODE_CALL_EXPR) {
        GrayType *nt = codegen->type_table
            ? typetable_get(codegen->type_table, node) : NULL;
        if (nt && nt->name && is_bigint_type(nt->name))
            return nt->name;
    }
    return NULL;
}

/* Emit a scalar expression converted to a wide integer type.
 *
 * The constructor is chosen from the SOURCE operand's signedness, never the
 * destination's. i128 and i256 represent every uint exactly, so widening a
 * uint is value-preserving — but routing it through from_i64 reinterprets
 * any value above INT64_MAX as negative. Only when the source type cannot
 * be resolved does the destination's signedness stand in.
 *
 * value_t may be NULL; the type table is consulted when it is. */
static void emit_scalar_to_bigint(CodeGen *codegen, const char *target_type,
                                  AstNode *value, GrayType *value_t) {
    const char *pfx = bigint_prefix(target_type);

    /* Integer literals carry their own width. One above INT64_MAX cannot go
     * through from_i64 at all, so it is parsed from its decimal text
     * directly into the wide type. */
    if (value->kind == NODE_INT_VALUE) {
        if (value->data.int_value.overflow) {
            emit_formatted(codegen, "%s_from_decimal(\"%s\")", pfx,
                value->data.int_value.literal);
        } else {
            const char *sfx = (target_type[0] == 'u') ? "u64" : "i64";
            emit_formatted(codegen, "%s_from_%s(%lldLL)", pfx, sfx,
                (long long)value->data.int_value.value);
        }
        return;
    }
    if (value->kind == NODE_PREFIX_EXPR && value->data.prefix.op == TOK_MINUS &&
        value->data.prefix.right && value->data.prefix.right->kind == NODE_INT_VALUE) {
        if (value->data.prefix.right->data.int_value.overflow) {
            emit_formatted(codegen, "%s_from_decimal(\"-%s\")", pfx,
                value->data.prefix.right->data.int_value.literal);
        } else {
            emit_formatted(codegen, "%s_from_i64(%lldLL)", pfx,
                -(long long)value->data.prefix.right->data.int_value.value);
        }
        return;
    }

    if (!value_t && codegen->type_table)
        value_t = typetable_get(codegen->type_table, value);

    bool src_unsigned = value_t
        ? (value_t->kind == TK_UINT || value_t->kind == TK_BYTE)
        : false;

    /* An unsigned destination has only from_u64 to offer, so it takes that
     * path whatever the source is; a signed one follows the source. */
    bool use_u64 = (target_type[0] == 'u') || src_unsigned;

    if (use_u64) {
        emit_formatted(codegen, "%s_from_u64((uint64_t)(", pfx);
    } else {
        emit_formatted(codegen, "%s_from_i64((int64_t)(", pfx);
    }
    emit_expression(codegen, value);
    emit(codegen, "))");
}

/* Coerce `value` to bigint type `bi` when it is not already a bigint, so it
 * can be assigned / passed / returned / stored where a bigint is expected.
 * Integer literals (including those wider than 64 bits) and other scalars are
 * wrapped with the matching constructor. Returns true when it emitted the
 * value; false means `bi` is not a bigint or `value` already is one, and the
 * caller should emit `value` normally. */
static bool emit_bigint_coerced(CodeGen *codegen, const char *bi, AstNode *value) {
    if (!bi || !is_bigint_type(bi)) return false;
    if (resolve_bigint_type(codegen, value)) return false;
    emit_scalar_to_bigint(codegen, bi, value, NULL);
    return true;
}

/* Emit `value` for a map key or value slot whose Grayscale type is `gray_tn`:
 * a wide-integer slot needs the scalar wrapped in its constructor, everything
 * else emits verbatim. Safe to call with any `gray_tn`. */
static void emit_map_slot_value(CodeGen *codegen, const char *gray_tn, AstNode *value) {
    if (!emit_bigint_coerced(codegen, gray_tn, value))
        emit_expression(codegen, value);
}

/* Emit a bigint operand, widening smaller integer or bigint operands so
 * mixed-width expressions like i128+i64 or i256+i128 pass correctly typed
 * values to the bigint arithmetic helpers. */
static void emit_bigint_operand(CodeGen *codegen, AstNode *operand,
                                const char *pfx, const char *bi_type,
                                GrayType *operand_t) {
    /* Integer literal, negative or not */
    if (operand->kind == NODE_INT_VALUE ||
        (operand->kind == NODE_PREFIX_EXPR &&
         operand->data.prefix.op == TOK_MINUS &&
         operand->data.prefix.right->kind == NODE_INT_VALUE)) {
        emit_scalar_to_bigint(codegen, bi_type, operand, operand_t);
        return;
    }
    /* Check if operand is a bigint label and if it needs widening to a larger bigint */
    if (operand->kind == NODE_LABEL) {
        const char *src_bi = lookup_bigint_variable(codegen, operand->data.label.value);
        if (src_bi) {
            /* Operand is already a bigint — emit directly if same type,
             * or wrap with a widening constructor for bigint→bigint promotion. */
            if (strcmp(src_bi, bi_type) == 0) {
                emit_expression(codegen, operand);
            } else if (strcmp(bi_type, "i256") == 0 && strcmp(src_bi, "i128") == 0) {
                emit_formatted(codegen, "gray_i256_from_i128(");
                emit_expression(codegen, operand);
                emit(codegen, ")");
            } else if (strcmp(bi_type, "u256") == 0 && strcmp(src_bi, "u128") == 0) {
                emit_formatted(codegen, "gray_u256_from_u128(");
                emit_expression(codegen, operand);
                emit(codegen, ")");
            } else {
                /* Unknown bigint-to-bigint: emit directly and let C catch it */
                emit_expression(codegen, operand);
            }
            return;
        }
        /* Non-bigint label: widen to the target bigint type. */
        emit_scalar_to_bigint(codegen, bi_type, operand, operand_t);
        return;
    }
    /* Non-label expression — emit directly */
    emit_expression(codegen, operand);
}

static bool is_mutable_parameter(CodeGen *codegen, const char *name) {
    if (!codegen->current_func) return false;
    for (int i = 0; i < codegen->current_func->data.func_decl.param_count; i++) {
        Param *param = &codegen->current_func->data.func_decl.params[i];
        if (param->mutable && strcmp(param->name, name) == 0) return true;
    }
    return false;
}

/* True if the function takes any & (mutable reference) or ^T (pointer)
 * parameter. Such a function can write freshly-allocated data — appended
 * array elements, grown map buckets, assigned string/struct fields — into
 * caller-owned memory through that parameter, and that data must outlive
 * the call. A private _func_arena or scope_restore watermark would reclaim
 * it on return, leaving the caller with dangling pointers. So these
 * functions run directly in the caller's arena: no private arena, no
 * scope-restore, no return-value escape (the result is already in the
 * caller's arena). */
static bool function_uses_caller_arena(CodeGen *codegen, AstNode *function_node) {
    if (!function_node || function_node->kind != NODE_FUNC_DECL) return false;
    for (int i = 0; i < function_node->data.func_decl.param_count; i++) {
        Param *param = &function_node->data.func_decl.params[i];
        if (param->mutable) return true;
        if (!param->type_name) continue;
        const char *tn = resolve_type_alias_codegen(codegen, param->type_name);
        if (tn && tn[0] == '^') return true;
    }
    return false;
}

/* Same reasoning one level down: a caller-arena function must not open
 * per-iteration or per-block arenas either. Anything it allocates can be
 * stored through a pointer parameter and outlive the block that created it,
 * so its allocations stay in the caller's arena for the whole call. */
static bool current_function_uses_caller_arena(CodeGen *codegen) {
    return function_uses_caller_arena(codegen, codegen->current_func);
}

static bool is_result_temporary(const char *name) {
    if (!name) return false;
    return strncmp(name, GRAY_SYNTH_TMP, sizeof(GRAY_SYNTH_TMP) - 1) == 0 ||
           strncmp(name, GRAY_SYNTH_OR, sizeof(GRAY_SYNTH_OR) - 1) == 0;
}

static int function_name_compare(const void *left, const void *right) {
    const AstNode *left_func = *(const AstNode *const *)left;
    const AstNode *right_func = *(const AstNode *const *)right;
    return strcmp(left_func->data.func_decl.name, right_func->data.func_decl.name);
}

/* Find a function declaration by name. Builds and reuses a sorted view of
 * codegen->all_funcs so lookups are O(log n) after the first call. The view is
 * invalidated whenever a new function is registered (see register sites). */
static AstNode *find_function(CodeGen *codegen, const char *name) {
    if (codegen->func_count == 0) return NULL;
    if (!codegen->funcs_by_name_built) {
        codegen->funcs_by_name = xrealloc(codegen->funcs_by_name,
            sizeof(AstNode *) * (size_t)codegen->func_count);
        memcpy(codegen->funcs_by_name, codegen->all_funcs,
            sizeof(AstNode *) * (size_t)codegen->func_count);
        qsort(codegen->funcs_by_name, (size_t)codegen->func_count, sizeof(AstNode *), function_name_compare);
        codegen->funcs_by_name_built = true;
    }
    /* Build a stack key node so bsearch can compare against the name field. */
    AstNode key;
    key.data.func_decl.name = name;
    AstNode *key_ptr = &key;
    AstNode **hit = bsearch(&key_ptr, codegen->funcs_by_name, (size_t)codegen->func_count,
        sizeof(AstNode *), function_name_compare);
    return hit ? *hit : NULL;
}

/* The function a bare name in a `ref(name)` refers to. Codegen renames every
 * declaration to its module-mangled spelling, so a name written inside an
 * imported module is not the key its function is indexed under — looking up
 * only the written name missed it and emitted a variable's address. */
static AstNode *find_referenced_function(CodeGen *codegen, AstNode *label) {
    const char *written = label->data.label.value;
    AstNode *target = find_function(codegen, written);
    if (target) return target;
    const char *resolved = codegen_resolve_ref(codegen, label, written);
    return resolved != written ? find_function(codegen, resolved) : NULL;
}

/* Build the (field_name, struct_name) index for func-typed fields once.
 * Order matches struct_decls so the first-match heuristic is preserved. */
static void build_function_field_index(CodeGen *codegen) {
    if (codegen->func_field_index_built) return;
    int total = 0;
    for (int si = 0; si < codegen->struct_decl_count; si++) {
        total += codegen->struct_decls[si]->data.struct_decl.field_count;
    }
    if (total > 0) {
        codegen->func_field_index = xmalloc(sizeof(*codegen->func_field_index) * (size_t)total);
    }
    for (int si = 0; si < codegen->struct_decl_count; si++) {
        AstNode *sd = codegen->struct_decls[si];
        for (int field_index = 0; field_index < sd->data.struct_decl.field_count; field_index++) {
            StructField *sf = &sd->data.struct_decl.fields[field_index];
            if (sf->type_name &&
                (strcmp(sf->type_name, "func") == 0 ||
                 strncmp(sf->type_name, "func(", 5) == 0)) {
                codegen->func_field_index[codegen->func_field_count].field_name = sf->name;
                codegen->func_field_index[codegen->func_field_count].struct_name = sd->data.struct_decl.name;
                codegen->func_field_count++;
            }
        }
    }
    codegen->func_field_index_built = true;
}

/* --- Expression Emission Helpers --- */

static void emit_label(CodeGen *codegen, AstNode *node) {
    const char *name = sanitize_name(node->data.label.value);
    const char *raw = node->data.label.value;
    /* : bare stdlib constants from using-modules */
    static const struct { const char *n; const char *mod; const char *val; } _cg_consts[] = {
        {"PI","math","3.14159265358979323846"},{"E","math","2.71828182845904523536"},
        {"TAU","math","6.28318530717958647692"},{"PHI","math","1.61803398874989484820"},
        {"SQRT2","math","1.41421356237309504880"},{"LN2","math","0.69314718055994530942"},
        {"LN10","math","2.30258509299404568402"},{"INF","math","(1.0/0.0)"},
        {"NEG_INF","math","(-1.0/0.0)"},{"EPSILON","math","2.2204460492503131e-16"},
        {"MAX_INT","math","9223372036854775807LL"},{"MIN_INT","math","(-9223372036854775807LL - 1)"},
        {"MAX_FLOAT","math","1.7976931348623157e308"},{"MIN_FLOAT","math","-1.7976931348623157e308"},
        {"MAC_OS","os","GrayEnum_Platform_MAC_OS"},{"LINUX","os","GrayEnum_Platform_LINUX"},
        {"WINDOWS","os","GrayEnum_Platform_WINDOWS"},{"OTHER","os","GrayEnum_Platform_OTHER"},
        {"O_RDONLY","io","GrayEnum_OpenFlag_O_RDONLY"},{"O_WRONLY","io","GrayEnum_OpenFlag_O_WRONLY"},
        {"O_RDWR","io","GrayEnum_OpenFlag_O_RDWR"},
        {"BASE_2","strconv","2"},{"BASE_8","strconv","8"},{"BASE_10","strconv","10"},
        {"BASE_16","strconv","16"},{"BASE_36","strconv","36"},
        {"NIL_UUID","uuid","gray_uuid_nil()"},
        {NULL,NULL,NULL}
    };
    bool emitted_const = false;
    for (int ui = 0; ui < codegen->using_module_count && !emitted_const; ui++) {
        const char *real_mod = resolve_alias(codegen, codegen->using_modules[ui]);
        for (int ci = 0; _cg_consts[ci].n; ci++) {
            if (strcmp(raw, _cg_consts[ci].n) == 0 &&
                strcmp(real_mod, _cg_consts[ci].mod) == 0) {
                emit(codegen, _cg_consts[ci].val);
                emitted_const = true;
                break;
            }
        }
    }
    if (emitted_const) return;
    if (is_mutable_parameter(codegen, raw)) {
        emit_formatted(codegen, "(*%s)", name);
    } else if (is_reference_variable(codegen, raw)) {
        emit_formatted(codegen, "(*%s)", name);
    } else {
        /* A bare name that names a module-level declaration is emitted under
         * that declaration's mangled name. Locals and parameters are not
         * declarations, so they resolve to nothing and stay as written —
         * which is why a binding that merely shares a name with a sibling
         * file of its module is left alone. */
        const char *resolved = codegen_resolve_ref(codegen, node, raw);
        emit(codegen, resolved != raw ? sanitize_name(resolved) : name);
    }
}

static void emit_string_value(CodeGen *codegen, AstNode *node) {
    /* Emit string literal, breaking hex escapes to prevent C's greedy \x parsing.
     * "A\x42C" → "A\x42" "C" (C string concatenation) */
    const char *cursor = node->data.string_value.value;
    /* Check for null bytes; if present, use gray_string_lit_len with explicit length
     * since strlen() would truncate at the null */
    bool has_null = false;
    int str_len = 0;
    for (const char *scan = cursor; *scan; scan++) {
        if (scan[0] == '\\' && scan[1] == 'x' && scan[2] == '0' && scan[3] == '0') {
            has_null = true;
            str_len++; /* \x00 = 1 byte */
            scan += 3;
        } else if (scan[0] == '\\' && scan[1] == '0') {
            has_null = true;
            str_len++; /* \0 = 1 byte */
            scan += 1;
        } else if (scan[0] == '\\' && scan[1]) {
            str_len++; /* other escape = 1 byte */
            scan += 1;
        } else {
            str_len++;
        }
    }
    /* Use macro form for file-scope compatibility */
    if (has_null && codegen->indent > 0) {
        emit_formatted(codegen, "gray_string_lit_len(\"");
    } else {
        emit(codegen, (codegen->indent == 0) ? "GRAY_STRING_LIT(\"" : "gray_string_lit(\"");
    }
    if (node->data.string_value.is_raw) {
        /* Raw string; escape special characters for C output */
        while (*cursor) {
            if (*cursor == '\\') {
                emit(codegen, "\\\\");
            } else if (*cursor == '"') {
                emit(codegen, "\\\"");
            } else if (*cursor == '\n') {
                emit(codegen, "\\n");
            } else if (*cursor == '\r') {
                emit(codegen, "\\r");
            } else if (*cursor == '\t') {
                emit(codegen, "\\t");
            } else {
                append_char_to_buffer(&codegen->output, *cursor);
            }
            cursor++;
        }
    } else {
        while (*cursor) {
            if (cursor[0] == '\\' && cursor[1] == 'x' && isxdigit((unsigned char)cursor[2])) {
                /* Emit \xNN then break the string if followed by a hex digit */
                append_char_to_buffer(&codegen->output, cursor[0]); /* \ */
                append_char_to_buffer(&codegen->output, cursor[1]); /* x */
                append_char_to_buffer(&codegen->output, cursor[2]); /* first hex */
                cursor += 3;
                if (isxdigit((unsigned char)*cursor)) {
                    append_char_to_buffer(&codegen->output, *cursor); /* second hex */
                    cursor++;
                }
                if (isxdigit((unsigned char)*cursor)) {
                    /* Next char is also hex; break the string */
                    emit(codegen, "\" \"");
                }
            } else if (cursor[0] == '\\' && cursor[1] == '$') {
                append_char_to_buffer(&codegen->output, '$');
                cursor += 2;
            } else if (*cursor == '\n') {
                emit(codegen, "\\n");
                cursor++;
            } else if (*cursor == '\r') {
                emit(codegen, "\\r");
                cursor++;
            } else {
                append_char_to_buffer(&codegen->output, *cursor);
                cursor++;
            }
        }
    }
    if (has_null && codegen->indent > 0) {
        emit_formatted(codegen, "\", %d)", str_len);
    } else {
        emit(codegen, "\")");
    }
}

static void emit_interpolated_string(CodeGen *codegen, AstNode *node) {
    /* Emit as chained gray_string_concat() calls instead of gray_string_format()
     * to preserve null bytes in string values (gray_string_format uses vsnprintf
     * which truncates at \0). gray_string_concat is null-safe (uses memcpy). */
    int part_count = node->data.interpolated_string.part_count;
    if (part_count == 0) {
        emit(codegen, "gray_string_lit(\"\")");
        return;
    }
    /* Emit N-1 opening gray_string_concat calls for left-associative chaining:
     * concat(arena, concat(arena, part0, part1), part2) */
    for (int i = 1; i < part_count; i++) {
        emit(codegen, "gray_string_concat(gray_default_arena, ");
    }
    /* Emit each part as a GrayString expression */
    for (int i = 0; i < part_count; i++) {
        if (i > 0) emit(codegen, ", ");
        AstNode *part = node->data.interpolated_string.parts[i];
        if (part->kind == NODE_STRING_VALUE) {
            /* Literal text — reuses NODE_STRING_VALUE codegen (null-safe) */
            emit_expression(codegen, part);
        } else {
            /* Expression — resolve type and emit as GrayString */
            GrayType *part_type = codegen->type_table ? typetable_get(codegen->type_table, part) : NULL;
            TypeKind tk = part_type ? part_type->kind : TK_UNKNOWN;
            if (tk == TK_UNKNOWN) {
                if (codegen->wildcard_binding) {
                    GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
                    if (wildcard_type) { tk = wildcard_type->kind; part_type = wildcard_type; }
                }
            }
            if (tk == TK_UNKNOWN) {
                if (part->kind == NODE_FLOAT_VALUE) tk = TK_FLOAT;
                else if (part->kind == NODE_BOOL_VALUE) tk = TK_BOOL;
                else if (part->kind == NODE_STRING_VALUE) tk = TK_STRING;
                else tk = TK_INT;
            }

            const char *bi = resolve_bigint_type(codegen, part);
            if (bi) {
                emit_formatted(codegen, "%s_to_string(gray_default_arena, ", bigint_prefix(bi));
                emit_expression(codegen, part);
                emit(codegen, ")");
            } else switch (tk) {
            case TK_STRING:
                emit_expression(codegen, part);
                break;
            case TK_BOOL:
                emit(codegen, "(");
                emit_expression(codegen, part);
                emit(codegen, ") ? gray_string_lit(\"true\") : gray_string_lit(\"false\")");
                break;
            case TK_FLOAT:
                emit(codegen, "gray_builtin_format_float(gray_default_arena, ");
                emit_expression(codegen, part);
                emit(codegen, ")");
                break;
            case TK_CHAR:
                emit(codegen, "gray_builtin_char_to_utf8(gray_default_arena, ");
                emit_expression(codegen, part);
                emit(codegen, ")");
                break;
            case TK_ARRAY: {
                int elem_kind_tag = 0;
                if (part_type && part_type->element_type) {
                    GrayType *et = type_from_name(part_type->element_type);
                    if (et->kind == TK_FLOAT) elem_kind_tag = 1;
                    else if (et->kind == TK_STRING) elem_kind_tag = 2;
                    else if (et->kind == TK_BOOL) elem_kind_tag = 3;
                    else if (et->kind == TK_UINT) elem_kind_tag = 4;
                    else if (et->kind == TK_BYTE) elem_kind_tag = 5;
                    else if (et->kind == TK_CHAR) elem_kind_tag = 6;
                    else if (et->kind == TK_ENUM) {
                        elem_kind_tag = (part_type->element_type && codegen_enum_is_string(codegen, part_type->element_type)) ? 2 : 7;
                    }
                }
                emit_formatted(codegen, "({ GrayArray _interp_arr = ");
                emit_expression(codegen, part);
                emit_formatted(codegen, "; gray_builtin_array_to_string(gray_default_arena, &_interp_arr, %d); })", elem_kind_tag);
                break;
            }
            case TK_MAP: {
                int value_kind_tag = 0;
                if (part_type && part_type->value_type) {
                    GrayType *vt = type_from_name(part_type->value_type);
                    if (vt->kind == TK_FLOAT) value_kind_tag = 1;
                    else if (vt->kind == TK_STRING) value_kind_tag = 2;
                    else if (vt->kind == TK_BOOL) value_kind_tag = 3;
                    else if (vt->kind == TK_UINT) value_kind_tag = 4;
                    else if (vt->kind == TK_BYTE) value_kind_tag = 5;
                    else if (vt->kind == TK_CHAR) value_kind_tag = 6;
                    else if (vt->kind == TK_ENUM) {
                        value_kind_tag = (part_type->value_type && codegen_enum_is_string(codegen, part_type->value_type)) ? 2 : 7;
                    }
                }
                emit_formatted(codegen, "({ GrayMap _interp_map = ");
                emit_expression(codegen, part);
                emit_formatted(codegen, "; gray_builtin_map_to_string(gray_default_arena, &_interp_map, %d); })", value_kind_tag);
                break;
            }
            case TK_ERROR:
                emit_expression(codegen, part);
                emit(codegen, " ? ");
                emit_expression(codegen, part);
                emit(codegen, "->msg : gray_string_lit(\"nil\")");
                break;
            case TK_UINT:
                emit(codegen, "gray_string_format(gray_default_arena, \"%llu\", (unsigned long long)(");
                emit_expression(codegen, part);
                emit(codegen, "))");
                break;
            case TK_STRUCT:
                if (part_type && part_type->name && strcmp(part_type->name, "UUID") == 0) {
                    emit_expression(codegen, part);
                    emit(codegen, ".value");
                } else {
                    emit(codegen, "gray_string_format(gray_default_arena, \"%lld\", (long long)(");
                    emit_expression(codegen, part);
                    emit(codegen, "))");
                }
                break;
            case TK_ENUM:
                if (part_type && part_type->name && codegen_enum_is_string(codegen, part_type->name)) {
                    emit_expression(codegen, part);
                } else if (part_type && codegen_enum_is_error_code(codegen, part_type->name)) {
                    emit(codegen, "gray_string_lit(gray_error_code_name((int64_t)(");
                    emit_expression(codegen, part);
                    emit(codegen, ")))");
                } else {
                    emit(codegen, "gray_string_format(gray_default_arena, \"%lld\", (long long)(");
                    emit_expression(codegen, part);
                    emit(codegen, "))");
                }
                break;
            default:
                emit(codegen, "gray_string_format(gray_default_arena, \"%lld\", (long long)(");
                emit_expression(codegen, part);
                emit(codegen, "))");
                break;
            }
        }
        if (i > 0) emit(codegen, ")");
    }
}

static void emit_array_value(CodeGen *codegen, AstNode *node) {
    /* Array literal: emit as GrayArray using gray_array_from */
    int count = node->data.array_value.count;
    if (count == 0) {
        /* Empty array; check type table for element type, falling
         * back to the var-decl context type if the node has none. */
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node) : NULL;
        if ((!arr_t || arr_t->kind == TK_UNKNOWN) && codegen->current_var_type && codegen->current_var_type[0]) {
            arr_t = type_from_name(codegen->current_var_type);
        }
        const char *elem_sz = "int64_t";
        if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type) {
            const char *etype = arr_t->element_type;
            GrayType *et = type_from_name(etype);
            if (et->kind == TK_FLOAT) elem_sz = (strcmp(etype, "f32") == 0) ? "float" : "double";
            else if (et->kind == TK_BOOL) elem_sz = "bool";
            else if (et->kind == TK_STRING) elem_sz = "GrayString";
            else if (et->kind == TK_ARRAY) elem_sz = "GrayArray";
            else if (et->kind == TK_MAP) elem_sz = "GrayMap";
            else if (et->kind == TK_STRUCT) elem_sz = gray_type_to_c_codegen(codegen, etype);
            else if (et->kind == TK_POINTER) elem_sz = gray_type_to_c_codegen(codegen, etype);
            else if (et->kind == TK_CHAR) elem_sz = "int32_t";
            else if (et->kind == TK_BYTE) elem_sz = "uint8_t";
            else if (strcmp(etype, "i128") == 0) elem_sz = "gray_i128";
            else if (strcmp(etype, "u128") == 0) elem_sz = "gray_u128";
            else if (strcmp(etype, "i256") == 0) elem_sz = "gray_i256";
            else if (strcmp(etype, "u256") == 0) elem_sz = "gray_u256";
        }
        emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), 4)", elem_sz);
        return;
    }

    /* Check if this is a nested array (elements are arrays) */
    if (node->data.array_value.elements[0]->kind == NODE_ARRAY_VALUE) {
        /* Nested array: each element is an GrayArray */
        emit_formatted(codegen, "gray_array_from(gray_default_arena, (GrayArray[]){");
        for (int i = 0; i < count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.array_value.elements[i]);
        }
        emit_formatted(codegen, "}, sizeof(GrayArray), %d)", count);
        return;
    }

    /* Array of maps: elements are map literals */
    if (node->data.array_value.elements[0]->kind == NODE_MAP_VALUE) {
        emit_formatted(codegen, "gray_array_from(gray_default_arena, (GrayMap[]){");
        for (int i = 0; i < count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.array_value.elements[i]);
        }
        emit_formatted(codegen, "}, sizeof(GrayMap), %d)", count);
        return;
    }

    /* Determine element type; try bigint detection first, then type table */
    const char *bi_elem = resolve_bigint_type(codegen, node->data.array_value.elements[0]);
    GrayType *elem_t = codegen->type_table
        ? typetable_get(codegen->type_table, node->data.array_value.elements[0])
        : NULL;
    if (!bi_elem && elem_t && elem_t->name && is_bigint_type(elem_t->name))
        bi_elem = elem_t->name;
    /* Also check var decl context for bigint element type — either a bare
     * bigint name (nested-array recursion) or an array type like "[i128]". */
    if (!bi_elem && codegen->current_var_type && is_bigint_type(codegen->current_var_type))
        bi_elem = codegen->current_var_type;
    if (!bi_elem && codegen->current_var_type) {
        const char *cvt = codegen->current_var_type;
        size_t cvt_len = strlen(cvt);
        if (cvt_len >= 3 && cvt[0] == '[' && cvt[cvt_len - 1] == ']') {
            char inner[TYPE_NAME_MAX];
            size_t ilen = cvt_len - 2;
            if (ilen < sizeof(inner)) {
                memcpy(inner, cvt + 1, ilen);
                inner[ilen] = '\0';
                char *comma = strchr(inner, ',');
                if (comma) *comma = '\0';
                if (is_bigint_type(inner)) bi_elem = type_from_name(inner)->name;
            }
        }
    }
    /* Fall back to the declared array type when the typetable has no
     * entry for the first element (happens for module-qualified struct
     * function calls — the call's return type isn't always threaded
     * into the table). Without this we default to int64_t and emit a
     * `(int64_t[]){struct_value}` cast that clang rejects. */
    if ((!elem_t || elem_t->kind == TK_UNKNOWN) &&
        codegen->current_var_type &&
        codegen->current_var_type[0] == '[' &&
        strncmp(codegen->current_var_type, "[func", 5) != 0) {
        size_t cvt_len = strlen(codegen->current_var_type);
        if (cvt_len >= 3 && codegen->current_var_type[cvt_len - 1] == ']') {
            char inferred[MSG_BUF_SIZE];
            size_t copy_len = cvt_len - 2;
            if (copy_len >= sizeof(inferred)) copy_len = sizeof(inferred) - 1;
            memcpy(inferred, codegen->current_var_type + 1, copy_len);
            inferred[copy_len] = '\0';
            /* Strip fixed-size ",N" suffix if present */
            char *comma = strchr(inferred, ',');
            if (comma) *comma = '\0';
            GrayType *inferred_t = type_from_name(inferred);
            if (inferred_t && inferred_t->kind != TK_UNKNOWN) elem_t = inferred_t;
        }
    }
    TypeKind tk = elem_t ? elem_t->kind : TK_INT;

    /* Integer literals in a declared [float]/[f32]/[f64] array must use
     * double so the C compound literal stores the correct IEEE 754 bits
     * instead of raw int64_t bit patterns. */
    if (tk == TK_INT && codegen->current_var_type) {
        const char *cvt = codegen->current_var_type;
        if (strcmp(cvt, "[float]") == 0 || strcmp(cvt, "[f32]") == 0 || strcmp(cvt, "[f64]") == 0)
            tk = TK_FLOAT;
    }

    const char *c_type;
    /* Check for bigint types first */
    if (bi_elem) {
        c_type = bigint_prefix(bi_elem);
    } else if (elem_t && elem_t->name && (strcmp(elem_t->name, "func") == 0 || strncmp(elem_t->name, "func(", 5) == 0)) {
        /* Function reference elements: store as generic fn ptrs, cast at
         * call sites (mirrors gray_type_to_c_codegen's handling of "func"). */
        c_type = "void *";
    } else if (codegen->current_var_type &&
               (codegen->current_var_type && (strcmp(codegen->current_var_type, "[func]") == 0 || strncmp(codegen->current_var_type, "[func(", 6) == 0))) {
        /* Declared as [func] but element inference missed it (e.g. empty
         * literal or heterogeneous func refs). */
        c_type = "void *";
    } else switch (tk) {
    case TK_FLOAT:  c_type = "double"; break;
    case TK_BOOL:   c_type = "bool"; break;
    case TK_STRING: c_type = "GrayString"; break;
    case TK_STRUCT: c_type = gray_type_to_c_codegen(codegen, elem_t->name); break;
    case TK_ENUM: {
        bool is_str = elem_t->name ? codegen_enum_is_string(codegen, elem_t->name) : false;
        static char enum_arr_buf[MSG_BUF_SIZE];
        if (is_str) {
            c_type = "GrayString";
        } else if (elem_t->name && strcmp(elem_t->name, "ErrorCode") == 0) {
            c_type = "GrayErrorCode";
        } else {
            snprintf(enum_arr_buf, sizeof(enum_arr_buf), "GrayEnum_%s", elem_t->name ? elem_t->name : "int");
            c_type = enum_arr_buf;
        }
        break;
    }
    case TK_POINTER: {
        const char *pointee = elem_t->element_type ? elem_t->element_type : "void";
        const char *c_pointee = gray_type_to_c_codegen(codegen, pointee);
        static char ptr_buf[MSG_BUF_SIZE];
        snprintf(ptr_buf, sizeof(ptr_buf), "%s *", c_pointee);
        c_type = ptr_buf;
        break;
    }
    case TK_MAP:    c_type = "GrayMap"; break;
    case TK_ARRAY:  c_type = "GrayArray"; break;
    case TK_CHAR:   c_type = "int32_t"; break;
    case TK_BYTE:   c_type = "uint8_t"; break;
    default:        c_type = "int64_t"; break;
    }

    /* A declared [f32] array stores packed 4-byte float; the TK_FLOAT case
     * (and the int-literal override above) otherwise emit double storage. */
    if (strcmp(c_type, "double") == 0 && codegen->current_var_type &&
        strcmp(codegen->current_var_type, "[f32]") == 0)
        c_type = "float";

    /* A non-empty integer-literal element carries TK_INT regardless of the
     * declared width/signedness, so [byte]/[i8..i64]/[u8..u64]/[uint] would
     * otherwise fall to int64_t storage. When the declaration pins a narrower
     * or unsigned integer element, match it — the same way [f32]/[f64] above
     * do for floats and empty literals already do via type_from_name. Covers
     * both the [T] and [T, N] forms. */
    if ((tk == TK_INT || tk == TK_UINT || tk == TK_BYTE) && !bi_elem &&
        codegen->current_var_type && codegen->current_var_type[0] == '[') {
        const char *cvt = codegen->current_var_type;
        size_t cvt_len = strlen(cvt);
        if (cvt_len >= 3 && cvt[cvt_len - 1] == ']') {
            char inner[TYPE_NAME_MAX];
            size_t ilen = cvt_len - 2;
            if (ilen < sizeof(inner)) {
                memcpy(inner, cvt + 1, ilen);
                inner[ilen] = '\0';
                char *comma = strchr(inner, ',');
                if (comma) *comma = '\0';
                GrayType *it = type_from_name(inner);
                if (it && !is_bigint_type(inner) &&
                    (it->kind == TK_INT || it->kind == TK_UINT || it->kind == TK_BYTE))
                    c_type = gray_type_to_c_codegen(codegen, inner);
            }
        }
    }

    emit_formatted(codegen, "gray_array_from(gray_default_arena, (%s[]){", c_type);
    for (int i = 0; i < count; i++) {
        if (i > 0) emit(codegen, ", ");
        /* A bigint element array stores gray_i128/gray_i256 values, so every
         * scalar / integer literal element needs the matching constructor. */
        if (!emit_bigint_coerced(codegen, bi_elem, node->data.array_value.elements[i]))
            emit_expression(codegen, node->data.array_value.elements[i]);
    }
    emit_formatted(codegen, "}, sizeof(%s), %d)", c_type, count);
}

static void emit_map_value(CodeGen *codegen, AstNode *node) {
    /* Map literal: emit inline construction with gray_map_set calls.
     * We need a temp variable, so wrap in a GCC statement expression. */
    int count = node->data.map_value.count;

    /* Determine key/value C types. Prefer the enclosing var/field declared
     * type when available; byte/char literals are typechecked as int, so
     * first-pair inference would miss the declared key type. */
    const char *c_key_type = "GrayString";
    const char *c_val_type = "int64_t";
    GrayType *decl_mt = (codegen->current_var_type &&
                       strncmp(codegen->current_var_type, "map[", 4) == 0)
        ? type_from_name(codegen->current_var_type) : NULL;
    /* An empty literal has neither a pair to infer from nor, in an
     * assignment, an enclosing declared type. The typechecker records the
     * element types of the context on the node itself, so use those rather
     * than defaulting to string keys and 8-byte values. */
    if (!decl_mt && codegen->type_table) {
        GrayType *node_mt = typetable_get(codegen->type_table, node);
        if (node_mt && node_mt->kind == TK_MAP && node_mt->key_type && node_mt->value_type)
            decl_mt = node_mt;
    }
    /* Grayscale type names for the key/value slots, tracked so a wide-integer
     * literal element can be wrapped in its constructor below. */
    const char *gray_key_tn = NULL;
    const char *gray_val_tn = NULL;
    if (decl_mt && decl_mt->key_type) {
        c_key_type = gray_map_element_c_type(codegen, decl_mt->key_type);
        gray_key_tn = decl_mt->key_type;
    }
    if (decl_mt && decl_mt->value_type) {
        c_val_type = gray_map_element_c_type(codegen, decl_mt->value_type);
        gray_val_tn = decl_mt->value_type;
    }
    if (count > 0) {
        GrayType *kt = codegen->type_table ? typetable_get(codegen->type_table, node->data.map_value.keys[0]) : NULL;
        GrayType *vt = codegen->type_table ? typetable_get(codegen->type_table, node->data.map_value.values[0]) : NULL;
        if (!decl_mt && kt) {
            c_key_type = gray_map_element_c_type(codegen, type_name(kt));
            gray_key_tn = type_name(kt);
        }
        if (!decl_mt && vt && vt->kind == TK_POINTER) {
            static char map_ptr_buf[MSG_BUF_SIZE];
            const char *pointee = vt->element_type ? vt->element_type : "void";
            snprintf(map_ptr_buf, sizeof(map_ptr_buf), "%s *", gray_type_to_c_codegen(codegen, pointee));
            c_val_type = map_ptr_buf;
        } else if (!decl_mt && vt) {
            c_val_type = gray_map_element_c_type(codegen, type_name(vt));
            gray_val_tn = type_name(vt);
        }
    }

    /* Use GCC statement expression: ({ GrayMap m = ...; gray_map_set(...); m; })
     * Capture counter before emitting values; nested map literals will
     * re-enter this case and increment the counter, so each level gets
     * a unique temp name. */
    int my_counter = codegen_next_id(codegen);
    emit_formatted(codegen, "({ GrayMap _ml%d = gray_map_new_kind(gray_default_arena, sizeof(%s), sizeof(%s), %d, %s); ",
        my_counter, c_key_type, c_val_type, count > 4 ? count * 2 : 8,
        gray_map_key_kind_macro(c_key_type));

    /* For nested map values, propagate the inner type so inner literals
     * resolve their key/value C types correctly. */
    const char *inner_var_type = NULL;
    if (decl_mt && decl_mt->value_type &&
        strncmp(decl_mt->value_type, "map[", 4) == 0) {
        inner_var_type = decl_mt->value_type;
    }

    for (int i = 0; i < count; i++) {
        emit_formatted(codegen, "{ %s _mk = ", c_key_type);
        emit_map_slot_value(codegen, gray_key_tn, node->data.map_value.keys[i]);
        emit_formatted(codegen, "; %s _mv = ", c_val_type);
        if (inner_var_type) {
            const char *saved = codegen->current_var_type;
            codegen->current_var_type = inner_var_type;
            emit_expression(codegen, node->data.map_value.values[i]);
            codegen->current_var_type = saved;
        } else {
            emit_map_slot_value(codegen, gray_val_tn, node->data.map_value.values[i]);
        }
        emit_formatted(codegen, "; gray_map_set(gray_default_arena, &_ml%d, &_mk, &_mv, \"%s\", %d); } ", my_counter, codegen->file, node->token.line);
    }
    emit_formatted(codegen, "_ml%d; })", my_counter);
}

/* Did the literal give this field an explicit value? */
static bool struct_literal_specifies_field(AstNode *node, const char *field_name) {
    for (int i = 0; i < node->data.struct_value.count; i++) {
        if (strcmp(node->data.struct_value.field_names[i], field_name) == 0) return true;
    }
    return false;
}

static void emit_struct_value(CodeGen *codegen, AstNode *node) {
    /* Struct literal: (GrayStruct_Name){.field = value, ...} */
    /* Resolve ? → concrete binding for type params */
    const char *sname = node->data.struct_value.name;
    if (strcmp(sname, "?") == 0 && codegen->wildcard_binding) {
        sname = codegen->wildcard_binding;
    } else if (node->resolved_decl) {
        sname = module_mangle(codegen->modules, node->resolved_decl);
    } else {
        codegen_enter_node(codegen, node);
        sname = codegen_resolve_type(codegen, sname);
    }
    /* Resolve unprefixed struct names from 'import and use' */
    if (sname[0] >= 'A' && sname[0] <= 'Z') {
        bool found = false;
        for (int si = 0; si < codegen->struct_decl_count; si++) {
            if (strcmp(codegen->struct_decls[si]->data.struct_decl.name, sname) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            sname = codegen_resolve_type(codegen, sname);
        }
    }
    /* : use mangled name for generic struct instantiations */
    if (node->data.struct_value.wildcard_binding) {
        const char *binding = node->data.struct_value.wildcard_binding;
        char mangled[MSG_BUF_SIZE];
        mangle_generic_name(mangled, sizeof(mangled), sname, binding);
        emit_formatted(codegen, "(GrayStruct_%s){", mangled);
    } else {
        emit_formatted(codegen, "(GrayStruct_%s){", sname);
    }
    /* Look up the struct decl so we can thread each field's declared
     * type into emit_expression as current_var_type. Without it, an
     * empty array literal in a field slot (e.g. `Bag{items: {}}`)
     * has no type context and codegen falls back to sizeof(int64_t)
     * as the element size — which subsequent arrays.append() then
     * uses as the write stride, truncating struct elements. */
    AstNode *sdecl_for_fields = find_struct_declaration(codegen, sname);
    for (int i = 0; i < node->data.struct_value.count; i++) {
        if (i > 0) emit(codegen, ", ");
        const char *fname = node->data.struct_value.field_names[i];
        emit_formatted(codegen, ".%s = ", sanitize_name(fname));
        const char *field_type = NULL;
        if (sdecl_for_fields) {
            for (int field_index = 0; field_index < sdecl_for_fields->data.struct_decl.field_count; field_index++) {
                if (strcmp(sdecl_for_fields->data.struct_decl.fields[field_index].name, fname) == 0) {
                    field_type = sdecl_for_fields->data.struct_decl.fields[field_index].type_name;
                    break;
                }
            }
        }
        if (field_type) {
            const char *saved = codegen->current_var_type;
            codegen->current_var_type = field_type;
            if (!emit_bigint_coerced(codegen, field_type, node->data.struct_value.field_values[i]))
                emit_expression(codegen, node->data.struct_value.field_values[i]);
            codegen->current_var_type = saved;
        } else {
            emit_expression(codegen, node->data.struct_value.field_values[i]);
        }
    }
    /* Track whether an initialiser has been emitted so the separating comma
     * follows what was actually written, not the field's position. */
    bool emitted_field = (node->data.struct_value.count > 0);
    /* Emit default values for fields not specified in the literal.
     * Field defaults and omitted array/map element types are written as the
     * struct's own module spells them, so resolve them against that module
     * rather than the one the literal appears in — otherwise a cross-module
     * `pkg.Outer{}` emits an unmangled `GrayStruct_Inner`. */
    const char *saved_struct_module = codegen->current_module;
    const char *saved_struct_file = codegen->current_file;
    if (sdecl_for_fields) codegen_enter_node(codegen, sdecl_for_fields);
    if (sdecl_for_fields) {
        for (int field_index = 0; field_index < sdecl_for_fields->data.struct_decl.field_count; field_index++) {
            StructField *sf = &sdecl_for_fields->data.struct_decl.fields[field_index];
            if (!sf->default_value) continue;
            if (struct_literal_specifies_field(node, sf->name)) continue;
            if (emitted_field) emit(codegen, ", ");
            emitted_field = true;
            emit_formatted(codegen, ".%s = ", sanitize_name(sf->name));
            emit_expression(codegen, sf->default_value);
        }
        /* Map and array fields the literal leaves out still need a real
         * table. C zero-fills them, and a zero-filled GrayMap/GrayArray has
         * capacity and element sizes of 0, so inserts are silently dropped.
         * new(Type) initialises these for the same reason — see
         * emit_new_expr. */
        for (int field_index = 0; field_index < sdecl_for_fields->data.struct_decl.field_count; field_index++) {
            StructField *sf = &sdecl_for_fields->data.struct_decl.fields[field_index];
            const char *ftn = sf->type_name;
            if (!ftn || sf->default_value) continue;
            bool field_is_map = strncmp(ftn, "map[", 4) == 0;
            bool field_is_array = (ftn[0] == '[');
            /* A string-backed enum is a GrayString at the C level, so a
             * zero-filled field is an empty string, not a valid variant.
             * Seed it with the first variant, matching new(EnumType). */
            const char *senum = codegen_resolve_type(codegen, ftn);
            bool field_is_str_enum = codegen_enum_is_string(codegen, senum);
            if (!field_is_map && !field_is_array && !field_is_str_enum) continue;
            if (struct_literal_specifies_field(node, sf->name)) continue;
            if (emitted_field) emit(codegen, ", ");
            emitted_field = true;
            emit_formatted(codegen, ".%s = ", sanitize_name(sf->name));
            if (field_is_str_enum) {
                int eidx = codegen_enum_index(codegen, senum);
                const char *fv = codegen->enum_decls[eidx]->data.enum_decl.values[0].name;
                emit_formatted(codegen, "GrayEnum_%s_%s", senum, fv);
                continue;
            }
            GrayType *ft = type_from_name(ftn);
            if (field_is_map) {
                const char *c_kt = "GrayString";
                const char *c_vt = "int64_t";
                if (ft && ft->key_type) c_kt = gray_map_element_c_type(codegen, ft->key_type);
                if (ft && ft->value_type) c_vt = gray_map_element_c_type(codegen, ft->value_type);
                emit_formatted(codegen, "gray_map_new_kind(gray_default_arena, sizeof(%s), sizeof(%s), 8, %s)",
                    c_kt, c_vt, gray_map_key_kind_macro(c_kt));
            } else {
                const char *c_elem = "int64_t";
                if (ft && ft->element_type) c_elem = gray_map_element_c_type(codegen, ft->element_type);
                emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), 4)", c_elem);
            }
        }
    }
    codegen->current_module = saved_struct_module;
    codegen->current_file = saved_struct_file;
    emit(codegen, "}");
}

static void emit_prefix_expr(CodeGen *codegen, AstNode *node) {
    /* For negation of int literals that are already negative (e.g. parser
     * stored -9223372036854775808 as the literal), emit directly.
     * Special case INT64_MIN to avoid C literal overflow warning. */
    if (node->data.prefix.op == TOK_MINUS &&
        node->data.prefix.right->kind == NODE_INT_VALUE &&
        node->data.prefix.right->data.int_value.value < 0) {
        int64_t v = node->data.prefix.right->data.int_value.value;
        if (v == INT64_MIN) {
            emit(codegen, "(-9223372036854775807LL - 1)");
        } else {
            emit_formatted(codegen, "(%lldLL)", (long long)v);
        }
        return;
    }
    /* Bigint negation */
    if (node->data.prefix.op == TOK_MINUS) {
        const char *bi_type = resolve_bigint_type(codegen, node->data.prefix.right);
        if (bi_type && (strcmp(bi_type, "i128") == 0 || strcmp(bi_type, "i256") == 0)) {
            emit_formatted(codegen, "%s_neg(", bigint_prefix(bi_type));
            emit_expression(codegen, node->data.prefix.right);
            emit(codegen, ")");
            return;
        }
    }
    /* Overflow-checked negation for signed integer types; STANDARD §3.1.1
     * promises arithmetic panics rather than silent wrap on overflow. */
    if (node->data.prefix.op == TOK_MINUS) {
        GrayType *ot = codegen->type_table ? typetable_get(codegen->type_table, node->data.prefix.right) : NULL;
        if (ot && ot->kind == TK_INT) {
            const char *sized_name = ot->name;
            const char *smin = NULL, *smax = NULL;
            bool _su = false;
            if (sized_name) sized_int_bounds(sized_name, &smin, &smax, &_su);
            if (smax && !_su) {
                emit(codegen, "gray_sized_neg_check(");
                emit_expression(codegen, node->data.prefix.right);
                emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d)", smin, smax, sized_name, codegen->file, node->token.line);
            } else {
                emit(codegen, "gray_neg_check(");
                emit_expression(codegen, node->data.prefix.right);
                emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
            }
            return;
        }
    }
    /* bit_not → ~ ; operands narrower than C's int (byte, u8, u16) must be
     * masked back to their width because C promotes them to int before
     * applying ~, yielding a negative value that fails the runtime range check. */
    if (node->data.prefix.op == TOK_BIT_NOT) {
        GrayType *bn_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.prefix.right) : NULL;
        const char *bn_mask = NULL;
        if (bn_t && bn_t->kind == TK_BYTE) {
            bn_mask = "uint8_t";
        } else if (bn_t && bn_t->name) {
            if (strcmp(bn_t->name, "u8") == 0)  bn_mask = "uint8_t";
            else if (strcmp(bn_t->name, "u16") == 0) bn_mask = "uint16_t";
        }
        if (bn_mask) {
            emit_formatted(codegen, "((%s)(~(", bn_mask);
            emit_expression(codegen, node->data.prefix.right);
            emit(codegen, ")))");
        } else {
            emit(codegen, "(~(");
            emit_expression(codegen, node->data.prefix.right);
            emit(codegen, "))");
        }
        return;
    }
    emit(codegen, "(");
    emit(codegen, operator_to_c_string(node->data.prefix.op));
    if (node->data.prefix.op == TOK_MINUS &&
        node->data.prefix.right->kind == NODE_INT_VALUE) {
        emit(codegen, " ");
    }
    /* Wrap infix operands in parens so !(a && b) emits as (!(a && b)) not (!a && b) */
    bool wrap = (node->data.prefix.right->kind == NODE_INFIX_EXPR);
    if (wrap) emit(codegen, "(");
    emit_expression(codegen, node->data.prefix.right);
    if (wrap) emit(codegen, ")");
    emit(codegen, ")");
}

static void emit_infix_expr(CodeGen *codegen, AstNode *node) {
    TokenType op = node->data.infix.op;

    /* Check if either operand is a string; need special handling */
    GrayType *left_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.infix.left) : NULL;
    GrayType *right_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.infix.right) : NULL;
    /* Inside a generic instantiation, operands that were
     * typed TK_UNKNOWN in the main pass (because they traced back
     * to a '?' parameter) should be treated as the active wildcard
     * binding so string/struct comparisons pick up the right path. */
    if (codegen && codegen->wildcard_binding) {
        static GrayType wildcard_type_static;
        GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
        if (wildcard_type) { wildcard_type_static = *wildcard_type; wildcard_type = &wildcard_type_static; }
        if (!left_type || left_type->kind == TK_UNKNOWN) left_type = wildcard_type;
        if (!right_type || right_type->kind == TK_UNKNOWN) right_type = wildcard_type;
    }
    bool left_is_str = (left_type && left_type->kind == TK_STRING) || node->data.infix.left->kind == NODE_STRING_VALUE;
    bool right_is_str = (right_type && right_type->kind == TK_STRING) || node->data.infix.right->kind == NODE_STRING_VALUE;
    /* Also treat string enum operands as strings for comparison purposes */
    if (left_type && left_type->kind == TK_ENUM && left_type->name &&
        codegen_enum_is_string(codegen, left_type->name)) left_is_str = true;
    if (right_type && right_type->kind == TK_ENUM && right_type->name &&
        codegen_enum_is_string(codegen, right_type->name)) right_is_str = true;

    if ((left_is_str || right_is_str) && op == TOK_EQ) {
        emit(codegen, "gray_string_eq(");
        emit_expression(codegen, node->data.infix.left);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }
    if ((left_is_str || right_is_str) && op == TOK_NOT_EQ) {
        emit(codegen, "!gray_string_eq(");
        emit_expression(codegen, node->data.infix.left);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }
    /* string + string: concatenate into a new GrayString. Chained
     * a + b + c nests naturally since '+' is left-associative. */
    if (left_is_str && right_is_str && op == TOK_PLUS) {
        emit(codegen, "gray_string_concat(gray_default_arena, ");
        emit_expression(codegen, node->data.infix.left);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }

    /* Bitwise keyword operators → C bitwise operators */
    if (op == TOK_BIT_AND || op == TOK_BIT_OR || op == TOK_BIT_XOR) {
        const char *c_op = operator_to_c_string(op);
        emit(codegen, "(");
        emit_expression(codegen, node->data.infix.left);
        emit_formatted(codegen, " %s ", c_op);
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }
    /* Bit shift operators with runtime bounds check.
     * A shift amount that is negative or >= 64 is undefined behavior
     * in C. Capture the amount once, validate it, then shift. */
    if (op == TOK_BIT_SHIFT_LEFT || op == TOK_BIT_SHIFT_RIGHT) {
        const char *c_op = operator_to_c_string(op);
        bool left_is_literal = node->data.infix.left->kind == NODE_INT_VALUE;
        emit(codegen, "({ int64_t _sa = (int64_t)(");
        emit_expression(codegen, node->data.infix.right);
        emit_formatted(codegen, "); if (_sa < 0 || _sa >= 64) { gray_panic_code_at(\"%s\", %d, \"P0092\", \"shift amount %%lld is out of range; must be in [0, 63]\", (long long)_sa); } (", codegen->file, node->token.line);
        if (left_is_literal) emit(codegen, "(int64_t)");
        emit_expression(codegen, node->data.infix.left);
        emit_formatted(codegen, ") %s (int)_sa; })", c_op);
        return;
    }

    /* in / not_in; array or range membership check */
    if (op == TOK_IN || op == TOK_NOT_IN) {
        bool negated = (op == TOK_NOT_IN);

        /* Check if right side is a range expression: x in range(a, b) */
        if (node->data.infix.right->kind == NODE_RANGE_EXPR) {
            AstNode *range = node->data.infix.right;
            if (negated) emit(codegen, "!(");
            emit(codegen, "(");
            emit_expression(codegen, node->data.infix.left);
            emit(codegen, " >= ");
            if (range->data.range_expr.start) {
                emit_expression(codegen, range->data.range_expr.start);
            } else {
                emit(codegen, "0");
            }
            emit(codegen, " && ");
            emit_expression(codegen, node->data.infix.left);
            emit(codegen, " < ");
            emit_expression(codegen, range->data.range_expr.end);
            /* Step check: value must be at a step interval from start */
            if (range->data.range_expr.step) {
                emit(codegen, " && (");
                emit_expression(codegen, node->data.infix.left);
                emit(codegen, " - ");
                if (range->data.range_expr.start) {
                    emit_expression(codegen, range->data.range_expr.start);
                } else {
                    emit(codegen, "0");
                }
                emit(codegen, ") % ");
                emit_expression(codegen, range->data.range_expr.step);
                emit(codegen, " == 0");
            }
            emit(codegen, ")");
            if (negated) emit(codegen, ")");
            return;
        }

        /* Map or array membership */
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.infix.right) : NULL;
        /* Map membership: key in map → gray_maps_has_key
         * Bind the map to a temp so &_im works even when the map
         * expression is an rvalue (e.g. pointer field access). */
        if (arr_t && arr_t->kind == TK_MAP) {
            codegen->needs_maps_h = true;
            int mid = codegen_next_id(codegen);
            if (negated) emit(codegen, "!");
            emit_formatted(codegen, "({ __auto_type _im%d = ", mid);
            emit_expression(codegen, node->data.infix.right);
            emit_formatted(codegen, "; %s _ik%d = ", gray_map_element_c_type(codegen, arr_t->key_type), mid);
            emit_map_slot_value(codegen, arr_t->key_type, node->data.infix.left);
            emit_formatted(codegen, "; gray_maps_has_key(&_im%d, &_ik%d); })", mid, mid);
            return;
        }
        /* String membership: char in string or string in string */
        if (arr_t && arr_t->kind == TK_STRING) {
            GrayType *left_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.infix.left) : NULL;
            if (left_t && left_t->kind == TK_CHAR) {
                /* char in string → memchr scan */
                if (negated) emit(codegen, "!");
                emit(codegen, "(memchr(");
                emit_expression(codegen, node->data.infix.right);
                emit(codegen, ".data, ");
                emit_expression(codegen, node->data.infix.left);
                emit(codegen, ", (size_t)");
                emit_expression(codegen, node->data.infix.right);
                emit(codegen, ".len) != NULL)");
            } else {
                /* string in string → substring check */
                codegen->needs_strings_h = true;
                if (negated) emit(codegen, "!");
                emit(codegen, "gray_strings_contains(");
                emit_expression(codegen, node->data.infix.right);
                emit(codegen, ", ");
                emit_expression(codegen, node->data.infix.left);
                emit(codegen, ")");
            }
            return;
        }
        if (negated) emit(codegen, "!");
        {
            codegen->needs_arrays_h = true;
            const char *contains_fn = "gray_arrays_contains_int";
            if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type) {
                if (strcmp(arr_t->element_type, "string") == 0)
                    contains_fn = "gray_arrays_contains_str";
                else if (strcmp(arr_t->element_type, "char") == 0)
                    contains_fn = "gray_arrays_contains_char";
                else if (strcmp(arr_t->element_type, "byte") == 0)
                    contains_fn = "gray_arrays_contains_byte";
                else if (strcmp(arr_t->element_type, "float") == 0 ||
                         strcmp(arr_t->element_type, "f32") == 0 ||
                         strcmp(arr_t->element_type, "f64") == 0)
                    contains_fn = "gray_arrays_contains_float";
            }
            emit_formatted(codegen, "%s(&", contains_fn);
            emit_expression(codegen, node->data.infix.right);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.infix.left);
            emit(codegen, ")");
        }
        return;
    }

    /* Helper: emit an operand in bigint context.
     * Wraps integer literals and non-bigint variables with the appropriate
     * from_i64/from_u64 constructor so mixed-width expressions like
     * i128 + i64 pass correctly to the bigint arithmetic helpers. */
    #define EMIT_BIGINT_OPERAND(codegen, operand, pfx, bi_type, operand_type) \
        emit_bigint_operand((codegen), (operand), (pfx), (bi_type), (operand_type))

    /* Bigint infix; emit function calls instead of C operators.
     * Must come before overflow-check and div-by-zero handlers since
     * bigint types share TK_INT/TK_UINT kind. */
    {
        const char *bi_type = resolve_bigint_type(codegen, node->data.infix.left);
        if (!bi_type) bi_type = resolve_bigint_type(codegen, node->data.infix.right);
        if (bi_type) {
            const char *pfx = bigint_prefix(bi_type);
            const char *fn_op = NULL;
            if (op == TOK_PLUS) fn_op = "add";
            else if (op == TOK_MINUS) fn_op = "sub";
            else if (op == TOK_ASTERISK) fn_op = "mul";
            else if (op == TOK_SLASH) fn_op = "div";
            else if (op == TOK_PERCENT) fn_op = "mod";
            else if (op == TOK_EQ) fn_op = "eq";
            else if (op == TOK_NOT_EQ) fn_op = "ne";
            else if (op == TOK_LT) fn_op = "lt";
            else if (op == TOK_GT) fn_op = "gt";
            else if (op == TOK_LT_EQ) fn_op = "le";
            else if (op == TOK_GT_EQ) fn_op = "ge";
            if (fn_op) {
                bool is_checked = (strcmp(fn_op, "add") == 0 || strcmp(fn_op, "sub") == 0 || strcmp(fn_op, "mul") == 0);
                bool needs_loc = (strcmp(fn_op, "div") == 0 || strcmp(fn_op, "mod") == 0);
                if (is_checked) {
                    emit_formatted(codegen, "%s_%s_checked(", pfx, fn_op);
                } else {
                    emit_formatted(codegen, "%s_%s(", pfx, fn_op);
                }
                EMIT_BIGINT_OPERAND(codegen, node->data.infix.left, pfx, bi_type, left_type);
                emit(codegen, ", ");
                EMIT_BIGINT_OPERAND(codegen, node->data.infix.right, pfx, bi_type, right_type);
                if (is_checked || needs_loc) {
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                } else {
                    emit(codegen, ")");
                }
                return;
            }
        }
    }

    /* Runtime division/modulo by zero check.
     * GNU statement expressions are not valid as C file-scope initializers,
     * so skip the runtime check when inside a const declaration. */
    if (!codegen->in_const_decl && (op == TOK_SLASH || op == TOK_PERCENT)) {
        bool is_float_div = (left_type && left_type->kind == TK_FLOAT) || (right_type && right_type->kind == TK_FLOAT);
        if (is_float_div) {
            /* Float division: check for zero (Grayscale panics, no IEEE 754 inf) */
            emit(codegen, "({ double _dv = (double)");
            emit_expression(codegen, node->data.infix.right);
            emit_formatted(codegen, "; if (_dv == 0.0) { gray_panic_code_at(\"%s\", %d, \"P0078\", \"division by zero\"); } (double)(", codegen->file, node->token.line);
            emit_expression(codegen, node->data.infix.left);
            emit_formatted(codegen, ") %s _dv; })", operator_to_c_string(op));
            return;
        } else {
            /* For signed integer division, also guard the TYPE_MIN / -1
             * case; that quotient overflows (it's |TYPE_MIN| which has
             * no positive representation), and in C it's UB. */
            bool is_signed = (left_type && left_type->kind == TK_INT) || (right_type && right_type->kind == TK_INT);
            const char *signed_min = NULL;
            if (is_signed) {
                const char *sized_name = (left_type && left_type->name) ? left_type->name : ((right_type && right_type->name) ? right_type->name : NULL);
                const char *_unused_max; bool _unused_u;
                if (!sized_name || !sized_int_bounds(sized_name, &signed_min, &_unused_max, &_unused_u))
                    signed_min = "(-9223372036854775807LL - 1)";
            }
            emit(codegen, "({ __auto_type _dv = ");
            emit_expression(codegen, node->data.infix.right);
            emit_formatted(codegen, "; if (!_dv) { gray_panic_code_at(\"%s\", %d, \"P0078\", \"division by zero\"); } ", codegen->file, node->token.line);
            if (is_signed) {
                const char *opname = (op == TOK_SLASH) ? "division" : "modulo";
                emit(codegen, "__auto_type _dn = ");
                emit_expression(codegen, node->data.infix.left);
                emit_formatted(codegen, "; if ((int64_t)_dn == %s && _dv == -1) { gray_panic_code_at(\"%s\", %d, \"P0079\", \"%s result is too large; value exceeds the range of this type\"); } _dn %s _dv; })",
                    signed_min, codegen->file, node->token.line, opname, operator_to_c_string(op));
            } else {
                emit(codegen, "(");
                emit_expression(codegen, node->data.infix.left);
                emit_formatted(codegen, ") %s _dv; })", operator_to_c_string(op));
            }
            return;
        }
    }

    /* Overflow-checked integer arithmetic for +, -, * */
    {
        bool left_is_int = (left_type && (left_type->kind == TK_INT || left_type->kind == TK_UINT || left_type->kind == TK_BYTE || left_type->kind == TK_CHAR));
        bool right_is_int = (right_type && (right_type->kind == TK_INT || right_type->kind == TK_UINT || right_type->kind == TK_BYTE || right_type->kind == TK_CHAR));
        bool left_is_float = (left_type && left_type->kind == TK_FLOAT);
        bool right_is_float = (right_type && right_type->kind == TK_FLOAT);
        bool is_arith = (op == TOK_PLUS || op == TOK_MINUS || op == TOK_ASTERISK);

        if (is_arith && left_is_int && right_is_int && !left_is_float && !right_is_float) {
            /* Check for sized types that need bounds-checked arithmetic.
             * For two bounded types (rank 1-3) use the wider one so e.g.
             * i8 + i16 evaluates in i16 space.  When one side is bounded
             * and the other is a raw int/uint (rank 4), prefer the bounded
             * type — a negative int added to a byte must fire P0016, not
             * silently bypass the byte check. */
            const char *left_name = (left_type && left_type->name) ? left_type->name : NULL;
            const char *right_name = (right_type && right_type->name) ? right_type->name : NULL;
            bool left_bounded = (int_type_rank(left_name) >= 1 && int_type_rank(left_name) <= 3);
            bool right_bounded = (int_type_rank(right_name) >= 1 && int_type_rank(right_name) <= 3);
            const char *sized_name;
            if (left_bounded && right_bounded)
                sized_name = (int_type_rank(right_name) > int_type_rank(left_name)) ? right_name : left_name;
            else if (left_bounded)
                sized_name = left_name;
            else if (right_bounded)
                sized_name = right_name;
            else
                sized_name = (int_type_rank(right_name) > int_type_rank(left_name)) ? right_name : (left_name ? left_name : right_name);
            const char *sized_min = NULL, *sized_max = NULL;
            bool sized_unsigned = false;
            if (sized_name) sized_int_bounds(sized_name, &sized_min, &sized_max, &sized_unsigned);

            if (sized_max) {
                /* Sized type; use bounds-checked arithmetic */
                const char *operator_function_name = NULL;
                if (sized_unsigned) {
                    if (op == TOK_PLUS) operator_function_name = "gray_usized_add_check";
                    else if (op == TOK_MINUS) operator_function_name = "gray_usized_sub_check";
                    else if (op == TOK_ASTERISK) operator_function_name = "gray_usized_mul_check";
                } else {
                    if (op == TOK_PLUS) operator_function_name = "gray_sized_add_check";
                    else if (op == TOK_MINUS) operator_function_name = "gray_sized_sub_check";
                    else if (op == TOK_ASTERISK) operator_function_name = "gray_sized_mul_check";
                }
                if (operator_function_name && !codegen->in_const_decl) {
                    emit_formatted(codegen, "%s(", operator_function_name);
                    emit_expression(codegen, node->data.infix.left);
                    emit(codegen, ", ");
                    emit_expression(codegen, node->data.infix.right);
                    if (sized_unsigned) {
                        emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d)", sized_max, sized_name, codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d)", sized_min, sized_max, sized_name, codegen->file, node->token.line);
                    }
                    return;
                }
            }

            /* 64-bit overflow checks for int/uint/i64/u64 */
            bool is_unsigned = (left_type && left_type->kind == TK_UINT) || (right_type && right_type->kind == TK_UINT);
            const char *function_name = NULL;
            if (is_unsigned) {
                if (op == TOK_PLUS) function_name = "gray_uadd_check";
                else if (op == TOK_MINUS) function_name = "gray_usub_check";
                else if (op == TOK_ASTERISK) function_name = "gray_umul_check";
            } else {
                if (op == TOK_PLUS) function_name = "gray_add_check";
                else if (op == TOK_MINUS) function_name = "gray_sub_check";
                else if (op == TOK_ASTERISK) function_name = "gray_mul_check";
            }
            if (function_name && !codegen->in_const_decl) {
                emit_formatted(codegen, "%s(", function_name);
                emit_expression(codegen, node->data.infix.left);
                emit(codegen, ", ");
                emit_expression(codegen, node->data.infix.right);
                emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                return;
            }
        }
    }

    /* Normal infix; always wrap sub-infix expressions in parens to
     * preserve the precedence the parser established via the AST shape.
     * Without this, (x + y) * z would emit as x + y * z. */
    bool l_infix = (node->data.infix.left->kind == NODE_INFIX_EXPR);
    bool r_infix = (node->data.infix.right->kind == NODE_INFIX_EXPR);
    if (l_infix) emit(codegen, "(");
    emit_expression(codegen, node->data.infix.left);
    if (l_infix) emit(codegen, ")");
    emit_formatted(codegen, " %s ", operator_to_c_string(op));
    if (r_infix) emit(codegen, "(");
    emit_expression(codegen, node->data.infix.right);
    if (r_infix) emit(codegen, ")");
}

static void emit_postfix_expr(CodeGen *codegen, AstNode *node) {
    if (node->data.postfix.op == TOK_CARET) {
        AstNode *_dp_left = node->data.postfix.left;
        bool is_raw_deref = (_dp_left->kind == NODE_LABEL &&
                             is_raw_variable(codegen, _dp_left->data.label.value));
        if (!is_raw_deref && _dp_left->kind == NODE_CALL_EXPR &&
            _dp_left->data.call.function->kind == NODE_LABEL &&
            strcmp(_dp_left->data.call.function->data.label.value, "raw") == 0) {
            is_raw_deref = true;
        }
        if (is_raw_deref) {
            /* Raw pointer: bare dereference, no nil check */
            emit(codegen, "(*");
            emit_expression(codegen, _dp_left);
            emit(codegen, ")");
        } else {
            /* Pointer dereference: p^ → (*p) with nil check. Route through
             * gray_ptr_check so `*p` stays an lvalue — a following `.field`
             * chain or `&` then lands on the real storage rather than a
             * by-value statement-expression copy. */
            GrayType *deref_t = codegen->type_table
                ? typetable_get(codegen->type_table, _dp_left) : NULL;
            const char *deref_pointee = (deref_t && deref_t->kind == TK_POINTER)
                ? deref_t->element_type : NULL;
            if (deref_pointee && deref_pointee[0] != '^' && !strchr(deref_pointee, '?')) {
                char c_pointee[TYPE_NAME_MAX];
                snprintf(c_pointee, sizeof(c_pointee), "%s",
                    gray_type_to_c_codegen(codegen, deref_pointee));
                emit_formatted(codegen, "(*(%s *)gray_ptr_check((void *)(", c_pointee);
                emit_expression(codegen, _dp_left);
                emit_formatted(codegen, "), \"%s\", %d))", codegen->file, node->token.line);
            } else {
                emit(codegen, "({ __auto_type _dp = ");
                emit_expression(codegen, _dp_left);
                emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } *_dp; })", codegen->file, node->token.line);
            }
        }
    } else if (node->data.postfix.op == TOK_INCREMENT) {
        /* Overflow-checked increment; sized types need bounds check */
        GrayType *postfix_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.postfix.left) : NULL;
        const char *sized_name = (postfix_type && postfix_type->name) ? postfix_type->name : NULL;
        const char *smin = NULL, *smax = NULL;
        bool su = false;
        bool is_uint = postfix_type && postfix_type->kind == TK_UINT;
        if (sized_name) sized_int_bounds(sized_name, &smin, &smax, &su);
        /* Pointer dereference target: p^++ needs lvalue-safe nil-check block */
        bool _inc_deref = (node->data.postfix.left->kind == NODE_POSTFIX_EXPR &&
                           node->data.postfix.left->data.postfix.op == TOK_CARET);
        if (_inc_deref) {
            AstNode *ptr_node = node->data.postfix.left->data.postfix.left;
            bool _raw = (ptr_node->kind == NODE_LABEL && is_raw_variable(codegen, ptr_node->data.label.value));
            emit(codegen, "({ __auto_type _dp = ");
            emit_expression(codegen, ptr_node);
            emit(codegen, "; ");
            if (!_raw) {
                emit_formatted(codegen, "if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ",
                    codegen->file, node->token.line);
            }
            if (smax) {
                if (su) {
                    emit(codegen, "*_dp = gray_usized_add_check(*_dp");
                    emit_formatted(codegen, ", 1, %s, \"%s\", \"%s\", %d); })", smax, sized_name, codegen->file, node->token.line);
                } else {
                    emit(codegen, "*_dp = gray_sized_add_check(*_dp");
                    emit_formatted(codegen, ", 1, %s, %s, \"%s\", \"%s\", %d); })", smin, smax, sized_name, codegen->file, node->token.line);
                }
            } else if (is_uint) {
                emit(codegen, "*_dp = gray_uadd_check(*_dp");
                emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
            } else {
                emit(codegen, "*_dp = gray_add_check(*_dp");
                emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
            }
        } else {
        /* Pointer member field: c.val++ where c is ^Struct, or c^.val++ */
        bool _inc_member_ptr = false;
        if (node->data.postfix.left->kind == NODE_MEMBER_EXPR) {
            AstNode *_mobj = node->data.postfix.left->data.member.object;
            GrayType *_mot = codegen->type_table ? typetable_get(codegen->type_table, _mobj) : NULL;
            bool _is_ref = (_mobj->kind == NODE_LABEL && is_reference_variable(codegen, _mobj->data.label.value));
            /* Also handle explicit deref: c^.val++ where object is NODE_POSTFIX_EXPR with TOK_CARET */
            bool _is_explicit_deref = (_mobj->kind == NODE_POSTFIX_EXPR && _mobj->data.postfix.op == TOK_CARET);
            if (_is_explicit_deref) {
                _mobj = _mobj->data.postfix.left; /* unwrap to the pointer itself */
                _mot = codegen->type_table ? typetable_get(codegen->type_table, _mobj) : NULL;
            }
            if (!_is_ref && ((_mot && _mot->kind == TK_POINTER) || _is_explicit_deref)) {
                _inc_member_ptr = true;
                const char *_fld = sanitize_name(node->data.postfix.left->data.member.member);
                bool _raw = (_mobj->kind == NODE_LABEL && is_raw_variable(codegen, _mobj->data.label.value));
                emit(codegen, "({ __auto_type _dp = ");
                emit_expression(codegen, _mobj);
                emit(codegen, "; ");
                if (!_raw) {
                    emit_formatted(codegen, "if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ",
                        codegen->file, node->token.line);
                }
                if (smax) {
                    if (su) {
                        emit_formatted(codegen, "_dp->%s = gray_usized_add_check(_dp->%s", _fld, _fld);
                        emit_formatted(codegen, ", 1, %s, \"%s\", \"%s\", %d); })", smax, sized_name, codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, "_dp->%s = gray_sized_add_check(_dp->%s", _fld, _fld);
                        emit_formatted(codegen, ", 1, %s, %s, \"%s\", \"%s\", %d); })", smin, smax, sized_name, codegen->file, node->token.line);
                    }
                } else if (is_uint) {
                    emit_formatted(codegen, "_dp->%s = gray_uadd_check(_dp->%s", _fld, _fld);
                    emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
                } else {
                    emit_formatted(codegen, "_dp->%s = gray_add_check(_dp->%s", _fld, _fld);
                    emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
                }
            }
        }
        if (!_inc_member_ptr) {
        emit(codegen, "(");
        emit_expression(codegen, node->data.postfix.left);
        if (smax) {
            if (su) {
                emit(codegen, " = gray_usized_add_check(");
                emit_expression(codegen, node->data.postfix.left);
                emit_formatted(codegen, ", 1, %s, \"%s\", \"%s\", %d))", smax, sized_name, codegen->file, node->token.line);
            } else {
                emit(codegen, " = gray_sized_add_check(");
                emit_expression(codegen, node->data.postfix.left);
                emit_formatted(codegen, ", 1, %s, %s, \"%s\", \"%s\", %d))", smin, smax, sized_name, codegen->file, node->token.line);
            }
        } else if (is_uint) {
            emit(codegen, " = gray_uadd_check(");
            emit_expression(codegen, node->data.postfix.left);
            emit_formatted(codegen, ", 1, \"%s\", %d))", codegen->file, node->token.line);
        } else {
            emit(codegen, " = gray_add_check(");
            emit_expression(codegen, node->data.postfix.left);
            emit_formatted(codegen, ", 1, \"%s\", %d))", codegen->file, node->token.line);
        }
        }
        }
    } else if (node->data.postfix.op == TOK_DECREMENT) {
        /* Overflow-checked decrement; sized types need bounds check */
        GrayType *pt = codegen->type_table ? typetable_get(codegen->type_table, node->data.postfix.left) : NULL;
        const char *sn = (pt && pt->name) ? pt->name : NULL;
        const char *smin = NULL, *smax = NULL;
        bool su = false;
        bool is_uint = pt && pt->kind == TK_UINT;
        if (sn) sized_int_bounds(sn, &smin, &smax, &su);
        /* Pointer dereference target: p^-- needs lvalue-safe nil-check block */
        bool _dec_deref = (node->data.postfix.left->kind == NODE_POSTFIX_EXPR &&
                           node->data.postfix.left->data.postfix.op == TOK_CARET);
        if (_dec_deref) {
            AstNode *ptr_node = node->data.postfix.left->data.postfix.left;
            bool _raw = (ptr_node->kind == NODE_LABEL && is_raw_variable(codegen, ptr_node->data.label.value));
            emit(codegen, "({ __auto_type _dp = ");
            emit_expression(codegen, ptr_node);
            emit(codegen, "; ");
            if (!_raw) {
                emit_formatted(codegen, "if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ",
                    codegen->file, node->token.line);
            }
            if (smax) {
                if (su) {
                    emit(codegen, "*_dp = gray_usized_sub_check(*_dp");
                    emit_formatted(codegen, ", 1, %s, \"%s\", \"%s\", %d); })", smax, sn, codegen->file, node->token.line);
                } else {
                    emit(codegen, "*_dp = gray_sized_sub_check(*_dp");
                    emit_formatted(codegen, ", 1, %s, %s, \"%s\", \"%s\", %d); })", smin, smax, sn, codegen->file, node->token.line);
                }
            } else if (is_uint) {
                emit(codegen, "*_dp = gray_usub_check(*_dp");
                emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
            } else {
                emit(codegen, "*_dp = gray_sub_check(*_dp");
                emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
            }
        } else {
        /* Pointer member field: c.val-- where c is ^Struct, or c^.val-- */
        bool _dec_member_ptr = false;
        if (node->data.postfix.left->kind == NODE_MEMBER_EXPR) {
            AstNode *_mobj = node->data.postfix.left->data.member.object;
            GrayType *_mot = codegen->type_table ? typetable_get(codegen->type_table, _mobj) : NULL;
            bool _is_ref = (_mobj->kind == NODE_LABEL && is_reference_variable(codegen, _mobj->data.label.value));
            /* Also handle explicit deref: c^.val-- where object is NODE_POSTFIX_EXPR with TOK_CARET */
            bool _is_explicit_deref = (_mobj->kind == NODE_POSTFIX_EXPR && _mobj->data.postfix.op == TOK_CARET);
            if (_is_explicit_deref) {
                _mobj = _mobj->data.postfix.left;
                _mot = codegen->type_table ? typetable_get(codegen->type_table, _mobj) : NULL;
            }
            if (!_is_ref && ((_mot && _mot->kind == TK_POINTER) || _is_explicit_deref)) {
                _dec_member_ptr = true;
                const char *_fld = sanitize_name(node->data.postfix.left->data.member.member);
                bool _raw = (_mobj->kind == NODE_LABEL && is_raw_variable(codegen, _mobj->data.label.value));
                emit(codegen, "({ __auto_type _dp = ");
                emit_expression(codegen, _mobj);
                emit(codegen, "; ");
                if (!_raw) {
                    emit_formatted(codegen, "if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ",
                        codegen->file, node->token.line);
                }
                if (smax) {
                    if (su) {
                        emit_formatted(codegen, "_dp->%s = gray_usized_sub_check(_dp->%s", _fld, _fld);
                        emit_formatted(codegen, ", 1, %s, \"%s\", \"%s\", %d); })", smax, sn, codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, "_dp->%s = gray_sized_sub_check(_dp->%s", _fld, _fld);
                        emit_formatted(codegen, ", 1, %s, %s, \"%s\", \"%s\", %d); })", smin, smax, sn, codegen->file, node->token.line);
                    }
                } else if (is_uint) {
                    emit_formatted(codegen, "_dp->%s = gray_usub_check(_dp->%s", _fld, _fld);
                    emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
                } else {
                    emit_formatted(codegen, "_dp->%s = gray_sub_check(_dp->%s", _fld, _fld);
                    emit_formatted(codegen, ", 1, \"%s\", %d); })", codegen->file, node->token.line);
                }
            }
        }
        if (!_dec_member_ptr) {
        emit(codegen, "(");
        emit_expression(codegen, node->data.postfix.left);
        if (smax) {
            if (su) {
                emit(codegen, " = gray_usized_sub_check(");
                emit_expression(codegen, node->data.postfix.left);
                emit_formatted(codegen, ", 1, %s, \"%s\", \"%s\", %d))", smax, sn, codegen->file, node->token.line);
            } else {
                emit(codegen, " = gray_sized_sub_check(");
                emit_expression(codegen, node->data.postfix.left);
                emit_formatted(codegen, ", 1, %s, %s, \"%s\", \"%s\", %d))", smin, smax, sn, codegen->file, node->token.line);
            }
        } else if (is_uint) {
            emit(codegen, " = gray_usub_check(");
            emit_expression(codegen, node->data.postfix.left);
            emit_formatted(codegen, ", 1, \"%s\", %d))", codegen->file, node->token.line);
        } else {
            emit(codegen, " = gray_sub_check(");
            emit_expression(codegen, node->data.postfix.left);
            emit_formatted(codegen, ", 1, \"%s\", %d))", codegen->file, node->token.line);
        }
        }
        }
    } else {
        emit_expression(codegen, node->data.postfix.left);
        emit(codegen, operator_to_c_string(node->data.postfix.op));
    }
}

static void emit_func_ref(CodeGen *codegen, AstNode *node) {
    /* ()func_name or ()Type.func; emit as C function pointer */
    codegen_enter_node(codegen, node);
    if (node->data.func_ref.function->kind == NODE_LABEL) {
        /* The referenced function is named as written, so it resolves like
         * any other reference — taking a reference to a function inside its
         * own module used to emit the unmangled symbol. */
        emit(codegen, "gray_fn_");
        emit(codegen, codegen_resolve_ref(codegen, node->data.func_ref.function,
            node->data.func_ref.function->data.label.value));
    } else if (node->data.func_ref.function->kind == NODE_MEMBER_EXPR) {
        /* ()StructName.funcName → gray_fn_StructName_funcName */
        AstNode *mem = node->data.func_ref.function;
        const char *qualifier = ast_member_qualifier(mem);
        if (mem->resolved_decl && mem->resolved_decl->kind == DECL_FUNC) {
            /* mod.func — the whole qualified name resolved to the function,
             * so its declaration names it outright. */
            emit_formatted(codegen, "gray_fn_%s",
                module_mangle(codegen->modules, mem->resolved_decl));
        } else if (qualifier) {
            const char *qual = codegen_resolve_decl(codegen, qualifier);
            emit_formatted(codegen, "gray_fn_%s_%s", qual, mem->data.member.member);
        } else {
            emit(codegen, "gray_fn_");
            emit_expression(codegen, node->data.func_ref.function);
        }
    } else {
        emit(codegen, "gray_fn_");
        emit_expression(codegen, node->data.func_ref.function);
    }
}

/* True when the qualifier of `a.b` names a value rather than a module. A
 * local or parameter shadows a module of the same name — `b.v` inside the
 * file whose module is `b` reads the local's field — and the typechecker
 * already resolves it that way, so a typed object is what says so here. */
static bool member_object_is_value(CodeGen *codegen, AstNode *node) {
    GrayType *obj_type = codegen->type_table
        ? typetable_get(codegen->type_table, node->data.member.object)
        : NULL;
    return obj_type && obj_type->kind != TK_UNKNOWN;
}

static void emit_member_expr(CodeGen *codegen, AstNode *node) {
    /* Check for module constants first */
    const char *object_name = ast_member_qualifier(node);
    if (object_name) {
        const char *mod = object_name;
        const char *mem = node->data.member.member;

        /* @math constants */
        if (strcmp(mod, "math") == 0) {
            if (strcmp(mem, "PI") == 0)      { emit(codegen, "3.14159265358979323846"); return; }
            if (strcmp(mem, "E") == 0)       { emit(codegen, "2.71828182845904523536"); return; }
            if (strcmp(mem, "TAU") == 0)     { emit(codegen, "6.28318530717958647692"); return; }
            if (strcmp(mem, "PHI") == 0)     { emit(codegen, "1.61803398874989484820"); return; }
            if (strcmp(mem, "SQRT2") == 0)   { emit(codegen, "1.41421356237309504880"); return; }
            if (strcmp(mem, "LN2") == 0)     { emit(codegen, "0.69314718055994530942"); return; }
            if (strcmp(mem, "LN10") == 0)    { emit(codegen, "2.30258509299404568402"); return; }
            if (strcmp(mem, "INF") == 0)     { emit(codegen, "(1.0/0.0)"); return; }
            if (strcmp(mem, "NEG_INF") == 0) { emit(codegen, "(-1.0/0.0)"); return; }
            if (strcmp(mem, "EPSILON") == 0) { emit(codegen, "2.2204460492503131e-16"); return; }
            if (strcmp(mem, "MAX_INT") == 0) { emit(codegen, "9223372036854775807LL"); return; }
            if (strcmp(mem, "MIN_INT") == 0) { emit(codegen, "(-9223372036854775807LL - 1)"); return; }
            if (strcmp(mem, "MAX_FLOAT") == 0) { emit(codegen, "1.7976931348623157e308"); return; }
            if (strcmp(mem, "MIN_FLOAT") == 0) { emit(codegen, "-1.7976931348623157e308"); return; }
        }

        /* @io OpenFlag enum via the module.VARIANT spelling */
        if (strcmp(mod, "io") == 0) {
            if (strcmp(mem, "O_RDONLY") == 0 || strcmp(mem, "O_WRONLY") == 0 ||
                strcmp(mem, "O_RDWR") == 0) {
                emit_formatted(codegen, "GrayEnum_OpenFlag_%s", mem);
                return;
            }
        }

        /* @os Platform enum via the module.VARIANT spelling */
        if (strcmp(mod, "os") == 0) {
            if (strcmp(mem, "MAC_OS") == 0 || strcmp(mem, "LINUX") == 0 ||
                strcmp(mem, "WINDOWS") == 0 || strcmp(mem, "OTHER") == 0) {
                emit_formatted(codegen, "GrayEnum_Platform_%s", mem);
                return;
            }
        }

        /* @strconv constants */
        if (strcmp(mod, "strconv") == 0) {
            if (strcmp(mem, "BASE_2") == 0)  { emit(codegen, "2"); return; }
            if (strcmp(mem, "BASE_8") == 0)  { emit(codegen, "8"); return; }
            if (strcmp(mem, "BASE_10") == 0) { emit(codegen, "10"); return; }
            if (strcmp(mem, "BASE_16") == 0) { emit(codegen, "16"); return; }
            if (strcmp(mem, "BASE_36") == 0) { emit(codegen, "36"); return; }
        }

        /* @uuid constants */
        if (strcmp(mod, "uuid") == 0) {
            if (strcmp(mem, "NIL_UUID") == 0) {
                emit(codegen, "gray_uuid_nil()");
                return;
            }
        }

        /* ErrorCode.VARIANT — the program-wide synthetic enum. Its slots are
         * #defined as GrayErrorCode_<V> in the preamble, never as a
         * GrayEnum_ErrorCode_<V> variant, and it has no AST decl for
         * codegen_is_enum to recognize. */
        if (strcmp(mod, "ErrorCode") == 0) {
            emit_formatted(codegen, "GrayErrorCode_%s", mem);
            return;
        }

        /* Check if this is an enum access: EnumName.VALUE or prefix_EnumName.VALUE */
        if (mod[0] >= 'A' && mod[0] <= 'Z') {
            /* Resolve unprefixed enum names from 'import and use' */
            const char *resolved_enum = NULL;
            if (codegen_is_enum(codegen, mod)) {
                resolved_enum = mod;
            } else if (strcmp(mod, "OpenFlag") == 0 || strcmp(mod, "Platform") == 0) {
                /* Stdlib-provided enums have no AST decl in codegen's registry. */
                resolved_enum = mod;
            } else {
                const char *resolved_name = codegen_resolve_type(codegen, mod);
                if (resolved_name != mod && codegen_is_enum(codegen, resolved_name)) resolved_enum = resolved_name;
            }
            if (resolved_enum) {
                if (codegen_enum_is_tagged(codegen, resolved_enum)) {
                    emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }", resolved_enum, resolved_enum, mem);
                } else {
                    emit_formatted(codegen, "GrayEnum_%s_%s", resolved_enum, mem);
                }
                return;
            }
        }
        /* Rewritten enum name from import: lib_Color.RED → GrayEnum_lib_Color_RED.
         * The module-prefixed name starts with the module name (lowercase),
         * so the uppercase-mod guard above misses it. codegen_is_enum is
         * authoritative — if the resolved identifier is a known enum, any
         * member access on it is an enum member access, regardless of the
         * member's first-letter casing (lowercase variants like
         * `type_change` are valid). */
        if (codegen_is_enum(codegen, mod)) {
            if (codegen_enum_is_tagged(codegen, mod)) {
                emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }", mod, mod, mem);
            } else {
                emit_formatted(codegen, "GrayEnum_%s_%s", mod, mem);
            }
            return;
        }

        /* C interop constant access: extern.EOF, extern.NULL, extern.EXIT_SUCCESS */
        if (strcmp(mod, "extern") == 0 && codegen->has_c_imports) {
            emit_formatted(codegen, "%s", mem);
            return;
        }

        /* User-module qualified constant/variable access: mod.NAME → mod_NAME
         *
         * Module membership must come from a registration, never from a name
         * guess: a prefix scan over declared functions used to live here and
         * claimed any local whose name was some function's prefix (`item.priority`
         * became `item_priority` whenever `do item_is_alive(...)` was in scope).
         * The symbol table answers directly for a user module, and gives the
         * mangled name at the same time. Stdlib modules are not in it, so those
         * still go through the import list. */
        if (mod[0] >= 'a' && mod[0] <= 'z' && !member_object_is_value(codegen, node)) {
            if (node->resolved_decl) {
                emit(codegen, module_mangle(codegen->modules, node->resolved_decl));
                return;
            }
            if (codegen_module_imported(codegen, mod)) {
                emit_formatted(codegen, "%s_%s", resolve_alias(codegen, mod), mem);
                return;
            }
        }
    }
    /* Module-qualified enum access: lib.Color.RED → GrayEnum_lib_Color_RED.
     * The first-letter casing heuristics aren't reliable — Grayscale permits
     * lowercase enum members (e.g. `type_change`), and a confident
     * `codegen_is_enum` lookup on the prefixed name is authoritative.
     * Verify the type really is a known enum before rewriting. */
    if (node->data.member.object->kind == NODE_MEMBER_EXPR) {
        AstNode *inner = node->data.member.object;
        const char *value = node->data.member.member;
        /* An alias standing for an enum reaches this the same way the enum
         * does — lib.Hue.RED is lib.Color.RED — so resolve it to the enum it
         * names instead of falling through to plain member access. */
        const char *prefixed = NULL;
        if (inner->resolved_decl && inner->resolved_decl->kind == DECL_ENUM) {
            prefixed = module_mangle(codegen->modules, inner->resolved_decl);
        } else if (inner->resolved_decl && inner->resolved_decl->kind == DECL_ALIAS) {
            const char *target = resolve_type_alias_codegen(codegen,
                module_mangle(codegen->modules, inner->resolved_decl));
            if (codegen_is_enum(codegen, target)) prefixed = target;
        }
        if (prefixed) {
            if (codegen_enum_is_tagged(codegen, prefixed)) {
                emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }",
                    prefixed, prefixed, value);
            } else {
                emit_formatted(codegen, "GrayEnum_%s_%s", prefixed, value);
            }
            return;
        }
    }
    /* Check if object is a pointer type; use -> instead of . */
    {
        GrayType *obj_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.member.object)
            : NULL;

        /* When accessing .v0 on a single-return value (not a multi-return struct),
         * just emit the value itself (e.g., from temp x, _ = single_return_func()) */
        /* When accessing .v0 on a single-return value (not a multi-return temp),
         * just emit the value itself. Skip this for _gray_tmp* variables which
         * are multi-return unpacking temps. */
        const char *mem_name = node->data.member.member;
        if (mem_name[0] == 'v' && mem_name[1] >= '0' && mem_name[1] <= '9' && mem_name[2] == '\0') {
            bool is_multi_temp = object_name && is_result_temporary(object_name);
            if (!is_multi_temp && obj_t &&
                (obj_t->kind == TK_INT || obj_t->kind == TK_UINT || obj_t->kind == TK_FLOAT ||
                 obj_t->kind == TK_BOOL || obj_t->kind == TK_STRING ||
                 obj_t->kind == TK_CHAR || obj_t->kind == TK_BYTE)) {
                if (mem_name[1] == '0') {
                    emit_expression(codegen, node->data.member.object);
                } else {
                    emit(codegen, "0 /* discarded */");
                }
                return;
            }
        }

        /* Ref vars are already dereferenced by label emission; use . not -> */
        bool obj_is_ref = object_name && is_reference_variable(codegen, object_name);
        bool obj_is_raw = object_name && is_raw_variable(codegen, object_name);
        /* Multi-return temp vars are always value types — never pointer-deref them */
        bool obj_is_multi_temp = object_name && is_result_temporary(object_name);
        if (!obj_is_multi_temp && !obj_is_ref && obj_t && obj_t->kind == TK_POINTER) {
            if (obj_is_raw) {
                /* Raw pointer: direct field access, no nil check */
                emit_expression(codegen, node->data.member.object);
                emit_formatted(codegen, "->%s", sanitize_name(node->data.member.member));
            } else {
                /* Nil-guarded pointer field access. Route through
                 * gray_ptr_check so the result stays an lvalue: a further
                 * `.field`, an index, or `&` on a nested value-struct field
                 * (e.g. `p.inner.items`) then lands on the real storage
                 * rather than a by-value statement-expression copy. */
                char c_pointee[TYPE_NAME_MAX];
                const char *pointee_tn = obj_t->element_type;
                if (pointee_tn && pointee_tn[0] != '^' && !strchr(pointee_tn, '?')) {
                    const char *c = gray_type_to_c_codegen(codegen, pointee_tn);
                    snprintf(c_pointee, sizeof(c_pointee), "%s", c);
                    emit_formatted(codegen, "((%s *)gray_ptr_check((void *)(", c_pointee);
                    emit_expression(codegen, node->data.member.object);
                    emit_formatted(codegen, "), \"%s\", %d))->%s",
                        codegen->file, node->token.line,
                        sanitize_name(node->data.member.member));
                } else {
                    emit(codegen, "({ __auto_type _dp = ");
                    emit_expression(codegen, node->data.member.object);
                    emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } _dp->%s; })",
                        codegen->file, node->token.line, sanitize_name(node->data.member.member));
                }
            }
        } else if (!obj_is_ref && obj_t && obj_t->kind == TK_ERROR) {
            /* Error has fields code (ErrorCode int) and msg; '.message' is an
             * accepted alias for '.msg'. The value is a GrayError* that is
             * NULL on the success path, so guard the read: without it a
             * `mut m = err.msg` after a successful call is a raw segfault
             * with no diagnostic. */
            const char *m = node->data.member.member;
            const char *field = strcmp(m, "message") == 0 ? "msg" : sanitize_name(m);
            emit(codegen, "({ __auto_type _err_v = ");
            emit_expression(codegen, node->data.member.object);
            emit_formatted(codegen, "; if (!_err_v) { gray_panic_code_at(\"%s\", %d, \"P0115\", \"read of '%%s' on a nil Error; check the error is non-nil before reading its fields\", \"%s\"); } _err_v->%s; })",
                codegen->file, node->token.line, m, field);
        } else {
            emit_expression(codegen, node->data.member.object);
            emit_formatted(codegen, ".%s", sanitize_name(node->data.member.member));
        }
    }
}

/* True when `left` is a map lookup (m[key], or a chained one). Such a lookup
 * lowers to a statement-expression that yields the stored value by rvalue, so
 * an outer index on it cannot take its address and must bind it to a temp. */
static bool index_left_is_map_lookup(CodeGen *codegen, AstNode *left) {
    if (!left || left->kind != NODE_INDEX_EXPR) return false;
    GrayType *inner = codegen->type_table
        ? typetable_get(codegen->type_table, left->data.index_expr.left)
        : NULL;
    return inner && inner->kind == TK_MAP;
}

static void emit_index_expr(CodeGen *codegen, AstNode *node) {
    /* Check if left side is an array (GrayArray) or string */
    GrayType *left_t = codegen->type_table
        ? typetable_get(codegen->type_table, node->data.index_expr.left)
        : NULL;
    if (left_t && left_t->kind == TK_ARRAY) {
        /* Determine element C type */
        const char *c_elem = "int64_t";
        const char *elem_tn = codegen_effective_type_string(codegen, left_t->element_type);
        if (elem_tn && (strcmp(elem_tn, "func") == 0 || strncmp(elem_tn, "func(", 5) == 0)) {
            c_elem = "void *";
        } else if (elem_tn) {
            GrayType *et = type_from_name(elem_tn);
            if (et->kind == TK_FLOAT) c_elem = (strcmp(elem_tn, "f32") == 0) ? "float" : "double";
            else if (et->kind == TK_BOOL) c_elem = "bool";
            else if (et->kind == TK_STRING) c_elem = "GrayString";
            else if (et->kind == TK_CHAR) c_elem = "int32_t";
            else if (et->kind == TK_BYTE) c_elem = "uint8_t";
            /* Sized int element types (u8/u16/i32/…) are stored packed by
             * cast(arr, [T]); reading them with the int64_t fall-through
             * strides past the buffer. Match the storage width. */
            else if ((et->kind == TK_INT || et->kind == TK_UINT) && !is_bigint_type(elem_tn))
                c_elem = gray_type_to_c_codegen(codegen, elem_tn);
            else if (et->kind == TK_ARRAY) c_elem = "GrayArray";
            else if (et->kind == TK_MAP) c_elem = "GrayMap";
            else if (et->kind == TK_STRUCT) c_elem = gray_type_to_c_codegen(codegen, elem_tn);
            else if (et->kind == TK_ENUM) {
                c_elem = codegen_enum_is_string(codegen, elem_tn)
                    ? "GrayString" : gray_type_to_c_codegen(codegen, elem_tn);
            }
            else if (et->kind == TK_POINTER) {
                static char idx_ptr_buf[MSG_BUF_SIZE];
                const char *pointee = et->element_type ? et->element_type : "void";
                snprintf(idx_ptr_buf, sizeof(idx_ptr_buf), "%s *", gray_type_to_c_codegen(codegen, pointee));
                c_elem = idx_ptr_buf;
            }
        }
        /* Check for bigint element types */
        if (left_t->element_type && is_bigint_type(left_t->element_type)) {
            c_elem = bigint_prefix(left_t->element_type);
        }
        /* If left is an rvalue, GRAY_ARRAY_GET's &(arr) would be invalid.
         * Handles three rvalue sources:
         *  1. function call result (NODE_CALL_EXPR)
         *  2. array field through struct pointer: b.items[i] where b: ^Bag
         *     (member emit wraps in GCC statement expr → rvalue)
         *  3. explicit deref then member: b^.items[i] (same rvalue issue)
         * For cases 2/3, inline the nil check and use _dp->field directly
         * so GRAY_ARRAY_GET receives an assignable target. */
        AstNode *arr_ptr_obj = NULL;
        const char *arr_ptr_field = NULL;
        if (node->data.index_expr.left->kind == NODE_MEMBER_EXPR) {
            AstNode *_mem = node->data.index_expr.left;
            AstNode *_obj = _mem->data.member.object;
            GrayType *_obj_t = codegen->type_table ? typetable_get(codegen->type_table, _obj) : NULL;
            if (_obj_t && _obj_t->kind == TK_POINTER) {
                arr_ptr_obj = _obj;
                arr_ptr_field = _mem->data.member.member;
            } else if (_obj->kind == NODE_POSTFIX_EXPR &&
                       _obj->data.postfix.op == TOK_CARET) {
                /* b^.field: strip the deref, use the underlying pointer */
                arr_ptr_obj = _obj->data.postfix.left;
                arr_ptr_field = _mem->data.member.member;
            }
        }
        if (arr_ptr_obj) {
            bool _arr_raw = (arr_ptr_obj->kind == NODE_LABEL && is_raw_variable(codegen, arr_ptr_obj->data.label.value));
            int my_dp = codegen_next_id(codegen);
            emit_formatted(codegen, "({ __auto_type _adp%d = ", my_dp);
            emit_expression(codegen, arr_ptr_obj);
            if (_arr_raw) {
                emit_formatted(codegen, "; GRAY_ARRAY_GET_AT(_adp%d->%s, %s, ",
                      my_dp, sanitize_name(arr_ptr_field), c_elem);
            } else {
                emit_formatted(codegen, "; if (!_adp%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                          "GRAY_ARRAY_GET_AT(_adp%d->%s, %s, ",
                      my_dp, codegen->file, node->token.line, my_dp, sanitize_name(arr_ptr_field), c_elem);
            }
            emit_expression(codegen, node->data.index_expr.index);
            emit_formatted(codegen, ", \"%s\", %d); })", codegen->file, node->token.line);
        } else if (node->data.index_expr.left->kind == NODE_POSTFIX_EXPR &&
                   node->data.index_expr.left->data.postfix.op == TOK_CARET) {
            /* p^[i]: direct dereference of container pointer */
            AstNode *_dp_inner = node->data.index_expr.left->data.postfix.left;
            bool _dp_raw = (_dp_inner->kind == NODE_LABEL && is_raw_variable(codegen, _dp_inner->data.label.value));
            int my_dp = codegen_next_id(codegen);
            emit_formatted(codegen, "({ __auto_type _adp%d = ", my_dp);
            emit_expression(codegen, _dp_inner);
            if (_dp_raw) {
                emit_formatted(codegen, "; GRAY_ARRAY_GET_AT(*_adp%d, %s, ",
                      my_dp, c_elem);
            } else {
                emit_formatted(codegen, "; if (!_adp%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                          "GRAY_ARRAY_GET_AT(*_adp%d, %s, ",
                      my_dp, codegen->file, node->token.line, my_dp, c_elem);
            }
            emit_expression(codegen, node->data.index_expr.index);
            emit_formatted(codegen, ", \"%s\", %d); })", codegen->file, node->token.line);
        } else if (node->data.index_expr.left->kind == NODE_CALL_EXPR ||
                   index_left_is_map_lookup(codegen, node->data.index_expr.left)) {
            emit_formatted(codegen, "({ GrayArray _ea = ");
            emit_expression(codegen, node->data.index_expr.left);
            emit_formatted(codegen, "; GRAY_ARRAY_GET_AT(_ea, %s, ", c_elem);
            emit_expression(codegen, node->data.index_expr.index);
            emit_formatted(codegen, ", \"%s\", %d); })", codegen->file, node->token.line);
        } else {
            emit_formatted(codegen, "GRAY_ARRAY_GET_AT(");
            emit_expression(codegen, node->data.index_expr.left);
            emit_formatted(codegen, ", %s, ", c_elem);
            emit_expression(codegen, node->data.index_expr.index);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        }
    } else if (left_t && left_t->kind == TK_MAP) {
        /* Map key access; use temp to handle rvalue keys like literals */
        const char *c_key = "GrayString";
        const char *c_val = "int64_t";
        if (left_t->key_type) c_key = gray_map_element_c_type(codegen, left_t->key_type);
        if (left_t->value_type) c_val = gray_map_element_c_type(codegen, left_t->value_type);
        /* When the left side is an rvalue (e.g. chained map access
         * like m["a"]["x"], or pointer field access like p.map_field),
         * store it in a temp to make it addressable. */
        bool map_is_rvalue = (node->data.index_expr.left->kind == NODE_INDEX_EXPR ||
            node->data.index_expr.left->kind == NODE_CALL_EXPR);
        if (!map_is_rvalue && node->data.index_expr.left->kind == NODE_MEMBER_EXPR) {
            AstNode *obj = node->data.index_expr.left->data.member.object;
            GrayType *obj_t = codegen->type_table
                ? typetable_get(codegen->type_table, obj) : NULL;
            if (obj_t && obj_t->kind == TK_POINTER) map_is_rvalue = true;
            if (obj->kind == NODE_POSTFIX_EXPR && obj->data.postfix.op == TOK_CARET)
                map_is_rvalue = true;
        }
        /* p^["key"]: direct dereference of map pointer yields rvalue */
        if (!map_is_rvalue && node->data.index_expr.left->kind == NODE_POSTFIX_EXPR &&
            node->data.index_expr.left->data.postfix.op == TOK_CARET) {
            map_is_rvalue = true;
        }
        /* Dereference the entry pointer *outside* the statement expression so
         * the result is an lvalue: `m[k].field = v`, `m[k].field += v`,
         * `bump(m[k].field)`, and `m[k1][k2] = v` all take the address of, or
         * assign through, this expression, exactly as the array form does. */
        if (map_is_rvalue) {
            emit_formatted(codegen, "(*(%s *)({ GrayMap _mt = ", c_val);
            emit_expression(codegen, node->data.index_expr.left);
            emit_formatted(codegen, "; %s _mk = ", c_key);
            emit_map_slot_value(codegen, left_t->key_type, node->data.index_expr.index);
            emit_formatted(codegen, "; void *_mv = gray_map_get(&_mt, &_mk); if (!_mv) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ", codegen->file, node->token.line);
            emit(codegen, "_mv; }))");
        } else {
            emit_formatted(codegen, "(*(%s *)({ %s _mk = ", c_val, c_key);
            emit_map_slot_value(codegen, left_t->key_type, node->data.index_expr.index);
            emit_formatted(codegen, "; void *_mv = gray_map_get(&");
            emit_expression(codegen, node->data.index_expr.left);
            emit_formatted(codegen, ", &_mk); if (!_mv) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ", codegen->file, node->token.line);
            emit(codegen, "_mv; }))");
        }
    } else if (left_t && left_t->kind == TK_STRING) {
        /* String indexing with bounds check: s.data[i] */
        emit_formatted(codegen, "({ GrayString _es = ");
        emit_expression(codegen, node->data.index_expr.left);
        emit_formatted(codegen, "; int32_t _ei = (int32_t)(");
        emit_expression(codegen, node->data.index_expr.index);
        emit_formatted(codegen, "); if (_ei < 0 || _ei >= _es.len) { gray_panic_code_at(\"%s\", %d, \"P0082\", \"string index %%d out of bounds (length %%d)\", _ei, _es.len); } ", codegen->file, node->token.line);
        emit(codegen, "(int32_t)(unsigned char)_es.data[_ei]; })");
    } else {
        /* Fallback */
        emit_expression(codegen, node->data.index_expr.left);
        emit(codegen, "[");
        emit_expression(codegen, node->data.index_expr.index);
        emit(codegen, "]");
    }
}

static void emit_cast_expr(CodeGen *codegen, AstNode *node) {
    /* cast(value, type); dispatch to conversion functions for non-trivial casts */
    const char *target = node->data.cast.target_type;
    AstNode *val = node->data.cast.value;
    GrayType *val_t = codegen->type_table ? typetable_get(codegen->type_table, val) : NULL;
    TypeKind val_kind = val_t ? val_t->kind : TK_UNKNOWN;

    /* Infer kind from AST if type table has no info */
    if (val_kind == TK_UNKNOWN) {
        if (val->kind == NODE_STRING_VALUE || val->kind == NODE_INTERPOLATED_STRING)
            val_kind = TK_STRING;
        else if (val->kind == NODE_BOOL_VALUE) val_kind = TK_BOOL;
        else if (val->kind == NODE_FLOAT_VALUE) val_kind = TK_FLOAT;
        else if (val->kind == NODE_INT_VALUE) val_kind = TK_INT;
    }

    /* Array cast: allocate new array and convert each element with range checks */
    if (node->data.cast.is_array) {
        const char *src_elem = (val_t && val_t->element_type) ? val_t->element_type : "int";
        const char *dst_elem = node->data.cast.element_type;
        const char *src_c = gray_type_to_c_codegen(codegen, src_elem);
        const char *dst_c = gray_type_to_c_codegen(codegen, dst_elem);
        int id = codegen_next_id(codegen);

        bool src_is_float = (strcmp(src_elem, "float") == 0 || strcmp(src_elem, "f32") == 0 || strcmp(src_elem, "f64") == 0);
        bool dst_is_float = (strcmp(dst_elem, "float") == 0 || strcmp(dst_elem, "f32") == 0 || strcmp(dst_elem, "f64") == 0);
        bool dst_is_uint = (strcmp(dst_elem, "uint") == 0 || strcmp(dst_elem, "u64") == 0);

        emit_formatted(codegen, "({ GrayArray _ca%d = ", id);
        emit_expression(codegen, val);
        emit_formatted(codegen, "; GrayArray _cr%d = gray_array_new(gray_default_arena, sizeof(%s), _ca%d.len); ", id, dst_c, id);
        emit_formatted(codegen, "for (int32_t _ci%d = 0; _ci%d < _ca%d.len; _ci%d++) { ", id, id, id, id);
        emit_formatted(codegen, "%s _cv%d = ((%s*)_ca%d.data)[_ci%d]; ", src_c, id, src_c, id, id);

        if (src_is_float && !dst_is_float) {
            /* float → integer: use overflow-safe float conversion */
            if (dst_is_uint) {
                emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)gray_float_to_uint((double)_cv%d, \"%s\", %d); ",
                    dst_c, id, id, dst_c, id, codegen->file, node->token.line);
            } else {
                emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)gray_float_to_int((double)_cv%d, \"%s\", %d); ",
                    dst_c, id, id, dst_c, id, codegen->file, node->token.line);
            }
        } else {
            /* Integer → integer or integer → float: range-check for narrowing */
            const char *smin = NULL, *smax = NULL;
            bool is_unsigned = false;
            sized_int_bounds(dst_elem, &smin, &smax, &is_unsigned);
            if (smax && is_unsigned) {
                emit_formatted(codegen, "gray_ucast_check(_cv%d, %s, \"%s\", \"%s\", %d); ",
                    id, smax, dst_elem, codegen->file, node->token.line);
            } else if (smax) {
                emit_formatted(codegen, "gray_cast_check(_cv%d, %s, %s, \"%s\", \"%s\", %d); ",
                    id, smin, smax, dst_elem, codegen->file, node->token.line);
            }
            emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)_cv%d; ", dst_c, id, id, dst_c, id);
        }

        emit_formatted(codegen, "} _cr%d.len = _ca%d.len; _cr%d; })", id, id, id);
        return;
    }

    /* string-backed enum <-> string: both are GrayString at runtime, so the
     * cast is a pure reinterpretation with no conversion. */
    if (codegen_enum_is_string(codegen, target) ||
        (strcmp(target, "string") == 0 && val_t && val_t->name &&
         codegen_enum_is_string(codegen, val_t->name))) {
        emit_expression(codegen, val);
        return;
    }

    if (strcmp(target, "string") == 0) {
        /* any → string: use to_string functions */
        if (val_kind == TK_CHAR) {
            /* char → string: single-character string, not ASCII value */
            emit(codegen, "gray_string_new(gray_default_arena, (char[]){(char)(");
            emit_expression(codegen, val);
            emit(codegen, "), '\\0'}, 1)");
        } else {
            emit_to_string(codegen, val);
        }
    } else if ((strcmp(target, "int") == 0 || strcmp(target, "i64") == 0) && val_kind == TK_STRING) {
        /* string → int */
        emit(codegen, "gray_builtin_string_to_int(");
        emit_expression(codegen, val);
        emit(codegen, ")");
    } else if ((strcmp(target, "float") == 0 || strcmp(target, "f64") == 0) && val_kind == TK_STRING) {
        /* string → float */
        emit(codegen, "gray_builtin_string_to_float(");
        emit_expression(codegen, val);
        emit(codegen, ")");
    } else if ((strcmp(target, "int") == 0 || strcmp(target, "i64") == 0) && val_kind == TK_FLOAT) {
        /* float → int: overflow-safe */
        emit(codegen, "gray_float_to_int((double)(");
        emit_expression(codegen, val);
        emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
    } else if ((strcmp(target, "uint") == 0 || strcmp(target, "u64") == 0) && val_kind == TK_FLOAT) {
        /* float → uint: negative values and overflow are undefined behavior in C; panic instead */
        emit(codegen, "gray_float_to_uint((double)(");
        emit_expression(codegen, val);
        emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
    } else if (val_kind == TK_STRING) {
        /* string → numeric (targets other than int/float handled above):
         * parse to int64/double first, then apply narrowing check */
        if (strcmp(target, "uint") == 0 || strcmp(target, "u64") == 0) {
            emit(codegen, "(uint64_t)gray_builtin_string_to_int(");
            emit_expression(codegen, val);
            emit(codegen, ")");
        } else if (strcmp(target, "f32") == 0) {
            emit(codegen, "(float)gray_builtin_string_to_float(");
            emit_expression(codegen, val);
            emit(codegen, ")");
        } else {
            /* Sized integer targets — parse then range-check */
            const char *smin = NULL, *smax = NULL;
            bool is_unsigned = false;
            sized_int_bounds(target, &smin, &smax, &is_unsigned);

            if (smax && is_unsigned) {
                emit_formatted(codegen, "(%s)gray_ucast_check(gray_builtin_string_to_int(", gray_type_to_c_codegen(codegen, target));
                emit_expression(codegen, val);
                emit_formatted(codegen, "), %s, \"%s\", \"%s\", %d)", smax, target, codegen->file, node->token.line);
            } else if (smax) {
                emit_formatted(codegen, "(%s)gray_cast_check(gray_builtin_string_to_int(", gray_type_to_c_codegen(codegen, target));
                emit_expression(codegen, val);
                emit_formatted(codegen, "), %s, %s, \"%s\", \"%s\", %d)", smin, smax, target, codegen->file, node->token.line);
            } else {
                /* Fallback: parse to int and cast */
                emit_formatted(codegen, "((%s)gray_builtin_string_to_int(", gray_type_to_c_codegen(codegen, target));
                emit_expression(codegen, val);
                emit(codegen, "))");
            }
        }
    } else {
        /* Wide integer (i128/u128/i256/u256) cast handling */
        bool target_is_bi = is_bigint_type(target);
        const char *src_bi = (val_t && val_t->name && is_bigint_type(val_t->name))
            ? val_t->name : resolve_bigint_type(codegen, val);
        if (target_is_bi || src_bi) {
            if (target_is_bi && !src_bi) {
                /* scalar → wide: use from_i64 / from_u64 */
                emit_scalar_to_bigint(codegen, target, val, val_t);
            } else if (!target_is_bi && src_bi) {
                /* wide → scalar: range-checked extraction to int64/uint64,
                 * with additional narrow-range check for sub-64-bit targets */
                bool dst_unsigned = (strcmp(target, "uint") == 0 || strcmp(target, "u64") == 0 ||
                    strcmp(target, "u8") == 0 || strcmp(target, "byte") == 0 ||
                    strcmp(target, "u16") == 0 || strcmp(target, "u32") == 0);
                const char *bp = bigint_prefix(src_bi);

                /* Determine if an additional narrow range check is needed */
                const char *nmin = NULL, *nmax = NULL;
                bool narrow_unsigned = false;
                sized_int_bounds(target, &nmin, &nmax, &narrow_unsigned);

                if (nmax && narrow_unsigned) {
                    emit_formatted(codegen, "(%s)gray_ucast_check((int64_t)%s_to_u64(", gray_type_to_c_codegen(codegen, target), bp);
                    emit_expression(codegen, val);
                    emit_formatted(codegen, ", \"%s\", %d), %s, \"%s\", \"%s\", %d)", codegen->file, node->token.line, nmax, target, codegen->file, node->token.line);
                } else if (nmax) {
                    emit_formatted(codegen, "(%s)gray_cast_check(%s_to_i64(", gray_type_to_c_codegen(codegen, target), bp);
                    emit_expression(codegen, val);
                    emit_formatted(codegen, ", \"%s\", %d), %s, %s, \"%s\", \"%s\", %d)", codegen->file, node->token.line, nmin, nmax, target, codegen->file, node->token.line);
                } else if (dst_unsigned) {
                    emit_formatted(codegen, "(%s)%s_to_u64(", gray_type_to_c_codegen(codegen, target), bp);
                    emit_expression(codegen, val);
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                } else {
                    emit_formatted(codegen, "(%s)%s_to_i64(", gray_type_to_c_codegen(codegen, target), bp);
                    emit_expression(codegen, val);
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                }
            } else {
                /* wide → wide: use cross-type constructors */
                if (strcmp(src_bi, "i128") == 0 && strcmp(target, "u128") == 0)
                    { emit(codegen, "gray_u128_from_i128("); emit_expression(codegen, val); emit(codegen, ")"); }
                else if (strcmp(src_bi, "u128") == 0 && strcmp(target, "i128") == 0)
                    { emit(codegen, "gray_i128_from_u128("); emit_expression(codegen, val); emit(codegen, ")"); }
                else if (strcmp(src_bi, "i128") == 0 && strcmp(target, "i256") == 0)
                    { emit(codegen, "gray_i256_from_i128("); emit_expression(codegen, val); emit(codegen, ")"); }
                else if (strcmp(src_bi, "u128") == 0 && strcmp(target, "u256") == 0)
                    { emit(codegen, "gray_u256_from_u128("); emit_expression(codegen, val); emit(codegen, ")"); }
                else if (strcmp(src_bi, "i256") == 0 && strcmp(target, "i128") == 0)
                    { emit(codegen, "gray_i128_from_i256("); emit_expression(codegen, val); emit(codegen, ")"); }
                else if (strcmp(src_bi, "u256") == 0 && strcmp(target, "u128") == 0)
                    { emit(codegen, "gray_u128_from_u256("); emit_expression(codegen, val); emit(codegen, ")"); }
                else
                    { emit_expression(codegen, val); } /* same-type no-op */
            }
            return;
        }

        /* Numeric casts: range-checked for narrowing, raw for widening */
        const char *smin = NULL, *smax = NULL;
        bool is_unsigned = false;
        sized_int_bounds(target, &smin, &smax, &is_unsigned);

        if (smax && is_unsigned) {
            emit_formatted(codegen, "(%s)gray_ucast_check(", gray_type_to_c_codegen(codegen, target));
            emit_expression(codegen, val);
            emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d)", smax, target, codegen->file, node->token.line);
        } else if (smax) {
            emit_formatted(codegen, "(%s)gray_cast_check(", gray_type_to_c_codegen(codegen, target));
            emit_expression(codegen, val);
            emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d)", smin, smax, target, codegen->file, node->token.line);
        } else if ((strcmp(target, "uint") == 0 || strcmp(target, "u64") == 0) &&
                   val_kind == TK_INT) {
            /* signed int → uint/u64: panic if value is negative */
            emit_formatted(codegen, "(uint64_t)gray_ucast_check((int64_t)(");
            emit_expression(codegen, val);
            emit_formatted(codegen, "), 18446744073709551615ULL, \"%s\", \"%s\", %d)", target, codegen->file, node->token.line);
        } else if ((strcmp(target, "int") == 0 || strcmp(target, "i64") == 0) &&
                   val_kind == TK_UINT &&
                   val_t && val_t->name &&
                   (strcmp(val_t->name, "uint") == 0 || strcmp(val_t->name, "u64") == 0)) {
            /* uint/u64 → int/i64: panic if value exceeds INT64_MAX */
            emit_formatted(codegen, "(int64_t)gray_uint_to_int_check((uint64_t)(");
            emit_expression(codegen, val);
            emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
        } else if (codegen_is_enum(codegen, target) &&
                   !codegen_enum_is_string(codegen, target) &&
                   !codegen_enum_is_tagged(codegen, target)) {
            /* int → enum: the value has to name a declared variant. Without
             * this the cast stored whatever it was given, and the result
             * matched no variant in a `when` or an `==`. The variant names go
             * into the array as written so the C compiler supplies their
             * values, explicit ones included. */
            AstNode *edecl = codegen->enum_decls[codegen_enum_index(codegen, target)];
            emit_formatted(codegen, "(%s)gray_enum_cast_check((int64_t)(",
                gray_type_to_c_codegen(codegen, target));
            emit_expression(codegen, val);
            emit(codegen, "), (const int64_t[]){");
            for (int variant_index = 0; variant_index < edecl->data.enum_decl.value_count; variant_index++) {
                if (variant_index > 0) emit(codegen, ", ");
                emit_formatted(codegen, "GrayEnum_%s_%s", target,
                    edecl->data.enum_decl.values[variant_index].name);
            }
            /* The panic names the enum the way the program spells it, not the
             * module-prefixed key the C constants are built from. */
            const char *display = edecl->data.enum_decl.original_name
                ? edecl->data.enum_decl.original_name : target;
            emit_formatted(codegen, "}, %d, %s, \"%s\", \"%s\", %d)",
                edecl->data.enum_decl.value_count,
                edecl->data.enum_decl.is_flags ? "true" : "false",
                display, codegen->file, node->token.line);
        } else {
            emit_formatted(codegen, "((%s)(", gray_type_to_c_codegen(codegen, target));
            emit_expression(codegen, val);
            emit(codegen, "))");
        }
    }
}

/* True when new(Type) must run field initializers rather than a bare zeroed
 * allocation: the struct has a map/array field, a field default, or a nested
 * value-struct field that itself needs one. Value structs cannot be cyclic,
 * so the recursion terminates. */
static bool struct_needs_new_init(CodeGen *codegen, AstNode *sdecl, int depth) {
    if (!sdecl || depth > 8) return false;
    for (int i = 0; i < sdecl->data.struct_decl.field_count; i++) {
        const char *ft = sdecl->data.struct_decl.fields[i].type_name;
        if (sdecl->data.struct_decl.fields[i].default_value) return true;
        if (ft && (strncmp(ft, "map[", 4) == 0 || ft[0] == '[')) return true;
        if (ft && codegen_enum_is_string(codegen, codegen_resolve_type(codegen, ft)))
            return true;
        if (ft && ft[0] != '^') {
            GrayType *fttype = type_from_name(ft);
            if (fttype && fttype->kind == TK_STRUCT &&
                struct_needs_new_init(codegen, find_struct_declaration(codegen, ft), depth + 1))
                return true;
        }
    }
    return false;
}

/* Emit the container / default initializers for one struct level reached
 * through `access` (e.g. "_np->" or "_np->inner."), recursing into nested
 * value-struct fields so a map/array buried inside them is still given a
 * live header instead of the zero one new()'s allocation leaves. */
static void emit_new_struct_init(CodeGen *codegen, AstNode *sdecl,
                                 const char *access, int depth) {
    if (!sdecl || depth > 8) return;
    for (int i = 0; i < sdecl->data.struct_decl.field_count; i++) {
        const char *field_name = sdecl->data.struct_decl.fields[i].name;
        const char *field_type = sdecl->data.struct_decl.fields[i].type_name;
        if (field_type && strncmp(field_type, "map[", 4) == 0) {
            GrayType *map_type = type_from_name(field_type);
            const char *c_kt = "GrayString";
            const char *c_vt = "int64_t";
            if (map_type && map_type->key_type) c_kt = gray_map_element_c_type(codegen, map_type->key_type);
            if (map_type && map_type->value_type) c_vt = gray_map_element_c_type(codegen, map_type->value_type);
            emit_formatted(codegen, "%s%s = gray_map_new_kind(gray_heap_arena, sizeof(%s), sizeof(%s), 8, %s); ",
                access, sanitize_name(field_name), c_kt, c_vt, gray_map_key_kind_macro(c_kt));
        } else if (field_type && field_type[0] == '[') {
            GrayType *arg_type = type_from_name(field_type);
            const char *c_elem = "int64_t";
            if (arg_type && arg_type->element_type)
                c_elem = gray_map_element_c_type(codegen, arg_type->element_type);
            emit_formatted(codegen, "%s%s = gray_array_new(gray_heap_arena, sizeof(%s), 4); ",
                access, sanitize_name(field_name), c_elem);
        } else if (field_type &&
                   codegen_enum_is_string(codegen, codegen_resolve_type(codegen, field_type))) {
            /* String-backed enum field: zero is an empty string, not a
             * variant. Seed with the first variant, matching new(EnumType). */
            const char *senum = codegen_resolve_type(codegen, field_type);
            int eidx = codegen_enum_index(codegen, senum);
            const char *fv = codegen->enum_decls[eidx]->data.enum_decl.values[0].name;
            emit_formatted(codegen, "%s%s = GrayEnum_%s_%s; ",
                access, sanitize_name(field_name), senum, fv);
        } else if (field_type && field_type[0] != '^') {
            GrayType *fttype = type_from_name(field_type);
            if (fttype && fttype->kind == TK_STRUCT) {
                AstNode *nested = find_struct_declaration(codegen, field_type);
                if (nested && struct_needs_new_init(codegen, nested, depth + 1)) {
                    char inner_access[MSG_BUF_SIZE];
                    snprintf(inner_access, sizeof(inner_access), "%s%s.",
                        access, sanitize_name(field_name));
                    emit_new_struct_init(codegen, nested, inner_access, depth + 1);
                }
            }
        }
        if (sdecl->data.struct_decl.fields[i].default_value) {
            emit_formatted(codegen, "%s%s = ", access, sanitize_name(field_name));
            emit_expression(codegen, sdecl->data.struct_decl.fields[i].default_value);
            emit(codegen, "; ");
        }
    }
}

static void emit_new_expr(CodeGen *codegen, AstNode *node) {
    /* new(Type) → zeroed allocation on default arena, returns pointer.
     * Map and array fields need explicit initialization because a
     * zero-filled GrayMap/GrayArray has key_size/value_size/elem_size = 0
     * and operations on them silently fail. */
    const char *sname = node->data.new_expr.type_name;
    /* Resolve ? → concrete binding for type params */
    if (strcmp(sname, "?") == 0 && codegen->wildcard_binding) {
        sname = codegen->wildcard_binding;
    } else {
        /* new() names its type as written; the struct declaration it has to
         * find (for field defaults) is keyed by the module's spelling. */
        codegen_enter_node(codegen, node);
        sname = codegen_resolve_type(codegen, sname);
    }
    const char *c_type = gray_type_to_c_codegen(codegen, sname);
    AstNode *sdecl = find_struct_declaration(codegen, sname);
    if (sdecl && struct_needs_new_init(codegen, sdecl, 0)) {
        /* Field defaults are expressions from the struct's own file, so they
         * resolve against the struct's module, not the caller's. */
        const char *caller_module = codegen->current_module;
        codegen_enter_node(codegen, sdecl);
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_new_struct_init(codegen, sdecl, "_np->", 0);
        emit(codegen, "_np; })");
        codegen->current_module = caller_module;
    } else if (sname[0] == '[') {
        /* Array type — allocate + initialize metadata */
        GrayType *arr_type = type_from_name(sname);
        const char *c_elem = "int64_t";
        if (arr_type && arr_type->element_type)
            c_elem = gray_map_element_c_type(codegen, arr_type->element_type);
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_formatted(codegen, "*_np = gray_array_new(gray_heap_arena, sizeof(%s), 4); _np; })", c_elem);
    } else if (strncmp(sname, "map[", 4) == 0) {
        /* Map type — allocate + initialize metadata */
        GrayType *map_type = type_from_name(sname);
        const char *c_kt = "GrayString";
        const char *c_vt = "int64_t";
        if (map_type && map_type->key_type) c_kt = gray_map_element_c_type(codegen, map_type->key_type);
        if (map_type && map_type->value_type) c_vt = gray_map_element_c_type(codegen, map_type->value_type);
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_formatted(codegen, "*_np = gray_map_new_kind(gray_heap_arena, sizeof(%s), sizeof(%s), 8, %s); _np; })",
            c_kt, c_vt, gray_map_key_kind_macro(c_kt));
    } else if (codegen_enum_is_string(codegen, sname)) {
        /* String enum — assign first variant so the value is valid */
        int eidx = codegen_enum_index(codegen, sname);
        AstNode *decl = codegen->enum_decls[eidx];
        const char *first_variant = decl->data.enum_decl.values[0].name;
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_formatted(codegen, "*_np = GrayEnum_%s_%s; _np; })", sname, first_variant);
    } else {
        emit_formatted(codegen, "((%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)))", c_type, c_type);
    }
}

/* --- emit_expression --- */

static void emit_expression(CodeGen *codegen, AstNode *node) {
    if (!node) return;

    switch (node->kind) {
    case NODE_LABEL:
        emit_label(codegen, node);
        break;

    case NODE_INT_VALUE:
        if (node->data.int_value.overflow) {
            /* Literal exceeds INT64_MAX; for u64/uint contexts emit as a
             * decimal ULL so it works for any base (0o, 0b, 0x literals). */
            const char *bi_ctx = resolve_bigint_type(codegen, node);
            if (!bi_ctx) {
                emit_formatted(codegen, "%lluULL",
                    (unsigned long long)(uint64_t)node->data.int_value.value);
            } else {
                emit_formatted(codegen, "%s_from_decimal(\"%s\")", bigint_prefix(bi_ctx),
                    node->data.int_value.literal);
            }
        } else {
            emit_formatted(codegen, "%lld", (long long)node->data.int_value.value);
        }
        break;

    case NODE_FLOAT_VALUE: {
        /* Emit float with enough precision, ensuring a decimal point so C
         * treats it as double (e.g. 1.0 must emit "1.0", not "1") */
        char fbuf[VAR_NAME_BUF];
        snprintf(fbuf, sizeof(fbuf), "%.17g", node->data.float_value.value);
        if (!strchr(fbuf, '.') && !strchr(fbuf, 'e')) {
            size_t flen = strlen(fbuf);
            fbuf[flen] = '.';
            fbuf[flen+1] = '0';
            fbuf[flen+2] = '\0';
        }
        emit(codegen, fbuf);
        break;
    }

    case NODE_STRING_VALUE:
        emit_string_value(codegen, node);
        break;

    case NODE_BOOL_VALUE:
        emit(codegen, node->data.bool_value.value ? "true" : "false");
        break;

    case NODE_CHAR_VALUE:
        /* A char is a Unicode codepoint (int32_t at the C boundary), so emit
         * the numeric value — a C char constant cannot hold codepoints above
         * 0x7F. */
        emit_formatted(codegen, "((int32_t)%d)", (int)node->data.char_value.value);
        break;

    case NODE_NIL_VALUE:
        emit(codegen, "NULL");
        break;

    case NODE_INTERPOLATED_STRING:
        emit_interpolated_string(codegen, node);
        break;

    case NODE_ARRAY_VALUE:
        emit_array_value(codegen, node);
        break;

    case NODE_MAP_VALUE:
        emit_map_value(codegen, node);
        break;

    case NODE_STRUCT_VALUE:
        emit_struct_value(codegen, node);
        break;

    case NODE_PREFIX_EXPR:
        emit_prefix_expr(codegen, node);
        break;

    case NODE_INFIX_EXPR:
        emit_infix_expr(codegen, node);
        break;

    case NODE_POSTFIX_EXPR:
        emit_postfix_expr(codegen, node);
        break;

    case NODE_FUNC_REF:
        emit_func_ref(codegen, node);
        break;

    case NODE_CALL_EXPR:
        emit_call_expression(codegen, node);
        break;

    case NODE_MEMBER_EXPR:
        emit_member_expr(codegen, node);
        break;

    case NODE_INDEX_EXPR:
        emit_index_expr(codegen, node);
        break;

    case NODE_CAST_EXPR:
        emit_cast_expr(codegen, node);
        break;

    case NODE_NEW_EXPR:
        emit_new_expr(codegen, node);
        break;

    case NODE_IMPLICIT_ENUM: {
        const char *ename = node->data.implicit_enum.resolved_enum;
        const char *variant = node->data.implicit_enum.variant;
        if (ename) {
            if (strcmp(ename, "ErrorCode") == 0) {
                emit_formatted(codegen, "GrayErrorCode_%s", variant);
            } else if (codegen_enum_is_tagged(codegen, ename)) {
                emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }", ename, ename, variant);
            } else {
                emit_formatted(codegen, "GrayEnum_%s_%s", ename, variant);
            }
        }
        break;
    }

    default:
        emit_formatted(codegen, "0 /* grayc: unhandled expression kind %d at %s:%d */",
            node->kind, codegen->file, node->token.line);
        break;
    }
}

/* Check if a call is a stdlib call like std.println or just println (with using) */
static bool is_stdlib_call(AstNode *node, const char **module, const char **func) {
    if (node->data.call.function->kind == NODE_MEMBER_EXPR) {
        AstNode *obj = node->data.call.function->data.member.object;
        if (obj->kind == NODE_LABEL) {
            /* A qualifier bound to a user module resolves to a declaration
             * written in Grayscale. Its C name comes from mangling that
             * declaration, not from the stdlib emitter for a module that
             * happens to share the name. */
            DeclEntry *resolved = node->data.call.function->resolved_decl;
            if (resolved && !resolved->external) return false;
            *module = obj->data.label.value;
            *func = node->data.call.function->data.member.member;
            return true;
        }
    } else if (node->data.call.function->kind == NODE_LABEL) {
        /* Direct call like println() via using */
        *module = NULL;
        *func = node->data.call.function->data.label.value;
        return true;
    }
    return false;
}

/* --- Stdlib call emission helpers --- */

/* If arg is a ref() call, return the inner argument so print functions
 * use the underlying value's type and emit the value, not the address. */
static AstNode *unwrap_reference_argument(AstNode *arg) {
    if (arg->kind == NODE_CALL_EXPR &&
        arg->data.call.function->kind == NODE_LABEL &&
        strcmp(arg->data.call.function->data.label.value, "ref") == 0 &&
        arg->data.call.arg_count == 1) {
        return arg->data.call.args[0];
    }
    return arg;
}

static const char *resolve_print_suffix(CodeGen *codegen, AstNode *arg) {
    /* addr() calls always print in hex format */
    if (arg->kind == NODE_CALL_EXPR && arg->data.call.function->kind == NODE_LABEL &&
        strcmp(arg->data.call.function->data.label.value, "addr") == 0) return "_addr";
    /* During wildcard monomorphisation the type table retains the type from the
     * first instantiation. If the arg is a label that names a '?'-typed parameter
     * of the current function, use the active binding instead so each instantiation
     * gets the correct print variant. */
    if (codegen->wildcard_binding && arg->kind == NODE_LABEL && codegen->current_func) {
        const char *label = arg->data.label.value;
        for (int i = 0; i < codegen->current_func->data.func_decl.param_count; i++) {
            Param *param = &codegen->current_func->data.func_decl.params[i];
            if (param->type_name && strchr(param->type_name, '?') &&
                strcmp(param->name, label) == 0) {
                GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
                if (wildcard_type) {
                    switch (wildcard_type->kind) {
                    case TK_STRING:  return "_str";
                    case TK_FLOAT:   return "_float";
                    case TK_BOOL:    return "_bool";
                    case TK_CHAR:    return "_char";
                    case TK_UINT:    return "_uint";
                    case TK_POINTER: return "_addr";
                    default:         return "_int";
                    }
                }
            }
        }
    }
    GrayType *type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
    if (type && type->kind != TK_UNKNOWN) {
        switch (type->kind) {
        case TK_STRING:  return "_str";
        case TK_FLOAT:   return "_float";
        case TK_BOOL:    return "_bool";
        case TK_CHAR:    return "_char";
        case TK_UINT:    return "_uint";
        case TK_POINTER: return "_addr";
        case TK_ENUM:
            return (type->name && codegen_enum_is_string(codegen, type->name)) ? "_str" : "_int";
        default:         return "_int";
        }
    }
    if (arg->kind == NODE_STRING_VALUE || arg->kind == NODE_INTERPOLATED_STRING) return "_str";
    if (arg->kind == NODE_FLOAT_VALUE) return "_float";
    if (arg->kind == NODE_BOOL_VALUE) return "_bool";
    if (arg->kind == NODE_CHAR_VALUE) return "_char";
    /* For call expressions, check the return type of the called function */
    if (arg->kind == NODE_CALL_EXPR && arg->data.call.function->kind == NODE_MEMBER_EXPR) {
        AstNode *function_node = arg->data.call.function;
        const char *obj = ast_member_qualifier(function_node);
        if (obj) {
            const char *mem = function_node->data.member.member;
            /* Check if it's a known stdlib module function that returns string */
            if ((strcmp(obj, "strings") == 0) ||
                (strcmp(obj, "encoding") == 0) ||
                (strcmp(obj, "crypto") == 0)) return "_str";
            if (strcmp(obj, "uuid") == 0 &&
                (strcmp(mem, "generate_compact") == 0 ||
                 strcmp(mem, "to_string") == 0)) return "_str";
            /* Check if it's a struct-namespaced function or instance struct function call */
            {
                const char *struct_name = NULL;
                /* Direct struct type call: Foo.greet() */
                if (obj[0] >= 'A' && obj[0] <= 'Z') {
                    struct_name = obj;
                } else {
                    /* Instance call: f.greet() — look up variable's struct type */
                    GrayType *obj_t = codegen->type_table
                        ? typetable_get(codegen->type_table, function_node->data.member.object) : NULL;
                    if (obj_t && (obj_t->kind == TK_STRUCT || obj_t->kind == TK_POINTER) && obj_t->name) {
                        struct_name = obj_t->name;
                    }
                }
                if (struct_name) {
                    AstNode *sdecl = find_struct_declaration(codegen, struct_name);
                    if (sdecl) {
                        for (int field_index = 0; field_index < sdecl->data.struct_decl.func_count; field_index++) {
                            AstNode *sf = sdecl->data.struct_decl.funcs[field_index].func_decl;
                            if (!sf || sf->kind != NODE_FUNC_DECL) continue;
                            /* Match: struct funcs are prefixed as StructName_funcName in all_funcs,
                             * but the original name is stored before prefixing in the struct decl.
                             * After codegen_init prefixes them, compare with prefixed form. */
                            const char *sf_name = sf->data.func_decl.name;
                            /* Check both prefixed (StructName_func) and bare (func) forms */
                            bool match = (strcmp(sf_name, mem) == 0);
                            if (!match) {
                                size_t sn_len = strlen(struct_name);
                                if (strlen(sf_name) > sn_len + 1 &&
                                    strncmp(sf_name, struct_name, sn_len) == 0 &&
                                    sf_name[sn_len] == '_' &&
                                    strcmp(sf_name + sn_len + 1, mem) == 0) {
                                    match = true;
                                }
                            }
                            if (match && sf->data.func_decl.return_type_count > 0) {
                                const char *return_type_str = sf->data.func_decl.return_types[0];
                                if (strcmp(return_type_str, "string") == 0) return "_str";
                                if (strcmp(return_type_str, "float") == 0 || strcmp(return_type_str, "f32") == 0 || strcmp(return_type_str, "f64") == 0) return "_float";
                                if (strcmp(return_type_str, "bool") == 0) return "_bool";
                                if (strcmp(return_type_str, "char") == 0) return "_char";
                                if (strcmp(return_type_str, "uint") == 0 || strcmp(return_type_str, "u8") == 0 ||
                                    strcmp(return_type_str, "u16") == 0 || strcmp(return_type_str, "u32") == 0 ||
                                    strcmp(return_type_str, "u64") == 0) return "_uint";
                                return "_int";
                            }
                        }
                    }
                }
            }
        }
    }
    if (arg->kind == NODE_CALL_EXPR && arg->data.call.function->kind == NODE_LABEL) {
        const char *function_name = arg->data.call.function->data.label.value;
        if (strcmp(function_name, "input") == 0 || strcmp(function_name, "type_of") == 0) return "_str";
        if (strcmp(function_name, "addr") == 0) return "_addr";
    }
    return "_int";
}

static void emit_to_string(CodeGen *codegen, AstNode *arg) {
    /* Bigint to_string */
    const char *bi_type = resolve_bigint_type(codegen, arg);
    if (bi_type) {
        emit_formatted(codegen, "%s_to_string(gray_default_arena, ", bigint_prefix(bi_type));
        emit_expression(codegen, arg);
        emit(codegen, ")");
        return;
    }
    GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
    if (arg_type && arg_type->kind == TK_ERROR) {
        int tag = codegen_next_id(codegen);
        emit_formatted(codegen, "({ GrayError *_gray_str_err%d = (", tag);
        emit_expression(codegen, arg);
        emit_formatted(codegen, "); _gray_str_err%d ? _gray_str_err%d->msg : gray_c_string_dup(gray_default_arena, \"nil\"); })", tag, tag);
        return;
    }
    if (arg_type && arg_type->kind == TK_ENUM &&
        codegen_enum_is_error_code(codegen, arg_type->name)) {
        emit(codegen, "gray_string_lit(gray_error_code_name((int64_t)(");
        emit_expression(codegen, arg);
        emit(codegen, ")))");
        return;
    }
    if (arg_type && arg_type->kind == TK_CHAR) {
        emit(codegen, "gray_builtin_char_to_utf8(gray_default_arena, ");
        emit_expression(codegen, arg);
        emit(codegen, ")");
    } else {
        if (arg_type && arg_type->kind == TK_FLOAT)
            emit(codegen, "gray_builtin_to_string_float(gray_default_arena, ");
        else if (arg_type && arg_type->kind == TK_BOOL)
            emit(codegen, "gray_builtin_to_string_bool(gray_default_arena, ");
        else if (arg_type && arg_type->kind == TK_UINT)
            emit(codegen, "gray_builtin_to_string_uint(gray_default_arena, ");
        else
            emit(codegen, "gray_builtin_to_string_int(gray_default_arena, ");
        emit_expression(codegen, arg);
        emit(codegen, ")");
    }
}

/* Emit a fmt format string literal with %d/%i/%u upgraded to %lld/%llu for
 * Grayscale int/uint arguments (which are int64_t/uint64_t) to avoid -Wformat.
 * If append_newline is true, a \n is appended before the closing quote. */
static void emit_format_string_normalized_extended(CodeGen *codegen, const char *fmt_str, AstNode *call_node, bool append_newline) {
    const char *ptr = fmt_str;
    int directive_index = 1; /* which call arg corresponds to the next directive */
    append_char_to_buffer(&codegen->output, '"');
    while (*ptr) {
        if (*ptr != '%') { append_char_to_buffer(&codegen->output, *ptr++); continue; }
        /* Emit '%' and start scanning the directive */
        append_char_to_buffer(&codegen->output, '%');
        ptr++;
        if (!*ptr) break;
        if (*ptr == '%') { append_char_to_buffer(&codegen->output, '%'); ptr++; continue; }
        /* Buffer flags / width / precision so they can be filtered once the
         * conversion is known — a numeric directive whose arg is a bigint is
         * downgraded to %s, and the 0/#/+/space flags and precision are
         * undefined behaviour (or misread as string precision) on an 's'
         * conversion, so they are dropped rather than left to leak a
         * -Wformat warning and produce wrong output. Width and '-' survive. */
        char flags[8]; int flags_len = 0;
        char width[16]; int width_len = 0;
        char precision[16]; int precision_len = 0;
        while (*ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '0' || *ptr == '#') {
            if (flags_len < (int)sizeof(flags) - 1) flags[flags_len++] = *ptr;
            ptr++;
        }
        while (*ptr >= '0' && *ptr <= '9') {
            if (width_len < (int)sizeof(width) - 1) width[width_len++] = *ptr;
            ptr++;
        }
        if (*ptr == '.') {
            if (precision_len < (int)sizeof(precision) - 1) precision[precision_len++] = *ptr;
            ptr++;
            while (*ptr >= '0' && *ptr <= '9') {
                if (precision_len < (int)sizeof(precision) - 1) precision[precision_len++] = *ptr;
                ptr++;
            }
        }
        flags[flags_len] = '\0'; width[width_len] = '\0'; precision[precision_len] = '\0';
        /* Check for existing length modifier (buffered; a downgrade to %s drops it) */
        char length_mod[4]; int length_mod_len = 0;
        if (*ptr == 'h' || *ptr == 'l' || *ptr == 'L') {
            length_mod[length_mod_len++] = *ptr++;
            if ((length_mod[0] == 'h' && *ptr == 'h') || (length_mod[0] == 'l' && *ptr == 'l'))
                length_mod[length_mod_len++] = *ptr++;
        }
        length_mod[length_mod_len] = '\0';
        bool has_length = length_mod_len > 0;
        char spec = *ptr ? *ptr++ : 0;
        if (!spec) break;
        GrayType *directive_type = (directive_index < call_node->data.call.arg_count && codegen->type_table)
            ? typetable_get(codegen->type_table, call_node->data.call.args[directive_index]) : NULL;
        bool arg_is_bigint = directive_type && directive_type->name &&
            is_bigint_type(directive_type->name);
        char emit_spec = spec;
        bool downgraded_to_s = false;
        if (spec == 'b') {
            /* %b isn't a real C conversion; emit_format_arguments() already
             * stringifies bool args to "true"/"false", so %s reads them back
             * correctly. Passing %b through verbatim hits vsnprintf as a literal
             * 'b' and never consumes the argument, desyncing every directive
             * after it. */
            emit_spec = 's';
            downgraded_to_s = true;
        } else if (arg_is_bigint && (spec == 'd' || spec == 'i' || spec == 'u' ||
                   spec == 'x' || spec == 'X' || spec == 'o')) {
            /* i128/u128/i256/u256 are struct-backed; emit_format_arguments()
             * converts them to a decimal/hex/octal string, read back with %s. */
            emit_spec = 's';
            downgraded_to_s = true;
        }
        /* Emit flags/width/precision, filtered when the directive became %s. */
        for (int fi = 0; fi < flags_len; fi++) {
            if (downgraded_to_s && flags[fi] != '-') continue;
            append_char_to_buffer(&codegen->output, flags[fi]);
        }
        for (int wi = 0; wi < width_len; wi++)
            append_char_to_buffer(&codegen->output, width[wi]);
        if (!downgraded_to_s) {
            for (int pi = 0; pi < precision_len; pi++)
                append_char_to_buffer(&codegen->output, precision[pi]);
            for (int li = 0; li < length_mod_len; li++)
                append_char_to_buffer(&codegen->output, length_mod[li]);
        }
        if (!downgraded_to_s && !has_length && directive_type) {
            /* Grayscale int/uint are 64-bit; widen the directive so the vararg
             * read matches the (unsigned) long long emit_format_arguments casts
             * the argument to. */
            if ((spec == 'd' || spec == 'i') && directive_type->kind == TK_INT) {
                append_char_to_buffer(&codegen->output, 'l');
                append_char_to_buffer(&codegen->output, 'l');
            } else if (spec == 'u' && directive_type->kind == TK_UINT) {
                append_char_to_buffer(&codegen->output, 'l');
                append_char_to_buffer(&codegen->output, 'l');
            } else if ((spec == 'x' || spec == 'X' || spec == 'o') &&
                       (directive_type->kind == TK_INT || directive_type->kind == TK_UINT)) {
                append_char_to_buffer(&codegen->output, 'l');
                append_char_to_buffer(&codegen->output, 'l');
            }
        }
        append_char_to_buffer(&codegen->output, emit_spec);
        directive_index++;
    }
    if (append_newline) { append_char_to_buffer(&codegen->output, '\\'); append_char_to_buffer(&codegen->output, 'n'); }
    append_char_to_buffer(&codegen->output, '"');
}

static void emit_format_string_normalized(CodeGen *codegen, const char *fmt_str, AstNode *call_node) {
    emit_format_string_normalized_extended(codegen, fmt_str, call_node, false);
}

/* Record the conversion spec char of each directive in fmt_str, 1:1 with the
 * arguments that follow (matching emit_format_string_normalized's directive
 * walk). Returns the count recorded, capped at max. */
static int scan_format_specs(const char *fmt_str, char *specs, int max) {
    const char *p = fmt_str;
    int n = 0;
    while (*p) {
        if (*p != '%') { p++; continue; }
        p++;
        if (!*p) break;
        if (*p == '%') { p++; continue; }
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
        if (*p == 'h') { p++; if (*p == 'h') p++; }
        else if (*p == 'l') { p++; if (*p == 'l') p++; }
        else if (*p == 'L') p++;
        if (!*p) break;
        if (n < max) specs[n] = *p;
        n++;
        p++;
    }
    return n;
}

static void emit_format_arguments(CodeGen *codegen, AstNode *node, int start_idx) {
    char specs[64];
    int nspecs = 0;
    AstNode *fmt0 = node->data.call.args[0];
    if (fmt0->kind == NODE_STRING_VALUE)
        nspecs = scan_format_specs(fmt0->data.string_value.value, specs, 64);
    for (int i = start_idx; i < node->data.call.arg_count; i++) {
        emit(codegen, ", ");
        AstNode *arg = node->data.call.args[i];
        GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        if (arg_type && arg_type->name && is_bigint_type(arg_type->name)) {
            /* Struct-backed big integers cannot ride in a printf vararg slot;
             * the directive was rewritten to %s, so pass a converted string. */
            const char *pfx = bigint_prefix(arg_type->name);
            char spec = (i - 1 >= 0 && i - 1 < nspecs) ? specs[i - 1] : 'd';
            if (spec == 'x' || spec == 'X') {
                emit_formatted(codegen, "%s_to_hex_string(gray_default_arena, ", pfx);
                emit_expression(codegen, arg);
                emit_formatted(codegen, ", %s).data", spec == 'X' ? "true" : "false");
            } else if (spec == 'o') {
                emit_formatted(codegen, "%s_to_octal_string(gray_default_arena, ", pfx);
                emit_expression(codegen, arg);
                emit(codegen, ").data");
            } else {
                emit_formatted(codegen, "%s_to_string(gray_default_arena, ", pfx);
                emit_expression(codegen, arg);
                emit(codegen, ").data");
            }
        } else if (arg_type && arg_type->kind == TK_STRING) {
            emit_expression(codegen, arg);
            emit(codegen, ".data");
        } else if (arg_type && arg_type->kind == TK_BOOL) {
            emit_expression(codegen, arg);
            emit(codegen, " ? \"true\" : \"false\"");
        } else if (arg_type && arg_type->kind == TK_INT && !is_bigint_type(arg_type->name)) {
            /* The directive may have been upgraded to %lld (a 64-bit read),
             * but an integer literal emits as C `int`. Cast so the vararg
             * slot always carries the full width — the Win64 ABI leaves the
             * upper half of a 32-bit store as garbage. */
            emit(codegen, "(long long)(");
            emit_expression(codegen, arg);
            emit(codegen, ")");
        } else if (arg_type && arg_type->kind == TK_UINT && !is_bigint_type(arg_type->name)) {
            emit(codegen, "(unsigned long long)(");
            emit_expression(codegen, arg);
            emit(codegen, ")");
        } else {
            emit_expression(codegen, arg);
        }
    }
}

/* --- Composite type printing --- */

/* Global counter for unique print variable names */
static int _gray_print_uid = 0;

/* Look up a struct declaration by name */
static AstNode *find_struct_declaration(CodeGen *codegen, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < codegen->struct_decl_count; i++) {
        if (strcmp(codegen->struct_decls[i]->data.struct_decl.name, name) == 0) {
            return codegen->struct_decls[i];
        }
    }
    /* Declarations are keyed by their mangled spelling, but a type is written
     * as it is named where it appears — bare inside its own module, `using`'d,
     * or qualified. Matching the written name alone found nothing for a struct
     * declared in another module, and every caller reads that as "not a struct
     * I know": the deep-copy walk then reported no heap-backed fields, so a
     * function returning such a struct skipped its escape copy and handed back
     * maps, arrays, and strings pointing into its own destroyed arena. */
    const char *resolved = codegen_resolve_type(codegen, name);
    if (resolved && resolved != name && strcmp(resolved, name) != 0) {
        for (int i = 0; i < codegen->struct_decl_count; i++) {
            if (strcmp(codegen->struct_decls[i]->data.struct_decl.name, resolved) == 0) {
                return codegen->struct_decls[i];
            }
        }
    }
    return NULL;
}

/* Emit C statements that print the value of c_expr (of type t) to stream.
 * stream is "stdout" or "stderr". Handles all types recursively. */
/* Cycle guard for emit_value_print struct recursion. */
static const char *emit_value_print_visiting[CYCLE_GUARD_DEPTH];
static int emit_value_print_depth = 0;

static void emit_value_print(CodeGen *codegen, const char *c_expr, GrayType *type, const char *stream, bool in_container) {
    if (!type || type->kind == TK_UNKNOWN) {
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%%lld\", (long long)(%s));\n", stream, c_expr);
        return;
    }

    /* A string-backed enum is a GrayString at the C level, not an integer;
     * casting it to long long is invalid C. Print its string value, matching
     * how `println` renders such an enum directly. */
    if (type->kind == TK_ENUM && type->name &&
        codegen_enum_is_string(codegen, type->name)) {
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%%.*s\", (int)(%s).len, (%s).data);\n",
               stream, c_expr, c_expr);
        return;
    }

    /* ErrorCode (and #error_code enums): print the variant name, not the raw
     * slot number. */
    if (type->kind == TK_ENUM && codegen_enum_is_error_code(codegen, type->name)) {
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%%s\", gray_error_code_name((int64_t)(%s)));\n",
               stream, c_expr);
        return;
    }

    switch (type->kind) {
    case TK_INT: case TK_BYTE: case TK_ENUM:
        if (type->name && is_bigint_type(type->name)) {
            const char *pfx = bigint_prefix(type->name);
            emit_indent(codegen);
            emit_formatted(codegen, "{ GrayString _bs = %s_to_string(gray_default_arena, %s); fprintf(%s, \"%%.*s\", (int)_bs.len, _bs.data); }\n",
                   pfx, c_expr, stream);
        } else {
            emit_indent(codegen);
            emit_formatted(codegen, "fprintf(%s, \"%%lld\", (long long)(%s));\n", stream, c_expr);
        }
        break;
    case TK_UINT:
        if (type->name && is_bigint_type(type->name)) {
            const char *pfx = bigint_prefix(type->name);
            emit_indent(codegen);
            emit_formatted(codegen, "{ GrayString _bs = %s_to_string(gray_default_arena, %s); fprintf(%s, \"%%.*s\", (int)_bs.len, _bs.data); }\n",
                   pfx, c_expr, stream);
        } else {
            emit_indent(codegen);
            emit_formatted(codegen, "fprintf(%s, \"%%llu\", (unsigned long long)(%s));\n", stream, c_expr);
        }
        break;
    case TK_FLOAT:
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%%g\", (double)(%s));\n", stream, c_expr);
        break;
    case TK_STRING:
        emit_indent(codegen);
        if (in_container) {
            emit_formatted(codegen, "fprintf(%s, \"\\\"%%.*s\\\"\", (int)(%s).len, (%s).data);\n",
                   stream, c_expr, c_expr);
        } else {
            emit_formatted(codegen, "fprintf(%s, \"%%.*s\", (int)(%s).len, (%s).data);\n",
                   stream, c_expr, c_expr);
        }
        break;
    case TK_BOOL:
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%%s\", (%s) ? \"true\" : \"false\");\n",
               stream, c_expr);
        break;
    case TK_CHAR:
        emit_indent(codegen);
        if (in_container) {
            emit_formatted(codegen, "{ GrayString _cs = gray_builtin_char_to_utf8(gray_default_arena, %s); fprintf(%s, \"'\"); fwrite(_cs.data, 1, (size_t)_cs.len, %s); fprintf(%s, \"'\"); }\n", c_expr, stream, stream, stream);
        } else {
            emit_formatted(codegen, "{ GrayString _cs = gray_builtin_char_to_utf8(gray_default_arena, %s); fwrite(_cs.data, 1, (size_t)_cs.len, %s); }\n", c_expr, stream);
        }
        break;
    case TK_NIL:
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"nil\");\n", stream);
        break;
    case TK_ARRAY: {
        int uid = _gray_print_uid++;
        const char *elem_tn = type->element_type ? type->element_type : "int";
        GrayType *elem_t = type_from_name(elem_tn);
        char c_elem[TYPE_NAME_MAX];
        strncpy(c_elem, gray_type_to_c_codegen(codegen, elem_tn), sizeof(c_elem) - 1);
        c_elem[sizeof(c_elem) - 1] = '\0';

        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"{\");\n", stream);
        emit_indent(codegen);
        emit_formatted(codegen, "for (int32_t _gray_pi%d = 0; _gray_pi%d < (%s).len; _gray_pi%d++) {\n",
               uid, uid, c_expr, uid);
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "if (_gray_pi%d > 0) fprintf(%s, \", \");\n", uid, stream);

        /* For composite element types, capture in temp var */
        char elem_expr[MSG_BUF_SIZE];
        if (elem_t->kind == TK_STRUCT || elem_t->kind == TK_ARRAY ||
            elem_t->kind == TK_MAP || elem_t->kind == TK_POINTER) {
            int euid = _gray_print_uid++;
            emit_indent(codegen);
            emit_formatted(codegen, "%s _gray_pv%d = GRAY_ARRAY_GET((%s), %s, _gray_pi%d);\n",
                   c_elem, euid, c_expr, c_elem, uid);
            snprintf(elem_expr, sizeof(elem_expr), "_gray_pv%d", euid);
        } else {
            snprintf(elem_expr, sizeof(elem_expr),
                     "GRAY_ARRAY_GET((%s), %s, _gray_pi%d)", c_expr, c_elem, uid);
        }

        emit_value_print(codegen, elem_expr, elem_t, stream, true);

        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"}\");\n", stream);
        break;
    }
    case TK_MAP: {
        int uid = _gray_print_uid++;
        const char *key_tn = type->key_type ? type->key_type : "string";
        const char *val_tn = type->value_type ? type->value_type : "int";
        GrayType *key_t = type_from_name(key_tn);
        GrayType *val_t = type_from_name(val_tn);
        char c_key[TYPE_NAME_MAX], c_val[TYPE_NAME_MAX];
        strncpy(c_key, gray_type_to_c_codegen(codegen, key_tn), sizeof(c_key) - 1);
        c_key[sizeof(c_key) - 1] = '\0';
        strncpy(c_val, gray_type_to_c_codegen(codegen, val_tn), sizeof(c_val) - 1);
        c_val[sizeof(c_val) - 1] = '\0';

        char mi[SHORT_VAR_BUF], sl[SHORT_VAR_BUF], fst[SHORT_VAR_BUF];
        snprintf(mi, sizeof(mi), "_gray_mi%d", uid);
        snprintf(sl, sizeof(sl), "_gray_sl%d", uid);
        snprintf(fst, sizeof(fst), "_gray_fst%d", uid);

        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"{\");\n", stream);
        emit_indent(codegen);
        emit_formatted(codegen, "if ((%s).count == 0) fprintf(%s, \":\");\n", c_expr, stream);
        emit_indent(codegen);
        emit_formatted(codegen, "bool %s = true;\n", fst);
        emit_indent(codegen);
        emit_formatted(codegen, "for (int32_t %s = 0; %s < (%s).order_len; %s++) {\n",
               mi, mi, c_expr, mi);
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "int32_t %s = (%s).order[%s];\n", sl, c_expr, mi);
        emit_indent(codegen);
        emit_formatted(codegen, "if (%s < 0) continue;\n", sl);
        emit_indent(codegen);
        emit_formatted(codegen, "if (!%s) fprintf(%s, \", \");\n", fst, stream);
        emit_indent(codegen);
        emit_formatted(codegen, "%s = false;\n", fst);

        /* Print key */
        char key_expr[MSG_BUF_SIZE];
        snprintf(key_expr, sizeof(key_expr),
                 "*(%s *)gray_map_key_at(&(%s), %s)", c_key, c_expr, sl);
        emit_value_print(codegen, key_expr, key_t, stream, true);

        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \": \");\n", stream);

        /* Print value */
        char val_expr[MSG_BUF_SIZE];
        snprintf(val_expr, sizeof(val_expr),
                 "*(%s *)gray_map_value_at(&(%s), %s)", c_val, c_expr, sl);
        emit_value_print(codegen, val_expr, val_t, stream, true);

        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"}\");\n", stream);
        break;
    }
    case TK_STRUCT: {
        const char *struct_name = type->name;
        AstNode *sdecl = find_struct_declaration(codegen, struct_name);
        /* Use the user-facing name (without module prefix) for display.
         * Check struct decls first, then enum decls (enums are struct-backed). */
        const char *display_name = struct_name;
        if (sdecl && sdecl->data.struct_decl.original_name) {
            display_name = sdecl->data.struct_decl.original_name;
        } else {
            int eidx = codegen_enum_index(codegen, struct_name);
            if (eidx >= 0 && codegen->enum_decls[eidx] &&
                codegen->enum_decls[eidx]->data.enum_decl.original_name) {
                display_name = codegen->enum_decls[eidx]->data.enum_decl.original_name;
            }
        }

        /* Cycle detection: if already printing this struct type, emit a
         * placeholder to avoid infinite recursion on circular references. */
        for (int _j = 0; _j < emit_value_print_depth; _j++) {
            if (emit_value_print_visiting[_j] && strcmp(emit_value_print_visiting[_j], struct_name) == 0) {
                emit_indent(codegen);
                emit_formatted(codegen, "fprintf(%s, \"%s{...}\");\n", stream, display_name);
                break;
            }
        }
        bool _already = false;
        for (int _j = 0; _j < emit_value_print_depth; _j++) {
            if (emit_value_print_visiting[_j] && strcmp(emit_value_print_visiting[_j], struct_name) == 0) {
                _already = true; break;
            }
        }
        if (_already) break;

        if (emit_value_print_depth < CYCLE_GUARD_DEPTH) emit_value_print_visiting[emit_value_print_depth++] = struct_name;

        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%s{\");\n", stream, display_name);

        if (sdecl) {
            for (int i = 0; i < sdecl->data.struct_decl.field_count; i++) {
                StructField *field = &sdecl->data.struct_decl.fields[i];
                if (i > 0) {
                    emit_indent(codegen);
                    emit_formatted(codegen, "fprintf(%s, \", \");\n", stream);
                }
                emit_indent(codegen);
                emit_formatted(codegen, "fprintf(%s, \"%s: \");\n", stream, field->name);

                char field_expr[MSG_BUF_SIZE];
                snprintf(field_expr, sizeof(field_expr), "(%s).%s", c_expr, field->name);
                GrayType *field_graytype = type_from_name(field->type_name);
                emit_value_print(codegen, field_expr, field_graytype, stream, true);
            }
        } else if (struct_name && strcmp(struct_name, "SourceLocation") == 0) {
            /* Compiler-registered struct with no AST declaration: its fields
             * are known but find_struct_declaration() returns NULL, so walk
             * them explicitly instead of emitting an empty SourceLocation{}. */
            const char *sl_names[3] = { "file", "line", "column" };
            const char *sl_types[3] = { "string", "int", "int" };
            for (int i = 0; i < 3; i++) {
                if (i > 0) {
                    emit_indent(codegen);
                    emit_formatted(codegen, "fprintf(%s, \", \");\n", stream);
                }
                emit_indent(codegen);
                emit_formatted(codegen, "fprintf(%s, \"%s: \");\n", stream, sl_names[i]);
                char field_expr[MSG_BUF_SIZE];
                snprintf(field_expr, sizeof(field_expr), "(%s).%s", c_expr, sl_names[i]);
                emit_value_print(codegen, field_expr, type_from_name(sl_types[i]), stream, true);
            }
        }

        emit_value_print_depth--;
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"}\");\n", stream);
        break;
    }
    case TK_POINTER: {
        /* Print the address as hex (0x...). Pointers are addresses; printing
         * the pointee instead would be surprising and lose the only thing
         * a pointer carries. Use 'p^' if you actually want the pointee. */
        emit_indent(codegen);
        emit_formatted(codegen, "if ((%s) == NULL) {\n", c_expr);
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"nil\");\n", stream);
        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "} else {\n");
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"0x%%\" PRIxPTR, (uintptr_t)(%s));\n", stream, c_expr);
        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        break;
    }
    default:
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"%%lld\", (long long)(%s));\n", stream, c_expr);
        break;
    }
}

/* Try to emit a composite type print. Returns true if handled. */
static bool emit_composite_print(CodeGen *codegen, AstNode *node,
                                  const char *stream, bool newline) {
    if (node->data.call.arg_count < 1) return false;

    AstNode *arg = unwrap_reference_argument(node->data.call.args[0]);
    GrayType *type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
    if (!type) return false;
    if (type->kind != TK_STRUCT && type->kind != TK_ARRAY &&
        type->kind != TK_MAP && type->kind != TK_POINTER) return false;
    /* Enum types are stored as TK_STRUCT but should print as integers */
    if (type->kind == TK_STRUCT && type->name && codegen_is_enum(codegen, type->name)) return false;

    /* Emit a block to scope temp variables */
    emit(codegen, "{\n");
    codegen->indent++;

    /* Capture expression in temp var to evaluate only once */
    int uid = _gray_print_uid++;
    char c_type[TYPE_NAME_MAX];
    if (type->kind == TK_ARRAY) snprintf(c_type, sizeof(c_type), "GrayArray");
    else if (type->kind == TK_MAP) snprintf(c_type, sizeof(c_type), "GrayMap");
    else if (type->kind == TK_POINTER) {
        const char *pointee_tn = type->element_type ? type->element_type : "int";
        snprintf(c_type, sizeof(c_type), "%s *", gray_type_to_c_codegen(codegen, pointee_tn));
    }
    else { strncpy(c_type, gray_type_to_c_codegen(codegen, type_name(type)), sizeof(c_type) - 1); c_type[sizeof(c_type) - 1] = '\0'; }

    emit_indent(codegen);
    emit_formatted(codegen, "%s _gray_pv%d = ", c_type, uid);
    emit_expression(codegen, arg);
    emit(codegen, ";\n");

    char var[SHORT_VAR_BUF];
    snprintf(var, sizeof(var), "_gray_pv%d", uid);

    emit_value_print(codegen, var, type, stream, false);

    if (newline) {
        emit_indent(codegen);
        emit_formatted(codegen, "fprintf(%s, \"\\n\");\n", stream);
    }

    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
    emit_indent(codegen);
    emit(codegen, "(void)0"); /* Absorb trailing ;\n from emit_expression_statement */

    return true;
}

/* Shared emitter for print/println/eprint/eprintln argument formatting.
 * `variant` is the C builtin name fragment (e.g. "println", "eprint"). */
static void emit_print_variant(CodeGen *codegen, AstNode *node, const char *variant) {
    AstNode *arg = unwrap_reference_argument(node->data.call.args[0]);
    GrayType *arg_t = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
    if (arg_t && arg_t->kind == TK_ERROR) {
        emit_formatted(codegen, "gray_builtin_%s_str(", variant);
        emit_expression(codegen, arg);
        emit(codegen, " ? ");
        emit_expression(codegen, arg);
        emit(codegen, "->msg : gray_string_lit(\"nil\"))");
    } else if (arg_t && arg_t->kind == TK_ENUM &&
               codegen_enum_is_error_code(codegen, arg_t->name)) {
        emit_formatted(codegen, "gray_builtin_%s_str(gray_string_lit(gray_error_code_name((int64_t)(", variant);
        emit_expression(codegen, arg);
        emit(codegen, "))))");
    } else if (arg_t && arg_t->kind == TK_STRUCT && arg_t->name &&
               strcmp(arg_t->name, "UUID") == 0) {
        emit_formatted(codegen, "gray_builtin_%s_str(", variant);
        emit_expression(codegen, arg);
        emit(codegen, ".value)");
    } else {
        const char *bi_type = resolve_bigint_type(codegen, arg);
        if (bi_type) {
            emit_formatted(codegen, "gray_builtin_%s_str(%s_to_string(gray_default_arena, ", variant, bigint_prefix(bi_type));
            emit_expression(codegen, arg);
            emit(codegen, "))");
        } else {
            emit_formatted(codegen, "gray_builtin_%s%s(", variant, resolve_print_suffix(codegen, arg));
            emit_expression(codegen, arg);
            emit(codegen, ")");
        }
    }
}

/* --- Builtin call handler (no-module functions) --- */

static bool emit_builtin_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "println") == 0) {
        if (node->data.call.arg_count == 0) {
            emit(codegen, "putchar('\\n')");
        } else {
            if (emit_composite_print(codegen, node, "stdout", true)) return true;
            emit_print_variant(codegen, node, "println");
        }
        return true;
    }

    if (strcmp(func, "len") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        GrayType *type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        if (type && type->kind == TK_MAP) {
            emit(codegen, "(int64_t)(");
            emit_expression(codegen, arg);
            emit(codegen, ").count");
        } else {
            emit(codegen, "(int64_t)(");
            emit_expression(codegen, arg);
            emit(codegen, ").len");
        }
        return true;
    }

    if (strcmp(func, "type_of") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        /* Bigint type_of: return the exact type name */
        const char *bi_type = resolve_bigint_type(codegen, arg);
        if (bi_type) {
            emit_formatted(codegen, "gray_string_lit(\"%s\")", bi_type);
            return true;
        }
        GrayType *type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        /* Range expression: type_of(range(0, 5)) → "Range<int>" */
        if (arg->kind == NODE_RANGE_EXPR ||
            (arg->kind == NODE_CALL_EXPR && arg->data.call.function->kind == NODE_LABEL &&
             strcmp(arg->data.call.function->data.label.value, "range") == 0)) {
            emit_formatted(codegen, "gray_string_lit(\"Range<int>\")");
            return true;
        }
        /* Enum member access: type_of(Color.RED) → "Color" */
        if (ast_member_qualifier(arg) &&
            type && (type->kind == TK_INT || type->kind == TK_UINT || type->kind == TK_STRING)) {
            const char *obj_name = ast_member_qualifier(arg);
            if (obj_name[0] >= 'A' && obj_name[0] <= 'Z' &&
                strcmp(obj_name, "std") != 0 && strcmp(obj_name, "math") != 0 &&
                strcmp(obj_name, "os") != 0) {
                emit_formatted(codegen, "gray_string_lit(\"%s\")", obj_name);
                return true;
            }
        }
        char written[MSG_BUF_SIZE], written2[MSG_BUF_SIZE];
        if (type && type->kind == TK_ARRAY && type->element_type) {
            emit_formatted(codegen, "gray_string_lit(\"[%s]\")",
                codegen_written_type_name(codegen, type->element_type, written, sizeof(written)));
        } else if (type && type->kind == TK_MAP) {
            const char *kt = type->key_type ? type->key_type : "unknown";
            const char *vt = type->value_type ? type->value_type : "unknown";
            emit_formatted(codegen, "gray_string_lit(\"map[%s:%s]\")",
                codegen_written_type_name(codegen, kt, written, sizeof(written)),
                codegen_written_type_name(codegen, vt, written2, sizeof(written2)));
        } else if (type && type->kind == TK_POINTER && type->element_type) {
            emit_formatted(codegen, "gray_string_lit(\"^%s\")",
                codegen_written_type_name(codegen, type->element_type, written, sizeof(written)));
        } else {
            const char *type_str = type ? type_name(type) : "unknown";
            emit_formatted(codegen, "gray_string_lit(\"%s\")",
                codegen_written_type_name(codegen, type_str, written, sizeof(written)));
        }
        return true;
    }

    if (strcmp(func, "fields") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        GrayType *type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        const char *sname = NULL;
        if (type && type->kind == TK_STRUCT && type->name) {
            sname = type->name;
        } else if (type && type->kind == TK_POINTER && type->element_type) {
            sname = type->element_type;
        }
        AstNode *sdecl = sname ? find_struct_declaration(codegen, sname) : NULL;
        if (sdecl) {
            int fc = sdecl->data.struct_decl.field_count;
            if (fc == 0) {
                emit(codegen, "gray_array_from(gray_default_arena, (GrayString[]){gray_string_lit(\"\")}, sizeof(GrayString), 0)");
            } else {
                emit(codegen, "gray_array_from(gray_default_arena, (GrayString[]){");
                for (int i = 0; i < fc; i++) {
                    if (i > 0) emit(codegen, ", ");
                    emit_formatted(codegen, "gray_string_lit(\"%s\")", sdecl->data.struct_decl.fields[i].name);
                }
                emit_formatted(codegen, "}, sizeof(GrayString), %d)", fc);
            }
        } else {
            emit(codegen, "gray_array_from(gray_default_arena, (GrayString[]){gray_string_lit(\"\")}, sizeof(GrayString), 0)");
        }
        return true;
    }

    if (strcmp(func, "size_of") == 0 && node->data.call.arg_count == 1) {
        AstNode *type_arg = node->data.call.args[0];
        if (type_arg->kind == NODE_LABEL) {
            emit_formatted(codegen, "(int64_t)sizeof(%s)", gray_type_to_c_codegen(codegen, type_arg->data.label.value));
        } else {
            /* Literal or expression: infer C type and emit sizeof() */
            const char *c_type = NULL;
            if (type_arg->kind == NODE_INT_VALUE) {
                c_type = "int64_t";
            } else if (type_arg->kind == NODE_FLOAT_VALUE) {
                c_type = "double";
            } else if (type_arg->kind == NODE_BOOL_VALUE) {
                c_type = "bool";
            } else if (type_arg->kind == NODE_STRING_VALUE ||
                       type_arg->kind == NODE_INTERPOLATED_STRING) {
                c_type = "GrayString";
            } else if (type_arg->kind == NODE_CHAR_VALUE) {
                c_type = "int32_t";
            } else {
                /* Fallback: consult the type table */
                GrayType *type = codegen->type_table ? typetable_get(codegen->type_table, type_arg) : NULL;
                if (type && type->name) c_type = gray_type_to_c_codegen(codegen, type->name);
            }
            if (c_type) {
                emit_formatted(codegen, "(int64_t)sizeof(%s)", c_type);
            } else {
                emit(codegen, "0");
            }
        }
        return true;
    }

    if (strcmp(func, "addr") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        /* Special case: addr(p^.field) or addr(p.field) where p: ^T.
         * Normal codegen wraps pointer deref member access in a GCC
         * statement expression → rvalue; &(rvalue) is illegal in C.
         * Detect these patterns and emit &(_dp->field) directly so
         * GRAY_ARRAY_GET / clang receive a proper assignable target. */
        AstNode *addr_ptr_expr = NULL;
        const char *addr_field = NULL;
        if (arg->kind == NODE_MEMBER_EXPR) {
            AstNode *obj = arg->data.member.object;
            if (obj->kind == NODE_POSTFIX_EXPR &&
                obj->data.postfix.op == TOK_CARET) {
                /* addr(p^.field): p is the underlying pointer */
                addr_ptr_expr = obj->data.postfix.left;
                addr_field = arg->data.member.member;
            } else {
                /* addr(p.field) where p is a pointer type (auto-deref) */
                GrayType *obj_t = codegen->type_table
                    ? typetable_get(codegen->type_table, obj) : NULL;
                if (obj_t && obj_t->kind == TK_POINTER) {
                    addr_ptr_expr = obj;
                    addr_field = arg->data.member.member;
                }
            }
        }
        if (addr_ptr_expr && addr_field) {
            int my_dp = codegen_next_id(codegen);
            emit_formatted(codegen, "({ __auto_type _aadp%d = ", my_dp);
            emit_expression(codegen, addr_ptr_expr);
            emit_formatted(codegen, "; if (!_aadp%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                      "&_aadp%d->%s; })",
                  my_dp, codegen->file, node->token.line, my_dp, sanitize_name(addr_field));
        } else if (arg->kind == NODE_POSTFIX_EXPR && arg->data.postfix.op == TOK_CARET) {
            /* addr(p^): &(*p) simplifies to p; nil-check p first */
            AstNode *inner = arg->data.postfix.left;
            bool inner_raw = (inner->kind == NODE_LABEL &&
                              is_raw_variable(codegen, inner->data.label.value));
            if (inner_raw) {
                emit_expression(codegen, inner);
            } else {
                int my_dp = codegen_next_id(codegen);
                emit_formatted(codegen, "({ __auto_type _aadp%d = ", my_dp);
                emit_expression(codegen, inner);
                emit_formatted(codegen, "; if (!_aadp%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } _aadp%d; })",
                      my_dp, codegen->file, node->token.line, my_dp);
            }
        } else {
            /* addr() returns a pointer to the argument */
            emit(codegen, "&");
            emit_expression(codegen, arg);
        }
        return true;
    }

    if (strcmp(func, "raw") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        /* raw() is the unsafe escape hatch that bypasses const-source write
         * protection.  When the source variable is const, &var produces a
         * const-qualified pointer in C, which __auto_type would propagate.
         * Look up the result type and emit an explicit cast to strip const. */
        const char *raw_cast = NULL;
        GrayType *raw_t = codegen->type_table
            ? typetable_get(codegen->type_table, node) : NULL;
        if (raw_t && raw_t->kind == TK_POINTER && raw_t->name) {
            char pname[MSG_BUF_SIZE];
            snprintf(pname, sizeof(pname), "^%s", raw_t->name);
            raw_cast = gray_type_to_c_codegen(codegen, pname);
        }
        /* Handle raw(p^.field) / raw(p.field) where p is a pointer —
         * same pattern as addr() but without the nil check. */
        AstNode *raw_ptr_expr = NULL;
        const char *raw_field = NULL;
        if (arg->kind == NODE_MEMBER_EXPR) {
            AstNode *obj = arg->data.member.object;
            if (obj->kind == NODE_POSTFIX_EXPR &&
                obj->data.postfix.op == TOK_CARET) {
                raw_ptr_expr = obj->data.postfix.left;
                raw_field = arg->data.member.member;
            } else {
                GrayType *obj_t = codegen->type_table
                    ? typetable_get(codegen->type_table, obj) : NULL;
                if (obj_t && obj_t->kind == TK_POINTER) {
                    raw_ptr_expr = obj;
                    raw_field = arg->data.member.member;
                }
            }
        }
        if (raw_ptr_expr && raw_field) {
            /* raw(p^.field): bare &p->field, no nil check */
            if (raw_cast) emit_formatted(codegen, "(%s)", raw_cast);
            emit(codegen, "&(");
            emit_expression(codegen, raw_ptr_expr);
            emit_formatted(codegen, ")->%s", sanitize_name(raw_field));
        } else if (arg->kind == NODE_POSTFIX_EXPR && arg->data.postfix.op == TOK_CARET) {
            /* raw(p^): &(*p) simplifies to p, no nil check */
            if (raw_cast) emit_formatted(codegen, "(%s)", raw_cast);
            emit_expression(codegen, arg->data.postfix.left);
        } else {
            if (raw_cast) emit_formatted(codegen, "(%s)", raw_cast);
            emit(codegen, "&");
            emit_expression(codegen, arg);
        }
        return true;
    }

    if (strcmp(func, "ref") == 0 && node->data.call.arg_count == 1) {
        /* Check if argument is a function name; emit as function pointer */
        if (node->data.call.args[0]->kind == NODE_LABEL) {
            AstNode *target = find_referenced_function(codegen, node->data.call.args[0]);
            if (target) {
                /* Function reference: emit gray_fn_name (function pointer).
                 * The declaration carries the mangled name, which is the
                 * symbol the function is defined under. */
                emit_formatted(codegen, "gray_fn_%s", target->data.func_decl.name);
                return true;
            }
        }
        /* Variable reference: emit &var */
        emit(codegen, "&");
        emit_expression(codegen, node->data.call.args[0]);
        return true;
    }

    if (strcmp(func, "exit") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_exit(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "system") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_system(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "here") == 0 && node->data.call.arg_count == 0) {
        /* Compile-time substitution: emit a SourceLocation literal with the
         * source position of the 'here' identifier itself (not the '(' that
         * follows it, which is what NODE_CALL_EXPR's own token points at). */
        AstNode *function_node = node->data.call.function;
        Token tok = function_node ? function_node->token : node->token;
        /* tok.file comes straight from the parser for imported files, so it
         * still needs the separator normalization codegen->file already had. */
        char *normalized = tok.file ? normalize_path_separators(tok.file) : NULL;
        const char *file = normalized ? normalized : codegen->file;
        emit_formatted(codegen,
            "(GrayStruct_SourceLocation){.file = gray_string_lit(\"%s\"), "
            ".line = %d, .column = %d}",
            file ? file : "", tok.line, tok.column);
        free(normalized);
        return true;
    }

    if (strcmp(func, "embed") == 0 && node->data.call.arg_count == 1) {
        /* Compile-time file embedding: read the file and emit its content as
         * an GrayString literal. Path is resolved relative to the source file. */
        AstNode *arg = node->data.call.args[0];
        const char *embed_path = arg->data.string_value.value;

        char resolved[4096];
        if (embed_path[0] != '/' && codegen->file) {
            const char *last_slash = strrchr(codegen->file, '/');
            if (last_slash) {
                snprintf(resolved, sizeof(resolved), "%.*s%s",
                    (int)(last_slash - codegen->file + 1), codegen->file, embed_path);
            } else {
                snprintf(resolved, sizeof(resolved), "%s", embed_path);
            }
        } else {
            snprintf(resolved, sizeof(resolved), "%s", embed_path);
        }

        FILE *ef = fopen(resolved, "rb");
        if (!ef) {
            /* Typechecker validated this; reaching here is an ICE */
            codegen_internal_error("embed(): file not readable after typechecker validation", __FILE__, __LINE__);
            return true;
        }
        fseek(ef, 0, SEEK_END);
        long file_size = ftell(ef);
        fseek(ef, 0, SEEK_SET);

        unsigned char *file_buf = malloc((size_t)(file_size > 0 ? file_size : 1));
        size_t nread = fread(file_buf, 1, (size_t)file_size, ef);
        fclose(ef);

        /* Emit every byte as \xNN — safe because after 2 hex digits the next
         * character is always \ or " so no hex-continuation ambiguity. */
        char *esc = malloc((size_t)(nread * 4 + 16));
        size_t esc_len = 0;
        for (size_t i = 0; i < nread; i++) {
            esc_len += (size_t)snprintf(esc + esc_len, 5, "\\x%02x", file_buf[i]);
        }
        esc[esc_len] = '\0';

        /* GRAY_STRING_LIT is a compile-time macro — valid at file scope */
        emit(codegen, "GRAY_STRING_LIT(\"");
        emit(codegen, esc);
        emit(codegen, "\")");

        free(file_buf);
        free(esc);
        return true;
    }

    if (strcmp(func, "panic") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_panic_msg(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "assert") == 0 && node->data.call.arg_count >= 1) {
        emit(codegen, "gray_builtin_assert(");
        emit_expression(codegen, node->data.call.args[0]);
        if (node->data.call.arg_count >= 2) {
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[1]);
        } else {
            emit(codegen, ", gray_string_lit(\"\")");
        }
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }

    if (strcmp(func, "error") == 0 && node->data.call.arg_count >= 1) {
        /* Forms: error(msg), error(code), error(code, msg). The first arg is a
         * code unless it is a plain string; slot 0 (Unknown) is the default. */
        AstNode *a0 = node->data.call.args[0];
        GrayType *a0t = codegen->type_table ? typetable_get(codegen->type_table, a0) : NULL;
        bool first_is_code = !(a0t && a0t->kind == TK_STRING);
        emit(codegen, "gray_error_new(gray_default_arena, ");
        if (first_is_code) {
            emit(codegen, "(int64_t)(");
            emit_expression(codegen, a0);
            emit(codegen, "), ");
            if (node->data.call.arg_count >= 2) {
                emit_expression(codegen, node->data.call.args[1]);
            } else {
                emit(codegen, "gray_string_lit(\"\")");
            }
        } else {
            emit(codegen, "0, ");
            emit_expression(codegen, a0);
        }
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "input") == 0) {
        emit(codegen, "gray_builtin_input(gray_default_arena)");
        return true;
    }

    if (strcmp(func, "flush") == 0) {
        emit(codegen, "gray_builtin_flush()");
        return true;
    }


    if (strcmp(func, "eprintln") == 0) {
        if (node->data.call.arg_count == 0) {
            emit(codegen, "fputc('\\n', stderr)");
        } else {
            if (emit_composite_print(codegen, node, "stderr", true)) return true;
            emit_print_variant(codegen, node, "eprintln");
        }
        return true;
    }

    if (strcmp(func, "eprint") == 0 && node->data.call.arg_count > 0) {
        if (emit_composite_print(codegen, node, "stderr", false)) return true;
        emit_print_variant(codegen, node, "eprint");
        return true;
    }

    if (strcmp(func, "sleep_s") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_sleep_s(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "sleep_ms") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_sleep_ms(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "sleep_ns") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_sleep_ns(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    /* Bigint conversion functions: i128(), u128(), i256(), u256() */
    if (node->data.call.arg_count == 1 && is_bigint_type(func)) {
        AstNode *carg = node->data.call.args[0];
        const char *src_bi = resolve_bigint_type(codegen, carg);
        const char *pfx = bigint_prefix(func);
        if (src_bi) {
            /* Bigint→bigint cast */
            emit_formatted(codegen, "%s_from_%s(", pfx, src_bi);
            emit_expression(codegen, carg);
            emit(codegen, ")");
        } else {
            /* Scalar→bigint: e.g., gray_i128_from_i64(x) */
            emit_scalar_to_bigint(codegen, func, carg, NULL);
        }
        return true;
    }

    /* Sized type conversion functions: i8(), u16(), f32(), etc. */
    if (node->data.call.arg_count == 1) {
        const char *cast_type = NULL;
        if (strcmp(func, "int") == 0) cast_type = "int64_t";
        else if (strcmp(func, "uint") == 0) cast_type = "uint64_t";
        else if (strcmp(func, "float") == 0) cast_type = "double";
        else if (strcmp(func, "char") == 0) cast_type = "int32_t";
        else if (strcmp(func, "byte") == 0) cast_type = "uint8_t";
        else if (strcmp(func, "bool") == 0) cast_type = "bool";
        if (cast_type) {
            AstNode *carg = node->data.call.args[0];
            /* Bigint→scalar: e.g., int(x128) → gray_i128_to_i64(x128) */
            const char *src_bi = resolve_bigint_type(codegen, carg);
            if (src_bi) {
                const char *src_pfx = bigint_prefix(src_bi);
                bool src_unsigned = (strcmp(src_bi, "u128") == 0 || strcmp(src_bi, "u256") == 0);
                bool dst_unsigned = (strcmp(func, "uint") == 0);
                const char *to_suffix = (src_unsigned || dst_unsigned) ? "u64" : "i64";
                emit_formatted(codegen, "((%s)%s_to_%s(", cast_type, src_pfx, to_suffix);
                emit_expression(codegen, carg);
                emit_formatted(codegen, ", \"%s\", %d))", codegen->file, node->token.line);
                return true;
            }
            /* String→numeric conversion */
            GrayType *carg_t = codegen->type_table ? typetable_get(codegen->type_table, carg) : NULL;
            bool is_string_src = (carg->kind == NODE_STRING_VALUE || carg->kind == NODE_INTERPOLATED_STRING ||
                                  (carg_t && carg_t->kind == TK_STRING));
            if (is_string_src && (strcmp(func, "int") == 0 || strcmp(func, "uint") == 0)) {
                emit(codegen, "gray_builtin_string_to_int(");
                emit_expression(codegen, carg);
                emit(codegen, ")");
            } else if (is_string_src && strcmp(func, "float") == 0) {
                emit(codegen, "gray_builtin_string_to_float(");
                emit_expression(codegen, carg);
                emit(codegen, ")");
            } else if (strcmp(func, "int") == 0 &&
                (carg->kind == NODE_FLOAT_VALUE || (carg_t && carg_t->kind == TK_FLOAT))) {
                /* Use overflow-safe conversion for float→int */
                emit_formatted(codegen, "gray_float_to_int((double)(");
                emit_expression(codegen, carg);
                emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
            } else {
                emit_formatted(codegen, "((%s)(", cast_type);
                emit_expression(codegen, carg);
                emit(codegen, "))");
            }
            return true;
        }
        /* string() conversion */
        if (strcmp(func, "string") == 0) {
            emit_to_string(codegen, node->data.call.args[0]);
            return true;
        }
    }

    if (strcmp(func, "copy") == 0 && node->data.call.arg_count == 1) {
        AstNode *arg = node->data.call.args[0];
        GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        if (arg_type && (arg_type->kind == TK_ARRAY || arg_type->kind == TK_MAP || arg_type->kind == TK_STRUCT)) {
            /* Route every container kind through the unified deep-copy
             * emitter so nested collections, structs containing
             * collections, collections containing such structs, and any
             * transitive mix of those all come out fully independent
             * of the source , ). */
            int tag = codegen_next_id(codegen);
            const char *c_type = (arg_type->kind == TK_ARRAY) ? "GrayArray"
                               : (arg_type->kind == TK_MAP) ? "GrayMap"
                               : gray_type_to_c_codegen(codegen, arg_type->name);
            emit_formatted(codegen, "({ %s _cpy%d = ", c_type, tag);
            emit_expression(codegen, arg);
            emit(codegen, "; ");
            char src_var[SHORT_VAR_BUF];
            snprintf(src_var, sizeof(src_var), "_cpy%d", tag);
            char full_tn[MSG_BUF_SIZE];
            if (arg_type->kind == TK_ARRAY) {
                snprintf(full_tn, sizeof(full_tn), "[%s]",
                    arg_type->element_type ? arg_type->element_type : "");
            } else if (arg_type->kind == TK_MAP) {
                snprintf(full_tn, sizeof(full_tn), "map[%s:%s]",
                    arg_type->key_type ? arg_type->key_type : "",
                    arg_type->value_type ? arg_type->value_type : "");
            } else {
                const char *_name = arg_type->name ? arg_type->name : "";
                strncpy(full_tn, _name, sizeof(full_tn) - 1);
                full_tn[sizeof(full_tn) - 1] = '\0';
            }
            emit_value_deep_copy(codegen, full_tn, src_var);
            emit(codegen, "; })");
        } else {
            emit_expression(codegen, arg);
        }
        return true;
    }

    if (strcmp(func, "print") == 0 && node->data.call.arg_count > 0) {
        if (emit_composite_print(codegen, node, "stdout", false)) return true;
        emit_print_variant(codegen, node, "print");
        return true;
    }

    /* to_char(str, index); extract Nth Unicode codepoint */
    if (strcmp(func, "to_char") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_builtin_to_char(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }

    /* char_count(str); return Unicode codepoint count */
    if (strcmp(func, "char_count") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_builtin_char_count(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    /* c_string(ptr); convert C char* to Grayscale string. Copies onto the
     * arena so the result is safe to use even after the C-side buffer
     * is freed or overwritten. NULL maps to "" instead of crashing. */
    if (strcmp(func, "c_string") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_c_string_dup(gray_default_arena, (const char *)");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    return false;
}

/* --- @mem module --- */

static bool emit_mem_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "arena") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_mem_arena(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "destroy") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_mem_destroy(");
        emit_expression(codegen, node->data.call.args[0]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }
    if (strcmp(func, "reset") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_mem_reset(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "usage") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_mem_usage(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "raw_copy") == 0 && node->data.call.arg_count == 3) {
        emit(codegen, "gray_mem_copy(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "zero") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_mem_zero(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "fill") == 0 && node->data.call.arg_count == 3) {
        emit(codegen, "gray_mem_set(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "init") == 0 && node->data.call.arg_count == 2) {
        AstNode *arena_arg = node->data.call.args[0];
        AstNode *type_arg = node->data.call.args[1];
        const char *type_str = "int64_t";
        if (type_arg->kind == NODE_LABEL) {
            type_str = gray_type_to_c_codegen(codegen, type_arg->data.label.value);
        }
        emit_formatted(codegen, "(%s *)gray_arena_alloc(", type_str);
        emit_expression(codegen, arena_arg);
        emit_formatted(codegen, ", sizeof(%s))", type_str);
        return true;
    }
    if (strcmp(func, "alloc") == 0 && node->data.call.arg_count == 2) {
        AstNode *arena_arg = node->data.call.args[0];
        AstNode *value_arg = node->data.call.args[1];
        GrayType *vt = codegen->type_table ? typetable_get(codegen->type_table, value_arg) : NULL;

        /* alloc(a Arena, value T) -> ^T for every T. The string and array
         * branches below build their value in the target arena and used to
         * hand it back directly, so the same call returned a pointer or a
         * value depending on what it was given. Binding the arena once and
         * boxing whatever the branch produced keeps a single return shape
         * without evaluating the arena expression twice. */
        int tag = codegen_next_id(codegen);
        /* The pointer is typed from the value's Grayscale type, not deduced
         * from the C expression: __auto_type on a literal `64` deduces C `int`,
         * and a `^int` is int64_t *. */
        char c_val[MSG_BUF_SIZE];
        snprintf(c_val, sizeof(c_val), "%s",
                 (vt && vt->kind != TK_UNKNOWN && type_name(vt))
                     ? gray_type_to_c_codegen(codegen, type_name(vt))
                     : (value_arg->kind == NODE_ARRAY_VALUE ? "GrayArray" : "__auto_type"));
        emit_formatted(codegen, "({ GrayArena *_aa%d = ", tag);
        emit_expression(codegen, arena_arg);
        emit_formatted(codegen, "; %s _av%d = ", c_val, tag);
        if (vt && vt->kind == TK_STRING) {
            emit_formatted(codegen, "({ GrayString _s%d = ", tag);
            emit_expression(codegen, value_arg);
            emit_formatted(codegen, "; gray_string_new(_aa%d, _s%d.data, _s%d.len); })",
                           tag, tag, tag);
        } else if (value_arg->kind == NODE_ARRAY_VALUE) {
            int count = value_arg->data.array_value.count;
            GrayType *elem_t = (count > 0 && codegen->type_table)
                ? typetable_get(codegen->type_table, value_arg->data.array_value.elements[0])
                : NULL;
            const char *c_type = "int64_t";
            if (elem_t) {
                if (elem_t->kind == TK_FLOAT) c_type = "double";
                else if (elem_t->kind == TK_STRING) c_type = "GrayString";
                else if (elem_t->kind == TK_BOOL) c_type = "bool";
            }
            emit_formatted(codegen, "gray_array_from(_aa%d, (%s[]){", tag, c_type);
            for (int i = 0; i < count; i++) {
                if (i > 0) emit(codegen, ", ");
                emit_expression(codegen, value_arg->data.array_value.elements[i]);
            }
            emit_formatted(codegen, "}, sizeof(%s), %d)", c_type, count);
        } else {
            emit_expression(codegen, value_arg);
        }
        emit_formatted(codegen,
            "; __typeof__(_av%d) *_ap%d = (__typeof__(_av%d) *)gray_arena_alloc(_aa%d, sizeof(_av%d)); "
            "*_ap%d = _av%d; _ap%d; })",
            tag, tag, tag, tag, tag, tag, tag, tag);
        return true;
    }
    return false;
}

/* --- @math module --- */

static bool emit_math_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "abs") == 0 && node->data.call.arg_count == 1) {
        GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        emit_formatted(codegen, "gray_math_abs_%s(", (arg_type && arg_type->kind == TK_FLOAT) ? "float" : "int");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "neg") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "(-(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, "))");
        return true;
    }
    if ((strcmp(func, "min") == 0 || strcmp(func, "max") == 0) && node->data.call.arg_count == 2) {
        GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        emit_formatted(codegen, "gray_math_%s_%s(", func, (arg_type && arg_type->kind == TK_FLOAT) ? "float" : "int");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "clamp") == 0 && node->data.call.arg_count == 3) {
        GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        emit_formatted(codegen, "gray_math_clamp_%s(", (arg_type && arg_type->kind == TK_FLOAT) ? "float" : "int");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    /* Generic: math.func(args...) → gray_math_func(args...) */
    emit_formatted(codegen, "gray_math_%s(", func);
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[i]);
    }
    emit(codegen, ")");
    return true;
}

/* Helper: returns true when the expression is an assignable target whose address can be
 * taken directly with &.  For rvalues (function calls, literals, etc.) we must
 * materialise into a statement-expression temporary first. */
static bool expression_is_assignable(AstNode *expr) {
    return expr->kind == NODE_LABEL ||
           expr->kind == NODE_MEMBER_EXPR ||
           expr->kind == NODE_INDEX_EXPR;
}

/* Emit &expr, materialising rvalues into a statement-expression temporary
 * whose name is generated internally via codegen_next_id(). */
static void emit_address_of(CodeGen *codegen, AstNode *expr) {
    /* Anything reached through a pointer is emitted as a nil-checked GCC
     * statement expression, whose result is an rvalue — `&` on one of those
     * is invalid C, and materialising a copy instead would silently drop the
     * callee's mutations. Hoist the nil check and take the address off the
     * pointer itself, which is already an assignable target.
     * Covers `p.field` (auto-deref), `p^.field`, and a bare `p^`. */
    AstNode *ptr_expr = NULL;      /* pointer to nil-check */
    /* Field path from the pointee down to the target, innermost last
     * (["inner", "items"] for `p.inner.items`). Empty for a bare deref. */
    #define ADDR_OF_MAX_FIELDS 16
    const char *field_path[ADDR_OF_MAX_FIELDS];
    int field_depth = 0;
    if (expr->kind == NODE_MEMBER_EXPR) {
        /* Walk a chain of value-struct field accesses down to the first base
         * reached through a pointer, so the whole chain is taken off that
         * pointer as one lvalue (&_ap->inner.items) rather than off a
         * by-value statement-expression result. */
        AstNode *cur = expr;
        while (cur->kind == NODE_MEMBER_EXPR && field_depth < ADDR_OF_MAX_FIELDS) {
            AstNode *obj = cur->data.member.object;
            field_path[field_depth++] = cur->data.member.member;
            bool obj_is_ref = (obj->kind == NODE_LABEL &&
                is_reference_variable(codegen, obj->data.label.value));
            GrayType *obj_t = codegen->type_table
                ? typetable_get(codegen->type_table, obj) : NULL;
            if (!obj_is_ref && obj_t && obj_t->kind == TK_POINTER) {
                ptr_expr = obj;
                break;
            }
            if (obj->kind == NODE_POSTFIX_EXPR && obj->data.postfix.op == TOK_CARET) {
                /* p^.field...: strip the deref, use the underlying pointer */
                ptr_expr = obj->data.postfix.left;
                break;
            }
            cur = obj;
        }
        if (!ptr_expr) field_depth = 0;
    } else if (expr->kind == NODE_POSTFIX_EXPR && expr->data.postfix.op == TOK_CARET) {
        /* p^: the pointer already has the type the callee wants */
        ptr_expr = expr->data.postfix.left;
    }
    if (ptr_expr) {
        bool raw = (ptr_expr->kind == NODE_LABEL &&
            is_raw_variable(codegen, ptr_expr->data.label.value));
        if (raw && field_depth == 0) {
            emit_expression(codegen, ptr_expr);
            return;
        }
        int id = codegen_next_id(codegen);
        emit_formatted(codegen, "({ __auto_type _ap%d = ", id);
        emit_expression(codegen, ptr_expr);
        if (raw) {
            emit(codegen, "; ");
        } else {
            emit_formatted(codegen, "; if (!_ap%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ",
                id, codegen->file, expr->token.line);
        }
        if (field_depth > 0) {
            emit_formatted(codegen, "&_ap%d", id);
            for (int i = field_depth - 1; i >= 0; i--)
                emit_formatted(codegen, "%s%s", i == field_depth - 1 ? "->" : ".",
                    sanitize_name(field_path[i]));
            emit(codegen, "; })");
        } else {
            emit_formatted(codegen, "_ap%d; })", id);
        }
        return;
    }
    #undef ADDR_OF_MAX_FIELDS
    if (expression_is_assignable(expr)) {
        emit(codegen, "&");
        emit_expression(codegen, expr);
    } else {
        /* Materialize the rvalue as a one-element array compound literal,
         * which decays to the pointer the callee wants. Its lifetime is the
         * enclosing block — unlike a statement-expression local, whose
         * storage ends at the closing brace, leaving the escaping pointer
         * dangling (GCC -O2 reuses the slot; clang only survived by luck).
         * __typeof__ does not evaluate its operand, so the expression text
         * appears twice but runs once. */
        emit(codegen, "(__typeof__(");
        emit_expression(codegen, expr);
        emit(codegen, ")[]){");
        emit_expression(codegen, expr);
        emit(codegen, "}");
    }
}

/* Emit one call argument for a user-defined function, taking &arg when the
 * parameter is mutable. This is the logic that was duplicated (and had
 * drifted) across five call-emission paths: module-qualified calls,
 * namespaced struct-function calls, instance dispatch, and general direct
 * calls. Only the direct-call copy handled array/map elements, and only
 * the instance-dispatch copy had the &(p^) cancellation; this consolidates
 * both into every caller. */
static void emit_mutable_call_argument(CodeGen *codegen, AstNode *arg, bool mut_param) {
    if (!mut_param) {
        emit_expression(codegen, arg);
        return;
    }
    if (arg->kind == NODE_POSTFIX_EXPR && arg->data.postfix.op == TOK_CARET) {
        /* &(p^) cancels out — emit the inner pointer directly */
        emit_expression(codegen, arg->data.postfix.left);
        return;
    }
    if (arg->kind == NODE_LABEL) {
        const char *vn = arg->data.label.value;
        if (is_mutable_parameter(codegen, vn)) { emit(codegen, vn); return; }
        /* A bare name that names a module-level declaration is emitted under
         * its mangled name; resolve it the same way emit_label() does before
         * taking its address. */
        const char *resolved = codegen_resolve_ref(codegen, arg, vn);
        emit_formatted(codegen, "&%s", sanitize_name(resolved != vn ? resolved : vn));
        return;
    }
    if (arg->kind == NODE_INDEX_EXPR) {
        /* Array/map indexing always codegens as a GNU statement-expression
         * whose result is a dereferenced value, not an lvalue — `&` on that
         * is invalid C. Build a statement-expression that resolves to the
         * pointer itself instead, mirroring gray_array_get_ptr/gray_map_get. */
        GrayType *left_t = codegen->type_table
            ? typetable_get(codegen->type_table, arg->data.index_expr.left) : NULL;
        if (left_t && left_t->kind == TK_MAP) {
            const char *c_key = "GrayString";
            if (left_t->key_type) c_key = gray_map_element_c_type(codegen, left_t->key_type);
            emit_formatted(codegen, "({ %s _mk = ", c_key);
            emit_map_slot_value(codegen, left_t->key_type, arg->data.index_expr.index);
            emit(codegen, "; void *_mv = gray_map_get(&");
            emit_expression(codegen, arg->data.index_expr.left);
            emit_formatted(codegen, ", &_mk); if (!_mv) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ",
                codegen->file, arg->token.line);
            /* Computed here, after c_key has been emitted: both share the
             * one static buffer gray_type_to_c_codegen returns. */
            const char *c_val = left_t->value_type
                ? gray_map_element_c_type(codegen, left_t->value_type) : "int64_t";
            emit_formatted(codegen, "(%s *)_mv; })", c_val);
        } else {
            /* The element pointer is typed by the array's element type. A
             * fixed int64_t * here is the wrong pointer type for every
             * element that is not an integer. */
            const char *c_elem = (left_t && left_t->element_type)
                ? gray_map_element_c_type(codegen, left_t->element_type) : "int64_t";
            emit_formatted(codegen, "(%s *)gray_array_get_ptr(&", c_elem);
            emit_expression(codegen, arg->data.index_expr.left);
            emit(codegen, ", ");
            emit_expression(codegen, arg->data.index_expr.index);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, arg->token.line);
        }
        return;
    }
    /* NODE_MEMBER_EXPR (struct field) goes through emit_address_of, which
     * already knows how to take its address correctly. */
    emit_address_of(codegen, arg);
}

/* --- @maps module --- */

static bool emit_maps_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "get_keys") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_maps_get_keys(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "get_values") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_maps_get_values(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "has_key") == 0) {
        /* Key buffer must match the map's declared key storage type
         * (gray_map_element_c_type), not whatever C type the argument expression
         * happens to have; otherwise the hash/memcmp compares the wrong
         * number of bytes. */
        const char *c_key = "int64_t";
        GrayType *map_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (map_t && map_t->kind == TK_MAP && map_t->key_type)
            c_key = gray_map_element_c_type(codegen, map_t->key_type);
        emit_formatted(codegen, "({ %s _hk = ", c_key);
        emit_map_slot_value(codegen, (map_t && map_t->kind == TK_MAP) ? map_t->key_type : NULL,
            node->data.call.args[1]);
        emit(codegen, "; gray_maps_has_key(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", &_hk); })");
        return true;
    }
    if (strcmp(func, "remove_key") == 0 && node->data.call.arg_count == 2) {
        const char *c_key = "int64_t";
        GrayType *map_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (map_t && map_t->kind == TK_MAP && map_t->key_type)
            c_key = gray_map_element_c_type(codegen, map_t->key_type);
        emit_formatted(codegen, "({ %s _rk = ", c_key);
        emit_map_slot_value(codegen, (map_t && map_t->kind == TK_MAP) ? map_t->key_type : NULL,
            node->data.call.args[1]);
        emit(codegen, "; gray_map_remove(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit_formatted(codegen, ", &_rk, \"%s\", %d); })", codegen->file, node->token.line);
        return true;
    }
    if (strcmp(func, "clear") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_map_clear(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }
    if (strcmp(func, "is_empty") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_maps_is_empty(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "merge") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_maps_merge(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "is_equal") == 0 && node->data.call.arg_count == 2) {
        GrayType *map_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        bool str_keys = map_t && map_t->key_type && strcmp(map_t->key_type, "string") == 0;
        bool str_values = map_t && map_t->value_type && strcmp(map_t->value_type, "string") == 0;
        emit(codegen, "gray_maps_is_equal(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.args[1]);
        emit_formatted(codegen, ", %s, %s)", str_keys ? "true" : "false", str_values ? "true" : "false");
        return true;
    }
    if (strcmp(func, "contains_value") == 0 && node->data.call.arg_count == 2) {
        /* Determine value type from map to ensure correct size */
        GrayType *map_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *c_val_type = "int64_t";
        const char *bi_val = NULL;
        if (map_t && map_t->value_type) {
            /* Match the slot's C type (sized ints and f32 included) so the
             * staged comparison value is the same width as the stored slot. */
            c_val_type = gray_map_element_c_type(codegen, map_t->value_type);
            if (is_bigint_type(map_t->value_type)) bi_val = map_t->value_type;
        }
        emit_formatted(codegen, "({ %s _cv = ", c_val_type);
        emit_map_slot_value(codegen, bi_val, node->data.call.args[1]);
        emit(codegen, "; gray_maps_contains_value(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", &_cv); })");
        return true;
    }
    if (strcmp(func, "get_or_default") == 0 && node->data.call.arg_count == 3) {
        /* get_or_default(m, key, default); lookup key, return default if missing.
         * A wide-integer key or value needs its explicit struct type instead of
         * __auto_type/__typeof__, which would infer a plain int from a literal. */
        GrayType *map_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *bi_key = (map_t && map_t->kind == TK_MAP && map_t->key_type &&
            is_bigint_type(map_t->key_type)) ? map_t->key_type : NULL;
        const char *bi_val = (map_t && map_t->kind == TK_MAP && map_t->value_type &&
            is_bigint_type(map_t->value_type)) ? map_t->value_type : NULL;
        emit(codegen, "({ ");
        if (bi_key) emit_formatted(codegen, "%s _gk = ", bigint_prefix(bi_key));
        else emit(codegen, "__auto_type _gk = ");
        emit_map_slot_value(codegen, bi_key, node->data.call.args[1]);
        emit(codegen, "; void *_gv = gray_map_get(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", &_gk); _gv ? *(");
        if (bi_val) {
            emit(codegen, bigint_prefix(bi_val));
        } else {
            emit(codegen, "__typeof__(");
            emit_expression(codegen, node->data.call.args[2]);
            emit(codegen, ")");
        }
        emit(codegen, " *)_gv : ");
        emit_map_slot_value(codegen, bi_val, node->data.call.args[2]);
        emit(codegen, "; })");
        return true;
    }
    return false;
}

/* --- @time module --- */

static bool emit_time_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool needs_arena = (strcmp(func, "format") == 0 || strcmp(func, "to_iso") == 0 ||
        strcmp(func, "date") == 0 || strcmp(func, "to_clock") == 0);
    bool is_fallible = (strcmp(func, "parse") == 0);

    if (is_fallible) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        emit_formatted(codegen, is_multi_var ? "gray_time_%s_result(" : "gray_time_%s(", func);
        for (int i = 0; i < node->data.call.arg_count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[i]);
        }
        emit(codegen, ")");
        return true;
    }

    emit_formatted(codegen, "gray_time_%s(", func);
    if (needs_arena) emit(codegen, "gray_default_arena, ");
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @runtime module --- */

static bool emit_runtime_call(CodeGen *codegen, AstNode *node, const char *func) {
    (void)node;
    if (strcmp(func, "version") == 0) {
        emit_formatted(codegen, "gray_string_lit(\"%s\")", GRAY_VERSION);
        return true;
    }
    emit_formatted(codegen, "gray_runtime_%s()", func);
    return true;
}

/* --- @uuid module --- */

static bool emit_uuid_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "generate") == 0) {
        emit(codegen, "gray_uuid_generate(gray_default_arena)"); return true;
    }
    if (strcmp(func, "generate_compact") == 0) {
        emit(codegen, "gray_uuid_generate_compact(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(func, "generate_random") == 0) {
        emit(codegen, "gray_uuid_generate_random(gray_default_arena)"); return true;
    }
    if (strcmp(func, "generate_time_ordered") == 0) {
        emit(codegen, "gray_uuid_generate_time_ordered(gray_default_arena)"); return true;
    }
    if (strcmp(func, "is_valid") == 0) {
        emit(codegen, "gray_uuid_is_valid(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(func, "parse") == 0) {
        emit(codegen, "gray_uuid_parse(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(func, "to_string") == 0) {
        emit(codegen, "gray_uuid_to_string(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")"); return true;
    }
    return false;
}

/* --- @regex module --- */

static bool emit_regex_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "is_valid") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_regex_is_valid(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "is_match") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_regex_match(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "find") == 0 && node->data.call.arg_count == 2) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        emit_formatted(codegen, "gray_regex_find%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "find_all") == 0 && node->data.call.arg_count == 2) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        emit_formatted(codegen, "gray_regex_find_all%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "replace") == 0 && node->data.call.arg_count == 3) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        emit_formatted(codegen, "gray_regex_replace%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "split") == 0 && node->data.call.arg_count == 2) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        emit_formatted(codegen, "gray_regex_split%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @server module --- */

static bool emit_server_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "add_router") == 0) {
        emit(codegen, "gray_server_router()");
        return true;
    }
    if (strcmp(func, "add_route") == 0 && node->data.call.arg_count == 4) {
        emit(codegen, "gray_server_route(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ", (GrayResponse (*)(GrayRequest))");
        emit_expression(codegen, node->data.call.args[3]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "listen") == 0 && node->data.call.arg_count == 2) {
        /* Grayscale: server.listen(router, port)  →  C: gray_server_listen(port, &router) */
        emit(codegen, "gray_server_listen(");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "listen") == 0 && node->data.call.arg_count == 3) {
        /* Grayscale: server.listen(router, port, host)  →  C: gray_server_listen_host(port, host, &router) */
        emit(codegen, "gray_server_listen_host(");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "text") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_server_text(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "json") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_server_json(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "html") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_server_html(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "redirect") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_server_redirect(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "cors") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_server_cors(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "use") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_server_use(");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", (GrayMiddleware)");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @http module --- */

static bool emit_http_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_multi_var = is_result_temporary(codegen->current_var_name);
    const char *sfx = is_multi_var ? "_result" : "";
    if (strcmp(func, "get") == 0 && node->data.call.arg_count == 2) {
        emit_formatted(codegen, "gray_http_get%s(gray_default_arena, ", sfx);
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "post") == 0 && node->data.call.arg_count == 3) {
        emit_formatted(codegen, "gray_http_post%s(gray_default_arena, ", sfx);
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "put") == 0 && node->data.call.arg_count == 3) {
        emit_formatted(codegen, "gray_http_put%s(gray_default_arena, ", sfx);
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "delete") == 0 && node->data.call.arg_count == 2) {
        emit_formatted(codegen, "gray_http_delete%s(gray_default_arena, ", sfx);
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "head") == 0 && node->data.call.arg_count == 2) {
        emit_formatted(codegen, "gray_http_head%s(gray_default_arena, ", sfx);
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "patch") == 0 && node->data.call.arg_count == 3) {
        emit_formatted(codegen, "gray_http_patch%s(gray_default_arena, ", sfx);
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @net module --- */

static bool emit_net_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_multi_var = is_result_temporary(codegen->current_var_name);
    if (strcmp(func, "connect") == 0 && node->data.call.arg_count == 2) {
        emit_formatted(codegen, "gray_net_dial%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "close") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_net_close(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "send") == 0 && node->data.call.arg_count == 2) {
        if (is_multi_var) {
            emit(codegen, "gray_net_send_result(gray_default_arena, ");
        } else {
            emit(codegen, "gray_net_send(");
        }
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "receive") == 0 && node->data.call.arg_count == 2) {
        emit_formatted(codegen, "gray_net_recv%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "listen") == 0 && node->data.call.arg_count == 1) {
        emit_formatted(codegen, "gray_net_listen%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "listen") == 0 && node->data.call.arg_count == 2) {
        /* Grayscale: net.listen(host, port)  →  C: gray_net_listen_host(arena, host, port) */
        emit_formatted(codegen, "gray_net_listen_host%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "accept") == 0 && node->data.call.arg_count == 1) {
        emit_formatted(codegen, "gray_net_accept%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "set_timeout") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_net_set_timeout(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "resolve") == 0 && node->data.call.arg_count == 1) {
        emit_formatted(codegen, "gray_net_resolve%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @encoding module --- */

static bool emit_encoding_call(CodeGen *codegen, AstNode *node, const char *func) {
    /* Byte conversion functions (formerly @bytes) need address-of for array args */
    bool is_byte_to = (strcmp(func, "to_string") == 0 || strcmp(func, "to_hex") == 0 ||
        strcmp(func, "to_base64") == 0);
    emit_formatted(codegen, "gray_encoding_%s(gray_default_arena, ", func);
    if (is_byte_to) {
        emit_address_of(codegen, node->data.call.args[0]);
    } else {
        emit_expression(codegen, node->data.call.args[0]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @crypto module --- */

static bool emit_crypto_call(CodeGen *codegen, AstNode *node, const char *func) {
    emit_formatted(codegen, "gray_crypto_%s(gray_default_arena, ", func);
    emit_expression(codegen, node->data.call.args[0]);
    emit(codegen, ")");
    return true;
}

/* --- @binary module --- */

static bool emit_binary_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_encode = (strncmp(func, "encode", 6) == 0);
    bool is_decode = (strncmp(func, "decode", 6) == 0);
    /* Append _le for default little-endian if no endian suffix present */
    bool has_endian = strstr(func, "_le") || strstr(func, "_be");
    if (has_endian || strcmp(func, "encode_u8") == 0 || strcmp(func, "decode_u8") == 0 ||
        strcmp(func, "encode_i8") == 0 || strcmp(func, "decode_i8") == 0) {
        emit_formatted(codegen, "gray_binary_%s(", func);
    } else if (is_encode || is_decode) {
        emit_formatted(codegen, "gray_binary_%s_le(", func);
    } else {
        emit_formatted(codegen, "gray_binary_%s(", func);
    }
    if (is_encode) emit(codegen, "gray_default_arena, ");
    if (is_decode) {
        emit_address_of(codegen, node->data.call.args[0]);
        emit_formatted(codegen, ", \"%s\", %d", codegen->file, node->token.line);
    } else {
        emit_expression(codegen, node->data.call.args[0]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @csv module --- */

static bool emit_csv_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_multi_var = is_result_temporary(codegen->current_var_name);
    if (strcmp(func, "parse") == 0) {
        emit(codegen, "gray_csv_parse(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "read_file") == 0) {
        emit_formatted(codegen, "gray_csv_read%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "encode") == 0) {
        emit(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, "; gray_csv_stringify(gray_default_arena, &_csv_a); })");
        return true;
    }
    if (strcmp(func, "headers") == 0) {
        emit(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, "; gray_csv_headers(gray_default_arena, &_csv_a); })");
        return true;
    }
    if (strcmp(func, "write_file") == 0) {
        if (is_multi_var) {
            emit(codegen, "({ GrayArray _csv_a = ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, "; gray_csv_write_result(gray_default_arena, ");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", &_csv_a); })");
        } else {
            emit(codegen, "({ GrayArray _csv_a = ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, "; gray_csv_write(gray_default_arena, ");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", &_csv_a); })");
        }
        return true;
    }
    return false;
}

/* --- @json module --- */

static bool emit_json_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "encode") == 0) {
        AstNode *arg = node->data.call.args[0];
        GrayType *arg_t = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        if (arg_t && arg_t->kind == TK_MAP) {
            /* Typed map: dispatch based on value type.
             * Materialize into a temporary to handle rvalue expressions
             * (e.g. inline map literals). */
            emit(codegen, "({ GrayMap _jm = ");
            emit_expression(codegen, arg);
            if (arg_t->value_type && strcmp(arg_t->value_type, "int") == 0) {
                emit(codegen, "; gray_json_encode_map_int(gray_default_arena, &_jm); })");
            } else if (arg_t->value_type && strcmp(arg_t->value_type, "float") == 0) {
                emit(codegen, "; gray_json_encode_map_float(gray_default_arena, &_jm); })");
            } else if (arg_t->value_type && strcmp(arg_t->value_type, "bool") == 0) {
                emit(codegen, "; gray_json_encode_map_bool(gray_default_arena, &_jm); })");
            } else {
                emit(codegen, "; gray_json_encode_map(gray_default_arena, &_jm); })");
            }
        } else if (arg_t && arg_t->kind == TK_ARRAY) {
            /* Typed array: dispatch based on element type.
             * Materialize into a temporary for the same rvalue reason. */
            emit(codegen, "({ GrayArray _ja = ");
            emit_expression(codegen, arg);
            if (arg_t->element_type && strcmp(arg_t->element_type, "float") == 0) {
                emit(codegen, "; gray_json_encode_array_float(gray_default_arena, &_ja); })");
            } else if (arg_t->element_type && strcmp(arg_t->element_type, "string") == 0) {
                emit(codegen, "; gray_json_encode_array_string(gray_default_arena, &_ja); })");
            } else if (arg_t->element_type && strcmp(arg_t->element_type, "bool") == 0) {
                emit(codegen, "; gray_json_encode_array_bool(gray_default_arena, &_ja); })");
            } else {
                emit(codegen, "; gray_json_encode_array_int(gray_default_arena, &_ja); })");
            }
        } else if (arg_t && arg_t->kind == TK_INT) {
            /* Int: format as JSON number string */
            emit(codegen, "({ char _jbuf[32]; snprintf(_jbuf, sizeof(_jbuf), \"%\" PRId64, (int64_t)");
            emit_expression(codegen, arg);
            emit(codegen, "); gray_string_new(gray_default_arena, _jbuf, (int32_t)strlen(_jbuf)); })");
        } else if (arg_t && arg_t->kind == TK_FLOAT) {
            emit(codegen, "({ char _jbuf[64]; snprintf(_jbuf, sizeof(_jbuf), \"%g\", (double)");
            emit_expression(codegen, arg);
            emit(codegen, "); gray_string_new(gray_default_arena, _jbuf, (int32_t)strlen(_jbuf)); })");
        } else if (arg_t && arg_t->kind == TK_BOOL) {
            emit(codegen, "(");
            emit_expression(codegen, arg);
            emit(codegen, " ? gray_string_lit(\"true\") : gray_string_lit(\"false\"))");
        } else if (arg_t && arg_t->kind == TK_STRING) {
            /* String: wrap in quotes */
            emit(codegen, "({ GrayString _js = ");
            emit_expression(codegen, arg);
            emit(codegen, "; char *_jbuf = gray_arena_alloc(gray_default_arena, _js.len + 3); ");
            emit(codegen, "_jbuf[0] = '\"'; memcpy(_jbuf+1, _js.data, _js.len); _jbuf[_js.len+1] = '\"'; _jbuf[_js.len+2] = '\\0'; ");
            emit(codegen, "gray_string_new(gray_default_arena, _jbuf, _js.len + 2); })");
        } else {
            /* Fallback: store in temp to allow & */
            emit(codegen, "({ __auto_type _jtmp = ");
            emit_expression(codegen, arg);
            emit(codegen, "; gray_json_encode_map(gray_default_arena, (GrayMap *)&_jtmp); })");
        }
        return true;
    }
    if (strcmp(func, "decode") == 0) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        emit_formatted(codegen, "gray_json_decode%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    /* : json.parse(); dispatch to per-struct helper when the
     * call node's typetable entry is a #json struct (pushed by the
     * var_decl handler via ). Falls back to gray_json_decode for
     * the map-based path. */
    if (strcmp(func, "parse") == 0 && node->data.call.arg_count >= 1) {
        GrayType *target_t = codegen->type_table ? typetable_get(codegen->type_table, node) : NULL;
        if (target_t && target_t->kind == TK_STRUCT && target_t->name) {
            AstNode *sdecl = find_struct_declaration(codegen, target_t->name);
            if (sdecl && sdecl->data.struct_decl.is_json) {
                emit_formatted(codegen, "gray_json_parse_%s(gray_default_arena, ", target_t->name);
                emit_expression(codegen, node->data.call.args[0]);
                emit(codegen, ")");
                return true;
            }
        }
        /* Array of #json structs: [StructName] */
        if (target_t && target_t->kind == TK_ARRAY && target_t->element_type) {
            AstNode *sdecl = find_struct_declaration(codegen, target_t->element_type);
            if (sdecl && sdecl->data.struct_decl.is_json) {
                emit_formatted(codegen, "gray_json_parse_array_%s(gray_default_arena, ", target_t->element_type);
                emit_expression(codegen, node->data.call.args[0]);
                emit(codegen, ")");
                return true;
            }
        }
        /* Fallback: map-based decode */
        emit(codegen, "gray_json_decode(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    /* : json.stringify(); dispatch to per-struct helper when
     * the argument is a #json struct. */
    if (strcmp(func, "stringify") == 0 && node->data.call.arg_count >= 1) {
        AstNode *arg = node->data.call.args[0];
        GrayType *arg_t = codegen->type_table ? typetable_get(codegen->type_table, arg) : NULL;
        if (arg_t && arg_t->kind == TK_STRUCT && arg_t->name) {
            AstNode *sdecl = find_struct_declaration(codegen, arg_t->name);
            if (sdecl && sdecl->data.struct_decl.is_json) {
                emit_formatted(codegen, "gray_json_stringify_%s(gray_default_arena, ", arg_t->name);
                emit_expression(codegen, arg);
                emit(codegen, ")");
                return true;
            }
        }
        /* Fallback: encode as map */
        emit(codegen, "({ __auto_type _jtmp = ");
        emit_expression(codegen, arg);
        emit(codegen, "; gray_json_encode_map(gray_default_arena, (GrayMap *)&_jtmp); })");
        return true;
    }
    if (strcmp(func, "is_valid") == 0) {
        emit(codegen, "gray_json_is_valid(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "pretty_print") == 0) {
        emit(codegen, "gray_json_pretty_map(gray_default_arena, &");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @sqlite module --- */

static bool emit_sqlite_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_fallible = (strcmp(func, "open") == 0 || strcmp(func, "exec") == 0 ||
        strcmp(func, "query") == 0 || strcmp(func, "exec_params") == 0 ||
        strcmp(func, "query_params") == 0);
    bool is_multi_var = is_result_temporary(codegen->current_var_name);
    if (strcmp(func, "open") == 0) {
        emit_formatted(codegen, "gray_sqlite_open%s(gray_default_arena, ", (is_fallible && is_multi_var) ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "close") == 0) {
        emit(codegen, "gray_sqlite_close(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "exec") == 0) {
        if (is_multi_var) {
            emit(codegen, "gray_sqlite_exec_result(gray_default_arena, ");
        } else {
            emit(codegen, "gray_sqlite_exec(");
        }
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "exec_params") == 0) {
        if (is_multi_var) {
            emit(codegen, "gray_sqlite_exec_params_result(gray_default_arena, ");
        } else {
            emit(codegen, "gray_sqlite_exec_params(");
        }
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "query") == 0) {
        emit_formatted(codegen, "gray_sqlite_query%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "query_params") == 0) {
        emit_formatted(codegen, "gray_sqlite_query_params%s(gray_default_arena, ", is_multi_var ? "_result" : "");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @random module --- */

static bool emit_random_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "rand_float") == 0) {
        if (node->data.call.arg_count == 0) {
            emit(codegen, "gray_random_float_unit()");
        } else if (node->data.call.arg_count == 2) {
            emit(codegen, "gray_random_float_range(");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(func, "rand_int") == 0) {
        if (node->data.call.arg_count == 1) {
            emit(codegen, "gray_random_int_max(");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ")");
        } else if (node->data.call.arg_count == 2) {
            emit(codegen, "gray_random_int_range(");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(func, "rand_bool") == 0) { emit(codegen, "gray_random_bool()"); return true; }
    if (strcmp(func, "rand_byte") == 0) { emit(codegen, "gray_random_byte()"); return true; }
    if (strcmp(func, "rand_char") == 0) {
        if (node->data.call.arg_count == 2) {
            emit(codegen, "gray_random_char_range(");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, ")");
        } else {
            emit(codegen, "gray_random_char()");
        }
        return true;
    }
    if (strcmp(func, "shuffle") == 0) {
        emit(codegen, "gray_random_shuffle(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "sample") == 0) {
        emit(codegen, "gray_random_sample(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "choice") == 0) {
        /* Determine element C type from the array's type info */
        const char *c_elem = "int64_t";
        GrayType *arr_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type) {
            GrayType *et = type_from_name(arr_t->element_type);
            if (et->kind == TK_FLOAT) c_elem = "double";
            else if (et->kind == TK_BOOL) c_elem = "bool";
            else if (et->kind == TK_STRING) c_elem = "GrayString";
            else if (et->kind == TK_CHAR) c_elem = "int32_t";
            else if (et->kind == TK_BYTE) c_elem = "uint8_t";
            else if (et->kind == TK_STRUCT) c_elem = gray_type_to_c_codegen(codegen, arr_t->element_type);
            else if (et->kind == TK_ENUM) {
                c_elem = codegen_enum_is_string(codegen, arr_t->element_type)
                    ? "GrayString" : gray_type_to_c_codegen(codegen, arr_t->element_type);
            }
        }
        if (expression_is_assignable(node->data.call.args[0])) {
            emit(codegen, "({ int32_t _ri = gray_random_int_max(");
            emit_expression(codegen, node->data.call.args[0]);
            emit_formatted(codegen, ".len); *(%s *)gray_array_get_ptr(&", c_elem);
            emit_expression(codegen, node->data.call.args[0]);
            emit_formatted(codegen, ", _ri, \"%s\", %d); })", codegen->file, node->token.line);
        } else {
            emit(codegen, "({ __auto_type _ra = ");
            emit_expression(codegen, node->data.call.args[0]);
            emit_formatted(codegen, "; int32_t _ri = gray_random_int_max(_ra.len); *(%s *)gray_array_get_ptr(&_ra, _ri, \"%s\", %d); })", c_elem, codegen->file, node->token.line);
        }
        return true;
    }
    if (strcmp(func, "seed") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_random_seed(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @arrays module --- */

/* Emit &arr for arrays.append/prepend/insert_at. Identical to
 * emit_address_of — kept as a name that reads at the call sites. Keeping a
 * second implementation is what let the two drift apart, leaving pointer
 * shapes handled for arrays but not for maps. */
static void emit_array_argument_address(CodeGen *codegen, AstNode *arg) {
    emit_address_of(codegen, arg);
}

static bool emit_arrays_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "append") == 0 && node->data.call.arg_count == 2) {
        GrayType *val_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[1]) : NULL;
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *elem_tn = (arr_t && arr_t->kind == TK_ARRAY) ? arr_t->element_type : NULL;
        bool elem_is_string = (val_t && val_t->kind == TK_STRING) ||
            (elem_tn && strcmp(elem_tn, "string") == 0);
        const char *c_elem = "__auto_type";
        /* An inline tagged-enum constructor at the call site is left as
         * TK_UNKNOWN in the type table; fall back to the array's declared
         * element type so the temporary is not declared as `__auto_type` and
         * then spliced into `sizeof(__auto_type)`. */
        if (val_t && val_t->kind != TK_UNKNOWN) {
            switch (val_t->kind) {
            case TK_INT: c_elem = "int64_t"; break;
            case TK_UINT: c_elem = "uint64_t"; break;
            case TK_FLOAT: c_elem = "double"; break;
            case TK_BOOL: c_elem = "bool"; break;
            case TK_CHAR: c_elem = "int32_t"; break;
            case TK_BYTE: c_elem = "uint8_t"; break;
            case TK_STRING: c_elem = "GrayString"; break;
            case TK_ARRAY: c_elem = "GrayArray"; break;
            case TK_MAP: c_elem = "GrayMap"; break;
            case TK_FUNCTION: c_elem = "void *"; break;
            default: break;
            }
            /* Wide ints share TK_INT/TK_UINT but need their own C types */
            if (val_t->name && (val_t->kind == TK_INT || val_t->kind == TK_UINT)) {
                const char *mapped = gray_type_to_c_codegen(codegen, val_t->name);
                if (mapped) c_elem = mapped;
            }
            if (val_t->kind == TK_STRUCT) {
                c_elem = gray_type_to_c_codegen(codegen, val_t->name);
            }
            if (val_t->kind == TK_ENUM) {
                c_elem = gray_type_to_c_codegen(codegen, val_t->name);
            }
            if (val_t->kind == TK_POINTER && val_t->name) {
                /* val_t->name is the pointee (e.g. "int"); prepend ^ for gray_type_to_c_codegen */
                static char _ptr_tn[TYPE_NAME_MAX];
                snprintf(_ptr_tn, sizeof(_ptr_tn), "^%s", val_t->name);
                c_elem = gray_type_to_c_codegen(codegen, _ptr_tn);
            }
        } else if (elem_is_string) {
            c_elem = "GrayString";
        } else if (elem_tn) {
            GrayType *et = type_from_name(elem_tn);
            if (et->kind == TK_ARRAY) c_elem = "GrayArray";
            else if (et->kind == TK_MAP) c_elem = "GrayMap";
            else if (et->kind == TK_STRUCT) c_elem = gray_type_to_c_codegen(codegen, elem_tn);
            else if (et->kind == TK_ENUM) c_elem = gray_type_to_c_codegen(codegen, elem_tn);
            else if (et->kind == TK_FUNCTION) c_elem = "void *";
            else if (et->kind == TK_POINTER) c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        }
        /* [f32] elements are stored packed as 4-byte float; a float-typed
         * value would otherwise be staged and appended as an 8-byte double. */
        if (elem_tn && strcmp(elem_tn, "f32") == 0) c_elem = "float";
        const char *alloc_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
        emit_formatted(codegen, "{ %s _av = ", c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, "; ");
        /* : escape arena-allocated data to the outer arena when
         * inside a loop scope. Strings get a simple copy; arrays, maps,
         * and structs with embedded pointers need a full deep copy. */
        if (codegen->loop_scope_depth > 0) {
            if (elem_is_string) {
                emit_formatted(codegen, "_av = gray_string_new(%s, _av.data, _av.len); ", alloc_arena);
            } else if (elem_tn && type_needs_deep_copy(codegen, elem_tn)) {
                emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _av = ", alloc_arena);
                emit_value_deep_copy(codegen, elem_tn, "_av");
                emit(codegen, "; gray_default_arena = _esc; } ");
            }
        }
        /* Ensure elem_size is set on the target array before appending;
         * struct fields may be zero-initialized with no elem_size. */
        emit(codegen, "{ GrayArray *_tgt = ");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit_formatted(codegen, "; if (_tgt->elem_size == 0) _tgt->elem_size = sizeof(%s); ", c_elem);
        emit_formatted(codegen, "gray_arrays_append(%s, _tgt, &_av); } }", alloc_arena);
        return true;
    }
    if (strcmp(func, "insert_at") == 0 && node->data.call.arg_count == 3) {
        GrayType *val_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[2]) : NULL;
        const char *c_elem = "__auto_type";
        if (val_t) {
            switch (val_t->kind) {
            case TK_INT: c_elem = "int64_t"; break;
            case TK_UINT: c_elem = "uint64_t"; break;
            case TK_FLOAT: c_elem = "double"; break;
            case TK_BOOL: c_elem = "bool"; break;
            case TK_CHAR: c_elem = "int32_t"; break;
            case TK_BYTE: c_elem = "uint8_t"; break;
            case TK_STRING: c_elem = "GrayString"; break;
            case TK_FUNCTION: c_elem = "void *"; break;
            default: break;
            }
            /* Wide ints share TK_INT/TK_UINT but need their own C types */
            if (val_t->name && (val_t->kind == TK_INT || val_t->kind == TK_UINT)) {
                const char *mapped = gray_type_to_c_codegen(codegen, val_t->name);
                if (mapped) c_elem = mapped;
            }
            if (val_t->kind == TK_STRUCT) {
                c_elem = gray_type_to_c_codegen(codegen, val_t->name);
            }
            if (val_t->kind == TK_ENUM) {
                c_elem = gray_type_to_c_codegen(codegen, val_t->name);
            }
            if (val_t->kind == TK_POINTER && val_t->name) {
                static char _ia_ptr_tn[TYPE_NAME_MAX];
                snprintf(_ia_ptr_tn, sizeof(_ia_ptr_tn), "^%s", val_t->name);
                c_elem = gray_type_to_c_codegen(codegen, _ia_ptr_tn);
            }
        }
        const char *ia_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
        emit_formatted(codegen, "{ %s _iv = ", c_elem);
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, "; ");
        GrayType *ia_arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *ia_elem_tn = (ia_arr_t && ia_arr_t->kind == TK_ARRAY) ? ia_arr_t->element_type : NULL;
        bool ia_str = (val_t && val_t->kind == TK_STRING) ||
            (ia_elem_tn && strcmp(ia_elem_tn, "string") == 0);
        if (codegen->loop_scope_depth > 0) {
            if (ia_str) {
                emit_formatted(codegen, "_iv = gray_string_new(%s, _iv.data, _iv.len); ", ia_arena);
            } else if (ia_elem_tn && type_needs_deep_copy(codegen, ia_elem_tn)) {
                emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _iv = ", ia_arena);
                emit_value_deep_copy(codegen, ia_elem_tn, "_iv");
                emit(codegen, "; gray_default_arena = _esc; } ");
            }
        }
        emit_formatted(codegen, "gray_arrays_insert_at(%s, ", ia_arena);
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", &_iv); }");
        return true;
    }
    if (strcmp(func, "remove_at") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_arrays_remove_at(");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "remove") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type &&
            strcmp(arr_t->element_type, "string") == 0) {
            emit(codegen, "gray_arrays_remove_str(");
        } else if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type &&
                   strcmp(arr_t->element_type, "float") == 0) {
            emit(codegen, "gray_arrays_remove_float(");
        } else {
            emit(codegen, "gray_arrays_remove_int(");
        }
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "clear") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_arrays_clear(");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "sort_asc") == 0 && node->data.call.arg_count == 1) {
        GrayType *sa_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *sa_elem = (sa_t && sa_t->kind == TK_ARRAY) ? sa_t->element_type : NULL;
        if (sa_elem && strcmp(sa_elem, "float") == 0)
            emit(codegen, "gray_arrays_sort_asc_float(");
        else if (sa_elem && strcmp(sa_elem, "string") == 0)
            emit(codegen, "gray_arrays_sort_asc_str(");
        else
            emit(codegen, "gray_arrays_sort_asc(");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "sort_desc") == 0 && node->data.call.arg_count == 1) {
        GrayType *sd_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *sd_elem = (sd_t && sd_t->kind == TK_ARRAY) ? sd_t->element_type : NULL;
        if (sd_elem && strcmp(sd_elem, "float") == 0)
            emit(codegen, "gray_arrays_sort_desc_float(");
        else if (sd_elem && strcmp(sd_elem, "string") == 0)
            emit(codegen, "gray_arrays_sort_desc_str(");
        else
            emit(codegen, "gray_arrays_sort_desc(");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "is_empty") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_arrays_is_empty(");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "contains") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type &&
            strcmp(arr_t->element_type, "string") == 0) {
            emit(codegen, "gray_arrays_contains_str(");
        } else {
            emit(codegen, "gray_arrays_contains_int(");
        }
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "is_equal") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type &&
            strcmp(arr_t->element_type, "string") == 0) {
            emit(codegen, "gray_arrays_is_equal_str(");
        } else {
            emit(codegen, "gray_arrays_is_equal_prim(");
        }
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "index_of") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        if (arr_t && arr_t->kind == TK_ARRAY && arr_t->element_type &&
            strcmp(arr_t->element_type, "string") == 0) {
            emit(codegen, "gray_arrays_index_of_str(");
        } else {
            emit(codegen, "gray_arrays_index_of_int(");
        }
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    /* prepend/fill need special value wrapping */
    if (strcmp(func, "prepend") == 0 && node->data.call.arg_count == 2) {
        const char *pp_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
        GrayType *pp_arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *pp_elem_tn = (pp_arr_t && pp_arr_t->kind == TK_ARRAY) ? pp_arr_t->element_type : NULL;
        bool pp_str = pp_elem_tn && strcmp(pp_elem_tn, "string") == 0;
        const char *pp_c_elem = "int64_t";
        if (pp_elem_tn) {
            GrayType *pet = type_from_name(pp_elem_tn);
            if (pet->kind == TK_FLOAT) pp_c_elem = "double";
            else if (pet->kind == TK_BOOL) pp_c_elem = "bool";
            else if (pet->kind == TK_CHAR) pp_c_elem = "int32_t";
            else if (pet->kind == TK_BYTE) pp_c_elem = "uint8_t";
            else if (pet->kind == TK_STRING) pp_c_elem = "GrayString";
            else if (pet->kind == TK_ARRAY) pp_c_elem = "GrayArray";
            else if (pet->kind == TK_MAP) pp_c_elem = "GrayMap";
            else if (pet->kind == TK_STRUCT) pp_c_elem = gray_type_to_c_codegen(codegen, pp_elem_tn);
            else if (pet->kind == TK_ENUM) pp_c_elem = gray_type_to_c_codegen(codegen, pp_elem_tn);
            else if (pet->kind == TK_INT || pet->kind == TK_UINT)
                pp_c_elem = gray_type_to_c_codegen(codegen, pp_elem_tn);
        }
        emit_formatted(codegen, "{ %s _pv = ", pp_c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, "; ");
        if (codegen->loop_scope_depth > 0) {
            if (pp_str) {
                emit_formatted(codegen, "_pv = gray_string_new(%s, _pv.data, _pv.len); ", pp_arena);
            } else if (pp_elem_tn && type_needs_deep_copy(codegen, pp_elem_tn)) {
                emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _pv = ", pp_arena);
                emit_value_deep_copy(codegen, pp_elem_tn, "_pv");
                emit(codegen, "; gray_default_arena = _esc; } ");
            }
        }
        emit_formatted(codegen, "gray_arrays_prepend(%s, ", pp_arena);
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", &_pv); }");
        return true;
    }
    if (strcmp(func, "fill") == 0 && node->data.call.arg_count == 3) {
        GrayType *fl_arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *fl_c_elem = "int64_t";
        if (fl_arr_t && fl_arr_t->kind == TK_ARRAY && fl_arr_t->element_type) {
            GrayType *fet = type_from_name(fl_arr_t->element_type);
            if (fet->kind == TK_FLOAT) fl_c_elem = "double";
            else if (fet->kind == TK_BOOL) fl_c_elem = "bool";
            else if (fet->kind == TK_CHAR) fl_c_elem = "int32_t";
            else if (fet->kind == TK_BYTE) fl_c_elem = "uint8_t";
            else if (fet->kind == TK_STRING) fl_c_elem = "GrayString";
            else if (fet->kind == TK_ARRAY) fl_c_elem = "GrayArray";
            else if (fet->kind == TK_MAP) fl_c_elem = "GrayMap";
            else if (fet->kind == TK_STRUCT) fl_c_elem = gray_type_to_c_codegen(codegen, fl_arr_t->element_type);
            else if (fet->kind == TK_ENUM) fl_c_elem = gray_type_to_c_codegen(codegen, fl_arr_t->element_type);
            else if (fet->kind == TK_INT || fet->kind == TK_UINT)
                fl_c_elem = gray_type_to_c_codegen(codegen, fl_arr_t->element_type);
        }
        emit_formatted(codegen, "{ %s _fv = ", fl_c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, "; gray_arrays_fill(gray_default_arena, ");
        emit_array_argument_address(codegen, node->data.call.args[0]);
        emit(codegen, ", &_fv, ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, "); }");
        return true;
    }
    if ((strcmp(func, "get_first") == 0 || strcmp(func, "get_last") == 0 ||
         strcmp(func, "remove_last") == 0 || strcmp(func, "remove_first") == 0) &&
        node->data.call.arg_count == 1) {
        GrayType *af_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *af_elem = (af_t && af_t->kind == TK_ARRAY) ? af_t->element_type : NULL;
        const char *af_ctype = af_elem ? gray_type_to_c_codegen(codegen, af_elem) : "int64_t";
        bool af_is_get = (strcmp(func, "get_first") == 0 || strcmp(func, "get_last") == 0);
        const char *af_ptr_fn = (strcmp(func, "get_first") == 0 || strcmp(func, "remove_first") == 0)
                                ? "gray_arrays_first_ptr" : "gray_arrays_last_ptr";
        const char *af_raw_fn = (strcmp(func, "remove_first") == 0)
                                ? "gray_arrays_remove_first_raw" : "gray_arrays_remove_last_raw";
        if (af_is_get) {
            emit_formatted(codegen, "(*(%s *)%s(", af_ctype, af_ptr_fn);
            emit_array_argument_address(codegen, node->data.call.args[0]);
            emit(codegen, "))");
        } else {
            emit_formatted(codegen, "({ %s _rafv; %s(", af_ctype, af_raw_fn);
            emit_array_argument_address(codegen, node->data.call.args[0]);
            emit(codegen, ", &_rafv); _rafv; })");
        }
        return true;
    }

    /* --- map / filter / reduce: inline loop emission --- */
    if (strcmp(func, "map") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *elem_tn = (arr_t && arr_t->kind == TK_ARRAY) ? arr_t->element_type : "int";
        const char *c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        emit(codegen, "({ GrayArray _m_src = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit_formatted(codegen, "; %s (*_m_fn)(%s) = (void *)", c_elem, c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit_formatted(codegen, "; GrayArray _m_res = gray_array_new(gray_default_arena, sizeof(%s), _m_src.len);", c_elem);
        emit_formatted(codegen, "for (int32_t _m_i = 0; _m_i < _m_src.len; _m_i++) { ");
        emit_formatted(codegen, "%s _m_v = _m_fn(((%s *)_m_src.data)[_m_i]); ", c_elem, c_elem);
        emit_formatted(codegen, "gray_arrays_append(gray_default_arena, &_m_res, &_m_v); } _m_res; })");
        return true;
    }
    if (strcmp(func, "filter") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *elem_tn = (arr_t && arr_t->kind == TK_ARRAY) ? arr_t->element_type : "int";
        const char *c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        emit(codegen, "({ GrayArray _f_src = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit_formatted(codegen, "; bool (*_f_fn)(%s) = (void *)", c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit_formatted(codegen, "; GrayArray _f_res = gray_array_new(gray_default_arena, sizeof(%s), _f_src.len);", c_elem);
        emit_formatted(codegen, "for (int32_t _f_i = 0; _f_i < _f_src.len; _f_i++) { ");
        emit_formatted(codegen, "%s _f_v = ((%s *)_f_src.data)[_f_i]; ", c_elem, c_elem);
        emit_formatted(codegen, "if (_f_fn(_f_v)) { gray_arrays_append(gray_default_arena, &_f_res, &_f_v); } } _f_res; })");
        return true;
    }
    if (strcmp(func, "any") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *elem_tn = (arr_t && arr_t->kind == TK_ARRAY) ? arr_t->element_type : "int";
        const char *c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        emit(codegen, "({ GrayArray _a_src = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit_formatted(codegen, "; bool (*_a_fn)(%s) = (void *)", c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit_formatted(codegen, "; bool _a_res = false; ");
        emit_formatted(codegen, "for (int32_t _a_i = 0; _a_i < _a_src.len; _a_i++) { ");
        emit_formatted(codegen, "if (_a_fn(((%s *)_a_src.data)[_a_i])) { _a_res = true; break; } } _a_res; })", c_elem);
        return true;
    }
    if (strcmp(func, "all") == 0 && node->data.call.arg_count == 2) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *elem_tn = (arr_t && arr_t->kind == TK_ARRAY) ? arr_t->element_type : "int";
        const char *c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        emit(codegen, "({ GrayArray _l_src = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit_formatted(codegen, "; bool (*_l_fn)(%s) = (void *)", c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit_formatted(codegen, "; bool _l_res = true; ");
        emit_formatted(codegen, "for (int32_t _l_i = 0; _l_i < _l_src.len; _l_i++) { ");
        emit_formatted(codegen, "if (!_l_fn(((%s *)_l_src.data)[_l_i])) { _l_res = false; break; } } _l_res; })", c_elem);
        return true;
    }
    if (strcmp(func, "reduce") == 0 && node->data.call.arg_count == 3) {
        GrayType *arr_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[0]) : NULL;
        const char *elem_tn = (arr_t && arr_t->kind == TK_ARRAY) ? arr_t->element_type : "int";
        const char *c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        emit(codegen, "({ GrayArray _r_src = ");
        emit_expression(codegen, node->data.call.args[0]);
        emit_formatted(codegen, "; %s _r_acc = ", c_elem);
        emit_expression(codegen, node->data.call.args[1]);
        emit_formatted(codegen, "; %s (*_r_fn)(%s, %s) = (void *)", c_elem, c_elem, c_elem);
        emit_expression(codegen, node->data.call.args[2]);
        emit_formatted(codegen, "; for (int32_t _r_i = 0; _r_i < _r_src.len; _r_i++) { ");
        emit_formatted(codegen, "_r_acc = _r_fn(_r_acc, ((%s *)_r_src.data)[_r_i]); } _r_acc; })", c_elem);
        return true;
    }

    /* Generic: arrays.func(&arr, ...) or arrays.func(arena, &arr, ...) */
    bool needs_arena = (strcmp(func, "reverse") == 0 || strcmp(func, "slice") == 0 ||
        strcmp(func, "concat") == 0 || strcmp(func, "deduplicate") == 0 ||
        strcmp(func, "flatten") == 0 || strcmp(func, "split_every") == 0 ||
        strcmp(func, "pair") == 0);
    bool ref_args = (strcmp(func, "concat") == 0 || strcmp(func, "pair") == 0);
    emit_formatted(codegen, "gray_arrays_%s(", func);
    if (needs_arena) emit(codegen, "gray_default_arena, ");
    emit_array_argument_address(codegen, node->data.call.args[0]);
    for (int i = 1; i < node->data.call.arg_count; i++) {
        emit(codegen, ", ");
        if (ref_args) {
            AstNode *rarg = node->data.call.args[i];
            if (rarg->kind == NODE_LABEL || rarg->kind == NODE_MEMBER_EXPR || rarg->kind == NODE_INDEX_EXPR) {
                emit(codegen, "&");
                emit_expression(codegen, rarg);
            } else {
                /* Rvalue: compound literal, not a statement-expression temp
                 * (whose storage dies at the closing brace — see
                 * emit_address_of). */
                emit(codegen, "(__typeof__(");
                emit_expression(codegen, rarg);
                emit(codegen, ")[]){");
                emit_expression(codegen, rarg);
                emit(codegen, "}");
            }
        } else {
            emit_expression(codegen, node->data.call.args[i]);
        }
    }
    emit(codegen, ")");
    return true;
}

/* --- @os module --- */

static bool emit_os_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "args") == 0) {
        emit(codegen, "gray_os_args(gray_default_arena)"); return true;
    }
    if (strcmp(func, "get_env") == 0) {
        emit(codegen, "gray_os_get_env(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "current_dir") == 0) {
        emit(codegen, "gray_os_cwd(gray_default_arena)"); return true;
    }
    if (strcmp(func, "hostname") == 0) {
        emit(codegen, "gray_os_hostname(gray_default_arena)"); return true;
    }
    if (strcmp(func, "current_os") == 0) { emit(codegen, "gray_os_current_os()"); return true; }
    if (strcmp(func, "arch") == 0) { emit(codegen, "gray_os_arch()"); return true; }
    if (strcmp(func, "pid") == 0) { emit(codegen, "gray_os_pid()"); return true; }
    if (strcmp(func, "set_env") == 0) {
        emit(codegen, "gray_os_set_env(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "unset_env") == 0) {
        emit(codegen, "gray_os_unset_env(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "exec") == 0) {
        emit(codegen, "gray_os_exec(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @io module --- */

static bool emit_io_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_fallible = (strcmp(func, "read_file") == 0 ||
        strcmp(func, "read_bytes") == 0 ||
        strcmp(func, "read_lines") == 0 ||
        strcmp(func, "file_size") == 0 ||
        strcmp(func, "write_file") == 0 ||
        strcmp(func, "delete_file") == 0 ||
        strcmp(func, "append_file") == 0 ||
        strcmp(func, "rename_file") == 0 ||
        strcmp(func, "copy_file") == 0 ||
        strcmp(func, "move_file") == 0 ||
        strcmp(func, "list_dir") == 0 ||
        strcmp(func, "make_dir") == 0 ||
        strcmp(func, "make_dir_all") == 0 ||
        strcmp(func, "remove_dir") == 0 ||
        strcmp(func, "remove_dir_all") == 0 ||
        strcmp(func, "walk") == 0 ||
        strcmp(func, "glob") == 0 ||
        strcmp(func, "write_bytes") == 0 ||
        strcmp(func, "append_bytes") == 0 ||
        strcmp(func, "temp_file") == 0 ||
        strcmp(func, "temp_dir") == 0);
    bool needs_arena = (strcmp(func, "read_file") == 0 ||
        strcmp(func, "read_bytes") == 0 ||
        strcmp(func, "read_lines") == 0 ||
        strcmp(func, "list_dir") == 0 ||
        strcmp(func, "walk") == 0 ||
        strcmp(func, "glob") == 0 ||
        strcmp(func, "temp_file") == 0 ||
        strcmp(func, "temp_dir") == 0 ||
        strcmp(func, "path_join") == 0 ||
        strcmp(func, "dirname") == 0 ||
        strcmp(func, "basename") == 0 ||
        strcmp(func, "extension") == 0 ||
        strcmp(func, "normalize") == 0);
    if (is_fallible) {
        /* Use non-result version when assigned to a single variable (typed or
         * inferred).  Use _result version only for multi-var destructuring
         * (temp vars prefixed with _gray_tmp). */
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        bool use_non_result = !is_multi_var;
        if (use_non_result) {
            if (needs_arena) {
                emit_formatted(codegen, "gray_io_%s(gray_default_arena", func);
                if (node->data.call.arg_count > 0) emit(codegen, ", ");
            } else {
                emit_formatted(codegen, "gray_io_%s(", func);
            }
            for (int i = 0; i < node->data.call.arg_count; i++) {
                if (i > 0) emit(codegen, ", ");
                emit_expression(codegen, node->data.call.args[i]);
            }
            /* read_lines' optional line limit defaults to 0 (read to EOF) */
            if (strcmp(func, "read_lines") == 0 && node->data.call.arg_count == 1) {
                emit(codegen, ", 0");
            }
            emit(codegen, ")");
        } else {
            emit_formatted(codegen, "gray_io_%s_result(gray_default_arena", func);
            if (node->data.call.arg_count > 0) emit(codegen, ", ");
            for (int i = 0; i < node->data.call.arg_count; i++) {
                if (i > 0) emit(codegen, ", ");
                emit_expression(codegen, node->data.call.args[i]);
            }
            if (strcmp(func, "read_lines") == 0 && node->data.call.arg_count == 1) {
                emit(codegen, ", 0");
            }
            emit(codegen, ")");
        }
        return true;
    }
    /* Non-fallible functions */
    if (needs_arena) {
        emit_formatted(codegen, "gray_io_%s(gray_default_arena", func);
        if (node->data.call.arg_count > 0) emit(codegen, ", ");
    } else {
        emit_formatted(codegen, "gray_io_%s(", func);
    }
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @strings module --- */

static bool emit_strings_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool needs_arena = (strcmp(func, "to_upper") == 0 || strcmp(func, "to_lower") == 0 ||
        strcmp(func, "trim") == 0 || strcmp(func, "trim_left") == 0 ||
        strcmp(func, "trim_right") == 0 ||
        strcmp(func, "remove_prefix") == 0 || strcmp(func, "remove_suffix") == 0 ||
        strcmp(func, "replace") == 0 ||
        strcmp(func, "repeat") == 0 || strcmp(func, "reverse") == 0 ||
        strcmp(func, "slice") == 0 || strcmp(func, "split") == 0 ||
        strcmp(func, "split_whitespace") == 0 || strcmp(func, "split_n") == 0 ||
        strcmp(func, "to_title") == 0 || strcmp(func, "to_snake_case") == 0 ||
        strcmp(func, "to_camel_case") == 0 ||
        strcmp(func, "join") == 0 ||
        strcmp(func, "builder") == 0 || strcmp(func, "build") == 0 ||
        strcmp(func, "append_char") == 0 || strcmp(func, "prepend_char") == 0 ||
        strcmp(func, "insert_char_at") == 0 || strcmp(func, "remove_at") == 0 ||
        strcmp(func, "set_char_at") == 0 ||
        strcmp(func, "to_chars") == 0 || strcmp(func, "from_chars") == 0);

    emit_formatted(codegen, "gray_strings_%s(", func);
    bool wrote_arg = false;
    if (needs_arena) {
        emit(codegen, "gray_default_arena");
        wrote_arg = true;
    }
    if (strcmp(func, "from_chars") == 0) {
        if (wrote_arg) emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (wrote_arg || i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @fmt module --- */

static void emit_format_body(CodeGen *codegen, AstNode *node, const char *prefix, bool newline) {
    emit(codegen, prefix);
    AstNode *fmt_arg = node->data.call.args[0];
    if (fmt_arg->kind == NODE_STRING_VALUE) {
        if (newline)
            emit_format_string_normalized_extended(codegen, fmt_arg->data.string_value.value, node, true);
        else
            emit_format_string_normalized(codegen, fmt_arg->data.string_value.value, node);
    } else {
        emit_expression(codegen, fmt_arg);
        emit(codegen, ".data");
    }
    emit_format_arguments(codegen, node, 1);
    emit(codegen, ")");
}

static bool emit_format_call(CodeGen *codegen, AstNode *node, const char *func) {
    static const struct { const char *name; const char *prefix; bool newline; } fmt_variants[] = {
        {"printf",     "printf(",                                false},
        {"printfln",   "printf(",                                true},
        {"eprintf",    "fprintf(stderr, ",                       false},
        {"eprintfln",  "fprintf(stderr, ",                       true},
        {"sprintf",    "gray_string_format(gray_default_arena, ", false},
        {"sprintfln",  "gray_string_format(gray_default_arena, ", true},
    };
    for (int i = 0; i < (int)(sizeof(fmt_variants) / sizeof(fmt_variants[0])); i++) {
        if (strcmp(func, fmt_variants[i].name) == 0 && node->data.call.arg_count >= 1) {
            emit_format_body(codegen, node, fmt_variants[i].prefix, fmt_variants[i].newline);
            return true;
        }
    }

    if (strcmp(func, "pad_left") == 0 && node->data.call.arg_count == 3) {
        emit(codegen, "gray_fmt_pad_left(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", (char)(");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, "))");
        return true;
    }

    if (strcmp(func, "pad_right") == 0 && node->data.call.arg_count == 3) {
        emit(codegen, "gray_fmt_pad_right(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", (char)(");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, "))");
        return true;
    }

    if (strcmp(func, "center") == 0 && node->data.call.arg_count == 3) {
        emit(codegen, "gray_fmt_center(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", (char)(");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, "))");
        return true;
    }

    if (strcmp(func, "int_to_hex") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_fmt_int_to_hex(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "int_to_binary") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_fmt_int_to_binary(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "int_to_octal") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_fmt_int_to_octal(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "float_fixed") == 0 && node->data.call.arg_count == 2) {
        emit(codegen, "gray_fmt_float_fixed(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(func, "float_sci") == 0 && node->data.call.arg_count == 1) {
        emit(codegen, "gray_fmt_float_sci(gray_default_arena, ");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }

    return false;
}

static bool emit_threads_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "spawn") == 0 && node->data.call.arg_count >= 1) {
        if (node->data.call.arg_count == 1) {
            emit(codegen, "gray_threads_spawn(");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ")");
        } else {
            emit(codegen, "gray_threads_spawn_arg(");
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(func, "join") == 0) {
        emit(codegen, "gray_threads_join(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "detach") == 0) {
        emit(codegen, "gray_threads_detach(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "is_alive") == 0) {
        emit(codegen, "gray_threads_is_alive(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "get_id") == 0) {
        emit(codegen, "gray_threads_id()");
        return true;
    }
    if (strcmp(func, "yield") == 0) {
        emit(codegen, "gray_threads_yield()");
        return true;
    }
    if (strcmp(func, "sleep") == 0) {
        emit(codegen, "gray_threads_sleep(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "thread_count") == 0) {
        emit(codegen, "gray_threads_thread_count()");
        return true;
    }
    return false;
}

/* --- chars module --- */

static bool emit_chars_call(CodeGen *codegen, AstNode *node, const char *func) {
    emit_formatted(codegen, "gray_chars_%s(", func);
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- strconv module --- */

static bool emit_strconv_call(CodeGen *codegen, AstNode *node, const char *func) {
    bool is_fallible = (strcmp(func, "to_int") == 0 ||
        strcmp(func, "to_uint") == 0 ||
        strcmp(func, "to_float") == 0 ||
        strcmp(func, "to_bool") == 0 ||
        strcmp(func, "unquote") == 0);
    bool has_base = (strcmp(func, "to_int") == 0 ||
        strcmp(func, "to_uint") == 0);
    bool needs_arena = (strcmp(func, "from_int") == 0 ||
        strcmp(func, "from_uint") == 0 ||
        strcmp(func, "from_float") == 0 ||
        strcmp(func, "format_int") == 0 ||
        strcmp(func, "format_uint") == 0 ||
        strcmp(func, "quote") == 0 ||
        strcmp(func, "unquote") == 0);

    if (is_fallible) {
        bool is_multi_var = is_result_temporary(codegen->current_var_name);
        if (is_multi_var) {
            emit_formatted(codegen, "gray_strconv_%s_result(", func);
        } else {
            emit_formatted(codegen, "gray_strconv_%s(", func);
        }
        if (needs_arena) emit(codegen, "gray_default_arena, ");
        for (int i = 0; i < node->data.call.arg_count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[i]);
        }
        /* Default base=10 for to_int/to_uint when not provided */
        if (has_base && node->data.call.arg_count == 1) {
            emit(codegen, ", 10");
        }
        emit(codegen, ")");
        return true;
    }

    if (needs_arena) {
        emit_formatted(codegen, "gray_strconv_%s(gray_default_arena, ", func);
    } else {
        emit_formatted(codegen, "gray_strconv_%s(", func);
    }
    for (int i = 0; i < node->data.call.arg_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @sync module --- */

static bool emit_sync_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "mutex") == 0) {
        emit(codegen, "gray_sync_mutex()");
        return true;
    }
    if (strcmp(func, "lock") == 0) {
        emit(codegen, "gray_sync_lock(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "unlock") == 0) {
        emit(codegen, "gray_sync_unlock(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "try_lock") == 0) {
        emit(codegen, "gray_sync_try_lock(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "destroy") == 0) {
        emit(codegen, "gray_sync_destroy(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @atomic module --- */

static bool emit_atomic_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "spinlock") == 0) {
        emit(codegen, "gray_atomic_mod_spinlock()");
        return true;
    }
    if (strcmp(func, "fence") == 0) {
        emit(codegen, "gray_atomic_mod_fence()");
        return true;
    }
    /* spinlock_destroy takes a pointer so the caller's _internal can be nulled */
    if (node->data.call.arg_count == 1 && strcmp(func, "spinlock_destroy") == 0) {
        emit(codegen, "gray_atomic_mod_spinlock_destroy(&");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    /* Single-argument functions: load, spin_lock, spin_unlock, spin_trylock */
    if (node->data.call.arg_count == 1) {
        if (strcmp(func, "load") == 0 || strcmp(func, "spin_lock") == 0 ||
            strcmp(func, "spin_trylock") == 0 || strcmp(func, "spin_unlock") == 0) {
            emit_formatted(codegen, "gray_atomic_mod_%s(", func);
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ")");
            return true;
        }
    }
    /* Two-argument functions: store, add, sub, exchange, and, or, xor */
    if (node->data.call.arg_count == 2) {
        if (strcmp(func, "store") == 0 || strcmp(func, "add") == 0 ||
            strcmp(func, "sub") == 0 || strcmp(func, "exchange") == 0 ||
            strcmp(func, "and") == 0 || strcmp(func, "or") == 0 ||
            strcmp(func, "xor") == 0) {
            emit_formatted(codegen, "gray_atomic_mod_%s(", func);
            emit_expression(codegen, node->data.call.args[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.args[1]);
            emit(codegen, ")");
            return true;
        }
    }
    /* Three-argument: cas */
    if (strcmp(func, "cas") == 0 && node->data.call.arg_count == 3) {
        emit(codegen, "gray_atomic_mod_cas(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[2]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @channels module --- */

static bool emit_channels_call(CodeGen *codegen, AstNode *node, const char *func) {
    if (strcmp(func, "open") == 0) {
        emit(codegen, "gray_channels_open(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "send") == 0) {
        emit(codegen, "gray_channels_send(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "receive") == 0) {
        emit(codegen, "gray_channels_receive(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "close") == 0) {
        emit(codegen, "gray_channels_close(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "try_send") == 0) {
        emit(codegen, "gray_channels_try_send(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.args[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(func, "try_receive") == 0) {
        emit(codegen, "gray_channels_try_receive(");
        emit_expression(codegen, node->data.call.args[0]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- Main call dispatcher --- */

/* Tagged enum construction: explicit `Shape.Circle(3.14)` or implicit
 * `.Circle(3.14)`. Returns true when it emitted the constructor. */
static bool emit_tagged_enum_construction(CodeGen *codegen, AstNode *node) {
    /* Tagged enum construction: Shape.Circle(3.14) */
    if (ast_member_qualifier(node->data.call.function)) {
        const char *ename = ast_member_qualifier(node->data.call.function);
        const char *vname = node->data.call.function->data.member.member;
        /* Also check using-module-resolved names */
        const char *resolved_ename = ename;
        if (!codegen_is_enum(codegen, ename)) {
            const char *resolved_name = codegen_resolve_type(codegen, ename);
            if (resolved_name != ename && codegen_is_enum(codegen, resolved_name)) resolved_ename = resolved_name;
        }
        if (codegen_is_enum(codegen, resolved_ename) && codegen_enum_is_tagged(codegen, resolved_ename)) {
            /* Emit compound literal: (GrayEnum_Shape){ .tag = GrayEnum_Shape_TAG_Circle, .data.Circle = { args } } */
            int eidx = codegen_enum_index(codegen, resolved_ename);
            AstNode *decl = codegen->enum_decls[eidx];
            int vidx = -1;
            for (int variant_index = 0; variant_index < decl->data.enum_decl.value_count; variant_index++) {
                if (strcmp(decl->data.enum_decl.values[variant_index].name, vname) == 0) { vidx = variant_index; break; }
            }
            emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s", resolved_ename, resolved_ename, vname);
            if (vidx >= 0 && decl->data.enum_decl.values[vidx].payload_count > 0) {
                emit_formatted(codegen, ", .data.%s = { ", vname);
                for (int ai = 0; ai < node->data.call.arg_count; ai++) {
                    if (ai > 0) emit(codegen, ", ");
                    emit_expression(codegen, node->data.call.args[ai]);
                }
                emit(codegen, " }");
            }
            emit(codegen, " }");
            return true;
        }
    }

    /* Tagged enum construction via implicit selector: .Circle(3.14) */
    if (node->data.call.function->kind == NODE_IMPLICIT_ENUM) {
        const char *ename = node->data.call.function->data.implicit_enum.resolved_enum;
        const char *vname = node->data.call.function->data.implicit_enum.variant;
        if (ename && codegen_enum_is_tagged(codegen, ename)) {
            int eidx = codegen_enum_index(codegen, ename);
            AstNode *decl = codegen->enum_decls[eidx];
            int vidx = -1;
            for (int variant_index = 0; variant_index < decl->data.enum_decl.value_count; variant_index++) {
                if (strcmp(decl->data.enum_decl.values[variant_index].name, vname) == 0) { vidx = variant_index; break; }
            }
            emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s", ename, ename, vname);
            if (vidx >= 0 && decl->data.enum_decl.values[vidx].payload_count > 0) {
                emit_formatted(codegen, ", .data.%s = { ", vname);
                for (int ai = 0; ai < node->data.call.arg_count; ai++) {
                    if (ai > 0) emit(codegen, ", ");
                    emit_expression(codegen, node->data.call.args[ai]);
                }
                emit(codegen, " }");
            }
            emit(codegen, " }");
            return true;
        }
    }
    return false;
}

/* Struct-namespaced (Name.func()) calls and mod.Struct.func() chains.
 * Returns true when it emitted the call; false to fall through to the
 * general function-call path. */
static bool emit_namespaced_call(CodeGen *codegen, AstNode *node) {
    /* Check for struct-namespaced or user-module function call: Name.func() */
    if (node->data.call.function->kind == NODE_MEMBER_EXPR) {
        AstNode *obj = node->data.call.function->data.member.object;
        const char *member = node->data.call.function->data.member.member;
        /* Handle mod.Struct.func() triple chain: geometry.Vec2.create().
         * The struct the qualified name refers to is on the object node; the
         * function is that struct's, namespaced under it. */
        const char *chain_mod = NULL, *chain_type = NULL;
        if (ast_member_chain(node->data.call.function, &chain_mod, &chain_type)) {
            char full_name[MSG_BUF_SIZE];
            if (obj->resolved_decl) {
                char owner[MSG_BUF_SIZE];
                snprintf(full_name, sizeof(full_name), "%s_%s",
                    module_mangle_into(obj->resolved_decl, owner, sizeof(owner)), member);
            } else {
                snprintf(full_name, sizeof(full_name), "%s_%s_%s",
                    chain_mod, chain_type, member);
            }
            AstNode *ns_func = find_function(codegen, full_name);
            if (ns_func) {
                emit_formatted(codegen, "gray_fn_%s(", full_name);
                for (int i = 0; i < node->data.call.arg_count; i++) {
                    if (i > 0) emit(codegen, ", ");
                    bool mut_param = i < ns_func->data.func_decl.param_count &&
                        ns_func->data.func_decl.params[i].mutable;
                    emit_mutable_call_argument(codegen, node->data.call.args[i], mut_param);
                }
                emit(codegen, ")");
                return true;
            }
        }
        if (obj->kind == NODE_LABEL) {
            const char *raw_name = obj->data.label.value;

            /* C interop: extern.func(); emit raw C function call */
            if (strcmp(raw_name, "extern") == 0 && codegen->has_c_imports) {
                emit_formatted(codegen, "%s(", member);
                for (int i = 0; i < node->data.call.arg_count; i++) {
                    if (i > 0) emit(codegen, ", ");
                    /* Auto-convert GrayString to char* for C functions */
                    GrayType *arg_t = codegen->type_table
                        ? typetable_get(codegen->type_table, node->data.call.args[i])
                        : NULL;
                    if (arg_t && arg_t->kind == TK_STRING) {
                        emit_expression(codegen, node->data.call.args[i]);
                        emit(codegen, ".data");
                    } else {
                        emit_expression(codegen, node->data.call.args[i]);
                    }
                }
                emit(codegen, ")");
                return true;
            }

            /* The qualifier may be a struct type, which is namespaced under
             * its module, or an import alias. Try the symbol table first. */
            const char *resolved_name = codegen_resolve_decl(codegen, raw_name);
            if (resolved_name == raw_name) resolved_name = resolve_alias(codegen, raw_name);
            /* Try to find as a namespaced function: Name_func or ResolvedAlias_func */
            char ns_name[IDENT_BUF];
            snprintf(ns_name, sizeof(ns_name), "%s_%s", resolved_name, member);
            AstNode *ns_func = find_function(codegen, ns_name);
            /* If not found, try using-module-prefixed struct names so
             * bare Product.create() from 'import and use' resolves to
             * types_Product_create. */
            static char using_resolved[IDENT_BUF];
            if (!ns_func && resolved_name[0] >= 'A' && resolved_name[0] <= 'Z') {
                for (int ui = 0; ui < codegen->using_module_count; ui++) {
                    const char *real_mod = resolve_alias(codegen, codegen->using_modules[ui]);
                    char prefixed[IDENT_BUF];
                    snprintf(prefixed, sizeof(prefixed), "%s_%s_%s", real_mod, resolved_name, member);
                    ns_func = find_function(codegen, prefixed);
                    if (ns_func) {
                        snprintf(using_resolved, sizeof(using_resolved), "%s_%s", real_mod, resolved_name);
                        resolved_name = using_resolved;
                        snprintf(ns_name, sizeof(ns_name), "%s_%s", resolved_name, member);
                        break;
                    }
                }
            }
            bool instance_dispatch = false;
            bool obj_is_ptr = false;
            if (!ns_func) {
                /* : check if `member` is a func-typed data field
                 * on the struct. If so, emit as a function-pointer call
                 * through the field access. We get here when the variable
                 * has a struct type but neither <struct>_<member> nor
                 * bare <member> is a registered function. */
                GrayType *inst_t = codegen->type_table ? typetable_get(codegen->type_table, obj) : NULL;
                /* Save pointer flag before fallback may overwrite inst_t with TK_STRUCT */
                obj_is_ptr = inst_t && inst_t->kind == TK_POINTER;
                /* If type table missed, scan var decls for a new() initializer */
                if (!obj_is_ptr && obj->kind == NODE_LABEL) {
                    const char *vname = obj->data.label.value;
                    for (int si = 0; si < codegen->func_count && !obj_is_ptr; si++) {
                        AstNode *fd = codegen->all_funcs[si];
                        if (!fd->data.func_decl.body) continue;
                        for (int bi = 0; bi < fd->data.func_decl.body->data.block.count && !obj_is_ptr; bi++) {
                            AstNode *st = fd->data.func_decl.body->data.block.stmts[bi];
                            if (st->kind == NODE_VAR_DECL &&
                                strcmp(st->data.var_decl.name, vname) == 0 &&
                                st->data.var_decl.value &&
                                st->data.var_decl.value->kind == NODE_NEW_EXPR) {
                                obj_is_ptr = true;
                                if ((!inst_t || inst_t->kind == TK_UNKNOWN) &&
                                    st->data.var_decl.value->data.new_expr.type_name) {
                                    inst_t = type_struct(st->data.var_decl.value->data.new_expr.type_name);
                                }
                            }
                        }
                    }
                }
                /* Fall back to scanning var decls for struct type */
                if ((!inst_t || inst_t->kind == TK_UNKNOWN) && obj->kind == NODE_LABEL) {
                    const char *vname = obj->data.label.value;
                    for (int si = 0; si < codegen->func_count && (!inst_t || inst_t->kind == TK_UNKNOWN); si++) {
                        AstNode *fd = codegen->all_funcs[si];
                        if (!fd->data.func_decl.body) continue;
                        for (int bi = 0; bi < fd->data.func_decl.body->data.block.count; bi++) {
                            AstNode *st = fd->data.func_decl.body->data.block.stmts[bi];
                            if (st->kind != NODE_VAR_DECL ||
                                strcmp(st->data.var_decl.name, vname) != 0) continue;
                            const char *tn = st->data.var_decl.type_name;
                            if (tn && find_struct_declaration(codegen, tn)) {
                                inst_t = type_struct(tn);
                                break;
                            }
                            if (st->data.var_decl.value &&
                                st->data.var_decl.value->kind == NODE_STRUCT_VALUE) {
                                const char *sn = st->data.var_decl.value->data.struct_value.name;
                                if (sn && find_struct_declaration(codegen, sn)) {
                                    inst_t = type_struct(sn);
                                    break;
                                }
                            }
                        }
                    }
                }
                /* Fall back to scanning struct decls if the type_table
                 * doesn't have a hit for the label. */
                if (!inst_t || inst_t->kind == TK_UNKNOWN) {
                    build_function_field_index(codegen);
                    for (int i = 0; i < codegen->func_field_count; i++) {
                        if (strcmp(codegen->func_field_index[i].field_name, member) == 0) {
                            inst_t = type_struct(codegen->func_field_index[i].struct_name);
                            break;
                        }
                    }
                }
                if (inst_t && (inst_t->kind == TK_STRUCT || inst_t->kind == TK_POINTER)) {
                    const char *sn = (inst_t->kind == TK_POINTER && inst_t->element_type)
                        ? inst_t->element_type : inst_t->name;
                    AstNode *sdecl = sn ? find_struct_declaration(codegen, sn) : NULL;
                    if (sdecl) {
                        for (int field_index = 0; field_index < sdecl->data.struct_decl.field_count; field_index++) {
                            if (strcmp(sdecl->data.struct_decl.fields[field_index].name, member) == 0 &&
                                sdecl->data.struct_decl.fields[field_index].type_name &&
                                (strcmp(sdecl->data.struct_decl.fields[field_index].type_name, "func") == 0 || strncmp(sdecl->data.struct_decl.fields[field_index].type_name, "func(", 5) == 0)) {
                                int nargs = node->data.call.arg_count;
                                if (nargs > 0 && !node->data.call.args) nargs = 0;
                                GrayType *ret_t = codegen->type_table ? typetable_get(codegen->type_table, node) : NULL;
                                const char *c_ret = (ret_t && ret_t->kind != TK_UNKNOWN && ret_t->kind != TK_VOID)
                                    ? gray_type_to_c_codegen(codegen, type_name(ret_t)) : "int64_t";
                                if (ret_t && ret_t->kind == TK_VOID) c_ret = "void";
                                emit_formatted(codegen, "((%s (*)(", c_ret);
                                for (int ai = 0; ai < nargs; ai++) {
                                    if (ai > 0) emit(codegen, ", ");
                                    GrayType *arg_type = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[ai]) : NULL;
                                    emit(codegen, arg_type ? gray_type_to_c_codegen(codegen, type_name(arg_type)) : "int64_t");
                                }
                                emit(codegen, "))");
                                emit_expression(codegen, obj);
                                emit_formatted(codegen, "%s%s)(", obj_is_ptr ? "->" : ".", member);
                                for (int ai = 0; ai < nargs; ai++) {
                                    if (ai > 0) emit(codegen, ", ");
                                    emit_expression(codegen, node->data.call.args[ai]);
                                }
                                emit(codegen, ")");
                                return true;
                            }
                        }
                    }
                }
                /* Instance dispatch: var.func() -> StructType_func(&var)
                 * Look up the variable's struct type and try StructName_member. */
                if (inst_t && (inst_t->kind == TK_STRUCT || inst_t->kind == TK_POINTER)) {
                    const char *sn = (inst_t->kind == TK_POINTER && inst_t->element_type)
                        ? inst_t->element_type : inst_t->name;
                    if (sn) {
                        snprintf(ns_name, sizeof(ns_name), "%s_%s", sn, member);
                        ns_func = find_function(codegen, ns_name);
                        if (ns_func) {
                            resolved_name = sn;
                            instance_dispatch = true;
                        }
                    }
                }
                /* Try bare function name (for main-file functions in circular imports) */
                if (!ns_func) ns_func = find_function(codegen, member);
                if (ns_func && strcmp(ns_name, member) != 0) {
                    /* Already handled by struct dispatch below */
                } else if (ns_func) {
                    emit_formatted(codegen, "gray_fn_%s(", member);
                    for (int i = 0; i < node->data.call.arg_count; i++) {
                        if (i > 0) emit(codegen, ", ");
                        bool mut_param = i < ns_func->data.func_decl.param_count &&
                            ns_func->data.func_decl.params[i].mutable;
                        emit_mutable_call_argument(codegen, node->data.call.args[i], mut_param);
                    }
                    emit(codegen, ")");
                    return true;
                }
            }
            if (ns_func) {
                /* Generic struct function: mangle with concrete binding */
                if (func_is_generic(ns_func)) {
                    const char *binding = NULL;
                    char *dyn_binding = NULL;
                    int cc = ns_func->data.func_decl.param_count < node->data.call.arg_count
                        ? ns_func->data.func_decl.param_count : node->data.call.arg_count;
                    for (int pi = 0; pi < cc && !binding; pi++) {
                        /* Type parameter: the binding is the argument label
                         * itself (a struct name), not a resolved value type.
                         * Without this the loop found nothing to bind and
                         * mangled the call against a specialization that was
                         * never emitted. */
                        if (ns_func->data.func_decl.params[pi].is_type_param) {
                            if (node->data.call.args[pi]->kind == NODE_LABEL) {
                                const char *lbl = node->data.call.args[pi]->data.label.value;
                                if (strcmp(lbl, "?") == 0 && codegen->wildcard_binding)
                                    binding = codegen->wildcard_binding;
                                else
                                    binding = lbl;
                            }
                            continue;
                        }
                        const char *ptn = ns_func->data.func_decl.params[pi].type_name;
                        if (!ptn || !strchr(ptn, '?')) continue;
                        GrayType *arg_type = codegen->type_table
                            ? typetable_get(codegen->type_table, node->data.call.args[pi]) : NULL;
                        if (!arg_type) continue;
                        if (strcmp(ptn, "?") == 0) {
                            const char *type_str = type_name(arg_type);
                            binding = ((arg_type->kind == TK_UNKNOWN || strcmp(type_str, "unknown") == 0)
                                && codegen->wildcard_binding)
                                ? codegen->wildcard_binding : type_str;
                        } else {
                            dyn_binding = codegen_bind_wildcard(ptn, type_name(arg_type));
                            if (dyn_binding) binding = dyn_binding;
                        }
                    }
                    char mangled[MSG_BUF_SIZE];
                    size_t pos = (size_t)snprintf(mangled, sizeof(mangled),
                        "gray_fn_%s_%s__", resolved_name, member);
                    if (binding) {
                        for (const char *ch = binding; *ch && pos < sizeof(mangled) - 1; ch++) {
                            mangled[pos++] = (isalnum((unsigned char)*ch) || *ch == '_') ? *ch : '_';
                        }
                    }
                    mangled[pos] = '\0';
                    emit_formatted(codegen, "%s(", mangled);
                    free(dyn_binding);
                } else {
                    emit_formatted(codegen, "gray_fn_%s_%s(", resolved_name, member);
                }
                /* For instance dispatch, inject the instance as the first
                 * argument when the struct function expects more params
                 * than the call site provides. */
                bool self_injected = false;
                if (instance_dispatch &&
                    ns_func->data.func_decl.param_count > node->data.call.arg_count) {
                    if (ns_func->data.func_decl.params[0].mutable) {
                        if (obj_is_ptr) emit_expression(codegen, obj);
                        else { emit(codegen, "&"); emit_expression(codegen, obj); }
                    } else {
                        emit_expression(codegen, obj);
                    }
                    self_injected = true;
                }
                bool arg_emitted = self_injected;
                for (int i = 0; i < node->data.call.arg_count; i++) {
                    int pi = self_injected ? i + 1 : i;
                    /* Type params are erased in C; emitting one leaked the
                     * type name into the output as a bare identifier. */
                    if (pi < ns_func->data.func_decl.param_count &&
                        ns_func->data.func_decl.params[pi].is_type_param) continue;
                    if (arg_emitted) emit(codegen, ", ");
                    arg_emitted = true;
                    bool mut_param = pi < ns_func->data.func_decl.param_count &&
                        ns_func->data.func_decl.params[pi].mutable;
                    emit_mutable_call_argument(codegen, node->data.call.args[i], mut_param);
                }
                /* Inject default values for omitted trailing parameters */
                {
                    int self_offset = self_injected ? 1 : 0;
                    int first_default = node->data.call.arg_count + self_offset;
                    bool any_emitted = arg_emitted;
                    for (int i = first_default; i < ns_func->data.func_decl.param_count; i++) {
                        if (ns_func->data.func_decl.params[i].is_type_param) continue;
                        if (any_emitted) emit(codegen, ", ");
                        any_emitted = true;
                        if (ns_func->data.func_decl.params[i].default_value) {
                            emit_expression(codegen, ns_func->data.func_decl.params[i].default_value);
                        } else {
                            emit(codegen, "0");
                        }
                    }
                }
                emit(codegen, ")");
                return true;
            }
        }
    }
    return false;
}

static void emit_call_expression_body(CodeGen *codegen, AstNode *node) {
    const char *module = NULL;
    const char *func = NULL;

    if (is_stdlib_call(node, &module, &func)) {
        /* Resolve import aliases: io@std → io.println maps to std.println */
        if (module) module = resolve_alias(codegen, module);

        /* No-module builtins (println, len, type_of, etc.) */
        /* Also handle std.println(); std module functions are builtins */
        if (!module && emit_builtin_call(codegen, node, func)) return;

        /* Module dispatch table — sorted alphabetically for binary search */
        typedef bool (*ModuleHandler)(CodeGen *, AstNode *, const char *);
        typedef struct { const char *name; ModuleHandler handler; } ModuleEntry;
        static const ModuleEntry modules[] = {
            {"arrays",   emit_arrays_call},
            {"atomic",   emit_atomic_call},
            {"binary",   emit_binary_call},
            {"channels", emit_channels_call},
            {"chars",    emit_chars_call},
            {"crypto",   emit_crypto_call},
            {"csv",      emit_csv_call},
            {"encoding", emit_encoding_call},
            {"fmt",      emit_format_call},
            {"http",     emit_http_call},
            {"io",       emit_io_call},
            {"json",     emit_json_call},
            {"maps",     emit_maps_call},
            {"math",     emit_math_call},
            {"mem",      emit_mem_call},
            {"net",      emit_net_call},
            {"os",       emit_os_call},
            {"random",   emit_random_call},
            {"regex",    emit_regex_call},
            {"runtime",  emit_runtime_call},
            {"server",   emit_server_call},
            {"sqlite",   emit_sqlite_call},
            {"strconv",  emit_strconv_call},
            {"strings",  emit_strings_call},
            {"sync",     emit_sync_call},
            {"threads",  emit_threads_call},
            {"time",     emit_time_call},
            {"uuid",     emit_uuid_call},
        };
        if (module) {
            int low = 0, high = (int)(sizeof(modules) / sizeof(modules[0])) - 1;
            while (low <= high) {
                int midpoint = (low + high) / 2;
                int cmp = strcmp(module, modules[midpoint].name);
                if (cmp == 0) {
                    if (modules[midpoint].handler(codegen, node, func)) return;
                    break;
                }
                if (cmp < 0) high = midpoint - 1;
                else low = midpoint + 1;
            }
        }
        /* Unqualified call not handled by builtins; try 'using' modules.
         * We must verify the function name belongs to the module before calling
         * the handler, since some handlers emit code for any function name.
         * A user function of the same name — the program's own or the current
         * module's — shadows a `using`'d stdlib function, so the general call
         * path below emits it instead (the typechecker resolves the bare name
         * to that function the same way). */
        bool user_shadows_using = false;
        if (!module && func) {
            user_shadows_using = find_function(codegen, func) != NULL;
            if (!user_shadows_using) {
                const char *rd = codegen_resolve_decl(codegen, func);
                if (rd != func) user_shadows_using = find_function(codegen, rd) != NULL;
            }
        }
        if (!module && !user_shadows_using) {
            for (int ui = 0; ui < codegen->using_module_count; ui++) {
                const char *umod = codegen->using_modules[ui];
                const char *real_mod = resolve_alias(codegen, umod);
                /* 1) Check stdlib table (authoritative source in typechecker) */
                bool found = stdlib_has_func(real_mod, func);
                if (found) {
                    /* Dispatch to the stdlib module handler */
                    for (int mi = 0; mi < (int)(sizeof(modules) / sizeof(modules[0])); mi++) {
                        if (strcmp(real_mod, modules[mi].name) == 0) {
                            if (modules[mi].handler(codegen, node, func)) return;
                            break;
                        }
                    }
                }
                /* 2) Try user-defined module: <module>_<func> */
                if (!found) {
                    char prefixed[IDENT_BUF];
                    snprintf(prefixed, sizeof(prefixed), "%s_%s", real_mod, func);
                    AstNode *uf = find_function(codegen, prefixed);
                    /* A generic function needs its per-instantiation name,
                     * which the general call path derives from the argument
                     * types; this shortcut would emit the unspecialised
                     * symbol, so leave those to it. */
                    if (uf && func_is_generic(uf)) uf = NULL;
                    if (uf) {
                        int pc = uf->data.func_decl.param_count;
                        int ac = node->data.call.arg_count;
                        int total = ac < pc ? pc : ac;
                        emit_formatted(codegen, "gray_fn_%s_%s(", real_mod, func);
                        for (int i = 0; i < total; i++) {
                            if (i > 0) emit(codegen, ", ");
                            if (i < ac) {
                                bool mut_param = i < pc && uf->data.func_decl.params[i].mutable;
                                emit_mutable_call_argument(codegen, node->data.call.args[i], mut_param);
                            } else if (i < pc && uf->data.func_decl.params[i].default_value) {
                                emit_expression(codegen, uf->data.func_decl.params[i].default_value);
                            }
                        }
                        emit(codegen, ")");
                        return;
                    }
                }
            }
        }
    }

    if (emit_tagged_enum_construction(codegen, node)) return;

    /* Struct-namespaced (Name.func()) and mod.Struct.func() chains */
    if (emit_namespaced_call(codegen, node)) return;

    /* Generic function call */
    const char *fn_name = NULL;
    if (node->data.call.function->kind == NODE_LABEL) {
        fn_name = node->data.call.function->data.label.value;
    }

    /* Look up function to check if it's a known function or a variable (function pointer) */
    AstNode *target_func = fn_name ? find_function(codegen, fn_name) : NULL;

    /* A name written bare inside its own module, or reachable through a
     * `using`, names a function registered under its module's spelling. This
     * used to match on the text after the first underscore of every declared
     * function, which claimed any name that happened to be some function's
     * suffix. */
    const char *resolved_fn_name = fn_name;
    if (!target_func && fn_name) {
        const char *resolved = codegen_resolve_decl(codegen, fn_name);
        if (resolved != fn_name) {
            AstNode *found = find_function(codegen, resolved);
            if (found) {
                target_func = found;
                resolved_fn_name = resolved;
            }
        }
    }

    bool direct_known_call = (fn_name && target_func);
    if (fn_name && target_func) {
        /* Known function; use gray_fn_ prefix. For generic functions,
         * rewrite to the mangled instantiation name derived from the
         * first wildcard parameter's argument type ). */
        bool tf_generic = func_is_generic(target_func);
        if (tf_generic) {
            /* Derive the concrete binding by scanning params for the
             * first '?' slot and reading the matching arg's type. */
            const char *binding = NULL;
            char *dynamic_binding = NULL;
            int pc = target_func->data.func_decl.param_count;
            int ac = node->data.call.arg_count;
            int cc = pc < ac ? pc : ac;
            for (int pi = 0; pi < cc && !binding; pi++) {
                /* Type parameter: binding is the arg label directly.
                 * When forwarding (T→"?"), resolve via the outer binding. */
                if (target_func->data.func_decl.params[pi].is_type_param) {
                    if (node->data.call.args[pi]->kind == NODE_LABEL) {
                        const char *lbl = node->data.call.args[pi]->data.label.value;
                        if (strcmp(lbl, "?") == 0 && codegen->wildcard_binding)
                            binding = codegen->wildcard_binding;
                        else
                            binding = lbl;
                    }
                    continue;
                }
                const char *ptn = target_func->data.func_decl.params[pi].type_name;
                if (!ptn || !strchr(ptn, '?')) continue;
                GrayType *arg_type = codegen->type_table
                    ? typetable_get(codegen->type_table, node->data.call.args[pi]) : NULL;
                if (!arg_type) continue;
                if (strcmp(ptn, "?") == 0) {
                    /* Inside a generic body the arg's typetable entry is still
                     * TK_UNKNOWN from the main-pass walk; use the outer binding. */
                    const char *type_str = type_name(arg_type);
                    if ((arg_type->kind == TK_UNKNOWN || strcmp(type_str, "unknown") == 0) &&
                        codegen->wildcard_binding) {
                        binding = codegen->wildcard_binding;
                    } else {
                        binding = type_str;
                    }
                } else {
                    /* Composite param type (e.g. [[?]], [map[K:?]], map[K:[?]]).
                     * Use the recursive helper to peel layers until '?' is reached. */
                    char *derived = codegen_bind_wildcard(ptn, type_name(arg_type));
                    if (derived) {
                        free(dynamic_binding);
                        dynamic_binding = derived;
                        binding = dynamic_binding;
                    }
                }
            }
            size_t rfn_len = strlen(resolved_fn_name);
            size_t bind_len = binding ? strlen(binding) : 0;
            size_t mn_need = 8 + rfn_len + 2 + bind_len + 1; /* 8 = strlen("gray_fn_") */
            char *mangled = malloc(mn_need);
            if (!mangled) return;
            size_t pos = (size_t)snprintf(mangled, mn_need, "gray_fn_%s__",
                resolved_fn_name);
            if (binding) {
                for (const char *ch = binding; *ch && pos < mn_need - 1; ch++) {
                    mangled[pos++] = (isalnum((unsigned char)*ch) || *ch == '_') ? *ch : '_';
                }
            }
            mangled[pos] = '\0';
            emit(codegen, mangled);
            free(mangled);
            free(dynamic_binding);
        } else {
            emit(codegen, "gray_fn_");
            emit(codegen, resolved_fn_name);
        }
    } else if (fn_name) {
        /* Not a known function; variable holding a function pointer (void *).
         * Cast to appropriate function pointer type based on the variable's
         * typed-func signature, falling back to a brittle var_decl scan only
         * when no signature is available (bare-func paths). */
        int nargs = node->data.call.arg_count;
        GrayFuncSig *typed_sig = NULL;
        GrayType *callee_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.call.function) : NULL;
        if (callee_t && callee_t->kind == TK_FUNCTION) {
            typed_sig = callee_t->func_sig;
        }
        AstNode *ref_func = NULL;
        /* Always scan, even when typed_sig is set: the FuncSig in the
         * AST decl carries default values that the typed-func sig does
         * not, so the scan is needed for the defaults-fill path below. */
        {
            for (int si = 0; si < codegen->func_count && !ref_func; si++) {
                AstNode *fn_decl = codegen->all_funcs[si];
                if (!fn_decl->data.func_decl.body) continue;
                for (int bi = 0; bi < fn_decl->data.func_decl.body->data.block.count && !ref_func; bi++) {
                    AstNode *st = fn_decl->data.func_decl.body->data.block.stmts[bi];
                    if (st->kind == NODE_VAR_DECL &&
                        strcmp(st->data.var_decl.name, fn_name) == 0 &&
                        st->data.var_decl.value &&
                        st->data.var_decl.value->kind == NODE_FUNC_REF) {
                        AstNode *fref = st->data.var_decl.value->data.func_ref.function;
                        if (fref->kind == NODE_LABEL) {
                            ref_func = find_function(codegen, fref->data.label.value);
                        } else if (ast_member_qualifier(fref)) {
                            const char *resolved_obj = ast_member_qualifier(fref);
                            const char *resolved_member = fref->data.member.member;
                            char resolved_name[IDENT_BUF];
                            snprintf(resolved_name, sizeof(resolved_name), "%s_%s", resolved_obj, resolved_member);
                            ref_func = find_function(codegen, resolved_name);
                        }
                    }
                }
            }
            if (ref_func) target_func = ref_func;
        }
        /* Return type: typed_sig wins, else fall back to call-node type table */
        const char *c_ret = "int64_t";
        if (typed_sig) {
            if (typed_sig->return_count == 0) c_ret = "void";
            else c_ret = gray_type_to_c_codegen(codegen, typed_sig->return_types[0]);
        } else {
            GrayType *ret_t = codegen->type_table ? typetable_get(codegen->type_table, node) : NULL;
            if (ret_t && ret_t->kind != TK_UNKNOWN) c_ret = gray_type_to_c_codegen(codegen, type_name(ret_t));
            if (ret_t && ret_t->kind == TK_VOID) c_ret = "void";
        }
        int cast_count = nargs;
        if (typed_sig && typed_sig->param_count > nargs) cast_count = typed_sig->param_count;
        if (ref_func && ref_func->data.func_decl.param_count > nargs) {
            cast_count = ref_func->data.func_decl.param_count;
        }
        emit_formatted(codegen, "((%s (*)(", c_ret);
        for (int i = 0; i < cast_count; i++) {
            if (i > 0) emit(codegen, ", ");
            bool mut_p = false;
            if (typed_sig && i < typed_sig->param_count) {
                mut_p = typed_sig->param_mutable[i];
            } else if (ref_func && i < ref_func->data.func_decl.param_count) {
                mut_p = ref_func->data.func_decl.params[i].mutable;
            }
            if (typed_sig && i < typed_sig->param_count) {
                /* Param type comes straight from the signature; authoritative */
                emit(codegen, gray_type_to_c_codegen(codegen, typed_sig->param_types[i]));
                if (mut_p) emit(codegen, " *");
            } else if (i < nargs) {
                GrayType *arg_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[i]) : NULL;
                if (arg_t && arg_t->kind != TK_UNKNOWN) {
                    emit(codegen, gray_type_to_c_codegen(codegen, type_name(arg_t)));
                    if (mut_p) emit(codegen, " *");
                } else {
                    emit(codegen, "int64_t");
                    if (mut_p) emit(codegen, " *");
                }
            } else if (ref_func && i < ref_func->data.func_decl.param_count) {
                const char *ptn = ref_func->data.func_decl.params[i].type_name;
                emit(codegen, ptn ? gray_type_to_c_codegen(codegen, ptn) : "int64_t");
                if (mut_p) emit(codegen, " *");
            } else {
                emit(codegen, "int64_t");
            }
        }
        if (cast_count == 0) emit(codegen, "void");
        emit(codegen, "))");
        emit(codegen, sanitize_name(fn_name));
        emit(codegen, ")");
        /* Stash the typed_sig in a side channel so the arg emission below
         * can pick up param mutability when target_func is NULL. */
        codegen->pending_call_typed_sig = typed_sig;
    } else if (node->data.call.function->kind == NODE_MEMBER_EXPR) {
        /* Module-qualified call fallback: module.func() → gray_module_func() */
        AstNode *obj = node->data.call.function->data.member.object;
        const char *member = node->data.call.function->data.member.member;
        if (obj->kind == NODE_LABEL) {
            const char *mod_name = codegen_resolve_decl(codegen, obj->data.label.value);
            if (mod_name == obj->data.label.value)
                mod_name = resolve_alias(codegen, obj->data.label.value);
            emit_formatted(codegen, "gray_%s_%s", mod_name, member);
            /* Look up target_func for default params / mutable ref handling */
            char prefixed[256];
            snprintf(prefixed, sizeof(prefixed), "%s_%s", mod_name, member);
            for (int field_index = 0; field_index < codegen->func_count; field_index++) {
                if (strcmp(codegen->all_funcs[field_index]->data.func_decl.name, prefixed) == 0) {
                    target_func = codegen->all_funcs[field_index];
                    break;
                }
            }
            if (!target_func) {
                /* Try just the bare member name */
                for (int field_index = 0; field_index < codegen->func_count; field_index++) {
                    const char *registered = codegen->all_funcs[field_index]->data.func_decl.name;
                    const char *us = strchr(registered, '_');
                    if (us && strcmp(us + 1, member) == 0) {
                        target_func = codegen->all_funcs[field_index];
                        break;
                    }
                }
            }
        } else {
            emit_expression(codegen, node->data.call.function);
        }
    } else if (node->data.call.function->kind == NODE_INDEX_EXPR) {
        /* Indexed callee (e.g. ops[0](x, y)). The index expression yields a
         * void * for [func] arrays (see NODE_INDEX_EXPR emitter), which isn't
         * directly callable; wrap with a function-pointer cast derived from
         * the call site's arg types and return type. */
        int nargs = node->data.call.arg_count;
        GrayType *ret_t = codegen->type_table ? typetable_get(codegen->type_table, node) : NULL;
        const char *c_ret = (ret_t && ret_t->kind != TK_UNKNOWN) ? gray_type_to_c_codegen(codegen, type_name(ret_t)) : "int64_t";
        if (ret_t && ret_t->kind == TK_VOID) c_ret = "void";
        emit_formatted(codegen, "((%s (*)(", c_ret);
        for (int i = 0; i < nargs; i++) {
            if (i > 0) emit(codegen, ", ");
            GrayType *arg_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.call.args[i]) : NULL;
            if (arg_t && arg_t->kind != TK_UNKNOWN) {
                emit(codegen, gray_type_to_c_codegen(codegen, type_name(arg_t)));
            } else {
                emit(codegen, "int64_t");
            }
        }
        if (nargs == 0) emit(codegen, "void");
        emit(codegen, "))");
        emit_expression(codegen, node->data.call.function);
        emit(codegen, ")");
    } else {
        emit_expression(codegen, node->data.call.function);
    }

    /* Determine total args: provided + defaults */
    GrayFuncSig *call_typed_sig = (GrayFuncSig *)codegen->pending_call_typed_sig;
    codegen->pending_call_typed_sig = NULL; /* one-shot; clear before recursion */
    int total_args = node->data.call.arg_count;
    int param_count = target_func ? target_func->data.func_decl.param_count
                                  : (call_typed_sig ? call_typed_sig->param_count : 0);
    if (total_args < param_count) total_args = param_count;

    /* If any default-fill positions reference a parameter name, the default
     * expression has to evaluate with those names in scope. Inline-paste
     * into the caller would emit `gray_fn_f(10, a + 1)` and clang errors on
     * `a`. Detect "defaults will fire AND none of the affected params are
     * &-mut" and rewrite as a statement expression that binds the earlier
     * provided args to local copies named after the parameters. */
    bool uses_defaults = false;
    bool any_mut_param = false;
    if (target_func) {
        for (int i = node->data.call.arg_count; i < param_count; i++) {
            if (target_func->data.func_decl.params[i].default_value) {
                uses_defaults = true;
                break;
            }
        }
        for (int i = 0; i < target_func->data.func_decl.param_count; i++) {
            if (target_func->data.func_decl.params[i].mutable) {
                any_mut_param = true;
                break;
            }
        }
    }
    /* Mut params would need address aliasing rather than value-copy bindings;
     * those edge cases keep the inline-paste path. The wrap also
     * only works for the direct-call branch; call-through-variable paths
     * have a cast prefix (e.g. ((T (*)(...))g)) that this code can't safely
     * reconstruct. */
    if (uses_defaults && !any_mut_param && direct_known_call) {
        /* Re-emit: ({ T0 a0 = arg0; T1 a1 = arg1; ...; gray_fn_X(a0, a1, default_for_n, ...); }) */
        size_t out_len = codegen->output.len;
        size_t fn_emit_len = 0;
        /* Pull off the just-emitted "gray_fn_<name>" so we can prepend the
         * statement-expression bindings. */
        char saved_fn[TYPE_NAME_MAX] = {0};
        if (out_len > 0) {
            /* Walk back to start of the most recent identifier */
            size_t scan = out_len;
            while (scan > 0) {
                char ch = codegen->output.data[scan - 1];
                if (!(isalnum((unsigned char)ch) || ch == '_')) break;
                scan--;
            }
            fn_emit_len = out_len - scan;
            if (fn_emit_len > 0 && fn_emit_len < sizeof(saved_fn)) {
                memcpy(saved_fn, codegen->output.data + scan, fn_emit_len);
                saved_fn[fn_emit_len] = '\0';
                codegen->output.len = scan;
            }
        }
        emit(codegen, "({ ");
        for (int i = 0; i < node->data.call.arg_count; i++) {
            if (target_func->data.func_decl.params[i].is_type_param) continue;
            const char *pname = target_func->data.func_decl.params[i].name;
            const char *ptn = target_func->data.func_decl.params[i].type_name;
            const char *c_ty = ptn ? gray_type_to_c_codegen(codegen, ptn) : "int64_t";
            emit_formatted(codegen, "%s %s = ", c_ty, pname ? sanitize_name(pname) : "_arg");
            if (!emit_bigint_coerced(codegen, ptn, node->data.call.args[i]) &&
                !emit_narrowing_cast(codegen, ptn, node->data.call.args[i], node->token.line))
                emit_expression(codegen, node->data.call.args[i]);
            emit(codegen, "; ");
        }
        emit(codegen, saved_fn);
        emit(codegen, "(");
        {
            bool df_first = true;
            for (int i = 0; i < total_args; i++) {
                if (target_func->data.func_decl.params[i].is_type_param) continue;
                if (!df_first) emit(codegen, ", ");
                df_first = false;
                if (i < node->data.call.arg_count) {
                    const char *pname = target_func->data.func_decl.params[i].name;
                    emit_formatted(codegen, "%s", pname ? sanitize_name(pname) : "_arg");
                } else {
                    emit_expression(codegen, target_func->data.func_decl.params[i].default_value);
                }
            }
        }
        emit(codegen, "); })");
        return;
    }

    emit(codegen, "(");
    bool first_arg = true;
    for (int i = 0; i < total_args; i++) {
        /* Skip type params — erased in C */
        if (target_func && i < target_func->data.func_decl.param_count &&
            target_func->data.func_decl.params[i].is_type_param) continue;
        if (!first_arg) emit(codegen, ", ");
        first_arg = false;

        if (i < node->data.call.arg_count) {
            /* Provided argument */
            bool needs_addr = false;
            if (target_func && i < target_func->data.func_decl.param_count) {
                needs_addr = target_func->data.func_decl.params[i].mutable;
            } else if (call_typed_sig && i < call_typed_sig->param_count) {
                needs_addr = call_typed_sig->param_mutable[i];
            }
            if (needs_addr) {
                emit_mutable_call_argument(codegen, node->data.call.args[i], true);
            } else {
                const char *param_tn = NULL;
                if (target_func && i < target_func->data.func_decl.param_count)
                    param_tn = target_func->data.func_decl.params[i].type_name;
                else if (call_typed_sig && i < call_typed_sig->param_count)
                    param_tn = call_typed_sig->param_types[i];
                if (!emit_bigint_coerced(codegen, param_tn, node->data.call.args[i]) &&
                    !emit_narrowing_cast(codegen, param_tn, node->data.call.args[i], node->token.line))
                    emit_expression(codegen, node->data.call.args[i]);
            }
        } else if (target_func && i < param_count &&
                   target_func->data.func_decl.params[i].default_value) {
            /* Default value */
            emit_expression(codegen, target_func->data.func_decl.params[i].default_value);
        } else {
            /* No arg and no default; emit zero */
            emit(codegen, "0");
        }
    }
    emit(codegen, ")");
}

/* Resolve a call to the declaration it names, for the two shapes that can
 * reach a user function: a bare name and a member call (struct functions and
 * imported functions are registered under a qualified name, so those match on
 * the trailing component). Returns NULL when the match is ambiguous or the
 * callees disagree, which leaves the caller on the conservative path. */
static AstNode *resolve_called_function(CodeGen *codegen, AstNode *node) {
    AstNode *fn = node->data.call.function;
    const char *name = NULL;
    if (fn->kind == NODE_LABEL) name = fn->data.label.value;
    else if (fn->kind == NODE_MEMBER_EXPR) name = fn->data.member.member;
    if (!name) return NULL;

    /* A module-qualified stdlib call (channels.send(...)) is never a user
     * function. Matching one here by trailing name component against a
     * same-named struct function wraps it in the caller-arena block and
     * captures its result with __auto_type, which fails to compile when the
     * stdlib function returns void. */
    if (fn->kind == NODE_MEMBER_EXPR && fn->data.member.object &&
        fn->data.member.object->kind == NODE_LABEL &&
        codegen_module_imported(codegen, fn->data.member.object->data.label.value)) {
        return NULL;
    }

    AstNode *exact = find_function(codegen, name);
    if (exact) return exact;

    size_t nlen = strlen(name);
    AstNode *match = NULL;
    for (int i = 0; i < codegen->func_count; i++) {
        AstNode *cand = codegen->all_funcs[i];
        const char *reg = cand->data.func_decl.name;
        size_t rlen = strlen(reg);
        if (rlen <= nlen + 1) continue;
        if (reg[rlen - nlen - 1] != '_' || strcmp(reg + rlen - nlen, name) != 0) continue;
        if (!match) { match = cand; continue; }
        /* Several candidates: usable only while they agree on what matters. */
        if (function_uses_caller_arena(codegen, match) != function_uses_caller_arena(codegen, cand) ||
            (match->data.func_decl.return_type_count == 0) !=
            (cand->data.func_decl.return_type_count == 0))
            return NULL;
    }
    return match;
}

/* A function that runs in the caller's arena writes through its pointer
 * parameters into data the caller owns, and so does anything it calls. Inside
 * a scope arena that ambient arena is the block's, which dies at the end of
 * the block while the mutated data lives on — so run the call in the
 * function-level arena instead. */
static void emit_call_expression(CodeGen *codegen, AstNode *node) {
    AstNode *callee = codegen->loop_scope_depth > 0
        ? resolve_called_function(codegen, node) : NULL;
    if (!callee || !function_uses_caller_arena(codegen, callee)) {
        emit_call_expression_body(codegen, node);
        return;
    }

    int id = codegen_next_id(codegen);
    emit_formatted(codegen, "({ GrayArena *_gray_csave%d = gray_default_arena; "
                            "gray_default_arena = _gray_outer_arena; ", id);
    if (callee->data.func_decl.return_type_count == 0) {
        emit_call_expression_body(codegen, node);
        emit_formatted(codegen, "; gray_default_arena = _gray_csave%d; })", id);
    } else {
        emit_formatted(codegen, "__auto_type _gray_cval%d = ", id);
        emit_call_expression_body(codegen, node);
        emit_formatted(codegen, "; gray_default_arena = _gray_csave%d; _gray_cval%d; })", id, id);
    }
}

/* --- Statement Emission --- */

static const char *extract_array_element_type(const char *type_name) {
    if (!type_name || type_name[0] != '[') return NULL;
    static char buffer[TYPE_NAME_MAX];
    size_t len = strlen(type_name);
    if (len < 3) return NULL;
    /* Dynamic array "[int]" -> "int" */
    /* Fixed-size "[int,3]" -> "int" (strip size) */
    /* Nested "[[int]]" -> "[int]" */
    const char *start = type_name + 1;
    const char *end = type_name + len - 1;
    /* Find the comma for fixed-size, or just strip brackets */
    for (size_t i = 1; i < len - 1; i++) {
        if (type_name[i] == ',') {
            size_t elen = i - 1;
            memcpy(buffer, start, elen);
            buffer[elen] = '\0';
            return buffer;
        }
    }
    size_t elen = (size_t)(end - start);
    memcpy(buffer, start, elen);
    buffer[elen] = '\0';
    return buffer;
}

/* Extract size from fixed-size array type "[int,3]" -> 3, returns 0 if dynamic */
static int extract_array_size(const char *type_name) {
    if (!type_name || type_name[0] != '[') return 0;
    const char *comma = strchr(type_name, ',');
    if (!comma) return 0;
    return atoi(comma + 1);
}

/* Check if type is a nested array "[[...]]" */
static bool is_nested_array_type(const char *type_name) {
    return type_name && type_name[0] == '[' && type_name[1] == '[';
}

/* Emit a runtime range-check cast for narrowing integer assignments.
 * Wraps val in gray_cast_check/gray_ucast_check when target is a sized
 * integer type (i8/i16/i32/u8/u16/u32/byte).  Returns true when a
 * check was emitted; caller should emit val normally when false. */
static bool emit_narrowing_cast(CodeGen *codegen, const char *target,
                                AstNode *val, int line) {
    if (!target) return false;
    const char *smin = NULL, *smax = NULL;
    bool is_unsigned = false;
    if      (strcmp(target, "i8")   == 0) { smin = "-128";          smax = "127"; }
    else if (strcmp(target, "i16")  == 0) { smin = "-32768";        smax = "32767"; }
    else if (strcmp(target, "i32")  == 0) { smin = "-2147483648LL"; smax = "2147483647LL"; }
    else if (strcmp(target, "u8")   == 0 ||
             strcmp(target, "byte") == 0) { is_unsigned = true; smax = "255"; }
    else if (strcmp(target, "u16")  == 0) { is_unsigned = true; smax = "65535"; }
    else if (strcmp(target, "u32")  == 0) { is_unsigned = true; smax = "4294967295ULL"; }
    else return false;

    const char *c_target = gray_type_to_c_codegen(codegen, target);
    if (codegen->in_const_decl) {
        /* File-scope const: typechecker already validated the value fits;
         * emit a plain cast so C sees a compile-time constant. */
        emit_formatted(codegen, "(%s)(", c_target);
        emit_expression(codegen, val);
        emit(codegen, ")");
    } else if (is_unsigned) {
        emit_formatted(codegen, "(%s)gray_ucast_check(", c_target);
        emit_expression(codegen, val);
        emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d)", smax, target, codegen->file, line);
    } else {
        emit_formatted(codegen, "(%s)gray_cast_check(", c_target);
        emit_expression(codegen, val);
        emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d)", smin, smax, target, codegen->file, line);
    }
    return true;
}

/* Emit the initializer for a fixed-size array declaration.
 * When the value is a partial array literal (count < fixed_size), a C
 * compound literal with an explicit size is used so the trailing slots are
 * zero-initialized by C semantics, and fixed_size is passed as the GrayArray
 * length so index bounds match the declared size N, not the init count k. */
static void emit_fixed_size_array_initializer(CodeGen *codegen, AstNode *value,
                                       const char *elem_type, int fixed_size) {
    if (value && value->kind == NODE_ARRAY_VALUE &&
        value->data.array_value.count < fixed_size) {
        const char *c_elem_type = gray_type_to_c_codegen(codegen, elem_type);
        int count = value->data.array_value.count;
        emit_formatted(codegen, "gray_array_from(gray_default_arena, (%s[%d]){", c_elem_type, fixed_size);
        for (int i = 0; i < count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, value->data.array_value.elements[i]);
        }
        emit_formatted(codegen, "}, sizeof(%s), %d)", c_elem_type, fixed_size);
    } else {
        emit_expression(codegen, value);
    }
}

/* ── variable declaration sub-handlers ─────────────────────────────── */

static void emit_vardecl_array(CodeGen *codegen, AstNode *node,
                                const char *type_name, const char *elem_type) {
    /* [func] with a single func ref init: emit as void* (function pointer),
     * not GrayArray. Array-literal inits still use GrayArray. */
    if ((strcmp(elem_type, "func") == 0 || strncmp(elem_type, "func(", 5) == 0) &&
        node->data.var_decl.value &&
        node->data.var_decl.value->kind == NODE_FUNC_REF) {
        emit_formatted(codegen, "void *%s = ", sanitize_name(node->data.var_decl.name));
        emit_expression(codegen, node->data.var_decl.value);
        emit(codegen, ";\n");
        return;
    }
    int fixed_size = extract_array_size(type_name);
    if (fixed_size > 0) {
        /* Thread the declared [T, N] type so a full-init literal (which
         * routes through emit_array_value) sizes its elements to the
         * annotation rather than defaulting to int64_t. */
        const char *saved_fx_var_type = codegen->current_var_type;
        codegen->current_var_type = type_name;
        /* Fixed-size array: use GrayArray but initialized with exact capacity */
        if (codegen->indent == 0) {
            /* File scope: emit uninitialized global, defer init to gray_init_globals */
            emit_formatted(codegen, "GrayArray %s;\n", sanitize_name(node->data.var_decl.name));
            /* Store deferred init in the init buffer */
            if (node->data.var_decl.value) {
                append_format_to_buffer(&codegen->global_init, "    %s = ", sanitize_name(node->data.var_decl.name));
                /* Temporarily redirect output to global_init buffer */
                Buf saved = codegen->output;
                codegen->output = codegen->global_init;
                codegen->indent = 1;
                emit_fixed_size_array_initializer(codegen, node->data.var_decl.value, elem_type, fixed_size);
                emit(codegen, ";\n");
                codegen->global_init = codegen->output;
                codegen->output = saved;
                codegen->indent = 0;
            }
        } else {
            emit_formatted(codegen, "GrayArray %s = ", sanitize_name(node->data.var_decl.name));
            if (node->data.var_decl.value) {
                emit_fixed_size_array_initializer(codegen, node->data.var_decl.value, elem_type, fixed_size);
            } else {
                const char *c_elem_type = gray_type_to_c_codegen(codegen, elem_type);
                emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), %d)", c_elem_type, fixed_size);
            }
            emit(codegen, ";\n");
        }
        codegen->current_var_type = saved_fx_var_type;
        return;
    }

    if (is_nested_array_type(type_name)) {
        codegen->current_var_type = type_name;
        AstNode *init = node->data.var_decl.value;
        bool label_init = init && init->kind == NODE_LABEL;
        const char *label_elem_tn = NULL;
        if (label_init) {
            GrayType *src_t = codegen->type_table
                ? typetable_get(codegen->type_table, init) : NULL;
            if (src_t && src_t->kind == TK_ARRAY) {
                label_elem_tn = src_t->element_type;
            }
        }
        if (codegen->indent == 0) {
            emit_formatted(codegen, "GrayArray %s;\n", sanitize_name(node->data.var_decl.name));
            Buf saved = codegen->output; codegen->output = codegen->global_init; codegen->indent = 1;
            emit_formatted(codegen, "    %s = ", sanitize_name(node->data.var_decl.name));
            if (label_init) emit_deep_array_copy(codegen, init, label_elem_tn);
            else if (init) emit_expression(codegen, init);
            else emit(codegen, "gray_array_new(gray_default_arena, sizeof(GrayArray), 4)");
            emit(codegen, ";\n");
            codegen->global_init = codegen->output; codegen->output = saved; codegen->indent = 0;
        } else {
            emit_formatted(codegen, "GrayArray %s = ", sanitize_name(node->data.var_decl.name));
            if (label_init) emit_deep_array_copy(codegen, init, label_elem_tn);
            else if (init) emit_expression(codegen, init);
            else emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(GrayArray), 4)");
            emit(codegen, ";\n");
        }
        return;
    }

    /* Dynamic array: use GrayArray */
    const char *c_elem_type = gray_type_to_c_codegen(codegen, elem_type);
    if (codegen->indent == 0) {
        emit_formatted(codegen, "GrayArray %s;\n", sanitize_name(node->data.var_decl.name));
        Buf saved = codegen->output; codegen->output = codegen->global_init; codegen->indent = 1;
        emit_formatted(codegen, "    %s = ", sanitize_name(node->data.var_decl.name));
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_ARRAY_VALUE &&
            node->data.var_decl.value->data.array_value.count == 0) {
            /* Empty array literal with type annotation; use the declared elem size. */
            emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), 4)", c_elem_type);
        } else if (node->data.var_decl.value) {
            const char *saved_var_type = codegen->current_var_type;
            codegen->current_var_type = type_name;
            emit_expression(codegen, node->data.var_decl.value);
            codegen->current_var_type = saved_var_type;
        } else {
            emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), 4)", c_elem_type);
        }
        emit(codegen, ";\n");
        codegen->global_init = codegen->output; codegen->output = saved; codegen->indent = 0;
    } else {
    emit_formatted(codegen, "GrayArray %s = ", sanitize_name(node->data.var_decl.name));
    if (node->data.var_decl.value &&
        node->data.var_decl.value->kind == NODE_ARRAY_VALUE &&
        node->data.var_decl.value->data.array_value.count == 0) {
        /* Empty array literal with type annotation; use correct elem size */
        emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), 4)", c_elem_type);
    } else if (node->data.var_decl.value &&
               (node->data.var_decl.value->kind == NODE_LABEL ||
                node->data.var_decl.value->kind == NODE_MEMBER_EXPR)) {
        /* Copy-by-default: deep copy when assigning from another variable
         * or a struct field access (e.g. `mut copy [int] = s.field`).
         * Without this, member-expr sources share backing storage with the
         * originating struct field (#1789). */
        GrayType *src_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.var_decl.value) : NULL;
        const char *elem_tn = (src_t && src_t->kind == TK_ARRAY)
            ? src_t->element_type : NULL;
        emit_deep_array_copy(codegen, node->data.var_decl.value, elem_tn);
    } else if (node->data.var_decl.value) {
        /* Thread the declared array type so the array-literal codegen
         * can infer the element type when the typetable misses the
         * first element (e.g. module-qualified struct function calls). */
        const char *saved_var_type = codegen->current_var_type;
        codegen->current_var_type = type_name;
        emit_expression(codegen, node->data.var_decl.value);
        codegen->current_var_type = saved_var_type;
    } else {
        emit_formatted(codegen, "gray_array_new(gray_default_arena, sizeof(%s), 4)", c_elem_type);
    }
    emit(codegen, ";\n");
    } /* end else (indent > 0) */
}

static void emit_vardecl_map(CodeGen *codegen, AstNode *node,
                              const char *type_name) {
    /* Parse K:V from type string to determine C types */
    GrayType *map_type = type_from_name(type_name);
    const char *c_kt = "GrayString";
    const char *c_vt = "int64_t";
    if (map_type && map_type->key_type) c_kt = gray_map_element_c_type(codegen, map_type->key_type);
    if (map_type && map_type->value_type) c_vt = gray_map_element_c_type(codegen, map_type->value_type);

    if (codegen->indent == 0) {
        /* File scope: emit zero-init global, defer initializer to
         * gray_init_globals — map literals expand to GCC statement
         * expressions which are not legal at file scope. */
        emit_formatted(codegen, "GrayMap %s;\n", sanitize_name(node->data.var_decl.name));
        Buf saved = codegen->output; codegen->output = codegen->global_init; codegen->indent = 1;
        emit_formatted(codegen, "    %s = ", sanitize_name(node->data.var_decl.name));
        if (node->data.var_decl.value &&
            node->data.var_decl.value->kind == NODE_LABEL) {
            int tag = codegen_next_id(codegen);
            char src_var[VAR_NAME_BUF];
            snprintf(src_var, sizeof(src_var), "_ms%d", tag);
            emit_formatted(codegen, "({ GrayMap %s = ", src_var);
            emit_expression(codegen, node->data.var_decl.value);
            emit(codegen, "; ");
            emit_value_deep_copy(codegen, type_name, src_var);
            emit(codegen, "; })");
        } else if (node->data.var_decl.value) {
            const char *saved_var_type = codegen->current_var_type;
            codegen->current_var_type = type_name;
            emit_expression(codegen, node->data.var_decl.value);
            codegen->current_var_type = saved_var_type;
        } else {
            emit_formatted(codegen, "gray_map_new_kind(gray_default_arena, sizeof(%s), sizeof(%s), 8, %s)",
                c_kt, c_vt, gray_map_key_kind_macro(c_kt));
        }
        emit(codegen, ";\n");
        codegen->global_init = codegen->output; codegen->output = saved; codegen->indent = 0;
        return;
    }

    emit_formatted(codegen, "GrayMap %s = ", sanitize_name(node->data.var_decl.name));
    if (node->data.var_decl.value &&
        node->data.var_decl.value->kind == NODE_LABEL) {
        /* Copy-by-default: deep copy when assigning a map from another
         * variable so mutations to the copy don't alias the original. */
        int tag = codegen_next_id(codegen);
        char src_var[VAR_NAME_BUF];
        snprintf(src_var, sizeof(src_var), "_ms%d", tag);
        emit_formatted(codegen, "({ GrayMap %s = ", src_var);
        emit_expression(codegen, node->data.var_decl.value);
        emit(codegen, "; ");
        emit_value_deep_copy(codegen, type_name, src_var);
        emit(codegen, "; })");
    } else if (node->data.var_decl.value) {
        const char *saved_var_type = codegen->current_var_type;
        codegen->current_var_type = type_name;
        emit_expression(codegen, node->data.var_decl.value);
        codegen->current_var_type = saved_var_type;
    } else {
        /* No initializer; create empty map */
        emit_formatted(codegen, "gray_map_new_kind(gray_default_arena, sizeof(%s), sizeof(%s), 8, %s)",
            c_kt, c_vt, gray_map_key_kind_macro(c_kt));
    }
    emit(codegen, ";\n");
}

/* Emit the C zero value for c_type (no leading " = "). Used both for
 * value-less declarations and for file-scope globals whose real
 * initializer is deferred into the global-init buffer. */
static void emit_c_zero_value(CodeGen *codegen, const char *c_type) {
    if (strcmp(c_type, "int64_t") == 0) emit(codegen, "0");
    else if (strcmp(c_type, "double") == 0) emit(codegen, "0.0");
    else if (strcmp(c_type, "bool") == 0) emit(codegen, "false");
    else if (strcmp(c_type, "GrayString") == 0) emit(codegen, "(GrayString){\"\", 0}");
    else if (strcmp(c_type, "GrayArray") == 0) emit(codegen, "(GrayArray){0}");
    else if (strcmp(c_type, "GrayMap") == 0) emit(codegen, "(GrayMap){0}");
    else if (strcmp(c_type, "gray_i128") == 0) emit(codegen, "GRAY_I128_ZERO");
    else if (strcmp(c_type, "gray_u128") == 0) emit(codegen, "GRAY_U128_ZERO");
    else if (strcmp(c_type, "gray_i256") == 0) emit(codegen, "GRAY_I256_ZERO");
    else if (strcmp(c_type, "gray_u256") == 0) emit(codegen, "GRAY_U256_ZERO");
    else emit(codegen, "{0}");
}

/* True when a scalar/struct variable initializer lowers to a C constant
 * expression and is therefore legal at file scope. Anything else
 * (runtime-checked negation, struct literals with array/map fields,
 * string interpolation, calls, references to other globals) must be
 * deferred into the global-init buffer, matching emit_vardecl_array()
 * and emit_vardecl_map(). */
static bool initializer_is_c_constant(AstNode *value) {
    if (!value) return true;
    switch (value->kind) {
        case NODE_BOOL_VALUE:
        case NODE_CHAR_VALUE:
        case NODE_FLOAT_VALUE:
        case NODE_STRING_VALUE:
        case NODE_NIL_VALUE:
            return true;
        case NODE_INT_VALUE:
            /* Overflowed literals lower to a from_decimal() runtime call. */
            return !value->data.int_value.overflow &&
                   !value->data.int_value.overflow_u64;
        default:
            return false;
    }
}

static void emit_vardecl_init(CodeGen *codegen, AstNode *node,
                               const char *c_type, const char *type_name) {
    if (node->data.var_decl.value) {
        emit(codegen, " = ");
        codegen->current_var_name = node->data.var_decl.name;
        codegen->current_var_type = node->data.var_decl.type_name;
        /* Signal to emit_expression that we are inside a file-scope const
         * initializer.  Overflow-check wrappers (gray_add_check etc.) are
         * runtime function calls; C rejects them as file-scope initializers.
         * The typechecker has already verified no overflow for such exprs. */
        bool prev_in_const_decl = codegen->in_const_decl;
        if (codegen->indent == 0 && !node->data.var_decl.mutable)
            codegen->in_const_decl = true;
        /* Bigint literal zero: emit zero constant instead of plain 0 */
        if (type_name && is_bigint_type(type_name) &&
            node->data.var_decl.value->kind == NODE_INT_VALUE &&
            node->data.var_decl.value->data.int_value.value == 0) {
            if (strcmp(type_name, "i128") == 0) emit(codegen, "GRAY_I128_ZERO");
            else if (strcmp(type_name, "u128") == 0) emit(codegen, "GRAY_U128_ZERO");
            else if (strcmp(type_name, "i256") == 0) emit(codegen, "GRAY_I256_ZERO");
            else if (strcmp(type_name, "u256") == 0) emit(codegen, "GRAY_U256_ZERO");
        } else if (type_name && is_bigint_type(type_name) &&
                   node->data.var_decl.value->kind == NODE_INT_VALUE) {
            /* Integer literal for bigint type */
            const char *pfx = bigint_prefix(type_name);
            if (node->data.var_decl.value->data.int_value.overflow) {
                /* Overflowed literal: parse from decimal string at runtime */
                emit_formatted(codegen, "%s_from_decimal(\"%s\")", pfx,
                    node->data.var_decl.value->data.int_value.literal);
            } else if (codegen->in_const_decl) {
                /* File-scope const: emit compound literal instead of function call */
                int64_t v = node->data.var_decl.value->data.int_value.value;
                if (strcmp(type_name, "i128") == 0)
                    emit_formatted(codegen, "((gray_i128){(uint64_t)%lldLL, %s})", (long long)v, v < 0 ? "-1" : "0");
                else if (strcmp(type_name, "u128") == 0)
                    emit_formatted(codegen, "((gray_u128){%lluULL, 0})", (unsigned long long)(uint64_t)v);
                else if (strcmp(type_name, "i256") == 0)
                    emit_formatted(codegen, "((gray_i256){{(uint64_t)%lldLL, %s, %s, %s}})",
                        (long long)v, v < 0 ? "(uint64_t)-1" : "0",
                        v < 0 ? "(uint64_t)-1" : "0", v < 0 ? "(uint64_t)-1" : "0");
                else if (strcmp(type_name, "u256") == 0)
                    emit_formatted(codegen, "((gray_u256){{%lluULL, 0, 0, 0}})", (unsigned long long)(uint64_t)v);
            } else {
                /* Fits in 64-bit: use direct constructor */
                int64_t v = node->data.var_decl.value->data.int_value.value;
                const char *from_suffix = (strcmp(type_name, "u128") == 0 || strcmp(type_name, "u256") == 0) ? "u64" : "i64";
                emit_formatted(codegen, "%s_from_%s(%lldLL)", pfx, from_suffix, (long long)v);
            }
        } else if (type_name && is_bigint_type(type_name) &&
                   node->data.var_decl.value->kind == NODE_PREFIX_EXPR &&
                   node->data.var_decl.value->data.prefix.op == TOK_MINUS &&
                   node->data.var_decl.value->data.prefix.right->kind == NODE_INT_VALUE) {
            /* Negated integer literal for bigint: -N → from_i64(-N) or from_decimal("-N") */
            const char *pfx = bigint_prefix(type_name);
            AstNode *inner = node->data.var_decl.value->data.prefix.right;
            if (inner->data.int_value.overflow) {
                emit_formatted(codegen, "%s_from_decimal(\"-%s\")", pfx, inner->data.int_value.literal);
            } else {
                emit_formatted(codegen, "%s_from_i64(%lldLL)", pfx,
                    -(long long)inner->data.int_value.value);
            }
        } else if (type_name && is_bigint_type(type_name) &&
                   node->data.var_decl.value->kind == NODE_INFIX_EXPR) {
            /* Infix expression assigned to bigint; use bigint operations.
             * Emit the infix manually with bigint operand wrapping since
             * resolve_bigint_type won't detect raw literals as bigint. */
            AstNode *infix = node->data.var_decl.value;
            const char *pfx = bigint_prefix(type_name);
            TokenType op = infix->data.infix.op;
            const char *fn_op = NULL;
            if (op == TOK_PLUS) fn_op = "add";
            else if (op == TOK_MINUS) fn_op = "sub";
            else if (op == TOK_ASTERISK) fn_op = "mul";
            else if (op == TOK_SLASH) fn_op = "div";
            else if (op == TOK_PERCENT) fn_op = "mod";
            if (fn_op) {
                bool is_checked = (strcmp(fn_op, "add") == 0 || strcmp(fn_op, "sub") == 0 || strcmp(fn_op, "mul") == 0);
                bool needs_loc = (strcmp(fn_op, "div") == 0 || strcmp(fn_op, "mod") == 0);
                if (is_checked)
                    emit_formatted(codegen, "%s_%s_checked(", pfx, fn_op);
                else
                    emit_formatted(codegen, "%s_%s(", pfx, fn_op);
                EMIT_BIGINT_OPERAND(codegen, infix->data.infix.left, pfx, type_name, NULL);
                emit(codegen, ", ");
                EMIT_BIGINT_OPERAND(codegen, infix->data.infix.right, pfx, type_name, NULL);
                if (is_checked || needs_loc)
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                else
                    emit(codegen, ")");
            } else {
                emit_expression(codegen, node->data.var_decl.value);
            }
        } else if (type_name && type_name[0] == '^' &&
                   node->data.var_decl.value->kind == NODE_LABEL &&
                   is_reference_variable(codegen, node->data.var_decl.value->data.label.value)) {
            /* Assigning a ref variable to a ^T pointer; pass the pointer through
             * without auto-dereferencing */
            emit(codegen, node->data.var_decl.value->data.label.value);
        } else if (node->data.var_decl.value->kind == NODE_CALL_EXPR &&
                   node->data.var_decl.value->data.call.function->kind == NODE_LABEL &&
                   strcmp(node->data.var_decl.value->data.call.function->data.label.value, "addr") == 0 &&
                   type_name && (strcmp(type_name, "uint") == 0 || strcmp(type_name, "int") == 0 ||
                                 strcmp(type_name, "u64") == 0 || strcmp(type_name, "i64") == 0)) {
            /* addr() assigned to integer type; cast pointer to uintptr_t */
            emit(codegen, "(uintptr_t)");
            emit_expression(codegen, node->data.var_decl.value);
        } else if (node->data.var_decl.value->kind == NODE_LABEL &&
                   type_name && type_needs_deep_copy(codegen, type_name)) {
            /* Copy-by-default: deep copy structs (and maps) that contain
             * arrays/maps/strings so the copy is fully independent. */
            int tag = codegen_next_id(codegen);
            char src_var[VAR_NAME_BUF];
            snprintf(src_var, sizeof(src_var), "_vdc%d", tag);
            emit_formatted(codegen, "({ %s %s = ", c_type, src_var);
            emit_expression(codegen, node->data.var_decl.value);
            emit(codegen, "; ");
            emit_value_deep_copy(codegen, type_name, src_var);
            emit(codegen, "; })");
        } else if (type_name && is_bigint_type(type_name) &&
                   !resolve_bigint_type(codegen, node->data.var_decl.value)) {
            /* Scalar variable/expression assigned to a wide integer type.
             * Wrap with from_i64/from_u64 so the C assignment is valid.
             * Covers: mut big i128 = some_int_var */
            emit_scalar_to_bigint(codegen, type_name, node->data.var_decl.value, NULL);
        } else if (!emit_narrowing_cast(codegen, type_name, node->data.var_decl.value, node->token.line)) {
            emit_expression(codegen, node->data.var_decl.value);
        }
        codegen->in_const_decl = prev_in_const_decl;
        codegen->current_var_name = NULL;
        codegen->current_var_type = NULL;
    } else {
        /* Zero-initialize when no value is provided */
        emit(codegen, " = ");
        emit_c_zero_value(codegen, c_type);
    }

    emit(codegen, ";\n");
}

static void emit_variable_declaration(CodeGen *codegen, AstNode *node) {
    emit_indent(codegen);

    const char *type_name = node->data.var_decl.type_name;
    const char *elem_type = extract_array_element_type(type_name);

    if (elem_type) {
        emit_vardecl_array(codegen, node, type_name, elem_type);
        return;
    }

    /* Map type: map[K:V] */
    if (type_name && strncmp(type_name, "map[", 4) == 0) {
        emit_vardecl_map(codegen, node, type_name);
        return;
    }

    const char *c_type = gray_type_to_c_codegen(codegen, type_name);

    /* Register bigint variable for type tracking */
    if (type_name && is_bigint_type(type_name)) {
        register_bigint_variable(codegen, node->data.var_decl.name, type_name);
    } else if (!type_name && node->data.var_decl.value) {
        /* Inferred-type var (e.g. `mut b = copy(a)`): consult the typetable
         * so wide-integer types propagate through copy(), function calls,
         * member access, etc. Without this the var is silently treated as
         * int and downstream uses (println, arithmetic) emit the wrong
         * runtime calls. Also try resolve_bigint_type() so constructor
         * calls like `mut a = i128(42)` register correctly when the
         * typetable stores the base type name ("int") rather than the
         * width-specific name ("i128"). */
        GrayType *vt = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.var_decl.value)
            : NULL;
        /* An array/map type stores its element type name in ->name (e.g. an
         * inferred [i128] has name "i128"), so gate on the kind too — the var
         * itself is the container, not a wide integer. */
        bool vt_is_bigint_scalar = vt && vt->name && is_bigint_type(vt->name) &&
            (vt->kind == TK_INT || vt->kind == TK_UINT);
        const char *bi_name = vt_is_bigint_scalar
            ? vt->name : resolve_bigint_type(codegen, node->data.var_decl.value);
        if (bi_name) {
            register_bigint_variable(codegen, node->data.var_decl.name, bi_name);
        }
    }

    /* Skip blank identifiers (_) */
    if (strcmp(node->data.var_decl.name, "_") == 0) {
        if (node->data.var_decl.value) {
            emit_indent(codegen);
            emit(codegen, "(void)(");
            emit_expression(codegen, node->data.var_decl.value);
            emit(codegen, ");\n");
        }
        return;
    }

    /* If no type annotation, try to infer from value */
    if (!type_name && node->data.var_decl.value) {
        AstNode *val = node->data.var_decl.value;
        if (val->kind == NODE_STRING_VALUE || val->kind == NODE_INTERPOLATED_STRING) {
            c_type = "GrayString";
        } else if (val->kind == NODE_FLOAT_VALUE) {
            c_type = "double";
        } else if (val->kind == NODE_BOOL_VALUE) {
            c_type = "bool";
        } else if (val->kind == NODE_ARRAY_VALUE) {
            c_type = "GrayArray";
        } else if (val->kind == NODE_MAP_VALUE) {
            c_type = "GrayMap";
        } else if (val->kind == NODE_STRUCT_VALUE) {
            /* : use mangled name for generic struct instantiations */
            if (val->data.struct_value.wildcard_binding) {
                const char *binding = val->data.struct_value.wildcard_binding;
                const char *base = val->data.struct_value.name;
                static char sv_buf[MSG_BUF_SIZE];
                size_t string_pos = snprintf(sv_buf, sizeof(sv_buf), "%s__", base);
                for (const char *ch = binding; *ch && string_pos < sizeof(sv_buf) - 1; ch++)
                    sv_buf[string_pos++] = (isalnum((unsigned char)*ch) || *ch == '_') ? *ch : '_';
                sv_buf[string_pos] = '\0';
                c_type = gray_type_to_c_codegen(codegen, sv_buf);
            } else {
                c_type = gray_type_to_c_codegen(codegen, val->data.struct_value.name);
            }
        } else if (val->kind == NODE_INFIX_EXPR) {
            /* Check type table for infix result type */
            GrayType *infix_t = codegen->type_table ? typetable_get(codegen->type_table, val) : NULL;
            if (infix_t && infix_t->kind == TK_STRING) {
                c_type = "GrayString";
            } else if (infix_t && infix_t->kind == TK_FLOAT) {
                c_type = "double";
            } else if (infix_t && infix_t->kind == TK_BOOL) {
                c_type = "bool";
            } else {
                c_type = "__auto_type";
            }
        } else if (val->kind == NODE_CALL_EXPR || val->kind == NODE_NEW_EXPR ||
                   val->kind == NODE_MEMBER_EXPR || val->kind == NODE_INDEX_EXPR ||
                   val->kind == NODE_POSTFIX_EXPR) {
            /* Use __auto_type for function calls, new(), member access, index,
             * and postfix expressions (e.g. ptr^ dereference — without this,
             * `mut x = p^` where p is ^StructType would be emitted as int64_t
             * instead of the correct struct type). */
            c_type = "__auto_type";
        } else if (val->kind == NODE_FUNC_REF) {
            /* Function reference; use __auto_type to capture the pointer type */
            c_type = "__auto_type";
        } else if (val->kind == NODE_LABEL) {
            /* Variable reference; use __auto_type to propagate the source type */
            c_type = "__auto_type";
        } else if (val->kind == NODE_CAST_EXPR) {
            /* Cast expression; use the target type */
            c_type = gray_type_to_c_codegen(codegen, val->data.cast.target_type);
        }
    }

    /* Detect ref() assignment; register as transparent reference (but not for function refs) */
    if (node->data.var_decl.value && node->data.var_decl.value->kind == NODE_CALL_EXPR) {
        AstNode *function_node = node->data.var_decl.value->data.call.function;
        if (function_node->kind == NODE_LABEL && strcmp(function_node->data.label.value, "ref") == 0) {
            /* Register as a transparent reference for any assignable source —
             * variable, struct field, or index expression. Without this,
             * ref(struct.field) was registered as a plain value var and
             * later GRAY_ARRAY_SET(&(r), ...) produced GrayArray ** instead of
             * GrayArray *. The auto-deref path in NODE_LABEL emission
             * handles field/index sources the same as variable sources. */
            if (node->data.var_decl.value->data.call.arg_count == 1) {
                AstNode *arg = node->data.var_decl.value->data.call.args[0];
                bool is_assignable =
                    (arg->kind == NODE_LABEL && !find_referenced_function(codegen, arg)) ||
                    arg->kind == NODE_MEMBER_EXPR ||
                    arg->kind == NODE_INDEX_EXPR;
                if (is_assignable) {
                    register_reference_variable(codegen, node->data.var_decl.name);
                }
            }
        }
    }

    /* Detect raw() assignment; register as raw pointer (nil check skipped on deref) */
    bool is_raw_init = false;
    if (node->data.var_decl.value && node->data.var_decl.value->kind == NODE_CALL_EXPR) {
        AstNode *fn = node->data.var_decl.value->data.call.function;
        if (fn->kind == NODE_LABEL && strcmp(fn->data.label.value, "raw") == 0) {
            register_raw_variable(codegen, node->data.var_decl.name);
            is_raw_init = true;
        }
    }
    /* If a pointer variable shadows a raw variable from an outer scope,
     * push a non-raw override so inner dereferences get nil checks. */
    if (!is_raw_init && type_name && type_name[0] == '^' &&
        is_raw_variable(codegen, node->data.var_decl.name)) {
        unregister_raw_variable(codegen, node->data.var_decl.name);
    }

    /* Detect new() assignment; register as heap-tracked pointer so later
     * field container reassignment through it targets gray_heap_arena.
     * Any other declaration clears a shadowed outer-scope heap variable
     * of the same name. */
    if (is_new_call(node->data.var_decl.value)) {
        register_heap_variable(codegen, node->data.var_decl.name, true);
    } else if (is_heap_variable(codegen, node->data.var_decl.name)) {
        register_heap_variable(codegen, node->data.var_decl.name, false);
    }

    /* File-scope initializer that isn't a C constant expression: emit a
     * zero-initialized global and defer the real initializer into the
     * global-init buffer, the same way emit_vardecl_array/map do. C
     * rejects non-constant file-scope initializers (runtime-checked
     * negation, struct literals with array/map fields, string
     * interpolation, ...). __auto_type needs its initializer inline, so
     * leave those on the normal path. */
    if (codegen->indent == 0 && node->data.var_decl.value &&
        strcmp(c_type, "__auto_type") != 0 &&
        !initializer_is_c_constant(node->data.var_decl.value)) {
        emit_formatted(codegen, "%s %s = ", c_type, sanitize_name(node->data.var_decl.name));
        emit_c_zero_value(codegen, c_type);
        emit(codegen, ";\n");
        Buf saved = codegen->output;
        codegen->output = codegen->global_init;
        codegen->indent = 1;
        emit_formatted(codegen, "    %s", sanitize_name(node->data.var_decl.name));
        emit_vardecl_init(codegen, node, c_type, type_name);
        codegen->global_init = codegen->output;
        codegen->output = saved;
        codegen->indent = 0;
        return;
    }

    if (!node->data.var_decl.mutable) {
        if (type_name && type_name[0] == '^') {
            /* const pointer: T * const p — the pointer is immutable, not the
             * pointed-to data.  Placing const before the type would produce
             * const T * p (pointer to const T), which incorrectly propagates
             * the const qualifier through dereferences and field accesses. */
            emit_formatted(codegen, "%s const %s", c_type, sanitize_name(node->data.var_decl.name));
        } else {
            emit(codegen, "const ");
            emit_formatted(codegen, "%s %s", c_type, sanitize_name(node->data.var_decl.name));
        }
    } else {
        emit_formatted(codegen, "%s %s", c_type, sanitize_name(node->data.var_decl.name));
    }

    emit_vardecl_init(codegen, node, c_type, type_name);
}

/* True when field_t owns arena-backed memory that must be re-homed to
 * gray_heap_arena rather than the enclosing function's own scoped arena
 * when reassigned through a pointer known to point into gray_heap_arena. */
static bool field_type_needs_arena_escape(GrayType *field_t) {
    return field_t && (field_t->kind == TK_MAP || field_t->kind == TK_ARRAY ||
                        field_t->kind == TK_STRING || field_t->kind == TK_STRUCT);
}

/* Emit `ref = value;` with gray_default_arena swapped to gray_heap_arena for
 * the duration of evaluating value, so any container the value allocates
 * (map/array/string/struct literal) lives as long as the heap-allocated
 * struct it's being attached to, rather than the current function's own
 * scoped arena that gets destroyed when the function returns. */
static void emit_heap_escaped_field_assign(CodeGen *codegen, AstNode *node, const char *ref) {
    emit_formatted(codegen, "{ GrayArena *_esc_h = gray_default_arena; gray_default_arena = gray_heap_arena; %s = ", ref);
    emit_expression(codegen, node->data.assign.value);
    emit(codegen, "; gray_default_arena = _esc_h; }");
}

static void emit_assign_statement(CodeGen *codegen, AstNode *node) {
    /* Implicit declaration: emit as C variable declaration */
    if (node->data.assign.is_decl &&
        node->data.assign.target->kind == NODE_LABEL) {
        emit_indent(codegen);
        GrayType *t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.assign.target)
            : NULL;
        const char *c_type = t ? gray_type_to_c_codegen(codegen, type_name(t)) : "__auto_type";
        const char *decl_name = node->data.assign.target->data.label.value;
        if (is_new_call(node->data.assign.value)) {
            register_heap_variable(codegen, decl_name, true);
        }
        emit_formatted(codegen, "%s %s = ", c_type, sanitize_name(decl_name));
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, ";\n");
        return;
    }

    /* Track raw/addr reassignment: p = raw(x) makes p raw,
     * p = addr(x) removes raw status so nil checks are restored. */
    if (node->data.assign.target->kind == NODE_LABEL &&
        node->data.assign.value && node->data.assign.value->kind == NODE_CALL_EXPR) {
        AstNode *fn = node->data.assign.value->data.call.function;
        if (fn->kind == NODE_LABEL) {
            const char *var = node->data.assign.target->data.label.value;
            if (strcmp(fn->data.label.value, "raw") == 0) {
                register_raw_variable(codegen, var);
            } else if (strcmp(fn->data.label.value, "addr") == 0 &&
                       is_raw_variable(codegen, var)) {
                unregister_raw_variable(codegen, var);
            }
        }
    }
    /* Track heap-pointer reassignment: p = new(T) makes p heap-tracked;
     * any other reassignment of a previously heap-tracked p clears it. */
    if (node->data.assign.target->kind == NODE_LABEL) {
        const char *var = node->data.assign.target->data.label.value;
        if (is_new_call(node->data.assign.value)) {
            register_heap_variable(codegen, var, true);
        } else if (is_heap_variable(codegen, var)) {
            register_heap_variable(codegen, var, false);
        }
    }

    emit_indent(codegen);

    /* Check for array index assignment: arr[i] = value */
    if (node->data.assign.target->kind == NODE_INDEX_EXPR) {
        AstNode *left = node->data.assign.target->data.index_expr.left;
        GrayType *left_t = codegen->type_table ? typetable_get(codegen->type_table, left) : NULL;
        if (left_t && left_t->kind == TK_ARRAY) {
            const char *c_elem = "int64_t";
            if (left_t->element_type) {
                if (strcmp(left_t->element_type, "func") == 0 || strncmp(left_t->element_type, "func(", 5) == 0) {
                    c_elem = "void *";
                } else {
                    c_elem = gray_type_to_c_codegen(codegen, left_t->element_type);
                }
            }
            /* m[key][i] = v: the map lookup lowers to a statement-expression
             * that yields the stored GrayArray by rvalue, so GRAY_ARRAY_SET_AT's
             * &(arr) is invalid. Bind it to a temp — the GrayArray header is a
             * view over the stored buffer, so element writes still land there. */
            if (index_left_is_map_lookup(codegen, left)) {
                AstNode *idx = node->data.assign.target->data.index_expr.index;
                TokenType aop_m = node->data.assign.op;
                bool is_compound_m = (aop_m == TOK_PLUS_ASSIGN || aop_m == TOK_MINUS_ASSIGN || aop_m == TOK_ASTERISK_ASSIGN);
                const char *sn = left_t->element_type;
                const char *smin = NULL, *smax = NULL;
                bool su = false;
                if (sn) sized_int_bounds(sn, &smin, &smax, &su);
                const char *sized_fn = (is_compound_m && smax) ? sized_check_func(aop_m, su) : NULL;
                emit_formatted(codegen, "{ GrayArray _ea = ");
                emit_expression(codegen, left);
                emit(codegen, "; ");
                if (sized_fn) {
                    emit_formatted(codegen, "GRAY_ARRAY_SET_AT(_ea, %s, ", c_elem);
                    emit_expression(codegen, idx);
                    emit_formatted(codegen, ", %s(GRAY_ARRAY_GET_AT(_ea, %s, ", sized_fn, c_elem);
                    emit_expression(codegen, idx);
                    emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                    emit_expression(codegen, node->data.assign.value);
                    if (su) {
                        emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d), \"%s\", %d); }\n", smax, sn, codegen->file, node->token.line, codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d), \"%s\", %d); }\n", smin, smax, sn, codegen->file, node->token.line, codegen->file, node->token.line);
                    }
                    return;
                }
                emit_formatted(codegen, "GRAY_ARRAY_SET_AT(_ea, %s, ", c_elem);
                emit_expression(codegen, idx);
                emit(codegen, ", ");
                if (is_compound_m && strcmp(c_elem, "GrayString") == 0 && aop_m == TOK_PLUS_ASSIGN) {
                    /* The concat result must outlive the loop iteration that
                     * produced it — inside a nested loop that means the outer
                     * arena, not the per-iteration gray_default_arena. */
                    emit_formatted(codegen, "gray_string_concat(%s, GRAY_ARRAY_GET_AT(_ea, GrayString, ",
                        codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena");
                    emit_expression(codegen, idx);
                    emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                    emit_expression(codegen, node->data.assign.value);
                    emit(codegen, ")");
                } else if (is_compound_m) {
                    const char *binop = "+";
                    if (aop_m == TOK_MINUS_ASSIGN) binop = "-";
                    else if (aop_m == TOK_ASTERISK_ASSIGN) binop = "*";
                    emit_formatted(codegen, "GRAY_ARRAY_GET_AT(_ea, %s, ", c_elem);
                    emit_expression(codegen, idx);
                    emit_formatted(codegen, ", \"%s\", %d) %s (", codegen->file, node->token.line, binop);
                    emit_expression(codegen, node->data.assign.value);
                    emit(codegen, ")");
                } else {
                    emit_expression(codegen, node->data.assign.value);
                }
                emit_formatted(codegen, ", \"%s\", %d); }\n", codegen->file, node->token.line);
                return;
            }
            /* Check for array field through struct pointer (rvalue assignability issue).
             * b.items[i] = val where b: ^Bag — the normal member emit produces a
             * GCC statement expression (rvalue); GRAY_ARRAY_SET's &(arr) would fail.
             * Inline the nil check and use _dp->field directly as an assignable target. */
            {
                AstNode *_set_ptr_obj = NULL;
                const char *_set_ptr_field = NULL;
                if (left->kind == NODE_MEMBER_EXPR) {
                    AstNode *_sobj = left->data.member.object;
                    GrayType *_sobj_t = codegen->type_table ? typetable_get(codegen->type_table, _sobj) : NULL;
                    if (_sobj_t && _sobj_t->kind == TK_POINTER) {
                        _set_ptr_obj = _sobj;
                        _set_ptr_field = left->data.member.member;
                    } else if (_sobj->kind == NODE_POSTFIX_EXPR &&
                               _sobj->data.postfix.op == TOK_CARET) {
                        _set_ptr_obj = _sobj->data.postfix.left;
                        _set_ptr_field = left->data.member.member;
                    }
                }
                if (_set_ptr_obj) {
                    bool _set_raw = (_set_ptr_obj->kind == NODE_LABEL && is_raw_variable(codegen, _set_ptr_obj->data.label.value));
                    int my_dp = codegen_next_id(codegen);
                    TokenType aop2 = node->data.assign.op;
                    bool is_compound2 = (aop2 == TOK_PLUS_ASSIGN || aop2 == TOK_MINUS_ASSIGN || aop2 == TOK_ASTERISK_ASSIGN);
                    emit_formatted(codegen, "{ __auto_type _asdp%d = ", my_dp);
                    emit_expression(codegen, _set_ptr_obj);
                    if (_set_raw) {
                        emit_formatted(codegen, "; GRAY_ARRAY_SET_AT(_asdp%d->%s, %s, ",
                              my_dp, sanitize_name(_set_ptr_field), c_elem);
                    } else {
                        emit_formatted(codegen, "; if (!_asdp%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                                  "GRAY_ARRAY_SET_AT(_asdp%d->%s, %s, ",
                              my_dp, codegen->file, node->token.line, my_dp, sanitize_name(_set_ptr_field), c_elem);
                    }
                    emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                    emit(codegen, ", ");
                    if (is_compound2 && strcmp(c_elem, "GrayString") == 0 && aop2 == TOK_PLUS_ASSIGN) {
                        emit_formatted(codegen, "gray_string_concat(%s, GRAY_ARRAY_GET_AT(_asdp%d->%s, GrayString, ",
                            codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena", my_dp, sanitize_name(_set_ptr_field));
                        emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                        emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                        emit_expression(codegen, node->data.assign.value);
                        emit(codegen, ")");
                    } else if (is_compound2) {
                        const char *binop = "+";
                        if (aop2 == TOK_MINUS_ASSIGN) binop = "-";
                        else if (aop2 == TOK_ASTERISK_ASSIGN) binop = "*";
                        emit_formatted(codegen, "GRAY_ARRAY_GET_AT(_asdp%d->%s, %s, ", my_dp, sanitize_name(_set_ptr_field), c_elem);
                        emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                        emit_formatted(codegen, ", \"%s\", %d) %s (", codegen->file, node->token.line, binop);
                        emit_expression(codegen, node->data.assign.value);
                        emit(codegen, ")");
                    } else {
                        emit_expression(codegen, node->data.assign.value);
                    }
                    emit_formatted(codegen, ", \"%s\", %d); }\n", codegen->file, node->token.line);
                    return;
                }
            }
            /* p^[i] = v: direct dereference of array pointer */
            if (left->kind == NODE_POSTFIX_EXPR && left->data.postfix.op == TOK_CARET) {
                AstNode *_dp_inner = left->data.postfix.left;
                bool _dp_raw = (_dp_inner->kind == NODE_LABEL && is_raw_variable(codegen, _dp_inner->data.label.value));
                int my_dp = codegen_next_id(codegen);
                TokenType aop3 = node->data.assign.op;
                bool is_compound3 = (aop3 == TOK_PLUS_ASSIGN || aop3 == TOK_MINUS_ASSIGN || aop3 == TOK_ASTERISK_ASSIGN);
                emit_formatted(codegen, "{ __auto_type _asdp%d = ", my_dp);
                emit_expression(codegen, _dp_inner);
                if (_dp_raw) {
                    emit_formatted(codegen, "; GRAY_ARRAY_SET_AT(*_asdp%d, %s, ",
                          my_dp, c_elem);
                } else {
                    emit_formatted(codegen, "; if (!_asdp%d) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                              "GRAY_ARRAY_SET_AT(*_asdp%d, %s, ",
                          my_dp, codegen->file, node->token.line, my_dp, c_elem);
                }
                emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                emit(codegen, ", ");
                if (is_compound3 && strcmp(c_elem, "GrayString") == 0 && aop3 == TOK_PLUS_ASSIGN) {
                    emit_formatted(codegen, "gray_string_concat(%s, GRAY_ARRAY_GET_AT(*_asdp%d, GrayString, ",
                        codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena", my_dp);
                    emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                    emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                    emit_expression(codegen, node->data.assign.value);
                    emit(codegen, ")");
                } else if (is_compound3) {
                    const char *binop = "+";
                    if (aop3 == TOK_MINUS_ASSIGN) binop = "-";
                    else if (aop3 == TOK_ASTERISK_ASSIGN) binop = "*";
                    emit_formatted(codegen, "GRAY_ARRAY_GET_AT(*_asdp%d, %s, ", my_dp, c_elem);
                    emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                    emit_formatted(codegen, ", \"%s\", %d) %s (", codegen->file, node->token.line, binop);
                    emit_expression(codegen, node->data.assign.value);
                    emit(codegen, ")");
                } else {
                    emit_expression(codegen, node->data.assign.value);
                }
                emit_formatted(codegen, ", \"%s\", %d); }\n", codegen->file, node->token.line);
                return;
            }
            /* Compound assignment on array element with sized-type overflow check */
            TokenType aop = node->data.assign.op;
            bool is_compound = (aop == TOK_PLUS_ASSIGN || aop == TOK_MINUS_ASSIGN || aop == TOK_ASTERISK_ASSIGN);
            if (is_compound && left_t->element_type) {
                const char *sn = left_t->element_type;
                const char *smin = NULL, *smax = NULL;
                bool su = false;
                sized_int_bounds(sn, &smin, &smax, &su);
                if (smax) {
                    const char *function_name = sized_check_func(aop, su);
                    if (function_name) {
                        /* GET reads sizeof(type) bytes, so it must use the real
                         * element width — an int64_t read over-runs a packed
                         * sub-8-byte array (from cast). SET's memcpy length is
                         * the runtime elem_size, so the wider temp is harmless
                         * and keeps literal arrays (8-byte slots) safe. */
                        emit_formatted(codegen, "GRAY_ARRAY_SET_AT(");
                        emit_expression(codegen, left);
                        emit(codegen, ", int64_t, ");
                        emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                        emit_formatted(codegen, ", %s(GRAY_ARRAY_GET_AT(", function_name);
                        emit_expression(codegen, left);
                        emit_formatted(codegen, ", %s, ", c_elem);
                        emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                        emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                        emit_expression(codegen, node->data.assign.value);
                        if (su) {
                            emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d), \"%s\", %d);\n", smax, sn, codegen->file, node->token.line, codegen->file, node->token.line);
                        } else {
                            emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d), \"%s\", %d);\n", smin, smax, sn, codegen->file, node->token.line, codegen->file, node->token.line);
                        }
                        return;
                    }
                }
            }
            /* Plain assignment of a string to a string array element inside a
             * loop: the RHS (a concat, a call return, ...) may live in the
             * per-iteration arena, which is torn down before the array is read
             * again. Deep-copy into the outer arena — the same escape the
             * plain-variable and += paths use. */
            if (!is_compound && strcmp(c_elem, "GrayString") == 0 &&
                codegen->loop_scope_depth > 0) {
                emit(codegen, "{ GrayString _esc_v = ");
                emit_expression(codegen, node->data.assign.value);
                emit(codegen, "; GRAY_ARRAY_SET_AT(");
                emit_expression(codegen, left);
                emit(codegen, ", GrayString, ");
                emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                emit_formatted(codegen, ", gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len), \"%s\", %d); }\n",
                    codegen->file, node->token.line);
                return;
            }
            emit_formatted(codegen, "GRAY_ARRAY_SET_AT(");
            emit_expression(codegen, left);
            emit_formatted(codegen, ", %s, ", c_elem);
            emit_expression(codegen, node->data.assign.target->data.index_expr.index);
            emit(codegen, ", ");
            /* Non-sized compound assignment on array element: read-modify-write */
            if (is_compound && strcmp(c_elem, "GrayString") == 0 && aop == TOK_PLUS_ASSIGN) {
                emit_formatted(codegen, "gray_string_concat(%s, GRAY_ARRAY_GET_AT(",
                    codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena");
                emit_expression(codegen, left);
                emit(codegen, ", GrayString, ");
                emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                emit_expression(codegen, node->data.assign.value);
                emit(codegen, ")");
            } else if (is_compound) {
                const char *binop = "+";
                if (aop == TOK_MINUS_ASSIGN) binop = "-";
                else if (aop == TOK_ASTERISK_ASSIGN) binop = "*";
                emit_formatted(codegen, "GRAY_ARRAY_GET_AT(");
                emit_expression(codegen, left);
                emit_formatted(codegen, ", %s, ", c_elem);
                emit_expression(codegen, node->data.assign.target->data.index_expr.index);
                emit_formatted(codegen, ", \"%s\", %d) %s (", codegen->file, node->token.line, binop);
                emit_expression(codegen, node->data.assign.value);
                emit(codegen, ")");
            } else {
                emit_expression(codegen, node->data.assign.value);
            }
            emit_formatted(codegen, ", \"%s\", %d);\n", codegen->file, node->token.line);
            return;
        }
        if (left_t && left_t->kind == TK_MAP) {
            /* Map key assignment: gray_map_set(arena, &m, &key, &value)
             * We need &m (address of the map), but the map expression may
             * be an rvalue (e.g. pointer-deref field access via GCC statement
             * expression). Check whether the map lives behind a pointer and
             * use arrow syntax to get an assignable target if so, otherwise emit
             * directly. */
            const char *c_val = "int64_t";
            if (left_t->value_type) c_val = gray_map_element_c_type(codegen, left_t->value_type);
            const char *c_key = "GrayString";
            if (left_t->key_type) c_key = gray_map_element_c_type(codegen, left_t->key_type);
            const char *ms_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
            bool ms_str_key = left_t->key_type && strcmp(left_t->key_type, "string") == 0;
            bool ms_str_val = left_t->value_type && strcmp(left_t->value_type, "string") == 0;

            /* Detect pointer-to-struct field access: left is a MEMBER_EXPR
             * whose object is a pointer type (`p.field`) or an explicit
             * dereference of one (`p^.field`). In that case the GCC statement
             * expression for nil-checked deref yields an rvalue and &(rvalue)
             * is illegal. Instead, nil-check then use -> to get an assignable
             * target. map_ptr_obj is the pointer to check and arrow through,
             * which for the `p^.field` spelling is the operand of the `^`. */
            bool map_via_ptr = false;
            bool map_raw = false;
            AstNode *map_ptr_obj = NULL;
            if (left->kind == NODE_MEMBER_EXPR) {
                AstNode *obj = left->data.member.object;
                GrayType *obj_t = codegen->type_table ? typetable_get(codegen->type_table, obj) : NULL;
                if (obj_t && obj_t->kind == TK_POINTER) {
                    map_ptr_obj = obj;
                } else if (obj->kind == NODE_POSTFIX_EXPR && obj->data.postfix.op == TOK_CARET) {
                    map_ptr_obj = obj->data.postfix.left;
                }
                if (map_ptr_obj) {
                    map_via_ptr = true;
                    map_raw = (map_ptr_obj->kind == NODE_LABEL &&
                        is_raw_variable(codegen, map_ptr_obj->data.label.value));
                }
            }
            /* p^["key"] = v: direct dereference of map pointer. The pointer
             * is already a GrayMap*, so nil-check and pass it directly. */
            bool map_direct_deref = false;
            bool map_deref_raw = false;
            if (!map_via_ptr && left->kind == NODE_POSTFIX_EXPR && left->data.postfix.op == TOK_CARET) {
                map_direct_deref = true;
                map_deref_raw = (left->data.postfix.left->kind == NODE_LABEL &&
                    is_raw_variable(codegen, left->data.postfix.left->data.label.value));
            }

            bool ms_compound = (node->data.assign.op != TOK_ASSIGN);
            const char *ms_base_op = NULL;
            if (ms_compound) {
                switch (node->data.assign.op) {
                    case TOK_PLUS_ASSIGN:     ms_base_op = "+"; break;
                    case TOK_MINUS_ASSIGN:    ms_base_op = "-"; break;
                    case TOK_ASTERISK_ASSIGN: ms_base_op = "*"; break;
                    case TOK_SLASH_ASSIGN:    ms_base_op = "/"; break;
                    case TOK_PERCENT_ASSIGN:  ms_base_op = "%"; break;
                    default: ms_compound = false; break;
                }
            }
            emit_formatted(codegen, "{ %s _mk = ", c_key);
            emit_map_slot_value(codegen, left_t->key_type, node->data.assign.target->data.index_expr.index);
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                if (ms_str_key) {
                    emit_formatted(codegen, "_mk = gray_string_new(%s, _mk.data, _mk.len); ", ms_arena);
                } else if (left_t->key_type && type_needs_deep_copy(codegen, left_t->key_type)) {
                    emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _mk = ", ms_arena);
                    emit_value_deep_copy(codegen, left_t->key_type, "_mk");
                    emit(codegen, "; gray_default_arena = _esc; } ");
                }
            }
            /* For compound assignments, read the existing value first so the
             * operation is applied on top of the current entry rather than
             * against a zero/uninitialized base. */
            if (ms_compound) {
                if (map_via_ptr) {
                    /* Capture _mp early so _cur can reference the map field. */
                    emit_formatted(codegen, "__auto_type _mp = ");
                    emit_expression(codegen, map_ptr_obj);
                    if (map_raw) {
                        emit_formatted(codegen, "; void *_cur = gray_map_get(&_mp->%s, &_mk); "
                              "if (!_cur) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ",
                              sanitize_name(left->data.member.member),
                              codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, "; if (!_mp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                              "void *_cur = gray_map_get(&_mp->%s, &_mk); "
                              "if (!_cur) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ",
                              codegen->file, node->token.line, sanitize_name(left->data.member.member),
                              codegen->file, node->token.line);
                    }
                } else if (map_direct_deref) {
                    emit_formatted(codegen, "__auto_type _mp = ");
                    emit_expression(codegen, left->data.postfix.left);
                    if (map_deref_raw) {
                        emit_formatted(codegen, "; void *_cur = gray_map_get(_mp, &_mk); "
                              "if (!_cur) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ",
                              codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, "; if (!_mp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                              "void *_cur = gray_map_get(_mp, &_mk); "
                              "if (!_cur) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ",
                              codegen->file, node->token.line,
                              codegen->file, node->token.line);
                    }
                } else {
                    emit_formatted(codegen, "void *_cur = gray_map_get(&");
                    emit_expression(codegen, left);
                    emit_formatted(codegen, ", &_mk); if (!_cur) { gray_panic_code_at(\"%s\", %d, \"P0081\", \"key not found in map\"); } ", codegen->file, node->token.line);
                }
            }
            const char *ms_bi_val = (left_t->value_type && is_bigint_type(left_t->value_type))
                ? left_t->value_type : NULL;
            emit_formatted(codegen, "%s _mv = ", c_val);
            if (ms_compound && ms_bi_val) {
                /* Wide-integer entries have no C arithmetic operators; route the
                 * read-modify-write through the same helpers the infix path uses. */
                const char *pfx = bigint_prefix(ms_bi_val);
                const char *fn = ms_base_op[0] == '+' ? "add_checked"
                              : ms_base_op[0] == '-' ? "sub_checked"
                              : ms_base_op[0] == '*' ? "mul_checked"
                              : ms_base_op[0] == '/' ? "div" : "mod";
                emit_formatted(codegen, "%s_%s(*(%s*)_cur, ", pfx, fn, c_val);
                emit_map_slot_value(codegen, ms_bi_val, node->data.assign.value);
                emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
            } else if (ms_compound && ms_str_val && node->data.assign.op == TOK_PLUS_ASSIGN) {
                emit(codegen, "gray_string_concat(gray_default_arena, *(GrayString*)_cur, ");
                emit_expression(codegen, node->data.assign.value);
                emit(codegen, ")");
            } else if (ms_compound) {
                emit_formatted(codegen, "*(%s*)_cur %s (", c_val, ms_base_op);
                emit_expression(codegen, node->data.assign.value);
                emit(codegen, ")");
            } else {
                emit_map_slot_value(codegen, left_t->value_type, node->data.assign.value);
            }
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                if (ms_str_val) {
                    emit_formatted(codegen, "_mv = gray_string_new(%s, _mv.data, _mv.len); ", ms_arena);
                } else if (left_t->value_type && type_needs_deep_copy(codegen, left_t->value_type)) {
                    emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _mv = ", ms_arena);
                    emit_value_deep_copy(codegen, left_t->value_type, "_mv");
                    emit(codegen, "; gray_default_arena = _esc; } ");
                }
            }
            if (map_via_ptr) {
                if (ms_compound) {
                    /* _mp was captured above; just set and close the outer block. */
                    emit_formatted(codegen, "gray_map_set(%s, &_mp->%s, &_mk, &_mv, \"%s\", %d); }\n",
                        ms_arena, sanitize_name(left->data.member.member), codegen->file, node->token.line);
                } else {
                    /* Nil-check the pointer, then use -> to yield an assignable target. */
                    emit_formatted(codegen, "{ __auto_type _mp = ");
                    emit_expression(codegen, map_ptr_obj);
                    if (map_raw) {
                        emit_formatted(codegen, "; gray_map_set(%s, &_mp->%s, &_mk, &_mv, \"%s\", %d); } }\n",
                            ms_arena, sanitize_name(left->data.member.member), codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, "; if (!_mp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                            "gray_map_set(%s, &_mp->%s, &_mk, &_mv, \"%s\", %d); } }\n",
                            codegen->file, node->token.line, ms_arena, sanitize_name(left->data.member.member), codegen->file, node->token.line);
                    }
                }
            } else if (map_direct_deref) {
                if (ms_compound) {
                    /* _mp was captured above; pass it directly as GrayMap*. */
                    emit_formatted(codegen, "gray_map_set(%s, _mp, &_mk, &_mv, \"%s\", %d); }\n",
                        ms_arena, codegen->file, node->token.line);
                } else {
                    emit_formatted(codegen, "{ __auto_type _mp = ");
                    emit_expression(codegen, left->data.postfix.left);
                    if (map_deref_raw) {
                        emit_formatted(codegen, "; gray_map_set(%s, _mp, &_mk, &_mv, \"%s\", %d); } }\n",
                            ms_arena, codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, "; if (!_mp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } "
                            "gray_map_set(%s, _mp, &_mk, &_mv, \"%s\", %d); } }\n",
                            codegen->file, node->token.line, ms_arena, codegen->file, node->token.line);
                    }
                }
            } else {
                emit_formatted(codegen, "gray_map_set(%s, &", ms_arena);
                emit_expression(codegen, left);
                emit_formatted(codegen, ", &_mk, &_mv, \"%s\", %d); }\n", codegen->file, node->token.line);
            }
            return;
        }
    }

    /* Pointer dereference assignment: p^ = value → nil check + *p = value */
    if (node->data.assign.target->kind == NODE_POSTFIX_EXPR &&
        node->data.assign.target->data.postfix.op == TOK_CARET) {
        AstNode *ptr_node = node->data.assign.target->data.postfix.left;
        GrayType *ptr_t = codegen->type_table ? typetable_get(codegen->type_table, ptr_node) : NULL;
        const char *bi_elem = (ptr_t && ptr_t->kind == TK_POINTER && ptr_t->element_type &&
                               is_bigint_type(ptr_t->element_type))
                              ? ptr_t->element_type : NULL;
        bool _deref_raw = (ptr_node->kind == NODE_LABEL && is_raw_variable(codegen, ptr_node->data.label.value));
        emit(codegen, "{ __auto_type _dp = ");
        emit_expression(codegen, ptr_node);
        if (!_deref_raw) {
            emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
        } else {
            emit(codegen, "; ");
        }
        if (!bi_elem && emit_checked_ptr_compound(codegen, node, "*_dp")) {
            emit(codegen, "; }\n");
            return;
        }
        emit(codegen, "*_dp");
        emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.op));
        if (bi_elem) {
            emit_bigint_operand(codegen, node->data.assign.value,
                                bigint_prefix(bi_elem), bi_elem, NULL);
        } else {
            emit_expression(codegen, node->data.assign.value);
        }
        emit(codegen, "; }\n");
        return;
    }
    /* Pointer deref field assignment: p^.field = value → nil check + p->field = value */
    if (node->data.assign.target->kind == NODE_MEMBER_EXPR &&
        node->data.assign.target->data.member.object->kind == NODE_POSTFIX_EXPR &&
        node->data.assign.target->data.member.object->data.postfix.op == TOK_CARET) {
        AstNode *ptr = node->data.assign.target->data.member.object->data.postfix.left;
        const char *field = node->data.assign.target->data.member.member;
        bool _fld_raw = (ptr->kind == NODE_LABEL && is_raw_variable(codegen, ptr->data.label.value));
        emit(codegen, "{ __auto_type _dp = ");
        emit_expression(codegen, ptr);
        if (_fld_raw) {
            emit(codegen, "; ");
        } else {
            emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
        }
        char _ref2[MSG_BUF_SIZE];
        snprintf(_ref2, sizeof(_ref2), "_dp->%s", field);
        if (emit_checked_ptr_compound(codegen, node, _ref2)) {
            emit(codegen, "; }\n");
            return;
        }
        if (node->data.assign.op == TOK_ASSIGN && ptr->kind == NODE_LABEL &&
            is_heap_variable(codegen, ptr->data.label.value)) {
            GrayType *field_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
            if (field_type_needs_arena_escape(field_t)) {
                emit_heap_escaped_field_assign(codegen, node, _ref2);
                emit(codegen, "; }\n");
                return;
            }
        }
        emit(codegen, _ref2);
        emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.op));
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, "; }\n");
        return;
    }
    /* Nested pointer field assignment: o.inner.val = value (where some ancestor is ptr<T>)
     * Walk the member chain to find the pointer root, then emit nil-check + chain. */
    if (node->data.assign.target->kind == NODE_MEMBER_EXPR) {
        const char *chain[MAX_MEMBER_CHAIN];
        int depth = 0;
        AstNode *cur = node->data.assign.target;
        AstNode *ptr_root = NULL;
        while (cur->kind == NODE_MEMBER_EXPR && depth < 32) {
            chain[depth++] = cur->data.member.member;
            AstNode *obj = cur->data.member.object;
            GrayType *obj_t = codegen->type_table ? typetable_get(codegen->type_table, obj) : NULL;
            if (obj_t && obj_t->kind == TK_POINTER &&
                !(obj->kind == NODE_LABEL && is_reference_variable(codegen, obj->data.label.value))) {
                ptr_root = obj;
                break;
            }
            cur = obj;
        }
        if (ptr_root && depth > 1) {
            bool _nest_raw = (ptr_root->kind == NODE_LABEL && is_raw_variable(codegen, ptr_root->data.label.value));
            emit(codegen, "{ __auto_type _dp = ");
            emit_expression(codegen, ptr_root);
            if (_nest_raw) {
                emit(codegen, "; ");
            } else {
                emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
            }
            /* Build the field reference string for the chain */
            char _ref3[MSG_BUF_SIZE];
            int _pos = snprintf(_ref3, sizeof(_ref3), "_dp->");
            for (int i = depth - 1; i >= 0 && _pos < (int)sizeof(_ref3); i--) {
                _pos += snprintf(_ref3 + _pos, sizeof(_ref3) - _pos, "%s%s",
                                 sanitize_name(chain[i]), i > 0 ? "." : "");
            }
            if (emit_checked_ptr_compound(codegen, node, _ref3)) {
                emit(codegen, "; }\n");
                return;
            }
            emit(codegen, _ref3);
            emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.op));
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; }\n");
            return;
        }
    }
    /* Pointer field assignment: p.field = value (where p is ptr<T>) → nil check + p->field = value */
    if (node->data.assign.target->kind == NODE_MEMBER_EXPR) {
        AstNode *obj = node->data.assign.target->data.member.object;
        GrayType *obj_t = codegen->type_table ? typetable_get(codegen->type_table, obj) : NULL;
        bool is_ref = (obj->kind == NODE_LABEL && is_reference_variable(codegen, obj->data.label.value));
        bool _pf_raw = (obj->kind == NODE_LABEL && is_raw_variable(codegen, obj->data.label.value));
        if (!is_ref && obj_t && obj_t->kind == TK_POINTER) {
            const char *field = node->data.assign.target->data.member.member;
            /* p was assigned from new(): its pointee lives in gray_heap_arena,
             * so a container field written through it must be allocated there
             * too, not in the current function's own scoped arena. */
            if (node->data.assign.op == TOK_ASSIGN && obj->kind == NODE_LABEL &&
                is_heap_variable(codegen, obj->data.label.value)) {
                GrayType *field_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
                if (field_type_needs_arena_escape(field_t)) {
                    emit(codegen, "{ __auto_type _dp = ");
                    emit_expression(codegen, obj);
                    if (_pf_raw) {
                        emit(codegen, "; ");
                    } else {
                        emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
                    }
                    char _ref_h[MSG_BUF_SIZE];
                    snprintf(_ref_h, sizeof(_ref_h), "_dp->%s", sanitize_name(field));
                    emit_heap_escaped_field_assign(codegen, node, _ref_h);
                    emit(codegen, "; }\n");
                    return;
                }
            }
            /* When assigning an array/string to a struct field inside a
             * scoped block (if/loop), deep-copy to the outer arena so the
             * data survives the block's arena destruction. */
            if (node->data.assign.op == TOK_ASSIGN && codegen->loop_scope_depth > 0) {
                GrayType *field_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
                if (field_t && field_t->kind == TK_ARRAY) {
                    char type_str[MSG_BUF_SIZE];
                    snprintf(type_str, sizeof(type_str), "[%s]", field_t->element_type ? field_t->element_type : "");
                    emit(codegen, "{ __auto_type _dp = ");
                    emit_expression(codegen, obj);
                    if (_pf_raw) {
                        emit(codegen, "; ");
                    } else {
                        emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
                    }
                    emit_formatted(codegen, "{ GrayArray _esc_v = ");
                    emit_expression(codegen, node->data.assign.value);
                    emit(codegen, "; GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
                    emit_formatted(codegen, "_dp->%s = ", sanitize_name(field));
                    emit_array_deep_copy(codegen, type_str, "_esc_v");
                    emit(codegen, "; gray_default_arena = _esc_a; } }\n");
                    return;
                }
                if (field_t && field_t->kind == TK_STRING) {
                    emit(codegen, "{ __auto_type _dp = ");
                    emit_expression(codegen, obj);
                    if (_pf_raw) {
                        emit(codegen, "; ");
                    } else {
                        emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
                    }
                    emit_formatted(codegen, "{ GrayString _esc_v = ");
                    emit_expression(codegen, node->data.assign.value);
                    emit_formatted(codegen, "; _dp->%s = gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len); } }\n",
                        sanitize_name(field));
                    return;
                }
            }
            emit(codegen, "{ __auto_type _dp = ");
            emit_expression(codegen, obj);
            if (_pf_raw) {
                emit(codegen, "; ");
            } else {
                emit_formatted(codegen, "; if (!_dp) { gray_panic_code_at(\"%s\", %d, \"P0080\", \"nil pointer dereference\"); } ", codegen->file, node->token.line);
            }
            char _ref4[MSG_BUF_SIZE];
            snprintf(_ref4, sizeof(_ref4), "_dp->%s", sanitize_name(field));
            if (emit_checked_ptr_compound(codegen, node, _ref4)) {
                emit(codegen, "; }\n");
                return;
            }
            emit(codegen, _ref4);
            emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.op));
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; }\n");
            return;
        }
    }

    /* String append: s += t → s = gray_string_concat(arena, s, t).
     * Take the target's address once so a member/index target is not
     * re-evaluated. Inside a loop scope, build on the outer arena so the
     * result survives the iteration arena's destruction (mirrors the
     * plain '=' string escape below). */
    if (node->data.assign.op == TOK_PLUS_ASSIGN) {
        GrayType *tgt_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
        if (tgt_t && tgt_t->kind == TK_STRING) {
            const char *arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
            emit(codegen, "{ GrayString *_tgt = &(");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, "); GrayString _sv = ");
            emit_expression(codegen, node->data.assign.value);
            emit_formatted(codegen, "; *_tgt = gray_string_concat(%s, *_tgt, _sv); }\n", arena);
            return;
        }
    }

    /* Compound assignment overflow checks. The direct form (x = x OP y)
     * routes through the checked helpers; the compound form must do the
     * same so a OP= b never wraps where a = a OP b would panic. */
    {
        TokenType aop = node->data.assign.op;
        bool is_arith_compound = (aop == TOK_PLUS_ASSIGN || aop == TOK_MINUS_ASSIGN || aop == TOK_ASTERISK_ASSIGN);
        bool is_div_compound = (aop == TOK_SLASH_ASSIGN || aop == TOK_PERCENT_ASSIGN);
        if (is_arith_compound || is_div_compound) {
            GrayType *tgt_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
            const char *sn = (tgt_t && tgt_t->name) ? tgt_t->name : NULL;
            /* Wide integer compound assignment: emit bigint op functions */
            const char *tgt_bi = sn && is_bigint_type(sn) ? sn
                : resolve_bigint_type(codegen, node->data.assign.target);
            if (tgt_bi) {
                const char *pfx = bigint_prefix(tgt_bi);
                if (is_arith_compound) {
                    const char *fn_op = NULL;
                    if (aop == TOK_PLUS_ASSIGN) fn_op = "add";
                    else if (aop == TOK_MINUS_ASSIGN) fn_op = "sub";
                    else if (aop == TOK_ASTERISK_ASSIGN) fn_op = "mul";
                    if (fn_op) {
                        emit_expression(codegen, node->data.assign.target);
                        emit_formatted(codegen, " = %s_%s_checked(", pfx, fn_op);
                        emit_expression(codegen, node->data.assign.target);
                        emit(codegen, ", ");
                        EMIT_BIGINT_OPERAND(codegen, node->data.assign.value, pfx, tgt_bi, NULL);
                        emit_formatted(codegen, ", \"%s\", %d);\n", codegen->file, node->token.line);
                        return;
                    }
                }
                if (is_div_compound) {
                    const char *fn_op = (aop == TOK_SLASH_ASSIGN) ? "div" : "mod";
                    emit_expression(codegen, node->data.assign.target);
                    emit_formatted(codegen, " = %s_%s(", pfx, fn_op);
                    emit_expression(codegen, node->data.assign.target);
                    emit(codegen, ", ");
                    EMIT_BIGINT_OPERAND(codegen, node->data.assign.value, pfx, tgt_bi, NULL);
                    emit_formatted(codegen, ", \"%s\", %d);\n", codegen->file, node->token.line);
                    return;
                }
            }
            bool tgt_is_int = (tgt_t && (tgt_t->kind == TK_INT || tgt_t->kind == TK_UINT || tgt_t->kind == TK_BYTE));
            const char *smin = NULL, *smax = NULL;
            bool su = false;
            if (sn) sized_int_bounds(sn, &smin, &smax, &su);
            /* Sized arith: gray_(u)sized_*_check */
            if (is_arith_compound && smax) {
                const char *function_name = sized_check_func(aop, su);
                if (function_name) {
                    emit_formatted(codegen, "{ %s *_tgt = &(", gray_type_to_c_codegen(codegen, sn));
                    emit_expression(codegen, node->data.assign.target);
                    emit_formatted(codegen, "); *_tgt = %s(*_tgt, ", function_name);
                    emit_expression(codegen, node->data.assign.value);
                    if (su) {
                        emit_formatted(codegen, ", %s, \"%s\", \"%s\", %d); }\n", smax, sn, codegen->file, node->token.line);
                    } else {
                        emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d); }\n", smin, smax, sn, codegen->file, node->token.line);
                    }
                    return;
                }
            }
            /* Plain int/uint (i64/u64) arith: gray_(u)*_check */
            if (is_arith_compound && tgt_is_int && !smax) {
                bool unsigned_op = (tgt_t->kind == TK_UINT || tgt_t->kind == TK_BYTE);
                const char *function_name = NULL;
                if (unsigned_op) {
                    if (aop == TOK_PLUS_ASSIGN) function_name = "gray_uadd_check";
                    else if (aop == TOK_MINUS_ASSIGN) function_name = "gray_usub_check";
                    else if (aop == TOK_ASTERISK_ASSIGN) function_name = "gray_umul_check";
                } else {
                    if (aop == TOK_PLUS_ASSIGN) function_name = "gray_add_check";
                    else if (aop == TOK_MINUS_ASSIGN) function_name = "gray_sub_check";
                    else if (aop == TOK_ASTERISK_ASSIGN) function_name = "gray_mul_check";
                }
                if (function_name) {
                    const char *c_ty = unsigned_op ? "uint64_t" : "int64_t";
                    emit_formatted(codegen, "{ %s *_tgt = &(", c_ty);
                    emit_expression(codegen, node->data.assign.target);
                    emit_formatted(codegen, "); *_tgt = %s(*_tgt, ", function_name);
                    emit_expression(codegen, node->data.assign.value);
                    emit_formatted(codegen, ", \"%s\", %d); }\n", codegen->file, node->token.line);
                    return;
                }
            }
            /* /= and %=: divide-by-zero check + (signed only) TYPE_MIN/-1
             * overflow check, mirroring the direct-form codegen. */
            if (is_div_compound && tgt_is_int) {
                bool unsigned_op = (tgt_t->kind == TK_UINT || tgt_t->kind == TK_BYTE);
                const char *signed_min = NULL;
                if (!unsigned_op) {
                    const char *_unused_max; bool _unused_u;
                    if (!sn || !sized_int_bounds(sn, &signed_min, &_unused_max, &_unused_u))
                        signed_min = "(-9223372036854775807LL - 1)";
                }
                const char *opname = (aop == TOK_SLASH_ASSIGN) ? "division" : "modulo";
                const char *binop = (aop == TOK_SLASH_ASSIGN) ? "/" : "%";
                emit(codegen, "{ __auto_type _tgt_ref = &(");
                emit_expression(codegen, node->data.assign.target);
                emit(codegen, "); __auto_type _dv = ");
                emit_expression(codegen, node->data.assign.value);
                emit_formatted(codegen, "; if (!_dv) { gray_panic_code_at(\"%s\", %d, \"P0078\", \"division by zero\"); } ", codegen->file, node->token.line);
                if (!unsigned_op) {
                    emit_formatted(codegen, "if ((int64_t)*_tgt_ref == %s && _dv == -1) { gray_panic_code_at(\"%s\", %d, \"P0079\", \"%s result is too large; value exceeds the range of this type\"); } ",
                        signed_min, codegen->file, node->token.line, opname);
                }
                emit_formatted(codegen, "*_tgt_ref %s= _dv; }\n", binop);
                return;
            }
        }
    }

    /* Array copy-by-default: arr2 = arr1 deep-copies the array so nested
     * inner arrays get independent backing storage. Applies to any RHS
     * expression (variables, call results, etc.) to prevent use-after-free
     * when the source array lives on a function-local arena.
     * Inside a scoped arena (if-block / for_each), allocate the copy on
     * the outer arena: the target variable outlives the block, so a copy
     * made in the block's arena dangles once that arena is destroyed. */
    if (node->data.assign.op == TOK_ASSIGN) {
        GrayType *tgt_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
        if (tgt_t && tgt_t->kind == TK_ARRAY) {
            int tag = codegen_next_id(codegen);
            char src_var[VAR_NAME_BUF];
            snprintf(src_var, sizeof(src_var), "_dtop%d", tag);
            char full_tn[MSG_BUF_SIZE];
            snprintf(full_tn, sizeof(full_tn), "[%s]", tgt_t->element_type ? tgt_t->element_type : "");
            emit(codegen, "{ GrayArray ");
            emit_formatted(codegen, "%s = ", src_var);
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            }
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, full_tn, src_var);
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "; gray_default_arena = _esc_a; }\n");
            } else {
                emit(codegen, "; }\n");
            }
            return;
        }
        /* Map copy-by-default: map2 = map1 deep-copies the map.
         * Inside a scoped arena (if-block / loop body), allocate the copy on
         * the outer arena: the target variable outlives the block, so a copy
         * made in the block's arena dangles once that arena is destroyed. */
        if (tgt_t && tgt_t->kind == TK_MAP) {
            int tag = codegen_next_id(codegen);
            char src_var[VAR_NAME_BUF];
            snprintf(src_var, sizeof(src_var), "_ma%d", tag);
            emit(codegen, "{ GrayMap ");
            emit_formatted(codegen, "%s = ", src_var);
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "GrayArena *_esc_m = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            }
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, tgt_t->name, src_var);
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "; gray_default_arena = _esc_m; }\n");
            } else {
                emit(codegen, "; }\n");
            }
            return;
        }
        /* Struct copy-by-default: deep copy structs with container fields.
         * When inside a scoped arena (if-block / for_each), allocate on
         * the outer arena so the copy survives scope destruction. */
        if (tgt_t && tgt_t->kind == TK_STRUCT && tgt_t->name &&
            type_needs_deep_copy(codegen, tgt_t->name)) {
            int tag = codegen_next_id(codegen);
            const char *ct = gray_type_to_c_codegen(codegen, tgt_t->name);
            char src_var[VAR_NAME_BUF];
            snprintf(src_var, sizeof(src_var), "_sa%d", tag);
            emit_formatted(codegen, "{ %s %s = ", ct, src_var);
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            }
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, tgt_t->name, src_var);
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "; gray_default_arena = _esc_a; }\n");
            } else {
                emit(codegen, "; }\n");
            }
            return;
        }
    }

    /* Default assignment; suppress ref auto-deref when assigning to a pointer target */

    /* Plain '=' of a string to a struct field (arr[i].field, p.field, ...)
     * inside a loop: the RHS — a concat, an interpolation, a call return —
     * may live in the per-iteration arena, which is destroyed before the
     * field is read again. Take the field's address once, then deep-copy the
     * value into the outer arena (mirrors the plain-variable escape below and
     * the += path above). Pointer-object fields already returned earlier. */
    if (codegen->loop_scope_depth > 0 && node->data.assign.op == TOK_ASSIGN &&
        node->data.assign.target->kind == NODE_MEMBER_EXPR) {
        GrayType *tgt_t = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
        if (tgt_t && tgt_t->kind == TK_STRING) {
            emit(codegen, "{ GrayString *_tgt = &(");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, "); GrayString _esc_v = ");
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; *_tgt = gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len); }\n");
            return;
        }
    }

    /* : when inside a loop scope and assigning a string/container
     * value to a plain variable with =, escape the value to the outer
     * arena so it survives the iteration arena's destruction. */
    if (codegen->loop_scope_depth > 0 && node->data.assign.op == TOK_ASSIGN &&
        node->data.assign.target->kind == NODE_LABEL) {
        GrayType *tgt_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
        if (tgt_t && tgt_t->kind == TK_STRING) {
            emit(codegen, "{ GrayString _esc_v = ");
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; ");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len); }\n");
            return;
        }
        if (tgt_t && tgt_t->kind == TK_STRUCT && tgt_t->name &&
            type_needs_deep_copy(codegen, tgt_t->name)) {
            const char *c_type = gray_type_to_c_codegen(codegen, tgt_t->name);
            emit_formatted(codegen, "{ %s _esc_v = ", c_type);
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, tgt_t->name, "_esc_v");
            emit(codegen, "; gray_default_arena = _esc_a; }\n");
            return;
        }
    }

    emit_expression(codegen, node->data.assign.target);
    emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.op));
    /* A plain scalar / integer literal (of any width) assigned to a bigint
     * target must be wrapped with the matching constructor. */
    const char *assign_bi = NULL;
    if (node->data.assign.op == TOK_ASSIGN) {
        GrayType *tgt_bt = codegen->type_table
            ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
        if (tgt_bt && tgt_bt->name && is_bigint_type(tgt_bt->name))
            assign_bi = tgt_bt->name;
    }
    if (assign_bi && emit_bigint_coerced(codegen, assign_bi, node->data.assign.value)) {
        /* emitted */
    } else if (node->data.assign.value->kind == NODE_LABEL &&
        is_reference_variable(codegen, node->data.assign.value->data.label.value)) {
        GrayType *tgt_t = codegen->type_table ? typetable_get(codegen->type_table, node->data.assign.target) : NULL;
        if (tgt_t && tgt_t->kind == TK_POINTER) {
            emit(codegen, node->data.assign.value->data.label.value);
        } else {
            emit_expression(codegen, node->data.assign.value);
        }
    } else {
        emit_expression(codegen, node->data.assign.value);
    }
    emit(codegen, ";\n");
}

/* Collect ensure statements from a block, growing the buffer as needed. */
static void collect_ensures(AstNode *block, AstNode ***ensures, int *count, int *cap) {
    if (!block || block->kind != NODE_BLOCK_STMT) return;
    for (int i = 0; i < block->data.block.count; i++) {
        AstNode *stmt = block->data.block.stmts[i];
        if (stmt->kind != NODE_ENSURE_STMT) continue;
        if (*count == *cap) {
            int new_cap = *cap ? *cap * 2 : 8;
            *ensures = xrealloc(*ensures, sizeof(AstNode *) * (size_t)new_cap);
            *cap = new_cap;
        }
        (*ensures)[(*count)++] = stmt;
    }
}

/* Emit ensure cleanup calls in LIFO order */
static void emit_ensure_cleanup(CodeGen *codegen) {
    if (!codegen->current_func || !codegen->current_func->data.func_decl.body) return;

    AstNode **ensures = NULL;
    int ensure_count = 0;
    int ensure_cap = 0;
    collect_ensures(codegen->current_func->data.func_decl.body, &ensures, &ensure_count, &ensure_cap);

    /* Only the defer/ensure statements control flow has actually reached run at
     * this exit; a return that lexically precedes a defer must not splice it in
     * (the deferred expression may reference not-yet-declared variables, or
     * clean up a resource that was never acquired). */
    int reached = codegen->ensure_reached;
    if (reached > ensure_count) reached = ensure_count;

    /* Emit in reverse (LIFO) order */
    for (int i = reached - 1; i >= 0; i--) {
        emit_indent(codegen);
        emit_expression(codegen, ensures[i]->data.ensure_stmt.expr);
        emit(codegen, ";\n");
    }

    free(ensures);
}

/* Track nested scratch arenas so early-exit paths can unwind every live
 * one innermost-first. Without this, `return` (and the desugared
 * `or_return`) from inside a nested for_each/if/while/loop scope leaks
 * the per-scope arenas the codegen had emitted. */
static void scope_arena_push(CodeGen *codegen, const char *arena_var, const char *saved_var) {
    GROW_ARRAY(codegen->scope_arenas, codegen->scope_arena_count, codegen->scope_arena_cap);
    ScopeArena *entry = &codegen->scope_arenas[codegen->scope_arena_count++];
    snprintf(entry->arena_var, sizeof(entry->arena_var), "%s", arena_var);
    snprintf(entry->saved_var, sizeof(entry->saved_var), "%s", saved_var);
}

static void scope_arena_pop(CodeGen *codegen) {
    if (codegen->scope_arena_count > 0) codegen->scope_arena_count--;
}

/* Track active for_each iteration guards so early-return paths can
 * decrement .iterating for every live for_each loop. */
static void iter_guard_push(CodeGen *codegen, const char *expr) {
    GROW_ARRAY(codegen->iter_guards, codegen->iter_guard_count, codegen->iter_guard_cap);
    codegen->iter_guards[codegen->iter_guard_count++] = strdup(expr);
}

/* The guard expression for the innermost live for_each, so the decrement
 * targets exactly what the increment did. */
static const char *iter_guard_top(CodeGen *codegen) {
    if (codegen->iter_guard_count == 0) return NULL;
    return codegen->iter_guards[codegen->iter_guard_count - 1];
}

static void iter_guard_pop(CodeGen *codegen) {
    if (codegen->iter_guard_count > 0) {
        free(codegen->iter_guards[--codegen->iter_guard_count]);
    }
}

static void emit_iter_guard_unwind(CodeGen *codegen) {
    for (int i = codegen->iter_guard_count - 1; i >= 0; i--) {
        emit_formatted(codegen, "gray_atomic_sub32(&%s.iterating, 1); ", codegen->iter_guards[i]);
    }
}

/* True for a field path like b.items or a.b.items — an lvalue naming the
 * caller's array, with no calls or indexing to re-evaluate. Iterating one of
 * these can guard the real array instead of the by-value snapshot. */
static bool is_stable_field_path(AstNode *node) {
    while (node && node->kind == NODE_MEMBER_EXPR) {
        node = node->data.member.object;
    }
    return node && node->kind == NODE_LABEL;
}

/* Build the C expression string for a for_each collection. For tmp
 * variables, returns the tmp name. For labels, returns the sanitized
 * name (with deref wrapper for mutable params / ref vars). */
static char *iter_guard_expr(CodeGen *codegen, bool needs_tmp,
                             const char *tmp_name, AstNode *coll) {
    if (needs_tmp) return strdup(tmp_name);
    const char *raw = coll->data.label.value;
    /* A module-level collection is emitted under its module's mangled name,
     * the same as any other reference to it. */
    const char *resolved = codegen_resolve_decl(codegen, raw);
    const char *san = sanitize_name(resolved != raw ? resolved : raw);
    char buf[128];
    if (is_mutable_parameter(codegen, raw) || is_reference_variable(codegen, raw))
        snprintf(buf, sizeof(buf), "(*%s)", san);
    else
        snprintf(buf, sizeof(buf), "%s", san);
    return strdup(buf);
}

/* Emit the cleanup sequence for every live nested scratch arena,
 * innermost-first. Used in every early-exit return path before the
 * function-arena (or scope_restore) unwind. */
static void emit_scratch_arena_unwind(CodeGen *codegen) {
    emit_iter_guard_unwind(codegen);
    for (int i = codegen->scope_arena_count - 1; i >= 0; i--) {
        ScopeArena *entry = &codegen->scope_arenas[i];
        emit_formatted(codegen, "gray_default_arena = %s; ", entry->saved_var);
        emit_formatted(codegen, "gray_arena_destroy(%s, __FILE__, __LINE__); free(%s); ",
              entry->arena_var, entry->arena_var);
    }
}

/* Unwind only up to and including the innermost loop iteration arena.
 * Used by break/continue: we must clean up the current loop's arena and
 * any if-block arenas nested inside it, but must NOT touch outer loop
 * arenas which are still live. Loop arenas are named _iter_arena_N;
 * if-block arenas are named _if_arena_N. */
static void emit_loop_exit_unwind(CodeGen *codegen) {
    for (int i = codegen->scope_arena_count - 1; i >= 0; i--) {
        ScopeArena *entry = &codegen->scope_arenas[i];
        emit_formatted(codegen, "gray_default_arena = %s; ", entry->saved_var);
        emit_formatted(codegen, "gray_arena_destroy(%s, __FILE__, __LINE__); free(%s); ",
              entry->arena_var, entry->arena_var);
        if (strncmp(entry->arena_var, "_iter_arena_", 12) == 0) break;
    }
}

/* : emit escape + cleanup for a non-void function return.
 * Escapes the return value (_ret) to _func_saved, then unwinds any
 * nested scratch arenas live at this exit point, then
 * destroys the function arena. The escape must run first because it
 * may read from memory still owned by a scratch arena. */
static void emit_function_return_escape(CodeGen *codegen, const char *ret_type_name) {
    if (!ret_type_name) return;
    /* Caller-arena functions have no private _func_arena: the return value
     * is already allocated in the caller's arena, so there is nothing to
     * escape and nothing to destroy — just unwind any nested scratch. */
    if (function_uses_caller_arena(codegen, codegen->current_func)) {
        emit_scratch_arena_unwind(codegen);
        return;
    }
    GrayType *return_graytype = type_from_name(ret_type_name);
    if (return_graytype->kind == TK_STRING) {
        emit(codegen, "_ret = gray_string_new(_func_saved, _ret.data, _ret.len); ");
    } else if (return_graytype->kind == TK_ERROR) {
        emit(codegen, "if (_ret) { GrayError *_src_err = (GrayError *)_ret; ");
        emit(codegen, "GrayError *_esc_err = (GrayError *)gray_arena_alloc(_func_saved, sizeof(GrayError)); ");
        emit(codegen, "_esc_err->code = _src_err->code; ");
        emit(codegen, "_esc_err->msg = gray_string_new(_func_saved, _src_err->msg.data, _src_err->msg.len); ");
        emit(codegen, "_ret = _esc_err; } ");
    } else if (type_needs_deep_copy(codegen, ret_type_name)) {
        emit(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = _func_saved; _ret = ");
        emit_value_deep_copy(codegen, ret_type_name, "_ret");
        emit(codegen, "; gray_default_arena = _esc; } ");
    }
    emit_scratch_arena_unwind(codegen);
    emit(codegen, "gray_default_arena = _func_saved; ");
    emit(codegen, "gray_arena_destroy(_func_arena, __FILE__, __LINE__); free(_func_arena); ");
}

/* Escape every heap field of the live GrayMulti_* _ret struct from _func_arena
 * to _func_saved, then unwind scratch arenas and destroy _func_arena.
 * Mirrors emit_function_return_escape but covers all fields of a multi-return. */
static void emit_multi_function_return_escape(CodeGen *codegen) {
    if (function_uses_caller_arena(codegen, codegen->current_func)) {
        emit_scratch_arena_unwind(codegen);
        return;
    }
    int rc = codegen->current_func->data.func_decl.return_type_count;
    for (int i = 0; i < rc; i++) {
        const char *type_str = codegen->current_func->data.func_decl.return_types[i];
        if (!type_str) continue;
        GrayType *return_graytype = type_from_name(type_str);
        if (return_graytype->kind == TK_STRING) {
            emit_formatted(codegen, "_ret.v%d = gray_string_new(_func_saved, _ret.v%d.data, _ret.v%d.len); ", i, i, i);
        } else if (return_graytype->kind == TK_ERROR) {
            emit_formatted(codegen, "if (_ret.v%d) { GrayError *_esc_err = (GrayError *)gray_arena_alloc(_func_saved, sizeof(GrayError)); ", i);
            emit_formatted(codegen, "_esc_err->code = _ret.v%d->code; ", i);
            emit_formatted(codegen, "_esc_err->msg = gray_string_new(_func_saved, _ret.v%d->msg.data, _ret.v%d->msg.len); ", i, i);
            emit_formatted(codegen, "_ret.v%d = _esc_err; } ", i);
        } else if (type_needs_deep_copy(codegen, type_str)) {
            char field[32];
            snprintf(field, sizeof(field), "_ret.v%d", i);
            emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = _func_saved; _ret.v%d = ", i);
            emit_value_deep_copy(codegen, type_str, field);
            emit(codegen, "; gray_default_arena = _esc; } ");
        }
    }
    emit_scratch_arena_unwind(codegen);
    emit(codegen, "gray_default_arena = _func_saved; ");
    emit(codegen, "gray_arena_destroy(_func_arena, __FILE__, __LINE__); free(_func_arena); ");
}

static void emit_return_statement(CodeGen *codegen, AstNode *node) {
    /* Emit ensure cleanup before return */
    emit_ensure_cleanup(codegen);

    /* Caller-arena functions have no _scope_mark to restore. */
    bool caller_arena = codegen->current_func &&
                        function_uses_caller_arena(codegen, codegen->current_func);

    /* Guard against malformed AST: count > 0 but NULL values array */
    if (node->data.return_stmt.count > 0 && !node->data.return_stmt.values) {
        emit_indent(codegen);
        emit(codegen, "{ ");
        emit_scratch_arena_unwind(codegen);
        if (caller_arena) {
            emit(codegen, "gray_exit_func(); return; }\n");
        } else {
            emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); gray_exit_func(); return; }\n");
        }
        return;
    }

    if (node->data.return_stmt.count > 1 && codegen->current_func) {
        /* Multi-return: evaluate into temp, then exit and return */
        emit_indent(codegen);
        const char *mbn = multi_return_name(codegen->current_func);
        emit_formatted(codegen, "{ GrayMulti_%s _ret = (GrayMulti_%s){", mbn, mbn);
        for (int i = 0; i < node->data.return_stmt.count; i++) {
            if (i > 0) emit(codegen, ", ");
            const char *rbi = (i < codegen->current_func->data.func_decl.return_type_count)
                ? codegen->current_func->data.func_decl.return_types[i] : NULL;
            if (!emit_bigint_coerced(codegen, rbi, node->data.return_stmt.values[i]))
                emit_expression(codegen, node->data.return_stmt.values[i]);
        }
        emit(codegen, "}; ");
        emit_multi_function_return_escape(codegen);
        emit(codegen, "gray_exit_func(); return _ret; }\n");
    } else if (node->data.return_stmt.count == 1 && codegen->current_func &&
               codegen->current_func->data.func_decl.return_type_count > 1) {
        /* Single value returned from multi-return function (or_return propagation) */
        int rc = codegen->current_func->data.func_decl.return_type_count;
        emit_indent(codegen);
        const char *mbn2 = multi_return_name(codegen->current_func);
        emit_formatted(codegen, "{ GrayMulti_%s _ret = (GrayMulti_%s){", mbn2, mbn2);
        for (int i = 0; i < rc - 1; i++) {
            /* Use {0} for composite types (structs, arrays, maps, strings)
             * and 0 for scalars to avoid -Wbraced-scalar-init. */
            const char *return_type_str = codegen->current_func->data.func_decl.return_types[i];
            bool composite = false;
            if (return_type_str) {
                GrayType *rtt = type_from_name(return_type_str);
                if (rtt && (rtt->kind == TK_STRUCT || rtt->kind == TK_ARRAY ||
                            rtt->kind == TK_MAP || rtt->kind == TK_STRING ||
                            rtt->kind == TK_ERROR))
                    composite = true;
            }
            emit(codegen, composite ? "{0}, " : "0, ");
        }
        {
            const char *rbi = codegen->current_func->data.func_decl.return_types[rc - 1];
            if (!emit_bigint_coerced(codegen, rbi, node->data.return_stmt.values[0]))
                emit_expression(codegen, node->data.return_stmt.values[0]);
        }
        emit(codegen, "}; ");
        emit_multi_function_return_escape(codegen);
        emit(codegen, "gray_exit_func(); return _ret; }\n");
    } else if (codegen->current_func &&
               codegen->current_func->data.func_decl.return_type_count == 0) {
        /* Void function */
        emit_indent(codegen);
        emit(codegen, "{ ");
        emit_scratch_arena_unwind(codegen);
        if (caller_arena) {
            emit(codegen, "gray_exit_func(); return; }\n");
        } else {
            emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); gray_exit_func(); return; }\n");
        }
    } else if (node->data.return_stmt.count == 1) {
        /* Single return value: evaluate into temp, then exit and return */
        emit_indent(codegen);
        emit(codegen, "{ __auto_type _ret = ");
        const char *ret_bi = (codegen->current_func &&
            codegen->current_func->data.func_decl.return_type_count == 1)
            ? codegen->current_func->data.func_decl.return_types[0] : NULL;
        if (!emit_bigint_coerced(codegen, ret_bi, node->data.return_stmt.values[0]))
            emit_expression(codegen, node->data.return_stmt.values[0]);
        emit(codegen, "; ");
        if (codegen->current_func && codegen->current_func->data.func_decl.return_type_count > 0) {
            const char *ret_tn = codegen->current_func->data.func_decl.return_types[0];
            emit_function_return_escape(codegen, ret_tn);
        }
        emit(codegen, "gray_exit_func(); return _ret; }\n");
    } else if (node->data.return_stmt.count == 0 && codegen->current_func &&
               codegen->current_func->data.func_decl.return_names &&
               codegen->current_func->data.func_decl.return_type_count > 0) {
        /* Bare return in function with named return values; collect named vars */
        int rc = codegen->current_func->data.func_decl.return_type_count;
        if (rc == 1 && codegen->current_func->data.func_decl.return_names[0]) {
            emit_indent(codegen);
            emit_formatted(codegen, "{ __auto_type _ret = %s; ",
                sanitize_name(codegen->current_func->data.func_decl.return_names[0]));
            emit_function_return_escape(codegen, codegen->current_func->data.func_decl.return_types[0]);
            emit(codegen, "gray_exit_func(); return _ret; }\n");
        } else {
            emit_indent(codegen);
            const char *mbn3 = multi_return_name(codegen->current_func);
            emit_formatted(codegen, "{ GrayMulti_%s _ret = (GrayMulti_%s){", mbn3, mbn3);
            for (int i = 0; i < rc; i++) {
                if (i > 0) emit(codegen, ", ");
                if (codegen->current_func->data.func_decl.return_names[i]) {
                    emit_formatted(codegen, "%s", sanitize_name(codegen->current_func->data.func_decl.return_names[i]));
                } else {
                    emit(codegen, "0");
                }
            }
            emit(codegen, "}; ");
            emit_multi_function_return_escape(codegen);
            emit(codegen, "gray_exit_func(); return _ret; }\n");
        }
    } else {
        /* Bare return (no value, non-void; shouldn't happen but handle gracefully) */
        emit_indent(codegen);
        emit(codegen, "{ ");
        emit_scratch_arena_unwind(codegen);
        if (caller_arena) {
            emit(codegen, "gray_exit_func(); return; }\n");
        } else {
            emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); gray_exit_func(); return; }\n");
        }
    }
}

static void emit_block(CodeGen *codegen, AstNode *node) {
    for (int i = 0; i < node->data.block.count; i++) {
        emit_statement(codegen, node->data.block.stmts[i]);
    }
}

static void emit_if_statement(CodeGen *codegen, AstNode *node) {
    /* : per-block arena for if/otherwise so temporaries are freed */
    int prev_raw_var_count = codegen->raw_var_count;
    int isc = codegen_next_id(codegen);
    bool scoped = !current_function_uses_caller_arena(codegen);
    emit_indent(codegen);
    emit_formatted(codegen, "{ ");
    if (scoped) {
        if (codegen->loop_scope_depth == 0) {
            emit(codegen, "GrayArena *_gray_outer_arena = gray_default_arena; ");
        }
        emit_formatted(codegen, "GrayArena *_if_arena_%d = gray_arena_create(%d); ", isc, IF_ARENA_SIZE);
        emit_formatted(codegen, "GrayArena *_if_saved_%d = gray_default_arena; ", isc);
        emit_formatted(codegen, "gray_default_arena = _if_arena_%d;\n", isc);
        codegen->loop_scope_depth++;
        char av[32], sv[32];
        snprintf(av, sizeof(av), "_if_arena_%d", isc);
        snprintf(sv, sizeof(sv), "_if_saved_%d", isc);
        scope_arena_push(codegen, av, sv);
    } else {
        emit(codegen, "\n");
    }

    emit_indent(codegen);
    emit(codegen, "if (");
    emit_expression(codegen, node->data.if_stmt.condition);
    emit(codegen, ") {\n");

    codegen->indent++;
    emit_block(codegen, node->data.if_stmt.consequence);
    codegen->indent--;

    if (node->data.if_stmt.alternative) {
        if (node->data.if_stmt.alternative->kind == NODE_IF_STMT) {
            emit_indent(codegen);
            emit(codegen, "} else if (");
            emit_expression(codegen, node->data.if_stmt.alternative->data.if_stmt.condition);
            emit(codegen, ") {\n");
            codegen->indent++;
            emit_block(codegen, node->data.if_stmt.alternative->data.if_stmt.consequence);
            codegen->indent--;
            AstNode *alt = node->data.if_stmt.alternative->data.if_stmt.alternative;
            while (alt) {
                if (alt->kind == NODE_IF_STMT) {
                    emit_indent(codegen);
                    emit(codegen, "} else if (");
                    emit_expression(codegen, alt->data.if_stmt.condition);
                    emit(codegen, ") {\n");
                    codegen->indent++;
                    emit_block(codegen, alt->data.if_stmt.consequence);
                    codegen->indent--;
                    alt = alt->data.if_stmt.alternative;
                } else {
                    emit_indent(codegen);
                    emit(codegen, "} else {\n");
                    codegen->indent++;
                    emit_block(codegen, alt);
                    codegen->indent--;
                    break;
                }
            }
            emit_indent(codegen);
            emit(codegen, "}\n");
        } else {
            emit_indent(codegen);
            emit(codegen, "} else {\n");
            codegen->indent++;
            emit_block(codegen, node->data.if_stmt.alternative);
            codegen->indent--;
            emit_indent(codegen);
            emit(codegen, "}\n");
        }
    } else {
        emit_indent(codegen);
        emit(codegen, "}\n");
    }

    codegen->raw_var_count = prev_raw_var_count;
    emit_indent(codegen);
    if (scoped) {
        codegen->loop_scope_depth--;
        scope_arena_pop(codegen);
        emit_formatted(codegen, "gray_default_arena = _if_saved_%d; ", isc);
        emit_formatted(codegen, "gray_arena_destroy(_if_arena_%d, __FILE__, __LINE__); free(_if_arena_%d); ", isc, isc);
    }
    emit(codegen, "}\n");
}

/* Emit per-iteration scratch arena setup, the loop body, and arena teardown.
 * Caller is responsible for indent++ before and indent--/closing brace after. */
static void emit_loop_body_with_arena(CodeGen *codegen, AstNode *body) {
    int prev_raw_var_count = codegen->raw_var_count;
    if (current_function_uses_caller_arena(codegen)) {
        emit_block(codegen, body);
        codegen->raw_var_count = prev_raw_var_count;
        return;
    }
    if (codegen->loop_scope_depth == 0) {
        emit_indent(codegen);
        emit(codegen, "GrayArena *_gray_outer_arena = gray_default_arena;\n");
    }
    int depth = codegen->loop_scope_depth;
    emit_indent(codegen);
    emit_formatted(codegen, "GrayArena *_iter_arena_%d = gray_arena_create(%d);\n", depth, LOOP_ARENA_SIZE);
    emit_indent(codegen);
    emit_formatted(codegen, "GrayArena *_saved_arena_%d = gray_default_arena;\n", depth);
    emit_indent(codegen);
    emit_formatted(codegen, "gray_default_arena = _iter_arena_%d;\n", depth);
    codegen->loop_scope_depth++;
    {
        char av[32], sv[32];
        snprintf(av, sizeof(av), "_iter_arena_%d", depth);
        snprintf(sv, sizeof(sv), "_saved_arena_%d", depth);
        scope_arena_push(codegen, av, sv);
    }
    emit_block(codegen, body);
    codegen->loop_scope_depth--;
    codegen->raw_var_count = prev_raw_var_count;
    scope_arena_pop(codegen);
    emit_indent(codegen);
    emit_formatted(codegen, "gray_default_arena = _saved_arena_%d;\n", depth);
    emit_indent(codegen);
    emit_formatted(codegen, "gray_arena_destroy(_iter_arena_%d, __FILE__, __LINE__); free(_iter_arena_%d);\n", depth, depth);
}

static void emit_for_statement(CodeGen *codegen, AstNode *node) {
    emit_indent(codegen);

    AstNode *iter = node->data.for_stmt.iterable;
    if (iter && iter->kind == NODE_RANGE_EXPR) {
        /* for i in range(start, end) or range(start, end, step) */
        char blank_for_buf[64];
        const char *var;
        if (strcmp(node->data.for_stmt.var_name, "_") == 0) {
            snprintf(blank_for_buf, sizeof(blank_for_buf), "_gray_for_blank_%d", codegen_next_id(codegen));
            var = blank_for_buf;
        } else {
            var = sanitize_name(node->data.for_stmt.var_name);
        }

        if (iter->data.range_expr.start) {
            /* range(start, end) or range(start, end, step) */
            /* Determine comparison direction: static for literal step, runtime ternary for variable. */
            bool neg_step = false;
            bool known_direction = false;
            bool zero_step = false;
            if (iter->data.range_expr.step) {
                AstNode *step = iter->data.range_expr.step;
                if (step->kind == NODE_INT_VALUE) {
                    known_direction = true;
                    neg_step = step->data.int_value.value < 0;
                    zero_step = (step->data.int_value.value == 0);
                } else if (step->kind == NODE_PREFIX_EXPR && step->data.prefix.op == TOK_MINUS) {
                    known_direction = true;
                    neg_step = true;
                }
            }

            if (iter->data.range_expr.step && !known_direction) {
                /* Variable step: store step and end once, emit runtime direction ternary. */
                int svc = codegen_next_id(codegen);
                /* emit_indent already called above — use it for the var declaration line */
                emit_formatted(codegen, "int64_t _gray_step_%d = ", svc);
                emit_expression(codegen, iter->data.range_expr.step);
                emit_formatted(codegen, ", _gray_end_%d = ", svc);
                emit_expression(codegen, iter->data.range_expr.end);
                emit(codegen, ";\n");
                /* P0090: zero step at runtime is always a panic */
                emit_indent(codegen);
                emit_formatted(codegen, "if (_gray_step_%d == 0) { gray_panic_code_at(\"%s\", %d, \"P0090\", \"range step cannot be zero\"); }\n", svc, codegen->file, node->token.line);
                emit_indent(codegen);
                emit_formatted(codegen, "for (int64_t %s = ", var);
                emit_expression(codegen, iter->data.range_expr.start);
                emit_formatted(codegen, "; _gray_step_%d > 0 ? %s < _gray_end_%d : %s > _gray_end_%d", svc, var, svc, var, svc);
                emit_formatted(codegen, "; %s = gray_add_check(%s, _gray_step_%d, \"%s\", %d)", var, var, svc, codegen->file, node->token.line);
            } else if (zero_step) {
                /* P0090: literal zero step always panics; emit panic then a dead loop */
                emit_formatted(codegen, "gray_panic_code_at(\"%s\", %d, \"P0090\", \"range step cannot be zero\");\n", codegen->file, node->token.line);
                emit_indent(codegen);
                emit_formatted(codegen, "for (int64_t %s = 0; 0; (void)0", var);
            } else {
                emit_formatted(codegen, "for (int64_t %s = ", var);
                emit_expression(codegen, iter->data.range_expr.start);
                emit_formatted(codegen, "; %s %s ", var, neg_step ? ">" : "<");
                emit_expression(codegen, iter->data.range_expr.end);
                emit_formatted(codegen, "; %s", var);
                if (iter->data.range_expr.step) {
                    emit_formatted(codegen, " = gray_add_check(%s, ", var);
                    emit_expression(codegen, iter->data.range_expr.step);
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                } else {
                    emit(codegen, "++");
                }
            }
        } else {
            /* range(end) - start at 0 */
            emit_formatted(codegen, "for (int64_t %s = 0; %s < ", var, var);
            emit_expression(codegen, iter->data.range_expr.end);
            emit_formatted(codegen, "; %s++", var);
        }

        emit(codegen, ") {\n");
    } else {
        codegen_internal_error("non-range for loop reached codegen",
                               codegen->file, node->token.line);
    }

    codegen->indent++;
    emit_loop_body_with_arena(codegen, node->data.for_stmt.body);
    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
}

static void emit_while_statement(CodeGen *codegen, AstNode *node) {
    emit_indent(codegen);
    emit(codegen, "while (");
    emit_expression(codegen, node->data.while_stmt.condition);
    emit(codegen, ") {\n");

    codegen->indent++;
    emit_loop_body_with_arena(codegen, node->data.while_stmt.body);
    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
}

static void emit_loop_statement(CodeGen *codegen, AstNode *node) {
    emit_indent(codegen);
    emit(codegen, "for (;;) {\n");

    codegen->indent++;
    emit_loop_body_with_arena(codegen, node->data.loop_stmt.body);
    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
}

/* : extract the base (unmangled) function name for multi-return
 * struct references. The monomorphiser temporarily renames functions
 * to `<name>__<binding>`, but the GrayMulti typedef is emitted once
 * under the original name. Returns a pointer to a small ring of
 * static buffers so a few concurrent uses stay alive. */
static const char *multi_return_base_name(const char *fn_name) {
    static char bufs[4][MSG_BUF_SIZE];
    static int buf_slot = 0;
    char *out = bufs[buf_slot]; buf_slot = (buf_slot + 1) & 3;
    const char *dunder = strstr(fn_name, "__");
    if (dunder) {
        size_t prefix_len = (size_t)(dunder - fn_name);
        if (prefix_len >= sizeof(bufs[0])) prefix_len = sizeof(bufs[0]) - 1;
        memcpy(out, fn_name, prefix_len);
        out[prefix_len] = '\0';
    } else {
        strncpy(out, fn_name, sizeof(bufs[0]) - 1);
        out[sizeof(bufs[0]) - 1] = '\0';
    }
    return out;
}

/*  + : pick the right multi-return struct name. Use
 * the full mangled name when return types contain '?'
 * (per-instantiation struct), base name otherwise (shared). */
static const char *multi_return_name(AstNode *func) {
    bool has_wc = false;
    for (int i = 0; i < func->data.func_decl.return_type_count; i++) {
        if (func->data.func_decl.return_types[i] &&
            strchr(func->data.func_decl.return_types[i], '?')) {
            has_wc = true;
            break;
        }
    }
    return has_wc ? func->data.func_decl.name
                  : multi_return_base_name(func->data.func_decl.name);
}

/* Build a multi-return type name like GrayMulti_add */
static void emit_multi_return_typedef(CodeGen *codegen, AstNode *node) {
    emit_formatted(codegen, "typedef struct {\n");
    for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
        emit_formatted(codegen, "    %s v%d;\n",
            gray_type_to_c_codegen(codegen, node->data.func_decl.return_types[i]), i);
    }
    emit_formatted(codegen, "} GrayMulti_%s;\n\n", node->data.func_decl.name);
}

static const char *function_return_type(CodeGen *codegen, AstNode *node) {
    if (node->data.func_decl.return_type_count == 0) return "void";
    if (node->data.func_decl.return_type_count == 1) {
        return gray_type_to_c_codegen(codegen, node->data.func_decl.return_types[0]);
    }
    /*  + : for multi-return, use `GrayMulti_<name>`. The
     * monomorphiser temporarily renames the func to `<name>__<binding>`.
     * When return types DON'T contain '?', all instantiations share one
     * struct → use the base name ). When return types DO contain
     * '?', each instantiation gets its own struct → use the full
     * mangled name ). */
    static char buffer[MSG_BUF_SIZE];
    const char *fn_name = node->data.func_decl.name;
    bool has_wc_ret = false;
    for (int i = 0; i < node->data.func_decl.return_type_count; i++) {
        if (node->data.func_decl.return_types[i] &&
            strchr(node->data.func_decl.return_types[i], '?')) {
            has_wc_ret = true;
            break;
        }
    }
    if (!has_wc_ret) {
        /* : strip __<binding> suffix; shared struct. */
        snprintf(buffer, sizeof(buffer), "GrayMulti_%s", multi_return_base_name(fn_name));
    } else {
        /* : use the full (possibly mangled) name; per-instantiation
         * struct. The wildcard_binding is active so gray_type_to_c_codegen will
         * substitute '?' in the struct fields. */
        snprintf(buffer, sizeof(buffer), "GrayMulti_%s", fn_name);
    }
    return buffer;
}

static void emit_function_declaration(CodeGen *codegen, AstNode *node, bool is_main) {
    codegen_enter_node(codegen, node);
    /* Return type */
    if (is_main) {
        emit(codegen, "static void gray_fn_main(void)");
    } else {
        emit_formatted(codegen, "static %s ", function_return_type(codegen, node));
        emit_formatted(codegen, "gray_fn_%s(",
            codegen_decl_name(codegen, node, node->data.func_decl.name));

        /* Parameters — skip type params (erased in C) */
        bool first_param = true;
        for (int i = 0; i < node->data.func_decl.param_count; i++) {
            Param *param = &node->data.func_decl.params[i];
            if (param->is_type_param) continue;
            if (!first_param) emit(codegen, ", ");
            first_param = false;
            if (param->mutable) {
                emit_formatted(codegen, "%s *%s", gray_type_to_c_codegen(codegen,param->type_name), sanitize_name(param->name));
            } else {
                emit_formatted(codegen, "%s %s", gray_type_to_c_codegen(codegen,param->type_name), sanitize_name(param->name));
            }
        }

        if (first_param) {
            emit(codegen, "void");
        }
        emit(codegen, ")");
    }

    emit(codegen, " {\n");
    codegen->indent++;

    /* : scope-based memory management.
     * Void functions: save/restore arena watermark to free temporaries.
     * Non-void functions: create a per-function arena so temporaries
     * are freed, and escape the return value to the caller's arena. */
    bool is_void_fn = (node->data.func_decl.return_type_count == 0);
    bool caller_arena = function_uses_caller_arena(codegen, node);
    if (!is_main && !caller_arena) {
        if (is_void_fn) {
            emit_indent(codegen);
            emit(codegen, "GrayScopeMark _scope_mark = gray_scope_save(gray_default_arena);\n");
        } else {
            emit_indent(codegen);
            emit_formatted(codegen, "GrayArena *_func_arena = gray_arena_create(%d);\n", FUNC_ARENA_SIZE);
            emit_indent(codegen);
            emit(codegen, "GrayArena *_func_saved = gray_default_arena;\n");
            emit_indent(codegen);
            emit(codegen, "gray_default_arena = _func_arena;\n");
        }
    }

    AstNode *prev_func = codegen->current_func;
    int prev_using_count = codegen->using_module_count;
    int prev_ref_var_count = codegen->ref_var_count;
    int prev_raw_var_count = codegen->raw_var_count;
    int prev_bigint_var_count = codegen->bigint_var_count;
    int prev_iter_guard_count = codegen->iter_guard_count;
    int prev_ensure_reached = codegen->ensure_reached;
    codegen->current_func = node;
    codegen->ensure_reached = 0;

    /* Register bigint parameters for type tracking */
    for (int i = 0; i < node->data.func_decl.param_count; i++) {
        Param *param = &node->data.func_decl.params[i];
        if (param->type_name && is_bigint_type(param->type_name)) {
            register_bigint_variable(codegen, param->name, param->type_name);
        }
    }

    /* Named return variables are declared by the user in the function body,
     * not auto-generated. E3080 enforces the correct variable is returned. */

    if (node->data.func_decl.body) {
        /* Stack depth guard */
        emit_indent(codegen);
        emit_formatted(codegen, "gray_enter_func(\"%s\", %d);\n", codegen->file, node->token.line);
        emit_block(codegen, node->data.func_decl.body);
        /* Emit ensure cleanup at end of function (for implicit returns) */
        emit_ensure_cleanup(codegen);
        /* : cleanup function-scoped memory */
        if (!is_main && !caller_arena) {
            if (is_void_fn) {
                emit_indent(codegen);
                emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark);\n");
            } else {
                emit_indent(codegen);
                emit(codegen, "gray_default_arena = _func_saved;\n");
                emit_indent(codegen);
                emit(codegen, "gray_arena_destroy(_func_arena, __FILE__, __LINE__); free(_func_arena);\n");
            }
        }
        emit_indent(codegen);
        emit(codegen, "gray_exit_func();\n");

        /* Named return variables: E3080 enforces the user must explicitly
         * return the named variable, so no implicit fall-through is needed. */
    }
    codegen->current_func = prev_func;
    codegen->ensure_reached = prev_ensure_reached;
    codegen->using_module_count = prev_using_count;
    codegen->ref_var_count = prev_ref_var_count;
    codegen->raw_var_count = prev_raw_var_count;
    codegen->bigint_var_count = prev_bigint_var_count;
    codegen->iter_guard_count = prev_iter_guard_count;
    codegen->indent--;
    emit(codegen, "}\n\n");
}

static void emit_expression_statement(CodeGen *codegen, AstNode *node) {
    emit_indent(codegen);
    emit_expression(codegen, node->data.expr_stmt.expr);
    emit(codegen, ";\n");
}

/* ── for_each helpers ──────────────────────────────────────────────── */

static void emit_foreach_map(CodeGen *codegen, AstNode *node, AstNode *coll,
                              GrayType *coll_t, const char *idx_name,
                              bool *out_map_needs_tmp, char *map_tmp_name,
                              size_t map_tmp_size) {
    int mi_id = codegen_next_id(codegen);
    char mi_name[SHORT_VAR_BUF];
    snprintf(mi_name, sizeof(mi_name), "_gray_mi%d", mi_id);

    const char *c_key = "GrayString";
    const char *c_val = "int64_t";
    if (coll_t->key_type) c_key = gray_map_element_c_type(codegen, coll_t->key_type);
    if (coll_t->value_type) c_val = gray_map_element_c_type(codegen, coll_t->value_type);

    *out_map_needs_tmp = (coll->kind != NODE_LABEL);
    if (*out_map_needs_tmp) {
        snprintf(map_tmp_name, map_tmp_size, "_gray_map%d", mi_id);
        emit_formatted(codegen, "{ GrayMap %s = ", map_tmp_name);
        emit_expression(codegen, coll);
        emit(codegen, ";\n");
        emit_indent(codegen);
    }

    char slot_name[SHORT_VAR_BUF];
    snprintf(slot_name, sizeof(slot_name), "_gray_sl%d", mi_id);
    {
        char *guard_expr = iter_guard_expr(codegen, *out_map_needs_tmp, map_tmp_name, coll);
        iter_guard_push(codegen, guard_expr);
        free(guard_expr);
    }
    if (*out_map_needs_tmp) emit_formatted(codegen, "gray_atomic_add32(&%s.iterating, 1);\n", map_tmp_name);
    else { emit(codegen, "gray_atomic_add32(&"); emit_expression(codegen, coll); emit(codegen, ".iterating, 1);\n"); }
    emit_indent(codegen);
    emit_formatted(codegen, "for (int32_t %s = 0; %s < ", mi_name, mi_name);
    if (*out_map_needs_tmp) emit_formatted(codegen, "%s", map_tmp_name);
    else emit_expression(codegen, coll);
    emit_formatted(codegen, ".order_len; %s++) {\n", mi_name);
    codegen->indent++;
    emit_indent(codegen);
    emit_formatted(codegen, "int32_t %s = ", slot_name);
    if (*out_map_needs_tmp) emit_formatted(codegen, "%s", map_tmp_name);
    else emit_expression(codegen, coll);
    emit_formatted(codegen, ".order[%s];\n", mi_name);
    emit_indent(codegen);
    emit_formatted(codegen, "if (%s < 0) continue;\n", slot_name);

    if (node->data.for_each.index_name) {
        if (strcmp(node->data.for_each.index_name, "_") != 0) {
            emit_indent(codegen);
            emit_formatted(codegen, "%s %s = *(%s *)gray_map_key_at(&",
                c_key, sanitize_name(node->data.for_each.index_name), c_key);
            if (*out_map_needs_tmp) emit_formatted(codegen, "%s", map_tmp_name);
            else emit_expression(codegen, coll);
            emit_formatted(codegen, ", %s);\n", slot_name);
        }
        if (strcmp(node->data.for_each.var_name, "_") != 0) {
            emit_indent(codegen);
            emit_formatted(codegen, "%s %s = *(%s *)gray_map_value_at(&",
                c_val, sanitize_name(node->data.for_each.var_name), c_val);
            if (*out_map_needs_tmp) emit_formatted(codegen, "%s", map_tmp_name);
            else emit_expression(codegen, coll);
            emit_formatted(codegen, ", %s);\n", slot_name);
        }
    } else {
        emit_indent(codegen);
        emit_formatted(codegen, "%s %s = *(%s *)gray_map_key_at(&",
            c_key, sanitize_name(node->data.for_each.var_name), c_key);
        if (*out_map_needs_tmp) emit_formatted(codegen, "%s", map_tmp_name);
        else emit_expression(codegen, coll);
        emit_formatted(codegen, ", %s);\n", slot_name);
    }

    /* A wide-integer key or value binds as a struct, not int64_t; track it so
     * reads of the loop variable resolve to the bigint type. The caller
     * restores bigint_var_count after the loop body, so these are loop-scoped. */
    const char *key_bind = node->data.for_each.index_name
        ? node->data.for_each.index_name : node->data.for_each.var_name;
    const char *val_bind = node->data.for_each.index_name
        ? node->data.for_each.var_name : NULL;
    const char *key_bi = bigint_type_name(coll_t->key_type);
    const char *val_bi = bigint_type_name(coll_t->value_type);
    if (key_bi && key_bind && strcmp(key_bind, "_") != 0)
        register_bigint_variable(codegen, key_bind, key_bi);
    if (val_bi && val_bind && strcmp(val_bind, "_") != 0)
        register_bigint_variable(codegen, val_bind, val_bi);
}

static void emit_foreach_string(CodeGen *codegen, AstNode *node, AstNode *coll,
                                 const char *idx_name) {
    emit_formatted(codegen, "{ GrayString _gray_str = ");
    emit_expression(codegen, coll);
    emit(codegen, ";\n");
    emit_indent(codegen);
    emit_formatted(codegen, "for (int32_t %s = 0; %s < _gray_str.len; %s++) {\n", idx_name, idx_name, idx_name);
    codegen->indent++;
    emit_indent(codegen);
    emit_formatted(codegen, "int32_t %s = _gray_str.data[%s];\n", sanitize_name(node->data.for_each.var_name), idx_name);
}

static void emit_foreach_array(CodeGen *codegen, AstNode *node, AstNode *coll,
                                GrayType *coll_t, const char *idx_name,
                                bool *out_coll_needs_tmp, char *arr_tmp_name,
                                size_t arr_tmp_size) {
    const char *c_elem = "int64_t";
    if (coll_t && coll_t->kind == TK_ARRAY && coll_t->element_type) {
        const char *elem_tn = codegen_effective_type_string(codegen, coll_t->element_type);
        GrayType *et = type_from_name(elem_tn);
        if (et->kind == TK_FLOAT) c_elem = (strcmp(elem_tn, "f32") == 0) ? "float" : "double";
        else if (et->kind == TK_BOOL) c_elem = "bool";
        else if (et->kind == TK_STRING) c_elem = "GrayString";
        else if (et->kind == TK_ARRAY) c_elem = "GrayArray";
        else if (et->kind == TK_MAP) c_elem = "GrayMap";
        else if (et->kind == TK_STRUCT) c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        else if (et->kind == TK_POINTER) c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        else if (et->kind == TK_CHAR) c_elem = "int32_t";
        else if (et->kind == TK_BYTE) c_elem = "uint8_t";
        /* Wide integers are TK_INT/TK_UINT in the type system but structs in C;
         * the element is stored packed as that struct, like a map value. */
        else if (is_bigint_type(elem_tn)) c_elem = bigint_prefix(elem_tn);
        /* Sized int element types are stored packed by cast(arr, [T]); the
         * int64_t fall-through would stride past the buffer. Match storage. */
        else if (et->kind == TK_INT || et->kind == TK_UINT)
            c_elem = gray_type_to_c_codegen(codegen, elem_tn);
        else if (et->kind == TK_ENUM) {
            c_elem = codegen_enum_is_string(codegen, elem_tn)
                ? "GrayString" : gray_type_to_c_codegen(codegen, elem_tn);
        }
    }

    int alen_id = codegen_next_id(codegen);
    char len_name[SHORT_VAR_BUF];
    snprintf(len_name, sizeof(len_name), "_gray_alen%d", alen_id);
    *out_coll_needs_tmp = (coll->kind != NODE_LABEL);
    /* A field path names an array the caller can still reach, so the guard
     * has to sit on that array — putting it on the snapshot below would let
     * destructive mutations through unnoticed. */
    bool guard_real = *out_coll_needs_tmp && is_stable_field_path(coll);
    char guard_ptr[SHORT_VAR_BUF];
    guard_ptr[0] = '\0';
    if (*out_coll_needs_tmp) {
        snprintf(arr_tmp_name, arr_tmp_size, "_gray_arr%d", alen_id);
        emit(codegen, "{ ");
        if (guard_real) {
            snprintf(guard_ptr, sizeof(guard_ptr), "_gray_arrp%d", alen_id);
            emit_formatted(codegen, "GrayArray *%s = &(", guard_ptr);
            emit_expression(codegen, coll);
            emit_formatted(codegen, "); GrayArray %s = *%s;\n", arr_tmp_name, guard_ptr);
        } else {
            emit_formatted(codegen, "GrayArray %s = ", arr_tmp_name);
            emit_expression(codegen, coll);
            emit(codegen, ";\n");
        }
        emit_indent(codegen);
    }
    emit_formatted(codegen, "{ int32_t %s = ", len_name);
    if (*out_coll_needs_tmp) emit_formatted(codegen, "%s.len;\n", arr_tmp_name);
    else { emit_expression(codegen, coll); emit(codegen, ".len;\n"); }
    {
        char *guard_expr;
        if (guard_real) {
            char buf[SHORT_VAR_BUF + 4];
            snprintf(buf, sizeof(buf), "(*%s)", guard_ptr);
            guard_expr = strdup(buf);
        } else {
            guard_expr = iter_guard_expr(codegen, *out_coll_needs_tmp, arr_tmp_name, coll);
        }
        iter_guard_push(codegen, guard_expr);
        free(guard_expr);
    }
    emit_indent(codegen);
    emit_formatted(codegen, "gray_atomic_add32(&%s.iterating, 1);\n", iter_guard_top(codegen));
    emit_indent(codegen);
    emit_formatted(codegen, "for (int32_t %s = 0; %s < %s; %s++) {\n", idx_name, idx_name, len_name, idx_name);
    codegen->indent++;
    emit_indent(codegen);
    emit_formatted(codegen, "%s %s = GRAY_ARRAY_GET_AT(", c_elem, sanitize_name(node->data.for_each.var_name));
    if (*out_coll_needs_tmp) emit_formatted(codegen, "%s, %s, %s, \"%s\", %d);\n", arr_tmp_name, c_elem, idx_name, codegen->file, node->token.line);
    else { emit_expression(codegen, coll); emit_formatted(codegen, ", %s, %s, \"%s\", %d);\n", c_elem, idx_name, codegen->file, node->token.line); }

    /* Track a wide-integer element binding so reads resolve to the bigint type.
     * The caller restores bigint_var_count after the loop body. */
    if (coll_t && coll_t->kind == TK_ARRAY) {
        const char *elem_bi = bigint_type_name(coll_t->element_type);
        if (elem_bi && strcmp(node->data.for_each.var_name, "_") != 0)
            register_bigint_variable(codegen, node->data.for_each.var_name, elem_bi);
    }
}

static void emit_statement(CodeGen *codegen, AstNode *node) {
    codegen_enter_node(codegen, node);
    if (!node) return;

    switch (node->kind) {
    case NODE_VAR_DECL: {
        /* A module-level variable is emitted under the mangled name its
         * module gives it, which is what references to it resolve to. The
         * name is swapped in for the duration rather than threaded through
         * the fifteen places the emitter reads it — the same shape codegen
         * already uses for generic instantiations and struct namespacing. */
        DeclEntry *entry = module_table_entry_for_node(codegen->modules, node);
        const char *written = node->data.var_decl.name;
        if (entry && entry->kind == DECL_CONST)
            node->data.var_decl.name = module_mangle(codegen->modules, entry);
        emit_variable_declaration(codegen, node);
        node->data.var_decl.name = written;
        break;
    }
    case NODE_ASSIGN_STMT:
        emit_assign_statement(codegen, node);
        break;
    case NODE_RETURN_STMT:
        emit_return_statement(codegen, node);
        break;
    case NODE_EXPR_STMT:
        emit_expression_statement(codegen, node);
        break;
    case NODE_IF_STMT:
        emit_if_statement(codegen, node);
        break;
    case NODE_FOR_STMT:
        emit_for_statement(codegen, node);
        break;
    case NODE_FOR_EACH_STMT: {
        emit_indent(codegen);
        AstNode *coll = node->data.for_each.collection;
        GrayType *coll_t = codegen->type_table ? typetable_get(codegen->type_table, coll) : NULL;

        const char *idx_name = node->data.for_each.index_name;
        if (!idx_name) idx_name = "_gray_idx";
        bool is_map_iter = (coll_t && coll_t->kind == TK_MAP);
        bool coll_needs_tmp = false;
        char arr_tmp_name[SHORT_VAR_BUF];
        arr_tmp_name[0] = '\0';
        bool map_needs_tmp = false;
        char map_tmp_name[SHORT_VAR_BUF];
        map_tmp_name[0] = '\0';

        /* The foreach emitters may register wide-integer loop bindings; drop
         * them again once the body is emitted so they stay loop-scoped. */
        int prev_bigint_var_count = codegen->bigint_var_count;

        if (is_map_iter) {
            emit_foreach_map(codegen, node, coll, coll_t, idx_name,
                             &map_needs_tmp, map_tmp_name, sizeof(map_tmp_name));
        } else if (coll_t && coll_t->kind == TK_STRING) {
            emit_foreach_string(codegen, node, coll, idx_name);
        } else {
            emit_foreach_array(codegen, node, coll, coll_t, idx_name,
                               &coll_needs_tmp, arr_tmp_name, sizeof(arr_tmp_name));
        }

        emit_loop_body_with_arena(codegen, node->data.for_each.body);
        codegen->bigint_var_count = prev_bigint_var_count;
        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        /* Decrement map iteration guard */
        if (is_map_iter) {
            iter_guard_pop(codegen);
            emit_indent(codegen);
            if (map_needs_tmp) emit_formatted(codegen, "gray_atomic_sub32(&%s.iterating, 1);\n", map_tmp_name);
            else { emit(codegen, "gray_atomic_sub32(&"); emit_expression(codegen, coll); emit(codegen, ".iterating, 1);\n"); }
            if (map_needs_tmp) {
                emit_indent(codegen);
                emit(codegen, "}\n");
            }
        }
        /* Close extra scope for string iteration */
        if (coll_t && coll_t->kind == TK_STRING) {
            emit_indent(codegen);
            emit(codegen, "}\n");
        }
        /* Decrement array iteration guard, then close the snapshot block */
        if (coll_t && coll_t->kind != TK_MAP && coll_t->kind != TK_STRING) {
            emit_indent(codegen);
            emit_formatted(codegen, "gray_atomic_sub32(&%s.iterating, 1);\n", iter_guard_top(codegen));
            iter_guard_pop(codegen);
            emit_indent(codegen);
            emit(codegen, "}\n");
            if (coll_needs_tmp) {
                emit_indent(codegen);
                emit(codegen, "}\n");
            }
        }
        break;
    }
    case NODE_WHILE_STMT:
        emit_while_statement(codegen, node);
        break;
    case NODE_LOOP_STMT:
        emit_loop_statement(codegen, node);
        break;
    case NODE_BREAK_STMT:
        emit_indent(codegen);
        emit_loop_exit_unwind(codegen);
        emit(codegen, "break;\n");
        break;
    case NODE_CONTINUE_STMT:
        emit_indent(codegen);
        emit_loop_exit_unwind(codegen);
        emit(codegen, "continue;\n");
        break;
    case NODE_WHEN_STMT: {
        /* Emit as if-else chain for now (switch requires constant values) */
        AstNode *val = node->data.when_stmt.value;
        GrayType *when_val_t = codegen->type_table ? typetable_get(codegen->type_table, val) : NULL;
        bool when_is_string = (when_val_t && when_val_t->kind == TK_STRING);
        bool when_is_tagged = false;
        const char *when_tagged_ename = NULL;
        if (!when_is_string && when_val_t && when_val_t->kind == TK_ENUM && when_val_t->name) {
            if (codegen_enum_is_string(codegen, when_val_t->name)) when_is_string = true;
            if (codegen_enum_is_tagged(codegen, when_val_t->name)) {
                when_is_tagged = true;
                when_tagged_ename = when_val_t->name;
            }
        }
        /* Detect wide integer type for the when value */
        const char *when_bigint = (when_val_t && when_val_t->name && is_bigint_type(when_val_t->name))
            ? when_val_t->name : resolve_bigint_type(codegen, val);
        /* Evaluate the match expression once into a temporary so that
         * side-effecting expressions (function calls, increments, etc.)
         * are not re-executed for each is-arm. */
        char when_tmp[64];
        snprintf(when_tmp, sizeof(when_tmp), "_gray_when%d", codegen_next_id(codegen));
        emit_indent(codegen);
        emit_formatted(codegen, "__auto_type %s = ", when_tmp);
        emit_expression(codegen, val);
        emit(codegen, ";\n");
        for (int i = 0; i < node->data.when_stmt.case_count; i++) {
            WhenCase *wc = &node->data.when_stmt.cases[i];
            emit_indent(codegen);
            if (i == 0) {
                emit(codegen, "if (");
            } else {
                emit(codegen, "} else if (");
            }
            for (int j = 0; j < wc->value_count; j++) {
                if (j > 0) emit(codegen, " || ");
                if (wc->values[j]->kind == NODE_WHEN_PATTERN) {
                    /* Destructuring pattern: compare tag */
                    const char *vname = wc->values[j]->data.when_pattern.variant;
                    const char *ename = wc->values[j]->data.when_pattern.enum_name;
                    if (!ename) ename = when_tagged_ename;
                    emit(codegen, when_tmp);
                    emit_formatted(codegen, ".tag == GrayEnum_%s_TAG_%s", ename, vname);
                } else if (when_is_tagged) {
                    /* Tagged enum, plain variant: compare .tag */
                    AstNode *cv = wc->values[j];
                    const char *vname = NULL;
                    if (cv->kind == NODE_MEMBER_EXPR &&
                        (ast_member_qualifier(cv) || ast_member_chain(cv, NULL, NULL))) {
                        /* Enum.VARIANT, and the module-qualified
                         * mod.Enum.VARIANT — which nests one level deeper and
                         * otherwise fell through to constructing a value
                         * where a tag comparison belongs. */
                        vname = cv->data.member.member;
                    } else if (cv->kind == NODE_IMPLICIT_ENUM) {
                        vname = cv->data.implicit_enum.variant;
                    }
                    if (vname) {
                        emit(codegen, when_tmp);
                        emit_formatted(codegen, ".tag == GrayEnum_%s_TAG_%s", when_tagged_ename, vname);
                    } else {
                        emit(codegen, when_tmp);
                        emit(codegen, ".tag == ");
                        emit_expression(codegen, cv);
                    }
                } else if (wc->is_range && wc->values[j]->kind == NODE_RANGE_EXPR) {
                    AstNode *range = wc->values[j];
                    /* Check if step is a negative literal to reverse comparison direction */
                    bool neg_step = (range->data.range_expr.step &&
                        range->data.range_expr.step->kind == NODE_PREFIX_EXPR &&
                        range->data.range_expr.step->data.prefix.op == TOK_MINUS);
                    emit(codegen, "(");
                    emit(codegen, when_tmp);
                    emit(codegen, neg_step ? " <= " : " >= ");
                    emit_expression(codegen, range->data.range_expr.start);
                    emit(codegen, " && ");
                    emit(codegen, when_tmp);
                    emit(codegen, neg_step ? " > " : " < ");
                    emit_expression(codegen, range->data.range_expr.end);
                    if (range->data.range_expr.step) {
                        emit(codegen, " && (");
                        emit(codegen, when_tmp);
                        emit(codegen, " - ");
                        emit_expression(codegen, range->data.range_expr.start);
                        emit(codegen, ") % ");
                        emit_expression(codegen, range->data.range_expr.step);
                        emit(codegen, " == 0");
                    }
                    emit(codegen, ")");
                } else if (when_is_string) {
                    emit(codegen, "gray_string_eq(");
                    emit(codegen, when_tmp);
                    emit(codegen, ", ");
                    emit_expression(codegen, wc->values[j]);
                    emit(codegen, ")");
                } else if (when_bigint) {
                    emit_formatted(codegen, "%s_eq(", bigint_prefix(when_bigint));
                    emit(codegen, when_tmp);
                    emit(codegen, ", ");
                    emit_expression(codegen, wc->values[j]);
                    emit(codegen, ")");
                } else {
                    emit(codegen, when_tmp);
                    emit(codegen, " == ");
                    emit_expression(codegen, wc->values[j]);
                }
            }
            emit(codegen, ") {\n");
            codegen->indent++;
            /* Emit binding declarations for when patterns */
            for (int j = 0; j < wc->value_count; j++) {
                if (wc->values[j]->kind == NODE_WHEN_PATTERN) {
                    AstNode *pat = wc->values[j];
                    const char *vname = pat->data.when_pattern.variant;
                    const char *ename = pat->data.when_pattern.enum_name;
                    if (!ename) ename = when_tagged_ename;
                    int eidx = codegen_enum_index(codegen, ename);
                    if (eidx >= 0) {
                        AstNode *decl = codegen->enum_decls[eidx];
                        int vidx = -1;
                        for (int variant_index = 0; variant_index < decl->data.enum_decl.value_count; variant_index++) {
                            if (strcmp(decl->data.enum_decl.values[variant_index].name, vname) == 0) { vidx = variant_index; break; }
                        }
                        if (vidx >= 0) {
                            EnumVal *ev = &decl->data.enum_decl.values[vidx];
                            int limit = pat->data.when_pattern.binding_count < ev->payload_count
                                ? pat->data.when_pattern.binding_count : ev->payload_count;
                            for (int bi = 0; bi < limit; bi++) {
                                emit_indent(codegen);
                                emit_formatted(codegen, "%s %s = ",
                                    gray_type_to_c_codegen(codegen, ev->payload_types[bi]),
                                    pat->data.when_pattern.bindings[bi]);
                                emit(codegen, when_tmp);
                                emit_formatted(codegen, ".data.%s._%d;\n", vname, bi);
                            }
                        }
                    }
                }
            }
            emit_block(codegen, wc->body);
            codegen->indent--;
        }
        if (node->data.when_stmt.default_body) {
            emit_indent(codegen);
            if (node->data.when_stmt.case_count > 0) {
                emit(codegen, "} else {\n");
            } else {
                emit(codegen, "{\n");
            }
            codegen->indent++;
            emit_block(codegen, node->data.when_stmt.default_body);
            codegen->indent--;
        } else if (node->data.when_stmt.is_strict && node->data.when_stmt.case_count > 0) {
            /* A #strict enum `when` is exhaustive (E3056 fired otherwise), so
             * the fall-through is dead. Say so, or C warns that a value-
             * returning function may fall off the end (-Wreturn-type). */
            emit_indent(codegen);
            emit(codegen, "} else { __builtin_unreachable(); }\n");
            break;
        }
        emit_indent(codegen);
        emit(codegen, "}\n");
        break;
    }
    case NODE_FUNC_DECL: {
        /* Generic function ): emit one specialised copy per
         * concrete instantiation the typechecker recorded. If a
         * generic function was declared but never called, there are
         * no instantiations and we skip emission entirely; the
         * un-specialised form has '?' in its signature and can't be
         * compiled as C. */
        bool has_wc = func_is_generic(node);
        if (has_wc) {
            const char *orig_name = node->data.func_decl.name;
            for (int inst_index = 0; inst_index < node->data.func_decl.instantiation_count; inst_index++) {
                const char *concrete = node->data.func_decl.instantiations[inst_index];
                char mangled[MSG_BUF_SIZE];
                mangle_generic_name(mangled, sizeof(mangled), orig_name, concrete);

                node->data.func_decl.name = mangled;
                const char *saved = codegen->wildcard_binding;
                codegen->wildcard_binding = concrete;
                /* Per-instantiation multi-return typedefs were already
                 * emitted in the forward-declaration loop ). */
                emit_function_declaration(codegen, node, false);
                codegen->wildcard_binding = saved;
            }
            node->data.func_decl.name = orig_name;
        } else {
            emit_function_declaration(codegen, node,
                strcmp(node->data.func_decl.name, "main") == 0);
        }
        break;
    }
    case NODE_BLOCK_STMT:
        /* Inline block (e.g., from multi-var declaration expansion) */
        emit_block(codegen, node);
        break;
    case NODE_ENSURE_STMT:
        /* Ensure is emitted at return/function-exit; record that control flow
         * has now reached this one so earlier returns don't run it. */
        codegen->ensure_reached++;
        break;
    case NODE_STRUCT_DECL:
        /* Struct declarations are emitted in the preamble */
        break;
    case NODE_ENUM_DECL:
        /* Enum declarations are emitted in the preamble */
        break;
    case NODE_ALIAS_DECL:
        /* Type aliases are erased at codegen — emit nothing */
        break;
    case NODE_MODULE_DECL:
        /* Module declarations are informational only */
        break;
    case NODE_IMPORT_STMT:
        /* Imports are handled during the preamble scan */
        break;
    case NODE_USING_STMT:
        /* Function-scoped using: add to using_modules so bare-name
         * dispatch works for the rest of this function body. */
        for (int j = 0; j < node->data.using_stmt.count; j++) {
            GROW_ARRAY(codegen->using_modules, codegen->using_module_count,
                codegen->using_module_cap);
            codegen->using_modules[codegen->using_module_count++] = node->data.using_stmt.modules[j];
        }
        break;
    default:
        emit_indent(codegen);
        emit_formatted(codegen, "/* grayc: unhandled statement kind %d at %s:%d */\n",
            node->kind, codegen->file, node->token.line);
        break;
    }
}

/* --- Public API --- */

static bool codegen_is_enum(CodeGen *codegen, const char *name) {
    return bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                   sizeof(const char *), keyword_compare) != NULL;
}

static bool codegen_enum_is_tagged(CodeGen *codegen, const char *name) {
    const char **hit = bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                               sizeof(const char *), keyword_compare);
    if (hit) return codegen->enum_is_tagged[hit - codegen->enum_names];
    return false;
}

static int codegen_enum_index(CodeGen *codegen, const char *name) {
    const char **hit = bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                               sizeof(const char *), keyword_compare);
    if (hit) return (int)(hit - codegen->enum_names);
    return -1;
}

/* True for the program-wide ErrorCode enum and for any user enum marked
 * #error_code. Their values share one global slot space, so they must also
 * share one string form: the variant name, via gray_error_code_name(). */
static bool codegen_enum_is_error_code(CodeGen *codegen, const char *name) {
    if (!name) return false;
    if (strcmp(name, "ErrorCode") == 0) return true;
    int idx = codegen_enum_index(codegen, name);
    return idx >= 0 && codegen->enum_decls[idx] &&
           codegen->enum_decls[idx]->data.enum_decl.is_error_code;
}

/* Source paths are emitted into C string literals in roughly a hundred places
 * (panic sites, gray_enter_func, here()). A Windows path like C:\Users\... would
 * be read as escape sequences there, and \U is a hard error rather than a
 * warning, so every program compiled from an absolute Windows path would fail
 * to build. Every Windows file API accepts forward slashes, so normalize once
 * on the way in instead of escaping at each emit site. This also keeps the
 * generated C identical across platforms, and fixes path handling in embed(),
 * which searches for '/' when splitting off the source directory. */
static char *normalize_path_separators(const char *path) {
    if (!path) return NULL;
    size_t len = strlen(path);
    char *copy = xmalloc(len + 1);
    memcpy(copy, path, len + 1);
    for (char *p = copy; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    return copy;
}

CodeGen codegen_create(const char *file) {
    /* Zero-initialize so fields absent from the explicit list below (e.g.
     * in_const_decl, current_var_name) start false/NULL instead of stack
     * garbage. A truthy in_const_decl silently suppresses every runtime
     * overflow and division check in the file. */
    CodeGen codegen = {0};
    codegen.output = buffer_create(OUTPUT_BUF_INITIAL);
    codegen.global_init = buffer_create(MSG_BUF_SIZE);
    codegen.indent = 0;
    codegen.has_mem = false;
    codegen.has_fmt = false;
    codegen.file_owned = normalize_path_separators(file);
    codegen.file = codegen.file_owned;
    codegen.enum_names = NULL;
    codegen.enum_is_string = NULL;
    codegen.enum_is_tagged = NULL;
    codegen.enum_decls = NULL;
    codegen.enum_count = 0;
    codegen.enum_cap = 0;
    codegen.current_func = NULL;
    codegen.loop_scope_depth = 0;
    codegen.all_funcs = NULL;
    codegen.func_count = 0;
    codegen.func_cap = 0;
    codegen.funcs_by_name = NULL;
    codegen.funcs_by_name_built = false;
    codegen.type_table = NULL;
    codegen.ref_vars = NULL;
    codegen.ref_var_count = 0;
    codegen.ref_var_cap = 0;
    codegen.raw_vars = NULL;
    codegen.raw_var_count = 0;
    codegen.raw_var_cap = 0;
    codegen.bigint_var_names = NULL;
    codegen.bigint_var_types = NULL;
    codegen.bigint_var_count = 0;
    codegen.bigint_var_cap = 0;
    codegen.struct_decls = NULL;
    codegen.struct_decl_count = 0;
    codegen.struct_decl_cap = 0;
    codegen.func_field_index = NULL;
    codegen.func_field_count = 0;
    codegen.func_field_index_built = false;
    codegen.using_modules = NULL;
    codegen.using_module_count = 0;
    codegen.using_module_cap = 0;
    codegen.imported_modules = NULL;
    codegen.imported_module_count = 0;
    codegen.imported_module_cap = 0;
    codegen.c_headers = NULL;
    codegen.c_header_count = 0;
    codegen.c_header_cap = 0;
    codegen.has_c_imports = false;
    codegen.type_alias_names = NULL;
    codegen.type_alias_targets = NULL;
    codegen.type_alias_count = 0;
    codegen.type_alias_cap = 0;
    codegen.wildcard_binding = NULL;
    codegen.pending_call_typed_sig = NULL;
    codegen.scope_arenas = NULL;
    codegen.scope_arena_count = 0;
    codegen.scope_arena_cap = 0;
    codegen.iter_guards = NULL;
    codegen.iter_guard_count = 0;
    codegen.iter_guard_cap = 0;
    codegen.ns_func_names = NULL;
    codegen.ns_func_name_count = 0;
    codegen.ns_func_name_cap = 0;
    codegen.temp_counter = 0;
    return codegen;
}

/* Check if a module name is in a set of imported stdlib modules. */
static bool has_stdlib_module(const char *const *modules, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (strcmp(modules[i], name) == 0) return true;
    return false;
}

void codegen_generate(CodeGen *codegen, AstNode *program) {
    if (program->kind != NODE_PROGRAM) return;

    /* Collect imported stdlib module names (used for conditional header inclusion) */
    #define MAX_STDLIB_IMPORTS 64
    const char *stdlib_imports[MAX_STDLIB_IMPORTS];
    int stdlib_import_count = 0;

    /* Statement buckets — single categorization pass to avoid repeated full scans. */
    int total = program->data.program.stmt_count;
    int enum_bucket_count = 0, enum_bucket_cap = 16;
    AstNode **enum_bucket = xmalloc(sizeof(AstNode *) * (size_t)enum_bucket_cap);
    int func_bucket_count = 0, func_bucket_cap = 16;
    AstNode **func_bucket = xmalloc(sizeof(AstNode *) * (size_t)func_bucket_cap);
    int var_bucket_count = 0, var_bucket_cap = 16;
    AstNode **var_bucket = xmalloc(sizeof(AstNode *) * (size_t)var_bucket_cap);
    int other_bucket_count = 0, other_bucket_cap = 16;
    AstNode **other_bucket = xmalloc(sizeof(AstNode *) * (size_t)other_bucket_cap);

    #define BUCKET_PUSH(arr, cnt, cap, val) do { \
        if ((cnt) >= (cap)) { \
            (cap) = (cap) * 2; \
            (arr) = xrealloc((arr), sizeof(AstNode *) * (size_t)(cap)); \
        } \
        (arr)[(cnt)++] = (val); \
    } while (0)

    for (int i = 0; i < total; i++) {
        AstNode *stmt = program->data.program.stmts[i];
        if (stmt->kind == NODE_IMPORT_STMT) {
            for (int j = 0; j < stmt->data.import_stmt.count; j++) {
                ImportItem *item = &stmt->data.import_stmt.items[j];
                if (item->is_stdlib && item->module) {
                    if (strcmp(item->module, "mem") == 0) codegen->has_mem = true;
                    if (strcmp(item->module, "fmt") == 0) codegen->has_fmt = true;
                    if (stdlib_import_count < MAX_STDLIB_IMPORTS)
                        stdlib_imports[stdlib_import_count++] = item->module;
                }
                /* Collect C interop headers */
                if (item->is_c_import && item->path) {
                    codegen->has_c_imports = true;
                    GROW_ARRAY(codegen->c_headers, codegen->c_header_count,
                        codegen->c_header_cap);
                    codegen->c_headers[codegen->c_header_count++] = item->path;
                }
                /* Track all imported module names */
                if (item->module) {
                    const char *mname = item->alias ? item->alias : item->module;
                    GROW_ARRAY(codegen->imported_modules, codegen->imported_module_count,
                        codegen->imported_module_cap);
                    codegen->imported_modules[codegen->imported_module_count++] = mname;
                }
            }
            /* import and use; register all modules for using */
            if (stmt->data.import_stmt.auto_use) {
                for (int j = 0; j < stmt->data.import_stmt.count; j++) {
                    ImportItem *item = &stmt->data.import_stmt.items[j];
                    if (item->module) {
                        GROW_ARRAY(codegen->using_modules, codegen->using_module_count,
                            codegen->using_module_cap);
                        codegen->using_modules[codegen->using_module_count++] = item->module;
                    }
                }
            }
        }
        if (stmt->kind == NODE_USING_STMT) {
            for (int j = 0; j < stmt->data.using_stmt.count; j++) {
                GROW_ARRAY(codegen->using_modules, codegen->using_module_count,
                    codegen->using_module_cap);
                codegen->using_modules[codegen->using_module_count++] = stmt->data.using_stmt.modules[j];
            }
        }
        if (stmt->kind == NODE_ALIAS_DECL) {
            /* Collect type aliases for resolution during codegen */
            if (codegen->type_alias_count >= codegen->type_alias_cap) {
                codegen->type_alias_cap = codegen->type_alias_cap ? codegen->type_alias_cap * 2 : 8;
                codegen->type_alias_names = xrealloc(codegen->type_alias_names,
                    sizeof(const char *) * (size_t)codegen->type_alias_cap);
                codegen->type_alias_targets = xrealloc(codegen->type_alias_targets,
                    sizeof(const char *) * (size_t)codegen->type_alias_cap);
            }
            /* Key the alias by its module's spelling, which is what a
             * reference to it resolves to; and resolve the target the same
             * way, since it names a type in the alias's own module. */
            codegen_enter_node(codegen, stmt);
            codegen->type_alias_names[codegen->type_alias_count] =
                codegen_decl_name(codegen, stmt, stmt->data.alias_decl.name);
            codegen->type_alias_targets[codegen->type_alias_count] =
                codegen_resolve_type(codegen, stmt->data.alias_decl.target_type);
            codegen->type_alias_count++;
            continue; /* aliases are erased — not emitted */
        }
        if (stmt->kind == NODE_STRUCT_DECL) {
            stmt->data.struct_decl.name =
                codegen_decl_name(codegen, stmt, stmt->data.struct_decl.name);
            GROW_ARRAY(codegen->struct_decls, codegen->struct_decl_count,
                codegen->struct_decl_cap);
            codegen->struct_decls[codegen->struct_decl_count++] = stmt;
        } else if (stmt->kind == NODE_ENUM_DECL) {
            BUCKET_PUSH(enum_bucket, enum_bucket_count, enum_bucket_cap, stmt);
        } else if (stmt->kind == NODE_FUNC_DECL) {
            if (stmt->data.func_decl.is_test) {
                /* #test functions exist only for `gray test`; a normal build
                 * drops them entirely (no forward decl, no definition, no
                 * call). A --test build keeps only the ones declared in the
                 * file being compiled — a #test reached through an import
                 * belongs to that module's own test run, not this one. */
                bool keep = codegen->test_mode;
                if (keep && stmt->token.file && codegen->file) {
                    char *nf = normalize_path_separators(stmt->token.file);
                    if (nf && strcmp(nf, codegen->file) != 0) keep = false;
                    free(nf);
                }
                if (!keep) continue;
            }
            BUCKET_PUSH(func_bucket, func_bucket_count, func_bucket_cap, stmt);
        } else if (stmt->kind == NODE_VAR_DECL) {
            BUCKET_PUSH(var_bucket, var_bucket_count, var_bucket_cap, stmt);
        } else if (stmt->kind != NODE_USING_STMT) {
            BUCKET_PUSH(other_bucket, other_bucket_count, other_bucket_cap, stmt);
        }
    }

    /* Emit preamble — core headers always included, stdlib headers only when imported */
    emit(codegen, "/* Generated by grayc */\n");
    emit(codegen, "#include \"runtime.h\"\n");
    emit(codegen, "#include \"array.h\"\n");
    emit(codegen, "#include \"map.h\"\n");
    emit(codegen, "#include \"builtins.h\"\n");
    /* os.h is always needed — generated main() calls gray_os_init(). */
    emit(codegen, "#include \"os.h\"\n");
    /* test.h declares the test-runner entry points used by the generated
     * main() when compiling with --test. */
    if (codegen->test_mode)
        emit(codegen, "#include \"test.h\"\n");
    /* arrays.h / maps.h / strings.h are spliced in here after body emission,
     * but only when the `in` operator or an explicit import needs them. */
    size_t collection_include_anchor = codegen->output.len;
    /* bigint.h is always needed — i128/u128/i256/u256 are keyword types
     * usable with no import. */
    emit(codegen, "#include \"bigint.h\"\n");

    /* An explicit import of one of these modules also needs its header. */
    if (has_stdlib_module(stdlib_imports, stdlib_import_count, "arrays"))
        codegen->needs_arrays_h = true;
    if (has_stdlib_module(stdlib_imports, stdlib_import_count, "maps"))
        codegen->needs_maps_h = true;
    if (has_stdlib_module(stdlib_imports, stdlib_import_count, "strings"))
        codegen->needs_strings_h = true;

    /* Remaining stdlib module headers: included only when imported. */
    static const struct { const char *module; const char *header; } stdlib_headers[] = {
        {"mem",      "mem.h"},
        {"fmt",      "fmt.h"},
        {"math",     "math.h"},
        {"io",       "io.h"},
        {"random",   "random.h"},
        {"time",     "time.h"},
        {"uuid",     "uuid.h"},
        {"encoding", "encoding.h"},
        {"crypto",   "crypto.h"},
        {"binary",   "binary.h"},
        {"csv",      "csv.h"},
        {"json",     "json.h"},
        {"strconv",  "strconv.h"},
        {"chars",    "chars.h"},
        {"sqlite",   "sqlite.h"},
        {"threads",  "threads.h"},
        {"sync",     "sync.h"},
        {"atomic",   "atomic_mod.h"},
        {"channels", "channels.h"},
        {"regex",    "regex.h"},
        {"net",      "net.h"},
        {"http",     "http.h"},
        {"server",   "server.h"},
        {"runtime",  "runtime_mod.h"},
    };
    for (int i = 0; i < (int)(sizeof(stdlib_headers) / sizeof(stdlib_headers[0])); i++) {
        if (has_stdlib_module(stdlib_imports, stdlib_import_count, stdlib_headers[i].module))
            emit_formatted(codegen, "#include \"%s\"\n", stdlib_headers[i].header);
    }

    /* Emit user C interop headers (after Grayscale internals to prevent collisions) */
    if (codegen->c_header_count > 0) {
        emit(codegen, "\n/* C interop headers */\n");
        for (int i = 0; i < codegen->c_header_count; i++) {
            const char *hdr = codegen->c_headers[i];
            /* Defense-in-depth: skip any path that slipped through with dangerous chars */
            bool safe = true;
            for (const char *scan = hdr; *scan; scan++) {
                unsigned char ch = (unsigned char)*scan;
                bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '/' || ch == '.' || ch == '_' || ch == '-' || ch == '+';
                if (!ok) { safe = false; break; }
            }
            if (!safe) continue;
            if (strncmp(hdr, "./", 2) == 0 || strncmp(hdr, "../", 3) == 0) {
                emit_formatted(codegen, "#include \"%s\"\n", hdr);
            } else {
                emit_formatted(codegen, "#include <%s>\n", hdr);
            }
        }
    }
    emit(codegen, "\n");

    /* Emit struct forward declarations before enums so tagged union
     * payloads can reference struct types by name. */
    {
        int struct_count = codegen->struct_decl_count < MAX_STRUCT_DECLS
                         ? codegen->struct_decl_count : MAX_STRUCT_DECLS;
        AstNode **structs = codegen->struct_decls;
        for (int i = 0; i < struct_count; i++) {
            if (structs[i]->data.struct_decl.is_generic) continue;
            {
                const char *sn = codegen_decl_name(codegen, structs[i],
                                                   structs[i]->data.struct_decl.name);
                emit_formatted(codegen, "typedef struct GrayStruct_%s GrayStruct_%s;\n", sn, sn);
            }
        }
        if (struct_count > 0) emit(codegen, "\n");
    }

    /* Open ErrorCode enum: builtin slots (0..N-1) then every #error_code enum's
     * variants in source order. Emitted before the enum typedefs so a
     * #error_code enum's own typedef can reference these slot #defines. The
     * numbering matches the typechecker's register_error_code_set() pass. */
    {
        emit(codegen, "typedef int64_t GrayErrorCode;\n");
        int slot = 0;
#define GRAY_ERR_EMIT(n) emit_formatted(codegen, "#define GrayErrorCode_%s %d\n", #n, slot++);
        GRAY_ERROR_CODE_BUILTINS(GRAY_ERR_EMIT)
#undef GRAY_ERR_EMIT
        for (int i = 0; i < enum_bucket_count; i++) {
            AstNode *es = enum_bucket[i];
            if (!es->data.enum_decl.is_error_code) continue;
            for (int j = 0; j < es->data.enum_decl.value_count; j++) {
                emit_formatted(codegen, "#define GrayErrorCode_%s %d\n",
                    es->data.enum_decl.values[j].name, slot++);
            }
        }
        emit(codegen, "static inline const char *gray_error_code_name(int64_t _c) {\n");
        emit(codegen, "    switch (_c) {\n");
        slot = 0;
#define GRAY_ERR_CASE(n) emit_formatted(codegen, "        case %d: return \"%s\";\n", slot++, #n);
        GRAY_ERROR_CODE_BUILTINS(GRAY_ERR_CASE)
#undef GRAY_ERR_CASE
        for (int i = 0; i < enum_bucket_count; i++) {
            AstNode *es = enum_bucket[i];
            if (!es->data.enum_decl.is_error_code) continue;
            for (int j = 0; j < es->data.enum_decl.value_count; j++) {
                emit_formatted(codegen, "        case %d: return \"%s\";\n",
                    slot++, es->data.enum_decl.values[j].name);
            }
        }
        emit(codegen, "        default: return \"Unknown\";\n    }\n}\n\n");
    }

    /* Enums a stdlib module exposes (io.OpenFlag, os.Platform): a plain C enum
     * typedef, emitted only when the owning module is imported. Variant value is
     * its position — matches the typechecker's stdlib_enum_map. */
    {
        static const struct {
            const char *name; const char *mod;
            const char *variants[6]; int count;
        } cg_stdlib_enums[] = {
            {"OpenFlag", "io", {"O_RDONLY", "O_WRONLY", "O_RDWR"}, 3},
            {"Platform", "os", {"MAC_OS", "LINUX", "WINDOWS", "OTHER"}, 4},
            {NULL, NULL, {NULL}, 0}
        };
        for (int i = 0; cg_stdlib_enums[i].name; i++) {
            if (!has_stdlib_module(stdlib_imports, stdlib_import_count, cg_stdlib_enums[i].mod))
                continue;
            emit(codegen, "typedef enum {\n");
            for (int j = 0; j < cg_stdlib_enums[i].count; j++) {
                emit_formatted(codegen, "    GrayEnum_%s_%s = %d,\n",
                    cg_stdlib_enums[i].name, cg_stdlib_enums[i].variants[j], j);
            }
            emit_formatted(codegen, "} GrayEnum_%s;\n\n", cg_stdlib_enums[i].name);
        }
    }

    /* Register all enums and emit non-tagged enum typedefs.
     * Tagged enum typedefs are deferred until after struct body
     * definitions because their payloads may contain struct values. */
    for (int i = 0; i < enum_bucket_count; i++) {
        AstNode *stmt = enum_bucket[i];
        /* Emit and register this enum under the name its module gives it.
         * The typedef, the variant constants, and the registry all read this
         * field, and they have to agree with what a reference resolves to.
         * Codegen is the last phase and owns the AST from here, so the
         * resolved name is written back rather than swapped per read. */
        stmt->data.enum_decl.name =
            codegen_decl_name(codegen, stmt, stmt->data.enum_decl.name);
        /* Check if this is a string enum (auto-detect from values) */
            bool is_string_enum = false;
            for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
                if (stmt->data.enum_decl.values[j].value &&
                    stmt->data.enum_decl.values[j].value->kind == NODE_STRING_VALUE) {
                    is_string_enum = true;
                    break;
                }
            }

            /* Register enum name and string flag */
            bool is_tagged = stmt->data.enum_decl.is_tagged;
            if (codegen->enum_count >= codegen->enum_cap) {
                codegen->enum_cap = codegen->enum_cap ? codegen->enum_cap * 2 : 8;
                codegen->enum_names = xrealloc(codegen->enum_names, sizeof(const char *) * codegen->enum_cap);
                codegen->enum_is_string = xrealloc(codegen->enum_is_string, sizeof(bool) * codegen->enum_cap);
                codegen->enum_is_tagged = xrealloc(codegen->enum_is_tagged, sizeof(bool) * codegen->enum_cap);
                codegen->enum_decls = xrealloc(codegen->enum_decls, sizeof(AstNode *) * codegen->enum_cap);
            }
            codegen->enum_names[codegen->enum_count] = stmt->data.enum_decl.name;
            codegen->enum_is_string[codegen->enum_count] = is_string_enum;
            codegen->enum_is_tagged[codegen->enum_count] = is_tagged;
            codegen->enum_decls[codegen->enum_count] = stmt;
            codegen->enum_count++;

            if (is_string_enum) {
                emit_formatted(codegen, "typedef GrayString GrayEnum_%s;\n", stmt->data.enum_decl.name);
                for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
                    EnumVal *ev = &stmt->data.enum_decl.values[j];
                    const char *str_val = ev->name;
                    if (ev->value && ev->value->kind == NODE_STRING_VALUE) {
                        str_val = ev->value->data.string_value.value;
                    }
                    emit_formatted(codegen, "#define GrayEnum_%s_%s ((GrayString){ \"%s\", %d })\n",
                        stmt->data.enum_decl.name, ev->name,
                        str_val, (int)strlen(str_val));
                }
                emit(codegen, "\n");
            } else if (is_tagged) {
                /* Defer tagged enum typedefs — only emit the tag enum now */
                const char *ename = codegen_decl_name(codegen, stmt, stmt->data.enum_decl.name);
                emit_formatted(codegen, "typedef enum {\n");
                for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
                    emit_formatted(codegen, "    GrayEnum_%s_TAG_%s = %d,\n", ename, stmt->data.enum_decl.values[j].name, j);
                }
                emit_formatted(codegen, "} GrayEnum_%s_Tag;\n\n", ename);
            } else {
                bool is_flags = stmt->data.enum_decl.is_flags;
                bool is_error_code = stmt->data.enum_decl.is_error_code;
                emit_formatted(codegen, "typedef enum {\n");
                for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
                    EnumVal *ev = &stmt->data.enum_decl.values[j];
                    emit_formatted(codegen, "    GrayEnum_%s_%s", stmt->data.enum_decl.name, ev->name);
                    if (is_error_code) {
                        /* Variant value is its global ErrorCode slot, defined
                         * once in the ErrorCode preamble. */
                        emit_formatted(codegen, " = GrayErrorCode_%s", ev->name);
                    } else if (ev->value) {
                        emit(codegen, " = ");
                        emit_expression(codegen, ev->value);
                    } else if (is_flags) {
                        emit_formatted(codegen, " = %lldLL", 1LL << j);
                    }
                    /* Non-flags without explicit value: omit `= N` and
                     * let C's enum auto-increment continue from the
                     * last explicit value ). The old code emitted
                     * `= j` (0-based position) which ignored preceding
                     * explicit values entirely. */
                    emit(codegen, ",\n");
                }
                emit_formatted(codegen, "} GrayEnum_%s;\n\n",
                    codegen_decl_name(codegen, stmt, stmt->data.enum_decl.name));
            }
    }

    /* Sort enum arrays for O(log n) bsearch in enum lookup functions */
    for (int i = 1; i < codegen->enum_count; i++) {
        const char *kn = codegen->enum_names[i];
        bool ks = codegen->enum_is_string[i];
        bool kt = codegen->enum_is_tagged[i];
        AstNode *kd = codegen->enum_decls[i];
        int j = i - 1;
        while (j >= 0 && strcmp(codegen->enum_names[j], kn) > 0) {
            codegen->enum_names[j+1] = codegen->enum_names[j];
            codegen->enum_is_string[j+1] = codegen->enum_is_string[j];
            codegen->enum_is_tagged[j+1] = codegen->enum_is_tagged[j];
            codegen->enum_decls[j+1] = codegen->enum_decls[j];
            j--;
        }
        codegen->enum_names[j+1] = kn;
        codegen->enum_is_string[j+1] = ks;
        codegen->enum_is_tagged[j+1] = kt;
        codegen->enum_decls[j+1] = kd;
    }

    /* Emit struct declarations in dependency order (topological sort).
     * Structs that reference other structs as value fields must come after them.
     * Forward declarations were already emitted above (before enums). */
    {
        int struct_count = codegen->struct_decl_count < MAX_STRUCT_DECLS
                         ? codegen->struct_decl_count : MAX_STRUCT_DECLS;
        AstNode **structs = codegen->struct_decls;

        /* Simple topological sort: repeatedly emit structs with no unresolved deps */
        bool emitted[MAX_STRUCT_DECLS] = {false};
        int emit_count = 0;
        for (int pass = 0; pass < struct_count && emit_count < struct_count; pass++) {
            for (int i = 0; i < struct_count; i++) {
                if (emitted[i]) continue;
                AstNode *struct_node = structs[i];
                bool deps_met = true;
                /* A field names its type as written in its own module's file,
                 * while a declaration is known by its C name. Resolve both
                 * sides before comparing: a bare `Inner` written inside module
                 * lib is the declaration named lib_Inner. Comparing the two
                 * spellings directly finds no dependency, and the struct is
                 * emitted before the one it holds by value. */
                codegen_enter_node(codegen, struct_node);
                for (int j = 0; j < struct_node->data.struct_decl.field_count; j++) {
                    const char *field_type = struct_node->data.struct_decl.fields[j].type_name;
                    if (!field_type) continue;
                    /* Only a by-value field constrains the order; a pointer,
                     * array or map field is satisfied by the forward
                     * declaration already emitted above. */
                    if (field_type[0] == '^' || field_type[0] == '[' ||
                        strncmp(field_type, "map[", 4) == 0) continue;
                    const char *dep = codegen_resolve_type(codegen, field_type);
                    /* Check if this field type is another user struct */
                    for (int k = 0; k < struct_count; k++) {
                        if (k != i && !emitted[k] &&
                            strcmp(codegen_decl_name(codegen, structs[k],
                                                     structs[k]->data.struct_decl.name),
                                   dep) == 0) {
                            deps_met = false;
                            break;
                        }
                    }
                    if (!deps_met) break;
                }
                if (deps_met) {
                    emitted[i] = true;
                    emit_count++;
                    /* : skip generic structs here; they're
                     * emitted per-instantiation below. */
                    if (struct_node->data.struct_decl.is_generic) continue;
                    /* Field types written bare name this struct's module. */
                    codegen_enter_node(codegen, struct_node);
                    emit_formatted(codegen, "struct GrayStruct_%s {\n",
                        codegen_decl_name(codegen, struct_node, struct_node->data.struct_decl.name));
                    for (int j = 0; j < struct_node->data.struct_decl.field_count; j++) {
                        StructField *field = &struct_node->data.struct_decl.fields[j];
                        emit_formatted(codegen, "    %s %s;\n", gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name));
                    }
                    emit(codegen, "};\n\n");
                }
            }
        }
        /* If any structs couldn't be emitted (circular deps), emit them anyway */
        for (int i = 0; i < struct_count; i++) {
            if (!emitted[i]) {
                AstNode *struct_node = structs[i];
                codegen_enter_node(codegen, struct_node);
                emit_formatted(codegen, "struct GrayStruct_%s {\n",
                    codegen_decl_name(codegen, struct_node, struct_node->data.struct_decl.name));
                for (int j = 0; j < struct_node->data.struct_decl.field_count; j++) {
                    StructField *field = &struct_node->data.struct_decl.fields[j];
                    emit_formatted(codegen, "    %s %s;\n", gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name));
                }
                emit(codegen, "};\n\n");
            }
        }
    }

    /* Emit deferred tagged enum typedefs now that full struct
     * definitions are available for by-value payload fields. */

    /* Forward-declare all tagged enum types so self-referential
     * pointer payloads (e.g. ^Expr inside Expr) can resolve. */
    for (int i = 0; i < enum_bucket_count; i++) {
        AstNode *stmt = enum_bucket[i];
        if (!stmt->data.enum_decl.is_tagged) continue;
        const char *ename = codegen_decl_name(codegen, stmt, stmt->data.enum_decl.name);
        emit_formatted(codegen, "typedef struct GrayEnum_%s GrayEnum_%s;\n", ename, ename);
    }

    for (int i = 0; i < enum_bucket_count; i++) {
        AstNode *stmt = enum_bucket[i];
        if (!stmt->data.enum_decl.is_tagged) continue;
        const char *ename = codegen_decl_name(codegen, stmt, stmt->data.enum_decl.name);

        /* Payload structs (only for variants with payloads) */
        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            EnumVal *ev = &stmt->data.enum_decl.values[j];
            if (ev->payload_count > 0) {
                emit_formatted(codegen, "typedef struct {");
                for (int k = 0; k < ev->payload_count; k++) {
                    if (k > 0) emit(codegen, "");
                    emit_formatted(codegen, " %s _%d;", gray_type_to_c_codegen(codegen, ev->payload_types[k]), k);
                }
                emit_formatted(codegen, " } GrayEnum_%s_Data_%s;\n", ename, ev->name);
            }
        }

        /* Tagged union struct (matches forward declaration above) */
        emit_formatted(codegen, "struct GrayEnum_%s {\n", ename);
        emit_formatted(codegen, "    GrayEnum_%s_Tag tag;\n", ename);
        bool has_any_payload = false;
        for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
            if (stmt->data.enum_decl.values[j].payload_count > 0) { has_any_payload = true; break; }
        }
        if (has_any_payload) {
            emit_formatted(codegen, "    union {\n");
            for (int j = 0; j < stmt->data.enum_decl.value_count; j++) {
                EnumVal *ev = &stmt->data.enum_decl.values[j];
                if (ev->payload_count > 0) {
                    emit_formatted(codegen, "        GrayEnum_%s_Data_%s %s;\n", ename, ev->name, ev->name);
                }
            }
            emit_formatted(codegen, "    } data;\n");
        }
        emit_formatted(codegen, "};\n\n");
    }

    /* : emit per-instantiation typedefs for generic (wildcard) structs.
     * For each recorded binding, substitute ? → concrete in field types
     * and emit under a mangled name (e.g. GrayStruct_Pair__int). */
    for (int i = 0; i < codegen->struct_decl_count; i++) {
        AstNode *stmt = codegen->struct_decls[i];
        if (!stmt->data.struct_decl.is_generic) continue;
        for (int inst_index = 0; inst_index < stmt->data.struct_decl.instantiation_count; inst_index++) {
            const char *concrete = stmt->data.struct_decl.instantiations[inst_index];
            char mangled[MSG_BUF_SIZE];
            mangle_generic_name(mangled, sizeof(mangled), stmt->data.struct_decl.name, concrete);
            /* Forward declaration */
            emit_formatted(codegen, "typedef struct GrayStruct_%s GrayStruct_%s;\n", mangled, mangled);
            emit_formatted(codegen, "struct GrayStruct_%s {\n", mangled);
            const char *saved = codegen->wildcard_binding;
            codegen->wildcard_binding = concrete;
            for (int j = 0; j < stmt->data.struct_decl.field_count; j++) {
                StructField *field = &stmt->data.struct_decl.fields[j];
                emit_formatted(codegen, "    %s %s;\n", gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name));
            }
            codegen->wildcard_binding = saved;
            emit(codegen, "};\n\n");
        }
    }

    /* : emit JSON parse/stringify helpers for #json structs. Each
     * #json struct gets two static functions:
     *   - gray_json_parse_<Name>(arena, json_string) → GrayStruct_<Name>
     *   - gray_json_stringify_<Name>(arena, struct_value) → GrayString
     * These are called by json.parse() / json.stringify() which the
     * typechecker dispatches based on the target/argument struct type. */
    for (int i = 0; i < codegen->struct_decl_count; i++) {
        AstNode *stmt = codegen->struct_decls[i];
        if (!stmt->data.struct_decl.is_json) continue;
        const char *struct_name = stmt->data.struct_decl.name;
        int field_count = stmt->data.struct_decl.field_count;

        /* --- parse: JSON string → struct --- */
        emit_formatted(codegen, "static GrayStruct_%s gray_json_parse_%s(GrayArena *arena, GrayString text) {\n", struct_name, struct_name);
        emit_formatted(codegen, "    GrayStruct_%s _r = {0};\n", struct_name);
        emit_formatted(codegen, "    GrayMap _m = gray_json_decode(arena, text);\n");
        for (int j = 0; j < field_count; j++) {
            StructField *field = &stmt->data.struct_decl.fields[j];
            if (strcmp(field->type_name, "string") == 0) {
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", field->name);
                emit_formatted(codegen, "      if (_v) _r.%s = *(GrayString *)_v; }\n", sanitize_name(field->name));
            } else if (strcmp(field->type_name, "int") == 0 || strcmp(field->type_name, "i64") == 0 ||
                       strcmp(field->type_name, "uint") == 0 || strcmp(field->type_name, "u64") == 0) {
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", field->name);
                emit_formatted(codegen, "      if (_v) { GrayString _sv = *(GrayString *)_v; _r.%s = gray_builtin_string_to_int(_sv); } }\n", sanitize_name(field->name));
            } else if (strcmp(field->type_name, "float") == 0 || strcmp(field->type_name, "f64") == 0) {
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", field->name);
                emit_formatted(codegen, "      if (_v) { GrayString _sv = *(GrayString *)_v; _r.%s = gray_builtin_string_to_float(_sv); } }\n", sanitize_name(field->name));
            } else if (strcmp(field->type_name, "bool") == 0) {
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", field->name);
                emit_formatted(codegen, "      if (_v) { GrayString _sv = *(GrayString *)_v; _r.%s = (_sv.len == 4 && memcmp(_sv.data, \"true\", 4) == 0); } }\n", sanitize_name(field->name));
            }
        }
        emit_formatted(codegen, "    return _r;\n}\n\n");

        /* --- stringify: struct → JSON string --- */
        emit_formatted(codegen, "static GrayString gray_json_stringify_%s(GrayArena *arena, GrayStruct_%s _s) {\n", struct_name, struct_name);

        /* Pass 1: compute exact buffer size at runtime.
         * Field names are identifiers (no escaping), so their contribution
         * is compile-time constant.  String values need json_escaped_len().
         * Numeric types use safe upper bounds (21 for int64, 24 for double). */
        {
            /* Compile-time constant part: braces + separators + all key literals */
            int fixed = 2; /* { } */
            for (int j = 0; j < field_count; j++) {
                StructField *field = &stmt->data.struct_decl.fields[j];
                if (j > 0) fixed += 2; /* ", " */
                fixed += 2 + (int)strlen(field->name) + 2; /* "key": */
                /* Value upper bound for non-string types */
                if (strcmp(field->type_name, "int") == 0 || strcmp(field->type_name, "i64") == 0 ||
                    strcmp(field->type_name, "uint") == 0 || strcmp(field->type_name, "u64") == 0) {
                    fixed += 21;
                } else if (strcmp(field->type_name, "float") == 0 || strcmp(field->type_name, "f64") == 0) {
                    fixed += 24;
                } else if (strcmp(field->type_name, "bool") == 0) {
                    fixed += 5;
                }
                /* string fields are added at runtime below */
            }
            emit_formatted(codegen, "    size_t _need = %d;\n", fixed);
        }
        /* Add runtime string field sizes */
        for (int j = 0; j < field_count; j++) {
            StructField *field = &stmt->data.struct_decl.fields[j];
            if (strcmp(field->type_name, "string") == 0) {
                emit_formatted(codegen, "    _need += json_escaped_len(_s.%s);\n", sanitize_name(field->name));
            }
        }

        /* Pass 2: allocate and write */
        emit_formatted(codegen, "    char *_buf = gray_arena_alloc(arena, _need + 1);\n");
        emit_formatted(codegen, "    int _pos = 0;\n");
        emit_formatted(codegen, "    _buf[_pos++] = '{';\n");
        for (int j = 0; j < field_count; j++) {
            StructField *field = &stmt->data.struct_decl.fields[j];
            if (j > 0) emit_formatted(codegen, "    _buf[_pos++] = ','; _buf[_pos++] = ' ';\n");
            /* Key */
            emit_formatted(codegen, "    _buf[_pos++] = '\"';\n");
            int fname_len = (int)strlen(field->name);
            emit_formatted(codegen, "    memcpy(_buf + _pos, \"%s\", %d); _pos += %d;\n",
                field->name, fname_len, fname_len);
            emit_formatted(codegen, "    _buf[_pos++] = '\"'; _buf[_pos++] = ':'; _buf[_pos++] = ' ';\n");
            /* Value */
            if (strcmp(field->type_name, "string") == 0) {
                emit_formatted(codegen, "    json_append_escaped(_buf, &_pos, _s.%s);\n", sanitize_name(field->name));
            } else if (strcmp(field->type_name, "int") == 0 || strcmp(field->type_name, "i64") == 0) {
                emit_formatted(codegen, "    _pos += snprintf(_buf + _pos, _need + 1 - (size_t)_pos, \"%%lld\", (long long)_s.%s);\n",
                    sanitize_name(field->name));
            } else if (strcmp(field->type_name, "uint") == 0 || strcmp(field->type_name, "u64") == 0) {
                emit_formatted(codegen, "    _pos += snprintf(_buf + _pos, _need + 1 - (size_t)_pos, \"%%llu\", (unsigned long long)_s.%s);\n",
                    sanitize_name(field->name));
            } else if (strcmp(field->type_name, "float") == 0 || strcmp(field->type_name, "f64") == 0) {
                emit_formatted(codegen, "    _pos += snprintf(_buf + _pos, _need + 1 - (size_t)_pos, \"%%g\", _s.%s);\n",
                    sanitize_name(field->name));
            } else if (strcmp(field->type_name, "bool") == 0) {
                emit_formatted(codegen, "    { const char *_bv = _s.%s ? \"true\" : \"false\"; int _bl = _s.%s ? 4 : 5;\n",
                    sanitize_name(field->name), sanitize_name(field->name));
                emit_formatted(codegen, "      memcpy(_buf + _pos, _bv, (size_t)_bl); _pos += _bl; }\n");
            }
        }
        emit_formatted(codegen, "    _buf[_pos++] = '}';\n");
        emit_formatted(codegen, "    _buf[_pos] = '\\0';\n");
        emit_formatted(codegen, "    return (GrayString){_buf, (int32_t)_pos};\n");
        emit_formatted(codegen, "}\n\n");

        /* --- parse array: JSON array string → GrayArray of structs --- */
        emit_formatted(codegen, "static GrayArray gray_json_parse_array_%s(GrayArena *arena, GrayString text) {\n", struct_name);
        emit_formatted(codegen, "    GrayArray _elems = gray_json_split_array(arena, text);\n");
        emit_formatted(codegen, "    GrayArray _result = gray_array_new(arena, sizeof(GrayStruct_%s), _elems.len > 0 ? _elems.len : 4);\n", struct_name);
        emit_formatted(codegen, "    for (int32_t _i = 0; _i < _elems.len; _i++) {\n");
        emit_formatted(codegen, "        GrayString _elem_str = *(GrayString *)((char *)_elems.data + (size_t)_i * (size_t)_elems.elem_size);\n");
        emit_formatted(codegen, "        GrayStruct_%s _item = gray_json_parse_%s(arena, _elem_str);\n", struct_name, struct_name);
        emit_formatted(codegen, "        gray_array_push(arena, &_result, &_item, __FILE__, __LINE__);\n");
        emit_formatted(codegen, "    }\n");
        emit_formatted(codegen, "    return _result;\n");
        emit_formatted(codegen, "}\n\n");
    }

    /* (Enum typedefs already emitted above, before struct definitions) */

    /* Collect all function declarations (including struct-namespaced) */
    for (int i = 0; i < func_bucket_count; i++) {
        AstNode *stmt = func_bucket[i];
        stmt->data.func_decl.name =
            codegen_decl_name(codegen, stmt, stmt->data.func_decl.name);
        GROW_ARRAY(codegen->all_funcs, codegen->func_count, codegen->func_cap);
        codegen->all_funcs[codegen->func_count++] = stmt;
    }
    /* The by-name index sorts on func_decl.name, which the loop above just
     * rewrote; anything built before this point is keyed by the old names. */
    codegen->funcs_by_name_built = false;
    /* Collect struct-namespaced functions with prefixed names */
    for (int i = 0; i < codegen->struct_decl_count; i++) {
        AstNode *stmt = codegen->struct_decls[i];
        for (int j = 0; j < stmt->data.struct_decl.func_count; j++) {
            AstNode *function_node = stmt->data.struct_decl.funcs[j].func_decl;
            if (function_node && function_node->kind == NODE_FUNC_DECL) {
                const char *struct_name = stmt->data.struct_decl.name;
                const char *fn_name = function_node->data.func_decl.name;
                size_t struct_name_len = strlen(struct_name);
                size_t fn_len = strlen(fn_name);
                size_t ns_len = struct_name_len + 1 + fn_len + 1;
                char *ns_name = malloc(ns_len);
                snprintf(ns_name, ns_len, "%s_%s", struct_name, fn_name);
                function_node->data.func_decl.name = ns_name;
                GROW_ARRAY(codegen->ns_func_names, codegen->ns_func_name_count,
                    codegen->ns_func_name_cap);
                codegen->ns_func_names[codegen->ns_func_name_count++] = ns_name;

                GROW_ARRAY(codegen->all_funcs, codegen->func_count, codegen->func_cap);
                codegen->all_funcs[codegen->func_count++] = function_node;
            }
        }
    }

    /* Emit multi-return type definitions. Skip generic functions whose
     * return types contain '?'; those need per-instantiation typedefs
     * emitted during monomorphisation ). Use codegen->all_funcs so that
     * struct-namespaced functions (already renamed to StructName_func)
     * are included alongside top-level functions. */
    for (int i = 0; i < codegen->func_count; i++) {
        AstNode *stmt = codegen->all_funcs[i];
        if (stmt->kind == NODE_FUNC_DECL &&
            stmt->data.func_decl.return_type_count > 1) {
            bool has_wc = false;
            for (int return_index = 0; return_index < stmt->data.func_decl.return_type_count; return_index++) {
                if (stmt->data.func_decl.return_types[return_index] &&
                    strchr(stmt->data.func_decl.return_types[return_index], '?')) {
                    has_wc = true;
                    break;
                }
            }
            /* A bare return type resolves against the function's own module,
             * so the typedef's field types must be emitted in that module —
             * otherwise a cross-module enum falls back to an undefined
             * GrayStruct_<Name> instead of GrayEnum_<module>_<Name>. */
            if (!has_wc) {
                codegen_enter_node(codegen, stmt);
                emit_multi_return_typedef(codegen, stmt);
            }
        }
    }

    /* Emit forward declarations for all functions (including struct-namespaced) */
    for (int i = 0; i < codegen->func_count; i++) {
        AstNode *stmt = codegen->all_funcs[i];
        if (stmt->kind != NODE_FUNC_DECL) continue;
        /* A parameter or return type written bare resolves against the module
         * the function was declared in, so the forward declaration has to be
         * emitted in that module — otherwise it disagrees with the definition
         * whenever two modules declare the same type name. */
        codegen_enter_node(codegen, stmt);
        if (strcmp(stmt->data.func_decl.name, "main") == 0) {
            emit(codegen, "static void gray_fn_main(void);\n");
            continue;
        }

        /* Detect wildcard generics ); emit one forward per
         * instantiation under a mangled name, skipping the un-specialised
         * signature which would contain '?' in C. */
        bool has_wc = func_is_generic(stmt);

        int emit_rounds = has_wc ? stmt->data.func_decl.instantiation_count : 1;
        const char *orig_name = stmt->data.func_decl.name;
        for (int round = 0; round < emit_rounds; round++) {
            const char *saved_binding = codegen->wildcard_binding;
            /* mangled is heap-allocated so the AST temporarily points at
             * stable memory while emit_multi_return_typedef / function_return_type
             * read stmt->data.func_decl.name. */
            char *mangled = NULL;
            if (has_wc) {
                mangled = xmalloc(MSG_BUF_SIZE);
                const char *concrete = stmt->data.func_decl.instantiations[round];
                codegen->wildcard_binding = concrete;
                mangle_generic_name(mangled, MSG_BUF_SIZE, orig_name, concrete);
            }
            const char *emit_name = has_wc ? mangled : orig_name;
            /* Temporarily set the func name to the mangled version so
             * function_return_type sees the right name for multi-return
             * structs  + ). */
            if (has_wc) stmt->data.func_decl.name = mangled;
            /* : emit per-instantiation multi-return typedef
             * before the forward declaration that references it. */
            if (has_wc && stmt->data.func_decl.return_type_count > 1) {
                bool has_wc_ret = false;
                for (int return_index = 0; return_index < stmt->data.func_decl.return_type_count; return_index++) {
                    if (stmt->data.func_decl.return_types[return_index] &&
                        strchr(stmt->data.func_decl.return_types[return_index], '?')) {
                        has_wc_ret = true;
                        break;
                    }
                }
                if (has_wc_ret) emit_multi_return_typedef(codegen, stmt);
            }
            emit_formatted(codegen, "static %s ", function_return_type(codegen, stmt));
            if (has_wc) stmt->data.func_decl.name = orig_name;
            emit_formatted(codegen, "gray_fn_%s(",
                has_wc ? emit_name : codegen_decl_name(codegen, stmt, emit_name));
            {
                bool fwd_first = true;
                for (int j = 0; j < stmt->data.func_decl.param_count; j++) {
                    Param *param = &stmt->data.func_decl.params[j];
                    if (param->is_type_param) continue;
                    if (!fwd_first) emit(codegen, ", ");
                    fwd_first = false;
                    if (param->mutable) {
                        emit_formatted(codegen, "%s *", gray_type_to_c_codegen(codegen,param->type_name));
                    } else {
                        emit(codegen, gray_type_to_c_codegen(codegen,param->type_name));
                    }
                }
                if (fwd_first) {
                    emit(codegen, "void");
                }
            }
            emit(codegen, ");\n");
            codegen->wildcard_binding = saved_binding;
            free(mangled);
        }
    }
    emit(codegen, "\n");

    /* Emit global constants/variables first so they're visible to all functions
     * (e.g. when used as default parameter values at a call site). */
    for (int i = 0; i < var_bucket_count; i++) {
        emit_statement(codegen, var_bucket[i]);
    }

    /* Emit remaining top-level statements (functions, enums, structs, etc.).
     * enum/struct/import/using/module are no-ops in emit_statement. */
    for (int i = 0; i < func_bucket_count; i++) {
        emit_statement(codegen, func_bucket[i]);
    }
    for (int i = 0; i < other_bucket_count; i++) {
        emit_statement(codegen, other_bucket[i]);
    }

    /* Emit struct-namespaced function definitions */
    for (int i = 0; i < codegen->struct_decl_count; i++) {
        AstNode *stmt = codegen->struct_decls[i];
        for (int j = 0; j < stmt->data.struct_decl.func_count; j++) {
            AstNode *function_node = stmt->data.struct_decl.funcs[j].func_decl;
            if (function_node && function_node->kind == NODE_FUNC_DECL) {
                emit_statement(codegen, function_node);
            }
        }
    }

    /* Emit C main() */
    emit(codegen, "int main(int argc, char **argv) {\n");
    emit(codegen, "    (void)argc; (void)argv;\n");
    {
        size_t limit = codegen->arena_limit > 0
            ? codegen->arena_limit : (size_t)1073741824;
        emit_formatted(codegen, "    gray_runtime_init(%zuULL);\n", limit);
    }
    emit(codegen, "    gray_os_init(argc, argv);\n");
    /* Initialize file-scope arrays that can't use C static initializers */
    if (codegen->global_init.len > 0) {
        append_string_to_buffer(&codegen->output, codegen->global_init.data);
    }
    if (codegen->test_mode) {
        /* Test runner: call each #test function under the runner's recovery
         * point so a failed assert/panic is recorded, not fatal. */
        emit(codegen, "    gray_test_begin();\n");
        for (int i = 0; i < func_bucket_count; i++) {
            AstNode *fn = func_bucket[i];
            if (fn->kind != NODE_FUNC_DECL || !fn->data.func_decl.is_test) continue;
            const char *name = fn->data.func_decl.original_name
                ? fn->data.func_decl.original_name : fn->data.func_decl.name;
            emit_formatted(codegen,
                "    gray_test_run(\"%s\", gray_fn_%s, \"%s\", %d);\n",
                name, fn->data.func_decl.name, codegen->file, fn->token.line);
        }
        emit(codegen, "    int _gray_test_rc = gray_test_end();\n");
        emit(codegen, "    gray_runtime_shutdown();\n");
        emit(codegen, "    return _gray_test_rc;\n");
        emit(codegen, "}\n");
    } else {
        emit(codegen, "    gray_fn_main();\n");
        emit(codegen, "    gray_runtime_shutdown();\n");
        emit(codegen, "    return 0;\n");
        emit(codegen, "}\n");
    }

    /* Splice the collection headers into the preamble now that body emission
     * has settled which ones are actually used. */
    if (codegen->needs_arrays_h || codegen->needs_maps_h || codegen->needs_strings_h) {
        Buf includes = buffer_create(64);
        if (codegen->needs_arrays_h)
            append_string_to_buffer(&includes, "#include \"arrays.h\"\n");
        if (codegen->needs_maps_h)
            append_string_to_buffer(&includes, "#include \"maps.h\"\n");
        if (codegen->needs_strings_h)
            append_string_to_buffer(&includes, "#include \"strings.h\"\n");

        Buf *out = &codegen->output;
        size_t ins_len = includes.len;
        size_t tail_len = out->len - collection_include_anchor;
        /* Reserve capacity and grow len by ins_len (append then shift). */
        append_bytes_to_buffer(out, includes.data, ins_len);
        memmove(out->data + collection_include_anchor + ins_len,
                out->data + collection_include_anchor, tail_len);
        memcpy(out->data + collection_include_anchor, includes.data, ins_len);

        buffer_destroy(&includes);
    }

    free(enum_bucket);
    free(func_bucket);
    free(var_bucket);
    free(other_bucket);
    #undef BUCKET_PUSH
}

const char *codegen_result(CodeGen *codegen) {
    return buffer_to_string(&codegen->output);
}

void codegen_destroy(CodeGen *codegen) {
    buffer_destroy(&codegen->output);
    buffer_destroy(&codegen->global_init);
    free(codegen->file_owned);
    free(codegen->enum_names);
    free(codegen->enum_is_string);
    free(codegen->enum_is_tagged);
    free(codegen->enum_decls);
    free(codegen->all_funcs);
    free(codegen->funcs_by_name);
    free(codegen->ref_vars);
    free(codegen->raw_vars);
    free(codegen->bigint_var_names);
    free(codegen->bigint_var_types);
    free(codegen->struct_decls);
    free(codegen->func_field_index);
    free(codegen->using_modules);
    free(codegen->type_alias_names);
    free(codegen->type_alias_targets);
    free(codegen->imported_modules);
    free(codegen->c_headers);
    free(codegen->scope_arenas);
    for (int i = 0; i < codegen->iter_guard_count; i++)
        free(codegen->iter_guards[i]);
    free(codegen->iter_guards);
    for (int i = 0; i < codegen->ns_func_name_count; i++)
        free(codegen->ns_func_names[i]);
    free(codegen->ns_func_names);
}
