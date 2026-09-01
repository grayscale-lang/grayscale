/*
 * lexer.c — Lexical analysis for Grayscale source code. Scans input text
 * character-by-character to produce a stream of tokens including operators,
 * keywords, literals, and string interpolations.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "lexer.h"
#include "../util/constants.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>

static void read_char(Lexer *lexer) {
    if (lexer->read_position >= lexer->input_len) {
        lexer->ch = 0;
    } else {
        lexer->ch = lexer->input[lexer->read_position];
    }
    lexer->position = lexer->read_position;
    lexer->read_position++;
    if (lexer->column < INT_MAX) lexer->column++;
    if (lexer->ch == '\n') {
        if (lexer->line < INT_MAX) lexer->line++;
        lexer->column = 0;
    }
}

static char peek_char(Lexer *lexer) {
    if (lexer->read_position >= lexer->input_len) return 0;
    return lexer->input[lexer->read_position];
}

/* Look ahead `offset` characters past the current one; offset 1 is peek_char. */
static char peek_char_at(Lexer *lexer, int offset) {
    int pos = lexer->read_position + offset - 1;
    if (pos < 0 || pos >= lexer->input_len) return 0;
    return lexer->input[pos];
}

static void skip_whitespace(Lexer *lexer) {
    while (lexer->ch == ' ' || lexer->ch == '\t' || lexer->ch == '\r' || lexer->ch == '\n') {
        read_char(lexer);
    }
}

static void skip_line_comment(Lexer *lexer) {
    /* Skip until end of line */
    while (lexer->ch != '\n' && lexer->ch != 0) {
        read_char(lexer);
    }
}

static void skip_block_comment(Lexer *lexer) {
    /* Skip past opening slash-star */
    read_char(lexer); /* skip * */
    while (lexer->ch != 0) {
        if (lexer->ch == '*' && peek_char(lexer) == '/') {
            read_char(lexer); /* skip * */
            read_char(lexer); /* skip / */
            return;
        }
        read_char(lexer);
    }
    /* EOF reached; unclosed comment */
    lexer->error_code = "E1003";
    lexer->error_msg = "unclosed multi-line comment";
}

static void skip_whitespace_and_comments(Lexer *lexer) {
    for (;;) {
        skip_whitespace(lexer);
        if (lexer->ch == '/' && peek_char(lexer) == '/') {
            read_char(lexer); /* skip first / */
            read_char(lexer); /* skip second / */
            skip_line_comment(lexer);
        } else if (lexer->ch == '/' && peek_char(lexer) == '*') {
            read_char(lexer); /* skip / */
            skip_block_comment(lexer);
        } else {
            break;
        }
    }
}

/* Scans an identifier's extent without copying it anywhere yet, so the
 * caller can check the keyword table against the raw source bytes first
 * and only pay for an arena copy (deduplicated via arena_intern_string)
 * when it turns out to be a real identifier, not a keyword. */
static void scan_identifier_span(Lexer *lexer, int *out_start, int *out_len) {
    int start = lexer->position;
    while (isalpha((unsigned char)lexer->ch) || lexer->ch == '_' || isdigit((unsigned char)lexer->ch)) {
        read_char(lexer);
    }
    int len = lexer->position - start;
    if (len > MAX_IDENTIFIER_LENGTH) {
        lexer->error_code = "E1024";
        lexer->error_msg = "identifier exceeds the maximum length of 255 characters";
    }
    *out_start = start;
    *out_len = len;
}

