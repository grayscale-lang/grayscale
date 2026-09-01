/*
 * test_codegen.c — End-to-end tests for grayc code generation that compile
 * Grayscale snippets, run the resulting binaries, and verify output.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#define popen  _popen
#define pclose _pclose
#define unlink _unlink
/* system() and popen() route through cmd.exe, which does not accept "./" as a
 * command prefix -- it reports "'.' is not recognized". Backslash is required. */
#define E2E_COMPILER ".\\grayc.exe"
#define E2E_EXE_SUFFIX ".exe"
/* /tmp is not a real location for a native Windows process; it would resolve
 * against the current drive root. */
static const char *e2e_tmpdir(void) {
    const char *t = getenv("TEMP");
    if (!t || !*t) t = getenv("TMP");
    return (t && *t) ? t : ".";
}
#else
#define E2E_COMPILER "./grayc"
#define E2E_EXE_SUFFIX ""
static const char *e2e_tmpdir(void) { return "/tmp"; }
#endif

static int test_number = 0;

/* Compile a .gray file to a binary. Diagnostics are captured and printed only
 * on failure — a swallowed compiler error once turned "gcc is not on PATH"
 * into 121 opaque NULL-output failures. */
static int e2e_compile(const char *gray_file, const char *binary_file) {
    char command[1024];
    snprintf(command, sizeof(command), E2E_COMPILER " \"%s\" -o \"%s\" 2>&1", gray_file, binary_file);
    FILE *pipe = popen(command, "r");
    if (!pipe) return -1;

    char cc_out[8192];
    size_t total = 0;
    size_t bytes_read;
    while ((bytes_read = fread(cc_out + total, 1, sizeof(cc_out) - total - 1, pipe)) > 0) {
        total += bytes_read;
    }
    cc_out[total] = '\0';

    int rc = pclose(pipe);
    if (rc != 0) {
        fprintf(stderr, "  test %d: compile failed:\n%s", test_number, cc_out);
    }
    return rc;
}

/* Compile and run a Grayscale program, return its stdout output */
static char *compile_and_run(const char *gray_source) {
    test_number++;
    static char output[4096];
    char gray_file[128], binary_file[128];

    snprintf(gray_file, sizeof(gray_file), "%s/grayc_e2e_%d.gray", e2e_tmpdir(), test_number);
    snprintf(binary_file, sizeof(binary_file), "%s/grayc_e2e_%d" E2E_EXE_SUFFIX, e2e_tmpdir(), test_number);

    /* Write source */
    FILE *file = fopen(gray_file, "w");
    if (!file) return NULL;
    fputs(gray_source, file);
    fclose(file);

    /* Compile */
    if (e2e_compile(gray_file, binary_file) != 0) {
        unlink(gray_file);
        return NULL;
    }

    /* Run and capture output */
    char run_cmd[256];
    snprintf(run_cmd, sizeof(run_cmd), "\"%s\" 2>&1", binary_file);
    FILE *pipe = popen(run_cmd, "r");
    if (!pipe) {
        unlink(gray_file);
        unlink(binary_file);
        return NULL;
    }

    size_t total = 0;
    size_t bytes_read;
    while ((bytes_read = fread(output + total, 1, sizeof(output) - total - 1, pipe)) > 0) {
        total += bytes_read;
    }
    output[total] = '\0';
    pclose(pipe);

    /* Text-mode _popen translates CRLF already; strip any '\r' a binary-mode
     * child leaks through so comparisons stay byte-exact. */
    size_t w = 0;
    for (size_t r = 0; r < total; r++) {
        if (output[r] != '\r') output[w++] = output[r];
    }
    total = w;
    output[total] = '\0';

    /* Remove trailing newline for easier comparison */
    if (total > 0 && output[total - 1] == '\n') {
        output[total - 1] = '\0';
    }

    unlink(gray_file);
    unlink(binary_file);
    return output;
}

/* --- Hello World --- */

static void test_e2e_hello(void) {
    char *output = compile_and_run(
        ""
        "do main() { println(\"Hello, World!\") }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "Hello, World!");
}

/* --- Arithmetic --- */

static void test_e2e_arithmetic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    println(2 + 3)\n"
        "    println(10 - 4)\n"
        "    println(3 * 7)\n"
        "    println(20 / 4)\n"
        "    println(17 % 5)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "5\n6\n21\n5\n2");
}

/* --- Variables --- */

static void test_e2e_variables(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut x int = 10\n"
        "    mut y int = 20\n"
        "    println(x + y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "30");
}

/* --- String interpolation --- */

static void test_e2e_interpolation(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut name string = \"Alice\"\n"
        "    mut age int = 30\n"
        "    println(\"${name} is ${age}\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "Alice is 30");
}

/* --- If/or/otherwise --- */

static void test_e2e_if_else(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut x int = 5\n"
        "    if x > 10 {\n"
        "        println(\"big\")\n"
        "    } or x > 3 {\n"
        "        println(\"medium\")\n"
        "    } otherwise {\n"
        "        println(\"small\")\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "medium");
}

/* --- For loop --- */

static void test_e2e_for_range(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    for i in range(1, 4) {\n"
        "        println(i)\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n2\n3");
}

/* --- While loop --- */

static void test_e2e_while(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut i int = 0\n"
        "    as_long_as i < 3 {\n"
        "        println(i)\n"
        "        i++\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0\n1\n2");
}

/* --- Loop with break --- */

static void test_e2e_loop_break(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut i int = 0\n"
        "    loop {\n"
        "        if i >= 3 { break }\n"
        "        println(i)\n"
        "        i++\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0\n1\n2");
}

/* --- Functions --- */

