/*
 * defines.c — Implementation of the -define: override pass declared in defines.h.
 *
 * Each override text is validated against the declared type of its constant
 * and turned into the literal node the parser would have produced for it;
 * that node then replaces the constant's initializer.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "defines.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "../lexer/lexer.h"

#define MAX_INTEGER_BITS 256
#define MAX_INTEGER_DIGITS 80  /* 2^256 has 78 decimal digits */

/* Writes 2^exponent in decimal into `out`. */
static void power_of_two_decimal(int exponent, char *out) {
    char digits[MAX_INTEGER_DIGITS] = {0};  /* little-endian */
    int digit_count = 1;
    digits[0] = 1;
    for (int step = 0; step < exponent; step++) {
        int carry = 0;
        for (int i = 0; i < digit_count; i++) {
            int doubled = digits[i] * 2 + carry;
            digits[i] = (char)(doubled % 10);
            carry = doubled / 10;
        }
        if (carry) digits[digit_count++] = (char)carry;
    }
    for (int i = 0; i < digit_count; i++) out[i] = (char)('0' + digits[digit_count - 1 - i]);
    out[digit_count] = '\0';
}

/* Decimal-text comparison of non-negative integers with no leading zeros. */
static int compare_magnitude(const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    if (left_length != right_length) return left_length < right_length ? -1 : 1;
    return strcmp(left, right);
}

/* Parses "i8".."i256" / "u8".."u256". Returns false for any other type. */
static bool integer_type_bits(const char *type_name, bool *is_signed, int *bits) {
    if ((type_name[0] != 'i' && type_name[0] != 'u') || !isdigit((unsigned char)type_name[1])) return false;
    *is_signed = type_name[0] == 'i';
    *bits = atoi(type_name + 1);
    return *bits >= 8 && *bits <= MAX_INTEGER_BITS;
}

/* Validates `text` as a base-10 integer of the given type and returns the
 * normalized expression text ("-12", "7"), or NULL when it is malformed or
 * out of range. */
static const char *normalize_integer(Arena *arena, const char *text, bool is_signed, int bits) {
    bool is_negative = false;
    const char *cursor = text;
    if (*cursor == '-' || *cursor == '+') {
        is_negative = *cursor == '-';
        cursor++;
    }
    if (!*cursor) return NULL;
    for (const char *digit = cursor; *digit; digit++) {
        if (!isdigit((unsigned char)*digit)) return NULL;
    }
    while (cursor[0] == '0' && cursor[1]) cursor++;
    if (strlen(cursor) >= MAX_INTEGER_DIGITS) return NULL;

    bool is_zero = cursor[0] == '0';
    if (is_negative && !is_zero && !is_signed) return NULL;

    char limit[MAX_INTEGER_DIGITS];
    power_of_two_decimal(is_signed ? bits - 1 : bits, limit);
    if (!(is_negative && !is_zero)) {
        /* maximum is 2^n - 1; a power of two never ends in 0, so no borrow */
        limit[strlen(limit) - 1]--;
    }
    if (compare_magnitude(cursor, limit) > 0) return NULL;

    char *normalized = arena_allocate(arena, strlen(cursor) + 2);
    normalized[0] = '\0';
    if (is_negative && !is_zero) strcpy(normalized, "-");
    strcat(normalized, cursor);
    return normalized;
}

/* Parses `expression_text` with the real parser and returns the initializer
 * of `const D i64 = <expression_text>`, so literals are built exactly as
 * source-written ones are. */
static AstNode *parse_initializer(Arena *arena, const char *input_file, const char *expression_text) {
    size_t length = strlen(expression_text) + 32;
    char *source = arena_allocate(arena, length);
    snprintf(source, length, "const D i64 = %s\n", expression_text);
    DiagnosticList *scratch = diagnostic_create();
    Lexer *lexer = lexer_create(arena, source, input_file);
    Parser *parser = parser_create(arena, lexer, input_file, scratch);
    AstNode *program = parser_parse_program(parser);
    bool has_errors = diagnostic_has_errors(scratch);
    diagnostic_destroy(scratch);
    if (has_errors || program->data.program.statement_count != 1) return NULL;
    return program->data.program.statements[0]->data.variable_declaration.value;
}

