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
    TOKEN_ILLEGAL,
    TOKEN_END_OF_FILE,

    /* Identifiers and literals */
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER_LITERAL,
    TOKEN_FLOATING_POINT_LITERAL,
    TOKEN_STRING,
    TOKEN_RAW_STRING,
    TOKEN_CHAR,

    /* Operators */
    TOKEN_ASSIGN,         /* = */
    TOKEN_PLUS,           /* + */
    TOKEN_MINUS,          /* - */
    TOKEN_BANG,           /* ! */
    TOKEN_ASTERISK,       /* * */
    TOKEN_SLASH,          /* / */
    TOKEN_PERCENT,        /* % */

    /* Comparison */
    TOKEN_LESS_THAN,             /* < */
    TOKEN_GREATER_THAN,             /* > */
    TOKEN_EQUAL,             /* == */
    TOKEN_NOT_EQUAL,         /* != */
    TOKEN_LESS_THAN_OR_EQUAL,          /* <= */
    TOKEN_GREATER_THAN_OR_EQUAL,          /* >= */

    /* Compound assignment */
    TOKEN_PLUS_ASSIGN,    /* += */
    TOKEN_MINUS_ASSIGN,   /* -= */
    TOKEN_ASTERISK_ASSIGN,/* *= */
    TOKEN_SLASH_ASSIGN,   /* /= */
    TOKEN_PERCENT_ASSIGN, /* %= */

    /* Increment/Decrement */
    TOKEN_INCREMENT,      /* ++ */
    TOKEN_DECREMENT,      /* -- */

    /* Logical */
    TOKEN_AND,            /* && */
    TOKEN_OR,             /* || */

    /* Delimiters */
    TOKEN_COMMA,          /* , */
    TOKEN_COLON,          /* : */
    TOKEN_SEMICOLON,      /* ; */
    TOKEN_NEWLINE,

    TOKEN_LEFT_PARENTHESIS,         /* ( */
    TOKEN_RIGHT_PARENTHESIS,         /* ) */
    TOKEN_LEFT_BRACE,         /* { */
    TOKEN_RIGHT_BRACE,         /* } */
    TOKEN_LEFT_BRACKET,       /* [ */
    TOKEN_RIGHT_BRACKET,       /* ] */

    /* Symbols */
    TOKEN_CARET,          /* ^  — pointer type */
    TOKEN_ARROW,          /* -> */
    TOKEN_DOT,            /* . */
    TOKEN_AT,             /* @ */
    TOKEN_AMPERSAND,      /* & */
    TOKEN_QUESTION,       /* ?  — wildcard type placeholder */

    /* Hash attributes */
    TOKEN_HASH_LEFT_BRACKET,  /* #[ — opens a single-line attribute list */
    TOKEN_STRICT,         /* #strict */
    TOKEN_FLAGS,          /* #flags */
    TOKEN_DOC,            /* #doc */
    TOKEN_JSON_ATTRIBUTE,      /* #json */
    TOKEN_DISCARD,        /* #discard */
    TOKEN_DEPRECATED,     /* #deprecated */
    TOKEN_TEST,           /* #test */
    TOKEN_ERROR_CODE_ATTRIBUTE, /* #error_code */

    /* Keywords */
    TOKEN_MUT,
    TOKEN_CONST,
    TOKEN_DO,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_OR_KEYWORD,
    TOKEN_OTHERWISE,
    TOKEN_FOR,
    TOKEN_FOR_EACH,
    TOKEN_AS_LONG_AS,
    TOKEN_LOOP,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_IN,
    TOKEN_NOT_IN,
    TOKEN_RANGE,
    TOKEN_IMPORT,
    TOKEN_USING,
    TOKEN_STRUCT,
    TOKEN_ENUM,
    TOKEN_NIL,
    TOKEN_NEW,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_BLANK,
    TOKEN_ENSURE,
    TOKEN_OR_RETURN,
    TOKEN_EXTERN,

    /* Module system keywords */
    TOKEN_PRIVATE,
    TOKEN_USE,

    /* When/Is keywords */
    TOKEN_WHEN,
    TOKEN_IS,
    TOKEN_DEFAULT,

    /* Type aliasing */
    TOKEN_ALIAS,

    /* Type conversion */
    TOKEN_CAST,

    /* Bitwise keyword operators */
    TOKEN_BIT_AND,
    TOKEN_BIT_OR,
    TOKEN_BIT_XOR,
    TOKEN_BIT_NOT,
    TOKEN_BIT_SHIFT_LEFT,
    TOKEN_BIT_SHIFT_RIGHT,

    TOKEN_COUNT /* sentinel */
} TokenType;

typedef struct {
    TokenType type;
    const char *literal;    /* Points into arena or static string */
    int line;
    int column;
    const char *file;       /* Source file this token came from (NULL = main file) */
    bool is_preceded_by_whitespace;    /* True if whitespace/comments were skipped before this token */
} Token;

/* Look up a source-text span of the given length against the keyword table,
 * without requiring a NUL terminator - lets the lexer check for a keyword
 * before copying the identifier text anywhere. Returns true and fills
 * out_type / out_keyword with the keyword's token type and canonical static
 * string on a match; returns false (leaving both untouched) for a plain
 * identifier. */
bool token_lookup_keyword_with_length(const char *identifier, int length, TokenType *out_type, const char **out_keyword);

/* Return true if the given token type is produced by the lexer's keyword
 * table (a reserved word). Derived from that table so the parser's
 * reserved-word check cannot drift from the lexer. */
bool token_type_is_keyword(TokenType type);

/* Return human-readable name for a token type */
const char *token_type_name(TokenType type);

/* Return the spelling a token was actually written with, for diagnostics.
 * Keyword tokens report the alias the user typed (`while`, not `as_long_as`);
 * every other token falls back to token_type_name. */
const char *token_display_name(Token token);

#endif
