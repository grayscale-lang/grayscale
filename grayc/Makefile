# Grayscale Compiler Makefile
#
# Copyright (c) 2025-Present Marshall A Burns
# Licensed under the MIT License. See LICENSE for details.

VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")

# Windows (MSYS2/Git Bash/MinGW make). Everywhere else EXE is empty, WINDOWS is
# undefined, and every rule below is exactly what it was.
ifeq ($(OS),Windows_NT)
  EXE = .exe
  WINDOWS = 1
endif

ifdef WINDOWS
  # There is no `cc` on a stock MinGW-w64 install (MSYS2 happens to ship a
  # shim, winget/choco do not). CC is a make built-in whose default value is
  # "cc", so `CC ?=` would never fire here — override only when the user has
  # not chosen a compiler themselves.
  ifeq ($(origin CC),default)
    CC = gcc
  endif
  # gnu11, not c11: -std=c11 defines __STRICT_ANSI__ on MinGW-w64, which
  # unbinds printf from the ANSI-conforming implementation. Every %zu and %lld
  # in the compiler would then be emitted verbatim into generated C and into
  # diagnostics, and the POSIX-shaped names in <io.h> would be hidden.
  STD_FLAGS = -std=gnu11 -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601
else
  CC ?= cc
  STD_FLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L
endif

CFLAGS = $(STD_FLAGS) -Wall -Wextra -Wpedantic -Wno-unused-parameter -DGRAY_VERSION=\"$(VERSION)\"
# Auto-generate header dependencies so incremental builds rebuild every
# TU whose transitively-included headers changed. Without this, editing
# a shared header (e.g. scope.h) leaves stale .o files that were built
# against the old struct layout — classic silent heap corruption.
DEPFLAGS = -MMD -MP
LDFLAGS = -lm

# Build mode
ifdef DEBUG
	CFLAGS += -g -O0 -DDEBUG
else
	CFLAGS += -O2
endif

# Source files (compiler only, not runtime/stdlib which are compiled into user binaries)
SRC = src/main.c \
      src/util/arena.c \
      src/util/buf.c \
      src/util/error.c \
      src/util/error_codes.c \
      src/util/platform.c \
      src/lexer/token.c \
      src/lexer/lexer.c \
      src/parser/ast.c \
      src/parser/parser.c \
      src/typechecker/types.c \
      src/typechecker/scope.c \
      src/typechecker/typechecker.c \
      src/codegen/codegen.c \
      src/fmt/fmt.c

OBJ = $(SRC:.c=.o)
BIN = grayc$(EXE)

# Runtime/stdlib files compiled into a static library for fast linking
RT_SRC = src/runtime/runtime.c \
         src/runtime/array.c \
         src/runtime/map.c \
         src/stdlib/builtins.c \
         src/stdlib/mem.c \
         src/stdlib/fmt.c \
         src/stdlib/math.c \
         src/stdlib/strings.c \
         src/stdlib/io.c \
         src/stdlib/maps.c \
         src/stdlib/os.c \
         src/stdlib/arrays.c \
         src/stdlib/random.c \
         src/stdlib/time.c \
         src/stdlib/uuid.c \
         src/stdlib/encoding.c \
         src/stdlib/crypto.c \
         src/stdlib/bytes.c \
         src/stdlib/binary.c \
         src/stdlib/csv.c \
         src/stdlib/json.c \
         src/stdlib/sqlite.c \
         src/stdlib/threads.c \
         src/stdlib/sync.c \
         src/stdlib/atomic.c \
         src/stdlib/channels.c \
         src/stdlib/strconv.c \
         src/vendor/sqlite3.c

# Sockets (net, http, server) still need the Winsock port, and regex needs an
# engine since Windows has no <regex.h>. They are left out of the archive on
# Windows rather than stubbed: the linker only pulls objects a program actually
# references, so everything that does not import these modules links normally.
ifndef WINDOWS
RT_SRC += src/stdlib/regex.c \
          src/stdlib/net.c \
          src/stdlib/http.c \
          src/stdlib/server.c
endif

RT_OBJ = $(RT_SRC:.c=.o)
RT_LIB = libgrayrt.a

# OS and architecture detection
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),arm64)
  ATOMIC_ARCH = arm64
else ifeq ($(UNAME_M),aarch64)
  ATOMIC_ARCH = arm64
else ifeq ($(UNAME_M),x86_64)
  ATOMIC_ARCH = x86_64
endif

