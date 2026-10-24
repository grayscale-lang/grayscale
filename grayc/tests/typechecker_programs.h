/*
 * typechecker_programs.h — One minimal program per diagnostic code the parser
 * and typechecker emit, each paired with the code it must produce. Used by
 * test_typechecker.c; the sources are kept in the same shape as the files
 * under integration-tests/ they were taken from.
 *
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_TYPECHECKER_PROGRAMS_H
#define GRAYC_TYPECHECKER_PROGRAMS_H

typedef struct {
    const char *name;
    const char *expected_code;
    const char *source;
} DiagnosticProgram;

static const DiagnosticProgram diagnostic_programs[] = {
    {
        "E1003_unclosed_comment",
        "E1003",
        "/*\n"
        " * Error Test: E1003 - unclosed-comment\n"
        " * Expected: \"unclosed multi-line comment\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    /* This comment is never closed\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "E1005_unclosed_char",
        "E1005",
        "/*\n"
        " * Error Test: E1005 - unclosed character literal\n"
        " * Expected: \"unclosed character; add a closing single quote\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut c char = 'a\n"
        "\n"
    },
    {
        "E1006_unicode_escape_empty",
        "E1006",
        "/*\n"
        " * Error Test: E1006 - unicode escape with no digits\n"
        " */\n"
        "// expect-error: E1006\n"
        "\n"
        "do main() {\n"
        "    println(\"\\u{}\")\n"
        "}\n"
        "\n"
    },
    {
        "E1007_invalid_escape_char",
        "E1007",
        "/*\n"
        " * Error Test: E1007 - invalid-escape-char\n"
        " * Expected: \"invalid escape sequence\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut c char = '\\q'  // \\q is not valid escape\n"
        "}\n"
        "\n"
    },
    {
        "E1010_invalid_number_format",
        "E1010",
        "/*\n"
        " * Error Test: E1010 - invalid-number-format\n"
        " * Expected: \"invalid\" or \"number\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 0x  // hex without digits\n"
        "}\n"
        "\n"
    },
    {
        "E1011_consecutive_underscores",
        "E1011",
        "/*\n"
        " * Error Test: E1011 - number-consecutive-underscores\n"
        " * Expected: \"consecutive underscores\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 1__000  // consecutive underscores\n"
        "}\n"
        "\n"
    },
    {
        "E1012_leading_underscore",
        "E1012",
        "/*\n"
        " * Error Test: E1012 - number-leading-underscore\n"
        " * Expected: \"leading underscore\" or \"starts with underscore\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = _1000  // leading underscore\n"
        "}\n"
        "\n"
    },
    {
        "E1013_trailing_underscore",
        "E1013",
        "/*\n"
        " * Error Test: E1013 - number-trailing-underscore\n"
        " * Expected: \"trailing underscore\" or \"ends with underscore\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 1000_  // trailing underscore\n"
        "}\n"
        "\n"
    },
    {
        "E1014_underscore_before_decimal",
        "E1014",
        "/*\n"
        " * Error Test: E1014 - number-underscore-before-decimal\n"
        " * Expected: \"underscore before decimal\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = 1_.5  // underscore before decimal\n"
        "}\n"
        "\n"
    },
    {
        "E1015_underscore_after_decimal",
        "E1015",
        "/*\n"
        " * Error Test: E1015 - number-underscore-after-decimal\n"
        " * Expected: \"underscore after decimal\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = 1._5  // underscore after decimal\n"
        "}\n"
        "\n"
    },
    {
        "E1016_trailing_decimal",
        "E1016",
        "/*\n"
        " * Error Test: E1016 - number-trailing-decimal\n"
        " * Expected: \"decimal\" or \"trailing\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = 1.  // trailing decimal\n"
        "}\n"
        "\n"
    },
    {
        "E1017_unclosed_raw_string",
        "E1017",
        "/*\n"
        " * Error Test: E1017 - unclosed-raw-string\n"
        " * Expected: \"unclosed raw string literal\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s string = `this raw string never ends\n"
        "}\n"
        "\n"
    },
    {
        "E1018_empty_char",
        "E1018",
        "/*\n"
        " * Error Test: E1018 - empty-char-literal\n"
        " * Expected: \"empty character literal\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut c char = ''  // Empty char literal\n"
        "}\n"
        "\n"
    },
    {
        "E1019_unexpected_hash",
        "E1019",
        "/*\n"
        " * Error Test: E1019 - unexpected '#' character\n"
        " * Expected: \"unexpected character '#'\"\n"
        " */\n"
        "\n"
        "#foo\n"
        "\n"
        "do main() {\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "E1020_illegal_or",
        "E1020",
        "/*\n"
        " * Error Test: E1020 - illegal-or-character\n"
        " * Expected: \"illegal OR character\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x bool = true | false  // Single | is illegal, should be ||\n"
        "}\n"
        "\n"
    },
    {
        "E1021_unclosed_interpolation",
        "E1021",
        "/*\n"
        " * Error Test: E1021 - unclosed-interpolation\n"
        " * Expected: \"interpolation not closed\" or \"${\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut name string = \"world\"\n"
        "    mut s string = \"hello ${name\"\n"
        "}\n"
        "\n"
    },
    {
        "E1022_unexpected_character",
        "E1022",
        "/*\n"
        " * Error Test: E1022 - unexpected character\n"
        " * Expected: \"unexpected character\"\n"
        " * Tests that an unknown character gets a specific diagnostic\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 42 ~ 3  // tilde is not a valid operator\n"
        "}\n"
        "\n"
    },
    {
        "E1023_unclosed_string",
        "E1023",
        "/*\n"
        " * Error Test: E1023 - string literal spans multiple lines\n"
        " */\n"
        "\n"
        "// expect-error: E1023\n"
        "\n"
        "do main() {\n"
        "    mut s string = \"this string never ends\n"
        "}\n"
        "\n"
    },
    {
        "E1024_identifier_too_long",
        "E1024",
        "/*\n"
        " * Error Test: E1024 - identifier-too-long\n"
        " * Expected: \"identifier exceeds the maximum length\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa i64 = 1\n"
        "}\n"
        "\n"
    },
    {
        "E1025_question_mark_wildcard",
        "E1025",
        "/*\n"
        " * Error Test: E1025 - '?' is not part of the language. A type parameter is\n"
        " * declared with 'generic'.\n"
        " * Expected: \"this symbol is not part of the language\"\n"
        " */\n"
        "// expect-error: E1025\n"
        "// expect-error-count: 1\n"
        "\n"
        "do identity(x ?) {\n"
        "    println(x)\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E12007_maps_find_key_composite_value",
        "E12007",
        "/*\n"
        " * Error Test: E12007 - find_key does not support composite value types\n"
        " */\n"
        "\n"
        "// expect-error: E12007\n"
        "\n"
        "import @maps\n"
        "\n"
        "do main() {\n"
        "    mut groups map[string:[i64]] = {\"a\": {1, 2}}\n"
        "    mut key, found = maps.find_key(groups, {1, 2})\n"
        "    println(key)\n"
        "    println(found)\n"
        "}\n"
        "\n"
    },
    {
        "E12008_maps_sorted_keys_f64_key",
        "E12008",
        "/*\n"
        " * Error Test: E12008 - sorted_keys needs string, integer, or char keys\n"
        " */\n"
        "\n"
        "// expect-error: E12008\n"
        "\n"
        "import @maps\n"
        "\n"
        "do main() {\n"
        "    mut weights map[f64:i64] = {1.5: 1}\n"
        "    println(maps.sorted_keys(weights))\n"
        "}\n"
        "\n"
    },
    {
        "E2001_unclosed_bracket",
        "E2001",
        "/*\n"
        " * Error Test: E2001 - unclosed-bracket\n"
        " * Expected: \"missing closing bracket\" or \"]\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3\n"
        "    mut y i64 = arr[0\n"
        "}\n"
        "\n"
    },
    {
        "E2002_interpolation_incomplete_expr",
        "E2002",
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    mut s string = \"${x +}\"\n"
        "    println(s)\n"
        "}\n"
        "\n"
    },
    {
        "E2011_multi_declare_const_no_value",
        "E2011",
        "/*\n"
        " * Error Test: E2011 - const names declared with no value\n"
        " */\n"
        "// expect-error: E2011\n"
        "\n"
        "do main() {\n"
        "    const A, B i64\n"
        "}\n"
        "\n"
    },
    {
        "E2012_duplicate_param",
        "E2012",
        "/*\n"
        " * Error Test: E2012 - duplicate-parameter\n"
        " * Expected: \"duplicate parameter name\"\n"
        " */\n"
        "\n"
        "do foo(x i64, x string) {  // Duplicate param name\n"
        "    println(\"test\")\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    foo(1, \"test\")\n"
        "}\n"
        "\n"
    },
    {
        "E2013_duplicate_struct_field",
        "E2013",
        "/*\n"
        " * Error Test: E2013 - duplicate field name in struct\n"
        " * Expected: \"duplicate field name 'x' in struct 'Point'\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    x i64\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2014_duplicate_enum_variant",
        "E2014",
        "/*\n"
        " * Error Test: E2014 - duplicate enum variant name\n"
        " * Expected: \"duplicate variant name\" error\n"
        " */\n"
        "\n"
        "const Direction enum {\n"
        "    NORTH\n"
        "    SOUTH\n"
        "    EAST\n"
        "    NORTH\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut d = Direction.NORTH\n"
        "}\n"
        "\n"
    },
    {
        "E2015_duplicate_struct_literal_field",
        "E2015",
        "/*\n"
        " * Error Test: E2015 - duplicate field in struct literal\n"
        " * Expected: \"duplicate field\" error\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = Point{x: 1, y: 2, x: 3}\n"
        "}\n"
        "\n"
    },
    {
        "E2016_empty_enum",
        "E2016",
        "/*\n"
        " * Error Test: E2016 - empty-enum\n"
        " * Expected: \"enum must have at least one value\"\n"
        " */\n"
        "\n"
        "const Status enum {\n"
        "    // Empty enum - no values\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"test\")\n"
        "}\n"
        "\n"
    },
    {
        "E2017_trailing_comma",
        "E2017",
        "/*\n"
        " * Error Test: E2017 - trailing-comma-array\n"
        " * Expected: \"trailing comma\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3,}  // Trailing comma\n"
        "}\n"
        "\n"
    },
    {
        "E2025_invalid_array_size",
        "E2025",
        "/*\n"
        " * Error Test: E2025 - invalid-array-size\n"
        " * Expected: \"expected integer for array size\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64, \"ten\"] = {1, 2, 3}  // String instead of i64 size\n"
        "}\n"
        "\n"
    },
    {
        "E2036_import_inside_block",
        "E2036",
        "/*\n"
        " * Error Test: E2036 - import-inside-block\n"
        " * Expected: \"import\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    import and use @math  // imports not allowed inside blocks\n"
        "    println(\"test\")\n"
        "}\n"
        "\n"
    },
    {
        "E2037_struct_duplicate_function",
        "E2037",
        "/*\n"
        " * Error Test: E2037 - struct declares two functions with the same name\n"
        " * Expected: \"duplicate function 'bump' in struct 'Counter'\" error\n"
        " */\n"
        "// expect-error: E2037\n"
        "// expect-error-count: 1\n"
        "\n"
        "const Counter struct {\n"
        "    value i64\n"
        "\n"
        "    do bump() {\n"
        "    }\n"
        "\n"
        "    do bump(amount i64) {\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"unused\")\n"
        "}\n"
        "\n"
    },
    {
        "E2038_reserved_type_as_variable",
        "E2038",
        "/*\n"
        " * Error Test: E2038 - reserved type name used as variable name\n"
        " * Expected: \"'i64' is a reserved type name\" error\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut i64 = 42\n"
        "}\n"
        "\n"
    },
    {
        "E2039_required_after_default",
        "E2039",
        "/*\n"
        " * Error Test: E2039 - required parameter after parameter with default value\n"
        " * Expected: compiler should reject this with E2039\n"
        " */\n"
        "\n"
        "do bad_func(x i64 = 10, y i64) {\n"
        "    // This should fail - y is required but comes after x which has a default\n"
        "    println(x + y)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    bad_func(5, 10)\n"
        "}\n"
        "\n"
    },
    {
        "E2043_duplicate_case_constant",
        "E2043",
        "/*\n"
        " * Error Test: E2043 - a when case repeats an earlier case written as a\n"
        " * negative integer\n"
        " * Expected: compile-time error on the second case\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut value = -1\n"
        "    when value {\n"
        "        is -1 { println(\"first\") }\n"
        "        is -1 { println(\"second\") }\n"
        "        default { }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E2050_continue_outside_loop",
        "E2050",
        "/*\n"
        " * Error Test: E2050 - continue-outside-loop\n"
        " * Expected: \"continue statement outside loop\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    continue  // continue outside of any loop\n"
        "}\n"
        "\n"
    },
    {
        "E2051_nested_function",
        "E2051",
        "/*\n"
        " * Error Test: E2051 - nested-function\n"
        " * Expected: \"nested\" or \"function inside function\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    do inner() {  // nested function not allowed\n"
        "        println(\"nested\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E2053_alias_inside_function",
        "E2053",
        "/*\n"
        " * Error Test: E2053 - alias inside function\n"
        " * Expected: alias must be at file scope\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    alias Meters = f64\n"
        "}\n"
        "\n"
    },
    {
        "E2056_function_call_at_file_scope",
        "E2056",
        "/*\n"
        " * Error Test: E2056 - executable-at-file-scope\n"
        " * Expected: \"file scope\" or \"not allowed\"\n"
        " */\n"
        "\n"
        "\n"
        "println(\"function call at file scope\")\n"
        "\n"
        "do main() {\n"
        "    println(\"main\")\n"
        "}\n"
        "\n"
    },
    {
        "E2057_invalid_interpolation_syntax",
        "E2057",
        "/*\n"
        " * Error Test: E2057 - invalid-interpolation-syntax\n"
        " * Expected: \"invalid interpolation syntax\" or \"$identifier\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut file string = \"test.gray\"\n"
        "    mut s string = \"$File: something\"\n"
        "}\n"
        "\n"
    },
    {
        "E2058_nested_struct_in_enum",
        "E2058",
        "// Test: E2058 - nested struct declaration inside enum\n"
        "// Expected: \"cannot declare a struct or enum\"\n"
        "\n"
        "const Color enum {\n"
        "    Red\n"
        "    const Point struct {\n"
        "        x i64\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"should not compile\")\n"
        "}\n"
        "\n"
    },
    {
        "E2059_empty_when",
        "E2059",
        "// Test: E2059 - empty when block\n"
        "// Expected: \"when block is empty\"\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    when x {\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E2060_too_many_return_values",
        "E2060",
        "/* Test: E2060 — too many return values (exceeds 16 limit)\n"
        " * Expected: compiler error E2060\n"
        " */\n"
        "\n"
        "\n"
        "do too_many() -> (a i64, b i64, c i64, d i64, e i64, f i64, g i64, h i64, i i64, j i64, k i64, l i64, m i64, n i64, o i64, p i64, q i64) {\n"
        "    return a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q = too_many()\n"
        "    println(a)\n"
        "}\n"
        "\n"
    },
    {
        "E2062_too_many_multi_vars",
        "E2062",
        "/*\n"
        " * Error Test: E2062 - too many variables in multi-variable declaration\n"
        " * Expected: \"too many variables in multi-variable declaration; maximum is 16\"\n"
        " */\n"
        "\n"
        "do foo() -> i64 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q = foo()\n"
        "}\n"
        "\n"
    },
    {
        "E2063_duplicate_named_return",
        "E2063",
        "/*\n"
        " * Error Test: E2063 - duplicate named return value\n"
        " * Expected: \"duplicate named return value 'result'\" error\n"
        " */\n"
        "\n"
        "do compute(x i64) -> (result i64, result i64) {\n"
        "    return x, x * 2\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a, b = compute(5)\n"
        "}\n"
        "\n"
    },
    {
        "E2064_struct_func_shadows_field",
        "E2064",
        "/*\n"
        " * Error Test: E2064 - struct function name conflicts with a field name\n"
        " * Expected: \"function 'x' conflicts with field 'x' in struct 'Point'\" error\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "\n"
        "    do x() -> i64 {\n"
        "        return 0\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = new(Point)\n"
        "}\n"
        "\n"
    },
    {
        "E2065_enum_variant_shadows_type",
        "E2065",
        "/*\n"
        " * Error Test: E2065 - enum variant has same name as its enum type\n"
        " * Expected: \"enum variant 'Color' cannot have the same name as its enum type 'Color'\" error\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    Color\n"
        "    RED\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c = Color.RED\n"
        "}\n"
        "\n"
    },
    {
        "E2066_struct_field_shadows_type",
        "E2066",
        "/*\n"
        " * Error Test: E2066 - struct field has same name as its struct type\n"
        " * Expected: \"struct field 'Foo' cannot have the same name as its struct type 'Foo'\" error\n"
        " */\n"
        "\n"
        "const Foo struct {\n"
        "    Foo i64\n"
        "    bar string\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut f = new(Foo)\n"
        "}\n"
        "\n"
    },
    {
        "E2067_empty_struct",
        "E2067",
        "/*\n"
        " * Error Test: E2067 - empty struct declaration\n"
        " * Expected: \"struct has no fields\" error\n"
        " */\n"
        "\n"
        "const Empty struct {\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut e = new(Empty)\n"
        "}\n"
        "\n"
    },
    {
        "E2068_mut_enum",
        "E2068",
        "/*\n"
        " * Error Test: E2068 - enum declared with mut instead of const\n"
        " * Expected: \"enums must be declared with 'const', not 'mut'\" error\n"
        " */\n"
        "\n"
        "mut Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c = Color.RED\n"
        "}\n"
        "\n"
    },
    {
        "E2069_enum_variants_same_line_no_separator",
        "E2069",
        "/*\n"
        " * Error Test: E2069 - enum variants on same line without a separator\n"
        " * Expected: error requiring a semicolon\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"${Color.RED}\")\n"
        "}\n"
        "\n"
    },
    {
        "E2071_whitespace_string_interpolation",
        "E2071",
        "/*\n"
        " * Error Test: E2071 - whitespace-only string interpolation ${ }\n"
        " *\n"
        " * whitespace-only interpolation is treated\n"
        " * the same as empty and rejected with E2071.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s string = \"value: ${ }\"\n"
        "}\n"
        "\n"
    },
    {
        "E2072_ampersand_i64",
        "E2072",
        "/*\n"
        " * Error Test: E2072 - '&' used as unary address-of on i64\n"
        " * Expected: E2072 error directing user to use addr()\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 42\n"
        "    mut p ^i64 = &x\n"
        "}\n"
        "\n"
    },
    {
        "E2073_call_with_space_before_paren",
        "E2073",
        "/*\n"
        " * Error Test: E2073 - call-whitespace-before-paren\n"
        " * Expected: \"function calls cannot have whitespace between the name and the opening parenthesis\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    println (\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "E2074_whitespace_before_dot",
        "E2074",
        "/*\n"
        " * Error Test: E2074 - whitespace before dot in member access\n"
        " * Expected: \"member access cannot have whitespace before the dot\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = Point{x: 1, y: 2}\n"
        "    mut val = p .x\n"
        "}\n"
        "\n"
    },
    {
        "E2075_whitespace_before_bracket",
        "E2075",
        "/*\n"
        " * Error Test: E2075 - whitespace before opening bracket\n"
        " * Expected: \"index expressions cannot have whitespace before the opening bracket\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64] = {10, 20, 30}\n"
        "    mut val = arr [0]\n"
        "}\n"
        "\n"
    },
    {
        "E2076_whitespace_before_postfix",
        "E2076",
        "/*\n"
        " * Error Test: E2076 - whitespace before postfix operator\n"
        " * Expected: \"postfix operators cannot have whitespace before them\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 0\n"
        "    x ++\n"
        "}\n"
        "\n"
    },
    {
        "E2077_empty_index_expression",
        "E2077",
        "// Error Test: E2077 - an index expression needs an expression between the brackets\n"
        "// expect-error: E2077\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3}\n"
        "    mut x i64 = arr[]\n"
        "}\n"
        "\n"
    },
    {
        "E2079_nil_as_return_type",
        "E2079",
        "/*\n"
        " * Error Test: E2079 - nil used as a type\n"
        " * Expected: \"'nil' is a value, not a type\"\n"
        " */\n"
        "\n"
        "do foo() -> nil {\n"
        "    return nil\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    foo()\n"
        "}\n"
        "\n"
    },
    {
        "E2081_caret_after_type",
        "E2081",
        "/*\n"
        " * Error Test: E2081 - caret after type name instead of before\n"
        " * Expected: \"'^' is a dereference operator, not a type modifier\"\n"
        " */\n"
        "\n"
        "do foo() -> i64^ {\n"
        "    mut x i64 = 42\n"
        "    return addr(x)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    foo()\n"
        "}\n"
        "\n"
    },
    {
        "E2082_typed_func_array",
        "E2082",
        "do double(n i64) -> i64 { return n * 2 }\n"
        "do main() {\n"
        "    const fns [func(i64) -> i64] = {}\n"
        "}\n"
        "\n"
    },
    {
        "E2083_payload_and_explicit_value",
        "E2083",
        "/*\n"
        " * Error Test: E2083 - enum variant with both payload and explicit value\n"
        " * Expected: \"enum variant 'RED' cannot have both a payload and an explicit value\"\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED(i64) = 5\n"
        "    GREEN\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2084_blank_ident_missing_eq",
        "E2084",
        "/*\n"
        " * Error Test: E2084 - blank identifier missing '='\n"
        " * Expected: error about missing '=' after '_'\n"
        " */\n"
        "\n"
        "do foo() -> i64 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut _ foo()\n"
        "}\n"
        "\n"
    },
    {
        "E2085_when_duplicate_default",
        "E2085",
        "/* Error Test: E2085 - when statement with multiple default branches */\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    when x {\n"
        "        is 1 { println(\"one\") }\n"
        "        default { println(\"default1\") }\n"
        "        default { println(\"default2\") }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E2086_in_missing_lhs",
        "E2086",
        "/*\n"
        " * Error Test: E2086 - 'in' used without a left-hand value\n"
        " * Expected: \"'in' requires a value on the left side\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    if in range(0, 10) {\n"
        "        println(\"test\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E2088_mixed_do_alias",
        "E2088",
        "/* Error Test: E2088 - mixed do/fn aliases in same file */\n"
        "// expect-error: E2088\n"
        "do helper() -> i64 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "fn main() {\n"
        "    println(helper())\n"
        "}\n"
        "\n"
    },
    {
        "E2089_discard_on_struct_field",
        "E2089",
        "const Foo struct {\n"
        "    #discard\n"
        "    x i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut f Foo = Foo{x: 1}\n"
        "    println(f.x)\n"
        "}\n"
        "\n"
    },
    {
        "E2090_dup_test",
        "E2090",
        "// expect-error: E2090\n"
        "// expect-error-count: 1\n"
        "// Duplicate #test on a top-level function.\n"
        "\n"
        "#test\n"
        "#test\n"
        "do check_thing() {\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2091_unknown_attribute_in_list",
        "E2091",
        "// expect-error: E2091\n"
        "// expect-error-count: 1\n"
        "// An unrecognised attribute name inside a #[...] list.\n"
        "\n"
        "#[test, bogus]\n"
        "do something() -> i64 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2092_attribute_list_multiline",
        "E2092",
        "// expect-error: E2092\n"
        "// expect-error-count: 1\n"
        "// A #[...] attribute list may not span more than one physical line.\n"
        "\n"
        "#[test,\n"
        "  discard]\n"
        "do something() -> i64 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2093_trailing_comma_attribute_list",
        "E2093",
        "// expect-error: E2093\n"
        "// expect-error-count: 1\n"
        "// A trailing comma before ']' in a #[...] attribute list.\n"
        "\n"
        "#[test,]\n"
        "do something() -> i64 {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2094_define_on_function",
        "E2094",
        "/*\n"
        " * Error Test: E2094 - #define on a function\n"
        " */\n"
        "\n"
        "// expect-error: E2094\n"
        "\n"
        "#define\n"
        "do helper() { }\n"
        "\n"
        "do main() {\n"
        "    helper()\n"
        "}\n"
        "\n"
    },
    {
        "E2095_json_tag_grouped_fields",
        "E2095",
        "// expect-error: E2095\n"
        "// expect-error-count: 1\n"
        "// A field tag applied to a comma-grouped field list.\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const Pair struct {\n"
        "    x, y i64 `json:\"V\"`\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2096_generic_in_global",
        "E2096",
        "/*\n"
        " * Error Test: E2096 - 'generic' is not a global variable type.\n"
        " */\n"
        "// expect-error: E2096\n"
        "// expect-error-count: 1\n"
        "\n"
        "const limit generic = 10\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2097_generic_default_value",
        "E2097",
        "/*\n"
        " * Error Test: E2097 - a 'generic' parameter's argument is the type the caller\n"
        " * passes, so it cannot have a default value.\n"
        " */\n"
        "// expect-error: E2097\n"
        "// expect-error-count: 1\n"
        "\n"
        "do make(kind generic = i64) {\n"
        "    println(\"make\")\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2098_import_after_declaration",
        "E2098",
        "const MY_ARRAY [i64, 5] = {1, 2, 3, 4, 5}\n"
        "\n"
        "do some_function() {\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
        "import @arrays\n"
        "\n"
        "do main() {\n"
        "    println(MY_ARRAY)\n"
        "}\n"
        "\n"
    },
    {
        "E2099_mutable_parameter_default_value",
        "E2099",
        "/*\n"
        " * Error Test: E2099 - a mutable (&) parameter modifies the caller's variable,\n"
        " * so it cannot have a default value.\n"
        " */\n"
        "// expect-error: E2099\n"
        "// expect-error-count: 1\n"
        "\n"
        "do f(&x i64 = 5) {\n"
        "    x += 1\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E2100_multi_declare_value_count_mismatch",
        "E2100",
        "/*\n"
        " * Error Test: E2100 - three names, two values\n"
        " */\n"
        "// expect-error: E2100\n"
        "\n"
        "do main() {\n"
        "    x, y, z i64 = 1, 2\n"
        "}\n"
        "\n"
    },
    {
        "E2101_multi_declare_per_name_types_with_values",
        "E2101",
        "/*\n"
        " * Error Test: E2101 - per-name types with a list of values\n"
        " */\n"
        "// expect-error: E2101\n"
        "\n"
        "do main() {\n"
        "    x i64, y f64 = 1, 2.0\n"
        "}\n"
        "\n"
    },
    {
        "E2102_or_default_bare_statement",
        "E2102",
        "/*\n"
        " * Error Test: E2102 - or_default with no binding\n"
        " */\n"
        "// expect-error: E2102\n"
        "\n"
        "do lookup() -> (i64, Error) {\n"
        "    return 1, nil\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    lookup() or_default 1\n"
        "}\n"
        "\n"
    },
    {
        "E3001_map_element_type_mismatch",
        "E3001",
        "do main() {\n"
        "    mut m map[string:i64] = {:}\n"
        "    m[\"f\"] = 2.9\n"
        "    println(m)\n"
        "}\n"
        "\n"
    },
    {
        "E3002_compound_assign_bool",
        "E3002",
        "/*\n"
        " * Error Test: E3002 - compound-assign-bool\n"
        " * Expected: \"invalid operands: cannot use '+=' with bool and bool\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut b bool = true\n"
        "    b += true\n"
        "}\n"
        "\n"
    },
    {
        "E3003_invalid_index_type",
        "E3003",
        "/*\n"
        " * Error Test: E3003 - invalid-index-type\n"
        " * Expected: \"index must be an integer\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3}\n"
        "    mut x i64 = arr[\"foo\"]  // String index on array\n"
        "}\n"
        "\n"
    },
    {
        "E3004_invalid_index_assignment",
        "E3004",
        "/*\n"
        " * Error Test: E3004 - string index assignment not supported\n"
        " * Expected: strings are not element-assignable\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s string = \"hello\"\n"
        "    s[0] = 5\n"
        "}\n"
        "\n"
    },
    {
        "E3005_const_reassign",
        "E3005",
        "/*\n"
        " * Error Test: E3005 - immutable-variable\n"
        " * Expected: \"cannot assign to immutable\" or \"const\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    const x i64 = 5\n"
        "    x = 10  // Cannot reassign const\n"
        "}\n"
        "\n"
    },
    {
        "E3006_multi_return_too_many_vars",
        "E3006",
        "/*\n"
        " * Error Test: E3006 - more variables than function returns\n"
        " * Expected: \"too many variables\" error\n"
        " */\n"
        "\n"
        "do pair() -> (i64, i64) {\n"
        "    return 1, 2\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a, b, c = pair()\n"
        "}\n"
        "\n"
    },
    {
        "E3007_negate_string",
        "E3007",
        "/*\n"
        " * Error Test: E3007 - cannot negate non-numeric type\n"
        " * Expected: \"cannot negate type 'string'\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s string = \"hello\"\n"
        "    mut x = -s\n"
        "}\n"
        "\n"
    },
    {
        "E3008_not_indexable",
        "E3008",
        "/*\n"
        " * Error Test: E3008 - not-indexable\n"
        " * Expected: \"type 'i64' does not support indexing; only arrays, maps, and strings can be indexed\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    x[0] = 10  // Trying to index an integer\n"
        "}\n"
        "\n"
    },
    {
        "E3009_not_iterable",
        "E3009",
        "/*\n"
        " * Error Test: E3009 - not-iterable\n"
        " * Expected: \"cannot iterate over type 'i64'; for_each requires an array, map, or string\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    for_each item in x {  // Cannot iterate over an integer\n"
        "        println(item)\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3010_deref_nonexistent_field",
        "E3010",
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = new(Point)\n"
        "    p^.z = 42\n"
        "}\n"
        "\n"
    },
    {
        "E3011_type_name_as_value",
        "E3011",
        "/*\n"
        " * Error Test: E3011 - type name used as value\n"
        " * Expected: \"is a type, not a value\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x = i64\n"
        "}\n"
        "\n"
    },
    {
        "E3012_addr_type_name",
        "E3012",
        "/*\n"
        " * Error Test: E3012 - addr() rejects type names as arguments\n"
        " * Expected: \"addr() requires a variable\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut p = addr(string)\n"
        "}\n"
        "\n"
    },
    {
        "E3013_member_access_invalid_type2",
        "E3013",
        "/*\n"
        " * Error Test: E3013 - member-access-invalid-type\n"
        " * Expected: \"not a struct\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s = \"hello\"\n"
        "    println(s.name)  // strings don't have members\n"
        "}\n"
        "\n"
    },
    {
        "E3015_not_callable",
        "E3015",
        "/*\n"
        " * Error Test: E3015 - not-callable\n"
        " * Expected: \"not a function\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    x()  // Trying to call an integer\n"
        "}\n"
        "\n"
    },
    {
        "E3016_dereference_non_pointer",
        "E3016",
        "/*\n"
        " * Error Test: E3016 - dereference non-pointer\n"
        " * Expected: \"cannot dereference non-pointer type\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 42\n"
        "    mut y i64 = x^\n"
        "}\n"
        "\n"
    },
    {
        "E3017_fmt_printf_array",
        "E3017",
        "/*\n"
        " * Error Test: E3017 - fmt.printf cannot format array\n"
        " * Expected: \"cannot format value of type\"\n"
        " */\n"
        "// expect-error: E3017\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    mut nums [i64] = {1, 2, 3}\n"
        "    fmt.printf(\"%s\", {nums})\n"
        "}\n"
        "\n"
    },
    {
        "E3018_when_i64_vs_string",
        "E3018",
        "/*\n"
        " * Error Test: E3018 - when/is type mismatch i64 vs string\n"
        " * Expected: \"type mismatch in 'when'\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    when x {\n"
        "        is \"hello\" { }\n"
        "        default { }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3019_reassign_signed_to_unsigned",
        "E3019",
        "do main() {\n"
        "    mut a u64 = cast(10, u64)\n"
        "    mut b i64 = 42\n"
        "    a = b\n"
        "}\n"
        "\n"
    },
    {
        "E3024_missing_return_statement",
        "E3024",
        "/*\n"
        " * Error Test: E3024 - missing-return-statement\n"
        " * Expected: \"missing return\" or \"must return\"\n"
        " */\n"
        "\n"
        "do getValue() -> i64 {\n"
        "    mut x i64 = 5\n"
        "    // missing return statement\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut v i64 = getValue()\n"
        "}\n"
        "\n"
    },
    {
        "E3027_stdlib_builder_mutable_parameter_temporary",
        "E3027",
        "import @strings\n"
        "\n"
        "do main() {\n"
        "    strings.builder_append(strings.builder(), \"x\")\n"
        "}\n"
        "\n"
    },
    {
        "E3031_struct_function_as_value",
        "E3031",
        "const Counter struct {\n"
        "    n i64\n"
        "    do inc(&c Counter) { c.n = c.n + 1 }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut f = Counter.inc\n"
        "}\n"
        "\n"
    },
    {
        "E3032_cross_enum_comparison",
        "E3032",
        "/*\n"
        " * Error Test: E3032 - comparing values from different enum types\n"
        " * Expected: type mismatch for cross-enum comparison\n"
        " */\n"
        "// expect-error: E3032\n"
        "// expect-error-count: 1\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "}\n"
        "\n"
        "const Dir enum {\n"
        "    NORTH\n"
        "    SOUTH\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c Color = Color.RED\n"
        "    mut d Dir = Dir.NORTH\n"
        "    if c == d {\n"
        "        println(\"same\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3033_negative_values",
        "E3033",
        "/* Error Test: E3033 - two variants share a negative value */\n"
        "const E enum {\n"
        "    A = -1\n"
        "    B = -1\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"${E.A}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3034_any_type_not_allowed",
        "E3034",
        "/*\n"
        " * Error Test: E3034 - any-type-not-allowed\n"
        " * Expected: \"'any' type is reserved for internal use\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x any = 42  // 'any' type is reserved\n"
        "}\n"
        "\n"
    },
    {
        "E3035_loop_break_return_missing",
        "E3035",
        "do pick(x i64) -> i64 {\n"
        "    loop {\n"
        "        if x > 3 {\n"
        "            break\n"
        "        }\n"
        "        return 1\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(pick(5))\n"
        "}\n"
        "\n"
    },
    {
        "E3036_binary_encode_u8_out_of_range",
        "E3036",
        "import @binary\n"
        "\n"
        "do main() {\n"
        "    println(binary.encode_u8(300))\n"
        "}\n"
        "\n"
    },
    {
        "E3038_void_variable_type",
        "E3038",
        "/*\n"
        " * Error Test: E3038 - void used as a variable type\n"
        " */\n"
        "// expect-error: E3038\n"
        "\n"
        "do main() {\n"
        "    mut x void\n"
        "}\n"
        "\n"
    },
    {
        "E3039_ensure_expects_call",
        "E3039",
        "/*\n"
        " * Error Test: E3039 - ensure-expects-call\n"
        " * Expected: \"ensure expects a function call\"\n"
        " */\n"
        "\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    ensure x  // ERROR: ensure expects a function call, not an identifier\n"
        "}\n"
        "\n"
    },
    {
        "E3040_multi_return_in_if_condition",
        "E3040",
        "// Multi-return function cannot appear as if condition\n"
        "do two() -> (i64, bool) { return 10, true }\n"
        "do main() {\n"
        "    if two() { println(\"true\") }\n"
        "}\n"
        "\n"
    },
    {
        "E3041_tagged_enum_in_interpolation",
        "E3041",
        "/*\n"
        " * Error Test: E3041 - tagged enum in string interpolation\n"
        " *\n"
        " * \"${r}\" where r is a tagged enum used to fall through codegen's\n"
        " * default %lld + (long long) cast and produce a raw clang diagnostic.\n"
        " * Typechecker now rejects at the interpolation site.\n"
        " */\n"
        "\n"
        "const Result enum {\n"
        "    Ok(i64)\n"
        "    Err(string)\n"
        "    None\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut r Result = .Ok(42)\n"
        "    println(\"result: ${r}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3043_bool_of_nil",
        "E3043",
        "do main() {\n"
        "    mut value bool = bool(nil)\n"
        "    println(value)\n"
        "}\n"
        "\n"
    },
    {
        "E3044_field_on_type_name",
        "E3044",
        "/*\n"
        " * Error Test: E3044 - field access on struct type name\n"
        " * Expected: \"cannot access field\" and \"use an instance variable\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut val i64 = Point.x\n"
        "}\n"
        "\n"
    },
    {
        "E3045_or_default_not_fallible",
        "E3045",
        "/*\n"
        " * Error Test: E3045 - or_default on a call that is not fallible\n"
        " */\n"
        "// expect-error: E3045\n"
        "\n"
        "do plain() -> i64 { return 1 }\n"
        "\n"
        "do main() {\n"
        "    mut x = plain() or_default 1\n"
        "    println(x)\n"
        "}\n"
        "\n"
    },
    {
        "E3046_literal_exceeds_every_integer_type",
        "E3046",
        "/*\n"
        " * Error Test: E3046 - an integer literal no integer type can hold: 2^256 is\n"
        " * one above the largest u256\n"
        " */\n"
        "\n"
        "// expect-error: E3046\n"
        "\n"
        "do main() {\n"
        "    mut x u256 = 115792089237316195423570985008687907853269984665640564039457584007913129639936\n"
        "}\n"
        "\n"
    },
    {
        "E3047_invalid_enum_member",
        "E3047",
        "/*\n"
        " * Error Test: E3047 - nonexistent enum member access\n"
        " * Expected: \"enum 'Color' has no member 'YELLOW'\" error\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c = Color.YELLOW\n"
        "}\n"
        "\n"
    },
    {
        "E3048_string_plus_i64",
        "E3048",
        "/*\n"
        " * Error Test: E3048 - '+' with a string and a non-string operand\n"
        " *\n"
        " * string + string concatenates, but a string mixed with any other\n"
        " * type must be rejected rather than leaking to the C compiler.\n"
        " *\n"
        " * expect-error: E3048\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s string = \"count: \" + 5\n"
        "    println(s)\n"
        "}\n"
        "\n"
    },
    {
        "E3049_enum_arithmetic_add",
        "E3049",
        "/*\n"
        " * Error Test: E3049 - arithmetic on enum values (addition)\n"
        " * Expected: \"cannot use '+' on enum values\" error\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x = Color.RED + Color.GREEN\n"
        "}\n"
        "\n"
    },
    {
        "E3050_array_literal_no_type",
        "E3050",
        "/*\n"
        " * Error Test: E3050 - empty array literal has no elements to infer a type from\n"
        " * Expected: \"array needs a type annotation\"\n"
        " */\n"
        "\n"
        "// expect-error: E3050\n"
        "\n"
        "do main() {\n"
        "    mut x = {}\n"
        "}\n"
        "\n"
    },
    {
        "E3051_map_literal_no_type",
        "E3051",
        "/*\n"
        " * Error Test: E3051 - empty map literal has no pair to infer a type from\n"
        " * Expected: \"map needs a type annotation\"\n"
        " */\n"
        "\n"
        "// expect-error: E3051\n"
        "\n"
        "do main() {\n"
        "    mut m = {:}\n"
        "}\n"
        "\n"
    },
    {
        "E3052_array_overflow",
        "E3052",
        "/*\n"
        " * Error Test: E3052 - too many elements in fixed-size array\n"
        " * Expected: \"too many elements in array initializer — declared size is 3, got 5\" error\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    const a [i64, 3] = {1, 2, 3, 4, 5}\n"
        "}\n"
        "\n"
    },
    {
        "E3053_empty_array_literal_map_element",
        "E3053",
        "do main() {\n"
        "    mut rows [map[string:i64]] = {{}}\n"
        "    println(\"${len(rows)}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3054_mut_array_with_size",
        "E3054",
        "/*\n"
        " * Error Test: E3054 - mutable array with fixed size\n"
        " * Expected: \"mutable arrays cannot have a fixed size\" error\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut a [i64, 3] = {1, 2, 3}\n"
        "}\n"
        "\n"
    },
    {
        "E3055_const_array_no_size",
        "E3055",
        "/*\n"
        " * Error Test: E3055 - const array without fixed size\n"
        " * Expected: \"const arrays must have a fixed size\" error\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    const a [i64] = {1, 2, 3}\n"
        "}\n"
        "\n"
    },
    {
        "E3056_strict_when_missing_cases",
        "E3056",
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c Color = Color.RED\n"
        "\n"
        "    #strict\n"
        "    when c {\n"
        "        is Color.RED {\n"
        "            println(\"red\")\n"
        "        }\n"
        "        // Missing GREEN and BLUE\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3057_arrays_counts_struct_element",
        "E3057",
        "/*\n"
        " * Error Test: E3057 - arrays.counts element type cannot be a map key\n"
        " */\n"
        "\n"
        "// expect-error: E3057\n"
        "\n"
        "import @arrays\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut points [Point] = {Point{x: 1, y: 2}}\n"
        "    println(arrays.counts(points))\n"
        "}\n"
        "\n"
    },
    {
        "E3058_generic_maps_sorted_keys_bool_key",
        "E3058",
        "/*\n"
        " * Error Test: E3058 - a generic function that calls maps.sorted_keys() is\n"
        " * instantiated with a key type that cannot be ordered.\n"
        " */\n"
        "\n"
        "import @maps\n"
        "\n"
        "do keys_sorted(kind generic, m map[kind:i64]) -> [kind] {\n"
        "    return maps.sorted_keys(m)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut flags map[bool:i64] = {true: 1}\n"
        "    println(keys_sorted(bool, flags))\n"
        "}\n"
        "\n"
    },
    {
        "E3059_const_map_declaration",
        "E3059",
        "/*\n"
        " * Error Test: E3059 - maps cannot be declared const\n"
        " * Expected: \"maps cannot be declared const\" error\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    const m map[string:i64] = {\"a\": 1, \"b\": 2}\n"
        "    println(\"${m[\"a\"]}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3061_self_reference_with_valid_pointer_sibling",
        "E3061",
        "/*\n"
        " * Error Test: E3061 - by-value self-reference still rejected even when a valid\n"
        " * pointer field is present in the same struct.\n"
        " * Expected: compiler error E3061 for the by-value 'self Node' field\n"
        " */\n"
        "\n"
        "const Node struct {\n"
        "    next ^Node\n"
        "    self Node\n"
        "}\n"
        "\n"
        "do main() {\n"
        "}\n"
        "\n"
    },
    {
        "E3062_const_mutex",
        "E3062",
        "/*\n"
        " * Error Test: E3062 - const on a mutex handle\n"
        " *\n"
        " * mutexes are inherently stateful (lock/unlock\n"
        " * mutate internal state).\n"
        " */\n"
        "\n"
        "import @sync\n"
        "\n"
        "do main() {\n"
        "    const m = sync.mutex()\n"
        "}\n"
        "\n"
    },
    {
        "E3066_threads_spawn_wrong_signature",
        "E3066",
        "/*\n"
        " * Error Test: E3066 - threads.spawn requires a func() reference\n"
        " */\n"
        "// expect-error: E3066\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @threads\n"
        "\n"
        "do worker(n i64) {\n"
        "    println(n)\n"
        "}\n"
        "\n"
        "unsafe do main() {\n"
        "    mut t Thread = threads.spawn(()worker)\n"
        "    threads.join(t)\n"
        "}\n"
        "\n"
    },
    {
        "E3068_void_in_func_return",
        "E3068",
        "/*\n"
        " * Error Test: E3068 - void-in-func-return\n"
        " * Expected: \"'void' is not a user-facing type\"\n"
        " * A typed-func return position must not spell 'void'; omit the '-> R' clause.\n"
        " */\n"
        "\n"
        "do work() -> (i64, i64) { return 1, 2 }\n"
        "\n"
        "do main() {\n"
        "    mut f func() -> (void, i64) = ()work\n"
        "    f\n"
        "}\n"
        "\n"
    },
    {
        "E3069_inverted_amp_param",
        "E3069",
        "/*\n"
        " * Error Test: E3069 - inverted-amp-param\n"
        " * Expected: \"'&' on a parameter must come before the name\"\n"
        " * The inverted `name &type` form previously spun the parser loop forever;\n"
        " * it must now diagnose cleanly. The correct form is `&name type`.\n"
        " */\n"
        "\n"
        "do bump(x &i64) { x = x + 1 }\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3070_defer_inside_unsafe_block",
        "E3070",
        "// expect-error: E3070\n"
        "do main() {\n"
        "    unsafe {\n"
        "        defer println(\"deferred\")\n"
        "        println(\"body\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3071_return_nil_generic",
        "E3071",
        "/*\n"
        " * Error Test: E3071 - return-nil-from-generic-return\n"
        " * Expected: \"'return nil' from a function whose return type contains a 'generic' parameter's name\"\n"
        " * A generic return takes any type argument — nil is not a value every type\n"
        " * can hold (i64, string, etc.), so it must be rejected at type-check.\n"
        " */\n"
        "\n"
        "do identity(kind generic, x kind) -> kind {\n"
        "    return nil\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3072_return_nil_non_pointer",
        "E3072",
        "/*\n"
        " * Error Test: E3072 - return nil from non-pointer function\n"
        " * Expected: \"cannot return 'nil' from a function that returns 'i64'\"\n"
        " */\n"
        "\n"
        "do foo() -> i64 {\n"
        "    return nil\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    foo()\n"
        "}\n"
        "\n"
    },
    {
        "E3073_return_in_main",
        "E3073",
        "/*\n"
        " * Error Test: E3073 - return-in-main\n"
        " * Expected: \"'return' is not allowed in main()\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    return\n"
        "}\n"
        "\n"
    },
    {
        "E3074_array_compared_with_eq",
        "E3074",
        "/*\n"
        " * Error Test: E3074 - array-comparison\n"
        " * Expected: \"arrays cannot be compared with comparison operators\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut a [i64] = {1, 2, 3}\n"
        "    mut b [i64] = {1, 2, 3}\n"
        "    if a == b {\n"
        "        println(\"equal\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3075_chained_struct_call",
        "E3075",
        "/*\n"
        " * Error Test: E3075 - chained struct function calls\n"
        " * Expected: \"chained struct function calls are not supported\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "\n"
        "    do create(x i64, y i64) -> Point {\n"
        "        return Point{x: x, y: y}\n"
        "    }\n"
        "\n"
        "    do translate(p Point, dx i64, dy i64) -> Point {\n"
        "        return Point{x: p.x + dx, y: p.y + dy}\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = Point.create(1, 2).translate(5, 3)\n"
        "}\n"
        "\n"
    },
    {
        "E3076_map_comparison",
        "E3076",
        "/*\n"
        " * Error Test: E3076 - maps cannot be compared\n"
        " * Expected: \"maps cannot be compared with comparison operators\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut m1 map[string:i64] = {\"a\": 1}\n"
        "    mut m2 map[string:i64] = {\"a\": 1}\n"
        "    if m1 == m2 {\n"
        "        println(\"equal\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3077_struct_comparison",
        "E3077",
        "/*\n"
        " * Error Test: E3077 - structs cannot be compared\n"
        " * Expected: \"structs cannot be compared with comparison operators\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p1 = Point{x: 1, y: 2}\n"
        "    mut p2 = Point{x: 1, y: 2}\n"
        "    if p1 == p2 {\n"
        "        println(\"equal\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3078_pointer_arithmetic",
        "E3078",
        "/*\n"
        " * Error Test: E3078 - pointer arithmetic not supported\n"
        " * Expected: \"pointer arithmetic is not supported\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 42\n"
        "    mut p ^i64 = addr(x)\n"
        "    mut p2 = p + 1\n"
        "}\n"
        "\n"
    },
    {
        "E3079_mutable_ref_to_const",
        "E3079",
        "/*\n"
        " * Error Test: E3079 - mutable reference to const variable\n"
        " * Expected: \"cannot take a mutable reference to const variable\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    const x i64 = 42\n"
        "    mut r = ref(x)\n"
        "}\n"
        "\n"
    },
    {
        "E3080_named_return_wrong_expr",
        "E3080",
        "/*\n"
        " * Error Test: E3080 - named return must return the named variable\n"
        " * Expected: \"function must return named variable 'result'\"\n"
        " */\n"
        "\n"
        "do add(a i64, b i64) -> (result i64) {\n"
        "    mut result i64 = a + b\n"
        "    return 99\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    add(1, 2)\n"
        "}\n"
        "\n"
    },
    {
        "E3081_bare_function_name",
        "E3081",
        "/*\n"
        " * Error Test: E3081 - function name used as statement without call\n"
        " * Expected: \"function 'println' used as a statement without being called\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    println\n"
        "}\n"
        "\n"
    },
    {
        "E3083_from_c_string_non_pointer",
        "E3083",
        "/*\n"
        " * Error Test: E3083 - from_c_string() requires a raw C pointer\n"
        " * Expected: E3083 \"from_c_string() requires a raw C pointer\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut msg = from_c_string(\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "E3084_type_of_type_name",
        "E3084",
        "/*\n"
        " * Error Test: E3084 - type_of() with a type name instead of a value\n"
        " * Expected: E3084 \"type_of() expects a value, not a type name\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(type_of(Point))\n"
        "}\n"
        "\n"
    },
    {
        "E3085_u64_in_i64_stored_range",
        "E3085",
        "/* Error Test: E3085 - a u64 value checked against a Range<i64> */\n"
        "do main() {\n"
        "    mut a u64 = 1\n"
        "    mut r = range(0, 5)\n"
        "    println(a in r)\n"
        "}\n"
        "\n"
    },
    {
        "E3086_fmt_non_literal_format",
        "E3086",
        "/*\n"
        " * Error Test: E3086 - fmt format string must be a literal\n"
        " * Expected: \"fmt format string must be a string literal\"\n"
        " */\n"
        "\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    mut pattern string = \"value: %d\"\n"
        "    fmt.printfln(pattern, {42})\n"
        "}\n"
        "\n"
    },
    {
        "E3087_fmt_escaped_percent_n",
        "E3087",
        "// expect-error: E3087\n"
        "// expect-error-count: 1\n"
        "// A directive written with an escape is still a directive: \"\\x25n\" is %n.\n"
        "\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    fmt.printfln(\"a\\x25n|\", {})\n"
        "}\n"
        "\n"
    },
    {
        "E3088_fmt_using_bare_printfln",
        "E3088",
        "// expect-error: E3088\n"
        "// A bare printfln reached through 'using fmt' checks its directives like fmt.printfln.\n"
        "\n"
        "import @fmt\n"
        "using fmt\n"
        "\n"
        "do main() {\n"
        "    printfln(\"%d\", {\"x\"})\n"
        "}\n"
        "\n"
    },
    {
        "E3089_os_memory_usage_suggests_or_default",
        "E3089",
        "// expect-contains: 'or_default' to substitute fallback values\n"
        "import @os\n"
        "\n"
        "do main() {\n"
        "    mut result = os.memory_usage()\n"
        "    println(result)\n"
        "}\n"
        "\n"
    },
    {
        "E3090_not_on_string",
        "E3090",
        "/* Error Test: E3090 - '!' used on a string type */\n"
        "do main() {\n"
        "    mut s string = \"hello\"\n"
        "    mut r bool = !s\n"
        "    println(r)\n"
        "}\n"
        "\n"
    },
    {
        "E3091_integer_as_long_as_condition",
        "E3091",
        "do main() {\n"
        "    as_long_as 5 {\n"
        "        break\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3092_i64_nil_compare",
        "E3092",
        "/* Error Test: E3092 - i64 compared to nil */\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    if x == nil { println(\"nil\") }\n"
        "}\n"
        "\n"
    },
    {
        "E3093_array_arithmetic",
        "E3093",
        "/* Error Test: E3093 - arithmetic operator on array */\n"
        "do main() {\n"
        "    mut a [i64] = {1, 2}\n"
        "    mut b [i64] = {3, 4}\n"
        "    mut c = a + b\n"
        "    println(c)\n"
        "}\n"
        "\n"
    },
    {
        "E3094_array_index_wrong_type",
        "E3094",
        "/* Error Test: E3094 - wrong type assigned to array element by index */\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3}\n"
        "    arr[0] = \"hello\"\n"
        "    println(arr[0])\n"
        "}\n"
        "\n"
    },
    {
        "E3095_in_on_i64",
        "E3095",
        "/*\n"
        " * Error Test: E3095 - 'in' on non-collection type\n"
        " * Expected: \"'in' only works with arrays, maps, and strings; got 'i64'\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 42\n"
        "    if 5 in x {\n"
        "        println(\"found\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3096_negate_u8",
        "E3096",
        "do main() {\n"
        "    mut a u8 = 5\n"
        "    mut b u8 = -a\n"
        "    println(b)\n"
        "}\n"
        "\n"
    },
    {
        "E3098_struct_mismatch_pointer_deref",
        "E3098",
        "/*\n"
        " * Error Test: E3098 - struct type mismatch through pointer dereference\n"
        " * ptrA^ = ptrB^ where ptrA and ptrB point to\n"
        " * different struct types was silently allowed by the typechecker.\n"
        " * Expected: E3098\n"
        " */\n"
        "\n"
        "const Vec2 struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "const Vec3 struct {\n"
        "    x i64\n"
        "    y i64\n"
        "    z i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut v2 = new(Vec2)\n"
        "    mut v3 = new(Vec3)\n"
        "    v3^ = v2^\n"
        "}\n"
        "\n"
    },
    {
        "E3099_user_openflag_collision",
        "E3099",
        "// expect-error: E3099\n"
        "// A user enum named OpenFlag collides with io's OpenFlag while @io is imported.\n"
        "\n"
        "import @io\n"
        "\n"
        "const OpenFlag enum {\n"
        "    A\n"
        "    B\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3100_generic_parameter_as_value",
        "E3100",
        "do f(k generic) -> i64 { return k }\n"
        "\n"
        "do main() {\n"
        "    println(f(i64))\n"
        "}\n"
        "\n"
    },
    {
        "E3101_mut_func_ref",
        "E3101",
        "/*\n"
        " * Error Test: E3101 - mut on a func reference declaration\n"
        " * Expected: compile-time error on both explicit and inferred type\n"
        " */\n"
        "\n"
        "do double(n i64) -> i64 { return n * 2 }\n"
        "\n"
        "do main() {\n"
        "    mut f func(i64) -> i64 = ()double\n"
        "    mut g = ()double\n"
        "}\n"
        "\n"
    },
    {
        "E3102_func_return_assigned_to_var",
        "E3102",
        "do double(n i64) -> i64 { return n * 2 }\n"
        "do get_fn() -> func(i64) -> i64 { return ()double }\n"
        "do main() {\n"
        "    const f = get_fn()\n"
        "    println(f(5))\n"
        "}\n"
        "\n"
    },
    {
        "E3103_json_struct_func_field",
        "E3103",
        "/*\n"
        " * Error Test: E3103 - #json struct with func-typed field\n"
        " * Expected: E3103 error, compilation fails\n"
        " */\n"
        "\n"
        "import @json\n"
        "\n"
        "do double(n i64) -> i64 { return n * 2 }\n"
        "\n"
        "#json\n"
        "const Wrapper struct {\n"
        "    value i64\n"
        "    transform func(i64) -> i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut w = Wrapper{value: 10, transform: ()double}\n"
        "}\n"
        "\n"
    },
    {
        "E3104_json_struct_has_func",
        "E3104",
        "/*\n"
        " * Error Test: E3104 - #json struct declares a function\n"
        " * Expected: E3104 error, compilation fails\n"
        " */\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const User struct {\n"
        "    name string\n"
        "    age  i64\n"
        "\n"
        "    do greet() {\n"
        "        println(\"hi\")\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3105_unknown_directive_p",
        "E3105",
        "/* Error Test: E3105 - unknown format directive %p */\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    fmt.printf(\"%p\\n\", {x})\n"
        "}\n"
        "\n"
    },
    {
        "E3106_dangling_percent_after_text",
        "E3106",
        "/* Error Test: E3106 - dangling % after text */\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    fmt.printf(\"hello %\", {})\n"
        "}\n"
        "\n"
    },
    {
        "E3107_zero_args_for_directive",
        "E3107",
        "/* Error Test: E3107 - no elements for format directives */\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    fmt.printf(\"%d %s %f\\n\", {})\n"
        "}\n"
        "\n"
    },
    {
        "E3108_no_directives_with_args",
        "E3108",
        "/* Error Test: E3108 - no directives but elements passed */\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    fmt.printf(\"hello\\n\", {42})\n"
        "}\n"
        "\n"
    },
    {
        "E3109_json_struct_field_default",
        "E3109",
        "/*\n"
        " * Error Test: E3109 - #json struct cannot have default field values\n"
        " * Expected: E3109 error\n"
        " */\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const Settings struct {\n"
        "    name string = \"default\"\n"
        "    value i64 = 42\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s = new(Settings)\n"
        "    println(s^.name)\n"
        "}\n"
        "\n"
    },
    {
        "E3110_implicit_enum_no_context",
        "E3110",
        "/*\n"
        " * Error Test: E3110 - implicit enum selector without type context\n"
        " * Expected: \"implicit enum selector '.RED' requires type context\" error\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x = .RED\n"
        "}\n"
        "\n"
    },
    {
        "E3111_payload_on_string_enum",
        "E3111",
        "/*\n"
        " * Error Test: E3111 - payload on string enum variant\n"
        " * Expected: \"payload types are not allowed on string enum variants\"\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED = \"red\"\n"
        "    GREEN = \"green\"\n"
        "    BLUE(i64)\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3112_payload_on_flags_enum",
        "E3112",
        "/*\n"
        " * Error Test: E3112 - payload on #flags enum variant\n"
        " * Expected: \"payload types are not allowed on #flags enum variants\"\n"
        " */\n"
        "\n"
        "#flags\n"
        "const Perms enum {\n"
        "    READ(i64)\n"
        "    WRITE\n"
        "    EXEC\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3113_wrong_payload_count",
        "E3113",
        "/*\n"
        " * Error Test: E3113 - wrong number of payload values\n"
        " * Expected: \"variant 'Circle' of enum 'Shape' expects 1 payload value(s), got 2\"\n"
        " */\n"
        "\n"
        "const Shape enum {\n"
        "    Circle(f64)\n"
        "    Rect(f64, f64)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s Shape = Shape.Circle(1.0, 2.0)\n"
        "}\n"
        "\n"
    },
    {
        "E3114_no_payload_variant_called",
        "E3114",
        "/*\n"
        " * Error Test: E3114 - calling variant that has no payload\n"
        " * Expected: \"variant 'Point' of enum 'Shape' has no payload; remove the arguments\"\n"
        " */\n"
        "\n"
        "const Shape enum {\n"
        "    Circle(f64)\n"
        "    Point\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s Shape = Shape.Point(5)\n"
        "}\n"
        "\n"
    },
    {
        "E3115_call_plain_enum_variant",
        "E3115",
        "/*\n"
        " * Error Test: E3115 - calling variant on non-tagged enum\n"
        " * Expected: \"enum 'Color' is not a tagged enum; variant 'RED' cannot be called\"\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c Color = Color.RED(5)\n"
        "}\n"
        "\n"
    },
    {
        "E3116_wrong_binding_count",
        "E3116",
        "/*\n"
        " * Error Test: E3116 - wrong number of bindings in when/is\n"
        " * Expected: \"wrong number of bindings for variant 'Circle'; expected 1, got 2\"\n"
        " */\n"
        "\n"
        "const Shape enum {\n"
        "    Circle(f64)\n"
        "    Point\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s Shape = Shape.Circle(3.14)\n"
        "    when s {\n"
        "        is Circle(x, y) {\n"
        "            println(\"circle\")\n"
        "        }\n"
        "        default {\n"
        "            println(\"other\")\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3117_enum_i64_comparison",
        "E3117",
        "/*\n"
        " * Error Test: E3117 - enum compared with integer type\n"
        " * Expected: cannot compare enum with i64\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c Color = .RED\n"
        "    mut x i64 = 0\n"
        "    if c == x { println(\"bad\") }\n"
        "}\n"
        "\n"
    },
    {
        "E3118_i64_to_enum_assign",
        "E3118",
        "/*\n"
        " * Error Test: E3118 - integer assigned to enum variable\n"
        " * Expected: cannot assign i64 to enum\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut c Color = .RED\n"
        "    c = 1\n"
        "}\n"
        "\n"
    },
    {
        "E3119_sized_array_in_param",
        "E3119",
        "do take_ints(arr [i64, 5]) {\n"
        "    println(\"bad\")\n"
        "}\n"
        "\n"
        "do take_strings(arr [string, 2]) {\n"
        "    println(\"bad\")\n"
        "}\n"
        "\n"
        "do main() { }\n"
        "\n"
    },
    {
        "E3120_pointer_ordering_comparison",
        "E3120",
        "/* Error Test: E3120 - ordering comparison on pointer types */\n"
        "do main() {\n"
        "    mut x i64 = 10\n"
        "    mut y i64 = 20\n"
        "    mut px ^i64 = addr(x)\n"
        "    mut py ^i64 = addr(y)\n"
        "    if px < py { println(\"less\") }\n"
        "}\n"
        "\n"
    },
    {
        "E3121_when_array_subject",
        "E3121",
        "/* Error Test: E3121 - array used as when condition */\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3}\n"
        "    when arr {\n"
        "        default { println(\"default\") }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3122_write_through_const_ptr",
        "E3122",
        "/* Error Test: E3122 - cannot modify value through pointer to const-declared variable */\n"
        "do main() {\n"
        "    const x i64 = 5\n"
        "    mut p ^i64 = addr(x)\n"
        "    p^ = 99\n"
        "}\n"
        "\n"
    },
    {
        "E3123_foreach_both_blank",
        "E3123",
        "/* Error Test: E3123 - for_each with both index and value as _ */\n"
        "do main() {\n"
        "    mut items [i64] = {1, 2, 3}\n"
        "    mut count i64 = 0\n"
        "    for_each _, _ in items {\n"
        "        count++\n"
        "    }\n"
        "    println(count)\n"
        "}\n"
        "\n"
    },
    {
        "E3124_tagged_enum_equality",
        "E3124",
        "const Shape enum {\n"
        "    Circle(f64)\n"
        "    Rect(f64, f64)\n"
        "    Point\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s1 Shape = Shape.Circle(3.14)\n"
        "    mut s2 Shape = Shape.Circle(3.14)\n"
        "    if s1 == s2 {\n"
        "        println(\"equal\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3125_non_const_array_size",
        "E3125",
        "/*\n"
        " * Error Test: E3125 - array size must be compile-time constant\n"
        " * Expected: \"'n' is not a compile-time integer constant\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut n i64 = 5\n"
        "    const arr [i64, n] = {1, 2, 3, 4, 5}\n"
        "}\n"
        "\n"
    },
    {
        "E3126_negative_array_size",
        "E3126",
        "/*\n"
        " * Error Test: E3126 - negative array size\n"
        " * Expected: \"array size must be greater than zero\"\n"
        " */\n"
        "\n"
        "const NEG i8 = -1\n"
        "const arr [string, NEG] = {\"a\"}\n"
        "\n"
    },
    {
        "E3127_type_param_enum_struct_literal",
        "E3127",
        "/*\n"
        " * Error Test: E3127 - the same kind{...} body form with an enum binding. An enum\n"
        " * names a real type, so the call site accepts it; the struct literal does not.\n"
        " * Expected: \"generic parameter 'kind' is used as a struct literal\"\n"
        " */\n"
        "\n"
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "}\n"
        "\n"
        "do mk_stack(kind generic) -> kind {\n"
        "    return kind{}\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x = mk_stack(Color)\n"
        "    println(x)\n"
        "}\n"
        "\n"
    },
    {
        "E3128_type_param_expression",
        "E3128",
        "/*\n"
        " * Error Test: E3128 - a non-type expression as a type argument. Widening the\n"
        " * gate to any type name left this rejected.\n"
        " * Expected: \"got a non-type expression\"\n"
        " */\n"
        "\n"
        "do make(kind generic) -> ^kind {\n"
        "    return new(kind)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = make(1 + 2)\n"
        "}\n"
        "\n"
    },
    {
        "E3129_empty_loop_body",
        "E3129",
        "/*\n"
        " * Error Test: E3129 - empty loop body\n"
        " * Expected: \"empty loop body; this will loop forever at runtime\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    loop {\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3130_bare_func_struct_field",
        "E3130",
        "/*\n"
        " * Error Test: E3130 - bare-func-struct-field\n"
        " * Expected: \"bare 'func' is not allowed as a struct field type\"\n"
        " */\n"
        "\n"
        "const Foobar struct {\n"
        "    name string\n"
        "    callback func\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"unreachable\")\n"
        "}\n"
        "\n"
    },
    {
        "E3131_file_scope_const_no_type",
        "E3131",
        "/*\n"
        " * Error Test: E3131 - file-scope-const-no-type\n"
        " * Expected: \"file-scope 'const' requires an explicit type annotation\"\n"
        " */\n"
        "\n"
        "const CONFIG_FILE = \".config\"\n"
        "const MAX_RETRIES = 5\n"
        "const PI = 3.14\n"
        "const DEBUG = true\n"
        "const SEPARATOR = ','\n"
        "\n"
        "do main() {\n"
        "    println(\"unreachable\")\n"
        "}\n"
        "\n"
    },
    {
        "E3132_alias_undefined_target",
        "E3132",
        "/*\n"
        " * Error Test: E3132 - alias target type not defined\n"
        " * Expected: alias target type is not defined\n"
        " */\n"
        "\n"
        "alias Foo = DoesNotExist\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3133_alias_circular",
        "E3133",
        "/*\n"
        " * Error Test: E3133 - circular alias reference\n"
        " * Expected: alias creates a circular reference\n"
        " */\n"
        "\n"
        "alias A = B\n"
        "alias B = A\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3134_alias_module_qualified",
        "E3134",
        "/*\n"
        " * Error Test: E3134 - alias cannot target module-qualified type\n"
        " * Expected: alias cannot target a module-qualified type\n"
        " */\n"
        "\n"
        "alias DB = sqlite.Database\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3135_alias_generic",
        "E3135",
        "/*\n"
        " * Error Test: E3135 - alias cannot target 'generic'\n"
        " * Expected: alias 'Any' cannot target 'generic'\n"
        " */\n"
        "\n"
        "alias Any = generic\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3136_empty_inline_array_literal",
        "E3136",
        "do main() {\n"
        "    for_each val in {} {\n"
        "        mut x string = val\n"
        "        println(x)\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3137_int64_min_div_const_neg_one",
        "E3137",
        "// Error Test: E3137 - INT64_MIN / -1 is undefined in C and must be caught\n"
        "// at compile time even when the operands come through const bindings\n"
        "// rather than bare literals.\n"
        "// expect-error: E3137\n"
        "// expect-error-count: 1\n"
        "\n"
        "const MIN i64 = -9223372036854775808\n"
        "const NEG1 i64 = -1\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = MIN / NEG1\n"
        "    println(\"${x}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3138_f64_literal_overflow",
        "E3138",
        "/*\n"
        " * Error Test: E3138 - float literal exceeds the range of f64\n"
        " * Expected: \"float literal overflows f64\"\n"
        " */\n"
        "\n"
        "// expect-error: E3138\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = 2.0e308\n"
        "    println(\"${x}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3140_json_struct_array_of_arrays_field",
        "E3140",
        "/*\n"
        " * Error Test: E3140 - #json struct with an array-of-arrays field\n"
        " */\n"
        "\n"
        "// expect-error: E3140\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const S struct {\n"
        "    grid [[i64]]\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"unreachable\")\n"
        "}\n"
        "\n"
    },
    {
        "E3141_enum_variant_through_alias",
        "E3141",
        "/*\n"
        " * Error Test: E3141 - enum-variant access on a struct type reached through\n"
        " * an alias. Same failure as the direct form; the alias is not what makes it\n"
        " * invalid.\n"
        " */\n"
        "\n"
        "// expect-error: E3141\n"
        "// expect-error-count: 1\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "}\n"
        "\n"
        "alias Vec2 = Point\n"
        "\n"
        "do main() {\n"
        "    mut p Vec2 = Vec2.RED\n"
        "    println(p.x)\n"
        "}\n"
        "\n"
    },
    {
        "E3142_func_return_type",
        "E3142",
        "/*\n"
        " * Error Test: E3142 - a function declared with a func return type\n"
        " *\n"
        " * STANDARD 7.6.6: a returned func value cannot be called, assigned, or\n"
        " * stored, so the declaration itself is rejected rather than every use of\n"
        " * the result.\n"
        " */\n"
        "\n"
        "// expect-error: E3142\n"
        "\n"
        "do inc(x i64) -> i64 {\n"
        "    return x + 1\n"
        "}\n"
        "\n"
        "do pick() -> func(i64) -> i64 {\n"
        "    return ()inc\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"hi\")\n"
        "}\n"
        "\n"
    },
    {
        "E3143_flags_enum_too_many_variants",
        "E3143",
        "// expect-error: E3143\n"
        "// expect-error-count: 1\n"
        "// A #flags enum has one bit per variant, and int64 has 63 usable bits\n"
        "// (bit 63 is the sign bit). 64 variants overflows and is rejected.\n"
        "\n"
        "#flags\n"
        "const F enum {\n"
        "    M0\n"
        "    M1\n"
        "    M2\n"
        "    M3\n"
        "    M4\n"
        "    M5\n"
        "    M6\n"
        "    M7\n"
        "    M8\n"
        "    M9\n"
        "    M10\n"
        "    M11\n"
        "    M12\n"
        "    M13\n"
        "    M14\n"
        "    M15\n"
        "    M16\n"
        "    M17\n"
        "    M18\n"
        "    M19\n"
        "    M20\n"
        "    M21\n"
        "    M22\n"
        "    M23\n"
        "    M24\n"
        "    M25\n"
        "    M26\n"
        "    M27\n"
        "    M28\n"
        "    M29\n"
        "    M30\n"
        "    M31\n"
        "    M32\n"
        "    M33\n"
        "    M34\n"
        "    M35\n"
        "    M36\n"
        "    M37\n"
        "    M38\n"
        "    M39\n"
        "    M40\n"
        "    M41\n"
        "    M42\n"
        "    M43\n"
        "    M44\n"
        "    M45\n"
        "    M46\n"
        "    M47\n"
        "    M48\n"
        "    M49\n"
        "    M50\n"
        "    M51\n"
        "    M52\n"
        "    M53\n"
        "    M54\n"
        "    M55\n"
        "    M56\n"
        "    M57\n"
        "    M58\n"
        "    M59\n"
        "    M60\n"
        "    M61\n"
        "    M62\n"
        "    M63\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = cast(F.M0, i64)\n"
        "    println(\"${x}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3144_error_code_on_struct",
        "E3144",
        "// expect-error: E3144\n"
        "// #error_code is only valid on enum declarations.\n"
        "\n"
        "#error_code\n"
        "const Config struct {\n"
        "    x i64\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3145_error_code_string_enum",
        "E3145",
        "// expect-error: E3145\n"
        "// A #error_code enum must be plain integer-backed.\n"
        "\n"
        "#error_code\n"
        "const Codes enum {\n"
        "    A = \"a\"\n"
        "    B = \"b\"\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3146_error_code_explicit_values",
        "E3146",
        "// expect-error: E3146\n"
        "// The compiler owns ErrorCode numbering; explicit variant values are rejected.\n"
        "\n"
        "#error_code\n"
        "const Codes enum {\n"
        "    A = 10\n"
        "    B = 20\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3147_error_code_tagged_enum",
        "E3147",
        "// expect-error: E3147\n"
        "// A #error_code enum cannot have payload (tagged) variants.\n"
        "\n"
        "#error_code\n"
        "const Codes enum {\n"
        "    PLAIN\n"
        "    WITH_DATA(i64)\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3148_error_code_variant_collision",
        "E3148",
        "// expect-error: E3148\n"
        "// Two #error_code enums cannot declare the same variant name.\n"
        "\n"
        "#error_code\n"
        "const A enum {\n"
        "    SHARED\n"
        "}\n"
        "\n"
        "#error_code\n"
        "const B enum {\n"
        "    SHARED\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3149_when_errcode_no_default",
        "E3149",
        "// expect-error: E3149\n"
        "// ErrorCode is an open enum: `when err.code` always needs a default branch.\n"
        "\n"
        "do f() -> (i64, Error) {\n"
        "    return 0, error(.NotFound, \"x\")\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut _, err = f()\n"
        "    when err.code {\n"
        "        is .NotFound { println(\"nf\") }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3150_read_lines_negative_limit",
        "E3150",
        "// expect-error: E3150\n"
        "// io.read_lines rejects a negative line limit at compile time.\n"
        "\n"
        "import @io\n"
        "\n"
        "do main() {\n"
        "    mut lines, _ = io.read_lines(\"data.txt\", -1)\n"
        "    println(len(lines))\n"
        "}\n"
        "\n"
    },
    {
        "E3151_when_errcode_strict",
        "E3151",
        "// expect-error: E3151\n"
        "// expect-error-count: 1\n"
        "// ErrorCode is an open enum: `when err.code` can never be #strict, even\n"
        "// with a default branch present.\n"
        "\n"
        "do f() -> (i64, Error) {\n"
        "    return 0, error(.NotFound, \"x\")\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut _, err = f()\n"
        "\n"
        "    #strict\n"
        "    when err.code {\n"
        "        is .NotFound { println(\"nf\") }\n"
        "        default      { println(\"d\") }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3152_error_code_flags_enum",
        "E3152",
        "// expect-error: E3152\n"
        "// A #error_code enum cannot also be #flags: ErrorCode is numbered by\n"
        "// global slot, so the power-of-2 bit values would be silently discarded.\n"
        "\n"
        "#error_code\n"
        "#flags\n"
        "const Both enum {\n"
        "    X\n"
        "    Y\n"
        "    Z\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3153_error_in_array_literal",
        "E3153",
        "/*\n"
        " * Error Test: E3153 - Error value inside an array literal with no declared type\n"
        " */\n"
        "// expect-error: E3153\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut e Error = error(.NotFound, \"gone\")\n"
        "    println(len({e}))\n"
        "}\n"
        "\n"
    },
    {
        "E3155_narrowing_i16_to_i8_declaration",
        "E3155",
        "// Error Test: E3155 - an i16 value stored into an i8 variable narrows\n"
        "// expect-error: E3155\n"
        "\n"
        "do main() {\n"
        "    mut b i16 = 300\n"
        "    mut a i8 = b\n"
        "    println(a)\n"
        "}\n"
        "\n"
    },
    {
        "E3156_integer_compared_with_bool",
        "E3156",
        "do main() {\n"
        "    mut flag bool = true\n"
        "    mut n i64 = 1\n"
        "    println(n == flag)\n"
        "    println(n < flag)\n"
        "}\n"
        "\n"
    },
    {
        "E3157_nil_to_i64",
        "E3157",
        "/*\n"
        " * Error Test: E3157 - nil cannot be assigned to i64\n"
        " * Expected: error about non-nullable type\n"
        " */\n"
        "// expect-error: E3157\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = nil\n"
        "}\n"
        "\n"
    },
    {
        "E3159_generic_struct_int_argument",
        "E3159",
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do identity(kind generic, x kind) -> kind { return x }\n"
        "\n"
        "do main() {\n"
        "    mut b = identity(Point, 5)\n"
        "    println(\"${b.x}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3160_param_no_type_annotation",
        "E3160",
        "/*\n"
        " * Error Test: E3160 - a parameter with no type annotation is only allowed\n"
        " * when its default value is an enum member the compiler can infer from.\n"
        " */\n"
        "// expect-error: E3160\n"
        "// expect-error-count: 1\n"
        "\n"
        "do scale(factor = 2) -> i64 {\n"
        "    return factor * 10\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"${scale()}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3161_ref_dynamic_array_index",
        "E3161",
        "/*\n"
        " * Error Test: E3161 - ref() rejects a dynamic array index expression;\n"
        " * the backing store relocates when the array grows.\n"
        " */\n"
        "// expect-error: E3161\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut a [i64] = {10, 20, 30, 40}\n"
        "    mut r = ref(a[1])\n"
        "}\n"
        "\n"
    },
    {
        "E3162_return_addr_through_unannotated_local",
        "E3162",
        "do mk() -> ^i64 {\n"
        "  x i64 = 5\n"
        "  p = addr(x)\n"
        "  return p\n"
        "}\n"
        "\n"
        "do main() {\n"
        "  println(mk()^)\n"
        "}\n"
        "\n"
    },
    {
        "E3163_dangling_addr_loop",
        "E3163",
        "do main() {\n"
        "    mut p ^i64 = nil\n"
        "    for i in range(3) {\n"
        "        mut x i64 = i\n"
        "        p = addr(x)\n"
        "    }\n"
        "    println(p^)\n"
        "}\n"
        "\n"
    },
    {
        "E3164_mem_use_after_destroy_branch",
        "E3164",
        "/*\n"
        " * Error Test: E3164 - pointer into a @mem arena used after mem.destroy()\n"
        " * Expected: the destroy happens on only one branch of an if; after the join\n"
        " * the arena counts as destroyed, so the deref past it is still caught.\n"
        " */\n"
        "\n"
        "import @mem\n"
        "\n"
        "unsafe do main() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mut p ^i64 = mem.alloc(a, 42)\n"
        "    mut cond bool = true\n"
        "    if cond {\n"
        "        mem.destroy(a)\n"
        "    }\n"
        "    println(p^)\n"
        "}\n"
        "\n"
    },
    {
        "E3165_mem_use_after_reset",
        "E3165",
        "/*\n"
        " * Error Test: E3165 - pointer into a @mem arena used after mem.reset()\n"
        " * Expected: mem.reset() releases the arena's memory without destroying the\n"
        " * handle; a pointer taken before the reset is still dangling.\n"
        " */\n"
        "\n"
        "import @mem\n"
        "\n"
        "unsafe do main() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mut p ^i64 = mem.alloc(a, 42)\n"
        "    mem.reset(a)\n"
        "    println(p^)\n"
        "}\n"
        "\n"
    },
    {
        "E3166_double_arena_destroy",
        "E3166",
        "/*\n"
        " * Error Test: E3166 - double mem.destroy() on the same arena\n"
        " *\n"
        " * compile-time detection of straight-line double\n"
        " * destroy on the same variable.\n"
        " */\n"
        "\n"
        "import @mem\n"
        "\n"
        "unsafe do main() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mem.destroy(a)\n"
        "    mem.destroy(a)\n"
        "}\n"
        "\n"
    },
    {
        "E3167_cast_pointer_reinterpret",
        "E3167",
        "/*\n"
        " * Error Test: E3167 - cast() cannot reinterpret one pointer type as another\n"
        " * Expected: E3167 \"cast() cannot reinterpret pointer types\" error\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "const Vec struct {\n"
        "    a i64\n"
        "    b i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = new(Point)\n"
        "    mut q ^Vec = cast(p, ^Vec)\n"
        "    println(q^.a)\n"
        "}\n"
        "\n"
    },
    {
        "E3169_arena_pointer_escapes_to_global",
        "E3169",
        "// expect-error: E3169\n"
        "// expect-error-count: 2\n"
        "/*\n"
        " * Error Test: E3169 - a pointer into a @mem arena stored where it outlives\n"
        " * the function (a module-level global), then the arena is torn down in the\n"
        " * same function (#2696). Caught whether the store is direct or laundered\n"
        " * through a helper whose escape summary is PARAM_ESCAPE_GLOBAL, and\n"
        " * regardless of whether the destroy is textually before or after the store.\n"
        " */\n"
        "\n"
        "import @mem\n"
        "\n"
        "mut g ^i64 = nil\n"
        "\n"
        "do stash_g(p ^i64) { g = p }\n"
        "\n"
        "unsafe do store_direct() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mut p ^i64 = mem.alloc(a, 42)\n"
        "    g = p\n"
        "    mem.destroy(a)\n"
        "}\n"
        "\n"
        "unsafe do store_via_helper() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mut p ^i64 = mem.alloc(a, 42)\n"
        "    stash_g(p)\n"
        "    mem.destroy(a)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    store_direct()\n"
        "    store_via_helper()\n"
        "    println(\"unreachable\")\n"
        "}\n"
        "\n"
    },
    {
        "E3170_json_tag_malformed",
        "E3170",
        "// expect-error: E3170\n"
        "// expect-error-count: 1\n"
        "// A #json field tag that isn't exactly `json:\"Name\"`.\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const User struct {\n"
        "    name string `bogus`\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3171_json_tag_mixed_same_struct",
        "E3171",
        "// expect-error: E3171\n"
        "// expect-error-count: 1\n"
        "// A #json struct with one tagged field and one untagged field.\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const User struct {\n"
        "    name string `json:\"Name\"`\n"
        "    age i64\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3172_json_tag_duplicate_key",
        "E3172",
        "// expect-error: E3172\n"
        "// expect-error-count: 1\n"
        "// Two fields of the same #json struct tagged with the same JSON key.\n"
        "\n"
        "import @json\n"
        "\n"
        "#json\n"
        "const Dup struct {\n"
        "    name string `json:\"X\"`\n"
        "    age i64 `json:\"X\"`\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E3173_json_struct_tagged_enum_field",
        "E3173",
        "/*\n"
        " * Error Test: E3173 - #json struct with a tagged enum field\n"
        " */\n"
        "\n"
        "// expect-error: E3173\n"
        "\n"
        "import @json\n"
        "\n"
        "const Shape enum {\n"
        "    Circle(f64)\n"
        "    Square(f64)\n"
        "}\n"
        "\n"
        "#json\n"
        "const Item struct {\n"
        "    name string\n"
        "    shape Shape\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"unreachable\")\n"
        "}\n"
        "\n"
    },
    {
        "E3174_json_parse_non_json_struct",
        "E3174",
        "/*\n"
        " * Error Test: E3174 - json.parse() into a struct without the #json\n"
        " * attribute. Without it, gray_json_parse_<Name> is never generated, so\n"
        " * codegen fell back to gray_json_decode (which returns a GrayMap, not the\n"
        " * struct) and leaked a raw C initialization-type error.\n"
        " */\n"
        "// expect-error: E3174\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @json\n"
        "\n"
        "const Plain struct {\n"
        "    x i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p Plain = json.parse(\"{\\\"x\\\": 1}\")\n"
        "    println(p.x)\n"
        "}\n"
        "\n"
    },
    {
        "E3175_struct_field_default_wrong_type",
        "E3175",
        "/*\n"
        " * Error Test: E3175 - struct field default value has wrong type\n"
        " * Expected: E3175 error\n"
        " */\n"
        "// expect-error: E3175\n"
        "// expect-error-count: 1\n"
        "\n"
        "const S struct {\n"
        "    x i64 = \"not an i64\"\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s S = S{}\n"
        "    println(\"${s.x}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3176_flags_enum_multi_bit_value",
        "E3176",
        "// expect-error: E3176\n"
        "// expect-error-count: 1\n"
        "// A #flags variant with an explicit value must occupy exactly one bit. WRITE = 3\n"
        "// carries READ's bit, so composing and testing flags would alias the two.\n"
        "// 0 stays valid as a \"none\" sentinel.\n"
        "\n"
        "#flags\n"
        "const Permissions enum {\n"
        "    NONE = 0\n"
        "    READ = 1\n"
        "    WRITE = 3\n"
        "    EXECUTE = 4\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = Permissions.WRITE\n"
        "    println(\"${cast(p, i64)}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3177_fmt_length_modifier",
        "E3177",
        "// expect-error: E3177\n"
        "// A C length modifier would make C read the value at a different width.\n"
        "\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    mut n i64 = 70000\n"
        "    fmt.printfln(\"%hd\", {n})\n"
        "}\n"
        "\n"
    },
    {
        "E3178_fmt_precision_on_char",
        "E3178",
        "// expect-error: E3178\n"
        "// A precision has no meaning for %c.\n"
        "\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    fmt.printfln(\"[%.2c]\", {'a'})\n"
        "}\n"
        "\n"
    },
    {
        "E3179_fmt_width_too_large",
        "E3179",
        "// expect-error: E3179\n"
        "// expect-error-count: 1\n"
        "// C's printf takes a width as an int; a larger one prints nothing at all.\n"
        "\n"
        "import @fmt\n"
        "\n"
        "do main() {\n"
        "    fmt.printfln(\"[%2147483648d]\", {1})\n"
        "}\n"
        "\n"
    },
    {
        "E3180_compound_assign_string",
        "E3180",
        "/*\n"
        " * Error Test: E3180 - compound-assign-string\n"
        " * Expected: \"cannot use '-=' on string type\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut s string = \"hello\"\n"
        "    s -= \"world\"\n"
        "}\n"
        "\n"
    },
    {
        "E3181_modulo_f64",
        "E3181",
        "/*\n"
        " * Error Test: E3181 - modulo-f64\n"
        " * Expected: \"%\" and \"integer\"\n"
        " */\n"
        "\n"
        "\n"
        "do main() {\n"
        "    mut f f64 = 3.14\n"
        "    mut x = f % 2  // Should fail: modulo on f64\n"
        "}\n"
        "\n"
    },
    {
        "E3182_nil_mul_i64",
        "E3182",
        "/*\n"
        " * Error Test: E3182 - nil used as operand of '*'\n"
        " *\n"
        " * the check covers every binary operator\n"
        " * except '==' and '!=', so nil rejection should fire on '*' as\n"
        " * well as '+'.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    println(nil * 5)\n"
        "}\n"
        "\n"
    },
    {
        "E3183_multi_return_too_few_vars",
        "E3183",
        "/*\n"
        " * Error Test: E3183 - too few variables for multi-return destructuring\n"
        " */\n"
        "// expect-error: E3183\n"
        "// expect-error-count: 1\n"
        "\n"
        "do get_three() -> (i64, i64, i64) {\n"
        "    return 1, 2, 3\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a, b = get_three()\n"
        "}\n"
        "\n"
    },
    {
        "E3184_return_value_from_void_function",
        "E3184",
        "/*\n"
        " * Error Test: E3184 - returning a value from a void function\n"
        " */\n"
        "// expect-error: E3184\n"
        "// expect-error-count: 1\n"
        "\n"
        "do helper() {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    helper()\n"
        "}\n"
        "\n"
    },
    {
        "E3185_void_return_type",
        "E3185",
        "/*\n"
        " * Error Test: E3185 - void return type\n"
        " * Expected: \"missing return value; function expects a return value\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    test()\n"
        "}\n"
        "\n"
        "do test() -> void {  // Should fail: void cannot be used as return type\n"
        "    return\n"
        "}\n"
        "\n"
    },
    {
        "E3186_dot_call_non_struct",
        "E3186",
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3}\n"
        "    arr.banana(4)\n"
        "}\n"
        "\n"
    },
    {
        "E3187_void_call_as_println_argument",
        "E3187",
        "/*\n"
        " * Error Test: E3187 - void call passed to println\n"
        " */\n"
        "// expect-error: E3187\n"
        "\n"
        "do nothing() { }\n"
        "\n"
        "do main() {\n"
        "    println(nothing())\n"
        "}\n"
        "\n"
    },
    {
        "E3188_too_many_return_values",
        "E3188",
        "/*\n"
        " * Error Test: E3188 - return with too many values\n"
        " */\n"
        "// expect-error: E3188\n"
        "// expect-error-count: 1\n"
        "\n"
        "do pair() -> (i64, i64) {\n"
        "    return 1, 2, 3\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a, b = pair()\n"
        "    println(\"${a} ${b}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3189_strict_when_bool_missing_false",
        "E3189",
        "// expect-error: E3189\n"
        "/*\n"
        " * Error Test: E3189 - #strict when over a bool subject must handle both\n"
        " * true and false; this one never handles false.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut flag bool = true\n"
        "    #strict\n"
        "    when flag {\n"
        "        is true { println(\"yes\") }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3190_strict_when_non_enum_without_default",
        "E3190",
        "/*\n"
        " * Error Test: E3190 - #strict when on a non-enum value needs a default branch\n"
        " */\n"
        "// expect-error: E3190\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut count i64 = 3\n"
        "    #strict\n"
        "    when count {\n"
        "        is 1 {\n"
        "            println(\"one\")\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3191_error_has_no_such_field",
        "E3191",
        "/*\n"
        " * Error Test: E3191 - Error has only the fields msg and code\n"
        " */\n"
        "// expect-error: E3191\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut failure Error = error(ErrorCode.Unknown, \"boom\")\n"
        "    println(failure.detail)\n"
        "}\n"
        "\n"
    },
    {
        "E3192_const_pointer_through_pointer_to_pointer",
        "E3192",
        "/*\n"
        " * Error Test: E3192 - a write through a pointer to a pointer at a\n"
        " * const-declared variable\n"
        " */\n"
        "\n"
        "// expect-error: E3192\n"
        "// expect-error-count: 1\n"
        "\n"
        "const Box struct {\n"
        "    v i64\n"
        "}\n"
        "const K Box = Box{v: 1}\n"
        "\n"
        "do main() {\n"
        "    mut p ^Box = addr(K)\n"
        "    mut pp ^^Box = addr(p)\n"
        "    pp^^.v = 9\n"
        "}\n"
        "\n"
    },
    {
        "E3193_const_pointer_to_writing_function",
        "E3193",
        "/*\n"
        " * Error Test: E3193 - pointer to a const-declared variable passed to a\n"
        " * function that writes through it\n"
        " * Expected: E3122 error — the callee would modify the constant\n"
        " */\n"
        "\n"
        "const Counter struct {\n"
        "    n i64\n"
        "}\n"
        "\n"
        "do bump(c ^Counter) {\n"
        "    c^.n += 1\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    const k Counter = Counter{n: 5}\n"
        "    bump(addr(k))\n"
        "}\n"
        "\n"
    },
    {
        "E3194_zero_literal_array_size",
        "E3194",
        "/*\n"
        " * Error Test: E3194 - a literal `0` written directly in a fixed-size array's\n"
        " * size position. The size >0 check only ran along the const-identifier\n"
        " * resolution path; a literal digit skipped it entirely and compiled clean.\n"
        " */\n"
        "// expect-error: E3194\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    const f [i64, 0] = {}\n"
        "    println(len(f))\n"
        "}\n"
        "\n"
    },
    {
        "E3195_maps_is_equal_different_map_types",
        "E3195",
        "/*\n"
        " * Error Test: E3195 - comparing maps whose key or value types differ\n"
        " */\n"
        "// expect-error: E3195\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @maps\n"
        "\n"
        "do main() {\n"
        "    mut left map[string:i64] = {\"a\": 1}\n"
        "    mut right map[string:string] = {\"a\": \"x\"}\n"
        "    mut same bool = maps.is_equal(left, right)\n"
        "    println(same)\n"
        "}\n"
        "\n"
    },
    {
        "E3196_infer_type_from_nil",
        "E3196",
        "/*\n"
        " * Error Test: E3196 - a variable initialized with nil has no type to infer\n"
        " */\n"
        "// expect-error: E3196\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut empty = nil\n"
        "}\n"
        "\n"
    },
    {
        "E3199_generic_key_value_mismatch",
        "E3199",
        "/*\n"
        " * Error Test: E3199 - map[kind:kind] requires key type to equal value type\n"
        " *\n"
        " * One type argument stands for both slots, so the argument's key type must\n"
        " * equal its value type. Otherwise the argument does not match the parameter's\n"
        " * type and the call must fail cleanly.\n"
        " */\n"
        "// expect-error: E3199\n"
        "// expect-error-count: 1\n"
        "\n"
        "do count(kind generic, m map[kind:kind]) -> i64 {\n"
        "    mut n i64 = 0\n"
        "    for_each k, v in m {\n"
        "        n += 1\n"
        "    }\n"
        "    return n\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut mixed map[string:i64] = {\"a\": 1, \"b\": 2}\n"
        "    mut n i64 = count(string, mixed)  // key type != value type → reject\n"
        "}\n"
        "\n"
    },
    {
        "E3200_generic_struct_function_conflict",
        "E3200",
        "/*\n"
        " * Error Test: E3200 - an argument of a generic struct function whose parameter\n"
        " * is typed by a generic name must have the type that name is bound to.\n"
        " * Expected: \"'kind' of struct function 'Box.wrap' is i64, but argument 3 is string\"\n"
        " */\n"
        "// expect-error: E3200\n"
        "// expect-error-count: 1\n"
        "\n"
        "const Box struct {\n"
        "    value i64\n"
        "\n"
        "    do wrap(self Box, kind generic, item kind) -> kind {\n"
        "        return item\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut b = Box{value: 1}\n"
        "    println(b.wrap(i64, \"not an i64\"))\n"
        "}\n"
        "\n"
    },
    {
        "E3201_modulo_by_zero",
        "E3201",
        "/*\n"
        " * Error Test: E3201 - modulo-by-zero\n"
        " * Expected: \"modulo\" or \"zero\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 10 % 0  // modulo by zero\n"
        "}\n"
        "\n"
    },
    {
        "E3202_pointer_in_interpolation",
        "E3202",
        "/*\n"
        " * Error Test: E3202 - pointer value in string interpolation\n"
        " *\n"
        " * \"${ptr}\" where ptr is ^T used to leak to\n"
        " * clang the same way as structs did. Typechecker now rejects with\n"
        " * a hint to dereference with ^ or format the pointee.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut v i64 = 42\n"
        "    mut p ^i64 = addr(v)\n"
        "    println(\"ptr: ${p}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3203_negative_literal_index",
        "E3203",
        "/*\n"
        " * Error Test: E3203 - negative literal array index\n"
        " * Expected: array index cannot be negative\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut arr [i64] = {1, 2, 3}\n"
        "    println(arr[-1])\n"
        "}\n"
        "\n"
    },
    {
        "E3204_for_in_not_range",
        "E3204",
        "/*\n"
        " * Error Test: E3204 - 'for x in' used with something that is not a range\n"
        " * Expected: error directing to use for_each instead\n"
        " */\n"
        "\n"
        "// expect-error: E3204\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut values [i64] = {1, 2, 3}\n"
        "    for x in values {\n"
        "        println(\"${x}\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3205_range_println",
        "E3205",
        "/*\n"
        " * Error Test: E3205 - a range cannot be printed\n"
        " */\n"
        "\n"
        "// expect-error: E3205\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut r = range(0, 3)\n"
        "    println(r)\n"
        "}\n"
        "\n"
    },
    {
        "E3206_range_step_zero_literal",
        "E3206",
        "/*\n"
        " * Error Test: E3206 - a literal zero step is rejected at compile time\n"
        " */\n"
        "\n"
        "// expect-error: E3206\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    for i in range(0, 10, 0) {\n"
        "        println(i)\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E3207_or_default_too_many_fallbacks",
        "E3207",
        "/*\n"
        " * Error Test: E3207 - two fallbacks for a call returning one value\n"
        " */\n"
        "// expect-error: E3207\n"
        "\n"
        "do one() -> (i64, Error) {\n"
        "    return 1, nil\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a = one() or_default 1, 2\n"
        "    println(a)\n"
        "}\n"
        "\n"
    },
    {
        "E3208_or_default_wrong_type",
        "E3208",
        "/*\n"
        " * Error Test: E3208 - fallback of the wrong type\n"
        " */\n"
        "// expect-error: E3208\n"
        "\n"
        "do one() -> (i64, Error) {\n"
        "    return 1, nil\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a = one() or_default \"text\"\n"
        "    println(a)\n"
        "}\n"
        "\n"
    },
    {
        "E3209_enum_names_on_flags_enum",
        "E3209",
        "/*\n"
        " * Error Test: E3209 - names() on a flags enum\n"
        " */\n"
        "// expect-error: E3209\n"
        "\n"
        "#flags\n"
        "const Perms enum { READ; WRITE; EXEC }\n"
        "\n"
        "do main() {\n"
        "    println(Perms.names())\n"
        "}\n"
        "\n"
    },
    {
        "E3210_json_parse_as_call_argument",
        "E3210",
        "/*\n"
        " * Error Test: E3210 - json.parse() as a call argument has no declared target\n"
        " * type; it used to leak a C error passing GrayMap to a GrayString parameter.\n"
        " */\n"
        "\n"
        "import @json\n"
        "\n"
        "do main() {\n"
        "    println(json.encode(json.parse(\"{\\\"a\\\": 1}\")))\n"
        "}\n"
        "\n"
    },
    {
        "E3211_or_panic_on_non_call",
        "E3211",
        "/* Error Test: E3211 - or_panic on a non-call expression */\n"
        "do main() {\n"
        "    mut n = 5 or_panic\n"
        "    println(n)\n"
        "}\n"
        "\n"
    },
    {
        "E3212_or_on_float",
        "E3212",
        "/* Error Test: E3212 - '||' used on an f64 operand */\n"
        "do main() {\n"
        "    mut ratio f64 = 1.5\n"
        "    mut r bool = true || ratio\n"
        "    println(r)\n"
        "}\n"
        "\n"
    },
    {
        "E3213_enum_auto_increment_past_i64",
        "E3213",
        "/* Error Test: E3213 - a variant after the largest i64 value wraps */\n"
        "const E enum {\n"
        "    A = 9223372036854775807\n"
        "    B\n"
        "    C\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"${E.B}\")\n"
        "}\n"
        "\n"
    },
    {
        "E3215_distinct_alias_distinct_array",
        "E3215",
        "/*\n"
        " * Error Test: E3215 - distinct alias: distinct array\n"
        " */\n"
        "\n"
        "// expect-error: E3215\n"
        "// expect-error-count: 1\n"
        "\n"
        "distinct alias UserId = i64\n"
        "distinct alias OrderId = i64\n"
        "distinct alias Score = f64\n"
        "distinct alias Flag = bool\n"
        "distinct alias Letter = char\n"
        "distinct alias Label = string\n"
        "distinct alias Tiny = u8\n"
        "distinct alias Huge = i128\n"
        "distinct alias Wide = u256\n"
        "const Pt struct {\n"
        "    id UserId\n"
        "    tag Label\n"
        "}\n"
        "distinct alias Xs = [i64]\n"
        "do main() {\n"
        "    println(1)\n"
        "}\n"
        "\n"
    },
    {
        "E4001_multi_declaration_initializer_sees_earlier_name",
        "E4001",
        "do main() {\n"
        "    mut p, q = 1, p\n"
        "    println(\"${p} ${q}\")\n"
        "}\n"
        "\n"
    },
    {
        "E4002_undefined_function",
        "E4002",
        "/*\n"
        " * Error Test: E4002 - undefined-function\n"
        " * Expected: \"undefined function\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    nonexistent_func()  // Function doesn't exist\n"
        "}\n"
        "\n"
    },
    {
        "E4003_duplicate_declaration",
        "E4003",
        "/*\n"
        " * Error Test: E4003 - duplicate-declaration\n"
        " * Expected: \"duplicate\" or \"already declared\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    mut x i64 = 10  // duplicate declaration\n"
        "}\n"
        "\n"
    },
    {
        "E4004_duplicate_function",
        "E4004",
        "/*\n"
        " * Error Test: E4004 - function already declared\n"
        " * Expected: \"function 'greet' already declared\"\n"
        " */\n"
        "\n"
        "do greet() {\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
        "do greet() {\n"
        "    println(\"hi\")\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    greet()\n"
        "}\n"
        "\n"
    },
    {
        "E4005_unknown_stdlib_math_fn",
        "E4005",
        "/*\n"
        " * Error Test: E4005 - typo'd call on the @math stdlib module\n"
        " *\n"
        " */\n"
        "\n"
        "import @math\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = math.bogus(5.0)\n"
        "    println(x)\n"
        "}\n"
        "\n"
    },
    {
        "E4006_reserved_prefix_ez_",
        "E4006",
        "/*\n"
        " * Error Test: E4006 - reserved-prefix\n"
        " * Expected: \"name '...' uses reserved prefix (gray_, _gray_, Gray)\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut gray_value i64 = 42\n"
        "    println(\"${gray_value}\")\n"
        "}\n"
        "\n"
    },
    {
        "E4007_duplicate_struct_name",
        "E4007",
        "/*\n"
        " * Error Test: E4007 - duplicate struct name\n"
        " * Expected: \"a type named 'Point' is already declared\" error\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "const Point struct {\n"
        "    a f64\n"
        "    b f64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = new(Point)\n"
        "}\n"
        "\n"
    },
    {
        "E4008_main_with_return_type",
        "E4008",
        "/*\n"
        " * Error Test: E4008 - main() with return type\n"
        " * Expected: \"main function cannot have a return type\" error\n"
        " */\n"
        "\n"
        "do main() -> i64 {\n"
        "    return 0\n"
        "}\n"
        "\n"
    },
    {
        "E4012_var_shadows_type",
        "E4012",
        "/*\n"
        " * Error Test: E4012 - variable shadows a type definition\n"
        " * Expected: \"variable 'Point' shadows a type definition with the same name\"\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut Point i64 = 5\n"
        "}\n"
        "\n"
    },
    {
        "E4013_shadows_function",
        "E4013",
        "/*\n"
        " * Error Test: E4013 - variable shadows function name\n"
        " * Expected: compiler should reject this with E4013\n"
        " */\n"
        "\n"
        "do helper() { println(\"hi\") }\n"
        "\n"
        "do main() {\n"
        "    mut helper = \"shadowed\"\n"
        "}\n"
        "\n"
    },
    {
        "E4016_undefined_type_name_near_statement_word",
        "E4016",
        "// expect-error: E4016\n"
        "\n"
        "do main() {\n"
        "    mut assrt Strng\n"
        "}\n"
        "\n"
    },
    {
        "E4017_funcref_private_struct_fn",
        "E4017",
        "/*\n"
        " * Error Test: E4017 - func reference to private struct function from outside\n"
        " * Expected: E4017 error, compilation fails\n"
        " */\n"
        "\n"
        "const Calc struct {\n"
        "    value i64\n"
        "\n"
        "    private do secret(n i64) -> i64 {\n"
        "        return n * 99\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    const f = ()Calc.secret\n"
        "    println(f(2))\n"
        "}\n"
        "\n"
    },
    {
        "E4018_dot_call_nonexistent_struct_func",
        "E4018",
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p Point = Point{x: 1, y: 2}\n"
        "    p.banana()\n"
        "}\n"
        "\n"
    },
    {
        "E4019_funcref_builtin",
        "E4019",
        "/*\n"
        " * Error Test: E4019 - func reference to builtin function\n"
        " * Expected: E4019 error, compilation fails\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    const f = ()println\n"
        "}\n"
        "\n"
    },
    {
        "E4020_alias_duplicate",
        "E4020",
        "/*\n"
        " * Error Test: E4020 - duplicate type alias\n"
        " * Expected: type alias is already declared\n"
        " */\n"
        "\n"
        "alias Meters = f64\n"
        "alias Meters = i64\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E4022_struct_func_shadows_top_level",
        "E4022",
        "/*\n"
        " * Error Test: E4022 - a struct function sharing a name with a top-level\n"
        " * function shadows it for bare calls inside the struct\n"
        " * Expected: \"struct function 'Actor.rename' conflicts with the top-level function 'rename'\"\n"
        " */\n"
        "// expect-error: E4022\n"
        "// expect-error-count: 1\n"
        "\n"
        "do rename(n string) -> string {\n"
        "    return n\n"
        "}\n"
        "\n"
        "const Actor struct {\n"
        "    id i64\n"
        "    name string\n"
        "\n"
        "    do rename(a Actor, n string) -> Actor {\n"
        "        return Actor{id: a.id, name: n}\n"
        "    }\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut a Actor = Actor{id: 1, name: \"alice\"}\n"
        "    println(rename(a.name))\n"
        "}\n"
        "\n"
    },
    {
        "E4024_unknown_stdlib_module_member",
        "E4024",
        "/*\n"
        " * Error Test: E4024 - a stdlib module member that does not exist.\n"
        " * Expected: the unknown member is named. An annotated declaration used to\n"
        " * accept this silently, because the member typed as unknown and the checks\n"
        " * downstream skip unknown to avoid cascading.\n"
        " */\n"
        "\n"
        "// expect-error: E4024\n"
        "\n"
        "import @math\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = math.NOT_A_CONSTANT\n"
        "    println(\"${x}\")\n"
        "}\n"
        "\n"
    },
    {
        "E4026_alias_named_main",
        "E4026",
        "// expect-error: E4026\n"
        "// expect-error-count: 1\n"
        "// A type alias cannot be named 'main'.\n"
        "\n"
        "alias main = i64\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E4027_return_keyword_as_param",
        "E4027",
        "/*\n"
        " * Error Test: E4027 - return keyword as parameter name\n"
        " */\n"
        "// expect-error: E4027\n"
        "// expect-error-count: 1\n"
        "\n"
        "do test(return i64) {\n"
        "    return\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E4028_parameter_named_after_module",
        "E4028",
        "/*\n"
        " * Error Test: E4028 - parameter named after a stdlib module\n"
        " */\n"
        "// expect-error: E4028\n"
        "\n"
        "do f(math i64) { }\n"
        "\n"
        "do main() { }\n"
        "\n"
    },
    {
        "E4029_invalid_private_usage",
        "E4029",
        "/*\n"
        " * Error Test: E4029 - private modifier inside a function\n"
        " * Expected: \"private modifier cannot be used here\"\n"
        " */\n"
        "// expect-error: E4029\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    private mut x i64 = 5  // private not allowed inside functions\n"
        "}\n"
        "\n"
    },
    {
        "E4031_ambiguous_using_call",
        "E4031",
        "/*\n"
        " * Error Test: E4031 - two `import and use` modules both provide `escape`,\n"
        " * so an unqualified call is ambiguous and must be qualified.\n"
        " * Expected: \"'escape' is provided by more than one module in scope\n"
        " *            ('regex' and 'chars'); call it qualified, e.g. 'regex.escape'\"\n"
        " */\n"
        "// expect-error: E4031\n"
        "// expect-error-count: 1\n"
        "\n"
        "import and use @regex\n"
        "import and use @chars\n"
        "\n"
        "do main() {\n"
        "    println(escape('A'))\n"
        "}\n"
        "\n"
    },
    {
        "E4035_no_main",
        "E4035",
        "/*\n"
        " * Error Test: E4035 - no-main-function\n"
        " * Expected: \"must define a main() function\"\n"
        " */\n"
        "\n"
        "do helper() {\n"
        "    println(\"I'm not main!\")\n"
        "}\n"
        "\n"
    },
    {
        "E4036_func_conflicts_struct",
        "E4036",
        "/*\n"
        " * Error Test: E4036 - function name conflicts with struct type\n"
        " * Expected: \"function 'Point' conflicts with a type of the same name\" error\n"
        " */\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do Point() -> i64 {\n"
        "    return 0\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut p = new(Point)\n"
        "}\n"
        "\n"
    },
    {
        "E4037_removed_type_name_stdlib_type_arg",
        "E4037",
        "// Error Test: E4037 - a removed type name passed as a stdlib type argument\n"
        "// points to the sized type that replaces it.\n"
        "// expect-error: E4037\n"
        "// expect-error-count: 1\n"
        "// expect-contains: use 'i64', or declare 'alias int = i64'\n"
        "\n"
        "import @mem\n"
        "\n"
        "unsafe do main() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mut p = mem.init(a, int)\n"
        "}\n"
        "\n"
    },
    {
        "E4038_funcref_stdlib_module",
        "E4038",
        "/*\n"
        " * Error Test: E4038 - func reference to stdlib module function\n"
        " * Expected: E4019 error, compilation fails\n"
        " */\n"
        "\n"
        "import @math\n"
        "\n"
        "do main() {\n"
        "    const f = ()math.sqrt\n"
        "}\n"
        "\n"
    },
    {
        "E4039_generic_used_before_declared",
        "E4039",
        "/*\n"
        " * Error Test: E4039 - a generic parameter must be declared before any\n"
        " * parameter whose type uses its name.\n"
        " */\n"
        "// expect-error: E4039\n"
        "// expect-error-count: 1\n"
        "\n"
        "do identity(x kind, kind generic) -> kind {\n"
        "    return x\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(identity(1, i64))\n"
        "}\n"
        "\n"
    },
    {
        "E4041_misspelled_statement_alone",
        "E4041",
        "// expect-error: E4041\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 1\n"
        "    for k in range(0, 2) {\n"
        "        brak\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E5007_const_map_clear",
        "E5007",
        "/*\n"
        " * Error Test: E5007 - clear on const map\n"
        " * Expected: cannot modify immutable map\n"
        " */\n"
        "\n"
        "import @maps\n"
        "\n"
        "do main() {\n"
        "    const m map[string:i64] = {\"a\": 1}\n"
        "    maps.clear(m)\n"
        "}\n"
        "\n"
    },
    {
        "E5008_rand_i64_one_argument",
        "E5008",
        "// expect-error: E5008\n"
        "// random.rand_i64 takes both min and max.\n"
        "\n"
        "import @random\n"
        "\n"
        "do main() {\n"
        "    mut n i64 = random.rand_i64(5)\n"
        "    println(n)\n"
        "}\n"
        "\n"
    },
    {
        "E5009_invalid_strconv_base",
        "E5009",
        "/*\n"
        " * Error Test: E5009 - invalid base for integer conversion\n"
        " * Expected: \"invalid base\" error at compile time\n"
        " */\n"
        "\n"
        "import @strconv\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = strconv.to_i64(\"42\", 1)\n"
        "}\n"
        "\n"
    },
    {
        "E5011_maps_find_key_discarded",
        "E5011",
        "import @maps\n"
        "\n"
        "do main() {\n"
        "    mut m map[string:i64] = {\"a\": 1}\n"
        "    maps.find_key(m, 1)\n"
        "}\n"
        "\n"
    },
    {
        "E5012_multi_declare_blank_no_value",
        "E5012",
        "/*\n"
        " * Error Test: E5012 - '_' cannot be declared with no value\n"
        " */\n"
        "// expect-error: E5012\n"
        "\n"
        "do main() {\n"
        "    mut x, _ i64\n"
        "}\n"
        "\n"
    },
    {
        "E5013_file_scope_func_call",
        "E5013",
        "/*\n"
        " * Error Test: E5013 - function call in file-scope initializer\n"
        " * Expected: \"function calls are not allowed in file-scope initializers\"\n"
        " */\n"
        "\n"
        "do compute() -> i64 {\n"
        "    return 42\n"
        "}\n"
        "\n"
        "mut result i64 = compute()\n"
        "\n"
        "do main() {\n"
        "    println(\"${result}\")\n"
        "}\n"
        "\n"
    },
    {
        "E5014_here_with_args",
        "E5014",
        "/*\n"
        " * Error Test: E5014 - here() called with arguments\n"
        " *\n"
        " * The here() builtin substitutes the call site's file, line, and\n"
        " * column at compile time and takes no arguments. Passing any\n"
        " * argument should fire E5014.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut loc SourceLocation = here(42)\n"
        "    println(\"${loc.line}\")\n"
        "}\n"
        "\n"
    },
    {
        "E5015_postfix_requires_identifier",
        "E5015",
        "/*\n"
        " * Error Test: E5015 - postfix-requires-identifier\n"
        " * Expected: \"postfix\" or \"identifier\" or \"variable\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    5++  // postfix operator needs variable\n"
        "}\n"
        "\n"
    },
    {
        "E5016_fields_shadows_builtin",
        "E5016",
        "// expect-error: E5016\n"
        "/*\n"
        " * Error Test: E5016 - 'fields' used as a variable name\n"
        " *\n"
        " * Builtin functions cannot be shadowed by user variables, even when their\n"
        " * names are handled outside the original reserved-name list.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut fields i64 = 1\n"
        "}\n"
        "\n"
    },
    {
        "E5017_embed_non_literal",
        "E5017",
        "/*\n"
        " * Error Test: E5017 - embed() argument must be a string literal\n"
        " * Expected: error when a variable is passed instead of a string literal\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut path string = \"config.txt\"\n"
        "    const content string = embed(path)\n"
        "    println(content)\n"
        "}\n"
        "\n"
    },
    {
        "E5018_embed_file_not_found",
        "E5018",
        "/*\n"
        " * Error Test: E5018 - embed() cannot open file\n"
        " * Expected: error when the embedded file does not exist\n"
        " */\n"
        "\n"
        "const Missing string = embed(\"this_file_does_not_exist.txt\")\n"
        "\n"
        "do main() {\n"
        "    println(Missing)\n"
        "}\n"
        "\n"
    },
    {
        "E5023_postfix_requires_integer",
        "E5023",
        "/*\n"
        " * Error Test: E5023 - postfix-requires-integer\n"
        " * Expected: \"postfix operator requires integer operand\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = 3.14\n"
        "    x++  // postfix operator needs integer operand\n"
        "}\n"
        "\n"
    },
    {
        "E5025_assign_to_literal",
        "E5025",
        "/*\n"
        " * Error Test: E5025 - assignment to a literal\n"
        " * Expected: \"cannot assign to this expression\" error\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    5 = 10\n"
        "}\n"
        "\n"
    },
    {
        "E5026_atomic_add_u64_pointer",
        "E5026",
        "import @atomic\n"
        "\n"
        "do main() {\n"
        "    mut u u64 = 1\n"
        "    println(atomic.add(addr(u), 1))\n"
        "}\n"
        "\n"
    },
    {
        "E5028_print_func_ref",
        "E5028",
        "/*\n"
        " * Error Test: E5028 - func reference passed to print function\n"
        " * Expected: compile-time error rejecting func ref as print argument\n"
        " */\n"
        "\n"
        "do double(n i64) -> i64 { return n * 2 }\n"
        "\n"
        "do main() {\n"
        "    const f = ()double\n"
        "    println(f)\n"
        "}\n"
        "\n"
    },
    {
        "E5029_copy_func_ref",
        "E5029",
        "/*\n"
        " * Error Test: E5029 - copy() on a func reference\n"
        " * Expected: compile-time error\n"
        " */\n"
        "\n"
        "do double(n i64) -> i64 { return n * 2 }\n"
        "\n"
        "do main() {\n"
        "    const f = ()double\n"
        "    const g = copy(f)\n"
        "}\n"
        "\n"
    },
    {
        "E5030_chained_call_on_func_return",
        "E5030",
        "do double(n i64) -> i64 { return n * 2 }\n"
        "do get_fn() -> func(i64) -> i64 { return ()double }\n"
        "do main() {\n"
        "    println(get_fn()(5))\n"
        "}\n"
        "\n"
    },
    {
        "E5031_unknown_named_arg",
        "E5031",
        "/*\n"
        " * Error Test: E5031 - unknown parameter name in named argument\n"
        " * Expected: \"unknown parameter name 'z' in call to 'add'\"\n"
        " */\n"
        "\n"
        "do add(a i64, b i64) -> i64 {\n"
        "    return a + b\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = add(z: 5, b: 10)\n"
        "}\n"
        "\n"
    },
    {
        "E5032_named_arg_already_positional",
        "E5032",
        "/*\n"
        " * Error Test: E5032 - parameter already provided positionally\n"
        " * Expected: \"parameter 'a' is already provided positionally (argument 1)\"\n"
        " */\n"
        "\n"
        "do add(a i64, b i64) -> i64 {\n"
        "    return a + b\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = add(1, a: 5)\n"
        "}\n"
        "\n"
    },
    {
        "E5033_positional_after_named",
        "E5033",
        "/*\n"
        " * Error Test: E5033 - positional argument after named argument\n"
        " * Expected: \"positional argument after named argument in call to 'add'\"\n"
        " */\n"
        "\n"
        "do add(a i64, b i64) -> i64 {\n"
        "    return a + b\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = add(a: 1, 2)\n"
        "}\n"
        "\n"
    },
    {
        "E5034_named_args_builtin",
        "E5034",
        "/*\n"
        " * Error Test: E5034 - named arguments on builtin function\n"
        " * Expected: \"named arguments are not supported for builtin function 'println'\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    println(value: \"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "E5035_alias_named_after_module",
        "E5035",
        "/*\n"
        " * Error Test: E5035 - alias named after a stdlib module\n"
        " */\n"
        "// expect-error: E5035\n"
        "\n"
        "const math = i64\n"
        "\n"
        "do main() { }\n"
        "\n"
    },
    {
        "E5036_sized_type_as_function",
        "E5036",
        "do main() {\n"
        "    mut a = i8(100)\n"
        "    mut b = i16(200)\n"
        "    mut c = i32(300)\n"
        "    mut d = i64(400)\n"
        "    mut e = u8(50)\n"
        "    mut f = u16(600)\n"
        "    mut g = u32(700)\n"
        "    mut h = u64(800)\n"
        "    mut i = f32(1.5)\n"
        "    mut j = f64(2.5)\n"
        "}\n"
        "\n"
    },
    {
        "E5037_copy_pointer",
        "E5037",
        "/* Error Test: E5037 - copy() on a pointer type */\n"
        "const Point struct {\n"
        "    x i64\n"
        "}\n"
        "do main() {\n"
        "    mut p = new(Point)\n"
        "    mut q = copy(p)\n"
        "    println(q)\n"
        "}\n"
        "\n"
    },
    {
        "E5038_tagged_enum_print",
        "E5038",
        "const Shape enum {\n"
        "    Circle(f64)\n"
        "    Rect(f64, f64)\n"
        "    Point\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s Shape = Shape.Point\n"
        "    print(s)\n"
        "}\n"
        "\n"
    },
    {
        "E5039_const_i64_overflow",
        "E5039",
        "/*\n"
        " * Error Test: E5039 - constant expression overflows type 'i64'\n"
        " * Expected: overflow in const arithmetic expression is a compile-time error\n"
        " */\n"
        "\n"
        "const MAX i64 = 9223372036854775807\n"
        "const OVER i64 = MAX + 1\n"
        "\n"
        "do main() {\n"
        "    println(OVER)\n"
        "}\n"
        "\n"
    },
    {
        "E5040_const_runtime_call",
        "E5040",
        "do main() {\n"
        "    const foobar = input()\n"
        "}\n"
        "\n"
    },
    {
        "E5042_discard_void",
        "E5042",
        "/*\n"
        " * Error Test: E5042 - #discard on void function\n"
        " * Expected: \"discard\" and \"void\"\n"
        " */\n"
        "\n"
        "#discard\n"
        "do doNothing() {\n"
        "    println(\"side effect only\")\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    doNothing()\n"
        "}\n"
        "\n"
    },
    {
        "E5043_fields_type_name",
        "E5043",
        "const Foo struct {\n"
        "    a i64\n"
        "    b string\n"
        "    c bool\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(fields(Foo))\n"
        "}\n"
        "\n"
    },
    {
        "E5045_strings_repeat_negative_const",
        "E5045",
        "/*\n"
        " * Error Test: E5045 - constant argument outside a stdlib function's domain\n"
        " * strings.repeat with a negative constant count (runtime P0072).\n"
        " *\n"
        " * expect-error: E5045\n"
        " */\n"
        "\n"
        "import @strings\n"
        "\n"
        "do main() {\n"
        "    println(strings.repeat(\"x\", -3))\n"
        "}\n"
        "\n"
    },
    {
        "E5049_empty_array_literal_returned_as_map",
        "E5049",
        "do pick() -> map[string:i64] {\n"
        "    return {}\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"${len(pick())}\")\n"
        "}\n"
        "\n"
    },
    {
        "E5051_struct_field_append",
        "E5051",
        "/*\n"
        " * Error Test: E5051 - length-changing array op on a fixed-size struct field\n"
        " * Expected: compiler should reject this with E5051\n"
        " */\n"
        "// expect-error: E5051\n"
        "\n"
        "import @arrays\n"
        "\n"
        "const Buffer struct {\n"
        "    data [i64, 4]\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut b Buffer = Buffer{data: {1, 2, 3, 4}}\n"
        "    arrays.append(b.data, 5)  // ERROR: 'data' is a fixed-size array field\n"
        "}\n"
        "\n"
    },
    {
        "E5056_math_sqrt_negative_const",
        "E5056",
        "/*\n"
        " * Error Test: E5056 - constant argument outside a stdlib function's domain\n"
        " * math.sqrt of a negative constant is decidable at compile time (runtime P0064).\n"
        " *\n"
        " * expect-error: E5056\n"
        " */\n"
        "\n"
        "import @math\n"
        "\n"
        "do main() {\n"
        "    println(math.sqrt(-4.0))\n"
        "}\n"
        "\n"
    },
    {
        "E5057_math_max_mixed_number_types",
        "E5057",
        "/*\n"
        " * Error Test: E5057 - math.max arguments must share one number type\n"
        " */\n"
        "// expect-error: E5057\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @math\n"
        "\n"
        "do main() {\n"
        "    mut whole i64 = 1\n"
        "    mut fraction f64 = 2.5\n"
        "    println(math.max(whole, fraction))\n"
        "}\n"
        "\n"
    },
    {
        "E5058_arrays_is_equal_struct_elements",
        "E5058",
        "/*\n"
        " * Error Test: E5058 - arrays.is_equal only supports primitive and string elements\n"
        " */\n"
        "// expect-error: E5058\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @arrays\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut left [Point] = {Point{x: 1, y: 2}}\n"
        "    mut right [Point] = {Point{x: 1, y: 2}}\n"
        "    println(arrays.is_equal(left, right))\n"
        "}\n"
        "\n"
    },
    {
        "E5060_csv_filter_rows_callback_not_reference",
        "E5060",
        "/*\n"
        " * Error Test: E5060 - csv.filter_rows needs a function reference\n"
        " */\n"
        "// expect-error: E5060\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @csv\n"
        "\n"
        "do main() {\n"
        "    mut rows [[string]] = {{\"a\", \"b\"}}\n"
        "    mut kept [[string]] = csv.filter_rows(rows, 5)\n"
        "    println(len(kept))\n"
        "}\n"
        "\n"
    },
    {
        "E5061_csv_filter_rows_callback_wrong_signature",
        "E5061",
        "/*\n"
        " * Error Test: E5061 - csv.filter_rows callback must be func([string]) -> bool\n"
        " */\n"
        "// expect-error: E5061\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @csv\n"
        "\n"
        "do keep() -> bool {\n"
        "    return true\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut rows [[string]] = {{\"a\", \"b\"}}\n"
        "    mut kept [[string]] = csv.filter_rows(rows, ()keep)\n"
        "    println(len(kept))\n"
        "}\n"
        "\n"
    },
    {
        "E5062_default_param_array_wrong_type",
        "E5062",
        "/*\n"
        " * Error Test: E5062 - default value is not assignable to an array parameter\n"
        " * Expected: \"default value for parameter 'values' has wrong type; expected [i64], got string\" error\n"
        " */\n"
        "// expect-error: E5062\n"
        "// expect-error-count: 1\n"
        "\n"
        "do total(values [i64] = \"none\") {\n"
        "    println(len(values))\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    total()\n"
        "}\n"
        "\n"
    },
    {
        "E5063_to_char_index_out_of_bounds_const",
        "E5063",
        "/*\n"
        " * Error Test: E5063 - constant argument outside a builtin's domain\n"
        " * to_char() with a constant index past the end of a string literal (runtime P0050).\n"
        " *\n"
        " * expect-error: E5063\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    println(to_char(\"abc\", 9))\n"
        "}\n"
        "\n"
    },
    {
        "E5064_call_bare_func_map_element",
        "E5064",
        "/*\n"
        " * Error Test: E5064 - calling a value of a map[string:func]\n"
        " * Expected: E5064 error — a bare func element has no signature to check the call against\n"
        " */\n"
        "// expect-error: E5064\n"
        "// expect-error-count: 1\n"
        "\n"
        "do halve(f f64) -> f64 { return f / 2.0 }\n"
        "\n"
        "do main() {\n"
        "    mut m map[string:func] = {:}\n"
        "    m[\"h\"] = ()halve\n"
        "    println(m[\"h\"](5.0))\n"
        "}\n"
        "\n"
    },
    {
        "E5065_raw_outside_unsafe",
        "E5065",
        "/*\n"
        " * Error Test: E5065 - raw() outside an unsafe block or unsafe function.\n"
        " */\n"
        "// expect-error: E5065\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    mut value i64 = 1\n"
        "    mut pointer = raw(value)\n"
        "    println(pointer^)\n"
        "}\n"
        "\n"
    },
    {
        "E6001_module_not_found",
        "E6001",
        "/*\n"
        " * Error Test: E6001 - module-not-found\n"
        " * Expected: \"unknown module '@nonexistent_module'\"\n"
        " */\n"
        "\n"
        "import @nonexistent_module\n"
        "\n"
        "do main() {\n"
        "    println(\"test\")\n"
        "}\n"
        "\n"
    },
    {
        "E6007_stdlib_import_alias",
        "E6007",
        "/*\n"
        " * Error Test: E6007 - aliasing a standard library import\n"
        " * Expected: \"standard library import '@math' cannot be aliased\"\n"
        " */\n"
        "\n"
        "import m @math\n"
        "\n"
        "do main() {\n"
        "    println(m.abs(-7))\n"
        "}\n"
        "\n"
    },
    {
        "E6008_module_member_readonly",
        "E6008",
        "/*\n"
        " * Error Test: E6008 - module-member-readonly\n"
        " * Expected: \"cannot assign to module member\"\n"
        " */\n"
        "// expect-error: E6008\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @math\n"
        "\n"
        "do main() {\n"
        "    math.PI = 3.0  // Trying to modify stdlib constant\n"
        "}\n"
        "\n"
    },
    {
        "E6010_module_never_imported",
        "E6010",
        "/*\n"
        " * Error Test: E6010 - a call through a module name that was never imported.\n"
        " * Nothing declared it anywhere, and it still passed the typechecker and\n"
        " * reached the C compiler.\n"
        " * Expected: \"unknown module 'completely_made_up'\"\n"
        " */\n"
        "// expect-error: E6010\n"
        "// expect-error-count: 1\n"
        "\n"
        "do main() {\n"
        "    println(\"before\")\n"
        "    completely_made_up.does_not_exist()\n"
        "    println(\"after\")\n"
        "}\n"
        "\n"
    },
    {
        "E6012_json_struct_without_import",
        "E6012",
        "// expect-error: E6012\n"
        "// expect-error-count: 1\n"
        "// A #json struct with no 'import @json' in the file. The generated serializer\n"
        "// helpers reference the json runtime, so this must be rejected up front rather\n"
        "// than leaking a C compiler error.\n"
        "\n"
        "#json\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {}\n"
        "\n"
    },
    {
        "E6014_import_bare_identifier",
        "E6014",
        "/*\n"
        " * Error Test: E6014 - import followed by a bare identifier\n"
        " */\n"
        "// expect-error: E6014\n"
        "\n"
        "import foo\n"
        "\n"
        "do main() { }\n"
        "\n"
    },
    {
        "E7014_char_above_max_codepoint",
        "E7014",
        "do main() {\n"
        "    mut c char = char(1114112)\n"
        "    println(c)\n"
        "}\n"
        "\n"
    },
    {
        "E7015_len_unsupported_type",
        "E7015",
        "/*\n"
        " * Error Test: E7015 - len-unsupported-type\n"
        " * Expected: \"len\" or \"not supported\" or \"unsupported\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = len(42)  // len not supported for i64\n"
        "}\n"
        "\n"
    },
    {
        "E8001_bit_or_on_f64",
        "E8001",
        "/*\n"
        " * Error Test: E8001 - bit_or on f64\n"
        " * Expected: E8001 — 'bit_or' can only be used with integers\n"
        " */\n"
        "do main() {\n"
        "    mut x f64 = 1.5\n"
        "    mut y f64 = 2.5\n"
        "    mut z = x bit_or y\n"
        "}\n"
        "\n"
    },
    {
        "E8002_bit_not_on_f64",
        "E8002",
        "/*\n"
        " * Error Test: E8002 - bit_not on f64\n"
        " * Expected: E8002 — 'bit_not' can only be used with integers\n"
        " */\n"
        "do main() {\n"
        "    mut x f64 = 3.14\n"
        "    mut z = bit_not x\n"
        "}\n"
        "\n"
    },
    {
        "E9002_array_non_numeric",
        "E9002",
        "/*\n"
        " * Error Test: E9002 - array-non-numeric\n"
        " * Expected: \"numeric\" or \"array\"\n"
        " */\n"
        "\n"
        "import @arrays\n"
        "\n"
        "do main() {\n"
        "    mut arr [string] = {\"a\", \"b\", \"c\"}\n"
        "    mut total = arrays.sum(arr)  // sum requires numeric array\n"
        "}\n"
        "\n"
    },
    {
        "E9003_arrays_sort_by_not_func_ref",
        "E9003",
        "/*\n"
        " * Error Test: E9003 - sort_by requires a function reference\n"
        " */\n"
        "// expect-error: E9003\n"
        "\n"
        "import @arrays\n"
        "\n"
        "do main() {\n"
        "    mut numbers [i64] = {3, 1, 2}\n"
        "    arrays.sort_by(numbers, 5)\n"
        "}\n"
        "\n"
    },
    {
        "E9004_arrays_sort_by_func_value_wrong_sig",
        "E9004",
        "import @arrays\n"
        "\n"
        "do wrap(&arr [i64], less func(string, string) -> bool) {\n"
        "    arrays.sort_by(arr, less)\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut xs [i64] = {3, 1, 2}\n"
        "    wrap(xs, ()lt)\n"
        "}\n"
        "\n"
        "do lt(a string, b string) -> bool {\n"
        "    return len(a) < len(b)\n"
        "}\n"
        "\n"
    },
    {
        "E9005_range_invalid_bounds",
        "E9005",
        "/*\n"
        " * Error Test: E9005 - range-invalid-bounds\n"
        " * Expected: \"invalid range\" or \"start\" or \"less than or equal to\"\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    for i in range(10, 5) {\n"
        "        // This should error - start > end\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "E9006_arrays_contains_struct",
        "E9006",
        "/*\n"
        " * Error Test: E9006 - arrays.contains() with struct element type\n"
        " * Expected: E9006 error rejecting composite element type\n"
        " */\n"
        "\n"
        "import @arrays\n"
        "\n"
        "const Point struct {\n"
        "    x i64\n"
        "    y i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut pts [Point] = {Point{x: 1, y: 2}, Point{x: 3, y: 4}}\n"
        "    mut target Point = Point{x: 1, y: 2}\n"
        "    mut found bool = arrays.contains(pts, target)\n"
        "    println(\"${found}\")\n"
        "}\n"
        "\n"
    },
    {
        "E9007_arrays_sort_asc_struct",
        "E9007",
        "/*\n"
        " * Error Test: E9007 - 'arrays.sort_asc()' (and sort_desc/is_sorted/\n"
        " * binary_search) on a struct-element array. Previously had no\n"
        " * comparable-type check at all: sort_asc/sort_desc/is_sorted compiled and\n"
        " * ran clean, silently reinterpreting the struct's raw leading bytes as the\n"
        " * sort key.\n"
        " */\n"
        "// expect-error: E9007\n"
        "// expect-error-count: 1\n"
        "\n"
        "import @arrays\n"
        "\n"
        "const Person struct {\n"
        "    name string\n"
        "    age i64\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut arr [Person] = {Person{name: \"Zed\", age: 1}, Person{name: \"Amy\", age: 2}}\n"
        "    arrays.sort_asc(arr)\n"
        "}\n"
        "\n"
    },
    {
        "W1001_unused_variable",
        "W1001",
        "/*\n"
        " * Warning Test: W1001 - unused-variable\n"
        " * Expected: warning about variable declared but never used\n"
        " */\n"
        "\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 42  // This variable is never used\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "W1002_unused_import",
        "W1002",
        "/*\n"
        " * Warning Test: W1002 - unused-import\n"
        " * Expected: warning about module imported but not used\n"
        " */\n"
        "\n"
        "import @math  // This import is never used\n"
        "\n"
        "do main() {\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "W1003_unused_function",
        "W1003",
        "/*\n"
        " * Warning Test: W1003 - unused-function\n"
        " * Expected: warning about function declared but never called\n"
        " */\n"
        "\n"
        "\n"
        "do unused_helper() -> i64 {\n"
        "    return 42\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(\"hello\")\n"
        "}\n"
        "\n"
    },
    {
        "W1004_no_value_declaration",
        "W1004",
        "/*\n"
        " * Warning Test: W1004 - no-value declaration\n"
        " * Expected: warning that a typed variable declared with no value defaults\n"
        " * to its zero value. Fires the same for implicit and explicit `mut`.\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    foobar i64          // implicit mut, no value\n"
        "    mut baz string      // explicit mut, no value\n"
        "    foobar = 1\n"
        "    baz = \"x\"\n"
        "    println(foobar)\n"
        "    println(baz)\n"
        "}\n"
        "\n"
    },
    {
        "W1005_typed_blank_identifier",
        "W1005",
        "// Test: W1005 typed-blank-identifier warning\n"
        "// Typed blank identifiers should trigger W1005 warning but still run successfully\n"
        "\n"
        "\n"
        "do returnTwo() -> (i64, string) {\n"
        "    return 42, \"hello\"\n"
        "}\n"
        "\n"
        "do returnThree() -> (i64, string, bool) {\n"
        "    return 1, \"two\", true\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    // Test 1: typed blank after named - should warn for the typed blank\n"
        "    mut x i64, _ string = returnTwo()\n"
        "    println(x)\n"
        "\n"
        "    // Test 2: all typed blanks - should warn for both\n"
        "    mut _ i64, _ string = returnTwo()\n"
        "    println(\"discarded both\")\n"
        "\n"
        "    // Test 3: typed blank in the middle\n"
        "    mut a i64, _ string, c bool = returnThree()\n"
        "    println(a)\n"
        "    println(c)\n"
        "\n"
        "    // These should NOT warn (untyped blanks):\n"
        "    mut y, _ = returnTwo()\n"
        "    println(y)\n"
        "\n"
        "    mut _, z = returnTwo()\n"
        "    println(z)\n"
        "}\n"
        "\n"
    },
    {
        "W2002_nested_block_variable_shadows",
        "W2002",
        "/*\n"
        " * Warning Test: W2002 - variable in a nested block shadows an outer variable\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 1\n"
        "    if x > 0 {\n"
        "        mut x i64 = 2\n"
        "        println(x)\n"
        "    }\n"
        "    println(x)\n"
        "}\n"
        "\n"
    },
    {
        "W2003_unreachable_println_after_return",
        "W2003",
        "/*\n"
        " * Warning Test: W2003 - println after return\n"
        " * Expected: warning about unreachable code\n"
        " */\n"
        "\n"
        "\n"
        "do greet() -> string {\n"
        "    return \"hello\"\n"
        "    println(\"goodbye\")  // unreachable\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(greet())\n"
        "}\n"
        "\n"
    },
    {
        "W2007_shadows_global",
        "W2007",
        "/*\n"
        " * Warning Test: W2007 - shadows-global\n"
        " * Expected: warning about variable shadowing a global\n"
        " */\n"
        "\n"
        "\n"
        "const MAX i64 = 100\n"
        "\n"
        "do main() {\n"
        "    mut MAX i64 = 50  // Shadows the global constant MAX\n"
        "    println(MAX)\n"
        "}\n"
        "\n"
    },
    {
        "W2008_param_shadows_enum_variant",
        "W2008",
        "/*\n"
        " * Warning Test: W2008 - parameter shadows enum variant\n"
        " * Verifies that shadowing still compiles and runs correctly\n"
        " */\n"
        "\n"
        "const Dir enum {\n"
        "    NORTH\n"
        "    SOUTH\n"
        "}\n"
        "\n"
        "do move(NORTH i64) -> i64 {\n"
        "    return NORTH + 1\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut passed i64 = 0\n"
        "    mut failed i64 = 0\n"
        "\n"
        "    mut result = move(5)\n"
        "    if result == 6 {\n"
        "        println(\"  [PASS] param shadowing enum variant works\")\n"
        "        passed += 1\n"
        "    } otherwise {\n"
        "        println(\"  [FAIL] param shadowing enum variant: got ${result}\")\n"
        "        failed += 1\n"
        "    }\n"
        "\n"
        "    println(\"Results: ${passed} passed, ${failed} failed\")\n"
        "    if failed > 0 {\n"
        "        println(\"SOME TESTS FAILED\")\n"
        "    } otherwise {\n"
        "        println(\"ALL TESTS PASSED\")\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "W2011_named_return_unused",
        "W2011",
        "/*\n"
        " * Warning Test: W2011 - named return value unused\n"
        " * Expected: W2011 warning — named return 'result' declared in signature\n"
        " * but no matching variable exists in the function body.\n"
        " */\n"
        "\n"
        "do getValue() -> (result i64) {\n"
        "    return 42\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    println(getValue())\n"
        "}\n"
        "\n"
    },
    {
        "W2012_when_f64",
        "W2012",
        "/*\n"
        " * Warning Test: W2012 - when-condition-is-f64\n"
        " * Expected: warning that f64 equality in when is imprecise\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x f64 = 0.5\n"
        "    when x {\n"
        "        is 0.5 { println(\"half\") }\n"
        "        default { println(\"other\") }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "W3003_partial_init_one_element",
        "W3003",
        "/*\n"
        " * Warning Test: W3003 - partial init with 1 of 5 elements\n"
        " * Expected: warning about fixed-size array not fully initialized\n"
        " */\n"
        "\n"
        "\n"
        "do main() {\n"
        "    const arr [i64, 5] = {42}\n"
        "    println(arr)\n"
        "}\n"
        "\n"
    },
    {
        "W3005_implicit_enum_selector",
        "W3005",
        "/*\n"
        " * Warning Test: W3005 - implicit enum selector in when/is\n"
        " * Expected: warning about non-strict when without default\n"
        " */\n"
        "\n"
        "const Direction enum {\n"
        "    NORTH\n"
        "    SOUTH\n"
        "    EAST\n"
        "    WEST\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut d Direction = .NORTH\n"
        "\n"
        "    when d {\n"
        "        is .NORTH {\n"
        "            println(\"north\")\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
    },
    {
        "W3006_empty_default",
        "W3006",
        "/*\n"
        " * Warning Test: W3006 - empty default in when statement\n"
        " * Expected: warning about empty default branch silently ignoring unmatched values\n"
        " */\n"
        "\n"
        "do main() {\n"
        "    mut x i64 = 5\n"
        "    when x {\n"
        "        is 1 { println(\"one\") }\n"
        "        is 2 { println(\"two\") }\n"
        "        is 3 { println(\"three\") }\n"
        "        default { }\n"
        "    }\n"
        "    println(\"done\")\n"
        "}\n"
        "\n"
    },
    {
        "W3007_deprecated_enum",
        "W3007",
        "/*\n"
        " * Warning Test: W3007 - deprecated-enum\n"
        " * Expected: warning when a #deprecated enum's type or a variant of it\n"
        " * is referenced (var-decl annotation, EnumName.VARIANT access).\n"
        " */\n"
        "\n"
        "#deprecated(\"use Status2 instead\")\n"
        "const Status enum {\n"
        "    ACTIVE\n"
        "    INACTIVE\n"
        "}\n"
        "\n"
        "do main() {\n"
        "    mut s Status = Status.ACTIVE // warning: Status is deprecated (both the annotation and the variant access)\n"
        "    println(\"${s}\")\n"
        "}\n"
        "\n"
    },
};

#define DIAGNOSTIC_PROGRAM_COUNT ((int)(sizeof(diagnostic_programs) / sizeof(diagnostic_programs[0])))

#endif
