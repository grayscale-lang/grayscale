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
#include "../util/xalloc.h"
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

bool function_has_generic_parameters(const AstNode *declaration) {
    if (!declaration || declaration->kind != NODE_FUNCTION_DECLARATION) return false;
    for (int i = 0; i < declaration->data.function_declaration.parameter_count; i++) {
        if (declaration->data.function_declaration.parameters[i].is_type_parameter) return true;
    }
    return false;
}

void generic_bindings_init(GenericBindings *bindings, const AstNode *declaration,
                           const char *binding_text) {
    bindings->count = 0;
    bindings->names = NULL;
    bindings->types = NULL;
    if (!function_has_generic_parameters(declaration)) return;
    int parameter_count = declaration->data.function_declaration.parameter_count;
    bindings->names = xcalloc((size_t)parameter_count, sizeof(const char *));
    bindings->types = xcalloc((size_t)parameter_count, sizeof(const char *));
    const char *cursor = binding_text;
    for (int i = 0; i < parameter_count; i++) {
        const Parameter *parameter = &declaration->data.function_declaration.parameters[i];
        if (!parameter->is_type_parameter) continue;
        bindings->names[bindings->count] = parameter->name;
        if (cursor) {
            const char *end = strchr(cursor, GENERIC_BINDING_SEPARATOR);
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            char *type_text = xmalloc(length + 1);
            memcpy(type_text, cursor, length);
            type_text[length] = '\0';
            bindings->types[bindings->count] = type_text;
            cursor = end ? end + 1 : NULL;
        }
        bindings->count++;
    }
}

void generic_bindings_clear(GenericBindings *bindings) {
    for (int i = 0; i < bindings->count; i++) free((void *)bindings->types[i]);
    free(bindings->names);
    free(bindings->types);
    bindings->count = 0;
    bindings->names = NULL;
    bindings->types = NULL;
}

char *generic_bindings_join(const GenericBindings *bindings) {
    size_t capacity = 1;
    for (int i = 0; i < bindings->count; i++) capacity += strlen(bindings->types[i]) + 1;
    char *joined = xmalloc(capacity);
    size_t length = 0;
    for (int i = 0; i < bindings->count; i++) {
        if (i > 0) joined[length++] = GENERIC_BINDING_SEPARATOR;
        size_t type_length = strlen(bindings->types[i]);
        memcpy(joined + length, bindings->types[i], type_length);
        length += type_length;
    }
    joined[length] = '\0';
    return joined;
}

int generic_bindings_find(const GenericBindings *bindings, const char *name) {
    if (!bindings || !name) return -1;
    for (int i = 0; i < bindings->count; i++) {
        if (strcmp(bindings->names[i], name) == 0) return i;
    }
    return -1;
}

static bool is_type_identifier_character(char character) {
    return isalnum((unsigned char)character) || character == '_';
}

char *generic_bindings_substitute(const GenericBindings *bindings, const char *type_text) {
    if (!type_text) return NULL;
    if (!bindings || bindings->count == 0) return strdup(type_text);
    size_t capacity = strlen(type_text) + 1;
    char *output = xmalloc(capacity);
    size_t length = 0;
    const char *cursor = type_text;
    while (*cursor) {
        /* "?name" is the placeholder an unbound generic leaves in a type
         * spelling; it is replaced the same way the bare name is. */
        bool is_placeholder = *cursor == '?' && is_type_identifier_character(cursor[1]);
        if (!is_placeholder && !is_type_identifier_character(*cursor)) {
            if (length + 2 > capacity) { capacity *= 2; output = xrealloc(output, capacity); }
            output[length++] = *cursor++;
            continue;
        }
        const char *token_start = cursor;
        if (is_placeholder) cursor++;
        const char *start = cursor;
        while (is_type_identifier_character(*cursor)) cursor++;
        size_t word_length = (size_t)(cursor - start);
        bool is_member = (token_start > type_text && token_start[-1] == '.') || *cursor == '.';
        int index = -1;
        if (!is_member) {
            for (int i = 0; i < bindings->count; i++) {
                if (strlen(bindings->names[i]) == word_length &&
                    memcmp(bindings->names[i], start, word_length) == 0) { index = i; break; }
            }
        }
        const char *bound = index >= 0 ? bindings->types[index] : NULL;
        const char *replacement = bound;
        size_t replacement_length = bound ? strlen(bound) : 0;
        if (!bound) {
            /* Not a generic, or an unbound one: the token stays as written,
             * except that a bare unbound name becomes its placeholder. */
            replacement = token_start;
            replacement_length = (size_t)(cursor - token_start);
        }
        bool needs_marker = index >= 0 && !bound && !is_placeholder;
        while (length + replacement_length + 2 > capacity) { capacity *= 2; output = xrealloc(output, capacity); }
        if (needs_marker) output[length++] = '?';
        memcpy(output + length, replacement, replacement_length);
        length += replacement_length;
    }
    output[length] = '\0';
    return output;
}
