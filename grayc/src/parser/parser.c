/*
 * parser.c — Recursive descent parser for Grayscale. Transforms the token
 * stream from the lexer into an abstract syntax tree using Pratt parsing
 * for expressions and precedence-based operator handling.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "parser.h"
#include "../typechecker/types.h"
#include "../util/constants.h"
#include "../util/reserved.h"
#include "../util/xalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_MULTI_VARIABLES 16
#define MAX_SHARED_RETURNS 16
#define FLOATING_POINT_LITERAL_BUFFER_SIZE    128
#define TEMPORARY_NAME_BUFFER_SIZE     32
#define FIELD_NAME_BUFFER_SIZE           8
/* Attributes collected from one `#[a, b, ...]` list before they are applied
 * to the following declaration. One slot per AttributeBit: duplicates are rejected
 * as they are parsed, so a list can never hold more distinct entries than
 * there are attributes. */
#define MAX_ATTRIBUTE_LIST_ENTRIES 8

/* Operator precedence levels */
typedef enum {
    PRECEDENCE_LOWEST,
    PRECEDENCE_OR,            /* || */
    PRECEDENCE_AND,           /* && */
    PRECEDENCE_EQUALS,        /* == != */
    PRECEDENCE_BITWISE,       /* bit_and, bit_or, bit_xor — above == so a bit_and b == c → (a bit_and b) == c */
    PRECEDENCE_LESS_GREATER,   /* > < >= <= */
    PRECEDENCE_MEMBERSHIP,    /* in, not_in */
    PRECEDENCE_SHIFT,         /* bit_shift_left, bit_shift_right */
    PRECEDENCE_SUM,           /* + - */
    PRECEDENCE_PRODUCT,       /* * / % */
    PRECEDENCE_PREFIX,        /* -x !x bit_not x */
    PRECEDENCE_CALL,          /* f(x) */
    PRECEDENCE_INDEX,         /* a[i] a.b */
    PRECEDENCE_POSTFIX,       /* x++ x-- */
} Precedence;

/* Forward declarations */
static AstNode *parse_statement(Parser *parser);
static AstNode *parse_expression(Parser *parser, Precedence precedence);
static AstNode *parse_block_statement(Parser *parser);
static AstNode *parse_struct_literal(Parser *parser, const char *name);
static AstNode *maybe_apply_or_return(Parser *parser, AstNode *original_declaration);

/* --- Helpers --- */

/* One bit per attribute, tracked in parser->seen_attribute_mask for the
 * declaration currently being parsed so a repeat can be rejected (E2090). */
typedef enum {
    ATTRIBUTE_STRICT     = 1u << 0,
    ATTRIBUTE_FLAGS      = 1u << 1,
    ATTRIBUTE_JSON       = 1u << 2,
    ATTRIBUTE_DISCARD    = 1u << 3,
    ATTRIBUTE_TEST       = 1u << 4,
    ATTRIBUTE_DEPRECATED = 1u << 5,
    ATTRIBUTE_DOC        = 1u << 6,
    ATTRIBUTE_ERROR_CODE = 1u << 7,
} AttributeBit;

/* Returns true when this attribute was already applied to the current
 * declaration, emitting E2090 at the current token in that case. */
static bool reject_duplicate_attribute(Parser *parser, AttributeBit bit, const char *name) {
    if (parser->seen_attribute_mask & bit) {
        diagnostic_error_code_formatted(parser->diagnostics, "E2090",
            parser->file, parser->current_token.line, parser->current_token.column, 0, name);
        return true;
    }
    parser->seen_attribute_mask |= bit;
    return false;
}

/* Emits E2094 for an attribute applied to the wrong kind of declaration, or
 * with a malformed argument. Pins the code so the situation stays 1:1 with its
 * diagnostic (see scripts/check_error_codes.gray). `message` is arena-owned. */
static void emit_attribute_error(Parser *parser, const char *message, int line, int column) {
    diagnostic_error_message(parser->diagnostics, "E2094", message, parser->file, line, column, 0);
}

/* Maps a bare attribute name (as written inside a `#[...]` list) to its
 * AttributeBit. Returns 0 for an unrecognised name. */
static AttributeBit attribute_bit_for_name(const char *name) {
    if (strcmp(name, "strict") == 0)     return ATTRIBUTE_STRICT;
    if (strcmp(name, "flags") == 0)      return ATTRIBUTE_FLAGS;
    if (strcmp(name, "json") == 0)       return ATTRIBUTE_JSON;
    if (strcmp(name, "discard") == 0)    return ATTRIBUTE_DISCARD;
    if (strcmp(name, "test") == 0)       return ATTRIBUTE_TEST;
    if (strcmp(name, "deprecated") == 0) return ATTRIBUTE_DEPRECATED;
    if (strcmp(name, "doc") == 0)        return ATTRIBUTE_DOC;
    if (strcmp(name, "error_code") == 0) return ATTRIBUTE_ERROR_CODE;
    return (AttributeBit)0;
}

/* Applies one attribute (identified by its bare name) to the declaration that
 * follows a `#[...]` list, emitting the same E2094 misapplied-attribute message
 * the stacked `#attr` form uses when the declaration kind does not accept it.
 * `deprecated_message` is the optional #deprecated string (NULL otherwise); `where`
 * locates the diagnostic. */
static void apply_named_attribute(Parser *parser, AstNode *statement,
                                  const char *name, const char *deprecated_message,
                                  Token where) {
    if (strcmp(name, "test") == 0) {
        if (statement && statement->kind == NODE_FUNCTION_DECLARATION) {
            statement->data.function_declaration.is_test = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#test attribute can only be applied to function declarations"), where.line, where.column);
        }
    } else if (strcmp(name, "discard") == 0) {
        if (statement && statement->kind == NODE_FUNCTION_DECLARATION) {
            statement->data.function_declaration.is_discard = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#discard attribute can only be applied to function declarations"), where.line, where.column);
        }
    } else if (strcmp(name, "json") == 0) {
        if (statement && statement->kind == NODE_STRUCT_DECLARATION) {
            statement->data.struct_declaration.is_json = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#json attribute can only be applied to struct declarations"), where.line, where.column);
        }
    } else if (strcmp(name, "flags") == 0) {
        if (statement && statement->kind == NODE_ENUM_DECLARATION) {
            statement->data.enum_declaration.is_flags = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#flags attribute can only be applied to enum declarations"), where.line, where.column);
        }
    } else if (strcmp(name, "error_code") == 0) {
        if (statement && statement->kind == NODE_ENUM_DECLARATION) {
            statement->data.enum_declaration.is_error_code = true;
        } else {
            diagnostic_error_code(parser->diagnostics, "E3144",
                parser->file, where.line, where.column, 0);
        }
    } else if (strcmp(name, "strict") == 0) {
        if (statement && statement->kind == NODE_WHEN_STATEMENT) {
            statement->data.when_statement.is_strict = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#strict attribute can only be applied to when statements"), where.line, where.column);
        }
    } else if (strcmp(name, "deprecated") == 0) {
        if (statement && statement->kind == NODE_FUNCTION_DECLARATION) {
            statement->data.function_declaration.is_deprecated = true;
            statement->data.function_declaration.deprecated_message = deprecated_message;
        } else if (statement && statement->kind == NODE_STRUCT_DECLARATION) {
            statement->data.struct_declaration.is_deprecated = true;
            statement->data.struct_declaration.deprecated_message = deprecated_message;
        } else if (statement && statement->kind == NODE_ENUM_DECLARATION) {
            statement->data.enum_declaration.is_deprecated = true;
            statement->data.enum_declaration.deprecated_message = deprecated_message;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#deprecated attribute can only be applied to function, struct, or enum declarations"), where.line, where.column);
        }
    }
    /* "doc": the parser discards #doc metadata today, so there is nothing to
     * attach here — the entry is still validated for name and duplication. */
}

static void next_token(Parser *parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
    /* Surface lexer errors (E1xxx). The lexer does not call diagnostic_error_message()
     * directly — it sets error_code/error_msg on itself and returns
     * TOKEN_ILLEGAL. We detect that here and emit the diagnostic so the
     * lexer stays free of diagnostic dependencies. After emitting, clear
     * error_code so the same error is not reported twice. */
    if (parser->peek_token.type == TOKEN_ILLEGAL && parser->lexer->error_code) {
        diagnostic_error_message(parser->diagnostics, parser->lexer->error_code,
            arena_copy_string(parser->arena, parser->lexer->error_message),
            parser->file, parser->peek_token.line, parser->peek_token.column, 0);
        parser->lexer->error_code = NULL;
    }
}

static bool current_token_is(Parser *parser, TokenType type) {
    return parser->current_token.type == type;
}

static bool peek_token_is(Parser *parser, TokenType type) {
    return parser->peek_token.type == type;
}

static bool expect_peek_token(Parser *parser, TokenType type) {
    if (peek_token_is(parser, type)) {
        next_token(parser);
        return true;
    }
    char message[MESSAGE_BUFFER_SIZE];
    snprintf(message, sizeof(message), "expected '%s', got '%s'",
        token_type_name(type), token_display_name(parser->peek_token));
    /* Point at current token (where the expected token should be), not the peek token */
    diagnostic_error_message(parser->diagnostics, "E2001", arena_copy_string(parser->arena, message),
        parser->file, parser->current_token.line, parser->current_token.column, 0);
    return false;
}

/* Check if a token type is a keyword (reserved word). Derived from the
 * lexer's keyword table so this cannot drift as keywords are added.
 * `_` (TOKEN_BLANK) is excluded: it is the blank identifier and is valid in
 * binding positions. */
static bool is_keyword_token(TokenType type) {
    return type != TOKEN_BLANK && token_type_is_keyword(type);
}


/* Synchronize parser after an error; skip to a safe point.
 * Advances past the current line and stops at the next statement boundary. */
static void synchronize_parser(Parser *parser) {
    int error_line = parser->current_token.line;
    /* First, skip past the current line to avoid re-parsing the same error */
    while (!current_token_is(parser, TOKEN_END_OF_FILE) && parser->current_token.line == error_line) {
        next_token(parser);
    }
    /* Then find the next statement-starting token */
    while (!current_token_is(parser, TOKEN_END_OF_FILE)) {
        switch (parser->current_token.type) {
        case TOKEN_DO: case TOKEN_MUT: case TOKEN_CONST:
        case TOKEN_RETURN: case TOKEN_IF: case TOKEN_FOR:
        case TOKEN_FOR_EACH: case TOKEN_AS_LONG_AS: case TOKEN_LOOP:
        case TOKEN_WHEN: case TOKEN_IMPORT: case TOKEN_USING:
        case TOKEN_BREAK: case TOKEN_CONTINUE: case TOKEN_ALIAS:
        case TOKEN_RIGHT_BRACE:
        case TOKEN_IDENTIFIER:
            return;
        default:
            next_token(parser);
        }
    }
}

/* E4027: a reserved keyword written where `what` (e.g. "a variable name") is
 * expected. Emits at `token` and synchronizes; returns true on a match. */
static bool reject_keyword_as_name(Parser *parser, const Token *token, const char *what) {
    if (!is_keyword_token(token->type)) return false;
    char message[MESSAGE_BUFFER_SIZE];
    snprintf(message, sizeof(message), "'%s' is a reserved keyword and cannot be used as %s",
        token->literal, what);
    diagnostic_error_message(parser->diagnostics, "E4027", arena_copy_string(parser->arena, message),
        parser->file, token->line, token->column, 0);
    synchronize_parser(parser);
    return true;
}

/* --- Speculative-parse snapshots ---
 * Save the full lexer + token position, try a parse that may not pan out, and
 * restore on failure. Used wherever the grammar needs unbounded lookahead:
 * module-qualified struct literals, `const Name struct`, when-patterns. */
typedef struct {
    int position, read_position;
    char current_character;
    int line, column;
    Token current_token, peek_token;
} ParserSnapshot;

static void parser_snapshot_save(Parser *parser, ParserSnapshot *snapshot) {
    snapshot->position = parser->lexer->position;
    snapshot->read_position = parser->lexer->read_position;
    snapshot->current_character = parser->lexer->current_character;
    snapshot->line = parser->lexer->line;
    snapshot->column = parser->lexer->column;
    snapshot->current_token = parser->current_token;
    snapshot->peek_token = parser->peek_token;
}

static void parser_snapshot_restore(Parser *parser, const ParserSnapshot *snapshot) {
    parser->lexer->position = snapshot->position;
    parser->lexer->read_position = snapshot->read_position;
    parser->lexer->current_character = snapshot->current_character;
    parser->lexer->line = snapshot->line;
    parser->lexer->column = snapshot->column;
    parser->current_token = snapshot->current_token;
    parser->peek_token = snapshot->peek_token;
}

static Precedence get_token_precedence(TokenType type) {
    switch (type) {
    case TOKEN_OR:              return PRECEDENCE_OR;
    case TOKEN_AND:             return PRECEDENCE_AND;
    case TOKEN_EQUAL:
    case TOKEN_NOT_EQUAL:          return PRECEDENCE_EQUALS;
    case TOKEN_BIT_AND:
    case TOKEN_BIT_OR:
    case TOKEN_BIT_XOR:         return PRECEDENCE_BITWISE;
    case TOKEN_LESS_THAN:
    case TOKEN_GREATER_THAN:
    case TOKEN_LESS_THAN_OR_EQUAL:
    case TOKEN_GREATER_THAN_OR_EQUAL:           return PRECEDENCE_LESS_GREATER;
    case TOKEN_IN:
    case TOKEN_NOT_IN:          return PRECEDENCE_MEMBERSHIP;
    case TOKEN_BIT_SHIFT_LEFT:
    case TOKEN_BIT_SHIFT_RIGHT: return PRECEDENCE_SHIFT;
    case TOKEN_PLUS:
    case TOKEN_MINUS:           return PRECEDENCE_SUM;
    case TOKEN_ASTERISK:
    case TOKEN_SLASH:
    case TOKEN_PERCENT:         return PRECEDENCE_PRODUCT;
    case TOKEN_LEFT_PARENTHESIS:          return PRECEDENCE_CALL;
    case TOKEN_LEFT_BRACKET:        return PRECEDENCE_INDEX;
    case TOKEN_DOT:             return PRECEDENCE_INDEX;
    case TOKEN_INCREMENT:
    case TOKEN_DECREMENT:
    case TOKEN_CARET:           return PRECEDENCE_POSTFIX;
    default:                  return PRECEDENCE_LOWEST;
    }
}

/* --- Expression Parsing --- */

/* True when a type spelling contains the generic wildcard `?`. The wildcard is
 * stored as the literal string "?" in the same slot as any other type name and
 * carried unchanged until the typechecker replaces it with a concrete type. */
static bool type_string_has_wildcard(const char *type_name) {
    if (!type_name) return false;
    for (const char *cursor = type_name; *cursor; cursor++) {
        if (*cursor == '?') return true;
    }
    return false;
}

/* Read a type name: simple (i64, Person) or qualified (models.Task).
 * Assumes current token is the first identifier. Returns arena-allocated string. */
static const char *read_type_name(Parser *parser) {
    /* Wildcard type placeholder: `?` in a type position */
    if (current_token_is(parser, TOKEN_QUESTION)) {
        return "?";
    }
    const char *name = parser->current_token.literal;
    if (peek_token_is(parser, TOKEN_DOT)) {
        next_token(parser); /* skip . */
        next_token(parser); /* qualified part */
        size_t qualifier_length = strlen(name), member_length = strlen(parser->current_token.literal);
        size_t qualified_length = qualifier_length + member_length + 2;
        char *qualified = arena_allocate(parser->arena, qualified_length);
        /* The qualifier stays attached: mod.Type is carried through as written
         * and resolved against the symbol table, not flattened to mod_Type
         * here where there is nothing to resolve it against. */
        snprintf(qualified, qualified_length, "%s.%s", name, parser->current_token.literal);
        return qualified;
    }
    return name;
}

/* True when the parser sits on a token that can only begin a pointer or
 * container type spelling: ^T, [T], [T,N], or map[K:V].  'map' is a reserved
 * type name, so it can never be a value here. */
static bool current_starts_complex_type(Parser *parser) {
    if (current_token_is(parser, TOKEN_CARET) || current_token_is(parser, TOKEN_LEFT_BRACKET)) return true;
    return current_token_is(parser, TOKEN_IDENTIFIER) &&
           strcmp(parser->current_token.literal, "map") == 0 &&
           peek_token_is(parser, TOKEN_LEFT_BRACKET);
}

/* Parse a complex type annotation.
 * Precondition: parser is ON the first token of the type ([, ^, map, or IDENT).
 * Postcondition: returns the type string, parser on the last token of the type.
 * Returns NULL on parse error (diagnostic already emitted). */
