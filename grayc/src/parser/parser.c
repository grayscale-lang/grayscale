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

#define MAX_MULTI_VARS 16
#define MAX_SHARED_RETURNS 16
#define FLOAT_LIT_BUF    128
#define TMP_NAME_BUF     32
#define FIELD_NAME_BUF           8

/* Operator precedence levels */
typedef enum {
    PREC_LOWEST,
    PREC_OR,            /* || */
    PREC_AND,           /* && */
    PREC_EQUALS,        /* == != */
    PREC_BITWISE,       /* bit_and, bit_or, bit_xor — above == so a bit_and b == c → (a bit_and b) == c */
    PREC_LESSGREATER,   /* > < >= <= */
    PREC_MEMBERSHIP,    /* in, not_in */
    PREC_SHIFT,         /* bit_shift_left, bit_shift_right */
    PREC_SUM,           /* + - */
    PREC_PRODUCT,       /* * / % */
    PREC_PREFIX,        /* -x !x bit_not x */
    PREC_CALL,          /* f(x) */
    PREC_INDEX,         /* a[i] a.b */
    PREC_POSTFIX,       /* x++ x-- */
} Precedence;

/* Forward declarations */
static AstNode *parse_statement(Parser *parser);
static AstNode *parse_expression(Parser *parser, Precedence prec);
static AstNode *parse_block_statement(Parser *parser);
static AstNode *parse_struct_literal(Parser *parser, const char *name);
static AstNode *maybe_apply_or_return(Parser *parser, AstNode *var_decl);

/* --- Helpers --- */

static void next_token(Parser *parser) {
    parser->cur_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
    /* Surface lexer errors (E1xxx). The lexer does not call diagnostic_error()
     * directly — it sets error_code/error_msg on itself and returns
     * TOK_ILLEGAL. We detect that here and emit the diagnostic so the
     * lexer stays free of diagnostic dependencies. After emitting, clear
     * error_code so the same error is not reported twice. */
    if (parser->peek_token.type == TOK_ILLEGAL && parser->lexer->error_code) {
        diagnostic_error_message(parser->diag, parser->lexer->error_code,
            arena_copy_string(parser->arena, parser->lexer->error_msg),
            parser->file, parser->peek_token.line, parser->peek_token.column, 0);
        parser->lexer->error_code = NULL;
    }
}

static bool current_token_is(Parser *parser, TokenType type) {
    return parser->cur_token.type == type;
}

static bool peek_token_is(Parser *parser, TokenType type) {
    return parser->peek_token.type == type;
}

static bool expect_peek_token(Parser *parser, TokenType type) {
    if (peek_token_is(parser, type)) {
        next_token(parser);
        return true;
    }
    char buf[MSG_BUF_SIZE];
    snprintf(buf, sizeof(buf), "expected '%s', got '%s'",
        token_type_name(type), token_display_name(parser->peek_token));
    /* Point at current token (where the expected token should be), not the peek token */
    diagnostic_error_message(parser->diag, "E2001", arena_copy_string(parser->arena, buf),
        parser->file, parser->cur_token.line, parser->cur_token.column, 0);
    return false;
}

/* Check if a token type is a keyword (reserved word) */
static bool is_keyword_token(TokenType type) {
    switch (type) {
    case TOK_MUT: case TOK_CONST: case TOK_DO: case TOK_RETURN:
    case TOK_IF: case TOK_OR_KW: case TOK_OTHERWISE:
    case TOK_FOR: case TOK_FOR_EACH: case TOK_AS_LONG_AS:
    case TOK_LOOP: case TOK_BREAK: case TOK_CONTINUE:
    case TOK_IN: case TOK_NOT_IN: case TOK_RANGE:
    case TOK_IMPORT: case TOK_USING: case TOK_STRUCT: case TOK_ENUM:
    case TOK_NIL: case TOK_NEW: case TOK_TRUE: case TOK_FALSE:
    case TOK_ENSURE: case TOK_OR_RETURN: case TOK_WHEN:
    case TOK_MODULE: case TOK_PRIVATE: case TOK_ALIAS:
    case TOK_IS: case TOK_DEFAULT:
        return true;
    default:
        return false;
    }
}


/* Synchronize parser after an error; skip to a safe point.
 * Advances past the current line and stops at the next statement boundary. */
static void synchronize_parser(Parser *parser) {
    int error_line = parser->cur_token.line;
    /* First, skip past the current line to avoid re-parsing the same error */
    while (!current_token_is(parser, TOK_EOF) && parser->cur_token.line == error_line) {
        next_token(parser);
    }
    /* Then find the next statement-starting token */
    while (!current_token_is(parser, TOK_EOF)) {
        switch (parser->cur_token.type) {
        case TOK_DO: case TOK_MUT: case TOK_CONST:
        case TOK_RETURN: case TOK_IF: case TOK_FOR:
        case TOK_FOR_EACH: case TOK_AS_LONG_AS: case TOK_LOOP:
        case TOK_WHEN: case TOK_IMPORT: case TOK_USING:
        case TOK_BREAK: case TOK_CONTINUE: case TOK_ALIAS:
        case TOK_RBRACE:
        case TOK_IDENT:
            return;
        default:
            next_token(parser);
        }
    }
}

static Precedence get_token_precedence(TokenType type) {
    switch (type) {
    case TOK_OR:              return PREC_OR;
    case TOK_AND:             return PREC_AND;
    case TOK_EQ:
    case TOK_NOT_EQ:          return PREC_EQUALS;
    case TOK_BIT_AND:
    case TOK_BIT_OR:
    case TOK_BIT_XOR:         return PREC_BITWISE;
    case TOK_LT:
    case TOK_GT:
    case TOK_LT_EQ:
    case TOK_GT_EQ:           return PREC_LESSGREATER;
    case TOK_IN:
    case TOK_NOT_IN:          return PREC_MEMBERSHIP;
    case TOK_BIT_SHIFT_LEFT:
    case TOK_BIT_SHIFT_RIGHT: return PREC_SHIFT;
    case TOK_PLUS:
    case TOK_MINUS:           return PREC_SUM;
    case TOK_ASTERISK:
    case TOK_SLASH:
    case TOK_PERCENT:         return PREC_PRODUCT;
    case TOK_LPAREN:          return PREC_CALL;
    case TOK_LBRACKET:        return PREC_INDEX;
    case TOK_DOT:             return PREC_INDEX;
    case TOK_INCREMENT:
    case TOK_DECREMENT:
    case TOK_CARET:           return PREC_POSTFIX;
    default:                  return PREC_LOWEST;
    }
}

/* --- Expression Parsing --- */

/* Read a type name: simple (int, Person) or qualified (models.Task).
 * Assumes current token is the first identifier. Returns arena-allocated string. */
/* Wildcard type placeholder for generics. Stored as the
 * string "?" in the same slot as any other type name so the rest of
 * the compiler can carry it through unchanged until typechecker
 * instantiation (slice 2) replaces it with a concrete type. */
static bool type_string_has_wildcard(const char *type_name) {
    if (!type_name) return false;
    for (const char *ch = type_name; *ch; ch++) {
        if (*ch == '?') return true;
    }
    return false;
}

static const char *read_type_name(Parser *parser) {
    /* Wildcard type placeholder: `?` in a type position */
    if (current_token_is(parser, TOK_QUESTION)) {
        return "?";
    }
    const char *name = parser->cur_token.literal;
    if (peek_token_is(parser, TOK_DOT)) {
        next_token(parser); /* skip . */
        next_token(parser); /* qualified part */
        size_t nlen = strlen(name), qlen = strlen(parser->cur_token.literal);
        size_t len = nlen + qlen + 2;
        char *qualified = arena_alloc(parser->arena, len);
        /* Use underscore for module-qualified types: mod.Type → mod_Type */
        snprintf(qualified, len, "%s_%s", name, parser->cur_token.literal);
        return qualified;
    }
    return name;
}

/* Parse a complex type annotation.
 * Precondition: parser is ON the first token of the type ([, ^, map, or IDENT).
 * Postcondition: returns the type string, parser on the last token of the type.
 * Returns NULL on parse error (diagnostic already emitted). */
static const char *parse_complex_type(Parser *parser) {
    if (current_token_is(parser, TOK_QUESTION)) {
        /* Bare wildcard type: ? */
        return "?";
    }
    if (current_token_is(parser, TOK_LBRACKET)) {
        /* Array type: [int], [int,3], [[int]], [[[int]]], etc. */
        next_token(parser); /* element type or nested [ */
        if (current_token_is(parser, TOK_LBRACKET)) {
            /* Nested array type: count depth of brackets */
            int depth = 1;
            while (current_token_is(parser, TOK_LBRACKET)) {
                depth++;
                if (depth > 64) {
                    diagnostic_error_message(parser->diag, "E2001",
                        arena_copy_string(parser->arena,"type nesting is too deep; maximum depth is 64"),
                        parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                    return NULL;
                }
                next_token(parser);
            }
            const char *inner = read_type_name(parser);
            if (peek_token_is(parser, TOK_COLON)) {
                /* Last bracket was map shorthand: [[K:V]] = [map[K:V]] */
                depth--;
                next_token(parser); /* skip : */
                next_token(parser); /* value type */
                const char *val_type = parse_complex_type(parser);
                if (!val_type) return NULL;
                if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
                size_t klen = strlen(inner), vlen = strlen(val_type);
                size_t map_len = klen + vlen + 7;
                char *map_str = arena_alloc(parser->arena, map_len);
                snprintf(map_str, map_len, "map[%s:%s]", inner, val_type);
                inner = map_str;
            }
            for (int d = 0; d < depth; d++) {
                if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
            }
            size_t ts_len = strlen(inner) + (size_t)depth * 2 + 1;
            char *type_str = arena_alloc(parser->arena, ts_len);
            int pos = 0;
            for (int d = 0; d < depth; d++) type_str[pos++] = '[';
            memcpy(type_str + pos, inner, strlen(inner));
            pos += (int)strlen(inner);
            for (int d = 0; d < depth; d++) type_str[pos++] = ']';
            type_str[pos] = '\0';
            return type_str;
        } else if (current_token_is(parser, TOK_CARET)) {
            /* Array of pointers: [^Type] */
            next_token(parser); /* skip ^ to type name */
            const char *pointee = read_type_name(parser);
            if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
            size_t ts_len = strlen(pointee) + 4;
            char *type_str = arena_alloc(parser->arena, ts_len);
            snprintf(type_str, ts_len, "[^%s]", pointee);
            return type_str;
        } else if (current_token_is(parser, TOK_IDENT) && strcmp(parser->cur_token.literal, "map") == 0 &&
                   peek_token_is(parser, TOK_LBRACKET)) {
            /* Array of maps: [map[K:V]] or fixed-size [map[K:V], N] */
            const char *elem = parse_complex_type(parser);
            if (!elem) return NULL;
            if (peek_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip , */
                next_token(parser); /* size */
                if (!current_token_is(parser, TOK_INT) && !current_token_is(parser, TOK_IDENT)) {
                    diagnostic_error_code(parser->diag, "E2025", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                }
                const char *sz = parser->cur_token.literal;
                if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
                size_t elen = strlen(elem), szlen = strlen(sz);
                size_t ts_len = elen + szlen + 4;
                char *type_str = arena_alloc(parser->arena, ts_len);
                snprintf(type_str, ts_len, "[%s,%s]", elem, sz);
                return type_str;
            }
            if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
            size_t ts_len = strlen(elem) + 3;
            char *type_str = arena_alloc(parser->arena, ts_len);
            snprintf(type_str, ts_len, "[%s]", elem);
            return type_str;
        } else if (current_token_is(parser, TOK_IDENT) && strcmp(parser->cur_token.literal, "func") == 0 &&
                   peek_token_is(parser, TOK_LPAREN)) {
            /* Arrays of typed func signatures are not supported. */
            diagnostic_error_code(parser->diag, "E2082", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            return NULL;
        } else {
            const char *elem = read_type_name(parser);
            if (peek_token_is(parser, TOK_COLON)) {
                /* Map shorthand: [K:V] → normalized to "map[K:V]" */
                next_token(parser); /* skip : */
                next_token(parser); /* value type */
                const char *val_type = parse_complex_type(parser);
                if (!val_type) return NULL;
                if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
                size_t klen = strlen(elem), vlen = strlen(val_type);
                size_t ts_len = klen + vlen + 7;
                char *type_str = arena_alloc(parser->arena, ts_len);
                snprintf(type_str, ts_len, "map[%s:%s]", elem, val_type);
                return type_str;
            } else if (peek_token_is(parser, TOK_COMMA)) {
                /* Fixed-size array: [int, 3] or [int, SIZE] */
                next_token(parser); /* skip , */
                next_token(parser); /* size */
                if (!current_token_is(parser, TOK_INT) && !current_token_is(parser, TOK_IDENT)) {
                    diagnostic_error_code(parser->diag, "E2025", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                }
                const char *sz = parser->cur_token.literal;
                if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
                size_t elen = strlen(elem), szlen = strlen(sz);
                size_t ts_len = elen + szlen + 4;
                char *type_str = arena_alloc(parser->arena, ts_len);
                snprintf(type_str, ts_len, "[%s,%s]", elem, sz);
                return type_str;
            } else {
                /* Dynamic array: [int] */
                if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
                size_t ts_len = strlen(elem) + 3;
                char *type_str = arena_alloc(parser->arena, ts_len);
                snprintf(type_str, ts_len, "[%s]", elem);
                return type_str;
            }
        }
    } else if (current_token_is(parser, TOK_CARET)) {
        /* Pointer type: ^T; recurse to support ^^T, ^^^T, etc. */
        next_token(parser);
        const char *pointee = parse_complex_type(parser);
        if (!pointee) return NULL;
        size_t ts_len = strlen(pointee) + 2;
        char *type_str = arena_alloc(parser->arena, ts_len);
        snprintf(type_str, ts_len, "^%s", pointee);
        return type_str;
    } else if (current_token_is(parser, TOK_IDENT) && strcmp(parser->cur_token.literal, "map") == 0 &&
               peek_token_is(parser, TOK_LBRACKET)) {
        /* Map type: map[K:V]; V is parsed recursively to support nesting */
        next_token(parser); /* skip [ */
        next_token(parser); /* key type */
        const char *key_type = parser->cur_token.literal;
        if (!expect_peek_token(parser, TOK_COLON)) return NULL;
        next_token(parser); /* value type */
        const char *val_type = parse_complex_type(parser);
        if (!val_type) return NULL;
        if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
        /* "map[" + key + ":" + val + "]" + '\0' = klen + vlen + 7 */
        size_t klen = strlen(key_type), vlen = strlen(val_type);
        size_t ts_len = klen + vlen + 7;
        char *type_str = arena_alloc(parser->arena, ts_len);
        snprintf(type_str, ts_len, "map[%s:%s]", key_type, val_type);
        return type_str;
    } else if (current_token_is(parser, TOK_IDENT) && strcmp(parser->cur_token.literal, "func") == 0) {
        /* Typed function reference: func(P1, P2, ...) [-> R | -> (R1, R2, ...)]
         * Encoded as a flat string: "func(p1,p2,...)->ret" so the existing
         * type-string plumbing can carry it. `&` on a param is preserved. */
        if (!peek_token_is(parser, TOK_LPAREN)) {
            /* Bare 'func' without a signature — valid as an untyped func reference
             * (e.g. map[string:func], [func], struct fields).  Just return "func". */
            return "func";
        }
        next_token(parser); /* consume ( */
        /* Build params buffer */
        char params[MSG_BUF_LARGE] = {0};
        size_t plen = 0;
        if (!peek_token_is(parser, TOK_RPAREN)) {
            next_token(parser); /* first param */
            for (;;) {
                bool mut_p = false;
                if (current_token_is(parser, TOK_AMPERSAND)) {
                    mut_p = true;
                    next_token(parser);
                }
                const char *pt = parse_complex_type(parser);
                if (!pt) return NULL;
                int n = snprintf(params + plen, sizeof(params) - plen,
                    "%s%s%s", plen ? "," : "", mut_p ? "&" : "", pt);
                if (n < 0 || (size_t)n >= sizeof(params) - plen) return NULL;
                plen += (size_t)n;
                if (!peek_token_is(parser, TOK_COMMA)) break;
                next_token(parser); /* , */
                next_token(parser); /* next param */
            }
        }
        if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
        /* Optional -> R | -> (R1, R2, ...).
         * Absence of -> means "no return value" (the canonical encoding
         * just omits the suffix; there is no user-facing 'void' type). */
        char ret[MSG_BUF_SIZE] = {0};
        bool has_return = false;
        if (peek_token_is(parser, TOK_ARROW)) {
            next_token(parser); /* -> */
            next_token(parser); /* first return type or ( */
            has_return = true;
            if (current_token_is(parser, TOK_LPAREN)) {
                size_t rlen = 0;
                ret[rlen++] = '(';
                next_token(parser); /* first return type */
                for (;;) {
                    const char *rt = parse_complex_type(parser);
                    if (!rt) return NULL;
                    if (rt && strcmp(rt, "void") == 0) {
                        diagnostic_error_code(parser->diag, "E3068", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                        return NULL;
                    }
                    int n = snprintf(ret + rlen, sizeof(ret) - rlen, "%s%s",
                        rlen > 1 ? "," : "", rt);
                    if (n < 0 || (size_t)n >= sizeof(ret) - rlen) return NULL;
                    rlen += (size_t)n;
                    if (!peek_token_is(parser, TOK_COMMA)) break;
                    next_token(parser); /* , */
                    next_token(parser); /* next return type */
                }
                if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
                if (rlen + 2 >= sizeof(ret)) return NULL;
                ret[rlen++] = ')';
                ret[rlen] = '\0';
            } else {
                const char *rt = parse_complex_type(parser);
                if (!rt) return NULL;
                if (strcmp(rt, "void") == 0) {
                    diagnostic_error_code(parser->diag, "E3068", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                    return NULL;
                }
                snprintf(ret, sizeof(ret), "%s", rt);
            }
        }
        size_t ts_len = 5 /* "func(" */ + plen + 5 /* ")->\0" + slack */ + strlen(ret) + 1;
        char *type_str = arena_alloc(parser->arena, ts_len);
        if (has_return) {
            snprintf(type_str, ts_len, "func(%s)->%s", params, ret);
        } else {
            snprintf(type_str, ts_len, "func(%s)", params);
        }
        return type_str;
    } else {
        /* Plain type name (possibly qualified: module.Type) */
        return read_type_name(parser);
    }
}

static AstNode *parse_identifier(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
    node->data.label.value = parser->cur_token.literal;
    return node;
}

static AstNode *parse_int_literal(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_INT_VALUE, parser->cur_token);
    const char *s = parser->cur_token.literal;
    /* Accumulate as uint64_t so the full UINT64_MAX range is representable
     * and overflow detection works the same for every base. */
    uint64_t uval = 0;
    bool overflow_u64 = false;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s) {
            if (*s == '_') { s++; continue; }
            unsigned d = 0;
            if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
            if (uval > (UINT64_MAX >> 4)) overflow_u64 = true;
            uval = uval * 16 + d;
            s++;
        }
    } else if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) {
        s += 2;
        while (*s) {
            if (*s == '_') { s++; continue; }
            unsigned d = (unsigned)(*s - '0');
            if (uval > (UINT64_MAX >> 3)) overflow_u64 = true;
            uval = uval * 8 + d;
            s++;
        }
    } else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        s += 2;
        while (*s) {
            if (*s == '_') { s++; continue; }
            unsigned d = (unsigned)(*s - '0');
            if (uval > (UINT64_MAX >> 1)) overflow_u64 = true;
            uval = uval * 2 + d;
            s++;
        }
    } else {
        while (*s) {
            if (*s != '_') {
                unsigned d = (unsigned)(*s - '0');
                if (uval > (UINT64_MAX - d) / 10) overflow_u64 = true;
                uval = uval * 10 + d;
            }
            s++;
        }
    }

    node->data.int_value.value = (int64_t)uval;
    node->data.int_value.literal = parser->cur_token.literal;
    node->data.int_value.overflow = overflow_u64 || uval > (uint64_t)INT64_MAX;
    node->data.int_value.overflow_u64 = overflow_u64;
    return node;
}

static AstNode *parse_float_literal(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_FLOAT_VALUE, parser->cur_token);
    /* Strip underscores before parsing; atof stops at _ */
    const char *lit = parser->cur_token.literal;
    if (strchr(lit, '_')) {
        char buf[FLOAT_LIT_BUF];
        int j = 0;
        for (int i = 0; lit[i] && j < (int)sizeof(buf) - 1; i++) {
            if (lit[i] != '_') buf[j++] = lit[i];
        }
        buf[j] = '\0';
        node->data.float_value.value = atof(buf);
    } else {
        node->data.float_value.value = atof(lit);
    }
    return node;
}

