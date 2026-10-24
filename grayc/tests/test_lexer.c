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
    ASSERT_EQ(token.type, TOKEN_END_OF_FILE);
}

static void test_single_char_tokens(void) {
    Lexer *lexer = create_test_lexer("(){}[],.:@");
    ASSERT_EQ(next_token(lexer).type, TOKEN_LEFT_PARENTHESIS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_RIGHT_PARENTHESIS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LEFT_BRACE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_RIGHT_BRACE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LEFT_BRACKET);
    ASSERT_EQ(next_token(lexer).type, TOKEN_RIGHT_BRACKET);
    ASSERT_EQ(next_token(lexer).type, TOKEN_COMMA);
    ASSERT_EQ(next_token(lexer).type, TOKEN_DOT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_COLON);
    ASSERT_EQ(next_token(lexer).type, TOKEN_AT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_END_OF_FILE);
}

static void test_two_char_tokens(void) {
    Lexer *lexer = create_test_lexer("== != <= >= += -= *= /= ++ -- -> && ||");
    ASSERT_EQ(next_token(lexer).type, TOKEN_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_NOT_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LESS_THAN_OR_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_GREATER_THAN_OR_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_PLUS_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_MINUS_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ASTERISK_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_SLASH_ASSIGN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_INCREMENT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_DECREMENT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ARROW);
    ASSERT_EQ(next_token(lexer).type, TOKEN_AND);
    ASSERT_EQ(next_token(lexer).type, TOKEN_OR);
}

static void test_integer_literal(void) {
    Lexer *lexer = create_test_lexer("42");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "42");
}

static void test_integer_with_separators(void) {
    Lexer *lexer = create_test_lexer("1_000_000");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "1_000_000");
}

static void test_float_literal(void) {
    Lexer *lexer = create_test_lexer("3.14");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_FLOATING_POINT_LITERAL);
    ASSERT_STR_EQ(token.literal, "3.14");
}