ifdef WINDOWS
# arch/*/atomic.s is written for the System V AMD64 and AAPCS conventions,
# taking arguments in rdi/rsi/rdx. Windows x64 passes them in rcx/rdx/r8, so
# that assembly reads the wrong registers here — and it still assembles
# cleanly, making the result silent corruption rather than a build error. Use
# the intrinsic implementation instead, which is correct for any ABI.
ATOMIC_SRC = src/runtime/atomic_builtin.c
ATOMIC_OBJ = src/runtime/atomic_builtin.o
else ifdef ATOMIC_ARCH
ATOMIC_SRC = src/runtime/arch/$(ATOMIC_ARCH)/atomic.s
ATOMIC_OBJ = src/runtime/arch/$(ATOMIC_ARCH)/atomic.o
endif

# Include paths for compiler source
INCLUDES = -Isrc

.PHONY: all build compiler-only clean install test leaks

all: build

build: $(BIN) $(RT_LIB)
	@echo "gray: build complete ($(BIN) + $(RT_LIB))"

# Compiler frontend only — skips libgrayrt.a. Useful for fast iteration on
# the lexer/parser/typechecker.
compiler-only: $(BIN)
	@echo "gray: compiler-only build complete ($(BIN); $(RT_LIB) not built)"

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(RT_LIB): $(RT_OBJ) $(ATOMIC_OBJ)
	ar rcs $@ $^

# Assembly rule for arch-specific atomic operations. On Windows ATOMIC_SRC is
# a .c file, which the generic src/runtime/%.o rule below already builds with
# the full runtime flags.
ifndef WINDOWS
ifdef ATOMIC_ARCH
$(ATOMIC_OBJ): $(ATOMIC_SRC)
	$(CC) -c -o $@ $<
endif
endif

# Runtime/stdlib flags: same as CFLAGS but no -Wpedantic (GCC statement expressions)
RT_CFLAGS = $(STD_FLAGS) -Wall -Wextra -Wno-unused-parameter -O2

src/runtime/%.o: src/runtime/%.c
	$(CC) $(RT_CFLAGS) $(INCLUDES) $(DEPFLAGS) -c -o $@ $<

src/stdlib/%.o: src/stdlib/%.c
	$(CC) $(RT_CFLAGS) $(INCLUDES) $(DEPFLAGS) -c -o $@ $<

# SQLite amalgamation — relaxed warnings, single-threaded mode
VENDOR_CFLAGS = -std=c11 -w -O2 -DSQLITE_THREADSAFE=0

