/*
 * token.c — Token type definitions and keyword lookup. Contains the sorted
 * keyword table with binary search for identifier-to-keyword resolution, and
 * the token_type_name function for human-readable token descriptions.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "token.h"
#include <limits.h>
#include <string.h>

typedef struct {
    const char *keyword;
    TokenType type;
} KeywordEntry;

/* Sorted by keyword for binary search */
static const KeywordEntry keywords[] = {
    {"_",               TOKEN_BLANK},
    {"alias",           TOKEN_ALIAS},
    {"as_long_as",      TOKEN_AS_LONG_AS},
    {"bit_and",         TOKEN_BIT_AND},
    {"bit_not",         TOKEN_BIT_NOT},
    {"bit_or",          TOKEN_BIT_OR},
    {"bit_shift_left",  TOKEN_BIT_SHIFT_LEFT},
    {"bit_shift_right", TOKEN_BIT_SHIFT_RIGHT},
    {"bit_xor",         TOKEN_BIT_XOR},
    {"break",           TOKEN_BREAK},
    {"case",        TOKEN_IS},
    {"cast",        TOKEN_CAST},
    {"const",       TOKEN_CONST},
    {"continue",    TOKEN_CONTINUE},
    {"default",     TOKEN_DEFAULT},
    {"defer",       TOKEN_ENSURE},
    {"do",          TOKEN_DO},
    {"elif",        TOKEN_OR_KEYWORD},
    {"else",        TOKEN_OTHERWISE},
    {"ensure",      TOKEN_ENSURE},
    {"enum",        TOKEN_ENUM},
    {"extern",      TOKEN_EXTERN},
    {"false",       TOKEN_FALSE},
    {"fn",          TOKEN_DO},
    {"for",         TOKEN_FOR},
    {"for_each",    TOKEN_FOR_EACH},
    {"if",          TOKEN_IF},
    {"import",      TOKEN_IMPORT},
    {"in",          TOKEN_IN},
    {"is",          TOKEN_IS},
    {"loop",        TOKEN_LOOP},
    {"mut",         TOKEN_MUT},
    {"new",         TOKEN_NEW},
    {"nil",         TOKEN_NIL},
    {"not_in",      TOKEN_NOT_IN},
    {"or",          TOKEN_OR_KEYWORD},
    {"or_return",   TOKEN_OR_RETURN},
    {"otherwise",   TOKEN_OTHERWISE},
    {"private",     TOKEN_PRIVATE},
    {"range",       TOKEN_RANGE},
    {"return",      TOKEN_RETURN},
    {"struct",      TOKEN_STRUCT},
    {"switch",      TOKEN_WHEN},
    {"true",        TOKEN_TRUE},
    {"use",         TOKEN_USE},
    {"using",       TOKEN_USING},
    {"when",        TOKEN_WHEN},
    {"while",       TOKEN_AS_LONG_AS},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

/* Compares a length-bounded source span against a NUL-terminated keyword, in
 * the same lexical order strcmp would give if the span were NUL-terminated
 * at span_length. Lets the binary search below run before the span has been
 * copied anywhere. */
static int keyword_span_compare(const char *span, int span_length, const char *keyword) {
    for (int i = 0; i < span_length; i++) {
        unsigned char span_character = (unsigned char)span[i];
        unsigned char keyword_character = (unsigned char)keyword[i];
        if (keyword_character == '\0') return 1;
        if (span_character != keyword_character) return (int)span_character - (int)keyword_character;
    }
    return keyword[span_length] == '\0' ? 0 : -1;
}

bool token_lookup_keyword_with_length(const char *identifier, int length, TokenType *out_type, const char **out_keyword) {
    int low = 0;
    int high = (int)KEYWORD_COUNT - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        int comparison = keyword_span_compare(identifier, length, keywords[middle].keyword);
        if (comparison == 0) {
            *out_type = keywords[middle].type;
            *out_keyword = keywords[middle].keyword;
            return true;
        }
        if (comparison < 0) high = middle - 1;
        else low = middle + 1;
    }
    return false;
}

bool token_type_is_keyword(TokenType type) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (keywords[i].type == type) return true;
    }
    return false;
}

/* Return the spelling a keyword token was actually written with, so
 * diagnostics quote the user's source rather than the canonical spelling of
 * an aliased keyword (`while` must not be reported as `as_long_as`). The
 * lexer stores the matched table entry in tok.literal, so a lookup that
 * round-trips back to the same token type confirms the literal is a real
 * keyword spelling; everything else (identifiers, punctuation, EOF) falls
 * back to the token type's name. */
const char *token_display_name(Token token) {
    if (token.literal) {
        TokenType keyword_type;
        const char *keyword_text;
        size_t length = strlen(token.literal);
        if (length <= INT_MAX &&
            token_lookup_keyword_with_length(token.literal, (int)length, &keyword_type, &keyword_text) &&
            keyword_type == token.type) {
            return token.literal;
        }
    }
    return token_type_name(token.type);
}