static void test_string_literal(void) {
    Lexer *lexer = create_test_lexer("\"hello world\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_STRING);
    ASSERT_STR_EQ(token.literal, "hello world");
}

static void test_string_with_escapes(void) {
    Lexer *lexer = create_test_lexer("\"hello\\nworld\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_STRING);
    ASSERT_STR_EQ(token.literal, "hello\\nworld");
}

static void test_char_literal(void) {
    Lexer *lexer = create_test_lexer("'A'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_CHAR);
    ASSERT_STR_EQ(token.literal, "A");
}

static void test_raw_string(void) {
    Lexer *lexer = create_test_lexer("`raw string`");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_RAW_STRING);
    ASSERT_STR_EQ(token.literal, "raw string");
}

static void test_keywords(void) {
    Lexer *lexer = create_test_lexer("mut const do return if or otherwise for for_each as_long_as loop break continue");
    ASSERT_EQ(next_token(lexer).type, TOKEN_MUT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_CONST);
    ASSERT_EQ(next_token(lexer).type, TOKEN_DO);
    ASSERT_EQ(next_token(lexer).type, TOKEN_RETURN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IF);
    ASSERT_EQ(next_token(lexer).type, TOKEN_OR_KEYWORD);
    ASSERT_EQ(next_token(lexer).type, TOKEN_OTHERWISE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_FOR);
    ASSERT_EQ(next_token(lexer).type, TOKEN_FOR_EACH);
    ASSERT_EQ(next_token(lexer).type, TOKEN_AS_LONG_AS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LOOP);
    ASSERT_EQ(next_token(lexer).type, TOKEN_BREAK);
    ASSERT_EQ(next_token(lexer).type, TOKEN_CONTINUE);
}

static void test_more_keywords(void) {
    Lexer *lexer = create_test_lexer("import using struct enum nil new true false when is default cast ensure private generic");
    ASSERT_EQ(next_token(lexer).type, TOKEN_IMPORT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_USING);
    ASSERT_EQ(next_token(lexer).type, TOKEN_STRUCT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ENUM);
    ASSERT_EQ(next_token(lexer).type, TOKEN_NIL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_NEW);
    ASSERT_EQ(next_token(lexer).type, TOKEN_TRUE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_FALSE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_WHEN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_DEFAULT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_CAST);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ENSURE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_PRIVATE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_GENERIC);
}

static void test_identifiers(void) {
    Lexer *lexer = create_test_lexer("foo bar_baz count123");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(token1.literal, "foo");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(token2.literal, "bar_baz");
    Token token3 = next_token(lexer);
    ASSERT_EQ(token3.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(token3.literal, "count123");
}

/* Repeated identifiers must resolve to the same arena-owned pointer
 * (deduplicated via arena_intern_string), while distinct identifiers must
 * still get distinct pointers. */
static void test_identifier_interning(void) {
    Lexer *lexer = create_test_lexer("count count count total count");
    Token first = next_token(lexer);
    ASSERT_EQ(first.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(first.literal, "count");

    Token second = next_token(lexer);
    ASSERT_EQ(second.type, TOKEN_IDENTIFIER);
    ASSERT_EQ((long long)(intptr_t)second.literal, (long long)(intptr_t)first.literal);

    Token third = next_token(lexer);
    ASSERT_EQ((long long)(intptr_t)third.literal, (long long)(intptr_t)first.literal);

    Token distinct = next_token(lexer);
    ASSERT_EQ(distinct.type, TOKEN_IDENTIFIER);
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
    ASSERT_EQ(from_a.type, TOKEN_IDENTIFIER);
    ASSERT_EQ(from_b.type, TOKEN_IDENTIFIER);
    ASSERT_EQ((long long)(intptr_t)from_b.literal, (long long)(intptr_t)from_a.literal);
}

/* Keywords must resolve to the keyword table's own static string (never an
 * arena copy), and every occurrence of the same keyword must share that
 * one static pointer. */
static void test_keyword_bypasses_arena(void) {
    Lexer *lexer = create_test_lexer("return return if");
    Token first_return = next_token(lexer);
    ASSERT_EQ(first_return.type, TOKEN_RETURN);
    ASSERT_STR_EQ(first_return.literal, "return");

    Token second_return = next_token(lexer);
    ASSERT_EQ(second_return.type, TOKEN_RETURN);
    ASSERT_EQ((long long)(intptr_t)second_return.literal, (long long)(intptr_t)first_return.literal);

    Token if_tok = next_token(lexer);
    ASSERT_EQ(if_tok.type, TOKEN_IF);
    ASSERT_STR_EQ(if_tok.literal, "if");
}

static void test_skip_comments(void) {
    Lexer *lexer = create_test_lexer("a // line comment\nb");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(token1.literal, "a");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOKEN_IDENTIFIER);
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
    Lexer *lexer = create_test_lexer("#strict #flags #doc #test");
    ASSERT_EQ(next_token(lexer).type, TOKEN_STRICT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_FLAGS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_DOC);
    ASSERT_EQ(next_token(lexer).type, TOKEN_TEST);
}

static void test_hello_world_tokens(void) {
    Lexer *lexer = create_test_lexer("do main() {\n    println(\"Hello\")\n}");
    ASSERT_EQ(next_token(lexer).type, TOKEN_DO);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);  /* main */
    ASSERT_EQ(next_token(lexer).type, TOKEN_LEFT_PARENTHESIS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_RIGHT_PARENTHESIS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LEFT_BRACE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);  /* println */
    ASSERT_EQ(next_token(lexer).type, TOKEN_LEFT_PARENTHESIS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_STRING); /* "Hello" */
    ASSERT_EQ(next_token(lexer).type, TOKEN_RIGHT_PARENTHESIS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_RIGHT_BRACE);
    ASSERT_EQ(next_token(lexer).type, TOKEN_END_OF_FILE);
}

static void test_v3_keyword_aliases(void) {
    /* mut maps to TOKEN_MUT, while maps to TOKEN_AS_LONG_AS */
    Lexer *lexer = create_test_lexer("mut while");
    ASSERT_EQ(next_token(lexer).type, TOKEN_MUT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_AS_LONG_AS);
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
    ASSERT_EQ(next_token(lexer).type, TOKEN_NOT_IN);
}

static void test_not_in_vs_bang(void) {
    /* !in should be NOT_IN, !inside should be BANG + IDENT */
    Lexer *lexer = create_test_lexer("!in !inside");
    ASSERT_EQ(next_token(lexer).type, TOKEN_NOT_IN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_BANG);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);
}

