/*
 * test_typechecker.c — Unit tests for the grayc type checker, validating
 * type inference, type errors, and expression type resolution.
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
#include "../src/typechecker/typechecker.h"

static Arena *arena;

static TypeTable *typecheck_test_input(const char *input) {
    DiagnosticList *diagnostics = diagnostic_create();
    diagnostics->use_color = false;
    Lexer *lexer =lexer_create(arena, input, "test.gray");
    Parser *parser = parser_create(arena, lexer, "test.gray", diagnostics);
    AstNode *program = parser_parse_program(parser);
    TypeChecker *checker =typechecker_create(diagnostics, "test.gray");
    typechecker_check(checker, program);
    return typechecker_get_table(checker);
}

/* Helper: parse, type check, and get the type of the first expression in main */
static GrayType *expression_type(const char *expr_code) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
        "do main() { mut _result = %s }", expr_code);
    DiagnosticList *diagnostics = diagnostic_create();
    diagnostics->use_color = false;
    Lexer *lexer =lexer_create(arena, buffer, "test.gray");
    Parser *parser = parser_create(arena, lexer, "test.gray", diagnostics);
    AstNode *program = parser_parse_program(parser);
    TypeChecker *checker =typechecker_create(diagnostics, "test.gray");
    typechecker_check(checker, program);
    TypeTable *table =typechecker_get_table(checker);

    /* Find the variable declaration's value and look up its type */
    AstNode *main_function =program->data.program.stmts[0];
    AstNode *var_decl = main_function->data.func_decl.body->data.block.stmts[0];
    if (var_decl->kind == NODE_VAR_DECL && var_decl->data.var_decl.value) {
        return typetable_get(table, var_decl->data.var_decl.value);
    }
    return NULL;
}

/* --- Scope and Symbol Table --- */

static void test_scope_define_lookup(void) {
    Scope *scope = scope_create(NULL);
    scope_define(scope, "x", &TYPE_INT, true);
    Symbol *symbol =scope_lookup(scope, "x");
    ASSERT_NOT_NULL(symbol);
    ASSERT_EQ(symbol->type->kind, TK_INT);
    ASSERT(symbol->mutable);
}

static void test_scope_nested(void) {
    Scope *outer = scope_create(NULL);
    scope_define(outer, "x", &TYPE_INT, true);
    Scope *inner = scope_create(outer);
    scope_define(inner, "y", &TYPE_STRING, false);

    /* inner can see both */
    ASSERT_NOT_NULL(scope_lookup(inner, "x"));
    ASSERT_NOT_NULL(scope_lookup(inner, "y"));

    /* outer can only see x */
    ASSERT_NOT_NULL(scope_lookup(outer, "x"));
    ASSERT(scope_lookup(outer, "y") == NULL);
}

static void test_scope_shadow(void) {
    Scope *outer = scope_create(NULL);
    scope_define(outer, "x", &TYPE_INT, true);
    Scope *inner = scope_create(outer);
    scope_define(inner, "x", &TYPE_STRING, false);

    /* inner sees the shadowed string version */
    Symbol *symbol =scope_lookup_local(inner, "x");
    ASSERT_NOT_NULL(symbol);
    ASSERT_EQ(symbol->type->kind, TK_STRING);
}

static void test_scope_undefined(void) {
    Scope *scope = scope_create(NULL);
    ASSERT(scope_lookup(scope, "nonexistent") == NULL);
}

/* --- Type Constructors --- */

static void test_type_from_name_primitives(void) {
    ASSERT_EQ(type_from_name("int")->kind, TK_INT);
    ASSERT_EQ(type_from_name("i8")->kind, TK_INT);
    ASSERT_EQ(type_from_name("i16")->kind, TK_INT);
    ASSERT_EQ(type_from_name("i32")->kind, TK_INT);
    ASSERT_EQ(type_from_name("i64")->kind, TK_INT);
    ASSERT_EQ(type_from_name("u8")->kind, TK_UINT);
    ASSERT_EQ(type_from_name("u16")->kind, TK_UINT);
    ASSERT_EQ(type_from_name("u32")->kind, TK_UINT);
    ASSERT_EQ(type_from_name("u64")->kind, TK_UINT);
    ASSERT_EQ(type_from_name("uint")->kind, TK_UINT);
    ASSERT_EQ(type_from_name("float")->kind, TK_FLOAT);
    ASSERT_EQ(type_from_name("f32")->kind, TK_FLOAT);
    ASSERT_EQ(type_from_name("f64")->kind, TK_FLOAT);
    ASSERT_EQ(type_from_name("bool")->kind, TK_BOOL);
    ASSERT_EQ(type_from_name("char")->kind, TK_CHAR);
    ASSERT_EQ(type_from_name("byte")->kind, TK_BYTE);
    ASSERT_EQ(type_from_name("string")->kind, TK_STRING);
    ASSERT_EQ(type_from_name("void")->kind, TK_VOID);
    ASSERT_EQ(type_from_name("nil")->kind, TK_NIL);
}

static void test_type_from_name_array(void) {
    GrayType *type =type_from_name("[int]");
    ASSERT_EQ(type->kind, TK_ARRAY);
    ASSERT_STR_EQ(type->element_type, "int");
}

static void test_type_from_name_struct(void) {
    GrayType *type =type_from_name("Person");
    ASSERT_EQ(type->kind, TK_STRUCT);
    ASSERT_STR_EQ(type->name, "Person");
}

static void test_type_is_numeric(void) {
    ASSERT(type_is_numeric(&TYPE_INT));
    ASSERT(type_is_numeric(&TYPE_FLOAT));
    ASSERT(type_is_numeric(&TYPE_CHAR));
    ASSERT(type_is_numeric(&TYPE_BYTE));
    ASSERT(!type_is_numeric(&TYPE_STRING));
    ASSERT(!type_is_numeric(&TYPE_BOOL));
}

/* --- Expression Type Resolution --- */

static void test_resolve_int_literal(void) {
    GrayType *type =expression_type("42");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_INT);
}

static void test_resolve_float_literal(void) {
    GrayType *type =expression_type("3.14");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_FLOAT);
}

static void test_resolve_string_literal(void) {
    GrayType *type =expression_type("\"hello\"");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_STRING);
}

static void test_resolve_bool_literal(void) {
    GrayType *type =expression_type("true");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_nil(void) {
    GrayType *type =expression_type("nil");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_NIL);
}

static void test_resolve_arithmetic(void) {
    GrayType *type =expression_type("1 + 2");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_INT);
}

static void test_resolve_float_arithmetic(void) {
    GrayType *type =expression_type("1.0 + 2");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_FLOAT);
}

static void test_resolve_comparison(void) {
    GrayType *type =expression_type("1 < 2");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_logical(void) {
    GrayType *type =expression_type("true && false");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_negation(void) {
    GrayType *type =expression_type("-42");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_INT);
}

static void test_resolve_not(void) {
    GrayType *type =expression_type("!true");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_array_literal(void) {
    GrayType *type =expression_type("{1, 2, 3}");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_ARRAY);
}

/* --- Variable Type Resolution --- */

static void test_resolve_typed_variable(void) {
    TypeTable *table =typecheck_test_input(
        "do main() {\n"
        "    mut x int = 42\n"
        "    mut y string = \"hello\"\n"
        "}");
    (void)table;
    /* If it doesn't crash, the scope correctly handled the declarations */
    ASSERT_NOT_NULL(table);
}

/* --- Builtin Function Types --- */

static void test_resolve_len(void) {
    GrayType *type =expression_type("len(\"hello\")");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_INT);
}

static void test_resolve_type_of(void) {
    GrayType *type =expression_type("type_of(42)");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_STRING);
}

static void test_resolve_to_float(void) {
    GrayType *type =expression_type("float(42)");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_FLOAT);
}

/* --- Struct Type Resolution --- */