static const char *parse_complex_type(Parser *parser) {
    if (current_token_is(parser, TOKEN_QUESTION)) {
        /* Bare wildcard type: ? */
        return "?";
    }
    if (current_token_is(parser, TOKEN_LEFT_BRACKET)) {
        /* Array type: [i64], [i64,3], [[i64]], [[[i64]]], etc. */
        next_token(parser); /* element type or nested [ */
        if (current_token_is(parser, TOKEN_LEFT_BRACKET)) {
            /* Nested array type: count depth of brackets */
            int depth = 1;
            while (current_token_is(parser, TOKEN_LEFT_BRACKET)) {
                depth++;
                if (depth > 64) {
                    diagnostic_error_message(parser->diagnostics, "E2001",
                        arena_copy_string(parser->arena,"type nesting is too deep; maximum depth is 64"),
                        parser->file, parser->current_token.line, parser->current_token.column, 0);
                    return NULL;
                }
                next_token(parser);
            }
            const char *inner = read_type_name(parser);
            if (peek_token_is(parser, TOKEN_COLON)) {
                /* Last bracket was map shorthand: [[K:V]] = [map[K:V]] */
                depth--;
                next_token(parser); /* skip : */
                next_token(parser); /* value type */
                const char *value_type = parse_complex_type(parser);
                if (!value_type) return NULL;
                if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
                size_t key_length = strlen(inner), value_length = strlen(value_type);
                size_t map_type_length = key_length + value_length + 7;
                char *map_type_name = arena_allocate(parser->arena, map_type_length);
                snprintf(map_type_name, map_type_length, "map[%s:%s]", inner, value_type);
                inner = map_type_name;
            }
            for (int level = 0; level < depth; level++) {
                if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
            }
            size_t type_name_length = strlen(inner) + (size_t)depth * 2 + 1;
            char *built_type_name = arena_allocate(parser->arena, type_name_length);
            int position = 0;
            for (int level = 0; level < depth; level++) built_type_name[position++] = '[';
            memcpy(built_type_name + position, inner, strlen(inner));
            position += (int)strlen(inner);
            for (int level = 0; level < depth; level++) built_type_name[position++] = ']';
            built_type_name[position] = '\0';
            return built_type_name;
        } else if (current_token_is(parser, TOKEN_CARET)) {
            /* Array of pointers: [^Type] */
            next_token(parser); /* skip ^ to type name */
            const char *pointee = read_type_name(parser);
            if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
            size_t type_name_length = strlen(pointee) + 4;
            char *built_type_name = arena_allocate(parser->arena, type_name_length);
            snprintf(built_type_name, type_name_length, "[^%s]", pointee);
            return built_type_name;
        } else if (current_token_is(parser, TOKEN_IDENTIFIER) && strcmp(parser->current_token.literal, "map") == 0 &&
                   peek_token_is(parser, TOKEN_LEFT_BRACKET)) {
            /* Array of maps: [map[K:V]] or fixed-size [map[K:V], N] */
            const char *element_type = parse_complex_type(parser);
            if (!element_type) return NULL;
            if (peek_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip , */
                next_token(parser); /* size */
                if (!current_token_is(parser, TOKEN_INTEGER_LITERAL) && !current_token_is(parser, TOKEN_IDENTIFIER)) {
                    diagnostic_error_code(parser->diagnostics, "E2025", parser->file, parser->current_token.line, parser->current_token.column, 0);
                }
                const char *size_text = parser->current_token.literal;
                if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
                size_t element_length = strlen(element_type), size_length = strlen(size_text);
                size_t type_name_length = element_length + size_length + 4;
                char *built_type_name = arena_allocate(parser->arena, type_name_length);
                snprintf(built_type_name, type_name_length, "[%s,%s]", element_type, size_text);
                return built_type_name;
            }
            if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
            size_t type_name_length = strlen(element_type) + 3;
            char *built_type_name = arena_allocate(parser->arena, type_name_length);
            snprintf(built_type_name, type_name_length, "[%s]", element_type);
            return built_type_name;
        } else if (current_token_is(parser, TOKEN_IDENTIFIER) && strcmp(parser->current_token.literal, "func") == 0 &&
                   peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            /* Arrays of typed func signatures are not supported. */
            diagnostic_error_code(parser->diagnostics, "E2082", parser->file, parser->current_token.line, parser->current_token.column, 0);
            return NULL;
        } else {
            const char *element_type = read_type_name(parser);
            if (peek_token_is(parser, TOKEN_COLON)) {
                /* Map shorthand: [K:V] → normalized to "map[K:V]" */
                next_token(parser); /* skip : */
                next_token(parser); /* value type */
                const char *value_type = parse_complex_type(parser);
                if (!value_type) return NULL;
                if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
                size_t key_length = strlen(element_type), value_length = strlen(value_type);
                size_t type_name_length = key_length + value_length + 7;
                char *built_type_name = arena_allocate(parser->arena, type_name_length);
                snprintf(built_type_name, type_name_length, "map[%s:%s]", element_type, value_type);
                return built_type_name;
            } else if (peek_token_is(parser, TOKEN_COMMA)) {
                /* Fixed-size array: [i64, 3] or [i64, SIZE] */
                next_token(parser); /* skip , */
                next_token(parser); /* size */
                if (!current_token_is(parser, TOKEN_INTEGER_LITERAL) && !current_token_is(parser, TOKEN_IDENTIFIER)) {
                    diagnostic_error_code(parser->diagnostics, "E2025", parser->file, parser->current_token.line, parser->current_token.column, 0);
                }
                const char *size_text = parser->current_token.literal;
                if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
                size_t element_length = strlen(element_type), size_length = strlen(size_text);
                size_t type_name_length = element_length + size_length + 4;
                char *built_type_name = arena_allocate(parser->arena, type_name_length);
                snprintf(built_type_name, type_name_length, "[%s,%s]", element_type, size_text);
                return built_type_name;
            } else {
                /* Dynamic array: [i64] */
                if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
                size_t type_name_length = strlen(element_type) + 3;
                char *built_type_name = arena_allocate(parser->arena, type_name_length);
                snprintf(built_type_name, type_name_length, "[%s]", element_type);
                return built_type_name;
            }
        }
    } else if (current_token_is(parser, TOKEN_CARET)) {
        /* Pointer type: ^T; recurse to support ^^T, ^^^T, etc. */
        next_token(parser);
        const char *pointee = parse_complex_type(parser);
        if (!pointee) return NULL;
        size_t type_name_length = strlen(pointee) + 2;
        char *built_type_name = arena_allocate(parser->arena, type_name_length);
        snprintf(built_type_name, type_name_length, "^%s", pointee);
        return built_type_name;
    } else if (current_token_is(parser, TOKEN_IDENTIFIER) && strcmp(parser->current_token.literal, "map") == 0 &&
               peek_token_is(parser, TOKEN_LEFT_BRACKET)) {
        /* Map type: map[K:V]; V is parsed recursively to support nesting */
        next_token(parser); /* skip [ */
        next_token(parser); /* key type */
        /* read_type_name consumes a module-qualified key (mod.Type), matching
         * the [K:V] shorthand path; a bare token would stop at the '.'. */
        const char *key_type = read_type_name(parser);
        if (!expect_peek_token(parser, TOKEN_COLON)) return NULL;
        next_token(parser); /* value type */
        const char *value_type = parse_complex_type(parser);
        if (!value_type) return NULL;
        if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
        /* "map[" + key + ":" + val + "]" + '\0' = klen + vlen + 7 */
        size_t key_length = strlen(key_type), value_length = strlen(value_type);
        size_t type_name_length = key_length + value_length + 7;
        char *built_type_name = arena_allocate(parser->arena, type_name_length);
        snprintf(built_type_name, type_name_length, "map[%s:%s]", key_type, value_type);
        return built_type_name;
    } else if (current_token_is(parser, TOKEN_IDENTIFIER) && strcmp(parser->current_token.literal, "func") == 0) {
        /* Typed function reference: func(P1, P2, ...) [-> R | -> (R1, R2, ...)]
         * Encoded as a flat string: "func(p1,p2,...)->ret" so the existing
         * type-string plumbing can carry it. `&` on a parameter is preserved. */
        if (!peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            /* Bare 'func' without a signature — valid as an untyped func reference
             * (e.g. map[string:func], [func], struct fields).  Just return "func". */
            return "func";
        }
        next_token(parser); /* consume ( */
        /* Build the parameter list */
        char parameter_list[MESSAGE_BUFFER_LARGE_SIZE] = {0};
        size_t parameter_list_length = 0;
        if (!peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
            next_token(parser); /* first parameter */
            for (;;) {
                bool is_mutable_parameter = false;
                if (current_token_is(parser, TOKEN_AMPERSAND)) {
                    is_mutable_parameter = true;
                    next_token(parser);
                }
                const char *parameter_type = parse_complex_type(parser);
                if (!parameter_type) return NULL;
                int written_length = snprintf(parameter_list + parameter_list_length, sizeof(parameter_list) - parameter_list_length,
                    "%s%s%s", parameter_list_length ? "," : "", is_mutable_parameter ? "&" : "", parameter_type);
                if (written_length < 0 || (size_t)written_length >= sizeof(parameter_list) - parameter_list_length) return NULL;
                parameter_list_length += (size_t)written_length;
                if (!peek_token_is(parser, TOKEN_COMMA)) break;
                next_token(parser); /* , */
                next_token(parser); /* next parameter */
            }
        }
        if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
        /* Optional -> R | -> (R1, R2, ...).
         * Absence of -> means "no return value" (the canonical encoding
         * just omits the suffix; there is no user-facing 'void' type). */
        char return_list[MESSAGE_BUFFER_SIZE] = {0};
        bool has_return = false;
        if (peek_token_is(parser, TOKEN_ARROW)) {
            next_token(parser); /* -> */
            next_token(parser); /* first return type or ( */
            has_return = true;
            if (current_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                size_t return_list_length = 0;
                return_list[return_list_length++] = '(';
                next_token(parser); /* first return type */
                for (;;) {
                    const char *return_type = parse_complex_type(parser);
                    if (!return_type) return NULL;
                    if (return_type && strcmp(return_type, "void") == 0) {
                        diagnostic_error_code(parser->diagnostics, "E3068", parser->file, parser->current_token.line, parser->current_token.column, 0);
                        return NULL;
                    }
                    int written_length = snprintf(return_list + return_list_length, sizeof(return_list) - return_list_length, "%s%s",
                        return_list_length > 1 ? "," : "", return_type);
                    if (written_length < 0 || (size_t)written_length >= sizeof(return_list) - return_list_length) return NULL;
                    return_list_length += (size_t)written_length;
                    if (!peek_token_is(parser, TOKEN_COMMA)) break;
                    next_token(parser); /* , */
                    next_token(parser); /* next return type */
                }
                if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
                if (return_list_length + 2 >= sizeof(return_list)) return NULL;
                return_list[return_list_length++] = ')';
                return_list[return_list_length] = '\0';
            } else {
                const char *return_type = parse_complex_type(parser);
                if (!return_type) return NULL;
                if (strcmp(return_type, "void") == 0) {
                    diagnostic_error_code(parser->diagnostics, "E3068", parser->file, parser->current_token.line, parser->current_token.column, 0);
                    return NULL;
                }
                snprintf(return_list, sizeof(return_list), "%s", return_type);
            }
        }
        size_t type_name_length = 5 /* "func(" */ + parameter_list_length + 5 /* ")->\0" + slack */ + strlen(return_list) + 1;
        char *built_type_name = arena_allocate(parser->arena, type_name_length);
        if (has_return) {
            snprintf(built_type_name, type_name_length, "func(%s)->%s", parameter_list, return_list);
        } else {
            snprintf(built_type_name, type_name_length, "func(%s)", parameter_list);
        }
        return built_type_name;
    } else {
        /* Plain type name (possibly qualified: module.Type) */
        return read_type_name(parser);
    }
}

/* The element type of an array spelling, or NULL when the spelling is not an
 * array.  "[i64]" -> "i64", "[i64,3]" -> "i64" (the size is not part of the
 * element type).  Nesting is respected, so "[[i64,3]]" -> "[i64,3]". */
static const char *array_element_type(Parser *parser, const char *type_name) {
    if (!type_name || type_name[0] != '[') return NULL;
    size_t length = strlen(type_name);
    if (length < 3 || type_name[length - 1] != ']') return NULL;
    size_t element_length = length - 2;
    int depth = 0;
    for (size_t i = 0; i < element_length; i++) {
        char character = type_name[1 + i];
        if (character == '[') depth++;
        else if (character == ']') depth--;
        else if (character == ',' && depth == 0) { element_length = i; break; }
    }
    char *element_type = arena_allocate(parser->arena, element_length + 1);
    memcpy(element_type, type_name + 1, element_length);
    element_type[element_length] = '\0';
    return element_type;
}

static AstNode *parse_identifier(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
    node->data.label.value = parser->current_token.literal;
    return node;
}

/* The decimal spelling of the digits of a hex/octal/binary literal (base
 * prefix already stripped, '_' separators allowed), at any width. The wide
 * integer types parse their literals from decimal text only. */
static const char *radix_digits_to_decimal(Parser *parser, const char *digits, unsigned base) {
    size_t digit_count = strlen(digits);
    char *decimal_digits = arena_allocate(parser->arena, 2 * digit_count + 2); /* little-endian decimal digits */
    size_t decimal_digit_count = 1;
    decimal_digits[0] = 0;
    for (const char *cursor = digits; *cursor; cursor++) {
        if (*cursor == '_') continue;
        unsigned carry = 0;
        if (*cursor >= '0' && *cursor <= '9') carry = (unsigned)(*cursor - '0');
        else if (*cursor >= 'a' && *cursor <= 'f') carry = (unsigned)(*cursor - 'a' + 10);
        else if (*cursor >= 'A' && *cursor <= 'F') carry = (unsigned)(*cursor - 'A' + 10);
        for (size_t j = 0; j < decimal_digit_count; j++) {
            unsigned product = (unsigned)decimal_digits[j] * base + carry;
            decimal_digits[j] = (char)(product % 10);
            carry = product / 10;
        }
        while (carry) {
            decimal_digits[decimal_digit_count++] = (char)(carry % 10);
            carry /= 10;
        }
    }
    char *decimal_text = arena_allocate(parser->arena, decimal_digit_count + 1);
    for (size_t j = 0; j < decimal_digit_count; j++) decimal_text[j] = (char)('0' + decimal_digits[decimal_digit_count - 1 - j]);
    decimal_text[decimal_digit_count] = '\0';
    return decimal_text;
}

static AstNode *parse_integer_literal(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_INTEGER_LITERAL, parser->current_token);
    const char *digits = parser->current_token.literal;
    /* Accumulate as uint64_t so the full UINT64_MAX range is representable
     * and overflow detection works the same for every base. */
    uint64_t unsigned_value = 0;
    bool is_above_u64_maximum = false;

    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits += 2;
        while (*digits) {
            if (*digits == '_') { digits++; continue; }
            unsigned digit_value = 0;
            if (*digits >= '0' && *digits <= '9') digit_value = (unsigned)(*digits - '0');
            else if (*digits >= 'a' && *digits <= 'f') digit_value = (unsigned)(*digits - 'a' + 10);
            else if (*digits >= 'A' && *digits <= 'F') digit_value = (unsigned)(*digits - 'A' + 10);
            if (unsigned_value > (UINT64_MAX >> 4)) is_above_u64_maximum = true;
            unsigned_value = unsigned_value * 16 + digit_value;
            digits++;
        }
    } else if (digits[0] == '0' && (digits[1] == 'o' || digits[1] == 'O')) {
        digits += 2;
        while (*digits) {
            if (*digits == '_') { digits++; continue; }
            unsigned digit_value = (unsigned)(*digits - '0');
            if (unsigned_value > (UINT64_MAX >> 3)) is_above_u64_maximum = true;
            unsigned_value = unsigned_value * 8 + digit_value;
            digits++;
        }
    } else if (digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B')) {
        digits += 2;
        while (*digits) {
            if (*digits == '_') { digits++; continue; }
            unsigned digit_value = (unsigned)(*digits - '0');
            if (unsigned_value > (UINT64_MAX >> 1)) is_above_u64_maximum = true;
            unsigned_value = unsigned_value * 2 + digit_value;
            digits++;
        }
    } else {
        while (*digits) {
            if (*digits != '_') {
                unsigned digit_value = (unsigned)(*digits - '0');
                if (unsigned_value > (UINT64_MAX - digit_value) / 10) is_above_u64_maximum = true;
                unsigned_value = unsigned_value * 10 + digit_value;
            }
            digits++;
        }
    }

    node->data.integer_literal.value = (int64_t)unsigned_value;
    const char *literal_text = parser->current_token.literal;
    if (literal_text[0] == '0' && (literal_text[1] == 'x' || literal_text[1] == 'X'))
        literal_text = radix_digits_to_decimal(parser, literal_text + 2, 16);
    else if (literal_text[0] == '0' && (literal_text[1] == 'o' || literal_text[1] == 'O'))
        literal_text = radix_digits_to_decimal(parser, literal_text + 2, 8);
    else if (literal_text[0] == '0' && (literal_text[1] == 'b' || literal_text[1] == 'B'))
        literal_text = radix_digits_to_decimal(parser, literal_text + 2, 2);
    node->data.integer_literal.literal = literal_text;
    node->data.integer_literal.is_above_i64_maximum = is_above_u64_maximum || unsigned_value > (uint64_t)INT64_MAX;
    node->data.integer_literal.is_above_u64_maximum = is_above_u64_maximum;
    return node;
}

static AstNode *parse_floating_point_literal(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_FLOATING_POINT_LITERAL, parser->current_token);
    /* Strip underscores before parsing; atof stops at _ */
    const char *literal_text = parser->current_token.literal;
    if (strchr(literal_text, '_')) {
        char digits_without_separators[FLOATING_POINT_LITERAL_BUFFER_SIZE];
        int length = 0;
        for (int i = 0; literal_text[i] && length < (int)sizeof(digits_without_separators) - 1; i++) {
            if (literal_text[i] != '_') digits_without_separators[length++] = literal_text[i];
        }
        digits_without_separators[length] = '\0';
        node->data.floating_point_literal.value = atof(digits_without_separators);
    } else {
        node->data.floating_point_literal.value = atof(literal_text);
    }
    /* A decimal literal has no spelling for infinity, so an infinite result
     * can only mean the value saturated past DBL_MAX. Reject it here, at the
     * point of conversion: the value is already wrong by the time anything
     * downstream sees it, and codegen would render it as the bare token
     * `inf`, which is not valid C. Underflow to zero is left alone; that is
     * IEEE-conformant, not an error. */
    if (isinf(node->data.floating_point_literal.value)) {
        diagnostic_error_code(parser->diagnostics, "E3138", parser->file,
            parser->current_token.line, parser->current_token.column, 0);
        node->data.floating_point_literal.value = 0.0;
    }
    return node;
}

static bool string_has_interpolation(const char *text) {
    for (int i = 0; text[i]; i++) {
        if (text[i] == '$' && text[i + 1] == '{') return true;
        if (text[i] == '\\') i++;
    }
    return false;
}

static AstNode *parse_interpolated_string(Parser *parser, const char *raw) {
    AstNode *node = ast_allocate(parser->arena, NODE_INTERPOLATED_STRING, parser->current_token);

    int capacity = GROW_ARRAY_INITIAL_CAPACITY;
    int count = 0;
    AstNode **parts = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);

    const char *cursor = raw;
    const char *segment_start = cursor;

    while (*cursor) {
        if (*cursor == '\\' && *(cursor + 1)) {
            cursor += 2;
            continue;
        }
        if (*cursor == '$' && *(cursor + 1) == '{') {
            /* Emit the text segment before ${ */
            if (cursor > segment_start) {
                ARENA_GROW(parser->arena, parts, count, capacity);
                AstNode *text = ast_allocate(parser->arena, NODE_STRING_VALUE, parser->current_token);
                text->data.string_value.value = arena_copy_string_with_length(parser->arena, segment_start, cursor - segment_start);
                parts[count++] = text;
            }

            /* Find matching } and parse the expression inside */
            cursor += 2; /* skip ${ */
            const char *expression_start = cursor;
            int brace_depth = 1;
            while (*cursor && brace_depth > 0) {
                /* Skip nested string literals so braces inside
                 * them are not counted against brace_depth. */
                if (*cursor == '"') {
                    cursor++; /* skip opening " */
                    while (*cursor && *cursor != '"') {
                        if (*cursor == '\\' && *(cursor + 1)) cursor++;
                        cursor++;
                    }
                    if (*cursor == '"') cursor++; /* skip closing " */
                    continue;
                }
                if (*cursor == '{') brace_depth++;
                else if (*cursor == '}') brace_depth--;
                if (brace_depth > 0) cursor++;
            }

            /* Guard against unbounded interpolation expressions */
            size_t expression_length = (size_t)(cursor - expression_start);
            if (expression_length > 65536) {
                diagnostic_error_message(parser->diagnostics, "E2001",
                    arena_copy_string(parser->arena, "string interpolation expression is too large (max 64KB)"),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                return NULL;
            }

            /* Parse the expression text */
            ARENA_GROW(parser->arena, parts, count, capacity);

            char *expression_text = arena_copy_string_with_length(parser->arena, expression_start, cursor - expression_start);
            /* reject '${}' (and whitespace-only '${ }') before
             * spinning up a sub-parser. Otherwise the sub-parser hits
             * EOF on an empty input and reports E2002 with the stale
             * file-start position it was initialized to, which is
             * actively misleading. */
            bool is_empty = true;
            for (const char *character = expression_text; *character; character++) {
                if (*character != ' ' && *character != '\t' && *character != '\n' && *character != '\r') {
                    is_empty = false;
                    break;
                }
            }
            if (is_empty) {
                diagnostic_error_code(parser->diagnostics, "E2071", parser->file, parser->current_token.line, parser->current_token.column, 0);
            } else {
                Lexer *expression_lexer = lexer_create(parser->arena, expression_text, parser->file);
                /* Offset the sub-lexer to the real source position of this
                 * ${...} expression so diagnostics point at the right line
                 * and column instead of always reporting 1:N. expression_start
                 * points to the first char of the expression (past "${"),
                 * so (expression_start - raw) is its byte offset from the opening
                 * quote. Count any newlines in the string before this point
                 * to handle multi-line strings correctly. */
                {
                    int line_offset = 0;
                    int column_from_newline = 0;
                    for (const char *scan_cursor = raw; scan_cursor < expression_start; scan_cursor++) {
                        if (*scan_cursor == '\n') { line_offset++; column_from_newline = 0; }
                        else column_from_newline++;
                    }
                    expression_lexer->line = parser->current_token.line + line_offset;
                    if (line_offset > 0)
                        expression_lexer->column = column_from_newline + 1;
                    else
                        expression_lexer->column = parser->current_token.column + 1 + (int)(expression_start - raw);
                }
                Parser *expression_parser = parser_create(parser->arena, expression_lexer, parser->file, parser->diagnostics);
                expression_parser->is_in_interpolation = true;
                AstNode *expression = parse_expression(expression_parser, PRECEDENCE_LOWEST);
                /* The sub-parser must consume the whole ${...} body. parse_expression
                 * stops with current_token on the last token of the expression and
                 * peek_token on whatever follows; anything other than EOF there —
                 * a ':05d' format spec, trailing garbage — was dropped silently,
                 * so the interpolation evaluated only the leading expression.
                 * Grayscale has no format-spec syntax. */
                if (expression && expression_parser->peek_token.type != TOKEN_END_OF_FILE &&
                    expression_parser->peek_token.type != TOKEN_ILLEGAL) {
                    char message[MESSAGE_BUFFER_SIZE];
                    snprintf(message, sizeof(message), "unexpected token '%s' in interpolation expression",
                        token_display_name(expression_parser->peek_token));
                    diagnostic_error_message(parser->diagnostics, "E2002",
                        arena_copy_string(parser->arena, message), parser->file,
                        expression_parser->peek_token.line, expression_parser->peek_token.column, 0);
                }
                if (expression) parts[count++] = expression;
            }

            if (*cursor == '}') cursor++;
            segment_start = cursor;
        } else {
            cursor++;
        }
    }

    /* Remaining text segment */
    if (cursor > segment_start) {
        ARENA_GROW(parser->arena, parts, count, capacity);
        AstNode *text = ast_allocate(parser->arena, NODE_STRING_VALUE, parser->current_token);
        text->data.string_value.value = arena_copy_string_with_length(parser->arena, segment_start, cursor - segment_start);
        parts[count++] = text;
    }

    node->data.interpolated_string.parts = parts;
    node->data.interpolated_string.part_count = count;
    return node;
}

static AstNode *parse_string_literal(Parser *parser) {
    const char *raw = parser->current_token.literal;
    bool is_raw = (parser->current_token.type == TOKEN_RAW_STRING);
    if (!is_raw && string_has_interpolation(raw)) {
        return parse_interpolated_string(parser, raw);
    }
    if (!is_raw) {
        /* E2057: check for bare $identifier (missing braces) */
        for (int i = 0; raw[i]; i++) {
            if (raw[i] == '\\') { i++; continue; }
            if (raw[i] == '$' && raw[i + 1] != '{' && raw[i + 1] != '\0' &&
                (isalpha((unsigned char)raw[i + 1]) || raw[i + 1] == '_')) {
                diagnostic_error_code(parser->diagnostics, "E2057", parser->file, parser->current_token.line, parser->current_token.column, 0);
                break;
            }
        }
    }
    AstNode *node = ast_allocate(parser->arena, NODE_STRING_VALUE, parser->current_token);
    node->data.string_value.value = raw;
    node->data.string_value.is_raw = is_raw;
    return node;
}

/* Decode the text between the quotes of a char literal (already validated by
 * the lexer) into a single Unicode codepoint. Handles escape sequences and
 * UTF-8 multibyte characters. Returns false and emits a diagnostic when a
 * \u{} escape names a value outside U+0000–U+10FFFF. */