static const char *read_number(Lexer *lexer, TokenType *type) {
    int start = lexer->position;
    *type = TOK_INT;

    /* Check for 0x, 0o, 0b prefixes */
    if (lexer->ch == '0') {
        char next = peek_char(lexer);
        if (next == 'x' || next == 'X') {
            read_char(lexer); read_char(lexer);
            int dstart = lexer->position;
            while (isxdigit((unsigned char)lexer->ch) || lexer->ch == '_') read_char(lexer);
            if (lexer->position == dstart) {
                lexer->error_code = "E1010";
                lexer->error_msg = "invalid number format: '0x' must be followed by hex digits (0-9, a-f)";
            }
            return arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
        }
        if (next == 'o' || next == 'O') {
            read_char(lexer); read_char(lexer);
            int dstart = lexer->position;
            while ((lexer->ch >= '0' && lexer->ch <= '7') || lexer->ch == '_') read_char(lexer);
            if (lexer->position == dstart) {
                lexer->error_code = "E1010";
                lexer->error_msg = "invalid number format: '0o' must be followed by octal digits (0-7)";
            }
            return arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
        }
        if (next == 'b' || next == 'B') {
            read_char(lexer); read_char(lexer);
            int dstart = lexer->position;
            while (lexer->ch == '0' || lexer->ch == '1' || lexer->ch == '_') read_char(lexer);
            if (lexer->position == dstart) {
                lexer->error_code = "E1010";
                lexer->error_msg = "invalid number format: '0b' must be followed by binary digits (0-1)";
            }
            return arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
        }
    }

    while (isdigit((unsigned char)lexer->ch) || lexer->ch == '_') {
        read_char(lexer);
    }

    if (lexer->ch == '.') {
        char next = peek_char(lexer);
        if (isdigit((unsigned char)next) || next == '_' || next == 0 || next == '\n' ||
            next == ' ' || next == ')' || next == '}' || next == ',' ||
            next == ';') {
            /* Consume decimal point; validation below will catch errors */
            *type = TOK_FLOAT;
            read_char(lexer);
            while (isdigit((unsigned char)lexer->ch) || lexer->ch == '_') {
                read_char(lexer);
            }
        }
    }

    /* Exponent: 1e9, 1.5e-3, 2E+10. Only consumed when real exponent digits
     * follow, so a bare identifier after a number (2E) still lexes as two
     * tokens the way it always did. */
    if (lexer->ch == 'e' || lexer->ch == 'E') {
        char next = peek_char(lexer);
        bool signed_exp = (next == '+' || next == '-');
        char after_sign = signed_exp ? peek_char_at(lexer, 2) : next;
        if (isdigit((unsigned char)after_sign)) {
            *type = TOK_FLOAT;
            read_char(lexer);                 /* e/E */
            if (signed_exp) read_char(lexer); /* +/- */
            while (isdigit((unsigned char)lexer->ch) || lexer->ch == '_') {
                read_char(lexer);
            }
        }
    }

    const char *num = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);

    /* Validate number literal format */
    if (num && !lexer->error_code) {
        const char *s = num;
        /* Skip 0x/0o/0b prefix */
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'o' || s[1] == 'O' || s[1] == 'b' || s[1] == 'B'))
            s += 2;
        int len = (int)strlen(s);
        if (len > 0) {
            /* E1013: trailing underscore */
            if (s[len-1] == '_') { lexer->error_code = "E1013"; lexer->error_msg = "number cannot end with underscore"; }
            /* E1011: consecutive underscores */
            for (int i = 0; s[i] && s[i+1]; i++) {
                if (s[i] == '_' && s[i+1] == '_') { lexer->error_code = "E1011"; lexer->error_msg = "number cannot have consecutive underscores"; break; }
            }
            /* E1014/E1015: underscore adjacent to decimal point */
            for (int i = 0; s[i]; i++) {
                if (s[i] == '.') {
                    if (i > 0 && s[i-1] == '_') { lexer->error_code = "E1014"; lexer->error_msg = "underscore cannot appear before decimal point"; }
                    if (s[i+1] == '_') { lexer->error_code = "E1015"; lexer->error_msg = "underscore cannot appear after decimal point"; }
                    /* E1016: trailing decimal */
                    if (!s[i+1] || (!isdigit((unsigned char)s[i+1]) && s[i+1] != '_')) { lexer->error_code = "E1016"; lexer->error_msg = "number cannot end with decimal point"; }
                    break;
                }
            }
        }
    }

    return num;
}