static void test_hex_literal(void) {
    Lexer *lexer = create_test_lexer("0xFF");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "0xFF");
}

static void test_octal_literal(void) {
    Lexer *lexer = create_test_lexer("0o77");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "0o77");
}

static void test_binary_literal(void) {
    Lexer *lexer = create_test_lexer("0b1010");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "0b1010");
}

static void test_or_return_keyword(void) {
    Lexer *lexer = create_test_lexer("or_return");
    ASSERT_EQ(next_token(lexer).type, TOKEN_OR_RETURN);
}

static void test_or_panic_keyword(void) {
    Lexer *lexer = create_test_lexer("or_panic");
    ASSERT_EQ(next_token(lexer).type, TOKEN_OR_PANIC);
}

static void test_or_default_keyword(void) {
    Lexer *lexer = create_test_lexer("or_default");
    ASSERT_EQ(next_token(lexer).type, TOKEN_OR_DEFAULT);
}

static void test_caret_token(void) {
    Lexer *lexer = create_test_lexer("^i64");
    ASSERT_EQ(next_token(lexer).type, TOKEN_CARET);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);
}

static void test_float_with_underscores(void) {
    Lexer *lexer = create_test_lexer("3.141_592_653");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_FLOATING_POINT_LITERAL);
    ASSERT_STR_EQ(token.literal, "3.141_592_653");
}

static void test_interpolation_tokens(void) {
    /* Interpolated string starts as a string token */
    Lexer *lexer = create_test_lexer("\"hello ${name}\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_STRING);
}

static void test_bang_in_vs_bang(void) {
    /* !in should be NOT_IN, ! alone should be BANG */
    Lexer *lexer = create_test_lexer("!in x !y");
    ASSERT_EQ(next_token(lexer).type, TOKEN_NOT_IN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER); /* x */
    ASSERT_EQ(next_token(lexer).type, TOKEN_BANG);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER); /* y */
}

/* --- Lexer Error Path Tests --- */

static void test_error_E1003_unclosed_comment(void) {
    Lexer *lexer = create_test_lexer("/* unclosed comment");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1003");
}

static void test_error_E1025_question_mark(void) {
    Lexer *lexer = create_test_lexer("?");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1025");
}

static void test_error_E1005_unclosed_char(void) {
    Lexer *lexer = create_test_lexer("'a");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1005");
}

static void test_error_E1006_bad_escape_string(void) {
    Lexer *lexer = create_test_lexer("\"hello\\q\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1006");
}

static void test_error_E1007_bad_escape_char(void) {
    Lexer *lexer = create_test_lexer("'\\q'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1007");
}

static void test_error_E1018_empty_char(void) {
    Lexer *lexer = create_test_lexer("''");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1018");
}

static void test_error_E1018_multichar(void) {
    Lexer *lexer = create_test_lexer("'ab'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1018");
}

static void test_error_E1010_bad_number_format(void) {
    Lexer *lexer = create_test_lexer("0x");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1010");
}

static void test_error_E1011_consecutive_underscores(void) {
    Lexer *lexer = create_test_lexer("1__000");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1011");
}

static void test_error_E1013_trailing_underscore(void) {
    Lexer *lexer = create_test_lexer("100_");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1013");
}

static void test_error_E1014_underscore_before_decimal(void) {
    Lexer *lexer = create_test_lexer("1_.5");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1014");
}

static void test_error_E1017_unclosed_raw_string(void) {
    Lexer *lexer = create_test_lexer("`unclosed raw");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1017");
}

static void test_error_E1010_bad_octal(void) {
    Lexer *lexer = create_test_lexer("0o");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1010");
}

