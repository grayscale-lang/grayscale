/*
 * test_lexer.c — Unit tests for the grayc lexer, covering tokenization
 * of literals, operators, keywords, comments, and edge cases.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include "../src/util/arena.h"
#include "../src/lexer/lexer.h"
#include <stdint.h>

static Arena *arena;

static Lexer *create_test_lexer(const char *input) {
    return lexer_create(arena, input, "test.gray");
}

static Token next_token(Lexer *lexer) {
    return lexer_next_token(lexer);
}

static void test_empty_input(void) {
    Lexer *lexer = create_test_lexer("");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_EOF);
}

static void test_single_char_tokens(void) {
    Lexer *lexer = create_test_lexer("(){}[],.:@");
    ASSERT_EQ(next_token(lexer).type, TOK_LPAREN);
    ASSERT_EQ(next_token(lexer).type, TOK_RPAREN);
    ASSERT_EQ(next_token(lexer).type, TOK_LBRACE);
    ASSERT_EQ(next_token(lexer).type, TOK_RBRACE);
    ASSERT_EQ(next_token(lexer).type, TOK_LBRACKET);
    ASSERT_EQ(next_token(lexer).type, TOK_RBRACKET);
    ASSERT_EQ(next_token(lexer).type, TOK_COMMA);
    ASSERT_EQ(next_token(lexer).type, TOK_DOT);
    ASSERT_EQ(next_token(lexer).type, TOK_COLON);
    ASSERT_EQ(next_token(lexer).type, TOK_AT);
    ASSERT_EQ(next_token(lexer).type, TOK_EOF);
}

static void test_two_char_tokens(void) {
    Lexer *lexer = create_test_lexer("== != <= >= += -= *= /= ++ -- -> && ||");
    ASSERT_EQ(next_token(lexer).type, TOK_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_NOT_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_LT_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_GT_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_PLUS_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOK_MINUS_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOK_ASTERISK_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOK_SLASH_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOK_INCREMENT);
    ASSERT_EQ(next_token(lexer).type, TOK_DECREMENT);
    ASSERT_EQ(next_token(lexer).type, TOK_ARROW);
    ASSERT_EQ(next_token(lexer).type, TOK_AND);
    ASSERT_EQ(next_token(lexer).type, TOK_OR);
}

static void test_integer_literal(void) {
    Lexer *lexer = create_test_lexer("42");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "42");
}

static void test_integer_with_separators(void) {
    Lexer *lexer = create_test_lexer("1_000_000");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "1_000_000");
}

static void test_float_literal(void) {
    Lexer *lexer = create_test_lexer("3.14");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_FLOAT);
    ASSERT_STR_EQ(token.literal, "3.14");
}

static void test_string_literal(void) {
    Lexer *lexer = create_test_lexer("\"hello world\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_STRING);
    ASSERT_STR_EQ(token.literal, "hello world");
}

static void test_string_with_escapes(void) {
    Lexer *lexer = create_test_lexer("\"hello\\nworld\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_STRING);
    ASSERT_STR_EQ(token.literal, "hello\\nworld");
}

static void test_char_literal(void) {
    Lexer *lexer = create_test_lexer("'A'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_CHAR);
    ASSERT_STR_EQ(token.literal, "A");
}

static void test_raw_string(void) {
    Lexer *lexer = create_test_lexer("`raw string`");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_RAW_STRING);
    ASSERT_STR_EQ(token.literal, "raw string");
}

static void test_keywords(void) {
    Lexer *lexer = create_test_lexer("mut const do return if or otherwise for for_each as_long_as loop break continue");
    ASSERT_EQ(next_token(lexer).type, TOK_MUT);
    ASSERT_EQ(next_token(lexer).type, TOK_CONST);
    ASSERT_EQ(next_token(lexer).type, TOK_DO);
    ASSERT_EQ(next_token(lexer).type, TOK_RETURN);
    ASSERT_EQ(next_token(lexer).type, TOK_IF);
    ASSERT_EQ(next_token(lexer).type, TOK_OR_KW);
    ASSERT_EQ(next_token(lexer).type, TOK_OTHERWISE);
    ASSERT_EQ(next_token(lexer).type, TOK_FOR);
    ASSERT_EQ(next_token(lexer).type, TOK_FOR_EACH);
    ASSERT_EQ(next_token(lexer).type, TOK_AS_LONG_AS);
    ASSERT_EQ(next_token(lexer).type, TOK_LOOP);
    ASSERT_EQ(next_token(lexer).type, TOK_BREAK);
    ASSERT_EQ(next_token(lexer).type, TOK_CONTINUE);
}

static void test_more_keywords(void) {
    Lexer *lexer = create_test_lexer("import using struct enum nil new true false when is default cast ensure module private");
    ASSERT_EQ(next_token(lexer).type, TOK_IMPORT);
    ASSERT_EQ(next_token(lexer).type, TOK_USING);
    ASSERT_EQ(next_token(lexer).type, TOK_STRUCT);
    ASSERT_EQ(next_token(lexer).type, TOK_ENUM);
    ASSERT_EQ(next_token(lexer).type, TOK_NIL);
    ASSERT_EQ(next_token(lexer).type, TOK_NEW);
    ASSERT_EQ(next_token(lexer).type, TOK_TRUE);
    ASSERT_EQ(next_token(lexer).type, TOK_FALSE);
    ASSERT_EQ(next_token(lexer).type, TOK_WHEN);
    ASSERT_EQ(next_token(lexer).type, TOK_IS);
    ASSERT_EQ(next_token(lexer).type, TOK_DEFAULT);
    ASSERT_EQ(next_token(lexer).type, TOK_CAST);
    ASSERT_EQ(next_token(lexer).type, TOK_ENSURE);
    ASSERT_EQ(next_token(lexer).type, TOK_MODULE);
    ASSERT_EQ(next_token(lexer).type, TOK_PRIVATE);
}

static void test_identifiers(void) {
    Lexer *lexer = create_test_lexer("foo bar_baz count123");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOK_IDENT);
    ASSERT_STR_EQ(token1.literal, "foo");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOK_IDENT);
    ASSERT_STR_EQ(token2.literal, "bar_baz");
    Token token3 = next_token(lexer);
    ASSERT_EQ(token3.type, TOK_IDENT);
    ASSERT_STR_EQ(token3.literal, "count123");
}

/* Repeated identifiers must resolve to the same arena-owned pointer
 * (deduplicated via arena_intern_string), while distinct identifiers must
 * still get distinct pointers. */
