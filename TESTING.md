# Grayscale Language Testing Guide

## Quick Start

To run the full test suite, use:

```bash
make test
```

---

## Compiler Tests

The Grayscale compiler has a comprehensive test suite written in C, located in `grayc/tests/`.

### Unit Tests

Unit tests validate individual compiler components:

- **Lexer Tests** (`grayc/tests/test_lexer.c`): Token scanning, keyword recognition, literal formats, comment handling, attribute tokens, operator disambiguation, and edge cases.
- **Parser Tests** (`grayc/tests/test_parser.c`): Declarations, imports, control flow, structs, enums, function references, attributes, map/array types, visibility, grouped params, compound assignments, and parser error codes.
- **Typechecker Tests** (`grayc/tests/test_typechecker.c`): Scope management, type resolution, expression inference, built-in return types, error detection, enum/map type resolution, bigint types, and error code coverage.
- **Util Tests** (`grayc/tests/test_util.c`): Arena allocator (create, alloc, alignment, multi-block, oversized, strdup/strndup), growable buffer (create, append, growth, formatting, indentation), and scope (lookup, define/update, hash rebuild, many symbols, immutability).

### End-to-End Tests

E2E tests (`grayc/tests/test_codegen.c`) compile Grayscale programs, run them, and verify output. Covers variables, control flow, functions, data structures, string features, structs, enums, maps, pointers, runtime checks, and more.

**Running:**

```bash
# From repo root
make test-unit        # unit tests (lexer + parser + typechecker + util)
make test-e2e         # e2e codegen tests

# Individual test suites (from grayc/)
./grayc/tests/test_lexer
./grayc/tests/test_parser
./grayc/tests/test_typechecker
./grayc/tests/test_util
./grayc/tests/test_codegen
```

### Integration Tests

Integration tests compile and run `.gray` programs end-to-end through the full compiler pipeline.

**Structure:**

- `integration-tests/pass/core/` — Core language feature tests covering arrays, control flow, structs, enums, maps, pointers, named returns, type inference, builtins, C interop, bigint types, wildcards, and more.
- `integration-tests/pass/stdlib/` — Stdlib module tests covering all stdlib modules.
- `integration-tests/pass/new/` — Project template tests (basic, cli, lib, multi, server_minimal, server_normal, client_minimal, client_normal).
- `integration-tests/pass/warnings/` — Warning detection tests covering all W-code warnings.
- `integration-tests/pass/multi-file/` — Multi-file import tests covering imports, aliases, structs, enums, private visibility, transitive imports, directory imports, and more.
- `integration-tests/pass/stress/` — Stress tests (core and stdlib).
- `integration-tests/fail/errors/` — Error detection tests covering all compiler error codes (E1xxx–E9xxx) and runtime panics.
- `integration-tests/fail/multi-file/` — Multi-file error detection tests covering cross-module type errors, private access violations, and circular imports.

**Running:**

```bash
make test-integration

# With verbose output on failures
bash scripts/run_tests.sh --verbose
```

### Sanitizer Tests

ASan (AddressSanitizer) and UBSan (Undefined Behavior Sanitizer) builds catch memory bugs and undefined behavior.

```bash
# UBSan — runs on all platforms
make test-ubsan

# ASan + UBSan — Linux recommended
make test-asan
```

---

## Go Tooling Tests

The Go CLI (`gray`) has unit tests for the packages it uses:

- `cli` — updater semver parsing/comparison and exact-version install validation.
- `internal/driver` — compiler binary lookup and `GRAY_COMPILER_PATH` override behavior.

```bash
make test-go
```

---

## Running Everything

```bash
make test
```

This builds the compiler, then runs unit tests, e2e tests, integration tests, and Go tests in sequence.

---

## CI

All tests run automatically on push to `main` via GitHub Actions:

| Platform | Compiler | Sanitizers | Go Tooling |
|----------|:--------:|:----------:|:----------:|
| Ubuntu   | unit + e2e + integration | UBSan + ASan |  |
| macOS    | unit + e2e + integration | UBSan |  |

CI workflow: `.github/workflows/ci.yml`
