/*
 * lexer.h — Public interface for the Grayscale lexer. Defines the Lexer
 * struct and functions for creating a lexer and advancing through tokens.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_LEXER_H
#define GRAYC_LEXER_H

#include "token.h"
#include "../util/arena.h"
#include <stdbool.h>

typedef struct {
    const char *input;
    int input_length;
    int position;
    int read_position;
    char current_character;
    int line;
    int column;
    const char *file;
    Arena *arena;   /* For allocating token literals */
    bool has_unterminated_string;
    const char *error_code; /* Set by lexer on error (E1003, E1004, etc.) */
    const char *error_message; /* Human-readable error message */
} Lexer;

Lexer *lexer_create(Arena *arena, const char *input, const char *file);
Token lexer_next_token(Lexer *lexer);

#endif