static bool decode_char_literal(Parser *parser, const char *text, int32_t *out) {
    if (text[0] == '\\') {
        switch (text[1]) {
        case 'n': *out = '\n'; return true;
        case 't': *out = '\t'; return true;
        case 'r': *out = '\r'; return true;
        case '\\': *out = '\\'; return true;
        case '\'': *out = '\''; return true;
        case '"': *out = '"'; return true;
        case '0': *out = '\0'; return true;
        case 'x': {
            /* \xNN — codepoint U+00NN, not a raw byte */
            *out = (int32_t)strtol(text + 2, NULL, 16);
            return true;
        }
        case 'u': {
            /* \u{H...} */
            long codepoint = strtol(text + 3, NULL, 16);
            if (codepoint < 0 || codepoint > 0x10FFFF) {
                diagnostic_error_message(parser->diagnostics, "E1006",
                    "'\\u{}' codepoint is outside the valid range U+0000 to U+10FFFF",
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                *out = 0;
                return false;
            }
            if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                /* UTF-16 surrogate halves are not Unicode scalar values and
                 * have no well-formed UTF-8 encoding. */
                diagnostic_error_message(parser->diagnostics, "E1006",
                    "'\\u{}' codepoint U+D800 to U+DFFF is a UTF-16 surrogate, not a character",
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                *out = 0;
                return false;
            }
            *out = (int32_t)codepoint;
            return true;
        }
        default: *out = (unsigned char)text[1]; return true;
        }
    }
    /* UTF-8 decode the first (only) codepoint. */
    unsigned char lead_byte = (unsigned char)text[0];
    if (lead_byte < 0x80) {
        *out = lead_byte;
    } else if ((lead_byte & 0xE0) == 0xC0) {
        *out = ((lead_byte & 0x1F) << 6) | ((unsigned char)text[1] & 0x3F);
    } else if ((lead_byte & 0xF0) == 0xE0) {
        *out = ((lead_byte & 0x0F) << 12) | (((unsigned char)text[1] & 0x3F) << 6) |
               ((unsigned char)text[2] & 0x3F);
    } else {
        *out = ((lead_byte & 0x07) << 18) | (((unsigned char)text[1] & 0x3F) << 12) |
               (((unsigned char)text[2] & 0x3F) << 6) | ((unsigned char)text[3] & 0x3F);
    }
    return true;
}

static AstNode *parse_bool_literal(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_BOOL_VALUE, parser->current_token);
    node->data.bool_value.value = (parser->current_token.type == TOKEN_TRUE);
    return node;
}

static AstNode *parse_nil_literal(Parser *parser) {
    return ast_allocate(parser->arena, NODE_NIL_VALUE, parser->current_token);
}

static AstNode *parse_prefix_expression(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_PREFIX_EXPRESSION, parser->current_token);
    node->data.prefix.operator = parser->current_token.type;
    next_token(parser);
    node->data.prefix.right = parse_expression(parser, PRECEDENCE_PREFIX);
    return node;
}

static AstNode *parse_grouped_expression(Parser *parser) {
    /* Check for function reference: ()func_name or ()Type.func */
    if (peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
        Token reference_token = parser->current_token;
        next_token(parser); /* consume ) */
        next_token(parser); /* move to identifier */

        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            return NULL;
        }

        /* Parse the function name; may be qualified with dots */
        AstNode *function_expression = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
        function_expression->data.label.value = parser->current_token.literal;

        while (peek_token_is(parser, TOKEN_DOT)) {
            next_token(parser); /* consume . */
            next_token(parser); /* move to member */
            AstNode *member = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, parser->current_token);
            member->data.member.object = function_expression;
            member->data.member.member = parser->current_token.literal;
            function_expression = member;
        }

        AstNode *reference = ast_allocate(parser->arena, NODE_FUNCTION_REFERENCE, reference_token);
        reference->data.function_reference.function = function_expression;
        return reference;
    }

    next_token(parser);
    AstNode *expression = parse_expression(parser, PRECEDENCE_LOWEST);
    if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
    return expression;
}

/* Parse prefix expression (the "nud" in Pratt parsing) */
static AstNode *parse_prefix(Parser *parser) {
    switch (parser->current_token.type) {
    case TOKEN_EXTERN:
    case TOKEN_IDENTIFIER:
        /* Check for module-qualified struct literal: mod.Name{ ... } */
        if (peek_token_is(parser, TOKEN_DOT)) {
            const char *module_name = parser->current_token.literal;
            if (module_name[0] >= 'a' && module_name[0] <= 'z') {
                ParserSnapshot snapshot;
                parser_snapshot_save(parser, &snapshot);

                next_token(parser); /* consume . */
                next_token(parser); /* move to potential type name */

                if (parser->current_token.type == TOKEN_IDENTIFIER &&
                    peek_token_is(parser, TOKEN_LEFT_BRACE) && !parser->should_suppress_struct_literal) {
                    /* mod.Name{; module-qualified struct literal */
                    char *prefixed = arena_allocate(parser->arena, MESSAGE_BUFFER_SIZE);
                    snprintf(prefixed, MESSAGE_BUFFER_SIZE, "%s.%s", module_name, parser->current_token.literal);
                    next_token(parser); /* move to { */
                    return parse_struct_literal(parser, prefixed);
                }

                parser_snapshot_restore(parser, &snapshot); /* not a struct literal */
            }
        }
        /* Check for struct literal: Name{ ... }
         * The name's casing has nothing to do with it. Requiring an initial
         * capital made every lowercase-named struct unusable: the literal
         * parsed as a bare label and the type name was then reported as a
         * value. should_suppress_struct_literal is what keeps `if x {` from being read as
         * one; the spelling of the name is not. */
        if (peek_token_is(parser, TOKEN_LEFT_BRACE) && !parser->should_suppress_struct_literal) {
            const char *name = parser->current_token.literal;
            next_token(parser); /* move to { */
            return parse_struct_literal(parser, name);
        }
        return parse_identifier(parser);
    case TOKEN_INTEGER_LITERAL:       return parse_integer_literal(parser);
    case TOKEN_FLOATING_POINT_LITERAL:     return parse_floating_point_literal(parser);
    case TOKEN_STRING:
    case TOKEN_RAW_STRING: return parse_string_literal(parser);
    case TOKEN_TRUE:
    case TOKEN_FALSE:     return parse_bool_literal(parser);
    case TOKEN_NIL:       return parse_nil_literal(parser);
    case TOKEN_CHAR: {
        AstNode *node = ast_allocate(parser->arena, NODE_CHAR_VALUE, parser->current_token);
        int32_t codepoint = 0;
        decode_char_literal(parser, parser->current_token.literal, &codepoint);
        node->data.char_value.value = codepoint;
        return node;
    }
    case TOKEN_MINUS:
    case TOKEN_BANG:
    case TOKEN_BIT_NOT: return parse_prefix_expression(parser);
    case TOKEN_AMPERSAND: {
        diagnostic_error_code(parser->diagnostics, "E2072",
            parser->file, parser->current_token.line, parser->current_token.column, 0);
        return parse_prefix_expression(parser);
    }
    case TOKEN_DOT: {
        /* .VARIANT — implicit enum selector (resolved by typechecker) */
        Token dot_token = parser->current_token;
        next_token(parser); /* consume dot */
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            char message[256];
            snprintf(message, sizeof(message), "expected enum variant name after '.'");
            diagnostic_error_message(parser->diagnostics, "E2001", arena_copy_string(parser->arena, message),
                parser->file, dot_token.line, dot_token.column, 0);
            return ast_allocate(parser->arena, NODE_NIL_VALUE, dot_token);
        }
        AstNode *node = ast_allocate(parser->arena, NODE_IMPLICIT_ENUM, dot_token);
        node->data.implicit_enum.variant = arena_copy_string(parser->arena, parser->current_token.literal);
        node->data.implicit_enum.resolved_enum = NULL;
        return node;
    }
    case TOKEN_LEFT_PARENTHESIS:    return parse_grouped_expression(parser);
    case TOKEN_LEFT_BRACE: {
        /* Could be array literal {1, 2, 3} or map literal {"k": v, ...}
         * Detect map by checking for colon after first expression */
        Token brace_token = parser->current_token;
        next_token(parser); /* skip { */

        /* Empty: {}; treat as empty array (context-dependent) */
        if (current_token_is(parser, TOKEN_RIGHT_BRACE)) {
            AstNode *node = ast_allocate(parser->arena, NODE_ARRAY_VALUE, brace_token);
            node->data.array_value.count = 0;
            node->data.array_value.elements = NULL;
            return node;
        }

        /* Empty map: {:} */
        if (current_token_is(parser, TOKEN_COLON) && peek_token_is(parser, TOKEN_RIGHT_BRACE)) {
            next_token(parser); /* skip } */
            AstNode *node = ast_allocate(parser->arena, NODE_MAP_VALUE, brace_token);
            node->data.map_value.count = 0;
            node->data.map_value.keys = NULL;
            node->data.map_value.values = NULL;
            return node;
        }

        /* Leading comma: {, 1, 2} — reuse the trailing-comma diagnostic. */
        if (current_token_is(parser, TOKEN_COMMA)) {
            diagnostic_error_code(parser->diagnostics, "E2017",
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            next_token(parser); /* skip the stray , and keep parsing */
        }

        /* Parse first expression */
        AstNode *first = parse_expression(parser, PRECEDENCE_LOWEST);

        if (peek_token_is(parser, TOKEN_COLON)) {
            /* Map literal: {"key": value, ...} */
            AstNode *node = ast_allocate(parser->arena, NODE_MAP_VALUE, brace_token);
            int capacity = GROW_ARRAY_INITIAL_CAPACITY;
            node->data.map_value.count = 0;
            node->data.map_value.keys = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);
            node->data.map_value.values = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);

            /* First key already parsed */
            node->data.map_value.keys[0] = first;
            next_token(parser); /* skip : */
            next_token(parser);
            node->data.map_value.values[0] = parse_expression(parser, PRECEDENCE_LOWEST);
            node->data.map_value.count = 1;

            while (peek_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip , */
                next_token(parser);
                if (current_token_is(parser, TOKEN_RIGHT_BRACE)) break;
                if (node->data.map_value.count >= capacity) {
                    capacity = GROW_NEXT_CAPACITY(capacity);
                    ARENA_GROW_TO(parser->arena, node->data.map_value.keys,
                        node->data.map_value.count, capacity);
                    ARENA_GROW_TO(parser->arena, node->data.map_value.values,
                        node->data.map_value.count, capacity);
                }
                node->data.map_value.keys[node->data.map_value.count] =
                    parse_expression(parser, PRECEDENCE_LOWEST);
                if (!expect_peek_token(parser, TOKEN_COLON)) return NULL;
                next_token(parser);
                node->data.map_value.values[node->data.map_value.count] =
                    parse_expression(parser, PRECEDENCE_LOWEST);
                node->data.map_value.count++;
            }
            if (peek_token_is(parser, TOKEN_RIGHT_BRACE)) next_token(parser);
            return node;
        }

        /* Array literal: {expr, expr, ...} */
        AstNode *node = ast_allocate(parser->arena, NODE_ARRAY_VALUE, brace_token);
        int capacity = GROW_ARRAY_INITIAL_CAPACITY;
        node->data.array_value.count = 0;
        node->data.array_value.elements = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);
        node->data.array_value.elements[node->data.array_value.count++] = first;

        while (peek_token_is(parser, TOKEN_COMMA)) {
            next_token(parser); /* skip , */
            next_token(parser);
            if (current_token_is(parser, TOKEN_RIGHT_BRACE)) {
                diagnostic_error_code(parser->diagnostics, "E2017",
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                break;
            }
            ARENA_GROW(parser->arena, node->data.array_value.elements,
                node->data.array_value.count, capacity);
            node->data.array_value.elements[node->data.array_value.count++] =
                parse_expression(parser, PRECEDENCE_LOWEST);
        }
        if (peek_token_is(parser, TOKEN_RIGHT_BRACE)) next_token(parser);
        return node;
    }
    case TOKEN_CAST: {
        /* cast(value, type) */
        AstNode *node = ast_allocate(parser->arena, NODE_CAST_EXPRESSION, parser->current_token);
        node->data.cast.is_array = false;
        node->data.cast.element_type = NULL;
        if (!expect_peek_token(parser, TOKEN_LEFT_PARENTHESIS)) return NULL;
        next_token(parser);
        node->data.cast.value = parse_expression(parser, PRECEDENCE_LOWEST);
        if (!expect_peek_token(parser, TOKEN_COMMA)) return NULL;
        next_token(parser);
        /* The target is an ordinary type annotation, so it goes through the
         * shared type parser rather than a hand-rolled subset. */
        const char *target = parse_complex_type(parser);
        if (!target) return NULL;
        node->data.cast.target_type = target;
        const char *element_type = array_element_type(parser, target);
        if (element_type) {
            node->data.cast.is_array = true;
            node->data.cast.element_type = element_type;
        }
        if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
        return node;
    }
    case TOKEN_NEW: {
        /* new(Type); allocate zeroed value on default arena */
        AstNode *node = ast_allocate(parser->arena, NODE_NEW_EXPRESSION, parser->current_token);
        if (!expect_peek_token(parser, TOKEN_LEFT_PARENTHESIS)) return NULL;
        next_token(parser);
        node->data.new_expression.type_name = parse_complex_type(parser);
        if (type_string_has_wildcard(node->data.new_expression.type_name)) {
            diagnostic_error_message(parser->diagnostics, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' cannot be used with 'new()'; 'new()' requires a concrete type"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
        }
        if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
        return node;
    }
    case TOKEN_RANGE: {
        /* range(end) or range(start, end) or range(start, end, step) */
        AstNode *node = ast_allocate(parser->arena, NODE_RANGE_EXPRESSION, parser->current_token);
        node->data.range_expression.start = NULL;
        node->data.range_expression.end = NULL;
        node->data.range_expression.step = NULL;
        if (!expect_peek_token(parser, TOKEN_LEFT_PARENTHESIS)) return NULL;
        next_token(parser);
        AstNode *first = parse_expression(parser, PRECEDENCE_LOWEST);
        if (peek_token_is(parser, TOKEN_COMMA)) {
            node->data.range_expression.start = first;
            next_token(parser); /* skip comma */
            next_token(parser);
            node->data.range_expression.end = parse_expression(parser, PRECEDENCE_LOWEST);
            if (peek_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip comma */
                next_token(parser);
                node->data.range_expression.step = parse_expression(parser, PRECEDENCE_LOWEST);
            }
        } else {
            /* range(end) - start defaults to 0 */
            node->data.range_expression.end = first;
        }
        if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
        return node;
    }
    case TOKEN_IN:
    case TOKEN_NOT_IN: {
        /* 'in'/'not_in'/'!in' used without a left-hand value */
        Token bad_token = parser->current_token;
        const char *operator_text = bad_token.literal;
        char message[MESSAGE_BUFFER_SIZE];
        snprintf(message, sizeof(message),
            "'%s' requires a value on the left side; '%s' checks whether a value belongs to a collection or range",
            operator_text, operator_text);
        diagnostic_error_message(parser->diagnostics, "E2086", arena_copy_string(parser->arena, message),
            parser->file, bad_token.line, bad_token.column, 0);
        /* Consume the operator and its right-hand operand so subsequent tokens
         * (like the if-body '{') are seen in the right context. */
        next_token(parser);
        parser->should_suppress_struct_literal = true;
        parse_expression(parser, PRECEDENCE_LOWEST);
        parser->should_suppress_struct_literal = false;
        /* Return a dummy bool so the condition slot is non-NULL and the
         * typechecker does not add a second spurious diagnostic. */
        AstNode *dummy = ast_allocate(parser->arena, NODE_BOOL_VALUE, bad_token);
        dummy->data.bool_value.value = true;
        return dummy;
    }
    default:
    {
        /* Skip generic error for ILLEGAL tokens; the lexer already emitted a specific diagnostic */
        if (parser->current_token.type != TOKEN_ILLEGAL) {
            char message[MESSAGE_BUFFER_SIZE];
            if (parser->current_token.type == TOKEN_END_OF_FILE && parser->is_in_interpolation)
                snprintf(message, sizeof(message), "unexpected end of interpolation expression");
            else
                snprintf(message, sizeof(message), "unexpected token '%s'",
                    token_display_name(parser->current_token));
            diagnostic_error_message(parser->diagnostics, "E2002", arena_copy_string(parser->arena, message),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
        }
    }
        return NULL;
    }
}

static AstNode *parse_infix_expression(Parser *parser, AstNode *left) {
    AstNode *node = ast_allocate(parser->arena, NODE_INFIX_EXPRESSION, parser->current_token);
    node->data.infix.left = left;
    node->data.infix.operator = parser->current_token.type;
    Precedence precedence = get_token_precedence(parser->current_token.type);
    bool is_membership = (parser->current_token.type == TOKEN_IN || parser->current_token.type == TOKEN_NOT_IN);
    /* Suppress struct-literal parsing on the right side of comparisons and
     * membership operators.  In `if x < Foo {`, the `{` is the
     * if-block, not the start of a struct literal `Foo{ ... }`. */
    bool should_suppress_struct_literal_on_right = is_membership ||
        parser->current_token.type == TOKEN_LESS_THAN || parser->current_token.type == TOKEN_GREATER_THAN ||
        parser->current_token.type == TOKEN_LESS_THAN_OR_EQUAL || parser->current_token.type == TOKEN_GREATER_THAN_OR_EQUAL ||
        parser->current_token.type == TOKEN_EQUAL || parser->current_token.type == TOKEN_NOT_EQUAL;
    next_token(parser);
    /* Restore what was there rather than clearing: a comparison inside an
     * already-suppressed context — `if a == 1 && b {` — would otherwise hand
     * the rest of the condition back an enabled flag, and `b {` would parse
     * as a struct literal. */
    bool saved_should_suppress_struct_literal = parser->should_suppress_struct_literal;
    if (should_suppress_struct_literal_on_right) parser->should_suppress_struct_literal = true;
    node->data.infix.right = parse_expression(parser, precedence);
    parser->should_suppress_struct_literal = saved_should_suppress_struct_literal;
    return node;
}

static AstNode *parse_call_expression(Parser *parser, AstNode *function) {
    if (parser->current_token.is_preceded_by_whitespace) {
        diagnostic_error_code(parser->diagnostics, "E2073", parser->file, parser->current_token.line, parser->current_token.column, 0);
        /* Still parse the argument list so we consume the closing ')'
         * and don't cascade into E2001/E2002 on the unrelated tokens. */
    }
    AstNode *node = ast_allocate(parser->arena, NODE_CALL_EXPRESSION, parser->current_token);
    node->data.call.function = function;

    /* Parse arguments with named-argument detection.
     * Named arguments use the syntax  name: value  at the call site.
     * Detection: current token is TOKEN_IDENTIFIER and peek is TOKEN_COLON. */
    int count = 0;
    int capacity = GROW_ARRAY_INITIAL_CAPACITY;
    AstNode **arguments = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);
    const char **names = arena_allocate(parser->arena, sizeof(const char *) * capacity);
    memset(names, 0, sizeof(const char *) * capacity);
    bool has_named_argument = false;

    if (peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
        next_token(parser);
    } else {
        for (;;) {
            next_token(parser);
            if (count >= capacity) {
                capacity = GROW_NEXT_CAPACITY(capacity);
                ARENA_GROW_TO(parser->arena, arguments, count, capacity);
                ARENA_GROW_TO(parser->arena, names, count, capacity);
                /* Unset names must read back as NULL, and the arena does not zero. */
                memset(names + count, 0, sizeof(const char *) * (size_t)(capacity - count));
            }

            /* Check for named argument: identifier followed by colon */
            if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_COLON)) {
                names[count] = arena_copy_string(parser->arena, parser->current_token.literal);
                has_named_argument = true;
                next_token(parser); /* skip identifier */
                next_token(parser); /* skip colon, now on value */
            }

            /* size_of(^T), size_of([T]), size_of(map[K:V]): parse the container
             * spelling as a type expression, not as a general expression. */
            if (function->kind == NODE_LABEL &&
                strcmp(function->data.label.value, "size_of") == 0 &&
                current_starts_complex_type(parser)) {
                const char *type_name = parse_complex_type(parser);
                AstNode *label = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
                label->data.label.value = type_name;
                arguments[count] = label;
            } else {
                arguments[count] = parse_expression(parser, PRECEDENCE_LOWEST);
            }
            count++;

            if (!peek_token_is(parser, TOKEN_COMMA)) break;
            next_token(parser); /* skip comma */
        }
        if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
    }

    node->data.call.arguments = arguments;
    node->data.call.argument_count = count;
    node->data.call.argument_names = has_named_argument ? names : NULL;
    return node;
}

static AstNode *parse_member_expression(Parser *parser, AstNode *object) {
    if (parser->current_token.is_preceded_by_whitespace) {
        diagnostic_error_code(parser->diagnostics, "E2074", parser->file, parser->current_token.line, parser->current_token.column, 0);
        /* Continue parsing the member so we swallow the identifier after '.'
         * and don't cascade into unrelated diagnostics. */
    }
    next_token(parser); /* skip the identifier after dot */
    AstNode *node = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, parser->current_token);
    node->data.member.object = object;
    node->data.member.member = parser->current_token.literal;
    return node;
}