static bool string_has_interpolation(const char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '$' && str[i + 1] == '{') return true;
        if (str[i] == '\\') i++;
    }
    return false;
}

static AstNode *parse_interpolated_string(Parser *parser, const char *raw) {
    AstNode *node = ast_alloc(parser->arena, NODE_INTERPOLATED_STRING, parser->cur_token);

    int cap = GROW_ARRAY_INIT_CAP;
    int count = 0;
    AstNode **parts = arena_alloc(parser->arena, sizeof(AstNode *) * cap);

    const char *s = raw;
    const char *seg_start = s;

    while (*s) {
        if (*s == '\\' && *(s + 1)) {
            s += 2;
            continue;
        }
        if (*s == '$' && *(s + 1) == '{') {
            /* Emit the text segment before ${ */
            if (s > seg_start) {
                ARENA_GROW(parser->arena, parts, count, cap);
                AstNode *text = ast_alloc(parser->arena, NODE_STRING_VALUE, parser->cur_token);
                text->data.string_value.value = arena_copy_string_with_length(parser->arena, seg_start, s - seg_start);
                parts[count++] = text;
            }

            /* Find matching } and parse the expression inside */
            s += 2; /* skip ${ */
            const char *expr_start = s;
            int brace_depth = 1;
            while (*s && brace_depth > 0) {
                /* Skip nested string literals so braces inside
                 * them are not counted against brace_depth. */
                if (*s == '"') {
                    s++; /* skip opening " */
                    while (*s && *s != '"') {
                        if (*s == '\\' && *(s + 1)) s++;
                        s++;
                    }
                    if (*s == '"') s++; /* skip closing " */
                    continue;
                }
                if (*s == '{') brace_depth++;
                else if (*s == '}') brace_depth--;
                if (brace_depth > 0) s++;
            }

            /* Guard against unbounded interpolation expressions */
            size_t expr_len = (size_t)(s - expr_start);
            if (expr_len > 65536) {
                diagnostic_error_message(parser->diag, "E2001",
                    arena_copy_string(parser->arena, "string interpolation expression is too large (max 64KB)"),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                return NULL;
            }

            /* Parse the expression text */
            ARENA_GROW(parser->arena, parts, count, cap);

            char *expr_text = arena_copy_string_with_length(parser->arena, expr_start, s - expr_start);
            /* reject '${}' (and whitespace-only '${ }') before
             * spinning up a sub-parser. Otherwise the sub-parser hits
             * EOF on an empty input and reports E2002 with the stale
             * file-start position it was initialized to, which is
             * actively misleading. */
            bool is_empty = true;
            for (const char *c = expr_text; *c; c++) {
                if (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\r') {
                    is_empty = false;
                    break;
                }
            }
            if (is_empty) {
                diagnostic_error_code(parser->diag, "E2071", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            } else {
                Lexer *expr_lexer = lexer_create(parser->arena, expr_text, parser->file);
                /* Offset the sub-lexer to the real source position of this
                 * ${...} expression so diagnostics point at the right line
                 * and column instead of always reporting 1:N. expr_start
                 * points to the first char of the expression (past "${"),
                 * so (expr_start - raw) is its byte offset from the opening
                 * quote. Count any newlines in the string before this point
                 * to handle multi-line strings correctly. */
                {
                    int line_off = 0;
                    int col_from_nl = 0;
                    for (const char *cp = raw; cp < expr_start; cp++) {
                        if (*cp == '\n') { line_off++; col_from_nl = 0; }
                        else col_from_nl++;
                    }
                    expr_lexer->line = parser->cur_token.line + line_off;
                    if (line_off > 0)
                        expr_lexer->column = col_from_nl + 1;
                    else
                        expr_lexer->column = parser->cur_token.column + 1 + (int)(expr_start - raw);
                }
                Parser *expr_parser = parser_create(parser->arena, expr_lexer, parser->file, parser->diag);
                expr_parser->in_interp = true;
                AstNode *expr = parse_expression(expr_parser, PREC_LOWEST);
                if (expr) parts[count++] = expr;
            }

            if (*s == '}') s++;
            seg_start = s;
        } else {
            s++;
        }
    }

    /* Remaining text segment */
    if (s > seg_start) {
        ARENA_GROW(parser->arena, parts, count, cap);
        AstNode *text = ast_alloc(parser->arena, NODE_STRING_VALUE, parser->cur_token);
        text->data.string_value.value = arena_copy_string_with_length(parser->arena, seg_start, s - seg_start);
        parts[count++] = text;
    }

    node->data.interpolated_string.parts = parts;
    node->data.interpolated_string.part_count = count;
    return node;
}

static AstNode *parse_string_literal(Parser *parser) {
    const char *raw = parser->cur_token.literal;
    bool is_raw = (parser->cur_token.type == TOK_RAW_STRING);
    if (!is_raw && string_has_interpolation(raw)) {
        return parse_interpolated_string(parser, raw);
    }
    if (!is_raw) {
        /* E2057: check for bare $identifier (missing braces) */
        for (int i = 0; raw[i]; i++) {
            if (raw[i] == '\\') { i++; continue; }
            if (raw[i] == '$' && raw[i + 1] != '{' && raw[i + 1] != '\0' &&
                (isalpha((unsigned char)raw[i + 1]) || raw[i + 1] == '_')) {
                diagnostic_error_code(parser->diag, "E2057", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                break;
            }
        }
    }
    AstNode *node = ast_alloc(parser->arena, NODE_STRING_VALUE, parser->cur_token);
    node->data.string_value.value = raw;
    node->data.string_value.is_raw = is_raw;
    return node;
}

static AstNode *parse_bool_literal(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_BOOL_VALUE, parser->cur_token);
    node->data.bool_value.value = (parser->cur_token.type == TOK_TRUE);
    return node;
}

static AstNode *parse_nil_literal(Parser *parser) {
    return ast_alloc(parser->arena, NODE_NIL_VALUE, parser->cur_token);
}

static AstNode *parse_prefix_expression(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_PREFIX_EXPR, parser->cur_token);
    node->data.prefix.op = parser->cur_token.type;
    next_token(parser);
    node->data.prefix.right = parse_expression(parser, PREC_PREFIX);
    return node;
}

static AstNode *parse_grouped_expression(Parser *parser) {
    /* Check for function reference: ()func_name or ()Type.func */
    if (peek_token_is(parser, TOK_RPAREN)) {
        Token ref_tok = parser->cur_token;
        next_token(parser); /* consume ) */
        next_token(parser); /* move to identifier */

        if (parser->cur_token.type != TOK_IDENT) {
            return NULL;
        }

        /* Parse the function name; may be qualified with dots */
        AstNode *func_expr = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
        func_expr->data.label.value = parser->cur_token.literal;

        while (peek_token_is(parser, TOK_DOT)) {
            next_token(parser); /* consume . */
            next_token(parser); /* move to member */
            AstNode *member = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
            member->data.member.object = func_expr;
            member->data.member.member = parser->cur_token.literal;
            func_expr = member;
        }

        AstNode *ref = ast_alloc(parser->arena, NODE_FUNC_REF, ref_tok);
        ref->data.func_ref.function = func_expr;
        return ref;
    }

    next_token(parser);
    AstNode *expr = parse_expression(parser, PREC_LOWEST);
    if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
    return expr;
}

/* Parse prefix expression (the "nud" in Pratt parsing) */
static AstNode *parse_prefix(Parser *parser) {
    switch (parser->cur_token.type) {
    case TOK_IDENT:
        /* Check for module-qualified struct literal: mod.Name{ ... } */
        if (peek_token_is(parser, TOK_DOT)) {
            const char *mod = parser->cur_token.literal;
            if (mod[0] >= 'a' && mod[0] <= 'z') {
                /* Save state for lookahead */
                Token saved_cur = parser->cur_token;
                Token saved_peek = parser->peek_token;
                int saved_pos = parser->lexer->position;
                int saved_rpos = parser->lexer->read_position;
                char saved_ch = parser->lexer->ch;
                int saved_line = parser->lexer->line;
                int saved_col = parser->lexer->column;

                next_token(parser); /* consume . */
                next_token(parser); /* move to potential type name */

                if (parser->cur_token.type == TOK_IDENT &&
                    peek_token_is(parser, TOK_LBRACE) && !parser->no_struct_literal &&
                    parser->cur_token.literal[0] >= 'A' && parser->cur_token.literal[0] <= 'Z') {
                    /* mod.Name{; module-qualified struct literal */
                    char *prefixed = arena_alloc(parser->arena, MSG_BUF_SIZE);
                    snprintf(prefixed, MSG_BUF_SIZE, "%s_%s", mod, parser->cur_token.literal);
                    next_token(parser); /* move to { */
                    return parse_struct_literal(parser, prefixed);
                }

                /* Not a struct literal; restore state */
                parser->cur_token = saved_cur;
                parser->peek_token = saved_peek;
                parser->lexer->position = saved_pos;
                parser->lexer->read_position = saved_rpos;
                parser->lexer->ch = saved_ch;
                parser->lexer->line = saved_line;
                parser->lexer->column = saved_col;
            }
        }
        /* Check for struct literal: Name{ ... }
         * Struct/enum names start with uppercase by convention. */
        if (peek_token_is(parser, TOK_LBRACE) && !parser->no_struct_literal) {
            const char *name = parser->cur_token.literal;
            if (name[0] >= 'A' && name[0] <= 'Z') {
                next_token(parser); /* move to { */
                return parse_struct_literal(parser, name);
            }
        }
        return parse_identifier(parser);
    case TOK_INT:       return parse_int_literal(parser);
    case TOK_FLOAT:     return parse_float_literal(parser);
    case TOK_STRING:
    case TOK_RAW_STRING: return parse_string_literal(parser);
    case TOK_TRUE:
    case TOK_FALSE:     return parse_bool_literal(parser);
    case TOK_NIL:       return parse_nil_literal(parser);
    case TOK_CHAR: {
        AstNode *node = ast_alloc(parser->arena, NODE_CHAR_VALUE, parser->cur_token);
        node->data.char_value.value = parser->cur_token.literal[0];
        if (parser->cur_token.literal[0] == '\\' && parser->cur_token.literal[1]) {
            switch (parser->cur_token.literal[1]) {
            case 'n': node->data.char_value.value = '\n'; break;
            case 't': node->data.char_value.value = '\t'; break;
            case 'r': node->data.char_value.value = '\r'; break;
            case '\\': node->data.char_value.value = '\\'; break;
            case '\'': node->data.char_value.value = '\''; break;
            case '0': node->data.char_value.value = '\0'; break;
            default: node->data.char_value.value = parser->cur_token.literal[1]; break;
            }
        }
        return node;
    }
    case TOK_MINUS:
    case TOK_BANG:
    case TOK_BIT_NOT: return parse_prefix_expression(parser);
    case TOK_AMPERSAND: {
        diagnostic_error_code(parser->diag, "E2072",
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        return parse_prefix_expression(parser);
    }
    case TOK_DOT: {
        /* .VARIANT — implicit enum selector (resolved by typechecker) */
        Token dot_tok = parser->cur_token;
        next_token(parser); /* consume dot */
        if (parser->cur_token.type != TOK_IDENT) {
            char buf[256];
            snprintf(buf, sizeof(buf), "expected enum variant name after '.'");
            diagnostic_error(parser->diag, "E2001", arena_copy_string(parser->arena, buf),
                parser->file, dot_tok.line, dot_tok.column, 0);
            return ast_alloc(parser->arena, NODE_NIL_VALUE, dot_tok);
        }
        AstNode *node = ast_alloc(parser->arena, NODE_IMPLICIT_ENUM, dot_tok);
        node->data.implicit_enum.variant = arena_copy_string(parser->arena, parser->cur_token.literal);
        node->data.implicit_enum.resolved_enum = NULL;
        return node;
    }
    case TOK_LPAREN:    return parse_grouped_expression(parser);
    case TOK_LBRACE: {
        /* Could be array literal {1, 2, 3} or map literal {"k": v, ...}
         * Detect map by checking for colon after first expression */
        Token brace_tok = parser->cur_token;
        next_token(parser); /* skip { */

        /* Empty: {}; treat as empty array (context-dependent) */
        if (current_token_is(parser, TOK_RBRACE)) {
            AstNode *node = ast_alloc(parser->arena, NODE_ARRAY_VALUE, brace_tok);
            node->data.array_value.count = 0;
            node->data.array_value.elements = NULL;
            return node;
        }

        /* Empty map: {:} */
        if (current_token_is(parser, TOK_COLON) && peek_token_is(parser, TOK_RBRACE)) {
            next_token(parser); /* skip } */
            AstNode *node = ast_alloc(parser->arena, NODE_MAP_VALUE, brace_tok);
            node->data.map_value.count = 0;
            node->data.map_value.keys = NULL;
            node->data.map_value.values = NULL;
            return node;
        }

        /* Leading comma: {, 1, 2} — reuse the trailing-comma diagnostic. */
        if (current_token_is(parser, TOK_COMMA)) {
            diagnostic_error_code(parser->diag, "E2017",
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            next_token(parser); /* skip the stray , and keep parsing */
        }

        /* Parse first expression */
        AstNode *first = parse_expression(parser, PREC_LOWEST);

        if (peek_token_is(parser, TOK_COLON)) {
            /* Map literal: {"key": value, ...} */
            AstNode *node = ast_alloc(parser->arena, NODE_MAP_VALUE, brace_tok);
            int cap = GROW_ARRAY_INIT_CAP;
            node->data.map_value.count = 0;
            node->data.map_value.keys = arena_alloc(parser->arena, sizeof(AstNode *) * cap);
            node->data.map_value.values = arena_alloc(parser->arena, sizeof(AstNode *) * cap);

            /* First key already parsed */
            node->data.map_value.keys[0] = first;
            next_token(parser); /* skip : */
            next_token(parser);
            node->data.map_value.values[0] = parse_expression(parser, PREC_LOWEST);
            node->data.map_value.count = 1;

            while (peek_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip , */
                next_token(parser);
                if (current_token_is(parser, TOK_RBRACE)) break;
                if (node->data.map_value.count >= cap) {
                    cap = GROW_NEXT_CAP(cap);
                    ARENA_GROW_TO(parser->arena, node->data.map_value.keys,
                        node->data.map_value.count, cap);
                    ARENA_GROW_TO(parser->arena, node->data.map_value.values,
                        node->data.map_value.count, cap);
                }
                node->data.map_value.keys[node->data.map_value.count] =
                    parse_expression(parser, PREC_LOWEST);
                if (!expect_peek_token(parser, TOK_COLON)) return NULL;
                next_token(parser);
                node->data.map_value.values[node->data.map_value.count] =
                    parse_expression(parser, PREC_LOWEST);
                node->data.map_value.count++;
            }
            if (peek_token_is(parser, TOK_RBRACE)) next_token(parser);
            return node;
        }

        /* Array literal: {expr, expr, ...} */
        AstNode *node = ast_alloc(parser->arena, NODE_ARRAY_VALUE, brace_tok);
        int cap = GROW_ARRAY_INIT_CAP;
        node->data.array_value.count = 0;
        node->data.array_value.elements = arena_alloc(parser->arena, sizeof(AstNode *) * cap);
        node->data.array_value.elements[node->data.array_value.count++] = first;

        while (peek_token_is(parser, TOK_COMMA)) {
            next_token(parser); /* skip , */
            next_token(parser);
            if (current_token_is(parser, TOK_RBRACE)) {
                diagnostic_error_code(parser->diag, "E2017",
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                break;
            }
            ARENA_GROW(parser->arena, node->data.array_value.elements,
                node->data.array_value.count, cap);
            node->data.array_value.elements[node->data.array_value.count++] =
                parse_expression(parser, PREC_LOWEST);
        }
        if (peek_token_is(parser, TOK_RBRACE)) next_token(parser);
        return node;
    }
    case TOK_CAST: {
        /* cast(value, type) */
        AstNode *node = ast_alloc(parser->arena, NODE_CAST_EXPR, parser->cur_token);
        node->data.cast.is_array = false;
        node->data.cast.element_type = NULL;
        if (!expect_peek_token(parser, TOK_LPAREN)) return NULL;
        next_token(parser);
        node->data.cast.value = parse_expression(parser, PREC_LOWEST);
        if (!expect_peek_token(parser, TOK_COMMA)) return NULL;
        next_token(parser);
        if (current_token_is(parser, TOK_LBRACKET)) {
            /* Array type: cast(value, [type]) */
            next_token(parser); /* element type */
            const char *elem = parser->cur_token.literal;
            if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
            size_t ts_len = strlen(elem) + 3;
            char *type_str = arena_alloc(parser->arena, ts_len);
            snprintf(type_str, ts_len, "[%s]", elem);
            node->data.cast.target_type = type_str;
            node->data.cast.is_array = true;
            node->data.cast.element_type = elem;
        } else {
            node->data.cast.target_type = parser->cur_token.literal;
        }
        if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
        return node;
    }
    case TOK_NEW: {
        /* new(Type); allocate zeroed value on default arena */
        AstNode *node = ast_alloc(parser->arena, NODE_NEW_EXPR, parser->cur_token);
        if (!expect_peek_token(parser, TOK_LPAREN)) return NULL;
        next_token(parser);
        node->data.new_expr.type_name = parse_complex_type(parser);
        if (type_string_has_wildcard(node->data.new_expr.type_name)) {
            diagnostic_error_message(parser->diag, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' cannot be used with 'new()'; 'new()' requires a concrete type"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
        return node;
    }
    case TOK_RANGE: {
        /* range(end) or range(start, end) or range(start, end, step) */
        AstNode *node = ast_alloc(parser->arena, NODE_RANGE_EXPR, parser->cur_token);
        node->data.range_expr.start = NULL;
        node->data.range_expr.end = NULL;
        node->data.range_expr.step = NULL;
        if (!expect_peek_token(parser, TOK_LPAREN)) return NULL;
        next_token(parser);
        AstNode *first = parse_expression(parser, PREC_LOWEST);
        if (peek_token_is(parser, TOK_COMMA)) {
            node->data.range_expr.start = first;
            next_token(parser); /* skip comma */
            next_token(parser);
            node->data.range_expr.end = parse_expression(parser, PREC_LOWEST);
            if (peek_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip comma */
                next_token(parser);
                node->data.range_expr.step = parse_expression(parser, PREC_LOWEST);
            }
        } else {
            /* range(end) - start defaults to 0 */
            node->data.range_expr.end = first;
        }
        if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
        return node;
    }
    case TOK_IN:
    case TOK_NOT_IN: {
        /* 'in'/'not_in'/'!in' used without a left-hand value */
        Token bad_tok = parser->cur_token;
        const char *op = bad_tok.literal;
        char buf[MSG_BUF_SIZE];
        snprintf(buf, sizeof(buf),
            "'%s' requires a value on the left side; '%s' checks whether a value belongs to a collection or range",
            op, op);
        diagnostic_error_message(parser->diag, "E2086", arena_copy_string(parser->arena, buf),
            parser->file, bad_tok.line, bad_tok.column, 0);
        /* Consume the operator and its right-hand operand so subsequent tokens
         * (like the if-body '{') are seen in the right context. */
        next_token(parser);
        parser->no_struct_literal = true;
        parse_expression(parser, PREC_LOWEST);
        parser->no_struct_literal = false;
        /* Return a dummy bool so the condition slot is non-NULL and the
         * typechecker does not add a second spurious diagnostic. */
        AstNode *dummy = ast_alloc(parser->arena, NODE_BOOL_VALUE, bad_tok);
        dummy->data.bool_value.value = true;
        return dummy;
    }
    default:
    {
        /* Skip generic error for ILLEGAL tokens; the lexer already emitted a specific diagnostic */
        if (parser->cur_token.type != TOK_ILLEGAL) {
            char buf[MSG_BUF_SIZE];
            if (parser->cur_token.type == TOK_EOF && parser->in_interp)
                snprintf(buf, sizeof(buf), "unexpected end of interpolation expression");
            else
                snprintf(buf, sizeof(buf), "unexpected token '%s'",
                    token_display_name(parser->cur_token));
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, buf),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
    }
        return NULL;
    }
}

static AstNode *parse_infix_expression(Parser *parser, AstNode *left) {
    AstNode *node = ast_alloc(parser->arena, NODE_INFIX_EXPR, parser->cur_token);
    node->data.infix.left = left;
    node->data.infix.op = parser->cur_token.type;
    Precedence prec = get_token_precedence(parser->cur_token.type);
    bool is_membership = (parser->cur_token.type == TOK_IN || parser->cur_token.type == TOK_NOT_IN);
    /* Suppress struct-literal parsing on the RHS of comparisons and
     * membership operators.  In `if x < Foo {`, the `{` is the
     * if-block, not the start of a struct literal `Foo{ ... }`. */
    bool suppress_struct_lit = is_membership ||
        parser->cur_token.type == TOK_LT || parser->cur_token.type == TOK_GT ||
        parser->cur_token.type == TOK_LT_EQ || parser->cur_token.type == TOK_GT_EQ ||
        parser->cur_token.type == TOK_EQ || parser->cur_token.type == TOK_NOT_EQ;
    next_token(parser);
    if (suppress_struct_lit) parser->no_struct_literal = true;
    node->data.infix.right = parse_expression(parser, prec);
    if (suppress_struct_lit) parser->no_struct_literal = false;
    return node;
}

static AstNode *parse_call_expression(Parser *parser, AstNode *function) {
    if (parser->cur_token.preceded_by_ws) {
        diagnostic_error_code(parser->diag, "E2073", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        /* Still parse the argument list so we consume the closing ')'
         * and don't cascade into E2001/E2002 on the unrelated tokens. */
    }
    AstNode *node = ast_alloc(parser->arena, NODE_CALL_EXPR, parser->cur_token);
    node->data.call.function = function;

    /* Parse arguments with named-argument detection.
     * Named args use the syntax  name: value  at the call site.
     * Detection: current token is TOK_IDENT and peek is TOK_COLON. */
    int count = 0;
    int cap = GROW_ARRAY_INIT_CAP;
    AstNode **args = arena_alloc(parser->arena, sizeof(AstNode *) * cap);
    const char **names = arena_alloc(parser->arena, sizeof(const char *) * cap);
    memset(names, 0, sizeof(const char *) * cap);
    bool has_named = false;

    if (peek_token_is(parser, TOK_RPAREN)) {
        next_token(parser);
    } else {
        for (;;) {
            next_token(parser);
            if (count >= cap) {
                cap = GROW_NEXT_CAP(cap);
                ARENA_GROW_TO(parser->arena, args, count, cap);
                ARENA_GROW_TO(parser->arena, names, count, cap);
                /* Unset names must read back as NULL, and the arena does not zero. */
                memset(names + count, 0, sizeof(const char *) * (size_t)(cap - count));
            }

            /* Check for named arg: ident followed by colon */
            if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_COLON)) {
                names[count] = arena_copy_string(parser->arena, parser->cur_token.literal);
                has_named = true;
                next_token(parser); /* skip ident */
                next_token(parser); /* skip colon, now on value */
            }

            /* size_of(^T): parse pointer type as a type expression, not a general expression */
            if (function->kind == NODE_LABEL &&
                strcmp(function->data.label.value, "size_of") == 0 &&
                current_token_is(parser, TOK_CARET)) {
                const char *type_str = parse_complex_type(parser);
                AstNode *label = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
                label->data.label.value = type_str;
                args[count] = label;
            } else {
                args[count] = parse_expression(parser, PREC_LOWEST);
            }
            count++;

            if (!peek_token_is(parser, TOK_COMMA)) break;
            next_token(parser); /* skip comma */
        }
        if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
    }

    node->data.call.args = args;
    node->data.call.arg_count = count;
    node->data.call.arg_names = has_named ? names : NULL;
    return node;
}

static AstNode *parse_member_expression(Parser *parser, AstNode *object) {
    if (parser->cur_token.preceded_by_ws) {
        diagnostic_error_code(parser->diag, "E2074", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        /* Continue parsing the member so we swallow the identifier after '.'
         * and don't cascade into unrelated diagnostics. */
    }
    next_token(parser); /* skip the identifier after dot */
    AstNode *node = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
    node->data.member.object = object;
    node->data.member.member = parser->cur_token.literal;
    return node;
}

static AstNode *parse_index_expression(Parser *parser, AstNode *left) {
    if (parser->cur_token.preceded_by_ws) {
        diagnostic_error_code(parser->diag, "E2075", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        /* Continue parsing so we consume the closing ']'. */
    }
    AstNode *node = ast_alloc(parser->arena, NODE_INDEX_EXPR, parser->cur_token);
    node->data.index_expr.left = left;
    next_token(parser);
    node->data.index_expr.index = parse_expression(parser, PREC_LOWEST);
    if (!expect_peek_token(parser, TOK_RBRACKET)) return NULL;
    return node;
}

static AstNode *parse_postfix_expression(Parser *parser, AstNode *left) {
    if (parser->cur_token.preceded_by_ws) {
        diagnostic_error_code(parser->diag, "E2076", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
    }
    AstNode *node = ast_alloc(parser->arena, NODE_POSTFIX_EXPR, parser->cur_token);
    node->data.postfix.left = left;
    node->data.postfix.op = parser->cur_token.type;
    return node;
}

/* Parse infix expression (the "led" in Pratt parsing) */
static AstNode *parse_infix(Parser *parser, AstNode *left) {
    switch (parser->cur_token.type) {
    case TOK_PLUS: case TOK_MINUS: case TOK_ASTERISK: case TOK_SLASH:
    case TOK_PERCENT:
    case TOK_EQ: case TOK_NOT_EQ: case TOK_LT: case TOK_GT:
    case TOK_LT_EQ: case TOK_GT_EQ:
    case TOK_AND: case TOK_OR:
    case TOK_IN: case TOK_NOT_IN:
    case TOK_BIT_AND: case TOK_BIT_OR: case TOK_BIT_XOR:
    case TOK_BIT_SHIFT_LEFT: case TOK_BIT_SHIFT_RIGHT:
        return parse_infix_expression(parser, left);
    case TOK_LPAREN:
        return parse_call_expression(parser, left);
    case TOK_DOT:
        return parse_member_expression(parser, left);
    case TOK_LBRACKET:
        return parse_index_expression(parser, left);
    case TOK_INCREMENT:
    case TOK_DECREMENT:
    case TOK_CARET:
        return parse_postfix_expression(parser, left);
    default:
        return left;
    }
}

static AstNode *parse_expression(Parser *parser, Precedence prec) {
    parser->depth++;
    if (parser->depth > MAX_PARSE_DEPTH) {
        diagnostic_error_message(parser->diag, "E2001",
            arena_copy_string(parser->arena,"expression is nested too deeply; maximum depth is 256"),
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        parser->depth--;
        return NULL;
    }

    AstNode *left = parse_prefix(parser);
    if (!left) { parser->depth--; return NULL; }

    while (!peek_token_is(parser, TOK_EOF) && prec < get_token_precedence(parser->peek_token.type)) {
        next_token(parser);
        left = parse_infix(parser, left);
        if (!left) { parser->depth--; return NULL; }
    }

    parser->depth--;
    return left;
}

/* --- Statement Parsing --- */

/* If peek is or_return, consume it and desugar
 *
 *     <var_decl with value = expr> or_return
 *
 * into a block:
 *
 *     mut _tmp = expr
 *     if _tmp.v1 != nil { return _tmp.v1 }
 *     <var_decl with value = _tmp.v0>
 *
 * Returns the desugared block, or NULL if no or_return was present
 * (caller keeps the original var_decl untouched). */
static AstNode *maybe_apply_or_return(Parser *parser, AstNode *var_decl) {
    if (!peek_token_is(parser, TOK_OR_RETURN)) return NULL;
    next_token(parser); /* consume or_return */

    /* Check for custom fallback values on the same line as the or_return token.
     * e.g. `mut val = risky(fail) or_return -99` → return -99, _tmp.v1
     * The caller provides all non-error return slots; _tmp.v1 is appended. */
    int or_return_line = parser->cur_token.line;
    AstNode *fallback_buf[16];
    int fallback_count = 0;
    if (parser->peek_token.type != TOK_EOF &&
        parser->peek_token.type != TOK_SEMICOLON &&
        parser->peek_token.type != TOK_RBRACE &&
        parser->peek_token.line == or_return_line) {
        next_token(parser); /* advance to first fallback token */
        while (fallback_count < 16) {
            fallback_buf[fallback_count++] = parse_expression(parser, PREC_LOWEST);
            if (!peek_token_is(parser, TOK_COMMA)) break;
            next_token(parser); /* skip comma */
            next_token(parser); /* advance to next fallback token */
        }
    }

    static int or_return_counter = 0;
    char *tmp_name = arena_alloc(parser->arena, TMP_NAME_BUF);
    snprintf(tmp_name, TMP_NAME_BUF, GRAY_SYNTH_OR "%d", or_return_counter++);

    AstNode *block = ast_alloc(parser->arena, NODE_BLOCK_STMT, parser->cur_token);
    block->data.block.cap = 4;
    block->data.block.count = 0;
    block->data.block.stmts = arena_alloc(parser->arena, sizeof(AstNode *) * block->data.block.cap);

    /* _tmp = expr */
    AstNode *tmp_decl = ast_alloc(parser->arena, NODE_VAR_DECL, parser->cur_token);
    tmp_decl->data.var_decl.mutable = true;
    tmp_decl->data.var_decl.name = tmp_name;
    tmp_decl->data.var_decl.synthetic = true;
    tmp_decl->data.var_decl.type_name = NULL;
    tmp_decl->data.var_decl.value = var_decl->data.var_decl.value;
    block->data.block.stmts[block->data.block.count++] = tmp_decl;

    /* if (_tmp.v1 != nil) { return _tmp.v1 } */
    AstNode *if_stmt = ast_alloc(parser->arena, NODE_IF_STMT, parser->cur_token);
    AstNode *err_access = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
    AstNode *tmp_label = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
    tmp_label->data.label.value = tmp_name;
    err_access->data.member.object = tmp_label;
    err_access->data.member.member = "v1";
    AstNode *nil_val = ast_alloc(parser->arena, NODE_NIL_VALUE, parser->cur_token);
    AstNode *cond = ast_alloc(parser->arena, NODE_INFIX_EXPR, parser->cur_token);
    cond->data.infix.left = err_access;
    cond->data.infix.op = TOK_NOT_EQ;
    cond->data.infix.right = nil_val;
    if_stmt->data.if_stmt.condition = cond;

    AstNode *ret_block = ast_alloc(parser->arena, NODE_BLOCK_STMT, parser->cur_token);
    ret_block->data.block.cap = 1;
    ret_block->data.block.count = 0;
    ret_block->data.block.stmts = arena_alloc(parser->arena, sizeof(AstNode *));
    AstNode *ret_stmt = ast_alloc(parser->arena, NODE_RETURN_STMT, parser->cur_token);
    /* Build _tmp.v1 node (the propagated error value) */
    AstNode *err_access2 = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
    AstNode *tmp_label2 = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
    tmp_label2->data.label.value = tmp_name;
    err_access2->data.member.object = tmp_label2;
    err_access2->data.member.member = "v1";
    if (fallback_count > 0) {
        /* Custom fallback values. If the user provided enough values to
         * cover all return slots (including the error), use them as-is.
         * Otherwise append the propagated error (_tmp.v1). */
        int func_ret = parser->current_func ? parser->current_func->data.func_decl.return_type_count : 0;
        bool user_covers_error = (func_ret > 0 && fallback_count >= func_ret);
        int total = user_covers_error ? fallback_count : fallback_count + 1;
        ret_stmt->data.return_stmt.values = arena_alloc(parser->arena, sizeof(AstNode *) * total);
        for (int i = 0; i < fallback_count; i++)
            ret_stmt->data.return_stmt.values[i] = fallback_buf[i];
        if (!user_covers_error)
            ret_stmt->data.return_stmt.values[fallback_count] = err_access2;
        ret_stmt->data.return_stmt.count = total;
    } else {
        /* Bare or_return: propagate just the error; codegen fills {0} for other slots */
        ret_stmt->data.return_stmt.values = arena_alloc(parser->arena, sizeof(AstNode *));
        ret_stmt->data.return_stmt.values[0] = err_access2;
        ret_stmt->data.return_stmt.count = 1;
    }
    ret_block->data.block.stmts[ret_block->data.block.count++] = ret_stmt;
    if_stmt->data.if_stmt.consequence = ret_block;
    if_stmt->data.if_stmt.alternative = NULL;
    block->data.block.stmts[block->data.block.count++] = if_stmt;

    /* x = _tmp.v0 */
    AstNode *var = ast_alloc(parser->arena, NODE_VAR_DECL, parser->cur_token);
    var->data.var_decl.mutable = var_decl->data.var_decl.mutable;
    var->data.var_decl.name = var_decl->data.var_decl.name;
    var->data.var_decl.type_name = var_decl->data.var_decl.type_name;
    AstNode *val_access = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
    AstNode *tmp_label3 = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
    tmp_label3->data.label.value = tmp_name;
    val_access->data.member.object = tmp_label3;
    val_access->data.member.member = "v0";
    var->data.var_decl.value = val_access;
    block->data.block.stmts[block->data.block.count++] = var;

    return block;
}

/* Bare throwaway: `_ = expr` (no mut/const keyword) at statement position.
 * Desugars to a var_decl(name="_", mutable=true), which the typechecker
 * and codegen already special-case to skip symbol creation and emit
 * `(void)(expr);`. or_return is supported via the shared helper. */
/* E5012: the throwaway '_' is only meaningful when the RHS is a
 * function call. Literal/identifier/arithmetic RHSes have no return
 * value to discard and no side effect to run, so `_ = 32` etc. are
 * dead code with a misleading name. Checked at parse time (before
 * or_return desugaring) so the user-written RHS, not the rewritten
 * member-access, is what gets validated. */
static void check_discard_target(Parser *parser, AstNode *value) {
    if (!value || value->kind == NODE_CALL_EXPR) return;
    diagnostic_error_code(parser->diag, "E5012",
        parser->file, value->token.line, value->token.column, 0);
}

static AstNode *parse_discard_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_VAR_DECL, parser->cur_token);
    node->data.var_decl.mutable = true;
    node->data.var_decl.name = "_";
    node->data.var_decl.type_name = NULL;
    node->data.var_decl.value = NULL;

    next_token(parser); /* consume = */
    next_token(parser); /* move to RHS */
    node->data.var_decl.value = parse_expression(parser, PREC_LOWEST);
    if (!node->data.var_decl.value) return NULL;

    check_discard_target(parser, node->data.var_decl.value);

    AstNode *desugared = maybe_apply_or_return(parser, node);
    if (desugared) return desugared;
    return node;
}

static AstNode *parse_var_declaration_ex(Parser *parser, bool bare) {
    AstNode *node = ast_alloc(parser->arena, NODE_VAR_DECL, parser->cur_token);

    if (bare) {
        node->data.var_decl.mutable = true;
        /* cur_token is already the variable name — don't advance */
    } else {
        node->data.var_decl.mutable = (parser->cur_token.type == TOK_MUT);

        if (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_BLANK)) {
            next_token(parser);
        } else if (is_keyword_token(parser->peek_token.type)) {
            char msg[MSG_BUF_SIZE];
            snprintf(msg, sizeof(msg),
                "'%s' is a reserved keyword and cannot be used as a variable name",
                parser->peek_token.literal);
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                parser->file, parser->peek_token.line, parser->peek_token.column, 0);
            synchronize_parser(parser);
            return NULL;
        } else {
            expect_peek_token(parser, TOK_IDENT); /* will error */
            return NULL;
        }
    }
    node->data.var_decl.name = parser->cur_token.literal;

    /* Optional type annotation. TOK_QUESTION is included so a bare
     * wildcard `?` in a var_decl flows through parse_complex_type and
     * lands on the existing E2070 diagnostic below; without it, the
     * token falls through to the generic "unexpected token" fallback
     * and the user gets no hint about why `?` isn't allowed here
     * (). */
    /* E2079: reject 'nil' as a type annotation. nil is a value per the
     * language, not a type; consume the token to avoid a cascading
     * "nil is an unexpected expression statement" diagnostic. */
    if (peek_token_is(parser, TOK_NIL)) {
        next_token(parser); /* consume nil */
        diagnostic_error_code(parser->diag, "E2079",
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
    }

    node->data.var_decl.type_name = NULL;
    if (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_CARET) || peek_token_is(parser, TOK_LBRACKET) ||
        peek_token_is(parser, TOK_STRUCT) || peek_token_is(parser, TOK_ENUM) ||
        peek_token_is(parser, TOK_QUESTION)) {
        next_token(parser);
        node->data.var_decl.type_name = parse_complex_type(parser);
        if (!node->data.var_decl.type_name) return NULL;
        /* E2070: wildcard `?` only allowed in function signatures */
        if (type_string_has_wildcard(node->data.var_decl.type_name)) {
            diagnostic_error_message(parser->diag, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' is only allowed in function parameter and return types; not in variable declarations"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        /* E2068: mut <name> struct/enum; should be const */
        if (node->data.var_decl.mutable &&
            (strcmp(node->data.var_decl.type_name, "struct") == 0 ||
             strcmp(node->data.var_decl.type_name, "enum") == 0)) {
            diagnostic_error_code_formatted(parser->diag, "E2068", parser->file, node->token.line, node->token.column, 0, node->data.var_decl.type_name);
            return NULL;
        }
    }

    /* Blank identifier requires '=' (or ',' for multi-var destructuring).
     * Checked after the type-annotation block so `mut _ int, ...` is allowed
     * but `mut _ foo()` is caught before the leftover tokens desync the parser. */
    if (strcmp(node->data.var_decl.name, "_") == 0 &&
        !peek_token_is(parser, TOK_ASSIGN) && !peek_token_is(parser, TOK_COMMA)) {
        const char *kw = node->data.var_decl.mutable ? "mut" : "const";
        char msg[MSG_BUF_SIZE];
        snprintf(msg, sizeof(msg),
            "blank identifier '_' requires '='; use '%s _ = <expr>' to discard a result", kw);
        diagnostic_error_message(parser->diag, "E2084", arena_copy_string(parser->arena, msg),
            parser->file, node->token.line, node->token.column, 0);
        synchronize_parser(parser);
        return NULL;
    }

    /* Check for multi-var declaration: temp x int, y int = expr OR temp _, _ = expr */
    if (peek_token_is(parser, TOK_COMMA)) {
            /* Collect all variable names and types */
            const char *names[MAX_MULTI_VARS];
            const char *types[MAX_MULTI_VARS];
            int var_count = 0;
            names[var_count] = node->data.var_decl.name;
            types[var_count] = node->data.var_decl.type_name;
            var_count++;

            while (peek_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip comma */
                /* The binding name must be an identifier or '_'. Without
                 * this check a keyword is taken as the name and the token
                 * after it consumed as a type annotation. */
                if (!peek_token_is(parser, TOK_IDENT) && !peek_token_is(parser, TOK_BLANK)) {
                    if (is_keyword_token(parser->peek_token.type)) {
                        char msg[MSG_BUF_SIZE];
                        snprintf(msg, sizeof(msg),
                            "'%s' is a reserved keyword and cannot be used as a variable name",
                            parser->peek_token.literal);
                        diagnostic_error_message(parser->diag, "E2002",
                            arena_copy_string(parser->arena, msg),
                            parser->file, parser->peek_token.line, parser->peek_token.column, 0);
                        synchronize_parser(parser);
                        return NULL;
                    }
                    expect_peek_token(parser, TOK_IDENT); /* will error */
                    return NULL;
                }
                next_token(parser); /* name (IDENT or _) */
                if (var_count >= MAX_MULTI_VARS) {
                    diagnostic_error_code_formatted(parser->diag, "E2062", parser->file, parser->cur_token.line, parser->cur_token.column, 0, MAX_MULTI_VARS);
                    return NULL;
                }
                names[var_count] = parser->cur_token.literal;
                if (current_token_is(parser, TOK_BLANK)) names[var_count] = "_";
                if (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_CARET) ||
                    peek_token_is(parser, TOK_LBRACKET) || peek_token_is(parser, TOK_STRUCT) ||
                    peek_token_is(parser, TOK_ENUM) || peek_token_is(parser, TOK_QUESTION)) {
                    next_token(parser);
                    types[var_count] = parse_complex_type(parser);
                    if (!types[var_count]) return NULL;
                } else {
                    types[var_count] = NULL;
                }
                var_count++;
            }

            /* Expect = expr */
            if (!expect_peek_token(parser, TOK_ASSIGN)) return NULL;
            next_token(parser);
            AstNode *value = parse_expression(parser, PREC_LOWEST);

            /* Generate unique temp name */
            static int multi_var_counter = 0;
            char *tmp_name = arena_alloc(parser->arena, TMP_NAME_BUF);
            snprintf(tmp_name, TMP_NAME_BUF, GRAY_SYNTH_TMP "%d", multi_var_counter++);

            /* Create a block with: __auto_type _tmp = expr; type x = _tmp.v0; ... */
            AstNode *block = ast_alloc(parser->arena, NODE_BLOCK_STMT, parser->cur_token);
            block->data.block.cap = var_count + 1;
            block->data.block.count = 0;
            block->data.block.stmts = arena_alloc(parser->arena, sizeof(AstNode *) * block->data.block.cap);

            /* temp _tmp = value */
            AstNode *tmp_decl = ast_alloc(parser->arena, NODE_VAR_DECL, parser->cur_token);
            tmp_decl->data.var_decl.mutable = true;
            tmp_decl->data.var_decl.name = tmp_name;
            tmp_decl->data.var_decl.synthetic = true;
            tmp_decl->data.var_decl.type_name = NULL;
            tmp_decl->data.var_decl.value = value;
            block->data.block.stmts[block->data.block.count++] = tmp_decl;

            /* Individual declarations: type x = _tmp.v0 */
            for (int i = 0; i < var_count; i++) {
                AstNode *vd = ast_alloc(parser->arena, NODE_VAR_DECL, parser->cur_token);
                vd->data.var_decl.mutable = node->data.var_decl.mutable;
                vd->data.var_decl.name = names[i];
                vd->data.var_decl.type_name = types[i];
                /* Value: _gray_tmp.vN */
                AstNode *member = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
                AstNode *label = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
                label->data.label.value = tmp_name;
                member->data.member.object = label;
                char *field = arena_alloc(parser->arena, FIELD_NAME_BUF);
                snprintf(field, FIELD_NAME_BUF, "v%d", i);
                member->data.member.member = field;
                vd->data.var_decl.value = member;
                block->data.block.stmts[block->data.block.count++] = vd;
            }

            return block;
    }

    /* = value */
    if (peek_token_is(parser, TOK_ASSIGN)) {
        next_token(parser); /* skip = */
        next_token(parser);
        node->data.var_decl.value = parse_expression(parser, PREC_LOWEST);

        if (node->data.var_decl.value &&
            strcmp(node->data.var_decl.name, "_") == 0) {
            check_discard_target(parser, node->data.var_decl.value);
        }

        AstNode *desugared = maybe_apply_or_return(parser, node);
        if (desugared) return desugared;
    }

    return node;
}

static AstNode *parse_var_declaration(Parser *parser) {
    return parse_var_declaration_ex(parser, false);
}

static AstNode *parse_return_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_RETURN_STMT, parser->cur_token);

    int cap = GROW_ARRAY_INIT_CAP;
    int count = 0;
    AstNode **values = arena_alloc(parser->arena, sizeof(AstNode *) * cap);

    /* Check if there's a value to return (peek, don't consume) */
    if (!peek_token_is(parser, TOK_RBRACE) && !peek_token_is(parser, TOK_EOF)) {
        next_token(parser);
        values[count++] = parse_expression(parser, PREC_LOWEST);

        while (peek_token_is(parser, TOK_COMMA)) {
            next_token(parser); /* skip comma */
            next_token(parser);
            ARENA_GROW(parser->arena, values, count, cap);
            values[count++] = parse_expression(parser, PREC_LOWEST);
        }
    }

    node->data.return_stmt.values = values;
    node->data.return_stmt.count = count;
    return node;
}

static AstNode *parse_block_statement(Parser *parser) {
    parser->depth++;
    if (parser->depth > MAX_PARSE_DEPTH) {
        diagnostic_error_message(parser->diag, "E2001",
            arena_copy_string(parser->arena,"block is nested too deeply; maximum depth is 256"),
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        parser->depth--;
        return NULL;
    }

    AstNode *node = ast_alloc(parser->arena, NODE_BLOCK_STMT, parser->cur_token);
    node->data.block.count = 0;
    node->data.block.cap = GROW_ARRAY_INIT_CAP;
    node->data.block.stmts = arena_alloc(parser->arena, sizeof(AstNode *) * node->data.block.cap);

    next_token(parser); /* skip { */

    while (!current_token_is(parser, TOK_RBRACE) && !current_token_is(parser, TOK_EOF)) {
        AstNode *stmt = parse_statement(parser);
        if (stmt) {
            ARENA_GROW(parser->arena, node->data.block.stmts,
                node->data.block.count, node->data.block.cap);
            node->data.block.stmts[node->data.block.count++] = stmt;
        } else {
            /* Error recovery: skip to next statement boundary */
            synchronize_parser(parser);
            if (current_token_is(parser, TOK_RBRACE)) break;
            continue;
        }
        next_token(parser);
    }

    parser->depth--;
    return node;
}

static AstNode *parse_func_declaration(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_FUNC_DECL, parser->cur_token);

    if (is_keyword_token(parser->peek_token.type)) {
        char msg[MSG_BUF_SIZE];
        snprintf(msg, sizeof(msg),
            "'%s' is a reserved keyword and cannot be used as a function name",
            parser->peek_token.literal);
        diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
            parser->file, parser->peek_token.line, parser->peek_token.column, 0);
        synchronize_parser(parser);
        return NULL;
    }
    if (!expect_peek_token(parser, TOK_IDENT)) return NULL;
    node->data.func_decl.name = parser->cur_token.literal;

    /* Parameters */
    if (!expect_peek_token(parser, TOK_LPAREN)) return NULL;

    int param_cap = GROW_ARRAY_INIT_CAP;
    node->data.func_decl.param_count = 0;
    node->data.func_decl.params = arena_alloc(parser->arena, sizeof(Param) * param_cap);

    if (!peek_token_is(parser, TOK_RPAREN)) {
        next_token(parser);
        do {
            ARENA_GROW(parser->arena, node->data.func_decl.params,
                node->data.func_decl.param_count, param_cap);

            Param *param = &node->data.func_decl.params[node->data.func_decl.param_count];
            memset(param, 0, sizeof(Param));

            /* Check for mutable parameter (&) */
            if (current_token_is(parser, TOK_AMPERSAND)) {
                param->mutable = true;
                next_token(parser);
            }

            param->name = parser->cur_token.literal;

            /* Check for reserved names as parameters */
            if (parser->cur_token.type != TOK_IDENT && parser->cur_token.type != TOK_BLANK) {
                /* Keyword used as parameter name */
                char buf[MSG_BUF_SIZE];
                snprintf(buf, sizeof(buf),
                    "'%s' is a keyword and cannot be used as a parameter name",
                    param->name);
                diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, buf),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            } else if (parser->cur_token.type == TOK_IDENT && is_reserved_name(param->name)) {
                char buf[MSG_BUF_SIZE];
                snprintf(buf, sizeof(buf),
                    "'%s' is a built-in name and cannot be used as a parameter name",
                    param->name);
                diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, buf),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            }

            /* Type name follows (unless next param or closing paren) */
            if (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_CARET) ||
                peek_token_is(parser, TOK_LBRACKET) || peek_token_is(parser, TOK_QUESTION) ||
                peek_token_is(parser, TOK_LT)) {
                next_token(parser);
                if (current_token_is(parser, TOK_LT)) {
                    /* <?> type parameter syntax */
                    if (!expect_peek_token(parser, TOK_QUESTION)) return NULL;
                    if (!expect_peek_token(parser, TOK_GT)) return NULL;
                    param->type_name = "?";
                    param->is_type_param = true;
                } else {
                    param->type_name = parse_complex_type(parser);
                    if (!param->type_name) return NULL;
                }
            } else if (peek_token_is(parser, TOK_AMPERSAND)) {
                /* Common mistake: `name &type` instead of `&name type`.
                 * Without this, the loop has no token to consume and
                 * spins until killed externally (#bug-report). */
                diagnostic_error_code_formatted(parser->diag, "E3069", parser->file, parser->peek_token.line, parser->peek_token.column, 0, param->name, "type");
                return NULL;
            }

            /* Check for default value: param type = expr */
            if (peek_token_is(parser, TOK_ASSIGN)) {
                next_token(parser); /* skip = */
                next_token(parser);
                param->default_value = parse_expression(parser, PREC_LOWEST);
            }

            node->data.func_decl.param_count++;

            if (peek_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip comma */
                next_token(parser);
            } else if (!peek_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_RPAREN) &&
                       !current_token_is(parser, TOK_EOF)) {
                /* Forward-progress guard: any unexpected token between
                 * params that isn't ',' or ')' would otherwise loop
                 * forever. Surface it as a parse error and bail. */
                char buf[MSG_BUF_SIZE];
                snprintf(buf, sizeof(buf),
                    "unexpected token '%s' in parameter list; expected ',' or ')'",
                    parser->peek_token.literal ? parser->peek_token.literal : "?");
                diagnostic_error_message(parser->diag, "E2001", arena_copy_string(parser->arena, buf),
                    parser->file, parser->peek_token.line, parser->peek_token.column, 0);
                return NULL;
            }
        } while (!current_token_is(parser, TOK_RPAREN) && !peek_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF));
    }

    if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;

    /* Backfill grouped param types and defaults (a, b int = 0 → both get int, both default to 0) */
    for (int i = node->data.func_decl.param_count - 1; i >= 0; i--) {
        Param *p_i = &node->data.func_decl.params[i];
        if (!p_i->type_name && i + 1 < node->data.func_decl.param_count) {
            p_i->type_name = node->data.func_decl.params[i + 1].type_name;
            if (!p_i->default_value && node->data.func_decl.params[i + 1].default_value) {
                p_i->default_value = node->data.func_decl.params[i + 1].default_value;
            }
        }
        if (!p_i->type_name && !p_i->default_value) {
            char buf[MSG_BUF_SIZE];
            snprintf(buf, sizeof(buf),
                "parameter '%s' is missing a type; every parameter must have a type (e.g., %s int)",
                p_i->name, p_i->name);
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, buf),
                parser->file, node->token.line, node->token.column, 0);
        }
    }

    /* E2087: type parameters (<?>) cannot be mixed with value parameters */
    {
        bool has_type_param = false, has_value_param = false;
        for (int i = 0; i < node->data.func_decl.param_count; i++) {
            if (node->data.func_decl.params[i].is_type_param)
                has_type_param = true;
            else
                has_value_param = true;
        }
        if (has_type_param && has_value_param) {
            diagnostic_error_code(parser->diag, "E2087",
                parser->file, node->token.line, node->token.column, 0);
        }
    }

    /* Return type(s) */
    node->data.func_decl.return_type_count = 0;
    node->data.func_decl.return_types = NULL;
    node->data.func_decl.return_names = NULL;

    if (peek_token_is(parser, TOK_ARROW)) {
        next_token(parser); /* skip -> */
        next_token(parser);

        /* E2002: missing return type after -> */
        if (current_token_is(parser, TOK_LBRACE)) {
            diagnostic_error_message(parser->diag, "E2002",
                arena_copy_string(parser->arena, "expected return type after '->', got '{'; either specify a type or remove the '->'"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            /* Parse body to avoid cascading errors */
            node->data.func_decl.body = parse_block_statement(parser);
            return node;
        }

        /* E2079: reject 'nil' as a return type. For a function that
         * returns nothing, the user should omit the '-> ...' clause. */
        if (current_token_is(parser, TOK_NIL)) {
            diagnostic_error_code(parser->diag, "E2079",
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            /* Skip the nil and parse the body so the error doesn't
             * cascade into a codegen crash on an unknown C type. */
            next_token(parser);
            if (!current_token_is(parser, TOK_LBRACE)) {
                /* Malformed; bail with what we have. */
                return node;
            }
            node->data.func_decl.body = parse_block_statement(parser);
            return node;
        }

        int ret_cap = 16;
        node->data.func_decl.return_types = arena_alloc(parser->arena, sizeof(const char *) * ret_cap);
        node->data.func_decl.return_names = arena_alloc(parser->arena, sizeof(const char *) * ret_cap);
        memset(node->data.func_decl.return_names, 0, sizeof(const char *) * ret_cap);

        if (current_token_is(parser, TOK_LPAREN)) {
            /* Multiple/named return types:
             *   -> (int, string)        plain types
             *   -> (x int, y int)       named returns
             *   -> (x, y int)           shared type
             *
             * Disambiguation: if the identifier is a known type name,
             * it's a plain type list, not names.
             */
            next_token(parser);
            while (!current_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF)) {
                /* Check if current ident is a type name (not a variable name) */
                bool is_type = false;
                if (current_token_is(parser, TOK_IDENT)) {
                    const char *lit = parser->cur_token.literal;
                    is_type = (is_any_int_type(lit) ||
                        strcmp(lit, "float") == 0 || strcmp(lit, "f32") == 0 ||
                        strcmp(lit, "f64") == 0 || strcmp(lit, "string") == 0 ||
                        strcmp(lit, "bool") == 0 || strcmp(lit, "char") == 0 ||
                        (strcmp(lit, "map") == 0 && peek_token_is(parser, TOK_LBRACKET)) ||
                        (strcmp(lit, "func") == 0 && peek_token_is(parser, TOK_LPAREN)) ||
                        (lit[0] >= 'A' && lit[0] <= 'Z')); /* struct/enum types */
                }

                /* map[K:V] and func(...) are complex types, not named returns */
                bool is_complex_type_start = is_type && current_token_is(parser, TOK_IDENT) &&
                    ((strcmp(parser->cur_token.literal, "map") == 0 && peek_token_is(parser, TOK_LBRACKET)) ||
                     (strcmp(parser->cur_token.literal, "func") == 0 && peek_token_is(parser, TOK_LPAREN)));

                if (current_token_is(parser, TOK_IDENT) && !is_complex_type_start &&
                    (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_QUESTION) ||
                     peek_token_is(parser, TOK_LBRACKET) || peek_token_is(parser, TOK_CARET)) &&
                    (!is_type || peek_token_is(parser, TOK_IDENT) ||
                     peek_token_is(parser, TOK_CARET) || peek_token_is(parser, TOK_LBRACKET) ||
                     peek_token_is(parser, TOK_QUESTION))) {
                    /* Named return: name type; store both (: accept
                     * TOK_QUESTION, TOK_LBRACKET, TOK_CARET as type-start
                     * tokens so `(first ?, items [int], ptr ^T)` work) */
                    const char *ret_name = parser->cur_token.literal;
                    next_token(parser);
                    int idx = node->data.func_decl.return_type_count;
                    if (idx >= ret_cap) {
                        diagnostic_error_code_formatted(parser->diag, "E2060", parser->file, parser->cur_token.line, parser->cur_token.column, 0, MAX_SHARED_RETURNS);
                        return NULL;
                    }
                    node->data.func_decl.return_names[idx] = ret_name;
                    node->data.func_decl.return_types[idx] = parse_complex_type(parser);
                    node->data.func_decl.return_type_count++;
                } else if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_COMMA) && !is_type) {
                    /* Shared type: (x, y int); collect names, assign same type */
                    const char *names[MAX_SHARED_RETURNS];
                    int shared = 0;
                    names[shared++] = parser->cur_token.literal;
                    while (peek_token_is(parser, TOK_COMMA)) {
                        next_token(parser); /* skip comma */
                        next_token(parser); /* next name */
                        names[shared++] = parser->cur_token.literal;
                        if (shared >= MAX_SHARED_RETURNS) break;
                        if (!peek_token_is(parser, TOK_COMMA)) break;
                    }
                    /* cur is last name, peek should be the shared type */
                    if (peek_token_is(parser, TOK_IDENT)) {
                        next_token(parser);
                        for (int s = 0; s < shared; s++) {
                            int idx = node->data.func_decl.return_type_count;
                            if (idx >= ret_cap) {
                                diagnostic_error_code_formatted(parser->diag, "E2060", parser->file, parser->cur_token.line, parser->cur_token.column, 0, MAX_SHARED_RETURNS);
                                return NULL;
                            }
                            node->data.func_decl.return_names[idx] = names[s];
                            node->data.func_decl.return_types[idx] = read_type_name(parser);
                            node->data.func_decl.return_type_count++;
                        }
                    }
                } else {
                    /* Plain type (no name) — use parse_complex_type to
                     * handle array, map, and pointer return types like
                     * [string], map[K:V], ^T, not just simple idents. */
                    int idx = node->data.func_decl.return_type_count;
                    if (idx >= ret_cap) {
                        diagnostic_error_code_formatted(parser->diag, "E2060", parser->file, parser->cur_token.line, parser->cur_token.column, 0, MAX_SHARED_RETURNS);
                        return NULL;
                    }
                    node->data.func_decl.return_names[idx] = NULL;
                    node->data.func_decl.return_types[idx] = parse_complex_type(parser);
                    node->data.func_decl.return_type_count++;
                }
                if (peek_token_is(parser, TOK_COMMA)) {
                    next_token(parser);
                }
                next_token(parser);
            }
        } else {
            /* Single return type (array, pointer, map, or plain) */
            node->data.func_decl.return_types[0] = parse_complex_type(parser);
            if (!node->data.func_decl.return_types[0]) return NULL;
            node->data.func_decl.return_type_count = 1;

            /* E2081: catch `-> Foo^` — '^' after a type name is a dereference
             * operator, not a type modifier; the correct form is `-> ^Foo`. */
            if (peek_token_is(parser, TOK_CARET)) {
                const char *type_name = node->data.func_decl.return_types[0];
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "'^' is a dereference operator, not a type modifier; "
                    "for a pointer return type write '^%s', not '%s^'",
                    type_name, type_name);
                next_token(parser); /* consume the '^' so we can point at it */
                diagnostic_error_message(parser->diag, "E2081", arena_copy_string(parser->arena, msg),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                /* Recover: parse the body to avoid cascading errors. */
                if (peek_token_is(parser, TOK_LBRACE)) {
                    next_token(parser);
                    node->data.func_decl.body = parse_block_statement(parser);
                }
                return node;
            }
        }
    }

    /* Body */
    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    AstNode *saved_func = parser->current_func;
    parser->current_func = node;
    node->data.func_decl.body = parse_block_statement(parser);
    parser->current_func = saved_func;

    return node;
}

