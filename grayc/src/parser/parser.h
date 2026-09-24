/*
 * parser.h — Public interface for the Grayscale parser. Defines the Parser
 * struct and functions for creating a parser from a lexer and producing an
 * AST program node from the token stream.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_PARSER_H
#define GRAYC_PARSER_H

#include "ast.h"
#include "../lexer/lexer.h"
#include "../util/arena.h"
#include "../util/error.h"

#define MAX_PARSE_DEPTH 256

typedef struct {
    Lexer *lexer;
    Arena *arena;
    Token current_token;
    Token peek_token;
    const char *file;
    DiagnosticList *diagnostics;
    int depth;
    bool should_suppress_struct_literal; /* suppress struct literal parsing (right side of in/not_in) */
    bool is_in_interpolation; /* true when parsing a ${...} sub-expression */
    AstNode *current_function; /* enclosing function node (for or_return) */
    uint32_t seen_attribute_mask; /* attributes already applied to the declaration being parsed */
} Parser;

Parser *parser_create(Arena *arena, Lexer *lexer, const char *file, DiagnosticList *diagnostics);
AstNode *parser_parse_program(Parser *parser);

#endif