static AstNode *parse_index_expression(Parser *parser, AstNode *left) {
    if (parser->current_token.is_preceded_by_whitespace) {
        diagnostic_error_code(parser->diagnostics, "E2075", parser->file, parser->current_token.line, parser->current_token.column, 0);
        /* Continue parsing so we consume the closing ']'. */
    }
    /* Reject 'arr[]' before descending into a sub-parse: with the current token on ']' there
     * is no prefix parser (E2002) and expect_peek then trips on the following
     * token (E2001) — two misleading errors for one mistake. Mirror E2071. */
    if (peek_token_is(parser, TOKEN_RIGHT_BRACKET)) {
        diagnostic_error_code(parser->diagnostics, "E2077", parser->file, parser->current_token.line, parser->current_token.column, 0);
        next_token(parser); /* consume '[', leaving the current token on ']' for resync */
        return NULL;
    }
    AstNode *node = ast_allocate(parser->arena, NODE_INDEX_EXPRESSION, parser->current_token);
    node->data.index_expression.left = left;
    next_token(parser);
    node->data.index_expression.index = parse_expression(parser, PRECEDENCE_LOWEST);
    if (!expect_peek_token(parser, TOKEN_RIGHT_BRACKET)) return NULL;
    return node;
}

static AstNode *parse_postfix_expression(Parser *parser, AstNode *left) {
    if (parser->current_token.is_preceded_by_whitespace) {
        diagnostic_error_code(parser->diagnostics, "E2076", parser->file, parser->current_token.line, parser->current_token.column, 0);
    }
    AstNode *node = ast_allocate(parser->arena, NODE_POSTFIX_EXPRESSION, parser->current_token);
    node->data.postfix.left = left;
    node->data.postfix.operator = parser->current_token.type;
    return node;
}

/* Parse infix expression (the "led" in Pratt parsing) */
static AstNode *parse_infix(Parser *parser, AstNode *left) {
    switch (parser->current_token.type) {
    case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_ASTERISK: case TOKEN_SLASH:
    case TOKEN_PERCENT:
    case TOKEN_EQUAL: case TOKEN_NOT_EQUAL: case TOKEN_LESS_THAN: case TOKEN_GREATER_THAN:
    case TOKEN_LESS_THAN_OR_EQUAL: case TOKEN_GREATER_THAN_OR_EQUAL:
    case TOKEN_AND: case TOKEN_OR:
    case TOKEN_IN: case TOKEN_NOT_IN:
    case TOKEN_BIT_AND: case TOKEN_BIT_OR: case TOKEN_BIT_XOR:
    case TOKEN_BIT_SHIFT_LEFT: case TOKEN_BIT_SHIFT_RIGHT:
        return parse_infix_expression(parser, left);
    case TOKEN_LEFT_PARENTHESIS:
        return parse_call_expression(parser, left);
    case TOKEN_DOT:
        return parse_member_expression(parser, left);
    case TOKEN_LEFT_BRACKET:
        return parse_index_expression(parser, left);
    case TOKEN_INCREMENT:
    case TOKEN_DECREMENT:
    case TOKEN_CARET:
        return parse_postfix_expression(parser, left);
    default:
        return left;
    }
}

static AstNode *parse_expression(Parser *parser, Precedence precedence) {
    parser->depth++;
    if (parser->depth > MAX_PARSE_DEPTH) {
        diagnostic_error_message(parser->diagnostics, "E2001",
            arena_copy_string(parser->arena,"expression is nested too deeply; maximum depth is 256"),
            parser->file, parser->current_token.line, parser->current_token.column, 0);
        parser->depth--;
        return NULL;
    }

    AstNode *left = parse_prefix(parser);
    if (!left) { parser->depth--; return NULL; }

    while (!peek_token_is(parser, TOKEN_END_OF_FILE) && precedence < get_token_precedence(parser->peek_token.type)) {
        next_token(parser);
        left = parse_infix(parser, left);
        if (!left) { parser->depth--; return NULL; }
    }

    parser->depth--;
    return left;
}

/* --- Statement Parsing --- */

/* One monotonically increasing id for every or_return temp, so the
 * `mut a = ... or_return`, `mut a, b = ... or_return`, and bare-statement
 * spellings never collide on `_gray_orN` within a single function. */
static int gray_or_return_temporary_id = 0;
static char *make_or_return_temporary_name(Arena *arena) {
    char *name = arena_allocate(arena, TEMPORARY_NAME_BUFFER_SIZE);
    snprintf(name, TEMPORARY_NAME_BUFFER_SIZE, GRAY_SYNTHETIC_OR "%d", gray_or_return_temporary_id++);
    return name;
}

/* `mut <temporary_name> = value`, marked synthetic, with the type inferred. */
static AstNode *make_synthetic_temporary_declaration(Parser *parser, char *temporary_name, AstNode *value) {
    AstNode *declaration = ast_allocate(parser->arena, NODE_VARIABLE_DECLARATION, parser->current_token);
    declaration->data.variable_declaration.is_mutable = true;
    declaration->data.variable_declaration.name = temporary_name;
    declaration->data.variable_declaration.is_synthetic = true;
    declaration->data.variable_declaration.value = value;
    return declaration;
}

/* After the or_return token has been consumed (parser->current_token IS
 * or_return), parse optional comma-separated fallback expressions written
 * on the same line — e.g. `... or_return -1, -2`. Returns the count and
 * fills fallback_values, which the caller sizes at MAX_MULTI_VARIABLES. */
static int parse_or_return_fallbacks(Parser *parser, AstNode **fallback_values) {
    int or_return_line = parser->current_token.line;
    int fallback_count = 0;
    if (parser->peek_token.type != TOKEN_END_OF_FILE &&
        parser->peek_token.type != TOKEN_SEMICOLON &&
        parser->peek_token.type != TOKEN_RIGHT_BRACE &&
        parser->peek_token.line == or_return_line) {
        next_token(parser); /* advance to first fallback token */
        while (fallback_count < MAX_MULTI_VARIABLES) {
            fallback_values[fallback_count++] = parse_expression(parser, PRECEDENCE_LOWEST);
            if (!peek_token_is(parser, TOKEN_COMMA)) break;
            next_token(parser); /* skip comma */
            next_token(parser); /* advance to next fallback token */
        }
    }
    return fallback_count;
}

/* Build the propagation guard for or_return:
 *
 *     if (_tmp.verr != nil) { return <fallbacks...>, _tmp.verr }
 *
 * `verr` is a sentinel member the typechecker rewrites to the concrete
 * trailing-Error slot (`v1` for `(T, Error)`, `vN` for a wider tuple) once
 * the unwrapped call's arity is known. With no fallbacks the return
 * propagates just the error and codegen fills {0} for the other slots. */
static AstNode *build_or_return_guard(Parser *parser, const char *temporary_name,
                                      AstNode **fallbacks, int fallback_count) {
    Token token = parser->current_token;
    const char *error_field = OR_RETURN_ERROR_SLOT;

    AstNode *if_statement = ast_allocate(parser->arena, NODE_IF_STATEMENT, token);
    AstNode *error_access = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, token);
    AstNode *temporary_label = ast_allocate(parser->arena, NODE_LABEL, token);
    temporary_label->data.label.value = temporary_name;
    error_access->data.member.object = temporary_label;
    error_access->data.member.member = error_field;
    AstNode *nil_value = ast_allocate(parser->arena, NODE_NIL_VALUE, token);
    AstNode *condition = ast_allocate(parser->arena, NODE_INFIX_EXPRESSION, token);
    condition->data.infix.left = error_access;
    condition->data.infix.operator = TOKEN_NOT_EQUAL;
    condition->data.infix.right = nil_value;
    if_statement->data.if_statement.condition = condition;

    AstNode *return_block = ast_allocate(parser->arena, NODE_BLOCK_STATEMENT, token);
    return_block->data.block.capacity = 1;
    return_block->data.block.count = 0;
    return_block->data.block.statements = arena_allocate(parser->arena, sizeof(AstNode *));
    AstNode *return_statement = ast_allocate(parser->arena, NODE_RETURN_STATEMENT, token);
    AstNode *propagated_error_access = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, token);
    AstNode *propagated_temporary_label = ast_allocate(parser->arena, NODE_LABEL, token);
    propagated_temporary_label->data.label.value = temporary_name;
    propagated_error_access->data.member.object = propagated_temporary_label;
    propagated_error_access->data.member.member = error_field;
    if (fallback_count > 0) {
        /* If the user provided enough values to cover all return slots
         * (including the error), use them as-is; otherwise append the
         * propagated error. */
        int function_return_count = parser->current_function ? parser->current_function->data.function_declaration.return_type_count : 0;
        bool user_covers_error = (function_return_count > 0 && fallback_count >= function_return_count);
        int total = user_covers_error ? fallback_count : fallback_count + 1;
        return_statement->data.return_statement.values = arena_allocate(parser->arena, sizeof(AstNode *) * total);
        for (int i = 0; i < fallback_count; i++)
            return_statement->data.return_statement.values[i] = fallbacks[i];
        if (!user_covers_error)
            return_statement->data.return_statement.values[fallback_count] = propagated_error_access;
        return_statement->data.return_statement.count = total;
    } else {
        return_statement->data.return_statement.values = arena_allocate(parser->arena, sizeof(AstNode *));
        return_statement->data.return_statement.values[0] = propagated_error_access;
        return_statement->data.return_statement.count = 1;
    }
    return_block->data.block.statements[return_block->data.block.count++] = return_statement;
    if_statement->data.if_statement.consequence = return_block;
    if_statement->data.if_statement.alternative = NULL;
    return if_statement;
}

/* If peek is or_return, consume it and desugar
 *
 *     <declaration with value = expr> or_return
 *
 * into a block:
 *
 *     mut _tmp = expr
 *     if _tmp.v1 != nil { return _tmp.v1 }
 *     <declaration with value = _tmp.v0>
 *
 * Returns the desugared block, or NULL if no or_return was present
 * (caller keeps the original declaration untouched). */
static AstNode *maybe_apply_or_return(Parser *parser, AstNode *original_declaration) {
    if (!peek_token_is(parser, TOKEN_OR_RETURN)) return NULL;
    next_token(parser); /* consume or_return */

    AstNode *fallback_values[MAX_MULTI_VARIABLES];
    int fallback_count = parse_or_return_fallbacks(parser, fallback_values);

    char *temporary_name = make_or_return_temporary_name(parser->arena);

    AstNode *block = ast_allocate(parser->arena, NODE_BLOCK_STATEMENT, parser->current_token);
    block->data.block.capacity = 3;
    block->data.block.count = 0;
    block->data.block.statements = arena_allocate(parser->arena, sizeof(AstNode *) * block->data.block.capacity);

    /* _tmp = expr */
    block->data.block.statements[block->data.block.count++] =
        make_synthetic_temporary_declaration(parser, temporary_name, original_declaration->data.variable_declaration.value);

    block->data.block.statements[block->data.block.count++] =
        build_or_return_guard(parser, temporary_name, fallback_values, fallback_count);

    /* x = _tmp.v0 */
    AstNode *unwrapped_declaration = ast_allocate(parser->arena, NODE_VARIABLE_DECLARATION, parser->current_token);
    unwrapped_declaration->data.variable_declaration.is_mutable = original_declaration->data.variable_declaration.is_mutable;
    unwrapped_declaration->data.variable_declaration.name = original_declaration->data.variable_declaration.name;
    unwrapped_declaration->data.variable_declaration.type_name = original_declaration->data.variable_declaration.type_name;
    AstNode *value_access = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, parser->current_token);
    AstNode *value_temporary_label = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
    value_temporary_label->data.label.value = temporary_name;
    value_access->data.member.object = value_temporary_label;
    value_access->data.member.member = "v0";
    unwrapped_declaration->data.variable_declaration.value = value_access;
    block->data.block.statements[block->data.block.count++] = unwrapped_declaration;

    return block;
}

/* Bare throwaway: `_ = expr` (no mut/const keyword) at statement position.
 * Desugars to a declaration(name="_", mutable=true), which the typechecker
 * and codegen already special-case to skip symbol creation and emit
 * `(void)(expr);`. or_return is supported via the shared helper. */
/* E5012: the throwaway '_' is only meaningful when the right side is a
 * function call. Literal/identifier/arithmetic right sides have no return
 * value to discard and no side effect to run, so `_ = 32` etc. are
 * dead code with a misleading name. Checked at parse time (before
 * or_return desugaring) so the user-written right side, not the rewritten
 * member-access, is what gets validated. */
static void check_discard_target(Parser *parser, AstNode *value) {
    if (!value || value->kind == NODE_CALL_EXPRESSION) return;
    diagnostic_error_code(parser->diagnostics, "E5012",
        parser->file, value->token.line, value->token.column, 0);
}

static AstNode *parse_discard_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_VARIABLE_DECLARATION, parser->current_token);
    node->data.variable_declaration.is_mutable = true;
    node->data.variable_declaration.name = "_";
    node->data.variable_declaration.type_name = NULL;
    node->data.variable_declaration.value = NULL;

    next_token(parser); /* consume = */
    next_token(parser); /* move to the right side */
    node->data.variable_declaration.value = parse_expression(parser, PRECEDENCE_LOWEST);
    if (!node->data.variable_declaration.value) return NULL;

    check_discard_target(parser, node->data.variable_declaration.value);

    AstNode *desugared = maybe_apply_or_return(parser, node);
    if (desugared) return desugared;
    return node;
}

static AstNode *parse_variable_declaration_common(Parser *parser, bool is_bare) {
    AstNode *node = ast_allocate(parser->arena, NODE_VARIABLE_DECLARATION, parser->current_token);

    if (is_bare) {
        node->data.variable_declaration.is_mutable = true;
        /* current_token is already the variable name — don't advance */
    } else {
        node->data.variable_declaration.is_mutable = (parser->current_token.type == TOKEN_MUT);

        if (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_BLANK)) {
            next_token(parser);
        } else {
            if (!reject_keyword_as_name(parser, &parser->peek_token, "a variable name")) {
                expect_peek_token(parser, TOKEN_IDENTIFIER); /* will error */
            }
            return NULL;
        }
    }
    node->data.variable_declaration.name = parser->current_token.literal;

    /* Optional type annotation. TOKEN_QUESTION is included so a bare
     * wildcard `?` in a declaration flows through parse_complex_type and
     * lands on the existing E2070 diagnostic below; without it, the
     * token falls through to the generic "unexpected token" fallback
     * and the user gets no hint about why `?` isn't allowed here. */
    /* E2079: reject 'nil' as a type annotation. nil is a value per the
     * language, not a type; consume the token to avoid a cascading
     * "nil is an unexpected expression statement" diagnostic. */
    if (peek_token_is(parser, TOKEN_NIL)) {
        next_token(parser); /* consume nil */
        diagnostic_error_code(parser->diagnostics, "E2079",
            parser->file, parser->current_token.line, parser->current_token.column, 0);
    }

    node->data.variable_declaration.type_name = NULL;
    if (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_CARET) || peek_token_is(parser, TOKEN_LEFT_BRACKET) ||
        peek_token_is(parser, TOKEN_STRUCT) || peek_token_is(parser, TOKEN_ENUM) ||
        peek_token_is(parser, TOKEN_QUESTION)) {
        next_token(parser);
        node->data.variable_declaration.type_name = parse_complex_type(parser);
        if (!node->data.variable_declaration.type_name) return NULL;
        /* E2070: wildcard `?` only allowed in function signatures */
        if (type_string_has_wildcard(node->data.variable_declaration.type_name)) {
            diagnostic_error_message(parser->diagnostics, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' is only allowed in function parameter and return types; not in variable declarations"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
        }
        /* E2068: mut <name> struct/enum; should be const */
        if (node->data.variable_declaration.is_mutable &&
            (strcmp(node->data.variable_declaration.type_name, "struct") == 0 ||
             strcmp(node->data.variable_declaration.type_name, "enum") == 0)) {
            diagnostic_error_code_formatted(parser->diagnostics, "E2068", parser->file, node->token.line, node->token.column, 0, node->data.variable_declaration.type_name);
            return NULL;
        }
    }

    /* Blank identifier requires '=' (or ',' for multi-var destructuring).
     * Checked after the type-annotation block so `mut _ i64, ...` is allowed
     * but `mut _ foo()` is caught before the leftover tokens desync the parser. */
    if (strcmp(node->data.variable_declaration.name, "_") == 0 &&
        !peek_token_is(parser, TOKEN_ASSIGN) && !peek_token_is(parser, TOKEN_COMMA)) {
        const char *keyword = node->data.variable_declaration.is_mutable ? "mut" : "const";
        char message[MESSAGE_BUFFER_SIZE];
        snprintf(message, sizeof(message),
            "blank identifier '_' requires '='; use '%s _ = <expr>' to discard a result", keyword);
        diagnostic_error_message(parser->diagnostics, "E2084", arena_copy_string(parser->arena, message),
            parser->file, node->token.line, node->token.column, 0);
        synchronize_parser(parser);
        return NULL;
    }

    /* Check for multi-var declaration: temp x i64, y i64 = expr OR temp _, _ = expr */
    if (peek_token_is(parser, TOKEN_COMMA)) {
            /* Collect all variable names and types */
            const char *names[MAX_MULTI_VARIABLES];
            const char *types[MAX_MULTI_VARIABLES];
            int variable_count = 0;
            names[variable_count] = node->data.variable_declaration.name;
            types[variable_count] = node->data.variable_declaration.type_name;
            variable_count++;

            while (peek_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip comma */
                /* The binding name must be an identifier or '_'. Without
                 * this check a keyword is taken as the name and the token
                 * after it consumed as a type annotation. */
                if (!peek_token_is(parser, TOKEN_IDENTIFIER) && !peek_token_is(parser, TOKEN_BLANK)) {
                    if (reject_keyword_as_name(parser, &parser->peek_token, "a variable name")) return NULL;
                    expect_peek_token(parser, TOKEN_IDENTIFIER); /* will error */
                    return NULL;
                }
                next_token(parser); /* name (IDENT or _) */
                if (variable_count >= MAX_MULTI_VARIABLES) {
                    diagnostic_error_code_formatted(parser->diagnostics, "E2062", parser->file, parser->current_token.line, parser->current_token.column, 0, MAX_MULTI_VARIABLES);
                    return NULL;
                }
                names[variable_count] = parser->current_token.literal;
                if (current_token_is(parser, TOKEN_BLANK)) names[variable_count] = "_";
                if (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_CARET) ||
                    peek_token_is(parser, TOKEN_LEFT_BRACKET) || peek_token_is(parser, TOKEN_STRUCT) ||
                    peek_token_is(parser, TOKEN_ENUM) || peek_token_is(parser, TOKEN_QUESTION)) {
                    next_token(parser);
                    types[variable_count] = parse_complex_type(parser);
                    if (!types[variable_count]) return NULL;
                } else {
                    types[variable_count] = NULL;
                }
                variable_count++;
            }

            /* Expect = expr */
            if (!expect_peek_token(parser, TOKEN_ASSIGN)) return NULL;
            next_token(parser);
            AstNode *value = parse_expression(parser, PRECEDENCE_LOWEST);

            /* or_return on a destructuring bind: `mut a, b = two() or_return`.
             * The N binding names are the non-error slots; the trailing Error
             * is propagated if non-nil, otherwise control falls through and
             * binds v0..v{N-1} as normal. */
            AstNode *or_return_fallback_values[MAX_MULTI_VARIABLES];
            int or_return_fallback_count = 0;
            bool has_or_return = peek_token_is(parser, TOKEN_OR_RETURN);
            if (has_or_return) {
                next_token(parser); /* consume or_return */
                or_return_fallback_count = parse_or_return_fallbacks(parser, or_return_fallback_values);
            }

            /* Generate unique temp name. An or_return destructure uses the
             * or_return prefix so the typechecker validates the (..., Error)
             * tail (E3045) and skips the "fewer variables than return values"
             * check — the trailing Error slot is consumed by the guard. */
            static int multi_variable_counter = 0;
            char *temporary_name;
            if (has_or_return) {
                temporary_name = make_or_return_temporary_name(parser->arena);
            } else {
                temporary_name = arena_allocate(parser->arena, TEMPORARY_NAME_BUFFER_SIZE);
                snprintf(temporary_name, TEMPORARY_NAME_BUFFER_SIZE, GRAY_SYNTHETIC_TEMPORARY "%d", multi_variable_counter++);
            }

            /* Create a block with: __auto_type _tmp = expr; type x = _tmp.v0; ... */
            AstNode *block = ast_allocate(parser->arena, NODE_BLOCK_STATEMENT, parser->current_token);
            block->data.block.capacity = variable_count + 2;
            block->data.block.count = 0;
            block->data.block.statements = arena_allocate(parser->arena, sizeof(AstNode *) * block->data.block.capacity);

            /* temp _tmp = value */
            block->data.block.statements[block->data.block.count++] =
                make_synthetic_temporary_declaration(parser, temporary_name, value);

            if (has_or_return) {
                block->data.block.statements[block->data.block.count++] =
                    build_or_return_guard(parser, temporary_name,
                                          or_return_fallback_values, or_return_fallback_count);
            }

            /* Individual declarations: type x = _tmp.v0 */
            for (int i = 0; i < variable_count; i++) {
                AstNode *binding_declaration = ast_allocate(parser->arena, NODE_VARIABLE_DECLARATION, parser->current_token);
                binding_declaration->data.variable_declaration.is_mutable = node->data.variable_declaration.is_mutable;
                binding_declaration->data.variable_declaration.name = names[i];
                binding_declaration->data.variable_declaration.type_name = types[i];
                /* Value: _gray_tmp.vN */
                AstNode *member = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, parser->current_token);
                AstNode *label = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
                label->data.label.value = temporary_name;
                member->data.member.object = label;
                char *field = arena_allocate(parser->arena, FIELD_NAME_BUFFER_SIZE);
                snprintf(field, FIELD_NAME_BUFFER_SIZE, "v%d", i);
                member->data.member.member = field;
                binding_declaration->data.variable_declaration.value = member;
                block->data.block.statements[block->data.block.count++] = binding_declaration;
            }

            return block;
    }

    /* = value */
    if (peek_token_is(parser, TOKEN_ASSIGN)) {
        next_token(parser); /* skip = */
        next_token(parser);
        node->data.variable_declaration.value = parse_expression(parser, PRECEDENCE_LOWEST);

        if (node->data.variable_declaration.value &&
            strcmp(node->data.variable_declaration.name, "_") == 0) {
            check_discard_target(parser, node->data.variable_declaration.value);
        }

        AstNode *desugared = maybe_apply_or_return(parser, node);
        if (desugared) return desugared;
    }

    return node;
}

