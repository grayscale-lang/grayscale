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
#define GRAY_SYNTH_PREFIX  "_gray_"
#define GRAY_SYNTH_TMP     "_gray_tmp"
#define GRAY_SYNTH_OR      "_gray_or"

typedef enum {
    /* Expressions */
    NODE_LABEL,
    NODE_INT_VALUE,
    NODE_FLOAT_VALUE,
    NODE_STRING_VALUE,
    NODE_INTERPOLATED_STRING,
    NODE_CHAR_VALUE,
    NODE_BOOL_VALUE,
    NODE_NIL_VALUE,
    NODE_ARRAY_VALUE,
    NODE_MAP_VALUE,
    NODE_STRUCT_VALUE,
    NODE_PREFIX_EXPR,
    NODE_INFIX_EXPR,
    NODE_POSTFIX_EXPR,
    NODE_CALL_EXPR,
    NODE_INDEX_EXPR,
    NODE_MEMBER_EXPR,
    NODE_NEW_EXPR,
    NODE_RANGE_EXPR,
    NODE_CAST_EXPR,
    NODE_FUNC_REF,
    NODE_IMPLICIT_ENUM,
    NODE_WHEN_PATTERN,

    /* Statements */
    NODE_VAR_DECL,
    NODE_ASSIGN_STMT,
    NODE_RETURN_STMT,
    NODE_ENSURE_STMT,
    NODE_EXPR_STMT,
    NODE_BLOCK_STMT,
    NODE_IF_STMT,
    NODE_WHEN_STMT,
    NODE_FOR_STMT,
    NODE_FOR_EACH_STMT,
    NODE_WHILE_STMT,
    NODE_LOOP_STMT,
    NODE_BREAK_STMT,
    NODE_CONTINUE_STMT,
    NODE_FUNC_DECL,
    NODE_IMPORT_STMT,
    NODE_USING_STMT,
    NODE_STRUCT_DECL,
    NODE_ENUM_DECL,
    NODE_ALIAS_DECL,
    NODE_MODULE_DECL,
    NODE_PROGRAM,
} NodeKind;

/* Forward declaration */
typedef struct AstNode AstNode;

/* Parameter for function declarations */
typedef struct {
    const char *name;
    const char *type_name;
    bool mutable;
    bool is_type_param;    /* true when declared with <?> syntax */
    AstNode *default_value;
} Param;

/* Field in struct declaration */
typedef struct {
    const char *name;
    const char *type_name;
    AstNode *default_value;
} StructField;

/* Function in struct declaration (namespaced free function) */
typedef struct {
    AstNode *func_decl; /* NODE_FUNC_DECL */
    bool is_private;
} StructFunc;

/* Enum value */
typedef struct {
    const char *name;
    AstNode *value;              /* optional explicit value (plain enums only) */
    const char **payload_types;  /* NULL if no payload */
    int payload_count;           /* 0 for plain variants */
} EnumVal;

/* Import item */
typedef struct {
    const char *alias;
    const char *module;
    const char *path;
    bool is_stdlib;
    bool is_c_import;   /* import c"header.h" — raw C header include */
    const char *source_dir; /* directory of the file containing this import (for transitive resolution) */
} ImportItem;

/* When case */
typedef struct {
    AstNode **values;
    int value_count;
    AstNode *body;
    bool is_range;
    Token kw_token;              /* preserves is/case keyword */
} WhenCase;

/* AST Node - tagged union */
struct AstNode {
    NodeKind kind;
    Token token;

    /* The declaration this node refers to, once the type checker has resolved
     * it. Codegen reads it rather than resolving the name a second time.
     * NULL on nodes that name no declaration. Opaque here: ast.h is included
     * by the parser, which has no symbol table. */
    struct DeclEntry_ *resolved_decl;

    union {
        /* NODE_LABEL */
        struct { const char *value; } label;

        /* NODE_INT_VALUE
         * value: low 64 bits of the literal as a signed bit pattern
         *        (cast to uint64_t to recover the original positive value
         *         when overflow=true)
         * overflow:     literal exceeds INT64_MAX (still ≤ UINT64_MAX)
         * overflow_u64: literal exceeds UINT64_MAX entirely */
        struct {
            int64_t value;
            const char *literal;
            bool overflow;
            bool overflow_u64;
        } int_value;

        /* NODE_FLOAT_VALUE */
        struct { double value; } float_value;

        /* NODE_STRING_VALUE */
        struct { const char *value; bool is_raw; } string_value;

        /* NODE_INTERPOLATED_STRING */
        struct { AstNode **parts; int part_count; } interpolated_string;

        /* NODE_CHAR_VALUE */
        struct { char value; } char_value;

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
        } struct_value;

        /* NODE_PREFIX_EXPR */
        struct { TokenType op; AstNode *right; } prefix;

        /* NODE_INFIX_EXPR */
        struct { AstNode *left; TokenType op; AstNode *right; } infix;

        /* NODE_POSTFIX_EXPR */
        struct { AstNode *left; TokenType op; } postfix;