static void test_resolve_struct_field(void) {
    TypeTable *table =typecheck_test_input(
        "const Person struct {\n"
        "    name string\n"
        "    age int\n"
        "}\n"
        "do main() {\n"
        "    mut p Person = Person{name: \"Alice\", age: 30}\n"
        "    mut n = p.name\n"
        "}");
    (void)table;
    ASSERT_NOT_NULL(table);
}

/* --- Function Return Type Resolution --- */

static void test_resolve_function_return(void) {
    TypeTable *table =typecheck_test_input(
        "do add(a int, b int) -> int { return a + b }\n"
        "do main() {\n"
        "    mut result = add(1, 2)\n"
        "}");
    (void)table;
    ASSERT_NOT_NULL(table);
}

/* --- Pointer Type Tests --- */

static void test_type_from_name_pointer(void) {
    GrayType *type =type_from_name("^int");
    ASSERT_EQ(type->kind, TK_POINTER);
    ASSERT_STR_EQ(type->element_type, "int");
}

static void test_type_pointer_constructor(void) {
    GrayType *type =type_pointer("Person");
    ASSERT_EQ(type->kind, TK_POINTER);
    ASSERT_STR_EQ(type->element_type, "Person");
}

static void test_resolve_addr(void) {
    GrayType *type =expression_type("addr(42)");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_POINTER);
}

/* Helper: parse and typecheck, return diagnostics */
static DiagnosticList *typecheck_diagnostics(const char *input) {
    DiagnosticList *diagnostics = diagnostic_create();
    diagnostics->use_color = false;
    Lexer *lexer =lexer_create(arena, input, "test.gray");
    Parser *parser = parser_create(arena, lexer, "test.gray", diagnostics);
    AstNode *program = parser_parse_program(parser);
    TypeChecker *checker =typechecker_create(diagnostics, "test.gray");
    typechecker_check(checker, program);
    return diagnostics;
}

/* Helper: check if a specific error code was emitted */
static bool has_error_code(DiagnosticList *diagnostics, const char *code) {
    for (int i = 0; i < diagnostics->count; i++) {
        if (diagnostics->items[i].code && strcmp(diagnostics->items[i].code, code) == 0) return true;
    }
    return false;
}