static AstNode *parse_variable_declaration(Parser *parser) {
    return parse_variable_declaration_common(parser, false);
}

/* `x i64 = 5`, `x, err = f()`: no mut/const keyword; current_token is the name. */
static AstNode *parse_bare_variable_declaration(Parser *parser) {
    return parse_variable_declaration_common(parser, true);
}

static AstNode *parse_return_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_RETURN_STATEMENT, parser->current_token);

    int capacity = GROW_ARRAY_INITIAL_CAPACITY;
    int count = 0;
    AstNode **values = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);

    /* Check if there's a value to return (peek, don't consume) */
    if (!peek_token_is(parser, TOKEN_RIGHT_BRACE) && !peek_token_is(parser, TOKEN_END_OF_FILE)) {
        next_token(parser);
        values[count++] = parse_expression(parser, PRECEDENCE_LOWEST);

        while (peek_token_is(parser, TOKEN_COMMA)) {
            next_token(parser); /* skip comma */
            next_token(parser);
            ARENA_GROW(parser->arena, values, count, capacity);
            values[count++] = parse_expression(parser, PRECEDENCE_LOWEST);
        }
    }

    node->data.return_statement.values = values;
    node->data.return_statement.count = count;
    return node;
}

static AstNode *parse_block_statement(Parser *parser) {
    parser->depth++;
    if (parser->depth > MAX_PARSE_DEPTH) {
        diagnostic_error_message(parser->diagnostics, "E2001",
            arena_copy_string(parser->arena,"block is nested too deeply; maximum depth is 256"),
            parser->file, parser->current_token.line, parser->current_token.column, 0);
        parser->depth--;
        return NULL;
    }

    AstNode *node = ast_allocate(parser->arena, NODE_BLOCK_STATEMENT, parser->current_token);
    node->data.block.count = 0;
    node->data.block.capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.block.statements = arena_allocate(parser->arena, sizeof(AstNode *) * node->data.block.capacity);

    next_token(parser); /* skip { */

    while (!current_token_is(parser, TOKEN_RIGHT_BRACE) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
        parser->seen_attribute_mask = 0;
        AstNode *statement = parse_statement(parser);
        if (statement) {
            ARENA_GROW(parser->arena, node->data.block.statements,
                node->data.block.count, node->data.block.capacity);
            node->data.block.statements[node->data.block.count++] = statement;
        } else {
            /* Error recovery: skip to next statement boundary */
            synchronize_parser(parser);
            if (current_token_is(parser, TOKEN_RIGHT_BRACE)) break;
            continue;
        }
        next_token(parser);
    }

    parser->depth--;
    return node;
}

static AstNode *parse_function_declaration(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_FUNCTION_DECLARATION, parser->current_token);

    if (reject_keyword_as_name(parser, &parser->peek_token, "a function name")) return NULL;
    if (!expect_peek_token(parser, TOKEN_IDENTIFIER)) return NULL;
    node->data.function_declaration.name = parser->current_token.literal;

    /* Parameters */
    if (!expect_peek_token(parser, TOKEN_LEFT_PARENTHESIS)) return NULL;

    int parameter_capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.function_declaration.parameter_count = 0;
    node->data.function_declaration.parameters = arena_allocate(parser->arena, sizeof(Parameter) * parameter_capacity);

    if (!peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
        next_token(parser);
        do {
            ARENA_GROW(parser->arena, node->data.function_declaration.parameters,
                node->data.function_declaration.parameter_count, parameter_capacity);

            Parameter *parameter = &node->data.function_declaration.parameters[node->data.function_declaration.parameter_count];
            memset(parameter, 0, sizeof(Parameter));

            /* Check for mutable parameter (&) */
            if (current_token_is(parser, TOKEN_AMPERSAND)) {
                parameter->is_mutable = true;
                next_token(parser);
            }

            parameter->name = parser->current_token.literal;

            /* Check for reserved names as parameters */
            if (parser->current_token.type != TOKEN_IDENTIFIER && parser->current_token.type != TOKEN_BLANK) {
                /* Keyword used as parameter name */
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "'%s' is a keyword and cannot be used as a parameter name",
                    parameter->name);
                diagnostic_error_message(parser->diagnostics, "E4027", arena_copy_string(parser->arena, message),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
            } else if (parser->current_token.type == TOKEN_IDENTIFIER && is_reserved_name(parameter->name)) {
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "'%s' is a built-in name and cannot be used as a parameter name",
                    parameter->name);
                diagnostic_error_message(parser->diagnostics, "E4028", arena_copy_string(parser->arena, message),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
            }

            /* Type name follows (unless next parameter or closing paren) */
            if (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_CARET) ||
                peek_token_is(parser, TOKEN_LEFT_BRACKET) || peek_token_is(parser, TOKEN_QUESTION) ||
                peek_token_is(parser, TOKEN_LESS_THAN)) {
                next_token(parser);
                if (current_token_is(parser, TOKEN_LESS_THAN)) {
                    /* <?> type parameter syntax */
                    if (!expect_peek_token(parser, TOKEN_QUESTION)) return NULL;
                    if (!expect_peek_token(parser, TOKEN_GREATER_THAN)) return NULL;
                    parameter->type_name = "?";
                    parameter->is_type_parameter = true;
                } else {
                    parameter->type_name = parse_complex_type(parser);
                    if (!parameter->type_name) return NULL;
                }
            } else if (peek_token_is(parser, TOKEN_AMPERSAND)) {
                /* Common mistake: `name &type` instead of `&name type`.
                 * Without this, the loop has no token to consume and
                 * spins until killed externally (#bug-report). */
                diagnostic_error_code_formatted(parser->diagnostics, "E3069", parser->file, parser->peek_token.line, parser->peek_token.column, 0, parameter->name, "type");
                return NULL;
            }

            /* Check for default value: parameter type = expr */
            if (peek_token_is(parser, TOKEN_ASSIGN)) {
                next_token(parser); /* skip = */
                next_token(parser);
                parameter->default_value = parse_expression(parser, PRECEDENCE_LOWEST);
            }

            node->data.function_declaration.parameter_count++;

            if (peek_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip comma */
                next_token(parser);
            } else if (!peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) &&
                       !current_token_is(parser, TOKEN_END_OF_FILE)) {
                /* Forward-progress guard: any unexpected token between
                 * parameters that isn't ',' or ')' would otherwise loop
                 * forever. Surface it as a parse error and bail. */
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "unexpected token '%s' in parameter list; expected ',' or ')'",
                    parser->peek_token.literal ? parser->peek_token.literal : "?");
                diagnostic_error_message(parser->diagnostics, "E2001", arena_copy_string(parser->arena, message),
                    parser->file, parser->peek_token.line, parser->peek_token.column, 0);
                return NULL;
            }
        } while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE));
    }

    if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;

    /* Backfill grouped parameter types and defaults (a, b i64 = 0 → both get i64, both default to 0) */
    for (int i = node->data.function_declaration.parameter_count - 1; i >= 0; i--) {
        Parameter *parameter = &node->data.function_declaration.parameters[i];
        if (!parameter->type_name && i + 1 < node->data.function_declaration.parameter_count) {
            parameter->type_name = node->data.function_declaration.parameters[i + 1].type_name;
            if (!parameter->default_value && node->data.function_declaration.parameters[i + 1].default_value) {
                parameter->default_value = node->data.function_declaration.parameters[i + 1].default_value;
            }
        }
        if (!parameter->type_name && !parameter->default_value) {
            char message[MESSAGE_BUFFER_SIZE];
            snprintf(message, sizeof(message),
                "parameter '%s' is missing a type; every parameter must have a type (e.g., %s i64)",
                parameter->name, parameter->name);
            diagnostic_error_message(parser->diagnostics, "E2002", arena_copy_string(parser->arena, message),
                parser->file, node->token.line, node->token.column, 0);
        }
    }

    /* E2087: type parameters (<?>) cannot be mixed with value parameters */
    {
        bool has_type_parameter = false, has_value_parameter = false;
        for (int i = 0; i < node->data.function_declaration.parameter_count; i++) {
            if (node->data.function_declaration.parameters[i].is_type_parameter)
                has_type_parameter = true;
            else
                has_value_parameter = true;
        }
        if (has_type_parameter && has_value_parameter) {
            diagnostic_error_code(parser->diagnostics, "E2087",
                parser->file, node->token.line, node->token.column, 0);
        }
    }

    /* Return type(s) */
    node->data.function_declaration.return_type_count = 0;
    node->data.function_declaration.return_types = NULL;
    node->data.function_declaration.return_names = NULL;

    if (peek_token_is(parser, TOKEN_ARROW)) {
        next_token(parser); /* skip -> */
        next_token(parser);

        /* E2002: missing return type after -> */
        if (current_token_is(parser, TOKEN_LEFT_BRACE)) {
            diagnostic_error_message(parser->diagnostics, "E2002",
                arena_copy_string(parser->arena, "expected return type after '->', got '{'; either specify a type or remove the '->'"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            /* Parse body to avoid cascading errors */
            node->data.function_declaration.body = parse_block_statement(parser);
            return node;
        }

        /* E2079: reject 'nil' as a return type. For a function that
         * returns nothing, the user should omit the '-> ...' clause. */
        if (current_token_is(parser, TOKEN_NIL)) {
            diagnostic_error_code(parser->diagnostics, "E2079",
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            /* Skip the nil and parse the body so the error doesn't
             * cascade into a codegen crash on an unknown C type. */
            next_token(parser);
            if (!current_token_is(parser, TOKEN_LEFT_BRACE)) {
                /* Malformed; bail with what we have. */
                return node;
            }
            node->data.function_declaration.body = parse_block_statement(parser);
            return node;
        }

        int return_capacity = 16;
        node->data.function_declaration.return_types = arena_allocate(parser->arena, sizeof(const char *) * return_capacity);
        node->data.function_declaration.return_names = arena_allocate(parser->arena, sizeof(const char *) * return_capacity);
        memset(node->data.function_declaration.return_names, 0, sizeof(const char *) * return_capacity);

        if (current_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            /* Multiple/named return types:
             *   -> (i64, string)        plain types
             *   -> (x i64, y i64)       named returns
             *   -> (x, y i64)           shared type
             *
             * Disambiguation: if the identifier is a known type name,
             * it's a plain type list, not names.
             */
            next_token(parser);
            while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
                /* Check if current identifier is a type name (not a variable name) */
                bool is_type = false;
                if (current_token_is(parser, TOKEN_IDENTIFIER)) {
                    const char *literal_text = parser->current_token.literal;
                    is_type = (is_integer_type_name(literal_text) ||
                        strcmp(literal_text, "f32") == 0 ||
                        strcmp(literal_text, "f64") == 0 || strcmp(literal_text, "string") == 0 ||
                        strcmp(literal_text, "bool") == 0 || strcmp(literal_text, "char") == 0 ||
                        (strcmp(literal_text, "map") == 0 && peek_token_is(parser, TOKEN_LEFT_BRACKET)) ||
                        (strcmp(literal_text, "func") == 0 && peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) ||
                        (literal_text[0] >= 'A' && literal_text[0] <= 'Z')); /* struct/enum types */
                }

                /* map[K:V] and func(...) are complex types, not named returns */
                bool is_complex_type_start = is_type && current_token_is(parser, TOKEN_IDENTIFIER) &&
                    ((strcmp(parser->current_token.literal, "map") == 0 && peek_token_is(parser, TOKEN_LEFT_BRACKET)) ||
                     (strcmp(parser->current_token.literal, "func") == 0 && peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)));

                if (current_token_is(parser, TOKEN_IDENTIFIER) && !is_complex_type_start &&
                    (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_QUESTION) ||
                     peek_token_is(parser, TOKEN_LEFT_BRACKET) || peek_token_is(parser, TOKEN_CARET)) &&
                    (!is_type || peek_token_is(parser, TOKEN_IDENTIFIER) ||
                     peek_token_is(parser, TOKEN_CARET) || peek_token_is(parser, TOKEN_LEFT_BRACKET) ||
                     peek_token_is(parser, TOKEN_QUESTION))) {
                    /* Named return: name type; store both (: accept
                     * TOKEN_QUESTION, TOKEN_LEFT_BRACKET, TOKEN_CARET as type-start
                     * tokens so `(first ?, items [i64], ptr ^T)` work) */
                    const char *return_name = parser->current_token.literal;
                    next_token(parser);
                    int return_index = node->data.function_declaration.return_type_count;
                    if (return_index >= return_capacity) {
                        diagnostic_error_code_formatted(parser->diagnostics, "E2060", parser->file, parser->current_token.line, parser->current_token.column, 0, MAX_SHARED_RETURNS);
                        return NULL;
                    }
                    node->data.function_declaration.return_names[return_index] = return_name;
                    node->data.function_declaration.return_types[return_index] = parse_complex_type(parser);
                    node->data.function_declaration.return_type_count++;
                } else if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_COMMA) && !is_type) {
                    /* Shared type: (x, y i64); collect names, assign same type */
                    const char *names[MAX_SHARED_RETURNS];
                    int shared = 0;
                    names[shared++] = parser->current_token.literal;
                    while (peek_token_is(parser, TOKEN_COMMA)) {
                        next_token(parser); /* skip comma */
                        next_token(parser); /* next name */
                        names[shared++] = parser->current_token.literal;
                        if (shared >= MAX_SHARED_RETURNS) break;
                        if (!peek_token_is(parser, TOKEN_COMMA)) break;
                    }
                    /* the current token is the last name, peek should be the shared type */
                    if (peek_token_is(parser, TOKEN_IDENTIFIER)) {
                        next_token(parser);
                        for (int shared_index = 0; shared_index < shared; shared_index++) {
                            int return_index = node->data.function_declaration.return_type_count;
                            if (return_index >= return_capacity) {
                                diagnostic_error_code_formatted(parser->diagnostics, "E2060", parser->file, parser->current_token.line, parser->current_token.column, 0, MAX_SHARED_RETURNS);
                                return NULL;
                            }
                            node->data.function_declaration.return_names[return_index] = names[shared_index];
                            node->data.function_declaration.return_types[return_index] = read_type_name(parser);
                            node->data.function_declaration.return_type_count++;
                        }
                    }
                } else {
                    /* Plain type (no name) — use parse_complex_type to
                     * handle array, map, and pointer return types like
                     * [string], map[K:V], ^T, not just simple idents. */
                    int return_index = node->data.function_declaration.return_type_count;
                    if (return_index >= return_capacity) {
                        diagnostic_error_code_formatted(parser->diagnostics, "E2060", parser->file, parser->current_token.line, parser->current_token.column, 0, MAX_SHARED_RETURNS);
                        return NULL;
                    }
                    node->data.function_declaration.return_names[return_index] = NULL;
                    node->data.function_declaration.return_types[return_index] = parse_complex_type(parser);
                    node->data.function_declaration.return_type_count++;
                }
                if (peek_token_is(parser, TOKEN_COMMA)) {
                    next_token(parser);
                }
                next_token(parser);
            }
        } else {
            /* Single return type (array, pointer, map, or plain) */
            node->data.function_declaration.return_types[0] = parse_complex_type(parser);
            if (!node->data.function_declaration.return_types[0]) return NULL;
            node->data.function_declaration.return_type_count = 1;

            /* E2081: catch `-> Foo^` — '^' after a type name is a dereference
             * operator, not a type modifier; the correct form is `-> ^Foo`. */
            if (peek_token_is(parser, TOKEN_CARET)) {
                const char *type_name = node->data.function_declaration.return_types[0];
                char message[256];
                snprintf(message, sizeof(message),
                    "'^' is a dereference operator, not a type modifier; "
                    "for a pointer return type write '^%s', not '%s^'",
                    type_name, type_name);
                next_token(parser); /* consume the '^' so we can point at it */
                diagnostic_error_message(parser->diagnostics, "E2081", arena_copy_string(parser->arena, message),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                /* Recover: parse the body to avoid cascading errors. */
                if (peek_token_is(parser, TOKEN_LEFT_BRACE)) {
                    next_token(parser);
                    node->data.function_declaration.body = parse_block_statement(parser);
                }
                return node;
            }
        }
    }

    /* Body */
    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    AstNode *saved_function = parser->current_function;
    parser->current_function = node;
    node->data.function_declaration.body = parse_block_statement(parser);
    parser->current_function = saved_function;

    return node;
}

/* Why `name` cannot be bound as a module name, or NULL when it can. The
 * phrase completes "module name 'x' ...". */
static const char *module_name_reject_reason(const char *name) {
    if (!name[0]) return "is empty";
    if (!isalpha((unsigned char)name[0]) && name[0] != '_')
        return "is not a valid identifier";
    for (const char *cursor = name + 1; *cursor; cursor++) {
        if (!isalnum((unsigned char)*cursor) && *cursor != '_')
            return "is not a valid identifier";
    }
    TokenType keyword_type;
    const char *keyword_text;
    if (token_lookup_keyword_with_length(name, (int)strlen(name), &keyword_type, &keyword_text))
        return "is a keyword";
    return NULL;
}