static void test_error_E1010_bad_binary(void) {
    Lexer *lexer = create_test_lexer("0b");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_ILLEGAL);
    ASSERT_NOT_NULL(lexer->error_code);
    ASSERT_STR_EQ(lexer->error_code, "E1010");
}

/* --- Single-character operator tokens --- */

static void test_single_operators(void) {
    Lexer *lexer = create_test_lexer("+ - * / % < > =");
    ASSERT_EQ(next_token(lexer).type, TOKEN_PLUS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_MINUS);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ASTERISK);
    ASSERT_EQ(next_token(lexer).type, TOKEN_SLASH);
    ASSERT_EQ(next_token(lexer).type, TOKEN_PERCENT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LESS_THAN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_GREATER_THAN);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ASSIGN);
}

static void test_double_asterisk(void) {
    Lexer *lexer = create_test_lexer("**");
    ASSERT_EQ(next_token(lexer).type, TOKEN_ASTERISK);
    ASSERT_EQ(next_token(lexer).type, TOKEN_ASTERISK);
}

static void test_percent_assign(void) {
    Lexer *lexer = create_test_lexer("%=");
    ASSERT_EQ(next_token(lexer).type, TOKEN_PERCENT_ASSIGN);
}

static void test_ampersand(void) {
    Lexer *lexer = create_test_lexer("&x");
    ASSERT_EQ(next_token(lexer).type, TOKEN_AMPERSAND);
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);
}

static void test_semicolon(void) {
    Lexer *lexer = create_test_lexer(";");
    ASSERT_EQ(next_token(lexer).type, TOKEN_SEMICOLON);
}

/* --- Missing keyword tokens --- */

static void test_keyword_in(void) {
    Lexer *lexer = create_test_lexer("in");
    ASSERT_EQ(next_token(lexer).type, TOKEN_IN);
}

static void test_keyword_range(void) {
    Lexer *lexer = create_test_lexer("range");
    ASSERT_EQ(next_token(lexer).type, TOKEN_RANGE);
}

static void test_keyword_use(void) {
    Lexer *lexer = create_test_lexer("use");
    ASSERT_EQ(next_token(lexer).type, TOKEN_USE);
}

static void test_keyword_blank(void) {
    Lexer *lexer = create_test_lexer("_");
    ASSERT_EQ(next_token(lexer).type, TOKEN_BLANK);
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
    ASSERT_EQ(token.type, TOKEN_CHAR);
    ASSERT_STR_EQ(token.literal, "\\n");
}

static void test_char_escape_tab(void) {
    Lexer *lexer = create_test_lexer("'\\t'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_CHAR);
    ASSERT_STR_EQ(token.literal, "\\t");
}

/* --- Additional lexer coverage --- */

static void test_zero_literal(void) {
    Lexer *lexer = create_test_lexer("0");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "0");
}

static void test_negative_float_tokens(void) {
    /* -3.14 should tokenize as MINUS then FLOAT */
    Lexer *lexer = create_test_lexer("-3.14");
    ASSERT_EQ(next_token(lexer).type, TOKEN_MINUS);
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_FLOATING_POINT_LITERAL);
    ASSERT_STR_EQ(token.literal, "3.14");
}

static void test_adjacent_operators(void) {
    /* >= should be GT_EQ, not GT + ASSIGN */
    Lexer *lexer = create_test_lexer(">= <= == !=");
    ASSERT_EQ(next_token(lexer).type, TOKEN_GREATER_THAN_OR_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_LESS_THAN_OR_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_EQUAL);
    ASSERT_EQ(next_token(lexer).type, TOKEN_NOT_EQUAL);
}

static void test_arrow_vs_minus(void) {
    /* -> should be ARROW, - > should be MINUS GT */
    Lexer *lexer = create_test_lexer("->");
    ASSERT_EQ(next_token(lexer).type, TOKEN_ARROW);
}

static void test_string_with_interpolation_marker(void) {
    /* String containing ${...} is still a string token */
    Lexer *lexer = create_test_lexer("\"hello ${name} world\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_STRING);
}

static void test_multiline_block_comment(void) {
    Lexer *lexer = create_test_lexer("a /* this is\na multiline\ncomment */ b");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(token1.literal, "a");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(token2.literal, "b");
}

static void test_empty_string(void) {
    Lexer *lexer = create_test_lexer("\"\"");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_STRING);
    ASSERT_STR_EQ(token.literal, "");
}