static void test_identifier_interning(void) {
    Lexer *lexer = create_test_lexer("count count count total count");
    Token first = next_token(lexer);
    ASSERT_EQ(first.type, TOK_IDENT);
    ASSERT_STR_EQ(first.literal, "count");

    Token second = next_token(lexer);
    ASSERT_EQ(second.type, TOK_IDENT);
    ASSERT_EQ((long long)(intptr_t)second.literal, (long long)(intptr_t)first.literal);

    Token third = next_token(lexer);
    ASSERT_EQ((long long)(intptr_t)third.literal, (long long)(intptr_t)first.literal);

    Token distinct = next_token(lexer);
    ASSERT_EQ(distinct.type, TOK_IDENT);
    ASSERT_STR_EQ(distinct.literal, "total");
    ASSERT_NE((long long)(intptr_t)distinct.literal, (long long)(intptr_t)first.literal);

    Token fifth = next_token(lexer);
    ASSERT_EQ((long long)(intptr_t)fifth.literal, (long long)(intptr_t)first.literal);
}

/* Interning is arena-scoped, not lexer-scoped: two separate lexers sharing
 * one arena (mirroring how main.gray and its imports are lexed) must still
 * share the same pointer for the same identifier text. */
static void test_identifier_interning_across_lexers(void) {
    Lexer *lexer_a = create_test_lexer("shared_name");
    Lexer *lexer_b = create_test_lexer("shared_name");
    Token from_a = next_token(lexer_a);
    Token from_b = next_token(lexer_b);
    ASSERT_EQ(from_a.type, TOK_IDENT);
    ASSERT_EQ(from_b.type, TOK_IDENT);
    ASSERT_EQ((long long)(intptr_t)from_b.literal, (long long)(intptr_t)from_a.literal);
}

/* Keywords must resolve to the keyword table's own static string (never an
 * arena copy), and every occurrence of the same keyword must share that
 * one static pointer. */
static void test_keyword_bypasses_arena(void) {
    Lexer *lexer = create_test_lexer("return return if");
    Token first_return = next_token(lexer);
    ASSERT_EQ(first_return.type, TOK_RETURN);
    ASSERT_STR_EQ(first_return.literal, "return");

    Token second_return = next_token(lexer);
    ASSERT_EQ(second_return.type, TOK_RETURN);
    ASSERT_EQ((long long)(intptr_t)second_return.literal, (long long)(intptr_t)first_return.literal);

    Token if_tok = next_token(lexer);
    ASSERT_EQ(if_tok.type, TOK_IF);
    ASSERT_STR_EQ(if_tok.literal, "if");
}