static AstNode *parse_import_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_IMPORT_STMT, parser->cur_token);

    int cap = GROW_ARRAY_INIT_CAP;
    node->data.import_stmt.count = 0;
    node->data.import_stmt.items = arena_alloc(parser->arena, sizeof(ImportItem) * cap);
    node->data.import_stmt.auto_use = false;

    do {
        next_token(parser);
        ImportItem *item = &node->data.import_stmt.items[node->data.import_stmt.count];
        memset(item, 0, sizeof(ImportItem));

        if (current_token_is(parser, TOK_IDENT) && strcmp(parser->cur_token.literal, "and") == 0) {
            /* import and use syntax; consume 'and' then 'use' */
            node->data.import_stmt.auto_use = true;
            next_token(parser); /* consume 'use' */
            next_token(parser); /* advance to alias or @module */
        }

        /* Check for C interop import: import c"header.h" */
        if (current_token_is(parser, TOK_IDENT) &&
            strcmp(parser->cur_token.literal, "c") == 0 &&
            peek_token_is(parser, TOK_STRING)) {
            next_token(parser); /* consume 'c', now on string */
            item->is_c_import = true;
            item->is_stdlib = false;
            item->path = parser->cur_token.literal;
            item->alias = "c";
            item->module = "c";
            /* Validate path: only [A-Za-z0-9./_+-] permitted to prevent injection */
            for (const char *q = item->path; *q; q++) {
                unsigned char c = (unsigned char)*q;
                bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') ||
                          c == '/' || c == '.' || c == '_' || c == '-' || c == '+';
                if (!ok) {
                    diagnostic_error(parser->diag, "E2080",
                        arena_copy_string(parser->arena, "invalid character in C header path; only [A-Za-z0-9./_+-] are permitted"),
                        parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                    break;
                }
            }
            goto import_item_done;
        }

        /* Check for alias: identifier followed by @ or string */
        if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_AT)) {
            if (strcmp(parser->cur_token.literal, "c") == 0) {
                diagnostic_error_message(parser->diag, "E2002",
                    arena_copy_string(parser->arena,"'c' is reserved for C interop; choose a different alias"),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            }
            item->alias = parser->cur_token.literal;
            next_token(parser); /* consume alias, now on @ */
        } else if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_STRING)) {
            item->alias = parser->cur_token.literal;
            next_token(parser); /* consume alias, now on string */
        }

        if (current_token_is(parser, TOK_AT)) {
            item->is_stdlib = true;
            next_token(parser);
            item->module = parser->cur_token.literal;
            if (!item->alias) item->alias = parser->cur_token.literal;
        } else if (current_token_is(parser, TOK_STRING)) {
            item->is_stdlib = false;
            item->path = parser->cur_token.literal;
            /* Derive module name from filename/directory if no alias */
            if (!item->alias) {
                const char *slash = strrchr(item->path, '/');
                const char *base = slash ? slash + 1 : item->path;
                size_t blen = strlen(base);
                if (blen > 5 && strcmp(base + blen - 5, ".gray") == 0) {
                    /* Strip .gray extension: "helpers.gray" → "helpers" */
                    char *mod = arena_alloc(parser->arena, blen - 4);
                    memcpy(mod, base, blen - 5);
                    mod[blen - 5] = '\0';
                    item->alias = mod;
                    item->module = mod;
                } else if (blen > 0) {
                    /* No .gray extension: use last path component as module name */
                    char *mod = arena_alloc(parser->arena, blen + 1);
                    memcpy(mod, base, blen);
                    mod[blen] = '\0';
                    item->alias = mod;
                    item->module = mod;
                }
            }
            /* Reject 'c' as a module name; reserved for C interop */
            if (item->alias && strcmp(item->alias, "c") == 0) {
                diagnostic_error_message(parser->diag, "E2002",
                    arena_copy_string(parser->arena,"'c' is reserved for C interop; rename the file or use an alias (e.g., 'import myc \"./c.gray\"')"),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            }
        } else if (current_token_is(parser, TOK_IDENT)) {
            char buf[MSG_BUF_SIZE];
            snprintf(buf, sizeof(buf),
                "expected '@module' or '\"path\"' after 'import', got '%s'",
                parser->cur_token.literal);
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, buf),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            return node;
        }

        import_item_done:
        node->data.import_stmt.count++;

        ARENA_GROW(parser->arena, node->data.import_stmt.items,
            node->data.import_stmt.count, cap);
    } while (peek_token_is(parser, TOK_COMMA) && (next_token(parser), 1));

    return node;
}

