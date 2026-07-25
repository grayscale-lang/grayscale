# Contributing to Grayscale

Thanks for your interest in contributing to Grayscale! This guide will help you get started.

> **Platform note:** Grayscale supports **macOS** and **Linux** only. Windows is not a supported contributor platform — you won't be able to build or test locally.

Before you start coding, skim [`STANDARD.md`](./STANDARD.md) — the language specification. It's the canonical reference for syntax, types, and every stdlib module.

## Table of Contents

- [Getting Started](#getting-started)
- [Installing Go](#installing-go)
- [Building from Source](#building-from-source)
- [How the Compiler Works](#how-the-compiler-works)
- [Making Changes](#making-changes)
- [Project Structure](#project-structure)
- [Running Tests](#running-tests)
- [Writing Tests](#writing-tests)
- [Code Style](#code-style)
- [Commit Messages](#commit-messages)
- [Submitting a Pull Request](#submitting-a-pull-request)
- [Reporting Issues](#reporting-issues)
- [AI-Assisted Contributions](#ai-assisted-contributions)
- [Good First Issues](#good-first-issues)

---

## Getting Started

### Prerequisites

- **Go 1.23 or higher** (for the `gray` CLI tooling)
- **C compiler** (gcc or clang)
- Git

### Fork and Clone the Repository

1. Fork the repository on GitHub
2. Clone your fork:

```bash
git clone https://github.com/YOUR-USERNAME/grayscale.git
cd grayscale
```

3. Add the upstream remote:

```bash
git remote add upstream https://github.com/grayscale-lang/grayscale.git
```

---

## Installing Go

> ⚠️ **Important:** Download the **pre-built binary**, not the source code!

Go to the official download page and select the installer for your platform:

👉 **https://go.dev/dl/**

| Platform | Download |
|----------|----------|
| macOS (Apple Silicon) | `go1.23.x.darwin-arm64.pkg` |
| macOS (Intel) | `go1.23.x.darwin-amd64.pkg` |
| Linux | `go1.23.x.linux-amd64.tar.gz` |

### macOS

1. Download the `.pkg` installer
2. Double-click to run it
3. Follow the installation prompts
4. **Restart your terminal**

### Linux

```bash
# Download (replace version as needed)
curl -LO https://go.dev/dl/go1.23.4.linux-amd64.tar.gz

# Extract to /usr/local
sudo tar -C /usr/local -xzf go1.23.4.linux-amd64.tar.gz

# Add to PATH (add this to ~/.bashrc or ~/.zshrc)
export PATH=$PATH:/usr/local/go/bin
```

### Verify Installation

```bash
go version
# Should output: go version go1.23.x ...
```

---

## Building from Source

```bash
# Build the binary
make build

# Verify it works — write a quick test file and run it
echo 'do main() { println("hello") }' > /tmp/test.gray
GRAY_COMPILER_PATH=./grayc/grayc ./gray /tmp/test.gray
```

> **Iterating on the compiler?** When you edit anything under `grayc/src/` and rebuild locally, you need to tell the `gray` wrapper to use your *local* `grayc` binary instead of the system-installed one. Set `GRAY_COMPILER_PATH=./grayc/grayc` on every command while iterating:
>
> ```bash
> GRAY_COMPILER_PATH=./grayc/grayc ./gray /tmp/test.gray
> ```
>
> Without this, you'll silently run against whatever `grayc` is installed on your system and wonder why your changes had no effect.

### Makefile Commands

| Command | Description |
|---------|-------------|
| `make build` | Build the `gray` binary |
| `make install` | Install to `/usr/local/bin` |
| `make clean` | Remove build artifacts |
| `make test` | Run the full test suite |
| `make test-unit` | C unit tests (lexer, parser, typechecker) |
| `make test-e2e` | End-to-end codegen tests |
| `make test-integration` | Integration tests (pass + fail) |
| `make test-go` | Go unit tests |
| `make test-ubsan` | UBSan sanitizer tests |
| `make test-asan` | ASan+UBSan tests (Linux recommended) |

All test targets can be run from the repo root — no need to `cd` into subdirectories.

---

## How the Compiler Works

Grayscale compiles source code to native binaries through a pipeline of stages. Understanding this helps you know which files to touch for different kinds of changes.

```
Source Code → Lexer → Parser → Type Checker → Code Gen → C Compiler → Binary
  (.gray)    tokens    AST     validated AST    .c file    cc/gcc       native
```

All compiler stages live in `grayc/src/`:

1. **Lexer** (`grayc/src/lexer/`) — Reads source text and produces tokens.
2. **Parser** (`grayc/src/parser/`) — Turns tokens into an Abstract Syntax Tree (AST).
3. **Type Checker** (`grayc/src/typechecker/`) — Walks the AST and validates types, catches errors before compilation.
4. **Code Generator** (`grayc/src/codegen/`) — Translates the AST into C source code.
5. **Runtime** (`grayc/src/runtime/`) — Core types (strings, arrays, maps, arenas) linked into every binary.
6. **Stdlib** (`grayc/src/stdlib/`) — Standard library modules compiled into `libgrayrt.a`.

The `gray` CLI (`cli/`) is a Go tooling wrapper that invokes the compiler. It provides watch mode, doc generation, and self-update.

**What this means in practice:** If you're adding a new language feature, you'll touch the C compiler stages (lexer → parser → typechecker → codegen). If you're adding a stdlib function, you add it to `grayc/src/stdlib/` and wire it into the codegen. If you're improving developer tooling (watch, etc.), you work in the Go code under `cli/`.

---

## Making Changes

The fastest way to iterate on a change:

```bash
# 1. Edit a file (e.g., grayc/src/stdlib/strings.c)

# 2. Rebuild
make build

# 3. Test with a quick .gray file
echo 'do main() { println("hello") }' > /tmp/test.gray
GRAY_COMPILER_PATH=./grayc/grayc ./gray /tmp/test.gray
```

This is a great way to quickly validate your change while developing. When your feature is working, make sure to add proper tests before submitting your PR (see [Writing Tests](#writing-tests)).

### Adding a Stdlib Function, Builtin, Constant, or Type

Any time you add or modify a user-facing stdlib function, builtin, constant, or type, every item below is **required** before your PR is ready.

#### C Implementation

- [ ] `grayc/src/stdlib/<module>.h` — declare the C function and add a `@man` block following the existing format
- [ ] `grayc/src/stdlib/<module>.c` — implement it

#### Typechecker (`grayc/src/typechecker/typechecker.c`) — 3 locations

- [ ] **Return type block** — find the `strcmp(mod, "<module>")` section and register what the function returns
- [ ] **`stdlib_func_meta[]`** — add a `StdlibFuncMeta` entry with arg count range, fallibility, and per-argument type checks
- [ ] **`_using_funcs[]`** — add `{"fn", "module", TK_RETURNTYPE}` so the `using` keyword resolves this function

#### Codegen (`grayc/src/codegen/codegen.c`)

- [ ] Emit the C call in `emit_<module>_call()`

#### Docs

- [ ] `STANDARD.md` — add the function to the module's table in the language spec
- [ ] Run `./scripts/generate_stdlib_man.sh` and commit the regenerated `cli/stdlib_man_data.go`

#### Build

- [ ] `make build` — must compile clean with zero warnings

PRs that add user-facing functionality without completing this checklist will not be merged.

### Adding or Modifying Diagnostics (Errors, Warnings, Panics)

All compiler diagnostics are defined in a single file: `grayc/src/util/error_codes.h`. This is the canonical registry.

1. **Add the code** to the appropriate macro list in `error_codes.h`:
   - `GRAY_ERROR("E####", "category", "message")` — compile-time errors
   - `GRAY_WARNING("W####", "category", "message")` — compile-time warnings
   - `GRAY_PANIC("P####", "category", "message")` — runtime panics

2. **Emit the diagnostic** from the appropriate compiler stage (parser, typechecker, or codegen) using the helpers in `grayc/src/util/error.h`:
   - `diagnostic_error_code()` / `diagnostic_warning_code()` — registry message, no args
   - `diagnostic_error_code_formatted()` — registry message template with `%s`/`%d` args
   - `diagnostic_error_message()` / `diagnostic_warning_message()` — custom message with a code

3. **Regenerate `ERRORS.md`** by running `./scripts/generate_errors.sh` and committing the output.

4. **Add a fail test** in `integration-tests/fail/errors/` named `E####_short_description.gray` that triggers the new error and verifies the compiler rejects it.

---

## Project Structure

```
grayscale/
├── cli/                   # Go CLI tooling wrapper
│   ├── main.go            # Entry point, version
│   ├── commands.go        # Subcommand definitions
│   ├── watch.go           # File watcher
│   ├── doc.go             # Documentation generator
│   └── update.go          # Self-update
├── internal/driver/        # Go wrapper for invoking grayc binary
├── grayc/                 # Grayscale compiler (C)
│   ├── src/lexer/         # Tokenization
│   ├── src/parser/        # Parsing (tokens → AST)
│   ├── src/typechecker/   # Static type checking
│   ├── src/codegen/       # Code generation (AST → C)
│   ├── src/runtime/       # Runtime (strings, arrays, maps, arenas)
│   ├── src/stdlib/        # Standard library modules
│   ├── src/util/          # Shared utilities (arena, buffer, error system)
│   └── tests/             # C unit and e2e tests
├── integration-tests/     # End-to-end test suite (.gray programs)
├── scripts/               # Code generation and test runner scripts
├── STANDARD.md            # Language specification
└── Makefile               # Build commands (builds both gray + grayc)
```

### Key Files for Common Tasks

| Task | Where to Look |
|------|---------------|
| Add a stdlib function | `grayc/src/stdlib/` + wire in `grayc/src/codegen/codegen.c` |
| Change syntax/grammar | `grayc/src/parser/parser.c` + `grayc/src/parser/ast.h` |
| Add a new keyword/token | `grayc/src/lexer/lexer.c` + `grayc/src/lexer/token.h` |
| Modify type checking | `grayc/src/typechecker/typechecker.c` |
| Change code generation | `grayc/src/codegen/codegen.c` |
| Add/modify error codes | `grayc/src/util/error_codes.h` + run `scripts/generate_errors.sh` |
| Add/modify stdlib docs | `@man` block in header + run `scripts/generate_stdlib_man.sh` |
| Add/modify builtin docs | `@man` block in header + run `scripts/generate_builtins_man.sh` |
| Improve CLI tooling | `cli/*.go` |
| Add a new language feature | Parser → Typechecker → Codegen (all in `grayc/src/`) |

---

## Running Tests

The simplest way to run everything:

```bash
make test
```

This builds the compiler, then runs unit tests, e2e tests, integration tests, and Go tests in sequence.

### Individual Suites

```bash
make test-unit        # C unit tests (lexer, parser, typechecker)
make test-e2e         # End-to-end codegen tests
make test-integration # Integration tests (pass + fail .gray programs)
make test-go          # Go unit tests
make test-ubsan       # UBSan sanitizer tests
make test-asan        # ASan+UBSan tests (Linux recommended)
```

All targets run from the repo root.

---

## Writing Tests

New features and bug fixes should include tests. The type of test depends on what you're changing.

### Unit Tests (C)

For compiler internals:

```bash
make test-unit          # lexer, parser, typechecker, util tests
make test-e2e           # full compilation pipeline tests
```

### Integration Tests (Grayscale)

For end-to-end behavior — verifying that Grayscale programs produce the right output. These are `.gray` files in the `integration-tests/` directory.

**Pass tests** (`integration-tests/pass/core/`) should run successfully and verify results:

```gray

do main() {
    mut passed int = 0
    mut failed int = 0

    // Test 1: description
    mut result int = 1 + 1
    if result == 2 {
        println("  [PASS] 1 + 1 = 2")
        passed += 1
    } otherwise {
        println("  [FAIL] 1 + 1: expected 2, got ${result}")
        failed += 1
    }

    println("Results: ${passed} passed, ${failed} failed")
    if failed > 0 {
        println("SOME TESTS FAILED")
    } otherwise {
        println("ALL TESTS PASSED")
    }
}
```

The test runner checks for `SOME TESTS FAILED` in the output — if it's present, the test fails.

**Fail tests** (`integration-tests/fail/errors/`) are minimal programs that should trigger a specific error. The test runner expects a non-zero exit code:

```gray
/*
 * Error Test: E3001 - type-mismatch
 * Expected: "type mismatch"
 */

do main() {
    mut x int = "hello"  // Should produce E3001
}
```

Name fail tests by error code: `E3001_type_mismatch.gray`.

**Multi-file tests** (`integration-tests/pass/multi-file/`) use subdirectories with a `main.gray` and supporting module files. Useful for testing imports, module visibility, and cross-file features.

For more details, see `TESTING.md`.

---

## Code Style

### Go code (`cli/`, `pkg/`, `internal/`)

- Run `gofmt` before committing
- Follow standard Go conventions
- Keep functions focused and readable

### C code (`grayc/src/`)

- The repo ships a `.clang-format` under `grayc/` — run `clang-format -i` on files you touch
- Match the style of the surrounding code; the compiler is consistent throughout

### General

- Add comments only for non-obvious logic — don't explain what the code already says
- When adding error codes, register them in `grayc/src/util/error_codes.h` (the canonical source). After adding or changing codes, run `scripts/generate_errors.sh` to regenerate `ERRORS.md` for the docs site.
- When adding any user-facing stdlib function, builtin, constant, or type: add `@man` blocks to the header, update `STANDARD.md`, and run the appropriate generation script (`scripts/generate_stdlib_man.sh` or `scripts/generate_builtins_man.sh`). See [Adding a Stdlib Function, Builtin, Constant, or Type](#adding-a-stdlib-function-builtin-constant-or-type) for the full checklist.

---

## Commit Messages

We use [Conventional Commits](https://www.conventionalcommits.org/). The format is:

```
type(scope): short description
```

**Types:**

| Type | When to Use |
|------|-------------|
| `feat` | New feature or functionality |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `test` | Adding or updating tests |
| `refactor` | Code change that doesn't fix a bug or add a feature |
| `chore` | Build, CI, tooling, or maintenance |

**Scope** is optional but helpful — it indicates what area of the codebase is affected. Canonical scopes:

| Scope | Area |
|-------|------|
| `lexer` | Tokenization (`grayc/src/lexer/`) |
| `parser` | Parsing / AST construction (`grayc/src/parser/`) |
| `typechecker` | Static type checking (`grayc/src/typechecker/`) |
| `codegen` | C code generation (`grayc/src/codegen/`) |
| `runtime` | Runtime library (`grayc/src/runtime/`) |
| `stdlib` | Standard library (`grayc/src/stdlib/`) — optionally nested, e.g. `stdlib/strings` |
| `cli` | Go CLI wrapper (`cli/`) |
| `tests` | Test additions or test infrastructure |
| `docs` | Documentation only |
| `ci` | GitHub Actions, release workflows |

Examples:

```bash
feat(parser): add named return variables support
fix(typechecker): prevent false positive on nested struct init
test(integration): add 7 core language integration tests
docs(stdlib): add doc comments to math module
feat(stdlib/strings): add 12 new string utility functions
chore(ci): add workflow_dispatch trigger to release-please
```

Keep the description short (under ~72 characters), lowercase, no period at the end.

---

## Submitting a Pull Request

1. **Create a branch** for your feature:
   ```bash
   git checkout -b feat/my-feature
   ```

2. **Make your changes** and test them

3. **Commit** with a clear message:
   ```bash
   git commit -m "feat(stdlib): add shout() function to strings module"
   ```

4. **Push** to your fork:
   ```bash
   git push origin feat/my-feature
   ```

5. **Open a Pull Request** against `main`

### Branch Naming

- `feat/` — New features
- `fix/` — Bug fixes
- `docs/` — Documentation changes
- `refactor/` — Code refactoring
- `test/` — Test additions

### PR Guidelines

- Keep changes focused and small when possible
- Include a clear description of what changed and why
- Make sure all tests pass (`make test`)
- Include unit and/or integration tests for new features and bug fixes

---

## Reporting Issues

### Bug Reports

**Every bug report must include the full output of `gray report`.** No exceptions. Run it and paste the complete output at the top of your issue — version, commit, install path, OS, CPU, RAM, and C compiler info are all load-bearing for triage. Bugs filed without `gray report` output will be asked for it before anything else happens.

Use this template:

```markdown
## Severity
Critical / High / Medium / Low — and why.

## System info
(paste full `gray report` output here)

## Summary
One paragraph. What's broken, in plain language.

## Reproduction
Minimal `.gray` program that triggers the bug. Inline the code in a fenced block.

## Expected
What should happen.

## Actual
What actually happens. Include the error message verbatim if there is one.

## Where to look (optional)
If you've poked around and have a guess at the root cause, note it. Saves triage time.

## Related (optional)
Link any related issues or recent commits that touched the same area.
```

Bugs filed in this shape get triaged quickly. One-paragraph bugs without a repro get parked.

### Feature Requests

- Describe the use case
- Explain why existing features don't solve it
- Provide examples of how it would work

---

## AI-Assisted Contributions

Grayscale was built with heavy use of AI tooling from day one, and AI-assisted contributions are absolutely welcome. Whether you're using Claude, Copilot, ChatGPT, or any other tool to help write code, that's fine. What matters is the end result.

That said, AI-generated code still needs a human behind it. You're responsible for what you submit. Don't just paste AI output into a PR — build it, run it, and make sure it actually works. If you can't explain what your code does and why, it's not ready to submit. AI gets things wrong all the time: hallucinated APIs, subtle logic bugs, code that looks right but breaks edge cases. Read through it critically before pushing.

The bar for AI-assisted contributions is the same as any other contribution: does it work, is it tested, and does it solve the problem?

---

## Good First Issues

Looking for something to work on? Check out issues labeled **"good first issue"**:

👉 [Good First Issues](https://github.com/grayscale-lang/grayscale/labels/good%20first%20issue)

Some ideas:
- Add documentation comments to stdlib functions
- Improve error messages
- Fix typos in docs

---

## Questions?

Feel free to open an issue if you get stuck or have questions!
