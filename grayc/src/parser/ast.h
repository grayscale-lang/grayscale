/*
 * ast.h — Abstract syntax tree node definitions for the Grayscale compiler.
 * Declares the NodeKind enum, the AstNode union struct, and supporting types
 * for parameters, struct fields, enum variants, and import items.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_AST_H
#define GRAYC_AST_H

#include "../lexer/token.h"
#include "../util/arena.h"
#include <stdbool.h>
#include <stdint.h>

/* Compiler-generated synthetic variable prefixes.
 * The parser creates these; the typechecker and codegen check for them. */
#define GRAY_SYNTHETIC_PREFIX  "_gray_"
#define GRAY_SYNTHETIC_TEMPORARY     "_gray_tmp"
#define GRAY_SYNTHETIC_OR      "_gray_or"

/* Sentinel member name for the or_return propagation guard's error access.
 * The parser emits `_gray_orN.verr`; the typechecker rewrites it to the
 * concrete trailing-Error slot (v1, v2, ...) once the call's arity is known. */
#define OR_RETURN_ERROR_SLOT "verr"

typedef enum {
    /* Expressions */
    NODE_LABEL,
    NODE_INTEGER_LITERAL,
    NODE_FLOATING_POINT_LITERAL,
    NODE_STRING_VALUE,
    NODE_INTERPOLATED_STRING,
    NODE_CHAR_VALUE,
    NODE_BOOL_VALUE,
    NODE_NIL_VALUE,
    NODE_ARRAY_VALUE,
    NODE_MAP_VALUE,
    NODE_STRUCT_VALUE,
    NODE_PREFIX_EXPRESSION,
    NODE_INFIX_EXPRESSION,
    NODE_POSTFIX_EXPRESSION,
    NODE_CALL_EXPRESSION,
    NODE_INDEX_EXPRESSION,
    NODE_MEMBER_EXPRESSION,
    NODE_NEW_EXPRESSION,
    NODE_RANGE_EXPRESSION,
    NODE_CAST_EXPRESSION,
    NODE_FUNCTION_REFERENCE,
    NODE_IMPLICIT_ENUM,
    NODE_WHEN_PATTERN,

    /* Statements */
    NODE_VARIABLE_DECLARATION,
    NODE_ASSIGN_STATEMENT,
    NODE_RETURN_STATEMENT,
    NODE_ENSURE_STATEMENT,
    NODE_EXPRESSION_STATEMENT,
    NODE_BLOCK_STATEMENT,
    NODE_IF_STATEMENT,
    NODE_WHEN_STATEMENT,
    NODE_FOR_STATEMENT,
    NODE_FOR_EACH_STATEMENT,
    NODE_WHILE_STATEMENT,
    NODE_LOOP_STATEMENT,
    NODE_BREAK_STATEMENT,
    NODE_CONTINUE_STATEMENT,
    NODE_FUNCTION_DECLARATION,
    NODE_IMPORT_STATEMENT,
    NODE_USING_STATEMENT,
    NODE_STRUCT_DECLARATION,
    NODE_ENUM_DECLARATION,
    NODE_ALIAS_DECLARATION,
    NODE_MODULE_DECLARATION,
    NODE_PROGRAM,
} NodeKind;

/* Forward declaration */
typedef struct AstNode AstNode;

/* Parameter for function declarations */
typedef struct {
    const char *name;
    const char *type_name;
    bool is_mutable;
    bool is_type_parameter; /* true when declared with <?> syntax */
    AstNode *default_value;
} Parameter;

/* Field in struct declaration */
typedef struct {
    const char *name;
    const char *type_name;
    AstNode *default_value;
    /* Optional trailing tag, written `` `json:"Name"` `` right after the
     * type. NULL when absent. The parser stores the raw backtick-string
     * content verbatim; the typechecker validates it (for #json structs)
     * and rewrites it in place to just the extracted key. */
    const char *json_tag;
} StructField;

/* Function in struct declaration (namespaced free function) */
typedef struct {
    AstNode *function_declaration; /* NODE_FUNCTION_DECLARATION */
    bool is_private;
} StructFunction;