static AstNode *parse_using_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_USING_STMT, parser->cur_token);

    int cap = GROW_ARRAY_INIT_CAP;
    node->data.using_stmt.count = 0;
    node->data.using_stmt.modules = arena_alloc(parser->arena, sizeof(const char *) * cap);

    do {
        next_token(parser);
        ARENA_GROW(parser->arena, node->data.using_stmt.modules,
            node->data.using_stmt.count, cap);
        node->data.using_stmt.modules[node->data.using_stmt.count++] = parser->cur_token.literal;
    } while (peek_token_is(parser, TOK_COMMA) && (next_token(parser), 1));

    return node;
}

static AstNode *parse_if_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_IF_STMT, parser->cur_token);

    next_token(parser);
    node->data.if_stmt.condition = parse_expression(parser, PREC_LOWEST);

    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    node->data.if_stmt.consequence = parse_block_statement(parser);

    node->data.if_stmt.alternative = NULL;

    if (peek_token_is(parser, TOK_OR_KW)) {
        next_token(parser); /* skip 'or' */
        /* 'or' acts like 'else if' */
        node->data.if_stmt.alternative = parse_if_statement(parser);
    } else if (peek_token_is(parser, TOK_OTHERWISE)) {
        next_token(parser); /* skip 'otherwise'/'else' */
        node->data.if_stmt.else_token = parser->cur_token;
        if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
        node->data.if_stmt.alternative = parse_block_statement(parser);
    }

    return node;
}