static void test_skip_comments(void) {
    Lexer *lexer = create_test_lexer("a // line comment\nb");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOK_IDENT);
    ASSERT_STR_EQ(token1.literal, "a");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOK_IDENT);
    ASSERT_STR_EQ(token2.literal, "b");
}

static void test_skip_block_comments(void) {
    Lexer *lexer = create_test_lexer("a /* block */ b");
    Token token1 = next_token(lexer);
    ASSERT_STR_EQ(token1.literal, "a");
    Token token2 = next_token(lexer);
    ASSERT_STR_EQ(token2.literal, "b");
}

static void test_line_tracking(void) {
    Lexer *lexer = create_test_lexer("a\nb\nc");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.line, 1);
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.line, 2);
    Token token3 = next_token(lexer);
    ASSERT_EQ(token3.line, 3);
}

static void test_hash_attributes(void) {
    Lexer *lexer = create_test_lexer("#strict #flags #doc");
    ASSERT_EQ(next_token(lexer).type, TOK_STRICT);
    ASSERT_EQ(next_token(lexer).type, TOK_FLAGS);
    ASSERT_EQ(next_token(lexer).type, TOK_DOC);
}

static void test_hello_world_tokens(void) {
    Lexer *lexer = create_test_lexer("do main() {\n    println(\"Hello\")\n}");
    ASSERT_EQ(next_token(lexer).type, TOK_DO);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);  /* main */
    ASSERT_EQ(next_token(lexer).type, TOK_LPAREN);
    ASSERT_EQ(next_token(lexer).type, TOK_RPAREN);
    ASSERT_EQ(next_token(lexer).type, TOK_LBRACE);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);  /* println */
    ASSERT_EQ(next_token(lexer).type, TOK_LPAREN);
    ASSERT_EQ(next_token(lexer).type, TOK_STRING); /* "Hello" */
    ASSERT_EQ(next_token(lexer).type, TOK_RPAREN);
    ASSERT_EQ(next_token(lexer).type, TOK_RBRACE);
    ASSERT_EQ(next_token(lexer).type, TOK_EOF);
}

static void test_v3_keyword_aliases(void) {
    /* mut maps to TOK_MUT, while maps to TOK_AS_LONG_AS */
    Lexer *lexer = create_test_lexer("mut while");
    ASSERT_EQ(next_token(lexer).type, TOK_MUT);
    ASSERT_EQ(next_token(lexer).type, TOK_AS_LONG_AS);
}

static void test_v3_keywords_coexist(void) {
    /* as_long_as and while produce the same token */
    Lexer *lexer = create_test_lexer("as_long_as while");
    Token token1 = next_token(lexer);
    Token token2 = next_token(lexer);
    ASSERT_EQ(token1.type, token2.type);
}

static void test_not_in_operator(void) {
    Lexer *lexer = create_test_lexer("!in");
    ASSERT_EQ(next_token(lexer).type, TOK_NOT_IN);
}

static void test_not_in_vs_bang(void) {
    /* !in should be NOT_IN, !inside should be BANG + IDENT */
    Lexer *lexer = create_test_lexer("!in !inside");
    ASSERT_EQ(next_token(lexer).type, TOK_NOT_IN);
    ASSERT_EQ(next_token(lexer).type, TOK_BANG);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);
}

static void test_hex_literal(void) {
    Lexer *lexer = create_test_lexer("0xFF");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "0xFF");
}

static void test_octal_literal(void) {
    Lexer *lexer = create_test_lexer("0o77");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "0o77");
}

static void test_binary_literal(void) {
    Lexer *lexer = create_test_lexer("0b1010");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "0b1010");
}

static void test_or_return_keyword(void) {
    Lexer *lexer = create_test_lexer("or_return");
    ASSERT_EQ(next_token(lexer).type, TOK_OR_RETURN);
}

static void test_caret_token(void) {
    Lexer *lexer = create_test_lexer("^int");
    ASSERT_EQ(next_token(lexer).type, TOK_CARET);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);
}

static void test_float_with_underscores(void) {
    Lexer *lexer = create_test_lexer("3.141_592_653");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_FLOAT);
    ASSERT_STR_EQ(token.literal, "3.141_592_653");
}