static void test_e2e_function_call(void) {
    char *output = compile_and_run(
        ""
        "do add(a int, b int) -> int { return a + b }\n"
        "do main() { println(add(3, 4)) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "7");
}

/* --- Recursion --- */

static void test_e2e_recursion(void) {
    char *output = compile_and_run(
        ""
        "do fib(n int) -> int {\n"
        "    if n <= 1 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "do main() { println(fib(10)) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "55");
}

/* --- Multiple returns --- */

static void test_e2e_multi_return(void) {
    char *output = compile_and_run(
        ""
        "do swap(a int, b int) -> (int, int) { return b, a }\n"
        "do main() {\n"
        "    mut x int, y int = swap(10, 20)\n"
        "    println(x)\n"
        "    println(y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "20\n10");
}

/* --- Mutable params --- */

static void test_e2e_mutable_parameter(void) {
    char *output = compile_and_run(
        ""
        "do inc(&n int) { n = n + 1 }\n"
        "do main() {\n"
        "    mut x int = 5\n"
        "    inc(x)\n"
        "    println(x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "6");
}

/* --- Ensure --- */

static void test_e2e_ensure(void) {
    char *output = compile_and_run(
        ""
        "do cleanup() { println(\"cleaned\") }\n"
        "do work() {\n"
        "    ensure cleanup()\n"
        "    println(\"working\")\n"
        "}\n"
        "do main() { work() }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "working\ncleaned");
}

/* --- Structs --- */

static void test_e2e_struct(void) {
    char *output = compile_and_run(
        ""
        "const Point struct {\n"
        "    x int\n"
        "    y int\n"
        "}\n"
        "do main() {\n"
        "    mut p Point = Point{x: 3, y: 4}\n"
        "    println(p.x)\n"
        "    println(p.y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "3\n4");
}

/* --- Enums --- */

static void test_e2e_enum(void) {
    char *output = compile_and_run(
        ""
        "const Color enum {\n"
        "    RED\n"
        "    GREEN\n"
        "    BLUE\n"
        "}\n"
        "do main() {\n"
        "    mut c Color = Color.GREEN\n"
        "    println(c)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1");
}

/* --- Arrays --- */

static void test_e2e_array(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut nums [int] = {10, 20, 30}\n"
        "    println(nums[0])\n"
        "    println(nums[2])\n"
        "    println(len(nums))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "10\n30\n3");
}

/* --- Array mutation --- */

static void test_e2e_array_set(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut nums [int] = {1, 2, 3}\n"
        "    nums[1] = 99\n"
        "    println(nums[1])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "99");
}

/* --- For each --- */

static void test_e2e_for_each(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut names [string] = {\"a\", \"b\", \"c\"}\n"
        "    for_each name in names {\n"
        "        println(name)\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "a\nb\nc");
}

/* --- When/Is --- */

static void test_e2e_when(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut x int = 2\n"
        "    when x {\n"
        "        is 1 { println(\"one\") }\n"
        "        is 2 { println(\"two\") }\n"
        "        default { println(\"other\") }\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "two");
}

/* --- Len builtin --- */

static void test_e2e_len_string(void) {
    char *output = compile_and_run(
        ""
        "do main() { println(len(\"hello\")) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "5");
}

/* --- type_of builtin --- */

static void test_e2e_type_of(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    println(type_of(42))\n"
        "    println(type_of(\"hi\"))\n"
        "    println(type_of(true))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "int\nstring\nbool");
}

/* --- Compound assignment --- */

static void test_e2e_compound_assign(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut x int = 10\n"
        "    x += 5\n"
        "    x -= 3\n"
        "    x *= 2\n"
        "    println(x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "24");
}

/* --- Char type --- */

static void test_e2e_char(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut c char = 'A'\n"
        "    println(c)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "A");
}

/* --- Blank identifier --- */

static void test_e2e_blank_identifier(void) {
    char *output = compile_and_run(
        ""
        "do pair() -> (int, int) { return 42, 99 }\n"
        "do main() {\n"
        "    mut _, b int = pair()\n"
        "    println(b)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "99");
}

/* ===== @mem Module Tests ===== */

static void test_e2e_mem_arena_create_destroy(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "    mut a = mem.arena(4096)\n"
        "    println(\"created\")\n"
        "    mem.destroy(a)\n"
        "    println(\"destroyed\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "created\ndestroyed");
}

static void test_e2e_mem_usage(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "    mut a = mem.arena(1024)\n"
        "    println(mem.usage(a))\n"
        "    mut s ^string = mem.alloc(a, \"hello\")\n"
        "    mut used int = mem.usage(a)\n"
        "    if used > 0 { println(\"allocated\") }\n"
        "    mem.destroy(a)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0\nallocated");
}

static void test_e2e_mem_reset(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "    mut a = mem.arena(1024)\n"
        "    mut s ^string = mem.alloc(a, \"hello\")\n"
        "    if mem.usage(a) > 0 { println(\"used\") }\n"
        "    mem.reset(a)\n"
        "    println(mem.usage(a))\n"
        "    mem.destroy(a)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "used\n0");
}

static void test_e2e_mem_alloc_string(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "    mut a = mem.arena(4096)\n"
        "    ensure mem.destroy(a)\n"
        "    mut s ^string = mem.alloc(a, \"arena string\")\n"
        "    println(s^)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "arena string");
}

static void test_e2e_mem_alloc_array(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "    mut a = mem.arena(4096)\n"
        "    ensure mem.destroy(a)\n"
        "    mut nums ^[int] = mem.alloc(a, {10, 20, 30})\n"
        "    println(nums^[0])\n"
        "    println(nums^[1])\n"
        "    println(nums^[2])\n"
        "    println(len(nums^))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "10\n20\n30\n3");
}

static void test_e2e_mem_ensure_cleanup(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do work() {\n"
        "    mut a = mem.arena(1024)\n"
        "    ensure mem.destroy(a)\n"
        "    mut s ^string = mem.alloc(a, \"working\")\n"
        "    println(s^)\n"
        "}\n"
        "do main() {\n"
        "    work()\n"
        "    println(\"done\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "working\ndone");
}

/* ===== Pointer Tests ===== */

static void test_e2e_ptr_new_deref(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "    mut a = mem.arena(4096)\n"
        "    ensure mem.destroy(a)\n"
        "    mut p ^int = mem.init(a, int)\n"
        "    p^ = 42\n"
        "    println(p^)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42");
}

static void test_e2e_ptr_struct(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "const Point struct {\n"
        "    x int\n"
        "    y int\n"
        "}\n"
        "do main() {\n"
        "    mut a = mem.arena(4096)\n"
        "    ensure mem.destroy(a)\n"
        "    mut p ^Point = mem.init(a, Point)\n"
        "    p^.x = 3\n"
        "    p^.y = 4\n"
        "    println(p^.x)\n"
        "    println(p^.y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "3\n4");
}

static void test_e2e_ptr_addr(void) {
    char *output = compile_and_run(
        ""
        "do set(p ^int, v int) {\n"
        "    p^ = v\n"
        "}\n"
        "do main() {\n"
        "    mut x int = 0\n"
        "    set(addr(x), 99)\n"
        "    println(x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "99");
}

static void test_e2e_ptr_nil(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut p ^int = nil\n"
        "    if p == nil {\n"
        "        println(\"null\")\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "null");
}

static void test_e2e_ptr_write_through(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do set_value(p ^int, val int) {\n"
        "    p^ = val\n"
        "}\n"
        "do main() {\n"
        "    mut a = mem.arena(4096)\n"
        "    ensure mem.destroy(a)\n"
        "    mut p ^int = mem.init(a, int)\n"
        "    set_value(p, 777)\n"
        "    println(p^)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "777");
}

/* ===== v3 Keyword Tests ===== */

static void test_e2e_mut_keyword(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut x int = 10\n"
        "    mut y = 20\n"
        "    println(x + y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "30");
}

static void test_e2e_while_keyword(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut i int = 0\n"
        "    while i < 3 {\n"
        "        println(i)\n"
        "        i++\n"
        "    }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0\n1\n2");
}

static void test_e2e_mut_while_combined(void) {
    char *output = compile_and_run(
        ""
        "do fib(n int) -> int {\n"
        "    mut a int = 0\n"
        "    mut b int = 1\n"
        "    mut i int = 0\n"
        "    while i < n {\n"
        "        mut next int = a + b\n"
        "        a = b\n"
        "        b = next\n"
        "        i++\n"
        "    }\n"
        "    return a\n"
        "}\n"
        "do main() { println(fib(10)) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "55");
}

/* ===== Default Params, in/not_in, OS, Arrays Tests ===== */

static void test_e2e_default_parameters(void) {
    char *output = compile_and_run(
        ""
        "do greet(name string = \"World\") -> string {\n"
        "    return \"Hello, ${name}!\"\n"
        "}\n"
        "do main() {\n"
        "    println(greet())\n"
        "    println(greet(\"grayc\"))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "Hello, World!\nHello, grayc!");
}

static void test_e2e_in_operator(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "    mut nums [int] = {1, 2, 3}\n"
        "    if 2 in nums { println(\"found\") }\n"
        "    if 9 !in nums { println(\"not found\") }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "found\nnot found");
}

static void test_e2e_os_args(void) {
    char *output = compile_and_run(
        "import @os\n"
        "do main() {\n"
        "    println(os.arch())\n"
        "}");
    ASSERT_NOT_NULL(output);
    /* Should be arm64 or x86_64 — just check it's not empty */
    ASSERT(strlen(output) > 0);
}

static void test_e2e_arrays_append(void) {
    char *output = compile_and_run(
        "import @arrays\n"
        "do main() {\n"
        "    mut nums [int] = {1, 2}\n"
        "    arrays.append(nums, 3)\n"
        "    println(len(nums))\n"
        "    println(nums[2])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "3\n3");
}

static void test_e2e_arrays_sort(void) {
    char *output = compile_and_run(
        "import @arrays\n"
        "do main() {\n"
        "    mut nums [int] = {3, 1, 2}\n"
        "    arrays.sort_asc(nums)\n"
        "    println(nums[0])\n"
        "    println(nums[1])\n"
        "    println(nums[2])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n2\n3");
}

static void test_e2e_hex_literal(void) {
    char *output = compile_and_run(
        ""
        "do main() { println(0xFF) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "255");
}

static void test_e2e_octal_literal(void) {
    char *output = compile_and_run(
        ""
        "do main() { println(0o10) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "8");
}

static void test_e2e_binary_literal(void) {
    char *output = compile_and_run(
        ""
        "do main() { println(0b1010) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "10");
}

/* ===== Fixed-size and Multi-dimensional Array Tests ===== */

static void test_e2e_fixed_array(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  const arr [int, 3] = {10, 20, 30}\n"
        "  println(\"${arr[0]},${arr[1]},${arr[2]}\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "10,20,30");
}

static void test_e2e_nested_array(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut m [[int]] = {{1, 2}, {3, 4}}\n"
        "  mut r0 = m[0]\n"
        "  mut r1 = m[1]\n"
        "  println(\"${r0[0]},${r0[1]},${r1[0]},${r1[1]}\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1,2,3,4");
}

/* ===== Threads Tests ===== */

static void test_e2e_threads_spawn_join(void) {
    char *output = compile_and_run(
        "import @threads\n"
        "do worker(id int) { println(\"w${id}\") }\n"
        "do main() {\n"
        "  mut t1 = threads.spawn(()worker, 1)\n"
        "  mut t2 = threads.spawn(()worker, 2)\n"
        "  threads.join(t1)\n"
        "  threads.join(t2)\n"
        "  println(\"done\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    /* Output order may vary due to threading, but "done" must be last */
    ASSERT(strstr(output, "done") != NULL);
    ASSERT(strstr(output, "w1") != NULL);
    ASSERT(strstr(output, "w2") != NULL);
}

static void test_e2e_threads_channel(void) {
    char *output = compile_and_run(
        "import @channels\n"
        "do main() {\n"
        "  mut ch = channels.open(4)\n"
        "  channels.send(ch, 42)\n"
        "  channels.send(ch, 100)\n"
        "  mut a = channels.receive(ch)\n"
        "  mut b = channels.receive(ch)\n"
        "  println(\"${a},${b}\")\n"
        "  channels.close(ch)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42,100");
}

static void test_e2e_threads_sleep(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  sleep_ms(10)\n"
        "  println(\"awake\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "awake");
}

/* ===== Function Reference Tests ===== */

static void test_e2e_function_reference_basic(void) {
    char *output = compile_and_run(
        ""
        "do double(n int) -> int { return n * 2 }\n"
        "do main() {\n"
        "  const f = ()double\n"
        "  println(f(21))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42");
}

static void test_e2e_function_reference_via_ref(void) {
    char *output = compile_and_run(
        ""
        "do negate(n int) -> int { return n * -1 }\n"
        "do main() {\n"
        "  mut f = ref(negate)\n"
        "  println(f(5))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "-5");
}


/* ===== Struct-Namespaced Functions ===== */

static void test_e2e_struct_function(void) {
    char *output = compile_and_run(
        ""
        "const Counter struct {\n"
        "  value int\n"
        "  do make(v int) -> Counter { return Counter{value: v} }\n"
        "  do inc(c Counter) -> Counter { return Counter{value: c.value + 1} }\n"
        "}\n"
        "do main() {\n"
        "  mut c = Counter.make(0)\n"
        "  c = Counter.inc(c)\n"
        "  c = Counter.inc(c)\n"
        "  println(c.value)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "2");
}

/* ===== or_return ===== */

static void test_e2e_or_return(void) {
    char *output = compile_and_run(
        ""
        "do fallible(ok bool) -> (string, Error) {\n"
        "  if ok { return \"success\", nil }\n"
        "  return \"\", error(\"failed\")\n"
        "}\n"
        "do wrapper() -> (string, Error) {\n"
        "  mut val = fallible(true) or_return\n"
        "  return val, nil\n"
        "}\n"
        "do main() {\n"
        "  mut v, e = wrapper()\n"
        "  println(v)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "success");
}

/* ===== Enum Attributes ===== */

static void test_e2e_flags_enum(void) {
    char *output = compile_and_run(
        ""
        "#flags\n"
        "const Perms enum {\n"
        "    READ\n"
        "    WRITE\n"
        "    EXEC\n"
        "}\n"
        "do main() {\n"
        "  println(Perms.READ)\n"
        "  println(Perms.WRITE)\n"
        "  println(Perms.EXEC)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n2\n4");
}

static void test_e2e_string_enum(void) {
    char *output = compile_and_run(
        ""
        "const Status enum {\n"
        "  TODO = \"todo\"\n"
        "  DONE = \"done\"\n"
        "}\n"
        "do main() {\n"
        "  println(Status.TODO)\n"
        "  println(Status.DONE)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "todo\ndone");
}

/* ===== Named Returns ===== */

static void test_e2e_named_return(void) {
    char *output = compile_and_run(
        ""
        "do divide(a int, b int) -> (q int, r int) {\n"
        "  mut q int = a / b\n"
        "  mut r int = a % b\n"
        "  return q, r\n"
        "}\n"
        "do main() {\n"
        "  mut q, r = divide(17, 5)\n"
        "  println(\"${q},${r}\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "3,2");
}

/* ===== Mutable Indexed/Member Params ===== */

static void test_e2e_mutable_indexed_parameter(void) {
    char *output = compile_and_run(
        ""
        "do inc(&n int) { n = n + 1 }\n"
        "do main() {\n"
        "  mut arr [int] = {10, 20, 30}\n"
        "  inc(arr[1])\n"
        "  println(arr[1])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "21");
}

static void test_e2e_mutable_member_parameter(void) {
    char *output = compile_and_run(
        ""
        "const P struct {\n"
        "    x int\n"
        "}\n"
        "do inc(&n int) { n = n + 1 }\n"
        "do main() {\n"
        "  mut p = P{x: 5}\n"
        "  inc(p.x)\n"
        "  println(p.x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "6");
}

/* ===== Map Operations ===== */

static void test_e2e_map_basic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut m map[string:int] = {\"a\": 1, \"b\": 2}\n"
        "  println(m[\"a\"])\n"
        "  println(m[\"b\"])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n2");
}

static void test_e2e_map_foreach(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut m map[string:int] = {\"x\": 10}\n"
        "  mut total int = 0\n"
        "  for_each _, v in m {\n"
        "    total = total + v\n"
        "  }\n"
        "  println(total)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "10");
}

/* ===== Division by Zero ===== */

static void test_e2e_divide_by_zero(void) {
    /* Compile and run — should panic, not crash silently */
    test_number++;
    char gray_file[128], binary_file[128];
    snprintf(gray_file, sizeof(gray_file), "%s/grayc_e2e_%d.gray", e2e_tmpdir(), test_number);
    snprintf(binary_file, sizeof(binary_file), "%s/grayc_e2e_%d" E2E_EXE_SUFFIX, e2e_tmpdir(), test_number);

    const char *src =
        ""
        "do main() {\n"
        "  mut x int = 10\n"
        "  mut y int = 0\n"
        "  println(x / y)\n"
        "}";

    FILE *file = fopen(gray_file, "w");
    ASSERT(file != NULL);
    fputs(src, file);
    fclose(file);

    if (e2e_compile(gray_file, binary_file) != 0) {
        unlink(gray_file);
        ASSERT(0 && "compile failed");
    }

    char run_cmd[256];
    snprintf(run_cmd, sizeof(run_cmd), "\"%s\" 2>&1", binary_file);
    FILE *pipe = popen(run_cmd, "r");
    char output[4096] = {0};
    if (pipe) {
        fread(output, 1, sizeof(output) - 1, pipe);
        pclose(pipe);
    }

    unlink(gray_file);
    unlink(binary_file);

    ASSERT(strstr(output, "division by zero") != NULL);
}

/* ===== Sized Integer Types ===== */

static void test_e2e_sized_int_i8(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x i8 = 127\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "127\ni8");
}

static void test_e2e_sized_int_u8(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x u8 = 255\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "255\nu8");
}

static void test_e2e_sized_int_i32(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x i32 = 100000\n"
        "  mut y i32 = 200000\n"
        "  println(x + y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "300000");
}

static void test_e2e_sized_int_u64(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x u64 = 1000000\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1000000\nu64");
}

static void test_e2e_sized_float_f32(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x f32 = 3.14\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "f32");
}

static void test_e2e_byte_type(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut b byte = cast(255, byte)\n"
        "  println(b)\n"
        "  println(type_of(b))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "255\nbyte");
}

/* ===== Cast Expression ===== */

static void test_e2e_cast(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x int = 42\n"
        "  mut y u8 = cast(x, u8)\n"
        "  println(y)\n"
        "  mut f float = 3.7\n"
        "  mut i int = cast(f, int)\n"
        "  println(i)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42\n3");
}

/* ===== Continue in Loop ===== */

static void test_e2e_continue(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut result int = 0\n"
        "  for i in range(0, 10) {\n"
        "    if i % 2 == 0 { continue }\n"
        "    result += i\n"
        "  }\n"
        "  println(result)\n"
        "}");
    ASSERT_NOT_NULL(output);
    /* sum of odd numbers 1+3+5+7+9 = 25 */
    ASSERT_STR_EQ(output, "25");
}

/* ===== Modulo/Divide Compound Assign ===== */

static void test_e2e_modulo_division_assign(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x int = 20\n"
        "  x /= 4\n"
        "  println(x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "5");
}

/* ===== Nested Structs ===== */

static void test_e2e_nested_struct(void) {
    char *output = compile_and_run(
        ""
        "const Point struct {\n"
        "    x int\n"
        "    y int\n"
        "}\n"
        "const Rect struct {\n"
        "    origin Point\n"
        "    size Point\n"
        "}\n"
        "do main() {\n"
        "  mut r = Rect{origin: Point{x: 1, y: 2}, size: Point{x: 10, y: 20}}\n"
        "  println(r.origin.x)\n"
        "  println(r.size.y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n20");
}

/* ===== P4: Remaining sized integer types ===== */

static void test_e2e_sized_int_i16(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x i16 = 32767\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "32767\ni16");
}

static void test_e2e_sized_int_i64(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x i64 = 9223372036854775807\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "9223372036854775807\ni64");
}

static void test_e2e_sized_int_u16(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x u16 = 65535\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "65535\nu16");
}

static void test_e2e_sized_int_u32(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x u32 = 4294967295\n"
        "  println(x)\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "4294967295\nu32");
}

static void test_e2e_sized_float_f64(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x f64 = 3.141592653589793\n"
        "  println(type_of(x))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "f64");
}

/* Note: ** power operator codegen emits raw C '**' which is invalid.
 * This is a pre-existing bug — skipping E2E test until codegen is fixed. */

/* ===== P5: Range with step ===== */

static void test_e2e_range_with_step(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut result int = 0\n"
        "  for i in range(0, 10, 3) {\n"
        "    result += i\n"
        "  }\n"
        "  println(result)\n"
        "}");
    ASSERT_NOT_NULL(output);
    /* 0 + 3 + 6 + 9 = 18 */
    ASSERT_STR_EQ(output, "18");
}

/* ===== P5: Percent assign ===== */

static void test_e2e_percent_assign(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x int = 17\n"
        "  x %= 5\n"
        "  println(x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "2");
}

/* ===== Boolean Logic ===== */

static void test_e2e_bool_and_or(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a bool = true\n"
        "  mut b bool = false\n"
        "  if a && !b { println(\"and-ok\") }\n"
        "  if a || b { println(\"or-ok\") }\n"
        "  if !(a && b) { println(\"not-ok\") }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "and-ok\nor-ok\nnot-ok");
}

static void test_e2e_short_circuit(void) {
    char *output = compile_and_run(
        ""
        "mut called int = 0\n"
        "do side() -> bool { called++\n return true }\n"
        "do main() {\n"
        "  if false && side() { println(\"bad\") }\n"
        "  println(called)\n"
        "  if true || side() { println(\"good\") }\n"
        "  println(called)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0\ngood\n0");
}

/* ===== String Comparison ===== */

static void test_e2e_string_compare(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a string = \"hello\"\n"
        "  mut b string = \"hello\"\n"
        "  mut c string = \"world\"\n"
        "  if a == b { println(\"eq\") }\n"
        "  if a != c { println(\"neq\") }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "eq\nneq");
}

/* ===== Negative Arithmetic ===== */

static void test_e2e_negative_arithmetic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x int = -10\n"
        "  mut y int = 3\n"
        "  println(x + y)\n"
        "  println(x * y)\n"
        "  println(x / y)\n"
        "  println(-x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "-7\n-30\n-3\n10");
}

/* ===== Variable Shadowing ===== */

static void test_e2e_variable_shadowing(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x int = 1\n"
        "  println(x)\n"
        "  if true {\n"
        "    mut x int = 2\n"
        "    println(x)\n"
        "  }\n"
        "  println(x)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n2\n1");
}

/* ===== Nested Control Flow ===== */

static void test_e2e_nested_control_flow(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut total int = 0\n"
        "  for i in range(0, 3) {\n"
        "    when i {\n"
        "      is 0 { total += 10 }\n"
        "      is 1 { total += 20 }\n"
        "      default { total += 30 }\n"
        "    }\n"
        "  }\n"
        "  println(total)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "60");
}

/* ===== Map Mutation ===== */

static void test_e2e_map_set(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut m map[string:int] = {\"a\": 1}\n"
        "  m[\"a\"] = 99\n"
        "  m[\"b\"] = 42\n"
        "  println(m[\"a\"])\n"
        "  println(m[\"b\"])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "99\n42");
}

/* ===== For_each with Index ===== */

static void test_e2e_foreach_index(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut arr [string] = {\"a\", \"b\", \"c\"}\n"
        "  for_each i, item in arr {\n"
        "    println(\"${i}:${item}\")\n"
        "  }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0:a\n1:b\n2:c");
}

/* ===== Const Values ===== */

static void test_e2e_const_values(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  const PI float = 3.14\n"
        "  const NAME string = \"Grayscale\"\n"
        "  const FLAG bool = true\n"
        "  println(PI)\n"
        "  println(NAME)\n"
        "  println(FLAG)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "3.14\nGrayscale\ntrue");
}

/* ===== Empty Containers ===== */

static void test_e2e_empty_array(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut arr [int] = {}\n"
        "  println(len(arr))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0");
}

static void test_e2e_empty_map(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut m map[string:int] = {:}\n"
        "  println(len(m))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0");
}

/* ===== Nested Function Calls ===== */

static void test_e2e_nested_calls(void) {
    char *output = compile_and_run(
        ""
        "do add(a int, b int) -> int { return a + b }\n"
        "do mul(a int, b int) -> int { return a * b }\n"
        "do main() {\n"
        "  println(add(1, mul(2, 3)))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "7");
}

/* ===== String Indexing ===== */

static void test_e2e_string_index(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut s string = \"hello\"\n"
        "  println(s[0])\n"
        "  println(s[4])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "h\no");
}

/* ===== Enum Comparison ===== */

static void test_e2e_enum_compare(void) {
    char *output = compile_and_run(
        ""
        "const Dir enum {\n"
        "    UP\n"
        "    DOWN\n"
        "    LEFT\n"
        "    RIGHT\n"
        "}\n"
        "do main() {\n"
        "  mut d = Dir.LEFT\n"
        "  if d == Dir.LEFT { println(\"left\") }\n"
        "  if d != Dir.UP { println(\"not up\") }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "left\nnot up");
}

/* ===== Grouped Parameters ===== */

static void test_e2e_grouped_parameters(void) {
    char *output = compile_and_run(
        ""
        "do add3(a, b, c int) -> int { return a + b + c }\n"
        "do main() {\n"
        "  println(add3(10, 20, 30))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "60");
}

/* ===== Scope Lifetime ===== */

static void test_e2e_loop_scope(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut total int = 0\n"
        "  for i in range(0, 3) {\n"
        "    mut local int = i * 10\n"
        "    total += local\n"
        "  }\n"
        "  println(total)\n"
        "}");
    ASSERT_NOT_NULL(output);
    /* 0 + 10 + 20 = 30 */
    ASSERT_STR_EQ(output, "30");
}

/* ===== #strict when ===== */

static void test_e2e_strict_when(void) {
    char *output = compile_and_run(
        ""
        "const Dir enum {\n"
        "    UP\n"
        "    DOWN\n"
        "    LEFT\n"
        "    RIGHT\n"
        "}\n"
        "do main() {\n"
        "  mut d = Dir.LEFT\n"
        "  mut label string = \"\"\n"
        "  #strict\n"
        "  when d {\n"
        "    is Dir.UP { label = \"up\" }\n"
        "    is Dir.DOWN { label = \"down\" }\n"
        "    is Dir.LEFT { label = \"left\" }\n"
        "    is Dir.RIGHT { label = \"right\" }\n"
        "  }\n"
        "  println(label)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "left");
}

/* ===== C interop ===== */

static void test_e2e_c_interop(void) {
    char *output = compile_and_run(
        ""
        "import c \"stdlib.h\"\n"
        "do main() {\n"
        "  println(c.EXIT_SUCCESS)\n"
        "  println(c.EXIT_FAILURE)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "0\n1");
}

/* ===== Atomic ===== */

static void test_e2e_atomic(void) {
    char *output = compile_and_run(
        ""
        "import @atomic\n"
        "do main() {\n"
        "  mut val int = 0\n"
        "  mut ptr ^int = addr(val)\n"
        "  atomic.store(ptr, 42)\n"
        "  println(atomic.load(ptr))\n"
        "  mut old int = atomic.add(ptr, 8)\n"
        "  println(old)\n"
        "  println(atomic.load(ptr))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42\n42\n50");
}

/* ===== Bigint Types ===== */

static void test_e2e_i128_arithmetic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a i128 = i128(100)\n"
        "  mut b i128 = i128(42)\n"
        "  println(a + b)\n"
        "  println(a - b)\n"
        "  println(a * b)\n"
        "  println(a / b)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "142\n58\n4200\n2");
}

static void test_e2e_u128_arithmetic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a u128 = u128(200)\n"
        "  mut b u128 = u128(50)\n"
        "  println(a + b)\n"
        "  println(a - b)\n"
        "  println(a * b)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "250\n150\n10000");
}

static void test_e2e_i256_arithmetic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a i256 = i256(1000)\n"
        "  mut b i256 = i256(234)\n"
        "  println(a + b)\n"
        "  println(a - b)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1234\n766");
}

static void test_e2e_u256_arithmetic(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a u256 = u256(5000)\n"
        "  mut b u256 = u256(3000)\n"
        "  println(a + b)\n"
        "  println(a - b)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "8000\n2000");
}

static void test_e2e_bigint_comparison(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a i128 = i128(10)\n"
        "  mut b i128 = i128(20)\n"
        "  mut eq bool = a == b\n"
        "  mut lt bool = a < b\n"
        "  mut gt bool = a > b\n"
        "  println(eq)\n"
        "  println(lt)\n"
        "  println(gt)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "false\ntrue\nfalse");
}

static void test_e2e_bigint_cast(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a i128 = i128(42)\n"
        "  mut b int = int(a)\n"
        "  println(b)\n"
        "  mut c uint = 100\n"
        "  mut d u128 = u128(c)\n"
        "  println(d)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42\n100");
}

static void test_e2e_bigint_interpolation(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a i128 = i128(42)\n"
        "  mut b u128 = u128(99)\n"
        "  println(\"i128=${a} u128=${b}\")\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "i128=42 u128=99");
}

static void test_e2e_bigint_mixed_width(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a i256 = i256(1000)\n"
        "  mut b i128 = i128(234)\n"
        "  mut c i256 = a + b\n"
        "  println(c)\n"
        "  mut d u256 = u256(5000)\n"
        "  mut e u128 = u128(3000)\n"
        "  mut f u256 = d + e\n"
        "  println(f)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1234\n8000");
}

/* ===== Struct Functions (Extended) ===== */

static void test_e2e_struct_multiple_functions(void) {
    char *output = compile_and_run(
        ""
        "const Vec struct {\n"
        "  x int\n"
        "  y int\n"
        "  do make(x int, y int) -> Vec { return Vec{x: x, y: y} }\n"
        "  do sum(v Vec) -> int { return v.x + v.y }\n"
        "  do scale(v Vec, factor int) -> Vec {\n"
        "    return Vec{x: v.x * factor, y: v.y * factor}\n"
        "  }\n"
        "}\n"
        "do main() {\n"
        "  mut v = Vec.make(3, 4)\n"
        "  println(Vec.sum(v))\n"
        "  mut s = Vec.scale(v, 2)\n"
        "  println(s.x)\n"
        "  println(s.y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "7\n6\n8");
}

static void test_e2e_struct_self_dispatch(void) {
    char *output = compile_and_run(
        ""
        "const Counter struct {\n"
        "  value int\n"
        "  do make(v int) -> Counter { return Counter{value: v} }\n"
        "  do get(c Counter) -> int { return c.value }\n"
        "  do inc(c Counter) -> Counter {\n"
        "    return Counter{value: c.value + 1}\n"
        "  }\n"
        "}\n"
        "do main() {\n"
        "  mut c = Counter.make(0)\n"
        "  c = c.inc()\n"
        "  c = c.inc()\n"
        "  c = c.inc()\n"
        "  println(c.get())\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "3");
}

static void test_e2e_struct_static_dispatch(void) {
    char *output = compile_and_run(
        ""
        "const Adder struct {\n"
        "  val int\n"
        "  do make(v int) -> Adder { return Adder{val: v} }\n"
        "  do add(a Adder, n int) -> Adder {\n"
        "    return Adder{val: a.val + n}\n"
        "  }\n"
        "}\n"
        "do main() {\n"
        "  mut a = Adder.make(10)\n"
        "  mut b = Adder.add(a, 5)\n"
        "  println(b.val)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "15");
}

/* ===== Multi-Return (Extended) ===== */

static void test_e2e_multi_return_three(void) {
    char *output = compile_and_run(
        ""
        "do triple(a int) -> (int, int, int) { return a, a * 2, a * 3 }\n"
        "do main() {\n"
        "  mut x, y, z = triple(5)\n"
        "  println(x)\n"
        "  println(y)\n"
        "  println(z)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "5\n10\n15");
}

static void test_e2e_multi_return_mixed(void) {
    char *output = compile_and_run(
        ""
        "do info() -> (string, int) { return \"hello\", 42 }\n"
        "do main() {\n"
        "  mut s, n = info()\n"
        "  println(s)\n"
        "  println(n)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "hello\n42");
}

static void test_e2e_multi_return_wildcard(void) {
    char *output = compile_and_run(
        ""
        "do divide(a int, b int) -> (int, int) { return a / b, a % b }\n"
        "do main() {\n"
        "  mut _, r int = divide(17, 5)\n"
        "  println(r)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "2");
}

/* ===== Error Handling (Extended) ===== */

static void test_e2e_or_return_error_path(void) {
    char *output = compile_and_run(
        ""
        "do risky() -> (string, Error) {\n"
        "  return \"\", error(\"oops\")\n"
        "}\n"
        "do caller() -> (string, Error) {\n"
        "  mut val = risky() or_return\n"
        "  return val, nil\n"
        "}\n"
        "do main() {\n"
        "  mut v, e = caller()\n"
        "  if e != nil {\n"
        "    println(\"propagated\")\n"
        "  } otherwise {\n"
        "    println(v)\n"
        "  }\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "propagated");
}

static void test_e2e_or_return_fallback(void) {
    char *output = compile_and_run(
        ""
        "do risky() -> (string, Error) {\n"
        "  return \"\", error(\"bad\")\n"
        "}\n"
        "do safe() -> (string, Error) {\n"
        "  mut val = risky() or_return \"fallback\"\n"
        "  return val, nil\n"
        "}\n"
        "do main() {\n"
        "  mut v, e = safe()\n"
        "  println(v)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "fallback");
}

static void test_e2e_ensure_multiple(void) {
    char *output = compile_and_run(
        ""
        "do step1() { println(\"step1\") }\n"
        "do step2() { println(\"step2\") }\n"
        "do step3() { println(\"step3\") }\n"
        "do work() {\n"
        "  ensure step1()\n"
        "  ensure step2()\n"
        "  ensure step3()\n"
        "  println(\"working\")\n"
        "}\n"
        "do main() { work() }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "working\nstep3\nstep2\nstep1");
}

static void test_e2e_ensure_early_return(void) {
    char *output = compile_and_run(
        ""
        "do cleanup() { println(\"cleaned\") }\n"
        "do work(flag bool) {\n"
        "  ensure cleanup()\n"
        "  if flag {\n"
        "    println(\"early\")\n"
        "    return\n"
        "  }\n"
        "  println(\"normal\")\n"
        "}\n"
        "do main() { work(true) }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "early\ncleaned");
}

/* ===== Pointer Operations (Extended) ===== */

static void test_e2e_ptr_to_ptr(void) {
    char *output = compile_and_run(
        "import @mem\n"
        "do main() {\n"
        "  mut a = mem.arena(4096)\n"
        "  ensure mem.destroy(a)\n"
        "  mut p ^int = mem.init(a, int)\n"
        "  p^ = 42\n"
        "  mut pp = addr(p)\n"
        "  println(pp^^)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "42");
}

static void test_e2e_ptr_compare(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut x int = 10\n"
        "  mut y int = 20\n"
        "  mut p1 ^int = addr(x)\n"
        "  mut p2 ^int = addr(x)\n"
        "  mut p3 ^int = addr(y)\n"
        "  println(p1 == p2)\n"
        "  println(p1 == p3)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "true\nfalse");
}

/* ===== Container Operations (Extended) ===== */

static void test_e2e_map_int_keys(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut m map[int:string] = {1: \"one\", 2: \"two\", 3: \"three\"}\n"
        "  println(m[2])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "two");
}

static void test_e2e_array_of_structs(void) {
    char *output = compile_and_run(
        ""
        "const Point struct {\n"
        "  x int\n"
        "  y int\n"
        "}\n"
        "do main() {\n"
        "  mut points [Point] = {Point{x: 1, y: 2}, Point{x: 3, y: 4}}\n"
        "  println(points[0].x)\n"
        "  println(points[1].y)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "1\n4");
}

/* ===== Feature gaps (issue #2556) ===== */

static void test_e2e_bitwise_ops(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  println(0b1100 bit_and 0b1010)\n"
        "  println(0b1100 bit_or 0b1010)\n"
        "  println(0b1100 bit_xor 0b1010)\n"
        "  println(1 bit_shift_left 4)\n"
        "  println(64 bit_shift_right 2)\n"
        "  mut n int = bit_not 0\n"
        "  println(n)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "8\n14\n6\n16\n16\n-1");
}

static void test_e2e_defer_keyword(void) {
    char *output = compile_and_run(
        ""
        "do log(s string) { println(s) }\n"
        "do work() {\n"
        "  defer log(\"c\")\n"
        "  defer log(\"b\")\n"
        "  log(\"a\")\n"
        "}\n"
        "do main() { work() }");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "a\nb\nc");
}

static void test_e2e_string_stdlib(void) {
    char *output = compile_and_run(
        ""
        "import @strings\n"
        "do main() {\n"
        "  println(strings.to_upper(\"hi\"))\n"
        "  println(strings.to_lower(\"Hi\"))\n"
        "  println(strings.trim(\"  x  \"))\n"
        "  println(strings.replace(\"aaa\", \"a\", \"b\"))\n"
        "  println(strings.contains(\"hello\", \"ell\"))\n"
        "  mut parts [string] = strings.split(\"a,b,c\", \",\")\n"
        "  println(parts[1])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "HI\nhi\nx\nbbb\ntrue\nb");
}

/* chars module: scalar ASCII case folding (#2559) */
static void test_e2e_chars_stdlib(void) {
    char *output = compile_and_run(
        ""
        "import @chars\n"
        "do main() {\n"
        "  println(chars.to_upper('a'))\n"
        "  println(chars.to_lower('Z'))\n"
        "  println(chars.to_upper('5'))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "A\nz\n5");
}

/* strings char-editing functions (#2560) */
static void test_e2e_strings_char_editing(void) {
    char *output = compile_and_run(
        ""
        "import @strings\n"
        "do main() {\n"
        "  mut s string = \"helo\"\n"
        "  s = strings.insert_char_at(s, 3, 'l')\n"
        "  s = strings.append_char(s, '!')\n"
        "  s = strings.prepend_char(s, '>')\n"
        "  s = strings.remove_at(s, 0)\n"
        "  s = strings.set_char_at(s, 0, 'H')\n"
        "  println(s)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "Hello!");
}

/* strconv arbitrary-base formatting and quoted-literal round-trip (#2434) */
static void test_e2e_strconv_format_quote(void) {
    char *output = compile_and_run(
        ""
        "import @strconv\n"
        "do main() {\n"
        "  println(strconv.format_int(255, 16))\n"
        "  println(strconv.format_int(-10, 2))\n"
        "  println(strconv.format_uint(8, 8))\n"
        "  println(strconv.quote(\"a\\tb\"))\n"
        "  mut v, _ = strconv.unquote(\"\\\"hi\\\"\")\n"
        "  println(v)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "ff\n-1010\n10\n\"a\\tb\"\nhi");
}

/* `mut` array/map literal type inference for primitives (#2374) */
static void test_e2e_infer_mut_literals(void) {
    char *output = compile_and_run(
        ""
        "do main() {\n"
        "  mut a = {10, 20, 30}\n"
        "  mut m = {\"a\": 1, \"b\": 2}\n"
        "  println(a[2])\n"
        "  println(m[\"b\"])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "30\n2");
}

/* fmt format directives on i128/u128/i256/u256 operands (#2586) */
static void test_e2e_fmt_bigint_directives(void) {
    char *output = compile_and_run(
        ""
        "import @fmt\n"
        "do main() {\n"
        "  mut a i128 = 255\n"
        "  println(fmt.sprintf(\"%d\", a))\n"
        "  println(fmt.sprintf(\"%x\", a))\n"
        "  mut b u256 = 4096\n"
        "  println(fmt.sprintf(\"%o\", b))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "255\nff\n10000");
}

static void test_e2e_when_multi_value(void) {
    char *output = compile_and_run(
        ""
        "do classify(n int) -> string {\n"
        "  when n {\n"
        "    is 0 { return \"zero\" }\n"
        "    is 1, 2, 3 { return \"small\" }\n"
        "    default { return \"big\" }\n"
        "  }\n"
        "  return \"?\"\n"
        "}\n"
        "do main() {\n"
        "  println(classify(0))\n"
        "  println(classify(2))\n"
        "  println(classify(99))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "zero\nsmall\nbig");
}

static void test_e2e_map_remove(void) {
    char *output = compile_and_run(
        ""
        "import @maps\n"
        "do main() {\n"
        "  mut m map[string:int] = {\"a\": 1, \"b\": 2, \"c\": 3}\n"
        "  maps.remove_key(m, \"b\")\n"
        "  println(len(m))\n"
        "  println(\"b\" in m)\n"
        "  println(m[\"c\"])\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "2\nfalse\n3");
}

static void test_e2e_tagged_enum(void) {
    char *output = compile_and_run(
        ""
        "const Point struct {\n"
        "  x int\n"
        "  y int\n"
        "}\n"
        "const Shape enum {\n"
        "  Circle(int)\n"
        "  Rect(Point)\n"
        "  Empty\n"
        "}\n"
        "do area(s Shape) -> int {\n"
        "  when s {\n"
        "    is Shape.Circle(r) { return r * r }\n"
        "    is Shape.Rect(p) { return p.x * p.y }\n"
        "    is .Empty { return 0 }\n"
        "  }\n"
        "  return -1\n"
        "}\n"
        "do main() {\n"
        "  println(area(Shape.Circle(3)))\n"
        "  println(area(Shape.Rect(Point{x: 3, y: 4})))\n"
        "  println(area(Shape.Empty))\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "9\n12\n0");
}

static void test_e2e_struct_field_defaults(void) {
    char *output = compile_and_run(
        ""
        "const Config struct {\n"
        "  host string = \"localhost\"\n"
        "  port int = 8080\n"
        "}\n"
        "do main() {\n"
        "  mut c = new(Config)\n"
        "  println(c^.host)\n"
        "  println(c^.port)\n"
        "  mut c2 Config = Config{port: 9090}\n"
        "  println(c2.host)\n"
        "  println(c2.port)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "localhost\n8080\nlocalhost\n9090");
}

static void test_e2e_generic_type_param(void) {
    char *output = compile_and_run(
        ""
        "const Box struct {\n"
        "  value int\n"
        "}\n"
        "do make_it(T <?>) -> ^? {\n"
        "  return new(T)\n"
        "}\n"
        "do main() {\n"
        "  mut b = make_it(Box)\n"
        "  b^.value = 7\n"
        "  println(b^.value)\n"
        "}");
    ASSERT_NOT_NULL(output);
    ASSERT_STR_EQ(output, "7");
}

int main(void) {
    /* Must run from the grayc/ directory */
    if (access(E2E_COMPILER, 0) != 0) {
        fprintf(stderr, "test_codegen: must run from the grayc/ directory\n");
        return 1;
    }

    printf("\n");
    RUN_TEST(test_e2e_hello);
    RUN_TEST(test_e2e_arithmetic);
    RUN_TEST(test_e2e_variables);
    RUN_TEST(test_e2e_interpolation);
    RUN_TEST(test_e2e_if_else);
    RUN_TEST(test_e2e_for_range);
    RUN_TEST(test_e2e_while);
    RUN_TEST(test_e2e_loop_break);
    RUN_TEST(test_e2e_function_call);
    RUN_TEST(test_e2e_recursion);
    RUN_TEST(test_e2e_multi_return);
    RUN_TEST(test_e2e_mutable_parameter);
    RUN_TEST(test_e2e_ensure);
    RUN_TEST(test_e2e_struct);
    RUN_TEST(test_e2e_enum);
    RUN_TEST(test_e2e_array);
    RUN_TEST(test_e2e_array_set);
    RUN_TEST(test_e2e_for_each);
    RUN_TEST(test_e2e_when);
    RUN_TEST(test_e2e_len_string);
    RUN_TEST(test_e2e_type_of);
    RUN_TEST(test_e2e_compound_assign);
    RUN_TEST(test_e2e_char);
    RUN_TEST(test_e2e_blank_identifier);

    /* @mem module */
    RUN_TEST(test_e2e_mem_arena_create_destroy);
    RUN_TEST(test_e2e_mem_usage);
    RUN_TEST(test_e2e_mem_reset);
    RUN_TEST(test_e2e_mem_alloc_string);
    RUN_TEST(test_e2e_mem_alloc_array);
    RUN_TEST(test_e2e_mem_ensure_cleanup);

    /* Pointers */
    RUN_TEST(test_e2e_ptr_new_deref);
    RUN_TEST(test_e2e_ptr_struct);
    RUN_TEST(test_e2e_ptr_addr);
    RUN_TEST(test_e2e_ptr_nil);
    RUN_TEST(test_e2e_ptr_write_through);

    /* v3 keywords */
    RUN_TEST(test_e2e_mut_keyword);
    RUN_TEST(test_e2e_while_keyword);
    RUN_TEST(test_e2e_mut_while_combined);

    /* New features */
    RUN_TEST(test_e2e_default_parameters);
    RUN_TEST(test_e2e_in_operator);
    RUN_TEST(test_e2e_os_args);
    RUN_TEST(test_e2e_arrays_append);
    RUN_TEST(test_e2e_arrays_sort);
    RUN_TEST(test_e2e_hex_literal);
    RUN_TEST(test_e2e_octal_literal);
    RUN_TEST(test_e2e_binary_literal);

    /* Fixed-size and multi-dimensional arrays */
    RUN_TEST(test_e2e_fixed_array);
    RUN_TEST(test_e2e_nested_array);

    /* Threads */
    RUN_TEST(test_e2e_threads_spawn_join);
    RUN_TEST(test_e2e_threads_channel);
    RUN_TEST(test_e2e_threads_sleep);

    /* Function references */
    RUN_TEST(test_e2e_function_reference_basic);
    RUN_TEST(test_e2e_function_reference_via_ref);

    /* Struct-namespaced functions */
    RUN_TEST(test_e2e_struct_function);

    /* or_return */
    RUN_TEST(test_e2e_or_return);

    /* Enum attributes */
    RUN_TEST(test_e2e_flags_enum);
    RUN_TEST(test_e2e_string_enum);

    /* Named returns */
    RUN_TEST(test_e2e_named_return);

    /* Mutable indexed/member params */
    RUN_TEST(test_e2e_mutable_indexed_parameter);
    RUN_TEST(test_e2e_mutable_member_parameter);

    /* Map operations */
    RUN_TEST(test_e2e_map_basic);
    RUN_TEST(test_e2e_map_foreach);

    /* Division by zero */
    RUN_TEST(test_e2e_divide_by_zero);

    /* Sized integer types */
    RUN_TEST(test_e2e_sized_int_i8);
    RUN_TEST(test_e2e_sized_int_u8);
    RUN_TEST(test_e2e_sized_int_i32);
    RUN_TEST(test_e2e_sized_int_u64);
    RUN_TEST(test_e2e_sized_float_f32);
    RUN_TEST(test_e2e_byte_type);

    /* Cast expression */
    RUN_TEST(test_e2e_cast);

    /* Continue */
    RUN_TEST(test_e2e_continue);

    /* Compound assign */
    RUN_TEST(test_e2e_modulo_division_assign);

    /* Nested structs */
    RUN_TEST(test_e2e_nested_struct);

    /* P4: Remaining sized integer types */
    RUN_TEST(test_e2e_sized_int_i16);
    RUN_TEST(test_e2e_sized_int_i64);
    RUN_TEST(test_e2e_sized_int_u16);
    RUN_TEST(test_e2e_sized_int_u32);
    RUN_TEST(test_e2e_sized_float_f64);

    /* P5: Range with step, percent assign */
    RUN_TEST(test_e2e_range_with_step);
    RUN_TEST(test_e2e_percent_assign);

    /* Boolean logic */
    RUN_TEST(test_e2e_bool_and_or);
    RUN_TEST(test_e2e_short_circuit);

    /* String comparison */
    RUN_TEST(test_e2e_string_compare);

    /* Negative arithmetic */
    RUN_TEST(test_e2e_negative_arithmetic);

    /* Variable shadowing */
    RUN_TEST(test_e2e_variable_shadowing);

    /* Nested control flow */
    RUN_TEST(test_e2e_nested_control_flow);

    /* Map mutation */
    RUN_TEST(test_e2e_map_set);

    /* For_each with index */
    RUN_TEST(test_e2e_foreach_index);

    /* Const values */
    RUN_TEST(test_e2e_const_values);

    /* Empty containers */
    RUN_TEST(test_e2e_empty_array);
    RUN_TEST(test_e2e_empty_map);

    /* Nested function calls */
    RUN_TEST(test_e2e_nested_calls);

    /* String indexing */
    RUN_TEST(test_e2e_string_index);

    /* Enum comparison */
    RUN_TEST(test_e2e_enum_compare);

    /* Grouped parameters */
    RUN_TEST(test_e2e_grouped_parameters);

    /* Scope lifetime */
    RUN_TEST(test_e2e_loop_scope);

    /* #strict when */
    RUN_TEST(test_e2e_strict_when);

    /* C interop */
    RUN_TEST(test_e2e_c_interop);

    /* Atomic */
    RUN_TEST(test_e2e_atomic);

    /* Bigint types */
    RUN_TEST(test_e2e_i128_arithmetic);
    RUN_TEST(test_e2e_u128_arithmetic);
    RUN_TEST(test_e2e_i256_arithmetic);
    RUN_TEST(test_e2e_u256_arithmetic);
    RUN_TEST(test_e2e_bigint_comparison);
    RUN_TEST(test_e2e_bigint_cast);
    RUN_TEST(test_e2e_bigint_interpolation);
    RUN_TEST(test_e2e_bigint_mixed_width);

    /* Struct functions (extended) */
    RUN_TEST(test_e2e_struct_multiple_functions);
    RUN_TEST(test_e2e_struct_self_dispatch);
    RUN_TEST(test_e2e_struct_static_dispatch);

    /* Multi-return (extended) */
    RUN_TEST(test_e2e_multi_return_three);
    RUN_TEST(test_e2e_multi_return_mixed);
    RUN_TEST(test_e2e_multi_return_wildcard);

    /* Error handling (extended) */
    RUN_TEST(test_e2e_or_return_error_path);
    RUN_TEST(test_e2e_or_return_fallback);
    RUN_TEST(test_e2e_ensure_multiple);
    RUN_TEST(test_e2e_ensure_early_return);

    /* Pointer operations (extended) */
    RUN_TEST(test_e2e_ptr_to_ptr);
    RUN_TEST(test_e2e_ptr_compare);

    /* Container operations (extended) */
    RUN_TEST(test_e2e_map_int_keys);
    RUN_TEST(test_e2e_array_of_structs);

    /* Feature gaps (issue #2556) */
    RUN_TEST(test_e2e_bitwise_ops);
    RUN_TEST(test_e2e_defer_keyword);
    RUN_TEST(test_e2e_string_stdlib);
    RUN_TEST(test_e2e_chars_stdlib);
    RUN_TEST(test_e2e_strings_char_editing);
    RUN_TEST(test_e2e_strconv_format_quote);
    RUN_TEST(test_e2e_infer_mut_literals);
    RUN_TEST(test_e2e_fmt_bigint_directives);
    RUN_TEST(test_e2e_when_multi_value);
    RUN_TEST(test_e2e_map_remove);
    RUN_TEST(test_e2e_tagged_enum);
    RUN_TEST(test_e2e_struct_field_defaults);
    RUN_TEST(test_e2e_generic_type_param);

    PRINT_RESULTS();
    return _test_fail > 0 ? 1 : 0;
}
