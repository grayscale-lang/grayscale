/*
 * ast.c — AST node construction helpers. Provides the ast_allocate function
 * for arena-allocating and zero-initializing new AST nodes with a given
 * kind and source token.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "ast.h"
#include "../util/arena.h"
#include <ctype.h>
#include <string.h>

AstNode *ast_allocate(Arena *arena, NodeKind kind, Token token) {
    AstNode *node = arena_allocate(arena, sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->kind = kind;
    node->token = token;
    return node;
}

const char *ast_member_qualifier(const AstNode *node) {
    if (!node || node->kind != NODE_MEMBER_EXPRESSION) return NULL;
    const AstNode *object = node->data.member.object;
    if (!object || object->kind != NODE_LABEL) return NULL;
    return object->data.label.value;
}

int ast_string_decode(const AstNode *node, char *out) {
    const char *cursor = node->data.string_value.value;
    if (node->data.string_value.is_raw) {
        strcpy(out, cursor);
        return (int)strlen(out);
    }
    char *start = out;
    while (*cursor) {
        if (*cursor != '\\' || !cursor[1]) { *out++ = *cursor++; continue; }
        cursor++;
        char escape = *cursor++;
        switch (escape) {
        case 'n': *out++ = '\n'; break;
        case 't': *out++ = '\t'; break;
        case 'r': *out++ = '\r'; break;
        case '0': *out++ = '\0'; break;
        case 'a': *out++ = '\a'; break;
        case 'b': *out++ = '\b'; break;
        case 'f': *out++ = '\f'; break;
        case 'v': *out++ = '\v'; break;
        case 'x': {
            /* The lexer admits exactly two hex digits. */
            int value = 0;
            for (int digit = 0; digit < 2 && isxdigit((unsigned char)*cursor); digit++, cursor++)
                value = value * 16 + (isdigit((unsigned char)*cursor) ? *cursor - '0' : (tolower((unsigned char)*cursor) - 'a' + 10));
            *out++ = (char)value;
            break;
        }
        default: *out++ = escape; break; /* \\ \" \' \$ */
        }
    }
    *out = '\0';
    return (int)(out - start);
}

const char *ast_member_base_qualifier(const AstNode *node) {
    const char *bare = ast_member_qualifier(node);
    if (bare) return bare;
    if (!node || node->kind != NODE_MEMBER_EXPRESSION) return NULL;
    const AstNode *object = node->data.member.object;
    if (!object || object->kind != NODE_POSTFIX_EXPRESSION ||
        object->data.postfix.operator != TOKEN_CARET) return NULL;
    const AstNode *left = object->data.postfix.left;
    if (!left || left->kind != NODE_LABEL) return NULL;
    return left->data.label.value;
}

bool ast_member_chain(const AstNode *node, const char **out_qualifier, const char **out_type) {
    if (!node || node->kind != NODE_MEMBER_EXPRESSION) return false;
    const AstNode *object = node->data.member.object;
    const char *qualifier = ast_member_qualifier(object);
    if (!qualifier) return false;
    if (out_qualifier) *out_qualifier = qualifier;
    if (out_type) *out_type = object->data.member.member;
    return true;
}