static const char *read_string(Lexer *lexer) {
    read_char(lexer); /* skip opening " */
    int start = lexer->position;
    int brace_depth = 0;

    while (lexer->ch != 0) {
        if (lexer->ch == '\\') {
            read_char(lexer); /* move to escape char */
            /* Validate escape sequence */
            if (lexer->ch == 'x') {
                /* Hex escape: \xNN; exactly two hex digits */
                read_char(lexer);
                if (!isxdigit((unsigned char)lexer->ch)) {
                    lexer->error_code = "E1006";
                    lexer->error_msg = "invalid hex escape sequence; \\x must be followed by exactly two hex digits";
                    continue;
                }
                read_char(lexer);
                if (!isxdigit((unsigned char)lexer->ch)) {
                    lexer->error_code = "E1006";
                    lexer->error_msg = "invalid hex escape sequence; \\x must be followed by exactly two hex digits";
                    continue;
                }
                read_char(lexer);
                continue;
            } else if (lexer->ch != 'n' && lexer->ch != 't' && lexer->ch != 'r' && lexer->ch != '\\' &&
                lexer->ch != '"' && lexer->ch != '\'' && lexer->ch != '0' &&
                lexer->ch != 'a' && lexer->ch != 'b' && lexer->ch != 'f' && lexer->ch != 'v' &&
                lexer->ch != '$' && lexer->ch != 0) {
                lexer->error_code = "E1006";
                lexer->error_msg = "invalid escape sequence in string";
            }
            read_char(lexer);
            continue;
        }
        if (lexer->ch == '$' && peek_char(lexer) == '{') {
            brace_depth++;
            read_char(lexer); /* skip $ */
            read_char(lexer); /* skip { */
            continue;
        }
        /* Inside interpolation: skip nested string literals so that
         * braces within them are not counted against brace_depth. */
        if (lexer->ch == '"' && brace_depth > 0) {
            read_char(lexer); /* skip opening " */
            while (lexer->ch != 0 && lexer->ch != '"') {
                if (lexer->ch == '\\' && peek_char(lexer) != 0) {
                    read_char(lexer); /* skip backslash */
                }
                read_char(lexer);
            }
            if (lexer->ch == '"') read_char(lexer); /* skip closing " */
            continue;
        }
        if (lexer->ch == '{' && brace_depth > 0) {
            brace_depth++;
            read_char(lexer);
            continue;
        }
        if (lexer->ch == '}' && brace_depth > 0) {
            brace_depth--;
            read_char(lexer);
            continue;
        }
        if (lexer->ch == '"' && brace_depth == 0) {
            break; /* end of string */
        }
        if (lexer->ch == '\n' && brace_depth == 0) {
            lexer->error_code = "E1023";
            lexer->error_msg = "string literals cannot span multiple lines; use a raw string with backticks for multi-line text";
            break;
        }
        read_char(lexer);
    }

    const char *str = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
    if (lexer->ch == '"') {
        read_char(lexer); /* skip closing " */
    } else if (!lexer->error_code) {
        lexer->unterminated_string = true;
    }
    return str;
}

static const char *read_raw_string(Lexer *lexer) {
    read_char(lexer); /* skip opening ` */
    int start = lexer->position;

    while (lexer->ch != '`' && lexer->ch != 0) {
        read_char(lexer);
    }

    const char *str = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
    if (lexer->ch == '`') {
        read_char(lexer); /* skip closing ` */
    } else {
        lexer->error_code = "E1017";
        lexer->error_msg = "unclosed raw string literal";
    }
    return str;
}

/* Length of the UTF-8 sequence a lead byte introduces, or 0 if it is not a
 * valid lead byte (continuation byte or 0xF8+). */
static int utf8_seq_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

