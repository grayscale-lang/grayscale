# Grayscale Compiler Makefile
#
# Copyright (c) 2025-Present Marshall A Burns
# Licensed under the MIT License. See LICENSE for details.

CC ?= cc
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Wno-unused-parameter -DGRAY_VERSION=\"$(VERSION)\"
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
BIN = grayc

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
         src/stdlib/regex.c \
         src/stdlib/net.c \
         src/stdlib/http.c \
         src/stdlib/server.c \
         src/stdlib/strconv.c \
         src/vendor/sqlite3.c
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

ifdef ATOMIC_ARCH
  ATOMIC_SRC = src/runtime/arch/$(ATOMIC_ARCH)/atomic.s
  ATOMIC_OBJ = src/runtime/arch/$(ATOMIC_ARCH)/atomic.o
endif

# Include paths for compiler source
INCLUDES = -Isrc

.PHONY: all build clean install test leaks

all: build

build: $(BIN) $(RT_LIB)
	@echo "gray: build complete ($(BIN) + $(RT_LIB))"

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

$(RT_LIB): $(RT_OBJ) $(ATOMIC_OBJ)
	ar rcs $@ $^

# Assembly rule for arch-specific atomic operations
ifdef ATOMIC_ARCH
$(ATOMIC_OBJ): $(ATOMIC_SRC)
	$(CC) -c -o $@ $<
endif

# Runtime/stdlib flags: same as CFLAGS but no -Wpedantic (GCC statement expressions)
RT_CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -O2

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
	rm -f $(OBJ) $(RT_OBJ) $(ATOMIC_OBJ) $(BIN) $(RT_LIB) $(OBJ:.o=.d) $(RT_OBJ:.o=.d) tests/test_lexer tests/test_parser tests/test_typechecker tests/test_util tests/test_codegen tests/test_runtime tests/test_panics tests/test_stdlib tests/*_san tests/*.d

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

tests/test_lexer: tests/test_lexer.c $(TEST_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_parser: tests/test_parser.c $(TEST_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_typechecker: tests/test_typechecker.c $(TEST_DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_util: tests/test_util.c src/util/arena.o src/util/buf.o src/typechecker/scope.o src/typechecker/types.o src/util/error.o src/util/error_codes.o
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_codegen: tests/test_codegen.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L $(INCLUDES) -o $@ $< $(LDFLAGS)

tests/test_runtime: tests/test_runtime.c $(RT_TEST_DEPS)
	$(CC) $(RT_CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

tests/test_panics: tests/test_panics.c $(PANIC_TEST_DEPS)
	$(CC) $(RT_CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

# Objects needed by the stdlib test binary (runtime + tested stdlib modules)
STDLIB_TEST_DEPS = src/runtime/runtime.o src/runtime/array.o src/runtime/map.o \
                   src/stdlib/strings.o src/stdlib/arrays.o src/stdlib/maps.o \
                   src/stdlib/math.o src/stdlib/fmt.o src/stdlib/encoding.o \
                   src/stdlib/strconv.o $(ATOMIC_OBJ)

tests/test_stdlib: tests/test_stdlib.c $(STDLIB_TEST_DEPS)
	$(CC) $(RT_CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

test-unit: $(TEST_DEPS) tests/test_lexer tests/test_parser tests/test_typechecker tests/test_util tests/test_runtime tests/test_panics tests/test_stdlib
	@echo ""
	@echo "=== Lexer Tests ==="
	@./tests/test_lexer
	@echo "=== Parser Tests ==="
	@./tests/test_parser
	@echo "=== Type Checker Tests ==="
	@./tests/test_typechecker
	@echo "=== Util Tests ==="
	@./tests/test_util
	@echo "=== Runtime Tests ==="
	@./tests/test_runtime
	@echo "=== Panic Tests ==="
	@./tests/test_panics
	@echo "=== Stdlib Tests ==="
	@./tests/test_stdlib

test-e2e: build tests/test_codegen
	@echo ""
	@echo "=== End-to-End Codegen Tests ==="
	@./tests/test_codegen

test-integration: build
	@../scripts/run_tests.sh

# Sanitizer builds — catch undefined behavior (and memory bugs on Linux)
# Must compile ALL source files with sanitizer flags
TEST_SRC_FILES = src/util/arena.c src/util/buf.c src/util/error.c src/util/error_codes.c \
                 src/lexer/token.c src/lexer/lexer.c \
                 src/parser/ast.c src/parser/parser.c \
                 src/typechecker/types.c src/typechecker/scope.c src/typechecker/typechecker.c

# UBSan works everywhere; ASan needs Linux for reliable CI
UBSAN_FLAGS = -fsanitize=undefined -fno-omit-frame-pointer -g -O1
ASAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

test-ubsan:
	@echo ""
	@echo "=== UBSan Lexer Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_lexer_san tests/test_lexer.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_lexer_san
	@echo "=== UBSan Parser Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_parser_san tests/test_parser.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_parser_san
	@echo "=== UBSan Type Checker Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_typechecker_san tests/test_typechecker.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_typechecker_san
	@echo "=== UBSan Util Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_util_san tests/test_util.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_util_san
	@echo "=== UBSan Runtime Tests ==="
	@$(CC) $(UBSAN_FLAGS) $(INCLUDES) -o tests/test_runtime_san tests/test_runtime.c src/runtime/runtime.c src/runtime/array.c src/runtime/map.c $(LDFLAGS)
	@./tests/test_runtime_san
	@rm -f tests/test_lexer_san tests/test_parser_san tests/test_typechecker_san tests/test_util_san tests/test_runtime_san
	@echo ""
	@echo "All UBSan tests passed — no undefined behavior detected."

test-asan:
	@echo ""
	@echo "=== ASan+UBSan Tests (Linux recommended) ==="
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_lexer_san tests/test_lexer.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_lexer_san
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_parser_san tests/test_parser.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_parser_san
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_typechecker_san tests/test_typechecker.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_typechecker_san
	@echo "=== ASan+UBSan Util Tests ==="
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_util_san tests/test_util.c $(TEST_SRC_FILES) $(LDFLAGS)
	@./tests/test_util_san
	@echo "=== ASan+UBSan Runtime Tests ==="
	@$(CC) $(ASAN_FLAGS) $(INCLUDES) -o tests/test_runtime_san tests/test_runtime.c src/runtime/runtime.c src/runtime/array.c src/runtime/map.c $(LDFLAGS)
	@./tests/test_runtime_san
	@rm -f tests/test_lexer_san tests/test_parser_san tests/test_typechecker_san tests/test_util_san tests/test_runtime_san
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
