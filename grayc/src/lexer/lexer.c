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

static void read_character(Lexer *lexer) {
    if (lexer->read_position >= lexer->input_length) {
        lexer->current_character = 0;
    } else {
        lexer->current_character = lexer->input[lexer->read_position];
    }
    lexer->position = lexer->read_position;
    lexer->read_position++;
    if (lexer->column < INT_MAX) lexer->column++;
    if (lexer->current_character == '\n') {
        if (lexer->line < INT_MAX) lexer->line++;
        lexer->column = 0;
    }
}

static char peek_character(Lexer *lexer) {
    if (lexer->read_position >= lexer->input_length) return 0;
    return lexer->input[lexer->read_position];
}

/* Look ahead `offset` characters past the current one; offset 1 is peek_character. */
static char peek_character_at(Lexer *lexer, int offset) {
    int lookahead_position = lexer->read_position + offset - 1;
    if (lookahead_position < 0 || lookahead_position >= lexer->input_length) return 0;
    return lexer->input[lookahead_position];
}

static void skip_whitespace(Lexer *lexer) {
    while (lexer->current_character == ' ' || lexer->current_character == '\t' || lexer->current_character == '\r' || lexer->current_character == '\n') {
        read_character(lexer);
    }
}

static void skip_line_comment(Lexer *lexer) {
    /* Skip until end of line */
    while (lexer->current_character != '\n' && lexer->current_character != 0) {
        read_character(lexer);
    }
}

static void skip_block_comment(Lexer *lexer) {
    /* Skip past opening slash-star */
    read_character(lexer); /* skip * */
    while (lexer->current_character != 0) {
        if (lexer->current_character == '*' && peek_character(lexer) == '/') {
            read_character(lexer); /* skip * */
            read_character(lexer); /* skip / */
            return;
        }
        read_character(lexer);
    }
    /* EOF reached; unclosed comment */
    lexer->error_code = "E1003";
    lexer->error_message = "unclosed multi-line comment";
}

