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
    Token cur_token;
    Token peek_token;
    const char *file;
    DiagnosticList *diag;
    int depth;
    bool no_struct_literal; /* suppress struct literal parsing (RHS of in/not_in) */
    bool in_interp;         /* true when parsing a ${...} sub-expression */
} Parser;

Parser *parser_create(Arena *arena, Lexer *lexer, const char *file, DiagnosticList *diag);
AstNode *parser_parse_program(Parser *parser);

#endif
