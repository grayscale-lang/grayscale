# Contributing to Grayscale

Thanks for your interest in contributing to Grayscale! This guide will help you get started.

> **Platform note:** Grayscale builds on **macOS**, **Linux**, and **Windows**. Windows contributors should read [Building from Source -> Windows](#windows) for toolchain setup.

Before you start coding, skim [`STANDARD.md`](./STANDARD.md) — the language specification. It's the canonical reference for syntax, types, and every stdlib module.

## Table of Contents

- [Getting Started](#getting-started)
- [Installing Go](#installing-go)
- [Building from Source](#building-from-source)
  - [Unix-based systems (macOS and Linux)](#unix-based-systems-macos-and-linux)
  - [Windows](#windows)
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

Grayscale builds with `make` on all three platforms. Start with
[Unix-based systems (macOS and Linux)](#unix-based-systems-macos-and-linux) or
[Windows](#windows), then come back to the shared sections below.

### Unix-based systems (macOS and Linux)

```bash
# Build the binary
make build

# Verify it works — write a quick test file and run it
echo 'do main() { println("hello") }' > main.gray
./gray main.gray
```

> **Iterating on the compiler?** When `./gray` runs from a source checkout, it
> automatically prefers the locally built `grayc/grayc` binary next to it over
> the embedded or installed compiler, so a `make -C grayc build` is picked up
> on the next run with no extra setup. To point at a compiler somewhere else
> entirely, set `GRAY_COMPILER_PATH=/path/to/grayc` — the explicit override
> always wins.

#### Makefile Commands

| Command | Description |
|---------|-------------|
| `make build` | Build the `gray` binary (compiler embedded) |
| `make stubs` | Create empty embed stubs (for dev `go build`) |
| `make install` | Install `gray` to `/usr/local/bin` |
| `make uninstall` | Remove `gray` from `/usr/local/bin` |
| `make clean` | Remove built binaries |
| `make leaks` | Check compiler for memory leaks (macOS: `leaks`, Linux: `valgrind`) |
| `make benchmark` | Run the real-workload benchmark suite (POSIX only); see `benchmarks/README.md` |
| `make test` | Run the full test suite (unit + e2e + integration + Go) |
| `make test-unit` | C unit tests (lexer, parser, typechecker) |
| `make test-e2e` | End-to-end codegen tests |
| `make test-integration` | Integration tests (pass + fail) |
| `make test-go` | Go unit tests |
| `make test-ubsan` | UBSan sanitizer tests |
| `make test-asan` | ASan+UBSan sanitizer tests (Linux recommended) |

All test targets can be run from the repo root — no need to `cd` into subdirectories.

### Windows

#### Prerequisites

- **MinGW-w64 GCC or Clang.** MSVC is not supported and is not planned: the
  compiler emits GNU C (statement expressions, `__auto_type`, `__typeof__`,
  `__builtin_*_overflow`) into every generated program.
- **Go 1.23 or higher**

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT.Base   # or: choco install mingw
```

MSYS2 works too; the scripts find `C:\msys64\ucrt64\bin` and similar locations
automatically and put them on `PATH` for the session. (That last part matters:
`cc1.exe` loads its DLLs from beside `gcc.exe`, and without them the build
fails with exit 1 and no diagnostic at all.)

#### Build and test

Use `make`, the same as every other platform. It needs a POSIX shell, which
MSYS2 and Git Bash both provide.

```bash
make build       # grayc.exe + gray.exe
make install     # install to %USERPROFILE%\.gray\bin (prints PATH instructions)
make test-unit   # C unit suites
make test-e2e    # end-to-end codegen tests
make test-go     # Go unit tests
make clean
```

The Makefiles detect Windows via `OS=Windows_NT`, add the `.exe` suffix, select
`gcc` over the nonexistent `cc`, and switch to the MinGW flag set. The
sanitizer and leak-check targets have no MinGW equivalent and say so rather
than failing obscurely.

`test_panics` is the one suite that does not run on Windows: it forks a child
process per case to capture the panic, and Windows has no `fork`.

#### How grayc finds a C compiler

grayc picks the C compiler for generated programs in this order:

1. `--cc <command>` on the grayc command line (what `gray cross` uses)
2. `GRAY_CC`, then `CC` environment variables — each probed with `--version`
   and skipped if it does not run; multi-word values are ignored, use `--cc`
   for those
3. `gcc`, `clang`, then `cc` on `PATH`
4. Windows only: well-known install locations
   (`C:\msys64\{ucrt64,mingw64,clang64}\bin`, `C:\mingw64\bin`, chocolatey's
   MinGW, `%ProgramFiles%\LLVM`)

When the winner is an absolute path, its directory is prepended to `PATH` for
the process — `cc1.exe` resolves its DLLs via `PATH` from beside `gcc.exe`,
so the path alone is not enough. The practical upshot: `grayc.exe` works from
PowerShell or cmd even when MinGW is not on `PATH`.

#### Writing portable compiler code

`grayc/src/util/platform.h` is the single place OS differences are handled.
When touching compiler code, use it rather than POSIX headers directly:

| Instead of | Use |
|------------|-----|
| `strrchr(path, '/')` | `gray_path_basename()` / `gray_path_rsep()` |
| `snprintf(buf, n, "%s/%s", a, b)` | `gray_path_join()` |
| `realpath(p, NULL)` | `gray_realpath()` / `gray_realpath_into()` |
| `strcmp()` on paths | `gray_path_equal()` |
| `stat()` + `S_ISDIR`/`S_ISREG` | `gray_is_dir()` / `gray_is_file()` |
| `access(p, R_OK)`, `unlink()`, `getcwd()` | `gray_file_readable()`, `gray_remove_file()`, `gray_getcwd()` |
| `p[0] == '/'` | `gray_path_is_absolute()` |
| `tmpfile()`, `/tmp/...` | `gray_tmpfile()`, `gray_temp_path()` |
| `isatty(STDERR_FILENO)` | `gray_stderr_is_tty()` / `gray_stdout_is_tty()` |
| `system()`, `fork()` + `execv()` | `gray_spawn_path()` / `gray_spawn_exact()` / `gray_spawn_quiet()` |

The compiler invokes the C compiler through an **argv array**, never a shell
string. That means arguments containing spaces need no quoting and can never be
reinterpreted as shell syntax — do not reintroduce `system()`.

`platform.h` deliberately does not include `<windows.h>`; that header defines
`ERROR`, `min`, and `max` as macros, which collide with the compiler's own
`Severity` enum. Keep Windows headers inside `platform.c`.

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
touch main.gray
# Inside the main.gray file:
# do main(){
#   println("hello")
# }
./gray main.gray
```

This is a great way to quickly validate your change while developing. When your feature is working, make sure to add proper tests before submitting your PR (see [Writing Tests](#writing-tests)).

### Adding a Stdlib Function, Builtin, Constant, or Type

Any time you add or modify a user-facing stdlib function, builtin, constant, or
type, all of the following are **required** before your PR is ready:

- [ ] **C implementation** — declare it in `grayc/src/stdlib/<module>.h` with a
      `@man` block, implement it in `grayc/src/stdlib/<module>.c`
- [ ] **Typechecker** — `grayc/src/typechecker/typechecker.c` knows the
      signature: return type, argument count and types, and fallibility
- [ ] **Codegen** — `grayc/src/codegen/codegen.c` emits the C call
- [ ] **`STANDARD.md`** — add it to the module's table in the language spec
- [ ] Run `./scripts/generate_stdlib_man.sh` and commit the regenerated file
- [ ] `make build` compiles clean with zero warnings

The typechecker and codegen internals move around; don't work from a list of
symbol names. Find an existing function in the same module, grep for every place
its name appears, and mirror it. PRs that add user-facing functionality without
completing this checklist will not be merged.

### Adding or Modifying Diagnostics (Errors, Warnings, Panics)

All compiler diagnostics are defined in a single file: `grayc/src/util/error_codes.h`. This is the canonical registry.

**Choosing a code.** One error code = one user-visible situation. A custom
message passed to `diagnostic_error_message` supplies site-specific context (a
name, a value, a type in the sentence) — not a different *kind* of error. Reach
for the most specific existing code; if none fits, add a new one at the end of
its range (never fill historical gaps). Do not reuse a broad code like `E3001`
("assignment type mismatch") as a catch-all — argument type mismatch is `E5026`,
a bad number of arguments is `E5008`. `scripts/check_error_codes.gray` runs in CI
and fails the build on a code that is emitted but unregistered, registered but
never emitted, duplicated, emitted from both `parser.c` and `typechecker.c`, or
spread across more than 15 call sites.

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
├── cli/                      # Go CLI tooling wrapper (the `gray` command)
│   ├── main.go               # Entry point, version
│   ├── commands.go           # Subcommand definitions (build, run, check, cross, report, ...)
│   ├── new.go                # `gray new` project scaffolding
│   ├── watch.go              # File watcher (`gray watch`)
│   ├── fmt.go                # `gray fmt` driver
│   ├── doc.go                # Documentation generator (`gray doc`)
│   ├── verify.go             # Install / toolchain verification
│   ├── update.go             # Self-update
│   ├── *_man_data.go         # Generated man data (stdlib, builtins, lang) — do not hand-edit
│   └── templates/            # Embedded project templates used by `gray new`
├── internal/driver/          # Embeds the grayc binary and invokes it from the CLI
├── grayc/                    # Grayscale compiler (C)
│   ├── Makefile              # Compiler build (the root Makefile delegates here)
│   ├── src/main.c            # Compiler entry point — drives the pipeline
│   ├── src/lexer/            # Tokenization
│   ├── src/parser/           # Parsing (tokens → AST) and import resolution
│   ├── src/typechecker/      # Static type checking
│   ├── src/codegen/          # Code generation (AST → C)
│   ├── src/fmt/              # Source formatter backend for `gray fmt`
│   ├── src/runtime/          # Runtime (strings, arrays, maps, arenas)
│   ├── src/stdlib/           # Standard library modules → libgrayrt.a
│   ├── src/util/             # Shared utilities (arena, buffer, error system)
│   ├── src/vendor/           # Vendored third-party C (sqlite3)
│   └── tests/                # C unit and e2e test suites
├── integration-tests/        # End-to-end test suite (.gray programs: pass/ + fail/)
├── scripts/                  # Code-gen and test-runner scripts (.sh + .ps1 for Windows)
├── .github/                  # CI workflows, issue templates, PR template
├── README.md                 # Project overview
├── STANDARD.md               # Language specification
├── ERRORS.md                 # Generated diagnostic registry (from error_codes.h)
├── TESTING.md                # Test layout and conventions
├── CHANGELOG.md              # Generated by release-please
├── Makefile                  # Top-level build (builds both gray + grayc)
└── LICENSE
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
| Change source formatting | `grayc/src/fmt/` + `cli/fmt.go` |
| Fix module/import resolution | `grayc/src/parser/imports.c` + `grayc/src/parser/module_table.c` |
| Scaffold a new project type | `cli/new.go` + `cli/templates/` |
| Improve CLI tooling | `cli/*.go` |
| Change CI / release workflows | `.github/workflows/` |
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

### Go code (`cli/`, `internal/`)

- Run `gofmt` before committing
- Follow standard Go conventions
- Keep functions focused and readable

### C code (`grayc/src/`)

- The repo ships a `.clang-format` under `grayc/` — run `clang-format -i` on files you touch
- Match the style of the surrounding code; the compiler is consistent throughout

### General

- Add comments only for non-obvious logic — don't explain what the code already says
- Add yourself to the `Contributors:` section of the header of any source file you change; add the section if it isn't there. GitHub handle at minimum, first and last name if you choose — one line.
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

Open a bug issue and fill in the form — it walks you through severity, summary,
reproduction, and expected vs. actual behavior.

**Every bug report must include the full output of `gray report`.** No exceptions. Run it and paste the complete output into the issue — version, commit, install path, OS, CPU, RAM, and C compiler info are all load-bearing for triage. Bugs filed without `gray report` output, or without a minimal `.gray` repro, get parked until they have both.

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
