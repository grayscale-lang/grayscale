/*
 * token.h — Token type enumeration and Token struct for the Grayscale lexer.
 * Defines all token kinds (operators, keywords, literals, delimiters) and
 * the lookup interface for resolving identifiers to keyword tokens.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_TOKEN_H
#define GRAYC_TOKEN_H

#include <stdbool.h>

typedef enum {
    /* Special tokens */
    TOK_ILLEGAL,
    TOK_EOF,

    /* Identifiers and literals */
    TOK_IDENT,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_RAW_STRING,
    TOK_CHAR,

    /* Operators */
    TOK_ASSIGN,         /* = */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_BANG,           /* ! */
    TOK_ASTERISK,       /* * */
    TOK_SLASH,          /* / */
    TOK_PERCENT,        /* % */

    /* Comparison */
    TOK_LT,             /* < */
    TOK_GT,             /* > */
    TOK_EQ,             /* == */
    TOK_NOT_EQ,         /* != */
    TOK_LT_EQ,          /* <= */
    TOK_GT_EQ,          /* >= */

    /* Compound assignment */
    TOK_PLUS_ASSIGN,    /* += */
    TOK_MINUS_ASSIGN,   /* -= */
    TOK_ASTERISK_ASSIGN,/* *= */
    TOK_SLASH_ASSIGN,   /* /= */
    TOK_PERCENT_ASSIGN, /* %= */

    /* Increment/Decrement */
    TOK_INCREMENT,      /* ++ */
    TOK_DECREMENT,      /* -- */

    /* Logical */
    TOK_AND,            /* && */
    TOK_OR,             /* || */

    /* Delimiters */
    TOK_COMMA,          /* , */
    TOK_COLON,          /* : */
    TOK_SEMICOLON,      /* ; */
    TOK_NEWLINE,

    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */

    /* Symbols */
    TOK_CARET,          /* ^  — pointer type */
    TOK_ARROW,          /* -> */
    TOK_DOT,            /* . */
    TOK_AT,             /* @ */
    TOK_AMPERSAND,      /* & */
    TOK_QUESTION,       /* ?  — wildcard type placeholder */

    /* Hash attributes */
    TOK_HASH_LBRACKET,  /* #[ — opens a single-line attribute list */
    TOK_STRICT,         /* #strict */
    TOK_FLAGS,          /* #flags */
    TOK_DOC,            /* #doc */
    TOK_JSON_ATTR,      /* #json */
    TOK_DISCARD,        /* #discard */
    TOK_DEPRECATED,     /* #deprecated */
    TOK_TEST,           /* #test */

    /* Keywords */
    TOK_MUT,
    TOK_CONST,
    TOK_DO,
    TOK_RETURN,
    TOK_IF,
    TOK_OR_KW,
    TOK_OTHERWISE,
    TOK_FOR,
    TOK_FOR_EACH,
    TOK_AS_LONG_AS,
    TOK_LOOP,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_IN,
    TOK_NOT_IN,
    TOK_RANGE,
    TOK_IMPORT,
    TOK_USING,
    TOK_STRUCT,
    TOK_ENUM,
    TOK_NIL,
    TOK_NEW,
    TOK_TRUE,
    TOK_FALSE,
    TOK_BLANK,
    TOK_ENSURE,
    TOK_OR_RETURN,

    /* Module system keywords */
    TOK_PRIVATE,
    TOK_USE,

    /* When/Is keywords */
    TOK_WHEN,
    TOK_IS,
    TOK_DEFAULT,

    /* Type aliasing */
    TOK_ALIAS,

    /* Type conversion */
    TOK_CAST,

    /* Bitwise keyword operators */
    TOK_BIT_AND,
    TOK_BIT_OR,
    TOK_BIT_XOR,
    TOK_BIT_NOT,
    TOK_BIT_SHIFT_LEFT,
    TOK_BIT_SHIFT_RIGHT,

    TOK_COUNT /* sentinel */
} TokenType;

typedef struct {
    TokenType type;
    const char *literal;    /* Points into arena or static string */
    int line;
    int column;
    const char *file;       /* Source file this token came from (NULL = main file) */
    bool preceded_by_ws;    /* True if whitespace/comments were skipped before this token */
} Token;

/* Look up a source-text span of the given length against the keyword table,
 * without requiring a NUL terminator - lets the lexer check for a keyword
 * before copying the identifier text anywhere. Returns true and fills
 * out_type / out_keyword with the keyword's token type and canonical static
 * string on a match; returns false (leaving both untouched) for a plain
 * identifier. */
bool token_lookup_keyword_n(const char *ident, int len, TokenType *out_type, const char **out_keyword);

/* Return human-readable name for a token type */
const char *token_type_name(TokenType type);

/* Return the spelling a token was actually written with, for diagnostics.
 * Keyword tokens report the alias the user typed (`while`, not `as_long_as`);
 * every other token falls back to token_type_name. */
const char *token_display_name(Token tok);

#endif