static AstNode *parse_struct_declaration(Parser *parser) {
    /* cur_token is the struct name (IDENT), already consumed by caller */
    AstNode *node = ast_alloc(parser->arena, NODE_STRUCT_DECL, parser->cur_token);
    node->data.struct_decl.name = parser->cur_token.literal;

    next_token(parser); /* skip 'struct' keyword */
    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    int brace_line = parser->cur_token.line;
    next_token(parser); /* skip { */

    /* Reject inline struct declarations; fields must be on separate lines */
    if (parser->cur_token.line == brace_line && !current_token_is(parser, TOK_RBRACE)) {
        diagnostic_error_message(parser->diag, "E2002",
            arena_copy_string(parser->arena,"struct fields must be on separate lines; inline struct declarations are not allowed"),
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
    }

    int prev_field_line = -1;
    int field_cap = GROW_ARRAY_INIT_CAP;
    int func_cap = GROW_ARRAY_INIT_CAP;
    node->data.struct_decl.field_count = 0;
    node->data.struct_decl.fields = arena_alloc(parser->arena, sizeof(StructField) * field_cap);
    node->data.struct_decl.func_count = 0;
    node->data.struct_decl.funcs = arena_alloc(parser->arena, sizeof(StructFunc) * func_cap);

    bool pending_discard = false;
    bool pending_deprecated = false;
    const char *pending_deprecated_message = NULL;
    while (!current_token_is(parser, TOK_RBRACE) && !current_token_is(parser, TOK_EOF)) {
        /* : skip #doc attributes on struct functions. Consume
         * the attribute + any parenthesised args, then continue so
         * the next token (do/private do) is handled normally. */
        if (current_token_is(parser, TOK_DOC)) {
            if (peek_token_is(parser, TOK_LPAREN)) {
                next_token(parser);
                while (!current_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF))
                    next_token(parser);
            }
            next_token(parser);
            continue;
        }
        /* #discard inside struct body: set pending flag, then the
         * next iteration will attach it to the parsed function. */
        if (current_token_is(parser, TOK_DISCARD)) {
            pending_discard = true;
            next_token(parser);
            continue;
        }
        /* #deprecated inside struct body: same pending-flag treatment,
         * independent of pending_discard so both can stack on one function. */
        if (current_token_is(parser, TOK_DEPRECATED)) {
            next_token(parser); /* consume #deprecated */
            pending_deprecated = true;
            pending_deprecated_message = NULL;
            if (current_token_is(parser, TOK_LPAREN)) {
                next_token(parser); /* consume ( */
                if (current_token_is(parser, TOK_STRING)) {
                    pending_deprecated_message = arena_copy_string(parser->arena, parser->cur_token.literal);
                    next_token(parser); /* consume string */
                } else {
                    diagnostic_error_message(parser->diag, "E2002",
                        arena_copy_string(parser->arena,
                            "#deprecated expects a string literal message, e.g. #deprecated(\"use x() instead\")"),
                        parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                }
                if (current_token_is(parser, TOK_RPAREN)) {
                    next_token(parser); /* consume ) */
                } else {
                    diagnostic_error_message(parser->diag, "E2002",
                        arena_copy_string(parser->arena, "expected ')' after #deprecated message"),
                        parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                }
            }
            continue;
        }
        /* Check for struct-namespaced function: do func() or private do func() */
        if (current_token_is(parser, TOK_DO)) {
            AstNode *fn = parse_func_declaration(parser);
            if (fn) {
                if (pending_discard) {
                    fn->data.func_decl.is_discard = true;
                    pending_discard = false;
                }
                if (pending_deprecated) {
                    fn->data.func_decl.is_deprecated = true;
                    fn->data.func_decl.deprecated_message = pending_deprecated_message;
                    pending_deprecated = false;
                    pending_deprecated_message = NULL;
                }
                ARENA_GROW(parser->arena, node->data.struct_decl.funcs,
                    node->data.struct_decl.func_count, func_cap);
                node->data.struct_decl.funcs[node->data.struct_decl.func_count++].func_decl = fn;
            }
            next_token(parser);
            continue;
        }
        if (current_token_is(parser, TOK_PRIVATE) && peek_token_is(parser, TOK_DO)) {
            next_token(parser); /* consume 'private' */
            AstNode *fn = parse_func_declaration(parser);
            if (fn) {
                fn->data.func_decl.is_private = true;
                if (pending_discard) {
                    fn->data.func_decl.is_discard = true;
                    pending_discard = false;
                }
                if (pending_deprecated) {
                    fn->data.func_decl.is_deprecated = true;
                    fn->data.func_decl.deprecated_message = pending_deprecated_message;
                    pending_deprecated = false;
                    pending_deprecated_message = NULL;
                }
                ARENA_GROW(parser->arena, node->data.struct_decl.funcs,
                    node->data.struct_decl.func_count, func_cap);
                int idx = node->data.struct_decl.func_count++;
                node->data.struct_decl.funcs[idx].func_decl = fn;
                node->data.struct_decl.funcs[idx].is_private = true;
            }
            next_token(parser);
            continue;
        }

        /* E2058: nested struct/enum declaration */
        if (current_token_is(parser, TOK_CONST)) {
            diagnostic_error_code_formatted(parser->diag, "E2058",
                parser->file, parser->cur_token.line, parser->cur_token.column, 0,
                "struct", node->data.struct_decl.name);
            /* Skip to the end of the nested declaration to avoid cascading errors */
            int depth = 0;
            while (!current_token_is(parser, TOK_EOF)) {
                if (current_token_is(parser, TOK_LBRACE)) depth++;
                if (current_token_is(parser, TOK_RBRACE)) {
                    if (depth <= 1) { next_token(parser); break; }
                    depth--;
                }
                next_token(parser);
            }
            continue;
        }

        ARENA_GROW(parser->arena, node->data.struct_decl.fields,
            node->data.struct_decl.field_count, field_cap);

        /* E2070 ( follow-up): wildcard `?` in field-name position
         * used to slip past the struct-field guard; the existing check
         * further down only inspects the type slot; and embed '?' in
         * the generated C struct identifier, where clang rejected it
         * with a raw C error. Catch it here before reading the name. */
        if (current_token_is(parser, TOK_QUESTION)) {
            diagnostic_error_message(parser->diag, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' is not allowed as a struct field name; only in function parameter and return types"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            next_token(parser); /* skip the '?' */
            /* Skip the trailing type token (if any) so we don't cascade. */
            if (!current_token_is(parser, TOK_RBRACE) && !current_token_is(parser, TOK_EOF)) {
                parse_complex_type(parser);
                next_token(parser);
            }
            continue;
        }

        /* E2089: #discard on a struct field instead of a function */
        if (pending_discard) {
            diagnostic_error_code(parser->diag, "E2089",
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            pending_discard = false;
        }

        /* E2002: multiple fields on the same line */
        if (prev_field_line >= 0 && parser->cur_token.line == prev_field_line) {
            diagnostic_error_message(parser->diag, "E2002",
                arena_copy_string(parser->arena,"struct fields must be on separate lines"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        prev_field_line = parser->cur_token.line;

        /* Collect one or more comma-separated field names, then read the
         * shared type and backfill (mirrors the parameter grouping logic).
         * Example: `x, y, z float` → three fields, all typed float.       */
        int group_start = node->data.struct_decl.field_count;
        for (;;) {
            ARENA_GROW(parser->arena, node->data.struct_decl.fields,
                node->data.struct_decl.field_count, field_cap);
            /* Reject reserved keywords and type names as struct field names */
            if (is_keyword_token(parser->cur_token.type)) {
                char msg[MSG_BUF_SIZE];
                snprintf(msg, sizeof(msg),
                    "'%s' is a reserved keyword and cannot be used as a struct field name",
                    parser->cur_token.literal);
                diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                synchronize_parser(parser);
                break;
            }
            if (current_token_is(parser, TOK_IDENT) && is_reserved_name(parser->cur_token.literal)) {
                char msg[MSG_BUF_SIZE];
                snprintf(msg, sizeof(msg),
                    "'%s' is a built-in name and cannot be used as a struct field name",
                    parser->cur_token.literal);
                diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                synchronize_parser(parser);
                break;
            }
            StructField *field = &node->data.struct_decl.fields[node->data.struct_decl.field_count];
            field->name = parser->cur_token.literal;
            field->type_name = NULL;
            node->data.struct_decl.field_count++;
            next_token(parser);
            if (current_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip comma, loop for next name */
            } else {
                break;
            }
        }
        /* Current token is now the type; parse it and backfill all names in this group */
        const char *type_name = parse_complex_type(parser);
        if (!type_name) return NULL;
        /* E2070: wildcard `?` is not allowed as a struct field type */
        if (type_string_has_wildcard(type_name)) {
            diagnostic_error_message(parser->diag, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' cannot be used as a struct field type"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            return NULL;
        }
        for (int i = group_start; i < node->data.struct_decl.field_count; i++) {
            node->data.struct_decl.fields[i].type_name = type_name;
            node->data.struct_decl.fields[i].default_value = NULL;
        }
        next_token(parser);

        /* Parse optional default value: `= expr` */
        if (current_token_is(parser, TOK_ASSIGN)) {
            next_token(parser); /* skip '=' */
            AstNode *def = parse_expression(parser, PREC_LOWEST);
            for (int i = group_start; i < node->data.struct_decl.field_count; i++) {
                node->data.struct_decl.fields[i].default_value = def;
            }
            next_token(parser);
        }

        /* Skip optional trailing comma after a field type */
        if (current_token_is(parser, TOK_COMMA)) next_token(parser);

        /* Reject semicolons */
        if (current_token_is(parser, TOK_SEMICOLON)) {
            diagnostic_error_message(parser->diag, "E2069",
                arena_copy_string(parser->arena,"semicolons are not used; put each struct field on its own line"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            next_token(parser);
        }
    }

    return node;
}

static AstNode *parse_enum_declaration(Parser *parser) {
    /* cur_token is the enum name (IDENT), already consumed by caller */
    AstNode *node = ast_alloc(parser->arena, NODE_ENUM_DECL, parser->cur_token);
    node->data.enum_decl.name = parser->cur_token.literal;
    node->data.enum_decl.is_flags = false;
    node->data.enum_decl.is_tagged = false;

    next_token(parser); /* skip 'enum' keyword */
    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    int enum_brace_line = parser->cur_token.line;
    next_token(parser); /* skip { */

    /* Reject inline enum declarations; variants must be on separate lines */
    if (parser->cur_token.line == enum_brace_line && !current_token_is(parser, TOK_RBRACE)) {
        diagnostic_error_message(parser->diag, "E2002",
            arena_copy_string(parser->arena,"enum variants must be on separate lines; inline enum declarations are not allowed"),
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
    }

    int prev_variant_line = -1;
    int val_cap = GROW_ARRAY_INIT_CAP;
    node->data.enum_decl.value_count = 0;
    node->data.enum_decl.values = arena_alloc(parser->arena, sizeof(EnumVal) * val_cap);

    while (!current_token_is(parser, TOK_RBRACE) && !current_token_is(parser, TOK_EOF)) {
        ARENA_GROW(parser->arena, node->data.enum_decl.values,
            node->data.enum_decl.value_count, val_cap);

        /* E2058: nested struct/enum declaration */
        if (current_token_is(parser, TOK_CONST)) {
            diagnostic_error_code_formatted(parser->diag, "E2058",
                parser->file, parser->cur_token.line, parser->cur_token.column, 0,
                "enum", node->data.enum_decl.name);
            /* Skip to the end of the nested declaration to avoid cascading errors */
            int depth = 0;
            while (!current_token_is(parser, TOK_EOF)) {
                if (current_token_is(parser, TOK_LBRACE)) depth++;
                if (current_token_is(parser, TOK_RBRACE)) {
                    if (depth <= 1) { next_token(parser); break; }
                    depth--;
                }
                next_token(parser);
            }
            continue;
        }

        /* E2070 ( follow-up): wildcard `?` in variant-name position
         * used to slip past the parser and embed '?' in the generated C
         * enum identifier, where clang rejected it with a raw C error.
         * Catch it here before reading the variant name. */
        if (current_token_is(parser, TOK_QUESTION)) {
            diagnostic_error_message(parser->diag, "E2070",
                arena_copy_string(parser->arena,
                    "wildcard type '?' is not allowed in enum declarations; only in function parameter and return types"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            next_token(parser); /* skip the '?' */
            /* Skip an optional trailing ',' so we don't cascade into the
             * next variant with a stale cur_token. */
            if (current_token_is(parser, TOK_COMMA)) next_token(parser);
            continue;
        }

        /* Reject reserved names as enum variant names */
        if (is_keyword_token(parser->cur_token.type)) {
            char msg[MSG_BUF_SIZE];
            snprintf(msg, sizeof(msg),
                "'%s' is a reserved keyword and cannot be used as an enum variant name",
                parser->cur_token.literal);
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            synchronize_parser(parser);
            continue;
        }
        if (current_token_is(parser, TOK_IDENT) && is_reserved_name(parser->cur_token.literal)) {
            char msg[MSG_BUF_SIZE];
            snprintf(msg, sizeof(msg),
                "'%s' is a built-in name and cannot be used as an enum variant name",
                parser->cur_token.literal);
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            synchronize_parser(parser);
            continue;
        }

        /* E2002: multiple variants on the same line */
        if (prev_variant_line >= 0 && parser->cur_token.line == prev_variant_line) {
            diagnostic_error_message(parser->diag, "E2002",
                arena_copy_string(parser->arena,"enum variants must be on separate lines"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        prev_variant_line = parser->cur_token.line;

        EnumVal *variant = &node->data.enum_decl.values[node->data.enum_decl.value_count];
        variant->name = parser->cur_token.literal;
        variant->value = NULL;
        variant->payload_types = NULL;
        variant->payload_count = 0;

        /* Check for payload types: VARIANT(type1, type2, ...) */
        if (peek_token_is(parser, TOK_LPAREN)) {
            next_token(parser); /* consume ( */
            next_token(parser); /* first token of first type */
            int pt_cap = GROW_ARRAY_INIT_CAP;
            variant->payload_types = arena_alloc(parser->arena, sizeof(const char *) * pt_cap);
            while (!current_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF)) {
                ARENA_GROW(parser->arena, variant->payload_types, variant->payload_count, pt_cap);
                const char *type_str = parse_complex_type(parser);
                if (!type_str) return NULL;
                variant->payload_types[variant->payload_count++] = type_str;
                next_token(parser); /* advance past last token of type */
                if (current_token_is(parser, TOK_COMMA)) next_token(parser);
            }
            /* cur_token is now TOK_RPAREN */
        }

        /* Check for explicit value: VALUE = expr */
        if (peek_token_is(parser, TOK_ASSIGN)) {
            /* E2083: payload and explicit value are mutually exclusive */
            if (variant->payload_count > 0) {
                diagnostic_error_code_formatted(parser->diag, "E2083",
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0,
                    variant->name);
            }
            next_token(parser); /* skip = */
            next_token(parser);
            variant->value = parse_expression(parser, PREC_LOWEST);
        }

        node->data.enum_decl.value_count++;
        /* Skip optional trailing comma */
        if (peek_token_is(parser, TOK_COMMA)) {
            next_token(parser);
        }
        next_token(parser);

        /* Reject semicolons */
        if (current_token_is(parser, TOK_SEMICOLON)) {
            diagnostic_error_message(parser->diag, "E2069",
                arena_copy_string(parser->arena,"semicolons are not used; put each enum variant on its own line"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            next_token(parser);
        }
    }

    /* Set is_tagged if any variant has a payload */
    for (int j = 0; j < node->data.enum_decl.value_count; j++) {
        if (node->data.enum_decl.values[j].payload_count > 0) {
            node->data.enum_decl.is_tagged = true;
            break;
        }
    }

    return node;
}

/* Parse struct literal: StructName{field: value, ...} */
static AstNode *parse_struct_literal(Parser *parser, const char *name) {
    AstNode *node = ast_alloc(parser->arena, NODE_STRUCT_VALUE, parser->cur_token);
    node->data.struct_value.name = name;

    int cap = GROW_ARRAY_INIT_CAP;
    node->data.struct_value.count = 0;
    node->data.struct_value.field_names = arena_alloc(parser->arena, sizeof(const char *) * cap);
    node->data.struct_value.field_values = arena_alloc(parser->arena, sizeof(AstNode *) * cap);

    next_token(parser); /* skip { */

    while (!current_token_is(parser, TOK_RBRACE) && !current_token_is(parser, TOK_EOF)) {
        if (node->data.struct_value.count >= cap) {
            cap = GROW_NEXT_CAP(cap);
            ARENA_GROW_TO(parser->arena, node->data.struct_value.field_names,
                node->data.struct_value.count, cap);
            ARENA_GROW_TO(parser->arena, node->data.struct_value.field_values,
                node->data.struct_value.count, cap);
        }

        node->data.struct_value.field_names[node->data.struct_value.count] = parser->cur_token.literal;

        if (!expect_peek_token(parser, TOK_COLON)) return NULL;
        next_token(parser);
        node->data.struct_value.field_values[node->data.struct_value.count] =
            parse_expression(parser, PREC_LOWEST);
        node->data.struct_value.count++;

        if (peek_token_is(parser, TOK_COMMA)) {
            next_token(parser);
        }
        next_token(parser);
    }

    return node;
}

static AstNode *parse_ensure_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_ENSURE_STMT, parser->cur_token);
    next_token(parser);
    node->data.ensure_stmt.expr = parse_expression(parser, PREC_LOWEST);
    return node;
}

static AstNode *parse_for_statement(Parser *parser) {
    Token for_tok = parser->cur_token;

    /* Optional parentheses: for (i in range(...)) */
    bool has_parens = peek_token_is(parser, TOK_LPAREN);
    if (has_parens) next_token(parser);

    if (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_BLANK)) {
        next_token(parser);  /* advance: cur_token = IDENT or BLANK */
        if (peek_token_is(parser, TOK_IN)) {
            /* --- iteration form: for x in range(...) { } --- */
            /* 'for x in ...' is only valid with range().
             * For collection iteration, users must use for_each. */
            const char *var = parser->cur_token.literal;
            AstNode *node = ast_alloc(parser->arena, NODE_FOR_STMT, for_tok);
            node->data.for_stmt.var_name = var;
            node->data.for_stmt.var_type = NULL;
            next_token(parser);  /* consume IN */
            next_token(parser);  /* advance to iterable start */
            if (!current_token_is(parser, TOK_RANGE)) {
                char msg[MSG_BUF_SIZE];
                snprintf(msg, sizeof(msg),
                    "'for %s in ...' only supports 'range()'; use 'for_each %s in ...' to iterate over a collection",
                    var, var);
                diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                    parser->file, for_tok.line, for_tok.column, 0);
                synchronize_parser(parser);
                return NULL;
            }
            node->data.for_stmt.iterable = parse_expression(parser, PREC_LOWEST);
            if (has_parens && peek_token_is(parser, TOK_RPAREN)) next_token(parser);
            if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
            node->data.for_stmt.body = parse_block_statement(parser);
            return node;
        }
        /* else: cur_token = IDENT, fall through to while-style */
    } else {
        next_token(parser);  /* advance to condition start token */
    }

    /* --- while-style: for condition { } --- */
    AstNode *wnode = ast_alloc(parser->arena, NODE_WHILE_STMT, for_tok);
    wnode->data.while_stmt.condition = parse_expression(parser, PREC_LOWEST);
    if (has_parens && peek_token_is(parser, TOK_RPAREN)) next_token(parser);
    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    wnode->data.while_stmt.body = parse_block_statement(parser);
    return wnode;
}

static AstNode *parse_for_each_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_FOR_EACH_STMT, parser->cur_token);

    next_token(parser);

    /* Optional parentheses: for_each (val in arr) {} */
    bool has_paren = current_token_is(parser, TOK_LPAREN);
    if (has_paren) next_token(parser);

    node->data.for_each.index_name = NULL;
    node->data.for_each.var_name = parser->cur_token.literal;

    /* Check for index, value pattern: for_each i, item in collection */
    if (peek_token_is(parser, TOK_COMMA)) {
        node->data.for_each.index_name = node->data.for_each.var_name;
        next_token(parser); /* skip comma */
        next_token(parser);
        node->data.for_each.var_name = parser->cur_token.literal;
    }

    if (!expect_peek_token(parser, TOK_IN)) return NULL;

    next_token(parser);
    /* Parse collection carefully; prevent struct literal parser from consuming
     * the block-opening { after identifiers or module.Name expressions */
    if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_LBRACE)) {
        /* Bare identifier followed by {; parse as label only */
        node->data.for_each.collection = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
        node->data.for_each.collection->data.label.value = parser->cur_token.literal;
    } else if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_DOT)) {
        /* Chained member access: a.b.c; parse manually to avoid
         * parse_expression triggering struct literal parsing on { */
        AstNode *result = ast_alloc(parser->arena, NODE_LABEL, parser->cur_token);
        result->data.label.value = parser->cur_token.literal;
        while (peek_token_is(parser, TOK_DOT)) {
            next_token(parser); /* consume . */
            next_token(parser); /* move to member */
            AstNode *member = ast_alloc(parser->arena, NODE_MEMBER_EXPR, parser->cur_token);
            member->data.member.object = result;
            member->data.member.member = parser->cur_token.literal;
            result = member;
        }
        /* If the chain is followed by (, it is a function call (e.g., maps.get_keys(m)) */
        if (peek_token_is(parser, TOK_LPAREN)) {
            next_token(parser); /* move to ( */
            result = parse_call_expression(parser, result);
        }
        node->data.for_each.collection = result;
    } else {
        node->data.for_each.collection = parse_expression(parser, PREC_LOWEST);
    }

    if (has_paren) {
        if (!expect_peek_token(parser, TOK_RPAREN)) return NULL;
    }

    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    node->data.for_each.body = parse_block_statement(parser);

    return node;
}

static AstNode *parse_while_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_WHILE_STMT, parser->cur_token);

    next_token(parser);
    node->data.while_stmt.condition = parse_expression(parser, PREC_LOWEST);

    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    node->data.while_stmt.body = parse_block_statement(parser);

    return node;
}

static AstNode *parse_loop_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_LOOP_STMT, parser->cur_token);

    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    node->data.loop_stmt.body = parse_block_statement(parser);

    return node;
}

/* --- Speculative parse helpers for when-pattern detection --- */

typedef struct {
    int position, read_position;
    char ch;
    int line, column;
    Token cur_token, peek_token;
} ParserSnapshot;

static void parser_snapshot_save(Parser *parser, ParserSnapshot *snap) {
    snap->position = parser->lexer->position;
    snap->read_position = parser->lexer->read_position;
    snap->ch = parser->lexer->ch;
    snap->line = parser->lexer->line;
    snap->column = parser->lexer->column;
    snap->cur_token = parser->cur_token;
    snap->peek_token = parser->peek_token;
}

static void parser_snapshot_restore(Parser *parser, const ParserSnapshot *snap) {
    parser->lexer->position = snap->position;
    parser->lexer->read_position = snap->read_position;
    parser->lexer->ch = snap->ch;
    parser->lexer->line = snap->line;
    parser->lexer->column = snap->column;
    parser->cur_token = snap->cur_token;
    parser->peek_token = snap->peek_token;
}

/* Check if current position holds IDENT(IDENT, ..., IDENT).
 * Assumes cur_token is the IDENT before LPAREN and peek is LPAREN.
 * Consumes tokens past the closing paren (caller must restore). */
static bool scan_paren_bindings(Parser *parser) {
    next_token(parser); /* skip IDENT */
    next_token(parser); /* skip ( */
    int bind_count = 0;
    while (!current_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF)) {
        if (!current_token_is(parser, TOK_IDENT)) return false;
        bind_count++;
        next_token(parser);
        if (current_token_is(parser, TOK_COMMA)) next_token(parser);
    }
    return bind_count > 0 && current_token_is(parser, TOK_RPAREN);
}

static AstNode *parse_when_statement(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_WHEN_STMT, parser->cur_token);

    next_token(parser);
    parser->no_struct_literal = true;
    node->data.when_stmt.value = parse_expression(parser, PREC_LOWEST);
    parser->no_struct_literal = false;
    node->data.when_stmt.is_strict = false;
    node->data.when_stmt.default_body = NULL;

    if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
    next_token(parser); /* skip { */

    int case_cap = GROW_ARRAY_INIT_CAP;
    node->data.when_stmt.case_count = 0;
    node->data.when_stmt.cases = arena_alloc(parser->arena, sizeof(WhenCase) * case_cap);

    while (!current_token_is(parser, TOK_RBRACE) && !current_token_is(parser, TOK_EOF)) {
        if (current_token_is(parser, TOK_IS)) {
            ARENA_GROW(parser->arena, node->data.when_stmt.cases,
                node->data.when_stmt.case_count, case_cap);

            WhenCase *when_case = &node->data.when_stmt.cases[node->data.when_stmt.case_count];
            memset(when_case, 0, sizeof(WhenCase));
            when_case->kw_token = parser->cur_token;

            int val_cap = GROW_ARRAY_INIT_CAP;
            when_case->value_count = 0;
            when_case->values = arena_alloc(parser->arena, sizeof(AstNode *) * val_cap);
            when_case->is_range = false;

            /* Parse case values: is 1, 2, 3 { } */
            next_token(parser);

            /* Detect destructuring pattern: IDENT(IDENT,...) or .IDENT(IDENT,...)
             * All tokens inside parens must be bare identifiers (no operators/literals). */
            {
                bool try_pattern = false;
                bool is_implicit_pat = false;
                bool is_explicit_enum = false;
                bool is_qualified_enum = false;
                Token pat_tok = parser->cur_token;

                if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_LPAREN)) {
                    /* IDENT(IDENT,...) pattern form */
                    ParserSnapshot snap;
                    parser_snapshot_save(parser, &snap);
                    try_pattern = scan_paren_bindings(parser);
                    parser_snapshot_restore(parser, &snap);
                } else if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_DOT)) {
                    /* IDENT.IDENT(IDENT,...) explicit enum pattern form, or
                     * mod.IDENT.IDENT(IDENT,...) for an imported enum */
                    ParserSnapshot snap;
                    parser_snapshot_save(parser, &snap);
                    next_token(parser); /* skip enum name */
                    next_token(parser); /* skip DOT */
                    if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_DOT)) {
                        next_token(parser); /* skip enum name */
                        next_token(parser); /* skip DOT */
                        is_qualified_enum = true;
                    }
                    if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_LPAREN)) {
                        try_pattern = scan_paren_bindings(parser);
                        if (try_pattern) is_explicit_enum = true;
                    }
                    if (!try_pattern) is_qualified_enum = false;
                    parser_snapshot_restore(parser, &snap);
                } else if (current_token_is(parser, TOK_DOT) && peek_token_is(parser, TOK_IDENT)) {
                    /* .IDENT(IDENT,...) implicit pattern form */
                    ParserSnapshot snap;
                    parser_snapshot_save(parser, &snap);
                    next_token(parser); /* skip dot */
                    if (peek_token_is(parser, TOK_LPAREN)) {
                        try_pattern = scan_paren_bindings(parser);
                        if (try_pattern) is_implicit_pat = true;
                    }
                    parser_snapshot_restore(parser, &snap);
                }

                if (try_pattern) {
                    /* Actually consume and build NODE_WHEN_PATTERN */
                    AstNode *pat = ast_alloc(parser->arena, NODE_WHEN_PATTERN, pat_tok);
                    pat->data.when_pattern.is_implicit = is_implicit_pat;

                    if (is_explicit_enum) {
                        pat->data.when_pattern.enum_name = arena_copy_string(parser->arena, parser->cur_token.literal);
                        next_token(parser); /* skip enum name (or module) */
                        next_token(parser); /* skip dot */
                        if (is_qualified_enum) {
                            char qualified[MSG_BUF_SIZE];
                            snprintf(qualified, sizeof(qualified), "%s_%s",
                                pat->data.when_pattern.enum_name, parser->cur_token.literal);
                            pat->data.when_pattern.enum_name = arena_copy_string(parser->arena, qualified);
                            next_token(parser); /* skip enum name */
                            next_token(parser); /* skip dot */
                        }
                    } else {
                        pat->data.when_pattern.enum_name = NULL;
                        if (is_implicit_pat) {
                            next_token(parser); /* skip dot */
                        }
                    }
                    pat->data.when_pattern.variant = arena_copy_string(parser->arena, parser->cur_token.literal);
                    next_token(parser); /* skip IDENT (variant) */
                    next_token(parser); /* skip ( */

                    int binding_count = 0, binding_cap = GROW_ARRAY_INIT_CAP;
                    pat->data.when_pattern.bindings = arena_alloc(parser->arena, sizeof(const char *) * binding_cap);
                    while (!current_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF)) {
                        ARENA_GROW(parser->arena, pat->data.when_pattern.bindings, binding_count, binding_cap);
                        /* Reject reserved names as binding names */
                        if (is_reserved_name(parser->cur_token.literal)) {
                            char msg[MSG_BUF_SIZE];
                            snprintf(msg, sizeof(msg),
                                "'%s' is a built-in name and cannot be used as a binding name",
                                parser->cur_token.literal);
                            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
                        }
                        pat->data.when_pattern.bindings[binding_count++] = arena_copy_string(parser->arena, parser->cur_token.literal);
                        next_token(parser);
                        if (current_token_is(parser, TOK_COMMA)) next_token(parser);
                    }
                    pat->data.when_pattern.binding_count = binding_count;
                    /* cur_token is RPAREN, peek should be LBRACE */

                    when_case->values[when_case->value_count++] = pat;
                    goto when_case_body;
                }
            }

            if (current_token_is(parser, TOK_RANGE)) {
                when_case->is_range = true;
            }
            parser->no_struct_literal = true;
            if (when_case->value_count < val_cap) {
                when_case->values[when_case->value_count++] = parse_expression(parser, PREC_LOWEST);
            }
            while (peek_token_is(parser, TOK_COMMA)) {
                next_token(parser); /* skip comma */
                next_token(parser); /* next value */
                ARENA_GROW(parser->arena, when_case->values, when_case->value_count, val_cap);
                if (current_token_is(parser, TOK_RANGE)) {
                    when_case->is_range = true;
                }
                when_case->values[when_case->value_count++] = parse_expression(parser, PREC_LOWEST);
            }

            when_case_body:
            parser->no_struct_literal = false;
            if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
            when_case->body = parse_block_statement(parser);
            node->data.when_stmt.case_count++;

        } else if (current_token_is(parser, TOK_DEFAULT)) {
            if (node->data.when_stmt.default_body) {
                diagnostic_error_code(parser->diag, "E2085", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            }
            if (!expect_peek_token(parser, TOK_LBRACE)) return NULL;
            node->data.when_stmt.default_body = parse_block_statement(parser);
        }

        next_token(parser);
    }

    /* E2059: empty when block */
    if (node->data.when_stmt.case_count == 0 && node->data.when_stmt.default_body == NULL) {
        diagnostic_error_code(parser->diag, "E2059", parser->file, node->token.line, node->token.column, 0);
    }

    return node;
}