        /* NODE_CALL_EXPR */
        struct { AstNode *function; AstNode **args; int arg_count; const char **arg_names; } call;

        /* NODE_INDEX_EXPR */
        struct { AstNode *left; AstNode *index; } index_expr;

        /* NODE_MEMBER_EXPR */
        struct { AstNode *object; const char *member; } member;

        /* NODE_NEW_EXPR */
        struct { const char *type_name; } new_expr;

        /* NODE_RANGE_EXPR */
        struct { AstNode *start; AstNode *end; AstNode *step; } range_expr;

        /* NODE_CAST_EXPR */
        struct {
            AstNode *value;
            const char *target_type;
            bool is_array;
            const char *element_type;
        } cast;

        /* NODE_FUNC_REF — ()func_name */
        struct { AstNode *function; } func_ref;

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

        /* NODE_VAR_DECL */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            const char *type_name;
            AstNode *value;
            bool mutable;
            bool is_private;
            bool synthetic;            /* parser-generated temp, not user-written */
        } var_decl;

        /* NODE_ASSIGN_STMT */
        struct {
            AstNode *target;
            TokenType op;
            AstNode *value;
            bool is_decl;  /* true when typechecker promotes to implicit declaration */
        } assign;

        /* NODE_RETURN_STMT */
        struct { AstNode **values; int count; } return_stmt;

        /* NODE_ENSURE_STMT */
        struct { AstNode *expr; } ensure_stmt;

        /* NODE_EXPR_STMT */
        struct { AstNode *expr; } expr_stmt;

        /* NODE_BLOCK_STMT */
        struct { AstNode **stmts; int count; int cap; } block;

        /* NODE_IF_STMT */
        struct {
            AstNode *condition;
            AstNode *consequence;
            AstNode *alternative; /* can be another if_stmt or block */
            Token else_token;     /* preserves else/otherwise keyword */
        } if_stmt;

        /* NODE_WHEN_STMT */
        struct {
            AstNode *value;
            WhenCase *cases;
            int case_count;
            AstNode *default_body;
            bool is_strict;
        } when_stmt;

        /* NODE_FOR_STMT */
        struct {
            const char *var_name;
            const char *var_type;
            AstNode *iterable;
            AstNode *body;
        } for_stmt;

        /* NODE_FOR_EACH_STMT */
        struct {
            const char *index_name;
            const char *var_name;
            AstNode *collection;
            AstNode *body;
        } for_each;

        /* NODE_WHILE_STMT */
        struct { AstNode *condition; AstNode *body; } while_stmt;

        /* NODE_LOOP_STMT */
        struct { AstNode *body; } loop_stmt;

        /* NODE_FUNC_DECL */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            Param *params;
            int param_count;
            const char **return_types;
            const char **return_names; /* Named return params (NULL if unnamed) */
            int return_type_count;
            AstNode *body;
            bool is_private;
            bool is_discard;
            bool is_deprecated;              /* #deprecated attribute */
            const char *deprecated_message;  /* NULL if bare #deprecated */
            /* Wildcard generics concrete type bindings recorded
             * by the typechecker per call site. Codegen emits one
             * specialised C function for each entry. NULL/0 for
             * non-generic functions. */
            const char **instantiations;
            int instantiation_count;
        } func_decl;

        /* NODE_IMPORT_STMT */
        struct {
            ImportItem *items;
            int count;
            bool auto_use;
        } import_stmt;

        /* NODE_USING_STMT */
        struct {
            const char **modules;
            int count;
        } using_stmt;

        /* NODE_STRUCT_DECL */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            StructField *fields;
            int field_count;
            StructFunc *funcs;
            int func_count;
            bool is_json; /* #json attribute — enables JSON ser/deser */
            bool is_generic; /* has ? in at least one field type */
            const char **instantiations; /* concrete bindings */
            int instantiation_count;
            bool is_deprecated;              /* #deprecated attribute */
            const char *deprecated_message;  /* NULL if bare #deprecated */
        } struct_decl;

        /* NODE_ENUM_DECL */
        struct {
            const char *name;
            const char *original_name; /* pre-prefix name for error messages */
            EnumVal *values;
            int value_count;
            bool is_flags;
            bool is_tagged;  /* true if ANY variant has a payload */
            bool is_deprecated;              /* #deprecated attribute */
            const char *deprecated_message;  /* NULL if bare #deprecated */
        } enum_decl;

        /* NODE_ALIAS_DECL */
        struct {
            const char *name;
            const char *target_type;
            bool is_private;
        } alias_decl;

        /* NODE_MODULE_DECL */
        struct { const char *name; } module_decl;

        /* NODE_PROGRAM */
        struct {
            AstNode *module_decl;
            AstNode **using_stmts;
            int using_count;
            AstNode **stmts;
            int stmt_count;
            int stmt_cap;
        } program;
    } data;
};

/* Node constructor helpers */
AstNode *ast_alloc(Arena *arena, NodeKind kind, Token token);

#endif