static void test_error_type_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x string = 42 }");
    ASSERT(diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_error_wrong_arg_count(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do add(a int, b int) -> int { return a + b }\n"
        "do main() { add(1, 2, 3) }");
    ASSERT(diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_error_deref_non_pointer(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 42\n mut y = x^ }");
    ASSERT(diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_resolve_map_type(void) {
    GrayType *type =expression_type("{\"a\": 1}");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_MAP);
}

static void test_resolve_string_enum(void) {
    TypeTable *table =typecheck_test_input(
        "const Status enum { TODO = \"todo\" DONE = \"done\" }\n"
        "do main() { mut s = Status.TODO }");
    (void)table;
    /* Should not crash — string enum member resolves correctly */
}

static void test_type_from_name_map(void) {
    GrayType *type =type_from_name("map[string:int]");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_MAP);
}

/* --- E3xxx: Type Error Detection --- */

static void test_error_E3001_type_mismatch_assign(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = \"hello\" }");
    ASSERT(has_error_code(diagnostics, "E3001"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3002_invalid_operator(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = \"a\" - \"b\" }");
    ASSERT(has_error_code(diagnostics, "E3002"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3003_non_int_index(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut a [int] = {1,2,3}\n mut x = a[\"bad\"] }");
    ASSERT(has_error_code(diagnostics, "E3003"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3005_const_reassign(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { const x int = 5\n x = 10 }");
    ASSERT(has_error_code(diagnostics, "E3005"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3006_return_from_void(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() { return 42 }\n"
        "do main() { foo() }");
    ASSERT(has_error_code(diagnostics, "E3006"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E5023_increment_float(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x float = 1.0\n x++ }");
    ASSERT(has_error_code(diagnostics, "E5023"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3008_index_non_array(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 5\n mut y = x[0] }");
    ASSERT(has_error_code(diagnostics, "E3008"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3009_foreach_non_iterable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 5\n for_each item in x { } }");
    ASSERT(has_error_code(diagnostics, "E3009"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3010_struct_no_field(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Point struct { x int\n y int }\n"
        "do main() { mut p Point = Point{x: 1, y: 2}\n mut z = p.z }");
    ASSERT(has_error_code(diagnostics, "E3010"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3013_field_on_primitive(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 42\n mut y = x.foo }");
    ASSERT(has_error_code(diagnostics, "E3013"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3015_call_non_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 42\n x() }");
    ASSERT(has_error_code(diagnostics, "E3015"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3016_deref_non_pointer(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 42\n mut y = x^ }");
    ASSERT(has_error_code(diagnostics, "E3016"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3036_out_of_range(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x uint = -5 }");
    ASSERT(has_error_code(diagnostics, "E3036"));
    diagnostic_destroy(diagnostics);
}

/* Regression test for #2414: the literal constant folder negated INT64_MIN
 * directly, which is undefined behavior in signed arithmetic even though
 * two's-complement hardware happens to print the right value. This is the
 * exact literal (-9223372036854775808) that reaches the folder through the
 * E3036 range check below, so it must typecheck clean, not report overflow. */
static void test_valid_int64_min_literal(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x i64 = -9223372036854775808 }");
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3024_missing_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() -> int { }\n"
        "do main() { foo() }");
    ASSERT(has_error_code(diagnostics, "E3024"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3027_const_to_mut_param(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do modify(&arr [int]) { arr[0] = 999 }\n"
        "do main() { const nums [int] = {1, 2, 3}\n modify(nums) }");
    ASSERT(has_error_code(diagnostics, "E3027"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3034_any_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x any = 42 }");
    ASSERT(has_error_code(diagnostics, "E3034"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3038_void_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x void = 42 }");
    ASSERT(has_error_code(diagnostics, "E3038"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3018_when_type_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 5\n when x { is \"bad\" { } default { } } }");
    ASSERT(has_error_code(diagnostics, "E3018"));
    diagnostic_destroy(diagnostics);
}

/* --- E4xxx: Name Problems --- */

static void test_error_E4001_undefined_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = y }");
    ASSERT(has_error_code(diagnostics, "E4001"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4002_undefined_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { nonexistent() }");
    ASSERT(has_error_code(diagnostics, "E4002"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4003_duplicate_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 1\n mut x int = 2 }");
    ASSERT(has_error_code(diagnostics, "E4003"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4004_duplicate_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() { }\n"
        "do foo() { }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E4004"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4005_no_main(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() { }");
    ASSERT(has_error_code(diagnostics, "E4005"));
    diagnostic_destroy(diagnostics);
}

/* --- E5xxx: Usage Problems --- */

static void test_error_E5008_wrong_arg_count_specific(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do add(a int, b int) -> int { return a + b }\n"
        "do main() { add(1) }");
    ASSERT(has_error_code(diagnostics, "E5008"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E5015_increment_literal(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { 42++ }");
    ASSERT(has_error_code(diagnostics, "E5015"));
    diagnostic_destroy(diagnostics);
}

/* --- Warnings --- */

static void test_warning_W1001_unused_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 42 }");
    ASSERT(diagnostic_warning_count(diagnostics) > 0);
    ASSERT(has_error_code(diagnostics, "W1001"));
    diagnostic_destroy(diagnostics);
}

static void test_warning_W1003_unused_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do unused() { }\n"
        "do main() { }");
    ASSERT(diagnostic_warning_count(diagnostics) > 0);
    ASSERT(has_error_code(diagnostics, "W1003"));
    diagnostic_destroy(diagnostics);
}

static void test_warning_W2002_shadow_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 1\n if true { mut x int = 2 } }");
    ASSERT(has_error_code(diagnostics, "W2002"));
    diagnostic_destroy(diagnostics);
}

/* --- More E3xxx --- */

static void test_error_E3011_type_as_value(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = int }");
    ASSERT(has_error_code(diagnostics, "E3011"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3012_addr_literal(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut p = addr(42) }");
    ASSERT(has_error_code(diagnostics, "E3012"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3032_compare_diff_enums(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED\n BLUE }\n"
        "const Size enum { SMALL\n BIG }\n"
        "do main() { mut x bool = Color.RED == Size.SMALL }");
    ASSERT(has_error_code(diagnostics, "E3032"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3007_negate_non_numeric(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = -\"hello\" }");
    ASSERT(has_error_code(diagnostics, "E3007"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3035_not_all_paths_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo(x int) -> int {\n"
        "    if x > 0 { return 1 }\n"
        "}\n"
        "do main() { foo(1) }");
    ASSERT(has_error_code(diagnostics, "E3035"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3039_ensure_non_call(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { ensure 42 }");
    ASSERT(has_error_code(diagnostics, "E3039"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3040_multi_return_single_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do pair() -> (int, int) { return 1, 2 }\n"
        "do main() { mut x = pair() }");
    ASSERT(has_error_code(diagnostics, "E3040"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3044_field_on_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Point struct { x int\n y int }\n"
        "do main() { mut v = Point.x }");
    ASSERT(has_error_code(diagnostics, "E3044"));
    diagnostic_destroy(diagnostics);
}

/* --- More E4xxx --- */

static void test_error_E4006_reserved_name(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut gray_internal int = 5 }");
    ASSERT(has_error_code(diagnostics, "E4006"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4012_shadow_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Point struct { x int }\n"
        "do main() { mut Point int = 5 }");
    ASSERT(has_error_code(diagnostics, "E4012"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4013_shadow_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do helper() { }\n"
        "do main() { mut helper int = 5 }");
    ASSERT(has_error_code(diagnostics, "E4013"));
    diagnostic_destroy(diagnostics);
}

/* --- More E5xxx --- */

static void test_error_E5007_append_const_array(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @arrays\n"
        "do main() { const arr [int] = {1, 2}\n arrays.append(arr, 3) }");
    ASSERT(has_error_code(diagnostics, "E5007"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E5011_unused_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do get() -> int { return 42 }\n"
        "do main() { get() }");
    ASSERT(has_error_code(diagnostics, "E5011"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3019_signed_to_unsigned(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = -5\n mut y uint = x }");
    ASSERT(has_error_code(diagnostics, "E3019"));
    diagnostic_destroy(diagnostics);
}

/* --- E2xxx: Parser errors detected during typechecking --- */

static void test_error_E2050_break_outside_loop(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { break }");
    ASSERT(has_error_code(diagnostics, "E2050"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2051_nested_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { do inner() { } }");
    ASSERT(has_error_code(diagnostics, "E2051"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2053_struct_in_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { const Foo struct { x int } }");
    ASSERT(has_error_code(diagnostics, "E2053"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2043_duplicate_case(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 1\n when x { is 1 { } is 1 { } default { } } }");
    ASSERT(has_error_code(diagnostics, "E2043"));
    diagnostic_destroy(diagnostics);
}

/* Note: compiler emits E2037 for reserved struct names (should be E2038) */
static void test_error_E2037_reserved_struct_name(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const string struct { x int }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2037"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2016_empty_enum(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Empty enum { }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2016"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2012_duplicate_param(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo(a int, a int) -> int { return a }\n"
        "do main() { foo(1, 2) }");
    ASSERT(has_error_code(diagnostics, "E2012"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2013_duplicate_struct_field(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Bad struct { x int\n x int }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2013"));
    diagnostic_destroy(diagnostics);
}

/* --- Stdlib errors emitted by typechecker --- */

static void test_error_E12006_duplicate_map_key(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut m = {\"a\": 1, \"a\": 2} }");
    ASSERT(has_error_code(diagnostics, "E12006"));
    diagnostic_destroy(diagnostics);
}

/* --- E2xxx: Additional parser-level errors in typechecker --- */

static void test_error_E2011_const_no_value(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { const x int }");
    ASSERT(has_error_code(diagnostics, "E2011"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2036_import_in_function(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { import @math }");
    ASSERT(has_error_code(diagnostics, "E2036"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2038_reserved_enum_name(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const int enum { A\n B }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2038"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2039_required_after_default_param(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo(a int = 1, b int) { }\n"
        "do main() { foo(1, 2) }");
    ASSERT(has_error_code(diagnostics, "E2039"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2056_statement_at_file_scope(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "if true { }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2056"));
    diagnostic_destroy(diagnostics);
}

/* --- E3xxx: Additional type errors --- */

static void test_error_E3017_fmt_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "const Point struct { x int\n y int }\n"
        "do main() {\n"
        "    mut p Point = Point{x: 1, y: 2}\n"
        "    fmt.printf(\"%s\", p)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3017"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3033_duplicate_enum_value(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED = 1\n BLUE = 1 }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3033"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3041_new_unknown_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut p = new(Bogus) }");
    ASSERT(has_error_code(diagnostics, "E3041"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3041_interpolate_void(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do noop() { }\n"
        "do main() { mut s string = \"result: ${noop()}\" }");
    ASSERT(has_error_code(diagnostics, "E3041"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3043_invalid_cast(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Point struct { x int\n y int }\n"
        "do main() {\n"
        "    mut p Point = Point{x: 1, y: 2}\n"
        "    mut x = cast(p, int)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3043"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3045_or_return_no_error(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do get() -> int { return 42 }\n"
        "do main() { mut x = get() or_return }");
    ASSERT(has_error_code(diagnostics, "E3045"));
    diagnostic_destroy(diagnostics);
}

/* --- E4xxx: Additional name errors --- */

static void test_error_E4014_shadow_module(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @math\n"
        "do main() { mut math int = 5 }");
    ASSERT(has_error_code(diagnostics, "E4014"));
    diagnostic_destroy(diagnostics);
}

/* --- E5xxx: Additional usage errors --- */

static void test_error_E5024_signed_return_as_unsigned(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() -> uint {\n"
        "    mut x int = -5\n"
        "    return x\n"
        "}\n"
        "do main() { foo() }");
    ASSERT(has_error_code(diagnostics, "E5024"));
    diagnostic_destroy(diagnostics);
}

/* --- E6xxx: Module errors --- */

static void test_error_E6001_unknown_module(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @nonexistent\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E6001"));
    diagnostic_destroy(diagnostics);
}

/* --- Stdlib type errors in typechecker --- */

static void test_error_E7004_repeat_float_count(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @strings\n"
        "do main() { mut s = strings.repeat(\"ha\", 3.5) }");
    ASSERT(has_error_code(diagnostics, "E7004"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E9002_sum_non_numeric(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @arrays\n"
        "do main() { mut a [string] = {\"a\", \"b\"}\n arrays.sum(a) }");
    ASSERT(has_error_code(diagnostics, "E9002"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E9005_invalid_range(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { for i in range(10, 1) { } }");
    ASSERT(has_error_code(diagnostics, "E9005"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E12001_map_func_on_array(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @maps\n"
        "do main() { mut a [int] = {1, 2}\n maps.keys(a) }");
    ASSERT(has_error_code(diagnostics, "E12001"));
    diagnostic_destroy(diagnostics);
}

/* --- Additional Warnings --- */

static void test_warning_W1005_typed_blank(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do pair() -> (int, int) { return 1, 2 }\n"
        "do main() { mut x int, _ int = pair() }");
    ASSERT(has_error_code(diagnostics, "W1005"));
    diagnostic_destroy(diagnostics);
}

static void test_warning_W1004_no_value_declaration(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { foobar int\n println(foobar) }");
    ASSERT(has_error_code(diagnostics, "W1004"));
    diagnostic_destroy(diagnostics);

    /* A value, explicit or the zero value, silences it. */
    DiagnosticList *ok = typecheck_diagnostics(
        "do main() { mut foobar int = 0\n println(foobar) }");
    ASSERT(!has_error_code(ok, "W1004"));
    diagnostic_destroy(ok);
}

static void test_warning_W2001_unused_import(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @math\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "W1002"));
    diagnostic_destroy(diagnostics);
}

static void test_warning_W3003_partial_array_init(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut a [int, 5] = {1, 2} }");
    ASSERT(has_error_code(diagnostics, "W3003"));
    diagnostic_destroy(diagnostics);
}

/* --- Additional typechecker coverage --- */

static void test_resolve_char_literal(void) {
    GrayType *type =expression_type("'A'");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_CHAR);
}

static void test_resolve_modulo(void) {
    GrayType *type =expression_type("10 % 3");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_INT);
}


static void test_resolve_equality(void) {
    GrayType *type =expression_type("1 == 1");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_inequality(void) {
    GrayType *type =expression_type("1 != 2");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_or_logic(void) {
    GrayType *type =expression_type("true || false");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_string_comparison(void) {
    GrayType *type =expression_type("\"a\" == \"b\"");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_BOOL);
}

static void test_resolve_float_negation(void) {
    GrayType *type =expression_type("-3.14");
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(type->kind, TK_FLOAT);
}

static void test_scope_deeply_nested(void) {
    Scope *scope1 = scope_create(NULL);
    scope_define(scope1, "a", &TYPE_INT, true);
    Scope *scope2 = scope_create(scope1);
    scope_define(scope2, "b", &TYPE_STRING, true);
    Scope *scope3 = scope_create(scope2);
    scope_define(scope3, "c", &TYPE_BOOL, true);

    /* s3 can see all three */
    ASSERT_NOT_NULL(scope_lookup(scope3, "a"));
    ASSERT_NOT_NULL(scope_lookup(scope3, "b"));
    ASSERT_NOT_NULL(scope_lookup(scope3, "c"));

    /* s1 can only see a */
    ASSERT_NOT_NULL(scope_lookup(scope1, "a"));
    ASSERT(scope_lookup(scope1, "b") == NULL);
    ASSERT(scope_lookup(scope1, "c") == NULL);
}

static void test_scope_local_only(void) {
    Scope *outer = scope_create(NULL);
    scope_define(outer, "x", &TYPE_INT, true);
    Scope *inner = scope_create(outer);

    /* lookup_local should NOT find outer's x */
    ASSERT(scope_lookup_local(inner, "x") == NULL);
    /* but regular lookup should */
    ASSERT_NOT_NULL(scope_lookup(inner, "x"));
}

static void test_type_from_name_bigint(void) {
    GrayType *type1 = type_from_name("i128");
    ASSERT_NOT_NULL(type1);
    GrayType *type2 = type_from_name("u128");
    ASSERT_NOT_NULL(type2);
    GrayType *type3 = type_from_name("i256");
    ASSERT_NOT_NULL(type3);
    GrayType *type4 = type_from_name("u256");
    ASSERT_NOT_NULL(type4);
}

static void test_type_is_numeric_uint(void) {
    ASSERT(type_is_numeric(&TYPE_UINT));
}

static void test_error_E3002_bool_arithmetic(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = true + false }");
    ASSERT(has_error_code(diagnostics, "E3002"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3001_bool_to_int(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = true }");
    ASSERT(has_error_code(diagnostics, "E3001"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3001_int_to_string(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x string = 42 }");
    ASSERT(has_error_code(diagnostics, "E3001"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3005_const_array_reassign(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { const arr [int] = {1,2,3}\n arr = {4,5,6} }");
    ASSERT(has_error_code(diagnostics, "E3005"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3009_foreach_int(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { for_each x in 42 { } }");
    ASSERT(has_error_code(diagnostics, "E3009"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4003_duplicate_variable_same_scope(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 1\n mut x string = \"hi\" }");
    ASSERT(has_error_code(diagnostics, "E4003"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E5008_too_many_args(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do add(a int, b int) -> int { return a + b }\n"
        "do main() { add(1, 2, 3) }");
    ASSERT(has_error_code(diagnostics, "E5008"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3008_index_string(void) {
    /* String indexing should work, so this should not error */
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut s string = \"hello\"\n mut c = s[0] }");
    ASSERT(!has_error_code(diagnostics, "E3008"));
    diagnostic_destroy(diagnostics);
}

static void test_valid_nested_struct(void) {
    /* Nested struct access should typecheck without errors */
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Inner struct {\n"
        "    val int\n"
        "}\n"
        "const Outer struct {\n"
        "    inner Inner\n"
        "}\n"
        "do main() {\n"
        "    mut o Outer = Outer{inner: Inner{val: 42}}\n"
        "    mut v int = o.inner.val\n"
        "    println(\"${v}\")\n"
        "}");
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_valid_enum_member_access(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "do main() { mut c = Color.RED\n println(\"${c}\") }");
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_valid_multi_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do swap(a int, b int) -> (int, int) { return b, a }\n"
        "do main() { mut x int, y int = swap(1, 2)\n println(\"${x} ${y}\") }");
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_valid_when_statement(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "    mut x int = 2\n"
        "    when x {\n"
        "        is 1 { println(\"one\") }\n"
        "        is 2 { println(\"two\") }\n"
        "        default { println(\"other\") }\n"
        "    }\n"
        "}");
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_valid_struct_function_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Point struct {\n"
        "    x int\n"
        "    y int\n"
        "    do origin() -> Point {\n"
        "        return Point{x: 0, y: 0}\n"
        "    }\n"
        "}\n"
        "do main() { mut p Point = Point.origin()\n println(\"${p.x}\") }");
    ASSERT(!diagnostic_has_errors(diagnostics));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2050_continue_outside_loop(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { continue }");
    ASSERT(has_error_code(diagnostics, "E2050"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3024_some_paths_missing_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo(x int) -> int {\n"
        "    if x > 0 { return 1 }\n"
        "    if x < 0 { return -1 }\n"
        "}\n"
        "do main() { foo(1) }");
    ASSERT(has_error_code(diagnostics, "E3035") || has_error_code(diagnostics, "E3024"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4007_duplicate_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Foo struct { x int }\n"
        "const Foo struct { y int }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E4007"));
    diagnostic_destroy(diagnostics);
}

/* ===== Batch: untested error codes ===== */

static void test_error_E3047_enum_no_member(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED GREEN BLUE }\n"
        "do main() { mut c = Color.YELLOW }");
    ASSERT(has_error_code(diagnostics, "E3047"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3048_string_plus(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut s string = \"a\" + \"b\" }");
    ASSERT(has_error_code(diagnostics, "E3048"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3057_invalid_map_key(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do main() { mut m map[P:int] = {} }");
    ASSERT(has_error_code(diagnostics, "E3057"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3059_const_map(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { const m map[string:int] = {\"a\": 1} }");
    ASSERT(has_error_code(diagnostics, "E3059"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3061_recursive_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Node struct { next Node }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3061"));
    diagnostic_destroy(diagnostics);
}

static void test_valid_recursive_pointer_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Node struct {\n value int\n next ^Node\n}\n"
        "do main() { }");
    ASSERT(diagnostics->count == 0);
    diagnostic_destroy(diagnostics);
}

static void test_error_E3063_return_addr_local(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do bad() -> ptr<int> {\n"
        "  mut x int = 42\n"
        "  return addr(x)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3063"));
    diagnostic_destroy(diagnostics);
}

/* E3068 requires parsing "void" as a return type, which the parser may
   reject before the typechecker sees it. Removed — tested via integration. */

static void test_error_E3072_return_nil_non_pointer(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do bad() -> int { return nil }");
    ASSERT(has_error_code(diagnostics, "E3072"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3073_return_in_main(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { return }");
    ASSERT(has_error_code(diagnostics, "E3073"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3074_array_compare(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut a [int] = {1, 2}\n"
        "  mut b [int] = {1, 2}\n"
        "  if a == b { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3074"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3076_map_compare(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut a map[string:int] = {\"x\": 1}\n"
        "  mut b map[string:int] = {\"x\": 1}\n"
        "  if a == b { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3076"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3078_pointer_arithmetic(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut x int = 10\n"
        "  mut p = addr(x)\n"
        "  mut q = p + 1\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3078"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3080_named_return_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do get() -> (result int) {\n"
        "  mut other int = 42\n"
        "  return other\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3080"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3082_wildcard_named_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do get(x ?) -> (val ?) { return x }\n"
        "do main() { println(get(1)) }");
    ASSERT(has_error_code(diagnostics, "E3082"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2014_duplicate_enum_variant(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED RED }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2014"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2015_duplicate_struct_field_init(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do main() { mut p = P{x: 1, x: 2} }");
    ASSERT(has_error_code(diagnostics, "E2015"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2063_duplicate_named_return(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do bad() -> (x int, x int) {\n"
        "  mut x int = 1\n"
        "  return x, x\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2063"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2064_field_func_conflict(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const S struct {\n"
        "  name int\n"
        "  do name() -> int { return 0 }\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2064"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2065_enum_variant_same_as_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { Color }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2065"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2066_struct_field_same_as_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Foo struct { Foo int }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2066"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E2067_empty_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Empty struct { }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E2067"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3083_c_string_non_pointer(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut msg = c_string(\"hello\") }");
    ASSERT(has_error_code(diagnostics, "E3083"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4008_main_with_params(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main(x int) { }");
    ASSERT(has_error_code(diagnostics, "E4008"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E5025_invalid_assign_target(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { 42 = 10 }");
    ASSERT(has_error_code(diagnostics, "E5025"));
    diagnostic_destroy(diagnostics);
}


/* ===== Batch 2: 84 untested E3xxx error codes (#2098) ===== */

/* --- Array/map initialization --- */

static void test_error_E3050_array_no_type_annotation(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut a = {1, 2, 3} }");
    ASSERT(has_error_code(diagnostics, "E3050"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3051_map_no_type_annotation(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut m = {\"a\": 1} }");
    ASSERT(has_error_code(diagnostics, "E3051"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3052_fixed_array_too_many(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut a [int, 2] = {1, 2, 3} }");
    ASSERT(has_error_code(diagnostics, "E3052"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3053_array_element_type_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut a [int] = {1, \"two\", 3} }");
    ASSERT(has_error_code(diagnostics, "E3053"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3054_mut_array_fixed_size(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut a [int, 3] = {1, 2, 3} }");
    ASSERT(has_error_code(diagnostics, "E3054"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3055_const_array_no_size(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { const a [int] = {1, 2, 3} }");
    ASSERT(has_error_code(diagnostics, "E3055"));
    diagnostic_destroy(diagnostics);
}

/* --- Pointer/comparison --- */

static void test_error_E3077_struct_compare(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do main() {\n"
        "  mut a P = P{x: 1}\n"
        "  mut b P = P{x: 2}\n"
        "  if a == b { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3077"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3079_mut_ref_to_const(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  const x int = 42\n"
        "  mut p = ref(x)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3079"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3092_nil_compare_non_nullable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut x int = 5\n"
        "  if x == nil { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3092"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3096_negate_unsigned(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x uint = 5\n mut y = -x }");
    ASSERT(has_error_code(diagnostics, "E3096"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3098_struct_assign_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const A struct { x int }\n"
        "const B struct { x int }\n"
        "do main() {\n"
        "  mut a A = A{x: 1}\n"
        "  mut p = addr(a)\n"
        "  p^ = B{x: 2}\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3098"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3120_pointer_ordering(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut x int = 10\n"
        "  mut p = addr(x)\n"
        "  mut q = addr(x)\n"
        "  if p < q { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3120"));
    diagnostic_destroy(diagnostics);
}

/* --- Operator/expression misc --- */

static void test_error_E3004_string_index_assign(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut s string = \"hello\"\n s[0] = 'x' }");
    ASSERT(has_error_code(diagnostics, "E3004"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3031_bare_function_name(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() -> int { return 42 }\n"
        "do main() { mut x = foo }");
    ASSERT(has_error_code(diagnostics, "E3031"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3046_int_literal_overflow(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x int = 99999999999999999999 }");
    ASSERT(has_error_code(diagnostics, "E3046"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3049_enum_arithmetic(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED GREEN BLUE }\n"
        "do main() { mut x = Color.RED + Color.GREEN }");
    ASSERT(has_error_code(diagnostics, "E3049"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3081_bare_function_stmt(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo() -> int { return 42 }\n"
        "do main() { foo }");
    ASSERT(has_error_code(diagnostics, "E3081"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3084_type_of_type_name(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do main() { mut t = type_of(P) }");
    ASSERT(has_error_code(diagnostics, "E3084"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3085_in_type_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut a [int] = {1, 2, 3}\n"
        "  if \"hello\" in a { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3085"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3090_not_non_bool(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = !42 }");
    ASSERT(has_error_code(diagnostics, "E3090"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3093_arithmetic_on_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do main() {\n"
        "  mut a P = P{x: 1}\n"
        "  mut b = a + 1\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3093"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3095_in_invalid_rhs(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut x int = 5\n"
        "  if 1 in x { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3095"));
    diagnostic_destroy(diagnostics);
}

/* --- Format string validation --- */

static void test_error_E3086_fmt_non_string_format(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { mut x int = 42\n fmt.printf(x) }");
    ASSERT(has_error_code(diagnostics, "E3086"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3087_fmt_specifier_n(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { fmt.printf(\"%n\") }");
    ASSERT(has_error_code(diagnostics, "E3087"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3088_fmt_arg_type_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { fmt.printf(\"%d\", \"hello\") }");
    ASSERT(has_error_code(diagnostics, "E3088"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3105_fmt_unknown_specifier(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { fmt.printf(\"%q\", 42) }");
    ASSERT(has_error_code(diagnostics, "E3105"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3106_fmt_dangling_percent(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { fmt.printf(\"%\") }");
    ASSERT(has_error_code(diagnostics, "E3106"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3107_fmt_too_few_args(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { fmt.printf(\"%d %d\", 1) }");
    ASSERT(has_error_code(diagnostics, "E3107"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3108_fmt_too_many_args(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @fmt\n"
        "do main() { fmt.printf(\"%d\", 1, 2) }");
    ASSERT(has_error_code(diagnostics, "E3108"));
    diagnostic_destroy(diagnostics);
}

/* --- Enum features --- */

static void test_error_E3111_string_enum_tagged(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Mode enum {\n"
        "  Fast(int) = \"fast\"\n"
        "  Slow = \"slow\"\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3111"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3112_flags_enum_tagged(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "#flags\n"
        "const Perms enum {\n"
        "  Read(int)\n"
        "  Write\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3112"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3113_tagged_payload_count(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Shape enum {\n"
        "  Circle(float)\n"
        "  Point\n"
        "}\n"
        "do main() { mut s Shape = Shape.Circle(1.0, 2.0) }");
    ASSERT(has_error_code(diagnostics, "E3113"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3114_plain_variant_called(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Shape enum {\n"
        "  Circle(float)\n"
        "  Point\n"
        "}\n"
        "do main() { mut s Shape = Shape.Point(1.0) }");
    ASSERT(has_error_code(diagnostics, "E3114"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3115_plain_enum_called(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED GREEN BLUE }\n"
        "do main() { mut c = Color.RED(1) }");
    ASSERT(has_error_code(diagnostics, "E3115"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3116_tagged_when_binding_count(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Shape enum {\n"
        "  Circle(float)\n"
        "  Point\n"
        "}\n"
        "do main() {\n"
        "  mut s Shape = Shape.Circle(3.14)\n"
        "  when s {\n"
        "    is Circle(a, b) { }\n"
        "    is Point { }\n"
        "  }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3116"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3117_enum_int_compare(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED GREEN BLUE }\n"
        "do main() {\n"
        "  mut c = Color.RED\n"
        "  if c == 0 { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3117"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3118_int_to_enum_assign(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED GREEN BLUE }\n"
        "do main() {\n"
        "  mut c Color = Color.RED\n"
        "  c = 1\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3118"));
    diagnostic_destroy(diagnostics);
}

/* --- Resource/handle/func ref --- */

static void test_error_E3062_const_handle(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Channel struct { id int }\n"
        "do main() {\n"
        "  const ch Channel = Channel{id: 1}\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3062"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3064_arena_already_destroyed(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @mem\n"
        "do main() {\n"
        "  mut a = mem.arena(1024)\n"
        "  mem.destroy(a)\n"
        "  mem.destroy(a)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3064"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3066_func_ref_sig_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo(a string) { }\n"
        "do apply(f func(int)) { }\n"
        "do main() { apply(()foo) }");
    ASSERT(has_error_code(diagnostics, "E3066"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3027_non_assignable_ref_param(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do modify(&x int) { x = 10 }\n"
        "do main() { modify(42) }");
    ASSERT(has_error_code(diagnostics, "E3027"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3070_nested_ensure(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @io\n"
        "do main() {\n"
        "  if true {\n"
        "    ensure io.write_file(\"test.txt\", \"data\")\n"
        "  }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3070"));
    diagnostic_destroy(diagnostics);
}

/* --- JSON struct validation --- */

static void test_error_E3103_json_struct_func_field(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "#json\n"
        "const Config struct {\n"
        "  callback func(int) -> int\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3103"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3104_json_struct_method(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "#json\n"
        "const Config struct {\n"
        "  name string\n"
        "  do greet() { println(\"hi\") }\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3104"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3109_json_struct_default_value(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "#json\n"
        "const Config struct {\n"
        "  name string = \"default\"\n"
        "}\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3109"));
    diagnostic_destroy(diagnostics);
}

/* --- Conditions/loops/when --- */

static void test_error_E3056_incomplete_when(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Color enum { RED GREEN BLUE }\n"
        "do main() {\n"
        "  mut c = Color.RED\n"
        "  #strict\n"
        "  when c {\n"
        "    is Color.RED { }\n"
        "    is Color.GREEN { }\n"
        "  }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3056"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3091_non_bool_condition(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut s string = \"hello\"\n"
        "  if s { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3091"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3121_struct_when_subject(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do main() {\n"
        "  mut p P = P{x: 1}\n"
        "  when p { default { } }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3121"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3123_foreach_both_discarded(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut a [int] = {1, 2, 3}\n"
        "  for_each _, _ in a { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3123"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3129_empty_while_body(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { while true { } }");
    ASSERT(has_error_code(diagnostics, "E3129"));
    diagnostic_destroy(diagnostics);
}

/* --- Fixed-size arrays --- */

static void test_error_E3119_fixed_array_in_param(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do foo(arr [int, 3]) { }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3119"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3125_array_size_not_const(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut n int = 5\n const a [int, n] = {1, 2, 3} }");
    ASSERT(has_error_code(diagnostics, "E3125"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3126_array_size_zero(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const SIZE int = 0\n"
        "do main() { const a [int, SIZE] = {} }");
    ASSERT(has_error_code(diagnostics, "E3126"));
    diagnostic_destroy(diagnostics);
}

/* --- Generics/wildcards --- */

static void test_error_E3058_generic_type_error(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do to_int(x ?) -> int { return x }\n"
        "do main() { to_int(\"hello\") }");
    ASSERT(has_error_code(diagnostics, "E3058"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3060_wildcard_return_no_param(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do bad(x int) -> ? { return x }\n"
        "do main() { bad(1) }");
    ASSERT(has_error_code(diagnostics, "E3060"));
    diagnostic_destroy(diagnostics);
}

/* A type parameter takes any type name; a T{...} literal in the body is what
 * narrows one to struct types, and E3127 is reported at the literal. */
static void test_error_E3127_struct_literal_non_struct(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do mk_stack(T <?>) -> ? { return T{} }\n"
        "do main() { mk_stack(int) }");
    ASSERT(has_error_code(diagnostics, "E3127"));
    diagnostic_destroy(diagnostics);
}

static void test_E3127_not_reported_for_primitive_type_arg(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do identity(t <?>) -> int { return size_of(t) }\n"
        "do main() { identity(int) }");
    ASSERT(!has_error_code(diagnostics, "E3127"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E4016_type_arg_names_no_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do identity(t <?>) -> int { return size_of(t) }\n"
        "do main() { identity(Nonexistent) }");
    ASSERT(has_error_code(diagnostics, "E4016"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3128_sizeof_variable(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do identity(t <?>) -> int { return size_of(t) }\n"
        "do main() {\n"
        "  mut x int = 5\n"
        "  identity(x)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3128"));
    diagnostic_destroy(diagnostics);
}

/* --- Enum advanced --- */

static void test_error_E3110_implicit_enum_no_context(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() { mut x = .HELLO }");
    ASSERT(has_error_code(diagnostics, "E3110"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3124_tagged_enum_equality(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Shape enum {\n"
        "  Circle(float)\n"
        "  Point\n"
        "}\n"
        "do main() {\n"
        "  mut a Shape = Shape.Circle(1.0)\n"
        "  mut b Shape = Shape.Circle(1.0)\n"
        "  if a == b { }\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3124"));
    diagnostic_destroy(diagnostics);
}

/* --- Remaining misc --- */

static void test_error_E3071_return_nil_wildcard_ptr(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do bad(x ?) -> ? { return nil }\n"
        "do main() { bad(1) }");
    ASSERT(has_error_code(diagnostics, "E3071"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3075_chain_struct_calls(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Builder struct {\n"
        "  val int\n"
        "  do create() -> Builder { return Builder{val: 0} }\n"
        "  do set(self Builder, v int) -> Builder { return Builder{val: v} }\n"
        "}\n"
        "do main() { mut b = Builder.create().set(1) }");
    ASSERT(has_error_code(diagnostics, "E3075"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3089_fallible_no_error_handling(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "import @io\n"
        "do main() { mut data = io.read_file(\"test.txt\") }");
    ASSERT(has_error_code(diagnostics, "E3089"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3094_array_index_assign_type(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do main() {\n"
        "  mut a [int] = {1, 2, 3}\n"
        "  a[0] = \"bad\"\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3094"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3099_reserved_struct_name(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const Router struct { path string }\n"
        "do main() { }");
    ASSERT(has_error_code(diagnostics, "E3099"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3100_type_as_func_arg(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "const P struct { x int }\n"
        "do foo(x int) { }\n"
        "do main() { foo(P) }");
    ASSERT(has_error_code(diagnostics, "E3100"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3101_mut_func_ref(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do greet() { println(\"hi\") }\n"
        "do main() { mut f = ()greet }");
    ASSERT(has_error_code(diagnostics, "E3101"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3102_func_return_to_var(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do get_fn() -> func() { return ref(greet) }\n"
        "do greet() { println(\"hi\") }\n"
        "do main() { mut f = get_fn() }");
    ASSERT(has_error_code(diagnostics, "E3102"));
    diagnostic_destroy(diagnostics);
}

static void test_error_E3122_addr_const_var(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do take(&x int) { x = 10 }\n"
        "do main() {\n"
        "  const v int = 5\n"
        "  take(v)\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3122") || has_error_code(diagnostics, "E3027"));
    diagnostic_destroy(diagnostics);
}

/* --- E3097: addr of inner-scope variable assigned to outer pointer --- */

static void test_error_E3097_addr_scope_mismatch(void) {
    DiagnosticList *diagnostics = typecheck_diagnostics(
        "do setup() -> ^int {\n"
        "  mut outer int = 10\n"
        "  mut p = addr(outer)\n"
        "  if true {\n"
        "    mut inner int = 20\n"
        "    p = addr(inner)\n"
        "  }\n"
        "  return p\n"
        "}");
    ASSERT(has_error_code(diagnostics, "E3097"));
    diagnostic_destroy(diagnostics);
}

int main(void) {
    arena = arena_create(256 * 1024);
    printf("\n");

    /* Scope tests */
    RUN_TEST(test_scope_define_lookup);
    RUN_TEST(test_scope_nested);
    RUN_TEST(test_scope_shadow);
    RUN_TEST(test_scope_undefined);

    /* Type constructor tests */
    RUN_TEST(test_type_from_name_primitives);
    RUN_TEST(test_type_from_name_array);
    RUN_TEST(test_type_from_name_struct);
    RUN_TEST(test_type_is_numeric);

    /* Expression resolution tests */
    RUN_TEST(test_resolve_int_literal);
    RUN_TEST(test_resolve_float_literal);
    RUN_TEST(test_resolve_string_literal);
    RUN_TEST(test_resolve_bool_literal);
    RUN_TEST(test_resolve_nil);
    RUN_TEST(test_resolve_arithmetic);
    RUN_TEST(test_resolve_float_arithmetic);
    RUN_TEST(test_resolve_comparison);
    RUN_TEST(test_resolve_logical);
    RUN_TEST(test_resolve_negation);
    RUN_TEST(test_resolve_not);
    RUN_TEST(test_resolve_array_literal);

    /* Variable tests */
    RUN_TEST(test_resolve_typed_variable);

    /* Builtin tests */
    RUN_TEST(test_resolve_len);
    RUN_TEST(test_resolve_type_of);
    RUN_TEST(test_resolve_to_float);

    /* Struct tests */
    RUN_TEST(test_resolve_struct_field);

    /* Function tests */
    RUN_TEST(test_resolve_function_return);

    /* Pointer tests */
    RUN_TEST(test_type_from_name_pointer);
    RUN_TEST(test_type_pointer_constructor);
    RUN_TEST(test_resolve_addr);

    /* Error detection tests */
    RUN_TEST(test_error_type_mismatch);
    RUN_TEST(test_error_wrong_arg_count);
    RUN_TEST(test_error_deref_non_pointer);

    /* Map/enum type tests */
    RUN_TEST(test_resolve_map_type);
    RUN_TEST(test_resolve_string_enum);
    RUN_TEST(test_type_from_name_map);

    /* E3xxx: Type error detection */
    RUN_TEST(test_error_E3001_type_mismatch_assign);
    RUN_TEST(test_error_E3002_invalid_operator);
    RUN_TEST(test_error_E3003_non_int_index);
    RUN_TEST(test_error_E3005_const_reassign);
    RUN_TEST(test_error_E3006_return_from_void);
    RUN_TEST(test_error_E5023_increment_float);
    RUN_TEST(test_error_E3008_index_non_array);
    RUN_TEST(test_error_E3009_foreach_non_iterable);
    RUN_TEST(test_error_E3010_struct_no_field);
    RUN_TEST(test_error_E3013_field_on_primitive);
    RUN_TEST(test_error_E3015_call_non_function);
    RUN_TEST(test_error_E3016_deref_non_pointer);
    RUN_TEST(test_error_E3036_out_of_range);
    RUN_TEST(test_valid_int64_min_literal);
    RUN_TEST(test_error_E3024_missing_return);
    RUN_TEST(test_error_E3027_const_to_mut_param);
    RUN_TEST(test_error_E3034_any_type);
    RUN_TEST(test_error_E3038_void_variable);
    RUN_TEST(test_error_E3018_when_type_mismatch);

    /* E4xxx: Name problems */
    RUN_TEST(test_error_E4001_undefined_variable);
    RUN_TEST(test_error_E4002_undefined_function);
    RUN_TEST(test_error_E4003_duplicate_variable);
    RUN_TEST(test_error_E4004_duplicate_function);
    RUN_TEST(test_error_E4005_no_main);

    /* E5xxx: Usage problems */
    RUN_TEST(test_error_E5008_wrong_arg_count_specific);
    RUN_TEST(test_error_E5015_increment_literal);

    /* Warnings */
    RUN_TEST(test_warning_W1001_unused_variable);
    RUN_TEST(test_warning_W1003_unused_function);
    RUN_TEST(test_warning_W2002_shadow_variable);

    /* More E3xxx */
    RUN_TEST(test_error_E3011_type_as_value);
    RUN_TEST(test_error_E3012_addr_literal);
    RUN_TEST(test_error_E3032_compare_diff_enums);
    RUN_TEST(test_error_E3007_negate_non_numeric);
    RUN_TEST(test_error_E3035_not_all_paths_return);
    RUN_TEST(test_error_E3039_ensure_non_call);
    RUN_TEST(test_error_E3040_multi_return_single_variable);
    RUN_TEST(test_error_E3044_field_on_type);

    /* More E4xxx */
    RUN_TEST(test_error_E4006_reserved_name);
    RUN_TEST(test_error_E4012_shadow_type);
    RUN_TEST(test_error_E4013_shadow_function);

    /* More E5xxx */
    RUN_TEST(test_error_E5007_append_const_array);
    RUN_TEST(test_error_E5011_unused_return);
    RUN_TEST(test_error_E3019_signed_to_unsigned);

    /* E2xxx: Parser-level errors in typechecker */
    RUN_TEST(test_error_E2050_break_outside_loop);
    RUN_TEST(test_error_E2051_nested_function);
    RUN_TEST(test_error_E2053_struct_in_function);
    RUN_TEST(test_error_E2043_duplicate_case);
    RUN_TEST(test_error_E2037_reserved_struct_name);
    RUN_TEST(test_error_E2016_empty_enum);
    RUN_TEST(test_error_E2012_duplicate_param);
    RUN_TEST(test_error_E2013_duplicate_struct_field);

    /* Stdlib errors in typechecker */
    RUN_TEST(test_error_E12006_duplicate_map_key);

    /* E2xxx: Additional parser-level errors in typechecker */
    RUN_TEST(test_error_E2011_const_no_value);
    RUN_TEST(test_error_E2036_import_in_function);
    RUN_TEST(test_error_E2038_reserved_enum_name);
    RUN_TEST(test_error_E2039_required_after_default_param);
    RUN_TEST(test_error_E2056_statement_at_file_scope);

    /* E3xxx: Additional type errors */
    RUN_TEST(test_error_E3017_fmt_struct);
    RUN_TEST(test_error_E3033_duplicate_enum_value);
    RUN_TEST(test_error_E3041_new_unknown_type);
    RUN_TEST(test_error_E3041_interpolate_void);
    RUN_TEST(test_error_E3043_invalid_cast);
    RUN_TEST(test_error_E3045_or_return_no_error);

    /* E4xxx: Additional name errors */
    RUN_TEST(test_error_E4014_shadow_module);

    /* E5xxx: Additional usage errors */
    RUN_TEST(test_error_E5024_signed_return_as_unsigned);

    /* E6xxx: Module errors */
    RUN_TEST(test_error_E6001_unknown_module);

    /* Stdlib type errors */
    RUN_TEST(test_error_E7004_repeat_float_count);
    RUN_TEST(test_error_E9002_sum_non_numeric);
    RUN_TEST(test_error_E9005_invalid_range);
    RUN_TEST(test_error_E12001_map_func_on_array);

    /* Additional warnings */
    RUN_TEST(test_warning_W1005_typed_blank);
    RUN_TEST(test_warning_W1004_no_value_declaration);
    RUN_TEST(test_warning_W2001_unused_import);
    RUN_TEST(test_warning_W3003_partial_array_init);

    /* Additional typechecker coverage */
    RUN_TEST(test_resolve_char_literal);
    RUN_TEST(test_resolve_modulo);
    RUN_TEST(test_resolve_equality);
    RUN_TEST(test_resolve_inequality);
    RUN_TEST(test_resolve_or_logic);
    RUN_TEST(test_resolve_string_comparison);
    RUN_TEST(test_resolve_float_negation);
    RUN_TEST(test_scope_deeply_nested);
    RUN_TEST(test_scope_local_only);
    RUN_TEST(test_type_from_name_bigint);
    RUN_TEST(test_type_is_numeric_uint);
    RUN_TEST(test_error_E3002_bool_arithmetic);
    RUN_TEST(test_error_E3001_bool_to_int);
    RUN_TEST(test_error_E3001_int_to_string);
    RUN_TEST(test_error_E3005_const_array_reassign);
    RUN_TEST(test_error_E3009_foreach_int);
    RUN_TEST(test_error_E4003_duplicate_variable_same_scope);
    RUN_TEST(test_error_E5008_too_many_args);
    RUN_TEST(test_error_E3008_index_string);
    RUN_TEST(test_valid_nested_struct);
    RUN_TEST(test_valid_enum_member_access);
    RUN_TEST(test_valid_multi_return);
    RUN_TEST(test_valid_when_statement);
    RUN_TEST(test_valid_struct_function_return);
    RUN_TEST(test_error_E2050_continue_outside_loop);
    RUN_TEST(test_error_E3024_some_paths_missing_return);
    RUN_TEST(test_error_E4007_duplicate_struct);

    /* Batch: previously untested error codes */
    RUN_TEST(test_error_E2014_duplicate_enum_variant);
    RUN_TEST(test_error_E2015_duplicate_struct_field_init);
    RUN_TEST(test_error_E2063_duplicate_named_return);
    RUN_TEST(test_error_E2064_field_func_conflict);
    RUN_TEST(test_error_E2065_enum_variant_same_as_type);
    RUN_TEST(test_error_E2066_struct_field_same_as_type);
    RUN_TEST(test_error_E2067_empty_struct);
    RUN_TEST(test_error_E3047_enum_no_member);
    RUN_TEST(test_error_E3048_string_plus);
    RUN_TEST(test_error_E3057_invalid_map_key);
    RUN_TEST(test_error_E3059_const_map);
    RUN_TEST(test_error_E3061_recursive_struct);
    RUN_TEST(test_valid_recursive_pointer_struct);
    RUN_TEST(test_error_E3063_return_addr_local);
    RUN_TEST(test_error_E3072_return_nil_non_pointer);
    RUN_TEST(test_error_E3073_return_in_main);
    RUN_TEST(test_error_E3074_array_compare);
    RUN_TEST(test_error_E3076_map_compare);
    RUN_TEST(test_error_E3078_pointer_arithmetic);
    RUN_TEST(test_error_E3080_named_return_mismatch);
    RUN_TEST(test_error_E3082_wildcard_named_return);
    RUN_TEST(test_error_E4008_main_with_params);
    RUN_TEST(test_error_E5025_invalid_assign_target);
    RUN_TEST(test_error_E3083_c_string_non_pointer);

    /* Batch 2: 84 untested E3xxx error codes (#2098) */
    RUN_TEST(test_error_E3050_array_no_type_annotation);
    RUN_TEST(test_error_E3051_map_no_type_annotation);
    RUN_TEST(test_error_E3052_fixed_array_too_many);
    RUN_TEST(test_error_E3053_array_element_type_mismatch);
    RUN_TEST(test_error_E3054_mut_array_fixed_size);
    RUN_TEST(test_error_E3055_const_array_no_size);
    RUN_TEST(test_error_E3077_struct_compare);
    RUN_TEST(test_error_E3079_mut_ref_to_const);
    RUN_TEST(test_error_E3092_nil_compare_non_nullable);
    RUN_TEST(test_error_E3096_negate_unsigned);
    RUN_TEST(test_error_E3098_struct_assign_mismatch);
    RUN_TEST(test_error_E3120_pointer_ordering);
    RUN_TEST(test_error_E3004_string_index_assign);
    RUN_TEST(test_error_E3031_bare_function_name);
    RUN_TEST(test_error_E3046_int_literal_overflow);
    RUN_TEST(test_error_E3049_enum_arithmetic);
    RUN_TEST(test_error_E3081_bare_function_stmt);
    RUN_TEST(test_error_E3084_type_of_type_name);
    RUN_TEST(test_error_E3085_in_type_mismatch);
    RUN_TEST(test_error_E3090_not_non_bool);
    RUN_TEST(test_error_E3093_arithmetic_on_struct);
    RUN_TEST(test_error_E3095_in_invalid_rhs);
    RUN_TEST(test_error_E3086_fmt_non_string_format);
    RUN_TEST(test_error_E3087_fmt_specifier_n);
    RUN_TEST(test_error_E3088_fmt_arg_type_mismatch);
    RUN_TEST(test_error_E3105_fmt_unknown_specifier);
    RUN_TEST(test_error_E3106_fmt_dangling_percent);
    RUN_TEST(test_error_E3107_fmt_too_few_args);
    RUN_TEST(test_error_E3108_fmt_too_many_args);
    RUN_TEST(test_error_E3111_string_enum_tagged);
    RUN_TEST(test_error_E3112_flags_enum_tagged);
    RUN_TEST(test_error_E3113_tagged_payload_count);
    RUN_TEST(test_error_E3114_plain_variant_called);
    RUN_TEST(test_error_E3115_plain_enum_called);
    RUN_TEST(test_error_E3116_tagged_when_binding_count);
    RUN_TEST(test_error_E3117_enum_int_compare);
    RUN_TEST(test_error_E3118_int_to_enum_assign);
    RUN_TEST(test_error_E3062_const_handle);
    RUN_TEST(test_error_E3064_arena_already_destroyed);
    RUN_TEST(test_error_E3066_func_ref_sig_mismatch);
    RUN_TEST(test_error_E3027_non_assignable_ref_param);
    RUN_TEST(test_error_E3070_nested_ensure);
    RUN_TEST(test_error_E3103_json_struct_func_field);
    RUN_TEST(test_error_E3104_json_struct_method);
    RUN_TEST(test_error_E3109_json_struct_default_value);
    RUN_TEST(test_error_E3056_incomplete_when);
    RUN_TEST(test_error_E3091_non_bool_condition);
    RUN_TEST(test_error_E3121_struct_when_subject);
    RUN_TEST(test_error_E3123_foreach_both_discarded);
    RUN_TEST(test_error_E3129_empty_while_body);
    RUN_TEST(test_error_E3119_fixed_array_in_param);
    RUN_TEST(test_error_E3125_array_size_not_const);
    RUN_TEST(test_error_E3126_array_size_zero);
    RUN_TEST(test_error_E3058_generic_type_error);
    RUN_TEST(test_error_E3060_wildcard_return_no_param);
    RUN_TEST(test_error_E3127_struct_literal_non_struct);
    RUN_TEST(test_E3127_not_reported_for_primitive_type_arg);
    RUN_TEST(test_error_E4016_type_arg_names_no_type);
    RUN_TEST(test_error_E3128_sizeof_variable);
    RUN_TEST(test_error_E3110_implicit_enum_no_context);
    RUN_TEST(test_error_E3124_tagged_enum_equality);
    RUN_TEST(test_error_E3071_return_nil_wildcard_ptr);
    RUN_TEST(test_error_E3075_chain_struct_calls);
    RUN_TEST(test_error_E3089_fallible_no_error_handling);
    RUN_TEST(test_error_E3094_array_index_assign_type);
    RUN_TEST(test_error_E3099_reserved_struct_name);
    RUN_TEST(test_error_E3100_type_as_func_arg);
    RUN_TEST(test_error_E3101_mut_func_ref);
    RUN_TEST(test_error_E3102_func_return_to_var);
    RUN_TEST(test_error_E3122_addr_const_var);
    RUN_TEST(test_error_E3097_addr_scope_mismatch);

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
