/*
 * test_parser.c — Unit tests for the grayc parser, verifying AST
 * construction from declarations, expressions, and control flow.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include "../src/util/arena.h"
#include "../src/util/error.h"
#include "../src/lexer/lexer.h"
#include "../src/parser/parser.h"

static Arena *arena;
static DiagnosticList *diagnostics;

static AstNode *parse_test_input(const char *input) {
    diagnostics = diagnostic_create();
    diagnostics->use_color = false;
    Lexer *lexer = lexer_create(arena, input, "test.gray");
    Parser *parser = parser_create(arena, lexer, "test.gray", diagnostics);
    return parser_parse_program(parser);
}

static AstNode *first_statement(AstNode *program) {
    if (!program || program->kind != NODE_PROGRAM) return NULL;
    if (program->data.program.stmt_count == 0) return NULL;
    return program->data.program.stmts[0];
}

static void test_parse_variable_declaration(void) {
    AstNode *program = parse_test_input("mut x int = 42");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT_STR_EQ(statement->data.var_decl.name, "x");
    ASSERT_STR_EQ(statement->data.var_decl.type_name, "int");
    ASSERT(statement->data.var_decl.mutable);
    ASSERT_NOT_NULL(statement->data.var_decl.value);
    ASSERT_EQ(statement->data.var_decl.value->kind, NODE_INT_VALUE);
}

static void test_parse_constant_declaration(void) {
    AstNode *program = parse_test_input("const PI float = 3.14");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT(!statement->data.var_decl.mutable);
    ASSERT_STR_EQ(statement->data.var_decl.name, "PI");
}

static void test_parse_function_declaration(void) {
    AstNode *program = parse_test_input("do add(a int, b int) -> int { return a + b }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
    ASSERT_STR_EQ(statement->data.func_decl.name, "add");
    ASSERT_EQ(statement->data.func_decl.param_count, 2);
    ASSERT_EQ(statement->data.func_decl.return_type_count, 1);
    ASSERT_STR_EQ(statement->data.func_decl.return_types[0], "int");
}

static void test_parse_function_no_return(void) {
    AstNode *program = parse_test_input("do greet() { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
    ASSERT_STR_EQ(statement->data.func_decl.name, "greet");
    ASSERT_EQ(statement->data.func_decl.param_count, 0);
    ASSERT_EQ(statement->data.func_decl.return_type_count, 0);
}

static void test_parse_import_single(void) {
    AstNode *program = parse_test_input("import @math");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_IMPORT_STMT);
    ASSERT_EQ(statement->data.import_stmt.count, 1);
    ASSERT(statement->data.import_stmt.items[0].is_stdlib);
    ASSERT_STR_EQ(statement->data.import_stmt.items[0].module, "math");
}

static void test_parse_import_multi(void) {
    AstNode *program = parse_test_input("import @math, @strings");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_IMPORT_STMT);
    ASSERT_EQ(statement->data.import_stmt.count, 2);
    ASSERT_STR_EQ(statement->data.import_stmt.items[0].module, "math");
    ASSERT_STR_EQ(statement->data.import_stmt.items[1].module, "strings");
}

static void test_parse_if_or_otherwise(void) {
    AstNode *program = parse_test_input("if x > 0 { } or x == 0 { } otherwise { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_IF_STMT);
    ASSERT_NOT_NULL(statement->data.if_stmt.consequence);
    ASSERT_NOT_NULL(statement->data.if_stmt.alternative);
    ASSERT_EQ(statement->data.if_stmt.alternative->kind, NODE_IF_STMT);
}

static void test_parse_for_range(void) {
    AstNode *program = parse_test_input("for i in range(0, 10) { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FOR_STMT);
    ASSERT_STR_EQ(statement->data.for_stmt.var_name, "i");
    ASSERT_NOT_NULL(statement->data.for_stmt.iterable);
    ASSERT_EQ(statement->data.for_stmt.iterable->kind, NODE_RANGE_EXPR);
}

static void test_parse_while(void) {
    AstNode *program = parse_test_input("as_long_as x < 10 { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_WHILE_STMT);
}

static void test_parse_loop(void) {
    AstNode *program = parse_test_input("loop { break }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_LOOP_STMT);
}

static void test_parse_struct_declaration(void) {
    AstNode *program = parse_test_input("const Person struct { name string\n age int }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_STRUCT_DECL);
    ASSERT_STR_EQ(statement->data.struct_decl.name, "Person");
    ASSERT_EQ(statement->data.struct_decl.field_count, 2);
}

static void test_parse_enum_declaration(void) {
    AstNode *program = parse_test_input("const Color enum { RED\n GREEN\n BLUE }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ENUM_DECL);
    ASSERT_STR_EQ(statement->data.enum_decl.name, "Color");
    ASSERT_EQ(statement->data.enum_decl.value_count, 3);
}

static void test_parse_struct_literal(void) {
    AstNode *program = parse_test_input("mut p = Person{name: \"Alice\", age: 30}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT_NOT_NULL(statement->data.var_decl.value);
    ASSERT_EQ(statement->data.var_decl.value->kind, NODE_STRUCT_VALUE);
    ASSERT_EQ(statement->data.var_decl.value->data.struct_value.count, 2);
}

static void test_parse_array_literal(void) {
    AstNode *program = parse_test_input("mut nums [int] = {1, 2, 3}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT_STR_EQ(statement->data.var_decl.type_name, "[int]");
    ASSERT_NOT_NULL(statement->data.var_decl.value);
    ASSERT_EQ(statement->data.var_decl.value->kind, NODE_ARRAY_VALUE);
    ASSERT_EQ(statement->data.var_decl.value->data.array_value.count, 3);
}

static void test_parse_ensure(void) {
    AstNode *program = parse_test_input("ensure cleanup()");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ENSURE_STMT);
}

static void test_parse_when(void) {
    AstNode *program = parse_test_input("when x { is 1 { } is 2 { } default { } }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_WHEN_STMT);
    ASSERT_EQ(statement->data.when_stmt.case_count, 2);
    ASSERT_NOT_NULL(statement->data.when_stmt.default_body);
}

static void test_parse_default_params(void) {
    AstNode *program = parse_test_input("do greet(name string = \"World\") { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
    ASSERT_EQ(statement->data.func_decl.param_count, 1);
    ASSERT_NOT_NULL(statement->data.func_decl.params[0].default_value);
}

static void test_parse_hex_int(void) {
    AstNode *program = parse_test_input("mut x int = 0xFF");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->data.var_decl.value->kind, NODE_INT_VALUE);
    ASSERT_EQ(statement->data.var_decl.value->data.int_value.value, 255);
}

static void test_parse_octal_int(void) {
    AstNode *program = parse_test_input("mut x int = 0o10");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->data.var_decl.value->data.int_value.value, 8);
}

static void test_parse_binary_int(void) {
    AstNode *program = parse_test_input("mut x int = 0b1010");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->data.var_decl.value->data.int_value.value, 10);
}

static void test_parse_mut_keyword(void) {
    AstNode *program = parse_test_input("mut x int = 42");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT(statement->data.var_decl.mutable);
}

static void test_parse_array_return_type(void) {
    AstNode *program = parse_test_input("do get() -> [int] { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
    ASSERT_EQ(statement->data.func_decl.return_type_count, 1);
    ASSERT_STR_EQ(statement->data.func_decl.return_types[0], "[int]");
}

static void test_parse_error_reports(void) {
    AstNode *program = parse_test_input("mut x int =");
    (void)program;
    ASSERT(diagnostic_has_errors(diagnostics));
}

static void test_parse_function_reference(void) {
    AstNode *program = parse_test_input("do main() { mut f = ()double }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
}

static void test_parse_struct_function(void) {
    AstNode *program = parse_test_input(
        "const Point struct {\n"
        "  x int\n"
        "  do create(x int) -> Point { return Point{x: x} }\n"
        "}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_STRUCT_DECL);
    ASSERT_EQ(statement->data.struct_decl.field_count, 1);
    ASSERT_EQ(statement->data.struct_decl.func_count, 1);
}

static void test_parse_or_return(void) {
    AstNode *program = parse_test_input(
        "do wrapper() -> (string, Error) {\n"
        "  mut val = fallible() or_return\n"
        "  return val, nil\n"
        "}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
}

static void test_parse_flags_enum(void) {
    AstNode *program = parse_test_input(
        "#flags\n"
        "const Perms enum { READ\n WRITE\n EXEC }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ENUM_DECL);
    ASSERT(statement->data.enum_decl.is_flags);
}

static void test_parse_string_enum(void) {
    AstNode *program = parse_test_input(
        "const Status enum {\n"
        "  TODO = \"todo\"\n"
        "  DONE = \"done\"\n"
        "}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ENUM_DECL);
    ASSERT_EQ(statement->data.enum_decl.value_count, 2);
}

static void test_parse_map_type(void) {
    AstNode *program = parse_test_input("mut m map[string:int] = {:}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT(strstr(statement->data.var_decl.type_name, "map") != NULL);
}

static void test_parse_fixed_array(void) {
    AstNode *program = parse_test_input("const arr [int, 3] = {1, 2, 3}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT(strstr(statement->data.var_decl.type_name, "int,3") != NULL);
}

static void test_parse_nested_array(void) {
    AstNode *program = parse_test_input("mut m [[int]] = {{1}, {2}}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_VAR_DECL);
    ASSERT(strstr(statement->data.var_decl.type_name, "[[int]]") != NULL);
}

static void test_parse_private_struct_function(void) {
    /* Private functions inside structs */
    AstNode *program = parse_test_input(
        "const Foo struct {\n"
        "  private do secret() -> int { return 42 }\n"
        "}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_STRUCT_DECL);
    ASSERT_EQ(statement->data.struct_decl.func_count, 1);
}