/* Enum value */
typedef struct {
    const char *name;
    AstNode *value;              /* optional explicit value (plain enums only) */
    const char **payload_types;  /* NULL if no payload */
    int payload_count;           /* 0 for plain variants */
} EnumValue;

/* Import item */
typedef struct {
    const char *alias;
    const char *module;
    const char *path;
    bool is_stdlib;
    bool is_c_import;   /* extern import "header.h" — raw C header include */
    const char *source_directory; /* directory of the file containing this import (for transitive resolution) */
    Token token;        /* the header-path string literal, for diagnostics (C imports only) */
} ImportItem;

/* When case */
typedef struct {
    AstNode **values;
    int value_count;
    AstNode *body;
    bool is_range;
    Token keyword_token;         /* preserves is/case keyword */
} WhenCase;

/* AST Node - tagged union */
struct AstNode {
    NodeKind kind;
    Token token;

    /* The declaration this node refers to, once the type checker has resolved
     * it. Codegen reads it rather than resolving the name a second time.
     * NULL on nodes that name no declaration. Opaque here: ast.h is included
     * by the parser, which has no symbol table. */
    struct DeclarationEntry_ *resolved_declaration;

    /* Set by the type checker on a value stored into a fixed-size [T,N]
     * struct field when the value's length can't be proven at compile time:
     * holds N, and codegen checks the length at runtime. 0 otherwise. */
    int runtime_fixed_length;

    /* Set by the type checker on a value with fewer than N elements stored
     * into a fixed-size [T,N] struct field: holds N, and codegen zero-fills
     * the stored array up to N. 0 otherwise. */
    int zero_fill_length;

    /* Set by the type checker on a number literal expression once it has
     * taken its type: the value folded at full width, as decimal text.
     * Codegen emits it as a constant of the node's type in place of the
     * expression. NULL on anything else, and on a literal shifted by a
     * non-constant count. */
    const char *folded_literal;

    /* Set by the type checker on a value it implicitly widens into a wide
     * integer type (i128, u128, i256, u256): that type's name. Codegen
     * converts the value to it wherever the value is emitted. NULL when the
     * value is stored at its own type. */
    const char *widen_to;

    union {
        /* NODE_LABEL */
        struct {
            const char *value;
            /* Set by the type checker when this name resolves to a
             * file-scope variable declared in the entry module (not a
             * local, parameter, or shadowing binding). Codegen gives such
             * a global a gray_g_ prefix so a name like `log` or `index`
             * cannot collide with a libc identifier from the runtime
             * headers. */
            bool is_file_global_reference;
            /* Set by the type checker when this name resolves to a local,
             * parameter, loop variable, or pattern binding. Such a binding
             * hides a same-named member a `using` brings in, so codegen
             * must emit it as written rather than resolve it as a module
             * member. */
            bool is_local_reference;
        } label;

        /* NODE_INTEGER_LITERAL
         * value: low 64 bits of the literal as a signed bit pattern
         *        (cast to uint64_t to recover the original positive value
         *         when is_above_i64_maximum=true)
         * is_above_i64_maximum: literal exceeds INT64_MAX (still ≤ UINT64_MAX)
         * is_above_u64_maximum: literal exceeds UINT64_MAX entirely
         * literal:      the digits as decimal text ('_' kept for a decimal
         *               literal; a hex/octal/binary literal is converted) */
        struct {
            int64_t value;
            const char *literal;
            bool is_above_i64_maximum;
            bool is_above_u64_maximum;
        } integer_literal;

        /* NODE_FLOATING_POINT_LITERAL */
        struct { double value; } floating_point_literal;

        /* NODE_STRING_VALUE */
        struct { const char *value; bool is_raw; } string_value;

        /* NODE_INTERPOLATED_STRING */
        struct { AstNode **parts; int part_count; } interpolated_string;

        /* NODE_CHAR_VALUE — a full Unicode codepoint (char is int32_t at the C
         * boundary), not a single byte. */
        struct { int32_t value; } char_value;