static AstNode *parse_import_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_IMPORT_STATEMENT, parser->current_token);

    int capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.import_statement.count = 0;
    node->data.import_statement.items = arena_allocate(parser->arena, sizeof(ImportItem) * capacity);
    node->data.import_statement.should_auto_use = false;

    bool is_extern_import = false;
    if (current_token_is(parser, TOKEN_EXTERN)) {
        is_extern_import = true;
        if (!expect_peek_token(parser, TOKEN_IMPORT)) return NULL;
    }

    do {
        next_token(parser);
        ImportItem *item = &node->data.import_statement.items[node->data.import_statement.count];
        memset(item, 0, sizeof(ImportItem));

        if (is_extern_import) {
            if (!current_token_is(parser, TOKEN_STRING)) {
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "expected string literal header path after 'extern import', got '%s'",
                    parser->current_token.literal ? parser->current_token.literal : "?");
                diagnostic_error_message(parser->diagnostics, "E6014", arena_copy_string(parser->arena, message),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                return node;
            }
            item->is_c_import = true;
            item->is_stdlib = false;
            item->path = parser->current_token.literal;
            item->token = parser->current_token;
            item->alias = "extern";
            item->module = "extern";
            /* Validate path: only [A-Za-z0-9./_+-] permitted to prevent injection */
            for (const char *cursor = item->path; *cursor; cursor++) {
                unsigned char character = (unsigned char)*cursor;
                bool is_allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') ||
                          character == '/' || character == '.' || character == '_' || character == '-' || character == '+';
                if (!is_allowed) {
                    diagnostic_error_message(parser->diagnostics, "E2080",
                        arena_copy_string(parser->arena, "invalid character in C header path; only [A-Za-z0-9./_+-] are permitted"),
                        parser->file, parser->current_token.line, parser->current_token.column, 0);
                    break;
                }
            }
            /* 'extern import ... and use' is disallowed; C symbols stay qualified */
            if (peek_token_is(parser, TOKEN_IDENTIFIER) && parser->peek_token.literal &&
                strcmp(parser->peek_token.literal, "and") == 0) {
                next_token(parser); /* consume 'and' */
                diagnostic_error_code(parser->diagnostics, "E6013", parser->file,
                    parser->current_token.line, parser->current_token.column, 0);
                if (peek_token_is(parser, TOKEN_USE)) next_token(parser); /* consume 'use' */
            }
            goto import_item_done;
        }

        if (current_token_is(parser, TOKEN_IDENTIFIER) && strcmp(parser->current_token.literal, "and") == 0) {
            /* import and use syntax; consume 'and' then 'use' */
            node->data.import_statement.should_auto_use = true;
            next_token(parser); /* consume 'use' */
            next_token(parser); /* advance to alias or @module */
        }

        /* Migration hint for the retired 'import c"header.h"' syntax. Only fires
         * when the path looks like a C header/source ('.h'/'.c'); 'c' is now a
         * valid alias for ordinary imports (import c "./config.gray"). */
        if (current_token_is(parser, TOKEN_IDENTIFIER) &&
            strcmp(parser->current_token.literal, "c") == 0 &&
            peek_token_is(parser, TOKEN_STRING) &&
            parser->peek_token.literal) {
            const char *header_path = parser->peek_token.literal;
            size_t header_path_length = strlen(header_path);
            if (header_path_length >= 2 && header_path[header_path_length - 2] == '.' &&
                (header_path[header_path_length - 1] == 'h' || header_path[header_path_length - 1] == 'c')) {
                diagnostic_error_message(parser->diagnostics, "E6014",
                    arena_copy_string(parser->arena, "'import c\"...\"' syntax has been replaced; use 'extern import \"...\"'"),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                next_token(parser); /* consume 'c', now on string */
                item->is_c_import = true;
                item->is_stdlib = false;
                item->path = parser->current_token.literal;
                item->token = parser->current_token;
                item->alias = "extern";
                item->module = "extern";
                goto import_item_done;
            }
        }

        /* Check for alias: identifier followed by @ or string */
        Token alias_token = parser->current_token;
        bool is_aliased_stdlib = false;
        if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_AT)) {
            is_aliased_stdlib = true;
            next_token(parser); /* consume alias, now on @ */
        } else if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_STRING)) {
            item->alias = parser->current_token.literal;
            next_token(parser); /* consume alias, now on string */
        }

        if (current_token_is(parser, TOKEN_AT)) {
            item->is_stdlib = true;
            next_token(parser);
            item->module = parser->current_token.literal;
            item->alias = parser->current_token.literal;
            /* A local import may be aliased because its name comes from the
             * filesystem and can collide or be unspellable. A stdlib module's
             * name is fixed, unique, and always a valid identifier, so a
             * second name for it is one the symbol table cannot key. */
            if (is_aliased_stdlib) {
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "standard library import '@%s' cannot be aliased", item->module);
                diagnostic_error_help(parser->diagnostics, "E6007",
                    arena_copy_string(parser->arena, message),
                    parser->file, alias_token.line, alias_token.column, 0,
                    "remove the alias and use the module's own name");
            }
        } else if (current_token_is(parser, TOKEN_STRING)) {
            item->is_stdlib = false;
            item->path = parser->current_token.literal;
            /* Derive module name from filename/directory if no alias */
            bool is_derived_module = !item->alias;
            if (!item->alias) {
                const char *slash = strrchr(item->path, '/');
                const char *base = slash ? slash + 1 : item->path;
                size_t base_length = strlen(base);
                if (base_length > 5 && strcmp(base + base_length - 5, ".gray") == 0) {
                    /* Strip .gray extension: "helpers.gray" → "helpers" */
                    char *module_name = arena_allocate(parser->arena, base_length - 4);
                    memcpy(module_name, base, base_length - 5);
                    module_name[base_length - 5] = '\0';
                    item->alias = module_name;
                    item->module = module_name;
                } else if (base_length > 0) {
                    /* No .gray extension: use last path component as module name */
                    char *module_name = arena_allocate(parser->arena, base_length + 1);
                    memcpy(module_name, base, base_length);
                    module_name[base_length] = '\0';
                    item->alias = module_name;
                    item->module = module_name;
                }
            }
            /* A derived module name comes from the filesystem, which allows
             * spellings Grayscale identifiers do not. Reject them here, where
             * an alias is the fix, rather than at the use site where the name
             * parses as something else entirely. */
            if (is_derived_module && item->module) {
                const char *reason = module_name_reject_reason(item->module);
                if (reason) {
                    char message[MESSAGE_BUFFER_SIZE];
                    char help[MESSAGE_BUFFER_SIZE];
                    snprintf(message, sizeof(message), "module name '%s' %s", item->module, reason);
                    snprintf(help, sizeof(help),
                        "give the import an alias, e.g. import m \"%s\"", item->path);
                    diagnostic_error_help(parser->diagnostics, "E6006",
                        arena_copy_string(parser->arena, message),
                        parser->file, parser->current_token.line, parser->current_token.column, 0,
                        arena_copy_string(parser->arena, help));
                }
            }
            /* Reject 'extern' as a module name; reserved for C interop */
            if (item->alias && strcmp(item->alias, "extern") == 0) {
                diagnostic_error_message(parser->diagnostics, "E6014",
                    arena_copy_string(parser->arena,"'extern' is reserved for C interop; rename the file or use an alias (e.g., 'import mymod \"./extern.gray\"')"),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
            }
        } else if (current_token_is(parser, TOKEN_IDENTIFIER)) {
            char message[MESSAGE_BUFFER_SIZE];
            snprintf(message, sizeof(message),
                "expected '@module' or '\"path\"' after 'import', got '%s'",
                parser->current_token.literal);
            diagnostic_error_message(parser->diagnostics, "E6014", arena_copy_string(parser->arena, message),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            return node;
        }

        import_item_done:
        node->data.import_statement.count++;

        ARENA_GROW(parser->arena, node->data.import_statement.items,
            node->data.import_statement.count, capacity);
    } while (peek_token_is(parser, TOKEN_COMMA) && (next_token(parser), 1));

    return node;
}

static AstNode *parse_using_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_USING_STATEMENT, parser->current_token);

    int capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.using_statement.count = 0;
    node->data.using_statement.modules = arena_allocate(parser->arena, sizeof(const char *) * capacity);

    do {
        next_token(parser);
        if (current_token_is(parser, TOKEN_EXTERN) ||
            (parser->current_token.literal && strcmp(parser->current_token.literal, "extern") == 0)) {
            diagnostic_error_code(parser->diagnostics, "E6013", parser->file,
                parser->current_token.line, parser->current_token.column, 0);
        }
        ARENA_GROW(parser->arena, node->data.using_statement.modules,
            node->data.using_statement.count, capacity);
        node->data.using_statement.modules[node->data.using_statement.count++] = parser->current_token.literal;
    } while (peek_token_is(parser, TOKEN_COMMA) && (next_token(parser), 1));

    return node;
}

static AstNode *parse_if_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_IF_STATEMENT, parser->current_token);

    next_token(parser);
    /* The '{' after a condition opens the block, never a struct literal, the
     * same way it does after a `when` subject. This used to be settled by
     * requiring an initial capital on a literal's type name, which made every
     * lowercase-named struct unusable everywhere else. */
    bool saved_should_suppress_struct_literal = parser->should_suppress_struct_literal;
    parser->should_suppress_struct_literal = true;
    node->data.if_statement.condition = parse_expression(parser, PRECEDENCE_LOWEST);
    parser->should_suppress_struct_literal = saved_should_suppress_struct_literal;

    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    node->data.if_statement.consequence = parse_block_statement(parser);

    node->data.if_statement.alternative = NULL;

    if (peek_token_is(parser, TOKEN_OR_KEYWORD)) {
        next_token(parser); /* skip 'or' */
        /* 'or' acts like 'else if' */
        node->data.if_statement.alternative = parse_if_statement(parser);
    } else if (peek_token_is(parser, TOKEN_OTHERWISE)) {
        next_token(parser); /* skip 'otherwise'/'else' */
        node->data.if_statement.else_token = parser->current_token;
        if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
        node->data.if_statement.alternative = parse_block_statement(parser);
    }

    return node;
}

/* E2058: a declaration nested inside a struct or enum body. `kind` and
 * `outer_name` name the enclosing declaration. Skips to the nested
 * declaration's closing brace to avoid cascading errors. */
static void reject_nested_declaration(Parser *parser, const char *kind, const char *outer_name) {
    diagnostic_error_code_formatted(parser->diagnostics, "E2058",
        parser->file, parser->current_token.line, parser->current_token.column, 0,
        kind, outer_name);
    int depth = 0;
    while (!current_token_is(parser, TOKEN_END_OF_FILE)) {
        if (current_token_is(parser, TOKEN_LEFT_BRACE)) depth++;
        if (current_token_is(parser, TOKEN_RIGHT_BRACE)) {
            if (depth <= 1) { next_token(parser); break; }
            depth--;
        }
        next_token(parser);
    }
}

static AstNode *parse_struct_declaration(Parser *parser) {
    /* current_token is the struct name (IDENT), already consumed by caller */
    AstNode *node = ast_allocate(parser->arena, NODE_STRUCT_DECLARATION, parser->current_token);
    node->data.struct_declaration.name = parser->current_token.literal;

    next_token(parser); /* skip 'struct' keyword */
    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    int brace_line = parser->current_token.line;
    next_token(parser); /* skip { */

    /* Reject inline struct declarations; fields must be on separate lines */
    if (parser->current_token.line == brace_line && !current_token_is(parser, TOKEN_RIGHT_BRACE)) {
        diagnostic_error_message(parser->diagnostics, "E2002",
            arena_copy_string(parser->arena,"struct fields must be on separate lines; inline struct declarations are not allowed"),
            parser->file, parser->current_token.line, parser->current_token.column, 0);
    }

    int previous_field_line = -1;
    int field_capacity = GROW_ARRAY_INITIAL_CAPACITY;
    int function_capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.struct_declaration.field_count = 0;
    node->data.struct_declaration.fields = arena_allocate(parser->arena, sizeof(StructField) * field_capacity);
    node->data.struct_declaration.function_count = 0;
    node->data.struct_declaration.functions = arena_allocate(parser->arena, sizeof(StructFunction) * function_capacity);

    bool has_pending_discard = false;
    bool has_pending_deprecated = false;
    const char *pending_deprecated_message = NULL;
    while (!current_token_is(parser, TOKEN_RIGHT_BRACE) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
        /* A non-attribute token starts the next struct member; clear the
         * per-declaration attribute set so duplicates are scoped to one member
         * ('private' and '#test' are not real starts, so they don't reset). */
        if (!current_token_is(parser, TOKEN_DOC) && !current_token_is(parser, TOKEN_DISCARD)
            && !current_token_is(parser, TOKEN_DEPRECATED) && !current_token_is(parser, TOKEN_TEST)
            && !current_token_is(parser, TOKEN_PRIVATE)) {
            parser->seen_attribute_mask = 0;
        }
        /* `#[...]` attribute lists are not supported on struct functions yet;
         * stack the attributes instead. Emit one error and skip the list so the
         * body keeps parsing. */
        if (current_token_is(parser, TOKEN_HASH_LEFT_BRACKET)) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "'#[...]' attribute lists are not supported on struct functions; stack the attributes one per line instead"), parser->current_token.line, parser->current_token.column);
            while (!current_token_is(parser, TOKEN_RIGHT_BRACKET) && !current_token_is(parser, TOKEN_END_OF_FILE)
                   && !current_token_is(parser, TOKEN_RIGHT_BRACE)) {
                next_token(parser);
            }
            if (current_token_is(parser, TOKEN_RIGHT_BRACKET)) next_token(parser);
            continue;
        }
        /* skip #doc attributes on struct functions. Consume
         * the attribute + any parenthesised arguments, then continue so
         * the next token (do/private do) is handled normally. */
        if (current_token_is(parser, TOKEN_DOC)) {
            reject_duplicate_attribute(parser, ATTRIBUTE_DOC, "#doc");
            if (peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                next_token(parser);
                while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE))
                    next_token(parser);
            }
            next_token(parser);
            continue;
        }
        /* #discard inside struct body: set pending flag, then the
         * next iteration will attach it to the parsed function. */
        if (current_token_is(parser, TOKEN_DISCARD)) {
            reject_duplicate_attribute(parser, ATTRIBUTE_DISCARD, "#discard");
            has_pending_discard = true;
            next_token(parser);
            continue;
        }
        /* #test is not allowed on struct functions — a test function must be
         * a top-level 'do' so the runner can call it directly. */
        if (current_token_is(parser, TOKEN_TEST)) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#test attribute can only be applied to top-level function declarations, not struct functions"), parser->current_token.line, parser->current_token.column);
            next_token(parser);
            continue;
        }
        /* #deprecated inside struct body: same pending-flag treatment,
         * independent of pending_discard so both can stack on one function. */
        if (current_token_is(parser, TOKEN_DEPRECATED)) {
            bool is_duplicate = reject_duplicate_attribute(parser, ATTRIBUTE_DEPRECATED, "#deprecated");
            next_token(parser); /* consume #deprecated */
            has_pending_deprecated = true;
            if (!is_duplicate) pending_deprecated_message = NULL;
            if (current_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                next_token(parser); /* consume ( */
                if (current_token_is(parser, TOKEN_STRING)) {
                    if (!is_duplicate) pending_deprecated_message = arena_copy_string(parser->arena, parser->current_token.literal);
                    next_token(parser); /* consume string */
                } else {
                    emit_attribute_error(parser, arena_copy_string(parser->arena, "#deprecated expects a string literal message, e.g. #deprecated(\"use x() instead\")"), parser->current_token.line, parser->current_token.column);
                }
                if (current_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
                    next_token(parser); /* consume ) */
                } else {
                    emit_attribute_error(parser, arena_copy_string(parser->arena, "expected ')' after #deprecated message"), parser->current_token.line, parser->current_token.column);
                }
            }
            continue;
        }
        /* Check for struct-namespaced function: do func() or private do func() */
        if (current_token_is(parser, TOKEN_DO)) {
            AstNode *function_declaration = parse_function_declaration(parser);
            if (function_declaration) {
                if (has_pending_discard) {
                    function_declaration->data.function_declaration.is_discard = true;
                    has_pending_discard = false;
                }
                if (has_pending_deprecated) {
                    function_declaration->data.function_declaration.is_deprecated = true;
                    function_declaration->data.function_declaration.deprecated_message = pending_deprecated_message;
                    has_pending_deprecated = false;
                    pending_deprecated_message = NULL;
                }
                ARENA_GROW(parser->arena, node->data.struct_declaration.functions,
                    node->data.struct_declaration.function_count, function_capacity);
                node->data.struct_declaration.functions[node->data.struct_declaration.function_count++].function_declaration = function_declaration;
            }
            next_token(parser);
            continue;
        }
        if (current_token_is(parser, TOKEN_PRIVATE) && peek_token_is(parser, TOKEN_DO)) {
            next_token(parser); /* consume 'private' */
            AstNode *function_declaration = parse_function_declaration(parser);
            if (function_declaration) {
                function_declaration->data.function_declaration.is_private = true;
                if (has_pending_discard) {
                    function_declaration->data.function_declaration.is_discard = true;
                    has_pending_discard = false;
                }
                if (has_pending_deprecated) {
                    function_declaration->data.function_declaration.is_deprecated = true;
                    function_declaration->data.function_declaration.deprecated_message = pending_deprecated_message;
                    has_pending_deprecated = false;
                    pending_deprecated_message = NULL;
                }
                ARENA_GROW(parser->arena, node->data.struct_declaration.functions,
                    node->data.struct_declaration.function_count, function_capacity);
                int function_index = node->data.struct_declaration.function_count++;
                node->data.struct_declaration.functions[function_index].function_declaration = function_declaration;
                node->data.struct_declaration.functions[function_index].is_private = true;
            }
            next_token(parser);
            continue;
        }

        /* E2058: nested struct/enum declaration */
        if (current_token_is(parser, TOKEN_CONST)) {
            reject_nested_declaration(parser, "struct", node->data.struct_declaration.name);
            continue;
        }

        ARENA_GROW(parser->arena, node->data.struct_declaration.fields,
            node->data.struct_declaration.field_count, field_capacity);

        /* E2070: wildcard `?` in field-name position used to slip past the
         * struct-field guard (the check further down only inspects the type
         * slot) and embed '?' in the generated C struct identifier, where
         * clang rejected it with a raw C error. Catch it here before reading
         * the name. */
        if (current_token_is(parser, TOKEN_QUESTION)) {
            diagnostic_error_message(parser->diagnostics, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' is not allowed as a struct field name; only in function parameter and return types"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            next_token(parser); /* skip the '?' */
            /* Skip the trailing type token (if any) so we don't cascade. */
            if (!current_token_is(parser, TOKEN_RIGHT_BRACE) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
                parse_complex_type(parser);
                next_token(parser);
            }
            continue;
        }

        /* E2089: #discard on a struct field instead of a function */
        if (has_pending_discard) {
            diagnostic_error_code(parser->diagnostics, "E2089",
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            has_pending_discard = false;
        }

        /* Same for #deprecated. Clearing the pending state is what stops the
         * attribute from drifting onto the next struct function in the body. */
        if (has_pending_deprecated) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#deprecated attribute can only be applied to function, struct, or enum declarations"), parser->current_token.line, parser->current_token.column);
            has_pending_deprecated = false;
            pending_deprecated_message = NULL;
        }

        /* E2002: multiple fields on the same line */
        if (previous_field_line >= 0 && parser->current_token.line == previous_field_line) {
            diagnostic_error_message(parser->diagnostics, "E2002",
                arena_copy_string(parser->arena,"struct fields must be on separate lines"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
        }
        previous_field_line = parser->current_token.line;

        /* Collect one or more comma-separated field names, then read the
         * shared type and backfill (mirrors the parameter grouping logic).
         * Example: `x, y, z f64` → three fields, all typed f64.       */
        int group_start = node->data.struct_declaration.field_count;
        bool was_field_name_rejected = false;
        for (;;) {
            ARENA_GROW(parser->arena, node->data.struct_declaration.fields,
                node->data.struct_declaration.field_count, field_capacity);
            /* Reject reserved keywords and type names as struct field names */
            if (reject_keyword_as_name(parser, &parser->current_token, "a struct field name")) {
                node->data.struct_declaration.field_count = group_start;
                was_field_name_rejected = true;
                break;
            }
            if (current_token_is(parser, TOKEN_IDENTIFIER) && is_reserved_name(parser->current_token.literal)) {
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "'%s' is a built-in name and cannot be used as a struct field name",
                    parser->current_token.literal);
                diagnostic_error_message(parser->diagnostics, "E4028", arena_copy_string(parser->arena, message),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                synchronize_parser(parser);
                node->data.struct_declaration.field_count = group_start;
                was_field_name_rejected = true;
                break;
            }
            StructField *field = &node->data.struct_declaration.fields[node->data.struct_declaration.field_count];
            field->name = parser->current_token.literal;
            field->type_name = NULL;
            field->json_tag = NULL;
            node->data.struct_declaration.field_count++;
            next_token(parser);
            if (current_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip comma, loop for next name */
            } else {
                break;
            }
        }
        /* A rejected field name has already been diagnosed and synchronized past;
         * the cursor now sits on the next field (or `}`). Re-enter the outer loop
         * rather than falling into the type-parse below, which would otherwise
         * consume the next field's name as a type and cascade a false error onto
         * its type keyword. */
        if (was_field_name_rejected) continue;

        /* Current token is now the type; parse it and backfill all names in this group */
        const char *type_name = parse_complex_type(parser);
        if (!type_name) return NULL;
        /* E2070: wildcard `?` is not allowed as a struct field type */
        if (type_string_has_wildcard(type_name)) {
            diagnostic_error_message(parser->diagnostics, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' cannot be used as a struct field type"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            return NULL;
        }
        for (int i = group_start; i < node->data.struct_declaration.field_count; i++) {
            node->data.struct_declaration.fields[i].type_name = type_name;
            node->data.struct_declaration.fields[i].default_value = NULL;
            node->data.struct_declaration.fields[i].json_tag = NULL;
        }
        next_token(parser);

        /* Optional field tag: `` `json:"Name"` `` right after the type,
         * before any default value. Stored raw here; the typechecker
         * validates it (for #json structs) and extracts the key. */
        if (current_token_is(parser, TOKEN_RAW_STRING)) {
            /* E2095: a tag names one JSON key, so it can't be shared by a
             * comma-grouped field list (`x, y i64 `json:"..."``) — every
             * field would serialize under the same key. */
            if (node->data.struct_declaration.field_count - group_start > 1) {
                diagnostic_error_message(parser->diagnostics, "E2095",
                    arena_copy_string(parser->arena,
                        "a field tag cannot be shared across grouped field names; give each field its own line and tag"),
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
            } else {
                const char *tag = parser->current_token.literal;
                node->data.struct_declaration.fields[group_start].json_tag = tag;
            }
            next_token(parser);
        }

        /* Parse optional default value: `= expr` */
        if (current_token_is(parser, TOKEN_ASSIGN)) {
            next_token(parser); /* skip '=' */
            AstNode *default_value = parse_expression(parser, PRECEDENCE_LOWEST);
            for (int i = group_start; i < node->data.struct_declaration.field_count; i++) {
                node->data.struct_declaration.fields[i].default_value = default_value;
            }
            next_token(parser);
        }

        /* Skip optional trailing comma after a field type */
        if (current_token_is(parser, TOKEN_COMMA)) next_token(parser);

        /* Reject semicolons */
        if (current_token_is(parser, TOKEN_SEMICOLON)) {
            diagnostic_error_message(parser->diagnostics, "E2069",
                arena_copy_string(parser->arena,"semicolons are not used; put each struct field on its own line"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            next_token(parser);
        }
    }

    return node;
}

static AstNode *parse_enum_declaration(Parser *parser) {
    /* current_token is the enum name (IDENT), already consumed by caller */
    AstNode *node = ast_allocate(parser->arena, NODE_ENUM_DECLARATION, parser->current_token);
    node->data.enum_declaration.name = parser->current_token.literal;
    node->data.enum_declaration.is_flags = false;
    node->data.enum_declaration.is_tagged = false;
    node->data.enum_declaration.is_error_code = false;

    next_token(parser); /* skip 'enum' keyword */
    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    int enum_brace_line = parser->current_token.line;
    next_token(parser); /* skip { */

    /* Reject inline enum declarations; variants must be on separate lines */
    if (parser->current_token.line == enum_brace_line && !current_token_is(parser, TOKEN_RIGHT_BRACE)) {
        diagnostic_error_message(parser->diagnostics, "E2002",
            arena_copy_string(parser->arena,"enum variants must be on separate lines; inline enum declarations are not allowed"),
            parser->file, parser->current_token.line, parser->current_token.column, 0);
    }

    int previous_variant_line = -1;
    int value_capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.enum_declaration.value_count = 0;
    node->data.enum_declaration.values = arena_allocate(parser->arena, sizeof(EnumValue) * value_capacity);

    while (!current_token_is(parser, TOKEN_RIGHT_BRACE) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
        ARENA_GROW(parser->arena, node->data.enum_declaration.values,
            node->data.enum_declaration.value_count, value_capacity);

        /* Neither attribute is meaningful on a variant, and both were being
         * read as the variant name, embedding '#discard'/'#deprecated' in the
         * generated C enumerator. Diagnose and consume them here so the name
         * slot below sees the real variant. */
        if (current_token_is(parser, TOKEN_DISCARD)) {
            diagnostic_error_message(parser->diagnostics, "E2089",
                arena_copy_string(parser->arena,
                    "#discard attribute can only be applied to function declarations, not enum variants"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            next_token(parser);
            continue;
        }
        if (current_token_is(parser, TOKEN_TEST)) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#test attribute can only be applied to function declarations, not enum variants"), parser->current_token.line, parser->current_token.column);
            next_token(parser);
            continue;
        }
        if (current_token_is(parser, TOKEN_DEPRECATED)) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#deprecated attribute can only be applied to function, struct, or enum declarations"), parser->current_token.line, parser->current_token.column);
            next_token(parser); /* consume #deprecated */
            /* Consume an optional ("message") so it is not read as a variant */
            if (current_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE))
                    next_token(parser);
                if (current_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) next_token(parser);
            }
            continue;
        }

        /* E2058: nested struct/enum declaration */
        if (current_token_is(parser, TOKEN_CONST)) {
            reject_nested_declaration(parser, "enum", node->data.enum_declaration.name);
            continue;
        }

        /* E2070: wildcard `?` in variant-name position used to slip past the
         * parser and embed '?' in the generated C enum identifier, where clang
         * rejected it with a raw C error. Catch it here before reading the
         * variant name. */
        if (current_token_is(parser, TOKEN_QUESTION)) {
            diagnostic_error_message(parser->diagnostics, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' is not allowed in enum declarations; only in function parameter and return types"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            next_token(parser); /* skip the '?' */
            /* Skip an optional trailing ',' so we don't cascade into the
             * next variant with a stale current_token. */
            if (current_token_is(parser, TOKEN_COMMA)) next_token(parser);
            continue;
        }

        /* Reject reserved names as enum variant names */
        if (reject_keyword_as_name(parser, &parser->current_token, "an enum variant name")) continue;
        if (current_token_is(parser, TOKEN_IDENTIFIER) && is_reserved_name(parser->current_token.literal)) {
            char message[MESSAGE_BUFFER_SIZE];
            snprintf(message, sizeof(message),
                "'%s' is a built-in name and cannot be used as an enum variant name",
                parser->current_token.literal);
            diagnostic_error_message(parser->diagnostics, "E4028", arena_copy_string(parser->arena, message),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            synchronize_parser(parser);
            continue;
        }

        /* E2002: multiple variants on the same line */
        if (previous_variant_line >= 0 && parser->current_token.line == previous_variant_line) {
            diagnostic_error_message(parser->diagnostics, "E2002",
                arena_copy_string(parser->arena,"enum variants must be on separate lines"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
        }
        previous_variant_line = parser->current_token.line;

        EnumValue *variant = &node->data.enum_declaration.values[node->data.enum_declaration.value_count];
        variant->name = parser->current_token.literal;
        variant->value = NULL;
        variant->payload_types = NULL;
        variant->payload_count = 0;

        /* Check for payload types: VARIANT(type1, type2, ...) */
        if (peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            next_token(parser); /* consume ( */
            next_token(parser); /* first token of first type */
            int payload_type_capacity = GROW_ARRAY_INITIAL_CAPACITY;
            variant->payload_types = arena_allocate(parser->arena, sizeof(const char *) * payload_type_capacity);
            while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
                ARENA_GROW(parser->arena, variant->payload_types, variant->payload_count, payload_type_capacity);
                const char *payload_type = parse_complex_type(parser);
                if (!payload_type) return NULL;
                variant->payload_types[variant->payload_count++] = payload_type;
                next_token(parser); /* advance past last token of type */
                if (current_token_is(parser, TOKEN_COMMA)) next_token(parser);
            }
            /* current_token is now TOKEN_RIGHT_PARENTHESIS */
        }

        /* Check for explicit value: VALUE = expr */
        if (peek_token_is(parser, TOKEN_ASSIGN)) {
            /* E2083: payload and explicit value are mutually exclusive */
            if (variant->payload_count > 0) {
                diagnostic_error_code_formatted(parser->diagnostics, "E2083",
                    parser->file, parser->current_token.line, parser->current_token.column, 0,
                    variant->name);
            }
            next_token(parser); /* skip = */
            next_token(parser);
            variant->value = parse_expression(parser, PRECEDENCE_LOWEST);
        }

        node->data.enum_declaration.value_count++;
        /* Skip optional trailing comma */
        if (peek_token_is(parser, TOKEN_COMMA)) {
            next_token(parser);
        }
        next_token(parser);

        /* Reject semicolons */
        if (current_token_is(parser, TOKEN_SEMICOLON)) {
            diagnostic_error_message(parser->diagnostics, "E2069",
                arena_copy_string(parser->arena,"semicolons are not used; put each enum variant on its own line"),
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            next_token(parser);
        }
    }

    /* Set is_tagged if any variant has a payload */
    for (int j = 0; j < node->data.enum_declaration.value_count; j++) {
        if (node->data.enum_declaration.values[j].payload_count > 0) {
            node->data.enum_declaration.is_tagged = true;
            break;
        }
    }

    return node;
}

/* Parse struct literal: StructName{field: value, ...} */
static AstNode *parse_struct_literal(Parser *parser, const char *name) {
    AstNode *node = ast_allocate(parser->arena, NODE_STRUCT_VALUE, parser->current_token);
    node->data.struct_value.name = name;

    int capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.struct_value.count = 0;
    node->data.struct_value.field_names = arena_allocate(parser->arena, sizeof(const char *) * capacity);
    node->data.struct_value.field_values = arena_allocate(parser->arena, sizeof(AstNode *) * capacity);

    next_token(parser); /* skip { */

    while (!current_token_is(parser, TOKEN_RIGHT_BRACE) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
        if (node->data.struct_value.count >= capacity) {
            capacity = GROW_NEXT_CAPACITY(capacity);
            ARENA_GROW_TO(parser->arena, node->data.struct_value.field_names,
                node->data.struct_value.count, capacity);
            ARENA_GROW_TO(parser->arena, node->data.struct_value.field_values,
                node->data.struct_value.count, capacity);
        }

        node->data.struct_value.field_names[node->data.struct_value.count] = parser->current_token.literal;

        if (!expect_peek_token(parser, TOKEN_COLON)) return NULL;
        next_token(parser);
        node->data.struct_value.field_values[node->data.struct_value.count] =
            parse_expression(parser, PRECEDENCE_LOWEST);
        node->data.struct_value.count++;

        if (peek_token_is(parser, TOKEN_COMMA)) {
            next_token(parser);
        }
        next_token(parser);
    }

    return node;
}

static AstNode *parse_ensure_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_ENSURE_STATEMENT, parser->current_token);
    next_token(parser);
    node->data.ensure_statement.expression = parse_expression(parser, PRECEDENCE_LOWEST);
    return node;
}

static AstNode *parse_for_statement(Parser *parser) {
    Token for_token = parser->current_token;

    /* Optional parentheses: for (i in range(...)) */
    bool has_parentheses = peek_token_is(parser, TOKEN_LEFT_PARENTHESIS);
    if (has_parentheses) next_token(parser);

    if (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_BLANK)) {
        next_token(parser);  /* advance: current_token = IDENT or BLANK */
        if (peek_token_is(parser, TOKEN_IN)) {
            /* --- iteration form: for x in range(...) { } --- */
            /* 'for x in ...' is only valid with range().
             * For collection iteration, users must use for_each. */
            const char *loop_variable_name = parser->current_token.literal;
            AstNode *node = ast_allocate(parser->arena, NODE_FOR_STATEMENT, for_token);
            node->data.for_statement.variable_name = loop_variable_name;
            node->data.for_statement.variable_type = NULL;
            next_token(parser);  /* consume IN */
            next_token(parser);  /* advance to iterable start */
            if (!current_token_is(parser, TOKEN_RANGE)) {
                char message[MESSAGE_BUFFER_SIZE];
                snprintf(message, sizeof(message),
                    "'for %s in ...' only supports 'range()'; use 'for_each %s in ...' to iterate over a collection",
                    loop_variable_name, loop_variable_name);
                diagnostic_error_message(parser->diagnostics, "E2002", arena_copy_string(parser->arena, message),
                    parser->file, for_token.line, for_token.column, 0);
                synchronize_parser(parser);
                return NULL;
            }
            node->data.for_statement.iterable = parse_expression(parser, PRECEDENCE_LOWEST);
            if (has_parentheses && peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) next_token(parser);
            if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
            node->data.for_statement.body = parse_block_statement(parser);
            return node;
        }
        /* else: current_token = IDENT, fall through to while-style */
    } else {
        next_token(parser);  /* advance to condition start token */
        /* `for <keyword> in ...` — keyword used as a loop variable name.
         * (A while-style condition may legitimately start with a keyword
         * such as `true`, so only a following `in` marks a binding.) */
        if (is_keyword_token(parser->current_token.type) && peek_token_is(parser, TOKEN_IN)) {
            reject_keyword_as_name(parser, &parser->current_token, "a loop variable name");
            return NULL;
        }
    }

    /* --- while-style: for condition { } --- */
    AstNode *while_node = ast_allocate(parser->arena, NODE_WHILE_STATEMENT, for_token);
    while_node->data.while_statement.condition = parse_expression(parser, PRECEDENCE_LOWEST);
    if (has_parentheses && peek_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) next_token(parser);
    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    while_node->data.while_statement.body = parse_block_statement(parser);
    return while_node;
}

static AstNode *parse_for_each_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_FOR_EACH_STATEMENT, parser->current_token);

    next_token(parser);

    /* Optional parentheses: for_each (val in arr) {} */
    bool has_parentheses = current_token_is(parser, TOKEN_LEFT_PARENTHESIS);
    if (has_parentheses) next_token(parser);

    if (reject_keyword_as_name(parser, &parser->current_token, "a loop variable name")) return NULL;
    node->data.for_each.index_name = NULL;
    node->data.for_each.variable_name = parser->current_token.literal;

    /* Check for index, value pattern: for_each i, item in collection */
    if (peek_token_is(parser, TOKEN_COMMA)) {
        node->data.for_each.index_name = node->data.for_each.variable_name;
        next_token(parser); /* skip comma */
        next_token(parser);
        if (reject_keyword_as_name(parser, &parser->current_token, "a loop variable name")) return NULL;
        node->data.for_each.variable_name = parser->current_token.literal;
    }

    if (!expect_peek_token(parser, TOKEN_IN)) return NULL;

    next_token(parser);
    /* Parse collection carefully; prevent struct literal parser from consuming
     * the block-opening { after identifiers or module.Name expressions */
    if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_LEFT_BRACE)) {
        /* Bare identifier followed by {; parse as label only */
        node->data.for_each.collection = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
        node->data.for_each.collection->data.label.value = parser->current_token.literal;
    } else if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_DOT)) {
        /* Chained member access: a.b.c; parse manually to avoid
         * parse_expression triggering struct literal parsing on { */
        AstNode *result = ast_allocate(parser->arena, NODE_LABEL, parser->current_token);
        result->data.label.value = parser->current_token.literal;
        while (peek_token_is(parser, TOKEN_DOT)) {
            next_token(parser); /* consume . */
            next_token(parser); /* move to member */
            AstNode *member = ast_allocate(parser->arena, NODE_MEMBER_EXPRESSION, parser->current_token);
            member->data.member.object = result;
            member->data.member.member = parser->current_token.literal;
            result = member;
        }
        /* If the chain is followed by (, it is a function call (e.g., maps.get_keys(m)) */
        if (peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            next_token(parser); /* move to ( */
            result = parse_call_expression(parser, result);
        }
        node->data.for_each.collection = result;
    } else {
        node->data.for_each.collection = parse_expression(parser, PRECEDENCE_LOWEST);
    }

    if (has_parentheses) {
        if (!expect_peek_token(parser, TOKEN_RIGHT_PARENTHESIS)) return NULL;
    }

    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    node->data.for_each.body = parse_block_statement(parser);

    return node;
}