static void skip_whitespace_and_comments(Lexer *lexer) {
    for (;;) {
        skip_whitespace(lexer);
        if (lexer->current_character == '/' && peek_character(lexer) == '/') {
            read_character(lexer); /* skip first / */
            read_character(lexer); /* skip second / */
            skip_line_comment(lexer);
        } else if (lexer->current_character == '/' && peek_character(lexer) == '*') {
            read_character(lexer); /* skip / */
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
static void scan_identifier_span(Lexer *lexer, int *out_start, int *out_length) {
    int start = lexer->position;
    while (isalpha((unsigned char)lexer->current_character) || lexer->current_character == '_' || isdigit((unsigned char)lexer->current_character)) {
        read_character(lexer);
    }
    int length = lexer->position - start;
    if (length > MAX_IDENTIFIER_LENGTH) {
        lexer->error_code = "E1024";
        lexer->error_message = "identifier exceeds the maximum length of 255 characters";
    }
    *out_start = start;
    *out_length = length;
}

/* Consume a 0x/0o/0b prefix and the base-`base` digits (and '_' separators)
 * after it; E1010 when no digit follows the prefix. */
static const char *read_prefixed_digits(Lexer *lexer, int start, int base, const char *error_message) {
    read_character(lexer); read_character(lexer);
    int digit_start = lexer->position;
    while (lexer->current_character == '_' ||
           (base == 16 ? isxdigit((unsigned char)lexer->current_character)
                       : lexer->current_character >= '0' && lexer->current_character < '0' + base)) {
        read_character(lexer);
    }
    if (lexer->position == digit_start) {
        lexer->error_code = "E1010";
        lexer->error_message = error_message;
    }
    return arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
}

static const char *read_number(Lexer *lexer, TokenType *type) {
    int start = lexer->position;
    *type = TOKEN_INTEGER_LITERAL;

    /* Check for 0x, 0o, 0b prefixes */
    if (lexer->current_character == '0') {
        char next = peek_character(lexer);
        if (next == 'x' || next == 'X') {
            return read_prefixed_digits(lexer, start, 16,
                "invalid number format: '0x' must be followed by hex digits (0-9, a-f)");
        }
        if (next == 'o' || next == 'O') {
            return read_prefixed_digits(lexer, start, 8,
                "invalid number format: '0o' must be followed by octal digits (0-7)");
        }
        if (next == 'b' || next == 'B') {
            return read_prefixed_digits(lexer, start, 2,
                "invalid number format: '0b' must be followed by binary digits (0-1)");
        }
    }

    while (isdigit((unsigned char)lexer->current_character) || lexer->current_character == '_') {
        read_character(lexer);
    }

    if (lexer->current_character == '.') {
        char next = peek_character(lexer);
        if (isdigit((unsigned char)next) || next == '_' || next == 0 || next == '\n' ||
            next == ' ' || next == ')' || next == '}' || next == ',' ||
            next == ';') {
            /* Consume decimal point; validation below will catch errors */
            *type = TOKEN_FLOATING_POINT_LITERAL;
            read_character(lexer);
            while (isdigit((unsigned char)lexer->current_character) || lexer->current_character == '_') {
                read_character(lexer);
            }
        }
    }

    /* Exponent: 1e9, 1.5e-3, 2E+10. Only consumed when real exponent digits
     * follow, so a bare identifier after a number (2E) still lexes as two
     * tokens the way it always did. */
    if (lexer->current_character == 'e' || lexer->current_character == 'E') {
        char next = peek_character(lexer);
        bool has_exponent_sign = (next == '+' || next == '-');
        char after_sign = has_exponent_sign ? peek_character_at(lexer, 2) : next;
        if (isdigit((unsigned char)after_sign)) {
            *type = TOKEN_FLOATING_POINT_LITERAL;
            read_character(lexer);                 /* e/E */
            if (has_exponent_sign) read_character(lexer); /* +/- */
            while (isdigit((unsigned char)lexer->current_character) || lexer->current_character == '_') {
                read_character(lexer);
            }
        }
    }

    const char *number_text = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);

    /* Validate number literal format */
    if (number_text && !lexer->error_code) {
        const char *digits = number_text;
        /* Skip 0x/0o/0b prefix */
        if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X' || digits[1] == 'o' || digits[1] == 'O' || digits[1] == 'b' || digits[1] == 'B'))
            digits += 2;
        int length = (int)strlen(digits);
        if (length > 0) {
            /* E1013: trailing underscore */
            if (digits[length-1] == '_') { lexer->error_code = "E1013"; lexer->error_message = "number cannot end with underscore"; }
            /* E1011: consecutive underscores */
            for (int i = 0; digits[i] && digits[i+1]; i++) {
                if (digits[i] == '_' && digits[i+1] == '_') { lexer->error_code = "E1011"; lexer->error_message = "number cannot have consecutive underscores"; break; }
            }
            /* E1014/E1015: underscore adjacent to decimal point */
            for (int i = 0; digits[i]; i++) {
                if (digits[i] == '.') {
                    if (i > 0 && digits[i-1] == '_') { lexer->error_code = "E1014"; lexer->error_message = "underscore cannot appear before decimal point"; }
                    if (digits[i+1] == '_') { lexer->error_code = "E1015"; lexer->error_message = "underscore cannot appear after decimal point"; }
                    /* E1016: trailing decimal */
                    if (!digits[i+1] || (!isdigit((unsigned char)digits[i+1]) && digits[i+1] != '_')) { lexer->error_code = "E1016"; lexer->error_message = "number cannot end with decimal point"; }
                    break;
                }
            }
        }
    }

    return number_text;
}

static const char *read_string(Lexer *lexer) {
    read_character(lexer); /* skip opening " */
    int start = lexer->position;
    int brace_depth = 0;

    while (lexer->current_character != 0) {
        if (lexer->current_character == '\\') {
            read_character(lexer); /* move to escape char */
            /* Validate escape sequence */
            if (lexer->current_character == 'x') {
                /* Hex escape: \xNN; exactly two hex digits */
                read_character(lexer);
                if (!isxdigit((unsigned char)lexer->current_character)) {
                    lexer->error_code = "E1006";
                    lexer->error_message = "invalid hex escape sequence; \\x must be followed by exactly two hex digits";
                    continue;
                }
                read_character(lexer);
                if (!isxdigit((unsigned char)lexer->current_character)) {
                    lexer->error_code = "E1006";
                    lexer->error_message = "invalid hex escape sequence; \\x must be followed by exactly two hex digits";
                    continue;
                }
                read_character(lexer);
                continue;
            } else if (lexer->current_character != 'n' && lexer->current_character != 't' && lexer->current_character != 'r' && lexer->current_character != '\\' &&
                lexer->current_character != '"' && lexer->current_character != '\'' && lexer->current_character != '0' &&
                lexer->current_character != 'a' && lexer->current_character != 'b' && lexer->current_character != 'f' && lexer->current_character != 'v' &&
                lexer->current_character != '$' && lexer->current_character != 0) {
                lexer->error_code = "E1006";
                lexer->error_message = "invalid escape sequence in string";
            }
            read_character(lexer);
            continue;
        }
        if (lexer->current_character == '$' && peek_character(lexer) == '{') {
            brace_depth++;
            read_character(lexer); /* skip $ */
            read_character(lexer); /* skip { */
            continue;
        }
        /* Inside interpolation: skip nested string literals so that
         * braces within them are not counted against brace_depth. */
        if (lexer->current_character == '"' && brace_depth > 0) {
            read_character(lexer); /* skip opening " */
            while (lexer->current_character != 0 && lexer->current_character != '"') {
                if (lexer->current_character == '\\' && peek_character(lexer) != 0) {
                    read_character(lexer); /* skip backslash */
                }
                read_character(lexer);
            }
            if (lexer->current_character == '"') read_character(lexer); /* skip closing " */
            continue;
        }
        /* Likewise skip nested char literals, so their escapes are not
         * validated as string escapes and a `\u{...}` brace is not counted.
         * The embedded expression is lexed properly on its own pass. */
        if (lexer->current_character == '\'' && brace_depth > 0) {
            read_character(lexer); /* skip opening ' */
            while (lexer->current_character != 0 && lexer->current_character != '\'') {
                if (lexer->current_character == '\\' && peek_character(lexer) != 0) {
                    read_character(lexer); /* skip backslash */
                }
                read_character(lexer);
            }
            if (lexer->current_character == '\'') read_character(lexer); /* skip closing ' */
            continue;
        }
        if (lexer->current_character == '{' && brace_depth > 0) {
            brace_depth++;
            read_character(lexer);
            continue;
        }
        if (lexer->current_character == '}' && brace_depth > 0) {
            brace_depth--;
            read_character(lexer);
            continue;
        }
        if (lexer->current_character == '"' && brace_depth == 0) {
            break; /* end of string */
        }
        if (lexer->current_character == '\n' && brace_depth == 0) {
            lexer->error_code = "E1023";
            lexer->error_message = "string literals cannot span multiple lines; use a raw string with backticks for multi-line text";
            break;
        }
        read_character(lexer);
    }

    const char *literal_text = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
    if (lexer->current_character == '"') {
        read_character(lexer); /* skip closing " */
    } else if (!lexer->error_code) {
        lexer->has_unterminated_string = true;
    }
    return literal_text;
}

static const char *read_raw_string(Lexer *lexer) {
    read_character(lexer); /* skip opening ` */
    int start = lexer->position;

    while (lexer->current_character != '`' && lexer->current_character != 0) {
        read_character(lexer);
    }

    const char *literal_text = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
    if (lexer->current_character == '`') {
        read_character(lexer); /* skip closing ` */
    } else {
        lexer->error_code = "E1017";
        lexer->error_message = "unclosed raw string literal";
    }
    return literal_text;
}

/* Length of the UTF-8 sequence a lead byte introduces, or 0 if it is not a
 * valid lead byte (continuation byte or 0xF8+). */
static int utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

static const char *read_char_literal(Lexer *lexer) {
    read_character(lexer); /* skip opening ' */
    int start = lexer->position;

    if (lexer->current_character == '\'') {
        /* empty literal '' */
        lexer->error_code = "E1018";
        lexer->error_message = "char literal must contain exactly one codepoint; use a string for multiple characters";
    } else if (lexer->current_character == '\\') {
        read_character(lexer); /* move to escape char */
        if (lexer->current_character == 'x') {
            /* \xNN — exactly two hex digits (codepoint U+00NN) */
            read_character(lexer);
            for (int i = 0; i < 2; i++) {
                if (!isxdigit((unsigned char)lexer->current_character)) {
                    lexer->error_code = "E1006";
                    lexer->error_message = "malformed '\\x' escape in character literal; expected exactly two hex digits";
                    break;
                }
                read_character(lexer);
            }
        } else if (lexer->current_character == 'u') {
            /* \u{H...} — 1 to 6 hex digits */
            read_character(lexer);
            if (lexer->current_character != '{') {
                lexer->error_code = "E1006";
                lexer->error_message = "malformed '\\u' escape in character literal; expected '\\u{...}'";
            } else {
                read_character(lexer); /* skip { */
                int hex_digit_count = 0;
                while (isxdigit((unsigned char)lexer->current_character)) { read_character(lexer); hex_digit_count++; }
                if (lexer->current_character != '}' || hex_digit_count < 1 || hex_digit_count > 6) {
                    lexer->error_code = "E1006";
                    lexer->error_message = "malformed '\\u{}' escape in character literal; expected 1 to 6 hex digits";
                } else {
                    read_character(lexer); /* skip } */
                }
            }
        } else if (lexer->current_character != 'n' && lexer->current_character != 't' && lexer->current_character != 'r' && lexer->current_character != '\\' &&
                   lexer->current_character != '\'' && lexer->current_character != '"' && lexer->current_character != '0' && lexer->current_character != 0) {
            lexer->error_code = "E1007";
            lexer->error_message = "invalid escape sequence in character literal";
            read_character(lexer);
        } else {
            read_character(lexer); /* skip simple escaped char */
        }
    } else {
        /* One UTF-8 encoded codepoint: consume the lead byte and its
         * continuation bytes so a non-ASCII character is a single literal. */
        int sequence_length = utf8_sequence_length((unsigned char)lexer->current_character);
        if (sequence_length == 0) {
            lexer->error_code = "E1018";
            lexer->error_message = "malformed UTF-8 in character literal";
            read_character(lexer);
        } else {
            read_character(lexer); /* consumed lead byte */
            for (int i = 1; i < sequence_length && !lexer->error_code; i++) {
                if (((unsigned char)lexer->current_character & 0xC0) != 0x80) {
                    lexer->error_code = "E1018";
                    lexer->error_message = "malformed UTF-8 in character literal";
                    break;
                }
                read_character(lexer);
            }
        }
    }

    /* Anything other than the closing quote now means more than one codepoint —
     * unless the line or input ended first, which is an unterminated literal. */
    if (lexer->current_character != '\'' && lexer->current_character != 0 && lexer->current_character != '\n' && !lexer->error_code) {
        while (lexer->current_character != '\'' && lexer->current_character != 0 && lexer->current_character != '\n') {
            read_character(lexer);
        }
        if (lexer->current_character == '\'') {
            lexer->error_code = "E1018";
            lexer->error_message = "char literal must contain exactly one codepoint; use a string for multiple characters";
        }
    }

    const char *literal_text = arena_copy_string_with_length(lexer->arena, lexer->input + start, lexer->position - start);
    if (lexer->current_character == '\'') {
        read_character(lexer); /* skip closing ' */
    } else if (!lexer->error_code) {
        lexer->error_code = "E1005";
        lexer->error_message = "unclosed character literal";
    }
    return literal_text;
}

static Token make_token(TokenType type, const char *literal, int line, int column) {
    Token token;
    token.type = type;
    token.literal = literal;
    token.line = line;
    token.column = column;
    token.file = NULL;
    return token;
}

static int check_upcoming_characters(Lexer *lexer, const char *expected_text, int length) {
    if (lexer->position + length > lexer->input_length) return 0;
    return strncmp(lexer->input + lexer->position, expected_text, length) == 0;
}

/* The `#name` attribute keywords. `#[` (an attribute list) is handled
 * separately since it is punctuation, not a keyword. */
static const struct { const char *spelling; TokenType type; } attribute_keywords[] = {
    {"#strict",     TOKEN_STRICT},
    {"#flags",      TOKEN_FLAGS},
    {"#doc",        TOKEN_DOC},
    {"#json",       TOKEN_JSON_ATTRIBUTE},
    {"#discard",    TOKEN_DISCARD},
    {"#deprecated", TOKEN_DEPRECATED},
    {"#error_code", TOKEN_ERROR_CODE_ATTRIBUTE},
    {"#test",       TOKEN_TEST},
};

Lexer *lexer_create(Arena *arena, const char *input, const char *file) {
    Lexer *lexer = arena_allocate(arena, sizeof(Lexer));
    lexer->input = input;
    size_t raw_length = strlen(input);
    if (raw_length > (size_t)INT32_MAX) raw_length = (size_t)INT32_MAX;
    lexer->input_length = (int)raw_length;
    lexer->position = 0;
    lexer->read_position = 0;
    lexer->current_character = 0;
    lexer->line = 1;
    lexer->column = 0;
    lexer->file = file;
    lexer->arena = arena;
    read_character(lexer);
    return lexer;
}

Token lexer_next_token(Lexer *lexer) {
    Token token;
    lexer->error_code = NULL;
    int position_before_skip = lexer->position;
    skip_whitespace_and_comments(lexer);
    bool has_leading_gap = lexer->position != position_before_skip;

    /* Check for lexer errors from comment/whitespace skipping */
    if (lexer->error_code) {
        token = make_token(TOKEN_ILLEGAL, lexer->error_message, lexer->line, lexer->column);
        goto done;
    }

    token.line = lexer->line;
    token.column = lexer->column;

    switch (lexer->current_character) {
    case 0:
        token = make_token(TOKEN_END_OF_FILE, "", lexer->line, lexer->column);
        goto done;

    case '=':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_EQUAL, "==", token.line, token.column);
        } else {
            token = make_token(TOKEN_ASSIGN, "=", token.line, token.column);
        }
        break;

    case '+':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_PLUS_ASSIGN, "+=", token.line, token.column);
        } else if (peek_character(lexer) == '+') {
            read_character(lexer);
            token = make_token(TOKEN_INCREMENT, "++", token.line, token.column);
        } else {
            token = make_token(TOKEN_PLUS, "+", token.line, token.column);
        }
        break;

    case '-':
        if (peek_character(lexer) == '>') {
            read_character(lexer);
            token = make_token(TOKEN_ARROW, "->", token.line, token.column);
        } else if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_MINUS_ASSIGN, "-=", token.line, token.column);
        } else if (peek_character(lexer) == '-') {
            read_character(lexer);
            token = make_token(TOKEN_DECREMENT, "--", token.line, token.column);
        } else {
            token = make_token(TOKEN_MINUS, "-", token.line, token.column);
        }
        break;

    case '!':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_NOT_EQUAL, "!=", token.line, token.column);
        } else if (peek_character(lexer) == 'i' && lexer->read_position + 1 < lexer->input_length &&
                   lexer->input[lexer->read_position + 1] == 'n' &&
                   (lexer->read_position + 2 >= lexer->input_length ||
                    !(isalnum((unsigned char)lexer->input[lexer->read_position + 2]) ||
                      lexer->input[lexer->read_position + 2] == '_'))) {
            /* !in → NOT_IN */
            read_character(lexer); /* skip i */
            read_character(lexer); /* skip n */
            token = make_token(TOKEN_NOT_IN, "!in", token.line, token.column);
        } else {
            token = make_token(TOKEN_BANG, "!", token.line, token.column);
        }
        break;

    case '*':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_ASTERISK_ASSIGN, "*=", token.line, token.column);
        } else {
            token = make_token(TOKEN_ASTERISK, "*", token.line, token.column);
        }
        break;

    case '/':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_SLASH_ASSIGN, "/=", token.line, token.column);
        } else {
            token = make_token(TOKEN_SLASH, "/", token.line, token.column);
        }
        break;

    case '%':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_PERCENT_ASSIGN, "%=", token.line, token.column);
        } else {
            token = make_token(TOKEN_PERCENT, "%", token.line, token.column);
        }
        break;

    case '<':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_LESS_THAN_OR_EQUAL, "<=", token.line, token.column);
        } else {
            token = make_token(TOKEN_LESS_THAN, "<", token.line, token.column);
        }
        break;

    case '>':
        if (peek_character(lexer) == '=') {
            read_character(lexer);
            token = make_token(TOKEN_GREATER_THAN_OR_EQUAL, ">=", token.line, token.column);
        } else {
            token = make_token(TOKEN_GREATER_THAN, ">", token.line, token.column);
        }
        break;

    case '&':
        if (peek_character(lexer) == '&') {
            read_character(lexer);
            token = make_token(TOKEN_AND, "&&", token.line, token.column);
        } else {
            token = make_token(TOKEN_AMPERSAND, "&", token.line, token.column);
        }
        break;

    case '?':
        token = make_token(TOKEN_QUESTION, "?", token.line, token.column);
        break;

    case '|':
        if (peek_character(lexer) == '|') {
            read_character(lexer);
            token = make_token(TOKEN_OR, "||", token.line, token.column);
        } else {
            lexer->error_code = "E1020";
            lexer->error_message = "unexpected character '|'; use '||' for logical OR";
            token = make_token(TOKEN_ILLEGAL, lexer->error_message, token.line, token.column);
        }
        break;

    case ',': token = make_token(TOKEN_COMMA, ",", token.line, token.column); break;
    case ':': token = make_token(TOKEN_COLON, ":", token.line, token.column); break;
    case ';': token = make_token(TOKEN_SEMICOLON, ";", token.line, token.column); break;
    case '(': token = make_token(TOKEN_LEFT_PARENTHESIS, "(", token.line, token.column); break;
    case ')': token = make_token(TOKEN_RIGHT_PARENTHESIS, ")", token.line, token.column); break;
    case '{': token = make_token(TOKEN_LEFT_BRACE, "{", token.line, token.column); break;
    case '}': token = make_token(TOKEN_RIGHT_BRACE, "}", token.line, token.column); break;
    case '[': token = make_token(TOKEN_LEFT_BRACKET, "[", token.line, token.column); break;
    case ']': token = make_token(TOKEN_RIGHT_BRACKET, "]", token.line, token.column); break;
    case '.': token = make_token(TOKEN_DOT, ".", token.line, token.column); break;
    case '@': token = make_token(TOKEN_AT, "@", token.line, token.column); break;
    case '^': token = make_token(TOKEN_CARET, "^", token.line, token.column); break;

    case '#': {
        if (check_upcoming_characters(lexer, "#[", 2)) {
            token = make_token(TOKEN_HASH_LEFT_BRACKET, "#[", token.line, token.column);
            read_character(lexer); /* consume '['; the trailing read_character consumes '#' */
            break;
        }
        bool was_matched = false;
        for (size_t i = 0; i < sizeof(attribute_keywords) / sizeof(attribute_keywords[0]); i++) {
            int spelling_length = (int)strlen(attribute_keywords[i].spelling);
            if (!check_upcoming_characters(lexer, attribute_keywords[i].spelling, spelling_length)) continue;
            token = make_token(attribute_keywords[i].type, attribute_keywords[i].spelling, token.line, token.column);
            /* Consume all but one character; the trailing read_character below takes the last. */
            for (int consumed_count = 1; consumed_count < spelling_length; consumed_count++) read_character(lexer);
            was_matched = true;
            break;
        }
        if (!was_matched) {
            lexer->error_code = "E1019";
            lexer->error_message = "unexpected character '#'; use '//' for comments, '#strict', '#flags', '#json', '#doc', '#discard', '#deprecated', '#test', '#error_code' for attributes, or '#[...]' for a single-line attribute list";
            token = make_token(TOKEN_ILLEGAL, lexer->error_message, token.line, token.column);
        }
        break;
    }

    case '"':
        lexer->has_unterminated_string = false;
        token.literal = read_string(lexer);
        if (lexer->has_unterminated_string) {
            lexer->error_code = "E1021";
            lexer->error_message = "string literal was never closed; add a closing double quote";
            token.type = TOKEN_ILLEGAL;
            token.literal = lexer->error_message;
        } else if (lexer->error_code) {
            token.type = TOKEN_ILLEGAL;
            token.literal = lexer->error_message;
        } else {
            token.type = TOKEN_STRING;
        }
        goto done;

    case '`':
        token.literal = read_raw_string(lexer);
        token.type = lexer->error_code ? TOKEN_ILLEGAL : TOKEN_RAW_STRING;
        if (lexer->error_code) token.literal = lexer->error_message;
        goto done;

    case '\'':
        lexer->error_code = NULL;
        token.literal = read_char_literal(lexer);
        token.type = lexer->error_code ? TOKEN_ILLEGAL : TOKEN_CHAR;
        if (lexer->error_code) token.literal = lexer->error_message;
        goto done;

    default:
        if (isalpha((unsigned char)lexer->current_character) || lexer->current_character == '_') {
            int start, length;
            scan_identifier_span(lexer, &start, &length);
            if (lexer->error_code) {
                token.type = TOKEN_ILLEGAL;
                token.literal = lexer->error_message;
            } else {
                TokenType keyword_type;
                const char *keyword_text;
                if (token_lookup_keyword_with_length(lexer->input + start, length, &keyword_type, &keyword_text)) {
                    token.type = keyword_type;
                    token.literal = keyword_text;
                } else {
                    token.type = TOKEN_IDENTIFIER;
                    token.literal = arena_intern_string(lexer->arena, lexer->input + start, (size_t)length);
                }
            }
            goto done;
        } else if (isdigit((unsigned char)lexer->current_character)) {
            TokenType number_type;
            token.literal = read_number(lexer, &number_type);
            if (lexer->error_code) {
                token.type = TOKEN_ILLEGAL;
                token.literal = lexer->error_message;
            } else {
                token.type = number_type;
            }
            goto done;
        } else {
            char message_buffer[TYPE_NAME_MAX];
            snprintf(message_buffer, sizeof(message_buffer), "unexpected character '%c'", lexer->current_character);
            lexer->error_code = "E1022";
            lexer->error_message = arena_copy_string(lexer->arena, message_buffer);
            token = make_token(TOKEN_ILLEGAL, lexer->error_message, token.line, token.column);
        }
        break;
    }

    read_character(lexer);
done:
    token.file = lexer->file;
    token.is_preceded_by_whitespace = has_leading_gap;
    return token;
}