static AstNode *parse_alias_declaration(Parser *parser) {
    AstNode *node = ast_alloc(parser->arena, NODE_ALIAS_DECL, parser->cur_token);

    if (!expect_peek_token(parser, TOK_IDENT)) return NULL;
    node->data.alias_decl.name = parser->cur_token.literal;
    node->data.alias_decl.is_private = false;

    if (!expect_peek_token(parser, TOK_ASSIGN)) return NULL;
    next_token(parser); /* advance to the type */

    /* E3134: detect module-qualified type (Ident.Ident) before parse_complex_type
     * rewrites it with underscore prefixing. */
    if (current_token_is(parser, TOK_IDENT) && peek_token_is(parser, TOK_DOT)) {
        char msg[MSG_BUF_SIZE];
        snprintf(msg, sizeof(msg),
            "alias '%s' cannot target a module-qualified type; only local types can be aliased",
            node->data.alias_decl.name);
        diagnostic_error_message(parser->diag, "E3134", arena_copy_string(parser->arena, msg),
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        synchronize_parser(parser);
        return NULL;
    }

    const char *target = parse_complex_type(parser);
    if (!target) return NULL;
    node->data.alias_decl.target_type = target;
    return node;
}

static bool is_assignment_operator(TokenType type) {
    return type == TOK_ASSIGN || type == TOK_PLUS_ASSIGN || type == TOK_MINUS_ASSIGN ||
           type == TOK_ASTERISK_ASSIGN || type == TOK_SLASH_ASSIGN || type == TOK_PERCENT_ASSIGN;
}

static AstNode *parse_statement(Parser *parser) {
    switch (parser->cur_token.type) {
    case TOK_PRIVATE: {
        /* private do / private const / private mut; consume and set flag */
        next_token(parser);
        AstNode *stmt = parse_statement(parser);
        if (stmt) {
            if (stmt->kind == NODE_FUNC_DECL) {
                stmt->data.func_decl.is_private = true;
            } else if (stmt->kind == NODE_VAR_DECL) {
                stmt->data.var_decl.is_private = true;
            } else if (stmt->kind == NODE_ALIAS_DECL) {
                stmt->data.alias_decl.is_private = true;
            }
        }
        return stmt;
    }
    case TOK_MUT:
    case TOK_CONST:
        /* Check for keyword used as name: const for struct / mut for int */
        if (is_keyword_token(parser->peek_token.type)) {
            char msg[MSG_BUF_SIZE];
            snprintf(msg, sizeof(msg),
                "'%s' is a reserved keyword and cannot be used as a name",
                parser->peek_token.literal);
            diagnostic_error_message(parser->diag, "E2002", arena_copy_string(parser->arena, msg),
                parser->file, parser->peek_token.line, parser->peek_token.column, 0);
            synchronize_parser(parser);
            return NULL;
        }
        /* Check if this is a struct or enum declaration: const Name struct { */
        if (parser->cur_token.type == TOK_CONST && peek_token_is(parser, TOK_IDENT)) {
            /* Save full parser state for lookahead */
            Token saved_cur = parser->cur_token;
            Token saved_peek = parser->peek_token;
            int saved_pos = parser->lexer->position;
            int saved_rpos = parser->lexer->read_position;
            char saved_ch = parser->lexer->ch;
            int saved_line = parser->lexer->line;
            int saved_col = parser->lexer->column;

            next_token(parser); /* now on IDENT (name) */
            if (peek_token_is(parser, TOK_STRUCT)) {
                return parse_struct_declaration(parser);
            }
            if (peek_token_is(parser, TOK_ENUM)) {
                return parse_enum_declaration(parser);
            }
            /* Not struct/enum; restore full state */
            parser->cur_token = saved_cur;
            parser->peek_token = saved_peek;
            parser->lexer->position = saved_pos;
            parser->lexer->read_position = saved_rpos;
            parser->lexer->ch = saved_ch;
            parser->lexer->line = saved_line;
            parser->lexer->column = saved_col;
        }
        return parse_var_declaration(parser);
    case TOK_DO:
        return parse_func_declaration(parser);
    case TOK_RETURN:
        return parse_return_statement(parser);
    case TOK_MODULE:
        diagnostic_error_code(parser->diag, "E2061", parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        next_token(parser); /* consume module name */
        return NULL;
    case TOK_IMPORT:
        return parse_import_statement(parser);
    case TOK_USING:
        return parse_using_statement(parser);
    case TOK_IF:
        return parse_if_statement(parser);
    case TOK_FOR:
        return parse_for_statement(parser);
    case TOK_FOR_EACH:
        return parse_for_each_statement(parser);
    case TOK_AS_LONG_AS:
        return parse_while_statement(parser);
    case TOK_LOOP:
        return parse_loop_statement(parser);
    case TOK_BREAK:
        return ast_alloc(parser->arena, NODE_BREAK_STMT, parser->cur_token);
    case TOK_CONTINUE:
        return ast_alloc(parser->arena, NODE_CONTINUE_STMT, parser->cur_token);
    case TOK_WHEN:
        return parse_when_statement(parser);
    case TOK_ALIAS:
        return parse_alias_declaration(parser);
    case TOK_STRICT:
        /* #strict; applies to the next when statement */
        next_token(parser);
        if (current_token_is(parser, TOK_WHEN)) {
            AstNode *ws = parse_when_statement(parser);
            if (ws) ws->data.when_stmt.is_strict = true;
            return ws;
        }
        /* If not followed by when, just skip */
        return parse_statement(parser);
    case TOK_FLAGS: {
        /* #flags; applies to the next enum declaration */
        next_token(parser); /* skip #flags */
        AstNode *stmt = parse_statement(parser);
        if (stmt && stmt->kind == NODE_ENUM_DECL) {
            stmt->data.enum_decl.is_flags = true;
        }
        return stmt;
    }
    case TOK_JSON_ATTR: {
        /* #json; applies to the next struct declaration () */
        next_token(parser);
        AstNode *stmt = parse_statement(parser);
        if (stmt && stmt->kind == NODE_STRUCT_DECL) {
            stmt->data.struct_decl.is_json = true;
        } else {
            diagnostic_error_message(parser->diag, "E2002",
                arena_copy_string(parser->arena,"#json attribute can only be applied to struct declarations"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        return stmt;
    }
    case TOK_DISCARD: {
        /* #discard; applies to the next function declaration */
        next_token(parser);
        AstNode *stmt = parse_statement(parser);
        if (stmt && stmt->kind == NODE_FUNC_DECL) {
            stmt->data.func_decl.is_discard = true;
        } else {
            diagnostic_error_message(parser->diag, "E2002",
                arena_copy_string(parser->arena, "#discard attribute can only be applied to function declarations"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        return stmt;
    }
    case TOK_DEPRECATED: {
        /* #deprecated or #deprecated("message"); applies to the next
         * function, struct, or enum declaration. */
        next_token(parser); /* consume #deprecated */
        const char *message = NULL;
        if (current_token_is(parser, TOK_LPAREN)) {
            next_token(parser); /* consume ( */
            if (current_token_is(parser, TOK_STRING)) {
                message = arena_copy_string(parser->arena, parser->cur_token.literal);
                next_token(parser); /* consume string */
            } else {
                diagnostic_error_message(parser->diag, "E2002",
                    arena_copy_string(parser->arena,
                        "#deprecated expects a string literal message, e.g. #deprecated(\"use x() instead\")"),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            }
            if (current_token_is(parser, TOK_RPAREN)) {
                next_token(parser); /* consume ) */
            } else {
                diagnostic_error_message(parser->diag, "E2002",
                    arena_copy_string(parser->arena, "expected ')' after #deprecated message"),
                    parser->file, parser->cur_token.line, parser->cur_token.column, 0);
            }
        }
        AstNode *stmt = parse_statement(parser);
        if (stmt && stmt->kind == NODE_FUNC_DECL) {
            stmt->data.func_decl.is_deprecated = true;
            stmt->data.func_decl.deprecated_message = message;
        } else if (stmt && stmt->kind == NODE_STRUCT_DECL) {
            stmt->data.struct_decl.is_deprecated = true;
            stmt->data.struct_decl.deprecated_message = message;
        } else if (stmt && stmt->kind == NODE_ENUM_DECL) {
            stmt->data.enum_decl.is_deprecated = true;
            stmt->data.enum_decl.deprecated_message = message;
        } else {
            diagnostic_error_message(parser->diag, "E2002",
                arena_copy_string(parser->arena,
                    "#deprecated attribute can only be applied to function, struct, or enum declarations"),
                parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        }
        return stmt;
    }
    case TOK_DOC:
        /* Skip #doc attribute tokens; consume args if present */
        if (peek_token_is(parser, TOK_LPAREN)) {
            next_token(parser);
            while (!current_token_is(parser, TOK_RPAREN) && !current_token_is(parser, TOK_EOF)) {
                next_token(parser);
            }
        }
        next_token(parser);
        return parse_statement(parser);
    case TOK_ENSURE:
        return parse_ensure_statement(parser);
    case TOK_BLANK:
        /* Bare throwaway: `_ = expr` discards the RHS without creating
         * a symbol. `_, x = func()` is a bare multi-var declaration. */
        if (peek_token_is(parser, TOK_ASSIGN)) {
            return parse_discard_statement(parser);
        }
        if (peek_token_is(parser, TOK_COMMA)) {
            return parse_var_declaration_ex(parser, true);
        }
        diagnostic_error_message(parser->diag, "E2002",
            arena_copy_string(parser->arena,"unexpected token '_'; the throwaway '_' is only valid as the entire left-hand side of an assignment"),
            parser->file, parser->cur_token.line, parser->cur_token.column, 0);
        synchronize_parser(parser);
        return NULL;
    default: {
        /* Bare variable declaration: x int = 5  or  x, err = func()
         * Also handles array types: x [int] = {1,2,3}
         * Whitespace before '[' disambiguates from index expressions (E2075).
         * The name and what follows it must share a line, so a bare
         * identifier cannot swallow the next statement's first token. */
        if (current_token_is(parser, TOK_IDENT) &&
            parser->peek_token.line == parser->cur_token.line &&
            (peek_token_is(parser, TOK_IDENT) || peek_token_is(parser, TOK_COMMA) ||
             (peek_token_is(parser, TOK_LBRACKET) && parser->peek_token.preceded_by_ws))) {
            return parse_var_declaration_ex(parser, true);
        }
        /* Could be assignment or expression statement */
        AstNode *expr = parse_expression(parser, PREC_LOWEST);
        if (!expr) return NULL;

        /* Check for assignment */
        if (is_assignment_operator(parser->peek_token.type)) {
            next_token(parser);
            AstNode *node = ast_alloc(parser->arena, NODE_ASSIGN_STMT, parser->cur_token);
            node->data.assign.target = expr;
            node->data.assign.op = parser->cur_token.type;
            next_token(parser);
            node->data.assign.value = parse_expression(parser, PREC_LOWEST);
            return node;
        }

        AstNode *node = ast_alloc(parser->arena, NODE_EXPR_STMT, parser->cur_token);
        node->data.expr_stmt.expr = expr;
        return node;
    }
    }
}

/* --- Public API --- */

Parser *parser_create(Arena *arena, Lexer *lexer, const char *file, DiagnosticList *diag) {
    Parser *parser = arena_alloc(arena, sizeof(Parser));
    parser->lexer = lexer;
    parser->arena = arena;
    parser->file = file;
    parser->diag = diag;
    parser->depth = 0;
    parser->no_struct_literal = false;
    parser->in_interp = false;
    parser->current_func = NULL;

    /* Read two tokens to fill cur and peek */
    next_token(parser);
    next_token(parser);

    return parser;
}

AstNode *parser_parse_program(Parser *parser) {
    Token tok = {TOK_EOF, "", 0, 0, NULL, false};
    AstNode *program = ast_alloc(parser->arena, NODE_PROGRAM, tok);
    program->data.program.module_decl = NULL;
    program->data.program.using_stmts = NULL;
    program->data.program.using_count = 0;
    program->data.program.stmt_count = 0;
    program->data.program.stmt_cap = GROW_ARRAY_INIT_CAP;
    program->data.program.stmts = arena_alloc(parser->arena,
        sizeof(AstNode *) * program->data.program.stmt_cap);

    while (!current_token_is(parser, TOK_EOF)) {
        AstNode *stmt = parse_statement(parser);
        if (stmt) {
            ARENA_GROW(parser->arena, program->data.program.stmts,
                program->data.program.stmt_count, program->data.program.stmt_cap);
            program->data.program.stmts[program->data.program.stmt_count++] = stmt;
        } else {
            /* Error recovery: skip to next statement boundary */
            synchronize_parser(parser);
            continue;
        }
        next_token(parser);
    }

    return program;
}