static void test_multiple_strings(void) {
    Lexer *lexer = create_test_lexer("\"hello\" \"world\"");
    Token token1 = next_token(lexer);
    ASSERT_EQ(token1.type, TOKEN_STRING);
    ASSERT_STR_EQ(token1.literal, "hello");
    Token token2 = next_token(lexer);
    ASSERT_EQ(token2.type, TOKEN_STRING);
    ASSERT_STR_EQ(token2.literal, "world");
}

static void test_consecutive_operators(void) {
    /* ++ -- should not be confused */
    Lexer *lexer = create_test_lexer("++ --");
    ASSERT_EQ(next_token(lexer).type, TOKEN_INCREMENT);
    ASSERT_EQ(next_token(lexer).type, TOKEN_DECREMENT);
}

static void test_large_integer_literal(void) {
    Lexer *lexer = create_test_lexer("9999999999");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "9999999999");
}

static void test_char_single_quote(void) {
    Lexer *lexer = create_test_lexer("'x'");
    Token token = next_token(lexer);
    ASSERT_EQ(token.type, TOKEN_CHAR);
    ASSERT_STR_EQ(token.literal, "x");
}

static void test_keyword_panic(void) {
    Lexer *lexer = create_test_lexer("panic");
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER); /* panic is a builtin func, not keyword */
}

static void test_mixed_tokens_line(void) {
    /* Realistic code line */
    Lexer *lexer = create_test_lexer("mut x i64 = 42");
    ASSERT_EQ(next_token(lexer).type, TOKEN_MUT);      /* mut */
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);     /* x */
    ASSERT_EQ(next_token(lexer).type, TOKEN_IDENTIFIER);     /* int */
    ASSERT_EQ(next_token(lexer).type, TOKEN_ASSIGN);    /* = */
    ASSERT_EQ(next_token(lexer).type, TOKEN_INTEGER_LITERAL);       /* 42 */
    ASSERT_EQ(next_token(lexer).type, TOKEN_END_OF_FILE);
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
    ASSERT_EQ(token.type, TOKEN_INTEGER_LITERAL);
    ASSERT_STR_EQ(token.literal, "0xABCD");
}

/* ===== token names ===== */