static void test_interpolation_tokens(void) {
    /* Interpolated string starts as a string token */
    Lexer *lexer = create_test_lexer("\"hello ${name}\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_STRING);
}

static void test_bang_in_vs_bang(void) {
    /* !in should be NOT_IN, ! alone should be BANG */
    Lexer *lexer = create_test_lexer("!in x !y");
    ASSERT_EQ(next_token(lexer).type, TOK_NOT_IN);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT); /* x */
    ASSERT_EQ(next_token(lexer).type, TOK_BANG);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT); /* y */
}

/* --- Lexer Error Path Tests --- */

static void test_error_E1003_unclosed_comment(void) {
    Lexer *lexer = create_test_lexer("/* unclosed comment");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1003");
}

static void test_error_E1005_unclosed_char(void) {
    Lexer *lexer = create_test_lexer("'a");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1005");
}

static void test_error_E1006_bad_escape_string(void) {
    Lexer *lexer = create_test_lexer("\"hello\\q\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1006");
}

static void test_error_E1007_bad_escape_char(void) {
    Lexer *lexer = create_test_lexer("'\\q'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1007");
}

static void test_error_E1010_bad_number_format(void) {
    Lexer *lexer = create_test_lexer("0x");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1010");
}

static void test_error_E1011_consecutive_underscores(void) {
    Lexer *lexer = create_test_lexer("1__000");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1011");
}

static void test_error_E1013_trailing_underscore(void) {
    Lexer *lexer = create_test_lexer("100_");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1013");
}

static void test_error_E1014_underscore_before_decimal(void) {
    Lexer *lexer = create_test_lexer("1_.5");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1014");
}

static void test_error_E1017_unclosed_raw_string(void) {
    Lexer *lexer = create_test_lexer("`unclosed raw");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1017");
}

static void test_error_E1010_bad_octal(void) {
    Lexer *lexer = create_test_lexer("0o");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1010");
}

static void test_error_E1010_bad_binary(void) {
    Lexer *lexer = create_test_lexer("0b");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1010");
}

/* --- Single-character operator tokens --- */

static void test_single_operators(void) {
    Lexer *lexer = create_test_lexer("+ - * / % < > =");
    ASSERT_EQ(next_token(lexer).type, TOK_PLUS);
    ASSERT_EQ(next_token(lexer).type, TOK_MINUS);
    ASSERT_EQ(next_token(lexer).type, TOK_ASTERISK);
    ASSERT_EQ(next_token(lexer).type, TOK_SLASH);
    ASSERT_EQ(next_token(lexer).type, TOK_PERCENT);
    ASSERT_EQ(next_token(lexer).type, TOK_LT);
    ASSERT_EQ(next_token(lexer).type, TOK_GT);
    ASSERT_EQ(next_token(lexer).type, TOK_ASSIGN);
}

static void test_double_asterisk(void) {
    Lexer *lexer = create_test_lexer("**");
    ASSERT_EQ(next_token(lexer).type, TOK_ASTERISK);
    ASSERT_EQ(next_token(lexer).type, TOK_ASTERISK);
}

static void test_percent_assign(void) {
    Lexer *lexer = create_test_lexer("%=");
    ASSERT_EQ(next_token(lexer).type, TOK_PERCENT_ASSIGN);
}

static void test_ampersand(void) {
    Lexer *lexer = create_test_lexer("&x");
    ASSERT_EQ(next_token(lexer).type, TOK_AMPERSAND);
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);
}

static void test_semicolon(void) {
    Lexer *lexer = create_test_lexer(";");
    ASSERT_EQ(next_token(lexer).type, TOK_SEMICOLON);
}

/* --- Missing keyword tokens --- */

static void test_keyword_in(void) {
    Lexer *lexer = create_test_lexer("in");
    ASSERT_EQ(next_token(lexer).type, TOK_IN);
}

static void test_keyword_range(void) {
    Lexer *lexer = create_test_lexer("range");
    ASSERT_EQ(next_token(lexer).type, TOK_RANGE);
}

static void test_keyword_use(void) {
    Lexer *lexer = create_test_lexer("use");
    ASSERT_EQ(next_token(lexer).type, TOK_USE);
}

static void test_keyword_blank(void) {
    Lexer *lexer = create_test_lexer("_");
    ASSERT_EQ(next_token(lexer).type, TOK_BLANK);
}


/* --- Column tracking --- */