static AstNode *parse_while_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_WHILE_STATEMENT, parser->current_token);

    next_token(parser);
    bool saved_should_suppress_struct_literal = parser->should_suppress_struct_literal;
    parser->should_suppress_struct_literal = true;
    node->data.while_statement.condition = parse_expression(parser, PRECEDENCE_LOWEST);
    parser->should_suppress_struct_literal = saved_should_suppress_struct_literal;

    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    node->data.while_statement.body = parse_block_statement(parser);

    return node;
}

static AstNode *parse_loop_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_LOOP_STATEMENT, parser->current_token);

    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    node->data.loop_statement.body = parse_block_statement(parser);

    return node;
}

/* Check if current position holds IDENT(IDENT, ..., IDENT).
 * Assumes current_token is the IDENT before LPAREN and peek is LPAREN.
 * Consumes tokens past the closing paren (caller must restore). */
static bool scan_parenthesized_bindings(Parser *parser) {
    next_token(parser); /* skip IDENT */
    next_token(parser); /* skip ( */
    int binding_count = 0;
    while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
        if (!current_token_is(parser, TOKEN_IDENTIFIER)) return false;
        binding_count++;
        next_token(parser);
        if (current_token_is(parser, TOKEN_COMMA)) next_token(parser);
    }
    return binding_count > 0 && current_token_is(parser, TOKEN_RIGHT_PARENTHESIS);
}

static AstNode *parse_when_statement(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_WHEN_STATEMENT, parser->current_token);

    next_token(parser);
    parser->should_suppress_struct_literal = true;
    node->data.when_statement.value = parse_expression(parser, PRECEDENCE_LOWEST);
    parser->should_suppress_struct_literal = false;
    node->data.when_statement.is_strict = false;
    node->data.when_statement.default_body = NULL;

    if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
    next_token(parser); /* skip { */

    int case_capacity = GROW_ARRAY_INITIAL_CAPACITY;
    node->data.when_statement.case_count = 0;
    node->data.when_statement.cases = arena_allocate(parser->arena, sizeof(WhenCase) * case_capacity);

    while (!current_token_is(parser, TOKEN_RIGHT_BRACE) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
        if (current_token_is(parser, TOKEN_IS)) {
            ARENA_GROW(parser->arena, node->data.when_statement.cases,
                node->data.when_statement.case_count, case_capacity);

            WhenCase *when_case = &node->data.when_statement.cases[node->data.when_statement.case_count];
            memset(when_case, 0, sizeof(WhenCase));
            when_case->keyword_token = parser->current_token;

            int value_capacity = GROW_ARRAY_INITIAL_CAPACITY;
            when_case->value_count = 0;
            when_case->values = arena_allocate(parser->arena, sizeof(AstNode *) * value_capacity);
            when_case->is_range = false;

            /* Parse case values: is 1, 2, 3 { } */
            next_token(parser);

            /* Detect destructuring pattern: IDENT(IDENT,...) or .IDENT(IDENT,...)
             * All tokens inside parens must be bare identifiers (no operators/literals). */
            {
                bool is_pattern_candidate = false;
                bool is_implicit_pattern = false;
                bool is_explicit_enum = false;
                bool is_qualified_enum = false;
                Token pattern_token = parser->current_token;

                if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                    /* IDENT(IDENT,...) pattern form */
                    ParserSnapshot snapshot;
                    parser_snapshot_save(parser, &snapshot);
                    is_pattern_candidate = scan_parenthesized_bindings(parser);
                    parser_snapshot_restore(parser, &snapshot);
                } else if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_DOT)) {
                    /* IDENT.IDENT(IDENT,...) explicit enum pattern form, or
                     * mod.IDENT.IDENT(IDENT,...) for an imported enum */
                    ParserSnapshot snapshot;
                    parser_snapshot_save(parser, &snapshot);
                    next_token(parser); /* skip enum name */
                    next_token(parser); /* skip DOT */
                    if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_DOT)) {
                        next_token(parser); /* skip enum name */
                        next_token(parser); /* skip DOT */
                        is_qualified_enum = true;
                    }
                    if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                        is_pattern_candidate = scan_parenthesized_bindings(parser);
                        if (is_pattern_candidate) is_explicit_enum = true;
                    }
                    if (!is_pattern_candidate) is_qualified_enum = false;
                    parser_snapshot_restore(parser, &snapshot);
                } else if (current_token_is(parser, TOKEN_DOT) && peek_token_is(parser, TOKEN_IDENTIFIER)) {
                    /* .IDENT(IDENT,...) implicit pattern form */
                    ParserSnapshot snapshot;
                    parser_snapshot_save(parser, &snapshot);
                    next_token(parser); /* skip dot */
                    if (peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                        is_pattern_candidate = scan_parenthesized_bindings(parser);
                        if (is_pattern_candidate) is_implicit_pattern = true;
                    }
                    parser_snapshot_restore(parser, &snapshot);
                }

                if (is_pattern_candidate) {
                    /* Actually consume and build NODE_WHEN_PATTERN */
                    AstNode *pattern = ast_allocate(parser->arena, NODE_WHEN_PATTERN, pattern_token);
                    pattern->data.when_pattern.is_implicit = is_implicit_pattern;

                    if (is_explicit_enum) {
                        pattern->data.when_pattern.enum_name = arena_copy_string(parser->arena, parser->current_token.literal);
                        next_token(parser); /* skip enum name (or module) */
                        next_token(parser); /* skip dot */
                        if (is_qualified_enum) {
                            char qualified[MESSAGE_BUFFER_SIZE];
                            snprintf(qualified, sizeof(qualified), "%s.%s",
                                pattern->data.when_pattern.enum_name, parser->current_token.literal);
                            pattern->data.when_pattern.enum_name = arena_copy_string(parser->arena, qualified);
                            next_token(parser); /* skip enum name */
                            next_token(parser); /* skip dot */
                        }
                    } else {
                        pattern->data.when_pattern.enum_name = NULL;
                        if (is_implicit_pattern) {
                            next_token(parser); /* skip dot */
                        }
                    }
                    pattern->data.when_pattern.variant = arena_copy_string(parser->arena, parser->current_token.literal);
                    next_token(parser); /* skip IDENT (variant) */
                    next_token(parser); /* skip ( */

                    int binding_count = 0, binding_capacity = GROW_ARRAY_INITIAL_CAPACITY;
                    pattern->data.when_pattern.bindings = arena_allocate(parser->arena, sizeof(const char *) * binding_capacity);
                    while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
                        ARENA_GROW(parser->arena, pattern->data.when_pattern.bindings, binding_count, binding_capacity);
                        /* Reject reserved names as binding names */
                        if (is_reserved_name(parser->current_token.literal)) {
                            char message[MESSAGE_BUFFER_SIZE];
                            snprintf(message, sizeof(message),
                                "'%s' is a built-in name and cannot be used as a binding name",
                                parser->current_token.literal);
                            diagnostic_error_message(parser->diagnostics, "E4028", arena_copy_string(parser->arena, message),
                                parser->file, parser->current_token.line, parser->current_token.column, 0);
                        }
                        pattern->data.when_pattern.bindings[binding_count++] = arena_copy_string(parser->arena, parser->current_token.literal);
                        next_token(parser);
                        if (current_token_is(parser, TOKEN_COMMA)) next_token(parser);
                    }
                    pattern->data.when_pattern.binding_count = binding_count;
                    /* current_token is RPAREN, peek should be LBRACE */

                    when_case->values[when_case->value_count++] = pattern;
                    goto when_case_body;
                }
            }

            if (current_token_is(parser, TOKEN_RANGE)) {
                when_case->is_range = true;
            }
            parser->should_suppress_struct_literal = true;
            if (when_case->value_count < value_capacity) {
                when_case->values[when_case->value_count++] = parse_expression(parser, PRECEDENCE_LOWEST);
            }
            while (peek_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* skip comma */
                next_token(parser); /* next value */
                ARENA_GROW(parser->arena, when_case->values, when_case->value_count, value_capacity);
                if (current_token_is(parser, TOKEN_RANGE)) {
                    when_case->is_range = true;
                }
                when_case->values[when_case->value_count++] = parse_expression(parser, PRECEDENCE_LOWEST);
            }

            when_case_body:
            parser->should_suppress_struct_literal = false;
            if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
            when_case->body = parse_block_statement(parser);
            node->data.when_statement.case_count++;

        } else if (current_token_is(parser, TOKEN_DEFAULT)) {
            if (node->data.when_statement.default_body) {
                diagnostic_error_code(parser->diagnostics, "E2085", parser->file, parser->current_token.line, parser->current_token.column, 0);
            }
            if (!expect_peek_token(parser, TOKEN_LEFT_BRACE)) return NULL;
            node->data.when_statement.default_body = parse_block_statement(parser);
        }

        next_token(parser);
    }

    /* E2059: empty when block */
    if (node->data.when_statement.case_count == 0 && node->data.when_statement.default_body == NULL) {
        diagnostic_error_code(parser->diagnostics, "E2059", parser->file, node->token.line, node->token.column, 0);
    }

    return node;
}

static AstNode *parse_alias_declaration(Parser *parser) {
    AstNode *node = ast_allocate(parser->arena, NODE_ALIAS_DECLARATION, parser->current_token);

    if (!expect_peek_token(parser, TOKEN_IDENTIFIER)) return NULL;
    node->data.alias_declaration.name = parser->current_token.literal;
    node->data.alias_declaration.is_private = false;

    if (!expect_peek_token(parser, TOKEN_ASSIGN)) return NULL;
    next_token(parser); /* advance to the type */

    /* E3134: detect module-qualified type (Ident.Ident) before parse_complex_type
     * rewrites it with underscore prefixing. */
    if (current_token_is(parser, TOKEN_IDENTIFIER) && peek_token_is(parser, TOKEN_DOT)) {
        char message[MESSAGE_BUFFER_SIZE];
        snprintf(message, sizeof(message),
            "alias '%s' cannot target a module-qualified type; only local types can be aliased",
            node->data.alias_declaration.name);
        diagnostic_error_message(parser->diagnostics, "E3134", arena_copy_string(parser->arena, message),
            parser->file, parser->current_token.line, parser->current_token.column, 0);
        synchronize_parser(parser);
        return NULL;
    }

    const char *target = parse_complex_type(parser);
    if (!target) return NULL;
    node->data.alias_declaration.target_type = target;
    return node;
}

