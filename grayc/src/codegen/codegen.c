/*
 * codegen.c — Walks the typed AST and emits equivalent C source code,
 * handling declarations, control flow, stdlib calls, and memory management.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 *
 * Contributors:
 *  - @sjh9714
 */

#include "codegen.h"
#include "../util/constants.h"
#include "../util/platform.h"
#include "../util/xalloc.h"
#include "../util/reserved.h"
#include "../util/error_code_builtins.h"
#include "../util/error_codes.h"
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
#define FUNCTION_ARENA_SIZE      65536
#define OUTPUT_BUFFER_INITIAL_SIZE   4096
#define MAX_MEMBER_CHAIN     32
#define VARIABLE_NAME_BUFFER_SIZE         64
#define SHORT_VARIABLE_BUFFER_SIZE        32
#define CYCLE_GUARD_DEPTH    64
#define C_HEADER_PATH_BUFFER_SIZE    2048

/* Return the C-syntax string for an operator TokenType. Used when emitting
 * the operator literally into C source code. */
static const char *operator_to_c_string(TokenType operator) {
    switch (operator) {
    case TOKEN_PLUS: return "+";
    case TOKEN_MINUS: return "-";
    case TOKEN_ASTERISK: return "*";
    case TOKEN_SLASH: return "/";
    case TOKEN_PERCENT: return "%";
    case TOKEN_EQUAL: return "==";
    case TOKEN_NOT_EQUAL: return "!=";
    case TOKEN_LESS_THAN: return "<";
    case TOKEN_GREATER_THAN: return ">";
    case TOKEN_LESS_THAN_OR_EQUAL: return "<=";
    case TOKEN_GREATER_THAN_OR_EQUAL: return ">=";
    case TOKEN_AND: return "&&";
    case TOKEN_OR: return "||";
    case TOKEN_BANG: return "!";
    case TOKEN_BIT_AND: return "&";
    case TOKEN_BIT_OR: return "|";
    case TOKEN_BIT_XOR: return "^";
    case TOKEN_BIT_NOT: return "~";
    case TOKEN_BIT_SHIFT_LEFT: return "<<";
    case TOKEN_BIT_SHIFT_RIGHT: return ">>";
    case TOKEN_INCREMENT: return "++";
    case TOKEN_DECREMENT: return "--";
    case TOKEN_ASSIGN: return "=";
    case TOKEN_PLUS_ASSIGN: return "+=";
    case TOKEN_MINUS_ASSIGN: return "-=";
    case TOKEN_ASTERISK_ASSIGN: return "*=";
    case TOKEN_SLASH_ASSIGN: return "/=";
    case TOKEN_PERCENT_ASSIGN: return "%=";
    case TOKEN_CARET: return "^";
    default: return "?";
    }
}

/* Forward declarations */
static void emit_statement(CodeGen *codegen, AstNode *node);
static void reset_line_directive(CodeGen *codegen);
static void emit_expression(CodeGen *codegen, AstNode *node);
static void emit_call_expression(CodeGen *codegen, AstNode *node);
static bool codegen_is_enum(CodeGen *codegen, const char *name);
static bool codegen_enum_is_tagged(CodeGen *codegen, const char *name);
static bool codegen_enum_is_error_code(CodeGen *codegen, const char *name);
static int codegen_enum_index(CodeGen *codegen, const char *name);
static void emit_to_string(CodeGen *codegen, AstNode *argument);
static bool emit_narrowing_cast(CodeGen *codegen, const char *target, AstNode *value, int line);
static AstNode *find_struct_declaration(CodeGen *codegen, const char *name);
static const char *codegen_resolve_type(CodeGen *codegen, const char *written);
static int extract_array_size(const char *type_name);
static const char *extract_array_element_type(const char *type_name);
static void emit_fixed_size_array_initializer(CodeGen *codegen, AstNode *value,
                                       const char *element_type_name, int fixed_size);


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
static bool codegen_module_imported(CodeGen *codegen, const char *module_name) {
    for (int i = 0; i < codegen->using_module_count; i++)
        if (strcmp(codegen->using_modules[i], module_name) == 0) return true;
    for (int i = 0; i < codegen->imported_module_count; i++)
        if (strcmp(codegen->imported_modules[i], module_name) == 0) return true;
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
                                             char *buffer, size_t buffer_length) {
    if (!name || !*name) {
        snprintf(buffer, buffer_length, "unknown");
        return buffer;
    }
    size_t length = strlen(name);
    char inner[MESSAGE_BUFFER_SIZE];
    if (length > 1 && name[0] == '^') {
        snprintf(buffer, buffer_length, "^%s",
                 codegen_written_type_name(codegen, name + 1, inner, sizeof(inner)));
        return buffer;
    }
    if (length > 2 && name[0] == '[' && name[length - 1] == ']') {
        char element_type_name[MESSAGE_BUFFER_SIZE];
        snprintf(element_type_name, sizeof(element_type_name), "%.*s", (int)(length - 2), name + 1);
        snprintf(buffer, buffer_length, "[%s]",
                 codegen_written_type_name(codegen, element_type_name, inner, sizeof(inner)));
        return buffer;
    }
    if (length > 5 && strncmp(name, "map[", 4) == 0 && name[length - 1] == ']') {
        char pair[MESSAGE_BUFFER_SIZE];
        snprintf(pair, sizeof(pair), "%.*s", (int)(length - 5), name + 4);
        char *colon = strchr(pair, ':');
        if (colon) {
            *colon = '\0';
            char lookup_key[MESSAGE_BUFFER_SIZE];
            codegen_written_type_name(codegen, pair, lookup_key, sizeof(lookup_key));
            snprintf(buffer, buffer_length, "map[%s:%s]", lookup_key,
                     codegen_written_type_name(codegen, colon + 1, inner, sizeof(inner)));
            return buffer;
        }
    }
    DeclarationEntry *entry = codegen->modules
        ? module_table_find_mangled(codegen->modules, name) : NULL;
    snprintf(buffer, buffer_length, "%s", entry && entry->name ? entry->name : name);
    return buffer;
}

static const char *codegen_resolve_declaration(CodeGen *codegen, const char *written) {
    if (!codegen || !codegen->modules || !written) return written;
    ResolveScope scope = codegen_scope(codegen);
    DeclarationEntry *entry = module_resolve_written(codegen->modules, &scope, written);
    return entry ? module_mangle(codegen->modules, entry) : written;
}

/* The declaration a reference node names: the type checker's cached answer
 * when it left one, otherwise resolved here. */
static const char *codegen_resolve_reference(CodeGen *codegen, AstNode *node,
                                       const char *written) {
    if (node && node->resolved_declaration)
        return module_mangle(codegen->modules, node->resolved_declaration);
    return codegen_resolve_declaration(codegen, written);
}

/* The spelling a bare name is emitted under. A local, parameter, loop
 * variable or pattern binding is emitted as written: it hides a same-named
 * module member a `using` brings in, so it must not be resolved as one. */
static const char *codegen_resolve_label(CodeGen *codegen, AstNode *label,
                                         const char *written) {
    if (label->data.label.is_local_reference) return written;
    return codegen_resolve_reference(codegen, label, written);
}

static const char *codegen_declaration_name(CodeGen *codegen, AstNode *node,
                                     const char *fallback) {
    /* While a generic instantiation is being emitted the caller has already
     * put the per-binding mangled name in hand (and on the node). That
     * mangling is by type argument, not by module, and wins. */
    if (codegen->wildcard_binding) return fallback;
    DeclarationEntry *entry = module_table_entry_for_node(codegen->modules, node);
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
    char stack_buffer[256];
    va_list arguments;
    va_start(arguments, format);
    int formatted_length = vsnprintf(stack_buffer, sizeof(stack_buffer), format, arguments);
    va_end(arguments);

    if (formatted_length < 0) return;

    if (formatted_length < (int)sizeof(stack_buffer)) {
        append_bytes_to_buffer(&codegen->output, stack_buffer, (size_t)formatted_length);
        return;
    }

    /* Slow path: formatted string exceeds stack buffer.
     * formatted_length is already the exact required length — no second measure needed. */
    size_t required_size = codegen->output.length + (size_t)formatted_length + 1;
    if (required_size > codegen->output.capacity) {
        size_t new_capacity = codegen->output.capacity * 2;
        if (new_capacity < required_size) new_capacity = required_size;
        codegen->output.data = xrealloc(codegen->output.data, new_capacity);
        codegen->output.capacity = new_capacity;
    }

    va_start(arguments, format);
    vsnprintf(codegen->output.data + codegen->output.length, (size_t)formatted_length + 1, format, arguments);
    va_end(arguments);
    codegen->output.length += (size_t)formatted_length;
}

static void emit_indent(CodeGen *codegen) {
    append_indent_to_buffer(&codegen->output, codegen->indent);
}

/* The C text for a runtime panic call — spliced into an emit_formatted via %s:
 *
 *     emit_formatted(cg, "if (!_dp) { %s; } ", panic_call(cg, node, "P0080", ""));
 *
 * The message comes from the error_codes.h registry (the single source of
 * truth); it is never re-typed at the call site. Any printf conversion in the
 * message is the runtime's, so `c_args` carries the matching C argument
 * expressions with a leading ", " (e.g. ", (long long)_sa"), or "" when the
 * message takes none. `loc` supplies the .gray line reported to the user. */
static const char *panic_call(CodeGen *codegen, const AstNode *location_node,
                              const char *code, const char *c_arguments) {
    const char *message = gray_error_message(code);
    if (!message) message = "runtime panic";  /* unregistered code: still emit valid C */
    const char *file = codegen->file ? codegen->file : "";
    int line = location_node ? location_node->token.line : 0;
    if (!c_arguments) c_arguments = "";
    const char *format = "gray_panic_code_at(\"%s\", %d, \"%s\", \"%s\"%s)";
    int need = snprintf(NULL, 0, format, file, line, code, message, c_arguments);
    char *buffer = arena_allocate(codegen->modules->arena, (size_t)need + 1);
    snprintf(buffer, (size_t)need + 1, format, file, line, code, message, c_arguments);
    return buffer;
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

static int keyword_compare(const void *key_pointer, const void *element) {
    return strcmp((const char *)key_pointer, *(const char *const *)element);
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
static int integer_type_name_width_rank(const char *type_name) {
    if (!type_name) return 0;
    if (strcmp(type_name, "i8")  == 0 || strcmp(type_name, "u8")   == 0) return 1;
    if (strcmp(type_name, "i16") == 0 || strcmp(type_name, "u16")  == 0) return 2;
    if (strcmp(type_name, "i32") == 0 || strcmp(type_name, "u32")  == 0) return 3;
    if (strcmp(type_name, "i64") == 0 || strcmp(type_name, "u64")  == 0) return 4;
    if (strcmp(type_name, "i128") == 0 || strcmp(type_name, "u128") == 0) return 5;
    if (strcmp(type_name, "i256") == 0 || strcmp(type_name, "u256") == 0) return 6;
    return 0;
}

/* Look up sized-integer bounds for overflow checking.
 * Returns true if the type is a sized integer, populating the out params.
 * For unsigned types, *is_unsigned is set and *min_out is NULL. */
/* min_out/max_out/is_unsigned may each be NULL when the caller wants only
 * the others. min_out stays NULL for unsigned types (their min is 0). */
static bool integer_type_name_bounds(const char *type_name,
                             const char **out_minimum, const char **out_maximum,
                             bool *is_unsigned) {
    const char *minimum_text = NULL, *maximum_text = NULL;
    bool is_unsigned_type = false, known = true;
    if (!type_name) known = false;
    else if (strcmp(type_name, "i8") == 0)  { minimum_text = "-128"; maximum_text = "127"; }
    else if (strcmp(type_name, "i16") == 0) { minimum_text = "-32768"; maximum_text = "32767"; }
    else if (strcmp(type_name, "i32") == 0) { minimum_text = "-2147483648LL"; maximum_text = "2147483647LL"; }
    else if (strcmp(type_name, "u8") == 0) { is_unsigned_type = true; maximum_text = "255"; }
    else if (strcmp(type_name, "u16") == 0) { is_unsigned_type = true; maximum_text = "65535"; }
    else if (strcmp(type_name, "u32") == 0) { is_unsigned_type = true; maximum_text = "4294967295ULL"; }
    else known = false;

    if (out_minimum) *out_minimum = minimum_text;
    if (out_maximum) *out_maximum = maximum_text;
    if (is_unsigned) *is_unsigned = is_unsigned_type;
    return known;
}

/* The bounds arguments of a gray_(u)sized_*_check / gray_(u)cast_check call:
 * `max, "T", "file", line` when unsigned, `min, max, "T", "file", line` when
 * signed. The caller emits the surrounding punctuation. */
static void emit_sized_bounds_arguments(CodeGen *codegen, const char *minimum_text, const char *maximum_text,
                                   bool is_unsigned, const char *type_name, int line) {
    if (is_unsigned)
        emit_formatted(codegen, "%s, \"%s\", \"%s\", %d", maximum_text, type_name, codegen->file, line);
    else
        emit_formatted(codegen, "%s, %s, \"%s\", \"%s\", %d", minimum_text, maximum_text, type_name, codegen->file, line);
}

/* Emit the overflow-checked negation of a signed (non-wide) integer of type
 * int_type: the operand is the expression `operand`, or the C expression
 * `operand_c` when that is non-NULL. */
static void emit_checked_negation(CodeGen *codegen, GrayType *integer_type, AstNode *operand,
                                  const char *operand_c_text, int line) {
    const char *source_minimum = NULL, *source_maximum = NULL;
    bool is_unsigned = false;
    bool sized = integer_type->name && integer_type_name_bounds(integer_type->name, &source_minimum, &source_maximum, &is_unsigned);
    emit(codegen, sized ? "gray_sized_neg_check(" : "gray_neg_check(");
    if (operand_c_text) emit(codegen, operand_c_text);
    else emit_expression(codegen, operand);
    if (sized)
        emit_formatted(codegen, ", %s, %s, \"%s\", \"%s\", %d)", source_minimum, source_maximum, integer_type->name, codegen->file, line);
    else
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, line);
}

/* A char holds a Unicode codepoint, U+0000–U+10FFFF; a conversion to char is
 * range-checked like a cast to an unsigned type with that maximum. */
#define CHAR_CODEPOINT_MAX "1114111"

/* Emit a range-checked narrowing to a sized integer: `check(value, bounds)`.
 * The check is picked by the source type so a u64 or floating-point value is checked
 * as it is rather than after a lossy conversion to int64_t. The value is the
 * expression `operand`, or the C expression `operand_c` when non-NULL. */
static void emit_range_checked_narrowing(CodeGen *codegen, GrayType *source_type,
                                         AstNode *operand, const char *operand_c_text,
                                         const char *minimum_text, const char *maximum_text, bool is_unsigned,
                                         const char *target, int line) {
    const char *source_suffix = "";
    const char *value_cast = "";
    if (source_type && source_type->kind == TYPE_KIND_FLOATING_POINT) {
        source_suffix = "_f64";
        value_cast = "(double)";
    } else if (source_type && source_type->name && strcmp(source_type->name, "u64") == 0) {
        source_suffix = "_u64";
        value_cast = "(uint64_t)";
    }
    emit_formatted(codegen, "%s%s(%s(", is_unsigned ? "gray_ucast_check" : "gray_cast_check",
                   source_suffix, value_cast);
    if (operand_c_text) emit(codegen, operand_c_text);
    else emit_expression(codegen, operand);
    emit(codegen, "), ");
    emit_sized_bounds_arguments(codegen, minimum_text, maximum_text, is_unsigned, target, line);
    emit(codegen, ")");
}

/* `ref += value` for a string target reached through a pointer, whose C
 * reference string is ref_str (e.g. "*_dp", "_dp->field"): ref =
 * gray_string_concat(arena, ref, value). Returns false for any other target
 * or operator. */
static bool emit_string_append_through(CodeGen *codegen, AstNode *node, const char *reference_text) {
    if (node->data.assign.operator != TOKEN_PLUS_ASSIGN) return false;
    GrayType *target_type = type_table_get(codegen->type_table, node->data.assign.target);
    if (!target_type || target_type->kind != TYPE_KIND_STRING) return false;
    emit_formatted(codegen, "%s = gray_string_concat(gray_default_arena, %s, ", reference_text, reference_text);
    emit_expression(codegen, node->data.assign.value);
    emit(codegen, ")");
    return true;
}

static const char *sanitize_name(const char *name) {
    if (!name || !is_c_keyword(name)) return name;
    static char buffers[4][MESSAGE_BUFFER_SIZE];
    static int slot_index = 0;
    int i = slot_index++ & 3;
    snprintf(buffers[i], sizeof(buffers[i]), "_gray_%s", name);
    return buffers[i];
}

/* C name for a file-scope variable declared in the entry module. The
 * gray_g_ prefix keeps a name like `log`, `index`, or `getline` from
 * colliding with a libc identifier pulled in by the runtime headers —
 * a collision C rejects with no Grayscale diagnostic. Locals, parameters,
 * and struct fields legally shadow such names and are left untouched.
 * Interned in the module arena so the pointer stays valid when it is
 * swapped onto a declaration node. */
static const char *global_variable_c_name(CodeGen *codegen, const char *name) {
    char message[MESSAGE_BUFFER_SIZE];
    int count = snprintf(message, sizeof(message), "gray_g_%s", name);
    return arena_intern_string(codegen->modules->arena, message,
                               count > 0 ? (size_t)count : 0);
}

/* True when a bare reference resolves to an entry-module file-scope
 * variable, i.e. one emitted through global_variable_c_name(). */
static bool label_is_entry_global(AstNode *node) {
    if (!node || node->kind != NODE_LABEL ||
        !node->data.label.is_file_global_reference)
        return false;
    return !node->resolved_declaration || node->resolved_declaration->kind == DECLARATION_CONST;
}

/* The variable a place expression is rooted at: field and index chains
 * stripped away. A module-qualified variable (`mod.v`) is a root itself. A dereference stops the walk — what a pointer addresses
 * is not the variable's own storage. */
static AstNode *place_root(AstNode *expression) {
    for (;;) {
        if (expression->kind == NODE_MEMBER_EXPRESSION && expression->resolved_declaration &&
            expression->resolved_declaration->kind == DECLARATION_CONST) return expression;
        if (expression->kind == NODE_MEMBER_EXPRESSION) expression = expression->data.member.object;
        else if (expression->kind == NODE_INDEX_EXPRESSION) expression = expression->data.index_expression.left;
        else return expression;
    }
}

/* True when `expr` is a place inside a module-level variable that holds
 * allocated storage (a string, array, map or struct). Whatever is stored
 * into one must outlive every function scope, so it cannot be allocated
 * in an arena a function's return rewinds. */
static bool place_is_module_storage(CodeGen *codegen, AstNode *expression) {
    AstNode *root = place_root(expression);
    if (root->kind != NODE_LABEL && root->kind != NODE_MEMBER_EXPRESSION) return false;
    bool module_level = (root->kind == NODE_LABEL && root->data.label.is_file_global_reference) ||
        (root->resolved_declaration && root->resolved_declaration->kind == DECLARATION_CONST);
    if (!module_level) return false;
    GrayType *root_type = type_table_get(codegen->type_table, root);
    return root_type && (root_type->kind == TYPE_KIND_STRING || root_type->kind == TYPE_KIND_ARRAY ||
                      root_type->kind == TYPE_KIND_MAP || root_type->kind == TYPE_KIND_STRUCT);
}

/* True when a statement stores into module-level storage: it assigns to a
 * place inside one, or passes one to a call that may mutate it. */
static bool statement_stores_into_module_storage(CodeGen *codegen, AstNode *statement) {
    if (statement->kind == NODE_ASSIGN_STATEMENT)
        return place_is_module_storage(codegen, statement->data.assign.target);
    if (statement->kind == NODE_EXPRESSION_STATEMENT && statement->data.expression_statement.expression &&
        statement->data.expression_statement.expression->kind == NODE_CALL_EXPRESSION) {
        AstNode *call = statement->data.expression_statement.expression;
        for (int i = 0; i < call->data.call.argument_count; i++) {
            if (place_is_module_storage(codegen, call->data.call.arguments[i])) return true;
        }
    }
    return false;
}

/* Build a mangled name for a generic instantiation: `base__concrete`
 * with non-alphanumeric characters replaced by underscores so
 * array/map bindings stay legal C identifiers. */
static void mangle_generic_name(char *buffer, size_t buffer_size, const char *base, const char *concrete) {
    size_t position = (size_t)snprintf(buffer, buffer_size, "%s__", base);
    for (const char *cursor = concrete; *cursor && position < buffer_size - 1; cursor++) {
        buffer[position++] = (isalnum((unsigned char)*cursor) || *cursor == '_') ? *cursor : '_';
    }
    buffer[position] = '\0';
}

/* Returns true if the function declaration contains any wildcard ('?')
 * type parameters or return types, indicating a generic function. */
static bool function_is_generic(AstNode *function_node) {
    for (int i = 0; i < function_node->data.function_declaration.parameter_count; i++) {
        if (function_node->data.function_declaration.parameters[i].type_name &&
            strchr(function_node->data.function_declaration.parameters[i].type_name, '?')) return true;
    }
    for (int i = 0; i < function_node->data.function_declaration.return_type_count; i++) {
        if (function_node->data.function_declaration.return_types[i] &&
            strchr(function_node->data.function_declaration.return_types[i], '?')) return true;
    }
    return false;
}

/* Map Grayscale type name to C type */
/* Return a type string with any '?' replaced by the active wildcard
 * binding. Returns the original pointer if no binding is active or the
 * string has no wildcard. The substituted string lives in a small ring
 * of static buffers so a handful of nested calls can each keep their
 * result alive simultaneously. */
static const char *multi_return_base_name(const char *function_name);
static const char *multi_return_name(AstNode *function_node);
static const char *codegen_effective_type_string(CodeGen *codegen, const char *type_name) {
    if (!type_name || !codegen || !codegen->wildcard_binding) return type_name;
    if (!strchr(type_name, '?')) return type_name;
    size_t binding_length = strlen(codegen->wildcard_binding);
    static char buffers[4][TYPE_NAME_MAX];
    static int slot = 0;
    char *output = buffers[slot];
    slot = (slot + 1) & 3;
    char *write_cursor = output;
    char *end_cursor = output + TYPE_NAME_MAX - 1;
    for (const char *read_cursor = type_name; *read_cursor && write_cursor < end_cursor; read_cursor++) {
        if (*read_cursor == '?') {
            size_t avail = (size_t)(end_cursor - write_cursor);
            size_t copy = binding_length < avail ? binding_length : avail;
            memcpy(write_cursor, codegen->wildcard_binding, copy);
            write_cursor += copy;
        } else {
            *write_cursor++ = *read_cursor;
        }
    }
    *write_cursor = '\0';
    return output;
}

/* Recursively derive what '?' binds to given a param type pattern (ptn)
 * containing '?' and a concrete argument type name (atn).
 * Returns malloc'd binding string (caller must free) or NULL on mismatch. */
static char *codegen_bind_wildcard(const char *parameter_type_name, const char *argument_type_name) {
    if (!parameter_type_name || !argument_type_name || !strchr(parameter_type_name, '?')) return NULL;
    if (strcmp(parameter_type_name, "?") == 0) return strdup(argument_type_name);
    size_t parameter_type_length = strlen(parameter_type_name);
    size_t argument_type_length = strlen(argument_type_name);
    /* Array layer: strip matching outer [...] brackets */
    if (parameter_type_length >= 3 && parameter_type_name[0] == '[' && parameter_type_name[parameter_type_length - 1] == ']') {
        if (argument_type_length < 3 || argument_type_name[0] != '[' || argument_type_name[argument_type_length - 1] != ']') return NULL;
        char *array_pattern_inner = gray_strndup(parameter_type_name + 1, parameter_type_length - 2);
        char *array_argument_inner = gray_strndup(argument_type_name + 1, argument_type_length - 2);
        char *result = codegen_bind_wildcard(array_pattern_inner, array_argument_inner);
        free(array_pattern_inner); free(array_argument_inner);
        return result;
    }
    /* Map layer: find top-level ':' in both sides and recurse into wildcard slot */
    if (parameter_type_length > 4 && strncmp(parameter_type_name, "map[", 4) == 0 && parameter_type_name[parameter_type_length - 1] == ']') {
        if (argument_type_length <= 4 || strncmp(argument_type_name, "map[", 4) != 0 || argument_type_name[argument_type_length - 1] != ']') return NULL;
        const char *map_pattern_content = parameter_type_name + 4; size_t map_pattern_content_length = parameter_type_length - 5;
        const char *map_argument_content = argument_type_name + 4; size_t map_argument_content_length = argument_type_length - 5;
        int depth = 0; const char *map_pattern_colon = NULL, *map_argument_colon = NULL;
        for (size_t i = 0; i < map_pattern_content_length; i++) {
            if (map_pattern_content[i] == '[') depth++; else if (map_pattern_content[i] == ']') depth--;
            else if (map_pattern_content[i] == ':' && depth == 0) { map_pattern_colon = map_pattern_content + i; break; }
        }
        depth = 0;
        for (size_t i = 0; i < map_argument_content_length; i++) {
            if (map_argument_content[i] == '[') depth++; else if (map_argument_content[i] == ']') depth--;
            else if (map_argument_content[i] == ':' && depth == 0) { map_argument_colon = map_argument_content + i; break; }
        }
        if (!map_pattern_colon || !map_argument_colon) return NULL;
        char *map_pattern_key = gray_strndup(map_pattern_content, (size_t)(map_pattern_colon - map_pattern_content));
        char *map_pattern_value = gray_strndup(map_pattern_colon + 1, map_pattern_content_length - (size_t)(map_pattern_colon - map_pattern_content) - 1);
        char *map_argument_key = gray_strndup(map_argument_content, (size_t)(map_argument_colon - map_argument_content));
        char *map_argument_value = gray_strndup(map_argument_colon + 1, map_argument_content_length - (size_t)(map_argument_colon - map_argument_content) - 1);
        char *result = NULL;
        if (strchr(map_pattern_key, '?')) result = codegen_bind_wildcard(map_pattern_key, map_argument_key);
        if (!result && strchr(map_pattern_value, '?')) result = codegen_bind_wildcard(map_pattern_value, map_argument_value);
        free(map_pattern_key); free(map_pattern_value); free(map_argument_key); free(map_argument_value);
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
            size_t length = strlen(inner) + 2;
            char *buffer = xmalloc(length);
            snprintf(buffer, length, "^%s", inner);
            return buffer;
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
        const char *substituted = codegen_effective_type_string(codegen, type_name);
        const char *saved = codegen->wildcard_binding;
        codegen->wildcard_binding = NULL;
        const char *resolved = gray_type_to_c_codegen(codegen, substituted);
        codegen->wildcard_binding = saved;
        return resolved;
    }

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
    if (strcmp(type_name, "f32") == 0)    return "float";
    if (strcmp(type_name, "f64") == 0)    return "double";
    if (strcmp(type_name, "bool") == 0)   return "bool";
    if (strcmp(type_name, "char") == 0)   return "int32_t";
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
        static char user_opaque_buffer[MESSAGE_BUFFER_SIZE];
        if (find_struct_declaration(codegen, type_name)) {
            snprintf(user_opaque_buffer, sizeof(user_opaque_buffer), "GrayStruct_%s", type_name);
            return user_opaque_buffer;
        }
        if (codegen_is_enum(codegen, type_name)) {
            snprintf(user_opaque_buffer, sizeof(user_opaque_buffer), "GrayEnum_%s", type_name);
            return user_opaque_buffer;
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
        static char pointer_buffers[4][MESSAGE_BUFFER_SIZE];
        static int pointer_buffer_index = 0;
        char *buffer = pointer_buffers[pointer_buffer_index++ & 3];
        const char *pointee = gray_type_to_c_codegen(codegen, type_name + 1);
        snprintf(buffer, MESSAGE_BUFFER_SIZE, "%s *", pointee);
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
        /* Ring buffer: see the identical comment on the user-type branch
         * below — a caller may hold this return value across another call
         * to this function before using it. */
        static char buffers[4][MESSAGE_BUFFER_SIZE];
        static int slot = 0;
        char *buffer = buffers[slot];
        slot = (slot + 1) & 3;
        if (codegen && codegen_is_enum(codegen, resolved)) {
            snprintf(buffer, sizeof(buffers[0]), "GrayEnum_%s", resolved);
        } else {
            snprintf(buffer, sizeof(buffers[0]), "GrayStruct_%s", resolved);
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
        const char *underscore = strrchr(type_name, '_');
        if (underscore && underscore[1] >= 'A' && underscore[1] <= 'Z') is_user_type = true;
    }
    if (is_user_type) {
        /* Ring buffer: a caller may hold this return value across another
         * call to this function before using it (e.g. emitting a for_each
         * element type while the collection expression it iterates is
         * itself emitted next, which needs its own struct name here) — a
         * single shared static buffer let that second call silently
         * overwrite the first result out from under its caller. */
        static char buffers[4][MESSAGE_BUFFER_SIZE];
        static int slot = 0;
        char *buffer = buffers[slot];
        slot = (slot + 1) & 3;
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
        const char *module_underscore = strrchr(resolved, '_');
        if (module_underscore && module_underscore[1] >= 'A' && module_underscore[1] <= 'Z') {
            bool is_known_declaration = codegen &&
                (find_struct_declaration(codegen, resolved) != NULL ||
                 codegen_is_enum(codegen, resolved));
            if (!is_known_declaration) {
                const char *base = module_underscore + 1;
                if (strcmp(base, type_name) != 0) {
                    const char *mapped = gray_type_to_c_codegen(codegen, base);
                    if (mapped != base) return mapped;
                }
            }
        }
        if (codegen && codegen_is_enum(codegen, resolved)) {
            snprintf(buffer, sizeof(buffers[0]), "GrayEnum_%s", resolved);
        } else {
            snprintf(buffer, sizeof(buffers[0]), "GrayStruct_%s", resolved);
        }
        return buffer;
    }

    return type_name;
}

static const char *wide_integer_prefix(const char *type_spelling);

/* Resolve a Grayscale type to its C type for map key/value storage.
 * Uses gray_type_to_c_codegen for struct/array/map types, hardcoded for primitives.
 * Routes the input through codegen_effective_type_string so '?' inside a generic
 * instantiation resolves to the active wildcard binding. */
static const char *gray_map_element_c_type(CodeGen *codegen, const char *gray_type_name) {
    if (!gray_type_name) return "int64_t";
    gray_type_name = codegen_effective_type_string(codegen, gray_type_name);
    /* Func references (bare or typed) are stored as void * in maps, same as
     * in arrays and all other composite types. */
    if (strcmp(gray_type_name, "func") == 0 || strncmp(gray_type_name, "func(", 5) == 0) return "void *";
    /* A tagged enum is a C struct, not an integer; storing one in a map has
     * to use its real type so the element size and the casts on read match. */
    if (codegen && codegen_is_enum(codegen, gray_type_name) &&
        codegen_enum_is_tagged(codegen, gray_type_name))
        return gray_type_to_c_codegen(codegen, gray_type_name);
    /* Wide integers are TYPE_KIND_SIGNED_INTEGER/TYPE_KIND_UNSIGNED_INTEGER in the type system but 16/32-byte
     * structs in C; a map slot must use the struct type so its size and the
     * casts on read match, just like a [i128] array element does. */
    if (is_wide_integer_type_name(gray_type_name)) return wide_integer_prefix(gray_type_name);
    GrayType *type = type_from_name(gray_type_name);
    if (!type) return "int64_t";
    switch (type->kind) {
    /* Honor the annotated width/signedness for sized integers (i8..u64) and
     * f32/f64, just like a scalar or struct field — the slot must match so
     * its size and the casts on read agree. */
    case TYPE_KIND_FLOATING_POINT:   return gray_type_to_c_codegen(codegen, gray_type_name);
    case TYPE_KIND_SIGNED_INTEGER:     return gray_type_to_c_codegen(codegen, gray_type_name);
    case TYPE_KIND_UNSIGNED_INTEGER:    return gray_type_to_c_codegen(codegen, gray_type_name);
    case TYPE_KIND_STRING:  return "GrayString";
    case TYPE_KIND_BOOL:    return "bool";
    case TYPE_KIND_CHAR:    return "int32_t";
    case TYPE_KIND_ARRAY:   return "GrayArray";
    case TYPE_KIND_MAP:     return "GrayMap";
    case TYPE_KIND_STRUCT:  return gray_type_to_c_codegen(codegen, gray_type_name);
    case TYPE_KIND_POINTER: return gray_type_to_c_codegen(codegen, gray_type_name);
    default:         return "int64_t";
    }
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

/* Walk a type for storage that a plain C copy would share with the source.
 * Arrays and maps always qualify; strings only when `count_strings`. */
static bool type_needs_copy_walk(CodeGen *codegen, const char *gray_type_name, bool count_strings) {
    if (!gray_type_name || !*gray_type_name) return false;
    if (gray_type_name[0] == '[') return true;
    if (strncmp(gray_type_name, "map[", 4) == 0) return true;
    if (strcmp(gray_type_name, "string") == 0) return count_strings;
    if (gray_type_name[0] == '^') return false; /* pointers alias; see header comment */
    AstNode *struct_declaration = find_struct_declaration(codegen, gray_type_name);
    if (!struct_declaration) return false;
    /* Cycle detection: if we're already visiting this struct, stop. */
    for (int j = 0; j < type_name_deep_copy_depth; j++) {
        if (strcmp(type_name_deep_copy_visiting[j], gray_type_name) == 0) return false;
    }
    if (type_name_deep_copy_depth < CYCLE_GUARD_DEPTH) type_name_deep_copy_visiting[type_name_deep_copy_depth++] = gray_type_name;
    for (int i = 0; i < struct_declaration->data.struct_declaration.field_count; i++) {
        const char *field_type = struct_declaration->data.struct_declaration.fields[i].type_name;
        if (type_needs_copy_walk(codegen, field_type, count_strings)) { type_name_deep_copy_depth--; return true; }
    }
    type_name_deep_copy_depth--;
    return false;
}

static bool type_needs_deep_copy(CodeGen *codegen, const char *gray_type_name) {
    return type_needs_copy_walk(codegen, gray_type_name, true);
}

/* True for arrays, maps, and structs holding either: values whose C copy
 * shares mutable backing storage with the source. */
static bool type_shares_storage(CodeGen *codegen, const char *gray_type_name) {
    return type_needs_copy_walk(codegen, gray_type_name, false);
}

/* Return a unique integer for temporary variable names, drawn from the
 * codegen-wide counter so every emitter gets a distinct id. */
static int codegen_next_id(CodeGen *codegen) { return codegen->temporary_counter++; }

static void emit_value_deep_copy(CodeGen *codegen, const char *gray_type_name, const char *source_variable);

static void emit_array_deep_copy(CodeGen *codegen, const char *gray_type_name, const char *source_variable) {
    size_t length = gray_type_name ? strlen(gray_type_name) : 0;
    if (length < 3 || gray_type_name[0] != '[' || gray_type_name[length - 1] != ']') {
        emit_formatted(codegen, "gray_array_copy(gray_default_arena, &%s)", source_variable);
        return;
    }

    /* Extract element type name from "[T]" (dropping any ",N" sized tail). */
    char element_type_name[MESSAGE_BUFFER_SIZE];
    size_t element_length = length - 2;
    if (element_length >= sizeof(element_type_name)) element_length = sizeof(element_type_name) - 1;
    memcpy(element_type_name, gray_type_name + 1, element_length);
    element_type_name[element_length] = '\0';
    char *comma = strchr(element_type_name, ',');
    if (comma) *comma = '\0';

    if (!type_needs_deep_copy(codegen, element_type_name)) {
        /* Flat element type; the shallow bulk memcpy in gray_array_copy
         * is already correct. */
        emit_formatted(codegen, "gray_array_copy(gray_default_arena, &%s)", source_variable);
        return;
    }

    /* Element needs its own deep copy. Allocate a fresh outer and walk
     * each slot, recursively deep-copying the element in place. */
    /* Snapshot c_element_type into a local buffer. gray_type_to_c_codegen returns a
     * pointer into a shared static buffer; the recursive
     * emit_value_deep_copy call below also resolves type names (when
     * the element is a struct with a nested struct field) and would
     * clobber that buffer, leaving c_element_type pointing at the inner field's
     * C type by the time we emit the outer cast. */
    char c_element_type_buffer[MESSAGE_BUFFER_SIZE];
    {
        const char *c_element_pointer_type = gray_type_to_c_codegen(codegen, element_type_name);
        snprintf(c_element_type_buffer, sizeof(c_element_type_buffer), "%s", c_element_pointer_type ? c_element_pointer_type : "");
    }
    const char *c_element_type = c_element_type_buffer;
    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen,
        "({ GrayArray _ds%d = %s; "
        "GrayArray _dd%d = GRAY_ARRAY_NEW_OF(gray_default_arena, %s, _ds%d.len); "
        "_dd%d.len = _ds%d.len; "
        "for (int32_t _di%d = 0; _di%d < _ds%d.len; _di%d++) { "
        "%s _de%d = ",
        unique_id, source_variable,
        unique_id, c_element_type, unique_id,
        unique_id, unique_id,
        unique_id, unique_id, unique_id, unique_id,
        c_element_type, unique_id);

    char inner_variable[MESSAGE_BUFFER_SIZE];
    snprintf(inner_variable, sizeof(inner_variable),
        "((%s *)_ds%d.data)[_di%d]", c_element_type, unique_id, unique_id);
    emit_value_deep_copy(codegen, element_type_name, inner_variable);

    emit_formatted(codegen,
        "; ((%s *)_dd%d.data)[_di%d] = _de%d; "
        "} _dd%d; })",
        c_element_type, unique_id, unique_id, unique_id, unique_id);
}

static void emit_map_deep_copy(CodeGen *codegen, const char *gray_type_name, const char *source_variable) {
    /* Parse "map[K:V]" into its two slots. */
    if (!gray_type_name || strncmp(gray_type_name, "map[", 4) != 0) {
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", source_variable);
        return;
    }
    size_t length = strlen(gray_type_name);
    if (length < 7 || gray_type_name[length - 1] != ']') {
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", source_variable);
        return;
    }
    const char *start = gray_type_name + 4;
    const char *colon = strchr(start, ':');
    if (!colon) {
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", source_variable);
        return;
    }
    char key_type_name[TYPE_NAME_MAX];
    char value_type_name[MESSAGE_BUFFER_SIZE];
    size_t key_length = (size_t)(colon - start);
    if (key_length >= sizeof(key_type_name)) key_length = sizeof(key_type_name) - 1;
    memcpy(key_type_name, start, key_length);
    key_type_name[key_length] = '\0';
    size_t value_length = length - 4 - key_length - 1 - 1; /* drop "map[", K, ":", "]" */
    if (value_length >= sizeof(value_type_name)) value_length = sizeof(value_type_name) - 1;
    memcpy(value_type_name, colon + 1, value_length);
    value_type_name[value_length] = '\0';

    if (!type_needs_deep_copy(codegen, value_type_name)) {
        /* Value type is flat; gray_map_copy handles string key deep-copy
         * internally when key_kind == GRAY_MAP_KEY_STRING. */
        emit_formatted(codegen, "gray_map_copy(gray_default_arena, &%s)", source_variable);
        return;
    }

    /* Value type needs recursion. Iterate the source in insertion order,
     * deep-copy each value, and insert into a fresh map. */
    const char *c_key_type = gray_map_element_c_type(codegen, key_type_name);
    const char *c_value_type = gray_map_element_c_type(codegen, value_type_name);
    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen,
        "({ GrayMap _ms%d = %s; "
        "GrayMap _md%d = gray_map_new_kind(gray_default_arena, _ms%d.key_size, _ms%d.value_size, "
        "_ms%d.order_len > 4 ? _ms%d.order_len * 2 : 8, _ms%d.key_kind, _ms%d.value_kind); "
        "for (int32_t _mi%d = 0; _mi%d < _ms%d.order_len; _mi%d++) { "
        "int32_t _mslot%d = _ms%d.order[_mi%d]; if (_mslot%d < 0) continue; "
        "%s _mk%d = *(%s *)gray_map_key_at(&_ms%d, _mslot%d); "
        "%s _mvs%d = *(%s *)gray_map_value_at(&_ms%d, _mslot%d); "
        "%s _mvd%d = ",
        unique_id, source_variable,
        unique_id, unique_id, unique_id, unique_id, unique_id, unique_id, unique_id,
        unique_id, unique_id, unique_id, unique_id,
        unique_id, unique_id, unique_id, unique_id,
        c_key_type, unique_id, c_key_type, unique_id, unique_id,
        c_value_type, unique_id, c_value_type, unique_id, unique_id,
        c_value_type, unique_id);

    char source_value_variable[VARIABLE_NAME_BUFFER_SIZE];
    snprintf(source_value_variable, sizeof(source_value_variable), "_mvs%d", unique_id);
    emit_value_deep_copy(codegen, value_type_name, source_value_variable);

    /* String keys store a pointer into the source arena — copy the data
     * before inserting so the destination map owns its key strings. */
    if (strcmp(key_type_name, "string") == 0) {
        emit_formatted(codegen,
            "; _mk%d = gray_string_new(gray_default_arena, _mk%d.data, _mk%d.len); "
            "gray_map_set(gray_default_arena, &_md%d, &_mk%d, &_mvd%d, __FILE__, __LINE__); "
            "} _md%d; })",
            unique_id, unique_id, unique_id, unique_id, unique_id, unique_id, unique_id);
    } else {
        emit_formatted(codegen,
            "; gray_map_set(gray_default_arena, &_md%d, &_mk%d, &_mvd%d, __FILE__, __LINE__); "
            "} _md%d; })",
            unique_id, unique_id, unique_id, unique_id);
    }
}

/* Cycle guard for emit_struct_deep_copy: prevents infinite recursion when
 * struct types reference each other in a cycle (e.g. A has [B], B has A). */
static const char *emit_struct_deep_copy_visiting[CYCLE_GUARD_DEPTH];
static int emit_struct_deep_copy_depth = 0;

static void emit_struct_deep_copy(CodeGen *codegen, const char *struct_type_name, const char *source_variable) {
    AstNode *struct_declaration = find_struct_declaration(codegen, struct_type_name);
    if (!struct_declaration) {
        /* No decl info; bitwise copy is the best we can do. */
        emit_formatted(codegen, "%s", source_variable);
        return;
    }
    /* Cycle detection: if already emitting a deep copy for this struct type,
     * fall back to a shallow (bitwise) copy to break the cycle. */
    for (int j = 0; j < emit_struct_deep_copy_depth; j++) {
        if (strcmp(emit_struct_deep_copy_visiting[j], struct_type_name) == 0) {
            emit_formatted(codegen, "%s", source_variable);
            return;
        }
    }
    if (emit_struct_deep_copy_depth < CYCLE_GUARD_DEPTH) emit_struct_deep_copy_visiting[emit_struct_deep_copy_depth++] = struct_type_name;
    const char *c_struct = gray_type_to_c_codegen(codegen, struct_type_name);
    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen,
        "({ %s _ss%d = %s; %s _sd%d = _ss%d; ",
        c_struct, unique_id, source_variable, c_struct, unique_id, unique_id);
    for (int i = 0; i < struct_declaration->data.struct_declaration.field_count; i++) {
        StructField *field = &struct_declaration->data.struct_declaration.fields[i];
        if (!field->type_name || !field->name) continue;
        if (!type_needs_deep_copy(codegen, field->type_name)) continue;
        char source_field[MESSAGE_BUFFER_SIZE];
        snprintf(source_field, sizeof(source_field), "_ss%d.%s", unique_id, field->name);
        emit_formatted(codegen, "_sd%d.%s = ", unique_id, field->name);
        emit_value_deep_copy(codegen, field->type_name, source_field);
        emit(codegen, "; ");
    }
    emit_formatted(codegen, "_sd%d; })", unique_id);
    emit_struct_deep_copy_depth--;
}

static void emit_value_deep_copy(CodeGen *codegen, const char *gray_type_name, const char *source_variable) {
    if (!type_needs_deep_copy(codegen, gray_type_name)) {
        /* Primitive / pointer / scalar struct; C value copy is correct. */
        emit_formatted(codegen, "%s", source_variable);
        return;
    }
    if (strcmp(gray_type_name, "string") == 0) {
        emit_formatted(codegen, "gray_string_new(gray_default_arena, %s.data, %s.len)", source_variable, source_variable);
        return;
    }
    if (gray_type_name[0] == '[') {
        emit_array_deep_copy(codegen, gray_type_name, source_variable);
        return;
    }
    if (strncmp(gray_type_name, "map[", 4) == 0) {
        emit_map_deep_copy(codegen, gray_type_name, source_variable);
        return;
    }
    /* Must be a struct that needs recursion. */
    emit_struct_deep_copy(codegen, gray_type_name, source_variable);
}

/* Entry point used by the three sites that hold an AstNode for the
 * source array (copy() builtin, var_decl copy-on-assign, assignment
 * copy-on-assign). Evaluates the AstNode once into a temp, then hands
 * the temp name to emit_value_deep_copy with a reconstructed "[elem]"
 * type string. */
static void emit_deep_array_copy(CodeGen *codegen, AstNode *source_node, const char *element_type_name) {
    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen, "({ GrayArray _dtop%d = ", unique_id);
    emit_expression(codegen, source_node);
    emit(codegen, "; ");
    char source_variable[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(source_variable, sizeof(source_variable), "_dtop%d", unique_id);
    char full_type_name[MESSAGE_BUFFER_SIZE];
    snprintf(full_type_name, sizeof(full_type_name), "[%s]", element_type_name ? element_type_name : "");
    emit_value_deep_copy(codegen, full_type_name, source_variable);
    emit(codegen, "; })");
}

/* True when `value` names storage that already exists — a variable, an
 * element, a field, or a pointer's referent — so binding it to a second home
 * needs a copy. Literals and call results are fresh and need none. */
static bool names_existing_storage(AstNode *value) {
    if (!value) return false;
    switch (value->kind) {
    case NODE_LABEL:
    case NODE_MEMBER_EXPRESSION:
    case NODE_INDEX_EXPRESSION:
        return true;
    case NODE_POSTFIX_EXPRESSION:
        return value->data.postfix.operator == TOKEN_CARET;
    default:
        return false;
    }
}

/* True when moving `value` (of type `gray_tn`) into or out of a container
 * element would leave two homes sharing one backing store. */
static bool composite_value_aliases(CodeGen *codegen, const char *gray_type_name, AstNode *value) {
    return names_existing_storage(value) && type_shares_storage(codegen, gray_type_name);
}

/* Emit `value` as the operand of a composite store or a composite read out of
 * a container: a deep copy when it aliases existing storage, otherwise the
 * expression itself. Inside a scoped arena the copy is allocated on the outer
 * arena, since the destination outlives the block. */
static void emit_composite_operand(CodeGen *codegen, const char *gray_type_name, AstNode *value) {
    if (!composite_value_aliases(codegen, gray_type_name, value)) {
        emit_expression(codegen, value);
        return;
    }
    int unique_id = codegen_next_id(codegen);
    char source_variable[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(source_variable, sizeof(source_variable), "_cv%d", unique_id);
    const char *c_type = gray_type_name[0] == '[' ? "GrayArray"
                       : strncmp(gray_type_name, "map[", 4) == 0 ? "GrayMap"
                       : gray_type_to_c_codegen(codegen, gray_type_name);
    emit_formatted(codegen, "({ %s %s = ", c_type, source_variable);
    emit_expression(codegen, value);
    emit(codegen, "; ");
    if (codegen->loop_scope_depth > 0) {
        emit_formatted(codegen, "GrayArena *_cva%d = gray_default_arena; gray_default_arena = _gray_outer_arena; ", unique_id);
        emit_formatted(codegen, "__auto_type _cvd%d = ", unique_id);
        emit_value_deep_copy(codegen, gray_type_name, source_variable);
        emit_formatted(codegen, "; gray_default_arena = _cva%d; _cvd%d; })", unique_id, unique_id);
        return;
    }
    emit_value_deep_copy(codegen, gray_type_name, source_variable);
    emit(codegen, "; })");
}

/* Resolve an import alias to the actual module name, or return the name
 * itself. The mapping is the type checker's, not a second copy built here. */
static const char *resolve_alias(CodeGen *codegen, const char *name) {
    return module_table_resolve_alias(codegen->modules, name);
}

/* Check if a variable name is a mutable parameter in the current function */
static bool is_reference_variable(CodeGen *codegen, const char *name) {
    for (int i = 0; i < codegen->reference_variable_count; i++) {
        if (strcmp(codegen->reference_variables[i], name) == 0) return true;
    }
    return false;
}

static void register_reference_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->reference_variables, codegen->reference_variable_count, codegen->reference_variable_capacity);
    codegen->reference_variables[codegen->reference_variable_count++] = name;
}

static bool is_raw_variable(CodeGen *codegen, const char *name) {
    /* Search from end: most recent entry for this name wins. */
    for (int i = codegen->raw_variable_count - 1; i >= 0; i--) {
        if (strcmp(codegen->raw_variables[i].name, name) == 0)
            return codegen->raw_variables[i].is_raw;
    }
    return false;
}

static void register_raw_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->raw_variables, codegen->raw_variable_count, codegen->raw_variable_capacity);
    codegen->raw_variables[codegen->raw_variable_count].name = name;
    codegen->raw_variables[codegen->raw_variable_count].is_raw = true;
    codegen->raw_variable_count++;
}

static void unregister_raw_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->raw_variables, codegen->raw_variable_count, codegen->raw_variable_capacity);
    codegen->raw_variables[codegen->raw_variable_count].name = name;
    codegen->raw_variables[codegen->raw_variable_count].is_raw = false;
    codegen->raw_variable_count++;
}

/* Was this variable last assigned the result of new()? Its pointee lives in
 * gray_heap_arena, so replacing a container field through it must also
 * target gray_heap_arena rather than the current function's scoped arena. */
static bool is_heap_variable(CodeGen *codegen, const char *name) {
    for (int i = codegen->heap_variable_count - 1; i >= 0; i--) {
        if (strcmp(codegen->heap_variables[i].name, name) == 0)
            return codegen->heap_variables[i].is_heap;
    }
    return false;
}

static void register_heap_variable(CodeGen *codegen, const char *name, bool is_heap) {
    GROW_ARRAY(codegen->heap_variables, codegen->heap_variable_count, codegen->heap_variable_capacity);
    codegen->heap_variables[codegen->heap_variable_count].name = name;
    codegen->heap_variables[codegen->heap_variable_count].is_heap = is_heap;
    codegen->heap_variable_count++;
}

/* True when value is a direct new(...) expression — the only construct
 * that hands back a pointer into gray_heap_arena. */
static bool is_new_call(AstNode *value) {
    return value && value->kind == NODE_NEW_EXPRESSION;
}

/* --- @mem arena liveness tracking (dereference-time use-after-destroy check) ---
 *
 * The typechecker's pointer checker proves most @mem escape/lifetime hazards
 * at compile time, but it deliberately gives up on an arena reached other
 * than by a plain parameter name (a global, or one read back out of a
 * struct/array/map field written through more than one hop) — see
 * STANDARD 11.7. For exactly that residual gap, codegen inserts a runtime
 * guard: whenever a variable is directly initialized from mem.init()/
 * mem.alloc(), every later dereference of that variable re-checks the
 * originating arena's `destroyed` flag and panics (P0117) instead of
 * silently reading freed memory.
 *
 * This only covers dereferences of the tracked variable itself (not a
 * value later copied out of it into another variable/field/container —
 * that path drops the tracking, same as raw_variables/heap_variables do), and only
 * registers when the arena argument is safe to re-evaluate a second time
 * (see is_stable_arena_expression) — anything else (a call result, an index
 * expression) is left unchecked rather than risk re-running a side effect
 * or reading a different arena than the one actually used. */

/* A variable/field/pointer-deref chain has a stable address and no side
 * effects, so re-emitting it at a later dereference site evaluates to the
 * same arena every time. Mirrors the addr()/raw()/ref() argument rule
 * (STANDARD 3.2, "recurses through member/index chains") minus the dynamic
 * array/map index case, which is deliberately excluded here: an index into
 * a container that could have been reassigned between the mem.init() call
 * and the later dereference is not guaranteed to still name the same
 * arena. */
static bool is_stable_arena_expression(AstNode *expression) {
    if (!expression) return false;
    switch (expression->kind) {
        case NODE_LABEL:
            return true;
        case NODE_MEMBER_EXPRESSION:
            return is_stable_arena_expression(expression->data.member.object);
        case NODE_POSTFIX_EXPRESSION:
            return expression->data.postfix.operator == TOKEN_CARET &&
                   is_stable_arena_expression(expression->data.postfix.left);
        default:
            return false;
    }
}

/* Search from end: most recent entry for this name wins. Returns the arena
 * expression to re-check, or NULL if the variable isn't (currently) a
 * tracked mem-arena pointer. */
static AstNode *is_mem_tracked_variable(CodeGen *codegen, const char *name) {
    for (int i = codegen->mem_variable_count - 1; i >= 0; i--) {
        if (strcmp(codegen->mem_variables[i].name, name) == 0)
            return codegen->mem_variables[i].arena_expression;
    }
    return NULL;
}

static void register_mem_variable(CodeGen *codegen, const char *name, AstNode *arena_expression) {
    GROW_ARRAY(codegen->mem_variables, codegen->mem_variable_count, codegen->mem_variable_capacity);
    codegen->mem_variables[codegen->mem_variable_count].name = name;
    codegen->mem_variables[codegen->mem_variable_count].arena_expression = arena_expression;
    codegen->mem_variable_count++;
}

static void unregister_mem_variable(CodeGen *codegen, const char *name) {
    GROW_ARRAY(codegen->mem_variables, codegen->mem_variable_count, codegen->mem_variable_capacity);
    codegen->mem_variables[codegen->mem_variable_count].name = name;
    codegen->mem_variables[codegen->mem_variable_count].arena_expression = NULL;
    codegen->mem_variable_count++;
}

/* Wrap `ptr_c_expr` with a call to gray_mem_check_live(arena, ptr, file,
 * line) — composable inside a larger cast/dereference expression without
 * breaking its lvalue-ness, the same way gray_ptr_check already is. Emits
 * `gray_mem_check_live(<arena_expr>, (void *)(` and leaves the caller to
 * close with `), file, line)` after emitting the pointer expression. */
static void emit_mem_check_live_open(CodeGen *codegen, AstNode *arena_expression) {
    emit(codegen, "gray_mem_check_live(");
    emit_expression(codegen, arena_expression);
    emit(codegen, ", (void *)(");
}
static void emit_mem_check_live_close(CodeGen *codegen, int line) {
    emit_formatted(codegen, "), \"%s\", %d)", codegen->file, line);
}

/* Returns true if the named enum is string-backed.
 * enum_names is sorted after the init pass, so we use bsearch. */
static bool codegen_enum_is_string(CodeGen *codegen, const char *name) {
    if (!name) return false;
    const char **match = bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                               sizeof(const char *), keyword_compare);
    if (match) return codegen->is_enum_string[match - codegen->enum_names];
    return false;
}

/* Register a wide integer variable's declared type name */
static void register_wide_integer_variable(CodeGen *codegen, const char *name, const char *type_name) {
    if (codegen->wide_integer_variable_count >= codegen->wide_integer_variable_capacity) {
        codegen->wide_integer_variable_capacity = codegen->wide_integer_variable_capacity ? codegen->wide_integer_variable_capacity * 2 : 8;
        codegen->wide_integer_variable_names = xrealloc(codegen->wide_integer_variable_names, sizeof(const char *) * codegen->wide_integer_variable_capacity);
        codegen->wide_integer_variable_types = xrealloc(codegen->wide_integer_variable_types, sizeof(const char *) * codegen->wide_integer_variable_capacity);
    }
    codegen->wide_integer_variable_names[codegen->wide_integer_variable_count] = name;
    codegen->wide_integer_variable_types[codegen->wide_integer_variable_count] = type_name;
    codegen->wide_integer_variable_count++;
}

/* Look up a variable's wide integer type name, or NULL if not wide integer */
static const char *lookup_wide_integer_variable(CodeGen *codegen, const char *name) {
    for (int i = codegen->wide_integer_variable_count - 1; i >= 0; i--) {
        if (strcmp(codegen->wide_integer_variable_names[i], name) == 0) return codegen->wide_integer_variable_types[i];
    }
    return NULL;
}

/* Get the wide integer type prefix for a given type name (e.g., "i128" → "gray_i128") */
static const char *wide_integer_prefix(const char *type_spelling) {
    if (strcmp(type_spelling, "i128") == 0) return "gray_i128";
    if (strcmp(type_spelling, "u128") == 0) return "gray_u128";
    if (strcmp(type_spelling, "i256") == 0) return "gray_i256";
    if (strcmp(type_spelling, "u256") == 0) return "gray_u256";
    return NULL;
}

/* Canonical wide-integer type name as a string literal, or NULL if not one.
 * Use this (not the caller's string) when registering a wide integer binding, so the
 * stored pointer has static lifetime. */
static const char *wide_integer_type_name(const char *type_spelling) {
    if (!type_spelling) return NULL;
    if (strcmp(type_spelling, "i128") == 0) return "i128";
    if (strcmp(type_spelling, "u128") == 0) return "u128";
    if (strcmp(type_spelling, "i256") == 0) return "i256";
    if (strcmp(type_spelling, "u256") == 0) return "u256";
    return NULL;
}

/* Resolve the wide integer type name for an expression (checks labels against tracked vars) */
static const char *resolve_wide_integer_type(CodeGen *codegen, AstNode *node) {
    if (!node) return NULL;
    /* A value widened into a wide type is that type once emitted. */
    if (node->widen_to) return wide_integer_type_name(node->widen_to);
    /* The type checker's resolved type decides. The walk below is for a node
     * it could not type: one inside a generic body, typed by the binding. */
    GrayType *resolved = type_table_get(codegen->type_table, node);
    if (resolved && resolved->kind != TYPE_KIND_UNKNOWN)
        return resolved->kind == TYPE_KIND_SIGNED_INTEGER || resolved->kind == TYPE_KIND_UNSIGNED_INTEGER
            ? wide_integer_type_name(resolved->name) : NULL;
    if (node->kind == NODE_LABEL) {
        const char *local_wide_integer = lookup_wide_integer_variable(codegen, node->data.label.value);
        if (local_wide_integer) return local_wide_integer;
        /* A module-level variable or constant is not in the local registry;
         * its resolved type says it is wide. */
        GrayType *label_type = type_table_get(codegen->type_table, node);
        return label_type && (label_type->kind == TYPE_KIND_SIGNED_INTEGER || label_type->kind == TYPE_KIND_UNSIGNED_INTEGER)
            ? wide_integer_type_name(label_type->name) : NULL;
    }
    /* If the node is a call to a wide integer cast function */
    if (node->kind == NODE_CALL_EXPRESSION && node->data.call.function->kind == NODE_LABEL) {
        const char *function_name = node->data.call.function->data.label.value;
        if (is_wide_integer_type_name(function_name)) return function_name;
    }
    /* cast(expr, i128/u128/i256/u256) — the result is already the target wide integer */
    if (node->kind == NODE_CAST_EXPRESSION && is_wide_integer_type_name(node->data.cast.target_type))
        return node->data.cast.target_type;
    /* -wide_integer_variable / bit_not wide_integer_variable — the result is still the same wide integer type */
    if (node->kind == NODE_PREFIX_EXPRESSION &&
        (node->data.prefix.operator == TOKEN_MINUS || node->data.prefix.operator == TOKEN_BIT_NOT))
        return resolve_wide_integer_type(codegen, node->data.prefix.right);
    /* If this is an infix expression, check left operand.
     * Operators that yield bool never yield a wide integer, whatever their operands
     * are — mirrors the bool-result set in resolve_infix_expression. */
    if (node->kind == NODE_INFIX_EXPRESSION) {
        TokenType operator = node->data.infix.operator;
        if (operator == TOKEN_EQUAL || operator == TOKEN_NOT_EQUAL ||
            operator == TOKEN_LESS_THAN || operator == TOKEN_GREATER_THAN ||
            operator == TOKEN_LESS_THAN_OR_EQUAL || operator == TOKEN_GREATER_THAN_OR_EQUAL ||
            operator == TOKEN_AND || operator == TOKEN_OR ||
            operator == TOKEN_IN || operator == TOKEN_NOT_IN)
            return NULL;
        const char *left_type = resolve_wide_integer_type(codegen, node->data.infix.left);
        if (left_type) return left_type;
        /* A shift has its left operand's type; the amount never widens it. */
        if (operator == TOKEN_BIT_SHIFT_LEFT || operator == TOKEN_BIT_SHIFT_RIGHT) return NULL;
        return resolve_wide_integer_type(codegen, node->data.infix.right);
    }
    /* Struct field access a.val — check the resolved field type from the type table */
    if (node->kind == NODE_MEMBER_EXPRESSION) {
        GrayType *field_type = type_table_get(codegen->type_table, node);
        if (field_type && field_type->name && is_wide_integer_type_name(field_type->name))
            return field_type->name;
    }
    /* Pointer dereference p^ — check whether the pointee type is wide integer */
    if (node->kind == NODE_POSTFIX_EXPRESSION && node->data.postfix.operator == TOKEN_CARET) {
        GrayType *pointer_type = type_table_get(codegen->type_table, node->data.postfix.left);
        if (pointer_type && pointer_type->kind == TYPE_KIND_POINTER && pointer_type->element_type &&
            is_wide_integer_type_name(pointer_type->element_type))
            return pointer_type->element_type;
    }
    /* Array indexing arr[i] and user-function calls: the typechecker records
     * the resolved element / return type, so trust that when it is wide integer. */
    if (node->kind == NODE_INDEX_EXPRESSION || node->kind == NODE_CALL_EXPRESSION) {
        GrayType *new_type = type_table_get(codegen->type_table, node);
        if (new_type && new_type->name && is_wide_integer_type_name(new_type->name))
            return new_type->name;
    }
    return NULL;
}

/* Emit a scalar expression converted to a wide integer type.
 *
 * The constructor is chosen from the SOURCE operand's signedness, never the
 * destination's. i128 and i256 represent every u64 exactly, so widening a
 * u64 is value-preserving — but routing it through from_i64 reinterprets
 * any value above INT64_MAX as negative. Only when the source type cannot
 * be resolved does the destination's signedness stand in.
 *
 * value_t may be NULL; the type table is consulted when it is. */
static void emit_scalar_to_wide_integer(CodeGen *codegen, const char *target_type,
                                  AstNode *value, GrayType *value_type) {
    const char *prefix = wide_integer_prefix(target_type);

    /* A literal that took this wide type is already a constant of it, and a
     * value the type checker widened converts itself. */
    if ((value->folded_literal || value->widen_to) && resolve_wide_integer_type(codegen, value)) {
        emit_expression(codegen, value);
        return;
    }

    if (!value_type && codegen->type_table)
        value_type = type_table_get(codegen->type_table, value);

    bool is_source_unsigned = value_type
        ? value_type->kind == TYPE_KIND_UNSIGNED_INTEGER
        : false;

    /* An unsigned destination has only from_u64 to offer, so it takes that
     * path whatever the source is; a signed one follows the source. */
    bool use_u64 = (target_type[0] == 'u') || is_source_unsigned;

    if (use_u64) {
        emit_formatted(codegen, "%s_from_u64((uint64_t)(", prefix);
    } else {
        emit_formatted(codegen, "%s_from_i64((int64_t)(", prefix);
    }
    emit_expression(codegen, value);
    emit(codegen, "))");
}

/* Coerce `value` to wide integer type `bi` when it is not already a wide integer, so it
 * can be assigned / passed / returned / stored where a wide integer is expected.
 * Integer literals (including those wider than 64 bits) and other scalars are
 * wrapped with the matching constructor. Returns true when it emitted the
 * value; false means `bi` is not a wide integer or `value` already is one, and the
 * caller should emit `value` normally. */
static bool emit_wide_integer_coerced(CodeGen *codegen, const char *wide_integer_type, AstNode *value) {
    if (!wide_integer_type || !is_wide_integer_type_name(wide_integer_type)) return false;
    if (resolve_wide_integer_type(codegen, value)) return false;
    emit_scalar_to_wide_integer(codegen, wide_integer_type, value, NULL);
    return true;
}

/* Emit `value` where the language gives it the declared type `gray_tn` (a
 * return slot, a parameter). An array or map literal takes its storage from
 * that type, so a [u8] literal is packed wherever it is written. */
static void emit_declared_value(CodeGen *codegen, const char *gray_type_name, AstNode *value) {
    if (gray_type_name && (value->kind == NODE_ARRAY_VALUE || value->kind == NODE_MAP_VALUE)) {
        const char *saved_variable_type = codegen->current_variable_type;
        codegen->current_variable_type = gray_type_name;
        emit_expression(codegen, value);
        codegen->current_variable_type = saved_variable_type;
        return;
    }
    emit_expression(codegen, value);
}

/* Emits a parameter's default value where a call omits the argument,
 * wrapping it in the wide integer constructor for a wide-integer parameter as an
 * explicit argument is. */
static void emit_parameter_default_value(CodeGen *codegen, Parameter *parameter) {
    if (!emit_wide_integer_coerced(codegen, parameter->type_name, parameter->default_value))
        emit_declared_value(codegen, parameter->type_name, parameter->default_value);
}

/* Emit `value` for a map key or value slot whose Grayscale type is `gray_tn`:
 * a wide-integer slot needs the scalar wrapped in its constructor, everything
 * else emits verbatim. Safe to call with any `gray_tn`. */
static void emit_map_slot_value(CodeGen *codegen, const char *gray_type_name, AstNode *value) {
    if (!emit_wide_integer_coerced(codegen, gray_type_name, value))
        emit_expression(codegen, value);
}

/* Emit an operand of a wide-integer operation computed in `bi_type`: a
 * value of that type as it is, a narrower wide integer widened (i128 into
 * i256, u128 into u256), and any other integer through the constructor. */
static void emit_wide_integer_operand(CodeGen *codegen, AstNode *operand,
                                const char *prefix, const char *wide_integer_type,
                                GrayType *operand_type) {
    const char *source_wide_integer = resolve_wide_integer_type(codegen, operand);
    if (!source_wide_integer) {
        emit_scalar_to_wide_integer(codegen, wide_integer_type, operand, operand_type);
    } else if (strcmp(source_wide_integer, wide_integer_type) == 0) {
        emit_expression(codegen, operand);
    } else {
        emit_formatted(codegen, "%s_from_%s(", prefix, source_wide_integer);
        emit_expression(codegen, operand);
        emit(codegen, ")");
    }
}

/* Emit a shift amount as an int64_t. A wide amount too large for int64_t
 * cannot be in range for any operand, so it panics with the shift's
 * max_amount. */
static void emit_shift_amount(CodeGen *codegen, AstNode *amount, int maximum_amount) {
    const char *wide = resolve_wide_integer_type(codegen, amount);
    if (wide) {
        emit_formatted(codegen, "%s_shift_amount(", wide_integer_prefix(wide));
        emit_expression(codegen, amount);
        emit_formatted(codegen, ", %d, \"%s\", %d)", maximum_amount, codegen->file, amount->token.line);
        return;
    }
    emit(codegen, "(int64_t)(");
    emit_expression(codegen, amount);
    emit(codegen, ")");
}

static bool is_mutable_parameter(CodeGen *codegen, const char *name) {
    if (!codegen->current_function) return false;
    for (int i = 0; i < codegen->current_function->data.function_declaration.parameter_count; i++) {
        Parameter *parameter = &codegen->current_function->data.function_declaration.parameters[i];
        if (parameter->is_mutable && strcmp(parameter->name, name) == 0) return true;
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
    if (!function_node || function_node->kind != NODE_FUNCTION_DECLARATION) return false;
    for (int i = 0; i < function_node->data.function_declaration.parameter_count; i++) {
        Parameter *parameter = &function_node->data.function_declaration.parameters[i];
        if (parameter->is_mutable) return true;
        if (!parameter->type_name) continue;
        const char *type_name = resolve_type_alias_codegen(codegen, parameter->type_name);
        if (type_name && type_name[0] == '^') return true;
    }
    return false;
}

/* Same reasoning one level down: a caller-arena function must not open
 * per-iteration or per-block arenas either. Anything it allocates can be
 * stored through a pointer parameter and outlive the block that created it,
 * so its allocations stay in the caller's arena for the whole call. */
static bool current_function_uses_caller_arena(CodeGen *codegen) {
    return function_uses_caller_arena(codegen, codegen->current_function);
}

static bool is_result_temporary(const char *name) {
    if (!name) return false;
    return strncmp(name, GRAY_SYNTHETIC_TEMPORARY, sizeof(GRAY_SYNTHETIC_TEMPORARY) - 1) == 0 ||
           strncmp(name, GRAY_SYNTHETIC_OR, sizeof(GRAY_SYNTHETIC_OR) - 1) == 0;
}

/* The value being emitted is bound to a compiler-generated destructuring
 * temporary (a multi-return capture or an or_return result), not a
 * user-named variable. */
static bool current_variable_is_result_temporary(CodeGen *codegen) {
    return is_result_temporary(codegen->current_variable_name);
}

static int function_name_compare(const void *left, const void *right) {
    const AstNode *left_function = *(const AstNode *const *)left;
    const AstNode *right_function = *(const AstNode *const *)right;
    return strcmp(left_function->data.function_declaration.name, right_function->data.function_declaration.name);
}

/* Find a function declaration by name. Builds and reuses a sorted view of
 * codegen->all_functions so lookups are O(log n) after the first call. The view is
 * invalidated whenever a new function is registered (see register sites). */
static AstNode *find_function(CodeGen *codegen, const char *name) {
    if (codegen->function_count == 0) return NULL;
    if (!codegen->is_functions_by_name_built) {
        codegen->functions_by_name = xrealloc(codegen->functions_by_name,
            sizeof(AstNode *) * (size_t)codegen->function_count);
        memcpy(codegen->functions_by_name, codegen->all_functions,
            sizeof(AstNode *) * (size_t)codegen->function_count);
        qsort(codegen->functions_by_name, (size_t)codegen->function_count, sizeof(AstNode *), function_name_compare);
        codegen->is_functions_by_name_built = true;
    }
    /* Build a stack search_key node so bsearch can compare against the name field. */
    AstNode search_key;
    search_key.data.function_declaration.name = name;
    AstNode *search_key_pointer = &search_key;
    AstNode **match = bsearch(&search_key_pointer, codegen->functions_by_name, (size_t)codegen->function_count,
        sizeof(AstNode *), function_name_compare);
    return match ? *match : NULL;
}

/* The function a bare name in a `ref(name)` refers to. Codegen renames every
 * declaration to its module-mangled spelling, so a name written inside an
 * imported module is not the key its function is indexed under — looking up
 * only the written name missed it and emitted a variable's address. */
static AstNode *find_referenced_function(CodeGen *codegen, AstNode *label) {
    const char *written = label->data.label.value;
    AstNode *target = find_function(codegen, written);
    if (target) return target;
    const char *resolved = codegen_resolve_label(codegen, label, written);
    return resolved != written ? find_function(codegen, resolved) : NULL;
}

/* Build the (field_name, struct_name) index for func-typed fields once.
 * Order matches struct_declarations so the first-match heuristic is preserved. */
static void build_function_field_index(CodeGen *codegen) {
    if (codegen->is_function_field_index_built) return;
    int total = 0;
    for (int struct_index = 0; struct_index < codegen->struct_declaration_count; struct_index++) {
        total += codegen->struct_declarations[struct_index]->data.struct_declaration.field_count;
    }
    if (total > 0) {
        codegen->function_field_index = xmalloc(sizeof(*codegen->function_field_index) * (size_t)total);
    }
    for (int struct_index = 0; struct_index < codegen->struct_declaration_count; struct_index++) {
        AstNode *struct_declaration = codegen->struct_declarations[struct_index];
        for (int field_index = 0; field_index < struct_declaration->data.struct_declaration.field_count; field_index++) {
            StructField *struct_field = &struct_declaration->data.struct_declaration.fields[field_index];
            if (struct_field->type_name &&
                (strcmp(struct_field->type_name, "func") == 0 ||
                 strncmp(struct_field->type_name, "func(", 5) == 0)) {
                codegen->function_field_index[codegen->function_field_count].field_name = struct_field->name;
                codegen->function_field_index[codegen->function_field_count].struct_name = struct_declaration->data.struct_declaration.name;
                codegen->function_field_count++;
            }
        }
    }
    codegen->is_function_field_index_built = true;
}

/* --- Expression Emission Helpers --- */

static void emit_label(CodeGen *codegen, AstNode *node) {
    const char *name = sanitize_name(node->data.label.value);
    const char *raw_name = node->data.label.value;
    /* bare stdlib constants from using-modules */
    static const struct { const char *name; const char *module_name; const char *value; } stdlib_constants[] = {
        {"PI","math","3.14159265358979323846"},{"E","math","2.71828182845904523536"},
        {"TAU","math","6.28318530717958647692"},{"PHI","math","1.61803398874989484820"},
        {"SQRT2","math","1.41421356237309504880"},{"LN2","math","0.69314718055994530942"},
        {"LN10","math","2.30258509299404568402"},{"INF","math","(1.0/0.0)"},
        {"NEG_INF","math","(-1.0/0.0)"},{"EPSILON","math","2.2204460492503131e-16"},
        {"MAX_I64","math","9223372036854775807LL"},{"MIN_I64","math","(-9223372036854775807LL - 1)"},
        {"MAX_F64","math","1.7976931348623157e308"},{"MIN_F64","math","-1.7976931348623157e308"},
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
    for (int using_index = 0; using_index < codegen->using_module_count && !emitted_const &&
                     !node->data.label.is_local_reference; using_index++) {
        const char *real_module = resolve_alias(codegen, codegen->using_modules[using_index]);
        for (int constant_index = 0; stdlib_constants[constant_index].name; constant_index++) {
            if (strcmp(raw_name, stdlib_constants[constant_index].name) == 0 &&
                strcmp(real_module, stdlib_constants[constant_index].module_name) == 0) {
                emit(codegen, stdlib_constants[constant_index].value);
                emitted_const = true;
                break;
            }
        }
    }
    if (emitted_const) return;
    if (is_mutable_parameter(codegen, raw_name)) {
        emit_formatted(codegen, "(*%s)", name);
    } else if (is_reference_variable(codegen, raw_name)) {
        emit_formatted(codegen, "(*%s)", name);
    } else if (label_is_entry_global(node)) {
        emit(codegen, global_variable_c_name(codegen, raw_name));
    } else {
        /* A bare name that names a module-level declaration is emitted under
         * that declaration's mangled name. Locals and parameters are not
         * declarations, so they resolve to nothing and stay as written —
         * which is why a binding that merely shares a name with a sibling
         * file of its module is left alone. */
        const char *resolved = codegen_resolve_label(codegen, node, raw_name);
        emit(codegen, resolved != raw_name ? sanitize_name(resolved) : name);
    }
}

/* Emit the escaped body of a C string literal for `node` (a NODE_STRING_VALUE),
 * without the surrounding quotes or any gray_string_lit wrapper. Hex escapes are
 * split with string concatenation ("A\x42" "C") to stop C's greedy \x parsing. */
static void emit_c_string_body(CodeGen *codegen, AstNode *node) {
    const char *cursor = node->data.string_value.value;
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
}

static void emit_string_value(CodeGen *codegen, AstNode *node) {
    /* Check for null bytes; if present, use gray_string_lit_len with explicit
     * length since strlen() would truncate at the null. */
    bool has_null = false;
    int string_length = 0;
    for (const char *scan = node->data.string_value.value; *scan; scan++) {
        if (scan[0] == '\\' && scan[1] == 'x' && scan[2] == '0' && scan[3] == '0') {
            has_null = true;
            string_length++; /* \x00 = 1 byte */
            scan += 3;
        } else if (scan[0] == '\\' && scan[1] == '0') {
            has_null = true;
            string_length++; /* \0 = 1 byte */
            scan += 1;
        } else if (scan[0] == '\\' && scan[1]) {
            string_length++; /* other escape = 1 byte */
            scan += 1;
        } else {
            string_length++;
        }
    }
    /* Use macro form for file-scope compatibility */
    if (has_null && codegen->indent > 0) {
        emit_formatted(codegen, "gray_string_lit_len(\"");
    } else {
        emit(codegen, (codegen->indent == 0) ? "GRAY_STRING_LIT(\"" : "gray_string_lit(\"");
    }
    emit_c_string_body(codegen, node);
    if (has_null && codegen->indent > 0) {
        emit_formatted(codegen, "\", %d)", string_length);
    } else {
        emit(codegen, "\")");
    }
}

static bool interp_container_needs_value_print(CodeGen *codegen, const GrayType *type);
static void emit_interpolated_container(CodeGen *codegen, AstNode *part, GrayType *type);

/* The bit size a floating-point type prints at: 32 for f32, 64 for f64. */
static int floating_point_bit_size(const char *floating_point_type_name) {
    return floating_point_type_name && strcmp(floating_point_type_name, "f32") == 0 ? 32 : 64;
}

static void emit_interpolated_string(CodeGen *codegen, AstNode *node) {
    /* Lower to a single gray_string_concat_n() over all parts: one allocation,
     * one copy per part. (gray_string_format is avoided throughout — its
     * vsnprintf truncates string values at an embedded \0; the concat path is
     * memcpy-based and null-safe.) */
    int part_count = node->data.interpolated_string.part_count;
    if (part_count == 0) {
        emit(codegen, "gray_string_lit(\"\")");
        return;
    }
    /* One part needs no join — emit it directly. */
    bool nary = part_count >= 2;
    if (nary) {
        emit_formatted(codegen, "gray_string_concat_n(gray_default_arena, %d", part_count);
    }
    /* Emit each part as a GrayString expression */
    for (int i = 0; i < part_count; i++) {
        if (nary) emit(codegen, ", ");
        AstNode *part = node->data.interpolated_string.parts[i];
        if (part->kind == NODE_STRING_VALUE) {
            /* Literal text — reuses NODE_STRING_VALUE codegen (null-safe) */
            emit_expression(codegen, part);
        } else {
            /* Expression — resolve type and emit as GrayString */
            GrayType *part_type = type_table_get(codegen->type_table, part);
            TypeKind type_kind = part_type ? part_type->kind : TYPE_KIND_UNKNOWN;
            if (type_kind == TYPE_KIND_UNKNOWN) {
                if (codegen->wildcard_binding) {
                    GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
                    if (wildcard_type) { type_kind = wildcard_type->kind; part_type = wildcard_type; }
                }
            }
            if (type_kind == TYPE_KIND_UNKNOWN) {
                if (part->kind == NODE_FLOATING_POINT_LITERAL) type_kind = TYPE_KIND_FLOATING_POINT;
                else if (part->kind == NODE_BOOL_VALUE) type_kind = TYPE_KIND_BOOL;
                else if (part->kind == NODE_STRING_VALUE) type_kind = TYPE_KIND_STRING;
                else type_kind = TYPE_KIND_SIGNED_INTEGER;
            }

            const char *wide_integer_type = resolve_wide_integer_type(codegen, part);
            if (wide_integer_type) {
                emit_formatted(codegen, "%s_to_string(gray_default_arena, ", wide_integer_prefix(wide_integer_type));
                emit_expression(codegen, part);
                emit(codegen, ")");
            } else switch (type_kind) {
            case TYPE_KIND_STRING:
                emit_expression(codegen, part);
                break;
            case TYPE_KIND_BOOL:
                emit(codegen, "(");
                emit_expression(codegen, part);
                emit(codegen, ") ? gray_string_lit(\"true\") : gray_string_lit(\"false\")");
                break;
            case TYPE_KIND_FLOATING_POINT:
                emit(codegen, "gray_builtin_format_float(gray_default_arena, ");
                emit_expression(codegen, part);
                emit_formatted(codegen, ", %d)", floating_point_bit_size(part_type ? part_type->name : NULL));
                break;
            case TYPE_KIND_CHAR:
                emit(codegen, "gray_builtin_char_to_utf8(gray_default_arena, ");
                emit_expression(codegen, part);
                emit(codegen, ")");
                break;
            case TYPE_KIND_ARRAY: {
                if (interp_container_needs_value_print(codegen, part_type)) {
                    emit_interpolated_container(codegen, part, part_type);
                    break;
                }
                int element_kind_tag = 0;
                if (part_type && part_type->element_type) {
                    GrayType *element_type = type_from_name(part_type->element_type);
                    if (element_type->kind == TYPE_KIND_FLOATING_POINT) element_kind_tag = 1;
                    else if (element_type->kind == TYPE_KIND_STRING) element_kind_tag = 2;
                    else if (element_type->kind == TYPE_KIND_BOOL) element_kind_tag = 3;
                    else if (element_type->kind == TYPE_KIND_CHAR) element_kind_tag = 6;
                    else if (element_type->kind == TYPE_KIND_ENUM) {
                        element_kind_tag = (part_type->element_type && codegen_enum_is_string(codegen, part_type->element_type)) ? 2 : 7;
                    }
                }
                emit_formatted(codegen, "({ GrayArray _interp_arr = ");
                emit_expression(codegen, part);
                emit_formatted(codegen, "; gray_builtin_array_to_string(gray_default_arena, &_interp_arr, %d); })", element_kind_tag);
                break;
            }
            case TYPE_KIND_MAP: {
                if (interp_container_needs_value_print(codegen, part_type)) {
                    emit_interpolated_container(codegen, part, part_type);
                    break;
                }
                int value_kind_tag = 0;
                if (part_type && part_type->value_type) {
                    GrayType *value_type = type_from_name(part_type->value_type);
                    if (value_type->kind == TYPE_KIND_FLOATING_POINT) value_kind_tag = 1;
                    else if (value_type->kind == TYPE_KIND_STRING) value_kind_tag = 2;
                    else if (value_type->kind == TYPE_KIND_BOOL) value_kind_tag = 3;
                    else if (value_type->kind == TYPE_KIND_CHAR) value_kind_tag = 6;
                    else if (value_type->kind == TYPE_KIND_ENUM) {
                        value_kind_tag = (part_type->value_type && codegen_enum_is_string(codegen, part_type->value_type)) ? 2 : 7;
                    }
                }
                emit_formatted(codegen, "({ GrayMap _interp_map = ");
                emit_expression(codegen, part);
                emit_formatted(codegen, "; gray_builtin_map_to_string(gray_default_arena, &_interp_map, %d); })", value_kind_tag);
                break;
            }
            case TYPE_KIND_ERROR:
                emit_expression(codegen, part);
                emit(codegen, " ? ");
                emit_expression(codegen, part);
                emit(codegen, "->msg : gray_string_lit(\"nil\")");
                break;
            case TYPE_KIND_UNSIGNED_INTEGER:
                emit(codegen, "gray_string_format(gray_default_arena, \"%llu\", (unsigned long long)(");
                emit_expression(codegen, part);
                emit(codegen, "))");
                break;
            case TYPE_KIND_STRUCT:
                if (part_type && part_type->name && strcmp(part_type->name, "UUID") == 0) {
                    emit_expression(codegen, part);
                    emit(codegen, ".value");
                } else {
                    emit(codegen, "gray_string_format(gray_default_arena, \"%lld\", (long long)(");
                    emit_expression(codegen, part);
                    emit(codegen, "))");
                }
                break;
            case TYPE_KIND_ENUM:
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
    }
    if (nary) emit(codegen, ")");
}

static void emit_array_value_as_declared(CodeGen *codegen, AstNode *node);

/* Array literal: its element type is the one the type checker resolved for
 * it — the slot it is stored into, or its own elements' type. */
static void emit_array_value(CodeGen *codegen, AstNode *node) {
    GrayType *literal_type = type_table_get(codegen->type_table, node);
    const char *saved_variable_type = codegen->current_variable_type;
    if (literal_type && literal_type->kind == TYPE_KIND_ARRAY && literal_type->element_type &&
        !strchr(literal_type->element_type, '?')) {
        char resolved[TYPE_NAME_MAX];
        snprintf(resolved, sizeof(resolved), "[%s]", literal_type->element_type);
        codegen->current_variable_type = arena_intern_string(codegen->modules->arena,
            resolved, strlen(resolved));
    }
    emit_array_value_as_declared(codegen, node);
    codegen->current_variable_type = saved_variable_type;
}

/* Array literal: emit as GrayArray using gray_array_from, storing elements as
 * codegen->current_variable_type gives them. */
static void emit_array_value_as_declared(CodeGen *codegen, AstNode *node) {
    int count = node->data.array_value.count;
    if (count == 0) {
        /* Empty array; check type table for element type, falling
         * back to the var-decl context type if the node has none. */
        GrayType *array_type = type_table_get(codegen->type_table, node);
        if ((!array_type || array_type->kind == TYPE_KIND_UNKNOWN) && codegen->current_variable_type && codegen->current_variable_type[0]) {
            array_type = type_from_name(codegen->current_variable_type);
        }
        const char *element_c_type = "int64_t";
        if (array_type && array_type->kind == TYPE_KIND_ARRAY && array_type->element_type) {
            const char *element_type_spelling_text = array_type->element_type;
            GrayType *element_type = type_from_name(element_type_spelling_text);
            if (element_type->kind == TYPE_KIND_FLOATING_POINT) element_c_type = (strcmp(element_type_spelling_text, "f32") == 0) ? "float" : "double";
            else if (element_type->kind == TYPE_KIND_BOOL) element_c_type = "bool";
            else if (element_type->kind == TYPE_KIND_STRING) element_c_type = "GrayString";
            else if (element_type->kind == TYPE_KIND_ARRAY) element_c_type = "GrayArray";
            else if (element_type->kind == TYPE_KIND_MAP) element_c_type = "GrayMap";
            else if (element_type->kind == TYPE_KIND_STRUCT) element_c_type = gray_type_to_c_codegen(codegen, element_type_spelling_text);
            else if (element_type->kind == TYPE_KIND_POINTER) element_c_type = gray_type_to_c_codegen(codegen, element_type_spelling_text);
            else if (element_type->kind == TYPE_KIND_CHAR) element_c_type = "int32_t";
            else if (element_type->kind == TYPE_KIND_SIGNED_INTEGER || element_type->kind == TYPE_KIND_UNSIGNED_INTEGER) element_c_type = gray_type_to_c_codegen(codegen, element_type_spelling_text);
        }
        emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", element_c_type);
        return;
    }

    /* Check if this is a nested array (elements are arrays) */
    if (node->data.array_value.elements[0]->kind == NODE_ARRAY_VALUE) {
        /* Nested array: each element is an GrayArray. Each inner literal is
         * emitted at the element type the declaration gives it, so a
         * [[i32]] holds 4-byte rows and a [[f32]] holds floats. */
        const char *saved_variable_type = codegen->current_variable_type;
        char inner_variable_type[TYPE_NAME_MAX];
        const char *element_type_name = extract_array_element_type(saved_variable_type);
        if (element_type_name && element_type_name[0] == '[') {
            snprintf(inner_variable_type, sizeof(inner_variable_type), "%s", element_type_name);
            codegen->current_variable_type = inner_variable_type;
        }
        emit_formatted(codegen, "gray_array_from(gray_default_arena, (GrayArray[]){");
        for (int i = 0; i < count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.array_value.elements[i]);
        }
        emit_formatted(codegen, "}, sizeof(GrayArray), %d, GRAY_ELEM_ARRAY)", count);
        codegen->current_variable_type = saved_variable_type;
        return;
    }

    /* Array of maps: elements are map literals */
    if (node->data.array_value.elements[0]->kind == NODE_MAP_VALUE) {
        emit_formatted(codegen, "gray_array_from(gray_default_arena, (GrayMap[]){");
        for (int i = 0; i < count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.array_value.elements[i]);
        }
        emit_formatted(codegen, "}, sizeof(GrayMap), %d, GRAY_ELEM_MAP)", count);
        return;
    }

    /* Determine element type; try wide integer detection first, then type table */
    const char *wide_integer_element = resolve_wide_integer_type(codegen, node->data.array_value.elements[0]);
    GrayType *element_type_for_copy = type_table_get(codegen->type_table, node->data.array_value.elements[0]);
    if (!wide_integer_element && element_type_for_copy && element_type_for_copy->name && is_wide_integer_type_name(element_type_for_copy->name))
        wide_integer_element = element_type_for_copy->name;
    /* Also check var decl context for wide integer element type — either a bare
     * wide integer name (nested-array recursion) or an array type like "[i128]". */
    if (!wide_integer_element && codegen->current_variable_type && is_wide_integer_type_name(codegen->current_variable_type))
        wide_integer_element = codegen->current_variable_type;
    if (!wide_integer_element && codegen->current_variable_type) {
        const char *current_type_spelling = codegen->current_variable_type;
        size_t current_type_spelling_length = strlen(current_type_spelling);
        if (current_type_spelling_length >= 3 && current_type_spelling[0] == '[' && current_type_spelling[current_type_spelling_length - 1] == ']') {
            char inner[TYPE_NAME_MAX];
            size_t inner_length = current_type_spelling_length - 2;
            if (inner_length < sizeof(inner)) {
                memcpy(inner, current_type_spelling + 1, inner_length);
                inner[inner_length] = '\0';
                char *comma = strchr(inner, ',');
                if (comma) *comma = '\0';
                if (is_wide_integer_type_name(inner)) wide_integer_element = type_from_name(inner)->name;
            }
        }
    }
    /* Fall back to the declared array type when the typetable has no
     * entry for the first element (happens for module-qualified struct
     * function calls — the call's return type isn't always threaded
     * into the table). Without this we default to int64_t and emit a
     * `(int64_t[]){struct_value}` cast that clang rejects. */
    if ((!element_type_for_copy || element_type_for_copy->kind == TYPE_KIND_UNKNOWN) &&
        codegen->current_variable_type &&
        codegen->current_variable_type[0] == '[' &&
        strncmp(codegen->current_variable_type, "[func", 5) != 0) {
        size_t current_type_spelling_length = strlen(codegen->current_variable_type);
        if (current_type_spelling_length >= 3 && codegen->current_variable_type[current_type_spelling_length - 1] == ']') {
            char inferred[MESSAGE_BUFFER_SIZE];
            size_t copy_length = current_type_spelling_length - 2;
            if (copy_length >= sizeof(inferred)) copy_length = sizeof(inferred) - 1;
            memcpy(inferred, codegen->current_variable_type + 1, copy_length);
            inferred[copy_length] = '\0';
            /* Strip fixed-size ",N" suffix if present */
            char *comma = strchr(inferred, ',');
            if (comma) *comma = '\0';
            GrayType *inferred_type = type_from_name(inferred);
            if (inferred_type && inferred_type->kind != TYPE_KIND_UNKNOWN) element_type_for_copy = inferred_type;
        }
    }
    /* Inside a generic function body, a wildcard-typed element (return {x, x}
     * for -> [?]) resolves to TYPE_KIND_UNKNOWN in the un-specialised pass. Use the
     * active instantiation binding so the compound literal stores the concrete
     * C type instead of defaulting to int64_t. */
    if ((!element_type_for_copy || element_type_for_copy->kind == TYPE_KIND_UNKNOWN) && codegen->wildcard_binding) {
        GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
        if (wildcard_type && wildcard_type->kind != TYPE_KIND_UNKNOWN) element_type_for_copy = wildcard_type;
    }
    TypeKind type_kind = element_type_for_copy ? element_type_for_copy->kind : TYPE_KIND_SIGNED_INTEGER;

    /* The declared element type, from both the [T] and [T, N] forms. */
    const char *declared_element_type = extract_array_element_type(codegen->current_variable_type);
    bool declared_f32 = declared_element_type && strcmp(declared_element_type, "f32") == 0;

    /* Integer literals in a declared [f32]/[f64] array must use
     * double so the C compound literal stores the correct IEEE 754 bits
     * instead of raw int64_t bit patterns. */
    if (type_kind == TYPE_KIND_SIGNED_INTEGER && declared_element_type &&
        (declared_f32 || strcmp(declared_element_type, "f64") == 0))
        type_kind = TYPE_KIND_FLOATING_POINT;

    const char *c_type;
    /* Check for wide integer types first */
    if (wide_integer_element) {
        c_type = wide_integer_prefix(wide_integer_element);
    } else if (element_type_for_copy && element_type_for_copy->name && (strcmp(element_type_for_copy->name, "func") == 0 || strncmp(element_type_for_copy->name, "func(", 5) == 0)) {
        /* Function reference elements: store as generic fn ptrs, cast at
         * call sites (mirrors gray_type_to_c_codegen's handling of "func"). */
        c_type = "void *";
    } else if (codegen->current_variable_type &&
               (codegen->current_variable_type && (strcmp(codegen->current_variable_type, "[func]") == 0 || strncmp(codegen->current_variable_type, "[func(", 6) == 0))) {
        /* Declared as [func] but element inference missed it (e.g. empty
         * literal or heterogeneous func refs). */
        c_type = "void *";
    } else switch (type_kind) {
    case TYPE_KIND_FLOATING_POINT:  c_type = "double"; break;
    case TYPE_KIND_BOOL:   c_type = "bool"; break;
    case TYPE_KIND_STRING: c_type = "GrayString"; break;
    case TYPE_KIND_STRUCT: c_type = gray_type_to_c_codegen(codegen, element_type_for_copy->name); break;
    case TYPE_KIND_ENUM: {
        bool is_string = element_type_for_copy->name ? codegen_enum_is_string(codegen, element_type_for_copy->name) : false;
        static char enum_array_buffer[MESSAGE_BUFFER_SIZE];
        if (is_string) {
            c_type = "GrayString";
        } else if (element_type_for_copy->name && strcmp(element_type_for_copy->name, "ErrorCode") == 0) {
            c_type = "GrayErrorCode";
        } else {
            snprintf(enum_array_buffer, sizeof(enum_array_buffer), "GrayEnum_%s", element_type_for_copy->name ? element_type_for_copy->name : "int");
            c_type = enum_array_buffer;
        }
        break;
    }
    case TYPE_KIND_POINTER: {
        const char *pointee = element_type_for_copy->element_type ? element_type_for_copy->element_type : "void";
        const char *c_pointee = gray_type_to_c_codegen(codegen, pointee);
        static char pointer_buffer[MESSAGE_BUFFER_SIZE];
        snprintf(pointer_buffer, sizeof(pointer_buffer), "%s *", c_pointee);
        c_type = pointer_buffer;
        break;
    }
    case TYPE_KIND_MAP:    c_type = "GrayMap"; break;
    case TYPE_KIND_ARRAY:  c_type = "GrayArray"; break;
    case TYPE_KIND_CHAR:   c_type = "int32_t"; break;
    default:        c_type = "int64_t"; break;
    }

    /* A declared [f32] array stores packed 4-byte float; the TYPE_KIND_FLOATING_POINT case
     * (and the integer-literal override above) otherwise emit double storage. */
    if (strcmp(c_type, "double") == 0 && declared_f32)
        c_type = "float";

    /* A non-empty integer-literal element carries TYPE_KIND_SIGNED_INTEGER regardless of the
     * declared width/signedness, so [i8..i64]/[u8..u64] would
     * otherwise fall to int64_t storage. When the declaration pins a narrower
     * or unsigned integer element, match it — the same way [f32]/[f64] above
     * do for floating-point values and empty literals already do via type_from_name. Covers
     * both the [T] and [T, N] forms. */
    if ((type_kind == TYPE_KIND_SIGNED_INTEGER || type_kind == TYPE_KIND_UNSIGNED_INTEGER) && !wide_integer_element &&
        codegen->current_variable_type && codegen->current_variable_type[0] == '[') {
        const char *current_type_spelling = codegen->current_variable_type;
        size_t current_type_spelling_length = strlen(current_type_spelling);
        if (current_type_spelling_length >= 3 && current_type_spelling[current_type_spelling_length - 1] == ']') {
            char inner[TYPE_NAME_MAX];
            size_t inner_length = current_type_spelling_length - 2;
            if (inner_length < sizeof(inner)) {
                memcpy(inner, current_type_spelling + 1, inner_length);
                inner[inner_length] = '\0';
                char *comma = strchr(inner, ',');
                if (comma) *comma = '\0';
                GrayType *inferred_element_type = type_from_name(inner);
                if (inferred_element_type && !is_wide_integer_type_name(inner) &&
                    (inferred_element_type->kind == TYPE_KIND_SIGNED_INTEGER || inferred_element_type->kind == TYPE_KIND_UNSIGNED_INTEGER))
                    c_type = gray_type_to_c_codegen(codegen, inner);
            }
        }
    }

    emit_formatted(codegen, "gray_array_from(gray_default_arena, (%s[]){", c_type);
    for (int i = 0; i < count; i++) {
        if (i > 0) emit(codegen, ", ");
        /* A wide integer element array stores gray_i128/gray_i256 values, so every
         * scalar / integer literal element needs the matching constructor. */
        if (!emit_wide_integer_coerced(codegen, wide_integer_element, node->data.array_value.elements[i]))
            emit_expression(codegen, node->data.array_value.elements[i]);
    }
    emit_formatted(codegen, "}, sizeof(%s), %d, GRAY_ELEM_KIND_OF(%s))", c_type, count, c_type);
}

static void emit_map_value(CodeGen *codegen, AstNode *node) {
    /* Map literal: emit inline construction with gray_map_set calls.
     * We need a temp variable, so wrap in a GCC statement expression. */
    int count = node->data.map_value.count;

    /* Determine key/value C types. Prefer the enclosing var/field declared
     * type when available; u8/char literals are typechecked as i64, so
     * first-pair inference would miss the declared key type. */
    const char *c_key_type = "GrayString";
    const char *c_value_type = "int64_t";
    /* The key and value types the type checker resolved for the literal. */
    GrayType *declared_map_type = type_table_get(codegen->type_table, node);
    if (!declared_map_type || declared_map_type->kind != TYPE_KIND_MAP || !declared_map_type->key_type || !declared_map_type->value_type ||
        strchr(declared_map_type->key_type, '?') || strchr(declared_map_type->value_type, '?'))
        declared_map_type = (codegen->current_variable_type &&
                   strncmp(codegen->current_variable_type, "map[", 4) == 0)
            ? type_from_name(codegen->current_variable_type) : NULL;
    /* An empty literal has neither a pair to infer from nor, in an
     * assignment, an enclosing declared type. The typechecker records the
     * element types of the context on the node itself, so use those rather
     * than defaulting to string keys and 8-byte values. */
    if (!declared_map_type && codegen->type_table) {
        GrayType *node_map_type = type_table_get(codegen->type_table, node);
        if (node_map_type && node_map_type->kind == TYPE_KIND_MAP && node_map_type->key_type && node_map_type->value_type)
            declared_map_type = node_map_type;
    }
    /* Grayscale type names for the key/value slots, tracked so a wide-integer
     * literal element can be wrapped in its constructor below. */
    const char *gray_key_type_name = NULL;
    const char *gray_value_type_name = NULL;
    if (declared_map_type && declared_map_type->key_type) {
        c_key_type = gray_map_element_c_type(codegen, declared_map_type->key_type);
        gray_key_type_name = declared_map_type->key_type;
    }
    if (declared_map_type && declared_map_type->value_type) {
        c_value_type = gray_map_element_c_type(codegen, declared_map_type->value_type);
        gray_value_type_name = declared_map_type->value_type;
    }
    if (count > 0) {
        GrayType *key_type = type_table_get(codegen->type_table, node->data.map_value.keys[0]);
        GrayType *value_type = type_table_get(codegen->type_table, node->data.map_value.values[0]);
        if (!declared_map_type && key_type) {
            c_key_type = gray_map_element_c_type(codegen, type_name(key_type));
            gray_key_type_name = type_name(key_type);
        }
        if (!declared_map_type && value_type && value_type->kind == TYPE_KIND_POINTER) {
            static char map_pointer_buffer[MESSAGE_BUFFER_SIZE];
            const char *pointee = value_type->element_type ? value_type->element_type : "void";
            snprintf(map_pointer_buffer, sizeof(map_pointer_buffer), "%s *", gray_type_to_c_codegen(codegen, pointee));
            c_value_type = map_pointer_buffer;
        } else if (!declared_map_type && value_type) {
            c_value_type = gray_map_element_c_type(codegen, type_name(value_type));
            gray_value_type_name = type_name(value_type);
        }
    }

    /* Use GCC statement expression: ({ GrayMap m = ...; gray_map_set(...); m; })
     * Capture counter before emitting values; nested map literals will
     * re-enter this case and increment the counter, so each level gets
     * a unique temp name. */
    int my_counter = codegen_next_id(codegen);
    emit_formatted(codegen, "({ GrayMap _ml%d = GRAY_MAP_NEW_OF(gray_default_arena, %s, %s, %d); ",
        my_counter, c_key_type, c_value_type, count > 4 ? count * 2 : 8);

    /* For nested map values, propagate the inner type so inner literals
     * resolve their key/value C types correctly. */
    const char *inner_variable_type = NULL;
    if (declared_map_type && declared_map_type->value_type &&
        (strncmp(declared_map_type->value_type, "map[", 4) == 0 || declared_map_type->value_type[0] == '[')) {
        inner_variable_type = declared_map_type->value_type;
    }

    for (int i = 0; i < count; i++) {
        emit_formatted(codegen, "{ %s _mk = ", c_key_type);
        emit_map_slot_value(codegen, gray_key_type_name, node->data.map_value.keys[i]);
        emit_formatted(codegen, "; %s _mv = ", c_value_type);
        if (inner_variable_type) {
            const char *saved = codegen->current_variable_type;
            codegen->current_variable_type = inner_variable_type;
            emit_expression(codegen, node->data.map_value.values[i]);
            codegen->current_variable_type = saved;
        } else {
            emit_map_slot_value(codegen, gray_value_type_name, node->data.map_value.values[i]);
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

static void emit_struct_zero_value_literal(CodeGen *codegen, const char *type_name, int depth);

/* Emits a struct field's own default value, wrapping it in the wide integer
 * constructor when the field is a wide integer, exactly as an explicit field
 * value in a struct literal is. */
static void emit_struct_field_default_value(CodeGen *codegen, StructField *struct_field) {
    if (!emit_wide_integer_coerced(codegen, struct_field->type_name, struct_field->default_value))
        emit_expression(codegen, struct_field->default_value);
}

/* Emits the zero-value default for one struct field that has no literal
 * value in scope: the field's own syntactic default if it has one, or (for
 * map/array/fixed-array/string-enum/struct fields, which C's implicit {0}
 * leaves unusable — an all-zero GrayArray/GrayMap has elem_size 0, and an
 * all-zero string-backed enum is not a valid variant) a real runtime zero
 * value. Recurses into emit_struct_zero_value_literal for a struct-typed
 * field so a nested struct gets the same treatment at any depth, instead of
 * falling through to a flat C {0}. Writes at most one designated
 * initializer, prefixed with ", " once `*emitted` is already true. */
static void emit_struct_field_zero_default(CodeGen *codegen, StructField *struct_field, int depth, bool *emitted) {
    const char *field_type_name = struct_field->type_name;
    if (struct_field->default_value) {
        if (*emitted) emit(codegen, ", ");
        *emitted = true;
        emit_formatted(codegen, ".%s = ", sanitize_name(struct_field->name));
        int default_fixed_size = extract_array_size(field_type_name);
        if (default_fixed_size > 0 && struct_field->default_value->kind == NODE_ARRAY_VALUE) {
            const char *saved_default_variable_type = codegen->current_variable_type;
            codegen->current_variable_type = field_type_name;
            const char *default_element_type = extract_array_element_type(field_type_name);
            emit_fixed_size_array_initializer(codegen, struct_field->default_value, default_element_type ? default_element_type : "i64", default_fixed_size);
            codegen->current_variable_type = saved_default_variable_type;
        } else {
            emit_struct_field_default_value(codegen, struct_field);
        }
        return;
    }
    if (!field_type_name) return;
    bool field_is_map = strncmp(field_type_name, "map[", 4) == 0;
    bool field_is_array = field_type_name[0] == '[';
    const char *string_enum = codegen_resolve_type(codegen, field_type_name);
    bool is_field_string_enum = codegen_enum_is_string(codegen, string_enum);
    GrayType *field_type = type_from_name(field_type_name);
    bool field_is_struct = !field_is_map && !field_is_array && !is_field_string_enum &&
                            field_type && field_type->kind == TYPE_KIND_STRUCT;
    if (!field_is_map && !field_is_array && !is_field_string_enum && !field_is_struct) return;
    if (*emitted) emit(codegen, ", ");
    *emitted = true;
    emit_formatted(codegen, ".%s = ", sanitize_name(struct_field->name));
    if (is_field_string_enum) {
        int enum_index = codegen_enum_index(codegen, string_enum);
        const char *field_value_text = codegen->enum_declarations[enum_index]->data.enum_declaration.values[0].name;
        emit_formatted(codegen, "GrayEnum_%s_%s", string_enum, field_value_text);
    } else if (field_is_struct) {
        emit_struct_zero_value_literal(codegen, field_type_name, depth + 1);
    } else if (field_is_map) {
        const char *c_key_type = "GrayString";
        const char *c_value_type_default = "int64_t";
        if (field_type && field_type->key_type) c_key_type = gray_map_element_c_type(codegen, field_type->key_type);
        if (field_type && field_type->value_type) c_value_type_default = gray_map_element_c_type(codegen, field_type->value_type);
        emit_formatted(codegen, "GRAY_MAP_NEW_OF(gray_default_arena, %s, %s, 8)",
            c_key_type, c_value_type_default);
    } else {
        const char *c_element_type = "int64_t";
        if (field_type && field_type->element_type) c_element_type = gray_map_element_c_type(codegen, field_type->element_type);
        int fixed_size = extract_array_size(field_type_name);
        if (fixed_size > 0) {
            emit_formatted(codegen, "gray_array_from(gray_default_arena, (%s[%d]){}, sizeof(%s), %d, GRAY_ELEM_KIND_OF(%s))",
                c_element_type, fixed_size, c_element_type, fixed_size, c_element_type);
        } else {
            emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", c_element_type);
        }
    }
}

/* Emits `(GrayStruct_X){ ... }` for a struct type with no literal at all —
 * every field its own syntactic default, or a real runtime zero value
 * instead of C's raw {0}, recursively. Used both for a struct-typed field
 * entirely omitted from an enclosing literal (via
 * emit_struct_field_zero_default's field_is_struct case) and for a
 * struct-typed variable declared with no initializer at all
 * (emit_c_zero_value), so both share the exact defaulting a direct `Type{}`
 * literal already gets right instead of falling back to a flat zero. depth
 * guards against runaway recursion through mutually-referential struct
 * fields. */
static void emit_struct_zero_value_literal(CodeGen *codegen, const char *type_name, int depth) {
    AstNode *struct_declaration = type_name ? find_struct_declaration(codegen, type_name) : NULL;
    const char *c_type = gray_type_to_c_codegen(codegen, type_name);
    if (!struct_declaration || depth > 8) {
        /* A compound literal, not a bare {0}: the latter is only valid as a
         * declaration initializer, and this is also the right-hand side of the
         * deferred assignment of a file-scope global inside gray_init_globals. */
        emit_formatted(codegen, "(%s){0}", c_type);
        return;
    }
    emit_formatted(codegen, "(%s){", c_type);
    bool emitted = false;
    for (int i = 0; i < struct_declaration->data.struct_declaration.field_count; i++)
        emit_struct_field_zero_default(codegen, &struct_declaration->data.struct_declaration.fields[i], depth, &emitted);
    emit(codegen, "}");
}

static void emit_struct_value(CodeGen *codegen, AstNode *node) {
    /* Struct literal: (GrayStruct_Name){.field = value, ...} */
    /* Resolve ? → concrete binding for type params */
    const char *struct_name_text = node->data.struct_value.name;
    if (strcmp(struct_name_text, "?") == 0 && codegen->wildcard_binding) {
        struct_name_text = codegen->wildcard_binding;
    } else if (node->resolved_declaration) {
        struct_name_text = module_mangle(codegen->modules, node->resolved_declaration);
    } else {
        codegen_enter_node(codegen, node);
        struct_name_text = codegen_resolve_type(codegen, struct_name_text);
    }
    /* Resolve unprefixed struct names from 'import and use' */
    if (struct_name_text[0] >= 'A' && struct_name_text[0] <= 'Z') {
        bool found = false;
        for (int struct_index = 0; struct_index < codegen->struct_declaration_count; struct_index++) {
            if (strcmp(codegen->struct_declarations[struct_index]->data.struct_declaration.name, struct_name_text) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            struct_name_text = codegen_resolve_type(codegen, struct_name_text);
        }
    }
    /* Wrapped in an extra pair of parens: a bare compound literal
     * (T){.a=x, .b=y} contains an unparenthesized top-level comma, which the
     * C preprocessor splits into separate macro arguments wherever this
     * value lands inside a function-like macro call (GRAY_ARRAY_SET_AT and
     * friends). ((T){.a=x, .b=y}) reads as one token run everywhere. */
    emit(codegen, "(");
    /* use mangled name for generic struct instantiations */
    if (node->data.struct_value.wildcard_binding) {
        const char *binding = node->data.struct_value.wildcard_binding;
        char mangled[MESSAGE_BUFFER_SIZE];
        mangle_generic_name(mangled, sizeof(mangled), struct_name_text, binding);
        emit_formatted(codegen, "(GrayStruct_%s){", mangled);
    } else {
        emit_formatted(codegen, "(GrayStruct_%s){", struct_name_text);
    }
    /* Look up the struct decl so we can thread each field's declared
     * type into emit_expression as current_variable_type. Without it, an
     * empty array literal in a field slot (e.g. `Bag{items: {}}`)
     * has no type context and codegen falls back to sizeof(int64_t)
     * as the element size — which subsequent arrays.append() then
     * uses as the write stride, truncating struct elements. */
    AstNode *struct_declaration_for_fields = find_struct_declaration(codegen, struct_name_text);
    for (int i = 0; i < node->data.struct_value.count; i++) {
        if (i > 0) emit(codegen, ", ");
        const char *field_name = node->data.struct_value.field_names[i];
        emit_formatted(codegen, ".%s = ", sanitize_name(field_name));
        const char *field_type = NULL;
        if (struct_declaration_for_fields) {
            for (int field_index = 0; field_index < struct_declaration_for_fields->data.struct_declaration.field_count; field_index++) {
                if (strcmp(struct_declaration_for_fields->data.struct_declaration.fields[field_index].name, field_name) == 0) {
                    field_type = struct_declaration_for_fields->data.struct_declaration.fields[field_index].type_name;
                    break;
                }
            }
        }
        if (field_type) {
            const char *saved = codegen->current_variable_type;
            codegen->current_variable_type = field_type;
            AstNode *field_value = node->data.struct_value.field_values[i];
            int fixed_size = extract_array_size(field_type);
            if (fixed_size > 0 && field_value->kind == NODE_ARRAY_VALUE) {
                /* [T,N] field: pad a partial literal to N so the field's
                 * declared length is what codegen sees, not the literal's
                 * own element count (mirrors emit_vardecl_array). */
                const char *field_element_type = extract_array_element_type(field_type);
                emit_fixed_size_array_initializer(codegen, field_value, field_element_type ? field_element_type : "i64", fixed_size);
            } else if (!emit_wide_integer_coerced(codegen, field_type, field_value)) {
                emit_expression(codegen, field_value);
            }
            codegen->current_variable_type = saved;
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
    if (struct_declaration_for_fields) codegen_enter_node(codegen, struct_declaration_for_fields);
    if (struct_declaration_for_fields) {
        for (int field_index = 0; field_index < struct_declaration_for_fields->data.struct_declaration.field_count; field_index++) {
            StructField *struct_field = &struct_declaration_for_fields->data.struct_declaration.fields[field_index];
            if (!struct_field->default_value) continue;
            if (struct_literal_specifies_field(node, struct_field->name)) continue;
            if (emitted_field) emit(codegen, ", ");
            emitted_field = true;
            emit_formatted(codegen, ".%s = ", sanitize_name(struct_field->name));
            int default_fixed_size = extract_array_size(struct_field->type_name);
            if (default_fixed_size > 0 && struct_field->default_value->kind == NODE_ARRAY_VALUE) {
                const char *saved_default_variable_type = codegen->current_variable_type;
                codegen->current_variable_type = struct_field->type_name;
                const char *default_element_type = extract_array_element_type(struct_field->type_name);
                emit_fixed_size_array_initializer(codegen, struct_field->default_value, default_element_type ? default_element_type : "i64", default_fixed_size);
                codegen->current_variable_type = saved_default_variable_type;
            } else {
                emit_struct_field_default_value(codegen, struct_field);
            }
        }
        /* Map and array fields the literal leaves out still need a real
         * table. C zero-fills them, and a zero-filled GrayMap/GrayArray has
         * capacity and element sizes of 0, so inserts are silently dropped.
         * new(Type) initialises these for the same reason — see
         * emit_new_expression. */
        for (int field_index = 0; field_index < struct_declaration_for_fields->data.struct_declaration.field_count; field_index++) {
            StructField *struct_field = &struct_declaration_for_fields->data.struct_declaration.fields[field_index];
            const char *field_type_name = struct_field->type_name;
            if (!field_type_name || struct_field->default_value) continue;
            bool field_is_map = strncmp(field_type_name, "map[", 4) == 0;
            bool field_is_array = (field_type_name[0] == '[');
            /* A string-backed enum is a GrayString at the C level, so a
             * zero-filled field is an empty string, not a valid variant.
             * Seed it with the first variant, matching new(EnumType). */
            const char *string_enum = codegen_resolve_type(codegen, field_type_name);
            bool is_field_string_enum = codegen_enum_is_string(codegen, string_enum);
            GrayType *field_type = type_from_name(field_type_name);
            /* A struct-typed field left out entirely still needs its own
             * fixed-array/map/string-enum fields defaulted the same way — an
             * omitted nested struct otherwise falls through to a flat C
             * {0}, dropping any fixed-size array field inside it to length
             * 0 instead of its declared N (e.g. Outer{} omitting an `inner
             * Inner` field whose own `data [i64,3]` field then reads back
             * as a 0-length array). */
            bool field_is_struct = !field_is_map && !field_is_array && !is_field_string_enum &&
                                    field_type && field_type->kind == TYPE_KIND_STRUCT;
            if (!field_is_map && !field_is_array && !is_field_string_enum && !field_is_struct) continue;
            if (struct_literal_specifies_field(node, struct_field->name)) continue;
            if (emitted_field) emit(codegen, ", ");
            emitted_field = true;
            emit_formatted(codegen, ".%s = ", sanitize_name(struct_field->name));
            if (is_field_string_enum) {
                int enum_index = codegen_enum_index(codegen, string_enum);
                const char *first_variant_name = codegen->enum_declarations[enum_index]->data.enum_declaration.values[0].name;
                emit_formatted(codegen, "GrayEnum_%s_%s", string_enum, first_variant_name);
                continue;
            }
            if (field_is_struct) {
                emit_struct_zero_value_literal(codegen, field_type_name, 1);
                continue;
            }
            if (field_is_map) {
                const char *c_key_type = "GrayString";
                const char *c_value_type_default = "int64_t";
                if (field_type && field_type->key_type) c_key_type = gray_map_element_c_type(codegen, field_type->key_type);
                if (field_type && field_type->value_type) c_value_type_default = gray_map_element_c_type(codegen, field_type->value_type);
                emit_formatted(codegen, "GRAY_MAP_NEW_OF(gray_default_arena, %s, %s, 8)",
                    c_key_type, c_value_type_default);
            } else {
                const char *c_element_type = "int64_t";
                if (field_type && field_type->element_type) c_element_type = gray_map_element_c_type(codegen, field_type->element_type);
                /* A [T,N] field omitted entirely is still a zero-valued
                 * array of length N, not an empty dynamic array —
                 * gray_array_new's capacity argument doesn't set length,
                 * so Buffer{} has to build the same zero-filled [T;N]
                 * compound literal an empty `= {}` initializer would. */
                int fixed_size = extract_array_size(field_type_name);
                if (fixed_size > 0) {
                    emit_formatted(codegen, "gray_array_from(gray_default_arena, (%s[%d]){}, sizeof(%s), %d, GRAY_ELEM_KIND_OF(%s))",
                        c_element_type, fixed_size, c_element_type, fixed_size, c_element_type);
                } else {
                    emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", c_element_type);
                }
            }
        }
    }
    codegen->current_module = saved_struct_module;
    codegen->current_file = saved_struct_file;
    emit(codegen, "})");
}

static void emit_prefix_expression(CodeGen *codegen, AstNode *node) {
    /* Bigint negation */
    if (node->data.prefix.operator == TOKEN_MINUS) {
        const char *wide_integer_type = resolve_wide_integer_type(codegen, node->data.prefix.right);
        if (wide_integer_type && (strcmp(wide_integer_type, "i128") == 0 || strcmp(wide_integer_type, "i256") == 0)) {
            emit_formatted(codegen, "%s_neg_checked(", wide_integer_prefix(wide_integer_type));
            emit_expression(codegen, node->data.prefix.right);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
            return;
        }
    }
    /* Overflow-checked negation for signed integer types; STANDARD §3.1.1
     * promises arithmetic panics rather than silent wrap on overflow. */
    if (node->data.prefix.operator == TOKEN_MINUS) {
        GrayType *operand_type = type_table_get(codegen->type_table, node->data.prefix.right);
        if (operand_type && operand_type->kind == TYPE_KIND_SIGNED_INTEGER) {
            emit_checked_negation(codegen, operand_type, node->data.prefix.right, NULL, node->token.line);
            return;
        }
    }
    /* bit_not → ~ ; operands narrower than C's int (u8, u16) must be
     * masked back to their width because C promotes them to int before
     * applying ~, yielding a negative value that fails the runtime range check. */
    if (node->data.prefix.operator == TOKEN_BIT_NOT) {
        const char *bitwise_not_wide_type = resolve_wide_integer_type(codegen, node->data.prefix.right);
        if (bitwise_not_wide_type) {
            emit_formatted(codegen, "%s_not(", wide_integer_prefix(bitwise_not_wide_type));
            emit_expression(codegen, node->data.prefix.right);
            emit(codegen, ")");
            return;
        }
        GrayType *bitwise_not_type = type_table_get(codegen->type_table, node->data.prefix.right);
        const char *bitwise_not_mask = NULL;
        if (bitwise_not_type && bitwise_not_type->name) {
            if (strcmp(bitwise_not_type->name, "u8") == 0)  bitwise_not_mask = "uint8_t";
            else if (strcmp(bitwise_not_type->name, "u16") == 0) bitwise_not_mask = "uint16_t";
        }
        if (bitwise_not_mask) {
            emit_formatted(codegen, "((%s)(~(", bitwise_not_mask);
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
    emit(codegen, operator_to_c_string(node->data.prefix.operator));
    /* Wrap infix operands in parens so !(a && b) emits as (!(a && b)) not (!a && b) */
    bool wrap = (node->data.prefix.right->kind == NODE_INFIX_EXPRESSION);
    if (wrap) emit(codegen, "(");
    emit_expression(codegen, node->data.prefix.right);
    if (wrap) emit(codegen, ")");
    emit(codegen, ")");
}

/* The number type the operation at `node` is computed in: its resolved
 * type, or the active binding inside a generic instantiation. */
static GrayType *codegen_operation_type(CodeGen *codegen, AstNode *node) {
    GrayType *type = type_table_get(codegen->type_table, node);
    if ((!type || type->kind == TYPE_KIND_UNKNOWN) && codegen->wildcard_binding)
        type = type_from_name(codegen->wildcard_binding);
    return type;
}

/* One operand of an arithmetic operation computed in `type`: the C
 * expression `operand_c` when given, otherwise `operand`, widened into a
 * wide integer type when the operation is one. */
static void emit_arithmetic_operand(CodeGen *codegen, AstNode *operand, const char *operand_c_text,
                                    GrayType *type) {
    if (operand_c_text) {
        emit(codegen, operand_c_text);
    } else if (type && type->name && is_wide_integer_type_name(type->name)) {
        emit_wide_integer_operand(codegen, operand, wide_integer_prefix(type->name), type->name,
            type_table_get(codegen->type_table, operand));
    } else {
        emit(codegen, "(");
        emit_expression(codegen, operand);
        emit(codegen, ")");
    }
}

/* Emit `left op right` for an arithmetic operator (+ - * / %) computed in
 * `type`, the one place that decides each type's checks: overflow on + - *,
 * division by zero and TYPE_MIN / -1 on / and %. Each operand is an AST node,
 * or a C expression string when the caller already holds it (compound
 * assignment). */
static void emit_arithmetic(CodeGen *codegen, TokenType operator, GrayType *type,
                            AstNode *left, const char *left_c,
                            AstNode *right, const char *right_c, AstNode *location_node) {
    const char *name = type ? type->name : NULL;
    bool is_division = operator == TOKEN_SLASH || operator == TOKEN_PERCENT;
    const char *c_operator = operator == TOKEN_PLUS ? "+" : operator == TOKEN_MINUS ? "-" :
                       operator == TOKEN_ASTERISK ? "*" : operator == TOKEN_SLASH ? "/" : "%";
    if (name && is_wide_integer_type_name(name)) {
        const char *function_name = operator == TOKEN_PLUS ? "add_checked" : operator == TOKEN_MINUS ? "sub_checked" :
                         operator == TOKEN_ASTERISK ? "mul_checked" : operator == TOKEN_SLASH ? "div" : "mod";
        emit_formatted(codegen, "%s_%s(", wide_integer_prefix(name), function_name);
        emit_arithmetic_operand(codegen, left, left_c, type);
        emit(codegen, ", ");
        emit_arithmetic_operand(codegen, right, right_c, type);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, location_node->token.line);
        return;
    }
    /* A file-scope constant initializer must be a C constant expression;
     * the type checker has folded or range-checked what reaches here. */
    bool checked = type && (type_kind_is_number(type->kind) || type->kind == TYPE_KIND_CHAR) &&
                   !codegen->is_in_const_declaration;
    if (!checked || (type->kind == TYPE_KIND_FLOATING_POINT && !is_division)) {
        emit(codegen, "(");
        emit_arithmetic_operand(codegen, left, left_c, type);
        emit_formatted(codegen, " %s ", c_operator);
        emit_arithmetic_operand(codegen, right, right_c, type);
        emit(codegen, ")");
        return;
    }
    if (type->kind == TYPE_KIND_FLOATING_POINT) {
        /* Grayscale panics on a zero divisor rather than yield an IEEE inf. */
        const char *c_floating_point_type = strcmp(name, "f32") == 0 ? "float" : "double";
        emit_formatted(codegen, "({ %s _dv = ", c_floating_point_type);
        emit_arithmetic_operand(codegen, right, right_c, type);
        emit_formatted(codegen, "; if (_dv == 0) { %s; } (%s)", panic_call(codegen, location_node, "P0078", ""), c_floating_point_type);
        emit_arithmetic_operand(codegen, left, left_c, type);
        emit(codegen, " / _dv; })");
        return;
    }
    bool is_unsigned = type->kind == TYPE_KIND_UNSIGNED_INTEGER;
    const char *source_minimum = NULL, *source_maximum = NULL;
    bool sized_unsigned = false;
    bool sized = name && integer_type_name_bounds(name, &source_minimum, &source_maximum, &sized_unsigned);
    if (is_division) {
        emit(codegen, "({ __auto_type _dv = ");
        emit_arithmetic_operand(codegen, right, right_c, type);
        emit_formatted(codegen, "; if (!_dv) { %s; } ", panic_call(codegen, location_node, "P0078", ""));
        if (is_unsigned) {
            emit_arithmetic_operand(codegen, left, left_c, type);
            emit_formatted(codegen, " %s _dv; })", c_operator);
        } else {
            /* TYPE_MIN / -1 overflows; in C it is undefined. */
            emit(codegen, "__auto_type _dn = ");
            emit_arithmetic_operand(codegen, left, left_c, type);
            emit_formatted(codegen, "; if ((int64_t)_dn == %s && _dv == -1) { %s; } _dn %s _dv; })",
                source_minimum ? source_minimum : "(-9223372036854775807LL - 1)",
                panic_call(codegen, location_node, "P0079", operator == TOKEN_SLASH ? ", \"division\"" : ", \"modulo\""),
                c_operator);
        }
        return;
    }
    const char *verb = operator == TOKEN_PLUS ? "add" : operator == TOKEN_MINUS ? "sub" : "mul";
    if (sized)
        emit_formatted(codegen, "gray_%ssized_%s_check(", sized_unsigned ? "u" : "", verb);
    else
        emit_formatted(codegen, "gray_%s%s_check(", is_unsigned ? "u" : "", verb);
    emit_arithmetic_operand(codegen, left, left_c, type);
    emit(codegen, ", ");
    emit_arithmetic_operand(codegen, right, right_c, type);
    emit(codegen, ", ");
    if (sized) emit_sized_bounds_arguments(codegen, source_minimum, source_maximum, sized_unsigned, name, location_node->token.line);
    else emit_formatted(codegen, "\"%s\", %d", codegen->file, location_node->token.line);
    emit(codegen, ")");
}

static void emit_arrays_value_call(CodeGen *codegen, const char *function_name, AstNode *array_argument,
                                   AstNode *value_argument);

static void emit_infix_expression(CodeGen *codegen, AstNode *node) {
    TokenType operator = node->data.infix.operator;

    /* Check if either operand is a string; need special handling */
    GrayType *left_type = type_table_get(codegen->type_table, node->data.infix.left);
    GrayType *right_type = type_table_get(codegen->type_table, node->data.infix.right);
    /* Inside a generic instantiation, operands that were
     * typed TYPE_KIND_UNKNOWN in the main pass (because they traced back
     * to a '?' parameter) should be treated as the active wildcard
     * binding so string/struct comparisons pick up the right path. */
    if (codegen && codegen->wildcard_binding) {
        static GrayType wildcard_type_static;
        GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
        if (wildcard_type) { wildcard_type_static = *wildcard_type; wildcard_type = &wildcard_type_static; }
        if (!left_type || left_type->kind == TYPE_KIND_UNKNOWN) left_type = wildcard_type;
        if (!right_type || right_type->kind == TYPE_KIND_UNKNOWN) right_type = wildcard_type;
    }
    bool is_left_string = (left_type && left_type->kind == TYPE_KIND_STRING) || node->data.infix.left->kind == NODE_STRING_VALUE;
    bool is_right_string = (right_type && right_type->kind == TYPE_KIND_STRING) || node->data.infix.right->kind == NODE_STRING_VALUE;
    /* Also treat string enum operands as strings for comparison purposes */
    if (left_type && left_type->kind == TYPE_KIND_ENUM && left_type->name &&
        codegen_enum_is_string(codegen, left_type->name)) is_left_string = true;
    if (right_type && right_type->kind == TYPE_KIND_ENUM && right_type->name &&
        codegen_enum_is_string(codegen, right_type->name)) is_right_string = true;

    if ((is_left_string || is_right_string) && operator == TOKEN_EQUAL) {
        emit(codegen, "gray_string_eq(");
        emit_expression(codegen, node->data.infix.left);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }
    if ((is_left_string || is_right_string) && operator == TOKEN_NOT_EQUAL) {
        emit(codegen, "!gray_string_eq(");
        emit_expression(codegen, node->data.infix.left);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }
    /* string + string: concatenate into a new GrayString. Chained
     * a + b + c nests naturally since '+' is left-associative. */
    if (is_left_string && is_right_string && operator == TOKEN_PLUS) {
        emit(codegen, "gray_string_concat(gray_default_arena, ");
        emit_expression(codegen, node->data.infix.left);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }

    /* Bitwise keyword operators → C bitwise operators */
    if (operator == TOKEN_BIT_AND || operator == TOKEN_BIT_OR || operator == TOKEN_BIT_XOR) {
        /* A wide result type makes the operation wide; a narrower operand is
         * widened to it, as for the arithmetic operators. */
        GrayType *bitwise_type = codegen_operation_type(codegen, node);
        const char *wide = bitwise_type && (bitwise_type->kind == TYPE_KIND_SIGNED_INTEGER || bitwise_type->kind == TYPE_KIND_UNSIGNED_INTEGER)
            ? wide_integer_type_name(bitwise_type->name) : NULL;
        if (wide) {
            const char *prefix = wide_integer_prefix(wide);
            emit_formatted(codegen, "%s_%s(", prefix,
                operator == TOKEN_BIT_AND ? "and" : operator == TOKEN_BIT_OR ? "or" : "xor");
            emit_wide_integer_operand(codegen, node->data.infix.left, prefix, wide, left_type);
            emit(codegen, ", ");
            emit_wide_integer_operand(codegen, node->data.infix.right, prefix, wide, right_type);
            emit(codegen, ")");
            return;
        }
        const char *c_operator = operator_to_c_string(operator);
        emit(codegen, "(");
        emit_expression(codegen, node->data.infix.left);
        emit_formatted(codegen, " %s ", c_operator);
        emit_expression(codegen, node->data.infix.right);
        emit(codegen, ")");
        return;
    }
    /* Bit shift operators with runtime bounds check.
     * The result is the left operand's type, so the shift happens at that
     * width: C would otherwise promote an 8/16-bit operand to int and keep
     * the bits shifted past its top. A shift amount that is negative or not
     * below the operand's bit width is undefined behavior in C. A left
     * shift goes through the unsigned type so shifting into or past the
     * sign bit is defined. Capture the amount once, validate it, then shift. */
    if (operator == TOKEN_BIT_SHIFT_LEFT || operator == TOKEN_BIT_SHIFT_RIGHT) {
        const char *shifted_wide = resolve_wide_integer_type(codegen, node->data.infix.left);
        if (shifted_wide) {
            emit_formatted(codegen, "%s_%s(", wide_integer_prefix(shifted_wide),
                operator == TOKEN_BIT_SHIFT_LEFT ? "shl" : "shr");
            emit_expression(codegen, node->data.infix.left);
            emit(codegen, ", ");
            emit_shift_amount(codegen, node->data.infix.right, strstr(shifted_wide, "256") ? 255 : 127);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
            return;
        }
        GrayType *shift_type = type_table_get(codegen->type_table, node->data.infix.left);
        const char *shift_name = (shift_type && shift_type->kind == TYPE_KIND_CHAR) ? "i32"
            : (shift_type && (shift_type->kind == TYPE_KIND_SIGNED_INTEGER || shift_type->kind == TYPE_KIND_UNSIGNED_INTEGER)) ? shift_type->name : NULL;
        int bits = 64;
        const char *c_type = (shift_type && shift_type->kind == TYPE_KIND_UNSIGNED_INTEGER) ? "uint64_t" : "int64_t";
        const char *unsigned_c_type = "uint64_t";
        if (shift_name) {
            if (strcmp(shift_name, "i8") == 0)       { bits = 8;  c_type = "int8_t";   unsigned_c_type = "uint8_t"; }
            else if (strcmp(shift_name, "u8") == 0)  { bits = 8;  c_type = "uint8_t";  unsigned_c_type = "uint8_t"; }
            else if (strcmp(shift_name, "i16") == 0) { bits = 16; c_type = "int16_t";  unsigned_c_type = "uint16_t"; }
            else if (strcmp(shift_name, "u16") == 0) { bits = 16; c_type = "uint16_t"; unsigned_c_type = "uint16_t"; }
            else if (strcmp(shift_name, "i32") == 0) { bits = 32; c_type = "int32_t";  unsigned_c_type = "uint32_t"; }
            else if (strcmp(shift_name, "u32") == 0) { bits = 32; c_type = "uint32_t"; unsigned_c_type = "uint32_t"; }
        }
        char panic_arguments[32];
        snprintf(panic_arguments, sizeof(panic_arguments), ", (long long)_sa, %d", bits - 1);
        emit(codegen, "({ int64_t _sa = ");
        emit_shift_amount(codegen, node->data.infix.right, bits - 1);
        emit_formatted(codegen, "; if (_sa < 0 || _sa >= %d) { %s; } (%s)((%s)(",
            bits, panic_call(codegen, node, "P0092", panic_arguments), c_type,
            operator == TOKEN_BIT_SHIFT_LEFT ? unsigned_c_type : c_type);
        emit_expression(codegen, node->data.infix.left);
        emit_formatted(codegen, ") %s (int)_sa); })", operator_to_c_string(operator));
        return;
    }

    /* in / not_in; array or range membership check */
    if (operator == TOKEN_IN || operator == TOKEN_NOT_IN) {
        bool negated = (operator == TOKEN_NOT_IN);

        /* Check if right side is a range expression: x in range(a, b) */
        if (node->data.infix.right->kind == NODE_RANGE_EXPRESSION) {
            AstNode *range = node->data.infix.right;
            const char *wide = resolve_wide_integer_type(codegen, node->data.infix.left);
            if (!wide && range->data.range_expression.start)
                wide = resolve_wide_integer_type(codegen, range->data.range_expression.start);
            if (!wide) wide = resolve_wide_integer_type(codegen, range->data.range_expression.end);
            if (!wide && range->data.range_expression.step)
                wide = resolve_wide_integer_type(codegen, range->data.range_expression.step);
            if (wide) {
                const char *prefix = wide_integer_prefix(wide);
                GrayType *left_operand_type = type_table_get(codegen->type_table, node->data.infix.left);
                if (negated) emit(codegen, "!(");
                emit_formatted(codegen, "(%s_ge(", prefix);
                emit_wide_integer_operand(codegen, node->data.infix.left, prefix, wide, left_operand_type);
                emit(codegen, ", ");
                if (range->data.range_expression.start) {
                    emit_wide_integer_operand(codegen, range->data.range_expression.start, prefix, wide, NULL);
                } else {
                    emit_formatted(codegen, "%s_from_u64(0)", prefix);
                }
                emit_formatted(codegen, ") && %s_lt(", prefix);
                emit_wide_integer_operand(codegen, node->data.infix.left, prefix, wide, left_operand_type);
                emit(codegen, ", ");
                emit_wide_integer_operand(codegen, range->data.range_expression.end, prefix, wide, NULL);
                emit(codegen, ")");
                /* Step check: value must be at a step interval from start */
                if (range->data.range_expression.step) {
                    emit_formatted(codegen, " && %s_eq(%s_mod(%s_sub(", prefix, prefix, prefix);
                    emit_wide_integer_operand(codegen, node->data.infix.left, prefix, wide, left_operand_type);
                    emit(codegen, ", ");
                    if (range->data.range_expression.start) {
                        emit_wide_integer_operand(codegen, range->data.range_expression.start, prefix, wide, NULL);
                    } else {
                        emit_formatted(codegen, "%s_from_u64(0)", prefix);
                    }
                    emit(codegen, "), ");
                    emit_wide_integer_operand(codegen, range->data.range_expression.step, prefix, wide, NULL);
                    emit_formatted(codegen, ", \"%s\", %d), %s_from_u64(0))",
                        codegen->file, node->token.line, prefix);
                }
                emit(codegen, ")");
                if (negated) emit(codegen, ")");
                return;
            }
            if (negated) emit(codegen, "!(");
            emit(codegen, "(");
            emit_expression(codegen, node->data.infix.left);
            emit(codegen, " >= ");
            if (range->data.range_expression.start) {
                emit_expression(codegen, range->data.range_expression.start);
            } else {
                emit(codegen, "0");
            }
            emit(codegen, " && ");
            emit_expression(codegen, node->data.infix.left);
            emit(codegen, " < ");
            emit_expression(codegen, range->data.range_expression.end);
            /* Step check: value must be at a step interval from start */
            if (range->data.range_expression.step) {
                emit(codegen, " && (");
                emit_expression(codegen, node->data.infix.left);
                emit(codegen, " - ");
                if (range->data.range_expression.start) {
                    emit_expression(codegen, range->data.range_expression.start);
                } else {
                    emit(codegen, "0");
                }
                emit(codegen, ") % ");
                emit_expression(codegen, range->data.range_expression.step);
                emit(codegen, " == 0");
            }
            emit(codegen, ")");
            if (negated) emit(codegen, ")");
            return;
        }

        /* Map or array membership */
        GrayType *array_type = type_table_get(codegen->type_table, node->data.infix.right);
        /* Map membership: key in map → gray_maps_has_key
         * Bind the map to a temp so &_im works even when the map
         * expression is an rvalue (e.g. pointer field access). */
        if (array_type && array_type->kind == TYPE_KIND_MAP) {
            codegen->needs_maps_header = true;
            int membership_id = codegen_next_id(codegen);
            if (negated) emit(codegen, "!");
            emit_formatted(codegen, "({ __auto_type _im%d = ", membership_id);
            emit_expression(codegen, node->data.infix.right);
            emit_formatted(codegen, "; %s _ik%d = ", gray_map_element_c_type(codegen, array_type->key_type), membership_id);
            emit_map_slot_value(codegen, array_type->key_type, node->data.infix.left);
            emit_formatted(codegen, "; gray_maps_has_key(&_im%d, &_ik%d); })", membership_id, membership_id);
            return;
        }
        /* String membership: char in string or string in string */
        if (array_type && array_type->kind == TYPE_KIND_STRING) {
            GrayType *left_operand_type = type_table_get(codegen->type_table, node->data.infix.left);
            if (left_operand_type && left_operand_type->kind == TYPE_KIND_CHAR) {
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
                codegen->needs_strings_header = true;
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
        codegen->needs_arrays_header = true;
        emit_arrays_value_call(codegen, "gray_arrays_contains", node->data.infix.right,
            node->data.infix.left);
        return;
    }

    /* Arithmetic is computed in the expression's resolved type. */
    if (operator == TOKEN_PLUS || operator == TOKEN_MINUS || operator == TOKEN_ASTERISK ||
        operator == TOKEN_SLASH || operator == TOKEN_PERCENT) {
        emit_arithmetic(codegen, operator, codegen_operation_type(codegen, node),
            node->data.infix.left, NULL, node->data.infix.right, NULL, node);
        return;
    }

    /* A comparison of wide integers compares at the wider operand's type. */
    if (operator == TOKEN_EQUAL || operator == TOKEN_NOT_EQUAL || operator == TOKEN_LESS_THAN || operator == TOKEN_GREATER_THAN ||
        operator == TOKEN_LESS_THAN_OR_EQUAL || operator == TOKEN_GREATER_THAN_OR_EQUAL) {
        const char *left_wide_integer = resolve_wide_integer_type(codegen, node->data.infix.left);
        const char *right_wide_integer = resolve_wide_integer_type(codegen, node->data.infix.right);
        const char *wide_integer_type = left_wide_integer && right_wide_integer
            ? (integer_type_name_width_rank(right_wide_integer) > integer_type_name_width_rank(left_wide_integer) ? right_wide_integer : left_wide_integer)
            : (left_wide_integer ? left_wide_integer : right_wide_integer);
        if (wide_integer_type) {
            const char *prefix = wide_integer_prefix(wide_integer_type);
            const char *function_operator = operator == TOKEN_EQUAL ? "eq" : operator == TOKEN_NOT_EQUAL ? "ne" : operator == TOKEN_LESS_THAN ? "lt" :
                                operator == TOKEN_GREATER_THAN ? "gt" : operator == TOKEN_LESS_THAN_OR_EQUAL ? "le" : "ge";
            emit_formatted(codegen, "%s_%s(", prefix, function_operator);
            emit_wide_integer_operand(codegen, node->data.infix.left, prefix, wide_integer_type, left_type);
            emit(codegen, ", ");
            emit_wide_integer_operand(codegen, node->data.infix.right, prefix, wide_integer_type, right_type);
            emit(codegen, ")");
            return;
        }
    }

    /* Normal infix; always wrap sub-infix expressions in parens to
     * preserve the precedence the parser established via the AST shape.
     * Without this, (x + y) * z would emit as x + y * z. */
    bool is_left_infix = (node->data.infix.left->kind == NODE_INFIX_EXPRESSION);
    bool is_right_infix = (node->data.infix.right->kind == NODE_INFIX_EXPRESSION);
    if (is_left_infix) emit(codegen, "(");
    emit_expression(codegen, node->data.infix.left);
    if (is_left_infix) emit(codegen, ")");
    emit_formatted(codegen, " %s ", operator_to_c_string(operator));
    if (is_right_infix) emit(codegen, "(");
    emit_expression(codegen, node->data.infix.right);
    if (is_right_infix) emit(codegen, ")");
}

/* Emit the address of the place `target` names — a variable, an array
 * element, a map value, a field, or a pointer's target, through any chain of
 * those — as a C pointer to the place's type. Each step is checked once on
 * the way: a nil pointer, an index out of bounds or written during for_each,
 * a missing map key. */
static void emit_place_address(CodeGen *codegen, AstNode *target) {
    if (target->kind == NODE_POSTFIX_EXPRESSION && target->data.postfix.operator == TOKEN_CARET) {
        AstNode *pointer_node = target->data.postfix.left;
        GrayType *pointer_type = type_table_get(codegen->type_table, pointer_node);
        const char *pointee = pointer_type && pointer_type->kind == TYPE_KIND_POINTER ? pointer_type->element_type : NULL;
        char c_pointee[TYPE_NAME_MAX];
        snprintf(c_pointee, sizeof(c_pointee), "%s", gray_type_to_c_codegen(codegen, pointee));
        bool is_raw = pointer_node->kind == NODE_LABEL && is_raw_variable(codegen, pointer_node->data.label.value);
        AstNode *mem_arena = (!is_raw && pointer_node->kind == NODE_LABEL)
            ? is_mem_tracked_variable(codegen, pointer_node->data.label.value) : NULL;
        emit_formatted(codegen, "((%s *)", c_pointee);
        if (!is_raw) emit(codegen, "gray_ptr_check((void *)(");
        else emit(codegen, "(");
        if (mem_arena) emit_mem_check_live_open(codegen, mem_arena);
        emit_expression(codegen, pointer_node);
        if (mem_arena) emit_mem_check_live_close(codegen, target->token.line);
        if (!is_raw) emit_formatted(codegen, "), \"%s\", %d))", codegen->file, target->token.line);
        else emit(codegen, "))");
        return;
    }
    if (target->kind == NODE_INDEX_EXPRESSION) {
        AstNode *left = target->data.index_expression.left;
        AstNode *index = target->data.index_expression.index;
        GrayType *left_type = type_table_get(codegen->type_table, left);
        if (left_type && left_type->kind == TYPE_KIND_ARRAY && left_type->element_type) {
            char c_element_type[TYPE_NAME_MAX];
            snprintf(c_element_type, sizeof(c_element_type), "%s", gray_type_to_c_codegen(codegen, left_type->element_type));
            emit(codegen, "GRAY_ARRAY_PTR_FOR_WRITE(");
            emit_place_address(codegen, left);
            emit_formatted(codegen, ", %s, ", c_element_type);
            emit_expression(codegen, index);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, target->token.line);
            return;
        }
        if (left_type && left_type->kind == TYPE_KIND_MAP && left_type->key_type && left_type->value_type) {
            char c_key_type[TYPE_NAME_MAX], c_value_type_buffer[TYPE_NAME_MAX];
            snprintf(c_key_type, sizeof(c_key_type), "%s", gray_map_element_c_type(codegen, left_type->key_type));
            snprintf(c_value_type_buffer, sizeof(c_value_type_buffer), "%s", gray_map_element_c_type(codegen, left_type->value_type));
            emit_formatted(codegen, "({ %s _pk = ", c_key_type);
            emit_map_slot_value(codegen, left_type->key_type, index);
            emit(codegen, "; void *_pv = gray_map_get(");
            emit_place_address(codegen, left);
            emit_formatted(codegen, ", &_pk); if (!_pv) { %s; } (%s *)_pv; })",
                panic_call(codegen, target, "P0081", ""), c_value_type_buffer);
            return;
        }
    }
    if (target->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *object = target->data.member.object;
        GrayType *object_type = type_table_get(codegen->type_table, object);
        const char *field = sanitize_name(target->data.member.member);
        bool is_reference = object->kind == NODE_LABEL && is_reference_variable(codegen, object->data.label.value);
        if (object_type && object_type->kind == TYPE_KIND_POINTER && !is_reference && object_type->element_type) {
            bool is_raw = object->kind == NODE_LABEL && is_raw_variable(codegen, object->data.label.value);
            char c_struct[TYPE_NAME_MAX];
            snprintf(c_struct, sizeof(c_struct), "%s", gray_type_to_c_codegen(codegen, object_type->element_type));
            emit_formatted(codegen, "(&((%s *)", c_struct);
            if (!is_raw) emit(codegen, "gray_ptr_check((void *)(");
            else emit(codegen, "(");
            emit_expression(codegen, object);
            if (!is_raw) emit_formatted(codegen, "), \"%s\", %d))", codegen->file, target->token.line);
            else emit(codegen, "))");
            emit_formatted(codegen, "->%s)", field);
            return;
        }
        if (object_type && object_type->kind == TYPE_KIND_STRUCT) {
            emit(codegen, "(&(");
            emit_place_address(codegen, object);
            emit_formatted(codegen, ")->%s)", field);
            return;
        }
    }
    /* A variable (or any other lvalue expression C can address directly). */
    emit(codegen, "(&(");
    emit_expression(codegen, target);
    emit(codegen, "))");
}

/* `target op= value` (op + - * / %) for a number target, and `target++` /
 * `target--` (`value` NULL): the target's address is taken once, then the new
 * value is computed by emit_arithmetic in the target's type, so every shape of
 * target gets the same checks. Returns false for a target that is not a
 * number. */
static bool emit_compound_arithmetic(CodeGen *codegen, AstNode *target, TokenType operator,
                                     AstNode *value, AstNode *location_node) {
    GrayType *target_type = type_table_get(codegen->type_table, target);
    if (!target_type || (!type_kind_is_number(target_type->kind) && target_type->kind != TYPE_KIND_CHAR))
        return false;
    char c_type[TYPE_NAME_MAX];
    snprintf(c_type, sizeof(c_type), "%s", gray_type_to_c_codegen(codegen, type_name(target_type)));
    emit_formatted(codegen, "({ %s *_lv = ", c_type);
    emit_place_address(codegen, target);
    emit(codegen, "; *_lv = ");
    const char *one_literal = NULL;
    if (!value) {
        if (target_type->name && is_wide_integer_type_name(target_type->name)) {
            static char wide_one[64];
            snprintf(wide_one, sizeof(wide_one), "%s_from_u64(1)", wide_integer_prefix(target_type->name));
            one_literal = wide_one;
        } else {
            one_literal = "1";
        }
    }
    emit_arithmetic(codegen, operator, target_type, NULL, "(*_lv)", value, one_literal, location_node);
    emit(codegen, "; })");
    return true;
}

static void emit_postfix_expression(CodeGen *codegen, AstNode *node) {
    if (node->data.postfix.operator == TOKEN_CARET) {
        AstNode *dereferenced_left = node->data.postfix.left;
        bool is_raw_deref = (dereferenced_left->kind == NODE_LABEL &&
                             is_raw_variable(codegen, dereferenced_left->data.label.value));
        if (!is_raw_deref && dereferenced_left->kind == NODE_CALL_EXPRESSION &&
            dereferenced_left->data.call.function->kind == NODE_LABEL &&
            strcmp(dereferenced_left->data.call.function->data.label.value, "raw") == 0) {
            is_raw_deref = true;
        }
        /* A raw() pointer bypasses every guardrail, including arena
         * liveness — same philosophy as its existing nil-check bypass. */
        AstNode *mem_arena = (!is_raw_deref && dereferenced_left->kind == NODE_LABEL)
            ? is_mem_tracked_variable(codegen, dereferenced_left->data.label.value) : NULL;
        if (is_raw_deref) {
            /* Raw pointer: bare dereference, no nil check */
            emit(codegen, "(*");
            emit_expression(codegen, dereferenced_left);
            emit(codegen, ")");
        } else {
            /* Pointer dereference: p^ → (*p) with nil check. Route through
             * gray_ptr_check so `*p` stays an lvalue — a following `.field`
             * chain or `&` then lands on the real storage rather than a
             * by-value statement-expression copy. */
            GrayType *dereferenced_type = type_table_get(codegen->type_table, dereferenced_left);
            const char *deref_pointee = (dereferenced_type && dereferenced_type->kind == TYPE_KIND_POINTER)
                ? dereferenced_type->element_type : NULL;
            if (deref_pointee && deref_pointee[0] != '^' && !strchr(deref_pointee, '?')) {
                char c_pointee[TYPE_NAME_MAX];
                snprintf(c_pointee, sizeof(c_pointee), "%s",
                    gray_type_to_c_codegen(codegen, deref_pointee));
                emit_formatted(codegen, "(*(%s *)gray_ptr_check((void *)(", c_pointee);
                if (mem_arena) emit_mem_check_live_open(codegen, mem_arena);
                emit_expression(codegen, dereferenced_left);
                if (mem_arena) emit_mem_check_live_close(codegen, node->token.line);
                emit_formatted(codegen, "), \"%s\", %d))", codegen->file, node->token.line);
            } else {
                emit(codegen, "({ __auto_type _dp = ");
                emit_expression(codegen, dereferenced_left);
                emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
                if (mem_arena) {
                    emit(codegen, "*(__typeof__(_dp))gray_mem_check_live(");
                    emit_expression(codegen, mem_arena);
                    emit_formatted(codegen, ", (void *)_dp, \"%s\", %d); })", codegen->file, node->token.line);
                } else {
                    emit(codegen, "*_dp; })");
                }
            }
        }
    } else if ((node->data.postfix.operator == TOKEN_INCREMENT || node->data.postfix.operator == TOKEN_DECREMENT) &&
               emit_compound_arithmetic(codegen, node->data.postfix.left,
                   node->data.postfix.operator == TOKEN_INCREMENT ? TOKEN_PLUS : TOKEN_MINUS, NULL, node)) {
        /* x++ / x-- is x += 1 / x -= 1, whatever shape x has. */
    } else {
        emit_expression(codegen, node->data.postfix.left);
        emit(codegen, operator_to_c_string(node->data.postfix.operator));
    }
}

static void emit_function_reference(CodeGen *codegen, AstNode *node) {
    /* ()func_name or ()Type.func; emit as C function pointer */
    codegen_enter_node(codegen, node);
    if (node->data.function_reference.function->kind == NODE_LABEL) {
        /* The referenced function is named as written, so it resolves like
         * any other reference — taking a reference to a function inside its
         * own module used to emit the unmangled symbol. */
        emit(codegen, "gray_fn_");
        emit(codegen, codegen_resolve_reference(codegen, node->data.function_reference.function,
            node->data.function_reference.function->data.label.value));
    } else if (node->data.function_reference.function->kind == NODE_MEMBER_EXPRESSION) {
        /* ()StructName.funcName → gray_fn_StructName_funcName */
        AstNode *member_node = node->data.function_reference.function;
        const char *qualifier = ast_member_qualifier(member_node);
        const char *chain_module = NULL, *chain_type = NULL;
        if (member_node->resolved_declaration && member_node->resolved_declaration->kind == DECLARATION_FUNCTION) {
            /* mod.func — the whole qualified name resolved to the function,
             * so its declaration names it outright. */
            emit_formatted(codegen, "gray_fn_%s",
                module_mangle(codegen->modules, member_node->resolved_declaration));
        } else if (qualifier) {
            const char *qualified_name = codegen_resolve_declaration(codegen, qualifier);
            emit_formatted(codegen, "gray_fn_%s_%s", qualified_name, member_node->data.member.member);
        } else if (ast_member_chain(member_node, &chain_module, &chain_type)) {
            /* mod.Struct.func — the struct function, namespaced under its
             * struct the way a direct call to it is. */
            if (member_node->data.member.object->resolved_declaration) {
                char owner[MESSAGE_BUFFER_SIZE];
                emit_formatted(codegen, "gray_fn_%s_%s",
                    module_mangle_into(member_node->data.member.object->resolved_declaration, owner, sizeof(owner)),
                    member_node->data.member.member);
            } else {
                emit_formatted(codegen, "gray_fn_%s_%s_%s", chain_module, chain_type,
                    member_node->data.member.member);
            }
        } else {
            emit(codegen, "gray_fn_");
            emit_expression(codegen, node->data.function_reference.function);
        }
    } else {
        emit(codegen, "gray_fn_");
        emit_expression(codegen, node->data.function_reference.function);
    }
}

/* True when the qualifier of `a.b` names a value rather than a module. A
 * local or parameter shadows a module of the same name — `b.v` inside the
 * file whose module is `b` reads the local's field — and the typechecker
 * already resolves it that way, so a typed object is what says so here. */
static bool member_object_is_value(CodeGen *codegen, AstNode *node) {
    GrayType *object_type = type_table_get(codegen->type_table, node->data.member.object);
    return object_type && object_type->kind != TYPE_KIND_UNKNOWN;
}

static void emit_member_expression(CodeGen *codegen, AstNode *node) {
    /* Check for module constants first */
    const char *object_name = ast_member_qualifier(node);
    if (object_name) {
        const char *module_name = object_name;
        const char *member_name = node->data.member.member;

        /* @math constants */
        if (strcmp(module_name, "math") == 0) {
            if (strcmp(member_name, "PI") == 0)      { emit(codegen, "3.14159265358979323846"); return; }
            if (strcmp(member_name, "E") == 0)       { emit(codegen, "2.71828182845904523536"); return; }
            if (strcmp(member_name, "TAU") == 0)     { emit(codegen, "6.28318530717958647692"); return; }
            if (strcmp(member_name, "PHI") == 0)     { emit(codegen, "1.61803398874989484820"); return; }
            if (strcmp(member_name, "SQRT2") == 0)   { emit(codegen, "1.41421356237309504880"); return; }
            if (strcmp(member_name, "LN2") == 0)     { emit(codegen, "0.69314718055994530942"); return; }
            if (strcmp(member_name, "LN10") == 0)    { emit(codegen, "2.30258509299404568402"); return; }
            if (strcmp(member_name, "INF") == 0)     { emit(codegen, "(1.0/0.0)"); return; }
            if (strcmp(member_name, "NEG_INF") == 0) { emit(codegen, "(-1.0/0.0)"); return; }
            if (strcmp(member_name, "EPSILON") == 0) { emit(codegen, "2.2204460492503131e-16"); return; }
            if (strcmp(member_name, "MAX_I64") == 0) { emit(codegen, "9223372036854775807LL"); return; }
            if (strcmp(member_name, "MIN_I64") == 0) { emit(codegen, "(-9223372036854775807LL - 1)"); return; }
            if (strcmp(member_name, "MAX_F64") == 0) { emit(codegen, "1.7976931348623157e308"); return; }
            if (strcmp(member_name, "MIN_F64") == 0) { emit(codegen, "-1.7976931348623157e308"); return; }
        }

        /* @io OpenFlag enum via the module.VARIANT spelling */
        if (strcmp(module_name, "io") == 0) {
            if (strcmp(member_name, "O_RDONLY") == 0 || strcmp(member_name, "O_WRONLY") == 0 ||
                strcmp(member_name, "O_RDWR") == 0) {
                emit_formatted(codegen, "GrayEnum_OpenFlag_%s", member_name);
                return;
            }
        }

        /* @os Platform enum via the module.VARIANT spelling */
        if (strcmp(module_name, "os") == 0) {
            if (strcmp(member_name, "MAC_OS") == 0 || strcmp(member_name, "LINUX") == 0 ||
                strcmp(member_name, "WINDOWS") == 0 || strcmp(member_name, "OTHER") == 0) {
                emit_formatted(codegen, "GrayEnum_Platform_%s", member_name);
                return;
            }
        }

        /* @strconv constants */
        if (strcmp(module_name, "strconv") == 0) {
            if (strcmp(member_name, "BASE_2") == 0)  { emit(codegen, "2"); return; }
            if (strcmp(member_name, "BASE_8") == 0)  { emit(codegen, "8"); return; }
            if (strcmp(member_name, "BASE_10") == 0) { emit(codegen, "10"); return; }
            if (strcmp(member_name, "BASE_16") == 0) { emit(codegen, "16"); return; }
            if (strcmp(member_name, "BASE_36") == 0) { emit(codegen, "36"); return; }
        }

        /* @uuid constants */
        if (strcmp(module_name, "uuid") == 0) {
            if (strcmp(member_name, "NIL_UUID") == 0) {
                emit(codegen, "gray_uuid_nil()");
                return;
            }
        }

        /* ErrorCode.VARIANT — the program-wide synthetic enum. Its slots are
         * #defined as GrayErrorCode_<V> in the preamble, never as a
         * GrayEnum_ErrorCode_<V> variant, and it has no AST decl for
         * codegen_is_enum to recognize. */
        if (strcmp(module_name, "ErrorCode") == 0) {
            emit_formatted(codegen, "GrayErrorCode_%s", member_name);
            return;
        }

        /* Check if this is an enum access: EnumName.VALUE or prefix_EnumName.VALUE */
        if (module_name[0] >= 'A' && module_name[0] <= 'Z') {
            /* Resolve unprefixed enum names from 'import and use' */
            const char *resolved_enum = NULL;
            if (codegen_is_enum(codegen, module_name)) {
                resolved_enum = module_name;
            } else if (strcmp(module_name, "OpenFlag") == 0 || strcmp(module_name, "Platform") == 0) {
                /* Stdlib-provided enums have no AST decl in codegen's registry. */
                resolved_enum = module_name;
            } else {
                const char *resolved_name = codegen_resolve_type(codegen, module_name);
                if (resolved_name != module_name && codegen_is_enum(codegen, resolved_name)) resolved_enum = resolved_name;
            }
            if (resolved_enum) {
                if (codegen_enum_is_tagged(codegen, resolved_enum)) {
                    emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }", resolved_enum, resolved_enum, member_name);
                } else {
                    emit_formatted(codegen, "GrayEnum_%s_%s", resolved_enum, member_name);
                }
                return;
            }
        }
        /* Rewritten enum name from import: lib_Color.RED → GrayEnum_lib_Color_RED.
         * The module-prefixed name starts with the module name (lowercase),
         * so the uppercase-module guard above misses it. codegen_is_enum is
         * authoritative — if the resolved identifier is a known enum, any
         * member access on it is an enum member access, regardless of the
         * member's first-letter casing (lowercase variants like
         * `type_change` are valid). */
        if (codegen_is_enum(codegen, module_name)) {
            if (codegen_enum_is_tagged(codegen, module_name)) {
                emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }", module_name, module_name, member_name);
            } else {
                emit_formatted(codegen, "GrayEnum_%s_%s", module_name, member_name);
            }
            return;
        }

        /* C interop constant access: extern.EOF, extern.NULL, extern.EXIT_SUCCESS */
        if (strcmp(module_name, "extern") == 0 && codegen->has_c_imports) {
            emit_formatted(codegen, "%s", member_name);
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
        if (module_name[0] >= 'a' && module_name[0] <= 'z' && !member_object_is_value(codegen, node)) {
            if (node->resolved_declaration) {
                emit(codegen, module_mangle(codegen->modules, node->resolved_declaration));
                return;
            }
            if (codegen_module_imported(codegen, module_name)) {
                emit_formatted(codegen, "%s_%s", resolve_alias(codegen, module_name), member_name);
                return;
            }
        }
    }
    /* Module-qualified enum access: lib.Color.RED → GrayEnum_lib_Color_RED.
     * The first-letter casing heuristics aren't reliable — Grayscale permits
     * lowercase enum members (e.g. `type_change`), and a confident
     * `codegen_is_enum` lookup on the prefixed name is authoritative.
     * Verify the type really is a known enum before rewriting. */
    if (node->data.member.object->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *inner = node->data.member.object;
        const char *value = node->data.member.member;
        /* An alias standing for an enum reaches this the same way the enum
         * does — lib.Hue.RED is lib.Color.RED — so resolve it to the enum it
         * names instead of falling through to plain member access. */
        const char *prefixed = NULL;
        if (inner->resolved_declaration && inner->resolved_declaration->kind == DECLARATION_ENUM) {
            prefixed = module_mangle(codegen->modules, inner->resolved_declaration);
        } else if (inner->resolved_declaration && inner->resolved_declaration->kind == DECLARATION_ALIAS) {
            const char *target = resolve_type_alias_codegen(codegen,
                module_mangle(codegen->modules, inner->resolved_declaration));
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
        GrayType *object_type = type_table_get(codegen->type_table, node->data.member.object);

        /* When accessing .v0 on a single-return value (not a multi-return struct),
         * just emit the value itself (e.g., from temp x, _ = single_return_func()) */
        /* When accessing .v0 on a single-return value (not a multi-return temp),
         * just emit the value itself. Skip this for _gray_tmp* variables which
         * are multi-return unpacking temps. */
        const char *mem_name = node->data.member.member;
        if (mem_name[0] == 'v' && mem_name[1] >= '0' && mem_name[1] <= '9' && mem_name[2] == '\0') {
            bool is_multi_temporary = object_name && is_result_temporary(object_name);
            if (!is_multi_temporary && object_type &&
                (object_type->kind == TYPE_KIND_SIGNED_INTEGER || object_type->kind == TYPE_KIND_UNSIGNED_INTEGER || object_type->kind == TYPE_KIND_FLOATING_POINT ||
                 object_type->kind == TYPE_KIND_BOOL || object_type->kind == TYPE_KIND_STRING ||
                 object_type->kind == TYPE_KIND_CHAR)) {
                if (mem_name[1] == '0') {
                    emit_expression(codegen, node->data.member.object);
                } else {
                    emit(codegen, "0 /* discarded */");
                }
                return;
            }
        }

        /* Ref vars are already dereferenced by label emission; use . not -> */
        bool is_object_reference = object_name && is_reference_variable(codegen, object_name);
        bool is_object_raw = object_name && is_raw_variable(codegen, object_name);
        /* Multi-return temp vars are always value types — never pointer-deref them */
        bool is_object_multi_temporary = object_name && is_result_temporary(object_name);
        if (!is_object_multi_temporary && !is_object_reference && object_type && object_type->kind == TYPE_KIND_POINTER) {
            if (is_object_raw) {
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
                const char *pointee_type_name = object_type->element_type;
                if (pointee_type_name && pointee_type_name[0] != '^' && !strchr(pointee_type_name, '?')) {
                    const char *cursor = gray_type_to_c_codegen(codegen, pointee_type_name);
                    snprintf(c_pointee, sizeof(c_pointee), "%s", cursor);
                    emit_formatted(codegen, "((%s *)gray_ptr_check((void *)(", c_pointee);
                    emit_expression(codegen, node->data.member.object);
                    emit_formatted(codegen, "), \"%s\", %d))->%s",
                        codegen->file, node->token.line,
                        sanitize_name(node->data.member.member));
                } else {
                    emit(codegen, "({ __auto_type _dp = ");
                    emit_expression(codegen, node->data.member.object);
                    emit_formatted(codegen, "; if (!_dp) { %s; } _dp->%s; })",
                        panic_call(codegen, node, "P0080", ""), sanitize_name(node->data.member.member));
                }
            }
        } else if (!is_object_reference && object_type && object_type->kind == TYPE_KIND_ERROR) {
            /* Error has fields code (ErrorCode, an integer enum) and msg; '.message' is an
             * accepted alias for '.msg'. The value is a GrayError* that is
             * NULL on the success path, so guard the read: without it a
             * `mut m = err.msg` after a successful call is a raw segfault
             * with no diagnostic. */
            const char *member_name_text = node->data.member.member;
            const char *field = strcmp(member_name_text, "message") == 0 ? "msg" : sanitize_name(member_name_text);
            emit(codegen, "({ __auto_type _err_v = ");
            emit_expression(codegen, node->data.member.object);
            char p0115_arguments[MESSAGE_BUFFER_SIZE];
            snprintf(p0115_arguments, sizeof(p0115_arguments), ", \"%s\"", member_name_text);
            emit_formatted(codegen, "; if (!_err_v) { %s; } _err_v->%s; })",
                panic_call(codegen, node, "P0115", p0115_arguments), field);
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
    if (!left || left->kind != NODE_INDEX_EXPRESSION) return false;
    GrayType *inner = type_table_get(codegen->type_table, left->data.index_expression.left);
    return inner && inner->kind == TYPE_KIND_MAP;
}

/* True when emit_index_expression(node) lowers `node` (an index expression) to a GCC
 * statement-expression, whose value is not addressable — so an outer subscript
 * on it must bind it to a temp instead of emitting GRAY_ARRAY_GET_AT's &(...).
 * Mirrors the rvalue-base cases handled in emit_index_expression, including a chain
 * (fn()[i][j]) where the middle subscript is itself such an rvalue. */
static bool index_expression_lowers_to_rvalue(CodeGen *codegen, AstNode *node) {
    if (!node || node->kind != NODE_INDEX_EXPRESSION) return false;
    AstNode *left = node->data.index_expression.left;
    if (!left) return false;
    if (left->kind == NODE_CALL_EXPRESSION) return true;
    if (index_left_is_map_lookup(codegen, left)) return true;
    if (left->kind == NODE_POSTFIX_EXPRESSION && left->data.postfix.operator == TOKEN_CARET)
        return true;
    if (left->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *object = left->data.member.object;
        GrayType *object_type = type_table_get(codegen->type_table, object);
        if (object_type && object_type->kind == TYPE_KIND_POINTER) return true;
        if (object->kind == NODE_POSTFIX_EXPRESSION && object->data.postfix.operator == TOKEN_CARET)
            return true;
    }
    if (left->kind == NODE_INDEX_EXPRESSION)
        return index_expression_lowers_to_rvalue(codegen, left);
    return false;
}

static void emit_index_expression(CodeGen *codegen, AstNode *node) {
    /* Check if left side is an array (GrayArray) or string */
    GrayType *left_type = type_table_get(codegen->type_table, node->data.index_expression.left);
    if (left_type && left_type->kind == TYPE_KIND_ARRAY) {
        /* Determine element C type */
        const char *c_element_type = "int64_t";
        const char *element_type_name = codegen_effective_type_string(codegen, left_type->element_type);
        if (element_type_name && (strcmp(element_type_name, "func") == 0 || strncmp(element_type_name, "func(", 5) == 0)) {
            c_element_type = "void *";
        } else if (element_type_name) {
            GrayType *element_type = type_from_name(element_type_name);
            if (element_type->kind == TYPE_KIND_FLOATING_POINT) c_element_type = (strcmp(element_type_name, "f32") == 0) ? "float" : "double";
            else if (element_type->kind == TYPE_KIND_BOOL) c_element_type = "bool";
            else if (element_type->kind == TYPE_KIND_STRING) c_element_type = "GrayString";
            else if (element_type->kind == TYPE_KIND_CHAR) c_element_type = "int32_t";
            /* Sized integer element types (u8/u16/i32/…) are stored packed by
             * cast(arr, [T]); reading them with the int64_t fall-through
             * strides past the buffer. Match the storage width. */
            else if ((element_type->kind == TYPE_KIND_SIGNED_INTEGER || element_type->kind == TYPE_KIND_UNSIGNED_INTEGER) && !is_wide_integer_type_name(element_type_name))
                c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
            else if (element_type->kind == TYPE_KIND_ARRAY) c_element_type = "GrayArray";
            else if (element_type->kind == TYPE_KIND_MAP) c_element_type = "GrayMap";
            else if (element_type->kind == TYPE_KIND_STRUCT) c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
            else if (element_type->kind == TYPE_KIND_ENUM) {
                c_element_type = codegen_enum_is_string(codegen, element_type_name)
                    ? "GrayString" : gray_type_to_c_codegen(codegen, element_type_name);
            }
            else if (element_type->kind == TYPE_KIND_POINTER) {
                static char index_pointer_buffer[MESSAGE_BUFFER_SIZE];
                const char *pointee = element_type->element_type ? element_type->element_type : "void";
                snprintf(index_pointer_buffer, sizeof(index_pointer_buffer), "%s *", gray_type_to_c_codegen(codegen, pointee));
                c_element_type = index_pointer_buffer;
            }
        }
        /* Check for wide integer element types */
        if (left_type->element_type && is_wide_integer_type_name(left_type->element_type)) {
            c_element_type = wide_integer_prefix(left_type->element_type);
        }
        /* If left is an rvalue, GRAY_ARRAY_GET's &(arr) would be invalid.
         * Handles three rvalue sources:
         *  1. function call result (NODE_CALL_EXPRESSION)
         *  2. array field through struct pointer: b.items[i] where b: ^Bag
         *     (member emit wraps in GCC statement expr → rvalue)
         *  3. explicit deref then member: b^.items[i] (same rvalue issue)
         * For cases 2/3, inline the nil check and use _dp->field directly
         * so GRAY_ARRAY_GET receives an assignable target. */
        AstNode *array_pointer_object = NULL;
        const char *array_pointer_field = NULL;
        if (node->data.index_expression.left->kind == NODE_MEMBER_EXPRESSION) {
            AstNode *_mem = node->data.index_expression.left;
            AstNode *indexed_object = _mem->data.member.object;
            GrayType *indexed_object_type = type_table_get(codegen->type_table, indexed_object);
            if (indexed_object_type && indexed_object_type->kind == TYPE_KIND_POINTER) {
                array_pointer_object = indexed_object;
                array_pointer_field = _mem->data.member.member;
            } else if (indexed_object->kind == NODE_POSTFIX_EXPRESSION &&
                       indexed_object->data.postfix.operator == TOKEN_CARET) {
                /* b^.field: strip the deref, use the underlying pointer */
                array_pointer_object = indexed_object->data.postfix.left;
                array_pointer_field = _mem->data.member.member;
            }
        }
        if (array_pointer_object) {
            bool is_array_raw = (array_pointer_object->kind == NODE_LABEL && is_raw_variable(codegen, array_pointer_object->data.label.value));
            int temporary_id = codegen_next_id(codegen);
            /* The element pointer is dereferenced outside the statement
             * expression so the result is an lvalue: `b.items[i].n = v`,
             * `b.items[i].n += v` and `b.items[i].n++` all assign through it
             * or take its address. */
            emit_formatted(codegen, "(*(%s *)({ __auto_type _adp%d = ", c_element_type, temporary_id);
            emit_expression(codegen, array_pointer_object);
            if (is_array_raw) {
                emit_formatted(codegen, "; gray_array_get_ptr(&_adp%d->%s, ",
                      temporary_id, sanitize_name(array_pointer_field));
            } else {
                emit_formatted(codegen, "; if (!_adp%d) { %s; } "
                          "gray_array_get_ptr(&_adp%d->%s, ",
                      temporary_id, panic_call(codegen, node, "P0080", ""), temporary_id, sanitize_name(array_pointer_field));
            }
            emit_expression(codegen, node->data.index_expression.index);
            emit_formatted(codegen, ", \"%s\", %d); }))", codegen->file, node->token.line);
        } else if (node->data.index_expression.left->kind == NODE_POSTFIX_EXPRESSION &&
                   node->data.index_expression.left->data.postfix.operator == TOKEN_CARET) {
            /* p^[i]: direct dereference of container pointer */
            AstNode *array_pointer = node->data.index_expression.left->data.postfix.left;
            bool array_pointer_is_raw = (array_pointer->kind == NODE_LABEL && is_raw_variable(codegen, array_pointer->data.label.value));
            int temporary_id = codegen_next_id(codegen);
            emit_formatted(codegen, "(*(%s *)({ __auto_type _adp%d = ", c_element_type, temporary_id);
            emit_expression(codegen, array_pointer);
            if (array_pointer_is_raw) {
                emit_formatted(codegen, "; gray_array_get_ptr(_adp%d, ", temporary_id);
            } else {
                emit_formatted(codegen, "; if (!_adp%d) { %s; } "
                          "gray_array_get_ptr(_adp%d, ",
                      temporary_id, panic_call(codegen, node, "P0080", ""), temporary_id);
            }
            emit_expression(codegen, node->data.index_expression.index);
            emit_formatted(codegen, ", \"%s\", %d); }))", codegen->file, node->token.line);
        } else if (node->data.index_expression.left->kind == NODE_CALL_EXPRESSION ||
                   index_left_is_map_lookup(codegen, node->data.index_expression.left) ||
                   (node->data.index_expression.left->kind == NODE_INDEX_EXPRESSION &&
                    index_expression_lowers_to_rvalue(codegen, node->data.index_expression.left))) {
            int element_array_id = codegen_next_id(codegen);
            emit_formatted(codegen, "({ GrayArray _ea%d = ", element_array_id);
            emit_expression(codegen, node->data.index_expression.left);
            emit_formatted(codegen, "; GRAY_ARRAY_GET_AT(_ea%d, %s, ", element_array_id, c_element_type);
            emit_expression(codegen, node->data.index_expression.index);
            emit_formatted(codegen, ", \"%s\", %d); })", codegen->file, node->token.line);
        } else {
            emit_formatted(codegen, "GRAY_ARRAY_GET_AT(");
            emit_expression(codegen, node->data.index_expression.left);
            emit_formatted(codegen, ", %s, ", c_element_type);
            emit_expression(codegen, node->data.index_expression.index);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        }
    } else if (left_type && left_type->kind == TYPE_KIND_MAP) {
        /* Map key access; use temp to handle rvalue keys like literals */
        const char *c_key_type = "GrayString";
        const char *c_value_type = "int64_t";
        if (left_type->key_type) c_key_type = gray_map_element_c_type(codegen, left_type->key_type);
        if (left_type->value_type) c_value_type = gray_map_element_c_type(codegen, left_type->value_type);
        /* When the left side is an rvalue (e.g. chained map access
         * like m["a"]["x"], or pointer field access like p.map_field),
         * store it in a temp to make it addressable. */
        bool map_is_rvalue = (node->data.index_expression.left->kind == NODE_INDEX_EXPRESSION ||
            node->data.index_expression.left->kind == NODE_CALL_EXPRESSION);
        if (!map_is_rvalue && node->data.index_expression.left->kind == NODE_MEMBER_EXPRESSION) {
            AstNode *object = node->data.index_expression.left->data.member.object;
            GrayType *object_type = type_table_get(codegen->type_table, object);
            if (object_type && object_type->kind == TYPE_KIND_POINTER) map_is_rvalue = true;
            if (object->kind == NODE_POSTFIX_EXPRESSION && object->data.postfix.operator == TOKEN_CARET)
                map_is_rvalue = true;
        }
        /* p^["key"]: direct dereference of map pointer yields rvalue */
        if (!map_is_rvalue && node->data.index_expression.left->kind == NODE_POSTFIX_EXPRESSION &&
            node->data.index_expression.left->data.postfix.operator == TOKEN_CARET) {
            map_is_rvalue = true;
        }
        /* Dereference the entry pointer *outside* the statement expression so
         * the result is an lvalue: `m[k].field = v`, `m[k].field += v`,
         * `bump(m[k].field)`, and `m[k1][k2] = v` all take the address of, or
         * assign through, this expression, exactly as the array form does. */
        if (map_is_rvalue) {
            emit_formatted(codegen, "(*(%s *)({ GrayMap _mt = ", c_value_type);
            emit_expression(codegen, node->data.index_expression.left);
            emit_formatted(codegen, "; %s _mk = ", c_key_type);
            emit_map_slot_value(codegen, left_type->key_type, node->data.index_expression.index);
            emit_formatted(codegen, "; void *_mv = gray_map_get(&_mt, &_mk); if (!_mv) { %s; } ", panic_call(codegen, node, "P0081", ""));
            emit(codegen, "_mv; }))");
        } else {
            emit_formatted(codegen, "(*(%s *)({ %s _mk = ", c_value_type, c_key_type);
            emit_map_slot_value(codegen, left_type->key_type, node->data.index_expression.index);
            emit_formatted(codegen, "; void *_mv = gray_map_get(&");
            emit_expression(codegen, node->data.index_expression.left);
            emit_formatted(codegen, ", &_mk); if (!_mv) { %s; } ", panic_call(codegen, node, "P0081", ""));
            emit(codegen, "_mv; }))");
        }
    } else if (left_type && left_type->kind == TYPE_KIND_STRING) {
        /* String indexing with bounds check: s.data[i] */
        emit_formatted(codegen, "({ GrayString _es = ");
        emit_expression(codegen, node->data.index_expression.left);
        emit_formatted(codegen, "; int64_t _ei = (int64_t)(");
        emit_expression(codegen, node->data.index_expression.index);
        emit_formatted(codegen, "); if (_ei < 0 || _ei >= _es.len) { %s; } ", panic_call(codegen, node, "P0082", ", (long long)_ei, _es.len"));
        emit(codegen, "(int32_t)(unsigned char)_es.data[_ei]; })");
    } else {
        /* Fallback */
        emit_expression(codegen, node->data.index_expression.left);
        emit(codegen, "[");
        emit_expression(codegen, node->data.index_expression.index);
        emit(codegen, "]");
    }
}

static void emit_cast_expression(CodeGen *codegen, AstNode *node) {
    /* cast(value, type); dispatch to conversion functions for non-trivial casts */
    const char *target = node->data.cast.target_type;
    AstNode *value = node->data.cast.value;
    GrayType *value_type = type_table_get(codegen->type_table, value);
    TypeKind value_kind = value_type ? value_type->kind : TYPE_KIND_UNKNOWN;

    /* Infer kind from AST if type table has no info */
    if (value_kind == TYPE_KIND_UNKNOWN) {
        if (value->kind == NODE_STRING_VALUE || value->kind == NODE_INTERPOLATED_STRING)
            value_kind = TYPE_KIND_STRING;
        else if (value->kind == NODE_BOOL_VALUE) value_kind = TYPE_KIND_BOOL;
        else if (value->kind == NODE_FLOATING_POINT_LITERAL) value_kind = TYPE_KIND_FLOATING_POINT;
        else if (value->kind == NODE_INTEGER_LITERAL) value_kind = TYPE_KIND_SIGNED_INTEGER;
    }

    /* Array cast: allocate new array and convert each element with range checks */
    if (node->data.cast.is_array) {
        const char *source_element_type = (value_type && value_type->element_type) ? value_type->element_type : "i64";
        const char *destination_element_type = node->data.cast.element_type;
        const char *source_c_type = gray_type_to_c_codegen(codegen, source_element_type);
        const char *destination_c_type = gray_type_to_c_codegen(codegen, destination_element_type);
        int unique_id = codegen_next_id(codegen);

        bool is_source_floating_point = (strcmp(source_element_type, "f32") == 0 || strcmp(source_element_type, "f64") == 0);
        bool is_destination_floating_point = (strcmp(destination_element_type, "f32") == 0 || strcmp(destination_element_type, "f64") == 0);
        bool is_destination_unsigned_integer = (strcmp(destination_element_type, "u64") == 0);

        emit_formatted(codegen, "({ GrayArray _ca%d = ", unique_id);
        emit_expression(codegen, value);
        emit_formatted(codegen, "; GrayArray _cr%d = GRAY_ARRAY_NEW_OF(gray_default_arena, %s, _ca%d.len); ", unique_id, destination_c_type, unique_id);
        emit_formatted(codegen, "for (int32_t _ci%d = 0; _ci%d < _ca%d.len; _ci%d++) { ", unique_id, unique_id, unique_id, unique_id);
        emit_formatted(codegen, "%s _cv%d = ((%s*)_ca%d.data)[_ci%d]; ", source_c_type, unique_id, source_c_type, unique_id, unique_id);

        const char *array_minimum = NULL, *array_maximum = NULL;
        bool is_array_unsigned = false;
        integer_type_name_bounds(destination_element_type, &array_minimum, &array_maximum, &is_array_unsigned);
        if (strcmp(destination_element_type, "char") == 0 && strcmp(source_element_type, "char") != 0) {
            array_maximum = CHAR_CODEPOINT_MAX;
            is_array_unsigned = true;
        }
        char source_value[32];
        snprintf(source_value, sizeof(source_value), "_cv%d", unique_id);
        if (array_maximum) {
            /* Narrowing to a sized integer: range-check the source as it is. */
            emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)", destination_c_type, unique_id, unique_id, destination_c_type);
            emit_range_checked_narrowing(codegen, type_from_name(source_element_type), NULL, source_value,
                                         array_minimum, array_maximum, is_array_unsigned, destination_element_type, node->token.line);
            emit(codegen, "; ");
        } else if (is_source_floating_point && !is_destination_floating_point) {
            /* floating-point → integer: use overflow-safe floating-point conversion */
            if (is_destination_unsigned_integer) {
                emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)gray_f64_to_u64((double)_cv%d, \"%s\", %d); ",
                    destination_c_type, unique_id, unique_id, destination_c_type, unique_id, codegen->file, node->token.line);
            } else {
                emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)gray_f64_to_i64((double)_cv%d, \"%s\", %d); ",
                    destination_c_type, unique_id, unique_id, destination_c_type, unique_id, codegen->file, node->token.line);
            }
        } else {
            emit_formatted(codegen, "((%s*)_cr%d.data)[_ci%d] = (%s)_cv%d; ", destination_c_type, unique_id, unique_id, destination_c_type, unique_id);
        }

        emit_formatted(codegen, "} _cr%d.len = _ca%d.len; _cr%d; })", unique_id, unique_id, unique_id);
        return;
    }

    /* string-backed enum <-> string: both are GrayString at runtime, so the
     * cast is a pure reinterpretation with no conversion. */
    if (codegen_enum_is_string(codegen, target) ||
        (strcmp(target, "string") == 0 && value_type && value_type->name &&
         codegen_enum_is_string(codegen, value_type->name))) {
        emit_expression(codegen, value);
        return;
    }

    if (strcmp(target, "string") == 0) {
        /* any → string: use to_string functions */
        if (value_kind == TYPE_KIND_CHAR) {
            /* char → string: single-character string, not ASCII value */
            emit(codegen, "gray_string_new(gray_default_arena, (char[]){(char)(");
            emit_expression(codegen, value);
            emit(codegen, "), '\\0'}, 1)");
        } else {
            emit_to_string(codegen, value);
        }
    } else if ((strcmp(target, "i64") == 0) && value_kind == TYPE_KIND_STRING) {
        /* string → i64 */
        emit(codegen, "gray_builtin_string_to_i64(");
        emit_expression(codegen, value);
        emit(codegen, ")");
    } else if ((strcmp(target, "f64") == 0) && value_kind == TYPE_KIND_STRING) {
        /* string → f64 */
        emit(codegen, "gray_builtin_string_to_f64(");
        emit_expression(codegen, value);
        emit(codegen, ")");
    } else if ((strcmp(target, "i64") == 0) && value_kind == TYPE_KIND_FLOATING_POINT) {
        /* f64 → i64: overflow-safe */
        emit(codegen, "gray_f64_to_i64((double)(");
        emit_expression(codegen, value);
        emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
    } else if ((strcmp(target, "u64") == 0) && value_kind == TYPE_KIND_FLOATING_POINT) {
        /* f64 → u64: negative values and overflow are undefined behavior in C; panic instead */
        emit(codegen, "gray_f64_to_u64((double)(");
        emit_expression(codegen, value);
        emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
    } else if (value_kind == TYPE_KIND_STRING) {
        /* string → numeric (targets other than i64/f64 handled above):
         * parse to int64/double first, then apply narrowing check */
        if (strcmp(target, "u64") == 0) {
            emit(codegen, "(uint64_t)gray_builtin_string_to_i64(");
            emit_expression(codegen, value);
            emit(codegen, ")");
        } else if (strcmp(target, "f32") == 0) {
            emit(codegen, "(float)gray_builtin_string_to_f64(");
            emit_expression(codegen, value);
            emit(codegen, ")");
        } else {
            /* Sized integer targets — parse then range-check */
            const char *source_minimum = NULL, *source_maximum = NULL;
            bool is_unsigned = false;
            integer_type_name_bounds(target, &source_minimum, &source_maximum, &is_unsigned);

            if (source_maximum) {
                emit_formatted(codegen, "(%s)%s(gray_builtin_string_to_i64(", gray_type_to_c_codegen(codegen, target),
                    is_unsigned ? "gray_ucast_check" : "gray_cast_check");
                emit_expression(codegen, value);
                emit(codegen, "), ");
                emit_sized_bounds_arguments(codegen, source_minimum, source_maximum, is_unsigned, target, node->token.line);
                emit(codegen, ")");
            } else {
                /* Fallback: parse to i64 and cast */
                emit_formatted(codegen, "((%s)gray_builtin_string_to_i64(", gray_type_to_c_codegen(codegen, target));
                emit_expression(codegen, value);
                emit(codegen, "))");
            }
        }
    } else {
        /* Wide integer (i128/u128/i256/u256) cast handling */
        bool is_target_wide_integer = is_wide_integer_type_name(target);
        const char *source_wide_integer = (value_type && value_type->name && is_wide_integer_type_name(value_type->name))
            ? value_type->name : resolve_wide_integer_type(codegen, value);
        if (is_target_wide_integer || source_wide_integer) {
            if (is_target_wide_integer && !source_wide_integer) {
                /* scalar → wide: use from_i64 / from_u64 */
                emit_scalar_to_wide_integer(codegen, target, value, value_type);
            } else if (!is_target_wide_integer && source_wide_integer) {
                /* wide → scalar: range-checked extraction to int64/uint64,
                 * with additional narrow-range check for sub-64-bit targets */
                bool is_destination_unsigned = (strcmp(target, "u64") == 0 ||
                    strcmp(target, "u8") == 0 ||
                    strcmp(target, "u16") == 0 || strcmp(target, "u32") == 0);
                const char *bounds_prefix = wide_integer_prefix(source_wide_integer);

                /* Determine if an additional narrow range check is needed */
                const char *narrow_minimum = NULL, *narrow_maximum = NULL;
                bool narrow_unsigned = false;
                integer_type_name_bounds(target, &narrow_minimum, &narrow_maximum, &narrow_unsigned);

                if (narrow_maximum) {
                    if (narrow_unsigned)
                        emit_formatted(codegen, "(%s)gray_ucast_check_u64(%s_to_u64(", gray_type_to_c_codegen(codegen, target), bounds_prefix);
                    else
                        emit_formatted(codegen, "(%s)gray_cast_check(%s_to_i64(", gray_type_to_c_codegen(codegen, target), bounds_prefix);
                    emit_expression(codegen, value);
                    emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
                    emit_sized_bounds_arguments(codegen, narrow_minimum, narrow_maximum, narrow_unsigned, target, node->token.line);
                    emit(codegen, ")");
                } else if (is_destination_unsigned) {
                    emit_formatted(codegen, "(%s)%s_to_u64(", gray_type_to_c_codegen(codegen, target), bounds_prefix);
                    emit_expression(codegen, value);
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                } else {
                    emit_formatted(codegen, "(%s)%s_to_i64(", gray_type_to_c_codegen(codegen, target), bounds_prefix);
                    emit_expression(codegen, value);
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                }
            } else {
                /* wide → wide: use cross-type constructors */
                if (strcmp(source_wide_integer, "i128") == 0 && strcmp(target, "u128") == 0)
                    { emit(codegen, "gray_u128_from_i128("); emit_expression(codegen, value); emit(codegen, ")"); }
                else if (strcmp(source_wide_integer, "u128") == 0 && strcmp(target, "i128") == 0)
                    { emit(codegen, "gray_i128_from_u128("); emit_expression(codegen, value); emit(codegen, ")"); }
                else if (strcmp(source_wide_integer, "i128") == 0 && strcmp(target, "i256") == 0)
                    { emit(codegen, "gray_i256_from_i128("); emit_expression(codegen, value); emit(codegen, ")"); }
                else if (strcmp(source_wide_integer, "u128") == 0 && strcmp(target, "u256") == 0)
                    { emit(codegen, "gray_u256_from_u128("); emit_expression(codegen, value); emit(codegen, ")"); }
                else if (strcmp(source_wide_integer, "i256") == 0 && strcmp(target, "i128") == 0)
                    { emit(codegen, "gray_i128_from_i256("); emit_expression(codegen, value); emit(codegen, ")"); }
                else if (strcmp(source_wide_integer, "u256") == 0 && strcmp(target, "u128") == 0)
                    { emit(codegen, "gray_u128_from_u256("); emit_expression(codegen, value); emit(codegen, ")"); }
                else
                    { emit_expression(codegen, value); } /* same-type no-op */
            }
            return;
        }

        /* Numeric casts: range-checked for narrowing, raw for widening */
        const char *source_minimum = NULL, *source_maximum = NULL;
        bool is_unsigned = false;
        integer_type_name_bounds(target, &source_minimum, &source_maximum, &is_unsigned);
        if (strcmp(target, "char") == 0 && value_kind != TYPE_KIND_CHAR) {
            source_maximum = CHAR_CODEPOINT_MAX;
            is_unsigned = true;
        }

        if (source_maximum) {
            emit_formatted(codegen, "(%s)", gray_type_to_c_codegen(codegen, target));
            emit_range_checked_narrowing(codegen, value_type, value, NULL, source_minimum, source_maximum, is_unsigned,
                                         target, node->token.line);
        } else if ((strcmp(target, "u64") == 0) &&
                   (value_kind == TYPE_KIND_SIGNED_INTEGER || value_kind == TYPE_KIND_UNKNOWN || value_kind == TYPE_KIND_C_FUNCTION)) {
            /* signed integer → u64: panic if value is negative. TYPE_KIND_C_FUNCTION
             * also covers an extern.call()/extern.CONST C-interop value,
             * which carries no Grayscale type of its own and so could be
             * either sign. */
            emit_formatted(codegen, "(uint64_t)gray_ucast_check((int64_t)(");
            emit_expression(codegen, value);
            emit_formatted(codegen, "), 18446744073709551615ULL, \"%s\", \"%s\", %d)", target, codegen->file, node->token.line);
        } else if ((strcmp(target, "i64") == 0) &&
                   value_kind == TYPE_KIND_UNSIGNED_INTEGER &&
                   value_type && value_type->name &&
                   (strcmp(value_type->name, "u64") == 0)) {
            /* u64 → i64: panic if value exceeds INT64_MAX */
            emit_formatted(codegen, "(int64_t)gray_u64_to_i64_check((uint64_t)(");
            emit_expression(codegen, value);
            emit_formatted(codegen, "), \"%s\", %d)", codegen->file, node->token.line);
        } else if (codegen_is_enum(codegen, target) &&
                   !codegen_enum_is_string(codegen, target) &&
                   !codegen_enum_is_tagged(codegen, target)) {
            /* integer → enum: the value has to name a declared variant. Without
             * this the cast stored whatever it was given, and the result
             * matched no variant in a `when` or an `==`. The variant names go
             * into the array as written so the C compiler supplies their
             * values, explicit ones included. */
            AstNode *target_enum_declaration = codegen->enum_declarations[codegen_enum_index(codegen, target)];
            emit_formatted(codegen, "(%s)gray_enum_cast_check((int64_t)(",
                gray_type_to_c_codegen(codegen, target));
            emit_expression(codegen, value);
            emit(codegen, "), (const int64_t[]){");
            for (int variant_index = 0; variant_index < target_enum_declaration->data.enum_declaration.value_count; variant_index++) {
                if (variant_index > 0) emit(codegen, ", ");
                emit_formatted(codegen, "GrayEnum_%s_%s", target,
                    target_enum_declaration->data.enum_declaration.values[variant_index].name);
            }
            /* The panic names the enum the way the program spells it, not the
             * module-prefixed key the C constants are built from. */
            const char *display = target_enum_declaration->data.enum_declaration.original_name
                ? target_enum_declaration->data.enum_declaration.original_name : target;
            emit_formatted(codegen, "}, %d, %s, \"%s\", \"%s\", %d)",
                target_enum_declaration->data.enum_declaration.value_count,
                target_enum_declaration->data.enum_declaration.is_flags ? "true" : "false",
                display, codegen->file, node->token.line);
        } else {
            emit_formatted(codegen, "((%s)(", gray_type_to_c_codegen(codegen, target));
            emit_expression(codegen, value);
            emit(codegen, "))");
        }
    }
}

/* True when new(Type) must run field initializers rather than a bare zeroed
 * allocation: the struct has a map/array field, a field default, or a nested
 * value-struct field that itself needs one. Value structs cannot be cyclic,
 * so the recursion terminates. */
static bool struct_needs_new_initializer(CodeGen *codegen, AstNode *struct_declaration, int depth) {
    if (!struct_declaration || depth > 8) return false;
    for (int i = 0; i < struct_declaration->data.struct_declaration.field_count; i++) {
        const char *field_type_name = struct_declaration->data.struct_declaration.fields[i].type_name;
        if (struct_declaration->data.struct_declaration.fields[i].default_value) return true;
        if (field_type_name && (strncmp(field_type_name, "map[", 4) == 0 || field_type_name[0] == '[')) return true;
        if (field_type_name && codegen_enum_is_string(codegen, codegen_resolve_type(codegen, field_type_name)))
            return true;
        if (field_type_name && field_type_name[0] != '^') {
            GrayType *field_gray_type = type_from_name(field_type_name);
            if (field_gray_type && field_gray_type->kind == TYPE_KIND_STRUCT &&
                struct_needs_new_initializer(codegen, find_struct_declaration(codegen, field_type_name), depth + 1))
                return true;
        }
    }
    return false;
}

/* Emit the container / default initializers for one struct level reached
 * through `access` (e.g. "_np->" or "_np->inner."), recursing into nested
 * value-struct fields so a map/array buried inside them is still given a
 * live header instead of the zero one new()'s allocation leaves. */
static void emit_new_struct_initializer(CodeGen *codegen, AstNode *struct_declaration,
                                 const char *access, int depth) {
    if (!struct_declaration || depth > 8) return;
    for (int i = 0; i < struct_declaration->data.struct_declaration.field_count; i++) {
        const char *field_name = struct_declaration->data.struct_declaration.fields[i].name;
        const char *field_type = struct_declaration->data.struct_declaration.fields[i].type_name;
        if (field_type && strncmp(field_type, "map[", 4) == 0) {
            GrayType *map_type = type_from_name(field_type);
            const char *c_key_type = "GrayString";
            const char *c_value_type_default = "int64_t";
            if (map_type && map_type->key_type) c_key_type = gray_map_element_c_type(codegen, map_type->key_type);
            if (map_type && map_type->value_type) c_value_type_default = gray_map_element_c_type(codegen, map_type->value_type);
            emit_formatted(codegen, "%s%s = GRAY_MAP_NEW_OF(gray_heap_arena, %s, %s, 8); ",
                access, sanitize_name(field_name), c_key_type, c_value_type_default);
        } else if (field_type && field_type[0] == '[') {
            GrayType *argument_type = type_from_name(field_type);
            const char *c_element_type = "int64_t";
            if (argument_type && argument_type->element_type)
                c_element_type = gray_map_element_c_type(codegen, argument_type->element_type);
            emit_formatted(codegen, "%s%s = GRAY_ARRAY_NEW_OF(gray_heap_arena, %s, 4); ",
                access, sanitize_name(field_name), c_element_type);
        } else if (field_type &&
                   codegen_enum_is_string(codegen, codegen_resolve_type(codegen, field_type))) {
            /* String-backed enum field: zero is an empty string, not a
             * variant. Seed with the first variant, matching new(EnumType). */
            const char *string_enum = codegen_resolve_type(codegen, field_type);
            int enum_index = codegen_enum_index(codegen, string_enum);
            const char *field_value_text = codegen->enum_declarations[enum_index]->data.enum_declaration.values[0].name;
            emit_formatted(codegen, "%s%s = GrayEnum_%s_%s; ",
                access, sanitize_name(field_name), string_enum, field_value_text);
        } else if (field_type && field_type[0] != '^') {
            GrayType *field_gray_type = type_from_name(field_type);
            if (field_gray_type && field_gray_type->kind == TYPE_KIND_STRUCT) {
                AstNode *nested = find_struct_declaration(codegen, field_type);
                if (nested && struct_needs_new_initializer(codegen, nested, depth + 1)) {
                    char inner_access[MESSAGE_BUFFER_SIZE];
                    snprintf(inner_access, sizeof(inner_access), "%s%s.",
                        access, sanitize_name(field_name));
                    emit_new_struct_initializer(codegen, nested, inner_access, depth + 1);
                }
            }
        }
        if (struct_declaration->data.struct_declaration.fields[i].default_value) {
            emit_formatted(codegen, "%s%s = ", access, sanitize_name(field_name));
            emit_struct_field_default_value(codegen, &struct_declaration->data.struct_declaration.fields[i]);
            emit(codegen, "; ");
        }
    }
}

static void emit_new_expression(CodeGen *codegen, AstNode *node) {
    /* new(Type) → zeroed allocation on default arena, returns pointer.
     * Map and array fields need explicit initialization because a
     * zero-filled GrayMap/GrayArray has key_size/value_size/elem_size = 0
     * and operations on them silently fail. */
    const char *type_name_text = node->data.new_expression.type_name;
    /* Resolve ? → concrete binding for type params */
    if (strcmp(type_name_text, "?") == 0 && codegen->wildcard_binding) {
        type_name_text = codegen->wildcard_binding;
    } else {
        /* new() names its type as written; the struct declaration it has to
         * find (for field defaults) is keyed by the module's spelling. */
        codegen_enter_node(codegen, node);
        type_name_text = codegen_resolve_type(codegen, type_name_text);
    }
    const char *c_type = gray_type_to_c_codegen(codegen, type_name_text);
    AstNode *struct_declaration = find_struct_declaration(codegen, type_name_text);
    if (struct_declaration && struct_needs_new_initializer(codegen, struct_declaration, 0)) {
        /* Field defaults are expressions from the struct's own file, so they
         * resolve against the struct's module, not the caller's. */
        const char *caller_module = codegen->current_module;
        codegen_enter_node(codegen, struct_declaration);
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_new_struct_initializer(codegen, struct_declaration, "_np->", 0);
        emit(codegen, "_np; })");
        codegen->current_module = caller_module;
    } else if (type_name_text[0] == '[') {
        /* Array type — allocate + initialize metadata */
        GrayType *array_type = type_from_name(type_name_text);
        const char *c_element_type = "int64_t";
        if (array_type && array_type->element_type)
            c_element_type = gray_map_element_c_type(codegen, array_type->element_type);
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_formatted(codegen, "*_np = GRAY_ARRAY_NEW_OF(gray_heap_arena, %s, 4); _np; })", c_element_type);
    } else if (strncmp(type_name_text, "map[", 4) == 0) {
        /* Map type — allocate + initialize metadata */
        GrayType *map_type = type_from_name(type_name_text);
        const char *c_key_type = "GrayString";
        const char *c_value_type_default = "int64_t";
        if (map_type && map_type->key_type) c_key_type = gray_map_element_c_type(codegen, map_type->key_type);
        if (map_type && map_type->value_type) c_value_type_default = gray_map_element_c_type(codegen, map_type->value_type);
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_formatted(codegen, "*_np = GRAY_MAP_NEW_OF(gray_heap_arena, %s, %s, 8); _np; })",
            c_key_type, c_value_type_default);
    } else if (codegen_enum_is_string(codegen, type_name_text)) {
        /* String enum — assign first variant so the value is valid */
        int enum_index = codegen_enum_index(codegen, type_name_text);
        AstNode *declaration = codegen->enum_declarations[enum_index];
        const char *first_variant = declaration->data.enum_declaration.values[0].name;
        emit_formatted(codegen, "({ %s *_np = (%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)); ",
            c_type, c_type, c_type);
        emit_formatted(codegen, "*_np = GrayEnum_%s_%s; _np; })", type_name_text, first_variant);
    } else {
        emit_formatted(codegen, "((%s *)gray_arena_alloc(gray_heap_arena, sizeof(%s)))", c_type, c_type);
    }
}

/* A value stored into a [T,N] struct field whose length the type checker
 * couldn't prove: evaluate it once and panic unless it has exactly N
 * elements. */
static void emit_runtime_fixed_length_check(CodeGen *codegen, AstNode *node) {
    int expected_length = node->runtime_fixed_length;
    node->runtime_fixed_length = 0;
    emit(codegen, "({ GrayArray _fixed_arr = ");
    emit_expression(codegen, node);
    node->runtime_fixed_length = expected_length;
    char panic_arguments[48];
    snprintf(panic_arguments, sizeof(panic_arguments), ", %d, (int)_fixed_arr.len", expected_length);
    emit_formatted(codegen, "; if (_fixed_arr.len != %d) { %s; } _fixed_arr; })",
        expected_length, panic_call(codegen, node, "P0131", panic_arguments));
}

/* A value with fewer than N elements stored into a [T,N] struct field: copy
 * it into a fresh N-element array whose remaining slots are zeroed. */
static void emit_zero_filled_fixed_array(CodeGen *codegen, AstNode *node) {
    int fixed_length = node->zero_fill_length;
    node->zero_fill_length = 0;
    emit(codegen, "({ GrayArray _short_arr = ");
    emit_expression(codegen, node);
    node->zero_fill_length = fixed_length;
    emit_formatted(codegen, "; GrayArray _padded_arr = gray_array_new(gray_default_arena, "
        "_short_arr.elem_size, %d, _short_arr.elem_kind); "
        "size_t _short_bytes = (size_t)_short_arr.len * (size_t)_short_arr.elem_size; "
        "if (_short_bytes) memcpy(_padded_arr.data, _short_arr.data, _short_bytes); "
        "memset((char *)_padded_arr.data + _short_bytes, 0, "
        "(size_t)%d * (size_t)_short_arr.elem_size - _short_bytes); "
        "_padded_arr.len = %d; _padded_arr; })",
        fixed_length, fixed_length, fixed_length);
}

/* The 256-bit two's complement of the decimal text `text` (an optional '-'
 * then digits) as four little-endian words. */
static void decimal_text_to_words(const char *text, uint64_t words[4]) {
    uint32_t limbs[8] = {0};
    bool negative = *text == '-';
    for (const char *cursor = text + (negative ? 1 : 0); *cursor; cursor++) {
        uint64_t carry = (uint64_t)(*cursor - '0');
        for (int i = 0; i < 8; i++) {
            uint64_t current_value = (uint64_t)limbs[i] * 10 + carry;
            limbs[i] = (uint32_t)current_value;
            carry = current_value >> 32;
        }
    }
    if (negative) {
        uint64_t carry = 1;
        for (int i = 0; i < 8; i++) {
            uint64_t current_value = (uint64_t)(uint32_t)~limbs[i] + carry;
            limbs[i] = (uint32_t)current_value;
            carry = current_value >> 32;
        }
    }
    for (int i = 0; i < 4; i++)
        words[i] = (uint64_t)limbs[2 * i] | ((uint64_t)limbs[2 * i + 1] << 32);
}

/* Emit a number literal expression the type checker folded, as a constant
 * of the type it took. A C constant everywhere, file scope included. */
static void emit_folded_literal(CodeGen *codegen, AstNode *node) {
    GrayType *type = type_table_get(codegen->type_table, node);
    const char *text = node->folded_literal;
    if (type && type->kind == TYPE_KIND_FLOATING_POINT) {
        char message[64];
        bool is_f32 = type->name && strcmp(type->name, "f32") == 0;
        double value = strtod(text, NULL);
        if (is_f32) snprintf(message, sizeof(message), "%.9g", (double)(float)value);
        else snprintf(message, sizeof(message), "%.17g", value);
        emit_formatted(codegen, "%s%s%s", message,
            (strchr(message, '.') || strchr(message, 'e')) ? "" : ".0", is_f32 ? "f" : "");
        return;
    }
    if (type && type->name && is_wide_integer_type_name(type->name)) {
        uint64_t word[4];
        decimal_text_to_words(text, word);
        if (strcmp(type->name, "i128") == 0)
            emit_formatted(codegen, "((gray_i128){0x%llxULL, (int64_t)0x%llxULL})",
                (unsigned long long)word[0], (unsigned long long)word[1]);
        else if (strcmp(type->name, "u128") == 0)
            emit_formatted(codegen, "((gray_u128){0x%llxULL, 0x%llxULL})",
                (unsigned long long)word[0], (unsigned long long)word[1]);
        else
            emit_formatted(codegen, "((gray_%s){{0x%llxULL, 0x%llxULL, 0x%llxULL, 0x%llxULL}})",
                type->name, (unsigned long long)word[0], (unsigned long long)word[1],
                (unsigned long long)word[2], (unsigned long long)word[3]);
        return;
    }
    if (text[0] == '-') {
        if (strcmp(text, "-9223372036854775808") == 0) emit(codegen, "(-9223372036854775807LL - 1)");
        else emit_formatted(codegen, "(%sLL)", text);
        return;
    }
    /* A value above INT64_MAX is only ever a u64. */
    bool above_i64 = strlen(text) > 19 || (strlen(text) == 19 && strcmp(text, "9223372036854775807") > 0);
    emit_formatted(codegen, above_i64 ? "%sULL" : "%s", text);
}

/* --- emit_expression --- */

static void emit_expression(CodeGen *codegen, AstNode *node) {
    if (!node) return;
    GrayType *node_type = type_table_get(codegen->type_table, node);
    if (node_type && node_type->kind == TYPE_KIND_LITERAL)
        codegen_internal_error("an untyped number literal reached codegen", codegen->file, node->token.line);
    if (node->folded_literal) {
        emit_folded_literal(codegen, node);
        return;
    }
    if (node->widen_to) {
        /* A value the type checker widens into a wide integer type. */
        const char *target = node->widen_to;
        node->widen_to = NULL;
        const char *source_wide = resolve_wide_integer_type(codegen, node);
        if (source_wide) {
            emit_formatted(codegen, "%s_from_%s(", wide_integer_prefix(target), source_wide);
            emit_expression(codegen, node);
            emit(codegen, ")");
        } else {
            emit_scalar_to_wide_integer(codegen, target, node, NULL);
        }
        node->widen_to = target;
        return;
    }
    if (node->runtime_fixed_length > 0) {
        emit_runtime_fixed_length_check(codegen, node);
        return;
    }
    if (node->zero_fill_length > 0) {
        emit_zero_filled_fixed_array(codegen, node);
        return;
    }

    switch (node->kind) {
    case NODE_LABEL:
        emit_label(codegen, node);
        break;

    case NODE_INTEGER_LITERAL:
        if (node->data.integer_literal.is_above_i64_maximum) {
            /* Literal exceeds INT64_MAX; for u64 contexts emit as a
             * decimal ULL so it works for any base (0o, 0b, 0x literals). */
            const char *wide_integer_context = resolve_wide_integer_type(codegen, node);
            if (!wide_integer_context) {
                emit_formatted(codegen, "%lluULL",
                    (unsigned long long)(uint64_t)node->data.integer_literal.value);
            } else {
                emit_formatted(codegen, "%s_from_decimal(\"%s\")", wide_integer_prefix(wide_integer_context),
                    node->data.integer_literal.literal);
            }
        } else {
            emit_formatted(codegen, "%lld", (long long)node->data.integer_literal.value);
        }
        break;

    case NODE_FLOATING_POINT_LITERAL: {
        /* Emit a floating-point value with enough precision, ensuring a decimal point so C
         * treats it as double (e.g. 1.0 must emit "1.0", not "1") */
        char folded_buffer[VARIABLE_NAME_BUFFER_SIZE];
        snprintf(folded_buffer, sizeof(folded_buffer), "%.17g", node->data.floating_point_literal.value);
        if (!strchr(folded_buffer, '.') && !strchr(folded_buffer, 'e')) {
            size_t folded_length = strlen(folded_buffer);
            folded_buffer[folded_length] = '.';
            folded_buffer[folded_length+1] = '0';
            folded_buffer[folded_length+2] = '\0';
        }
        emit(codegen, folded_buffer);
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

    case NODE_PREFIX_EXPRESSION:
        emit_prefix_expression(codegen, node);
        break;

    case NODE_INFIX_EXPRESSION:
        emit_infix_expression(codegen, node);
        break;

    case NODE_POSTFIX_EXPRESSION:
        emit_postfix_expression(codegen, node);
        break;

    case NODE_FUNCTION_REFERENCE:
        emit_function_reference(codegen, node);
        break;

    case NODE_CALL_EXPRESSION:
        emit_call_expression(codegen, node);
        break;

    case NODE_MEMBER_EXPRESSION:
        emit_member_expression(codegen, node);
        break;

    case NODE_INDEX_EXPRESSION:
        emit_index_expression(codegen, node);
        break;

    case NODE_CAST_EXPRESSION:
        emit_cast_expression(codegen, node);
        break;

    case NODE_NEW_EXPRESSION:
        emit_new_expression(codegen, node);
        break;

    case NODE_IMPLICIT_ENUM: {
        const char *enum_name = node->data.implicit_enum.resolved_enum;
        const char *variant = node->data.implicit_enum.variant;
        if (enum_name) {
            if (strcmp(enum_name, "ErrorCode") == 0) {
                emit_formatted(codegen, "GrayErrorCode_%s", variant);
            } else if (codegen_enum_is_tagged(codegen, enum_name)) {
                emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s }", enum_name, enum_name, variant);
            } else {
                emit_formatted(codegen, "GrayEnum_%s_%s", enum_name, variant);
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
static bool is_stdlib_call(AstNode *node, const char **module, const char **out_function_name) {
    if (node->data.call.function->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *object = node->data.call.function->data.member.object;
        if (object->kind == NODE_LABEL) {
            /* A qualifier bound to a user module resolves to a declaration
             * written in Grayscale. Its C name comes from mangling that
             * declaration, not from the stdlib emitter for a module that
             * happens to share the name. */
            DeclarationEntry *resolved = node->data.call.function->resolved_declaration;
            if (resolved && !resolved->is_external) return false;
            *module = object->data.label.value;
            *out_function_name = node->data.call.function->data.member.member;
            return true;
        }
    } else if (node->data.call.function->kind == NODE_LABEL) {
        /* Direct call like println() via using */
        *module = NULL;
        *out_function_name = node->data.call.function->data.label.value;
        return true;
    }
    return false;
}

/* --- Stdlib call emission helpers --- */

/* If arg is a ref() call, return the inner argument so print functions
 * use the underlying value's type and emit the value, not the address. */
static AstNode *unwrap_reference_argument(AstNode *argument) {
    if (argument->kind == NODE_CALL_EXPRESSION &&
        argument->data.call.function->kind == NODE_LABEL &&
        strcmp(argument->data.call.function->data.label.value, "ref") == 0 &&
        argument->data.call.argument_count == 1) {
        return argument->data.call.arguments[0];
    }
    return argument;
}

/* The print builtin suffix for `arg`; for "_float", `*float_bits` is set to
 * the bit size the value prints at. */
static const char *resolve_print_suffix(CodeGen *codegen, AstNode *argument, int *floating_point_bits) {
    *floating_point_bits = 64;
    /* addr() calls always print in hex format */
    if (argument->kind == NODE_CALL_EXPRESSION && argument->data.call.function->kind == NODE_LABEL &&
        strcmp(argument->data.call.function->data.label.value, "addr") == 0) return "_addr";
    /* During wildcard monomorphisation the type table retains the type from the
     * first instantiation. If the argument is a label that names a '?'-typed parameter
     * of the current function, use the active binding instead so each instantiation
     * gets the correct print variant. */
    if (codegen->wildcard_binding && argument->kind == NODE_LABEL && codegen->current_function) {
        const char *label = argument->data.label.value;
        for (int i = 0; i < codegen->current_function->data.function_declaration.parameter_count; i++) {
            Parameter *parameter = &codegen->current_function->data.function_declaration.parameters[i];
            if (parameter->type_name && strchr(parameter->type_name, '?') &&
                strcmp(parameter->name, label) == 0) {
                GrayType *wildcard_type = type_from_name(codegen->wildcard_binding);
                if (wildcard_type) {
                    switch (wildcard_type->kind) {
                    case TYPE_KIND_STRING:  return "_str";
                    case TYPE_KIND_FLOATING_POINT:
                        *floating_point_bits = floating_point_bit_size(wildcard_type->name);
                        return "_float";
                    case TYPE_KIND_BOOL:    return "_bool";
                    case TYPE_KIND_CHAR:    return "_char";
                    case TYPE_KIND_UNSIGNED_INTEGER:    return "_u64";
                    case TYPE_KIND_POINTER: return "_addr";
                    default:         return "_i64";
                    }
                }
            }
        }
    }
    GrayType *type = type_table_get(codegen->type_table, argument);
    if (type && type->kind != TYPE_KIND_UNKNOWN) {
        switch (type->kind) {
        case TYPE_KIND_STRING:  return "_str";
        case TYPE_KIND_FLOATING_POINT:
            *floating_point_bits = floating_point_bit_size(type->name);
            return "_float";
        case TYPE_KIND_BOOL:    return "_bool";
        case TYPE_KIND_CHAR:    return "_char";
        case TYPE_KIND_UNSIGNED_INTEGER:    return "_u64";
        case TYPE_KIND_POINTER: return "_addr";
        case TYPE_KIND_ENUM:
            return (type->name && codegen_enum_is_string(codegen, type->name)) ? "_str" : "_i64";
        default:         return "_i64";
        }
    }
    if (argument->kind == NODE_STRING_VALUE || argument->kind == NODE_INTERPOLATED_STRING) return "_str";
    if (argument->kind == NODE_FLOATING_POINT_LITERAL) return "_float";
    if (argument->kind == NODE_BOOL_VALUE) return "_bool";
    if (argument->kind == NODE_CHAR_VALUE) return "_char";
    /* For call expressions, check the return type of the called function */
    if (argument->kind == NODE_CALL_EXPRESSION && argument->data.call.function->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *function_node = argument->data.call.function;
        const char *object_name = ast_member_qualifier(function_node);
        if (object_name) {
            const char *member_name = function_node->data.member.member;
            /* Check if it's a known stdlib module function that returns string */
            if ((strcmp(object_name, "strings") == 0) ||
                (strcmp(object_name, "encoding") == 0) ||
                (strcmp(object_name, "crypto") == 0)) return "_str";
            if (strcmp(object_name, "uuid") == 0 &&
                (strcmp(member_name, "generate_compact") == 0 ||
                 strcmp(member_name, "to_string") == 0)) return "_str";
            /* Check if it's a struct-namespaced function or instance struct function call */
            {
                const char *struct_name = NULL;
                /* Direct struct type call: Foo.greet() */
                if (object_name[0] >= 'A' && object_name[0] <= 'Z') {
                    struct_name = object_name;
                } else {
                    /* Instance call: f.greet() — look up variable's struct type */
                    GrayType *object_type = type_table_get(codegen->type_table, function_node->data.member.object);
                    if (object_type && (object_type->kind == TYPE_KIND_STRUCT || object_type->kind == TYPE_KIND_POINTER) && object_type->name) {
                        struct_name = object_type->name;
                    }
                }
                if (struct_name) {
                    AstNode *struct_declaration = find_struct_declaration(codegen, struct_name);
                    if (struct_declaration) {
                        for (int field_index = 0; field_index < struct_declaration->data.struct_declaration.function_count; field_index++) {
                            AstNode *struct_function = struct_declaration->data.struct_declaration.functions[field_index].function_declaration;
                            if (!struct_function || struct_function->kind != NODE_FUNCTION_DECLARATION) continue;
                            /* Match: struct funcs are prefixed as StructName_funcName in all_functions,
                             * but the original name is stored before prefixing in the struct decl.
                             * After codegen_init prefixes them, compare with prefixed form. */
                            const char *struct_function_name = struct_function->data.function_declaration.name;
                            /* Check both prefixed (StructName_func) and bare (func) forms */
                            bool match = (strcmp(struct_function_name, member_name) == 0);
                            if (!match) {
                                size_t struct_name_length = strlen(struct_name);
                                if (strlen(struct_function_name) > struct_name_length + 1 &&
                                    strncmp(struct_function_name, struct_name, struct_name_length) == 0 &&
                                    struct_function_name[struct_name_length] == '_' &&
                                    strcmp(struct_function_name + struct_name_length + 1, member_name) == 0) {
                                    match = true;
                                }
                            }
                            if (match && struct_function->data.function_declaration.return_type_count > 0) {
                                const char *return_type_spelling = struct_function->data.function_declaration.return_types[0];
                                if (strcmp(return_type_spelling, "string") == 0) return "_str";
                                if (strcmp(return_type_spelling, "f32") == 0 || strcmp(return_type_spelling, "f64") == 0) {
                                    *floating_point_bits = floating_point_bit_size(return_type_spelling);
                                    return "_float";
                                }
                                if (strcmp(return_type_spelling, "bool") == 0) return "_bool";
                                if (strcmp(return_type_spelling, "char") == 0) return "_char";
                                if (strcmp(return_type_spelling, "u8") == 0 ||
                                    strcmp(return_type_spelling, "u16") == 0 || strcmp(return_type_spelling, "u32") == 0 ||
                                    strcmp(return_type_spelling, "u64") == 0) return "_u64";
                                return "_i64";
                            }
                        }
                    }
                }
            }
        }
    }
    if (argument->kind == NODE_CALL_EXPRESSION && argument->data.call.function->kind == NODE_LABEL) {
        const char *function_name = argument->data.call.function->data.label.value;
        if (strcmp(function_name, "input") == 0 || strcmp(function_name, "type_of") == 0) return "_str";
        if (strcmp(function_name, "addr") == 0) return "_addr";
    }
    return "_i64";
}

static void emit_to_string(CodeGen *codegen, AstNode *argument) {
    /* Bigint to_string */
    const char *wide_integer_type = resolve_wide_integer_type(codegen, argument);
    if (wide_integer_type) {
        emit_formatted(codegen, "%s_to_string(gray_default_arena, ", wide_integer_prefix(wide_integer_type));
        emit_expression(codegen, argument);
        emit(codegen, ")");
        return;
    }
    GrayType *argument_type = type_table_get(codegen->type_table, argument);
    if (argument_type && argument_type->kind == TYPE_KIND_ERROR) {
        int unique_id = codegen_next_id(codegen);
        emit_formatted(codegen, "({ GrayError *_gray_str_err%d = (", unique_id);
        emit_expression(codegen, argument);
        emit_formatted(codegen, "); _gray_str_err%d ? _gray_str_err%d->msg : gray_c_string_dup(gray_default_arena, \"nil\"); })", unique_id, unique_id);
        return;
    }
    if (argument_type && argument_type->kind == TYPE_KIND_ENUM &&
        codegen_enum_is_error_code(codegen, argument_type->name)) {
        emit(codegen, "gray_string_lit(gray_error_code_name((int64_t)(");
        emit_expression(codegen, argument);
        emit(codegen, ")))");
        return;
    }
    if (argument_type && argument_type->kind == TYPE_KIND_CHAR) {
        emit(codegen, "gray_builtin_char_to_utf8(gray_default_arena, ");
        emit_expression(codegen, argument);
        emit(codegen, ")");
    } else {
        if (argument_type && argument_type->kind == TYPE_KIND_FLOATING_POINT)
            emit(codegen, "gray_builtin_to_string_float(gray_default_arena, ");
        else if (argument_type && argument_type->kind == TYPE_KIND_BOOL)
            emit(codegen, "gray_builtin_to_string_bool(gray_default_arena, ");
        else if (argument_type && argument_type->kind == TYPE_KIND_UNSIGNED_INTEGER)
            emit(codegen, "gray_builtin_to_string_u64(gray_default_arena, ");
        else
            emit(codegen, "gray_builtin_to_string_i64(gray_default_arena, ");
        emit_expression(codegen, argument);
        if (argument_type && argument_type->kind == TYPE_KIND_FLOATING_POINT)
            emit_formatted(codegen, ", %d", floating_point_bit_size(argument_type->name));
        emit(codegen, ")");
    }
}

/* Emit a fmt format string literal with %d/%i/%u upgraded to %lld/%llu for
 * Grayscale i64/u64 arguments (which are int64_t/uint64_t) to avoid -Wformat.
 * If append_newline is true, a \n is appended before the closing quote. */
static void emit_format_string_normalized_extended(CodeGen *codegen, const char *format_text, AstNode *call_node, bool append_newline) {
    const char *cursor = format_text;
    int directive_index = 1; /* which call arg corresponds to the next directive */
    append_char_to_buffer(&codegen->output, '"');
    while (*cursor) {
        if (*cursor != '%') { append_char_to_buffer(&codegen->output, *cursor++); continue; }
        /* Emit '%' and start scanning the directive */
        append_char_to_buffer(&codegen->output, '%');
        cursor++;
        if (!*cursor) break;
        if (*cursor == '%') { append_char_to_buffer(&codegen->output, '%'); cursor++; continue; }
        /* Buffer flags / width / precision so they can be filtered once the
         * conversion is known — a numeric directive whose arg is a wide integer is
         * downgraded to %s, and the 0/#/+/space flags and precision are
         * undefined behaviour (or misread as string precision) on an 's'
         * conversion, so they are dropped rather than left to leak a
         * -Wformat warning and produce wrong output. Width and '-' survive. */
        char flags[8]; int flags_length = 0;
        char width[16]; int width_length = 0;
        char precision[16]; int precision_length = 0;
        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' || *cursor == '0' || *cursor == '#') {
            if (flags_length < (int)sizeof(flags) - 1) flags[flags_length++] = *cursor;
            cursor++;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            if (width_length < (int)sizeof(width) - 1) width[width_length++] = *cursor;
            cursor++;
        }
        if (*cursor == '.') {
            if (precision_length < (int)sizeof(precision) - 1) precision[precision_length++] = *cursor;
            cursor++;
            while (*cursor >= '0' && *cursor <= '9') {
                if (precision_length < (int)sizeof(precision) - 1) precision[precision_length++] = *cursor;
                cursor++;
            }
        }
        flags[flags_length] = '\0'; width[width_length] = '\0'; precision[precision_length] = '\0';
        /* Check for existing length modifier (buffered; a downgrade to %s drops it) */
        char length_modifier[4]; int length_modifier_length = 0;
        if (*cursor == 'h' || *cursor == 'l' || *cursor == 'L') {
            length_modifier[length_modifier_length++] = *cursor++;
            if ((length_modifier[0] == 'h' && *cursor == 'h') || (length_modifier[0] == 'l' && *cursor == 'l'))
                length_modifier[length_modifier_length++] = *cursor++;
        }
        length_modifier[length_modifier_length] = '\0';
        bool has_length = length_modifier_length > 0;
        char specifier = *cursor ? *cursor++ : 0;
        if (!specifier) break;
        GrayType *directive_type = (directive_index < call_node->data.call.argument_count && codegen->type_table)
            ? type_table_get(codegen->type_table, call_node->data.call.arguments[directive_index]) : NULL;
        bool is_argument_wide_integer = directive_type && directive_type->name &&
            is_wide_integer_type_name(directive_type->name);
        char emitted_specifier = specifier;
        bool was_downgraded_to_string = false;
        if (specifier == 'b') {
            /* %b isn't a real C conversion; emit_format_arguments() already
             * stringifies bool args to "true"/"false", so %s reads them back
             * correctly. Passing %b through verbatim hits vsnprintf as a literal
             * 'b' and never consumes the argument, desyncing every directive
             * after it. */
            emitted_specifier = 's';
            was_downgraded_to_string = true;
        } else if (is_argument_wide_integer && (specifier == 'd' || specifier == 'i' || specifier == 'u' ||
                   specifier == 'x' || specifier == 'X' || specifier == 'o')) {
            /* i128/u128/i256/u256 are struct-backed; emit_format_arguments()
             * converts them to a decimal/hex/octal string, read back with %s. */
            emitted_specifier = 's';
            was_downgraded_to_string = true;
        }
        /* Emit flags/width/precision, filtered when the directive became %s. */
        for (int field_index = 0; field_index < flags_length; field_index++) {
            if (was_downgraded_to_string && flags[field_index] != '-') continue;
            append_char_to_buffer(&codegen->output, flags[field_index]);
        }
        for (int width_index = 0; width_index < width_length; width_index++)
            append_char_to_buffer(&codegen->output, width[width_index]);
        if (!was_downgraded_to_string) {
            for (int parameter_index = 0; parameter_index < precision_length; parameter_index++)
                append_char_to_buffer(&codegen->output, precision[parameter_index]);
            for (int length_index = 0; length_index < length_modifier_length; length_index++)
                append_char_to_buffer(&codegen->output, length_modifier[length_index]);
        }
        if (!was_downgraded_to_string && !has_length && directive_type) {
            /* Grayscale i64/u64 are 64-bit; widen the directive so the vararg
             * read matches the (unsigned) long long emit_format_arguments casts
             * the argument to. */
            if ((specifier == 'd' || specifier == 'i') && directive_type->kind == TYPE_KIND_SIGNED_INTEGER) {
                append_char_to_buffer(&codegen->output, 'l');
                append_char_to_buffer(&codegen->output, 'l');
            } else if (specifier == 'u' && directive_type->kind == TYPE_KIND_UNSIGNED_INTEGER) {
                append_char_to_buffer(&codegen->output, 'l');
                append_char_to_buffer(&codegen->output, 'l');
            } else if ((specifier == 'x' || specifier == 'X' || specifier == 'o') &&
                       (directive_type->kind == TYPE_KIND_SIGNED_INTEGER || directive_type->kind == TYPE_KIND_UNSIGNED_INTEGER)) {
                append_char_to_buffer(&codegen->output, 'l');
                append_char_to_buffer(&codegen->output, 'l');
            }
        }
        append_char_to_buffer(&codegen->output, emitted_specifier);
        directive_index++;
    }
    if (append_newline) { append_char_to_buffer(&codegen->output, '\\'); append_char_to_buffer(&codegen->output, 'n'); }
    append_char_to_buffer(&codegen->output, '"');
}

static void emit_format_string_normalized(CodeGen *codegen, const char *format_text, AstNode *call_node) {
    emit_format_string_normalized_extended(codegen, format_text, call_node, false);
}

/* Record the conversion spec char of each directive in fmt_str, 1:1 with the
 * arguments that follow (matching emit_format_string_normalized's directive
 * walk). Returns the count recorded, capped at max. */
static int scan_format_specs(const char *format_text, char *specs, int maximum_count) {
    const char *cursor = format_text;
    int count = 0;
    while (*cursor) {
        if (*cursor != '%') { cursor++; continue; }
        cursor++;
        if (!*cursor) break;
        if (*cursor == '%') { cursor++; continue; }
        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' || *cursor == '0' || *cursor == '#') cursor++;
        while (*cursor >= '0' && *cursor <= '9') cursor++;
        if (*cursor == '.') { cursor++; while (*cursor >= '0' && *cursor <= '9') cursor++; }
        if (*cursor == 'h') { cursor++; if (*cursor == 'h') cursor++; }
        else if (*cursor == 'l') { cursor++; if (*cursor == 'l') cursor++; }
        else if (*cursor == 'L') cursor++;
        if (!*cursor) break;
        if (count < maximum_count) specs[count] = *cursor;
        count++;
        cursor++;
    }
    return count;
}

static void emit_format_arguments(CodeGen *codegen, AstNode *node, int start_index) {
    char specs[64];
    int specifier_count = 0;
    AstNode *format_argument = node->data.call.arguments[0];
    if (format_argument->kind == NODE_STRING_VALUE)
        specifier_count = scan_format_specs(format_argument->data.string_value.value, specs, 64);
    for (int i = start_index; i < node->data.call.argument_count; i++) {
        emit(codegen, ", ");
        AstNode *argument = node->data.call.arguments[i];
        GrayType *argument_type = type_table_get(codegen->type_table, argument);
        if (argument_type && argument_type->name && is_wide_integer_type_name(argument_type->name)) {
            /* Struct-backed big integers cannot ride in a printf vararg slot;
             * the directive was rewritten to %s, so pass a converted string. */
            const char *prefix = wide_integer_prefix(argument_type->name);
            char specifier = (i - 1 >= 0 && i - 1 < specifier_count) ? specs[i - 1] : 'd';
            if (specifier == 'x' || specifier == 'X') {
                emit_formatted(codegen, "%s_to_hex_string(gray_default_arena, ", prefix);
                emit_expression(codegen, argument);
                emit_formatted(codegen, ", %s).data", specifier == 'X' ? "true" : "false");
            } else if (specifier == 'o') {
                emit_formatted(codegen, "%s_to_octal_string(gray_default_arena, ", prefix);
                emit_expression(codegen, argument);
                emit(codegen, ").data");
            } else {
                emit_formatted(codegen, "%s_to_string(gray_default_arena, ", prefix);
                emit_expression(codegen, argument);
                emit(codegen, ").data");
            }
        } else if (argument_type && argument_type->kind == TYPE_KIND_STRING) {
            emit_expression(codegen, argument);
            emit(codegen, ".data");
        } else if (argument_type && argument_type->kind == TYPE_KIND_BOOL) {
            emit_expression(codegen, argument);
            emit(codegen, " ? \"true\" : \"false\"");
        } else if (argument_type && argument_type->kind == TYPE_KIND_SIGNED_INTEGER && !is_wide_integer_type_name(argument_type->name)) {
            /* The directive may have been upgraded to %lld (a 64-bit read),
             * but an integer literal emits as C `int`. Cast so the vararg
             * slot always carries the full width — the Win64 ABI leaves the
             * upper half of a 32-bit store as garbage. */
            emit(codegen, "(long long)(");
            emit_expression(codegen, argument);
            emit(codegen, ")");
        } else if (argument_type && argument_type->kind == TYPE_KIND_UNSIGNED_INTEGER && !is_wide_integer_type_name(argument_type->name)) {
            emit(codegen, "(unsigned long long)(");
            emit_expression(codegen, argument);
            emit(codegen, ")");
        } else {
            emit_expression(codegen, argument);
        }
    }
}

/* --- Composite type printing --- */

/* Global counter for unique print variable names */
static int gray_print_unique_id = 0;

/* Look up a struct declaration by name */
static AstNode *find_struct_declaration(CodeGen *codegen, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < codegen->struct_declaration_count; i++) {
        if (strcmp(codegen->struct_declarations[i]->data.struct_declaration.name, name) == 0) {
            return codegen->struct_declarations[i];
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
        for (int i = 0; i < codegen->struct_declaration_count; i++) {
            if (strcmp(codegen->struct_declarations[i]->data.struct_declaration.name, resolved) == 0) {
                return codegen->struct_declarations[i];
            }
        }
    }
    return NULL;
}

/* Emit C statements that print the value of c_expr (of type t) to stream.
 * stream is "stdout", "stderr", or the address of a GrayFmtOut, which
 * collects the text for string interpolation. Handles all types recursively. */
/* Cycle guard for emit_value_print struct recursion. */
static const char *emit_value_print_visiting[CYCLE_GUARD_DEPTH];
static int emit_value_print_depth = 0;

static void emit_value_print(CodeGen *codegen, const char *c_expression, GrayType *type, const char *stream, bool in_container) {
    if (!type || type->kind == TYPE_KIND_UNKNOWN) {
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"%%lld\", (long long)(%s));\n", stream, c_expression);
        return;
    }

    /* A string-backed enum is a GrayString at the C level, not an integer;
     * casting it to long long is invalid C. Print its string value, matching
     * how `println` renders such an enum directly. */
    if (type->kind == TYPE_KIND_ENUM && type->name &&
        codegen_enum_is_string(codegen, type->name)) {
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"%%.*s\", (int)(%s).len, (%s).data);\n",
               stream, c_expression, c_expression);
        return;
    }

    /* ErrorCode (and #error_code enums): print the variant name, not the raw
     * slot number. */
    if (type->kind == TYPE_KIND_ENUM && codegen_enum_is_error_code(codegen, type->name)) {
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"%%s\", gray_error_code_name((int64_t)(%s)));\n",
               stream, c_expression);
        return;
    }

    switch (type->kind) {
    case TYPE_KIND_SIGNED_INTEGER: case TYPE_KIND_ENUM:
        if (type->name && is_wide_integer_type_name(type->name)) {
            const char *prefix = wide_integer_prefix(type->name);
            emit_indent(codegen);
            emit_formatted(codegen, "{ GrayString _bs = %s_to_string(gray_default_arena, %s); gray_out_printf(%s, \"%%.*s\", (int)_bs.len, _bs.data); }\n",
                   prefix, c_expression, stream);
        } else {
            emit_indent(codegen);
            emit_formatted(codegen, "gray_out_printf(%s, \"%%lld\", (long long)(%s));\n", stream, c_expression);
        }
        break;
    case TYPE_KIND_UNSIGNED_INTEGER:
        if (type->name && is_wide_integer_type_name(type->name)) {
            const char *prefix = wide_integer_prefix(type->name);
            emit_indent(codegen);
            emit_formatted(codegen, "{ GrayString _bs = %s_to_string(gray_default_arena, %s); gray_out_printf(%s, \"%%.*s\", (int)_bs.len, _bs.data); }\n",
                   prefix, c_expression, stream);
        } else {
            emit_indent(codegen);
            emit_formatted(codegen, "gray_out_printf(%s, \"%%llu\", (unsigned long long)(%s));\n", stream, c_expression);
        }
        break;
    case TYPE_KIND_FLOATING_POINT:
        emit_indent(codegen);
        emit_formatted(codegen, "{ GrayString _fs = gray_builtin_format_float(gray_default_arena, %s, %d); "
            "gray_out_printf(%s, \"%%.*s\", (int)_fs.len, _fs.data); }\n",
            c_expression, floating_point_bit_size(type->name), stream);
        break;
    case TYPE_KIND_STRING:
        emit_indent(codegen);
        if (in_container) {
            emit_formatted(codegen, "gray_out_printf(%s, \"\\\"%%.*s\\\"\", (int)(%s).len, (%s).data);\n",
                   stream, c_expression, c_expression);
        } else {
            emit_formatted(codegen, "gray_out_printf(%s, \"%%.*s\", (int)(%s).len, (%s).data);\n",
                   stream, c_expression, c_expression);
        }
        break;
    case TYPE_KIND_BOOL:
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"%%s\", (%s) ? \"true\" : \"false\");\n",
               stream, c_expression);
        break;
    case TYPE_KIND_CHAR:
        emit_indent(codegen);
        if (in_container) {
            emit_formatted(codegen, "{ GrayString _cs = gray_builtin_char_to_utf8(gray_default_arena, %s); gray_out_printf(%s, \"'\"); gray_out_write(_cs.data, 1, (size_t)_cs.len, %s); gray_out_printf(%s, \"'\"); }\n", c_expression, stream, stream, stream);
        } else {
            emit_formatted(codegen, "{ GrayString _cs = gray_builtin_char_to_utf8(gray_default_arena, %s); gray_out_write(_cs.data, 1, (size_t)_cs.len, %s); }\n", c_expression, stream);
        }
        break;
    case TYPE_KIND_NIL:
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"nil\");\n", stream);
        break;
    case TYPE_KIND_ARRAY: {
        int unique_id = gray_print_unique_id++;
        const char *element_type_name = type->element_type ? type->element_type : "i64";
        GrayType *element_type = type_from_name(element_type_name);
        char c_element_type[TYPE_NAME_MAX];
        strncpy(c_element_type, gray_type_to_c_codegen(codegen, element_type_name), sizeof(c_element_type) - 1);
        c_element_type[sizeof(c_element_type) - 1] = '\0';

        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"{\");\n", stream);
        emit_indent(codegen);
        emit_formatted(codegen, "for (int32_t _gray_pi%d = 0; _gray_pi%d < (%s).len; _gray_pi%d++) {\n",
               unique_id, unique_id, c_expression, unique_id);
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "if (_gray_pi%d > 0) gray_out_printf(%s, \", \");\n", unique_id, stream);

        /* For composite element types, capture in temp var */
        char element_expression[MESSAGE_BUFFER_SIZE];
        if (element_type->kind == TYPE_KIND_STRUCT || element_type->kind == TYPE_KIND_ARRAY ||
            element_type->kind == TYPE_KIND_MAP || element_type->kind == TYPE_KIND_POINTER) {
            int element_unique_id = gray_print_unique_id++;
            emit_indent(codegen);
            emit_formatted(codegen, "%s _gray_pv%d = GRAY_ARRAY_GET((%s), %s, _gray_pi%d);\n",
                   c_element_type, element_unique_id, c_expression, c_element_type, unique_id);
            snprintf(element_expression, sizeof(element_expression), "_gray_pv%d", element_unique_id);
        } else {
            snprintf(element_expression, sizeof(element_expression),
                     "GRAY_ARRAY_GET((%s), %s, _gray_pi%d)", c_expression, c_element_type, unique_id);
        }

        emit_value_print(codegen, element_expression, element_type, stream, true);

        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"}\");\n", stream);
        break;
    }
    case TYPE_KIND_MAP: {
        int unique_id = gray_print_unique_id++;
        const char *key_type_name = type->key_type ? type->key_type : "string";
        const char *value_type_name = type->value_type ? type->value_type : "i64";
        GrayType *key_type = type_from_name(key_type_name);
        GrayType *value_type = type_from_name(value_type_name);
        char c_key_type[TYPE_NAME_MAX], c_value_type_buffer[TYPE_NAME_MAX];
        strncpy(c_key_type, gray_type_to_c_codegen(codegen, key_type_name), sizeof(c_key_type) - 1);
        c_key_type[sizeof(c_key_type) - 1] = '\0';
        strncpy(c_value_type_buffer, gray_type_to_c_codegen(codegen, value_type_name), sizeof(c_value_type_buffer) - 1);
        c_value_type_buffer[sizeof(c_value_type_buffer) - 1] = '\0';

        char map_iterator[SHORT_VARIABLE_BUFFER_SIZE], source_location[SHORT_VARIABLE_BUFFER_SIZE], first_flag[SHORT_VARIABLE_BUFFER_SIZE];
        snprintf(map_iterator, sizeof(map_iterator), "_gray_mi%d", unique_id);
        snprintf(source_location, sizeof(source_location), "_gray_sl%d", unique_id);
        snprintf(first_flag, sizeof(first_flag), "_gray_fst%d", unique_id);

        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"{\");\n", stream);
        emit_indent(codegen);
        emit_formatted(codegen, "if ((%s).count == 0) gray_out_printf(%s, \":\");\n", c_expression, stream);
        emit_indent(codegen);
        emit_formatted(codegen, "bool %s = true;\n", first_flag);
        emit_indent(codegen);
        emit_formatted(codegen, "for (int32_t %s = 0; %s < (%s).order_len; %s++) {\n",
               map_iterator, map_iterator, c_expression, map_iterator);
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "int32_t %s = (%s).order[%s];\n", source_location, c_expression, map_iterator);
        emit_indent(codegen);
        emit_formatted(codegen, "if (%s < 0) continue;\n", source_location);
        emit_indent(codegen);
        emit_formatted(codegen, "if (!%s) gray_out_printf(%s, \", \");\n", first_flag, stream);
        emit_indent(codegen);
        emit_formatted(codegen, "%s = false;\n", first_flag);

        /* Print key */
        char key_expression[MESSAGE_BUFFER_SIZE];
        snprintf(key_expression, sizeof(key_expression),
                 "*(%s *)gray_map_key_at(&(%s), %s)", c_key_type, c_expression, source_location);
        emit_value_print(codegen, key_expression, key_type, stream, true);

        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \": \");\n", stream);

        /* Print value */
        char value_expression[MESSAGE_BUFFER_SIZE];
        snprintf(value_expression, sizeof(value_expression),
                 "*(%s *)gray_map_value_at(&(%s), %s)", c_value_type_buffer, c_expression, source_location);
        emit_value_print(codegen, value_expression, value_type, stream, true);

        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"}\");\n", stream);
        break;
    }
    case TYPE_KIND_STRUCT: {
        const char *struct_name = type->name;
        AstNode *struct_declaration = find_struct_declaration(codegen, struct_name);
        /* Use the user-facing name (without module prefix) for display.
         * Check struct decls first, then enum decls (enums are struct-backed). */
        const char *display_name = struct_name;
        if (struct_declaration && struct_declaration->data.struct_declaration.original_name) {
            display_name = struct_declaration->data.struct_declaration.original_name;
        } else {
            int enum_index = codegen_enum_index(codegen, struct_name);
            if (enum_index >= 0 && codegen->enum_declarations[enum_index] &&
                codegen->enum_declarations[enum_index]->data.enum_declaration.original_name) {
                display_name = codegen->enum_declarations[enum_index]->data.enum_declaration.original_name;
            }
        }

        /* Cycle detection: if already printing this struct type, emit a
         * placeholder to avoid infinite recursion on circular references. */
        for (int nested_index = 0; nested_index < emit_value_print_depth; nested_index++) {
            if (emit_value_print_visiting[nested_index] && strcmp(emit_value_print_visiting[nested_index], struct_name) == 0) {
                emit_indent(codegen);
                emit_formatted(codegen, "gray_out_printf(%s, \"%s{...}\");\n", stream, display_name);
                break;
            }
        }
        bool _already = false;
        for (int nested_index = 0; nested_index < emit_value_print_depth; nested_index++) {
            if (emit_value_print_visiting[nested_index] && strcmp(emit_value_print_visiting[nested_index], struct_name) == 0) {
                _already = true; break;
            }
        }
        if (_already) break;

        if (emit_value_print_depth < CYCLE_GUARD_DEPTH) emit_value_print_visiting[emit_value_print_depth++] = struct_name;

        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"%s{\");\n", stream, display_name);

        if (struct_declaration) {
            for (int i = 0; i < struct_declaration->data.struct_declaration.field_count; i++) {
                StructField *field = &struct_declaration->data.struct_declaration.fields[i];
                if (i > 0) {
                    emit_indent(codegen);
                    emit_formatted(codegen, "gray_out_printf(%s, \", \");\n", stream);
                }
                emit_indent(codegen);
                emit_formatted(codegen, "gray_out_printf(%s, \"%s: \");\n", stream, field->name);

                char field_expression[MESSAGE_BUFFER_SIZE];
                snprintf(field_expression, sizeof(field_expression), "(%s).%s", c_expression, field->name);
                GrayType *field_graytype = type_from_name(field->type_name);
                emit_value_print(codegen, field_expression, field_graytype, stream, true);
            }
        } else if (struct_name && strcmp(struct_name, "SourceLocation") == 0) {
            /* Compiler-registered struct with no AST declaration: its fields
             * are known but find_struct_declaration() returns NULL, so walk
             * them explicitly instead of emitting an empty SourceLocation{}. */
            const char *source_location_names[3] = { "file", "line", "column" };
            const char *source_location_types[3] = { "string", "i64", "i64" };
            for (int i = 0; i < 3; i++) {
                if (i > 0) {
                    emit_indent(codegen);
                    emit_formatted(codegen, "gray_out_printf(%s, \", \");\n", stream);
                }
                emit_indent(codegen);
                emit_formatted(codegen, "gray_out_printf(%s, \"%s: \");\n", stream, source_location_names[i]);
                char field_expression[MESSAGE_BUFFER_SIZE];
                snprintf(field_expression, sizeof(field_expression), "(%s).%s", c_expression, source_location_names[i]);
                emit_value_print(codegen, field_expression, type_from_name(source_location_types[i]), stream, true);
            }
        }

        emit_value_print_depth--;
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"}\");\n", stream);
        break;
    }
    case TYPE_KIND_POINTER: {
        /* Print the address as hex (0x...). Pointers are addresses; printing
         * the pointee instead would be surprising and lose the only thing
         * a pointer carries. Use 'p^' if you actually want the pointee. */
        emit_indent(codegen);
        emit_formatted(codegen, "if ((%s) == NULL) {\n", c_expression);
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"nil\");\n", stream);
        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "} else {\n");
        codegen->indent++;
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"0x%%\" PRIxPTR, (uintptr_t)(%s));\n", stream, c_expression);
        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        break;
    }
    default:
        emit_indent(codegen);
        emit_formatted(codegen, "gray_out_printf(%s, \"%%lld\", (long long)(%s));\n", stream, c_expression);
        break;
    }
}

/* Can gray_builtin_array_to_string / gray_builtin_map_to_string, which read one
 * scalar per element, render this container? They cannot when an element or
 * value is itself an array, map, struct or pointer, nor when a map key is not
 * text: those go through emit_value_print, the formatter println uses. */
static bool interp_container_needs_value_print(CodeGen *codegen, const GrayType *type) {
    if (!type) return false;
    const char *nested[2] = { NULL, NULL };
    if (type->kind == TYPE_KIND_ARRAY) {
        nested[0] = type->element_type;
    } else if (type->kind == TYPE_KIND_MAP) {
        nested[0] = type->value_type;
        GrayType *key_type = type->key_type ? type_from_name(type->key_type) : NULL;
        bool string_key = key_type && (key_type->kind == TYPE_KIND_STRING ||
            (key_type->kind == TYPE_KIND_ENUM && codegen_enum_is_string(codegen, type->key_type)));
        if (key_type && !string_key) return true;
    } else {
        return false;
    }
    if (!nested[0]) return false;
    GrayType *inner = type_from_name(nested[0]);
    if (inner->kind == TYPE_KIND_STRUCT) return !codegen_is_enum(codegen, nested[0]);
    return inner->kind == TYPE_KIND_ARRAY || inner->kind == TYPE_KIND_MAP || inner->kind == TYPE_KIND_POINTER;
}

/* Interpolate an array or map with the code println prints it with, collected
 * into a GrayString instead of written to a stream. */
static void emit_interpolated_container(CodeGen *codegen, AstNode *part, GrayType *type) {
    int unique_id = gray_print_unique_id++;
    emit_formatted(codegen, "({ GrayFmtOut _gray_fo%d = {0}; %s _gray_pv%d = ", unique_id,
                   type->kind == TYPE_KIND_ARRAY ? "GrayArray" : "GrayMap", unique_id);
    emit_expression(codegen, part);
    emit(codegen, ";\n");
    char variable_name[SHORT_VARIABLE_BUFFER_SIZE], sink[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(variable_name, sizeof(variable_name), "_gray_pv%d", unique_id);
    snprintf(sink, sizeof(sink), "&_gray_fo%d", unique_id);
    emit_value_print(codegen, variable_name, type, sink, false);
    emit_formatted(codegen, "gray_fmt_out_finish(gray_default_arena, &_gray_fo%d); })", unique_id);
}

/* Try to emit a composite type print. Returns true if handled. */
static bool emit_composite_print(CodeGen *codegen, AstNode *node,
                                  const char *stream, bool newline) {
    if (node->data.call.argument_count < 1) return false;

    AstNode *argument = unwrap_reference_argument(node->data.call.arguments[0]);
    GrayType *type = type_table_get(codegen->type_table, argument);
    if (!type) return false;
    if (type->kind != TYPE_KIND_STRUCT && type->kind != TYPE_KIND_ARRAY &&
        type->kind != TYPE_KIND_MAP && type->kind != TYPE_KIND_POINTER) return false;
    /* Enum types are stored as TYPE_KIND_STRUCT but should print as integers */
    if (type->kind == TYPE_KIND_STRUCT && type->name && codegen_is_enum(codegen, type->name)) return false;

    /* Emit a block to scope temp variables */
    emit(codegen, "{\n");
    codegen->indent++;

    /* Capture expression in temp variable to evaluate only once */
    int unique_id = gray_print_unique_id++;
    char c_type[TYPE_NAME_MAX];
    if (type->kind == TYPE_KIND_ARRAY) snprintf(c_type, sizeof(c_type), "GrayArray");
    else if (type->kind == TYPE_KIND_MAP) snprintf(c_type, sizeof(c_type), "GrayMap");
    else if (type->kind == TYPE_KIND_POINTER) {
        const char *pointee_type_name = type->element_type ? type->element_type : "i64";
        snprintf(c_type, sizeof(c_type), "%s *", gray_type_to_c_codegen(codegen, pointee_type_name));
    }
    else { strncpy(c_type, gray_type_to_c_codegen(codegen, type_name(type)), sizeof(c_type) - 1); c_type[sizeof(c_type) - 1] = '\0'; }

    emit_indent(codegen);
    emit_formatted(codegen, "%s _gray_pv%d = ", c_type, unique_id);
    emit_expression(codegen, argument);
    emit(codegen, ";\n");

    char variable_name[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(variable_name, sizeof(variable_name), "_gray_pv%d", unique_id);

    emit_value_print(codegen, variable_name, type, stream, false);

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
    AstNode *argument = unwrap_reference_argument(node->data.call.arguments[0]);
    GrayType *argument_type = type_table_get(codegen->type_table, argument);
    if (argument_type && argument_type->kind == TYPE_KIND_ERROR) {
        emit_formatted(codegen, "gray_builtin_%s_str(", variant);
        emit_expression(codegen, argument);
        emit(codegen, " ? ");
        emit_expression(codegen, argument);
        emit(codegen, "->msg : gray_string_lit(\"nil\"))");
    } else if (argument_type && argument_type->kind == TYPE_KIND_ENUM &&
               codegen_enum_is_error_code(codegen, argument_type->name)) {
        emit_formatted(codegen, "gray_builtin_%s_str(gray_string_lit(gray_error_code_name((int64_t)(", variant);
        emit_expression(codegen, argument);
        emit(codegen, "))))");
    } else if (argument_type && argument_type->kind == TYPE_KIND_STRUCT && argument_type->name &&
               strcmp(argument_type->name, "UUID") == 0) {
        emit_formatted(codegen, "gray_builtin_%s_str(", variant);
        emit_expression(codegen, argument);
        emit(codegen, ".value)");
    } else {
        const char *wide_integer_type = resolve_wide_integer_type(codegen, argument);
        if (wide_integer_type) {
            emit_formatted(codegen, "gray_builtin_%s_str(%s_to_string(gray_default_arena, ", variant, wide_integer_prefix(wide_integer_type));
            emit_expression(codegen, argument);
            emit(codegen, "))");
        } else {
            int floating_point_bits;
            const char *suffix = resolve_print_suffix(codegen, argument, &floating_point_bits);
            emit_formatted(codegen, "gray_builtin_%s%s(", variant, suffix);
            emit_expression(codegen, argument);
            if (strcmp(suffix, "_float") == 0) emit_formatted(codegen, ", %d", floating_point_bits);
            emit(codegen, ")");
        }
    }
}

/* A builtin or stdlib call whose C form is `c_name(arg0, arg1, ...)` with every
 * argument passed through in order. */
typedef struct {
    const char *function_name;
    int argument_count;
    const char *c_name;
} PassthroughCall;

static bool emit_passthrough_call(CodeGen *codegen, AstNode *node, const char *function_name,
                                  const PassthroughCall *table) {
    for (const PassthroughCall *passthrough = table; passthrough->function_name; passthrough++) {
        if (strcmp(function_name, passthrough->function_name) != 0 || node->data.call.argument_count != passthrough->argument_count) continue;
        emit_formatted(codegen, "%s(", passthrough->c_name);
        for (int i = 0; i < passthrough->argument_count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[i]);
        }
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- Builtin call handler (no-module functions) --- */

static const PassthroughCall builtin_passthrough[] = {
    {"exit", 1, "gray_builtin_exit"},
    {"system", 1, "gray_builtin_system"},
    {"panic", 1, "gray_builtin_panic_msg"},
    {"sleep_s", 1, "gray_builtin_sleep_s"},
    {"sleep_ms", 1, "gray_builtin_sleep_ms"},
    {"sleep_ns", 1, "gray_builtin_sleep_ns"},
    {"char_count", 1, "gray_builtin_char_count"},
    {NULL, 0, NULL},
};

static bool emit_builtin_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "println") == 0) {
        if (node->data.call.argument_count == 0) {
            emit(codegen, "putchar('\\n')");
        } else {
            if (emit_composite_print(codegen, node, "stdout", true)) return true;
            emit_print_variant(codegen, node, "println");
        }
        return true;
    }

    if (strcmp(function_name, "len") == 0 && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        GrayType *type = type_table_get(codegen->type_table, argument);
        if (type && type->kind == TYPE_KIND_MAP) {
            emit(codegen, "(int64_t)(");
            emit_expression(codegen, argument);
            emit(codegen, ").count");
        } else {
            emit(codegen, "(int64_t)(");
            emit_expression(codegen, argument);
            emit(codegen, ").len");
        }
        return true;
    }

    if (strcmp(function_name, "type_of") == 0 && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        /* Bigint type_of: return the exact type name */
        const char *wide_integer_type = resolve_wide_integer_type(codegen, argument);
        if (wide_integer_type) {
            emit_formatted(codegen, "gray_string_lit(\"%s\")", wide_integer_type);
            return true;
        }
        GrayType *type = type_table_get(codegen->type_table, argument);
        /* Range expression: type_of(range(0, 5)) → "Range<i64>" */
        if (argument->kind == NODE_RANGE_EXPRESSION ||
            (argument->kind == NODE_CALL_EXPRESSION && argument->data.call.function->kind == NODE_LABEL &&
             strcmp(argument->data.call.function->data.label.value, "range") == 0)) {
            emit_formatted(codegen, "gray_string_lit(\"Range<i64>\")");
            return true;
        }
        /* Enum member access: type_of(Color.RED) → "Color" */
        if (ast_member_qualifier(argument) &&
            type && (type->kind == TYPE_KIND_SIGNED_INTEGER || type->kind == TYPE_KIND_UNSIGNED_INTEGER || type->kind == TYPE_KIND_STRING)) {
            const char *object_name = ast_member_qualifier(argument);
            if (object_name[0] >= 'A' && object_name[0] <= 'Z' &&
                strcmp(object_name, "std") != 0 && strcmp(object_name, "math") != 0 &&
                strcmp(object_name, "os") != 0) {
                emit_formatted(codegen, "gray_string_lit(\"%s\")", object_name);
                return true;
            }
        }
        char written[MESSAGE_BUFFER_SIZE], second_written[MESSAGE_BUFFER_SIZE];
        if (type && type->kind == TYPE_KIND_ARRAY && type->element_type) {
            emit_formatted(codegen, "gray_string_lit(\"[%s]\")",
                codegen_written_type_name(codegen, type->element_type, written, sizeof(written)));
        } else if (type && type->kind == TYPE_KIND_MAP) {
            const char *key_type_name = type->key_type ? type->key_type : "unknown";
            const char *value_type_text = type->value_type ? type->value_type : "unknown";
            emit_formatted(codegen, "gray_string_lit(\"map[%s:%s]\")",
                codegen_written_type_name(codegen, key_type_name, written, sizeof(written)),
                codegen_written_type_name(codegen, value_type_text, second_written, sizeof(second_written)));
        } else if (type && type->kind == TYPE_KIND_POINTER && type->element_type) {
            emit_formatted(codegen, "gray_string_lit(\"^%s\")",
                codegen_written_type_name(codegen, type->element_type, written, sizeof(written)));
        } else {
            const char *type_spelling = type ? type_name(type) : "unknown";
            emit_formatted(codegen, "gray_string_lit(\"%s\")",
                codegen_written_type_name(codegen, type_spelling, written, sizeof(written)));
        }
        return true;
    }

    if (strcmp(function_name, "fields") == 0 && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        GrayType *type = type_table_get(codegen->type_table, argument);
        const char *struct_name_text = NULL;
        if (type && type->kind == TYPE_KIND_STRUCT && type->name) {
            struct_name_text = type->name;
        } else if (type && type->kind == TYPE_KIND_POINTER && type->element_type) {
            struct_name_text = type->element_type;
        }
        AstNode *struct_declaration = struct_name_text ? find_struct_declaration(codegen, struct_name_text) : NULL;
        if (struct_declaration) {
            int struct_field_count = struct_declaration->data.struct_declaration.field_count;
            if (struct_field_count == 0) {
                emit(codegen, "gray_array_from(gray_default_arena, (GrayString[]){gray_string_lit(\"\")}, sizeof(GrayString), 0, GRAY_ELEM_STRING)");
            } else {
                emit(codegen, "gray_array_from(gray_default_arena, (GrayString[]){");
                for (int i = 0; i < struct_field_count; i++) {
                    if (i > 0) emit(codegen, ", ");
                    emit_formatted(codegen, "gray_string_lit(\"%s\")", struct_declaration->data.struct_declaration.fields[i].name);
                }
                emit_formatted(codegen, "}, sizeof(GrayString), %d, GRAY_ELEM_STRING)", struct_field_count);
            }
        } else {
            emit(codegen, "gray_array_from(gray_default_arena, (GrayString[]){gray_string_lit(\"\")}, sizeof(GrayString), 0, GRAY_ELEM_STRING)");
        }
        return true;
    }

    if (strcmp(function_name, "size_of") == 0 && node->data.call.argument_count == 1) {
        AstNode *type_argument = node->data.call.arguments[0];
        if (type_argument->kind == NODE_LABEL) {
            emit_formatted(codegen, "(int64_t)sizeof(%s)", gray_type_to_c_codegen(codegen, type_argument->data.label.value));
        } else {
            /* Literal or expression: infer C type and emit sizeof() */
            const char *c_type = NULL;
            if (type_argument->kind == NODE_INTEGER_LITERAL) {
                c_type = "int64_t";
            } else if (type_argument->kind == NODE_FLOATING_POINT_LITERAL) {
                c_type = "double";
            } else if (type_argument->kind == NODE_BOOL_VALUE) {
                c_type = "bool";
            } else if (type_argument->kind == NODE_STRING_VALUE ||
                       type_argument->kind == NODE_INTERPOLATED_STRING) {
                c_type = "GrayString";
            } else if (type_argument->kind == NODE_CHAR_VALUE) {
                c_type = "int32_t";
            } else {
                /* Fallback: consult the type table */
                GrayType *type = type_table_get(codegen->type_table, type_argument);
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

    if (strcmp(function_name, "addr") == 0 && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        /* Special case: addr(p^.field) or addr(p.field) where p: ^T.
         * Normal codegen wraps pointer deref member access in a GCC
         * statement expression → rvalue; &(rvalue) is illegal in C.
         * Detect these patterns and emit &(_dp->field) directly so
         * GRAY_ARRAY_GET / clang receive a proper assignable target. */
        AstNode *addr_pointer_expression = NULL;
        const char *address_field = NULL;
        if (argument->kind == NODE_MEMBER_EXPRESSION) {
            AstNode *object = argument->data.member.object;
            if (object->kind == NODE_POSTFIX_EXPRESSION &&
                object->data.postfix.operator == TOKEN_CARET) {
                /* addr(p^.field): p is the underlying pointer */
                addr_pointer_expression = object->data.postfix.left;
                address_field = argument->data.member.member;
            } else {
                /* addr(p.field) where p is a pointer type (auto-deref) */
                GrayType *object_type = type_table_get(codegen->type_table, object);
                if (object_type && object_type->kind == TYPE_KIND_POINTER) {
                    addr_pointer_expression = object;
                    address_field = argument->data.member.member;
                }
            }
        }
        if (addr_pointer_expression && address_field) {
            int temporary_id = codegen_next_id(codegen);
            emit_formatted(codegen, "({ __auto_type _aadp%d = ", temporary_id);
            emit_expression(codegen, addr_pointer_expression);
            emit_formatted(codegen, "; if (!_aadp%d) { %s; } "
                      "&_aadp%d->%s; })",
                  temporary_id, panic_call(codegen, node, "P0080", ""), temporary_id, sanitize_name(address_field));
        } else if (argument->kind == NODE_POSTFIX_EXPRESSION && argument->data.postfix.operator == TOKEN_CARET) {
            /* addr(p^): &(*p) simplifies to p; nil-check p first */
            AstNode *inner = argument->data.postfix.left;
            bool inner_raw = (inner->kind == NODE_LABEL &&
                              is_raw_variable(codegen, inner->data.label.value));
            if (inner_raw) {
                emit_expression(codegen, inner);
            } else {
                int temporary_id = codegen_next_id(codegen);
                emit_formatted(codegen, "({ __auto_type _aadp%d = ", temporary_id);
                emit_expression(codegen, inner);
                emit_formatted(codegen, "; if (!_aadp%d) { %s; } _aadp%d; })",
                      temporary_id, panic_call(codegen, node, "P0080", ""), temporary_id);
            }
        } else {
            /* addr() returns a pointer to the argument */
            emit(codegen, "&");
            emit_expression(codegen, argument);
        }
        return true;
    }

    if (strcmp(function_name, "raw") == 0 && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        /* raw() is the unsafe escape hatch that bypasses const-source write
         * protection.  When the source variable is const, &var produces a
         * const-qualified pointer in C, which __auto_type would propagate.
         * Look up the result type and emit an explicit cast to strip const. */
        const char *raw_cast = NULL;
        GrayType *raw_type = type_table_get(codegen->type_table, node);
        if (raw_type && raw_type->kind == TYPE_KIND_POINTER && raw_type->name) {
            char pointer_name_buffer[MESSAGE_BUFFER_SIZE];
            snprintf(pointer_name_buffer, sizeof(pointer_name_buffer), "^%s", raw_type->name);
            raw_cast = gray_type_to_c_codegen(codegen, pointer_name_buffer);
        }
        /* Handle raw(p^.field) / raw(p.field) where p is a pointer —
         * same pattern as addr() but without the nil check. */
        AstNode *raw_pointer_expression = NULL;
        const char *raw_field = NULL;
        if (argument->kind == NODE_MEMBER_EXPRESSION) {
            AstNode *object = argument->data.member.object;
            if (object->kind == NODE_POSTFIX_EXPRESSION &&
                object->data.postfix.operator == TOKEN_CARET) {
                raw_pointer_expression = object->data.postfix.left;
                raw_field = argument->data.member.member;
            } else {
                GrayType *object_type = type_table_get(codegen->type_table, object);
                if (object_type && object_type->kind == TYPE_KIND_POINTER) {
                    raw_pointer_expression = object;
                    raw_field = argument->data.member.member;
                }
            }
        }
        if (raw_pointer_expression && raw_field) {
            /* raw(p^.field): bare &p->field, no nil check */
            if (raw_cast) emit_formatted(codegen, "(%s)", raw_cast);
            emit(codegen, "&(");
            emit_expression(codegen, raw_pointer_expression);
            emit_formatted(codegen, ")->%s", sanitize_name(raw_field));
        } else if (argument->kind == NODE_POSTFIX_EXPRESSION && argument->data.postfix.operator == TOKEN_CARET) {
            /* raw(p^): &(*p) simplifies to p, no nil check */
            if (raw_cast) emit_formatted(codegen, "(%s)", raw_cast);
            emit_expression(codegen, argument->data.postfix.left);
        } else {
            if (raw_cast) emit_formatted(codegen, "(%s)", raw_cast);
            emit(codegen, "&");
            emit_expression(codegen, argument);
        }
        return true;
    }

    if (strcmp(function_name, "ref") == 0 && node->data.call.argument_count == 1) {
        /* Check if argument is a function name; emit as function pointer */
        if (node->data.call.arguments[0]->kind == NODE_LABEL) {
            AstNode *target = find_referenced_function(codegen, node->data.call.arguments[0]);
            if (target) {
                /* Function reference: emit gray_fn_name (function pointer).
                 * The declaration carries the mangled name, which is the
                 * symbol the function is defined under. */
                emit_formatted(codegen, "gray_fn_%s", target->data.function_declaration.name);
                return true;
            }
        }
        /* Variable reference: emit &var */
        emit(codegen, "&");
        emit_expression(codegen, node->data.call.arguments[0]);
        return true;
    }

    if (emit_passthrough_call(codegen, node, function_name, builtin_passthrough)) return true;

    if (strcmp(function_name, "here") == 0 && node->data.call.argument_count == 0) {
        /* Compile-time substitution: emit a SourceLocation literal with the
         * source position of the 'here' identifier itself (not the '(' that
         * follows it, which is what NODE_CALL_EXPRESSION's own token points at). */
        AstNode *function_node = node->data.call.function;
        Token location_token = function_node ? function_node->token : node->token;
        /* tok.file comes straight from the parser for imported files, so it
         * still needs the separator normalization codegen->file already had. */
        char *normalized = location_token.file ? normalize_path_separators(location_token.file) : NULL;
        const char *file = normalized ? normalized : codegen->file;
        emit_formatted(codegen,
            "(GrayStruct_SourceLocation){.file = gray_string_lit(\"%s\"), "
            ".line = %d, .column = %d}",
            file ? file : "", location_token.line, location_token.column);
        free(normalized);
        return true;
    }

    if (strcmp(function_name, "embed") == 0 && node->data.call.argument_count == 1) {
        /* Compile-time file embedding: read the file and emit its content as
         * an GrayString literal. Path is resolved relative to the source file. */
        AstNode *argument = node->data.call.arguments[0];
        const char *embed_path = argument->data.string_value.value;

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

        FILE *embed_file = fopen(resolved, "rb");
        if (!embed_file) {
            /* Typechecker validated this; reaching here is an ICE */
            codegen_internal_error("embed(): file not readable after typechecker validation", __FILE__, __LINE__);
            return true;
        }
        fseek(embed_file, 0, SEEK_END);
        long file_size = ftell(embed_file);
        fseek(embed_file, 0, SEEK_SET);

        unsigned char *file_buffer = malloc((size_t)(file_size > 0 ? file_size : 1));
        size_t bytes_read = fread(file_buffer, 1, (size_t)file_size, embed_file);
        fclose(embed_file);

        /* Emit every byte as \xNN — safe because after 2 hex digits the next
         * character is always \ or " so no hex-continuation ambiguity. */
        char *escaped = malloc((size_t)(bytes_read * 4 + 16));
        size_t escaped_length = 0;
        for (size_t i = 0; i < bytes_read; i++) {
            escaped_length += (size_t)snprintf(escaped + escaped_length, 5, "\\x%02x", file_buffer[i]);
        }
        escaped[escaped_length] = '\0';

        /* GRAY_STRING_LIT is a compile-time macro — valid at file scope */
        emit(codegen, "GRAY_STRING_LIT(\"");
        emit(codegen, escaped);
        emit(codegen, "\")");

        free(file_buffer);
        free(escaped);
        return true;
    }

    if (strcmp(function_name, "assert") == 0 && node->data.call.argument_count >= 1) {
        emit(codegen, "gray_builtin_assert(");
        emit_expression(codegen, node->data.call.arguments[0]);
        if (node->data.call.argument_count >= 2) {
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[1]);
        } else {
            emit(codegen, ", gray_string_lit(\"\")");
        }
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }

    if (strcmp(function_name, "error") == 0 && node->data.call.argument_count >= 1) {
        /* Forms: error(msg), error(code), error(code, msg). The first argument is a
         * code unless it is a plain string; slot 0 (Unknown) is the default. */
        AstNode *first_argument = node->data.call.arguments[0];
        GrayType *first_argument_type = type_table_get(codegen->type_table, first_argument);
        bool first_is_code = !(first_argument_type && first_argument_type->kind == TYPE_KIND_STRING);
        emit(codegen, "gray_error_new(gray_default_arena, ");
        if (first_is_code) {
            emit(codegen, "(int64_t)(");
            emit_expression(codegen, first_argument);
            emit(codegen, "), ");
            if (node->data.call.argument_count >= 2) {
                emit_expression(codegen, node->data.call.arguments[1]);
            } else {
                emit(codegen, "gray_string_lit(\"\")");
            }
        } else {
            emit(codegen, "0, ");
            emit_expression(codegen, first_argument);
        }
        emit(codegen, ")");
        return true;
    }

    if (strcmp(function_name, "input") == 0) {
        emit(codegen, "gray_builtin_input(gray_default_arena)");
        return true;
    }

    if (strcmp(function_name, "flush") == 0) {
        emit(codegen, "gray_builtin_flush()");
        return true;
    }


    if (strcmp(function_name, "eprintln") == 0) {
        if (node->data.call.argument_count == 0) {
            emit(codegen, "fputc('\\n', stderr)");
        } else {
            if (emit_composite_print(codegen, node, "stderr", true)) return true;
            emit_print_variant(codegen, node, "eprintln");
        }
        return true;
    }

    if (strcmp(function_name, "eprint") == 0 && node->data.call.argument_count > 0) {
        if (emit_composite_print(codegen, node, "stderr", false)) return true;
        emit_print_variant(codegen, node, "eprint");
        return true;
    }

    /* Bigint conversion functions: i128(), u128(), i256(), u256() */
    if (node->data.call.argument_count == 1 && is_wide_integer_type_name(function_name)) {
        AstNode *c_string_argument = node->data.call.arguments[0];
        const char *source_wide_integer = resolve_wide_integer_type(codegen, c_string_argument);
        const char *prefix = wide_integer_prefix(function_name);
        if (source_wide_integer && strcmp(source_wide_integer, function_name) == 0) {
            /* Already the target type: a literal took it, or an identity cast. */
            emit_expression(codegen, c_string_argument);
        } else if (source_wide_integer) {
            /* Wide integer→wide integer cast */
            emit_formatted(codegen, "%s_from_%s(", prefix, source_wide_integer);
            emit_expression(codegen, c_string_argument);
            emit(codegen, ")");
        } else {
            /* Scalar→wide integer: e.g., gray_i128_from_i64(x) */
            emit_scalar_to_wide_integer(codegen, function_name, c_string_argument, NULL);
        }
        return true;
    }

    /* char() and bool() conversion functions */
    if (node->data.call.argument_count == 1) {
        const char *cast_type = NULL;
        if (strcmp(function_name, "char") == 0) cast_type = "int32_t";
        else if (strcmp(function_name, "bool") == 0) cast_type = "bool";
        if (cast_type) {
            AstNode *c_string_argument = node->data.call.arguments[0];
            /* Bigint→scalar: e.g., char(x128) → gray_i128_to_i64(x128) */
            const char *source_wide_integer = resolve_wide_integer_type(codegen, c_string_argument);
            bool to_char = strcmp(function_name, "char") == 0;
            if (source_wide_integer) {
                const char *source_prefix = wide_integer_prefix(source_wide_integer);
                bool is_source_unsigned = (strcmp(source_wide_integer, "u128") == 0 || strcmp(source_wide_integer, "u256") == 0);
                const char *to_suffix = is_source_unsigned ? "u64" : "i64";
                if (to_char)
                    emit_formatted(codegen, "((%s)%s(", cast_type,
                                   is_source_unsigned ? "gray_ucast_check_u64" : "gray_ucast_check");
                emit_formatted(codegen, "((%s)%s_to_%s(", cast_type, source_prefix, to_suffix);
                emit_expression(codegen, c_string_argument);
                emit_formatted(codegen, ", \"%s\", %d))", codegen->file, node->token.line);
                if (to_char) {
                    emit(codegen, ", ");
                    emit_sized_bounds_arguments(codegen, NULL, CHAR_CODEPOINT_MAX, true, "char", node->token.line);
                    emit(codegen, "))");
                }
                return true;
            }
            GrayType *c_argument_type = type_table_get(codegen->type_table, c_string_argument);
            if (to_char && !(c_argument_type && c_argument_type->kind == TYPE_KIND_CHAR)) {
                emit_formatted(codegen, "((%s)", cast_type);
                emit_range_checked_narrowing(codegen, c_argument_type, c_string_argument, NULL, NULL, CHAR_CODEPOINT_MAX,
                                             true, "char", node->token.line);
                emit(codegen, ")");
                return true;
            }
            emit_formatted(codegen, "((%s)(", cast_type);
            emit_expression(codegen, c_string_argument);
            emit(codegen, "))");
            return true;
        }
        /* string() conversion */
        if (strcmp(function_name, "string") == 0) {
            emit_to_string(codegen, node->data.call.arguments[0]);
            return true;
        }
    }

    if (strcmp(function_name, "copy") == 0 && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        GrayType *argument_type = type_table_get(codegen->type_table, argument);
        if (argument_type && (argument_type->kind == TYPE_KIND_ARRAY || argument_type->kind == TYPE_KIND_MAP || argument_type->kind == TYPE_KIND_STRUCT)) {
            /* Route every container kind through the unified deep-copy
             * emitter so nested collections, structs containing
             * collections, collections containing such structs, and any
             * transitive mix of those all come out fully independent
             * of the source , ). */
            int unique_id = codegen_next_id(codegen);
            const char *c_type = (argument_type->kind == TYPE_KIND_ARRAY) ? "GrayArray"
                               : (argument_type->kind == TYPE_KIND_MAP) ? "GrayMap"
                               : gray_type_to_c_codegen(codegen, argument_type->name);
            emit_formatted(codegen, "({ %s _cpy%d = ", c_type, unique_id);
            emit_expression(codegen, argument);
            emit(codegen, "; ");
            char source_variable[SHORT_VARIABLE_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_cpy%d", unique_id);
            char full_type_name[MESSAGE_BUFFER_SIZE];
            if (argument_type->kind == TYPE_KIND_ARRAY) {
                snprintf(full_type_name, sizeof(full_type_name), "[%s]",
                    argument_type->element_type ? argument_type->element_type : "");
            } else if (argument_type->kind == TYPE_KIND_MAP) {
                snprintf(full_type_name, sizeof(full_type_name), "map[%s:%s]",
                    argument_type->key_type ? argument_type->key_type : "",
                    argument_type->value_type ? argument_type->value_type : "");
            } else {
                const char *_name = argument_type->name ? argument_type->name : "";
                strncpy(full_type_name, _name, sizeof(full_type_name) - 1);
                full_type_name[sizeof(full_type_name) - 1] = '\0';
            }
            emit_value_deep_copy(codegen, full_type_name, source_variable);
            emit(codegen, "; })");
        } else {
            emit_expression(codegen, argument);
        }
        return true;
    }

    if (strcmp(function_name, "print") == 0 && node->data.call.argument_count > 0) {
        if (emit_composite_print(codegen, node, "stdout", false)) return true;
        emit_print_variant(codegen, node, "print");
        return true;
    }

    /* to_char(str, index); extract Nth Unicode codepoint */
    if (strcmp(function_name, "to_char") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_builtin_to_char(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }

    /* char_count(str); return Unicode codepoint count */
    /* c_string(ptr); convert C char* to Grayscale string. Copies onto the
     * arena so the result is safe to use even after the C-side buffer
     * is freed or overwritten. NULL maps to "" instead of crashing. */
    if (strcmp(function_name, "c_string") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_c_string_dup(gray_default_arena, (const char *)");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }

    return false;
}

/* --- @mem module --- */

static const PassthroughCall mem_passthrough[] = {
    {"arena", 1, "gray_mem_arena"},
    {"reset", 1, "gray_mem_reset"},
    {"usage", 1, "gray_mem_usage"},
    {"raw_copy", 3, "gray_mem_copy"},
    {"zero", 2, "gray_mem_zero"},
    {"fill", 3, "gray_mem_set"},
    {NULL, 0, NULL},
};

static bool emit_mem_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (emit_passthrough_call(codegen, node, function_name, mem_passthrough)) return true;
    if (strcmp(function_name, "destroy") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_mem_destroy(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }
    if (strcmp(function_name, "init") == 0 && node->data.call.argument_count == 2) {
        AstNode *arena_argument = node->data.call.arguments[0];
        AstNode *type_argument = node->data.call.arguments[1];
        const char *type_spelling = "int64_t";
        if (type_argument->kind == NODE_LABEL) {
            type_spelling = gray_type_to_c_codegen(codegen, type_argument->data.label.value);
        }
        emit_formatted(codegen, "(%s *)gray_arena_alloc(", type_spelling);
        emit_expression(codegen, arena_argument);
        emit_formatted(codegen, ", sizeof(%s))", type_spelling);
        return true;
    }
    if (strcmp(function_name, "alloc") == 0 && node->data.call.argument_count == 2) {
        AstNode *arena_argument = node->data.call.arguments[0];
        AstNode *value_argument = node->data.call.arguments[1];
        GrayType *value_type = type_table_get(codegen->type_table, value_argument);

        /* alloc(a Arena, value T) -> ^T for every T. The string and array
         * branches below build their value in the target arena and used to
         * hand it back directly, so the same call returned a pointer or a
         * value depending on what it was given. Binding the arena once and
         * boxing whatever the branch produced keeps a single return shape
         * without evaluating the arena expression twice. */
        int unique_id = codegen_next_id(codegen);
        /* The pointer is typed from the value's Grayscale type, not deduced
         * from the C expression: __auto_type on a literal `64` deduces C `int`,
         * and a `^i64` is int64_t *. */
        char c_value_type[MESSAGE_BUFFER_SIZE];
        snprintf(c_value_type, sizeof(c_value_type), "%s",
                 (value_type && value_type->kind != TYPE_KIND_UNKNOWN && type_name(value_type))
                     ? gray_type_to_c_codegen(codegen, type_name(value_type))
                     : (value_argument->kind == NODE_ARRAY_VALUE ? "GrayArray" : "__auto_type"));
        emit_formatted(codegen, "({ GrayArena *_aa%d = ", unique_id);
        emit_expression(codegen, arena_argument);
        emit_formatted(codegen, "; %s _av%d = ", c_value_type, unique_id);
        if (value_type && value_type->kind == TYPE_KIND_STRING) {
            emit_formatted(codegen, "({ GrayString _s%d = ", unique_id);
            emit_expression(codegen, value_argument);
            emit_formatted(codegen, "; gray_string_new(_aa%d, _s%d.data, _s%d.len); })",
                           unique_id, unique_id, unique_id);
        } else if (value_argument->kind == NODE_ARRAY_VALUE) {
            int count = value_argument->data.array_value.count;
            GrayType *element_type = (count > 0 && codegen->type_table)
                ? type_table_get(codegen->type_table, value_argument->data.array_value.elements[0])
                : NULL;
            const char *c_type = "int64_t";
            if (element_type) {
                if (element_type->kind == TYPE_KIND_FLOATING_POINT) c_type = "double";
                else if (element_type->kind == TYPE_KIND_STRING) c_type = "GrayString";
                else if (element_type->kind == TYPE_KIND_BOOL) c_type = "bool";
            }
            emit_formatted(codegen, "gray_array_from(_aa%d, (%s[]){", unique_id, c_type);
            for (int i = 0; i < count; i++) {
                if (i > 0) emit(codegen, ", ");
                emit_expression(codegen, value_argument->data.array_value.elements[i]);
            }
            emit_formatted(codegen, "}, sizeof(%s), %d, GRAY_ELEM_KIND_OF(%s))", c_type, count, c_type);
        } else {
            emit_expression(codegen, value_argument);
        }
        emit_formatted(codegen,
            "; __typeof__(_av%d) *_ap%d = (__typeof__(_av%d) *)gray_arena_alloc(_aa%d, sizeof(_av%d)); "
            "*_ap%d = _av%d; _ap%d; })",
            unique_id, unique_id, unique_id, unique_id, unique_id, unique_id, unique_id, unique_id);
        return true;
    }
    return false;
}

/* --- @math module --- */

static bool emit_math_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if ((strcmp(function_name, "abs") == 0 || strcmp(function_name, "neg") == 0) && node->data.call.argument_count == 1) {
        AstNode *argument = node->data.call.arguments[0];
        GrayType *argument_type = type_table_get(codegen->type_table, argument);
        if (argument_type && argument_type->name && is_wide_integer_type_name(argument_type->name)) {
            /* An unsigned wide value is its own absolute value. */
            if (argument_type->kind == TYPE_KIND_UNSIGNED_INTEGER) {
                emit_expression(codegen, argument);
                return true;
            }
            emit_formatted(codegen, "%s_%s_checked(", wide_integer_prefix(argument_type->name), function_name);
            emit_expression(codegen, argument);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
            return true;
        }
        if (argument_type && argument_type->kind == TYPE_KIND_SIGNED_INTEGER) {
            /* Negating the most negative value overflows, as `-n` does. */
            if (strcmp(function_name, "neg") == 0) {
                emit_checked_negation(codegen, argument_type, argument, NULL, node->token.line);
                return true;
            }
            char temporary_name[32];
            snprintf(temporary_name, sizeof(temporary_name), "_abs%d", codegen_next_id(codegen));
            emit_formatted(codegen, "({ %s %s = ", gray_type_to_c_codegen(codegen, argument_type->name), temporary_name);
            emit_expression(codegen, argument);
            emit_formatted(codegen, "; %s < 0 ? ", temporary_name);
            emit_checked_negation(codegen, argument_type, NULL, temporary_name, node->token.line);
            emit_formatted(codegen, " : %s; })", temporary_name);
            return true;
        }
        if (strcmp(function_name, "neg") == 0) {
            emit(codegen, "(-(");
            emit_expression(codegen, argument);
            emit(codegen, "))");
            return true;
        }
        const char *suffix = (argument_type && argument_type->kind == TYPE_KIND_FLOATING_POINT) ? "f64" :
                              (argument_type && argument_type->kind == TYPE_KIND_UNSIGNED_INTEGER) ? "u64" : "i64";
        emit_formatted(codegen, "gray_math_abs_%s(", suffix);
        emit_expression(codegen, argument);
        emit(codegen, ")");
        return true;
    }
    if (((strcmp(function_name, "min") == 0 || strcmp(function_name, "max") == 0) && node->data.call.argument_count == 2) ||
        (strcmp(function_name, "clamp") == 0 && node->data.call.argument_count == 3)) {
        /* The result is the call's type T. The helpers work at 64 bits, so
         * when an argument is not already T (a literal, or a wider value)
         * the result is range-checked back into T. */
        GrayType *result_type = type_table_get(codegen->type_table, node);
        if (result_type && is_wide_integer_type_name(result_type->name)) {
            /* A wide integer: every argument is already T, compared with
             * T's own less-than. */
            const char *c_type = gray_type_to_c_codegen(codegen, result_type->name);
            int unique_id = codegen_next_id(codegen);
            emit(codegen, "({ ");
            for (int i = 0; i < node->data.call.argument_count; i++) {
                emit_formatted(codegen, "%s _mm%d_%d = ", c_type, unique_id, i);
                emit_expression(codegen, node->data.call.arguments[i]);
                emit(codegen, "; ");
            }
            const char *name = result_type->name;
            if (strcmp(function_name, "clamp") == 0)
                emit_formatted(codegen, "gray_%s_lt(_mm%d_0, _mm%d_1) ? _mm%d_1 : "
                               "gray_%s_lt(_mm%d_2, _mm%d_0) ? _mm%d_2 : _mm%d_0; })",
                               name, unique_id, unique_id, unique_id, name, unique_id, unique_id, unique_id, unique_id);
            else if (strcmp(function_name, "min") == 0)
                emit_formatted(codegen, "gray_%s_lt(_mm%d_1, _mm%d_0) ? _mm%d_1 : _mm%d_0; })",
                               name, unique_id, unique_id, unique_id, unique_id);
            else
                emit_formatted(codegen, "gray_%s_lt(_mm%d_0, _mm%d_1) ? _mm%d_1 : _mm%d_0; })",
                               name, unique_id, unique_id, unique_id, unique_id);
            return true;
        }
        const char *suffix = (result_type && result_type->kind == TYPE_KIND_FLOATING_POINT) ? "f64" :
                              (result_type && result_type->kind == TYPE_KIND_UNSIGNED_INTEGER) ? "u64" : "i64";
        const char *minimum_text = NULL, *maximum_text = NULL;
        bool is_unsigned = false;
        bool needs_check = false;
        if (result_type && result_type->name &&
            integer_type_name_bounds(result_type->name, &minimum_text, &maximum_text, &is_unsigned)) {
            for (int i = 0; i < node->data.call.argument_count; i++) {
                GrayType *argument_type = type_table_get(codegen->type_table, node->data.call.arguments[i]);
                if (!argument_type || !argument_type->name || strcmp(argument_type->name, result_type->name) != 0)
                    needs_check = true;
            }
        }
        if (needs_check) emit_formatted(codegen, "(%s)%s%s(", gray_type_to_c_codegen(codegen, result_type->name),
                                        is_unsigned ? "gray_ucast_check" : "gray_cast_check",
                                        strcmp(suffix, "u64") == 0 ? "_u64" : "");
        emit_formatted(codegen, "gray_math_%s_%s(", function_name, suffix);
        for (int i = 0; i < node->data.call.argument_count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[i]);
        }
        emit(codegen, ")");
        if (needs_check) {
            emit(codegen, ", ");
            emit_sized_bounds_arguments(codegen, minimum_text, maximum_text, is_unsigned, result_type->name, node->token.line);
            emit(codegen, ")");
        }
        return true;
    }
    /* Generic: math.func(args...) → gray_math_func(args...) */
    emit_formatted(codegen, "gray_math_%s(", function_name);
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* Helper: returns true when the expression is an assignable target whose address can be
 * taken directly with &.  For rvalues (function calls, literals, etc.) we must
 * materialise into a statement-expression temporary first. */
static bool expression_is_assignable(AstNode *expression) {
    return expression->kind == NODE_LABEL ||
           expression->kind == NODE_MEMBER_EXPRESSION ||
           expression->kind == NODE_INDEX_EXPRESSION;
}

/* Emit &expr, materialising rvalues into a statement-expression temporary
 * whose name is generated internally via codegen_next_id(). */
static void emit_address_of(CodeGen *codegen, AstNode *expression) {
    /* Anything reached through a pointer is emitted as a nil-checked GCC
     * statement expression, whose result is an rvalue — `&` on one of those
     * is invalid C, and materialising a copy instead would silently drop the
     * callee's mutations. Hoist the nil check and take the address off the
     * pointer itself, which is already an assignable target.
     * Covers `p.field` (auto-deref), `p^.field`, and a bare `p^`. */
    AstNode *pointer_expression = NULL;      /* pointer to nil-check */
    /* Field path from the pointee down to the target, innermost last
     * (["inner", "items"] for `p.inner.items`). Empty for a bare deref. */
    #define ADDR_OF_MAX_FIELDS 16
    const char *field_path[ADDR_OF_MAX_FIELDS];
    int field_depth = 0;
    if (expression->kind == NODE_MEMBER_EXPRESSION) {
        /* Walk a chain of value-struct field accesses down to the first base
         * reached through a pointer, so the whole chain is taken off that
         * pointer as one lvalue (&_ap->inner.items) rather than off a
         * by-value statement-expression result. */
        AstNode *current = expression;
        while (current->kind == NODE_MEMBER_EXPRESSION && field_depth < ADDR_OF_MAX_FIELDS) {
            AstNode *object = current->data.member.object;
            field_path[field_depth++] = current->data.member.member;
            bool is_object_reference = (object->kind == NODE_LABEL &&
                is_reference_variable(codegen, object->data.label.value));
            GrayType *object_type = type_table_get(codegen->type_table, object);
            if (!is_object_reference && object_type && object_type->kind == TYPE_KIND_POINTER) {
                pointer_expression = object;
                break;
            }
            if (object->kind == NODE_POSTFIX_EXPRESSION && object->data.postfix.operator == TOKEN_CARET) {
                /* p^.field...: strip the deref, use the underlying pointer */
                pointer_expression = object->data.postfix.left;
                break;
            }
            current = object;
        }
        if (!pointer_expression) field_depth = 0;
    } else if (expression->kind == NODE_POSTFIX_EXPRESSION && expression->data.postfix.operator == TOKEN_CARET) {
        /* p^: the pointer already has the type the callee wants */
        pointer_expression = expression->data.postfix.left;
    }
    if (pointer_expression) {
        bool is_raw = (pointer_expression->kind == NODE_LABEL &&
            is_raw_variable(codegen, pointer_expression->data.label.value));
        if (is_raw && field_depth == 0) {
            emit_expression(codegen, pointer_expression);
            return;
        }
        int unique_id = codegen_next_id(codegen);
        emit_formatted(codegen, "({ __auto_type _ap%d = ", unique_id);
        emit_expression(codegen, pointer_expression);
        if (is_raw) {
            emit(codegen, "; ");
        } else {
            emit_formatted(codegen, "; if (!_ap%d) { %s; } ",
                unique_id, panic_call(codegen, expression, "P0080", ""));
        }
        if (field_depth > 0) {
            emit_formatted(codegen, "&_ap%d", unique_id);
            for (int i = field_depth - 1; i >= 0; i--)
                emit_formatted(codegen, "%s%s", i == field_depth - 1 ? "->" : ".",
                    sanitize_name(field_path[i]));
            emit(codegen, "; })");
        } else {
            emit_formatted(codegen, "_ap%d; })", unique_id);
        }
        return;
    }
    #undef ADDR_OF_MAX_FIELDS
    if (expression_is_assignable(expression)) {
        emit(codegen, "&");
        emit_expression(codegen, expression);
    } else {
        /* Materialize the rvalue as a one-element array compound literal,
         * which decays to the pointer the callee wants. Its lifetime is the
         * enclosing block — unlike a statement-expression local, whose
         * storage ends at the closing brace, leaving the escaping pointer
         * dangling (GCC -O2 reuses the slot; clang only survived by luck).
         * __typeof__ does not evaluate its operand, so the expression text
         * appears twice but runs once. */
        emit(codegen, "(__typeof__(");
        emit_expression(codegen, expression);
        emit(codegen, ")[]){");
        emit_expression(codegen, expression);
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
static void emit_mutable_call_argument(CodeGen *codegen, AstNode *argument, bool is_mutable_parameter_flag) {
    if (!is_mutable_parameter_flag) {
        emit_expression(codegen, argument);
        return;
    }
    if (argument->kind == NODE_POSTFIX_EXPRESSION && argument->data.postfix.operator == TOKEN_CARET) {
        /* &(p^) cancels out — emit the inner pointer directly */
        emit_expression(codegen, argument->data.postfix.left);
        return;
    }
    if (argument->kind == NODE_LABEL) {
        const char *value_name = argument->data.label.value;
        if (is_mutable_parameter(codegen, value_name)) { emit(codegen, value_name); return; }
        if (label_is_entry_global(argument)) {
            emit_formatted(codegen, "&%s", global_variable_c_name(codegen, value_name));
            return;
        }
        /* A bare name that names a module-level declaration is emitted under
         * its mangled name; resolve it the same way emit_label() does before
         * taking its address. */
        const char *resolved = codegen_resolve_label(codegen, argument, value_name);
        emit_formatted(codegen, "&%s", sanitize_name(resolved != value_name ? resolved : value_name));
        return;
    }
    if (argument->kind == NODE_INDEX_EXPRESSION) {
        /* Array/map indexing always codegens as a GNU statement-expression
         * whose result is a dereferenced value, not an lvalue — `&` on that
         * is invalid C. Build a statement-expression that resolves to the
         * pointer itself instead, mirroring gray_array_get_ptr/gray_map_get. */
        GrayType *left_type = type_table_get(codegen->type_table, argument->data.index_expression.left);
        if (left_type && left_type->kind == TYPE_KIND_MAP) {
            const char *c_key_type = "GrayString";
            if (left_type->key_type) c_key_type = gray_map_element_c_type(codegen, left_type->key_type);
            emit_formatted(codegen, "({ %s _mk = ", c_key_type);
            emit_map_slot_value(codegen, left_type->key_type, argument->data.index_expression.index);
            emit(codegen, "; void *_mv = gray_map_get(&");
            emit_expression(codegen, argument->data.index_expression.left);
            emit_formatted(codegen, ", &_mk); if (!_mv) { %s; } ",
                panic_call(codegen, argument, "P0081", ""));
            /* Computed here, after c_key has been emitted: both share the
             * one static buffer gray_type_to_c_codegen returns. */
            const char *c_value_type = left_type->value_type
                ? gray_map_element_c_type(codegen, left_type->value_type) : "int64_t";
            emit_formatted(codegen, "(%s *)_mv; })", c_value_type);
        } else {
            /* The element pointer is typed by the array's element type. A
             * fixed int64_t * here is the wrong pointer type for every
             * element that is not an integer. */
            const char *c_element_type = (left_type && left_type->element_type)
                ? gray_map_element_c_type(codegen, left_type->element_type) : "int64_t";
            emit_formatted(codegen, "(%s *)gray_array_get_ptr(&", c_element_type);
            emit_expression(codegen, argument->data.index_expression.left);
            emit(codegen, ", ");
            emit_expression(codegen, argument->data.index_expression.index);
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, argument->token.line);
        }
        return;
    }
    /* NODE_MEMBER_EXPRESSION (struct field) goes through emit_address_of, which
     * already knows how to take its address correctly. */
    emit_address_of(codegen, argument);
}

/* --- @maps module --- */

static bool emit_maps_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "get_keys") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_maps_get_keys(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "get_values") == 0 && node->data.call.argument_count == 1) {
        GrayType *map_value_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        bool should_copy_map_value = map_value_type && map_value_type->kind == TYPE_KIND_MAP && map_value_type->value_type &&
            type_shares_storage(codegen, map_value_type->value_type);
        int unique_id = codegen_next_id(codegen);
        if (should_copy_map_value) emit_formatted(codegen, "({ GrayArray _gv%d = ", unique_id);
        emit(codegen, "gray_maps_get_values(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        if (should_copy_map_value) {
            /* The values are composites: copy them out of the map's slots. */
            char source_variable[SHORT_VARIABLE_BUFFER_SIZE], full_type_name[MESSAGE_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_gv%d", unique_id);
            snprintf(full_type_name, sizeof(full_type_name), "[%s]", map_value_type->value_type);
            emit(codegen, "; ");
            emit_value_deep_copy(codegen, full_type_name, source_variable);
            emit(codegen, "; })");
        }
        return true;
    }
    if (strcmp(function_name, "has_key") == 0) {
        /* Key buffer must match the map's declared key storage type
         * (gray_map_element_c_type), not whatever C type the argument expression
         * happens to have; otherwise the hash/memcmp compares the wrong
         * number of bytes. */
        const char *c_key_type = "int64_t";
        GrayType *map_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        if (map_type && map_type->kind == TYPE_KIND_MAP && map_type->key_type)
            c_key_type = gray_map_element_c_type(codegen, map_type->key_type);
        emit_formatted(codegen, "({ %s _hk = ", c_key_type);
        emit_map_slot_value(codegen, (map_type && map_type->kind == TYPE_KIND_MAP) ? map_type->key_type : NULL,
            node->data.call.arguments[1]);
        emit(codegen, "; gray_maps_has_key(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &_hk); })");
        return true;
    }
    if (strcmp(function_name, "remove_key") == 0 && node->data.call.argument_count == 2) {
        const char *c_key_type = "int64_t";
        GrayType *map_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        if (map_type && map_type->kind == TYPE_KIND_MAP && map_type->key_type)
            c_key_type = gray_map_element_c_type(codegen, map_type->key_type);
        emit_formatted(codegen, "({ %s _rk = ", c_key_type);
        emit_map_slot_value(codegen, (map_type && map_type->kind == TYPE_KIND_MAP) ? map_type->key_type : NULL,
            node->data.call.arguments[1]);
        emit(codegen, "; gray_map_remove(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, ", &_rk, \"%s\", %d); })", codegen->file, node->token.line);
        return true;
    }
    if (strcmp(function_name, "clear") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_map_clear(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }
    if (strcmp(function_name, "is_empty") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_maps_is_empty(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "merge") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_maps_merge(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "is_equal") == 0 && node->data.call.argument_count == 2) {
        GrayType *map_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        bool has_string_keys = map_type && map_type->key_type && strcmp(map_type->key_type, "string") == 0;
        bool has_string_values = map_type && map_type->value_type && strcmp(map_type->value_type, "string") == 0;
        emit(codegen, "gray_maps_is_equal(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, ", %s, %s)", has_string_keys ? "true" : "false", has_string_values ? "true" : "false");
        return true;
    }
    if (strcmp(function_name, "contains_value") == 0 && node->data.call.argument_count == 2) {
        /* Determine value type from map to ensure correct size */
        GrayType *map_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *c_value_type = "int64_t";
        const char *wide_integer_value = NULL;
        if (map_type && map_type->value_type) {
            /* Match the slot's C type (sized integers and f32 included) so the
             * staged comparison value is the same width as the stored slot. */
            c_value_type = gray_map_element_c_type(codegen, map_type->value_type);
            if (is_wide_integer_type_name(map_type->value_type)) wide_integer_value = map_type->value_type;
        }
        emit_formatted(codegen, "({ %s _cv = ", c_value_type);
        emit_map_slot_value(codegen, wide_integer_value, node->data.call.arguments[1]);
        emit(codegen, "; gray_maps_contains_value(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &_cv); })");
        return true;
    }
    if (strcmp(function_name, "get_or_default") == 0 && node->data.call.argument_count == 3) {
        /* get_or_default(m, key, default); lookup key, return default if missing.
         * A wide-integer key or value needs its explicit struct type instead of
         * __auto_type/__typeof__, which would infer a plain int from a literal. */
        GrayType *map_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *wide_integer_key = (map_type && map_type->kind == TYPE_KIND_MAP && map_type->key_type &&
            is_wide_integer_type_name(map_type->key_type)) ? map_type->key_type : NULL;
        const char *wide_integer_value = (map_type && map_type->kind == TYPE_KIND_MAP && map_type->value_type &&
            is_wide_integer_type_name(map_type->value_type)) ? map_type->value_type : NULL;
        emit(codegen, "({ ");
        if (wide_integer_key) emit_formatted(codegen, "%s _gk = ", wide_integer_prefix(wide_integer_key));
        else emit(codegen, "__auto_type _gk = ");
        emit_map_slot_value(codegen, wide_integer_key, node->data.call.arguments[1]);
        emit(codegen, "; void *_gv = gray_map_get(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &_gk); _gv ? *(");
        if (wide_integer_value) {
            emit(codegen, wide_integer_prefix(wide_integer_value));
        } else {
            emit(codegen, "__typeof__(");
            emit_expression(codegen, node->data.call.arguments[2]);
            emit(codegen, ")");
        }
        emit(codegen, " *)_gv : ");
        emit_map_slot_value(codegen, wide_integer_value, node->data.call.arguments[2]);
        emit(codegen, "; })");
        return true;
    }
    return false;
}

/* --- @time module --- */

static bool emit_time_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool needs_arena = (strcmp(function_name, "format") == 0 || strcmp(function_name, "to_iso") == 0 ||
        strcmp(function_name, "date") == 0 || strcmp(function_name, "to_clock") == 0 ||
        strcmp(function_name, "humanize") == 0 || strcmp(function_name, "format_duration") == 0 ||
        strcmp(function_name, "weekday_name") == 0 || strcmp(function_name, "month_name") == 0);
    bool is_fallible = (strcmp(function_name, "parse") == 0 || strcmp(function_name, "parse_duration") == 0);

    if (is_fallible) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, is_multi_variable ? "gray_time_%s_result(" : "gray_time_%s(", function_name);
        for (int i = 0; i < node->data.call.argument_count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[i]);
        }
        emit(codegen, ")");
        return true;
    }

    emit_formatted(codegen, "gray_time_%s(", function_name);
    if (needs_arena) emit(codegen, "gray_default_arena, ");
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @runtime module --- */

static bool emit_runtime_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    (void)node;
    if (strcmp(function_name, "version") == 0) {
        emit_formatted(codegen, "gray_string_lit(\"%s\")", GRAY_VERSION);
        return true;
    }
    emit_formatted(codegen, "gray_runtime_%s()", function_name);
    return true;
}

/* --- @uuid module --- */

static bool emit_uuid_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "generate") == 0) {
        emit(codegen, "gray_uuid_generate(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "generate_compact") == 0) {
        emit(codegen, "gray_uuid_generate_compact(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "generate_random") == 0) {
        emit(codegen, "gray_uuid_generate_random(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "generate_v5") == 0) {
        emit(codegen, "gray_uuid_generate_v5(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "generate_time_ordered") == 0) {
        emit(codegen, "gray_uuid_generate_time_ordered(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "is_valid") == 0) {
        emit(codegen, "gray_uuid_is_valid(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "parse") == 0) {
        emit(codegen, "gray_uuid_parse(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "to_string") == 0) {
        emit(codegen, "gray_uuid_to_string(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "to_bytes") == 0) {
        emit(codegen, "gray_uuid_to_bytes(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "from_bytes") == 0) {
        emit(codegen, "gray_uuid_from_bytes(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "version") == 0) {
        emit(codegen, "gray_uuid_version(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    if (strcmp(function_name, "timestamp") == 0) {
        emit(codegen, "gray_uuid_timestamp(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")"); return true;
    }
    return false;
}

/* --- @regex module --- */

static const PassthroughCall regex_passthrough[] = {
    {"is_valid", 1, "gray_regex_is_valid"},
    {"is_match", 2, "gray_regex_match"},
    {"count", 2, "gray_regex_count"},
    {NULL, 0, NULL},
};

static bool emit_regex_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (emit_passthrough_call(codegen, node, function_name, regex_passthrough)) return true;
    if (strcmp(function_name, "escape") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_regex_escape(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "find") == 0 && node->data.call.argument_count == 2) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_regex_find%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "find_all") == 0 && node->data.call.argument_count == 2) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_regex_find_all%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "find_groups") == 0 && node->data.call.argument_count == 2) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_regex_find_groups%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "find_all_groups") == 0 && node->data.call.argument_count == 2) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_regex_find_all_groups%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "replace") == 0 && node->data.call.argument_count == 3) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_regex_replace%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "split") == 0 && node->data.call.argument_count == 2) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_regex_split%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @server module --- */

static const PassthroughCall server_passthrough[] = {
    {"text", 2, "gray_server_text"},
    {"json", 2, "gray_server_json"},
    {"html", 2, "gray_server_html"},
    {"redirect", 2, "gray_server_redirect"},
    {NULL, 0, NULL},
};

static bool emit_server_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "add_router") == 0) {
        emit(codegen, "gray_server_router()");
        return true;
    }
    if (strcmp(function_name, "add_route") == 0 && node->data.call.argument_count == 4) {
        emit(codegen, "gray_server_route(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ", (GrayResponse (*)(GrayRequest))");
        emit_expression(codegen, node->data.call.arguments[3]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "listen") == 0 && node->data.call.argument_count == 2) {
        /* Grayscale: server.listen(router, port)  →  C: gray_server_listen(port, &router) */
        emit(codegen, "gray_server_listen(");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "listen") == 0 && node->data.call.argument_count == 3) {
        /* Grayscale: server.listen(router, port, host)  →  C: gray_server_listen_host(port, host, &router) */
        emit(codegen, "gray_server_listen_host(");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (emit_passthrough_call(codegen, node, function_name, server_passthrough)) return true;
    if (strcmp(function_name, "cors") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_server_cors(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "use") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_server_use(");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", (GrayMiddleware)");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @http module --- */

static bool emit_http_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_multi_variable = current_variable_is_result_temporary(codegen);
    const char *suffix = is_multi_variable ? "_result" : "";
    if (strcmp(function_name, "get") == 0 && node->data.call.argument_count == 2) {
        emit_formatted(codegen, "gray_http_get%s(gray_default_arena, ", suffix);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "post") == 0 && node->data.call.argument_count == 3) {
        emit_formatted(codegen, "gray_http_post%s(gray_default_arena, ", suffix);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "put") == 0 && node->data.call.argument_count == 3) {
        emit_formatted(codegen, "gray_http_put%s(gray_default_arena, ", suffix);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "delete") == 0 && node->data.call.argument_count == 2) {
        emit_formatted(codegen, "gray_http_delete%s(gray_default_arena, ", suffix);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "head") == 0 && node->data.call.argument_count == 2) {
        emit_formatted(codegen, "gray_http_head%s(gray_default_arena, ", suffix);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "patch") == 0 && node->data.call.argument_count == 3) {
        emit_formatted(codegen, "gray_http_patch%s(gray_default_arena, ", suffix);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", &");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @net module --- */

static const PassthroughCall net_passthrough[] = {
    {"close", 1, "gray_net_close"},
    {"set_timeout", 2, "gray_net_set_timeout"},
    {NULL, 0, NULL},
};

static bool emit_net_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_multi_variable = current_variable_is_result_temporary(codegen);
    if (strcmp(function_name, "connect") == 0 && node->data.call.argument_count == 2) {
        emit_formatted(codegen, "gray_net_dial%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (emit_passthrough_call(codegen, node, function_name, net_passthrough)) return true;
    if (strcmp(function_name, "send") == 0 && node->data.call.argument_count == 2) {
        if (is_multi_variable) {
            emit(codegen, "gray_net_send_result(gray_default_arena, ");
        } else {
            emit(codegen, "gray_net_send(");
        }
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "receive") == 0 && node->data.call.argument_count == 2) {
        emit_formatted(codegen, "gray_net_recv%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "listen") == 0 && node->data.call.argument_count == 1) {
        emit_formatted(codegen, "gray_net_listen%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "listen") == 0 && node->data.call.argument_count == 2) {
        /* Grayscale: net.listen(host, port)  →  C: gray_net_listen_host(arena, host, port) */
        emit_formatted(codegen, "gray_net_listen_host%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "accept") == 0 && node->data.call.argument_count == 1) {
        emit_formatted(codegen, "gray_net_accept%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "resolve") == 0 && node->data.call.argument_count == 1) {
        emit_formatted(codegen, "gray_net_resolve%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @encoding module --- */

static bool emit_encoding_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    /* Byte conversion functions (formerly @bytes) need address-of for array args */
    bool takes_u8_array = (strcmp(function_name, "to_string") == 0 || strcmp(function_name, "to_hex") == 0 ||
        strcmp(function_name, "to_base64") == 0);
    emit_formatted(codegen, "gray_encoding_%s(gray_default_arena, ", function_name);
    if (takes_u8_array) {
        emit_address_of(codegen, node->data.call.arguments[0]);
    } else {
        emit_expression(codegen, node->data.call.arguments[0]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @crypto module --- */

static bool emit_crypto_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    /* crc32 / entropy / constant_time_equal take no arena and return a scalar. */
    bool no_arena = strcmp(function_name, "crc32") == 0 || strcmp(function_name, "entropy") == 0 ||
                    strcmp(function_name, "constant_time_equal") == 0;
    emit_formatted(codegen, "gray_crypto_%s(", function_name);
    if (!no_arena) emit(codegen, "gray_default_arena, ");
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @binary module --- */

static bool emit_binary_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_encode = (strncmp(function_name, "encode", 6) == 0);
    bool is_decode = (strncmp(function_name, "decode", 6) == 0);
    /* Append _le for default little-endian if no endian suffix present */
    bool has_endian = strstr(function_name, "_le") || strstr(function_name, "_be");
    if (has_endian || strcmp(function_name, "encode_u8") == 0 || strcmp(function_name, "decode_u8") == 0 ||
        strcmp(function_name, "encode_i8") == 0 || strcmp(function_name, "decode_i8") == 0) {
        emit_formatted(codegen, "gray_binary_%s(", function_name);
    } else if (is_encode || is_decode) {
        emit_formatted(codegen, "gray_binary_%s_le(", function_name);
    } else {
        emit_formatted(codegen, "gray_binary_%s(", function_name);
    }
    if (is_encode) emit(codegen, "gray_default_arena, ");
    if (is_decode) {
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, ", \"%s\", %d", codegen->file, node->token.line);
    } else {
        /* encode_<T>: range-check a value narrower than it arrives, like a
         * parameter of type T. */
        char value_type[8] = "";
        if (is_encode) {
            size_t type_length = strcspn(function_name + 7, "_");
            if (type_length < sizeof(value_type)) {
                memcpy(value_type, function_name + 7, type_length);
                value_type[type_length] = '\0';
            }
        }
        if (!is_encode || !emit_narrowing_cast(codegen, value_type, node->data.call.arguments[0], node->token.line))
            emit_expression(codegen, node->data.call.arguments[0]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @csv module --- */

static const PassthroughCall csv_passthrough[] = {
    {"detect_delimiter", 1, "gray_csv_detect_delimiter"},
    {NULL, 0, NULL},
};

static bool emit_csv_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_multi_variable = current_variable_is_result_temporary(codegen);
    if (strcmp(function_name, "parse") == 0) {
        emit(codegen, "gray_csv_parse(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "read_file") == 0) {
        emit_formatted(codegen, "gray_csv_read%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "encode") == 0) {
        emit(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, "; gray_csv_stringify(gray_default_arena, &_csv_a); })");
        return true;
    }
    if (strcmp(function_name, "headers") == 0) {
        emit(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, "; gray_csv_headers(gray_default_arena, &_csv_a); })");
        return true;
    }
    if (strcmp(function_name, "parse_delimited") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_csv_parse_delimited(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (emit_passthrough_call(codegen, node, function_name, csv_passthrough)) return true;
    /* Single-array-arg record views: to_maps / from_maps / to_json / to_markdown */
    if ((strcmp(function_name, "to_maps") == 0 || strcmp(function_name, "from_maps") == 0 ||
         strcmp(function_name, "to_json") == 0 || strcmp(function_name, "to_markdown") == 0) &&
        node->data.call.argument_count == 1) {
        emit_formatted(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; gray_csv_%s(gray_default_arena, &_csv_a); })", function_name);
        return true;
    }
    /* (array, string) record views: column / sort_by_column */
    if ((strcmp(function_name, "column") == 0 || strcmp(function_name, "sort_by_column") == 0) &&
        node->data.call.argument_count == 2) {
        emit_formatted(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; gray_csv_%s(gray_default_arena, &_csv_a, ", function_name);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "); })");
        return true;
    }
    if (strcmp(function_name, "select") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "({ GrayArray _csv_a = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, "; GrayArray _csv_n = ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "; gray_csv_select(gray_default_arena, &_csv_a, &_csv_n); })");
        return true;
    }
    if (strcmp(function_name, "filter_rows") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "({ GrayArray _cf_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, "; bool (*_cf_fn)(GrayArray) = (void *)");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "; GrayArray _cf_res = GRAY_ARRAY_NEW_OF(gray_default_arena, GrayArray, _cf_src.len); ");
        emit(codegen, "for (int32_t _cf_i = 0; _cf_i < _cf_src.len; _cf_i++) { ");
        emit(codegen, "GrayArray _cf_row = ((GrayArray *)_cf_src.data)[_cf_i]; ");
        emit(codegen, "if (_cf_i == 0 || _cf_fn(_cf_row)) { GRAY_ARRAY_PUSH(gray_default_arena, &_cf_res, &_cf_row); } } _cf_res; })");
        return true;
    }
    if (strcmp(function_name, "write_file") == 0) {
        if (is_multi_variable) {
            emit(codegen, "({ GrayArray _csv_a = ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, "; gray_csv_write_result(gray_default_arena, ");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", &_csv_a); })");
        } else {
            emit(codegen, "({ GrayArray _csv_a = ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, "; gray_csv_write(gray_default_arena, ");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", &_csv_a); })");
        }
        return true;
    }
    return false;
}

/* --- @json module --- */

/* Which json.encode container helper a primitive element/value type maps to:
 * 's' signed (i8..i64, char), 'u' unsigned (u8..u64), 'f' floating-point (f32, f64),
 * 'b' bool, 'S' string, 0 other.
 * The container encoders read each slot at its real width. */
static char json_prim_class(const char *type_name) {
    if (!type_name) return 0;
    if (strcmp(type_name, "string") == 0) return 'S';
    if (strcmp(type_name, "bool") == 0) return 'b';
    if (is_wide_integer_type_name(type_name)) return 0;
    GrayType *type = type_from_name(type_name);
    if (!type) return 0;
    switch (type->kind) {
    case TYPE_KIND_FLOATING_POINT: return 'f';
    case TYPE_KIND_CHAR:  return 's';
    case TYPE_KIND_SIGNED_INTEGER:   return 's';
    case TYPE_KIND_UNSIGNED_INTEGER:  return 'u';
    default:       return 0;
    }
}

static bool emit_json_call(CodeGen *codegen, AstNode *node, const char *function_name_text) {
    if (strcmp(function_name_text, "encode") == 0) {
        AstNode *argument = node->data.call.arguments[0];
        GrayType *argument_type = type_table_get(codegen->type_table, argument);
        if (argument_type && argument_type->kind == TYPE_KIND_MAP) {
            const char *function_name = "gray_json_encode_map";
            switch (json_prim_class(argument_type->value_type)) {
            case 's': function_name = "gray_json_encode_map_int"; break;
            case 'u': function_name = "gray_json_encode_map_uint"; break;
            case 'f': function_name = "gray_json_encode_map_float"; break;
            case 'b': function_name = "gray_json_encode_map_bool"; break;
            default:  function_name = "gray_json_encode_map"; break; /* string */
            }
            emit(codegen, "({ GrayMap _jm = ");
            emit_expression(codegen, argument);
            emit_formatted(codegen, "; %s(gray_default_arena, &_jm); })", function_name);
        } else if (argument_type && argument_type->kind == TYPE_KIND_ARRAY) {
            const char *function_name = "gray_json_encode_array_int";
            switch (json_prim_class(argument_type->element_type)) {
            case 's': function_name = "gray_json_encode_array_int"; break;
            case 'u': function_name = "gray_json_encode_array_uint"; break;
            case 'f': function_name = "gray_json_encode_array_float"; break;
            case 'b': function_name = "gray_json_encode_array_bool"; break;
            case 'S': function_name = "gray_json_encode_array_string"; break;
            default:  function_name = "gray_json_encode_array_int"; break;
            }
            emit(codegen, "({ GrayArray _ja = ");
            emit_expression(codegen, argument);
            emit_formatted(codegen, "; %s(gray_default_arena, &_ja); })", function_name);
        } else if (argument_type && (argument_type->kind == TYPE_KIND_SIGNED_INTEGER || argument_type->kind == TYPE_KIND_CHAR)) {
            emit(codegen, "({ char _jbuf[32]; snprintf(_jbuf, sizeof(_jbuf), \"%\" PRId64, (int64_t)");
            emit_expression(codegen, argument);
            emit(codegen, "); gray_string_new(gray_default_arena, _jbuf, (int32_t)strlen(_jbuf)); })");
        } else if (argument_type && argument_type->kind == TYPE_KIND_UNSIGNED_INTEGER) {
            emit(codegen, "({ char _jbuf[32]; snprintf(_jbuf, sizeof(_jbuf), \"%\" PRIu64, (uint64_t)");
            emit_expression(codegen, argument);
            emit(codegen, "); gray_string_new(gray_default_arena, _jbuf, (int32_t)strlen(_jbuf)); })");
        } else if (argument_type && argument_type->kind == TYPE_KIND_FLOATING_POINT) {
            emit(codegen, "({ char _jbuf[64]; snprintf(_jbuf, sizeof(_jbuf), \"%g\", (double)");
            emit_expression(codegen, argument);
            emit(codegen, "); gray_string_new(gray_default_arena, _jbuf, (int32_t)strlen(_jbuf)); })");
        } else if (argument_type && argument_type->kind == TYPE_KIND_BOOL) {
            emit(codegen, "(");
            emit_expression(codegen, argument);
            emit(codegen, " ? gray_string_lit(\"true\") : gray_string_lit(\"false\"))");
        } else {
            /* String (and the only remaining case the typechecker allows). */
            emit(codegen, "gray_json_encode_string(gray_default_arena, ");
            emit_expression(codegen, argument);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(function_name_text, "decode") == 0) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        emit_formatted(codegen, "gray_json_decode%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    /* json.parse(); dispatch to per-struct helper when the
     * call node's typetable entry is a #json struct (pushed by the
     * var_decl handler via ). Falls back to gray_json_decode for
     * the map-based path. */
    if (strcmp(function_name_text, "parse") == 0 && node->data.call.argument_count >= 1) {
        GrayType *target_type = type_table_get(codegen->type_table, node);
        if (target_type && target_type->kind == TYPE_KIND_STRUCT && target_type->name) {
            AstNode *struct_declaration = find_struct_declaration(codegen, target_type->name);
            if (struct_declaration && struct_declaration->data.struct_declaration.is_json) {
                emit_formatted(codegen, "gray_json_parse_%s(gray_default_arena, ", target_type->name);
                emit_expression(codegen, node->data.call.arguments[0]);
                emit(codegen, ")");
                return true;
            }
        }
        /* Array of #json structs: [StructName] */
        if (target_type && target_type->kind == TYPE_KIND_ARRAY && target_type->element_type) {
            AstNode *struct_declaration = find_struct_declaration(codegen, target_type->element_type);
            if (struct_declaration && struct_declaration->data.struct_declaration.is_json) {
                emit_formatted(codegen, "gray_json_parse_array_%s(gray_default_arena, ", target_type->element_type);
                emit_expression(codegen, node->data.call.arguments[0]);
                emit(codegen, ")");
                return true;
            }
        }
        /* Fallback: map-based decode */
        emit(codegen, "gray_json_decode(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    /* json.stringify(); dispatch to per-struct helper when
     * the argument is a #json struct. */
    if (strcmp(function_name_text, "stringify") == 0 && node->data.call.argument_count >= 1) {
        AstNode *argument = node->data.call.arguments[0];
        GrayType *argument_type = type_table_get(codegen->type_table, argument);
        if (argument_type && argument_type->kind == TYPE_KIND_STRUCT && argument_type->name) {
            AstNode *struct_declaration = find_struct_declaration(codegen, argument_type->name);
            if (struct_declaration && struct_declaration->data.struct_declaration.is_json) {
                emit_formatted(codegen, "gray_json_stringify_%s(gray_default_arena, ", argument_type->name);
                emit_expression(codegen, argument);
                emit(codegen, ")");
                return true;
            }
        }
        /* Array of #json structs: [StructName]. Without this, an array
         * argument fell straight to the map fallback below, which
         * reinterprets the GrayArray's raw memory as a GrayMap and
         * segfaults reading its (nonexistent) key/value metadata. */
        if (argument_type && argument_type->kind == TYPE_KIND_ARRAY && argument_type->element_type) {
            AstNode *struct_declaration = find_struct_declaration(codegen, argument_type->element_type);
            if (struct_declaration && struct_declaration->data.struct_declaration.is_json) {
                emit_formatted(codegen, "gray_json_stringify_array_%s(gray_default_arena, ", argument_type->element_type);
                emit_expression(codegen, argument);
                emit(codegen, ")");
                return true;
            }
        }
        /* Fallback: encode as map */
        emit(codegen, "({ __auto_type _jtmp = ");
        emit_expression(codegen, argument);
        emit(codegen, "; gray_json_encode_map(gray_default_arena, (GrayMap *)&_jtmp); })");
        return true;
    }
    if (strcmp(function_name_text, "is_valid") == 0) {
        emit(codegen, "gray_json_is_valid(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name_text, "pretty_print") == 0) {
        emit(codegen, "gray_json_pretty_map(gray_default_arena, &");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @sqlite module --- */

static bool emit_sqlite_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_fallible = (strcmp(function_name, "open") == 0 || strcmp(function_name, "exec") == 0 ||
        strcmp(function_name, "query") == 0 || strcmp(function_name, "exec_params") == 0 ||
        strcmp(function_name, "query_params") == 0);
    bool is_multi_variable = current_variable_is_result_temporary(codegen);
    if (strcmp(function_name, "open") == 0) {
        emit_formatted(codegen, "gray_sqlite_open%s(gray_default_arena, ", (is_fallible && is_multi_variable) ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "close") == 0) {
        emit(codegen, "gray_sqlite_close(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "exec") == 0) {
        if (is_multi_variable) {
            emit(codegen, "gray_sqlite_exec_result(gray_default_arena, ");
        } else {
            emit(codegen, "gray_sqlite_exec(");
        }
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "exec_params") == 0) {
        if (is_multi_variable) {
            emit(codegen, "gray_sqlite_exec_params_result(gray_default_arena, ");
        } else {
            emit(codegen, "gray_sqlite_exec_params(");
        }
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "query") == 0) {
        emit_formatted(codegen, "gray_sqlite_query%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "query_params") == 0) {
        emit_formatted(codegen, "gray_sqlite_query_params%s(gray_default_arena, ", is_multi_variable ? "_result" : "");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @random module --- */

static const PassthroughCall random_passthrough[] = {
    {"seed", 1, "gray_random_seed"},
    {NULL, 0, NULL},
};

static bool emit_random_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "rand_f64") == 0) {
        if (node->data.call.argument_count == 0) {
            emit(codegen, "gray_random_f64_unit()");
        } else if (node->data.call.argument_count == 2) {
            emit(codegen, "gray_random_f64_range(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(function_name, "rand_i64") == 0) {
        if (node->data.call.argument_count == 1) {
            emit(codegen, "gray_random_i64_max(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ")");
        } else if (node->data.call.argument_count == 2) {
            emit(codegen, "gray_random_i64_range(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(function_name, "rand_bool") == 0) { emit(codegen, "gray_random_bool()"); return true; }
    if (strcmp(function_name, "rand_u8") == 0) { emit(codegen, "gray_random_u8()"); return true; }
    if (strcmp(function_name, "rand_char") == 0) {
        if (node->data.call.argument_count == 2) {
            emit(codegen, "gray_random_char_range(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, ")");
        } else {
            emit(codegen, "gray_random_char()");
        }
        return true;
    }
    if (strcmp(function_name, "rand_string") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_random_string(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "shuffle") == 0) {
        emit(codegen, "gray_random_shuffle(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "sample") == 0) {
        emit(codegen, "gray_random_sample(gray_default_arena, ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "choice") == 0) {
        /* Determine element C type from the array's type info */
        const char *c_element_type = "int64_t";
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        if (array_type && array_type->kind == TYPE_KIND_ARRAY && array_type->element_type) {
            GrayType *element_type = type_from_name(array_type->element_type);
            if (element_type->kind == TYPE_KIND_BOOL) c_element_type = "bool";
            else if (element_type->kind == TYPE_KIND_STRING) c_element_type = "GrayString";
            else if (element_type->kind == TYPE_KIND_CHAR) c_element_type = "int32_t";
            else if ((element_type->kind == TYPE_KIND_SIGNED_INTEGER || element_type->kind == TYPE_KIND_UNSIGNED_INTEGER || element_type->kind == TYPE_KIND_FLOATING_POINT) &&
                     !is_wide_integer_type_name(array_type->element_type))
                c_element_type = gray_type_to_c_codegen(codegen, array_type->element_type);
            else if (element_type->kind == TYPE_KIND_STRUCT) c_element_type = gray_type_to_c_codegen(codegen, array_type->element_type);
            else if (element_type->kind == TYPE_KIND_ENUM) {
                c_element_type = codegen_enum_is_string(codegen, array_type->element_type)
                    ? "GrayString" : gray_type_to_c_codegen(codegen, array_type->element_type);
            }
        }
        if (expression_is_assignable(node->data.call.arguments[0])) {
            emit(codegen, "({ int32_t _ri = gray_random_i64_max(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit_formatted(codegen, ".len); *(%s *)gray_array_get_ptr(&", c_element_type);
            emit_expression(codegen, node->data.call.arguments[0]);
            emit_formatted(codegen, ", _ri, \"%s\", %d); })", codegen->file, node->token.line);
        } else {
            emit(codegen, "({ __auto_type _ra = ");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit_formatted(codegen, "; int32_t _ri = gray_random_i64_max(_ra.len); *(%s *)gray_array_get_ptr(&_ra, _ri, \"%s\", %d); })", c_element_type, codegen->file, node->token.line);
        }
        return true;
    }
    if (emit_passthrough_call(codegen, node, function_name, random_passthrough)) return true;
    return false;
}

/* --- @arrays module --- */

/* Element type name of the array-typed expression `node`, or NULL when its
 * type is unknown or not an array. */
static const char *codegen_array_element_type(CodeGen *codegen, AstNode *node) {
    GrayType *type = type_table_get(codegen->type_table, node);
    return (type && type->kind == TYPE_KIND_ARRAY) ? type->element_type : NULL;
}

/* Emit &arr for arrays.append/prepend/insert_at. Identical to
 * emit_address_of — kept as a name that reads at the call sites. Keeping a
 * second implementation is what let the two drift apart, leaving pointer
 * shapes handled for arrays but not for maps. */
static void emit_array_argument_address(CodeGen *codegen, AstNode *argument) {
    emit_address_of(codegen, argument);
}

/* C element type for a value staged by arrays.append/insert_at. Falls back to
 * __auto_type when the value's type is unknown. */
static const char *array_value_c_type(CodeGen *codegen, GrayType *value_type) {
    if (!value_type) return "__auto_type";
    switch (value_type->kind) {
    case TYPE_KIND_SIGNED_INTEGER:
    case TYPE_KIND_UNSIGNED_INTEGER:
        /* Wide integers share TYPE_KIND_SIGNED_INTEGER/TYPE_KIND_UNSIGNED_INTEGER but need their own C types */
        if (value_type->name) {
            const char *mapped = gray_type_to_c_codegen(codegen, value_type->name);
            if (mapped) return mapped;
        }
        return value_type->kind == TYPE_KIND_SIGNED_INTEGER ? "int64_t" : "uint64_t";
    case TYPE_KIND_FLOATING_POINT: return "double";
    case TYPE_KIND_BOOL: return "bool";
    case TYPE_KIND_CHAR: return "int32_t";
    case TYPE_KIND_STRING: return "GrayString";
    case TYPE_KIND_ARRAY: return "GrayArray";
    case TYPE_KIND_MAP: return "GrayMap";
    case TYPE_KIND_FUNCTION: return "void *";
    case TYPE_KIND_STRUCT:
    case TYPE_KIND_ENUM:
        return gray_type_to_c_codegen(codegen, value_type->name);
    case TYPE_KIND_POINTER:
        if (value_type->name) {
            /* value_type->name is the pointee (e.g. "i64"); prepend ^ for gray_type_to_c_codegen */
            static char pointer_type_name[TYPE_NAME_MAX];
            snprintf(pointer_type_name, sizeof(pointer_type_name), "^%s", value_type->name);
            return gray_type_to_c_codegen(codegen, pointer_type_name);
        }
        return "__auto_type";
    default: return "__auto_type";
    }
}

/* A value staged in the temp `temp_name` for append/insert_at/prepend may
 * live in the per-iteration arena (inside a loop) or alias the source
 * (a composite passed by name). Copy it into `arena` — a string by a plain
 * copy, an array/map/struct with embedded pointers by a deep copy. */
static void emit_escape_staged_value(CodeGen *codegen, AstNode *value_argument, const char *element_type_name,
                                     bool is_string, const char *temporary_name, const char *arena) {
    if (codegen->loop_scope_depth == 0 && !composite_value_aliases(codegen, element_type_name, value_argument))
        return;
    if (is_string) {
        emit_formatted(codegen, "%s = gray_string_new(%s, %s.data, %s.len); ",
            temporary_name, arena, temporary_name, temporary_name);
    } else if (element_type_name && type_needs_deep_copy(codegen, element_type_name)) {
        emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; %s = ",
            arena, temporary_name);
        emit_value_deep_copy(codegen, element_type_name, temporary_name);
        emit(codegen, "; gray_default_arena = _esc; } ");
    }
}


/* Emit `fn(&array, &value)`, the value staged as the array's element C type
 * so the runtime reads it as the array's elem_kind says. */
static void emit_arrays_value_call(CodeGen *codegen, const char *function_name, AstNode *array_argument,
                                   AstNode *value_argument) {
    const char *element_type_name = codegen_array_element_type(codegen, array_argument);
    char c_element_type[MESSAGE_BUFFER_SIZE];
    snprintf(c_element_type, sizeof(c_element_type), "%s", gray_type_to_c_codegen(codegen, element_type_name ? element_type_name : "i64"));
    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen, "({ %s _av%d = ", c_element_type, unique_id);
    if (!emit_wide_integer_coerced(codegen, element_type_name, value_argument))
        emit_expression(codegen, value_argument);
    emit_formatted(codegen, "; %s(", function_name);
    emit_array_argument_address(codegen, array_argument);
    emit_formatted(codegen, ", &_av%d); })", unique_id);
}

/* Emit `fn(&array, &result[, file, line])` for a function that writes an
 * element-typed result, yielding the result. */
static void emit_arrays_out_call(CodeGen *codegen, const char *function_name, AstNode *array_argument,
                                 bool with_location, AstNode *location_node) {
    const char *element_type_name = codegen_array_element_type(codegen, array_argument);
    char c_element_type[MESSAGE_BUFFER_SIZE];
    snprintf(c_element_type, sizeof(c_element_type), "%s", gray_type_to_c_codegen(codegen, element_type_name ? element_type_name : "i64"));
    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen, "({ %s _ar%d; %s(", c_element_type, unique_id, function_name);
    emit_array_argument_address(codegen, array_argument);
    emit_formatted(codegen, ", &_ar%d", unique_id);
    if (with_location) emit_formatted(codegen, ", \"%s\", %d", codegen->file, location_node->token.line);
    emit_formatted(codegen, "); _ar%d; })", unique_id);
}

static bool emit_arrays_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "append") == 0 && node->data.call.argument_count == 2) {
        GrayType *value_type = type_table_get(codegen->type_table, node->data.call.arguments[1]);
        const char *element_type_name = codegen_array_element_type(codegen, node->data.call.arguments[0]);
        bool is_element_string = (value_type && value_type->kind == TYPE_KIND_STRING) ||
            (element_type_name && strcmp(element_type_name, "string") == 0);
        const char *c_element_type = "__auto_type";
        /* An inline tagged-enum constructor at the call site is left as
         * TYPE_KIND_UNKNOWN in the type table; fall back to the array's declared
         * element type so the temporary is not declared as `__auto_type` and
         * then spliced into `sizeof(__auto_type)`. */
        if (value_type && value_type->kind != TYPE_KIND_UNKNOWN) {
            c_element_type = array_value_c_type(codegen, value_type);
        } else if (is_element_string) {
            c_element_type = "GrayString";
        } else if (element_type_name) {
            GrayType *element_type = type_from_name(element_type_name);
            if (element_type->kind == TYPE_KIND_ARRAY) c_element_type = "GrayArray";
            else if (element_type->kind == TYPE_KIND_MAP) c_element_type = "GrayMap";
            else if (element_type->kind == TYPE_KIND_STRUCT) c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
            else if (element_type->kind == TYPE_KIND_ENUM) c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
            else if (element_type->kind == TYPE_KIND_FUNCTION) c_element_type = "void *";
            else if (element_type->kind == TYPE_KIND_POINTER) c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        }
        /* [f32] elements are stored packed as 4-byte float; a floating-point
         * value would otherwise be staged and appended as an 8-byte double. */
        if (element_type_name && strcmp(element_type_name, "f32") == 0) c_element_type = "float";
        const char *allocation_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
        emit_formatted(codegen, "{ %s _av = ", c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "; ");
        emit_escape_staged_value(codegen, node->data.call.arguments[1], element_type_name, is_element_string, "_av", allocation_arena);
        /* Ensure elem_size and elem_kind are set on the target array before
         * appending; struct fields may be zero-initialized with neither. */
        emit(codegen, "{ GrayArray *_tgt = ");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        if (strcmp(c_element_type, "__auto_type") == 0)
            emit(codegen, "; if (_tgt->elem_size == 0) _tgt->elem_size = sizeof(_av); ");
        else
            emit_formatted(codegen, "; if (_tgt->elem_size == 0) { _tgt->elem_size = sizeof(%s); "
                "_tgt->elem_kind = GRAY_ELEM_KIND_OF(%s); } ", c_element_type, c_element_type);
        emit_formatted(codegen, "gray_arrays_append(%s, _tgt, &_av); } }", allocation_arena);
        return true;
    }
    if (strcmp(function_name, "insert_at") == 0 && node->data.call.argument_count == 3) {
        GrayType *value_type = type_table_get(codegen->type_table, node->data.call.arguments[2]);
        const char *c_element_type = array_value_c_type(codegen, value_type);
        const char *insert_element_type_name = codegen_array_element_type(codegen, node->data.call.arguments[0]);
        /* [f32] elements are stored packed as 4-byte float. */
        if (insert_element_type_name && strcmp(insert_element_type_name, "f32") == 0) c_element_type = "float";
        const char *insert_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
        emit_formatted(codegen, "{ %s _iv = ", c_element_type);
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, "; ");
        bool is_insert_string = (value_type && value_type->kind == TYPE_KIND_STRING) ||
            (insert_element_type_name && strcmp(insert_element_type_name, "string") == 0);
        emit_escape_staged_value(codegen, node->data.call.arguments[2], insert_element_type_name, is_insert_string, "_iv", insert_arena);
        emit_formatted(codegen, "gray_arrays_insert_at(%s, ", insert_arena);
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", &_iv); }");
        return true;
    }
    if (strcmp(function_name, "remove_at") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_arrays_remove_at(");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "remove") == 0 && node->data.call.argument_count == 2) {
        emit_arrays_value_call(codegen, "gray_arrays_remove", node->data.call.arguments[0], node->data.call.arguments[1]);
        return true;
    }
    if (strcmp(function_name, "clear") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_arrays_clear(");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if ((strcmp(function_name, "sort_asc") == 0 || strcmp(function_name, "sort_desc") == 0) &&
        node->data.call.argument_count == 1) {
        emit(codegen, "gray_arrays_sort(");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, ", %s)", strcmp(function_name, "sort_desc") == 0 ? "true" : "false");
        return true;
    }
    if (strcmp(function_name, "is_empty") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_arrays_is_empty(");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if ((strcmp(function_name, "contains") == 0 || strcmp(function_name, "index_of") == 0 ||
         strcmp(function_name, "count") == 0) && node->data.call.argument_count == 2) {
        char function_name_buffer[MESSAGE_BUFFER_SIZE];
        snprintf(function_name_buffer, sizeof(function_name_buffer), "gray_arrays_%s", function_name);
        emit_arrays_value_call(codegen, function_name_buffer, node->data.call.arguments[0], node->data.call.arguments[1]);
        return true;
    }
    if (strcmp(function_name, "is_equal") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_arrays_is_equal(");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_array_argument_address(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    /* prepend/fill need special value wrapping */
    if (strcmp(function_name, "prepend") == 0 && node->data.call.argument_count == 2) {
        const char *prepend_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
        const char *prepend_element_type_name = codegen_array_element_type(codegen, node->data.call.arguments[0]);
        bool is_prepend_string = prepend_element_type_name && strcmp(prepend_element_type_name, "string") == 0;
        const char *prepend_c_element_type = "int64_t";
        if (prepend_element_type_name) {
            GrayType *prepend_element_type = type_from_name(prepend_element_type_name);
            if (prepend_element_type->kind == TYPE_KIND_FLOATING_POINT) prepend_c_element_type = "double";
            else if (prepend_element_type->kind == TYPE_KIND_BOOL) prepend_c_element_type = "bool";
            else if (prepend_element_type->kind == TYPE_KIND_CHAR) prepend_c_element_type = "int32_t";
            else if (prepend_element_type->kind == TYPE_KIND_STRING) prepend_c_element_type = "GrayString";
            else if (prepend_element_type->kind == TYPE_KIND_ARRAY) prepend_c_element_type = "GrayArray";
            else if (prepend_element_type->kind == TYPE_KIND_MAP) prepend_c_element_type = "GrayMap";
            else if (prepend_element_type->kind == TYPE_KIND_STRUCT) prepend_c_element_type = gray_type_to_c_codegen(codegen, prepend_element_type_name);
            else if (prepend_element_type->kind == TYPE_KIND_ENUM) prepend_c_element_type = gray_type_to_c_codegen(codegen, prepend_element_type_name);
            else if (prepend_element_type->kind == TYPE_KIND_SIGNED_INTEGER || prepend_element_type->kind == TYPE_KIND_UNSIGNED_INTEGER)
                prepend_c_element_type = gray_type_to_c_codegen(codegen, prepend_element_type_name);
            /* [f32] elements are stored packed as 4-byte float. */
            if (strcmp(prepend_element_type_name, "f32") == 0) prepend_c_element_type = "float";
        }
        emit_formatted(codegen, "{ %s _pv = ", prepend_c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "; ");
        emit_escape_staged_value(codegen, node->data.call.arguments[1], prepend_element_type_name, is_prepend_string, "_pv", prepend_arena);
        emit_formatted(codegen, "gray_arrays_prepend(%s, ", prepend_arena);
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &_pv); }");
        return true;
    }
    if (strcmp(function_name, "fill") == 0 && node->data.call.argument_count == 3) {
        GrayType *flatten_array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *flatten_c_element_type = "int64_t";
        if (flatten_array_type && flatten_array_type->kind == TYPE_KIND_ARRAY && flatten_array_type->element_type) {
            GrayType *first_element_type = type_from_name(flatten_array_type->element_type);
            if (first_element_type->kind == TYPE_KIND_BOOL) flatten_c_element_type = "bool";
            else if (first_element_type->kind == TYPE_KIND_CHAR) flatten_c_element_type = "int32_t";
            else if (first_element_type->kind == TYPE_KIND_STRING) flatten_c_element_type = "GrayString";
            else if (first_element_type->kind == TYPE_KIND_ARRAY) flatten_c_element_type = "GrayArray";
            else if (first_element_type->kind == TYPE_KIND_MAP) flatten_c_element_type = "GrayMap";
            else if (first_element_type->kind == TYPE_KIND_STRUCT) flatten_c_element_type = gray_type_to_c_codegen(codegen, flatten_array_type->element_type);
            else if (first_element_type->kind == TYPE_KIND_ENUM) flatten_c_element_type = gray_type_to_c_codegen(codegen, flatten_array_type->element_type);
            else if (first_element_type->kind == TYPE_KIND_SIGNED_INTEGER || first_element_type->kind == TYPE_KIND_UNSIGNED_INTEGER || first_element_type->kind == TYPE_KIND_FLOATING_POINT)
                flatten_c_element_type = gray_type_to_c_codegen(codegen, flatten_array_type->element_type);
        }
        const char *flatten_element_type_name = (flatten_array_type && flatten_array_type->kind == TYPE_KIND_ARRAY) ? flatten_array_type->element_type : NULL;
        emit_formatted(codegen, "{ %s _fv = ", flatten_c_element_type);
        if (!emit_wide_integer_coerced(codegen, flatten_element_type_name, node->data.call.arguments[1]))
            emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "; gray_arrays_fill(gray_default_arena, ");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ", &_fv, ");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, "); }");
        return true;
    }
    if ((strcmp(function_name, "get_first") == 0 || strcmp(function_name, "get_last") == 0 ||
         strcmp(function_name, "remove_last") == 0 || strcmp(function_name, "remove_first") == 0) &&
        node->data.call.argument_count == 1) {
        const char *append_function_element = codegen_array_element_type(codegen, node->data.call.arguments[0]);
        const char *append_function_c_type = append_function_element ? gray_type_to_c_codegen(codegen, append_function_element) : "int64_t";
        bool is_get_function = (strcmp(function_name, "get_first") == 0 || strcmp(function_name, "get_last") == 0);
        const char *append_function_pointer_function = (strcmp(function_name, "get_first") == 0 || strcmp(function_name, "remove_first") == 0)
                                ? "gray_arrays_first_ptr" : "gray_arrays_last_ptr";
        const char *append_function_raw_function = (strcmp(function_name, "remove_first") == 0)
                                ? "gray_arrays_remove_first_raw" : "gray_arrays_remove_last_raw";
        if (is_get_function && append_function_element && type_shares_storage(codegen, append_function_element)) {
            /* The element is a composite: hand back a copy, not a view of the
             * array's own slot. */
            int unique_id = codegen_next_id(codegen);
            emit_formatted(codegen, "({ %s _gf%d = *(%s *)%s(", append_function_c_type, unique_id, append_function_c_type, append_function_pointer_function);
            emit_array_argument_address(codegen, node->data.call.arguments[0]);
            emit(codegen, "); ");
            char source_variable[SHORT_VARIABLE_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_gf%d", unique_id);
            emit_value_deep_copy(codegen, append_function_element, source_variable);
            emit(codegen, "; })");
        } else if (is_get_function) {
            emit_formatted(codegen, "(*(%s *)%s(", append_function_c_type, append_function_pointer_function);
            emit_array_argument_address(codegen, node->data.call.arguments[0]);
            emit(codegen, "))");
        } else {
            emit_formatted(codegen, "({ %s _rafv; %s(", append_function_c_type, append_function_raw_function);
            emit_array_argument_address(codegen, node->data.call.arguments[0]);
            emit(codegen, ", &_rafv); _rafv; })");
        }
        return true;
    }

    /* --- map / filter / reduce: inline loop emission --- */
    if (strcmp(function_name, "map") == 0 && node->data.call.argument_count == 2) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _m_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; %s (*_m_fn)(%s) = (void *)", c_element_type, c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, "; GrayArray _m_res = GRAY_ARRAY_NEW_OF(gray_default_arena, %s, _m_src.len);", c_element_type);
        emit_formatted(codegen, "for (int32_t _m_i = 0; _m_i < _m_src.len; _m_i++) { ");
        emit_formatted(codegen, "%s _m_v = _m_fn(((%s *)_m_src.data)[_m_i]); ", c_element_type, c_element_type);
        emit_formatted(codegen, "gray_arrays_append(gray_default_arena, &_m_res, &_m_v); } _m_res; })");
        return true;
    }
    if (strcmp(function_name, "filter") == 0 && node->data.call.argument_count == 2) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _f_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; bool (*_f_fn)(%s) = (void *)", c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, "; GrayArray _f_res = GRAY_ARRAY_NEW_OF(gray_default_arena, %s, _f_src.len);", c_element_type);
        emit_formatted(codegen, "for (int32_t _f_i = 0; _f_i < _f_src.len; _f_i++) { ");
        emit_formatted(codegen, "%s _f_v = ((%s *)_f_src.data)[_f_i]; ", c_element_type, c_element_type);
        emit_formatted(codegen, "if (_f_fn(_f_v)) { gray_arrays_append(gray_default_arena, &_f_res, &_f_v); } } _f_res; })");
        return true;
    }
    if (strcmp(function_name, "any") == 0 && node->data.call.argument_count == 2) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _a_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; bool (*_a_fn)(%s) = (void *)", c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, "; bool _a_res = false; ");
        emit_formatted(codegen, "for (int32_t _a_i = 0; _a_i < _a_src.len; _a_i++) { ");
        emit_formatted(codegen, "if (_a_fn(((%s *)_a_src.data)[_a_i])) { _a_res = true; break; } } _a_res; })", c_element_type);
        return true;
    }
    if (strcmp(function_name, "all") == 0 && node->data.call.argument_count == 2) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _l_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; bool (*_l_fn)(%s) = (void *)", c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, "; bool _l_res = true; ");
        emit_formatted(codegen, "for (int32_t _l_i = 0; _l_i < _l_src.len; _l_i++) { ");
        emit_formatted(codegen, "if (!_l_fn(((%s *)_l_src.data)[_l_i])) { _l_res = false; break; } } _l_res; })", c_element_type);
        return true;
    }
    if (strcmp(function_name, "reduce") == 0 && node->data.call.argument_count == 3) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _r_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; %s _r_acc = ", c_element_type);
        if (!emit_wide_integer_coerced(codegen, element_type_name, node->data.call.arguments[1]))
            emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, "; %s (*_r_fn)(%s, %s) = (void *)", c_element_type, c_element_type, c_element_type);
        emit_expression(codegen, node->data.call.arguments[2]);
        emit_formatted(codegen, "; for (int32_t _r_i = 0; _r_i < _r_src.len; _r_i++) { ");
        emit_formatted(codegen, "_r_acc = _r_fn(_r_acc, ((%s *)_r_src.data)[_r_i]); } _r_acc; })", c_element_type);
        return true;
    }
    if (strcmp(function_name, "find_index") == 0 && node->data.call.argument_count == 2) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _fi_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; bool (*_fi_fn)(%s) = (void *)", c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, "; int64_t _fi_res = -1; ");
        emit_formatted(codegen, "for (int32_t _fi_i = 0; _fi_i < _fi_src.len; _fi_i++) { ");
        emit_formatted(codegen, "if (_fi_fn(((%s *)_fi_src.data)[_fi_i])) { _fi_res = _fi_i; break; } } _fi_res; })", c_element_type);
        return true;
    }
    if (strcmp(function_name, "find") == 0 && node->data.call.argument_count == 2) {
        GrayType *array_type = type_table_get(codegen->type_table, node->data.call.arguments[0]);
        const char *element_type_name = (array_type && array_type->kind == TYPE_KIND_ARRAY) ? array_type->element_type : "i64";
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        emit(codegen, "({ GrayArray _fd_src = ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, "; bool (*_fd_fn)(%s) = (void *)", c_element_type);
        emit_expression(codegen, node->data.call.arguments[1]);
        emit_formatted(codegen, "; %s _fd_v0 = (%s){0}; bool _fd_ok = false; ", c_element_type, c_element_type);
        emit_formatted(codegen, "for (int32_t _fd_i = 0; _fd_i < _fd_src.len; _fd_i++) { ");
        emit_formatted(codegen, "%s _fd_e = ((%s *)_fd_src.data)[_fd_i]; ", c_element_type, c_element_type);
        emit(codegen, "if (_fd_fn(_fd_e)) { _fd_v0 = _fd_e; _fd_ok = true; break; } } ");
        emit_formatted(codegen, "(struct { %s v0; bool v1; }){ _fd_v0, _fd_ok }; })", c_element_type);
        return true;
    }
    if (strcmp(function_name, "average") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_arrays_average(");
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        return true;
    }
    /* The sum, smallest and largest are the element type. */
    if ((strcmp(function_name, "get_sum") == 0 || strcmp(function_name, "get_min") == 0 ||
         strcmp(function_name, "get_max") == 0) && node->data.call.argument_count == 1) {
        char function_name_buffer[MESSAGE_BUFFER_SIZE];
        snprintf(function_name_buffer, sizeof(function_name_buffer), "gray_arrays_%s", function_name);
        emit_arrays_out_call(codegen, function_name_buffer, node->data.call.arguments[0],
            strcmp(function_name, "get_sum") == 0, node);
        return true;
    }
    if ((strcmp(function_name, "is_sorted") == 0 || strcmp(function_name, "min_index") == 0 ||
         strcmp(function_name, "max_index") == 0) && node->data.call.argument_count == 1) {
        emit_formatted(codegen, "gray_arrays_%s(", function_name);
        emit_array_argument_address(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "binary_search") == 0 && node->data.call.argument_count == 2) {
        emit_arrays_value_call(codegen, "gray_arrays_binary_search", node->data.call.arguments[0],
            node->data.call.arguments[1]);
        return true;
    }

    /* Generic: arrays.func(&arr, ...) or arrays.func(arena, &arr, ...) */
    bool needs_arena = (strcmp(function_name, "reverse") == 0 || strcmp(function_name, "slice") == 0 ||
        strcmp(function_name, "concat") == 0 || strcmp(function_name, "deduplicate") == 0 ||
        strcmp(function_name, "flatten") == 0 || strcmp(function_name, "split_every") == 0 ||
        strcmp(function_name, "pair") == 0 || strcmp(function_name, "rotate") == 0);
    bool has_reference_arguments = (strcmp(function_name, "concat") == 0 || strcmp(function_name, "pair") == 0);
    emit_formatted(codegen, "gray_arrays_%s(", function_name);
    if (needs_arena) emit(codegen, "gray_default_arena, ");
    emit_array_argument_address(codegen, node->data.call.arguments[0]);
    for (int i = 1; i < node->data.call.argument_count; i++) {
        emit(codegen, ", ");
        if (has_reference_arguments) {
            AstNode *range_argument = node->data.call.arguments[i];
            if (range_argument->kind == NODE_LABEL || range_argument->kind == NODE_MEMBER_EXPRESSION || range_argument->kind == NODE_INDEX_EXPRESSION) {
                emit(codegen, "&");
                emit_expression(codegen, range_argument);
            } else {
                /* Rvalue: compound literal, not a statement-expression temp
                 * (whose storage dies at the closing brace — see
                 * emit_address_of). */
                emit(codegen, "(__typeof__(");
                emit_expression(codegen, range_argument);
                emit(codegen, ")[]){");
                emit_expression(codegen, range_argument);
                emit(codegen, "}");
            }
        } else {
            emit_expression(codegen, node->data.call.arguments[i]);
        }
    }
    emit(codegen, ")");
    return true;
}

/* --- @os module --- */

static bool emit_os_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "args") == 0) {
        emit(codegen, "gray_os_args(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "get_env") == 0) {
        emit(codegen, "gray_os_get_env(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "lookup_env") == 0) {
        emit(codegen, "gray_os_lookup_env(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "environ") == 0) {
        emit(codegen, "gray_os_environ(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "current_dir") == 0) {
        emit(codegen, "gray_os_cwd(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "home_dir") == 0) {
        emit(codegen, "gray_os_home_dir(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "hostname") == 0) {
        emit(codegen, "gray_os_hostname(gray_default_arena)"); return true;
    }
    if (strcmp(function_name, "current_os") == 0) { emit(codegen, "gray_os_current_os()"); return true; }
    if (strcmp(function_name, "arch") == 0) { emit(codegen, "gray_os_arch()"); return true; }
    if (strcmp(function_name, "pid") == 0) { emit(codegen, "gray_os_pid()"); return true; }
    if (strcmp(function_name, "cpu_count") == 0) { emit(codegen, "gray_os_cpu_count()"); return true; }
    if (strcmp(function_name, "is_tty") == 0) { emit(codegen, "gray_os_is_tty()"); return true; }
    if (strcmp(function_name, "set_env") == 0) {
        emit(codegen, "gray_os_set_env(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "unset_env") == 0) {
        emit(codegen, "gray_os_unset_env(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "exec") == 0) {
        emit(codegen, "gray_os_exec(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @io module --- */

static bool emit_io_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_fallible = (strcmp(function_name, "read_file") == 0 ||
        strcmp(function_name, "read_bytes") == 0 ||
        strcmp(function_name, "read_lines") == 0 ||
        strcmp(function_name, "file_size") == 0 ||
        strcmp(function_name, "write_file") == 0 ||
        strcmp(function_name, "delete_file") == 0 ||
        strcmp(function_name, "append_file") == 0 ||
        strcmp(function_name, "rename_file") == 0 ||
        strcmp(function_name, "copy_file") == 0 ||
        strcmp(function_name, "move_file") == 0 ||
        strcmp(function_name, "list_dir") == 0 ||
        strcmp(function_name, "make_dir") == 0 ||
        strcmp(function_name, "make_dir_all") == 0 ||
        strcmp(function_name, "remove_dir") == 0 ||
        strcmp(function_name, "remove_dir_all") == 0 ||
        strcmp(function_name, "walk") == 0 ||
        strcmp(function_name, "glob") == 0 ||
        strcmp(function_name, "write_bytes") == 0 ||
        strcmp(function_name, "append_bytes") == 0 ||
        strcmp(function_name, "temp_file") == 0 ||
        strcmp(function_name, "temp_dir") == 0);
    bool needs_arena = (strcmp(function_name, "read_file") == 0 ||
        strcmp(function_name, "read_bytes") == 0 ||
        strcmp(function_name, "read_lines") == 0 ||
        strcmp(function_name, "read_stdin_all") == 0 ||
        strcmp(function_name, "read_stdin_bytes") == 0 ||
        strcmp(function_name, "list_dir") == 0 ||
        strcmp(function_name, "walk") == 0 ||
        strcmp(function_name, "glob") == 0 ||
        strcmp(function_name, "temp_file") == 0 ||
        strcmp(function_name, "temp_dir") == 0 ||
        strcmp(function_name, "path_join") == 0 ||
        strcmp(function_name, "dirname") == 0 ||
        strcmp(function_name, "basename") == 0 ||
        strcmp(function_name, "extension") == 0 ||
        strcmp(function_name, "normalize") == 0);
    if (is_fallible) {
        /* Use non-result version when assigned to a single variable (typed or
         * inferred).  Use _result version only for multi-var destructuring
         * (temp vars prefixed with _gray_tmp). */
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        bool use_non_result = !is_multi_variable;
        if (use_non_result) {
            if (needs_arena) {
                emit_formatted(codegen, "gray_io_%s(gray_default_arena", function_name);
                if (node->data.call.argument_count > 0) emit(codegen, ", ");
            } else {
                emit_formatted(codegen, "gray_io_%s(", function_name);
            }
            for (int i = 0; i < node->data.call.argument_count; i++) {
                if (i > 0) emit(codegen, ", ");
                emit_expression(codegen, node->data.call.arguments[i]);
            }
            /* read_lines' optional line limit defaults to 0 (read to EOF) */
            if (strcmp(function_name, "read_lines") == 0 && node->data.call.argument_count == 1) {
                emit(codegen, ", 0");
            }
            emit(codegen, ")");
        } else {
            emit_formatted(codegen, "gray_io_%s_result(gray_default_arena", function_name);
            if (node->data.call.argument_count > 0) emit(codegen, ", ");
            for (int i = 0; i < node->data.call.argument_count; i++) {
                if (i > 0) emit(codegen, ", ");
                emit_expression(codegen, node->data.call.arguments[i]);
            }
            if (strcmp(function_name, "read_lines") == 0 && node->data.call.argument_count == 1) {
                emit(codegen, ", 0");
            }
            emit(codegen, ")");
        }
        return true;
    }
    /* Non-fallible functions */
    if (needs_arena) {
        emit_formatted(codegen, "gray_io_%s(gray_default_arena", function_name);
        if (node->data.call.argument_count > 0) emit(codegen, ", ");
    } else {
        emit_formatted(codegen, "gray_io_%s(", function_name);
    }
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @strings module --- */

static bool emit_strings_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool needs_arena = (strcmp(function_name, "to_upper") == 0 || strcmp(function_name, "to_lower") == 0 ||
        strcmp(function_name, "trim") == 0 || strcmp(function_name, "trim_left") == 0 ||
        strcmp(function_name, "trim_right") == 0 ||
        strcmp(function_name, "remove_prefix") == 0 || strcmp(function_name, "remove_suffix") == 0 ||
        strcmp(function_name, "replace") == 0 ||
        strcmp(function_name, "repeat") == 0 || strcmp(function_name, "reverse") == 0 ||
        strcmp(function_name, "slice") == 0 || strcmp(function_name, "split") == 0 ||
        strcmp(function_name, "split_whitespace") == 0 || strcmp(function_name, "split_n") == 0 ||
        strcmp(function_name, "to_title") == 0 || strcmp(function_name, "to_snake_case") == 0 ||
        strcmp(function_name, "to_camel_case") == 0 || strcmp(function_name, "to_kebab_case") == 0 ||
        strcmp(function_name, "to_pascal_case") == 0 ||
        strcmp(function_name, "to_screaming_snake_case") == 0 ||
        strcmp(function_name, "capitalize") == 0 || strcmp(function_name, "truncate") == 0 ||
        strcmp(function_name, "join") == 0 ||
        strcmp(function_name, "builder") == 0 || strcmp(function_name, "build") == 0 ||
        strcmp(function_name, "append_char") == 0 || strcmp(function_name, "prepend_char") == 0 ||
        strcmp(function_name, "insert_char_at") == 0 || strcmp(function_name, "remove_at") == 0 ||
        strcmp(function_name, "set_char_at") == 0 ||
        strcmp(function_name, "to_chars") == 0 || strcmp(function_name, "from_chars") == 0);

    emit_formatted(codegen, "gray_strings_%s(", function_name);
    bool wrote_argument = false;
    if (needs_arena) {
        emit(codegen, "gray_default_arena");
        wrote_argument = true;
    }
    if (strcmp(function_name, "from_chars") == 0) {
        if (wrote_argument) emit(codegen, ", ");
        emit_address_of(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (wrote_argument || i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @fmt module --- */

static void emit_format_body(CodeGen *codegen, AstNode *node, const char *prefix, bool newline) {
    emit(codegen, prefix);
    AstNode *format_argument = node->data.call.arguments[0];
    if (format_argument->kind == NODE_STRING_VALUE) {
        if (newline)
            emit_format_string_normalized_extended(codegen, format_argument->data.string_value.value, node, true);
        else
            emit_format_string_normalized(codegen, format_argument->data.string_value.value, node);
    } else {
        emit_expression(codegen, format_argument);
        emit(codegen, ".data");
    }
    emit_format_arguments(codegen, node, 1);
    emit(codegen, ")");
}

static bool emit_format_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    static const struct { const char *name; const char *prefix; bool newline; } format_variants[] = {
        {"printf",     "printf(",                                false},
        {"printfln",   "printf(",                                true},
        {"eprintf",    "fprintf(stderr, ",                       false},
        {"eprintfln",  "fprintf(stderr, ",                       true},
        {"sprintf",    "gray_string_format(gray_default_arena, ", false},
        {"sprintfln",  "gray_string_format(gray_default_arena, ", true},
    };
    for (int i = 0; i < (int)(sizeof(format_variants) / sizeof(format_variants[0])); i++) {
        if (strcmp(function_name, format_variants[i].name) == 0 && node->data.call.argument_count >= 1) {
            emit_format_body(codegen, node, format_variants[i].prefix, format_variants[i].newline);
            return true;
        }
    }

    if (strcmp(function_name, "pad_left") == 0 && node->data.call.argument_count == 3) {
        emit(codegen, "gray_fmt_pad_left(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", (int32_t)(");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, "))");
        return true;
    }

    if (strcmp(function_name, "pad_right") == 0 && node->data.call.argument_count == 3) {
        emit(codegen, "gray_fmt_pad_right(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", (int32_t)(");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, "))");
        return true;
    }

    if (strcmp(function_name, "center") == 0 && node->data.call.argument_count == 3) {
        emit(codegen, "gray_fmt_center(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ", (int32_t)(");
        emit_expression(codegen, node->data.call.arguments[2]);
        emit(codegen, "))");
        return true;
    }

    if (strcmp(function_name, "i64_to_hex") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_fmt_i64_to_hex(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(function_name, "i64_to_binary") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_fmt_i64_to_binary(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(function_name, "i64_to_octal") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_fmt_i64_to_octal(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(function_name, "f64_to_fixed") == 0 && node->data.call.argument_count == 2) {
        emit(codegen, "gray_fmt_f64_to_fixed(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }

    if (strcmp(function_name, "f64_to_scientific") == 0 && node->data.call.argument_count == 1) {
        emit(codegen, "gray_fmt_f64_to_scientific(gray_default_arena, ");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }

    if ((strcmp(function_name, "format_number") == 0 || strcmp(function_name, "format_bytes") == 0) &&
        node->data.call.argument_count == 1) {
        emit_formatted(codegen, "gray_fmt_%s(gray_default_arena, ", function_name);
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }

    return false;
}

static bool emit_threads_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "spawn") == 0 && node->data.call.argument_count >= 1) {
        if (node->data.call.argument_count == 1) {
            emit(codegen, "gray_threads_spawn(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ")");
        } else {
            emit(codegen, "gray_threads_spawn_arg(");
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, ")");
        }
        return true;
    }
    if (strcmp(function_name, "join") == 0) {
        emit(codegen, "gray_threads_join(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "detach") == 0) {
        emit(codegen, "gray_threads_detach(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "is_alive") == 0) {
        emit(codegen, "gray_threads_is_alive(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "get_id") == 0) {
        emit(codegen, "gray_threads_id()");
        return true;
    }
    if (strcmp(function_name, "yield") == 0) {
        emit(codegen, "gray_threads_yield()");
        return true;
    }
    if (strcmp(function_name, "sleep") == 0) {
        emit(codegen, "gray_threads_sleep(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "thread_count") == 0) {
        emit(codegen, "gray_threads_thread_count()");
        return true;
    }
    return false;
}

/* --- chars module --- */

static bool emit_chars_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool needs_arena = (strcmp(function_name, "escape") == 0);
    emit_formatted(codegen, "gray_chars_%s(", function_name);
    if (needs_arena) emit(codegen, "gray_default_arena");
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (i > 0 || needs_arena) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- strconv module --- */

static bool emit_strconv_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    bool is_fallible = (strcmp(function_name, "to_i64") == 0 ||
        strcmp(function_name, "to_u64") == 0 ||
        strcmp(function_name, "to_f64") == 0 ||
        strcmp(function_name, "to_bool") == 0 ||
        strcmp(function_name, "unquote") == 0);
    bool has_base = (strcmp(function_name, "to_i64") == 0 ||
        strcmp(function_name, "to_u64") == 0);
    bool needs_arena = (strcmp(function_name, "from_i64") == 0 ||
        strcmp(function_name, "from_u64") == 0 ||
        strcmp(function_name, "from_f64") == 0 ||
        strcmp(function_name, "format_i64") == 0 ||
        strcmp(function_name, "format_u64") == 0 ||
        strcmp(function_name, "quote") == 0 ||
        strcmp(function_name, "unquote") == 0);

    if (is_fallible) {
        bool is_multi_variable = current_variable_is_result_temporary(codegen);
        if (is_multi_variable) {
            emit_formatted(codegen, "gray_strconv_%s_result(", function_name);
        } else {
            emit_formatted(codegen, "gray_strconv_%s(", function_name);
        }
        if (needs_arena) emit(codegen, "gray_default_arena, ");
        for (int i = 0; i < node->data.call.argument_count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[i]);
        }
        /* Default base=10 for to_i64/to_u64 when not provided */
        if (has_base && node->data.call.argument_count == 1) {
            emit(codegen, ", 10");
        }
        emit(codegen, ")");
        return true;
    }

    if (needs_arena) {
        emit_formatted(codegen, "gray_strconv_%s(gray_default_arena, ", function_name);
    } else {
        emit_formatted(codegen, "gray_strconv_%s(", function_name);
    }
    for (int i = 0; i < node->data.call.argument_count; i++) {
        if (i > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[i]);
    }
    emit(codegen, ")");
    return true;
}

/* --- @sync module --- */

static bool emit_sync_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "mutex") == 0) {
        emit(codegen, "gray_sync_mutex()");
        return true;
    }
    if (strcmp(function_name, "lock") == 0) {
        emit(codegen, "gray_sync_lock(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "unlock") == 0) {
        emit(codegen, "gray_sync_unlock(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "try_lock") == 0) {
        emit(codegen, "gray_sync_try_lock(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "destroy") == 0) {
        emit(codegen, "gray_sync_destroy(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    return false;
}

/* --- @atomic module --- */

static const PassthroughCall atomic_passthrough[] = {
    {"cas", 3, "gray_atomic_mod_cas"},
    {NULL, 0, NULL},
};

static bool emit_atomic_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "spinlock") == 0) {
        emit(codegen, "gray_atomic_mod_spinlock()");
        return true;
    }
    if (strcmp(function_name, "fence") == 0) {
        emit(codegen, "gray_atomic_mod_fence()");
        return true;
    }
    /* spinlock_destroy takes a pointer so the caller's _internal can be nulled */
    if (node->data.call.argument_count == 1 && strcmp(function_name, "spinlock_destroy") == 0) {
        emit(codegen, "gray_atomic_mod_spinlock_destroy(&");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    /* Single-argument functions: load, spin_lock, spin_unlock, spin_trylock */
    if (node->data.call.argument_count == 1) {
        if (strcmp(function_name, "load") == 0 || strcmp(function_name, "spin_lock") == 0 ||
            strcmp(function_name, "spin_trylock") == 0 || strcmp(function_name, "spin_unlock") == 0) {
            emit_formatted(codegen, "gray_atomic_mod_%s(", function_name);
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ")");
            return true;
        }
    }
    /* Two-argument functions: store, add, sub, exchange, and, or, xor */
    if (node->data.call.argument_count == 2) {
        if (strcmp(function_name, "store") == 0 || strcmp(function_name, "add") == 0 ||
            strcmp(function_name, "sub") == 0 || strcmp(function_name, "exchange") == 0 ||
            strcmp(function_name, "and") == 0 || strcmp(function_name, "or") == 0 ||
            strcmp(function_name, "xor") == 0) {
            emit_formatted(codegen, "gray_atomic_mod_%s(", function_name);
            emit_expression(codegen, node->data.call.arguments[0]);
            emit(codegen, ", ");
            emit_expression(codegen, node->data.call.arguments[1]);
            emit(codegen, ")");
            return true;
        }
    }
    /* Three-argument: cas */
    if (emit_passthrough_call(codegen, node, function_name, atomic_passthrough)) return true;
    return false;
}

/* --- @channels module --- */

static bool emit_channels_call(CodeGen *codegen, AstNode *node, const char *function_name) {
    if (strcmp(function_name, "open") == 0) {
        emit(codegen, "gray_channels_open(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "send") == 0) {
        emit(codegen, "gray_channels_send(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "receive") == 0) {
        emit(codegen, "gray_channels_receive(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "close") == 0) {
        emit(codegen, "gray_channels_close(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "try_send") == 0) {
        emit(codegen, "gray_channels_try_send(");
        emit_expression(codegen, node->data.call.arguments[0]);
        emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[1]);
        emit(codegen, ")");
        return true;
    }
    if (strcmp(function_name, "try_receive") == 0) {
        emit(codegen, "gray_channels_try_receive(");
        emit_expression(codegen, node->data.call.arguments[0]);
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
        const char *enum_name = ast_member_qualifier(node->data.call.function);
        const char *variant_name = node->data.call.function->data.member.member;
        /* Also check using-module-resolved names */
        const char *resolved_enum_name = enum_name;
        if (!codegen_is_enum(codegen, enum_name)) {
            const char *resolved_name = codegen_resolve_type(codegen, enum_name);
            if (resolved_name != enum_name && codegen_is_enum(codegen, resolved_name)) resolved_enum_name = resolved_name;
        }
        if (codegen_is_enum(codegen, resolved_enum_name) && codegen_enum_is_tagged(codegen, resolved_enum_name)) {
            /* Emit compound literal: (GrayEnum_Shape){ .tag = GrayEnum_Shape_TAG_Circle, .data.Circle = { args } } */
            int enum_index = codegen_enum_index(codegen, resolved_enum_name);
            AstNode *declaration = codegen->enum_declarations[enum_index];
            int matched_variant_index = -1;
            for (int variant_index = 0; variant_index < declaration->data.enum_declaration.value_count; variant_index++) {
                if (strcmp(declaration->data.enum_declaration.values[variant_index].name, variant_name) == 0) { matched_variant_index = variant_index; break; }
            }
            /* Wrapped in an extra pair of parens: a bare compound literal
             * contains an unparenthesized top-level comma once a payload is
             * present, which the C preprocessor splits into separate macro
             * arguments wherever this value lands inside a function-like
             * macro call (GRAY_ARRAY_SET_AT and friends). */
            emit(codegen, "(");
            emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s", resolved_enum_name, resolved_enum_name, variant_name);
            if (matched_variant_index >= 0 && declaration->data.enum_declaration.values[matched_variant_index].payload_count > 0) {
                emit_formatted(codegen, ", .data.%s = { ", variant_name);
                for (int argument_index = 0; argument_index < node->data.call.argument_count; argument_index++) {
                    if (argument_index > 0) emit(codegen, ", ");
                    emit_expression(codegen, node->data.call.arguments[argument_index]);
                }
                emit(codegen, " }");
            }
            emit(codegen, " })");
            return true;
        }
    }

    /* Tagged enum construction via implicit selector: .Circle(3.14) */
    if (node->data.call.function->kind == NODE_IMPLICIT_ENUM) {
        const char *enum_name = node->data.call.function->data.implicit_enum.resolved_enum;
        const char *variant_name = node->data.call.function->data.implicit_enum.variant;
        if (enum_name && codegen_enum_is_tagged(codegen, enum_name)) {
            int enum_index = codegen_enum_index(codegen, enum_name);
            AstNode *declaration = codegen->enum_declarations[enum_index];
            int matched_variant_index = -1;
            for (int variant_index = 0; variant_index < declaration->data.enum_declaration.value_count; variant_index++) {
                if (strcmp(declaration->data.enum_declaration.values[variant_index].name, variant_name) == 0) { matched_variant_index = variant_index; break; }
            }
            emit(codegen, "(");
            emit_formatted(codegen, "(GrayEnum_%s){ .tag = GrayEnum_%s_TAG_%s", enum_name, enum_name, variant_name);
            if (matched_variant_index >= 0 && declaration->data.enum_declaration.values[matched_variant_index].payload_count > 0) {
                emit_formatted(codegen, ", .data.%s = { ", variant_name);
                for (int argument_index = 0; argument_index < node->data.call.argument_count; argument_index++) {
                    if (argument_index > 0) emit(codegen, ", ");
                    emit_expression(codegen, node->data.call.arguments[argument_index]);
                }
                emit(codegen, " }");
            }
            emit(codegen, " })");
            return true;
        }
    }
    return false;
}

/* Emits a call through a func-typed struct field: `obj.field(args)`, or
 * `obj->field(args)` when `ptr_obj` (the object itself has pointer type,
 * so its field needs an arrow rather than a dot). An unset `func` field is
 * a NULL C function pointer with no other guard on the safe subset, so the
 * call is wrapped in a nil check that panics (P0118) instead of jumping
 * through NULL — which crashes with an uncatchable SIGILL and no
 * Grayscale diagnostic. */
static void emit_function_field_call(CodeGen *codegen, AstNode *node, AstNode *object,
                                  const char *member, bool is_pointer_object) {
    int argument_count = node->data.call.argument_count;
    if (argument_count > 0 && !node->data.call.arguments) argument_count = 0;
    GrayType *return_type = type_table_get(codegen->type_table, node);
    const char *c_return_type = (return_type && return_type->kind != TYPE_KIND_UNKNOWN && return_type->kind != TYPE_KIND_VOID)
        ? gray_type_to_c_codegen(codegen, type_name(return_type)) : "int64_t";
    if (return_type && return_type->kind == TYPE_KIND_VOID) c_return_type = "void";
    emit_formatted(codegen, "((%s (*)(", c_return_type);
    for (int argument_index = 0; argument_index < argument_count; argument_index++) {
        if (argument_index > 0) emit(codegen, ", ");
        GrayType *argument_type = type_table_get(codegen->type_table, node->data.call.arguments[argument_index]);
        emit(codegen, argument_type ? gray_type_to_c_codegen(codegen, type_name(argument_type)) : "int64_t");
    }
    emit(codegen, "))");
    emit(codegen, "({ void *_fp = (void *)(");
    emit_expression(codegen, object);
    emit_formatted(codegen, "%s%s); if (!_fp) { %s; } _fp; })",
        is_pointer_object ? "->" : ".", member, panic_call(codegen, node, "P0118", ""));
    emit(codegen, ")(");
    for (int argument_index = 0; argument_index < argument_count; argument_index++) {
        if (argument_index > 0) emit(codegen, ", ");
        emit_expression(codegen, node->data.call.arguments[argument_index]);
    }
    emit(codegen, ")");
}

/* Struct-namespaced (Name.func()) calls and mod.Struct.func() chains.
 * Returns true when it emitted the call; false to fall through to the
 * general function-call path. */
/* Emit one argument of a struct/namespaced function call. Unlike the general
 * call path, this dispatch never ran arguments through emit_narrowing_cast, so
 * an out-of-range value passed to a sized-integer parameter was truncated by C
 * with no runtime check. Apply the same checked cast here for non-mutable
 * sized-integer params; everything else keeps the existing behavior. */
static void emit_namespaced_call_argument(CodeGen *codegen, AstNode *argument,
                                          AstNode *function_node, int parameter_index, int line) {
    bool is_mutable_parameter = function_node && parameter_index < function_node->data.function_declaration.parameter_count &&
        function_node->data.function_declaration.parameters[parameter_index].is_mutable;
    const char *parameter_type_name = (function_node && parameter_index < function_node->data.function_declaration.parameter_count)
        ? function_node->data.function_declaration.parameters[parameter_index].type_name : NULL;
    if (!is_mutable_parameter && parameter_type_name && emit_narrowing_cast(codegen, parameter_type_name, argument, line))
        return;
    emit_mutable_call_argument(codegen, argument, is_mutable_parameter);
}

static bool emit_namespaced_call(CodeGen *codegen, AstNode *node) {
    /* Check for struct-namespaced or user-module function call: Name.func() */
    if (node->data.call.function->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *object = node->data.call.function->data.member.object;
        const char *member = node->data.call.function->data.member.member;

        /* A call through a func-typed struct field on an explicit pointer
         * dereference (`p^.field()`) is never touched by the typechecker's
         * instance-dispatch rewrite — that rewrite only fires for a real
         * struct function (retarget_member_object) — so `p^` reaches
         * codegen exactly as written. Nothing ever resolves that `p^`
         * expression node on its own (the typechecker reads the pointee
         * type off `p`'s symbol directly via ast_member_base_qualifier
         * without visiting the dereference), so it has no type_table
         * entry; resolve the pointee struct the same ways the label-object
         * path below resolves a plain `p.field()`. */
        if (object->kind == NODE_POSTFIX_EXPRESSION && object->data.postfix.operator == TOKEN_CARET &&
            object->data.postfix.left->kind == NODE_LABEL) {
            const char *variable_name_text = object->data.postfix.left->data.label.value;
            const char *struct_name_text = NULL;
            GrayType *pointer_type = type_table_get(codegen->type_table, object->data.postfix.left);
            if (pointer_type && pointer_type->kind == TYPE_KIND_POINTER && pointer_type->element_type) struct_name_text = pointer_type->element_type;
            if (!struct_name_text) {
                for (int struct_index = 0; struct_index < codegen->function_count && !struct_name_text; struct_index++) {
                    AstNode *function_declaration = codegen->all_functions[struct_index];
                    if (!function_declaration->data.function_declaration.body) continue;
                    for (int body_index = 0; body_index < function_declaration->data.function_declaration.body->data.block.count && !struct_name_text; body_index++) {
                        AstNode *body_statement = function_declaration->data.function_declaration.body->data.block.statements[body_index];
                        if (body_statement->kind != NODE_VARIABLE_DECLARATION || strcmp(body_statement->data.variable_declaration.name, variable_name_text) != 0) continue;
                        const char *declared_type_name = body_statement->data.variable_declaration.type_name;
                        if (declared_type_name && declared_type_name[0] == '^' && find_struct_declaration(codegen, declared_type_name + 1)) { struct_name_text = declared_type_name + 1; break; }
                        if (body_statement->data.variable_declaration.value && body_statement->data.variable_declaration.value->kind == NODE_NEW_EXPRESSION &&
                            body_statement->data.variable_declaration.value->data.new_expression.type_name) {
                            struct_name_text = body_statement->data.variable_declaration.value->data.new_expression.type_name;
                            break;
                        }
                    }
                }
            }
            if (!struct_name_text) {
                build_function_field_index(codegen);
                for (int i = 0; i < codegen->function_field_count; i++) {
                    if (strcmp(codegen->function_field_index[i].field_name, member) == 0) {
                        struct_name_text = codegen->function_field_index[i].struct_name;
                        break;
                    }
                }
            }
            AstNode *struct_declaration = struct_name_text ? find_struct_declaration(codegen, struct_name_text) : NULL;
            if (struct_declaration) {
                for (int field_index = 0; field_index < struct_declaration->data.struct_declaration.field_count; field_index++) {
                    StructField *struct_field = &struct_declaration->data.struct_declaration.fields[field_index];
                    if (strcmp(struct_field->name, member) == 0 && struct_field->type_name &&
                        (strcmp(struct_field->type_name, "func") == 0 || strncmp(struct_field->type_name, "func(", 5) == 0)) {
                        emit_function_field_call(codegen, node, object, member, false);
                        return true;
                    }
                }
            }
        }

        /* Handle mod.Struct.func() triple chain: geometry.Vec2.create().
         * The struct the qualified name refers to is on the object node; the
         * function is that struct's, namespaced under it. */
        const char *chain_module = NULL, *chain_type = NULL;
        if (ast_member_chain(node->data.call.function, &chain_module, &chain_type)) {
            char full_name[MESSAGE_BUFFER_SIZE];
            if (object->resolved_declaration) {
                char owner[MESSAGE_BUFFER_SIZE];
                snprintf(full_name, sizeof(full_name), "%s_%s",
                    module_mangle_into(object->resolved_declaration, owner, sizeof(owner)), member);
            } else {
                snprintf(full_name, sizeof(full_name), "%s_%s_%s",
                    chain_module, chain_type, member);
            }
            AstNode *namespaced_function = find_function(codegen, full_name);
            if (namespaced_function) {
                emit_formatted(codegen, "gray_fn_%s(", full_name);
                for (int i = 0; i < node->data.call.argument_count; i++) {
                    if (i > 0) emit(codegen, ", ");
                    emit_namespaced_call_argument(codegen, node->data.call.arguments[i],
                        namespaced_function, i, node->token.line);
                }
                emit(codegen, ")");
                return true;
            }
        }
        if (object->kind == NODE_LABEL) {
            const char *raw_name = object->data.label.value;

            /* C interop: extern.func(); emit raw C function call */
            if (strcmp(raw_name, "extern") == 0 && codegen->has_c_imports) {
                emit_formatted(codegen, "%s(", member);
                for (int i = 0; i < node->data.call.argument_count; i++) {
                    if (i > 0) emit(codegen, ", ");
                    AstNode *argument = node->data.call.arguments[i];
                    /* A string literal becomes a real C string literal, not
                     * gray_string_lit("...").data. A C literal is a valid
                     * const char*, so -Wformat-security stops firing and clang
                     * can check the printf family's varargs against it. */
                    if (argument->kind == NODE_STRING_VALUE) {
                        emit(codegen, "\"");
                        emit_c_string_body(codegen, argument);
                        emit(codegen, "\"");
                        continue;
                    }
                    /* Auto-convert GrayString to char* for C functions */
                    GrayType *argument_type = type_table_get(codegen->type_table, argument);
                    if (argument_type && argument_type->kind == TYPE_KIND_STRING) {
                        emit_expression(codegen, argument);
                        emit(codegen, ".data");
                    } else {
                        emit_expression(codegen, argument);
                    }
                }
                emit(codegen, ")");
                return true;
            }

            /* The qualifier may be a struct type, which is namespaced under
             * its module, or an import alias. Try the symbol table first. */
            const char *resolved_name = codegen_resolve_declaration(codegen, raw_name);
            if (resolved_name == raw_name) resolved_name = resolve_alias(codegen, raw_name);
            /* Try to find as a namespaced function: Name_func or ResolvedAlias_func */
            char namespaced_name[IDENTIFIER_BUFFER_SIZE];
            snprintf(namespaced_name, sizeof(namespaced_name), "%s_%s", resolved_name, member);
            AstNode *namespaced_function = find_function(codegen, namespaced_name);
            /* If not found, try using-module-prefixed struct names so
             * bare Product.create() from 'import and use' resolves to
             * types_Product_create. */
            static char using_resolved[IDENTIFIER_BUFFER_SIZE];
            if (!namespaced_function && resolved_name[0] >= 'A' && resolved_name[0] <= 'Z') {
                for (int using_index = 0; using_index < codegen->using_module_count; using_index++) {
                    const char *real_module = resolve_alias(codegen, codegen->using_modules[using_index]);
                    char prefixed[IDENTIFIER_BUFFER_SIZE];
                    snprintf(prefixed, sizeof(prefixed), "%s_%s_%s", real_module, resolved_name, member);
                    namespaced_function = find_function(codegen, prefixed);
                    if (namespaced_function) {
                        snprintf(using_resolved, sizeof(using_resolved), "%s_%s", real_module, resolved_name);
                        resolved_name = using_resolved;
                        snprintf(namespaced_name, sizeof(namespaced_name), "%s_%s", resolved_name, member);
                        break;
                    }
                }
            }
            bool instance_dispatch = false;
            bool is_object_pointer = false;
            if (!namespaced_function) {
                /* check if `member` is a func-typed data field
                 * on the struct. If so, emit as a function-pointer call
                 * through the field access. We get here when the variable
                 * has a struct type but neither <struct>_<member> nor
                 * bare <member> is a registered function. */
                GrayType *instantiation_type = type_table_get(codegen->type_table, object);
                /* Save pointer flag before fallback may overwrite instantiation_type with TYPE_KIND_STRUCT */
                is_object_pointer = instantiation_type && instantiation_type->kind == TYPE_KIND_POINTER;
                /* If type table missed, scan var decls for a new() initializer */
                if (!is_object_pointer && object->kind == NODE_LABEL) {
                    const char *variable_name_text = object->data.label.value;
                    for (int struct_index = 0; struct_index < codegen->function_count && !is_object_pointer; struct_index++) {
                        AstNode *function_declaration = codegen->all_functions[struct_index];
                        if (!function_declaration->data.function_declaration.body) continue;
                        for (int body_index = 0; body_index < function_declaration->data.function_declaration.body->data.block.count && !is_object_pointer; body_index++) {
                            AstNode *body_statement = function_declaration->data.function_declaration.body->data.block.statements[body_index];
                            if (body_statement->kind == NODE_VARIABLE_DECLARATION &&
                                strcmp(body_statement->data.variable_declaration.name, variable_name_text) == 0 &&
                                body_statement->data.variable_declaration.value &&
                                body_statement->data.variable_declaration.value->kind == NODE_NEW_EXPRESSION) {
                                is_object_pointer = true;
                                if ((!instantiation_type || instantiation_type->kind == TYPE_KIND_UNKNOWN) &&
                                    body_statement->data.variable_declaration.value->data.new_expression.type_name) {
                                    instantiation_type = type_struct(body_statement->data.variable_declaration.value->data.new_expression.type_name);
                                }
                            }
                        }
                    }
                }
                /* Fall back to scanning var decls for struct type */
                if ((!instantiation_type || instantiation_type->kind == TYPE_KIND_UNKNOWN) && object->kind == NODE_LABEL) {
                    const char *variable_name_text = object->data.label.value;
                    for (int struct_index = 0; struct_index < codegen->function_count && (!instantiation_type || instantiation_type->kind == TYPE_KIND_UNKNOWN); struct_index++) {
                        AstNode *function_declaration = codegen->all_functions[struct_index];
                        if (!function_declaration->data.function_declaration.body) continue;
                        for (int body_index = 0; body_index < function_declaration->data.function_declaration.body->data.block.count; body_index++) {
                            AstNode *body_statement = function_declaration->data.function_declaration.body->data.block.statements[body_index];
                            if (body_statement->kind != NODE_VARIABLE_DECLARATION ||
                                strcmp(body_statement->data.variable_declaration.name, variable_name_text) != 0) continue;
                            const char *declared_type_name = body_statement->data.variable_declaration.type_name;
                            if (declared_type_name && find_struct_declaration(codegen, declared_type_name)) {
                                instantiation_type = type_struct(declared_type_name);
                                break;
                            }
                            if (body_statement->data.variable_declaration.value &&
                                body_statement->data.variable_declaration.value->kind == NODE_STRUCT_VALUE) {
                                const char *struct_name_text = body_statement->data.variable_declaration.value->data.struct_value.name;
                                if (struct_name_text && find_struct_declaration(codegen, struct_name_text)) {
                                    instantiation_type = type_struct(struct_name_text);
                                    break;
                                }
                            }
                        }
                    }
                }
                /* Fall back to scanning struct decls if the type_table
                 * doesn't have a hit for the label. */
                if (!instantiation_type || instantiation_type->kind == TYPE_KIND_UNKNOWN) {
                    build_function_field_index(codegen);
                    for (int i = 0; i < codegen->function_field_count; i++) {
                        if (strcmp(codegen->function_field_index[i].field_name, member) == 0) {
                            instantiation_type = type_struct(codegen->function_field_index[i].struct_name);
                            break;
                        }
                    }
                }
                if (instantiation_type && (instantiation_type->kind == TYPE_KIND_STRUCT || instantiation_type->kind == TYPE_KIND_POINTER)) {
                    const char *struct_name_text = (instantiation_type->kind == TYPE_KIND_POINTER && instantiation_type->element_type)
                        ? instantiation_type->element_type : instantiation_type->name;
                    AstNode *struct_declaration = struct_name_text ? find_struct_declaration(codegen, struct_name_text) : NULL;
                    if (struct_declaration) {
                        for (int field_index = 0; field_index < struct_declaration->data.struct_declaration.field_count; field_index++) {
                            if (strcmp(struct_declaration->data.struct_declaration.fields[field_index].name, member) == 0 &&
                                struct_declaration->data.struct_declaration.fields[field_index].type_name &&
                                (strcmp(struct_declaration->data.struct_declaration.fields[field_index].type_name, "func") == 0 || strncmp(struct_declaration->data.struct_declaration.fields[field_index].type_name, "func(", 5) == 0)) {
                                emit_function_field_call(codegen, node, object, member, is_object_pointer);
                                return true;
                            }
                        }
                    }
                }
                /* Instance dispatch: var.func() -> StructType_func(&var)
                 * Look up the variable's struct type and try StructName_member. */
                if (instantiation_type && (instantiation_type->kind == TYPE_KIND_STRUCT || instantiation_type->kind == TYPE_KIND_POINTER)) {
                    const char *struct_name_text = (instantiation_type->kind == TYPE_KIND_POINTER && instantiation_type->element_type)
                        ? instantiation_type->element_type : instantiation_type->name;
                    if (struct_name_text) {
                        snprintf(namespaced_name, sizeof(namespaced_name), "%s_%s", struct_name_text, member);
                        namespaced_function = find_function(codegen, namespaced_name);
                        if (namespaced_function) {
                            resolved_name = struct_name_text;
                            instance_dispatch = true;
                        }
                    }
                }
                /* Try bare function name (for main-file functions in circular imports) */
                if (!namespaced_function) namespaced_function = find_function(codegen, member);
                if (namespaced_function && strcmp(namespaced_name, member) != 0) {
                    /* Already handled by struct dispatch below */
                } else if (namespaced_function) {
                    emit_formatted(codegen, "gray_fn_%s(", member);
                    for (int i = 0; i < node->data.call.argument_count; i++) {
                        if (i > 0) emit(codegen, ", ");
                        emit_namespaced_call_argument(codegen, node->data.call.arguments[i],
                            namespaced_function, i, node->token.line);
                    }
                    emit(codegen, ")");
                    return true;
                }
            }
            if (namespaced_function) {
                /* Generic struct function: mangle with concrete binding */
                if (function_is_generic(namespaced_function)) {
                    const char *binding = NULL;
                    char *dynamic_binding = NULL;
                    int clamped_argument_count = namespaced_function->data.function_declaration.parameter_count < node->data.call.argument_count
                        ? namespaced_function->data.function_declaration.parameter_count : node->data.call.argument_count;
                    for (int parameter_index = 0; parameter_index < clamped_argument_count && !binding; parameter_index++) {
                        /* Type parameter: the binding is the argument label
                         * itself (a struct name), not a resolved value type.
                         * Without this the loop found nothing to bind and
                         * mangled the call against a specialization that was
                         * never emitted. */
                        if (namespaced_function->data.function_declaration.parameters[parameter_index].is_type_parameter) {
                            if (node->data.call.arguments[parameter_index]->kind == NODE_LABEL) {
                                const char *label = node->data.call.arguments[parameter_index]->data.label.value;
                                if (strcmp(label, "?") == 0 && codegen->wildcard_binding)
                                    binding = codegen->wildcard_binding;
                                else
                                    binding = label;
                            }
                            continue;
                        }
                        const char *parameter_type_name = namespaced_function->data.function_declaration.parameters[parameter_index].type_name;
                        if (!parameter_type_name || !strchr(parameter_type_name, '?')) continue;
                        GrayType *argument_type_for_binding = type_table_get(codegen->type_table, node->data.call.arguments[parameter_index]);
                        if (!argument_type_for_binding) continue;
                        if (strcmp(parameter_type_name, "?") == 0) {
                            const char *type_spelling = type_name(argument_type_for_binding);
                            binding = ((argument_type_for_binding->kind == TYPE_KIND_UNKNOWN || strcmp(type_spelling, "unknown") == 0)
                                && codegen->wildcard_binding)
                                ? codegen->wildcard_binding : type_spelling;
                        } else {
                            dynamic_binding = codegen_bind_wildcard(parameter_type_name, type_name(argument_type_for_binding));
                            if (dynamic_binding) binding = dynamic_binding;
                        }
                    }
                    char mangled[MESSAGE_BUFFER_SIZE];
                    size_t position = (size_t)snprintf(mangled, sizeof(mangled),
                        "gray_fn_%s_%s__", resolved_name, member);
                    if (binding) {
                        for (const char *cursor = binding; *cursor && position < sizeof(mangled) - 1; cursor++) {
                            mangled[position++] = (isalnum((unsigned char)*cursor) || *cursor == '_') ? *cursor : '_';
                        }
                    }
                    mangled[position] = '\0';
                    emit_formatted(codegen, "%s(", mangled);
                    free(dynamic_binding);
                } else {
                    emit_formatted(codegen, "gray_fn_%s_%s(", resolved_name, member);
                }
                /* For instance dispatch, inject the instance as the first
                 * argument when the struct function expects more params
                 * than the call site provides. */
                bool self_injected = false;
                if (instance_dispatch &&
                    namespaced_function->data.function_declaration.parameter_count > node->data.call.argument_count) {
                    if (namespaced_function->data.function_declaration.parameters[0].is_mutable) {
                        if (is_object_pointer) emit_expression(codegen, object);
                        else { emit(codegen, "&"); emit_expression(codegen, object); }
                    } else {
                        emit_expression(codegen, object);
                    }
                    self_injected = true;
                }
                bool was_argument_emitted = self_injected;
                for (int i = 0; i < node->data.call.argument_count; i++) {
                    int parameter_index = self_injected ? i + 1 : i;
                    /* Type params are erased in C; emitting one leaked the
                     * type name into the output as a bare identifier. */
                    if (parameter_index < namespaced_function->data.function_declaration.parameter_count &&
                        namespaced_function->data.function_declaration.parameters[parameter_index].is_type_parameter) continue;
                    if (was_argument_emitted) emit(codegen, ", ");
                    was_argument_emitted = true;
                    emit_namespaced_call_argument(codegen, node->data.call.arguments[i],
                        namespaced_function, parameter_index, node->token.line);
                }
                /* Inject default values for omitted trailing parameters */
                {
                    int self_offset = self_injected ? 1 : 0;
                    int first_default = node->data.call.argument_count + self_offset;
                    bool any_emitted = was_argument_emitted;
                    for (int i = first_default; i < namespaced_function->data.function_declaration.parameter_count; i++) {
                        if (namespaced_function->data.function_declaration.parameters[i].is_type_parameter) continue;
                        if (any_emitted) emit(codegen, ", ");
                        any_emitted = true;
                        if (namespaced_function->data.function_declaration.parameters[i].default_value) {
                            emit_parameter_default_value(codegen, &namespaced_function->data.function_declaration.parameters[i]);
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
    const char *function_name = NULL;

    if (is_stdlib_call(node, &module, &function_name)) {
        /* Resolve import aliases: io@std → io.println maps to std.println */
        if (module) module = resolve_alias(codegen, module);

        /* No-module builtins (println, len, type_of, etc.) */
        /* Also handle std.println(); std module functions are builtins */
        if (!module && emit_builtin_call(codegen, node, function_name)) return;

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
            int low_index = 0, high = (int)(sizeof(modules) / sizeof(modules[0])) - 1;
            while (low_index <= high) {
                int midpoint = (low_index + high) / 2;
                int comparison = strcmp(module, modules[midpoint].name);
                if (comparison == 0) {
                    if (modules[midpoint].handler(codegen, node, function_name)) return;
                    break;
                }
                if (comparison < 0) high = midpoint - 1;
                else low_index = midpoint + 1;
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
        if (!module && function_name) {
            user_shadows_using = find_function(codegen, function_name) != NULL;
            if (!user_shadows_using) {
                const char *resolved_declaration = codegen_resolve_declaration(codegen, function_name);
                if (resolved_declaration != function_name) user_shadows_using = find_function(codegen, resolved_declaration) != NULL;
            }
        }
        if (!module && !user_shadows_using) {
            for (int using_index = 0; using_index < codegen->using_module_count; using_index++) {
                const char *using_module = codegen->using_modules[using_index];
                const char *real_module = resolve_alias(codegen, using_module);
                /* 1) Check stdlib table (authoritative source in typechecker) */
                bool found = stdlib_has_function(real_module, function_name);
                if (found) {
                    /* Dispatch to the stdlib module handler */
                    for (int module_index = 0; module_index < (int)(sizeof(modules) / sizeof(modules[0])); module_index++) {
                        if (strcmp(real_module, modules[module_index].name) == 0) {
                            if (modules[module_index].handler(codegen, node, function_name)) return;
                            break;
                        }
                    }
                }
                /* 2) Try user-defined module: <module>_<func> */
                if (!found) {
                    char prefixed[IDENTIFIER_BUFFER_SIZE];
                    snprintf(prefixed, sizeof(prefixed), "%s_%s", real_module, function_name);
                    AstNode *prefixed_function = find_function(codegen, prefixed);
                    /* A generic function needs its per-instantiation name,
                     * which the general call path derives from the argument
                     * types; this shortcut would emit the unspecialised
                     * symbol, so leave those to it. */
                    if (prefixed_function && function_is_generic(prefixed_function)) prefixed_function = NULL;
                    if (prefixed_function) {
                        int parameter_count = prefixed_function->data.function_declaration.parameter_count;
                        int argument_count = node->data.call.argument_count;
                        int slot_count = argument_count < parameter_count ? parameter_count : argument_count;
                        emit_formatted(codegen, "gray_fn_%s_%s(", real_module, function_name);
                        for (int i = 0; i < slot_count; i++) {
                            if (i > 0) emit(codegen, ", ");
                            if (i < argument_count) {
                                emit_namespaced_call_argument(codegen, node->data.call.arguments[i],
                                    prefixed_function, i, node->token.line);
                            } else if (i < parameter_count && prefixed_function->data.function_declaration.parameters[i].default_value) {
                                emit_parameter_default_value(codegen, &prefixed_function->data.function_declaration.parameters[i]);
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
    const char *function_name_text = NULL;
    if (node->data.call.function->kind == NODE_LABEL) {
        function_name_text = node->data.call.function->data.label.value;
    }

    /* Look up function to check if it's a known function or a variable (function pointer) */
    AstNode *target_function = function_name_text ? find_function(codegen, function_name_text) : NULL;

    /* A name written bare inside its own module, or reachable through a
     * `using`, names a function registered under its module's spelling. This
     * used to match on the text after the first underscore of every declared
     * function, which claimed any name that happened to be some function's
     * suffix. */
    const char *resolved_function_name = function_name_text;
    if (!target_function && function_name_text) {
        const char *resolved = codegen_resolve_declaration(codegen, function_name_text);
        if (resolved != function_name_text) {
            AstNode *found = find_function(codegen, resolved);
            if (found) {
                target_function = found;
                resolved_function_name = resolved;
            }
        }
    }

    bool direct_known_call = (function_name_text && target_function);
    if (function_name_text && target_function) {
        /* Known function; use gray_fn_ prefix. For generic functions,
         * rewrite to the mangled instantiation name derived from the
         * first wildcard parameter's argument type ). */
        bool is_target_function_generic = function_is_generic(target_function);
        if (is_target_function_generic) {
            /* Derive the concrete binding by scanning params for the
             * first '?' slot and reading the matching arg's type. */
            const char *binding = NULL;
            char *dynamic_binding = NULL;
            int parameter_count = target_function->data.function_declaration.parameter_count;
            int argument_count = node->data.call.argument_count;
            int paired_count = parameter_count < argument_count ? parameter_count : argument_count;
            for (int parameter_index = 0; parameter_index < paired_count && !binding; parameter_index++) {
                /* Type parameter: binding is the arg label directly.
                 * When forwarding (T→"?"), resolve via the outer binding. */
                if (target_function->data.function_declaration.parameters[parameter_index].is_type_parameter) {
                    if (node->data.call.arguments[parameter_index]->kind == NODE_LABEL) {
                        const char *label = node->data.call.arguments[parameter_index]->data.label.value;
                        if (strcmp(label, "?") == 0 && codegen->wildcard_binding)
                            binding = codegen->wildcard_binding;
                        else
                            binding = label;
                    }
                    continue;
                }
                const char *parameter_type_name = target_function->data.function_declaration.parameters[parameter_index].type_name;
                if (!parameter_type_name || !strchr(parameter_type_name, '?')) continue;
                GrayType *argument_type_for_binding = type_table_get(codegen->type_table, node->data.call.arguments[parameter_index]);
                if (!argument_type_for_binding) continue;
                if (strcmp(parameter_type_name, "?") == 0) {
                    /* Inside a generic body the arg's typetable entry is still
                     * TYPE_KIND_UNKNOWN from the main-pass walk; use the outer binding. */
                    const char *type_spelling = type_name(argument_type_for_binding);
                    if ((argument_type_for_binding->kind == TYPE_KIND_UNKNOWN || strcmp(type_spelling, "unknown") == 0) &&
                        codegen->wildcard_binding) {
                        binding = codegen->wildcard_binding;
                    } else {
                        binding = type_spelling;
                    }
                } else {
                    /* Composite param type (e.g. [[?]], [map[K:?]], map[K:[?]]).
                     * Use the recursive helper to peel layers until '?' is reached. */
                    char *derived = codegen_bind_wildcard(parameter_type_name, type_name(argument_type_for_binding));
                    if (derived) {
                        free(dynamic_binding);
                        dynamic_binding = derived;
                        binding = dynamic_binding;
                    }
                }
            }
            size_t resolved_function_name_length = strlen(resolved_function_name);
            size_t binding_length = binding ? strlen(binding) : 0;
            size_t mangled_name_size = 8 + resolved_function_name_length + 2 + binding_length + 1; /* 8 = strlen("gray_fn_") */
            char *mangled = malloc(mangled_name_size);
            if (!mangled) return;
            size_t mangled_position = (size_t)snprintf(mangled, mangled_name_size, "gray_fn_%s__",
                resolved_function_name);
            if (binding) {
                for (const char *cursor = binding; *cursor && mangled_position < mangled_name_size - 1; cursor++) {
                    mangled[mangled_position++] = (isalnum((unsigned char)*cursor) || *cursor == '_') ? *cursor : '_';
                }
            }
            mangled[mangled_position] = '\0';
            emit(codegen, mangled);
            free(mangled);
            free(dynamic_binding);
        } else {
            emit(codegen, "gray_fn_");
            emit(codegen, resolved_function_name);
        }
    } else if (function_name_text) {
        /* Not a known function; variable holding a function pointer (void *).
         * Cast to appropriate function pointer type based on the variable's
         * typed-func signature, falling back to a brittle var_decl scan only
         * when no signature is available (bare-func paths). */
        int call_argument_count = node->data.call.argument_count;
        GrayFunctionSignature *typed_signature = NULL;
        GrayType *callee_type = type_table_get(codegen->type_table, node->data.call.function);
        if (callee_type && callee_type->kind == TYPE_KIND_FUNCTION) {
            typed_signature = callee_type->function_signature;
        }
        AstNode *referenced_function = NULL;
        /* Always scan, even when typed_sig is set: the FunctionSignature in the
         * AST decl carries default values that the typed-func signature does
         * not, so the scan is needed for the defaults-fill path below. */
        {
            for (int struct_index = 0; struct_index < codegen->function_count && !referenced_function; struct_index++) {
                AstNode *candidate_function = codegen->all_functions[struct_index];
                if (!candidate_function->data.function_declaration.body) continue;
                for (int body_index = 0; body_index < candidate_function->data.function_declaration.body->data.block.count && !referenced_function; body_index++) {
                    AstNode *body_statement = candidate_function->data.function_declaration.body->data.block.statements[body_index];
                    if (body_statement->kind == NODE_VARIABLE_DECLARATION &&
                        strcmp(body_statement->data.variable_declaration.name, function_name_text) == 0 &&
                        body_statement->data.variable_declaration.value &&
                        body_statement->data.variable_declaration.value->kind == NODE_FUNCTION_REFERENCE) {
                        AstNode *function_reference = body_statement->data.variable_declaration.value->data.function_reference.function;
                        if (function_reference->kind == NODE_LABEL) {
                            referenced_function = find_function(codegen, function_reference->data.label.value);
                        } else if (ast_member_qualifier(function_reference)) {
                            const char *resolved_object = ast_member_qualifier(function_reference);
                            const char *resolved_member = function_reference->data.member.member;
                            char resolved_name[IDENTIFIER_BUFFER_SIZE];
                            snprintf(resolved_name, sizeof(resolved_name), "%s_%s", resolved_object, resolved_member);
                            referenced_function = find_function(codegen, resolved_name);
                        }
                    }
                }
            }
            if (referenced_function) target_function = referenced_function;
        }
        /* Return type: typed_sig wins, else fall back to call-node type table.
         * Multi-return needs an inline anonymous struct with v0,v1,...
         * fields — the same layout emit_multi_return_typedef() gives every
         * concrete function's own named GrayMulti_<name>. The callee is an
         * erased function pointer, so there is no single named typedef to
         * reuse here; an unnamed struct with matching field order and types
         * is layout-identical to whatever real function's GrayMulti_ struct
         * actually comes back, and the destructuring .v0/.v1 access below
         * reads it the same way either way. */
        char multi_return_buffer[MESSAGE_BUFFER_SIZE];
        const char *c_return_type = "int64_t";
        if (typed_signature) {
            if (typed_signature->return_count == 0) {
                c_return_type = "void";
            } else if (typed_signature->return_count == 1) {
                c_return_type = gray_type_to_c_codegen(codegen, typed_signature->return_types[0]);
            } else {
                int mangled_position = snprintf(multi_return_buffer, sizeof(multi_return_buffer), "struct {");
                for (int i = 0; i < typed_signature->return_count && mangled_position > 0 &&
                     (size_t)mangled_position < sizeof(multi_return_buffer); i++) {
                    int written = snprintf(multi_return_buffer + mangled_position, sizeof(multi_return_buffer) - (size_t)mangled_position,
                        " %s v%d;", gray_type_to_c_codegen(codegen, typed_signature->return_types[i]), i);
                    mangled_position = (written > 0) ? mangled_position + written : -1;
                }
                if (mangled_position > 0 && (size_t)mangled_position < sizeof(multi_return_buffer))
                    snprintf(multi_return_buffer + mangled_position, sizeof(multi_return_buffer) - (size_t)mangled_position, " }");
                c_return_type = multi_return_buffer;
            }
        } else {
            GrayType *return_type = type_table_get(codegen->type_table, node);
            if (return_type && return_type->kind != TYPE_KIND_UNKNOWN) c_return_type = gray_type_to_c_codegen(codegen, type_name(return_type));
            if (return_type && return_type->kind == TYPE_KIND_VOID) c_return_type = "void";
        }
        int cast_count = call_argument_count;
        if (typed_signature && typed_signature->parameter_count > call_argument_count) cast_count = typed_signature->parameter_count;
        if (referenced_function && referenced_function->data.function_declaration.parameter_count > call_argument_count) {
            cast_count = referenced_function->data.function_declaration.parameter_count;
        }
        emit_formatted(codegen, "((%s (*)(", c_return_type);
        for (int i = 0; i < cast_count; i++) {
            if (i > 0) emit(codegen, ", ");
            bool is_mutable_parameter = false;
            if (typed_signature && i < typed_signature->parameter_count) {
                is_mutable_parameter = typed_signature->is_parameter_mutable[i];
            } else if (referenced_function && i < referenced_function->data.function_declaration.parameter_count) {
                is_mutable_parameter = referenced_function->data.function_declaration.parameters[i].is_mutable;
            }
            if (typed_signature && i < typed_signature->parameter_count) {
                /* Parameter type comes straight from the signature; authoritative */
                emit(codegen, gray_type_to_c_codegen(codegen, typed_signature->parameter_types[i]));
                if (is_mutable_parameter) emit(codegen, " *");
            } else if (i < call_argument_count) {
                GrayType *argument_type = type_table_get(codegen->type_table, node->data.call.arguments[i]);
                if (argument_type && argument_type->kind != TYPE_KIND_UNKNOWN) {
                    emit(codegen, gray_type_to_c_codegen(codegen, type_name(argument_type)));
                    if (is_mutable_parameter) emit(codegen, " *");
                } else {
                    emit(codegen, "int64_t");
                    if (is_mutable_parameter) emit(codegen, " *");
                }
            } else if (referenced_function && i < referenced_function->data.function_declaration.parameter_count) {
                const char *parameter_type_spelling = referenced_function->data.function_declaration.parameters[i].type_name;
                emit(codegen, parameter_type_spelling ? gray_type_to_c_codegen(codegen, parameter_type_spelling) : "int64_t");
                if (is_mutable_parameter) emit(codegen, " *");
            } else {
                emit(codegen, "int64_t");
            }
        }
        if (cast_count == 0) emit(codegen, "void");
        emit(codegen, "))");
        emit(codegen, sanitize_name(function_name_text));
        emit(codegen, ")");
        /* Stash the typed_sig in a side channel so the arg emission below
         * can pick up param mutability when target_func is NULL. */
        codegen->pending_call_typed_signature = typed_signature;
    } else if (node->data.call.function->kind == NODE_MEMBER_EXPRESSION) {
        /* Module-qualified call fallback: module.func() → gray_module_func() */
        AstNode *object = node->data.call.function->data.member.object;
        const char *member = node->data.call.function->data.member.member;
        if (object->kind == NODE_LABEL) {
            const char *module_name = codegen_resolve_declaration(codegen, object->data.label.value);
            if (module_name == object->data.label.value)
                module_name = resolve_alias(codegen, object->data.label.value);
            emit_formatted(codegen, "gray_%s_%s", module_name, member);
            /* Look up target_func for default params / mutable ref handling */
            char prefixed[256];
            snprintf(prefixed, sizeof(prefixed), "%s_%s", module_name, member);
            for (int field_index = 0; field_index < codegen->function_count; field_index++) {
                if (strcmp(codegen->all_functions[field_index]->data.function_declaration.name, prefixed) == 0) {
                    target_function = codegen->all_functions[field_index];
                    break;
                }
            }
            if (!target_function) {
                /* Try just the bare member name */
                for (int field_index = 0; field_index < codegen->function_count; field_index++) {
                    const char *registered = codegen->all_functions[field_index]->data.function_declaration.name;
                    const char *underscore = strchr(registered, '_');
                    if (underscore && strcmp(underscore + 1, member) == 0) {
                        target_function = codegen->all_functions[field_index];
                        break;
                    }
                }
            }
        } else {
            emit_expression(codegen, node->data.call.function);
        }
    } else if (node->data.call.function->kind == NODE_INDEX_EXPRESSION) {
        /* Indexed callee (e.g. ops[0](x, y)). The index expression yields a
         * void * for [func] arrays (see NODE_INDEX_EXPRESSION emitter), which isn't
         * directly callable; wrap with a function-pointer cast derived from
         * the call site's arg types and return type. */
        int call_argument_count = node->data.call.argument_count;
        GrayType *return_type = type_table_get(codegen->type_table, node);
        const char *c_return_type = (return_type && return_type->kind != TYPE_KIND_UNKNOWN) ? gray_type_to_c_codegen(codegen, type_name(return_type)) : "int64_t";
        if (return_type && return_type->kind == TYPE_KIND_VOID) c_return_type = "void";
        emit_formatted(codegen, "((%s (*)(", c_return_type);
        for (int i = 0; i < call_argument_count; i++) {
            if (i > 0) emit(codegen, ", ");
            GrayType *argument_type = type_table_get(codegen->type_table, node->data.call.arguments[i]);
            if (argument_type && argument_type->kind != TYPE_KIND_UNKNOWN) {
                emit(codegen, gray_type_to_c_codegen(codegen, type_name(argument_type)));
            } else {
                emit(codegen, "int64_t");
            }
        }
        if (call_argument_count == 0) emit(codegen, "void");
        emit(codegen, "))");
        emit_expression(codegen, node->data.call.function);
        emit(codegen, ")");
    } else {
        emit_expression(codegen, node->data.call.function);
    }

    /* Determine total args: provided + defaults */
    GrayFunctionSignature *call_typed_signature = (GrayFunctionSignature *)codegen->pending_call_typed_signature;
    codegen->pending_call_typed_signature = NULL; /* one-shot; clear before recursion */
    int total_argument_count = node->data.call.argument_count;
    int parameter_count = target_function ? target_function->data.function_declaration.parameter_count
                                  : (call_typed_signature ? call_typed_signature->parameter_count : 0);
    if (total_argument_count < parameter_count) total_argument_count = parameter_count;

    /* If any default-fill positions reference a parameter name, the default
     * expression has to evaluate with those names in scope. Inline-paste
     * into the caller would emit `gray_fn_f(10, a + 1)` and clang errors on
     * `a`. Detect "defaults will fire AND none of the affected params are
     * &-mut" and rewrite as a statement expression that binds the earlier
     * provided args to local copies named after the parameters. */
    bool uses_defaults = false;
    bool has_mutable_parameter = false;
    if (target_function) {
        for (int i = node->data.call.argument_count; i < parameter_count; i++) {
            if (target_function->data.function_declaration.parameters[i].default_value) {
                uses_defaults = true;
                break;
            }
        }
        for (int i = 0; i < target_function->data.function_declaration.parameter_count; i++) {
            if (target_function->data.function_declaration.parameters[i].is_mutable) {
                has_mutable_parameter = true;
                break;
            }
        }
    }
    /* Mut params would need address aliasing rather than value-copy bindings;
     * those edge cases keep the inline-paste path. The wrap also
     * only works for the direct-call branch; call-through-variable paths
     * have a cast prefix (e.g. ((T (*)(...))g)) that this code can't safely
     * reconstruct. */
    if (uses_defaults && !has_mutable_parameter && direct_known_call) {
        /* Re-emit: ({ T0 a0 = arg0; T1 a1 = arg1; ...; gray_fn_X(a0, a1, default_for_n, ...); }) */
        size_t out_length = codegen->output.length;
        size_t function_emit_length = 0;
        /* Pull off the just-emitted "gray_fn_<name>" so we can prepend the
         * statement-expression bindings. */
        char saved_function_name[TYPE_NAME_MAX] = {0};
        if (out_length > 0) {
            /* Walk back to start of the most recent identifier */
            size_t scan = out_length;
            while (scan > 0) {
                char cursor = codegen->output.data[scan - 1];
                if (!(isalnum((unsigned char)cursor) || cursor == '_')) break;
                scan--;
            }
            function_emit_length = out_length - scan;
            if (function_emit_length > 0 && function_emit_length < sizeof(saved_function_name)) {
                memcpy(saved_function_name, codegen->output.data + scan, function_emit_length);
                saved_function_name[function_emit_length] = '\0';
                codegen->output.length = scan;
            }
        }
        emit(codegen, "({ ");
        for (int i = 0; i < node->data.call.argument_count; i++) {
            if (target_function->data.function_declaration.parameters[i].is_type_parameter) continue;
            const char *parameter_name = target_function->data.function_declaration.parameters[i].name;
            const char *parameter_type_spelling = target_function->data.function_declaration.parameters[i].type_name;
            const char *c_type = parameter_type_spelling ? gray_type_to_c_codegen(codegen, parameter_type_spelling) : "int64_t";
            emit_formatted(codegen, "%s %s = ", c_type, parameter_name ? sanitize_name(parameter_name) : "_arg");
            if (!emit_wide_integer_coerced(codegen, parameter_type_spelling, node->data.call.arguments[i]) &&
                !emit_narrowing_cast(codegen, parameter_type_spelling, node->data.call.arguments[i], node->token.line))
                emit_declared_value(codegen, parameter_type_spelling, node->data.call.arguments[i]);
            emit(codegen, "; ");
        }
        emit(codegen, saved_function_name);
        emit(codegen, "(");
        {
            bool is_first_default = true;
            for (int i = 0; i < total_argument_count; i++) {
                if (target_function->data.function_declaration.parameters[i].is_type_parameter) continue;
                if (!is_first_default) emit(codegen, ", ");
                is_first_default = false;
                if (i < node->data.call.argument_count) {
                    const char *parameter_name = target_function->data.function_declaration.parameters[i].name;
                    emit_formatted(codegen, "%s", parameter_name ? sanitize_name(parameter_name) : "_arg");
                } else {
                    emit_parameter_default_value(codegen, &target_function->data.function_declaration.parameters[i]);
                }
            }
        }
        emit(codegen, "); })");
        return;
    }

    emit(codegen, "(");
    bool is_first_argument = true;
    for (int i = 0; i < total_argument_count; i++) {
        /* Skip type params — erased in C */
        if (target_function && i < target_function->data.function_declaration.parameter_count &&
            target_function->data.function_declaration.parameters[i].is_type_parameter) continue;
        if (!is_first_argument) emit(codegen, ", ");
        is_first_argument = false;

        if (i < node->data.call.argument_count) {
            /* Provided argument */
            bool needs_address = false;
            if (target_function && i < target_function->data.function_declaration.parameter_count) {
                needs_address = target_function->data.function_declaration.parameters[i].is_mutable;
            } else if (call_typed_signature && i < call_typed_signature->parameter_count) {
                needs_address = call_typed_signature->is_parameter_mutable[i];
            }
            if (needs_address) {
                emit_mutable_call_argument(codegen, node->data.call.arguments[i], true);
            } else {
                const char *parameter_type_name_text = NULL;
                if (target_function && i < target_function->data.function_declaration.parameter_count)
                    parameter_type_name_text = target_function->data.function_declaration.parameters[i].type_name;
                else if (call_typed_signature && i < call_typed_signature->parameter_count)
                    parameter_type_name_text = call_typed_signature->parameter_types[i];
                if (!emit_wide_integer_coerced(codegen, parameter_type_name_text, node->data.call.arguments[i]) &&
                    !emit_narrowing_cast(codegen, parameter_type_name_text, node->data.call.arguments[i], node->token.line))
                    emit_declared_value(codegen, parameter_type_name_text, node->data.call.arguments[i]);
            }
        } else if (target_function && i < parameter_count &&
                   target_function->data.function_declaration.parameters[i].default_value) {
            /* Default value */
            emit_parameter_default_value(codegen, &target_function->data.function_declaration.parameters[i]);
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
    AstNode *function_node = node->data.call.function;
    const char *name = NULL;
    if (function_node->kind == NODE_LABEL) name = function_node->data.label.value;
    else if (function_node->kind == NODE_MEMBER_EXPRESSION) name = function_node->data.member.member;
    if (!name) return NULL;

    /* A module-qualified stdlib call (channels.send(...)) is never a user
     * function. Matching one here by trailing name component against a
     * same-named struct function wraps it in the caller-arena block and
     * captures its result with __auto_type, which fails to compile when the
     * stdlib function returns void. */
    if (function_node->kind == NODE_MEMBER_EXPRESSION && function_node->data.member.object &&
        function_node->data.member.object->kind == NODE_LABEL &&
        codegen_module_imported(codegen, function_node->data.member.object->data.label.value)) {
        return NULL;
    }

    AstNode *exact = find_function(codegen, name);
    if (exact) return exact;

    size_t name_length = strlen(name);
    AstNode *match = NULL;
    for (int i = 0; i < codegen->function_count; i++) {
        AstNode *cand = codegen->all_functions[i];
        const char *registered_name = cand->data.function_declaration.name;
        size_t registered_name_length = strlen(registered_name);
        if (registered_name_length <= name_length + 1) continue;
        if (registered_name[registered_name_length - name_length - 1] != '_' || strcmp(registered_name + registered_name_length - name_length, name) != 0) continue;
        if (!match) { match = cand; continue; }
        /* Several candidates: usable only while they agree on what matters. */
        if (function_uses_caller_arena(codegen, match) != function_uses_caller_arena(codegen, cand) ||
            (match->data.function_declaration.return_type_count == 0) !=
            (cand->data.function_declaration.return_type_count == 0))
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

    int unique_id = codegen_next_id(codegen);
    emit_formatted(codegen, "({ GrayArena *_gray_csave%d = gray_default_arena; "
                            "gray_default_arena = _gray_outer_arena; ", unique_id);
    if (callee->data.function_declaration.return_type_count == 0) {
        emit_call_expression_body(codegen, node);
        emit_formatted(codegen, "; gray_default_arena = _gray_csave%d; })", unique_id);
    } else {
        emit_formatted(codegen, "__auto_type _gray_cval%d = ", unique_id);
        emit_call_expression_body(codegen, node);
        emit_formatted(codegen, "; gray_default_arena = _gray_csave%d; _gray_cval%d; })", unique_id, unique_id);
    }
}

/* --- Statement Emission --- */

static const char *extract_array_element_type(const char *type_name) {
    if (!type_name || type_name[0] != '[') return NULL;
    static char buffer[TYPE_NAME_MAX];
    size_t length = strlen(type_name);
    if (length < 3) return NULL;
    /* Dynamic array "[i64]" -> "i64" */
    /* Fixed-size "[i64,3]" -> "i64" (strip size) */
    /* Nested "[[i64]]" -> "[i64]" */
    const char *start = type_name + 1;
    const char *end_cursor = type_name + length - 1;
    /* Find the comma for fixed-size, or just strip brackets */
    for (size_t i = 1; i < length - 1; i++) {
        if (type_name[i] == ',') {
            size_t element_length = i - 1;
            memcpy(buffer, start, element_length);
            buffer[element_length] = '\0';
            return buffer;
        }
    }
    size_t element_length = (size_t)(end_cursor - start);
    memcpy(buffer, start, element_length);
    buffer[element_length] = '\0';
    return buffer;
}

/* Extract size from fixed-size array type "[i64,3]" -> 3, returns 0 if dynamic */
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

/* Emit a runtime range-check cast for a value of unknown type stored into a
 * sized integer slot (i8/i16/i32/u8/u16/u32). Returns true when a check was
 * emitted; caller should emit val normally when false. */
static bool emit_narrowing_cast(CodeGen *codegen, const char *target,
                                AstNode *value, int line) {
    if (!target) return false;
    /* A value with a Grayscale type already fits: the type checker allows
     * only a same-type or widening store. Only a value whose type it cannot
     * see — a C interop result, or an unbound generic value — is checked. */
    GrayType *value_type = type_table_get(codegen->type_table, value);
    if (value_type && value_type->kind != TYPE_KIND_UNKNOWN && value_type->kind != TYPE_KIND_C_FUNCTION) return false;
    const char *source_minimum = NULL, *source_maximum = NULL;
    bool is_unsigned = false;
    if      (strcmp(target, "i8")   == 0) { source_minimum = "-128";          source_maximum = "127"; }
    else if (strcmp(target, "i16")  == 0) { source_minimum = "-32768";        source_maximum = "32767"; }
    else if (strcmp(target, "i32")  == 0) { source_minimum = "-2147483648LL"; source_maximum = "2147483647LL"; }
    else if (strcmp(target, "u8")   == 0) { is_unsigned = true; source_maximum = "255"; }
    else if (strcmp(target, "u16")  == 0) { is_unsigned = true; source_maximum = "65535"; }
    else if (strcmp(target, "u32")  == 0) { is_unsigned = true; source_maximum = "4294967295ULL"; }
    else if ((strcmp(target, "u64") == 0) &&
             type_table_get(codegen->type_table, value) &&
             (type_table_get(codegen->type_table, value)->kind == TYPE_KIND_UNKNOWN ||
              type_table_get(codegen->type_table, value)->kind == TYPE_KIND_C_FUNCTION)) {
        /* Already 64-bit, so no upper bound can be exceeded — but a value
         * whose real signedness Grayscale can't see (an extern C-interop
         * result, typed TYPE_KIND_C_FUNCTION) may still be negative, which would
         * silently reinterpret as a huge unsigned number. gray_ucast_check's
         * negative check catches that; the max is a no-op since int64_t
         * can't exceed it. Ordinary Grayscale-typed values (TYPE_KIND_SIGNED_INTEGER, TYPE_KIND_UNSIGNED_INTEGER,
         * literals) are skipped: a legitimate uint64 value >= 2^63 has the
         * same two's-complement bit pattern as a negative int64 and would
         * otherwise trip this check on valid input. */
        is_unsigned = true; source_maximum = "18446744073709551615ULL";
    }
    else return false;

    const char *c_target = gray_type_to_c_codegen(codegen, target);
    if (codegen->is_in_const_declaration) {
        /* File-scope const: typechecker already validated the value fits;
         * emit a plain cast so C sees a compile-time constant. */
        emit_formatted(codegen, "(%s)(", c_target);
        emit_expression(codegen, value);
        emit(codegen, ")");
    } else {
        emit_formatted(codegen, "(%s)", c_target);
        emit_range_checked_narrowing(codegen, type_table_get(codegen->type_table, value), value, NULL,
                                     source_minimum, source_maximum, is_unsigned, target, line);
    }
    return true;
}

/* Emit the initializer for a fixed-size array declaration.
 * When the value is a partial array literal (count < fixed_size), a C
 * compound literal with an explicit size is used so the trailing slots are
 * zero-initialized by C semantics, and fixed_size is passed as the GrayArray
 * length so index bounds match the declared size N, not the init count k. */
static void emit_fixed_size_array_initializer(CodeGen *codegen, AstNode *value,
                                       const char *element_type_name, int fixed_size) {
    if (value && value->kind == NODE_ARRAY_VALUE &&
        value->data.array_value.count < fixed_size) {
        const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        int count = value->data.array_value.count;
        emit_formatted(codegen, "gray_array_from(gray_default_arena, (%s[%d]){", c_element_type, fixed_size);
        for (int i = 0; i < count; i++) {
            if (i > 0) emit(codegen, ", ");
            emit_expression(codegen, value->data.array_value.elements[i]);
        }
        emit_formatted(codegen, "}, sizeof(%s), %d, GRAY_ELEM_KIND_OF(%s))", c_element_type, fixed_size, c_element_type);
    } else {
        emit_expression(codegen, value);
    }
}

/* ── variable declaration sub-handlers ─────────────────────────────── */

static void emit_vardecl_array(CodeGen *codegen, AstNode *node,
                                const char *type_name, const char *element_type_spelling) {
    /* [func] with a single func ref initializer: emit as void* (function pointer),
     * not GrayArray. Array-literal inits still use GrayArray. */
    if ((strcmp(element_type_spelling, "func") == 0 || strncmp(element_type_spelling, "func(", 5) == 0) &&
        node->data.variable_declaration.value &&
        node->data.variable_declaration.value->kind == NODE_FUNCTION_REFERENCE) {
        emit_formatted(codegen, "void *%s = ", sanitize_name(node->data.variable_declaration.name));
        emit_expression(codegen, node->data.variable_declaration.value);
        emit(codegen, ";\n");
        return;
    }
    int fixed_size = extract_array_size(type_name);
    if (fixed_size > 0) {
        /* Thread the declared [T, N] type so a full-initializer literal (which
         * routes through emit_array_value) sizes its elements to the
         * annotation rather than defaulting to int64_t. */
        const char *saved_fixed_variable_type = codegen->current_variable_type;
        codegen->current_variable_type = type_name;
        /* Fixed-size array: use GrayArray but initialized with exact capacity */
        if (codegen->indent == 0) {
            /* File scope: emit uninitialized global, defer initializer to gray_init_globals */
            emit_formatted(codegen, "GrayArray %s;\n", sanitize_name(node->data.variable_declaration.name));
            /* Store deferred initializer in the initializer buffer */
            if (node->data.variable_declaration.value) {
                append_format_to_buffer(&codegen->global_initializer, "    %s = ", sanitize_name(node->data.variable_declaration.name));
                /* Temporarily redirect output to global_init buffer */
                StringBuffer saved = codegen->output;
                codegen->output = codegen->global_initializer;
                codegen->indent = 1;
                emit_fixed_size_array_initializer(codegen, node->data.variable_declaration.value, element_type_spelling, fixed_size);
                emit(codegen, ";\n");
                codegen->global_initializer = codegen->output;
                codegen->output = saved;
                codegen->indent = 0;
            }
        } else {
            emit_formatted(codegen, "GrayArray %s = ", sanitize_name(node->data.variable_declaration.name));
            if (node->data.variable_declaration.value) {
                emit_fixed_size_array_initializer(codegen, node->data.variable_declaration.value, element_type_spelling, fixed_size);
            } else {
                const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_spelling);
                emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, %d)", c_element_type, fixed_size);
            }
            emit(codegen, ";\n");
        }
        codegen->current_variable_type = saved_fixed_variable_type;
        return;
    }

    if (is_nested_array_type(type_name)) {
        const char *saved_nested_variable_type = codegen->current_variable_type;
        codegen->current_variable_type = type_name;
        AstNode *initializer = node->data.variable_declaration.value;
        bool is_label_initializer = names_existing_storage(initializer);
        const char *label_element_type_name = NULL;
        if (is_label_initializer) {
            GrayType *source_type = type_table_get(codegen->type_table, initializer);
            if (source_type && source_type->kind == TYPE_KIND_ARRAY) {
                label_element_type_name = source_type->element_type;
            }
        }
        if (codegen->indent == 0) {
            emit_formatted(codegen, "GrayArray %s;\n", sanitize_name(node->data.variable_declaration.name));
            StringBuffer saved = codegen->output; codegen->output = codegen->global_initializer; codegen->indent = 1;
            emit_formatted(codegen, "    %s = ", sanitize_name(node->data.variable_declaration.name));
            if (is_label_initializer) emit_deep_array_copy(codegen, initializer, label_element_type_name);
            else if (initializer) emit_expression(codegen, initializer);
            else emit(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, GrayArray, 4)");
            emit(codegen, ";\n");
            codegen->global_initializer = codegen->output; codegen->output = saved; codegen->indent = 0;
        } else {
            emit_formatted(codegen, "GrayArray %s = ", sanitize_name(node->data.variable_declaration.name));
            if (is_label_initializer) emit_deep_array_copy(codegen, initializer, label_element_type_name);
            else if (initializer) emit_expression(codegen, initializer);
            else emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, GrayArray, 4)");
            emit(codegen, ";\n");
        }
        codegen->current_variable_type = saved_nested_variable_type;
        return;
    }

    /* Dynamic array: use GrayArray */
    const char *c_element_type = gray_type_to_c_codegen(codegen, element_type_spelling);
    if (codegen->indent == 0) {
        emit_formatted(codegen, "GrayArray %s;\n", sanitize_name(node->data.variable_declaration.name));
        StringBuffer saved = codegen->output; codegen->output = codegen->global_initializer; codegen->indent = 1;
        emit_formatted(codegen, "    %s = ", sanitize_name(node->data.variable_declaration.name));
        if (node->data.variable_declaration.value &&
            node->data.variable_declaration.value->kind == NODE_ARRAY_VALUE &&
            node->data.variable_declaration.value->data.array_value.count == 0) {
            /* Empty array literal with type annotation; use the declared elem size. */
            emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", c_element_type);
        } else if (node->data.variable_declaration.value) {
            const char *saved_variable_type = codegen->current_variable_type;
            codegen->current_variable_type = type_name;
            emit_expression(codegen, node->data.variable_declaration.value);
            codegen->current_variable_type = saved_variable_type;
        } else {
            emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", c_element_type);
        }
        emit(codegen, ";\n");
        codegen->global_initializer = codegen->output; codegen->output = saved; codegen->indent = 0;
    } else {
    emit_formatted(codegen, "GrayArray %s = ", sanitize_name(node->data.variable_declaration.name));
    if (node->data.variable_declaration.value &&
        node->data.variable_declaration.value->kind == NODE_ARRAY_VALUE &&
        node->data.variable_declaration.value->data.array_value.count == 0) {
        /* Empty array literal with type annotation; use correct elem size */
        emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", c_element_type);
    } else if (names_existing_storage(node->data.variable_declaration.value)) {
        /* Copy-by-default: deep copy when assigning from another variable,
         * a struct field, or a container element (e.g. `mut copy [i64] = s.field`).
         * Without this, member-expr sources share backing storage with the
         * originating struct field (#1789). */
        GrayType *source_type = type_table_get(codegen->type_table, node->data.variable_declaration.value);
        const char *element_type_name = (source_type && source_type->kind == TYPE_KIND_ARRAY)
            ? source_type->element_type : NULL;
        emit_deep_array_copy(codegen, node->data.variable_declaration.value, element_type_name);
    } else if (node->data.variable_declaration.value) {
        /* Thread the declared array type so the array-literal codegen
         * can infer the element type when the typetable misses the
         * first element (e.g. module-qualified struct function calls). */
        const char *saved_variable_type = codegen->current_variable_type;
        codegen->current_variable_type = type_name;
        emit_expression(codegen, node->data.variable_declaration.value);
        codegen->current_variable_type = saved_variable_type;
    } else {
        emit_formatted(codegen, "GRAY_ARRAY_NEW_OF(gray_default_arena, %s, 4)", c_element_type);
    }
    emit(codegen, ";\n");
    } /* end else (indent > 0) */
}

static void emit_vardecl_map(CodeGen *codegen, AstNode *node,
                              const char *type_name) {
    /* Parse K:V from type string to determine C types */
    GrayType *map_type = type_from_name(type_name);
    const char *c_key_type = "GrayString";
    const char *c_value_type_default = "int64_t";
    if (map_type && map_type->key_type) c_key_type = gray_map_element_c_type(codegen, map_type->key_type);
    if (map_type && map_type->value_type) c_value_type_default = gray_map_element_c_type(codegen, map_type->value_type);

    if (codegen->indent == 0) {
        /* File scope: emit zero-init global, defer initializer to
         * gray_init_globals — map literals expand to GCC statement
         * expressions which are not legal at file scope. */
        emit_formatted(codegen, "GrayMap %s;\n", sanitize_name(node->data.variable_declaration.name));
        StringBuffer saved = codegen->output; codegen->output = codegen->global_initializer; codegen->indent = 1;
        emit_formatted(codegen, "    %s = ", sanitize_name(node->data.variable_declaration.name));
        if (names_existing_storage(node->data.variable_declaration.value)) {
            int unique_id = codegen_next_id(codegen);
            char source_variable[VARIABLE_NAME_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_ms%d", unique_id);
            emit_formatted(codegen, "({ GrayMap %s = ", source_variable);
            emit_expression(codegen, node->data.variable_declaration.value);
            emit(codegen, "; ");
            emit_value_deep_copy(codegen, type_name, source_variable);
            emit(codegen, "; })");
        } else if (node->data.variable_declaration.value) {
            const char *saved_variable_type = codegen->current_variable_type;
            codegen->current_variable_type = type_name;
            emit_expression(codegen, node->data.variable_declaration.value);
            codegen->current_variable_type = saved_variable_type;
        } else {
            emit_formatted(codegen, "GRAY_MAP_NEW_OF(gray_default_arena, %s, %s, 8)",
                c_key_type, c_value_type_default);
        }
        emit(codegen, ";\n");
        codegen->global_initializer = codegen->output; codegen->output = saved; codegen->indent = 0;
        return;
    }

    emit_formatted(codegen, "GrayMap %s = ", sanitize_name(node->data.variable_declaration.name));
    if (names_existing_storage(node->data.variable_declaration.value)) {
        /* Copy-by-default: deep copy when assigning a map from another
         * variable or element so mutations to the copy don't alias the original. */
        int unique_id = codegen_next_id(codegen);
        char source_variable[VARIABLE_NAME_BUFFER_SIZE];
        snprintf(source_variable, sizeof(source_variable), "_ms%d", unique_id);
        emit_formatted(codegen, "({ GrayMap %s = ", source_variable);
        emit_expression(codegen, node->data.variable_declaration.value);
        emit(codegen, "; ");
        emit_value_deep_copy(codegen, type_name, source_variable);
        emit(codegen, "; })");
    } else if (node->data.variable_declaration.value) {
        const char *saved_variable_type = codegen->current_variable_type;
        codegen->current_variable_type = type_name;
        emit_expression(codegen, node->data.variable_declaration.value);
        codegen->current_variable_type = saved_variable_type;
    } else {
        /* No initializer; create empty map */
        emit_formatted(codegen, "GRAY_MAP_NEW_OF(gray_default_arena, %s, %s, 8)",
            c_key_type, c_value_type_default);
    }
    emit(codegen, ";\n");
}

/* Emit the C zero value for c_type (no leading " = "). Used both for
 * value-less declarations and for file-scope globals whose real
 * initializer is deferred into the global-init buffer. */
/* gray_type_name is the Grayscale type as written (e.g. "Inner"), needed
 * only to recurse into emit_struct_zero_value_literal when c_type doesn't
 * match any of the special-cased primitives below — a bare `mut x Inner`
 * with no initializer would otherwise fall through to a flat C {0}, which
 * drops any fixed-size array field inside Inner to length 0 instead of its
 * declared N, the same gap an omitted nested-struct field has.
 * force_constant is true for the file-scope deferred-init placeholder: that
 * value must be a pure C compile-time constant (the real, possibly
 * non-constant, initializer runs later in gray_init_globals), so struct
 * types fall back to a flat {0} there instead of recursing into
 * emit_struct_zero_value_literal, which can emit gray_array_new()/
 * gray_map_new_kind() runtime calls for array/map fields. */
static void emit_c_zero_value(CodeGen *codegen, const char *c_type, const char *gray_type_name, bool force_constant) {
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
    else {
        GrayType *zero_value_type = gray_type_name ? type_from_name(gray_type_name) : NULL;
        if (zero_value_type && zero_value_type->kind == TYPE_KIND_STRUCT && !force_constant) emit_struct_zero_value_literal(codegen, gray_type_name, 0);
        else emit(codegen, "{0}");
    }
}

/* True when a scalar/struct variable initializer lowers to a C constant
 * expression and is therefore legal at file scope. Anything else
 * (runtime-checked negation, struct literals with array/map fields,
 * string interpolation, calls, references to other globals) must be
 * deferred into the global-init buffer, matching emit_vardecl_array()
 * and emit_vardecl_map(). */
static bool initializer_is_c_constant(AstNode *value) {
    if (!value) return true;
    /* A folded number literal is emitted as a constant of its type. */
    if (value->folded_literal) return true;
    switch (value->kind) {
        case NODE_BOOL_VALUE:
        case NODE_CHAR_VALUE:
        case NODE_FLOATING_POINT_LITERAL:
        case NODE_STRING_VALUE:
        case NODE_NIL_VALUE:
            return true;
        default:
            return false;
    }
}

static void emit_variable_declaration_initializer(CodeGen *codegen, AstNode *node,
                               const char *c_type, const char *type_name) {
    if (node->data.variable_declaration.value) {
        emit(codegen, " = ");
        codegen->current_variable_name = node->data.variable_declaration.name;
        codegen->current_variable_type = node->data.variable_declaration.type_name;
        /* Signal to emit_expression that we are inside a file-scope const
         * initializer.  Overflow-check wrappers (gray_add_check etc.) are
         * runtime function calls; C rejects them as file-scope initializers.
         * The typechecker has already verified no overflow for such exprs.
         * A module-level mut whose initializer is a C constant is emitted
         * inline the same way, and its literal is range-checked (E3036). */
        bool was_in_const_declaration = codegen->is_in_const_declaration;
        if (codegen->indent == 0 &&
            (!node->data.variable_declaration.is_mutable || initializer_is_c_constant(node->data.variable_declaration.value)))
            codegen->is_in_const_declaration = true;
        if (type_name && type_name[0] == '^' &&
                   node->data.variable_declaration.value->kind == NODE_LABEL &&
                   is_reference_variable(codegen, node->data.variable_declaration.value->data.label.value)) {
            /* Assigning a ref variable to a ^T pointer; pass the pointer through
             * without auto-dereferencing */
            emit(codegen, node->data.variable_declaration.value->data.label.value);
        } else if (node->data.variable_declaration.value->kind == NODE_CALL_EXPRESSION &&
                   node->data.variable_declaration.value->data.call.function->kind == NODE_LABEL &&
                   strcmp(node->data.variable_declaration.value->data.call.function->data.label.value, "addr") == 0 &&
                   type_name && (strcmp(type_name, "u64") == 0 || strcmp(type_name, "i64") == 0)) {
            /* addr() assigned to integer type; cast pointer to uintptr_t */
            emit(codegen, "(uintptr_t)");
            emit_expression(codegen, node->data.variable_declaration.value);
        } else if ((node->data.variable_declaration.value->kind == NODE_LABEL &&
                    type_name && type_needs_deep_copy(codegen, type_name)) ||
                   composite_value_aliases(codegen, type_name, node->data.variable_declaration.value)) {
            /* Copy-by-default: deep copy structs (and maps) that contain
             * arrays/maps/strings so the copy is fully independent. */
            int unique_id = codegen_next_id(codegen);
            char source_variable[VARIABLE_NAME_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_vdc%d", unique_id);
            emit_formatted(codegen, "({ %s %s = ", c_type, source_variable);
            emit_expression(codegen, node->data.variable_declaration.value);
            emit(codegen, "; ");
            emit_value_deep_copy(codegen, type_name, source_variable);
            emit(codegen, "; })");
        } else if (type_name && is_wide_integer_type_name(type_name) &&
                   !resolve_wide_integer_type(codegen, node->data.variable_declaration.value)) {
            /* Scalar variable/expression assigned to a wide integer type.
             * Wrap with from_i64/from_u64 so the C assignment is valid.
             * Covers: mut big i128 = some_int_var */
            emit_scalar_to_wide_integer(codegen, type_name, node->data.variable_declaration.value, NULL);
        } else if (!emit_narrowing_cast(codegen, type_name, node->data.variable_declaration.value, node->token.line)) {
            emit_expression(codegen, node->data.variable_declaration.value);
        }
        codegen->is_in_const_declaration = was_in_const_declaration;
        codegen->current_variable_name = NULL;
        codegen->current_variable_type = NULL;
    } else {
        /* Zero-initialize when no value is provided */
        emit(codegen, " = ");
        emit_c_zero_value(codegen, c_type, type_name, false);
    }

    emit(codegen, ";\n");
}

/* source_name is the variable's name as written in Grayscale — the key the
 * raw/ref/heap-pointer trackers use, since reference sites look them up by
 * the source name. node->data.variable_declaration.name may already carry a mangled or
 * gray_g_-prefixed C name by the time this runs. */
/* An inferred declaration bound to existing composite storage copies like an
 * annotated one: name its type from the typetable so the copy path applies. */
static bool inferred_composite_type(CodeGen *codegen, AstNode *value, char *output, size_t capacity) {
    if (!names_existing_storage(value) || !codegen->type_table) return false;
    GrayType *type = type_table_get(codegen->type_table, value);
    if (!type || (type->kind != TYPE_KIND_ARRAY && type->kind != TYPE_KIND_MAP && type->kind != TYPE_KIND_STRUCT)) return false;
    snprintf(output, capacity, "%s", type_name(type));
    return type_shares_storage(codegen, output);
}

static void emit_variable_declaration(CodeGen *codegen, AstNode *node,
                                      const char *source_name) {
    emit_indent(codegen);

    char inferred_type_name[TYPE_NAME_MAX];
    const char *type_name = node->data.variable_declaration.type_name;
    if (!type_name && inferred_composite_type(codegen, node->data.variable_declaration.value,
                                              inferred_type_name, sizeof(inferred_type_name))) {
        type_name = inferred_type_name;
    }
    const char *element_type_spelling = extract_array_element_type(type_name);

    if (element_type_spelling) {
        emit_vardecl_array(codegen, node, type_name, element_type_spelling);
        return;
    }

    /* Map type: map[K:V] */
    if (type_name && strncmp(type_name, "map[", 4) == 0) {
        emit_vardecl_map(codegen, node, type_name);
        return;
    }

    const char *c_type = gray_type_to_c_codegen(codegen, type_name);

    /* Register wide integer variable for type tracking */
    if (type_name && is_wide_integer_type_name(type_name)) {
        register_wide_integer_variable(codegen, node->data.variable_declaration.name, type_name);
    } else if (!type_name && node->data.variable_declaration.value) {
        /* Inferred-type var (e.g. `mut b = copy(a)`): consult the typetable
         * so wide-integer types propagate through copy(), function calls,
         * member access, etc. Without this the var is silently treated as
         * i64 and downstream uses (println, arithmetic) emit the wrong
         * runtime calls. Also try resolve_wide_integer_type() so constructor
         * calls like `mut a = i128(42)` register correctly when the
         * typetable stores the base type name ("i64") rather than the
         * width-specific name ("i128"). */
        GrayType *value_type = type_table_get(codegen->type_table, node->data.variable_declaration.value);
        /* An array/map type stores its element type name in ->name (e.g. an
         * inferred [i128] has name "i128"), so gate on the kind too — the var
         * itself is the container, not a wide integer. */
        bool is_value_wide_integer_scalar = value_type && value_type->name && is_wide_integer_type_name(value_type->name) &&
            (value_type->kind == TYPE_KIND_SIGNED_INTEGER || value_type->kind == TYPE_KIND_UNSIGNED_INTEGER);
        const char *wide_integer_name = is_value_wide_integer_scalar
            ? value_type->name : resolve_wide_integer_type(codegen, node->data.variable_declaration.value);
        if (wide_integer_name) {
            register_wide_integer_variable(codegen, node->data.variable_declaration.name, wide_integer_name);
        }
    }

    /* Skip blank identifiers (_) */
    if (strcmp(node->data.variable_declaration.name, "_") == 0) {
        if (node->data.variable_declaration.value) {
            emit_indent(codegen);
            emit(codegen, "(void)(");
            emit_expression(codegen, node->data.variable_declaration.value);
            emit(codegen, ");\n");
        }
        return;
    }

    /* If no type annotation, try to infer from value */
    if (!type_name && node->data.variable_declaration.value) {
        AstNode *value = node->data.variable_declaration.value;
        if (value->kind == NODE_STRING_VALUE || value->kind == NODE_INTERPOLATED_STRING) {
            c_type = "GrayString";
        } else if (value->kind == NODE_FLOATING_POINT_LITERAL) {
            c_type = "double";
        } else if (value->kind == NODE_BOOL_VALUE) {
            c_type = "bool";
        } else if (value->kind == NODE_ARRAY_VALUE) {
            c_type = "GrayArray";
        } else if (value->kind == NODE_MAP_VALUE) {
            c_type = "GrayMap";
        } else if (value->kind == NODE_STRUCT_VALUE) {
            /* use mangled name for generic struct instantiations */
            if (value->data.struct_value.wildcard_binding) {
                const char *binding = value->data.struct_value.wildcard_binding;
                const char *base = value->data.struct_value.name;
                static char struct_value_buffer[MESSAGE_BUFFER_SIZE];
                size_t string_position = snprintf(struct_value_buffer, sizeof(struct_value_buffer), "%s__", base);
                for (const char *cursor = binding; *cursor && string_position < sizeof(struct_value_buffer) - 1; cursor++)
                    struct_value_buffer[string_position++] = (isalnum((unsigned char)*cursor) || *cursor == '_') ? *cursor : '_';
                struct_value_buffer[string_position] = '\0';
                c_type = gray_type_to_c_codegen(codegen, struct_value_buffer);
            } else {
                c_type = gray_type_to_c_codegen(codegen, value->data.struct_value.name);
            }
        } else if (value->kind == NODE_INFIX_EXPRESSION) {
            /* Check type table for infix result type */
            GrayType *infix_type = type_table_get(codegen->type_table, value);
            if (infix_type && infix_type->kind == TYPE_KIND_STRING) {
                c_type = "GrayString";
            } else if (infix_type && infix_type->kind == TYPE_KIND_FLOATING_POINT) {
                c_type = "double";
            } else if (infix_type && infix_type->kind == TYPE_KIND_BOOL) {
                c_type = "bool";
            } else {
                c_type = "__auto_type";
            }
        } else if (value->kind == NODE_CALL_EXPRESSION || value->kind == NODE_NEW_EXPRESSION ||
                   value->kind == NODE_MEMBER_EXPRESSION || value->kind == NODE_INDEX_EXPRESSION ||
                   value->kind == NODE_POSTFIX_EXPRESSION) {
            /* Use __auto_type for function calls, new(), member access, index,
             * and postfix expressions (e.g. ptr^ dereference — without this,
             * `mut x = p^` where p is ^StructType would be emitted as int64_t
             * instead of the correct struct type). */
            c_type = "__auto_type";
        } else if (value->kind == NODE_FUNCTION_REFERENCE) {
            /* Function reference; use __auto_type to capture the pointer type */
            c_type = "__auto_type";
        } else if (value->kind == NODE_LABEL) {
            /* Variable reference; use __auto_type to propagate the source type */
            c_type = "__auto_type";
        } else if (value->kind == NODE_CAST_EXPRESSION) {
            /* Cast expression; use the target type */
            c_type = gray_type_to_c_codegen(codegen, value->data.cast.target_type);
        }
    }

    /* Detect ref() assignment; register as transparent reference (but not for function refs) */
    if (node->data.variable_declaration.value && node->data.variable_declaration.value->kind == NODE_CALL_EXPRESSION) {
        AstNode *function_node = node->data.variable_declaration.value->data.call.function;
        if (function_node->kind == NODE_LABEL && strcmp(function_node->data.label.value, "ref") == 0) {
            /* Register as a transparent reference for any assignable source —
             * variable, struct field, or index expression. Without this,
             * ref(struct.field) was registered as a plain value var and
             * later GRAY_ARRAY_SET(&(r), ...) produced GrayArray ** instead of
             * GrayArray *. The auto-deref path in NODE_LABEL emission
             * handles field/index sources the same as variable sources. */
            if (node->data.variable_declaration.value->data.call.argument_count == 1) {
                AstNode *argument = node->data.variable_declaration.value->data.call.arguments[0];
                bool is_assignable =
                    (argument->kind == NODE_LABEL && !find_referenced_function(codegen, argument)) ||
                    argument->kind == NODE_MEMBER_EXPRESSION ||
                    argument->kind == NODE_INDEX_EXPRESSION;
                if (is_assignable) {
                    register_reference_variable(codegen, source_name);
                }
            }
        }
    }

    /* Detect raw() assignment; register as raw pointer (nil check skipped on deref) */
    bool is_raw_initializer = false;
    if (node->data.variable_declaration.value && node->data.variable_declaration.value->kind == NODE_CALL_EXPRESSION) {
        AstNode *called_function = node->data.variable_declaration.value->data.call.function;
        if (called_function->kind == NODE_LABEL && strcmp(called_function->data.label.value, "raw") == 0) {
            register_raw_variable(codegen, source_name);
            is_raw_initializer = true;
        }
    }
    /* If a pointer variable shadows a raw variable from an outer scope,
     * push a non-raw override so inner dereferences get nil checks. */
    if (!is_raw_initializer && type_name && type_name[0] == '^' &&
        is_raw_variable(codegen, source_name)) {
        unregister_raw_variable(codegen, source_name);
    }

    /* Detect new() assignment; register as heap-tracked pointer so later
     * field container reassignment through it targets gray_heap_arena.
     * Any other declaration clears a shadowed outer-scope heap variable
     * of the same name. */
    if (is_new_call(node->data.variable_declaration.value)) {
        register_heap_variable(codegen, source_name, true);
    } else if (is_heap_variable(codegen, source_name)) {
        register_heap_variable(codegen, source_name, false);
    }

    /* Detect mem.init()/mem.alloc() assignment; register the pointer as
     * arena-tracked so a later dereference of this variable catches a use
     * after that arena was destroyed/reset via a path the compile-time
     * pointer checker couldn't trace (STANDARD 11.7). Only when the arena
     * argument is safe to re-evaluate a second time at the deref site. */
    if (node->data.variable_declaration.value && node->data.variable_declaration.value->kind == NODE_CALL_EXPRESSION) {
        const char *mem_module = NULL, *mem_function = NULL;
        if (is_stdlib_call(node->data.variable_declaration.value, &mem_module, &mem_function) &&
            mem_module && strcmp(mem_module, "mem") == 0 &&
            (strcmp(mem_function, "init") == 0 || strcmp(mem_function, "alloc") == 0) &&
            node->data.variable_declaration.value->data.call.argument_count >= 1) {
            AstNode *arena_argument = node->data.variable_declaration.value->data.call.arguments[0];
            if (is_stable_arena_expression(arena_argument)) {
                register_mem_variable(codegen, source_name, arena_argument);
            } else {
                unregister_mem_variable(codegen, source_name);
            }
        } else if (type_name && type_name[0] == '^' && is_mem_tracked_variable(codegen, source_name)) {
            /* Re-declared from a non-mem source; clear shadowed tracking. */
            unregister_mem_variable(codegen, source_name);
        }
    }

    /* File-scope initializer that isn't a C constant expression: emit a
     * zero-initialized global and defer the real initializer into the
     * global-init buffer, the same way emit_vardecl_array/map do. C
     * rejects non-constant file-scope initializers (runtime-checked
     * negation, struct literals with array/map fields, string
     * interpolation, ...). __auto_type needs its initializer inline, so
     * leave those on the normal path. A value-less struct declaration
     * defers too: its real zero value (emit_c_zero_value's recursive,
     * non-force_constant form) can itself emit gray_array_new()/
     * gray_map_new_kind() calls for array/map fields with no default, which
     * are just as non-constant as an explicit struct-literal initializer. */
    GrayType *declared_struct_type = type_name ? type_from_name(type_name) : NULL;
    bool declaration_needs_deferral = node->data.variable_declaration.value
        ? !initializer_is_c_constant(node->data.variable_declaration.value)
        : (declared_struct_type && declared_struct_type->kind == TYPE_KIND_STRUCT);
    if (codegen->indent == 0 && strcmp(c_type, "__auto_type") != 0 &&
        declaration_needs_deferral) {
        emit_formatted(codegen, "%s %s = ", c_type, sanitize_name(node->data.variable_declaration.name));
        emit_c_zero_value(codegen, c_type, type_name, true);
        emit(codegen, ";\n");
        StringBuffer saved = codegen->output;
        codegen->output = codegen->global_initializer;
        codegen->indent = 1;
        emit_formatted(codegen, "    %s", sanitize_name(node->data.variable_declaration.name));
        emit_variable_declaration_initializer(codegen, node, c_type, type_name);
        codegen->global_initializer = codegen->output;
        codegen->output = saved;
        codegen->indent = 0;
        return;
    }

    if (!node->data.variable_declaration.is_mutable && type_name && type_name[0] == '^') {
        /* const pointer: T * const p — the pointer is immutable, not the
         * pointed-to data.  Placing const before the type would produce
         * const T * p (pointer to const T), which incorrectly propagates
         * the const qualifier through dereferences and field accesses. */
        emit_formatted(codegen, "%s const %s", c_type, sanitize_name(node->data.variable_declaration.name));
    } else {
        /* Non-pointer const-declared variables are deliberately NOT emitted
         * as C `const`. Grayscale enforces write-through-pointer protection
         * for const sources at the typechecker level (E3122), and raw()
         * deliberately bypasses that protection (STANDARD 3.1.7). Writing
         * through a pointer cast away from a C `const` object is undefined
         * behavior — an optimizing compiler is free to assume the object
         * never changes and fold reads of it, which is exactly what -O2
         * does to a `const` global: the raw()-write silently has no visible
         * effect instead of taking effect as documented. Leaving the C
         * storage mutable keeps raw()'s write well-defined; Grayscale's own
         * const protection is already enforced without help from C's. */
        emit_formatted(codegen, "%s %s", c_type, sanitize_name(node->data.variable_declaration.name));
    }

    emit_variable_declaration_initializer(codegen, node, c_type, type_name);
}

/* True when field_t owns arena-backed memory that must be re-homed to
 * gray_heap_arena rather than the enclosing function's own scoped arena
 * when reassigned through a pointer known to point into gray_heap_arena. */
static bool field_type_needs_arena_escape(GrayType *field_type) {
    return field_type && (field_type->kind == TYPE_KIND_MAP || field_type->kind == TYPE_KIND_ARRAY ||
                        field_type->kind == TYPE_KIND_STRING || field_type->kind == TYPE_KIND_STRUCT);
}

/* Emit `ref = value;` with gray_default_arena swapped to gray_heap_arena for
 * the duration of evaluating value, so any container the value allocates
 * (map/array/string/struct literal) lives as long as the heap-allocated
 * struct it's being attached to, rather than the current function's own
 * scoped arena that gets destroyed when the function returns. */
static void emit_heap_escaped_field_assign(CodeGen *codegen, AstNode *node, const char *reference_text) {
    emit_formatted(codegen, "{ GrayArena *_esc_h = gray_default_arena; gray_default_arena = gray_heap_arena; %s = ", reference_text);
    emit_expression(codegen, node->data.assign.value);
    emit(codegen, "; gray_default_arena = _esc_h; }");
}

/* The element store shared by every arr[i] = v target once the caller has
 * bound the array: `GRAY_ARRAY_SET_AT(<array_ref>, ...)` with the value for
 * '=' or a string '+=' concat, closing the caller's `{` block. array_ref is
 * the C lvalue of the array. */
static void emit_array_element_store(CodeGen *codegen, AstNode *node, GrayType *left_type,
                                     const char *c_element_type, bool is_compound, const char *array_reference) {
    AstNode *index_node = node->data.assign.target->data.index_expression.index;
    TokenType assign_operator = node->data.assign.operator;
    emit_formatted(codegen, "GRAY_ARRAY_SET_AT(%s, %s, ", array_reference, c_element_type);
    emit_expression(codegen, index_node);
    emit(codegen, ", ");
    if (is_compound && strcmp(c_element_type, "GrayString") == 0 && assign_operator == TOKEN_PLUS_ASSIGN) {
        /* The concat result must outlive the loop iteration that
         * produced it — inside a nested loop that means the outer
         * arena, not the per-iteration gray_default_arena. */
        emit_formatted(codegen, "gray_string_concat(%s, GRAY_ARRAY_GET_AT(%s, GrayString, ",
            codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena", array_reference);
        emit_expression(codegen, index_node);
        emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, ")");
    } else {
        emit_composite_operand(codegen, left_type->element_type, node->data.assign.value);
    }
    emit_formatted(codegen, ", \"%s\", %d); }\n", codegen->file, node->token.line);
}

/* arr[i] = v and compound forms; `left` is the indexed array expression. */
static void emit_array_index_assign(CodeGen *codegen, AstNode *node, AstNode *left, GrayType *left_type) {
    const char *c_element_type = "int64_t";
    if (left_type->element_type) {
        if (strcmp(left_type->element_type, "func") == 0 || strncmp(left_type->element_type, "func(", 5) == 0) {
            c_element_type = "void *";
        } else {
            c_element_type = gray_type_to_c_codegen(codegen, left_type->element_type);
        }
    }
    /* A number element's compound assignment is emitted by
     * emit_compound_arithmetic; a string element's += reaches here. */
    bool is_compound = node->data.assign.operator == TOKEN_PLUS_ASSIGN;
    /* m[key][i] = v: the map lookup lowers to a statement-expression
     * that yields the stored GrayArray by rvalue, so GRAY_ARRAY_SET_AT's
     * &(arr) is invalid. Bind it to a temp — the GrayArray header is a
     * view over the stored buffer, so element writes still land there. */
    if (index_left_is_map_lookup(codegen, left)) {
        emit_formatted(codegen, "{ GrayArray _ea = ");
        emit_expression(codegen, left);
        emit(codegen, "; ");
        emit_array_element_store(codegen, node, left_type, c_element_type, is_compound, "_ea");
        return;
    }
    /* Check for array field through struct pointer (rvalue assignability issue).
     * b.items[i] = val where b: ^Bag — the normal member emit produces a
     * GCC statement expression (rvalue); GRAY_ARRAY_SET's &(arr) would fail.
     * Inline the nil check and use _dp->field directly as an assignable target. */
    {
        AstNode *struct_pointer = NULL;
        const char *array_field_name = NULL;
        if (left->kind == NODE_MEMBER_EXPRESSION) {
            AstNode *member_object = left->data.member.object;
            GrayType *member_object_type = type_table_get(codegen->type_table, member_object);
            if (member_object_type && member_object_type->kind == TYPE_KIND_POINTER) {
                struct_pointer = member_object;
                array_field_name = left->data.member.member;
            } else if (member_object->kind == NODE_POSTFIX_EXPRESSION &&
                       member_object->data.postfix.operator == TOKEN_CARET) {
                struct_pointer = member_object->data.postfix.left;
                array_field_name = left->data.member.member;
            }
        }
        if (struct_pointer) {
            bool struct_pointer_is_raw = (struct_pointer->kind == NODE_LABEL && is_raw_variable(codegen, struct_pointer->data.label.value));
            int temporary_id = codegen_next_id(codegen);
            emit_formatted(codegen, "{ __auto_type _asdp%d = ", temporary_id);
            emit_expression(codegen, struct_pointer);
            emit(codegen, "; ");
            if (!struct_pointer_is_raw) {
                emit_formatted(codegen, "if (!_asdp%d) { %s; } ",
                      temporary_id, panic_call(codegen, node, "P0080", ""));
            }
            char array_reference[MESSAGE_BUFFER_SIZE];
            snprintf(array_reference, sizeof(array_reference), "_asdp%d->%s", temporary_id, sanitize_name(array_field_name));
            emit_array_element_store(codegen, node, left_type, c_element_type, is_compound, array_reference);
            return;
        }
    }
    /* p^[i] = v: direct dereference of array pointer */
    if (left->kind == NODE_POSTFIX_EXPRESSION && left->data.postfix.operator == TOKEN_CARET) {
        AstNode *array_pointer = left->data.postfix.left;
        bool array_pointer_is_raw = (array_pointer->kind == NODE_LABEL && is_raw_variable(codegen, array_pointer->data.label.value));
        int temporary_id = codegen_next_id(codegen);
        emit_formatted(codegen, "{ __auto_type _asdp%d = ", temporary_id);
        emit_expression(codegen, array_pointer);
        emit(codegen, "; ");
        if (!array_pointer_is_raw) {
            emit_formatted(codegen, "if (!_asdp%d) { %s; } ",
                  temporary_id, panic_call(codegen, node, "P0080", ""));
        }
        char array_reference[MESSAGE_BUFFER_SIZE];
        snprintf(array_reference, sizeof(array_reference), "*_asdp%d", temporary_id);
        emit_array_element_store(codegen, node, left_type, c_element_type, is_compound, array_reference);
        return;
    }
    /* Plain assignment of a string to a string array element inside a
     * loop: the RHS (a concat, a call return, ...) may live in the
     * per-iteration arena, which is torn down before the array is read
     * again. Deep-copy into the outer arena — the same escape the
     * plain-variable and += paths use. */
    if (!is_compound && strcmp(c_element_type, "GrayString") == 0 &&
        codegen->loop_scope_depth > 0) {
        emit(codegen, "{ GrayString _esc_v = ");
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, "; GRAY_ARRAY_SET_AT(");
        emit_expression(codegen, left);
        emit(codegen, ", GrayString, ");
        emit_expression(codegen, node->data.assign.target->data.index_expression.index);
        emit_formatted(codegen, ", gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len), \"%s\", %d); }\n",
            codegen->file, node->token.line);
        return;
    }
    emit_formatted(codegen, "GRAY_ARRAY_SET_AT(");
    emit_expression(codegen, left);
    emit_formatted(codegen, ", %s, ", c_element_type);
    emit_expression(codegen, node->data.assign.target->data.index_expression.index);
    emit(codegen, ", ");
    /* String element +=: concatenate onto the current element */
    if (is_compound && strcmp(c_element_type, "GrayString") == 0) {
        emit_formatted(codegen, "gray_string_concat(%s, GRAY_ARRAY_GET_AT(",
            codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena");
        emit_expression(codegen, left);
        emit(codegen, ", GrayString, ");
        emit_expression(codegen, node->data.assign.target->data.index_expression.index);
        emit_formatted(codegen, ", \"%s\", %d), ", codegen->file, node->token.line);
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, ")");
    } else {
        emit_composite_operand(codegen, left_type->element_type, node->data.assign.value);
    }
    emit_formatted(codegen, ", \"%s\", %d);\n", codegen->file, node->token.line);
}

/* m[key] = v and compound forms; `left` is the indexed map expression. */
static void emit_map_index_assign(CodeGen *codegen, AstNode *node, AstNode *left, GrayType *left_type) {
    /* Map key assignment: gray_map_set(arena, &m, &key, &value)
     * We need &m (address of the map), but the map expression may
     * be an rvalue (e.g. pointer-deref field access via GCC statement
     * expression). Check whether the map lives behind a pointer and
     * use arrow syntax to get an assignable target if so, otherwise emit
     * directly. */
    const char *c_value_type = "int64_t";
    if (left_type->value_type) c_value_type = gray_map_element_c_type(codegen, left_type->value_type);
    const char *c_key_type = "GrayString";
    if (left_type->key_type) c_key_type = gray_map_element_c_type(codegen, left_type->key_type);
    const char *ms_arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
    bool has_string_key = left_type->key_type && strcmp(left_type->key_type, "string") == 0;
    bool has_string_value = left_type->value_type && strcmp(left_type->value_type, "string") == 0;

    /* Detect pointer-to-struct field access: left is a MEMBER_EXPR
     * whose object is a pointer type (`p.field`) or an explicit
     * dereference of one (`p^.field`). In that case the GCC statement
     * expression for nil-checked deref yields an rvalue and &(rvalue)
     * is illegal. Instead, nil-check then use -> to get an assignable
     * target. map_pointer_object is the pointer to check and arrow through,
     * which for the `p^.field` spelling is the operand of the `^`. */
    bool is_map_via_pointer = false;
    bool map_raw = false;
    AstNode *map_pointer_object = NULL;
    if (left->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *object = left->data.member.object;
        GrayType *object_type = type_table_get(codegen->type_table, object);
        if (object_type && object_type->kind == TYPE_KIND_POINTER) {
            map_pointer_object = object;
        } else if (object->kind == NODE_POSTFIX_EXPRESSION && object->data.postfix.operator == TOKEN_CARET) {
            map_pointer_object = object->data.postfix.left;
        }
        if (map_pointer_object) {
            is_map_via_pointer = true;
            map_raw = (map_pointer_object->kind == NODE_LABEL &&
                is_raw_variable(codegen, map_pointer_object->data.label.value));
        }
    }
    /* p^["key"] = v: direct dereference of map pointer. The pointer
     * is already a GrayMap*, so nil-check and pass it directly. */
    bool map_direct_deref = false;
    bool map_deref_raw = false;
    if (!is_map_via_pointer && left->kind == NODE_POSTFIX_EXPRESSION && left->data.postfix.operator == TOKEN_CARET) {
        map_direct_deref = true;
        map_deref_raw = (left->data.postfix.left->kind == NODE_LABEL &&
            is_raw_variable(codegen, left->data.postfix.left->data.label.value));
    }

    /* A number entry's compound assignment is emitted by
     * emit_compound_arithmetic; a string entry's += reaches here. */
    bool ms_compound = node->data.assign.operator == TOKEN_PLUS_ASSIGN;
    emit_formatted(codegen, "{ %s _mk = ", c_key_type);
    emit_map_slot_value(codegen, left_type->key_type, node->data.assign.target->data.index_expression.index);
    emit(codegen, "; ");
    if (codegen->loop_scope_depth > 0) {
        if (has_string_key) {
            emit_formatted(codegen, "_mk = gray_string_new(%s, _mk.data, _mk.len); ", ms_arena);
        } else if (left_type->key_type && type_needs_deep_copy(codegen, left_type->key_type)) {
            emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _mk = ", ms_arena);
            emit_value_deep_copy(codegen, left_type->key_type, "_mk");
            emit(codegen, "; gray_default_arena = _esc; } ");
        }
    }
    /* For compound assignments, read the existing value first so the
     * operation is applied on top of the current entry rather than
     * against a zero/uninitialized base. */
    if (ms_compound) {
        if (is_map_via_pointer) {
            /* Capture _mp early so _cur can reference the map field. */
            emit_formatted(codegen, "__auto_type _mp = ");
            emit_expression(codegen, map_pointer_object);
            if (map_raw) {
                emit_formatted(codegen, "; void *_cur = gray_map_get(&_mp->%s, &_mk); "
                      "if (!_cur) { %s; } ",
                      sanitize_name(left->data.member.member),
                      panic_call(codegen, node, "P0081", ""));
            } else {
                emit_formatted(codegen, "; if (!_mp) { %s; } "
                      "void *_cur = gray_map_get(&_mp->%s, &_mk); "
                      "if (!_cur) { %s; } ",
                      panic_call(codegen, node, "P0080", ""),
                      sanitize_name(left->data.member.member),
                      panic_call(codegen, node, "P0081", ""));
            }
        } else if (map_direct_deref) {
            emit_formatted(codegen, "__auto_type _mp = ");
            emit_expression(codegen, left->data.postfix.left);
            if (map_deref_raw) {
                emit_formatted(codegen, "; void *_cur = gray_map_get(_mp, &_mk); "
                      "if (!_cur) { %s; } ",
                      panic_call(codegen, node, "P0081", ""));
            } else {
                emit_formatted(codegen, "; if (!_mp) { %s; } "
                      "void *_cur = gray_map_get(_mp, &_mk); "
                      "if (!_cur) { %s; } ",
                      panic_call(codegen, node, "P0080", ""),
                      panic_call(codegen, node, "P0081", ""));
            }
        } else {
            emit_formatted(codegen, "void *_cur = gray_map_get(&");
            emit_expression(codegen, left);
            emit_formatted(codegen, ", &_mk); if (!_cur) { %s; } ", panic_call(codegen, node, "P0081", ""));
        }
    }
    emit_formatted(codegen, "%s _mv = ", c_value_type);
    if (ms_compound && has_string_value && node->data.assign.operator == TOKEN_PLUS_ASSIGN) {
        emit(codegen, "gray_string_concat(gray_default_arena, *(GrayString*)_cur, ");
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, ")");
    } else if (codegen->loop_scope_depth == 0 &&
               composite_value_aliases(codegen, left_type->value_type, node->data.assign.value)) {
        /* Inside a loop the escape copy below already covers this. */
        emit_composite_operand(codegen, left_type->value_type, node->data.assign.value);
    } else {
        emit_map_slot_value(codegen, left_type->value_type, node->data.assign.value);
    }
    emit(codegen, "; ");
    if (codegen->loop_scope_depth > 0) {
        if (has_string_value) {
            emit_formatted(codegen, "_mv = gray_string_new(%s, _mv.data, _mv.len); ", ms_arena);
        } else if (left_type->value_type && type_needs_deep_copy(codegen, left_type->value_type)) {
            emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = %s; _mv = ", ms_arena);
            emit_value_deep_copy(codegen, left_type->value_type, "_mv");
            emit(codegen, "; gray_default_arena = _esc; } ");
        }
    }
    if (is_map_via_pointer) {
        if (ms_compound) {
            /* _mp was captured above; just set and close the outer block. */
            emit_formatted(codegen, "gray_map_set(%s, &_mp->%s, &_mk, &_mv, \"%s\", %d); }\n",
                ms_arena, sanitize_name(left->data.member.member), codegen->file, node->token.line);
        } else {
            /* Nil-check the pointer, then use -> to yield an assignable target. */
            emit_formatted(codegen, "{ __auto_type _mp = ");
            emit_expression(codegen, map_pointer_object);
            if (map_raw) {
                emit_formatted(codegen, "; gray_map_set(%s, &_mp->%s, &_mk, &_mv, \"%s\", %d); } }\n",
                    ms_arena, sanitize_name(left->data.member.member), codegen->file, node->token.line);
            } else {
                emit_formatted(codegen, "; if (!_mp) { %s; } "
                    "gray_map_set(%s, &_mp->%s, &_mk, &_mv, \"%s\", %d); } }\n",
                    panic_call(codegen, node, "P0080", ""), ms_arena, sanitize_name(left->data.member.member), codegen->file, node->token.line);
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
                emit_formatted(codegen, "; if (!_mp) { %s; } "
                    "gray_map_set(%s, _mp, &_mk, &_mv, \"%s\", %d); } }\n",
                    panic_call(codegen, node, "P0080", ""), ms_arena, codegen->file, node->token.line);
            }
        }
    } else {
        emit_formatted(codegen, "gray_map_set(%s, &", ms_arena);
        emit_expression(codegen, left);
        emit_formatted(codegen, ", &_mk, &_mv, \"%s\", %d); }\n", codegen->file, node->token.line);
    }
}

static void emit_assign_statement(CodeGen *codegen, AstNode *node) {
    /* Implicit declaration: emit as C variable declaration */
    if (node->data.assign.is_declaration &&
        node->data.assign.target->kind == NODE_LABEL) {
        emit_indent(codegen);
        GrayType *type = type_table_get(codegen->type_table, node->data.assign.target);
        const char *c_type = type ? gray_type_to_c_codegen(codegen, type_name(type)) : "__auto_type";
        const char *declaration_name = node->data.assign.target->data.label.value;
        if (is_new_call(node->data.assign.value)) {
            register_heap_variable(codegen, declaration_name, true);
        }
        if (node->data.assign.value && node->data.assign.value->kind == NODE_CALL_EXPRESSION) {
            const char *mem_module = NULL, *mem_function = NULL;
            if (is_stdlib_call(node->data.assign.value, &mem_module, &mem_function) &&
                mem_module && strcmp(mem_module, "mem") == 0 &&
                (strcmp(mem_function, "init") == 0 || strcmp(mem_function, "alloc") == 0) &&
                node->data.assign.value->data.call.argument_count >= 1) {
                AstNode *arena_argument = node->data.assign.value->data.call.arguments[0];
                if (is_stable_arena_expression(arena_argument)) {
                    register_mem_variable(codegen, declaration_name, arena_argument);
                }
            }
        }
        emit_formatted(codegen, "%s %s = ", c_type, sanitize_name(declaration_name));
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, ";\n");
        return;
    }

    /* Track raw/addr reassignment: p = raw(x) makes p raw,
     * p = addr(x) removes raw status so nil checks are restored. */
    if (node->data.assign.target->kind == NODE_LABEL &&
        node->data.assign.value && node->data.assign.value->kind == NODE_CALL_EXPRESSION) {
        AstNode *function_node = node->data.assign.value->data.call.function;
        if (function_node->kind == NODE_LABEL) {
            const char *variable_name = node->data.assign.target->data.label.value;
            if (strcmp(function_node->data.label.value, "raw") == 0) {
                register_raw_variable(codegen, variable_name);
            } else if (strcmp(function_node->data.label.value, "addr") == 0 &&
                       is_raw_variable(codegen, variable_name)) {
                unregister_raw_variable(codegen, variable_name);
            }
        }
    }
    /* Track heap-pointer reassignment: p = new(T) makes p heap-tracked;
     * any other reassignment of a previously heap-tracked p clears it. */
    if (node->data.assign.target->kind == NODE_LABEL) {
        const char *variable_name = node->data.assign.target->data.label.value;
        if (is_new_call(node->data.assign.value)) {
            register_heap_variable(codegen, variable_name, true);
        } else if (is_heap_variable(codegen, variable_name)) {
            register_heap_variable(codegen, variable_name, false);
        }
    }
    /* Track mem-arena-tracked reassignment: p = mem.init(a, T)/mem.alloc(a, v)
     * (re-)tracks p against arena expression a; any other reassignment of a
     * previously tracked p clears it, mirroring the heap-pointer case. */
    if (node->data.assign.target->kind == NODE_LABEL) {
        const char *variable_name = node->data.assign.target->data.label.value;
        const char *mem_module = NULL, *mem_function = NULL;
        bool is_mem_reinit = false;
        if (node->data.assign.value && node->data.assign.value->kind == NODE_CALL_EXPRESSION &&
            is_stdlib_call(node->data.assign.value, &mem_module, &mem_function) &&
            mem_module && strcmp(mem_module, "mem") == 0 &&
            (strcmp(mem_function, "init") == 0 || strcmp(mem_function, "alloc") == 0) &&
            node->data.assign.value->data.call.argument_count >= 1) {
            AstNode *arena_argument = node->data.assign.value->data.call.arguments[0];
            if (is_stable_arena_expression(arena_argument)) {
                register_mem_variable(codegen, variable_name, arena_argument);
                is_mem_reinit = true;
            }
        }
        if (!is_mem_reinit && is_mem_tracked_variable(codegen, variable_name)) {
            unregister_mem_variable(codegen, variable_name);
        }
    }

    emit_indent(codegen);

    /* x op= v on a number target, whatever shape the target has. */
    {
        TokenType assign_operator = node->data.assign.operator;
        TokenType arithmetic_operator = assign_operator == TOKEN_PLUS_ASSIGN ? TOKEN_PLUS :
                             assign_operator == TOKEN_MINUS_ASSIGN ? TOKEN_MINUS :
                             assign_operator == TOKEN_ASTERISK_ASSIGN ? TOKEN_ASTERISK :
                             assign_operator == TOKEN_SLASH_ASSIGN ? TOKEN_SLASH :
                             assign_operator == TOKEN_PERCENT_ASSIGN ? TOKEN_PERCENT : TOKEN_ASSIGN;
        if (arithmetic_operator != TOKEN_ASSIGN &&
            emit_compound_arithmetic(codegen, node->data.assign.target, arithmetic_operator,
                                     node->data.assign.value, node)) {
            emit(codegen, ";\n");
            return;
        }
    }

    /* Index assignment: arr[i] = v, m[key] = v */
    if (node->data.assign.target->kind == NODE_INDEX_EXPRESSION) {
        AstNode *left = node->data.assign.target->data.index_expression.left;
        GrayType *left_type = type_table_get(codegen->type_table, left);
        if (left_type && left_type->kind == TYPE_KIND_ARRAY) {
            emit_array_index_assign(codegen, node, left, left_type);
            return;
        }
        if (left_type && left_type->kind == TYPE_KIND_MAP) {
            emit_map_index_assign(codegen, node, left, left_type);
            return;
        }
    }

    /* Pointer dereference assignment: p^ = value → nil check + *p = value */
    if (node->data.assign.target->kind == NODE_POSTFIX_EXPRESSION &&
        node->data.assign.target->data.postfix.operator == TOKEN_CARET) {
        AstNode *pointer_node = node->data.assign.target->data.postfix.left;
        GrayType *pointer_type = type_table_get(codegen->type_table, pointer_node);
        const char *wide_integer_element = (pointer_type && pointer_type->kind == TYPE_KIND_POINTER && pointer_type->element_type &&
                               is_wide_integer_type_name(pointer_type->element_type))
                              ? pointer_type->element_type : NULL;
        bool _deref_raw = (pointer_node->kind == NODE_LABEL && is_raw_variable(codegen, pointer_node->data.label.value));
        emit(codegen, "{ __auto_type _dp = ");
        emit_expression(codegen, pointer_node);
        if (!_deref_raw) {
            emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
        } else {
            emit(codegen, "; ");
        }
        if (emit_string_append_through(codegen, node, "*_dp")) {
            emit(codegen, "; }\n");
            return;
        }
        emit(codegen, "*_dp");
        emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.operator));
        if (wide_integer_element) {
            emit_wide_integer_operand(codegen, node->data.assign.value,
                                wide_integer_prefix(wide_integer_element), wide_integer_element, NULL);
        } else {
            emit_composite_operand(codegen, pointer_type ? pointer_type->element_type : NULL, node->data.assign.value);
        }
        emit(codegen, "; }\n");
        return;
    }
    /* Pointer deref field assignment: p^.field = value → nil check + p->field = value */
    if (node->data.assign.target->kind == NODE_MEMBER_EXPRESSION &&
        node->data.assign.target->data.member.object->kind == NODE_POSTFIX_EXPRESSION &&
        node->data.assign.target->data.member.object->data.postfix.operator == TOKEN_CARET) {
        AstNode *pointer_target = node->data.assign.target->data.member.object->data.postfix.left;
        const char *field = node->data.assign.target->data.member.member;
        bool is_field_raw = (pointer_target->kind == NODE_LABEL && is_raw_variable(codegen, pointer_target->data.label.value));
        emit(codegen, "{ __auto_type _dp = ");
        emit_expression(codegen, pointer_target);
        if (is_field_raw) {
            emit(codegen, "; ");
        } else {
            emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
        }
        char field_reference[MESSAGE_BUFFER_SIZE];
        snprintf(field_reference, sizeof(field_reference), "_dp->%s", field);
        if (emit_string_append_through(codegen, node, field_reference)) {
            emit(codegen, "; }\n");
            return;
        }
        if (node->data.assign.operator == TOKEN_ASSIGN && pointer_target->kind == NODE_LABEL &&
            is_heap_variable(codegen, pointer_target->data.label.value)) {
            GrayType *field_type = type_table_get(codegen->type_table, node->data.assign.target);
            if (field_type_needs_arena_escape(field_type)) {
                emit_heap_escaped_field_assign(codegen, node, field_reference);
                emit(codegen, "; }\n");
                return;
            }
        }
        emit(codegen, field_reference);
        emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.operator));
        emit_expression(codegen, node->data.assign.value);
        emit(codegen, "; }\n");
        return;
    }
    /* Nested pointer field assignment: o.inner.val = value (where some ancestor is ptr<T>)
     * Walk the member chain to find the pointer root, then emit nil-check + chain. */
    if (node->data.assign.target->kind == NODE_MEMBER_EXPRESSION) {
        const char *chain[MAX_MEMBER_CHAIN];
        int depth = 0;
        AstNode *current = node->data.assign.target;
        AstNode *pointer_root = NULL;
        while (current->kind == NODE_MEMBER_EXPRESSION && depth < MAX_MEMBER_CHAIN) {
            chain[depth++] = current->data.member.member;
            AstNode *object = current->data.member.object;
            GrayType *object_type = type_table_get(codegen->type_table, object);
            if (object_type && object_type->kind == TYPE_KIND_POINTER &&
                !(object->kind == NODE_LABEL && is_reference_variable(codegen, object->data.label.value))) {
                pointer_root = object;
                break;
            }
            current = object;
        }
        if (pointer_root && depth > 1) {
            bool _nest_raw = (pointer_root->kind == NODE_LABEL && is_raw_variable(codegen, pointer_root->data.label.value));
            emit(codegen, "{ __auto_type _dp = ");
            emit_expression(codegen, pointer_root);
            if (_nest_raw) {
                emit(codegen, "; ");
            } else {
                emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
            }
            /* Build the field reference string for the chain */
            char index_reference[MESSAGE_BUFFER_SIZE];
            int position = snprintf(index_reference, sizeof(index_reference), "_dp->");
            for (int i = depth - 1; i >= 0 && position < (int)sizeof(index_reference); i--) {
                position += snprintf(index_reference + position, sizeof(index_reference) - position, "%s%s",
                                 sanitize_name(chain[i]), i > 0 ? "." : "");
            }
            if (emit_string_append_through(codegen, node, index_reference)) {
                emit(codegen, "; }\n");
                return;
            }
            emit(codegen, index_reference);
            emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.operator));
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; }\n");
            return;
        }
    }
    /* Pointer field assignment: p.field = value (where p is ptr<T>) → nil check + p->field = value */
    if (node->data.assign.target->kind == NODE_MEMBER_EXPRESSION) {
        AstNode *object = node->data.assign.target->data.member.object;
        GrayType *object_type = type_table_get(codegen->type_table, object);
        bool is_reference = (object->kind == NODE_LABEL && is_reference_variable(codegen, object->data.label.value));
        bool is_field_object_raw = (object->kind == NODE_LABEL && is_raw_variable(codegen, object->data.label.value));
        if (!is_reference && object_type && object_type->kind == TYPE_KIND_POINTER) {
            const char *field = node->data.assign.target->data.member.member;
            /* p was assigned from new(): its pointee lives in gray_heap_arena,
             * so a container field written through it must be allocated there
             * too, not in the current function's own scoped arena. */
            if (node->data.assign.operator == TOKEN_ASSIGN && object->kind == NODE_LABEL &&
                is_heap_variable(codegen, object->data.label.value)) {
                GrayType *field_type = type_table_get(codegen->type_table, node->data.assign.target);
                if (field_type_needs_arena_escape(field_type)) {
                    emit(codegen, "{ __auto_type _dp = ");
                    emit_expression(codegen, object);
                    if (is_field_object_raw) {
                        emit(codegen, "; ");
                    } else {
                        emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
                    }
                    char reference_holder[MESSAGE_BUFFER_SIZE];
                    snprintf(reference_holder, sizeof(reference_holder), "_dp->%s", sanitize_name(field));
                    emit_heap_escaped_field_assign(codegen, node, reference_holder);
                    emit(codegen, "; }\n");
                    return;
                }
            }
            /* When assigning an array/string to a struct field inside a
             * scoped block (if/loop), deep-copy to the outer arena so the
             * data survives the block's arena destruction. */
            if (node->data.assign.operator == TOKEN_ASSIGN && codegen->loop_scope_depth > 0) {
                GrayType *field_type = type_table_get(codegen->type_table, node->data.assign.target);
                if (field_type && field_type->kind == TYPE_KIND_ARRAY) {
                    char type_spelling_buffer[MESSAGE_BUFFER_SIZE];
                    snprintf(type_spelling_buffer, sizeof(type_spelling_buffer), "[%s]", field_type->element_type ? field_type->element_type : "");
                    emit(codegen, "{ __auto_type _dp = ");
                    emit_expression(codegen, object);
                    if (is_field_object_raw) {
                        emit(codegen, "; ");
                    } else {
                        emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
                    }
                    emit_formatted(codegen, "{ GrayArray _esc_v = ");
                    emit_expression(codegen, node->data.assign.value);
                    emit(codegen, "; GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
                    emit_formatted(codegen, "_dp->%s = ", sanitize_name(field));
                    emit_array_deep_copy(codegen, type_spelling_buffer, "_esc_v");
                    emit(codegen, "; gray_default_arena = _esc_a; } }\n");
                    return;
                }
                if (field_type && field_type->kind == TYPE_KIND_STRING) {
                    emit(codegen, "{ __auto_type _dp = ");
                    emit_expression(codegen, object);
                    if (is_field_object_raw) {
                        emit(codegen, "; ");
                    } else {
                        emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
                    }
                    emit_formatted(codegen, "{ GrayString _esc_v = ");
                    emit_expression(codegen, node->data.assign.value);
                    emit_formatted(codegen, "; _dp->%s = gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len); } }\n",
                        sanitize_name(field));
                    return;
                }
            }
            emit(codegen, "{ __auto_type _dp = ");
            emit_expression(codegen, object);
            if (is_field_object_raw) {
                emit(codegen, "; ");
            } else {
                emit_formatted(codegen, "; if (!_dp) { %s; } ", panic_call(codegen, node, "P0080", ""));
            }
            char pointer_field_reference[MESSAGE_BUFFER_SIZE];
            snprintf(pointer_field_reference, sizeof(pointer_field_reference), "_dp->%s", sanitize_name(field));
            if (emit_string_append_through(codegen, node, pointer_field_reference)) {
                emit(codegen, "; }\n");
                return;
            }
            emit(codegen, pointer_field_reference);
            emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.operator));
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
    if (node->data.assign.operator == TOKEN_PLUS_ASSIGN) {
        GrayType *target_type = type_table_get(codegen->type_table, node->data.assign.target);
        if (target_type && target_type->kind == TYPE_KIND_STRING) {
            const char *arena = codegen->loop_scope_depth > 0 ? "_gray_outer_arena" : "gray_default_arena";
            emit(codegen, "{ GrayString *_tgt = &(");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, "); GrayString _sv = ");
            emit_expression(codegen, node->data.assign.value);
            emit_formatted(codegen, "; *_tgt = gray_string_concat(%s, *_tgt, _sv); }\n", arena);
            return;
        }
    }

    /* Array copy-by-default: arr2 = arr1 deep-copies the array so nested
     * inner arrays get independent backing storage. Applies to any RHS
     * expression (variables, call results, etc.) to prevent use-after-free
     * when the source array lives on a function-local arena.
     * Inside a scoped arena (if-block / for_each), allocate the copy on
     * the outer arena: the target variable outlives the block, so a copy
     * made in the block's arena dangles once that arena is destroyed. */
    if (node->data.assign.operator == TOKEN_ASSIGN) {
        GrayType *target_type = type_table_get(codegen->type_table, node->data.assign.target);
        if (target_type && target_type->kind == TYPE_KIND_ARRAY) {
            int unique_id = codegen_next_id(codegen);
            char source_variable[VARIABLE_NAME_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_dtop%d", unique_id);
            char full_type_name[MESSAGE_BUFFER_SIZE];
            snprintf(full_type_name, sizeof(full_type_name), "[%s]", target_type->element_type ? target_type->element_type : "");
            emit(codegen, "{ GrayArray ");
            emit_formatted(codegen, "%s = ", source_variable);
            /* An array literal takes the target's element type, as in a
             * declaration, so a [f32] target gets packed float storage. */
            const char *saved_variable_type = codegen->current_variable_type;
            codegen->current_variable_type = full_type_name;
            emit_expression(codegen, node->data.assign.value);
            codegen->current_variable_type = saved_variable_type;
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            }
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, full_type_name, source_variable);
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
        if (target_type && target_type->kind == TYPE_KIND_MAP) {
            int unique_id = codegen_next_id(codegen);
            char source_variable[VARIABLE_NAME_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_ma%d", unique_id);
            emit(codegen, "{ GrayMap ");
            emit_formatted(codegen, "%s = ", source_variable);
            /* A map literal takes the target's key and value types, as in a
             * declaration, so a map[string:f32] target stores floating-point values. */
            const char *saved_variable_type = codegen->current_variable_type;
            codegen->current_variable_type = target_type->name;
            emit_expression(codegen, node->data.assign.value);
            codegen->current_variable_type = saved_variable_type;
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "GrayArena *_esc_m = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            }
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, target_type->name, source_variable);
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
        if (target_type && target_type->kind == TYPE_KIND_STRUCT && target_type->name &&
            type_needs_deep_copy(codegen, target_type->name)) {
            int unique_id = codegen_next_id(codegen);
            const char *c_type_text = gray_type_to_c_codegen(codegen, target_type->name);
            char source_variable[VARIABLE_NAME_BUFFER_SIZE];
            snprintf(source_variable, sizeof(source_variable), "_sa%d", unique_id);
            emit_formatted(codegen, "{ %s %s = ", c_type_text, source_variable);
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; ");
            if (codegen->loop_scope_depth > 0) {
                emit(codegen, "GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            }
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, target_type->name, source_variable);
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
    if (codegen->loop_scope_depth > 0 && node->data.assign.operator == TOKEN_ASSIGN &&
        node->data.assign.target->kind == NODE_MEMBER_EXPRESSION) {
        GrayType *target_type = type_table_get(codegen->type_table, node->data.assign.target);
        if (target_type && target_type->kind == TYPE_KIND_STRING) {
            emit(codegen, "{ GrayString *_tgt = &(");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, "); GrayString _esc_v = ");
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; *_tgt = gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len); }\n");
            return;
        }
    }

    /* when inside a loop scope and assigning a string/container
     * value to a plain variable with =, escape the value to the outer
     * arena so it survives the iteration arena's destruction. */
    if (codegen->loop_scope_depth > 0 && node->data.assign.operator == TOKEN_ASSIGN &&
        node->data.assign.target->kind == NODE_LABEL) {
        GrayType *target_type = type_table_get(codegen->type_table, node->data.assign.target);
        if (target_type && target_type->kind == TYPE_KIND_STRING) {
            emit(codegen, "{ GrayString _esc_v = ");
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; ");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = gray_string_new(_gray_outer_arena, _esc_v.data, _esc_v.len); }\n");
            return;
        }
        if (target_type && target_type->kind == TYPE_KIND_STRUCT && target_type->name &&
            type_needs_deep_copy(codegen, target_type->name)) {
            const char *c_type = gray_type_to_c_codegen(codegen, target_type->name);
            emit_formatted(codegen, "{ %s _esc_v = ", c_type);
            emit_expression(codegen, node->data.assign.value);
            emit(codegen, "; GrayArena *_esc_a = gray_default_arena; gray_default_arena = _gray_outer_arena; ");
            emit_expression(codegen, node->data.assign.target);
            emit(codegen, " = ");
            emit_value_deep_copy(codegen, target_type->name, "_esc_v");
            emit(codegen, "; gray_default_arena = _esc_a; }\n");
            return;
        }
    }

    emit_expression(codegen, node->data.assign.target);
    emit_formatted(codegen, " %s ", operator_to_c_string(node->data.assign.operator));
    /* A plain scalar / integer literal (of any width) assigned to a wide integer
     * target must be wrapped with the matching constructor. */
    const char *assign_wide_integer = NULL;
    if (node->data.assign.operator == TOKEN_ASSIGN) {
        GrayType *target_base_type = type_table_get(codegen->type_table, node->data.assign.target);
        if (target_base_type && target_base_type->name && is_wide_integer_type_name(target_base_type->name))
            assign_wide_integer = target_base_type->name;
    }
    if (assign_wide_integer && emit_wide_integer_coerced(codegen, assign_wide_integer, node->data.assign.value)) {
        /* emitted */
    } else if (node->data.assign.value->kind == NODE_LABEL &&
        is_reference_variable(codegen, node->data.assign.value->data.label.value)) {
        GrayType *target_type = type_table_get(codegen->type_table, node->data.assign.target);
        if (target_type && target_type->kind == TYPE_KIND_POINTER) {
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
static void collect_ensures(AstNode *block, AstNode ***ensures, int *count, int *capacity) {
    if (!block || block->kind != NODE_BLOCK_STATEMENT) return;
    for (int i = 0; i < block->data.block.count; i++) {
        AstNode *statement = block->data.block.statements[i];
        if (statement->kind != NODE_ENSURE_STATEMENT) continue;
        if (*count == *capacity) {
            int new_capacity = *capacity ? *capacity * 2 : 8;
            *ensures = xrealloc(*ensures, sizeof(AstNode *) * (size_t)new_capacity);
            *capacity = new_capacity;
        }
        (*ensures)[(*count)++] = statement;
    }
}

/* Emit ensure cleanup calls in LIFO order */
static void emit_ensure_cleanup(CodeGen *codegen) {
    if (!codegen->current_function || !codegen->current_function->data.function_declaration.body) return;

    AstNode **ensures = NULL;
    int ensure_count = 0;
    int ensure_capacity = 0;
    collect_ensures(codegen->current_function->data.function_declaration.body, &ensures, &ensure_count, &ensure_capacity);

    /* Only the defer/ensure statements control flow has actually reached run at
     * this exit; a return that lexically precedes a defer must not splice it in
     * (the deferred expression may reference not-yet-declared variables, or
     * clean up a resource that was never acquired). */
    int reached = codegen->ensure_reached;
    if (reached > ensure_count) reached = ensure_count;

    /* Emit in reverse (LIFO) order */
    for (int i = reached - 1; i >= 0; i--) {
        emit_indent(codegen);
        emit_expression(codegen, ensures[i]->data.ensure_statement.expression);
        emit(codegen, ";\n");
    }

    free(ensures);
}

/* Track nested scratch arenas so early-exit paths can unwind every live
 * one innermost-first. Without this, `return` (and the desugared
 * `or_return`) from inside a nested for_each/if/while/loop scope leaks
 * the per-scope arenas the codegen had emitted. */
static void scope_arena_push(CodeGen *codegen, const char *arena_variable, const char *saved_variable) {
    GROW_ARRAY(codegen->scope_arenas, codegen->scope_arena_count, codegen->scope_arena_capacity);
    ScopeArena *entry = &codegen->scope_arenas[codegen->scope_arena_count++];
    snprintf(entry->arena_variable, sizeof(entry->arena_variable), "%s", arena_variable);
    snprintf(entry->saved_variable, sizeof(entry->saved_variable), "%s", saved_variable);
}

static void scope_arena_pop(CodeGen *codegen) {
    if (codegen->scope_arena_count > 0) codegen->scope_arena_count--;
}

/* Track active for_each iteration guards so early-return paths can
 * decrement .iterating for every live for_each loop. */
static void iteration_guard_push(CodeGen *codegen, const char *expression_text) {
    GROW_ARRAY(codegen->iteration_guards, codegen->iteration_guard_count, codegen->iteration_guard_capacity);
    codegen->iteration_guards[codegen->iteration_guard_count++] = strdup(expression_text);
}

/* The guard expression for the innermost live for_each, so the decrement
 * targets exactly what the increment did. */
static const char *iteration_guard_top(CodeGen *codegen) {
    if (codegen->iteration_guard_count == 0) return NULL;
    return codegen->iteration_guards[codegen->iteration_guard_count - 1];
}

static void iteration_guard_pop(CodeGen *codegen) {
    if (codegen->iteration_guard_count > 0) {
        free(codegen->iteration_guards[--codegen->iteration_guard_count]);
    }
}

static void emit_iteration_guard_unwind(CodeGen *codegen) {
    for (int i = codegen->iteration_guard_count - 1; i >= 0; i--) {
        emit_formatted(codegen, "gray_atomic_sub32(&%s.iterating, 1); ", codegen->iteration_guards[i]);
    }
}

/* True for a field path like b.items or a.b.items — an lvalue naming the
 * caller's array, with no calls or indexing to re-evaluate. Iterating one of
 * these can guard the real array instead of the by-value snapshot. */
static bool is_stable_field_path(AstNode *node) {
    while (node && node->kind == NODE_MEMBER_EXPRESSION) {
        node = node->data.member.object;
    }
    return node && node->kind == NODE_LABEL;
}

/* Build the C expression string for a for_each collection. For tmp
 * variables, returns the tmp name. For labels, returns the sanitized
 * name (with deref wrapper for mutable params / ref vars). */
static char *iteration_guard_expression(CodeGen *codegen, bool needs_temporary,
                             const char *temporary_name, AstNode *coll) {
    if (needs_temporary) return strdup(temporary_name);
    const char *raw_expression = coll->data.label.value;
    /* A module-level collection is emitted under its module's mangled name,
     * the same as any other reference to it. */
    const char *sanitized = label_is_entry_global(coll)
        ? global_variable_c_name(codegen, raw_expression)
        : NULL;
    if (!sanitized) {
        const char *resolved = codegen_resolve_label(codegen, coll, raw_expression);
        sanitized = sanitize_name(resolved != raw_expression ? resolved : raw_expression);
    }
    char message[128];
    if (is_mutable_parameter(codegen, raw_expression) || is_reference_variable(codegen, raw_expression))
        snprintf(message, sizeof(message), "(*%s)", sanitized);
    else
        snprintf(message, sizeof(message), "%s", sanitized);
    return strdup(message);
}

/* Emit the cleanup sequence for every live nested scratch arena,
 * innermost-first. Used in every early-exit return path before the
 * function-arena (or scope_restore) unwind. */
static void emit_scratch_arena_unwind(CodeGen *codegen) {
    emit_iteration_guard_unwind(codegen);
    for (int i = codegen->scope_arena_count - 1; i >= 0; i--) {
        ScopeArena *entry = &codegen->scope_arenas[i];
        emit_formatted(codegen, "gray_default_arena = %s; ", entry->saved_variable);
        emit_formatted(codegen, "gray_arena_destroy(%s, __FILE__, __LINE__); free(%s); ",
              entry->arena_variable, entry->arena_variable);
    }
}

/* Unwind only up to and including the innermost loop iteration arena.
 * Used by break/continue: we must restore the current loop's arena
 * pointer but must NOT touch outer loop arenas which are still live.
 * An if nested inside a loop only watermarks the iteration arena (no
 * scope_arenas entry of its own), so the innermost live entry here is
 * always that _iter_arena_N; the loop tolerates a stray _if_arena_N
 * (only produced by a top-level if, never inside a loop) for safety.
 * A loop that opens no iteration arena has nothing to restore, and the
 * innermost entry then belongs to an enclosing scope that must stay live. */
static void emit_loop_exit_unwind(CodeGen *codegen) {
    if (codegen->is_in_no_arena_loop) return;
    for (int i = codegen->scope_arena_count - 1; i >= 0; i--) {
        ScopeArena *entry = &codegen->scope_arenas[i];
        emit_formatted(codegen, "gray_default_arena = %s; ", entry->saved_variable);
        /* The iteration arena is hoisted out of the loop: break/continue only
         * restore the arena pointer. The next iteration's reset and the
         * post-loop destroy own its lifetime. */
        if (strncmp(entry->arena_variable, "_iter_arena_", 12) == 0) break;
        emit_formatted(codegen, "gray_arena_destroy(%s, __FILE__, __LINE__); free(%s); ",
              entry->arena_variable, entry->arena_variable);
    }
}

/* emit escape + cleanup for a non-void function return.
 * Escapes the return value (_ret) to _func_saved, then unwinds any
 * nested scratch arenas live at this exit point, then
 * destroys the function arena. The escape must run first because it
 * may read from memory still owned by a scratch arena. */
static void emit_function_return_escape(CodeGen *codegen, const char *return_type_name) {
    if (!return_type_name) return;
    /* Caller-arena functions have no private _func_arena: the return value
     * is already allocated in the caller's arena, so there is nothing to
     * escape and nothing to destroy — just unwind any nested scratch. */
    if (function_uses_caller_arena(codegen, codegen->current_function)) {
        emit_scratch_arena_unwind(codegen);
        return;
    }
    GrayType *return_graytype = type_from_name(return_type_name);
    if (return_graytype->kind == TYPE_KIND_STRING) {
        emit(codegen, "_ret = gray_string_new(_func_saved, _ret.data, _ret.len); ");
    } else if (return_graytype->kind == TYPE_KIND_ERROR) {
        emit(codegen, "if (_ret) { GrayError *_src_err = (GrayError *)_ret; ");
        emit(codegen, "GrayError *_esc_err = (GrayError *)gray_arena_alloc(_func_saved, sizeof(GrayError)); ");
        emit(codegen, "_esc_err->code = _src_err->code; ");
        emit(codegen, "_esc_err->msg = gray_string_new(_func_saved, _src_err->msg.data, _src_err->msg.len); ");
        emit(codegen, "_ret = _esc_err; } ");
    } else if (type_needs_deep_copy(codegen, return_type_name)) {
        emit(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = _func_saved; _ret = ");
        emit_value_deep_copy(codegen, return_type_name, "_ret");
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
    if (function_uses_caller_arena(codegen, codegen->current_function)) {
        emit_scratch_arena_unwind(codegen);
        return;
    }
    int return_count = codegen->current_function->data.function_declaration.return_type_count;
    for (int i = 0; i < return_count; i++) {
        const char *type_spelling = codegen->current_function->data.function_declaration.return_types[i];
        if (!type_spelling) continue;
        GrayType *return_graytype = type_from_name(type_spelling);
        if (return_graytype->kind == TYPE_KIND_STRING) {
            emit_formatted(codegen, "_ret.v%d = gray_string_new(_func_saved, _ret.v%d.data, _ret.v%d.len); ", i, i, i);
        } else if (return_graytype->kind == TYPE_KIND_ERROR) {
            emit_formatted(codegen, "if (_ret.v%d) { GrayError *_esc_err = (GrayError *)gray_arena_alloc(_func_saved, sizeof(GrayError)); ", i);
            emit_formatted(codegen, "_esc_err->code = _ret.v%d->code; ", i);
            emit_formatted(codegen, "_esc_err->msg = gray_string_new(_func_saved, _ret.v%d->msg.data, _ret.v%d->msg.len); ", i, i);
            emit_formatted(codegen, "_ret.v%d = _esc_err; } ", i);
        } else if (type_needs_deep_copy(codegen, type_spelling)) {
            char field[SHORT_VARIABLE_BUFFER_SIZE];
            snprintf(field, sizeof(field), "_ret.v%d", i);
            emit_formatted(codegen, "{ GrayArena *_esc = gray_default_arena; gray_default_arena = _func_saved; _ret.v%d = ", i);
            emit_value_deep_copy(codegen, type_spelling, field);
            emit(codegen, "; gray_default_arena = _esc; } ");
        }
    }
    emit_scratch_arena_unwind(codegen);
    emit(codegen, "gray_default_arena = _func_saved; ");
    emit(codegen, "gray_arena_destroy(_func_arena, __FILE__, __LINE__); free(_func_arena); ");
}

static bool function_uses_watermark(CodeGen *codegen, AstNode *node);
static bool block_allocation_free(CodeGen *codegen, AstNode *body);

/* True when the current function's watermark path took a real _scope_mark
 * to restore. A void function may allocate temporaries it must free, so it
 * takes one unless its own body is provably alloc-free. The non-void
 * watermark path (see function_uses_watermark) is only reached when the
 * whole body is provably alloc-free, so it never takes one. */
static bool function_needs_scope_mark(CodeGen *codegen, AstNode *node) {
    if (!node || node->data.function_declaration.return_type_count != 0) return false;
    return !block_allocation_free(codegen, node->data.function_declaration.body);
}

static void emit_return_statement(CodeGen *codegen, AstNode *node) {
    /* Caller-arena functions have no _scope_mark to restore. */
    bool caller_arena = codegen->current_function &&
                        function_uses_caller_arena(codegen, codegen->current_function);
    /* A non-void function proven allocation-free uses the watermark path too:
     * restore the mark on return instead of tearing down a private arena. */
    bool watermark = codegen->current_function &&
                     codegen->current_function->data.function_declaration.return_type_count > 0 &&
                     function_uses_watermark(codegen, codegen->current_function);
    bool has_mark = function_needs_scope_mark(codegen, codegen->current_function);

    /* Guard against malformed AST: count > 0 but NULL values array */
    if (node->data.return_statement.count > 0 && !node->data.return_statement.values) {
        emit_indent(codegen);
        emit(codegen, "{ ");
        emit_ensure_cleanup(codegen);
        emit_scratch_arena_unwind(codegen);
        if (caller_arena || !has_mark) {
            emit(codegen, "gray_exit_func(); return; }\n");
        } else {
            emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); gray_exit_func(); return; }\n");
        }
        return;
    }

    if (node->data.return_statement.count > 1 && codegen->current_function) {
        /* Multi-return: evaluate into temp, then exit and return */
        emit_indent(codegen);
        const char *multi_return_base_name_text = multi_return_name(codegen->current_function);
        emit_formatted(codegen, "{ GrayMulti_%s _ret = (GrayMulti_%s){", multi_return_base_name_text, multi_return_base_name_text);
        for (int i = 0; i < node->data.return_statement.count; i++) {
            if (i > 0) emit(codegen, ", ");
            const char *return_wide_integer = (i < codegen->current_function->data.function_declaration.return_type_count)
                ? codegen->current_function->data.function_declaration.return_types[i] : NULL;
            if (!emit_wide_integer_coerced(codegen, return_wide_integer, node->data.return_statement.values[i]))
                emit_declared_value(codegen, return_wide_integer, node->data.return_statement.values[i]);
        }
        emit(codegen, "}; ");
        emit_ensure_cleanup(codegen);
        emit_multi_function_return_escape(codegen);
        emit(codegen, "gray_exit_func(); return _ret; }\n");
    } else if (node->data.return_statement.count == 1 && codegen->current_function &&
               codegen->current_function->data.function_declaration.return_type_count > 1) {
        /* Single value returned from multi-return function (or_return propagation) */
        int return_count = codegen->current_function->data.function_declaration.return_type_count;
        emit_indent(codegen);
        const char *multi_return_name_second = multi_return_name(codegen->current_function);
        emit_formatted(codegen, "{ GrayMulti_%s _ret = (GrayMulti_%s){", multi_return_name_second, multi_return_name_second);
        for (int i = 0; i < return_count - 1; i++) {
            /* Use {0} for composite types (structs, arrays, maps, strings)
             * and 0 for scalars to avoid -Wbraced-scalar-init. */
            const char *return_type_spelling = codegen->current_function->data.function_declaration.return_types[i];
            bool composite = false;
            if (return_type_spelling) {
                GrayType *return_type = type_from_name(return_type_spelling);
                if (return_type && (return_type->kind == TYPE_KIND_STRUCT || return_type->kind == TYPE_KIND_ARRAY ||
                            return_type->kind == TYPE_KIND_MAP || return_type->kind == TYPE_KIND_STRING ||
                            return_type->kind == TYPE_KIND_ERROR))
                    composite = true;
            }
            emit(codegen, composite ? "{0}, " : "0, ");
        }
        {
            const char *return_wide_integer = codegen->current_function->data.function_declaration.return_types[return_count - 1];
            if (!emit_wide_integer_coerced(codegen, return_wide_integer, node->data.return_statement.values[0]))
                emit_declared_value(codegen, return_wide_integer, node->data.return_statement.values[0]);
        }
        emit(codegen, "}; ");
        emit_ensure_cleanup(codegen);
        emit_multi_function_return_escape(codegen);
        emit(codegen, "gray_exit_func(); return _ret; }\n");
    } else if (codegen->current_function &&
               codegen->current_function->data.function_declaration.return_type_count == 0) {
        /* Void function */
        emit_indent(codegen);
        emit(codegen, "{ ");
        emit_ensure_cleanup(codegen);
        emit_scratch_arena_unwind(codegen);
        if (caller_arena || !has_mark) {
            emit(codegen, "gray_exit_func(); return; }\n");
        } else {
            emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); gray_exit_func(); return; }\n");
        }
    } else if (node->data.return_statement.count == 1) {
        /* Single return value: evaluate into temp, then exit and return */
        emit_indent(codegen);
        emit(codegen, "{ __auto_type _ret = ");
        const char *return_wide_integer_type = (codegen->current_function &&
            codegen->current_function->data.function_declaration.return_type_count == 1)
            ? codegen->current_function->data.function_declaration.return_types[0] : NULL;
        if (!emit_wide_integer_coerced(codegen, return_wide_integer_type, node->data.return_statement.values[0]))
            emit_declared_value(codegen, return_wide_integer_type, node->data.return_statement.values[0]);
        emit(codegen, "; ");
        emit_ensure_cleanup(codegen);
        if (codegen->current_function && codegen->current_function->data.function_declaration.return_type_count > 0) {
            if (watermark) {
                emit_scratch_arena_unwind(codegen);
                if (has_mark) emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); ");
            } else {
                const char *return_type_name = codegen->current_function->data.function_declaration.return_types[0];
                emit_function_return_escape(codegen, return_type_name);
            }
        }
        emit(codegen, "gray_exit_func(); return _ret; }\n");
    } else if (node->data.return_statement.count == 0 && codegen->current_function &&
               codegen->current_function->data.function_declaration.return_names &&
               codegen->current_function->data.function_declaration.return_type_count > 0) {
        /* Bare return in function with named return values; collect named vars */
        int return_count = codegen->current_function->data.function_declaration.return_type_count;
        if (return_count == 1 && codegen->current_function->data.function_declaration.return_names[0]) {
            emit_indent(codegen);
            emit_formatted(codegen, "{ __auto_type _ret = %s; ",
                sanitize_name(codegen->current_function->data.function_declaration.return_names[0]));
            emit_ensure_cleanup(codegen);
            emit_function_return_escape(codegen, codegen->current_function->data.function_declaration.return_types[0]);
            emit(codegen, "gray_exit_func(); return _ret; }\n");
        } else {
            emit_indent(codegen);
            const char *multi_return_name_third = multi_return_name(codegen->current_function);
            emit_formatted(codegen, "{ GrayMulti_%s _ret = (GrayMulti_%s){", multi_return_name_third, multi_return_name_third);
            for (int i = 0; i < return_count; i++) {
                if (i > 0) emit(codegen, ", ");
                if (codegen->current_function->data.function_declaration.return_names[i]) {
                    emit_formatted(codegen, "%s", sanitize_name(codegen->current_function->data.function_declaration.return_names[i]));
                } else {
                    emit(codegen, "0");
                }
            }
            emit(codegen, "}; ");
            emit_ensure_cleanup(codegen);
            emit_multi_function_return_escape(codegen);
            emit(codegen, "gray_exit_func(); return _ret; }\n");
        }
    } else {
        /* Bare return (no value, non-void; shouldn't happen but handle gracefully) */
        emit_indent(codegen);
        emit(codegen, "{ ");
        emit_ensure_cleanup(codegen);
        emit_scratch_arena_unwind(codegen);
        if (caller_arena || !has_mark) {
            emit(codegen, "gray_exit_func(); return; }\n");
        } else {
            emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark); gray_exit_func(); return; }\n");
        }
    }
}

static void emit_block(CodeGen *codegen, AstNode *node) {
    for (int i = 0; i < node->data.block.count; i++) {
        emit_statement(codegen, node->data.block.statements[i]);
    }
}

static bool codegen_statement_allocation_free(CodeGen *codegen, AstNode *statement);

static void emit_if_statement(CodeGen *codegen, AstNode *node) {
    /* An if that allocates nothing has no temporaries to free and opens no
     * scope at all. Otherwise, if/otherwise branches free their temporaries
     * on exit. When the if is
     * nested inside a loop or another scope (loop_scope_depth > 0) there is
     * already a distinct enclosing scratch arena and a separate
     * _gray_outer_arena for escaping writes, so the branch just watermarks
     * the enclosing arena (gray_scope_save/restore) — no allocator traffic
     * per entry. A top-level if (depth 0) shares its arena with
     * _gray_outer_arena, so a watermark restore would clobber escaped
     * writes; it keeps the private per-entry arena. */
    int previous_raw_variable_count = codegen->raw_variable_count;
    int if_scope_id = codegen_next_id(codegen);
    bool scoped = !current_function_uses_caller_arena(codegen) &&
                  !codegen_statement_allocation_free(codegen, node);
    bool watermark = scoped && codegen->loop_scope_depth > 0;
    emit_indent(codegen);
    emit_formatted(codegen, "{ ");
    if (scoped) {
        if (codegen->loop_scope_depth == 0) {
            emit(codegen, "GrayArena *_gray_outer_arena = gray_default_arena; ");
        }
        if (watermark) {
            emit_formatted(codegen, "GrayScopeMark _if_mark_%d = gray_scope_save(gray_default_arena);\n", if_scope_id);
        } else {
            emit_formatted(codegen, "GrayArena *_if_arena_%d = gray_arena_create(%d); ", if_scope_id, IF_ARENA_SIZE);
            emit_formatted(codegen, "GrayArena *_if_saved_%d = gray_default_arena; ", if_scope_id);
            emit_formatted(codegen, "gray_default_arena = _if_arena_%d;\n", if_scope_id);
        }
        codegen->loop_scope_depth++;
        if (!watermark) {
            char arena_variable[SHORT_VARIABLE_BUFFER_SIZE], saved_variable[SHORT_VARIABLE_BUFFER_SIZE];
            snprintf(arena_variable, sizeof(arena_variable), "_if_arena_%d", if_scope_id);
            snprintf(saved_variable, sizeof(saved_variable), "_if_saved_%d", if_scope_id);
            scope_arena_push(codegen, arena_variable, saved_variable);
        }
    } else {
        emit(codegen, "\n");
    }

    emit_indent(codegen);
    emit(codegen, "if (");
    emit_expression(codegen, node->data.if_statement.condition);
    emit(codegen, ") {\n");

    codegen->indent++;
    emit_block(codegen, node->data.if_statement.consequence);
    codegen->indent--;

    if (node->data.if_statement.alternative) {
        if (node->data.if_statement.alternative->kind == NODE_IF_STATEMENT) {
            emit_indent(codegen);
            emit(codegen, "} else if (");
            emit_expression(codegen, node->data.if_statement.alternative->data.if_statement.condition);
            emit(codegen, ") {\n");
            codegen->indent++;
            emit_block(codegen, node->data.if_statement.alternative->data.if_statement.consequence);
            codegen->indent--;
            AstNode *alternative = node->data.if_statement.alternative->data.if_statement.alternative;
            while (alternative) {
                if (alternative->kind == NODE_IF_STATEMENT) {
                    emit_indent(codegen);
                    emit(codegen, "} else if (");
                    emit_expression(codegen, alternative->data.if_statement.condition);
                    emit(codegen, ") {\n");
                    codegen->indent++;
                    emit_block(codegen, alternative->data.if_statement.consequence);
                    codegen->indent--;
                    alternative = alternative->data.if_statement.alternative;
                } else {
                    emit_indent(codegen);
                    emit(codegen, "} else {\n");
                    codegen->indent++;
                    emit_block(codegen, alternative);
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
            emit_block(codegen, node->data.if_statement.alternative);
            codegen->indent--;
            emit_indent(codegen);
            emit(codegen, "}\n");
        }
    } else {
        emit_indent(codegen);
        emit(codegen, "}\n");
    }

    codegen->raw_variable_count = previous_raw_variable_count;
    emit_indent(codegen);
    if (scoped) {
        codegen->loop_scope_depth--;
        if (watermark) {
            emit_formatted(codegen, "gray_scope_restore(gray_default_arena, _if_mark_%d); ", if_scope_id);
        } else {
            scope_arena_pop(codegen);
            emit_formatted(codegen, "gray_default_arena = _if_saved_%d; ", if_scope_id);
            emit_formatted(codegen, "gray_arena_destroy(_if_arena_%d, __FILE__, __LINE__); free(_if_arena_%d); ", if_scope_id, if_scope_id);
        }
    }
    emit(codegen, "}\n");
}

/* --- Non-allocating loop body fast path -------------------------------------
 *
 * A loop body that allocates nothing needs no per-iteration scratch arena:
 * there is no short-lived memory to reclaim, so the reset + arena-pointer
 * swaps (a non-inlined call in the inner loop) are pure overhead, and an if
 * that allocates nothing needs no scope mark. The predicate below is
 * deliberately narrow — scalar arithmetic, assignment, control flow over
 * those, and calls to functions on the watermark path. Anything it does not
 * positively recognise as allocation-free (any other call, any literal,
 * interpolation) keeps the arena. A wrong "arena-free" answer could only
 * strand a value in the enclosing arena until that scope ends, never
 * corrupt memory — but the conservative answer is always correct and the
 * only cost is a missed optimisation. */

static bool codegen_expression_is_string(CodeGen *codegen, AstNode *expression) {
    if (!expression) return false;
    if (expression->kind == NODE_STRING_VALUE || expression->kind == NODE_INTERPOLATED_STRING) return true;
    GrayType *type = type_table_get(codegen->type_table, expression);
    return type && type->kind == TYPE_KIND_STRING;
}

/* A value of this kind is copied by C assignment with no heap traffic.
 * Arrays, maps and structs deep-copy on assignment, so they are excluded. */
static bool codegen_type_is_copy_free(GrayType *type) {
    if (!type) return false;
    switch (type->kind) {
        case TYPE_KIND_SIGNED_INTEGER: case TYPE_KIND_UNSIGNED_INTEGER: case TYPE_KIND_FLOATING_POINT:
        case TYPE_KIND_BOOL: case TYPE_KIND_CHAR: case TYPE_KIND_STRING:
            return true;
        default:
            return false;
    }
}

static bool codegen_expression_allocation_free(CodeGen *codegen, AstNode *expression);

/* A call to a function on the watermark path allocates nothing that outlives
 * it. Arguments must be allocation-free and copy-free: an array, map or
 * struct argument is deep-copied into the caller's arena. Not recognised
 * while function_uses_watermark is scanning a body, so that scan keeps its
 * no-call rule and cannot recurse through callers of each other. */
static bool codegen_call_allocation_free(CodeGen *codegen, AstNode *expression) {
    if (codegen->watermark_probe) return false;
    AstNode *function_node = expression->data.call.function;
    if (!function_node || function_node->kind != NODE_LABEL) return false;
    AstNode *callee = find_function(codegen, function_node->data.label.value);
    if (!callee || callee->data.function_declaration.instantiation_count > 0) return false;
    if (expression->data.call.argument_count != callee->data.function_declaration.parameter_count) return false;
    for (int i = 0; i < expression->data.call.argument_count; i++) {
        if (expression->data.call.argument_names && expression->data.call.argument_names[i]) return false;
        AstNode *argument = expression->data.call.arguments[i];
        if (!codegen_expression_allocation_free(codegen, argument)) return false;
        if (!codegen_type_is_copy_free(type_table_get(codegen->type_table, argument))) return false;
    }
    return function_uses_watermark(codegen, callee);
}

static bool codegen_expression_allocation_free(CodeGen *codegen, AstNode *expression) {
    if (!expression) return true;
    switch (expression->kind) {
        case NODE_INTEGER_LITERAL: case NODE_FLOATING_POINT_LITERAL: case NODE_CHAR_VALUE:
        case NODE_BOOL_VALUE: case NODE_NIL_VALUE: case NODE_STRING_VALUE:
        case NODE_LABEL:
            return true;
        case NODE_PREFIX_EXPRESSION:
            return codegen_expression_allocation_free(codegen, expression->data.prefix.right);
        case NODE_POSTFIX_EXPRESSION:
            return codegen_expression_allocation_free(codegen, expression->data.postfix.left);
        case NODE_MEMBER_EXPRESSION:
            return codegen_expression_allocation_free(codegen, expression->data.member.object);
        case NODE_INDEX_EXPRESSION:
            return codegen_expression_allocation_free(codegen, expression->data.index_expression.left)
                && codegen_expression_allocation_free(codegen, expression->data.index_expression.index);
        case NODE_INFIX_EXPRESSION:
            /* string + string allocates a fresh GrayString */
            if (expression->data.infix.operator == TOKEN_PLUS
                && (codegen_expression_is_string(codegen, expression->data.infix.left)
                    || codegen_expression_is_string(codegen, expression->data.infix.right)))
                return false;
            return codegen_expression_allocation_free(codegen, expression->data.infix.left)
                && codegen_expression_allocation_free(codegen, expression->data.infix.right);
        case NODE_CALL_EXPRESSION:
            return codegen_call_allocation_free(codegen, expression);
        default:
            /* new(), array/map/struct literals, interpolation, casts,
             * ranges, func refs, implicit enums — assume allocation */
            return false;
    }
}

static bool block_allocation_free(CodeGen *codegen, AstNode *body);

static bool codegen_statement_allocation_free(CodeGen *codegen, AstNode *statement) {
    if (!statement) return true;
    switch (statement->kind) {
        case NODE_VARIABLE_DECLARATION: {
            AstNode *value_node = statement->data.variable_declaration.value;
            if (!value_node) return true;
            if (!codegen_expression_allocation_free(codegen, value_node)) return false;
            GrayType *value_type = type_table_get(codegen->type_table, value_node);
            return codegen_type_is_copy_free(value_type);
        }
        case NODE_ASSIGN_STATEMENT: {
            if (!codegen_expression_allocation_free(codegen, statement->data.assign.target)) return false;
            if (!codegen_expression_allocation_free(codegen, statement->data.assign.value)) return false;
            /* `s += t` on a string is a concat */
            if (statement->data.assign.operator == TOKEN_PLUS_ASSIGN
                && codegen_expression_is_string(codegen, statement->data.assign.target))
                return false;
            GrayType *statement_type = type_table_get(codegen->type_table, statement->data.assign.target);
            return codegen_type_is_copy_free(statement_type);
        }
        case NODE_EXPRESSION_STATEMENT:
            return codegen_expression_allocation_free(codegen, statement->data.expression_statement.expression);
        case NODE_IF_STATEMENT:
            return codegen_expression_allocation_free(codegen, statement->data.if_statement.condition)
                && block_allocation_free(codegen, statement->data.if_statement.consequence)
                && (!statement->data.if_statement.alternative
                    || (statement->data.if_statement.alternative->kind == NODE_IF_STATEMENT
                            ? codegen_statement_allocation_free(codegen, statement->data.if_statement.alternative)
                            : block_allocation_free(codegen, statement->data.if_statement.alternative)));
        case NODE_FOR_STATEMENT: {
            AstNode *iterable_node = statement->data.for_statement.iterable;
            return iterable_node && iterable_node->kind == NODE_RANGE_EXPRESSION
                && codegen_expression_allocation_free(codegen, iterable_node->data.range_expression.start)
                && codegen_expression_allocation_free(codegen, iterable_node->data.range_expression.end)
                && codegen_expression_allocation_free(codegen, iterable_node->data.range_expression.step)
                && block_allocation_free(codegen, statement->data.for_statement.body);
        }
        case NODE_WHILE_STATEMENT:
            return codegen_expression_allocation_free(codegen, statement->data.while_statement.condition)
                && block_allocation_free(codegen, statement->data.while_statement.body);
        case NODE_LOOP_STATEMENT:
            return block_allocation_free(codegen, statement->data.loop_statement.body);
        case NODE_BREAK_STATEMENT: case NODE_CONTINUE_STATEMENT:
            return true;
        default:
            /* when / for_each / return / ensure / bare block — keep the arena */
            return false;
    }
}

/* True when a non-void function can use the void watermark path (no private
 * 64 KB _func_arena on every call) instead of a per-call arena
 * create/destroy/free. Safe only when the return value cannot carry arena
 * memory and the body allocates nothing: a single copy-free scalar return
 * (no named returns, no wide integer), and every body statement allocation-free —
 * which, via codegen_statement_allocation_free, also rules out every call, so nothing the
 * body touches can escape or dangle when the watermark is restored. */
static bool function_uses_watermark(CodeGen *codegen, AstNode *node) {
    if (!node || node->kind != NODE_FUNCTION_DECLARATION) return false;
    if (node->data.function_declaration.return_type_count != 1) return false;
    /* return_names is allocated even for an unnamed return; entry 0 is NULL
     * unless the return value was actually given a name. */
    if (node->data.function_declaration.return_names && node->data.function_declaration.return_names[0])
        return false;
    if (function_uses_caller_arena(codegen, node)) return false;

    const char *return_type_name = node->data.function_declaration.return_types[0];
    if (!return_type_name || is_wide_integer_type_name(return_type_name)) return false;
    GrayType *return_type = type_from_name(return_type_name);
    if (!return_type) return false;
    switch (return_type->kind) {
        case TYPE_KIND_SIGNED_INTEGER: case TYPE_KIND_UNSIGNED_INTEGER: case TYPE_KIND_FLOATING_POINT:
        case TYPE_KIND_BOOL: case TYPE_KIND_CHAR:
            break;
        default:
            /* string / struct / array / map / error: the value or its fields
             * may point into the function arena */
            return false;
    }

    AstNode *body = node->data.function_declaration.body;
    if (!body || body->kind != NODE_BLOCK_STATEMENT) return false;
    codegen->watermark_probe++;
    bool is_allocation_free = true;
    for (int i = 0; i < body->data.block.count && is_allocation_free; i++) {
        AstNode *statement = body->data.block.statements[i];
        if (statement && statement->kind == NODE_RETURN_STATEMENT) {
            for (int j = 0; j < statement->data.return_statement.count && is_allocation_free; j++)
                is_allocation_free = codegen_expression_allocation_free(codegen, statement->data.return_statement.values[j]);
        } else {
            is_allocation_free = codegen_statement_allocation_free(codegen, statement);
        }
    }
    codegen->watermark_probe--;
    return is_allocation_free;
}

/* True when every statement in a block is provably allocation-free, so
 * codegen can omit all per-iteration and per-loop arena management. */
static bool block_allocation_free(CodeGen *codegen, AstNode *body) {
    if (!body || body->kind != NODE_BLOCK_STATEMENT) return false;
    for (int i = 0; i < body->data.block.count; i++) {
        if (!codegen_statement_allocation_free(codegen, body->data.block.statements[i])) return false;
    }
    return true;
}

/* The iteration scratch arena is created once before the loop and destroyed
 * once after; each iteration only rewinds it (bump-pointer reset, no allocator
 * traffic). emit_loop_arena_prologue runs at the caller's indent before the
 * loop header, emit_loop_arena_epilogue after the closing brace, and
 * emit_loop_body_with_arena emits the per-iteration reset + the body. A
 * caller-arena function opens no per-scope arenas at all, and neither does a
 * loop whose body is allocation-free (no_arena). */
static bool loop_arena_active(CodeGen *codegen) {
    return !current_function_uses_caller_arena(codegen);
}

static void emit_loop_arena_prologue(CodeGen *codegen, bool no_arena) {
    if (no_arena || !loop_arena_active(codegen)) return;
    int depth = codegen->loop_scope_depth;
    /* Wrap the whole loop in its own C block so the arena locals below are
     * scoped to this loop — two sibling loops at the same depth would
     * otherwise redeclare _iter_arena_<depth>. */
    emit_indent(codegen);
    emit(codegen, "{\n");
    codegen->indent++;
    if (depth == 0) {
        emit_indent(codegen);
        emit(codegen, "GrayArena *_gray_outer_arena = gray_default_arena;\n");
    }
    emit_indent(codegen);
    emit_formatted(codegen, "GrayArena *_iter_arena_%d = gray_arena_create(%d);\n", depth, LOOP_ARENA_SIZE);
    emit_indent(codegen);
    emit_formatted(codegen, "GrayArena *_saved_arena_%d = gray_default_arena;\n", depth);
}

static void emit_loop_arena_epilogue(CodeGen *codegen, bool no_arena) {
    if (no_arena || !loop_arena_active(codegen)) return;
    int depth = codegen->loop_scope_depth;
    emit_indent(codegen);
    emit_formatted(codegen, "gray_arena_destroy(_iter_arena_%d, __FILE__, __LINE__); free(_iter_arena_%d);\n", depth, depth);
    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
}

/* Emit the per-iteration arena reset and the loop body.
 * Caller is responsible for indent++ before and indent--/closing brace after,
 * and for emit_loop_arena_prologue/epilogue around the loop. */
static void emit_loop_body_with_arena(CodeGen *codegen, AstNode *body, bool no_arena) {
    int previous_raw_variable_count = codegen->raw_variable_count;
    if (no_arena || current_function_uses_caller_arena(codegen)) {
        bool was_in_no_arena_loop = codegen->is_in_no_arena_loop;
        codegen->is_in_no_arena_loop = true;
        emit_block(codegen, body);
        codegen->is_in_no_arena_loop = was_in_no_arena_loop;
        codegen->raw_variable_count = previous_raw_variable_count;
        return;
    }
    int depth = codegen->loop_scope_depth;
    emit_indent(codegen);
    emit_formatted(codegen, "gray_arena_reset(_iter_arena_%d); gray_default_arena = _iter_arena_%d;\n", depth, depth);
    codegen->loop_scope_depth++;
    {
        char arena_variable[SHORT_VARIABLE_BUFFER_SIZE], saved_variable[SHORT_VARIABLE_BUFFER_SIZE];
        snprintf(arena_variable, sizeof(arena_variable), "_iter_arena_%d", depth);
        snprintf(saved_variable, sizeof(saved_variable), "_saved_arena_%d", depth);
        scope_arena_push(codegen, arena_variable, saved_variable);
    }
    emit_block(codegen, body);
    codegen->loop_scope_depth--;
    codegen->raw_variable_count = previous_raw_variable_count;
    scope_arena_pop(codegen);
    emit_indent(codegen);
    emit_formatted(codegen, "gray_default_arena = _saved_arena_%d;\n", depth);
}

static void emit_for_statement(CodeGen *codegen, AstNode *node) {
    bool no_arena = block_allocation_free(codegen, node->data.for_statement.body);
    emit_loop_arena_prologue(codegen, no_arena);
    emit_indent(codegen);

    AstNode *iterable_node = node->data.for_statement.iterable;
    const char *wide = NULL;
    if (iterable_node && iterable_node->kind == NODE_RANGE_EXPRESSION) {
        /* for i in range(start, end) or range(start, end, step) */
        char blank_for_variable[VARIABLE_NAME_BUFFER_SIZE];
        const char *variable_name;
        if (strcmp(node->data.for_statement.variable_name, "_") == 0) {
            snprintf(blank_for_variable, sizeof(blank_for_variable), "_gray_for_blank_%d", codegen_next_id(codegen));
            variable_name = blank_for_variable;
        } else {
            variable_name = sanitize_name(node->data.for_statement.variable_name);
        }

        /* A wide (i128/u128/i256/u256) range: bounds are held and stepped in
         * the wide type, and the loop variable is one. */
        {
            AstNode *bounds[] = { iterable_node->data.range_expression.start, iterable_node->data.range_expression.end,
                                  iterable_node->data.range_expression.step };
            for (int bound_index = 0; bound_index < 3 && !wide; bound_index++)
                wide = bounds[bound_index] ? resolve_wide_integer_type(codegen, bounds[bound_index]) : NULL;
        }

        if (wide) {
            const char *prefix = wide_integer_prefix(wide);
            AstNode *start = iterable_node->data.range_expression.start;
            AstNode *step = iterable_node->data.range_expression.step;
            int range_end_id = codegen_next_id(codegen);
            emit_formatted(codegen, "%s _gray_end_%d = ", prefix, range_end_id);
            if (!emit_wide_integer_coerced(codegen, wide, iterable_node->data.range_expression.end))
                emit_expression(codegen, iterable_node->data.range_expression.end);
            emit(codegen, ";\n");
            if (step) {
                emit_indent(codegen);
                emit_formatted(codegen, "%s _gray_step_%d = ", prefix, range_end_id);
                if (!emit_wide_integer_coerced(codegen, wide, step))
                    emit_expression(codegen, step);
                emit(codegen, ";\n");
                emit_indent(codegen);
                emit_formatted(codegen, "if (%s_eq(_gray_step_%d, %s_from_u64(0))) { %s; }\n",
                    prefix, range_end_id, prefix, panic_call(codegen, node, "P0090", ""));
            }
            emit_indent(codegen);
            emit_formatted(codegen, "for (%s %s = ", prefix, variable_name);
            if (!start) {
                emit_formatted(codegen, "%s_from_u64(0)", prefix);
            } else if (!emit_wide_integer_coerced(codegen, wide, start)) {
                emit_expression(codegen, start);
            }
            /* An unsigned step is never negative, so only a signed one needs
             * its direction read at run time. */
            if (step && wide[0] == 'i') {
                emit_formatted(codegen,
                    "; %s_gt(_gray_step_%d, %s_from_u64(0)) ? %s_lt(%s, _gray_end_%d) : %s_gt(%s, _gray_end_%d)",
                    prefix, range_end_id, prefix, prefix, variable_name, range_end_id, prefix, variable_name, range_end_id);
            } else {
                emit_formatted(codegen, "; %s_lt(%s, _gray_end_%d)", prefix, variable_name, range_end_id);
            }
            emit_formatted(codegen, "; %s = %s_add_checked(%s, ", variable_name, prefix, variable_name);
            if (step) {
                emit_formatted(codegen, "_gray_step_%d", range_end_id);
            } else {
                emit_formatted(codegen, "%s_from_u64(1)", prefix);
            }
            emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
        } else if (iterable_node->data.range_expression.start) {
            /* range(start, end) or range(start, end, step) */
            /* Determine comparison direction: static for literal step, runtime ternary for variable. */
            bool is_negative_step = false;
            bool known_direction = false;
            bool zero_step = false;
            if (iterable_node->data.range_expression.step) {
                AstNode *step = iterable_node->data.range_expression.step;
                if (step->kind == NODE_INTEGER_LITERAL) {
                    known_direction = true;
                    is_negative_step = step->data.integer_literal.value < 0;
                    zero_step = (step->data.integer_literal.value == 0);
                } else if (step->kind == NODE_PREFIX_EXPRESSION && step->data.prefix.operator == TOKEN_MINUS) {
                    known_direction = true;
                    is_negative_step = true;
                }
            }

            if (iterable_node->data.range_expression.step && !known_direction) {
                /* Variable step: store step and end once, emit runtime direction ternary. */
                int range_step_id = codegen_next_id(codegen);
                /* emit_indent already called above — use it for the variable declaration line */
                emit_formatted(codegen, "int64_t _gray_step_%d = ", range_step_id);
                emit_expression(codegen, iterable_node->data.range_expression.step);
                emit_formatted(codegen, ", _gray_end_%d = ", range_step_id);
                emit_expression(codegen, iterable_node->data.range_expression.end);
                emit(codegen, ";\n");
                /* P0090: zero step at runtime is always a panic */
                emit_indent(codegen);
                emit_formatted(codegen, "if (_gray_step_%d == 0) { %s; }\n", range_step_id, panic_call(codegen, node, "P0090", ""));
                emit_indent(codegen);
                emit_formatted(codegen, "for (int64_t %s = ", variable_name);
                emit_expression(codegen, iterable_node->data.range_expression.start);
                emit_formatted(codegen, "; _gray_step_%d > 0 ? %s < _gray_end_%d : %s > _gray_end_%d", range_step_id, variable_name, range_step_id, variable_name, range_step_id);
                emit_formatted(codegen, "; %s = gray_add_check(%s, _gray_step_%d, \"%s\", %d)", variable_name, variable_name, range_step_id, codegen->file, node->token.line);
            } else if (zero_step) {
                /* P0090: literal zero step always panics; emit panic then a dead loop */
                emit_formatted(codegen, "%s;\n", panic_call(codegen, node, "P0090", ""));
                emit_indent(codegen);
                emit_formatted(codegen, "for (int64_t %s = 0; 0; (void)0", variable_name);
            } else {
                /* The end (and a non-literal step) is evaluated once, before
                 * the loop, not on every iteration. */
                AstNode *step = iterable_node->data.range_expression.step;
                bool hoist_step = step && step->kind != NODE_INTEGER_LITERAL;
                int iterator_end_id = codegen_next_id(codegen);
                emit_formatted(codegen, "__auto_type _gray_end_%d = ", iterator_end_id);
                emit_expression(codegen, iterable_node->data.range_expression.end);
                emit(codegen, ";\n");
                if (hoist_step) {
                    emit_indent(codegen);
                    emit_formatted(codegen, "__auto_type _gray_step_%d = ", iterator_end_id);
                    emit_expression(codegen, step);
                    emit(codegen, ";\n");
                }
                emit_indent(codegen);
                emit_formatted(codegen, "for (int64_t %s = ", variable_name);
                emit_expression(codegen, iterable_node->data.range_expression.start);
                emit_formatted(codegen, "; %s %s _gray_end_%d; %s", variable_name, is_negative_step ? ">" : "<", iterator_end_id, variable_name);
                if (step) {
                    emit_formatted(codegen, " = gray_add_check(%s, ", variable_name);
                    if (hoist_step) {
                        emit_formatted(codegen, "_gray_step_%d", iterator_end_id);
                    } else {
                        emit_expression(codegen, step);
                    }
                    emit_formatted(codegen, ", \"%s\", %d)", codegen->file, node->token.line);
                } else {
                    emit(codegen, "++");
                }
            }
        } else {
            /* range(end) - start at 0 */
            int iterator_end_id = codegen_next_id(codegen);
            emit_formatted(codegen, "__auto_type _gray_end_%d = ", iterator_end_id);
            emit_expression(codegen, iterable_node->data.range_expression.end);
            emit(codegen, ";\n");
            emit_indent(codegen);
            emit_formatted(codegen, "for (int64_t %s = 0; %s < _gray_end_%d; %s++", variable_name, variable_name, iterator_end_id, variable_name);
        }

        emit(codegen, ") {\n");
    } else {
        codegen_internal_error("non-range for loop reached codegen",
                               codegen->file, node->token.line);
    }

    int previous_wide_integer_variable_count = codegen->wide_integer_variable_count;
    if (wide)
        register_wide_integer_variable(codegen, node->data.for_statement.variable_name, wide_integer_type_name(wide));
    codegen->indent++;
    emit_loop_body_with_arena(codegen, node->data.for_statement.body, no_arena);
    codegen->indent--;
    codegen->wide_integer_variable_count = previous_wide_integer_variable_count;
    emit_indent(codegen);
    emit(codegen, "}\n");
    emit_loop_arena_epilogue(codegen, no_arena);
}

static void emit_while_statement(CodeGen *codegen, AstNode *node) {
    bool no_arena = block_allocation_free(codegen, node->data.while_statement.body);
    emit_loop_arena_prologue(codegen, no_arena);
    emit_indent(codegen);
    emit(codegen, "while (");
    emit_expression(codegen, node->data.while_statement.condition);
    emit(codegen, ") {\n");

    codegen->indent++;
    emit_loop_body_with_arena(codegen, node->data.while_statement.body, no_arena);
    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
    emit_loop_arena_epilogue(codegen, no_arena);
}

static void emit_loop_statement(CodeGen *codegen, AstNode *node) {
    bool no_arena = block_allocation_free(codegen, node->data.loop_statement.body);
    emit_loop_arena_prologue(codegen, no_arena);
    emit_indent(codegen);
    emit(codegen, "for (;;) {\n");

    codegen->indent++;
    emit_loop_body_with_arena(codegen, node->data.loop_statement.body, no_arena);
    codegen->indent--;
    emit_indent(codegen);
    emit(codegen, "}\n");
    emit_loop_arena_epilogue(codegen, no_arena);
}

/* extract the base (unmangled) function name for multi-return
 * struct references. The monomorphiser temporarily renames functions
 * to `<name>__<binding>`, but the GrayMulti typedef is emitted once
 * under the original name. Returns a pointer to a small ring of
 * static buffers so a few concurrent uses stay alive. */
static const char *multi_return_base_name(const char *function_name) {
    static char buffers[4][MESSAGE_BUFFER_SIZE];
    static int buffer_slot = 0;
    char *output = buffers[buffer_slot]; buffer_slot = (buffer_slot + 1) & 3;
    const char *dunder = strstr(function_name, "__");
    if (dunder) {
        size_t prefix_length = (size_t)(dunder - function_name);
        if (prefix_length >= sizeof(buffers[0])) prefix_length = sizeof(buffers[0]) - 1;
        memcpy(output, function_name, prefix_length);
        output[prefix_length] = '\0';
    } else {
        strncpy(output, function_name, sizeof(buffers[0]) - 1);
        output[sizeof(buffers[0]) - 1] = '\0';
    }
    return output;
}

/*  + : pick the right multi-return struct name. Use
 * the full mangled name when return types contain '?'
 * (per-instantiation struct), base name otherwise (shared). */
static const char *multi_return_name(AstNode *function_node) {
    bool has_wildcard = false;
    for (int i = 0; i < function_node->data.function_declaration.return_type_count; i++) {
        if (function_node->data.function_declaration.return_types[i] &&
            strchr(function_node->data.function_declaration.return_types[i], '?')) {
            has_wildcard = true;
            break;
        }
    }
    return has_wildcard ? function_node->data.function_declaration.name
                  : multi_return_base_name(function_node->data.function_declaration.name);
}

/* Build a multi-return type name like GrayMulti_add */
static void emit_multi_return_typedef(CodeGen *codegen, AstNode *node) {
    emit_formatted(codegen, "typedef struct {\n");
    for (int i = 0; i < node->data.function_declaration.return_type_count; i++) {
        emit_formatted(codegen, "    %s v%d;\n",
            gray_type_to_c_codegen(codegen, node->data.function_declaration.return_types[i]), i);
    }
    emit_formatted(codegen, "} GrayMulti_%s;\n\n", node->data.function_declaration.name);
}

static const char *function_return_type(CodeGen *codegen, AstNode *node) {
    if (node->data.function_declaration.return_type_count == 0) return "void";
    if (node->data.function_declaration.return_type_count == 1) {
        return gray_type_to_c_codegen(codegen, node->data.function_declaration.return_types[0]);
    }
    /*  + : for multi-return, use `GrayMulti_<name>`. The
     * monomorphiser temporarily renames the func to `<name>__<binding>`.
     * When return types DON'T contain '?', all instantiations share one
     * struct → use the base name ). When return types DO contain
     * '?', each instantiation gets its own struct → use the full
     * mangled name ). */
    static char buffer[MESSAGE_BUFFER_SIZE];
    const char *function_name = node->data.function_declaration.name;
    bool has_wildcard_return = false;
    for (int i = 0; i < node->data.function_declaration.return_type_count; i++) {
        if (node->data.function_declaration.return_types[i] &&
            strchr(node->data.function_declaration.return_types[i], '?')) {
            has_wildcard_return = true;
            break;
        }
    }
    if (!has_wildcard_return) {
        /* strip __<binding> suffix; shared struct. */
        snprintf(buffer, sizeof(buffer), "GrayMulti_%s", multi_return_base_name(function_name));
    } else {
        /* use the full (possibly mangled) name; per-instantiation
         * struct. The wildcard_binding is active so gray_type_to_c_codegen will
         * substitute '?' in the struct fields. */
        snprintf(buffer, sizeof(buffer), "GrayMulti_%s", function_name);
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
            codegen_declaration_name(codegen, node, node->data.function_declaration.name));

        /* Parameters — skip type params (erased in C) */
        bool is_first_parameter = true;
        for (int i = 0; i < node->data.function_declaration.parameter_count; i++) {
            Parameter *parameter = &node->data.function_declaration.parameters[i];
            if (parameter->is_type_parameter) continue;
            if (!is_first_parameter) emit(codegen, ", ");
            is_first_parameter = false;
            if (parameter->is_mutable) {
                emit_formatted(codegen, "%s *%s", gray_type_to_c_codegen(codegen,parameter->type_name), sanitize_name(parameter->name));
            } else {
                emit_formatted(codegen, "%s %s", gray_type_to_c_codegen(codegen,parameter->type_name), sanitize_name(parameter->name));
            }
        }

        if (is_first_parameter) {
            emit(codegen, "void");
        }
        emit(codegen, ")");
    }

    emit(codegen, " {\n");
    codegen->indent++;

    /* scope-based memory management.
     * Void functions: save/restore arena watermark to free temporaries.
     * Non-void functions: create a per-function arena so temporaries
     * are freed, and escape the return value to the caller's arena. */
    bool is_void_function = (node->data.function_declaration.return_type_count == 0);
    bool caller_arena = function_uses_caller_arena(codegen, node);
    /* A non-void function that provably allocates nothing and returns a
     * copy-free scalar needs no private arena — the watermark is enough,
     * exactly as for a void function. See function_needs_scope_mark: that
     * non-void watermark path never actually takes a mark, since it is only
     * reached when the whole body is alloc-free — skipping the save/restore
     * pair there (and the void case when its own body is alloc-free too)
     * avoids emitting it, and the C compiler work of optimizing it away, at
     * every call site of a function that never allocates. */
    bool uses_watermark_function = is_void_function || function_uses_watermark(codegen, node);
    bool needs_scope_mark = function_needs_scope_mark(codegen, node);
    if (!is_main && !caller_arena) {
        if (uses_watermark_function) {
            if (needs_scope_mark) {
                emit_indent(codegen);
                emit(codegen, "GrayScopeMark _scope_mark = gray_scope_save(gray_default_arena);\n");
            }
        } else {
            emit_indent(codegen);
            emit_formatted(codegen, "GrayArena *_func_arena = gray_arena_create(%d);\n", FUNCTION_ARENA_SIZE);
            emit_indent(codegen);
            emit(codegen, "GrayArena *_func_saved = gray_default_arena;\n");
            emit_indent(codegen);
            emit(codegen, "gray_default_arena = _func_arena;\n");
        }
    }

    AstNode *previous_function = codegen->current_function;
    int previous_using_count = codegen->using_module_count;
    int previous_reference_variable_count = codegen->reference_variable_count;
    int previous_raw_variable_count = codegen->raw_variable_count;
    int previous_wide_integer_variable_count = codegen->wide_integer_variable_count;
    int previous_iteration_guard_count = codegen->iteration_guard_count;
    int previous_ensure_reached = codegen->ensure_reached;
    codegen->current_function = node;
    codegen->ensure_reached = 0;

    /* Point codegen->file at this function's own module for the duration of
     * the body, so panic-location arguments (gray_panic_code_at, the sized/
     * bounds/nil checks, gray_enter_func) name the file the code lives in
     * rather than the entry file in a multi-file build. */
    const char *previous_file = codegen->file;
    char *function_file = node->token.file ? normalize_path_separators(node->token.file) : NULL;
    if (function_file) codegen->file = function_file;

    /* Register wide integer parameters for type tracking */
    for (int i = 0; i < node->data.function_declaration.parameter_count; i++) {
        Parameter *parameter = &node->data.function_declaration.parameters[i];
        if (parameter->type_name && is_wide_integer_type_name(parameter->type_name)) {
            register_wide_integer_variable(codegen, parameter->name, parameter->type_name);
        }
    }

    /* Named return variables are declared by the user in the function body,
     * not auto-generated. E3080 enforces the correct variable is returned. */

    if (node->data.function_declaration.body) {
        /* Stack depth guard */
        emit_indent(codegen);
        emit_formatted(codegen, "gray_enter_func(\"%s\", %d);\n", codegen->file, node->token.line);
        emit_block(codegen, node->data.function_declaration.body);
        /* Emit ensure cleanup at end of function (for implicit returns) */
        emit_ensure_cleanup(codegen);
        /* cleanup function-scoped memory */
        if (!is_main && !caller_arena) {
            if (uses_watermark_function) {
                if (needs_scope_mark) {
                    emit_indent(codegen);
                    emit(codegen, "gray_scope_restore(gray_default_arena, _scope_mark);\n");
                }
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
    codegen->current_function = previous_function;
    codegen->ensure_reached = previous_ensure_reached;
    codegen->using_module_count = previous_using_count;
    codegen->reference_variable_count = previous_reference_variable_count;
    codegen->raw_variable_count = previous_raw_variable_count;
    codegen->wide_integer_variable_count = previous_wide_integer_variable_count;
    codegen->iteration_guard_count = previous_iteration_guard_count;
    codegen->file = previous_file;
    free(function_file);
    codegen->indent--;
    emit(codegen, "}\n\n");
    /* Whatever comes next — another function's own prologue, or top-level
     * scaffolding — has no .gray origin of its own until the next statement
     * stamps one; don't let it inherit this function's last .gray location. */
    reset_line_directive(codegen);
}

static void emit_expression_statement(CodeGen *codegen, AstNode *node) {
    emit_indent(codegen);
    emit_expression(codegen, node->data.expression_statement.expression);
    emit(codegen, ";\n");
}

/* ── for_each helpers ──────────────────────────────────────────────── */

static void emit_foreach_map(CodeGen *codegen, AstNode *node, AstNode *coll,
                              GrayType *collection_type, const char *index_name,
                              bool *out_map_needs_temporary, char *map_temporary_name,
                              size_t map_temporary_size) {
    int mi_id = codegen_next_id(codegen);
    char mi_name[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(mi_name, sizeof(mi_name), "_gray_mi%d", mi_id);

    const char *c_key_type = "GrayString";
    const char *c_value_type = "int64_t";
    if (collection_type->key_type) c_key_type = gray_map_element_c_type(codegen, collection_type->key_type);
    if (collection_type->value_type) c_value_type = gray_map_element_c_type(codegen, collection_type->value_type);

    *out_map_needs_temporary = (coll->kind != NODE_LABEL);
    if (*out_map_needs_temporary) {
        snprintf(map_temporary_name, map_temporary_size, "_gray_map%d", mi_id);
        emit_formatted(codegen, "{ GrayMap %s = ", map_temporary_name);
        emit_expression(codegen, coll);
        emit(codegen, ";\n");
        emit_indent(codegen);
    }

    char slot_name[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(slot_name, sizeof(slot_name), "_gray_sl%d", mi_id);
    {
        char *guard_expression = iteration_guard_expression(codegen, *out_map_needs_temporary, map_temporary_name, coll);
        iteration_guard_push(codegen, guard_expression);
        free(guard_expression);
    }
    if (*out_map_needs_temporary) emit_formatted(codegen, "gray_atomic_add32(&%s.iterating, 1);\n", map_temporary_name);
    else { emit(codegen, "gray_atomic_add32(&"); emit_expression(codegen, coll); emit(codegen, ".iterating, 1);\n"); }
    emit_indent(codegen);
    emit_formatted(codegen, "for (int32_t %s = 0; %s < ", mi_name, mi_name);
    if (*out_map_needs_temporary) emit_formatted(codegen, "%s", map_temporary_name);
    else emit_expression(codegen, coll);
    emit_formatted(codegen, ".order_len; %s++) {\n", mi_name);
    codegen->indent++;
    emit_indent(codegen);
    emit_formatted(codegen, "int32_t %s = ", slot_name);
    if (*out_map_needs_temporary) emit_formatted(codegen, "%s", map_temporary_name);
    else emit_expression(codegen, coll);
    emit_formatted(codegen, ".order[%s];\n", mi_name);
    emit_indent(codegen);
    emit_formatted(codegen, "if (%s < 0) continue;\n", slot_name);

    if (node->data.for_each.index_name) {
        if (strcmp(node->data.for_each.index_name, "_") != 0) {
            emit_indent(codegen);
            emit_formatted(codegen, "%s %s = *(%s *)gray_map_key_at(&",
                c_key_type, sanitize_name(node->data.for_each.index_name), c_key_type);
            if (*out_map_needs_temporary) emit_formatted(codegen, "%s", map_temporary_name);
            else emit_expression(codegen, coll);
            emit_formatted(codegen, ", %s);\n", slot_name);
        }
        if (strcmp(node->data.for_each.variable_name, "_") != 0) {
            emit_indent(codegen);
            emit_formatted(codegen, "%s %s = *(%s *)gray_map_value_at(&",
                c_value_type, sanitize_name(node->data.for_each.variable_name), c_value_type);
            if (*out_map_needs_temporary) emit_formatted(codegen, "%s", map_temporary_name);
            else emit_expression(codegen, coll);
            emit_formatted(codegen, ", %s);\n", slot_name);
        }
    } else {
        emit_indent(codegen);
        emit_formatted(codegen, "%s %s = *(%s *)gray_map_key_at(&",
            c_key_type, sanitize_name(node->data.for_each.variable_name), c_key_type);
        if (*out_map_needs_temporary) emit_formatted(codegen, "%s", map_temporary_name);
        else emit_expression(codegen, coll);
        emit_formatted(codegen, ", %s);\n", slot_name);
    }

    /* A wide-integer key or value binds as a struct, not int64_t; track it so
     * reads of the loop variable resolve to the wide integer type. The caller
     * restores wide_integer_variable_count after the loop body, so these are loop-scoped. */
    const char *key_bind = node->data.for_each.index_name
        ? node->data.for_each.index_name : node->data.for_each.variable_name;
    const char *value_binding = node->data.for_each.index_name
        ? node->data.for_each.variable_name : NULL;
    const char *key_wide_integer = wide_integer_type_name(collection_type->key_type);
    const char *value_wide_integer = wide_integer_type_name(collection_type->value_type);
    if (key_wide_integer && key_bind && strcmp(key_bind, "_") != 0)
        register_wide_integer_variable(codegen, key_bind, key_wide_integer);
    if (value_wide_integer && value_binding && strcmp(value_binding, "_") != 0)
        register_wide_integer_variable(codegen, value_binding, value_wide_integer);
}

static void emit_foreach_string(CodeGen *codegen, AstNode *node, AstNode *coll,
                                 const char *index_name) {
    emit_formatted(codegen, "{ GrayString _gray_str = ");
    emit_expression(codegen, coll);
    emit(codegen, ";\n");
    emit_indent(codegen);
    emit_formatted(codegen, "for (int32_t %s = 0; %s < _gray_str.len; %s++) {\n", index_name, index_name, index_name);
    codegen->indent++;
    emit_indent(codegen);
    /* GrayString.data is char* (signed); widen the byte unsigned so a byte
     * >= 0x80 matches what s[i] indexing yields, not a negative codepoint. */
    emit_formatted(codegen, "int32_t %s = (unsigned char)_gray_str.data[%s];\n", sanitize_name(node->data.for_each.variable_name), index_name);
}

static void emit_foreach_array(CodeGen *codegen, AstNode *node, AstNode *coll,
                                GrayType *collection_type, const char *index_name,
                                bool *out_collection_needs_temporary, char *array_temporary_name,
                                size_t array_temporary_size) {
    const char *c_element_type = "int64_t";
    if (collection_type && collection_type->kind == TYPE_KIND_ARRAY && collection_type->element_type) {
        const char *element_type_name = codegen_effective_type_string(codegen, collection_type->element_type);
        GrayType *element_type = type_from_name(element_type_name);
        if (element_type->kind == TYPE_KIND_FLOATING_POINT) c_element_type = (strcmp(element_type_name, "f32") == 0) ? "float" : "double";
        else if (element_type->kind == TYPE_KIND_BOOL) c_element_type = "bool";
        else if (element_type->kind == TYPE_KIND_STRING) c_element_type = "GrayString";
        else if (element_type->kind == TYPE_KIND_ARRAY) c_element_type = "GrayArray";
        else if (element_type->kind == TYPE_KIND_MAP) c_element_type = "GrayMap";
        else if (element_type->kind == TYPE_KIND_STRUCT) c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        else if (element_type->kind == TYPE_KIND_POINTER) c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        else if (element_type->kind == TYPE_KIND_CHAR) c_element_type = "int32_t";
        /* Wide integers are TYPE_KIND_SIGNED_INTEGER/TYPE_KIND_UNSIGNED_INTEGER in the type system but structs in C;
         * the element is stored packed as that struct, like a map value. */
        else if (is_wide_integer_type_name(element_type_name)) c_element_type = wide_integer_prefix(element_type_name);
        /* Sized integer element types are stored packed by cast(arr, [T]); the
         * int64_t fall-through would stride past the buffer. Match storage. */
        else if (element_type->kind == TYPE_KIND_SIGNED_INTEGER || element_type->kind == TYPE_KIND_UNSIGNED_INTEGER)
            c_element_type = gray_type_to_c_codegen(codegen, element_type_name);
        else if (element_type->kind == TYPE_KIND_ENUM) {
            c_element_type = codegen_enum_is_string(codegen, element_type_name)
                ? "GrayString" : gray_type_to_c_codegen(codegen, element_type_name);
        }
    }

    int array_length_id = codegen_next_id(codegen);
    char length_name[SHORT_VARIABLE_BUFFER_SIZE];
    snprintf(length_name, sizeof(length_name), "_gray_alen%d", array_length_id);
    *out_collection_needs_temporary = (coll->kind != NODE_LABEL);
    /* A field path names an array the caller can still reach, so the guard
     * has to sit on that array — putting it on the snapshot below would let
     * destructive mutations through unnoticed. */
    bool guard_real = *out_collection_needs_temporary && is_stable_field_path(coll);
    char guard_pointer[SHORT_VARIABLE_BUFFER_SIZE];
    guard_pointer[0] = '\0';
    if (*out_collection_needs_temporary) {
        snprintf(array_temporary_name, array_temporary_size, "_gray_arr%d", array_length_id);
        emit(codegen, "{ ");
        if (guard_real) {
            snprintf(guard_pointer, sizeof(guard_pointer), "_gray_arrp%d", array_length_id);
            emit_formatted(codegen, "GrayArray *%s = &(", guard_pointer);
            emit_expression(codegen, coll);
            emit_formatted(codegen, "); GrayArray %s = *%s;\n", array_temporary_name, guard_pointer);
        } else {
            emit_formatted(codegen, "GrayArray %s = ", array_temporary_name);
            emit_expression(codegen, coll);
            emit(codegen, ";\n");
        }
        emit_indent(codegen);
    }
    emit_formatted(codegen, "{ int32_t %s = ", length_name);
    if (*out_collection_needs_temporary) emit_formatted(codegen, "%s.len;\n", array_temporary_name);
    else { emit_expression(codegen, coll); emit(codegen, ".len;\n"); }
    {
        char *guard_expression;
        if (guard_real) {
            char message[SHORT_VARIABLE_BUFFER_SIZE + 4];
            snprintf(message, sizeof(message), "(*%s)", guard_pointer);
            guard_expression = strdup(message);
        } else {
            guard_expression = iteration_guard_expression(codegen, *out_collection_needs_temporary, array_temporary_name, coll);
        }
        iteration_guard_push(codegen, guard_expression);
        free(guard_expression);
    }
    emit_indent(codegen);
    emit_formatted(codegen, "gray_atomic_add32(&%s.iterating, 1);\n", iteration_guard_top(codegen));
    emit_indent(codegen);
    emit_formatted(codegen, "for (int32_t %s = 0; %s < %s; %s++) {\n", index_name, index_name, length_name, index_name);
    codegen->indent++;
    emit_indent(codegen);
    emit_formatted(codegen, "%s %s = GRAY_ARRAY_GET_AT(", c_element_type, sanitize_name(node->data.for_each.variable_name));
    if (*out_collection_needs_temporary) emit_formatted(codegen, "%s, %s, %s, \"%s\", %d);\n", array_temporary_name, c_element_type, index_name, codegen->file, node->token.line);
    else { emit_expression(codegen, coll); emit_formatted(codegen, ", %s, %s, \"%s\", %d);\n", c_element_type, index_name, codegen->file, node->token.line); }

    /* Track a wide-integer element binding so reads resolve to the wide integer type.
     * The caller restores wide_integer_variable_count after the loop body. */
    if (collection_type && collection_type->kind == TYPE_KIND_ARRAY) {
        const char *element_wide_integer = wide_integer_type_name(collection_type->element_type);
        if (element_wide_integer && strcmp(node->data.for_each.variable_name, "_") != 0)
            register_wide_integer_variable(codegen, node->data.for_each.variable_name, element_wide_integer);
    }
}

/* True if evaluating this expression can reach C code that panics through
 * gray_panic_code() — which carries no location and relies on the per-statement
 * gray_panic_call_{file,line} stamp. That is any call, any allocation (new,
 * aggregate literals, string interpolation or a string literal that may be
 * concatenated), and casts. Scalar arithmetic, comparisons, label reads and
 * the located checks (indexing, member access, division, overflow) do not
 * need the stamp, so a statement built only from those can skip it. */
static bool expression_needs_panic_location(CodeGen *codegen, AstNode *expression) {
    if (!expression) return false;
    switch (expression->kind) {
    case NODE_CALL_EXPRESSION:
    case NODE_NEW_EXPRESSION:
    case NODE_INTERPOLATED_STRING:
    case NODE_STRING_VALUE:
    case NODE_ARRAY_VALUE:
    case NODE_MAP_VALUE:
    case NODE_STRUCT_VALUE:
    case NODE_CAST_EXPRESSION:
        return true;
    case NODE_PREFIX_EXPRESSION:
        return expression_needs_panic_location(codegen, expression->data.prefix.right);
    case NODE_POSTFIX_EXPRESSION:
        return expression_needs_panic_location(codegen, expression->data.postfix.left);
    case NODE_INFIX_EXPRESSION: {
        /* String '+' lowers to gray_string_concat, which allocates. */
        GrayType *type = type_table_get(codegen->type_table, expression);
        if (type && type->kind == TYPE_KIND_STRING) return true;
        return expression_needs_panic_location(codegen, expression->data.infix.left) ||
               expression_needs_panic_location(codegen, expression->data.infix.right);
    }
    case NODE_INDEX_EXPRESSION:
        return expression_needs_panic_location(codegen, expression->data.index_expression.left) ||
               expression_needs_panic_location(codegen, expression->data.index_expression.index);
    case NODE_MEMBER_EXPRESSION:
        return expression_needs_panic_location(codegen, expression->data.member.object);
    case NODE_RANGE_EXPRESSION:
        return expression_needs_panic_location(codegen, expression->data.range_expression.start) ||
               expression_needs_panic_location(codegen, expression->data.range_expression.end) ||
               expression_needs_panic_location(codegen, expression->data.range_expression.step);
    default:
        return false;
    }
}

/* Whether this statement needs its source location stamped for the runtime.
 * Only the expressions this statement evaluates directly are examined; nested
 * statements (loop and branch bodies) stamp themselves as they are emitted. */
static bool statement_needs_panic_location(CodeGen *codegen, AstNode *node) {
    switch (node->kind) {
    case NODE_VARIABLE_DECLARATION:
        return expression_needs_panic_location(codegen, node->data.variable_declaration.value);
    case NODE_ASSIGN_STATEMENT:
        return expression_needs_panic_location(codegen, node->data.assign.target) ||
               expression_needs_panic_location(codegen, node->data.assign.value);
    case NODE_RETURN_STATEMENT:
        for (int i = 0; i < node->data.return_statement.count; i++)
            if (expression_needs_panic_location(codegen, node->data.return_statement.values[i]))
                return true;
        return false;
    case NODE_EXPRESSION_STATEMENT:
        return expression_needs_panic_location(codegen, node->data.expression_statement.expression);
    case NODE_IF_STATEMENT:
        return expression_needs_panic_location(codegen, node->data.if_statement.condition);
    case NODE_WHILE_STATEMENT:
        return expression_needs_panic_location(codegen, node->data.while_statement.condition);
    case NODE_WHEN_STATEMENT:
        return expression_needs_panic_location(codegen, node->data.when_statement.value);
    case NODE_FOR_EACH_STATEMENT:
        return expression_needs_panic_location(codegen, node->data.for_each.collection);
    case NODE_BREAK_STATEMENT:
    case NODE_CONTINUE_STATEMENT:
        return false;
    default:
        /* NODE_FOR_STATEMENT, NODE_ENSURE_STATEMENT and anything not enumerated: keep the
         * stamp. These are rare relative to plain arithmetic statements and not
         * worth the risk of a stale location on a missed path. */
        return true;
    }
}

/* Emits `#line N "file"` at column 0 (a directive must not be indented).
 * file is codegen->file, already forward-slash normalized by
 * normalize_path_separators — never re-escaped here. */
static void emit_line_directive(CodeGen *codegen, const char *file, int line) {
    if (!codegen->should_emit_line_directives) return;
    emit_formatted(codegen, "#line %d \"%s\"\n", line, file);
}

/* Points subsequent generated C — compiler scaffolding with no .gray origin,
 * such as a function's own prologue before its first statement, or anything
 * emitted between one function body and the next — back at a synthetic
 * marker instead of letting it inherit whatever .gray location the last
 * directive named. Without this, a cc diagnostic in that scaffolding would
 * misreport a stale .gray file/line instead of the generated C. */
static void reset_line_directive(CodeGen *codegen) {
    if (!codegen->should_emit_line_directives) return;
    emit(codegen, "#line 1 \"<generated>\"\n");
    free(codegen->last_line_directive_file);
    codegen->last_line_directive_file = NULL;
    codegen->last_line_directive_line = 0;
}

static void emit_statement(CodeGen *codegen, AstNode *node) {
    codegen_enter_node(codegen, node);
    if (!node) return;

    /* Record this statement's source location for the runtime. A panic raised
     * from stdlib or builtin C code goes through gray_panic_code(), which has
     * no location of its own; it falls back to this so the user still sees the
     * .gray file and line, the same as a language-level panic. Only inside a
     * function body (indent > 0) — file-scope initializers cannot call, so
     * cannot panic this way. codegen->file is the enclosing function's own
     * module here (emit_function_declaration points it there). Statements that
     * evaluate only located operations skip the stamp. Emitted before the
     * #line directive below: it is itself scaffolding, not the statement's
     * own C, and must not shift the directive off the line it names. */
    if (codegen->indent > 0 && codegen->file && node->token.line > 0 &&
        statement_needs_panic_location(codegen, node)) {
        emit_indent(codegen);
        emit_formatted(codegen, "gray_panic_call_file = \"%s\"; gray_panic_call_line = %d;\n",
                       codegen->file, node->token.line);
    }

    /* Map this statement back to its .gray source for cc diagnostics,
     * sanitizers, and gcov. Only inside a function body (indent > 0) —
     * file-scope initializers are emitted in the preamble, which has no
     * .gray-mapped code around it. Skipped when the last directive already
     * named this exact file/line, so a run of statements sharing one source
     * line (or synthetic sub-statements) doesn't emit one directive each.
     * Emitted last, immediately before the statement's own C, so the line
     * it names is that C's actual line — not the panic stamp's. */
    if (codegen->indent > 0 && codegen->file && node->token.line > 0 &&
        (!codegen->last_line_directive_file ||
         codegen->last_line_directive_line != node->token.line ||
         strcmp(codegen->last_line_directive_file, codegen->file) != 0)) {
        emit_line_directive(codegen, codegen->file, node->token.line);
        free(codegen->last_line_directive_file);
        codegen->last_line_directive_file = strdup(codegen->file);
        codegen->last_line_directive_line = node->token.line;
    }

    switch (node->kind) {
    case NODE_VARIABLE_DECLARATION: {
        /* A module-level variable is emitted under the mangled name its
         * module gives it, which is what references to it resolve to. The
         * name is swapped in for the duration rather than threaded through
         * the fifteen places the emitter reads it — the same shape codegen
         * already uses for generic instantiations and struct namespacing. */
        DeclarationEntry *entry = module_table_entry_for_node(codegen->modules, node);
        const char *written = node->data.variable_declaration.name;
        if (entry && entry->kind == DECLARATION_CONST && !entry->is_module_entry)
            node->data.variable_declaration.name = module_mangle(codegen->modules, entry);
        else if (codegen->indent == 0 && !node->data.variable_declaration.is_synthetic)
            /* File-scope global in the entry module: gray_g_ prefix so the
             * name cannot collide with a libc identifier. References resolve
             * to the same prefixed name (label_is_entry_global). */
            node->data.variable_declaration.name = global_variable_c_name(codegen, written);
        emit_variable_declaration(codegen, node, written);
        node->data.variable_declaration.name = written;
        break;
    }
    case NODE_ASSIGN_STATEMENT:
    case NODE_EXPRESSION_STATEMENT: {
        /* A function's return rewinds the arena it ran in, but a module-level
         * container outlives every function. Run a store into one in the
         * heap arena, which is never rewound, so the copied map key, grown
         * backing store and stored element all survive the return. The
         * heap arena is null on a spawned thread, which keeps its own. */
        bool to_module = statement_stores_into_module_storage(codegen, node);
        if (to_module) {
            emit_indent(codegen);
            emit(codegen, "{ GrayArena *_gray_gsave = gray_default_arena; "
                          "if (gray_heap_arena) gray_default_arena = gray_heap_arena;\n");
            codegen->indent++;
        }
        if (node->kind == NODE_ASSIGN_STATEMENT) emit_assign_statement(codegen, node);
        else emit_expression_statement(codegen, node);
        if (to_module) {
            codegen->indent--;
            emit_indent(codegen);
            emit(codegen, "gray_default_arena = _gray_gsave; }\n");
        }
        break;
    }
    case NODE_RETURN_STATEMENT:
        emit_return_statement(codegen, node);
        break;
    case NODE_IF_STATEMENT:
        emit_if_statement(codegen, node);
        break;
    case NODE_FOR_STATEMENT:
        emit_for_statement(codegen, node);
        break;
    case NODE_FOR_EACH_STATEMENT: {
        /* for_each keeps its per-iteration arena unconditionally for now;
         * the non-allocating fast path covers for / while / loop only. */
        emit_loop_arena_prologue(codegen, false);
        emit_indent(codegen);
        AstNode *coll = node->data.for_each.collection;
        GrayType *collection_type = type_table_get(codegen->type_table, coll);

        const char *index_name = node->data.for_each.index_name;
        if (!index_name) index_name = "_gray_idx";
        bool is_map_iteration = (collection_type && collection_type->kind == TYPE_KIND_MAP);
        bool collection_needs_temporary = false;
        char array_temporary_name[SHORT_VARIABLE_BUFFER_SIZE];
        array_temporary_name[0] = '\0';
        bool map_needs_temporary = false;
        char map_temporary_name[SHORT_VARIABLE_BUFFER_SIZE];
        map_temporary_name[0] = '\0';

        /* The foreach emitters may register wide-integer loop bindings; drop
         * them again once the body is emitted so they stay loop-scoped. */
        int previous_wide_integer_variable_count = codegen->wide_integer_variable_count;

        if (is_map_iteration) {
            emit_foreach_map(codegen, node, coll, collection_type, index_name,
                             &map_needs_temporary, map_temporary_name, sizeof(map_temporary_name));
        } else if (collection_type && collection_type->kind == TYPE_KIND_STRING) {
            emit_foreach_string(codegen, node, coll, index_name);
        } else {
            emit_foreach_array(codegen, node, coll, collection_type, index_name,
                               &collection_needs_temporary, array_temporary_name, sizeof(array_temporary_name));
        }

        emit_loop_body_with_arena(codegen, node->data.for_each.body, false);
        codegen->wide_integer_variable_count = previous_wide_integer_variable_count;
        codegen->indent--;
        emit_indent(codegen);
        emit(codegen, "}\n");
        /* Decrement map iteration guard */
        if (is_map_iteration) {
            iteration_guard_pop(codegen);
            emit_indent(codegen);
            if (map_needs_temporary) emit_formatted(codegen, "gray_atomic_sub32(&%s.iterating, 1);\n", map_temporary_name);
            else { emit(codegen, "gray_atomic_sub32(&"); emit_expression(codegen, coll); emit(codegen, ".iterating, 1);\n"); }
            if (map_needs_temporary) {
                emit_indent(codegen);
                emit(codegen, "}\n");
            }
        }
        /* Close extra scope for string iteration */
        if (collection_type && collection_type->kind == TYPE_KIND_STRING) {
            emit_indent(codegen);
            emit(codegen, "}\n");
        }
        /* Decrement array iteration guard, then close the snapshot block */
        if (collection_type && collection_type->kind != TYPE_KIND_MAP && collection_type->kind != TYPE_KIND_STRING) {
            emit_indent(codegen);
            emit_formatted(codegen, "gray_atomic_sub32(&%s.iterating, 1);\n", iteration_guard_top(codegen));
            iteration_guard_pop(codegen);
            emit_indent(codegen);
            emit(codegen, "}\n");
            if (collection_needs_temporary) {
                emit_indent(codegen);
                emit(codegen, "}\n");
            }
        }
        emit_loop_arena_epilogue(codegen, false);
        break;
    }
    case NODE_WHILE_STATEMENT:
        emit_while_statement(codegen, node);
        break;
    case NODE_LOOP_STATEMENT:
        emit_loop_statement(codegen, node);
        break;
    case NODE_BREAK_STATEMENT:
        emit_indent(codegen);
        emit_loop_exit_unwind(codegen);
        emit(codegen, "break;\n");
        break;
    case NODE_CONTINUE_STATEMENT:
        emit_indent(codegen);
        emit_loop_exit_unwind(codegen);
        emit(codegen, "continue;\n");
        break;
    case NODE_WHEN_STATEMENT: {
        /* Emit as if-else chain for now (switch requires constant values) */
        AstNode *value = node->data.when_statement.value;
        GrayType *when_value_type = type_table_get(codegen->type_table, value);
        bool when_is_string = (when_value_type && when_value_type->kind == TYPE_KIND_STRING);
        bool when_is_tagged = false;
        const char *when_tagged_enum_name = NULL;
        if (!when_is_string && when_value_type && when_value_type->kind == TYPE_KIND_ENUM && when_value_type->name) {
            if (codegen_enum_is_string(codegen, when_value_type->name)) when_is_string = true;
            if (codegen_enum_is_tagged(codegen, when_value_type->name)) {
                when_is_tagged = true;
                when_tagged_enum_name = when_value_type->name;
            }
        }
        /* Detect wide integer type for the when value */
        const char *when_wide_integer = (when_value_type && when_value_type->name && is_wide_integer_type_name(when_value_type->name))
            ? when_value_type->name : resolve_wide_integer_type(codegen, value);
        /* Evaluate the match expression once into a temporary so that
         * side-effecting expressions (function calls, increments, etc.)
         * are not re-executed for each is-arm. */
        char when_temporary[VARIABLE_NAME_BUFFER_SIZE];
        snprintf(when_temporary, sizeof(when_temporary), "_gray_when%d", codegen_next_id(codegen));
        emit_indent(codegen);
        emit_formatted(codegen, "__auto_type %s = ", when_temporary);
        emit_expression(codegen, value);
        emit(codegen, ";\n");
        for (int i = 0; i < node->data.when_statement.case_count; i++) {
            WhenCase *when_case = &node->data.when_statement.cases[i];
            emit_indent(codegen);
            if (i == 0) {
                emit(codegen, "if (");
            } else {
                emit(codegen, "} else if (");
            }
            for (int j = 0; j < when_case->value_count; j++) {
                if (j > 0) emit(codegen, " || ");
                if (when_case->values[j]->kind == NODE_WHEN_PATTERN) {
                    /* Destructuring pattern: compare tag */
                    const char *variant_name = when_case->values[j]->data.when_pattern.variant;
                    const char *enum_name = when_case->values[j]->data.when_pattern.enum_name;
                    if (!enum_name) enum_name = when_tagged_enum_name;
                    emit(codegen, when_temporary);
                    emit_formatted(codegen, ".tag == GrayEnum_%s_TAG_%s", enum_name, variant_name);
                } else if (when_is_tagged) {
                    /* Tagged enum, plain variant: compare .tag */
                    AstNode *case_value = when_case->values[j];
                    const char *variant_name = NULL;
                    if (case_value->kind == NODE_MEMBER_EXPRESSION &&
                        (ast_member_qualifier(case_value) || ast_member_chain(case_value, NULL, NULL))) {
                        /* Enum.VARIANT, and the module-qualified
                         * mod.Enum.VARIANT — which nests one level deeper and
                         * otherwise fell through to constructing a value
                         * where a tag comparison belongs. */
                        variant_name = case_value->data.member.member;
                    } else if (case_value->kind == NODE_IMPLICIT_ENUM) {
                        variant_name = case_value->data.implicit_enum.variant;
                    }
                    if (variant_name) {
                        emit(codegen, when_temporary);
                        emit_formatted(codegen, ".tag == GrayEnum_%s_TAG_%s", when_tagged_enum_name, variant_name);
                    } else {
                        emit(codegen, when_temporary);
                        emit(codegen, ".tag == ");
                        emit_expression(codegen, case_value);
                    }
                } else if (when_case->is_range && when_case->values[j]->kind == NODE_RANGE_EXPRESSION) {
                    AstNode *range = when_case->values[j];
                    /* Check if step is a negative literal to reverse comparison direction */
                    bool is_negative_step = (range->data.range_expression.step &&
                        range->data.range_expression.step->kind == NODE_PREFIX_EXPRESSION &&
                        range->data.range_expression.step->data.prefix.operator == TOKEN_MINUS);
                    if (when_wide_integer) {
                        const char *prefix = wide_integer_prefix(when_wide_integer);
                        emit_formatted(codegen, "(%s_%s(", prefix, is_negative_step ? "le" : "ge");
                        emit(codegen, when_temporary);
                        emit(codegen, ", ");
                        if (!emit_wide_integer_coerced(codegen, when_wide_integer, range->data.range_expression.start))
                            emit_expression(codegen, range->data.range_expression.start);
                        emit_formatted(codegen, ") && %s_%s(", prefix, is_negative_step ? "gt" : "lt");
                        emit(codegen, when_temporary);
                        emit(codegen, ", ");
                        if (!emit_wide_integer_coerced(codegen, when_wide_integer, range->data.range_expression.end))
                            emit_expression(codegen, range->data.range_expression.end);
                        emit(codegen, ")");
                        if (range->data.range_expression.step) {
                            emit_formatted(codegen, " && %s_eq(%s_mod(%s_sub(", prefix, prefix, prefix);
                            emit(codegen, when_temporary);
                            emit(codegen, ", ");
                            if (!emit_wide_integer_coerced(codegen, when_wide_integer, range->data.range_expression.start))
                                emit_expression(codegen, range->data.range_expression.start);
                            emit(codegen, "), ");
                            if (!emit_wide_integer_coerced(codegen, when_wide_integer, range->data.range_expression.step))
                                emit_expression(codegen, range->data.range_expression.step);
                            emit_formatted(codegen, ", \"%s\", %d), %s_from_u64(0))",
                                codegen->file, node->token.line, prefix);
                        }
                        emit(codegen, ")");
                    } else {
                        emit(codegen, "(");
                        emit(codegen, when_temporary);
                        emit(codegen, is_negative_step ? " <= " : " >= ");
                        emit_expression(codegen, range->data.range_expression.start);
                        emit(codegen, " && ");
                        emit(codegen, when_temporary);
                        emit(codegen, is_negative_step ? " > " : " < ");
                        emit_expression(codegen, range->data.range_expression.end);
                        if (range->data.range_expression.step) {
                            emit(codegen, " && (");
                            emit(codegen, when_temporary);
                            emit(codegen, " - ");
                            emit_expression(codegen, range->data.range_expression.start);
                            emit(codegen, ") % ");
                            emit_expression(codegen, range->data.range_expression.step);
                            emit(codegen, " == 0");
                        }
                        emit(codegen, ")");
                    }
                } else if (when_is_string) {
                    emit(codegen, "gray_string_eq(");
                    emit(codegen, when_temporary);
                    emit(codegen, ", ");
                    emit_expression(codegen, when_case->values[j]);
                    emit(codegen, ")");
                } else if (when_wide_integer) {
                    emit_formatted(codegen, "%s_eq(", wide_integer_prefix(when_wide_integer));
                    emit(codegen, when_temporary);
                    emit(codegen, ", ");
                    emit_expression(codegen, when_case->values[j]);
                    emit(codegen, ")");
                } else {
                    emit(codegen, when_temporary);
                    emit(codegen, " == (");
                    emit_expression(codegen, when_case->values[j]);
                    emit(codegen, ")");
                }
            }
            emit(codegen, ") {\n");
            codegen->indent++;
            /* Emit binding declarations for when patterns */
            for (int j = 0; j < when_case->value_count; j++) {
                if (when_case->values[j]->kind == NODE_WHEN_PATTERN) {
                    AstNode *pattern = when_case->values[j];
                    const char *variant_name = pattern->data.when_pattern.variant;
                    const char *enum_name = pattern->data.when_pattern.enum_name;
                    if (!enum_name) enum_name = when_tagged_enum_name;
                    int enum_index = codegen_enum_index(codegen, enum_name);
                    if (enum_index >= 0) {
                        AstNode *declaration = codegen->enum_declarations[enum_index];
                        int matched_variant_index = -1;
                        for (int variant_index = 0; variant_index < declaration->data.enum_declaration.value_count; variant_index++) {
                            if (strcmp(declaration->data.enum_declaration.values[variant_index].name, variant_name) == 0) { matched_variant_index = variant_index; break; }
                        }
                        if (matched_variant_index >= 0) {
                            EnumValue *enum_value = &declaration->data.enum_declaration.values[matched_variant_index];
                            int limit = pattern->data.when_pattern.binding_count < enum_value->payload_count
                                ? pattern->data.when_pattern.binding_count : enum_value->payload_count;
                            for (int binding_index = 0; binding_index < limit; binding_index++) {
                                emit_indent(codegen);
                                emit_formatted(codegen, "%s %s = ",
                                    gray_type_to_c_codegen(codegen, enum_value->payload_types[binding_index]),
                                    pattern->data.when_pattern.bindings[binding_index]);
                                emit(codegen, when_temporary);
                                emit_formatted(codegen, ".data.%s._%d;\n", variant_name, binding_index);
                            }
                        }
                    }
                }
            }
            emit_block(codegen, when_case->body);
            codegen->indent--;
        }
        if (node->data.when_statement.default_body) {
            emit_indent(codegen);
            if (node->data.when_statement.case_count > 0) {
                emit(codegen, "} else {\n");
            } else {
                emit(codegen, "{\n");
            }
            codegen->indent++;
            emit_block(codegen, node->data.when_statement.default_body);
            codegen->indent--;
        } else if (node->data.when_statement.is_strict && node->data.when_statement.case_count > 0) {
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
    case NODE_FUNCTION_DECLARATION: {
        /* Generic function ): emit one specialised copy per
         * concrete instantiation the typechecker recorded. If a
         * generic function was declared but never called, there are
         * no instantiations and we skip emission entirely; the
         * un-specialised form has '?' in its signature and can't be
         * compiled as C. */
        bool has_wildcard = function_is_generic(node);
        if (has_wildcard) {
            const char *original_name = node->data.function_declaration.name;
            for (int instantiation_index = 0; instantiation_index < node->data.function_declaration.instantiation_count; instantiation_index++) {
                const char *concrete = node->data.function_declaration.instantiations[instantiation_index];
                char mangled[MESSAGE_BUFFER_SIZE];
                mangle_generic_name(mangled, sizeof(mangled), original_name, concrete);

                node->data.function_declaration.name = mangled;
                const char *saved = codegen->wildcard_binding;
                codegen->wildcard_binding = concrete;
                /* Per-instantiation multi-return typedefs were already
                 * emitted in the forward-declaration loop ). */
                emit_function_declaration(codegen, node, false);
                codegen->wildcard_binding = saved;
            }
            node->data.function_declaration.name = original_name;
        } else {
            emit_function_declaration(codegen, node,
                strcmp(node->data.function_declaration.name, "main") == 0);
        }
        break;
    }
    case NODE_BLOCK_STATEMENT:
        /* Inline block (e.g., from multi-var declaration expansion) */
        emit_block(codegen, node);
        break;
    case NODE_ENSURE_STATEMENT:
        /* Ensure is emitted at return/function-exit; record that control flow
         * has now reached this one so earlier returns don't run it. */
        codegen->ensure_reached++;
        break;
    case NODE_STRUCT_DECLARATION:
        /* Struct declarations are emitted in the preamble */
        break;
    case NODE_ENUM_DECLARATION:
        /* Enum declarations are emitted in the preamble */
        break;
    case NODE_ALIAS_DECLARATION:
        /* Type aliases are erased at codegen — emit nothing */
        break;
    case NODE_MODULE_DECLARATION:
        /* Module declarations are informational only */
        break;
    case NODE_IMPORT_STATEMENT:
        /* Imports are handled during the preamble scan */
        break;
    case NODE_USING_STATEMENT:
        /* Function-scoped using: add to using_modules so bare-name
         * dispatch works for the rest of this function body. */
        for (int j = 0; j < node->data.using_statement.count; j++) {
            GROW_ARRAY(codegen->using_modules, codegen->using_module_count,
                codegen->using_module_capacity);
            codegen->using_modules[codegen->using_module_count++] = node->data.using_statement.modules[j];
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
    const char **match = bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                               sizeof(const char *), keyword_compare);
    if (match) return codegen->is_enum_tagged[match - codegen->enum_names];
    return false;
}

static int codegen_enum_index(CodeGen *codegen, const char *name) {
    const char **match = bsearch(name, codegen->enum_names, (size_t)codegen->enum_count,
                               sizeof(const char *), keyword_compare);
    if (match) return (int)(match - codegen->enum_names);
    return -1;
}

/* True for the program-wide ErrorCode enum and for any user enum marked
 * #error_code. Their values share one global slot space, so they must also
 * share one string form: the variant name, via gray_error_code_name(). */
static bool codegen_enum_is_error_code(CodeGen *codegen, const char *name) {
    if (!name) return false;
    if (strcmp(name, "ErrorCode") == 0) return true;
    int enum_index = codegen_enum_index(codegen, name);
    return enum_index >= 0 && codegen->enum_declarations[enum_index] &&
           codegen->enum_declarations[enum_index]->data.enum_declaration.is_error_code;
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
    size_t length = strlen(path);
    char *copy = xmalloc(length + 1);
    memcpy(copy, path, length + 1);
    for (char *cursor = copy; *cursor; cursor++) {
        if (*cursor == '\\') *cursor = '/';
    }
    return copy;
}

CodeGen codegen_create(const char *file) {
    /* Zero-initialize so fields absent from the explicit list below (e.g.
     * is_in_const_declaration, current_variable_name) start false/NULL instead of stack
     * garbage. A truthy is_in_const_declaration silently suppresses every runtime
     * overflow and division check in the file. */
    CodeGen codegen = {0};
    codegen.output = buffer_create(OUTPUT_BUFFER_INITIAL_SIZE);
    codegen.global_initializer = buffer_create(MESSAGE_BUFFER_SIZE);
    codegen.indent = 0;
    codegen.has_mem = false;
    codegen.has_fmt = false;
    codegen.file_owned = normalize_path_separators(file);
    codegen.file = codegen.file_owned;
    codegen.should_emit_line_directives = true;
    codegen.enum_names = NULL;
    codegen.is_enum_string = NULL;
    codegen.is_enum_tagged = NULL;
    codegen.enum_declarations = NULL;
    codegen.enum_count = 0;
    codegen.enum_capacity = 0;
    codegen.current_function = NULL;
    codegen.loop_scope_depth = 0;
    codegen.all_functions = NULL;
    codegen.function_count = 0;
    codegen.function_capacity = 0;
    codegen.functions_by_name = NULL;
    codegen.is_functions_by_name_built = false;
    codegen.type_table = NULL;
    codegen.reference_variables = NULL;
    codegen.reference_variable_count = 0;
    codegen.reference_variable_capacity = 0;
    codegen.raw_variables = NULL;
    codegen.raw_variable_count = 0;
    codegen.raw_variable_capacity = 0;
    codegen.wide_integer_variable_names = NULL;
    codegen.wide_integer_variable_types = NULL;
    codegen.wide_integer_variable_count = 0;
    codegen.wide_integer_variable_capacity = 0;
    codegen.struct_declarations = NULL;
    codegen.struct_declaration_count = 0;
    codegen.struct_declaration_capacity = 0;
    codegen.function_field_index = NULL;
    codegen.function_field_count = 0;
    codegen.is_function_field_index_built = false;
    codegen.using_modules = NULL;
    codegen.using_module_count = 0;
    codegen.using_module_capacity = 0;
    codegen.imported_modules = NULL;
    codegen.imported_module_count = 0;
    codegen.imported_module_capacity = 0;
    codegen.c_headers = NULL;
    codegen.is_c_header_local = NULL;
    codegen.c_header_count = 0;
    codegen.c_header_capacity = 0;
    codegen.has_c_imports = false;
    codegen.type_alias_names = NULL;
    codegen.type_alias_targets = NULL;
    codegen.type_alias_count = 0;
    codegen.type_alias_capacity = 0;
    codegen.wildcard_binding = NULL;
    codegen.pending_call_typed_signature = NULL;
    codegen.scope_arenas = NULL;
    codegen.scope_arena_count = 0;
    codegen.scope_arena_capacity = 0;
    codegen.iteration_guards = NULL;
    codegen.iteration_guard_count = 0;
    codegen.iteration_guard_capacity = 0;
    codegen.namespaced_function_names = NULL;
    codegen.namespaced_function_name_count = 0;
    codegen.namespaced_function_name_capacity = 0;
    codegen.temporary_counter = 0;
    return codegen;
}

/* Check if a module name is in a set of imported stdlib modules. */
static bool has_stdlib_module(const char *const *modules, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (strcmp(modules[i], name) == 0) return true;
    return false;
}

/* Emits a struct's C body: `struct GrayStruct_<Name> { ... };`. */
static void codegen_emit_struct_body(CodeGen *codegen, AstNode *struct_node) {
    codegen_enter_node(codegen, struct_node);
    emit_formatted(codegen, "struct GrayStruct_%s {\n",
        codegen_declaration_name(codegen, struct_node, struct_node->data.struct_declaration.name));
    for (int j = 0; j < struct_node->data.struct_declaration.field_count; j++) {
        StructField *field = &struct_node->data.struct_declaration.fields[j];
        emit_formatted(codegen, "    %s %s;\n", gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name));
    }
    emit(codegen, "};\n\n");
}

/* Emits a tagged enum's C body: any payload structs, then
 * `struct GrayEnum_<Name> { tag; union { ... } data; };`. */
static void codegen_emit_tagged_enum_body(CodeGen *codegen, AstNode *enum_node) {
    codegen_enter_node(codegen, enum_node);
    const char *enum_name = codegen_declaration_name(codegen, enum_node, enum_node->data.enum_declaration.name);
    for (int j = 0; j < enum_node->data.enum_declaration.value_count; j++) {
        EnumValue *enum_value = &enum_node->data.enum_declaration.values[j];
        if (enum_value->payload_count > 0) {
            emit_formatted(codegen, "typedef struct {");
            for (int variant_index = 0; variant_index < enum_value->payload_count; variant_index++) {
                if (variant_index > 0) emit(codegen, "");
                emit_formatted(codegen, " %s _%d;", gray_type_to_c_codegen(codegen, enum_value->payload_types[variant_index]), variant_index);
            }
            emit_formatted(codegen, " } GrayEnum_%s_Data_%s;\n", enum_name, enum_value->name);
        }
    }
    emit_formatted(codegen, "struct GrayEnum_%s {\n", enum_name);
    emit_formatted(codegen, "    GrayEnum_%s_Tag tag;\n", enum_name);
    bool has_any_payload = false;
    for (int j = 0; j < enum_node->data.enum_declaration.value_count; j++) {
        if (enum_node->data.enum_declaration.values[j].payload_count > 0) { has_any_payload = true; break; }
    }
    if (has_any_payload) {
        emit_formatted(codegen, "    union {\n");
        for (int j = 0; j < enum_node->data.enum_declaration.value_count; j++) {
            EnumValue *enum_value = &enum_node->data.enum_declaration.values[j];
            if (enum_value->payload_count > 0) {
                emit_formatted(codegen, "        GrayEnum_%s_Data_%s %s;\n", enum_name, enum_value->name, enum_value->name);
            }
        }
        emit_formatted(codegen, "    } data;\n");
    }
    emit_formatted(codegen, "};\n\n");
}

/* Resolve a local C header import ("./x.h" / "../x.h") to its canonical
 * absolute path, using the importing file's own directory (item->source_dir,
 * falling back to the entry file's directory for an import written directly
 * in it — mirrors main.c's preflight_c_headers/add_local_c_header_dirs).
 *
 * Emitting the raw "./x.h" spelling verbatim, as written, is ambiguous once
 * two different directories each import their own same-named local header:
 * both produce the identical #include line, and the C preprocessor can only
 * resolve that text one way, so only one module's actual header ever lands
 * in the compiled C (#2729). A canonical absolute path makes each line name
 * its own file outright, independent of -iquote search order.
 *
 * Returns NULL (caller keeps the raw spelling) if the file cannot be
 * resolved — should not happen post-preflight, but codegen must not crash
 * either way. Caller-owned; deliberately never freed — see the c_headers[]
 * comment at the free site in codegen_destroy(). */
static const char *resolve_local_c_header(CodeGen *codegen, ImportItem *item) {
    char directory[C_HEADER_PATH_BUFFER_SIZE];
    if (item->source_directory) {
        snprintf(directory, sizeof(directory), "%s", item->source_directory);
    } else {
        snprintf(directory, sizeof(directory), "%s", codegen->file ? codegen->file : "");
        char *separator = gray_path_last_separator(directory);
        if (separator) separator[1] = '\0';
        else snprintf(directory, sizeof(directory), "./");
    }
    char joined[C_HEADER_PATH_BUFFER_SIZE];
    snprintf(joined, sizeof(joined), "%s%s", directory, item->path);
    char resolved[C_HEADER_PATH_BUFFER_SIZE];
    if (!gray_realpath_into(joined, resolved, sizeof(resolved))) return NULL;
    return strdup(resolved);
}

/* Top-level statements sorted by kind in one pass, plus the stdlib modules
 * imported (used for conditional header inclusion). */
#define MAX_STDLIB_IMPORTS 64
typedef struct {
    const char *stdlib_imports[MAX_STDLIB_IMPORTS];
    int stdlib_import_count;
    AstNode **enum_bucket;
    int enum_bucket_count, enum_bucket_capacity;
    AstNode **function_bucket;
    int function_bucket_count, function_bucket_capacity;
    AstNode **variable_bucket;
    int variable_bucket_count, variable_bucket_capacity;
    AstNode **other_bucket;
    int other_bucket_count, other_bucket_capacity;
} TopLevelStatements;

/* Single categorization pass over the program: records imports, C headers,
 * `using` modules, type aliases and structs on the codegen, and sorts every
 * other statement into its bucket. */
static void codegen_collect_top_level(CodeGen *codegen, AstNode *program, TopLevelStatements *top_level) {
    top_level->stdlib_import_count = 0;
    top_level->enum_bucket_count = 0; top_level->enum_bucket_capacity = 16;
    top_level->enum_bucket = xmalloc(sizeof(AstNode *) * (size_t)top_level->enum_bucket_capacity);
    top_level->function_bucket_count = 0; top_level->function_bucket_capacity = 16;
    top_level->function_bucket = xmalloc(sizeof(AstNode *) * (size_t)top_level->function_bucket_capacity);
    top_level->variable_bucket_count = 0; top_level->variable_bucket_capacity = 16;
    top_level->variable_bucket = xmalloc(sizeof(AstNode *) * (size_t)top_level->variable_bucket_capacity);
    top_level->other_bucket_count = 0; top_level->other_bucket_capacity = 16;
    top_level->other_bucket = xmalloc(sizeof(AstNode *) * (size_t)top_level->other_bucket_capacity);

    #define BUCKET_PUSH(array, count, capacity, value) do { \
        if ((count) >= (capacity)) { \
            (capacity) = (capacity) * 2; \
            (array) = xrealloc((array), sizeof(AstNode *) * (size_t)(capacity)); \
        } \
        (array)[(count)++] = (value); \
    } while (0)

    for (int i = 0; i < program->data.program.statement_count; i++) {
        AstNode *statement = program->data.program.statements[i];
        if (statement->kind == NODE_IMPORT_STATEMENT) {
            for (int j = 0; j < statement->data.import_statement.count; j++) {
                ImportItem *item = &statement->data.import_statement.items[j];
                if (item->is_stdlib && item->module) {
                    if (strcmp(item->module, "mem") == 0) codegen->has_mem = true;
                    if (strcmp(item->module, "fmt") == 0) codegen->has_fmt = true;
                    if (top_level->stdlib_import_count < MAX_STDLIB_IMPORTS)
                        top_level->stdlib_imports[top_level->stdlib_import_count++] = item->module;
                }
                /* Collect C interop headers */
                if (item->is_c_import && item->path) {
                    codegen->has_c_imports = true;
                    bool is_local = strncmp(item->path, "./", 2) == 0 ||
                                    strncmp(item->path, "../", 3) == 0;
                    /* c_headers/is_c_header_local grow in lockstep on a
                     * shared cap — GROW_ARRAY on each separately would only
                     * bump the second array every other resize, since the
                     * first call's cap bump already makes its own
                     * count>=cap check false. */
                    if (codegen->c_header_count >= codegen->c_header_capacity) {
                        codegen->c_header_capacity = GROW_NEXT_CAPACITY(codegen->c_header_capacity);
                        codegen->c_headers = xrealloc(codegen->c_headers,
                            sizeof(*codegen->c_headers) * (size_t)codegen->c_header_capacity);
                        codegen->is_c_header_local = xrealloc(codegen->is_c_header_local,
                            sizeof(*codegen->is_c_header_local) * (size_t)codegen->c_header_capacity);
                    }
                    const char *resolved = is_local
                        ? resolve_local_c_header(codegen, item) : NULL;
                    codegen->c_headers[codegen->c_header_count] = resolved ? resolved : item->path;
                    codegen->is_c_header_local[codegen->c_header_count] = is_local;
                    codegen->c_header_count++;
                }
                /* Track imported stdlib module names — codegen_module_imported()
                 * is a stdlib-only lookup; a user module's membership comes from
                 * the symbol table instead (see its resolved_decl callers), so
                 * one leaking in here falsely matches a user function's
                 * module-qualified call as a stdlib call. */
                if (item->is_stdlib && item->module) {
                    const char *module_name = item->alias ? item->alias : item->module;
                    GROW_ARRAY(codegen->imported_modules, codegen->imported_module_count,
                        codegen->imported_module_capacity);
                    codegen->imported_modules[codegen->imported_module_count++] = module_name;
                }
            }
            /* import and use; register all modules for using */
            if (statement->data.import_statement.should_auto_use) {
                for (int j = 0; j < statement->data.import_statement.count; j++) {
                    ImportItem *item = &statement->data.import_statement.items[j];
                    if (item->module) {
                        GROW_ARRAY(codegen->using_modules, codegen->using_module_count,
                            codegen->using_module_capacity);
                        codegen->using_modules[codegen->using_module_count++] = item->module;
                    }
                }
            }
        }
        if (statement->kind == NODE_USING_STATEMENT) {
            for (int j = 0; j < statement->data.using_statement.count; j++) {
                GROW_ARRAY(codegen->using_modules, codegen->using_module_count,
                    codegen->using_module_capacity);
                codegen->using_modules[codegen->using_module_count++] = statement->data.using_statement.modules[j];
            }
        }
        if (statement->kind == NODE_ALIAS_DECLARATION) {
            /* Collect type aliases for resolution during codegen */
            if (codegen->type_alias_count >= codegen->type_alias_capacity) {
                codegen->type_alias_capacity = codegen->type_alias_capacity ? codegen->type_alias_capacity * 2 : 8;
                codegen->type_alias_names = xrealloc(codegen->type_alias_names,
                    sizeof(const char *) * (size_t)codegen->type_alias_capacity);
                codegen->type_alias_targets = xrealloc(codegen->type_alias_targets,
                    sizeof(const char *) * (size_t)codegen->type_alias_capacity);
            }
            /* Key the alias by its module's spelling, which is what a
             * reference to it resolves to; and resolve the target the same
             * way, since it names a type in the alias's own module. */
            codegen_enter_node(codegen, statement);
            codegen->type_alias_names[codegen->type_alias_count] =
                codegen_declaration_name(codegen, statement, statement->data.alias_declaration.name);
            codegen->type_alias_targets[codegen->type_alias_count] =
                codegen_resolve_type(codegen, statement->data.alias_declaration.target_type);
            codegen->type_alias_count++;
            continue; /* aliases are erased — not emitted */
        }
        if (statement->kind == NODE_STRUCT_DECLARATION) {
            statement->data.struct_declaration.name =
                codegen_declaration_name(codegen, statement, statement->data.struct_declaration.name);
            GROW_ARRAY(codegen->struct_declarations, codegen->struct_declaration_count,
                codegen->struct_declaration_capacity);
            codegen->struct_declarations[codegen->struct_declaration_count++] = statement;
        } else if (statement->kind == NODE_ENUM_DECLARATION) {
            BUCKET_PUSH(top_level->enum_bucket, top_level->enum_bucket_count, top_level->enum_bucket_capacity, statement);
        } else if (statement->kind == NODE_FUNCTION_DECLARATION) {
            if (statement->data.function_declaration.is_test) {
                /* #test functions exist only for `gray test`; a normal build
                 * drops them entirely (no forward decl, no definition, no
                 * call). A --test build keeps only the ones declared in the
                 * file being compiled — a #test reached through an import
                 * belongs to that module's own test run, not this one. */
                bool keep = codegen->is_test_mode;
                if (keep && statement->token.file && codegen->file) {
                    char *normalized_file = normalize_path_separators(statement->token.file);
                    if (normalized_file && strcmp(normalized_file, codegen->file) != 0) keep = false;
                    free(normalized_file);
                }
                if (!keep) continue;
            }
            BUCKET_PUSH(top_level->function_bucket, top_level->function_bucket_count, top_level->function_bucket_capacity, statement);
        } else if (statement->kind == NODE_VARIABLE_DECLARATION) {
            BUCKET_PUSH(top_level->variable_bucket, top_level->variable_bucket_count, top_level->variable_bucket_capacity, statement);
        } else if (statement->kind != NODE_USING_STATEMENT) {
            BUCKET_PUSH(top_level->other_bucket, top_level->other_bucket_count, top_level->other_bucket_capacity, statement);
        }
    }
    #undef BUCKET_PUSH
}

/* Emit the #include preamble. Returns the output offset where arrays.h /
 * maps.h / strings.h are spliced in once body emission knows which are used. */
static size_t codegen_emit_preamble(CodeGen *codegen, const TopLevelStatements *top_level) {
    /* Emit preamble — core headers always included, stdlib headers only when imported */
    emit(codegen, "/* Generated by grayc */\n");
    emit(codegen, "#include \"runtime.h\"\n");
    emit(codegen, "#include \"array.h\"\n");
    emit(codegen, "#include \"map.h\"\n");
    /* Qualified, unlike its siblings: clang's own resource directory (which
     * zig cc searches ahead of -isystem) ships a builtins.h that would win. */
    emit(codegen, "#include \"stdlib/builtins.h\"\n");
    /* os.h is always needed — generated main() calls gray_os_init(). */
    emit(codegen, "#include \"os.h\"\n");
    /* test.h declares the test-runner entry points used by the generated
     * main() when compiling with --test. */
    if (codegen->is_test_mode)
        emit(codegen, "#include \"test.h\"\n");
    /* arrays.h / maps.h / strings.h are spliced in here after body emission,
     * but only when the `in` operator or an explicit import needs them. */
    size_t collection_include_anchor = codegen->output.length;
    /* bigint.h is always needed — i128/u128/i256/u256 are keyword types
     * usable with no import. */
    emit(codegen, "#include \"bigint.h\"\n");

    /* An explicit import of one of these modules also needs its header. */
    if (has_stdlib_module(top_level->stdlib_imports, top_level->stdlib_import_count, "arrays"))
        codegen->needs_arrays_header = true;
    if (has_stdlib_module(top_level->stdlib_imports, top_level->stdlib_import_count, "maps"))
        codegen->needs_maps_header = true;
    if (has_stdlib_module(top_level->stdlib_imports, top_level->stdlib_import_count, "strings"))
        codegen->needs_strings_header = true;

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
        if (has_stdlib_module(top_level->stdlib_imports, top_level->stdlib_import_count, stdlib_headers[i].module))
            emit_formatted(codegen, "#include \"%s\"\n", stdlib_headers[i].header);
    }

    /* Emit user C interop headers (after Grayscale internals to prevent collisions) */
    if (codegen->c_header_count > 0) {
        emit(codegen, "\n/* C interop headers */\n");
        for (int i = 0; i < codegen->c_header_count; i++) {
            const char *header_path = codegen->c_headers[i];
            /* Defense-in-depth: skip any path that slipped through with a
             * character that could break out of the #include "..."/<...>
             * string and inject arbitrary C. A local header is now resolved
             * to a real filesystem path (resolve_local_c_header, #2729) that
             * may legitimately contain spaces or other characters an
             * allowlist would reject — so deny only what can actually break
             * a quoted or angle-bracket #include: '"' and '>' end the two
             * delimited forms early, '\' starts a C escape, and control
             * characters (including newline) cannot appear on a directive
             * line at all. */
            bool safe = true;
            for (const char *scan = header_path; *scan; scan++) {
                unsigned char character = (unsigned char)*scan;
                if (character == '"' || character == '>' || character == '\\' || character < 0x20 || character == 0x7f) {
                    safe = false;
                    break;
                }
            }
            if (!safe) continue;
            if (codegen->is_c_header_local[i]) {
                emit_formatted(codegen, "#include \"%s\"\n", header_path);
            } else {
                emit_formatted(codegen, "#include <%s>\n", header_path);
            }
        }
    }
    emit(codegen, "\n");
    return collection_include_anchor;
}

/* Struct and enum type definitions: forward declarations, the ErrorCode
 * enum, stdlib and user enums, struct/tagged-enum bodies in dependency order,
 * and per-instantiation generic structs. */
static void codegen_emit_type_definitions(CodeGen *codegen, const TopLevelStatements *top_level) {
    /* Emit struct forward declarations before enums so tagged union
     * payloads can reference struct types by name. */
    {
        int struct_count = codegen->struct_declaration_count;
        AstNode **structs = codegen->struct_declarations;
        for (int i = 0; i < struct_count; i++) {
            if (structs[i]->data.struct_declaration.is_generic) continue;
            {
                const char *struct_name_text = codegen_declaration_name(codegen, structs[i],
                                                   structs[i]->data.struct_declaration.name);
                emit_formatted(codegen, "typedef struct GrayStruct_%s GrayStruct_%s;\n", struct_name_text, struct_name_text);
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
#define GRAY_ERROR_CODE_EMIT(variant) emit_formatted(codegen, "#define GrayErrorCode_%s %d\n", #variant, slot++);
        GRAY_ERROR_CODE_BUILTINS(GRAY_ERROR_CODE_EMIT)
#undef GRAY_ERROR_CODE_EMIT
        for (int i = 0; i < top_level->enum_bucket_count; i++) {
            AstNode *enum_statement = top_level->enum_bucket[i];
            if (!enum_statement->data.enum_declaration.is_error_code) continue;
            for (int j = 0; j < enum_statement->data.enum_declaration.value_count; j++) {
                emit_formatted(codegen, "#define GrayErrorCode_%s %d\n",
                    enum_statement->data.enum_declaration.values[j].name, slot++);
            }
        }
        emit(codegen, "static inline const char *gray_error_code_name(int64_t _c) {\n");
        emit(codegen, "    switch (_c) {\n");
        slot = 0;
#define GRAY_ERROR_CODE_CASE(variant) emit_formatted(codegen, "        case %d: return \"%s\";\n", slot++, #variant);
        GRAY_ERROR_CODE_BUILTINS(GRAY_ERROR_CODE_CASE)
#undef GRAY_ERROR_CODE_CASE
        for (int i = 0; i < top_level->enum_bucket_count; i++) {
            AstNode *enum_statement = top_level->enum_bucket[i];
            if (!enum_statement->data.enum_declaration.is_error_code) continue;
            for (int j = 0; j < enum_statement->data.enum_declaration.value_count; j++) {
                emit_formatted(codegen, "        case %d: return \"%s\";\n",
                    slot++, enum_statement->data.enum_declaration.values[j].name);
            }
        }
        emit(codegen, "        default: return \"Unknown\";\n    }\n}\n\n");
    }

    /* Enums a stdlib module exposes (io.OpenFlag, os.Platform): a plain C enum
     * typedef, emitted only when the owning module is imported. Variant value is
     * its position — matches the typechecker's stdlib_enum_map. */
    {
        static const struct {
            const char *name; const char *module_name;
            const char *variants[6]; int count;
        } codegen_stdlib_enums[] = {
            {"OpenFlag", "io", {"O_RDONLY", "O_WRONLY", "O_RDWR"}, 3},
            {"Platform", "os", {"MAC_OS", "LINUX", "WINDOWS", "OTHER"}, 4},
            {NULL, NULL, {NULL}, 0}
        };
        for (int i = 0; codegen_stdlib_enums[i].name; i++) {
            if (!has_stdlib_module(top_level->stdlib_imports, top_level->stdlib_import_count, codegen_stdlib_enums[i].module_name))
                continue;
            emit(codegen, "typedef enum {\n");
            for (int j = 0; j < codegen_stdlib_enums[i].count; j++) {
                emit_formatted(codegen, "    GrayEnum_%s_%s = %d,\n",
                    codegen_stdlib_enums[i].name, codegen_stdlib_enums[i].variants[j], j);
            }
            emit_formatted(codegen, "} GrayEnum_%s;\n\n", codegen_stdlib_enums[i].name);
        }
    }

    /* Register all enums and emit non-tagged enum typedefs.
     * Tagged enum typedefs are deferred until after struct body
     * definitions because their payloads may contain struct values. */
    for (int i = 0; i < top_level->enum_bucket_count; i++) {
        AstNode *statement = top_level->enum_bucket[i];
        /* Emit and register this enum under the name its module gives it.
         * The typedef, the variant constants, and the registry all read this
         * field, and they have to agree with what a reference resolves to.
         * Codegen is the last phase and owns the AST from here, so the
         * resolved name is written back rather than swapped per read. */
        statement->data.enum_declaration.name =
            codegen_declaration_name(codegen, statement, statement->data.enum_declaration.name);
        /* Check if this is a string enum (auto-detect from values) */
            bool is_string_enum = false;
            for (int j = 0; j < statement->data.enum_declaration.value_count; j++) {
                if (statement->data.enum_declaration.values[j].value &&
                    statement->data.enum_declaration.values[j].value->kind == NODE_STRING_VALUE) {
                    is_string_enum = true;
                    break;
                }
            }

            /* Register enum name and string flag */
            bool is_tagged = statement->data.enum_declaration.is_tagged;
            if (codegen->enum_count >= codegen->enum_capacity) {
                codegen->enum_capacity = codegen->enum_capacity ? codegen->enum_capacity * 2 : 8;
                codegen->enum_names = xrealloc(codegen->enum_names, sizeof(const char *) * codegen->enum_capacity);
                codegen->is_enum_string = xrealloc(codegen->is_enum_string, sizeof(bool) * codegen->enum_capacity);
                codegen->is_enum_tagged = xrealloc(codegen->is_enum_tagged, sizeof(bool) * codegen->enum_capacity);
                codegen->enum_declarations = xrealloc(codegen->enum_declarations, sizeof(AstNode *) * codegen->enum_capacity);
            }
            codegen->enum_names[codegen->enum_count] = statement->data.enum_declaration.name;
            codegen->is_enum_string[codegen->enum_count] = is_string_enum;
            codegen->is_enum_tagged[codegen->enum_count] = is_tagged;
            codegen->enum_declarations[codegen->enum_count] = statement;
            codegen->enum_count++;

            if (is_string_enum) {
                emit_formatted(codegen, "typedef GrayString GrayEnum_%s;\n", statement->data.enum_declaration.name);
                for (int j = 0; j < statement->data.enum_declaration.value_count; j++) {
                    EnumValue *enum_value = &statement->data.enum_declaration.values[j];
                    const char *string_value = enum_value->name;
                    if (enum_value->value && enum_value->value->kind == NODE_STRING_VALUE) {
                        string_value = enum_value->value->data.string_value.value;
                    }
                    emit_formatted(codegen, "#define GrayEnum_%s_%s ((GrayString){ \"%s\", %d })\n",
                        statement->data.enum_declaration.name, enum_value->name,
                        string_value, (int)strlen(string_value));
                }
                emit(codegen, "\n");
            } else if (is_tagged) {
                /* Defer tagged enum typedefs — only emit the tag enum now */
                const char *enum_name = codegen_declaration_name(codegen, statement, statement->data.enum_declaration.name);
                emit_formatted(codegen, "typedef enum {\n");
                for (int j = 0; j < statement->data.enum_declaration.value_count; j++) {
                    emit_formatted(codegen, "    GrayEnum_%s_TAG_%s = %d,\n", enum_name, statement->data.enum_declaration.values[j].name, j);
                }
                emit_formatted(codegen, "} GrayEnum_%s_Tag;\n\n", enum_name);
            } else {
                bool is_flags = statement->data.enum_declaration.is_flags;
                bool is_error_code = statement->data.enum_declaration.is_error_code;
                emit_formatted(codegen, "typedef enum {\n");
                for (int j = 0; j < statement->data.enum_declaration.value_count; j++) {
                    EnumValue *enum_value = &statement->data.enum_declaration.values[j];
                    emit_formatted(codegen, "    GrayEnum_%s_%s", statement->data.enum_declaration.name, enum_value->name);
                    if (is_error_code) {
                        /* Variant value is its global ErrorCode slot, defined
                         * once in the ErrorCode preamble. */
                        emit_formatted(codegen, " = GrayErrorCode_%s", enum_value->name);
                    } else if (enum_value->value) {
                        emit(codegen, " = ");
                        emit_expression(codegen, enum_value->value);
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
                    codegen_declaration_name(codegen, statement, statement->data.enum_declaration.name));
            }
    }

    /* Sort enum arrays for O(log n) bsearch in enum lookup functions */
    for (int i = 1; i < codegen->enum_count; i++) {
        const char *key_name = codegen->enum_names[i];
        bool key_is_string = codegen->is_enum_string[i];
        bool key_is_tagged = codegen->is_enum_tagged[i];
        AstNode *enum_declaration_node = codegen->enum_declarations[i];
        int j = i - 1;
        while (j >= 0 && strcmp(codegen->enum_names[j], key_name) > 0) {
            codegen->enum_names[j+1] = codegen->enum_names[j];
            codegen->is_enum_string[j+1] = codegen->is_enum_string[j];
            codegen->is_enum_tagged[j+1] = codegen->is_enum_tagged[j];
            codegen->enum_declarations[j+1] = codegen->enum_declarations[j];
            j--;
        }
        codegen->enum_names[j+1] = key_name;
        codegen->is_enum_string[j+1] = key_is_string;
        codegen->is_enum_tagged[j+1] = key_is_tagged;
        codegen->enum_declarations[j+1] = enum_declaration_node;
    }

    /* Forward-declare every tagged enum's union struct up front (mirroring
     * the GrayStruct_ forward declarations emitted above) so a struct field,
     * a pointer/array/map slot, or another tagged enum's payload can name it
     * regardless of where the topological sort below places its definition. */
    {
        bool any_tagged = false;
        for (int i = 0; i < top_level->enum_bucket_count; i++) {
            AstNode *statement = top_level->enum_bucket[i];
            if (!statement->data.enum_declaration.is_tagged) continue;
            const char *enum_name = codegen_declaration_name(codegen, statement, statement->data.enum_declaration.name);
            emit_formatted(codegen, "typedef struct GrayEnum_%s GrayEnum_%s;\n", enum_name, enum_name);
            any_tagged = true;
        }
        if (any_tagged) emit(codegen, "\n");
    }

    /* Emit struct and tagged-enum definitions together in dependency order
     * (topological sort). A struct can hold a tagged enum by value (a field
     * of that type) and a tagged enum can hold a struct by value (a payload
     * field of that type), so either kind of declaration may need to come
     * first — both are sorted together against one shared "emitted" set. */
    {
        int struct_count = codegen->struct_declaration_count;
        AstNode **structs = codegen->struct_declarations;

        int tagged_count = 0;
        AstNode **tagged_enums = top_level->enum_bucket_count > 0
            ? xmalloc(sizeof(AstNode *) * (size_t)top_level->enum_bucket_count) : NULL;
        for (int i = 0; i < top_level->enum_bucket_count; i++) {
            if (top_level->enum_bucket[i]->data.enum_declaration.is_tagged) tagged_enums[tagged_count++] = top_level->enum_bucket[i];
        }
        int total = struct_count + tagged_count;

        bool *emitted = xmalloc(sizeof(bool) * (size_t)(total > 0 ? total : 1));
        for (int i = 0; i < total; i++) emitted[i] = false;
        int emit_count = 0;

        /* Simple topological sort: repeatedly emit nodes with no unresolved deps. */
        for (int pass = 0; pass < total && emit_count < total; pass++) {
            for (int slot_index = 0; slot_index < total; slot_index++) {
                if (emitted[slot_index]) continue;
                bool is_struct_node = slot_index < struct_count;
                AstNode *node = is_struct_node ? structs[slot_index] : tagged_enums[slot_index - struct_count];
                codegen_enter_node(codegen, node);

                /* A field/payload names its type as written in its own
                 * module's file, while a declaration is known by its C name.
                 * Resolve both sides before comparing: a bare `Inner` written
                 * inside module lib is the declaration named lib_Inner. */
                const char *dependency_types[64];
                int dependency_count = 0;
                if (is_struct_node) {
                    for (int j = 0; j < node->data.struct_declaration.field_count && dependency_count < 64; j++) {
                        const char *field_type = node->data.struct_declaration.fields[j].type_name;
                        if (!field_type) continue;
                        /* Only a by-value field constrains the order; a
                         * pointer, array or map field is satisfied by the
                         * forward declaration already emitted above. */
                        if (field_type[0] == '^' || field_type[0] == '[' ||
                            strncmp(field_type, "map[", 4) == 0) continue;
                        dependency_types[dependency_count++] = codegen_resolve_type(codegen, field_type);
                    }
                } else {
                    for (int j = 0; j < node->data.enum_declaration.value_count; j++) {
                        EnumValue *enum_value = &node->data.enum_declaration.values[j];
                        for (int inner_index = 0; inner_index < enum_value->payload_count && dependency_count < 64; inner_index++) {
                            const char *payload_type = enum_value->payload_types[inner_index];
                            if (!payload_type) continue;
                            if (payload_type[0] == '^' || payload_type[0] == '[' ||
                                strncmp(payload_type, "map[", 4) == 0) continue;
                            dependency_types[dependency_count++] = codegen_resolve_type(codegen, payload_type);
                        }
                    }
                }

                bool deps_met = true;
                for (int dependency_index = 0; dependency_index < dependency_count && deps_met; dependency_index++) {
                    for (int inner_index = 0; inner_index < struct_count; inner_index++) {
                        if (inner_index == slot_index) continue;
                        if (emitted[inner_index]) continue;
                        if (strcmp(codegen_declaration_name(codegen, structs[inner_index], structs[inner_index]->data.struct_declaration.name),
                                   dependency_types[dependency_index]) == 0) {
                            deps_met = false;
                            break;
                        }
                    }
                    if (!deps_met) break;
                    for (int inner_index = 0; inner_index < tagged_count; inner_index++) {
                        int tagged_slot_index = struct_count + inner_index;
                        if (tagged_slot_index == slot_index) continue;
                        if (emitted[tagged_slot_index]) continue;
                        if (strcmp(codegen_declaration_name(codegen, tagged_enums[inner_index], tagged_enums[inner_index]->data.enum_declaration.name),
                                   dependency_types[dependency_index]) == 0) {
                            deps_met = false;
                            break;
                        }
                    }
                }
                if (!deps_met) continue;

                emitted[slot_index] = true;
                emit_count++;
                if (is_struct_node) {
                    /* Skip generic structs here; they're emitted
                     * per-instantiation below. */
                    if (node->data.struct_declaration.is_generic) continue;
                    codegen_emit_struct_body(codegen, node);
                } else {
                    codegen_emit_tagged_enum_body(codegen, node);
                }
            }
        }
        /* If any nodes couldn't be emitted (circular deps), emit them anyway. */
        for (int slot_index = 0; slot_index < total; slot_index++) {
            if (emitted[slot_index]) continue;
            bool is_struct_node = slot_index < struct_count;
            AstNode *node = is_struct_node ? structs[slot_index] : tagged_enums[slot_index - struct_count];
            if (is_struct_node) {
                codegen_emit_struct_body(codegen, node);
            } else {
                codegen_emit_tagged_enum_body(codegen, node);
            }
        }

        free(emitted);
        if (tagged_enums) free(tagged_enums);
    }

    /* emit per-instantiation typedefs for generic (wildcard) structs.
     * For each recorded binding, substitute ? → concrete in field types
     * and emit under a mangled name (e.g. GrayStruct_Pair__int). */
    for (int i = 0; i < codegen->struct_declaration_count; i++) {
        AstNode *statement = codegen->struct_declarations[i];
        if (!statement->data.struct_declaration.is_generic) continue;
        for (int instantiation_index = 0; instantiation_index < statement->data.struct_declaration.instantiation_count; instantiation_index++) {
            const char *concrete = statement->data.struct_declaration.instantiations[instantiation_index];
            char mangled[MESSAGE_BUFFER_SIZE];
            mangle_generic_name(mangled, sizeof(mangled), statement->data.struct_declaration.name, concrete);
            /* Forward declaration */
            emit_formatted(codegen, "typedef struct GrayStruct_%s GrayStruct_%s;\n", mangled, mangled);
            emit_formatted(codegen, "struct GrayStruct_%s {\n", mangled);
            const char *saved = codegen->wildcard_binding;
            codegen->wildcard_binding = concrete;
            for (int j = 0; j < statement->data.struct_declaration.field_count; j++) {
                StructField *field = &statement->data.struct_declaration.fields[j];
                emit_formatted(codegen, "    %s %s;\n", gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name));
            }
            codegen->wildcard_binding = saved;
            emit(codegen, "};\n\n");
        }
    }
}

static void codegen_emit_json_helpers(CodeGen *codegen) {
    /* emit JSON parse/stringify helpers for #json structs. Each
     * #json struct gets two static functions:
     *   - gray_json_parse_<Name>(arena, json_string) → GrayStruct_<Name>
     *   - gray_json_stringify_<Name>(arena, struct_value) → GrayString
     * These are called by json.parse() / json.stringify() which the
     * typechecker dispatches based on the target/argument struct type. */
    for (int i = 0; i < codegen->struct_declaration_count; i++) {
        AstNode *statement = codegen->struct_declarations[i];
        if (!statement->data.struct_declaration.is_json) continue;
        const char *struct_name = statement->data.struct_declaration.name;
        int field_count = statement->data.struct_declaration.field_count;

        /* --- parse: JSON string → struct --- */
        emit_formatted(codegen, "static GrayStruct_%s gray_json_parse_%s(GrayArena *arena, GrayString text) {\n", struct_name, struct_name);
        emit_formatted(codegen, "    GrayStruct_%s _r = {0};\n", struct_name);
        emit_formatted(codegen, "    GrayMap _m = gray_json_decode(arena, text);\n");
        for (int j = 0; j < field_count; j++) {
            StructField *field = &statement->data.struct_declaration.fields[j];
            /* A `` `json:"Name"` `` tag maps the field under that JSON key
             * instead of the Grayscale field name; the C struct member
             * accessed below stays keyed by the field name either way. */
            const char *json_key = field->json_tag ? field->json_tag : field->name;
            if (strcmp(field->type_name, "string") == 0) {
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", json_key);
                emit_formatted(codegen, "      if (_v) _r.%s = *(GrayString *)_v; }\n", sanitize_name(field->name));
            } else if (type_kind_is_number(type_from_name(field->type_name)->kind)) {
                /* A number field of any sized type decodes at that type. */
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", json_key);
                emit_formatted(codegen, "      if (_v) gray_json_field_decode(*(GrayString *)_v, GRAY_ELEM_KIND_OF(%s), &_r.%s, \"%s\", %d); }\n",
                    gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name),
                    codegen->file, statement->token.line);
            } else if (strcmp(field->type_name, "bool") == 0) {
                emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", json_key);
                emit_formatted(codegen, "      if (_v) { GrayString _sv = *(GrayString *)_v; _r.%s = (_sv.len == 4 && memcmp(_sv.data, \"true\", 4) == 0); } }\n", sanitize_name(field->name));
            } else {
                /* Enum field: serialized by backing type. Tagged enums are
                 * rejected on #json structs at typecheck time (E3173), so
                 * only plain integer-backed and string-backed enums reach here. */
                const char *resolved_field_type = codegen_resolve_type(codegen, field->type_name);
                if (codegen_is_enum(codegen, resolved_field_type) && !codegen_enum_is_tagged(codegen, resolved_field_type)) {
                    int enum_index = codegen_enum_index(codegen, resolved_field_type);
                    AstNode *field_enum_declaration = codegen->enum_declarations[enum_index];
                    const char *enum_display_name = field_enum_declaration->data.enum_declaration.original_name
                        ? field_enum_declaration->data.enum_declaration.original_name : resolved_field_type;
                    emit_formatted(codegen, "    { GrayString _k = gray_string_lit(\"%s\"); void *_v = gray_map_get(&_m, &_k);\n", json_key);
                    emit(codegen, "      if (_v) { GrayString _sv = *(GrayString *)_v;\n");
                    if (codegen_enum_is_string(codegen, resolved_field_type)) {
                        emit_formatted(codegen, "        _r.%s = gray_json_enum_from_str(_sv, (const GrayString[]){",
                            sanitize_name(field->name));
                        for (int variant_index = 0; variant_index < field_enum_declaration->data.enum_declaration.value_count; variant_index++) {
                            if (variant_index > 0) emit(codegen, ", ");
                            emit_formatted(codegen, "GrayEnum_%s_%s", resolved_field_type, field_enum_declaration->data.enum_declaration.values[variant_index].name);
                        }
                        emit_formatted(codegen, "}, %d, \"%s\"); } }\n",
                            field_enum_declaration->data.enum_declaration.value_count, enum_display_name);
                    } else {
                        emit(codegen, "        int64_t _iv = gray_builtin_string_to_i64(_sv);\n");
                        emit_formatted(codegen, "        _r.%s = (%s)gray_json_enum_from_number(_sv, _iv, (const int64_t[]){",
                            sanitize_name(field->name), gray_type_to_c_codegen(codegen, resolved_field_type));
                        for (int variant_index = 0; variant_index < field_enum_declaration->data.enum_declaration.value_count; variant_index++) {
                            if (variant_index > 0) emit(codegen, ", ");
                            emit_formatted(codegen, "GrayEnum_%s_%s", resolved_field_type, field_enum_declaration->data.enum_declaration.values[variant_index].name);
                        }
                        emit_formatted(codegen, "}, %d, \"%s\"); } }\n",
                            field_enum_declaration->data.enum_declaration.value_count, enum_display_name);
                    }
                }
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
                StructField *field = &statement->data.struct_declaration.fields[j];
                const char *json_key = field->json_tag ? field->json_tag : field->name;
                if (j > 0) fixed += 2; /* ", " */
                fixed += 2 + (int)strlen(json_key) + 2; /* "key": */
                /* Enum fields serialize by backing type: a string-backed enum
                 * needs the runtime json_escaped_len() pass below, same as a
                 * string field; a plain integer-backed enum takes the int64
                 * upper bound. Tagged enums never reach here (E3173). */
                const char *resolved_field_type = codegen_resolve_type(codegen, field->type_name);
                bool is_number_enum = codegen_is_enum(codegen, resolved_field_type) && !codegen_enum_is_string(codegen, resolved_field_type);
                /* Value upper bound for enum and bool fields */
                if (is_number_enum) {
                    fixed += 21;
                } else if (strcmp(field->type_name, "bool") == 0) {
                    fixed += 5;
                }
                /* string, string-backed enum and number fields are added at runtime below */
            }
            emit_formatted(codegen, "    size_t _need = %d;\n", fixed);
        }
        /* Add runtime string field sizes */
        for (int j = 0; j < field_count; j++) {
            StructField *field = &statement->data.struct_declaration.fields[j];
            const char *resolved_field_type = codegen_resolve_type(codegen, field->type_name);
            bool is_string_enum = codegen_is_enum(codegen, resolved_field_type) && codegen_enum_is_string(codegen, resolved_field_type);
            if (strcmp(field->type_name, "string") == 0 || is_string_enum) {
                emit_formatted(codegen, "    _need += json_escaped_len(_s.%s);\n", sanitize_name(field->name));
            } else if (type_kind_is_number(type_from_name(field->type_name)->kind)) {
                /* A number field of any sized type is rendered at that type. */
                emit_formatted(codegen, "    GrayString _nt%d = gray_json_number_text(arena, GRAY_ELEM_KIND_OF(%s), &_s.%s);\n",
                    j, gray_type_to_c_codegen(codegen, field->type_name), sanitize_name(field->name));
                emit_formatted(codegen, "    _need += (size_t)_nt%d.len;\n", j);
            }
        }

        /* Pass 2: allocate and write */
        emit_formatted(codegen, "    char *_buf = gray_arena_alloc(arena, _need + 1);\n");
        emit_formatted(codegen, "    int _pos = 0;\n");
        emit_formatted(codegen, "    _buf[_pos++] = '{';\n");
        for (int j = 0; j < field_count; j++) {
            StructField *field = &statement->data.struct_declaration.fields[j];
            const char *json_key = field->json_tag ? field->json_tag : field->name;
            if (j > 0) emit_formatted(codegen, "    _buf[_pos++] = ','; _buf[_pos++] = ' ';\n");
            /* Key */
            emit_formatted(codegen, "    _buf[_pos++] = '\"';\n");
            int field_name_length = (int)strlen(json_key);
            emit_formatted(codegen, "    memcpy(_buf + _pos, \"%s\", %d); _pos += %d;\n",
                json_key, field_name_length, field_name_length);
            emit_formatted(codegen, "    _buf[_pos++] = '\"'; _buf[_pos++] = ':'; _buf[_pos++] = ' ';\n");
            /* Value */
            const char *resolved_field_type = codegen_resolve_type(codegen, field->type_name);
            bool is_string_enum = codegen_is_enum(codegen, resolved_field_type) && codegen_enum_is_string(codegen, resolved_field_type);
            bool is_number_enum = codegen_is_enum(codegen, resolved_field_type) && !codegen_enum_is_string(codegen, resolved_field_type);
            if (strcmp(field->type_name, "string") == 0 || is_string_enum) {
                emit_formatted(codegen, "    json_append_escaped(_buf, &_pos, _s.%s);\n", sanitize_name(field->name));
            } else if (is_number_enum) {
                emit_formatted(codegen, "    _pos += snprintf(_buf + _pos, _need + 1 - (size_t)_pos, \"%%lld\", (long long)_s.%s);\n",
                    sanitize_name(field->name));
            } else if (type_kind_is_number(type_from_name(field->type_name)->kind)) {
                emit_formatted(codegen, "    memcpy(_buf + _pos, _nt%d.data, (size_t)_nt%d.len); _pos += _nt%d.len;\n", j, j, j);
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
        emit_formatted(codegen, "    GrayArray _result = GRAY_ARRAY_NEW_OF(arena, GrayStruct_%s, _elems.len > 0 ? _elems.len : 4);\n", struct_name);
        emit_formatted(codegen, "    for (int32_t _i = 0; _i < _elems.len; _i++) {\n");
        emit_formatted(codegen, "        GrayString _elem_str = *(GrayString *)((char *)_elems.data + (size_t)_i * (size_t)_elems.elem_size);\n");
        emit_formatted(codegen, "        GrayStruct_%s _item = gray_json_parse_%s(arena, _elem_str);\n", struct_name, struct_name);
        emit_formatted(codegen, "        gray_array_push(arena, &_result, &_item, __FILE__, __LINE__);\n");
        emit_formatted(codegen, "    }\n");
        emit_formatted(codegen, "    return _result;\n");
        emit_formatted(codegen, "}\n\n");

        /* --- stringify array: GrayArray of structs → JSON array string ---
         * Symmetric with parse array above; json.stringify() previously had
         * no dedicated codegen path for an array argument at all and fell
         * to the generic map-fallback, which reinterprets the GrayArray's
         * raw memory as a GrayMap and segfaults. */
        emit_formatted(codegen, "static GrayString gray_json_stringify_array_%s(GrayArena *arena, GrayArray _arr) {\n", struct_name);
        emit_formatted(codegen, "    GrayString *_parts = (GrayString *)gray_arena_alloc(arena, sizeof(GrayString) * (size_t)(_arr.len > 0 ? _arr.len : 1));\n");
        emit_formatted(codegen, "    size_t _need = 2;\n");
        emit_formatted(codegen, "    for (int32_t _i = 0; _i < _arr.len; _i++) {\n");
        emit_formatted(codegen, "        GrayStruct_%s _item = *(GrayStruct_%s *)((char *)_arr.data + (size_t)_i * (size_t)_arr.elem_size);\n", struct_name, struct_name);
        emit_formatted(codegen, "        GrayString _js = gray_json_stringify_%s(arena, _item);\n", struct_name);
        emit_formatted(codegen, "        _parts[_i] = _js;\n");
        emit_formatted(codegen, "        _need += (size_t)_js.len;\n");
        emit_formatted(codegen, "        if (_i > 0) _need += 2;\n");
        emit_formatted(codegen, "    }\n");
        emit_formatted(codegen, "    char *_buf = gray_arena_alloc(arena, _need + 1);\n");
        emit_formatted(codegen, "    int _pos = 0;\n");
        emit_formatted(codegen, "    _buf[_pos++] = '[';\n");
        emit_formatted(codegen, "    for (int32_t _i = 0; _i < _arr.len; _i++) {\n");
        emit_formatted(codegen, "        if (_i > 0) { _buf[_pos++] = ','; _buf[_pos++] = ' '; }\n");
        emit_formatted(codegen, "        memcpy(_buf + _pos, _parts[_i].data, (size_t)_parts[_i].len); _pos += _parts[_i].len;\n");
        emit_formatted(codegen, "    }\n");
        emit_formatted(codegen, "    _buf[_pos++] = ']';\n");
        emit_formatted(codegen, "    _buf[_pos] = '\\0';\n");
        emit_formatted(codegen, "    return (GrayString){_buf, (int32_t)_pos};\n");
        emit_formatted(codegen, "}\n\n");
    }
}

/* Register every function (struct functions under their prefixed names),
 * then emit multi-return typedefs and forward declarations for all of them. */
static void codegen_emit_forward_declarations(CodeGen *codegen, const TopLevelStatements *top_level) {
    /* Collect all function declarations (including struct-namespaced) */
    for (int i = 0; i < top_level->function_bucket_count; i++) {
        AstNode *statement = top_level->function_bucket[i];
        statement->data.function_declaration.name =
            codegen_declaration_name(codegen, statement, statement->data.function_declaration.name);
        GROW_ARRAY(codegen->all_functions, codegen->function_count, codegen->function_capacity);
        codegen->all_functions[codegen->function_count++] = statement;
    }
    /* The by-name index sorts on func_decl.name, which the loop above just
     * rewrote; anything built before this point is keyed by the old names. */
    codegen->is_functions_by_name_built = false;
    /* Collect struct-namespaced functions with prefixed names */
    for (int i = 0; i < codegen->struct_declaration_count; i++) {
        AstNode *statement = codegen->struct_declarations[i];
        for (int j = 0; j < statement->data.struct_declaration.function_count; j++) {
            AstNode *function_node = statement->data.struct_declaration.functions[j].function_declaration;
            if (function_node && function_node->kind == NODE_FUNCTION_DECLARATION) {
                const char *struct_name = statement->data.struct_declaration.name;
                const char *function_name = function_node->data.function_declaration.name;
                size_t struct_name_length = strlen(struct_name);
                size_t function_name_length = strlen(function_name);
                size_t namespace_length = struct_name_length + 1 + function_name_length + 1;
                char *namespaced_name = malloc(namespace_length);
                snprintf(namespaced_name, namespace_length, "%s_%s", struct_name, function_name);
                function_node->data.function_declaration.name = namespaced_name;
                GROW_ARRAY(codegen->namespaced_function_names, codegen->namespaced_function_name_count,
                    codegen->namespaced_function_name_capacity);
                codegen->namespaced_function_names[codegen->namespaced_function_name_count++] = namespaced_name;

                GROW_ARRAY(codegen->all_functions, codegen->function_count, codegen->function_capacity);
                codegen->all_functions[codegen->function_count++] = function_node;
            }
        }
    }

    /* Emit multi-return type definitions. Skip generic functions whose
     * return types contain '?'; those need per-instantiation typedefs
     * emitted during monomorphisation ). Use codegen->all_functions so that
     * struct-namespaced functions (already renamed to StructName_func)
     * are included alongside top-level functions. */
    for (int i = 0; i < codegen->function_count; i++) {
        AstNode *statement = codegen->all_functions[i];
        if (statement->kind == NODE_FUNCTION_DECLARATION &&
            statement->data.function_declaration.return_type_count > 1) {
            bool has_wildcard = false;
            for (int return_index = 0; return_index < statement->data.function_declaration.return_type_count; return_index++) {
                if (statement->data.function_declaration.return_types[return_index] &&
                    strchr(statement->data.function_declaration.return_types[return_index], '?')) {
                    has_wildcard = true;
                    break;
                }
            }
            /* A bare return type resolves against the function's own module,
             * so the typedef's field types must be emitted in that module —
             * otherwise a cross-module enum falls back to an undefined
             * GrayStruct_<Name> instead of GrayEnum_<module>_<Name>. */
            if (!has_wildcard) {
                codegen_enter_node(codegen, statement);
                emit_multi_return_typedef(codegen, statement);
            }
        }
    }

    /* Emit forward declarations for all functions (including struct-namespaced) */
    for (int i = 0; i < codegen->function_count; i++) {
        AstNode *statement = codegen->all_functions[i];
        if (statement->kind != NODE_FUNCTION_DECLARATION) continue;
        /* A parameter or return type written bare resolves against the module
         * the function was declared in, so the forward declaration has to be
         * emitted in that module — otherwise it disagrees with the definition
         * whenever two modules declare the same type name. */
        codegen_enter_node(codegen, statement);
        if (strcmp(statement->data.function_declaration.name, "main") == 0) {
            emit(codegen, "static void gray_fn_main(void);\n");
            continue;
        }

        /* Detect wildcard generics ); emit one forward per
         * instantiation under a mangled name, skipping the un-specialised
         * signature which would contain '?' in C. */
        bool has_wildcard = function_is_generic(statement);

        int emit_rounds = has_wildcard ? statement->data.function_declaration.instantiation_count : 1;
        const char *original_name = statement->data.function_declaration.name;
        for (int round = 0; round < emit_rounds; round++) {
            const char *saved_binding = codegen->wildcard_binding;
            /* mangled is heap-allocated so the AST temporarily points at
             * stable memory while emit_multi_return_typedef / function_return_type
             * read statement->data.function_declaration.name. */
            char *mangled = NULL;
            if (has_wildcard) {
                mangled = xmalloc(MESSAGE_BUFFER_SIZE);
                const char *concrete = statement->data.function_declaration.instantiations[round];
                codegen->wildcard_binding = concrete;
                mangle_generic_name(mangled, MESSAGE_BUFFER_SIZE, original_name, concrete);
            }
            const char *emit_name = has_wildcard ? mangled : original_name;
            /* Temporarily set the func name to the mangled version so
             * function_return_type sees the right name for multi-return
             * structs  + ). */
            if (has_wildcard) statement->data.function_declaration.name = mangled;
            /* emit per-instantiation multi-return typedef
             * before the forward declaration that references it. */
            if (has_wildcard && statement->data.function_declaration.return_type_count > 1) {
                bool has_wildcard_return = false;
                for (int return_index = 0; return_index < statement->data.function_declaration.return_type_count; return_index++) {
                    if (statement->data.function_declaration.return_types[return_index] &&
                        strchr(statement->data.function_declaration.return_types[return_index], '?')) {
                        has_wildcard_return = true;
                        break;
                    }
                }
                if (has_wildcard_return) emit_multi_return_typedef(codegen, statement);
            }
            emit_formatted(codegen, "static %s ", function_return_type(codegen, statement));
            if (has_wildcard) statement->data.function_declaration.name = original_name;
            emit_formatted(codegen, "gray_fn_%s(",
                has_wildcard ? emit_name : codegen_declaration_name(codegen, statement, emit_name));
            {
                bool is_first_forward = true;
                for (int j = 0; j < statement->data.function_declaration.parameter_count; j++) {
                    Parameter *parameter = &statement->data.function_declaration.parameters[j];
                    if (parameter->is_type_parameter) continue;
                    if (!is_first_forward) emit(codegen, ", ");
                    is_first_forward = false;
                    if (parameter->is_mutable) {
                        emit_formatted(codegen, "%s *", gray_type_to_c_codegen(codegen,parameter->type_name));
                    } else {
                        emit(codegen, gray_type_to_c_codegen(codegen,parameter->type_name));
                    }
                }
                if (is_first_forward) {
                    emit(codegen, "void");
                }
            }
            emit(codegen, ");\n");
            codegen->wildcard_binding = saved_binding;
            free(mangled);
        }
    }
    emit(codegen, "\n");
}

static void codegen_emit_bodies(CodeGen *codegen, const TopLevelStatements *top_level) {
    /* Emit global constants/variables first so they're visible to all functions
     * (e.g. when used as default parameter values at a call site). */
    for (int i = 0; i < top_level->variable_bucket_count; i++) {
        emit_statement(codegen, top_level->variable_bucket[i]);
    }

    /* Emit remaining top-level statements (functions, enums, structs, etc.).
     * enum/struct/import/using/module are no-ops in emit_statement. */
    for (int i = 0; i < top_level->function_bucket_count; i++) {
        emit_statement(codegen, top_level->function_bucket[i]);
    }
    for (int i = 0; i < top_level->other_bucket_count; i++) {
        emit_statement(codegen, top_level->other_bucket[i]);
    }

    /* Emit struct-namespaced function definitions */
    for (int i = 0; i < codegen->struct_declaration_count; i++) {
        AstNode *statement = codegen->struct_declarations[i];
        for (int j = 0; j < statement->data.struct_declaration.function_count; j++) {
            AstNode *function_node = statement->data.struct_declaration.functions[j].function_declaration;
            if (function_node && function_node->kind == NODE_FUNCTION_DECLARATION) {
                emit_statement(codegen, function_node);
            }
        }
    }
}

/* The C main(): runtime init, file-scope initializers, then gray_fn_main()
 * or, under --test, the #test runner. */
static void codegen_emit_main(CodeGen *codegen, const TopLevelStatements *top_level) {
    emit(codegen, "int main(int argc, char **argv) {\n");
    emit(codegen, "    (void)argc; (void)argv;\n");
    {
        size_t limit = codegen->arena_limit > 0
            ? codegen->arena_limit : (size_t)1073741824;
        emit_formatted(codegen, "    gray_runtime_init(%zuULL);\n", limit);
    }
    emit(codegen, "    gray_os_init(argc, argv);\n");
    /* Initialize file-scope arrays that can't use C static initializers */
    if (codegen->global_initializer.length > 0) {
        /* Module-level containers grow and copy their keys into the arena they
         * were created in. Create them in the heap arena, which no function
         * return rewinds. */
        emit(codegen, "    { GrayArena *_gray_init_saved = gray_default_arena; "
                      "gray_default_arena = gray_heap_arena;\n");
        append_string_to_buffer(&codegen->output, codegen->global_initializer.data);
        emit(codegen, "    gray_default_arena = _gray_init_saved; }\n");
    }
    if (codegen->is_test_mode) {
        /* Test runner: call each #test function under the runner's recovery
         * point so a failed assert/panic is recorded, not fatal. */
        emit(codegen, "    gray_test_begin();\n");
        for (int i = 0; i < top_level->function_bucket_count; i++) {
            AstNode *function_node = top_level->function_bucket[i];
            if (function_node->kind != NODE_FUNCTION_DECLARATION || !function_node->data.function_declaration.is_test) continue;
            const char *name = function_node->data.function_declaration.original_name
                ? function_node->data.function_declaration.original_name : function_node->data.function_declaration.name;
            emit_formatted(codegen,
                "    gray_test_run(\"%s\", gray_fn_%s, \"%s\", %d);\n",
                name, function_node->data.function_declaration.name, codegen->file, function_node->token.line);
        }
        emit(codegen, "    int _gray_test_rc = gray_test_end();\n");
        emit(codegen, "    return _gray_test_rc;\n");
        emit(codegen, "}\n");
    } else {
        emit(codegen, "    gray_fn_main();\n");
        emit(codegen, "    return 0;\n");
        emit(codegen, "}\n");
    }
}

void codegen_generate(CodeGen *codegen, AstNode *program) {
    if (program->kind != NODE_PROGRAM) return;

    TopLevelStatements top_level;
    codegen_collect_top_level(codegen, program, &top_level);
    size_t collection_include_anchor = codegen_emit_preamble(codegen, &top_level);
    codegen_emit_type_definitions(codegen, &top_level);
    codegen_emit_json_helpers(codegen);
    codegen_emit_forward_declarations(codegen, &top_level);
    codegen_emit_bodies(codegen, &top_level);
    codegen_emit_main(codegen, &top_level);

    /* Splice the collection headers into the preamble now that body emission
     * has settled which ones are actually used. */
    if (codegen->needs_arrays_header || codegen->needs_maps_header || codegen->needs_strings_header) {
        StringBuffer includes = buffer_create(64);
        if (codegen->needs_arrays_header)
            append_string_to_buffer(&includes, "#include \"arrays.h\"\n");
        if (codegen->needs_maps_header)
            append_string_to_buffer(&includes, "#include \"maps.h\"\n");
        if (codegen->needs_strings_header)
            append_string_to_buffer(&includes, "#include \"strings.h\"\n");

        StringBuffer *output = &codegen->output;
        size_t insert_length = includes.length;
        size_t tail_length = output->length - collection_include_anchor;
        /* Reserve capacity and grow len by insert_length (append then shift). */
        append_bytes_to_buffer(output, includes.data, insert_length);
        memmove(output->data + collection_include_anchor + insert_length,
                output->data + collection_include_anchor, tail_length);
        memcpy(output->data + collection_include_anchor, includes.data, insert_length);

        buffer_destroy(&includes);
    }

    free(top_level.enum_bucket);
    free(top_level.function_bucket);
    free(top_level.variable_bucket);
    free(top_level.other_bucket);
}

const char *codegen_result(CodeGen *codegen) {
    return buffer_to_string(&codegen->output);
}

void codegen_destroy(CodeGen *codegen) {
    buffer_destroy(&codegen->output);
    buffer_destroy(&codegen->global_initializer);
    free(codegen->file_owned);
    free(codegen->enum_names);
    free(codegen->is_enum_string);
    free(codegen->is_enum_tagged);
    free(codegen->enum_declarations);
    free(codegen->all_functions);
    free(codegen->functions_by_name);
    free(codegen->reference_variables);
    free(codegen->raw_variables);
    free(codegen->wide_integer_variable_names);
    free(codegen->wide_integer_variable_types);
    free(codegen->struct_declarations);
    free(codegen->function_field_index);
    free(codegen->using_modules);
    free(codegen->type_alias_names);
    free(codegen->type_alias_targets);
    free(codegen->imported_modules);
    /* Individual c_headers[] entries for a resolved local header are
     * strdup'd by resolve_local_c_header() and deliberately left unfreed —
     * grayc is a one-shot-per-process CLI compiler, and their number is
     * bounded by the program's own `extern import "./x.h"` count. */
    free(codegen->c_headers);
    free(codegen->is_c_header_local);
    free(codegen->scope_arenas);
    for (int i = 0; i < codegen->iteration_guard_count; i++)
        free(codegen->iteration_guards[i]);
    free(codegen->iteration_guards);
    for (int i = 0; i < codegen->namespaced_function_name_count; i++)
        free(codegen->namespaced_function_names[i]);
    free(codegen->namespaced_function_names);
    free(codegen->last_line_directive_file);
}