static const char *read_char_literal(Lexer *lexer) {
    read_char(lexer); /* skip opening ' */
    int start = lexer->position;

    if (lexer->ch == '\'') {
        /* empty literal '' */
        lexer->error_code = "E1018";
        lexer->error_msg = "char literal must contain exactly one codepoint; use a string for multiple characters";
    } else if (lexer->ch == '\\') {
        read_char(lexer); /* move to escape char */
        if (lexer->ch == 'x') {
            /* \xNN — exactly two hex digits (codepoint U+00NN) */
            read_char(lexer);
            for (int i = 0; i < 2; i++) {
                if (!isxdigit((unsigned char)lexer->ch)) {
                    lexer->error_code = "E1006";
                    lexer->error_msg = "malformed '\\x' escape in character literal; expected exactly two hex digits";
                    break;
                }
                read_char(lexer);
            }
        } else if (lexer->ch == 'u') {
            /* \u{H...} — 1 to 6 hex digits */
            read_char(lexer);
            if (lexer->ch != '{') {
                lexer->error_code = "E1006";
                lexer->error_msg = "malformed '\\u' escape in character literal; expected '\\u{...}'";
            } else {
                read_char(lexer); /* skip { */
                int nhex = 0;
                while (isxdigit((unsigned char)lexer->ch)) { read_char(lexer); nhex++; }
                if (lexer->ch != '}' || nhex < 1 || nhex > 6) {
                    lexer->error_code = "E1006";
                    lexer->error_msg = "malformed '\\u{}' escape in character literal; expected 1 to 6 hex digits";
                } else {
                    read_char(lexer); /* skip } */
                }
            }
        } else if (lexer->ch != 'n' && lexer->ch != 't' && lexer->ch != 'r' && lexer->ch != '\\' &&
                   lexer->ch != '\'' && lexer->ch != '"' && lexer->ch != '0' && lexer->ch != 0) {
            lexer->error_code = "E1007";
            lexer->error_msg = "invalid escape sequence in character literal";
            read_char(lexer);
        } else {
            read_char(lexer); /* skip simple escaped char */
        }
    } else {
        /* One UTF-8 encoded codepoint: consume the lead byte and its
         * continuation bytes so a non-ASCII character is a single literal. */
        int seqlen = utf8_seq_len((unsigned char)lexer->ch);
        if (seqlen == 0) {
            lexer->error_code = "E1018";
            lexer->error_msg = "malformed UTF-8 in character literal";
            read_char(lexer);
        } else {
            read_char(lexer); /* consumed lead byte */
            for (int i = 1; i < seqlen && !lexer->error_code; i++) {
                if (((unsigned char)lexer->ch & 0xC0) != 0x80) {
                    lexer->error_code = "E1018";
                    lexer->error_msg = "malformed UTF-8 in character literal";
                    break;
                }
                read_char(lexer);
            }
        }
    }

    /* Anything other than the closing quote now means more than one codepoint —
     * unless the line or input ended first, which is an unterminated literal. */
    if (lexer->ch != '\'' && lexer->ch != 0 && lexer->ch != '\n' && !lexer->error_code) {
        while (lexer->ch != '\'' && lexer->ch != 0 && lexer->ch != '\n') {
            read_char(lexer);
        }
        if (lexer->ch == '\'') {
            lexer->error_code = "E1018";
            lexer->error_msg = "char literal must contain exactly one codepoint; use a string for multiple characters";
        }
    }

    const char *str = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
    if (lexer->ch == '\'') {
        read_char(lexer); /* skip closing ' */
    } else if (!lexer->error_code) {
        lexer->error_code = "E1005";
        lexer->error_msg = "unclosed character literal";
    }
    return str;
}

static Token make_token(TokenType type, const char *literal, int line, int col) {
    Token t;
    t.type = type;
    t.literal = literal;
    t.line = line;
    t.column = col;
    t.file = NULL;
    return t;
}

static int check_upcoming_chars(Lexer *lexer, const char *s, int len) {
    if (lexer->position + len > lexer->input_len) return 0;
    return strncmp(lexer->input + lexer->position, s, len) == 0;
}

Lexer *lexer_create(Arena *arena, const char *input, const char *file) {
    Lexer *lexer = arena_alloc(arena, sizeof(Lexer));
    lexer->input = input;
    size_t raw_len = strlen(input);
    if (raw_len > (size_t)INT32_MAX) raw_len = (size_t)INT32_MAX;
    lexer->input_len = (int)raw_len;
    lexer->position = 0;
    lexer->read_position = 0;
    lexer->ch = 0;
    lexer->line = 1;
    lexer->column = 0;
    lexer->file = file;
    lexer->arena = arena;
    read_char(lexer);
    return lexer;
}