static void test_parse_for_each_index(void) {
    AstNode *program = parse_test_input(
        "do main() {\n"
        "  for_each i, item in arr { }\n"
        "}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FUNC_DECL);
}

/* Helper: get the first statement inside main()'s body */
static AstNode *body_statement(AstNode *program, int index) {
    AstNode *function = first_statement(program);
    if (!function || function->kind != NODE_FUNC_DECL) return NULL;
    AstNode *body = function->data.func_decl.body;
    if (!body || body->kind != NODE_BLOCK_STMT) return NULL;
    if (index >= body->data.block.count) return NULL;
    return body->data.block.stmts[index];
}

/* Helper: get the value expression from first var decl in main */
static AstNode *variable_value(AstNode *program) {
    AstNode *statement = body_statement(program, 0);
    if (!statement || statement->kind != NODE_VAR_DECL) return NULL;
    return statement->data.var_decl.value;
}

/* --- Expression AST Tests --- */

static void test_parse_infix_expression(void) {
    AstNode *program = parse_test_input("do main() { mut x = 1 + 2 * 3 }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_INFIX_EXPR);
    /* 1 + (2 * 3) — left is 1, right is 2*3 */
    ASSERT_EQ(value->data.infix.op, TOK_PLUS);
    ASSERT_EQ(value->data.infix.left->kind, NODE_INT_VALUE);
    ASSERT_EQ(value->data.infix.right->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.right->data.infix.op, TOK_ASTERISK);
}

static void test_parse_prefix_expression(void) {
    AstNode *program = parse_test_input("do main() { mut x = -42 }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_PREFIX_EXPR);
    ASSERT_EQ(value->data.prefix.op, TOK_MINUS);
    ASSERT_EQ(value->data.prefix.right->kind, NODE_INT_VALUE);
}

static void test_parse_not_expression(void) {
    AstNode *program = parse_test_input("do main() { mut x = !true }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_PREFIX_EXPR);
    ASSERT_EQ(value->data.prefix.op, TOK_BANG);
    ASSERT_EQ(value->data.prefix.right->kind, NODE_BOOL_VALUE);
}

static void test_parse_postfix_expression(void) {
    AstNode *program = parse_test_input("do main() { mut x int = 0\n x++ }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_EXPR_STMT);
    ASSERT_EQ(statement->data.expr_stmt.expr->kind, NODE_POSTFIX_EXPR);
    ASSERT_EQ(statement->data.expr_stmt.expr->data.postfix.op, TOK_INCREMENT);
}

static void test_parse_index_expression(void) {
    AstNode *program = parse_test_input("do main() { mut a [int] = {1,2,3}\n mut x = a[1] }");
    AstNode *value = body_statement(program, 1);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_VAR_DECL);
    ASSERT_EQ(value->data.var_decl.value->kind, NODE_INDEX_EXPR);
}

static void test_parse_member_expression(void) {
    AstNode *program = parse_test_input(
        "const P struct { x int }\n"
        "do main() { mut p P = P{x: 1}\n mut v = p.x }");
    /* main is stmt[1] (after struct) */
    AstNode *function = program->data.program.stmts[1];
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
    AstNode *var_declaration = function->data.func_decl.body->data.block.stmts[1];
    ASSERT_EQ(var_declaration->data.var_decl.value->kind, NODE_MEMBER_EXPR);
}

static void test_parse_call_expression(void) {
    AstNode *program = parse_test_input("do foo() -> int { return 1 }\n do main() { mut x = foo() }");
    AstNode *function = program->data.program.stmts[1];
    AstNode *var_declaration = function->data.func_decl.body->data.block.stmts[0];
    ASSERT_NOT_NULL(var_declaration);
    ASSERT_EQ(var_declaration->data.var_decl.value->kind, NODE_CALL_EXPR);
}

static void test_parse_cast_expression(void) {
    AstNode *program = parse_test_input("do main() { mut x = cast(42, u8) }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_CAST_EXPR);
    ASSERT_STR_EQ(value->data.cast.target_type, "u8");
}

static void test_parse_new_expression(void) {
    AstNode *program = parse_test_input(
        "const Foo struct { x int }\n"
        "do main() { mut p = new(Foo) }");
    AstNode *function = program->data.program.stmts[1];
    AstNode *var_declaration = function->data.func_decl.body->data.block.stmts[0];
    ASSERT_NOT_NULL(var_declaration);
    ASSERT_EQ(var_declaration->data.var_decl.value->kind, NODE_NEW_EXPR);
    ASSERT_STR_EQ(var_declaration->data.var_decl.value->data.new_expr.type_name, "Foo");
}

static void test_parse_comparison_operators(void) {
    AstNode *program = parse_test_input("do main() { mut x = 1 < 2 }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.op, TOK_LT);
}

static void test_parse_logical_and_or(void) {
    AstNode *program = parse_test_input("do main() { mut x = true && false || true }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_INFIX_EXPR);
    /* || has lower precedence than && */
    ASSERT_EQ(value->data.infix.op, TOK_OR);
}

/* --- Literal AST Tests --- */

static void test_parse_bool_literal(void) {
    AstNode *program = parse_test_input("do main() { mut x = true }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_BOOL_VALUE);
}

static void test_parse_float_literal(void) {
    AstNode *program = parse_test_input("do main() { mut x = 3.14 }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_FLOAT_VALUE);
}

static void test_parse_string_literal(void) {
    AstNode *program = parse_test_input("do main() { mut x = \"hello\" }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_STRING_VALUE);
}

static void test_parse_char_literal(void) {
    AstNode *program = parse_test_input("do main() { mut x = 'A' }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_CHAR_VALUE);
}

static void test_parse_nil_literal(void) {
    AstNode *program = parse_test_input("do main() { mut x = nil }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_NIL_VALUE);
}

/* --- Statement AST Tests --- */

static void test_parse_assign_statement(void) {
    AstNode *program = parse_test_input("do main() { mut x int = 1\n x = 2 }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ASSIGN_STMT);
}

static void test_parse_compound_assign(void) {
    AstNode *program = parse_test_input("do main() { mut x int = 1\n x += 5 }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ASSIGN_STMT);
}

static void test_parse_break_continue(void) {
    AstNode *program = parse_test_input("do main() { loop { break } }");
    AstNode *loop = body_statement(program, 0);
    ASSERT_NOT_NULL(loop);
    ASSERT_EQ(loop->kind, NODE_LOOP_STMT);
    AstNode *break_statement = loop->data.loop_stmt.body->data.block.stmts[0];
    ASSERT_EQ(break_statement->kind, NODE_BREAK_STMT);
}

static void test_parse_continue_statement(void) {
    AstNode *program = parse_test_input("do main() { loop { continue } }");
    AstNode *loop = body_statement(program, 0);
    AstNode *continue_statement = loop->data.loop_stmt.body->data.block.stmts[0];
    ASSERT_EQ(continue_statement->kind, NODE_CONTINUE_STMT);
}

static void test_parse_map_literal(void) {
    AstNode *program = parse_test_input("do main() { mut m = {\"a\": 1, \"b\": 2} }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_MAP_VALUE);
}

static void test_parse_multi_return(void) {
    AstNode *program = parse_test_input("do pair() -> (int, int) { return 1, 2 }");
    AstNode *function = first_statement(program);
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
    ASSERT_EQ(function->data.func_decl.return_type_count, 2);
}

static void test_parse_using_statement(void) {
    AstNode *program = parse_test_input("import @math\n using math");
    /* using should be stmt[1] */
    ASSERT(program->data.program.stmt_count >= 2);
    ASSERT_EQ(program->data.program.stmts[1]->kind, NODE_USING_STMT);
}

static void test_parse_blank_identifier(void) {
    AstNode *program = parse_test_input("do pair() -> (int, int) { return 1, 2 }\n do main() { mut _, b = pair() }");
    AstNode *function = program->data.program.stmts[1];
    /* Just check it parses without crashing */
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
}

/* --- P3: Remaining parser coverage gaps --- */

static void test_parse_range_with_step(void) {
    AstNode *program = parse_test_input("for i in range(0, 10, 2) { }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FOR_STMT);
    AstNode *range = statement->data.for_stmt.iterable;
    ASSERT_EQ(range->kind, NODE_RANGE_EXPR);
    ASSERT_NOT_NULL(range->data.range_expr.start);
    ASSERT_NOT_NULL(range->data.range_expr.end);
    ASSERT_NOT_NULL(range->data.range_expr.step);
    ASSERT_EQ(range->data.range_expr.step->kind, NODE_INT_VALUE);
    ASSERT_EQ(range->data.range_expr.step->data.int_value.value, 2);
}

static void test_parse_pointer_deref(void) {
    AstNode *program = parse_test_input(
        "const Node struct { val int }\n"
        "do main() { mut p = new(Node)\n mut v = p^.val }");
    AstNode *function = program->data.program.stmts[1];
    AstNode *var_declaration = function->data.func_decl.body->data.block.stmts[1];
    ASSERT_NOT_NULL(var_declaration);
    /* p^.val — should parse as member access on a deref */
    ASSERT_EQ(var_declaration->data.var_decl.value->kind, NODE_MEMBER_EXPR);
}

/* module keyword removed in v3.0 — test removed */

static void test_parse_local_import(void) {
    AstNode *program = parse_test_input("import \"./mylib\"");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_IMPORT_STMT);
    ASSERT(!statement->data.import_stmt.items[0].is_stdlib);
    ASSERT_STR_EQ(statement->data.import_stmt.items[0].path, "./mylib");
}


static void test_parse_precedence_add_mul(void) {
    /* 1 + 2 * 3 should parse as 1 + (2 * 3) */
    AstNode *program = parse_test_input("do main() { mut x = 1 + 2 * 3 }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.op, TOK_PLUS);
    ASSERT_EQ(value->data.infix.left->kind, NODE_INT_VALUE);
    ASSERT_EQ(value->data.infix.left->data.int_value.value, 1);
    ASSERT_EQ(value->data.infix.right->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.right->data.infix.op, TOK_ASTERISK);
}

static void test_parse_precedence_comparison_logical(void) {
    /* a > 0 && b < 10 should parse as (a > 0) && (b < 10) */
    AstNode *program = parse_test_input("do main() { mut x = 1 > 0 && 2 < 10 }");
    AstNode *value = variable_value(program);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.op, TOK_AND);
    ASSERT_EQ(value->data.infix.left->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.left->data.infix.op, TOK_GT);
    ASSERT_EQ(value->data.infix.right->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.infix.right->data.infix.op, TOK_LT);
}

static void test_parse_named_return(void) {
    AstNode *program = parse_test_input("do foo() -> (name string, age int) { }");
    AstNode *function = first_statement(program);
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
    ASSERT_EQ(function->data.func_decl.return_type_count, 2);
    ASSERT_STR_EQ(function->data.func_decl.return_names[0], "name");
    ASSERT_STR_EQ(function->data.func_decl.return_names[1], "age");
}

static void test_parse_error_bad_var_decl(void) {
    AstNode *program = parse_test_input("mut = 42");
    (void)program;
    ASSERT(diagnostic_has_errors(diagnostics));
}

static void test_parse_error_unexpected_token(void) {
    AstNode *program = parse_test_input("do 42() { }");
    (void)program;
    ASSERT(diagnostic_has_errors(diagnostics));
}

/* --- Additional parser coverage --- */

static void test_parse_for_each_statement(void) {
    AstNode *program = parse_test_input("do main() { mut arr [int] = {1,2,3}\n for_each x in arr { } }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_FOR_EACH_STMT);
}

static void test_parse_while_alias(void) {
    /* while is an alias for as_long_as */
    AstNode *program = parse_test_input("while true { break }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_WHILE_STMT);
}

static void test_parse_empty_block(void) {
    AstNode *program = parse_test_input("do main() { }");
    AstNode *function = first_statement(program);
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
    ASSERT_NOT_NULL(function->data.func_decl.body);
    ASSERT_EQ(function->data.func_decl.body->kind, NODE_BLOCK_STMT);
    ASSERT_EQ(function->data.func_decl.body->data.block.count, 0);
}

static void test_parse_grouped_params(void) {
    AstNode *program = parse_test_input("do add(a, b int) -> int { return a + b }");
    AstNode *function = first_statement(program);
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
    ASSERT_EQ(function->data.func_decl.param_count, 2);
    ASSERT_STR_EQ(function->data.func_decl.params[0].type_name, "int");
    ASSERT_STR_EQ(function->data.func_decl.params[1].type_name, "int");
}

static void test_parse_compound_assign_mul(void) {
    AstNode *program = parse_test_input("do main() { mut x int = 2\n x *= 5 }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ASSIGN_STMT);
}

static void test_parse_compound_assign_div(void) {
    AstNode *program = parse_test_input("do main() { mut x int = 10\n x /= 2 }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ASSIGN_STMT);
}

static void test_parse_compound_assign_mod(void) {
    AstNode *program = parse_test_input("do main() { mut x int = 10\n x %= 3 }");
    AstNode *statement = body_statement(program, 1);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ASSIGN_STMT);
}

static void test_parse_multiple_when_cases(void) {
    AstNode *program = parse_test_input(
        "when x { is 1 { } is 2 { } is 3 { } is 4 { } default { } }");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_WHEN_STMT);
    ASSERT_EQ(statement->data.when_stmt.case_count, 4);
}

static void test_parse_nested_if(void) {
    AstNode *program = parse_test_input(
        "do main() {\n"
        "  if true {\n"
        "    if false { }\n"
        "  }\n"
        "}");
    AstNode *outer_if = body_statement(program, 0);
    ASSERT_NOT_NULL(outer_if);
    ASSERT_EQ(outer_if->kind, NODE_IF_STMT);
    /* The inner if is inside the consequence block */
    AstNode *inner = outer_if->data.if_stmt.consequence->data.block.stmts[0];
    ASSERT_EQ(inner->kind, NODE_IF_STMT);
}

static void test_parse_struct_nested_field(void) {
    AstNode *program = parse_test_input(
        "const Inner struct { val int }\n"
        "const Outer struct { inner Inner }");
    ASSERT_EQ(program->data.program.stmt_count, 2);
    AstNode *outer = program->data.program.stmts[1];
    ASSERT_EQ(outer->kind, NODE_STRUCT_DECL);
    ASSERT_STR_EQ(outer->data.struct_decl.fields[0].type_name, "Inner");
}

static void test_parse_in_operator(void) {
    AstNode *program = parse_test_input("do main() { mut arr [int] = {1,2,3}\n mut x = 1 in arr }");
    AstNode *value = body_statement(program, 1);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->kind, NODE_VAR_DECL);
    ASSERT_EQ(value->data.var_decl.value->kind, NODE_INFIX_EXPR);
    ASSERT_EQ(value->data.var_decl.value->data.infix.op, TOK_IN);
}


static void test_parse_return_multiple_values(void) {
    AstNode *program = parse_test_input(
        "do pair() -> (int, string) {\n"
        "  return 42, \"hello\"\n"
        "}");
    AstNode *function = first_statement(program);
    ASSERT_NOT_NULL(function);
    ASSERT_EQ(function->kind, NODE_FUNC_DECL);
    ASSERT_EQ(function->data.func_decl.return_type_count, 2);
    ASSERT_STR_EQ(function->data.func_decl.return_types[0], "int");
    ASSERT_STR_EQ(function->data.func_decl.return_types[1], "string");
}

static void test_parse_import_alias(void) {
    /* Grayscale alias syntax: import alias @module */
    AstNode *program = parse_test_input("import m @math");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_IMPORT_STMT);
    ASSERT_STR_EQ(statement->data.import_stmt.items[0].module, "math");
    ASSERT_STR_EQ(statement->data.import_stmt.items[0].alias, "m");
}

static void test_parse_enum_with_values(void) {
    AstNode *program = parse_test_input(
        "const Priority enum {\n"
        "  LOW = 1\n"
        "  MED = 2\n"
        "  HIGH = 3\n"
        "}");
    AstNode *statement = first_statement(program);
    ASSERT_NOT_NULL(statement);
    ASSERT_EQ(statement->kind, NODE_ENUM_DECL);
    ASSERT_EQ(statement->data.enum_decl.value_count, 3);
}

/* Helper: check if a specific error code was emitted */
static bool parser_has_code(DiagnosticList *diagnostics, const char *code) {
    for (int i = 0; i < diagnostics->count; i++) {
        if (diagnostics->items[i].code && strcmp(diagnostics->items[i].code, code) == 0) return true;
    }
    return false;
}

/* Helper: find the message of the first diagnostic with a given code */
static const char *parser_message_for(DiagnosticList *diagnostics, const char *code) {
    for (int i = 0; i < diagnostics->count; i++) {
        if (diagnostics->items[i].code && strcmp(diagnostics->items[i].code, code) == 0)
            return diagnostics->items[i].message;
    }
    return NULL;
}

/* ===== Parser error code tests ===== */

static void test_parse_error_E2025_non_int_array_size(void) {
    AstNode *program = parse_test_input("do main() { mut a [int, \"five\"] = {} }");
    (void)program;
    ASSERT(diagnostic_has_errors(diagnostics));
}

static void test_parse_error_E2059_empty_when(void) {
    AstNode *program = parse_test_input("do main() { when 1 { } }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2059"));
}

static void test_parse_error_E2060_too_many_returns(void) {
    AstNode *program = parse_test_input(
        "do bad() -> (int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int) { }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2060"));
}

/* Regression test for #2413: the bound was checked after writing to
 * names[var_count]/types[var_count], so the 17th variable wrote one element
 * past the 16-element arrays before the check could fire. Seventeen names
 * (one initial + sixteen in the comma loop) is what triggers the write. */
static void test_parse_error_E2062_too_many_multi_vars(void) {
    AstNode *program = parse_test_input(
        "do main() { mut a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q = foo() }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2062"));
}

static void test_parse_error_E2068_mut_struct(void) {
    AstNode *program = parse_test_input("mut S struct { x int }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2068"));
}

static void test_parse_error_E2069_semicolon_in_struct(void) {
    AstNode *program = parse_test_input("const S struct { x int; y int }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2069"));
}

static void test_parse_error_E2070_wildcard_in_var(void) {
    AstNode *program = parse_test_input("do main() { mut x ? = 42 }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2070"));
}

static void test_parse_error_E2071_empty_interpolation(void) {
    AstNode *program = parse_test_input("do main() { println(\"${}\") }");
    (void)program;
    ASSERT(parser_has_code(diagnostics, "E2071"));
}

/* Source truncated in expression position: the parser hits EOF outside any
 * ${...} sub-expression, so it must report the plain unexpected-token wording
 * rather than the interpolation one. */
static void test_parse_error_truncated_at_eof(void) {
    AstNode *program = parse_test_input("do main() {\n    mut x int = ");
    (void)program;
    const char *message = parser_message_for(diagnostics, "E2002");
    ASSERT_NOT_NULL(message);
    ASSERT(strstr(message, "interpolation") == NULL);
}

int main(void) {
    arena = arena_create(256 * 1024);
    printf("\n");
    RUN_TEST(test_parse_variable_declaration);
    RUN_TEST(test_parse_constant_declaration);
    RUN_TEST(test_parse_function_declaration);
    RUN_TEST(test_parse_function_no_return);
    RUN_TEST(test_parse_import_single);
    RUN_TEST(test_parse_import_multi);
    RUN_TEST(test_parse_if_or_otherwise);
    RUN_TEST(test_parse_for_range);
    RUN_TEST(test_parse_while);
    RUN_TEST(test_parse_loop);
    RUN_TEST(test_parse_struct_declaration);
    RUN_TEST(test_parse_enum_declaration);
    RUN_TEST(test_parse_struct_literal);
    RUN_TEST(test_parse_array_literal);
    RUN_TEST(test_parse_ensure);
    RUN_TEST(test_parse_when);
    RUN_TEST(test_parse_default_params);
    RUN_TEST(test_parse_hex_int);
    RUN_TEST(test_parse_octal_int);
    RUN_TEST(test_parse_binary_int);
    RUN_TEST(test_parse_mut_keyword);
    RUN_TEST(test_parse_array_return_type);
    RUN_TEST(test_parse_error_reports);
    RUN_TEST(test_parse_function_reference);
    RUN_TEST(test_parse_struct_function);
    RUN_TEST(test_parse_or_return);
    RUN_TEST(test_parse_flags_enum);
    RUN_TEST(test_parse_string_enum);
    RUN_TEST(test_parse_map_type);
    RUN_TEST(test_parse_fixed_array);
    RUN_TEST(test_parse_nested_array);
    RUN_TEST(test_parse_private_struct_function);
    RUN_TEST(test_parse_for_each_index);

    /* Expression AST tests */
    RUN_TEST(test_parse_infix_expression);
    RUN_TEST(test_parse_prefix_expression);
    RUN_TEST(test_parse_not_expression);
    RUN_TEST(test_parse_postfix_expression);
    RUN_TEST(test_parse_index_expression);
    RUN_TEST(test_parse_member_expression);
    RUN_TEST(test_parse_call_expression);
    RUN_TEST(test_parse_cast_expression);
    RUN_TEST(test_parse_new_expression);
    RUN_TEST(test_parse_comparison_operators);
    RUN_TEST(test_parse_logical_and_or);

    /* Literal AST tests */
    RUN_TEST(test_parse_bool_literal);
    RUN_TEST(test_parse_float_literal);
    RUN_TEST(test_parse_string_literal);
    RUN_TEST(test_parse_char_literal);
    RUN_TEST(test_parse_nil_literal);

    /* Statement AST tests */
    RUN_TEST(test_parse_assign_statement);
    RUN_TEST(test_parse_compound_assign);
    RUN_TEST(test_parse_break_continue);
    RUN_TEST(test_parse_continue_statement);
    RUN_TEST(test_parse_map_literal);
    RUN_TEST(test_parse_multi_return);
    RUN_TEST(test_parse_using_statement);
    RUN_TEST(test_parse_blank_identifier);

    /* P3: Remaining parser coverage */
    RUN_TEST(test_parse_range_with_step);
    RUN_TEST(test_parse_pointer_deref);
    /* test_parse_module_decl removed — module keyword removed in v3.0 */
    RUN_TEST(test_parse_local_import);
    RUN_TEST(test_parse_precedence_add_mul);
    RUN_TEST(test_parse_precedence_comparison_logical);
    RUN_TEST(test_parse_named_return);
    RUN_TEST(test_parse_error_bad_var_decl);
    RUN_TEST(test_parse_error_unexpected_token);

    /* Additional parser coverage */
    RUN_TEST(test_parse_for_each_statement);
    RUN_TEST(test_parse_while_alias);
    RUN_TEST(test_parse_empty_block);
    RUN_TEST(test_parse_grouped_params);
    RUN_TEST(test_parse_compound_assign_mul);
    RUN_TEST(test_parse_compound_assign_div);
    RUN_TEST(test_parse_compound_assign_mod);
    RUN_TEST(test_parse_multiple_when_cases);
    RUN_TEST(test_parse_nested_if);
    RUN_TEST(test_parse_struct_nested_field);
    RUN_TEST(test_parse_in_operator);
    RUN_TEST(test_parse_return_multiple_values);
    RUN_TEST(test_parse_import_alias);
    RUN_TEST(test_parse_enum_with_values);

    /* Parser error code tests */
    RUN_TEST(test_parse_error_E2025_non_int_array_size);
    RUN_TEST(test_parse_error_E2059_empty_when);
    RUN_TEST(test_parse_error_E2060_too_many_returns);
    RUN_TEST(test_parse_error_E2062_too_many_multi_vars);
    RUN_TEST(test_parse_error_E2068_mut_struct);
    RUN_TEST(test_parse_error_E2069_semicolon_in_struct);
    RUN_TEST(test_parse_error_E2070_wildcard_in_var);
    RUN_TEST(test_parse_error_E2071_empty_interpolation);
    RUN_TEST(test_parse_error_truncated_at_eof);

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
