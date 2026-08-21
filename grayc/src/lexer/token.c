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
#include <string.h>

typedef struct {
    const char *keyword;
    TokenType type;
} KeywordEntry;

/* Sorted by keyword for binary search */
static const KeywordEntry keywords[] = {
    {"_",               TOK_BLANK},
    {"alias",           TOK_ALIAS},
    {"as_long_as",      TOK_AS_LONG_AS},
    {"bit_and",         TOK_BIT_AND},
    {"bit_not",         TOK_BIT_NOT},
    {"bit_or",          TOK_BIT_OR},
    {"bit_shift_left",  TOK_BIT_SHIFT_LEFT},
    {"bit_shift_right", TOK_BIT_SHIFT_RIGHT},
    {"bit_xor",         TOK_BIT_XOR},
    {"break",           TOK_BREAK},
    {"cast",        TOK_CAST},
    {"const",       TOK_CONST},
    {"continue",    TOK_CONTINUE},
    {"default",     TOK_DEFAULT},
    {"do",          TOK_DO},
    {"else",        TOK_OTHERWISE},
    {"ensure",      TOK_ENSURE},
    {"enum",        TOK_ENUM},
    {"false",       TOK_FALSE},
    {"for",         TOK_FOR},
    {"for_each",    TOK_FOR_EACH},
    {"if",          TOK_IF},
    {"import",      TOK_IMPORT},
    {"in",          TOK_IN},
    {"is",          TOK_IS},
    {"loop",        TOK_LOOP},
    {"module",      TOK_MODULE},
    {"mut",         TOK_MUT},
    {"new",         TOK_NEW},
    {"nil",         TOK_NIL},
    {"not_in",      TOK_NOT_IN},
    {"or",          TOK_OR_KW},
    {"or_return",   TOK_OR_RETURN},
    {"otherwise",   TOK_OTHERWISE},
    {"private",     TOK_PRIVATE},
    {"range",       TOK_RANGE},
    {"return",      TOK_RETURN},
    {"struct",      TOK_STRUCT},
    {"true",        TOK_TRUE},
    {"use",         TOK_USE},
    {"using",       TOK_USING},
    {"when",        TOK_WHEN},
    {"while",       TOK_AS_LONG_AS},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

/* Compares a length-bounded source span against a NUL-terminated keyword, in
 * the same lexical order strcmp would give if the span were NUL-terminated
 * at span_len. Lets the binary search below run before the span has been
 * copied anywhere. */
static int keyword_span_cmp(const char *span, int span_len, const char *keyword) {
    for (int i = 0; i < span_len; i++) {
        unsigned char span_ch = (unsigned char)span[i];
        unsigned char kw_ch = (unsigned char)keyword[i];
        if (kw_ch == '\0') return 1;
        if (span_ch != kw_ch) return (int)span_ch - (int)kw_ch;
    }
    return keyword[span_len] == '\0' ? 0 : -1;
}

bool token_lookup_keyword_n(const char *ident, int len, TokenType *out_type, const char **out_keyword) {
    int lo = 0;
    int hi = (int)KEYWORD_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = keyword_span_cmp(ident, len, keywords[mid].keyword);
        if (cmp == 0) {
            *out_type = keywords[mid].type;
            *out_keyword = keywords[mid].keyword;
            return true;
        }
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return false;
}

const char *token_type_name(TokenType type) {
    switch (type) {
    case TOK_ILLEGAL:        return "ILLEGAL";
    case TOK_EOF:            return "EOF";
    case TOK_IDENT:          return "IDENT";
    case TOK_INT:            return "INT";
    case TOK_FLOAT:          return "FLOAT";
    case TOK_STRING:         return "STRING";
    case TOK_RAW_STRING:     return "RAW_STRING";
    case TOK_CHAR:           return "CHAR";
    case TOK_ASSIGN:         return "=";
    case TOK_PLUS:           return "+";
    case TOK_MINUS:          return "-";
    case TOK_BANG:           return "!";
    case TOK_ASTERISK:       return "*";
    case TOK_SLASH:          return "/";
    case TOK_PERCENT:        return "%";
    case TOK_LT:             return "<";
    case TOK_GT:             return ">";
    case TOK_EQ:             return "==";
    case TOK_NOT_EQ:         return "!=";
    case TOK_LT_EQ:          return "<=";
    case TOK_GT_EQ:          return ">=";
    case TOK_PLUS_ASSIGN:    return "+=";
    case TOK_MINUS_ASSIGN:   return "-=";
    case TOK_ASTERISK_ASSIGN:return "*=";
    case TOK_SLASH_ASSIGN:   return "/=";
    case TOK_PERCENT_ASSIGN: return "%=";
    case TOK_INCREMENT:      return "++";
    case TOK_DECREMENT:      return "--";
    case TOK_AND:            return "&&";
    case TOK_OR:             return "||";
    case TOK_COMMA:          return ",";
    case TOK_COLON:          return ":";
    case TOK_SEMICOLON:      return ";";
    case TOK_NEWLINE:        return "NEWLINE";
    case TOK_LPAREN:         return "(";
    case TOK_RPAREN:         return ")";
    case TOK_LBRACE:         return "{";
    case TOK_RBRACE:         return "}";
    case TOK_LBRACKET:       return "[";
    case TOK_RBRACKET:       return "]";
    case TOK_ARROW:          return "->";
    case TOK_DOT:            return ".";
    case TOK_AT:             return "@";
    case TOK_CARET:          return "^";
    case TOK_AMPERSAND:      return "&";
    case TOK_QUESTION:       return "?";
    case TOK_STRICT:         return "#strict";
    case TOK_FLAGS:          return "#flags";
    case TOK_DOC:            return "#doc";
    case TOK_JSON_ATTR:      return "#json";
    case TOK_DISCARD:        return "#discard";
    case TOK_MUT:            return "mut";
    case TOK_CONST:          return "const";
    case TOK_DO:             return "do";
    case TOK_RETURN:         return "return";
    case TOK_IF:             return "if";
    case TOK_OR_KW:          return "or";
    case TOK_OTHERWISE:      return "otherwise";
    case TOK_FOR:            return "for";
    case TOK_FOR_EACH:       return "for_each";
    case TOK_AS_LONG_AS:     return "as_long_as";
    case TOK_LOOP:           return "loop";
    case TOK_BREAK:          return "break";
    case TOK_CONTINUE:       return "continue";
    case TOK_IN:             return "in";
    case TOK_NOT_IN:         return "not_in";
    case TOK_RANGE:          return "range";
    case TOK_IMPORT:         return "import";
    case TOK_USING:          return "using";
    case TOK_STRUCT:         return "struct";
    case TOK_ENUM:           return "enum";
    case TOK_NIL:            return "nil";
    case TOK_NEW:            return "new";
    case TOK_TRUE:           return "true";
    case TOK_FALSE:          return "false";
    case TOK_BLANK:          return "_";
    case TOK_ENSURE:         return "ensure";
    case TOK_OR_RETURN:      return "or_return";
    case TOK_MODULE:         return "module";
    case TOK_PRIVATE:        return "private";
    case TOK_USE:            return "use";
    case TOK_WHEN:           return "when";
    case TOK_IS:             return "is";
    case TOK_DEFAULT:        return "default";
    case TOK_ALIAS:          return "alias";
    case TOK_CAST:           return "cast";
    case TOK_BIT_AND:        return "bit_and";
    case TOK_BIT_OR:         return "bit_or";
    case TOK_BIT_XOR:        return "bit_xor";
    case TOK_BIT_NOT:        return "bit_not";
    case TOK_BIT_SHIFT_LEFT: return "bit_shift_left";
    case TOK_BIT_SHIFT_RIGHT:return "bit_shift_right";
    case TOK_COUNT:          return "COUNT";
    }
    return "UNKNOWN";
}