Token lexer_next_token(Lexer *lexer) {
    Token tok;
    lexer->error_code = NULL;
    int pre_skip_position = lexer->position;
    skip_whitespace_and_comments(lexer);
    bool had_leading_gap = lexer->position != pre_skip_position;

    /* Check for lexer errors from comment/whitespace skipping */
    if (lexer->error_code) {
        tok = make_token(TOK_ILLEGAL, lexer->error_msg, lexer->line, lexer->column);
        goto done;
    }

    tok.line = lexer->line;
    tok.column = lexer->column;

    switch (lexer->ch) {
    case 0:
        tok = make_token(TOK_EOF, "", lexer->line, lexer->column);
        goto done;

    case '=':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_EQ, "==", tok.line, tok.column);
        } else {
            tok = make_token(TOK_ASSIGN, "=", tok.line, tok.column);
        }
        break;

    case '+':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_PLUS_ASSIGN, "+=", tok.line, tok.column);
        } else if (peek_char(lexer) == '+') {
            read_char(lexer);
            tok = make_token(TOK_INCREMENT, "++", tok.line, tok.column);
        } else {
            tok = make_token(TOK_PLUS, "+", tok.line, tok.column);
        }
        break;

    case '-':
        if (peek_char(lexer) == '>') {
            read_char(lexer);
            tok = make_token(TOK_ARROW, "->", tok.line, tok.column);
        } else if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_MINUS_ASSIGN, "-=", tok.line, tok.column);
        } else if (peek_char(lexer) == '-') {
            read_char(lexer);
            tok = make_token(TOK_DECREMENT, "--", tok.line, tok.column);
        } else {
            tok = make_token(TOK_MINUS, "-", tok.line, tok.column);
        }
        break;

    case '!':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_NOT_EQ, "!=", tok.line, tok.column);
        } else if (peek_char(lexer) == 'i' && lexer->read_position + 1 < lexer->input_len &&
                   lexer->input[lexer->read_position + 1] == 'n' &&
                   (lexer->read_position + 2 >= lexer->input_len ||
                    !isalpha((unsigned char)lexer->input[lexer->read_position + 2]))) {
            /* !in → NOT_IN */
            read_char(lexer); /* skip i */
            read_char(lexer); /* skip n */
            tok = make_token(TOK_NOT_IN, "!in", tok.line, tok.column);
        } else {
            tok = make_token(TOK_BANG, "!", tok.line, tok.column);
        }
        break;

    case '*':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_ASTERISK_ASSIGN, "*=", tok.line, tok.column);
        } else {
            tok = make_token(TOK_ASTERISK, "*", tok.line, tok.column);
        }
        break;

    case '/':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_SLASH_ASSIGN, "/=", tok.line, tok.column);
        } else {
            tok = make_token(TOK_SLASH, "/", tok.line, tok.column);
        }
        break;

    case '%':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_PERCENT_ASSIGN, "%=", tok.line, tok.column);
        } else {
            tok = make_token(TOK_PERCENT, "%", tok.line, tok.column);
        }
        break;

    case '<':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_LT_EQ, "<=", tok.line, tok.column);
        } else {
            tok = make_token(TOK_LT, "<", tok.line, tok.column);
        }
        break;

    case '>':
        if (peek_char(lexer) == '=') {
            read_char(lexer);
            tok = make_token(TOK_GT_EQ, ">=", tok.line, tok.column);
        } else {
            tok = make_token(TOK_GT, ">", tok.line, tok.column);
        }
        break;

    case '&':
        if (peek_char(lexer) == '&') {
            read_char(lexer);
            tok = make_token(TOK_AND, "&&", tok.line, tok.column);
        } else {
            tok = make_token(TOK_AMPERSAND, "&", tok.line, tok.column);
        }
        break;

    case '?':
        tok = make_token(TOK_QUESTION, "?", tok.line, tok.column);
        break;

    case '|':
        if (peek_char(lexer) == '|') {
            read_char(lexer);
            tok = make_token(TOK_OR, "||", tok.line, tok.column);
        } else {
            lexer->error_code = "E1020";
            lexer->error_msg = "unexpected character '|'; use '||' for logical OR";
            tok = make_token(TOK_ILLEGAL, lexer->error_msg, tok.line, tok.column);
        }
        break;

    case ',': tok = make_token(TOK_COMMA, ",", tok.line, tok.column); break;
    case ':': tok = make_token(TOK_COLON, ":", tok.line, tok.column); break;
    case ';': tok = make_token(TOK_SEMICOLON, ";", tok.line, tok.column); break;
    case '(': tok = make_token(TOK_LPAREN, "(", tok.line, tok.column); break;
    case ')': tok = make_token(TOK_RPAREN, ")", tok.line, tok.column); break;
    case '{': tok = make_token(TOK_LBRACE, "{", tok.line, tok.column); break;
    case '}': tok = make_token(TOK_RBRACE, "}", tok.line, tok.column); break;
    case '[': tok = make_token(TOK_LBRACKET, "[", tok.line, tok.column); break;
    case ']': tok = make_token(TOK_RBRACKET, "]", tok.line, tok.column); break;
    case '.': tok = make_token(TOK_DOT, ".", tok.line, tok.column); break;
    case '@': tok = make_token(TOK_AT, "@", tok.line, tok.column); break;
    case '^': tok = make_token(TOK_CARET, "^", tok.line, tok.column); break;

    case '#':
        if (check_upcoming_chars(lexer, "#[", 2)) {
            tok = make_token(TOK_HASH_LBRACKET, "#[", tok.line, tok.column);
            read_char(lexer); /* consume '[' */
        } else if (check_upcoming_chars(lexer, "#strict", 7)) {
            tok = make_token(TOK_STRICT, "#strict", tok.line, tok.column);
            for (int i = 0; i < 6; i++) read_char(lexer);
        } else if (check_upcoming_chars(lexer, "#flags", 6)) {
            tok = make_token(TOK_FLAGS, "#flags", tok.line, tok.column);
            for (int i = 0; i < 5; i++) read_char(lexer);
        } else if (check_upcoming_chars(lexer, "#doc", 4)) {
            tok = make_token(TOK_DOC, "#doc", tok.line, tok.column);
            for (int i = 0; i < 3; i++) read_char(lexer);
        } else if (check_upcoming_chars(lexer, "#json", 5)) {
            tok = make_token(TOK_JSON_ATTR, "#json", tok.line, tok.column);
            for (int i = 0; i < 4; i++) read_char(lexer);
        } else if (check_upcoming_chars(lexer, "#discard", 8)) {
            tok = make_token(TOK_DISCARD, "#discard", tok.line, tok.column);
            for (int i = 0; i < 7; i++) read_char(lexer);
        } else if (check_upcoming_chars(lexer, "#deprecated", 11)) {
            tok = make_token(TOK_DEPRECATED, "#deprecated", tok.line, tok.column);
            for (int i = 0; i < 10; i++) read_char(lexer);
        } else if (check_upcoming_chars(lexer, "#test", 5)) {
            tok = make_token(TOK_TEST, "#test", tok.line, tok.column);
            for (int i = 0; i < 4; i++) read_char(lexer);
        } else {
            lexer->error_code = "E1019";
            lexer->error_msg = "unexpected character '#'; use '//' for comments, '#strict', '#flags', '#json', '#doc', '#discard', '#deprecated', '#test' for attributes, or '#[...]' for a single-line attribute list";
            tok = make_token(TOK_ILLEGAL, lexer->error_msg, tok.line, tok.column);
        }
        break;

    case '"':
        lexer->unterminated_string = false;
        tok.literal = read_string(lexer);
        if (lexer->unterminated_string) {
            lexer->error_code = "E1021";
            lexer->error_msg = "string literal was never closed; add a closing double quote";
            tok.type = TOK_ILLEGAL;
            tok.literal = lexer->error_msg;
        } else if (lexer->error_code) {
            tok.type = TOK_ILLEGAL;
            tok.literal = lexer->error_msg;
        } else {
            tok.type = TOK_STRING;
        }
        goto done;

    case '`':
        tok.literal = read_raw_string(lexer);
        tok.type = lexer->error_code ? TOK_ILLEGAL : TOK_RAW_STRING;
        if (lexer->error_code) tok.literal = lexer->error_msg;
        goto done;

    case '\'':
        lexer->error_code = NULL;
        tok.literal = read_char_literal(lexer);
        tok.type = lexer->error_code ? TOK_ILLEGAL : TOK_CHAR;
        if (lexer->error_code) tok.literal = lexer->error_msg;
        goto done;

    default:
        if (isalpha((unsigned char)lexer->ch) || lexer->ch == '_') {
            int start, len;
            scan_identifier_span(lexer, &start, &len);
            if (lexer->error_code) {
                tok.type = TOK_ILLEGAL;
                tok.literal = lexer->error_msg;
            } else {
                TokenType kw_type;
                const char *kw_str;
                if (token_lookup_keyword_n(lexer->input + start, len, &kw_type, &kw_str)) {
                    tok.type = kw_type;
                    tok.literal = kw_str;
                } else {
                    tok.type = TOK_IDENT;
                    tok.literal = arena_intern_string(lexer->arena, lexer->input + start, (size_t)len);
                }
            }
            goto done;
        } else if (isdigit((unsigned char)lexer->ch)) {
            TokenType num_type;
            tok.literal = read_number(lexer, &num_type);
            if (lexer->error_code) {
                tok.type = TOK_ILLEGAL;
                tok.literal = lexer->error_msg;
            } else {
                tok.type = num_type;
            }
            goto done;
        } else {
            char msg[TYPE_NAME_MAX];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", lexer->ch);
            lexer->error_code = "E1022";
            lexer->error_msg = arena_copy_string(lexer->arena, msg);
            tok = make_token(TOK_ILLEGAL, lexer->error_msg, tok.line, tok.column);
        }
        break;
    }

    read_char(lexer);
done:
    tok.file = lexer->file;
    tok.preceded_by_ws = had_leading_gap;
    return tok;
}