        /* NODE_BOOL_VALUE */
        struct { bool value; } bool_value;

        /* NODE_ARRAY_VALUE */
        struct { AstNode **elements; int count; } array_value;

        /* NODE_MAP_VALUE */
        struct {
            AstNode **keys;
            AstNode **values;
            int count;
        } map_value;

        /* NODE_STRUCT_VALUE */
        struct {
            const char *name;
            const char **field_names;
            AstNode **field_values;
            int count;
            const char *wildcard_binding; /*concrete type for ? fields */
            /* E3127 already reported for this literal. A return value is
             * resolved twice — once for the statement, once against the
             * declared return type — and one bad literal is one error. */
            bool was_type_parameter_rejected;
        } struct_value;

        /* NODE_PREFIX_EXPRESSION */
        struct { TokenType operator; AstNode *right; } prefix;

        /* NODE_INFIX_EXPRESSION */
        struct { AstNode *left; TokenType operator; AstNode *right; } infix;

        /* NODE_POSTFIX_EXPRESSION */
        struct { AstNode *left; TokenType operator; } postfix;

        /* NODE_CALL_EXPRESSION */
        struct { AstNode *function; AstNode **arguments; int argument_count; const char **argument_names; } call;

        /* NODE_INDEX_EXPRESSION */
        struct { AstNode *left; AstNode *index; } index_expression;

        /* NODE_MEMBER_EXPRESSION */
        struct { AstNode *object; const char *member; } member;

        /* NODE_NEW_EXPRESSION */
        struct { const char *type_name; } new_expression;

        /* NODE_RANGE_EXPRESSION */
        struct { AstNode *start; AstNode *end; AstNode *step; } range_expression;

        /* NODE_CAST_EXPRESSION */
        struct {
            AstNode *value;
            const char *target_type;
            bool is_array;
            const char *element_type;
        } cast;

        /* NODE_FUNCTION_REFERENCE — ()func_name */
        struct { AstNode *function; } function_reference;

        /* NODE_IMPLICIT_ENUM — .VARIANT (resolved by typechecker) */
        struct { const char *variant; const char *resolved_enum; } implicit_enum;

        /* NODE_WHEN_PATTERN — destructuring pattern in when/is */
        struct {
            const char *variant;       /* e.g. "Circle" */
            const char *enum_name;     /* resolved by typechecker, e.g. "Shape" */
            const char **bindings;     /* binding variable names */
            int binding_count;
            bool is_implicit;          /* true if .Circle(r) form */
        } when_pattern;

        /* NODE_VARIABLE_DECLARATION */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            const char *type_name;
            AstNode *value;
            bool is_mutable;
            bool is_private;
            bool is_synthetic;         /* parser-generated temporary, not user-written */
        } variable_declaration;

        /* NODE_ASSIGN_STATEMENT */
        struct {
            AstNode *target;
            TokenType operator;
            AstNode *value;
            bool is_declaration;  /* true when typechecker promotes to implicit declaration */
        } assign;

        /* NODE_RETURN_STATEMENT */
        struct { AstNode **values; int count; } return_statement;

        /* NODE_ENSURE_STATEMENT */
        struct { AstNode *expression; } ensure_statement;

        /* NODE_EXPRESSION_STATEMENT */
        struct { AstNode *expression; } expression_statement;

        /* NODE_BLOCK_STATEMENT */
        struct { AstNode **statements; int count; int capacity; } block;

        /* NODE_IF_STATEMENT */
        struct {
            AstNode *condition;
            AstNode *consequence;
            AstNode *alternative; /* can be another if_stmt or block */
            Token else_token;     /* preserves else/otherwise keyword */
        } if_statement;

        /* NODE_WHEN_STATEMENT */
        struct {
            AstNode *value;
            WhenCase *cases;
            int case_count;
            AstNode *default_body;
            bool is_strict;
        } when_statement;

        /* NODE_FOR_STATEMENT */
        struct {
            const char *variable_name;
            const char *variable_type;
            AstNode *iterable;
            AstNode *body;
        } for_statement;