static bool is_assignment_operator(TokenType type) {
    return type == TOKEN_ASSIGN || type == TOKEN_PLUS_ASSIGN || type == TOKEN_MINUS_ASSIGN ||
           type == TOKEN_ASTERISK_ASSIGN || type == TOKEN_SLASH_ASSIGN || type == TOKEN_PERCENT_ASSIGN;
}

static AstNode *parse_statement(Parser *parser) {
    switch (parser->current_token.type) {
    case TOKEN_PRIVATE: {
        /* private do / private const / private mut; consume and set flag */
        next_token(parser);
        AstNode *statement = parse_statement(parser);
        if (statement) {
            if (statement->kind == NODE_FUNCTION_DECLARATION) {
                statement->data.function_declaration.is_private = true;
            } else if (statement->kind == NODE_VARIABLE_DECLARATION) {
                statement->data.variable_declaration.is_private = true;
            } else if (statement->kind == NODE_ALIAS_DECLARATION) {
                statement->data.alias_declaration.is_private = true;
            } else if (statement->kind == NODE_STRUCT_DECLARATION) {
                statement->data.struct_declaration.is_private = true;
            } else if (statement->kind == NODE_ENUM_DECLARATION) {
                statement->data.enum_declaration.is_private = true;
            }
        }
        return statement;
    }
    case TOKEN_MUT:
    case TOKEN_CONST:
        /* Check for keyword used as name: const for struct / mut for i64 */
        if (reject_keyword_as_name(parser, &parser->peek_token, "a name")) return NULL;
        /* Check if this is a struct or enum declaration: const Name struct { */
        if (parser->current_token.type == TOKEN_CONST && peek_token_is(parser, TOKEN_IDENTIFIER)) {
            ParserSnapshot snapshot;
            parser_snapshot_save(parser, &snapshot);

            next_token(parser); /* now on IDENT (name) */
            if (peek_token_is(parser, TOKEN_STRUCT)) {
                return parse_struct_declaration(parser);
            }
            if (peek_token_is(parser, TOKEN_ENUM)) {
                return parse_enum_declaration(parser);
            }
            parser_snapshot_restore(parser, &snapshot); /* not struct/enum */
        }
        return parse_variable_declaration(parser);
    case TOKEN_DO:
        return parse_function_declaration(parser);
    case TOKEN_RETURN:
        return parse_return_statement(parser);
    case TOKEN_IMPORT:
        return parse_import_statement(parser);
    case TOKEN_USING:
        return parse_using_statement(parser);
    case TOKEN_IF:
        return parse_if_statement(parser);
    case TOKEN_FOR:
        return parse_for_statement(parser);
    case TOKEN_FOR_EACH:
        return parse_for_each_statement(parser);
    case TOKEN_AS_LONG_AS:
        return parse_while_statement(parser);
    case TOKEN_LOOP:
        return parse_loop_statement(parser);
    case TOKEN_BREAK:
        return ast_allocate(parser->arena, NODE_BREAK_STATEMENT, parser->current_token);
    case TOKEN_CONTINUE:
        return ast_allocate(parser->arena, NODE_CONTINUE_STATEMENT, parser->current_token);
    case TOKEN_WHEN:
        return parse_when_statement(parser);
    case TOKEN_ALIAS:
        return parse_alias_declaration(parser);
    case TOKEN_STRICT: {
        /* #strict; applies to the next when statement */
        bool is_duplicate = reject_duplicate_attribute(parser, ATTRIBUTE_STRICT, "#strict");
        next_token(parser);
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_WHEN_STATEMENT) {
            statement->data.when_statement.is_strict = true;
        } else if (!is_duplicate) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#strict attribute can only be applied to when statements"), parser->current_token.line, parser->current_token.column);
        }
        return statement;
    }
    case TOKEN_FLAGS: {
        /* #flags; applies to the next enum declaration */
        reject_duplicate_attribute(parser, ATTRIBUTE_FLAGS, "#flags");
        next_token(parser); /* skip #flags */
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_ENUM_DECLARATION) {
            statement->data.enum_declaration.is_flags = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#flags attribute can only be applied to enum declarations"), parser->current_token.line, parser->current_token.column);
        }
        return statement;
    }
    case TOKEN_ERROR_CODE_ATTRIBUTE: {
        /* #error_code; contributes an enum's variants to the ErrorCode set */
        reject_duplicate_attribute(parser, ATTRIBUTE_ERROR_CODE, "#error_code");
        next_token(parser); /* skip #error_code */
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_ENUM_DECLARATION) {
            statement->data.enum_declaration.is_error_code = true;
        } else {
            diagnostic_error_code(parser->diagnostics, "E3144",
                parser->file, parser->current_token.line, parser->current_token.column, 0);
        }
        return statement;
    }
    case TOKEN_JSON_ATTRIBUTE: {
        /* #json; applies to the next struct declaration */
        reject_duplicate_attribute(parser, ATTRIBUTE_JSON, "#json");
        next_token(parser);
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_STRUCT_DECLARATION) {
            statement->data.struct_declaration.is_json = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena,"#json attribute can only be applied to struct declarations"), parser->current_token.line, parser->current_token.column);
        }
        return statement;
    }
    case TOKEN_DISCARD: {
        /* #discard; applies to the next function declaration */
        reject_duplicate_attribute(parser, ATTRIBUTE_DISCARD, "#discard");
        next_token(parser);
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_FUNCTION_DECLARATION) {
            statement->data.function_declaration.is_discard = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#discard attribute can only be applied to function declarations"), parser->current_token.line, parser->current_token.column);
        }
        return statement;
    }
    case TOKEN_TEST: {
        /* #test; applies to the next function declaration */
        reject_duplicate_attribute(parser, ATTRIBUTE_TEST, "#test");
        next_token(parser);
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_FUNCTION_DECLARATION) {
            statement->data.function_declaration.is_test = true;
        } else {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#test attribute can only be applied to function declarations"), parser->current_token.line, parser->current_token.column);
        }
        return statement;
    }
    case TOKEN_DEPRECATED: {
        /* #deprecated or #deprecated("message"); applies to the next
         * function, struct, or enum declaration. */
        bool is_duplicate = reject_duplicate_attribute(parser, ATTRIBUTE_DEPRECATED, "#deprecated");
        next_token(parser); /* consume #deprecated */
        const char *message = NULL;
        if (current_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            next_token(parser); /* consume ( */
            if (current_token_is(parser, TOKEN_STRING)) {
                message = arena_copy_string(parser->arena, parser->current_token.literal);
                next_token(parser); /* consume string */
            } else {
                emit_attribute_error(parser, arena_copy_string(parser->arena, "#deprecated expects a string literal message, e.g. #deprecated(\"use x() instead\")"), parser->current_token.line, parser->current_token.column);
            }
            if (current_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
                next_token(parser); /* consume ) */
            } else {
                emit_attribute_error(parser, arena_copy_string(parser->arena, "expected ')' after #deprecated message"), parser->current_token.line, parser->current_token.column);
            }
        }
        AstNode *statement = parse_statement(parser);
        if (statement && statement->kind == NODE_FUNCTION_DECLARATION) {
            statement->data.function_declaration.is_deprecated = true;
            if (!is_duplicate) statement->data.function_declaration.deprecated_message = message;
        } else if (statement && statement->kind == NODE_STRUCT_DECLARATION) {
            statement->data.struct_declaration.is_deprecated = true;
            if (!is_duplicate) statement->data.struct_declaration.deprecated_message = message;
        } else if (statement && statement->kind == NODE_ENUM_DECLARATION) {
            statement->data.enum_declaration.is_deprecated = true;
            if (!is_duplicate) statement->data.enum_declaration.deprecated_message = message;
        } else if (!is_duplicate) {
            emit_attribute_error(parser, arena_copy_string(parser->arena, "#deprecated attribute can only be applied to function, struct, or enum declarations"), parser->current_token.line, parser->current_token.column);
        }
        return statement;
    }
    case TOKEN_DOC:
        /* Skip #doc attribute tokens; consume args if present */
        reject_duplicate_attribute(parser, ATTRIBUTE_DOC, "#doc");
        if (peek_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
            next_token(parser);
            while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
                next_token(parser);
            }
        }
        next_token(parser);
        return parse_statement(parser);
    case TOKEN_HASH_LEFT_BRACKET: {
        /* Single-line attribute list: `#[a, b, c("arg")]`. Additive sugar for
         * the stacked `#attr` form — each entry is validated against the
         * following declaration exactly as if it had been stacked.
         *
         * The list must stay on one physical line for now. If the attribute
         * set ever grows enough that one line becomes unwieldy, a multi-line
         * form can be permitted here. */
        Token list_start_token = parser->current_token;
        next_token(parser); /* consume '#[' */

        const char *names[MAX_ATTRIBUTE_LIST_ENTRIES];
        const char *deprecated_messages[MAX_ATTRIBUTE_LIST_ENTRIES];
        Token sites[MAX_ATTRIBUTE_LIST_ENTRIES];
        int count = 0;
        int seen_count = 0;
        bool is_malformed = false;

        while (!current_token_is(parser, TOKEN_RIGHT_BRACKET) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
            /* `#[#test]` — an inner '#' on an entry. */
            if (current_token_is(parser, TOKEN_STRICT) || current_token_is(parser, TOKEN_FLAGS) ||
                current_token_is(parser, TOKEN_DOC)    || current_token_is(parser, TOKEN_JSON_ATTRIBUTE) ||
                current_token_is(parser, TOKEN_DISCARD)|| current_token_is(parser, TOKEN_DEPRECATED) ||
                current_token_is(parser, TOKEN_TEST)) {
                diagnostic_error_code_help(parser->diagnostics, "E2093",
                    parser->file, parser->current_token.line, parser->current_token.column, 0,
                    "write attributes without '#' inside '#[...]'");
                is_malformed = true;
                break;
            }
            if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
                diagnostic_error_code_help(parser->diagnostics, "E2093",
                    parser->file, parser->current_token.line, parser->current_token.column, 0,
                    "expected an attribute name");
                is_malformed = true;
                break;
            }
            if (parser->current_token.line != list_start_token.line) {
                diagnostic_error_code(parser->diagnostics, "E2092",
                    parser->file, parser->current_token.line, parser->current_token.column, 0);
                is_malformed = true;
                break;
            }

            Token site = parser->current_token;
            const char *attribute_name = parser->current_token.literal;
            AttributeBit bit = attribute_bit_for_name(attribute_name);
            if (bit == (AttributeBit)0) {
                diagnostic_error_code_formatted(parser->diagnostics, "E2091",
                    parser->file, site.line, site.column, 0, attribute_name);
                is_malformed = true;
                break;
            }
            next_token(parser); /* consume the name */

            const char *deprecated_message = NULL;
            if (current_token_is(parser, TOKEN_LEFT_PARENTHESIS)) {
                /* Entry arguments are validated exactly as the stacked form
                 * does: only 'deprecated' and 'doc' accept a '(...)', and
                 * 'deprecated' requires exactly one string literal. */
                if (strcmp(attribute_name, "deprecated") == 0) {
                    next_token(parser); /* consume '(' */
                    if (!current_token_is(parser, TOKEN_STRING)) {
                        diagnostic_error_code_help(parser->diagnostics, "E2093",
                            parser->file, parser->current_token.line, parser->current_token.column, 0,
                            "'deprecated' takes exactly one string literal, e.g. deprecated(\"use x() instead\")");
                        is_malformed = true;
                        break;
                    }
                    deprecated_message = arena_copy_string(parser->arena, parser->current_token.literal);
                    next_token(parser); /* consume the string */
                    if (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
                        diagnostic_error_code_help(parser->diagnostics, "E2093",
                            parser->file, parser->current_token.line, parser->current_token.column, 0,
                            "'deprecated' takes exactly one string literal");
                        is_malformed = true;
                        break;
                    }
                    next_token(parser); /* consume ')' */
                } else if (strcmp(attribute_name, "doc") == 0) {
                    /* doc() args are accepted and discarded, as in the stacked form. */
                    next_token(parser); /* consume '(' */
                    while (!current_token_is(parser, TOKEN_RIGHT_PARENTHESIS) && !current_token_is(parser, TOKEN_END_OF_FILE)) {
                        next_token(parser);
                    }
                    if (current_token_is(parser, TOKEN_RIGHT_PARENTHESIS)) {
                        next_token(parser); /* consume ')' */
                    }
                } else {
                    char help[48];
                    snprintf(help, sizeof(help), "'%s' takes no arguments", attribute_name);
                    diagnostic_error_code_help(parser->diagnostics, "E2093",
                        parser->file, parser->current_token.line, parser->current_token.column, 0,
                        arena_copy_string(parser->arena, help));
                    is_malformed = true;
                    break;
                }
            }

            seen_count++;

            char canonical_name[24];
            snprintf(canonical_name, sizeof(canonical_name), "#%s", attribute_name);
            if (!reject_duplicate_attribute(parser, bit, arena_copy_string(parser->arena, canonical_name)) && count < MAX_ATTRIBUTE_LIST_ENTRIES) {
                names[count]    = arena_copy_string(parser->arena, attribute_name);
                deprecated_messages[count] = deprecated_message;
                sites[count]    = site;
                count++;
            }

            if (current_token_is(parser, TOKEN_COMMA)) {
                next_token(parser); /* consume ',' */
                if (current_token_is(parser, TOKEN_RIGHT_BRACKET)) {
                    diagnostic_error_code_help(parser->diagnostics, "E2093",
                        parser->file, parser->current_token.line, parser->current_token.column, 0,
                        "remove the trailing ',' before ']'");
                    is_malformed = true;
                    break;
                }
                continue;
            }
            if (current_token_is(parser, TOKEN_RIGHT_BRACKET)) {
                break;
            }
            diagnostic_error_code_help(parser->diagnostics, "E2093",
                parser->file, parser->current_token.line, parser->current_token.column, 0,
                "expected ',' or ']' after an attribute");
            is_malformed = true;
            break;
        }

        /* The closing ']' must sit on the opening line too — the in-loop check
         * only sees attribute-name tokens, so `#[flags\n]` and `#[a, b\n]`
         * would otherwise slip through. */
        if (!is_malformed && current_token_is(parser, TOKEN_RIGHT_BRACKET) &&
            parser->current_token.line != list_start_token.line) {
            diagnostic_error_code(parser->diagnostics, "E2092",
                parser->file, parser->current_token.line, parser->current_token.column, 0);
            is_malformed = true;
        }

        if (!is_malformed && seen_count == 0 && current_token_is(parser, TOKEN_RIGHT_BRACKET)) {
            diagnostic_error_code_help(parser->diagnostics, "E2093",
                parser->file, list_start_token.line, list_start_token.column, 0,
                "'#[...]' cannot be empty; list at least one attribute");
            is_malformed = true;
        }

        if (current_token_is(parser, TOKEN_RIGHT_BRACKET)) {
            next_token(parser); /* consume ']' */
        }

        /* On a malformed list, skip ahead to the declaration keyword so the
         * rest of the file still parses without a diagnostic cascade. */
        if (is_malformed) {
            while (!current_token_is(parser, TOKEN_END_OF_FILE) &&
                   !current_token_is(parser, TOKEN_DO) &&
                   !current_token_is(parser, TOKEN_CONST) &&
                   !current_token_is(parser, TOKEN_WHEN) &&
                   !current_token_is(parser, TOKEN_MUT)) {
                next_token(parser);
            }
        }

        AstNode *statement = parse_statement(parser);
        for (int i = 0; i < count; i++) {
            apply_named_attribute(parser, statement, names[i], deprecated_messages[i], sites[i]);
        }
        return statement;
    }
    case TOKEN_ENSURE:
        return parse_ensure_statement(parser);
    case TOKEN_BLANK:
        /* Bare throwaway: `_ = expr` discards the right side without creating
         * a symbol. `_, x = func()` is a bare multi-var declaration. */
        if (peek_token_is(parser, TOKEN_ASSIGN)) {
            return parse_discard_statement(parser);
        }
        if (peek_token_is(parser, TOKEN_COMMA)) {
            return parse_bare_variable_declaration(parser);
        }
        diagnostic_error_message(parser->diagnostics, "E2002",
            arena_copy_string(parser->arena,"unexpected token '_'; the throwaway '_' is only valid as the entire left-hand side of an assignment"),
            parser->file, parser->current_token.line, parser->current_token.column, 0);
        synchronize_parser(parser);
        return NULL;
    case TOKEN_EXTERN:
        if (peek_token_is(parser, TOKEN_IMPORT)) {
            return parse_import_statement(parser);
        }
        /* Not an import; parse as an expression statement (e.g. extern.printf(...)). */
        /* fallthrough */
    default: {
        /* Bare variable declaration: x i64 = 5  or  x, err = func()
         * Also handles array types: x [i64] = {1,2,3}
         * Whitespace before '[' disambiguates from index expressions (E2075).
         * The name and what follows it must share a line, so a bare
         * identifier cannot swallow the next statement's first token. */
        if (current_token_is(parser, TOKEN_IDENTIFIER) &&
            parser->peek_token.line == parser->current_token.line &&
            (peek_token_is(parser, TOKEN_IDENTIFIER) || peek_token_is(parser, TOKEN_COMMA) ||
             (peek_token_is(parser, TOKEN_LEFT_BRACKET) && parser->peek_token.is_preceded_by_whitespace))) {
            return parse_bare_variable_declaration(parser);
        }
        /* Could be assignment or expression statement */
        AstNode *expression = parse_expression(parser, PRECEDENCE_LOWEST);
        if (!expression) return NULL;

        /* Check for assignment */
        if (is_assignment_operator(parser->peek_token.type)) {
            next_token(parser);
            AstNode *node = ast_allocate(parser->arena, NODE_ASSIGN_STATEMENT, parser->current_token);
            node->data.assign.target = expression;
            node->data.assign.operator = parser->current_token.type;
            next_token(parser);
            node->data.assign.value = parse_expression(parser, PRECEDENCE_LOWEST);
            return node;
        }

        /* Bare `call() or_return`: no bindings, just propagate the trailing
         * error from the call's return tuple. */
        if (peek_token_is(parser, TOKEN_OR_RETURN)) {
            next_token(parser); /* consume or_return */
            AstNode *fallback_values[MAX_MULTI_VARIABLES];
            int fallback_count = parse_or_return_fallbacks(parser, fallback_values);
            char *temporary_name = make_or_return_temporary_name(parser->arena);

            AstNode *block = ast_allocate(parser->arena, NODE_BLOCK_STATEMENT, parser->current_token);
            block->data.block.capacity = 2;
            block->data.block.count = 0;
            block->data.block.statements = arena_allocate(parser->arena, sizeof(AstNode *) * block->data.block.capacity);

            block->data.block.statements[block->data.block.count++] =
                make_synthetic_temporary_declaration(parser, temporary_name, expression);

            block->data.block.statements[block->data.block.count++] =
                build_or_return_guard(parser, temporary_name, fallback_values, fallback_count);
            return block;
        }

        AstNode *node = ast_allocate(parser->arena, NODE_EXPRESSION_STATEMENT, parser->current_token);
        node->data.expression_statement.expression = expression;
        return node;
    }
    }
}

/* --- Public API --- */

Parser *parser_create(Arena *arena, Lexer *lexer, const char *file, DiagnosticList *diagnostics) {
    Parser *parser = arena_allocate(arena, sizeof(Parser));
    parser->lexer = lexer;
    parser->arena = arena;
    parser->file = file;
    parser->diagnostics = diagnostics;
    parser->depth = 0;
    parser->should_suppress_struct_literal = false;
    parser->is_in_interpolation = false;
    parser->current_function = NULL;
    parser->seen_attribute_mask = 0;

    /* Read two tokens to fill cur and peek */
    next_token(parser);
    next_token(parser);

    return parser;
}

AstNode *parser_parse_program(Parser *parser) {
    Token program_token = {TOKEN_END_OF_FILE, "", 0, 0, NULL, false};
    AstNode *program = ast_allocate(parser->arena, NODE_PROGRAM, program_token);
    program->data.program.module_declaration = NULL;
    program->data.program.using_statements = NULL;
    program->data.program.using_count = 0;
    program->data.program.statement_count = 0;
    program->data.program.statement_capacity = GROW_ARRAY_INITIAL_CAPACITY;
    program->data.program.statements = arena_allocate(parser->arena,
        sizeof(AstNode *) * program->data.program.statement_capacity);

    while (!current_token_is(parser, TOKEN_END_OF_FILE)) {
        parser->seen_attribute_mask = 0;
        AstNode *statement = parse_statement(parser);
        if (statement) {
            ARENA_GROW(parser->arena, program->data.program.statements,
                program->data.program.statement_count, program->data.program.statement_capacity);
            program->data.program.statements[program->data.program.statement_count++] = statement;
        } else {
            /* Error recovery: skip to next statement boundary */
            synchronize_parser(parser);
            continue;
        }
        next_token(parser);
    }

    return program;
}