static void test_column_tracking(void) {
    Lexer *lexer = create_test_lexer("ab cd");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.column, 1);
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.column, 4);
}

static void test_column_resets_on_newline(void) {
    Lexer *lexer = create_test_lexer("ab\ncd");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.line, 1);
    ASSERT_EQ(token1.column, 1);
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.line, 2);
    ASSERT_EQ(token2.column, 1);
}

/* --- Remaining lexer error paths --- */

/* Note: E1015 (underscore after decimal) and E1016 (trailing decimal) are
 * unreachable — read_number() only consumes the decimal point when followed
 * by a digit, so these validation checks are dead code. */

/* --- Char escape sequences --- */

static void test_char_escape_newline(void) {
    Lexer *lexer = create_test_lexer("'\\n'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_CHAR);
    ASSERT_STR_EQ(token.literal, "\\n");
}

static void test_char_escape_tab(void) {
    Lexer *lexer = create_test_lexer("'\\t'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_CHAR);
    ASSERT_STR_EQ(token.literal, "\\t");
}

/* --- Additional lexer coverage --- */

static void test_zero_literal(void) {
    Lexer *lexer = create_test_lexer("0");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "0");
}

static void test_negative_float_tokens(void) {
    /* -3.14 should tokenize as MINUS then FLOAT */
    Lexer *lexer = create_test_lexer("-3.14");
    ASSERT_EQ(next_token(lexer).type, TOK_MINUS);
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_FLOAT);
    ASSERT_STR_EQ(token.literal, "3.14");
}

static void test_adjacent_operators(void) {
    /* >= should be GT_EQ, not GT + ASSIGN */
    Lexer *lexer = create_test_lexer(">= <= == !=");
    ASSERT_EQ(next_token(lexer).type, TOK_GT_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_LT_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_EQ);
    ASSERT_EQ(next_token(lexer).type, TOK_NOT_EQ);
}

static void test_arrow_vs_minus(void) {
    /* -> should be ARROW, - > should be MINUS GT */
    Lexer *lexer = create_test_lexer("->");
    ASSERT_EQ(next_token(lexer).type, TOK_ARROW);
}

static void test_string_with_interpolation_marker(void) {
    /* String containing ${...} is still a string token */
    Lexer *lexer = create_test_lexer("\"hello ${name} world\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_STRING);
}

static void test_multiline_block_comment(void) {
    Lexer *lexer = create_test_lexer("a /* this is\na multiline\ncomment */ b");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOK_IDENT);
    ASSERT_STR_EQ(token1.literal, "a");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOK_IDENT);
    ASSERT_STR_EQ(token2.literal, "b");
}

static void test_empty_string(void) {
    Lexer *lexer = create_test_lexer("\"\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_STRING);
    ASSERT_STR_EQ(token.literal, "");
}

static void test_multiple_strings(void) {
    Lexer *lexer = create_test_lexer("\"hello\" \"world\"");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOK_STRING);
    ASSERT_STR_EQ(token1.literal, "hello");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOK_STRING);
    ASSERT_STR_EQ(token2.literal, "world");
}

static void test_consecutive_operators(void) {
    /* ++ -- should not be confused */
    Lexer *lexer = create_test_lexer("++ --");
    ASSERT_EQ(next_token(lexer).type, TOK_INCREMENT);
    ASSERT_EQ(next_token(lexer).type, TOK_DECREMENT);
}

static void test_large_integer_literal(void) {
    Lexer *lexer = create_test_lexer("9999999999");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "9999999999");
}

static void test_char_single_quote(void) {
    Lexer *lexer = create_test_lexer("'x'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_CHAR);
    ASSERT_STR_EQ(token.literal, "x");
}

static void test_keyword_panic(void) {
    Lexer *lexer = create_test_lexer("panic");
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT); /* panic is a builtin func, not keyword */
}

static void test_mixed_tokens_line(void) {
    /* Realistic code line */
    Lexer *lexer = create_test_lexer("mut x int = 42");
    ASSERT_EQ(next_token(lexer).type, TOK_MUT);      /* mut */
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);     /* x */
    ASSERT_EQ(next_token(lexer).type, TOK_IDENT);     /* int */
    ASSERT_EQ(next_token(lexer).type, TOK_ASSIGN);    /* = */
    ASSERT_EQ(next_token(lexer).type, TOK_INT);       /* 42 */
    ASSERT_EQ(next_token(lexer).type, TOK_EOF);
}