static void test_token_type_name_of_every_token(void) {
    static const struct { TokenType type; const char *name; } expected[] = {
        {TOKEN_ILLEGAL, "ILLEGAL"},
        {TOKEN_END_OF_FILE, "EOF"},
        {TOKEN_IDENTIFIER, "IDENT"},
        {TOKEN_INTEGER_LITERAL, "INT"},
        {TOKEN_FLOATING_POINT_LITERAL, "FLOAT"},
        {TOKEN_STRING, "STRING"},
        {TOKEN_RAW_STRING, "RAW_STRING"},
        {TOKEN_CHAR, "CHAR"},
        {TOKEN_ASSIGN, "="},
        {TOKEN_PLUS, "+"},
        {TOKEN_MINUS, "-"},
        {TOKEN_BANG, "!"},
        {TOKEN_ASTERISK, "*"},
        {TOKEN_SLASH, "/"},
        {TOKEN_PERCENT, "%"},
        {TOKEN_LESS_THAN, "<"},
        {TOKEN_GREATER_THAN, ">"},
        {TOKEN_EQUAL, "=="},
        {TOKEN_NOT_EQUAL, "!="},
        {TOKEN_LESS_THAN_OR_EQUAL, "<="},
        {TOKEN_GREATER_THAN_OR_EQUAL, ">="},
        {TOKEN_PLUS_ASSIGN, "+="},
        {TOKEN_MINUS_ASSIGN, "-="},
        {TOKEN_ASTERISK_ASSIGN, "*="},
        {TOKEN_SLASH_ASSIGN, "/="},
        {TOKEN_PERCENT_ASSIGN, "%="},
        {TOKEN_INCREMENT, "++"},
        {TOKEN_DECREMENT, "--"},
        {TOKEN_AND, "&&"},
        {TOKEN_OR, "||"},
        {TOKEN_COMMA, ","},
        {TOKEN_COLON, ":"},
        {TOKEN_SEMICOLON, ";"},
        {TOKEN_NEWLINE, "NEWLINE"},
        {TOKEN_LEFT_PARENTHESIS, "("},
        {TOKEN_RIGHT_PARENTHESIS, ")"},
        {TOKEN_LEFT_BRACE, "{"},
        {TOKEN_RIGHT_BRACE, "}"},
        {TOKEN_LEFT_BRACKET, "["},
        {TOKEN_RIGHT_BRACKET, "]"},
        {TOKEN_ARROW, "->"},
        {TOKEN_DOT, "."},
        {TOKEN_AT, "@"},
        {TOKEN_CARET, "^"},
        {TOKEN_AMPERSAND, "&"},
        {TOKEN_HASH_LEFT_BRACKET, "#["},
        {TOKEN_STRICT, "#strict"},
        {TOKEN_FLAGS, "#flags"},
        {TOKEN_DOC, "#doc"},
        {TOKEN_JSON_ATTRIBUTE, "#json"},
        {TOKEN_DISCARD, "#discard"},
        {TOKEN_DEPRECATED, "#deprecated"},
        {TOKEN_TEST, "#test"},
        {TOKEN_ERROR_CODE_ATTRIBUTE, "#error_code"},
        {TOKEN_DEFINE, "#define"},
        {TOKEN_MUT, "mut"},
        {TOKEN_CONST, "const"},
        {TOKEN_DO, "do"},
        {TOKEN_RETURN, "return"},
        {TOKEN_IF, "if"},
        {TOKEN_OR_KEYWORD, "or"},
        {TOKEN_OTHERWISE, "otherwise"},
        {TOKEN_FOR, "for"},
        {TOKEN_FOR_EACH, "for_each"},
        {TOKEN_AS_LONG_AS, "as_long_as"},
        {TOKEN_LOOP, "loop"},
        {TOKEN_BREAK, "break"},
        {TOKEN_CONTINUE, "continue"},
        {TOKEN_IN, "in"},
        {TOKEN_NOT_IN, "not_in"},
        {TOKEN_RANGE, "range"},
        {TOKEN_IMPORT, "import"},
        {TOKEN_USING, "using"},
        {TOKEN_STRUCT, "struct"},
        {TOKEN_ENUM, "enum"},
        {TOKEN_NIL, "nil"},
        {TOKEN_NEW, "new"},
        {TOKEN_TRUE, "true"},
        {TOKEN_FALSE, "false"},
        {TOKEN_BLANK, "_"},
        {TOKEN_ENSURE, "ensure"},
        {TOKEN_OR_RETURN, "or_return"},
        {TOKEN_OR_DEFAULT, "or_default"},
        {TOKEN_OR_PANIC, "or_panic"},
        {TOKEN_EXTERN, "extern"},
        {TOKEN_GENERIC, "generic"},
        {TOKEN_PRIVATE, "private"},
        {TOKEN_UNSAFE, "unsafe"},
        {TOKEN_USE, "use"},
        {TOKEN_WHEN, "when"},
        {TOKEN_IS, "is"},
        {TOKEN_DEFAULT, "default"},
        {TOKEN_ALIAS, "alias"},
        {TOKEN_DISTINCT, "distinct"},
        {TOKEN_CAST, "cast"},
        {TOKEN_BIT_AND, "bit_and"},
        {TOKEN_BIT_OR, "bit_or"},
        {TOKEN_BIT_XOR, "bit_xor"},
        {TOKEN_BIT_NOT, "bit_not"},
        {TOKEN_BIT_SHIFT_LEFT, "bit_shift_left"},
        {TOKEN_BIT_SHIFT_RIGHT, "bit_shift_right"},
        {TOKEN_COUNT, "COUNT"},
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        const char *actual = token_type_name(expected[i].type);
        ASSERT_NOT_NULL(actual);
        if (strcmp(actual, expected[i].name) != 0) {
            fprintf(stderr, "  token %d: \"%s\" != \"%s\"\n", (int)expected[i].type, actual, expected[i].name);
            ASSERT(false);
        }
    }
}