        /* NODE_FOR_EACH_STATEMENT */
        struct {
            const char *index_name;
            const char *variable_name;
            AstNode *collection;
            AstNode *body;
        } for_each;

        /* NODE_WHILE_STATEMENT */
        struct { AstNode *condition; AstNode *body; } while_statement;

        /* NODE_LOOP_STATEMENT */
        struct { AstNode *body; } loop_statement;

        /* NODE_FUNCTION_DECLARATION */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            Parameter *parameters;
            int parameter_count;
            const char **return_types;
            const char **return_names; /* Named return params (NULL if unnamed) */
            int return_type_count;
            AstNode *body;
            bool is_private;
            bool is_discard;
            bool is_test;                    /* #test attribute — test-only function */
            bool is_deprecated;              /* #deprecated attribute */
            const char *deprecated_message;  /* NULL if bare #deprecated */
            /* Wildcard generics concrete type bindings recorded
             * by the typechecker per call site. Codegen emits one
             * specialised C function for each entry. NULL/0 for
             * non-generic functions. */
            const char **instantiations;
            int instantiation_count;
        } function_declaration;

        /* NODE_IMPORT_STATEMENT */
        struct {
            ImportItem *items;
            int count;
            bool should_auto_use;
        } import_statement;

        /* NODE_USING_STATEMENT */
        struct {
            const char **modules;
            int count;
        } using_statement;

        /* NODE_STRUCT_DECLARATION */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            StructField *fields;
            int field_count;
            StructFunction *functions;
            int function_count;
            bool is_json; /* #json attribute — enables JSON serialization and deserialization */
            bool is_generic; /* has ? in at least one field type */
            const char **instantiations; /* concrete bindings */
            int instantiation_count;
            bool is_deprecated;              /* #deprecated attribute */
            const char *deprecated_message;  /* NULL if bare #deprecated */
            bool is_private;
        } struct_declaration;

        /* NODE_ENUM_DECLARATION */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            EnumValue *values;
            int value_count;
            bool is_flags;
            bool is_tagged;  /* true if ANY variant has a payload */
            bool is_error_code;              /* #error_code attribute */
            bool is_deprecated;              /* #deprecated attribute */
            const char *deprecated_message;  /* NULL if bare #deprecated */
            bool is_private;
        } enum_declaration;

        /* NODE_ALIAS_DECLARATION */
        struct {
            const char *name;
            const char *target_type;
            bool is_private;
        } alias_declaration;

        /* NODE_MODULE_DECLARATION */
        struct { const char *name; } module_declaration;

        /* NODE_PROGRAM */
        struct {
            AstNode *module_declaration;
            AstNode **using_statements;
            int using_count;
            AstNode **statements;
            int statement_count;
            int statement_capacity;
        } program;
    } data;
};

/* Node constructor helpers */
AstNode *ast_allocate(Arena *arena, NodeKind kind, Token token);

/* --- member expression shape accessors ---------------------------------
 *
 * `a.b` is a NODE_MEMBER_EXPRESSION whose object says what `a` is: a module, a
 * struct type, an enum type, a local, or another qualified name. Every phase
 * needs the written qualifier before it can decide which; these are the one
 * place the shape is tested, so a phase asks for the qualifier instead of
 * open-coding the node-kind check. */

/* The bare name a member expression is written against — "mod" in mod.f(),
 * "Type" in Type.VARIANT, "v" in v.field — or NULL when the object is not a
 * plain name. */
const char *ast_member_qualifier(const AstNode *node);

/* Like ast_member_qualifier, but sees through an explicit deref: `p^.f` is
 * written against `p`. */
const char *ast_member_base_qualifier(const AstNode *node);

/* The halves of a nested qualified spelling — mod.Type.member. Returns false,
 * leaving the outputs untouched, when the object is not itself written
 * against a bare name. Either output may be NULL. */
bool ast_member_chain(const AstNode *node, const char **out_qualifier,
                      const char **out_type);

#endif
