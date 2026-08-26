/*
 * ast.c — AST node construction helpers. Provides the ast_alloc function
 * for arena-allocating and zero-initializing new AST nodes with a given
 * kind and source token.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "ast.h"
#include "../util/arena.h"
#include <string.h>

AstNode *ast_alloc(Arena *arena, NodeKind kind, Token token) {
    AstNode *node = arena_alloc(arena, sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->kind = kind;
    node->token = token;
    return node;
}

const char *ast_member_qualifier(const AstNode *node) {
    if (!node || node->kind != NODE_MEMBER_EXPR) return NULL;
    const AstNode *object = node->data.member.object;
    if (!object || object->kind != NODE_LABEL) return NULL;
    return object->data.label.value;
}

const char *ast_member_base_qualifier(const AstNode *node) {
    const char *bare = ast_member_qualifier(node);
    if (bare) return bare;
    if (!node || node->kind != NODE_MEMBER_EXPR) return NULL;
    const AstNode *object = node->data.member.object;
    if (!object || object->kind != NODE_POSTFIX_EXPR ||
        object->data.postfix.op != TOK_CARET) return NULL;
    const AstNode *left = object->data.postfix.left;
    if (!left || left->kind != NODE_LABEL) return NULL;
    return left->data.label.value;
}

bool ast_member_chain(const AstNode *node, const char **out_qualifier,
                      const char **out_type) {
    if (!node || node->kind != NODE_MEMBER_EXPR) return false;
    const AstNode *object = node->data.member.object;
    const char *qualifier = ast_member_qualifier(object);
    if (!qualifier) return false;
    if (out_qualifier) *out_qualifier = qualifier;
    if (out_type) *out_type = object->data.member.member;
    return true;
}