static void test_token_type_name_is_unique_per_token(void) {
    static const TokenType types[] = {
TOKEN_ILLEGAL, TOKEN_END_OF_FILE, TOKEN_IDENTIFIER, TOKEN_INTEGER_LITERAL, TOKEN_FLOATING_POINT_LITERAL, TOKEN_STRING, TOKEN_RAW_STRING, TOKEN_CHAR, TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS, TOKEN_BANG, TOKEN_ASTERISK, TOKEN_SLASH, TOKEN_PERCENT, TOKEN_LESS_THAN, TOKEN_GREATER_THAN, TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS_THAN_OR_EQUAL, TOKEN_GREATER_THAN_OR_EQUAL, TOKEN_PLUS_ASSIGN, TOKEN_MINUS_ASSIGN, TOKEN_ASTERISK_ASSIGN, TOKEN_SLASH_ASSIGN, TOKEN_PERCENT_ASSIGN, TOKEN_INCREMENT, TOKEN_DECREMENT, TOKEN_AND, TOKEN_OR, TOKEN_COMMA, TOKEN_COLON, TOKEN_SEMICOLON, TOKEN_NEWLINE, TOKEN_LEFT_PARENTHESIS, TOKEN_RIGHT_PARENTHESIS, TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE, TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET, TOKEN_ARROW, TOKEN_DOT, TOKEN_AT, TOKEN_CARET, TOKEN_AMPERSAND, TOKEN_HASH_LEFT_BRACKET, TOKEN_STRICT, TOKEN_FLAGS, TOKEN_DOC, TOKEN_JSON_ATTRIBUTE, TOKEN_DISCARD, TOKEN_DEPRECATED, TOKEN_TEST, TOKEN_ERROR_CODE_ATTRIBUTE, TOKEN_DEFINE, TOKEN_MUT, TOKEN_CONST, TOKEN_DO, TOKEN_RETURN, TOKEN_IF, TOKEN_OR_KEYWORD, TOKEN_OTHERWISE, TOKEN_FOR, TOKEN_FOR_EACH, TOKEN_AS_LONG_AS, TOKEN_LOOP, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_IN, TOKEN_NOT_IN, TOKEN_RANGE, TOKEN_IMPORT, TOKEN_USING, TOKEN_STRUCT, TOKEN_ENUM, TOKEN_NIL, TOKEN_NEW, TOKEN_TRUE, TOKEN_FALSE, TOKEN_BLANK, TOKEN_ENSURE, TOKEN_OR_RETURN, TOKEN_OR_DEFAULT, TOKEN_OR_PANIC, TOKEN_EXTERN, TOKEN_GENERIC, TOKEN_PRIVATE, TOKEN_UNSAFE, TOKEN_USE, TOKEN_WHEN, TOKEN_IS, TOKEN_DEFAULT, TOKEN_ALIAS, TOKEN_DISTINCT, TOKEN_CAST, TOKEN_BIT_AND, TOKEN_BIT_OR, TOKEN_BIT_XOR, TOKEN_BIT_NOT, TOKEN_BIT_SHIFT_LEFT, TOKEN_BIT_SHIFT_RIGHT, TOKEN_COUNT
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        for (size_t j = i + 1; j < sizeof(types) / sizeof(types[0]); j++)
            ASSERT(strcmp(token_type_name(types[i]), token_type_name(types[j])) != 0);
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
    RUN_TEST(test_or_panic_keyword);
    RUN_TEST(test_or_default_keyword);
    RUN_TEST(test_error_E1025_question_mark);
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
    RUN_TEST(test_error_E1018_empty_char);
    RUN_TEST(test_error_E1018_multichar);
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

    printf("--- token names ---\n");
    RUN_TEST(test_token_type_name_of_every_token);
    RUN_TEST(test_token_type_name_is_unique_per_token);

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