const char *token_type_name(TokenType type) {
    switch (type) {
    case TOKEN_ILLEGAL:        return "ILLEGAL";
    case TOKEN_END_OF_FILE:            return "EOF";
    case TOKEN_IDENTIFIER:          return "IDENT";
    case TOKEN_INTEGER_LITERAL:            return "INT";
    case TOKEN_FLOATING_POINT_LITERAL:          return "FLOAT";
    case TOKEN_STRING:         return "STRING";
    case TOKEN_RAW_STRING:     return "RAW_STRING";
    case TOKEN_CHAR:           return "CHAR";
    case TOKEN_ASSIGN:         return "=";
    case TOKEN_PLUS:           return "+";
    case TOKEN_MINUS:          return "-";
    case TOKEN_BANG:           return "!";
    case TOKEN_ASTERISK:       return "*";
    case TOKEN_SLASH:          return "/";
    case TOKEN_PERCENT:        return "%";
    case TOKEN_LESS_THAN:             return "<";
    case TOKEN_GREATER_THAN:             return ">";
    case TOKEN_EQUAL:             return "==";
    case TOKEN_NOT_EQUAL:         return "!=";
    case TOKEN_LESS_THAN_OR_EQUAL:          return "<=";
    case TOKEN_GREATER_THAN_OR_EQUAL:          return ">=";
    case TOKEN_PLUS_ASSIGN:    return "+=";
    case TOKEN_MINUS_ASSIGN:   return "-=";
    case TOKEN_ASTERISK_ASSIGN:return "*=";
    case TOKEN_SLASH_ASSIGN:   return "/=";
    case TOKEN_PERCENT_ASSIGN: return "%=";
    case TOKEN_INCREMENT:      return "++";
    case TOKEN_DECREMENT:      return "--";
    case TOKEN_AND:            return "&&";
    case TOKEN_OR:             return "||";
    case TOKEN_COMMA:          return ",";
    case TOKEN_COLON:          return ":";
    case TOKEN_SEMICOLON:      return ";";
    case TOKEN_NEWLINE:        return "NEWLINE";
    case TOKEN_LEFT_PARENTHESIS:         return "(";
    case TOKEN_RIGHT_PARENTHESIS:         return ")";
    case TOKEN_LEFT_BRACE:         return "{";
    case TOKEN_RIGHT_BRACE:         return "}";
    case TOKEN_LEFT_BRACKET:       return "[";
    case TOKEN_RIGHT_BRACKET:       return "]";
    case TOKEN_ARROW:          return "->";
    case TOKEN_DOT:            return ".";
    case TOKEN_AT:             return "@";
    case TOKEN_CARET:          return "^";
    case TOKEN_AMPERSAND:      return "&";
    case TOKEN_QUESTION:       return "?";
    case TOKEN_HASH_LEFT_BRACKET:  return "#[";
    case TOKEN_STRICT:         return "#strict";
    case TOKEN_FLAGS:          return "#flags";
    case TOKEN_DOC:            return "#doc";
    case TOKEN_JSON_ATTRIBUTE:      return "#json";
    case TOKEN_DISCARD:        return "#discard";
    case TOKEN_DEPRECATED:     return "#deprecated";
    case TOKEN_TEST:           return "#test";
    case TOKEN_ERROR_CODE_ATTRIBUTE: return "#error_code";
    case TOKEN_MUT:            return "mut";
    case TOKEN_CONST:          return "const";
    case TOKEN_DO:             return "do";
    case TOKEN_RETURN:         return "return";
    case TOKEN_IF:             return "if";
    case TOKEN_OR_KEYWORD:          return "or";
    case TOKEN_OTHERWISE:      return "otherwise";
    case TOKEN_FOR:            return "for";
    case TOKEN_FOR_EACH:       return "for_each";
    case TOKEN_AS_LONG_AS:     return "as_long_as";
    case TOKEN_LOOP:           return "loop";
    case TOKEN_BREAK:          return "break";
    case TOKEN_CONTINUE:       return "continue";
    case TOKEN_IN:             return "in";
    case TOKEN_NOT_IN:         return "not_in";
    case TOKEN_RANGE:          return "range";
    case TOKEN_IMPORT:         return "import";
    case TOKEN_USING:          return "using";
    case TOKEN_STRUCT:         return "struct";
    case TOKEN_ENUM:           return "enum";
    case TOKEN_NIL:            return "nil";
    case TOKEN_NEW:            return "new";
    case TOKEN_TRUE:           return "true";
    case TOKEN_FALSE:          return "false";
    case TOKEN_BLANK:          return "_";
    case TOKEN_ENSURE:         return "ensure";
    case TOKEN_OR_RETURN:      return "or_return";
    case TOKEN_EXTERN:         return "extern";
    case TOKEN_PRIVATE:        return "private";
    case TOKEN_USE:            return "use";
    case TOKEN_WHEN:           return "when";
    case TOKEN_IS:             return "is";
    case TOKEN_DEFAULT:        return "default";
    case TOKEN_ALIAS:          return "alias";
    case TOKEN_CAST:           return "cast";
    case TOKEN_BIT_AND:        return "bit_and";
    case TOKEN_BIT_OR:         return "bit_or";
    case TOKEN_BIT_XOR:        return "bit_xor";
    case TOKEN_BIT_NOT:        return "bit_not";
    case TOKEN_BIT_SHIFT_LEFT: return "bit_shift_left";
    case TOKEN_BIT_SHIFT_RIGHT:return "bit_shift_right";
    case TOKEN_COUNT:          return "COUNT";
    }
    return "UNKNOWN";
}