/* Decodes `text` as exactly one UTF-8 encoded codepoint. */
static bool decode_single_codepoint(const char *text, int32_t *codepoint) {
    const unsigned char *bytes = (const unsigned char *)text;
    int extra;
    int32_t value;
    if (bytes[0] < 0x80)      { extra = 0; value = bytes[0]; }
    else if (bytes[0] >= 0xC2 && bytes[0] < 0xE0) { extra = 1; value = bytes[0] & 0x1F; }
    else if (bytes[0] >= 0xE0 && bytes[0] < 0xF0) { extra = 2; value = bytes[0] & 0x0F; }
    else if (bytes[0] >= 0xF0 && bytes[0] < 0xF5) { extra = 3; value = bytes[0] & 0x07; }
    else return false;
    for (int i = 1; i <= extra; i++) {
        if ((bytes[i] & 0xC0) != 0x80) return false;
        value = (value << 6) | (bytes[i] & 0x3F);
    }
    if (bytes[extra + 1] != '\0') return false;
    static const int32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    if (value < minimum[extra] || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    *codepoint = value;
    return true;
}

/* The literal node for `text` as a value of `type_name`, or NULL when `text`
 * is not a valid value of that type. `site` is the node being replaced. */
static AstNode *build_initializer(Arena *arena, const char *input_file, const char *type_name,
                                  const char *text, AstNode *site) {
    Token token = site->token;
    bool is_signed;
    int bits;
    if (integer_type_bits(type_name, &is_signed, &bits)) {
        const char *normalized = normalize_integer(arena, text, is_signed, bits);
        return normalized ? parse_initializer(arena, input_file, normalized) : NULL;
    }
    if (strcmp(type_name, "f32") == 0 || strcmp(type_name, "f64") == 0) {
        if (!*text || strspn(text, "0123456789+-.eE") != strlen(text)) return NULL;
        char *end;
        double value = strtod(text, &end);
        if (*end || !isfinite(value)) return NULL;
        if (strcmp(type_name, "f32") == 0 && fabs(value) > FLT_MAX) return NULL;
        AstNode *node = ast_allocate(arena, NODE_FLOATING_POINT_LITERAL, token);
        node->data.floating_point_literal.value = value;
        return node;
    }
    if (strcmp(type_name, "bool") == 0) {
        if (strcmp(text, "true") != 0 && strcmp(text, "false") != 0) return NULL;
        AstNode *node = ast_allocate(arena, NODE_BOOL_VALUE, token);
        node->data.bool_value.value = text[0] == 't';
        return node;
    }
    if (strcmp(type_name, "char") == 0) {
        int32_t codepoint;
        if (!decode_single_codepoint(text, &codepoint)) return NULL;
        AstNode *node = ast_allocate(arena, NODE_CHAR_VALUE, token);
        node->data.char_value.value = codepoint;
        return node;
    }
    token.type = TOKEN_RAW_STRING;
    token.literal = text;
    AstNode *node = ast_allocate(arena, NODE_STRING_VALUE, token);
    node->data.string_value.value = text;
    node->data.string_value.is_raw = true;
    return node;
}

static AstNode *find_constant(AstNode *program, const char *name) {
    for (int i = 0; i < program->data.program.statement_count; i++) {
        AstNode *statement = program->data.program.statements[i];
        if (statement && statement->kind == NODE_VARIABLE_DECLARATION &&
            !statement->data.variable_declaration.is_mutable &&
            strcmp(statement->data.variable_declaration.name, name) == 0) {
            return statement;
        }
    }
    return NULL;
}

void defines_apply(Arena *arena, DiagnosticList *diagnostics, AstNode *program,
                   const char *input_file, const char *const *overrides, int count) {
    for (int i = 0; i < count; i++) {
        const char *separator = strchr(overrides[i], '=');
        if (!separator) continue;  /* the CLI rejects a flag without '=' */
        size_t name_length = (size_t)(separator - overrides[i]);
        char *name = arena_allocate(arena, name_length + 1);
        memcpy(name, overrides[i], name_length);
        name[name_length] = '\0';
        const char *text = arena_copy_string(arena, separator + 1);

        AstNode *declaration = find_constant(program, name);
        if (!declaration) {
            diagnostic_error_code_formatted(diagnostics, "E4040", input_file, 0, 0, 0, name);
            continue;
        }
        Token site = declaration->token;
        if (!declaration->data.variable_declaration.is_define) {
            diagnostic_error_code_formatted(diagnostics, "E5066", input_file, site.line, site.column, 0, name);
            continue;
        }
        const char *type_name = declaration->data.variable_declaration.type_name;
        AstNode *value = declaration->data.variable_declaration.value;
        AstNode *replacement = build_initializer(arena, input_file, type_name, text,
                                                 value ? value : declaration);
        if (!replacement) {
            diagnostic_error_code_formatted(diagnostics, "E3214", input_file, site.line, site.column, 0,
                                            name, text, type_name);
            continue;
        }
        declaration->data.variable_declaration.value = replacement;
    }
}