static void test_line_tracking_with_comment(void) {
    /* Comments should not affect line tracking */
    Lexer *lexer = create_test_lexer("a\n// comment\nb");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.line, 1);
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.line, 3);
    ASSERT_STR_EQ(token2.literal, "b");
}

static void test_hex_upper_lower(void) {
    Lexer *lexer = create_test_lexer("0xABCD");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOK_INT);
    ASSERT_STR_EQ(token.literal, "0xABCD");
}

int main(void) {
    arena = arena_create(64 * 1024);
    printf("\n");
    RUN_TEST(test_empty_input);
    RUN_TEST(test_single_char_tokens);
    RUN_TEST(test_two_char_tokens);
    RUN_TEST(test_integer_literal);
    RUN_TEST(test_integer_with_separators);
    RUN_TEST(test_float_literal);
    RUN_TEST(test_string_literal);
    RUN_TEST(test_string_with_escapes);
    RUN_TEST(test_char_literal);
    RUN_TEST(test_raw_string);
    RUN_TEST(test_keywords);
    RUN_TEST(test_more_keywords);
    RUN_TEST(test_identifiers);
    RUN_TEST(test_identifier_interning);
    RUN_TEST(test_identifier_interning_across_lexers);
    RUN_TEST(test_keyword_bypasses_arena);
    RUN_TEST(test_skip_comments);
    RUN_TEST(test_skip_block_comments);
    RUN_TEST(test_line_tracking);
    RUN_TEST(test_hash_attributes);
    RUN_TEST(test_hello_world_tokens);
    RUN_TEST(test_v3_keyword_aliases);
    RUN_TEST(test_v3_keywords_coexist);
    RUN_TEST(test_not_in_operator);
    RUN_TEST(test_not_in_vs_bang);
    RUN_TEST(test_hex_literal);
    RUN_TEST(test_octal_literal);
    RUN_TEST(test_binary_literal);
    RUN_TEST(test_or_return_keyword);
    RUN_TEST(test_caret_token);
    RUN_TEST(test_float_with_underscores);
    RUN_TEST(test_interpolation_tokens);
    RUN_TEST(test_bang_in_vs_bang);

    /* Single-character operators */
    RUN_TEST(test_single_operators);
    RUN_TEST(test_double_asterisk);
    RUN_TEST(test_percent_assign);
    RUN_TEST(test_ampersand);
    RUN_TEST(test_semicolon);

    /* Missing keywords */
    RUN_TEST(test_keyword_in);
    RUN_TEST(test_keyword_range);
    RUN_TEST(test_keyword_use);
    RUN_TEST(test_keyword_blank);

    /* Column tracking */
    RUN_TEST(test_column_tracking);
    RUN_TEST(test_column_resets_on_newline);

    /* Char escape sequences */
    RUN_TEST(test_char_escape_newline);
    RUN_TEST(test_char_escape_tab);

    /* Lexer error path tests */
    RUN_TEST(test_error_E1003_unclosed_comment);
    RUN_TEST(test_error_E1005_unclosed_char);
    RUN_TEST(test_error_E1006_bad_escape_string);
    RUN_TEST(test_error_E1007_bad_escape_char);
    RUN_TEST(test_error_E1010_bad_number_format);
    RUN_TEST(test_error_E1011_consecutive_underscores);
    RUN_TEST(test_error_E1013_trailing_underscore);
    RUN_TEST(test_error_E1014_underscore_before_decimal);
    /* E1015 and E1016 are unreachable (see comment above) */
    RUN_TEST(test_error_E1017_unclosed_raw_string);
    RUN_TEST(test_error_E1010_bad_octal);
    RUN_TEST(test_error_E1010_bad_binary);

    /* Additional lexer coverage */
    RUN_TEST(test_zero_literal);
    RUN_TEST(test_negative_float_tokens);
    RUN_TEST(test_adjacent_operators);
    RUN_TEST(test_arrow_vs_minus);
    RUN_TEST(test_string_with_interpolation_marker);
    RUN_TEST(test_multiline_block_comment);
    RUN_TEST(test_empty_string);
    RUN_TEST(test_multiple_strings);
    RUN_TEST(test_consecutive_operators);
    RUN_TEST(test_large_integer_literal);
    RUN_TEST(test_char_single_quote);
    RUN_TEST(test_keyword_panic);
    RUN_TEST(test_mixed_tokens_line);
    RUN_TEST(test_line_tracking_with_comment);
    RUN_TEST(test_hex_upper_lower);

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