src/vendor/sqlite3.o: src/vendor/sqlite3.c
	$(CC) $(VENDOR_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c -o $@ $<

# Pull in generated header dependency files
-include $(OBJ:.o=.d) $(RT_OBJ:.o=.d)

clean:
	rm -f $(OBJ) $(RT_OBJ) $(ATOMIC_OBJ) $(BIN) $(RT_LIB) $(OBJ:.o=.d) $(RT_OBJ:.o=.d) tests/test_lexer$(EXE) tests/test_parser$(EXE) tests/test_typechecker$(EXE) tests/test_util$(EXE) tests/test_codegen$(EXE) tests/test_runtime$(EXE) tests/test_panics$(EXE) tests/test_stdlib$(EXE) tests/*_san* tests/*.d
	rm -rf tests/*.dSYM

install: build
	install -d /usr/local/bin
	install -m 755 $(BIN) /usr/local/bin/
	install -d /usr/local/lib/grayc/runtime
	install -d /usr/local/lib/grayc/stdlib
	install -m 644 src/runtime/runtime.h /usr/local/lib/grayc/runtime/
	install -m 644 src/runtime/array.h /usr/local/lib/grayc/runtime/
	install -m 644 src/runtime/map.h /usr/local/lib/grayc/runtime/
	install -m 644 src/stdlib/builtins.h /usr/local/lib/grayc/stdlib/
	install -m 644 src/stdlib/mem.h /usr/local/lib/grayc/stdlib/
	install -m 644 $(RT_LIB) /usr/local/lib/grayc/

# Shared objects needed by test binaries (everything except main.o)
TEST_DEPS = src/util/arena.o src/util/buf.o src/util/error.o src/util/error_codes.o \
            src/util/platform.o \
            src/lexer/token.o src/lexer/lexer.o \
            src/parser/ast.o src/parser/parser.o \
            src/typechecker/types.o src/typechecker/scope.o src/typechecker/typechecker.o

# Runtime objects needed by the runtime test binary
RT_TEST_DEPS = src/runtime/runtime.o src/runtime/array.o src/runtime/map.o $(ATOMIC_OBJ)

# Objects needed by the panic test binary (runtime + stdlib modules that emit panics)
PANIC_TEST_DEPS = src/runtime/runtime.o src/runtime/array.o src/runtime/map.o \
                  src/stdlib/encoding.o src/stdlib/arrays.o src/stdlib/builtins.o \
                  src/stdlib/strconv.o src/stdlib/random.o src/stdlib/strings.o \
                  src/stdlib/io.o $(ATOMIC_OBJ)

tests/test_lexer$(EXE): tests/test_lexer.c $(TEST_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_parser$(EXE): tests/test_parser.c $(TEST_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_typechecker$(EXE): tests/test_typechecker.c $(TEST_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_util$(EXE): tests/test_util.c src/util/arena.o src/util/buf.o src/typechecker/scope.o src/typechecker/types.o src/util/error.o src/util/error_codes.o src/util/platform.o
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_codegen$(EXE): tests/test_codegen.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(INCLUDES) -o $@ $< $(LDFLAGS)

tests/test_runtime$(EXE): tests/test_runtime.c $(RT_TEST_DEPS)
	$(CC) $(RT_CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_panics$(EXE): tests/test_panics.c $(PANIC_TEST_DEPS)
	$(CC) $(RT_CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

# Objects needed by the stdlib test binary (runtime + tested stdlib modules)
STDLIB_TEST_DEPS = src/runtime/runtime.o src/runtime/array.o src/runtime/map.o \
                   src/stdlib/strings.o src/stdlib/arrays.o src/stdlib/maps.o \
                   src/stdlib/math.o src/stdlib/fmt.o src/stdlib/encoding.o \
                   src/stdlib/strconv.o $(ATOMIC_OBJ)

tests/test_stdlib$(EXE): tests/test_stdlib.c $(STDLIB_TEST_DEPS)
	$(CC) $(RT_CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

# The runtime, panic, and stdlib suites link against the runtime and standard
# library, which are still POSIX-only; test_panics is additionally fork-based.
# On Windows run the frontend suites, and say plainly what was left out.
ifdef WINDOWS
UNIT_SUITES = lexer parser typechecker util
else
UNIT_SUITES = lexer parser typechecker util runtime panics stdlib
endif
UNIT_BINS = $(addprefix tests/test_,$(addsuffix $(EXE),$(UNIT_SUITES)))

test-unit: $(TEST_DEPS) $(UNIT_BINS)
	@echo ""
	@for suite in $(UNIT_SUITES); do \
		echo "=== $$suite Tests ==="; \
		./tests/test_$$suite$(EXE) || exit 1; \
	done
	@rm -f $(UNIT_BINS)
	@rm -rf tests/*.dSYM
ifdef WINDOWS
	@echo ""
	@echo "Skipped on Windows: runtime, panics, stdlib (need libgrayrt.a; panics is fork-based)."
endif

ifdef WINDOWS
test-e2e:
	@echo "test-e2e is not available on Windows: tests/test_codegen.c uses popen()"
	@echo "and /tmp, and it links Grayscale programs, which needs libgrayrt.a."
else
test-e2e: build tests/test_codegen$(EXE)
	@echo ""
	@echo "=== End-to-End Codegen Tests ==="
	@./tests/test_codegen$(EXE)
	@rm -f tests/test_codegen$(EXE)
	@rm -rf tests/*.dSYM
endif

ifdef WINDOWS
test-integration:
	@echo "test-integration is not available on Windows: scripts/run_tests.sh"
	@echo "needs bash, and the tests link binaries, which needs libgrayrt.a."
else
test-integration: build
	@../scripts/run_tests.sh
endif

# Sanitizer builds — catch undefined behavior (and memory bugs on Linux)
# Must compile ALL source files with sanitizer flags
TEST_SRC_FILES = src/util/arena.c src/util/buf.c src/util/error.c src/util/error_codes.c \
                 src/util/platform.c \
                 src/lexer/token.c src/lexer/lexer.c \
                 src/parser/ast.c src/parser/parser.c \
                 src/typechecker/types.c src/typechecker/scope.c src/typechecker/typechecker.c

# UBSan works everywhere; ASan needs Linux for reliable CI
UBSAN_FLAGS = -fsanitize=undefined -fno-omit-frame-pointer -g -O1
ASAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

# mingw-w64 GCC ships no sanitizer runtimes, so neither target can link there.
ifdef WINDOWS
test-ubsan test-asan:
	@echo "Sanitizers are not available with mingw-w64 GCC (no libubsan/libasan)."
	@echo "Run 'make test-ubsan' / 'make test-asan' on Linux or macOS."

leaks:
	@echo "Leak checking needs macOS 'leaks' or Linux valgrind; neither exists here."
else

test-ubsan:
	@echo ""
	@echo "=== UBSan Lexer Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_lexer_san$(EXE) tests/test_lexer.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_lexer_san$(EXE)
	@echo "=== UBSan Parser Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_parser_san$(EXE) tests/test_parser.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_parser_san$(EXE)
	@echo "=== UBSan Type Checker Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_typechecker_san$(EXE) tests/test_typechecker.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_typechecker_san$(EXE)
	@echo "=== UBSan Util Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_util_san$(EXE) tests/test_util.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_util_san$(EXE)
	@echo "=== UBSan Runtime Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_runtime_san$(EXE) tests/test_runtime.c src/runtime/runtime.c src/runtime/array.c src/runtime/map.c $(LDFLAGS)
	@./tests/test_runtime_san$(EXE)
	@rm -f tests/test_lexer_san$(EXE) tests/test_parser_san$(EXE) tests/test_typechecker_san$(EXE) tests/test_util_san$(EXE) tests/test_runtime_san$(EXE)
	@rm -rf tests/*.dSYM
	@echo ""
	@echo "All UBSan tests passed — no undefined behavior detected."

test-asan:
	@echo ""
	@echo "=== ASan+UBSan Tests (Linux recommended) ==="
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_lexer_san$(EXE) tests/test_lexer.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_lexer_san$(EXE)
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_parser_san$(EXE) tests/test_parser.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_parser_san$(EXE)
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_typechecker_san$(EXE) tests/test_typechecker.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_typechecker_san$(EXE)
	@echo "=== ASan+UBSan Util Tests ==="
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_util_san$(EXE) tests/test_util.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_util_san$(EXE)
	@echo "=== ASan+UBSan Runtime Tests ==="
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_runtime_san$(EXE) tests/test_runtime.c src/runtime/runtime.c src/runtime/array.c src/runtime/map.c $(LDFLAGS)
	@./tests/test_runtime_san$(EXE)
	@rm -f tests/test_lexer_san$(EXE) tests/test_parser_san$(EXE) tests/test_typechecker_san$(EXE) tests/test_util_san$(EXE) tests/test_runtime_san$(EXE)
	@rm -rf tests/*.dSYM
	@echo ""
	@echo "All ASan+UBSan tests passed — no memory errors or undefined behavior."

# Format check
format-check:
	@echo "Checking C code formatting..."
	@find src -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror 2>&1 && echo "All files formatted correctly" || echo "Formatting issues found — run 'make format' to fix"

format:
	@find src -name '*.c' -o -name '*.h' | xargs clang-format -i
	@echo "Formatted all C files"

test: test-unit test-e2e

leaks: $(BIN)
	@echo ""
ifeq ($(UNAME_S),Darwin)
	@echo "=== Leak check: macOS (leaks --atExit) ==="
	@echo "(compiling tests.gray through the compiler to exercise all allocations)"
	@echo ""
	@# Copy grayc to a temp binary and sign it with get-task-allow so leaks
	@# can attach. The copy is deleted immediately after; the repo binary
	@# is never modified and carries no residual entitlement.
	@cp ./grayc /tmp/grayc_leak_check
	@codesign -s - --entitlements ../scripts/leak-check.entitlements \
	    /tmp/grayc_leak_check 2>/dev/null
	@leaks --atExit -- /tmp/grayc_leak_check ../cli/tests.gray \
	    -o /tmp/gray_leak_out 2>&1; LEAK_EXIT=$$?; \
	    rm -f /tmp/grayc_leak_check /tmp/gray_leak_out; \
	    echo ""; \
	    if [ $$LEAK_EXIT -ne 0 ]; then \
	        echo "LEAK CHECK FAILED — memory leaks detected in the Grayscale compiler (grayc)."; \
	        echo "Scroll up to see the allocation stacks reported by 'leaks'."; \
	        echo "These are compiler bugs for contributors to fix — not issues with your Grayscale code."; \
	    else \
	        echo "LEAK CHECK PASSED — no memory leaks detected."; \
	    fi; \
	    exit $$LEAK_EXIT
else
	@echo "=== Leak check: Linux (valgrind) ==="
	@echo "(compiling tests.gray through the compiler to exercise all allocations)"
	@echo ""
	@command -v valgrind >/dev/null 2>&1 || \
	    { echo "valgrind not found; install with: apt install valgrind"; exit 1; }
	@valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
	    ./grayc ../cli/tests.gray -o /tmp/gray_leak_out 2>&1; LEAK_EXIT=$$?; \
	    rm -f /tmp/gray_leak_out; \
	    echo ""; \
	    if [ $$LEAK_EXIT -ne 0 ]; then \
	        echo "LEAK CHECK FAILED — memory leaks detected in the Grayscale compiler (grayc)."; \
	        echo "Scroll up to see the valgrind report with allocation stacks."; \
	        echo "These are compiler bugs for contributors to fix — not issues with your Grayscale code."; \
	    else \
	        echo "LEAK CHECK PASSED — no memory leaks detected."; \
	    fi; \
	    exit $$LEAK_EXIT
endif

# End of the POSIX-only sanitizer and leak-check targets.
endif
