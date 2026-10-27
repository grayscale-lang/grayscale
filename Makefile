# Grayscale Language Build System
.PHONY: build embed-tag stubs install uninstall clean help leaks benchmark \
       test test-unit test-e2e test-integration test-go \
       test-ubsan test-asan

# Reject unknown targets before running anything.
KNOWN_TARGETS := build embed-tag stubs install uninstall clean help leaks benchmark \
                 test test-unit test-e2e test-integration test-go \
                 test-ubsan test-asan
UNKNOWN_TARGETS := $(filter-out $(KNOWN_TARGETS),$(MAKECMDGOALS))
ifneq ($(UNKNOWN_TARGETS),)
  $(error Unknown target(s): $(UNKNOWN_TARGETS). Run 'make help' for valid targets)
endif
ifneq ($(filter-out 0 1,$(words $(MAKECMDGOALS))),)
  $(error pass exactly one target. Run 'make help' for valid targets)
endif

# Windows (MSYS2/Git Bash/MinGW make) needs an .exe suffix on every binary.
# GNU make sets OS=Windows_NT there; everywhere else EXE is empty and every
# rule below is byte-for-byte what it was.
ifeq ($(OS),Windows_NT)
  EXE = .exe
  WINDOWS = 1
endif

BINARY_NAME=gray$(EXE)
# Windows has no /usr/local/bin and no sudo; install per-user next to gray's
# own data directory (~/.gray) instead. Not added to PATH automatically —
# the install target prints instructions.
ifdef WINDOWS
  INSTALL_PATH=$(USERPROFILE)/.gray/bin
else
  INSTALL_PATH=/usr/local/bin
endif
GO=go

# Version info
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
BUILD_TIME=$(shell date -u '+%Y-%m-%d_%H:%M:%S')
MODULE=github.com/grayscale-lang/grayscale
LDFLAGS=-ldflags "-X main.Version=$(VERSION) -X main.BuildTime=$(BUILD_TIME)"

EMBED_DIR=internal/driver/runtime
CLI_EMBED_DIR=cli/runtime

# ===== Per-target help (make help TARGET=<name>) =====
# One HELP_<target> block per name in KNOWN_TARGETS. TARGET only selects a
# block when help is the goal; on any other target it is ignored.
define HELP_build
make build - Build the gray binary (compiler embedded)

  Builds grayc and the runtime, embeds them into the gray CLI, and writes
  ./gray (./gray.exe on Windows).

  Variables:
    CC=<compiler>   C compiler for grayc and the runtime
    VERSION=<tag>   Version string baked into the binary (default: git describe)

  Example: CC=clang VERSION=v0.12.0 make build
endef

define HELP_embed-tag
make embed-tag - Print the content hash that names the extracted runtime directory

  Hashes the staged grayc, libgrayrt.a and runtime sources under
  internal/driver/runtime. Run make build first so the staged assets exist.

  Variables: none

  Example: make embed-tag
endef

define HELP_stubs
make stubs - Create empty embed stubs (for dev go build)

  Creates the zero-length files go:embed needs under internal/driver/runtime
  so go build works without a full make build.

  Variables: none

  Example: make stubs
endef

define HELP_install
make install - Install gray to the install path

  Runs make build, copies ./gray to the install path (using sudo when the
  path is not writable) and installs shell completion.

  Variables:
    CC=<compiler>        C compiler for grayc and the runtime
    VERSION=<tag>        Version string baked into the binary (default: git describe)
    INSTALL_PATH=<dir>   Install directory (default: /usr/local/bin, ~/.gray/bin on Windows)

  Example: INSTALL_PATH=$$HOME/bin make install
endef

define HELP_uninstall
make uninstall - Remove gray from the install path

  Deletes gray, and any standalone grayc left by older install layouts,
  from the install path.

  Variables:
    INSTALL_PATH=<dir>   Install directory (default: /usr/local/bin, ~/.gray/bin on Windows)

  Example: make uninstall
endef

define HELP_clean
make clean - Remove built binaries

  Deletes ./gray, dist/, the staged embed assets and the grayc build output.

  Variables: none

  Example: make clean
endef

define HELP_help
make help - List the targets, or describe one

  With no TARGET, lists every target and the compiler variables. With
  TARGET=<name>, prints what that target does, the variables it reads and
  an example.

  Variables:
    TARGET=<name>   Target to describe

  Example: make help TARGET=build
endef

define HELP_leaks
make leaks - Check the compiler for memory leaks (macOS: leaks, Linux: valgrind)

  Builds grayc and runs it under the platform leak checker.

  Variables:
    CC=<compiler>   C compiler for grayc and the runtime

  Example: CC=clang make leaks
endef

define HELP_benchmark
make benchmark - Run the real-workload benchmark suite (POSIX only)

  Runs make build, then the benchmark programs, prints a table and writes
  benchmarks/results.json. Does nothing on Windows.

  Variables:
    CC=<compiler>        C compiler for grayc and the runtime
    GRAY_CC=<compiler>   C compiler for the benchmark programs and stopwatch (read before CC)
    VERSION=<tag>        Version string baked into the binary (default: git describe)

  Example: CC=clang GRAY_CC=gcc make benchmark
endef

define HELP_test
make test - Run the full test suite (unit + e2e + integration + Go)

  Runs make build, then the Go, C unit, end-to-end, integration and UBSan
  suites, plus ASan on Linux.

  Variables:
    CC=<compiler>        C compiler for grayc and the runtime
    GRAY_CC=<compiler>   C compiler for the integration test programs (read before CC)
    VERSION=<tag>        Version string baked into the binary (default: git describe)

  Example: CC=clang make test
endef

define HELP_test-unit
make test-unit - Run C unit tests (lexer, parser, typechecker)

  Builds and runs the compiler unit tests in grayc/.

  Variables:
    CC=<compiler>   C compiler for the unit tests

  Example: CC=clang make test-unit
endef

define HELP_test-e2e
make test-e2e - Run end-to-end codegen tests

  Runs make build, then compiles and runs the codegen test programs.

  Variables:
    CC=<compiler>   C compiler for grayc and the runtime
    VERSION=<tag>   Version string baked into the binary (default: git describe)

  Example: CC=clang make test-e2e
endef

define HELP_test-integration
make test-integration - Run integration tests (pass + fail)

  Runs make build, then compiles every integration test program and checks
  its output or diagnostics.

  Variables:
    CC=<compiler>        C compiler for grayc and the runtime
    GRAY_CC=<compiler>   C compiler for the test programs (read before CC)
    VERSION=<tag>        Version string baked into the binary (default: git describe)

  Example: GRAY_CC=gcc make test-integration
endef

define HELP_test-go
make test-go - Run Go unit tests

  Creates the embed stubs, then runs go test over cli/ and internal/driver/.

  Variables: none

  Example: make test-go
endef

define HELP_test-ubsan
make test-ubsan - Run UBSan sanitizer tests

  Builds the compiler with UndefinedBehaviorSanitizer and runs the tests.

  Variables:
    CC=<compiler>   C compiler for the sanitizer build

  Example: CC=clang make test-ubsan
endef

define HELP_test-asan
make test-asan - Run ASan+UBSan sanitizer tests (Linux recommended)

  Builds the compiler with AddressSanitizer and UBSan and runs the tests.
  Leak detection is off.

  Variables:
    CC=<compiler>   C compiler for the sanitizer build

  Example: CC=clang make test-asan
endef

# Exported so the recipe prints the block verbatim, newlines intact.
export HELP_BLOCK = $(HELP_$(TARGET))

# TARGET only applies to help (the goal, or the default goal when none given).
ifneq ($(filter-out help,$(MAKECMDGOALS)),)
  override TARGET :=
endif
ifneq ($(TARGET),)
  ifeq ($(filter $(TARGET),$(KNOWN_TARGETS)),)
    $(error Unknown target '$(TARGET)'. Run 'make help' for valid targets)
  endif
endif

help:
ifneq ($(TARGET),)
	@printf '%s\n' "$$HELP_BLOCK"
else
	@echo "Grayscale Language Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make build     - Build the gray binary (compiler embedded)"
	@echo "  make stubs     - Create empty embed stubs (for dev go build)"
	@echo "  make install   - Install gray to $(INSTALL_PATH)"
	@echo "  make uninstall - Remove gray from $(INSTALL_PATH)"
	@echo "  make clean     - Remove built binaries"
	@echo "  make leaks     - Check compiler for memory leaks (macOS: leaks, Linux: valgrind)"
	@echo "  make benchmark - Run the real-workload benchmark suite (POSIX only)"
	@echo ""
	@echo "Test targets:"
	@echo "  make test             - Run the full test suite (unit + e2e + integration + Go)"
	@echo "  make test-unit        - Run C unit tests (lexer, parser, typechecker)"
	@echo "  make test-e2e         - Run end-to-end codegen tests"
	@echo "  make test-integration - Run integration tests (pass + fail)"
	@echo "  make test-go          - Run Go unit tests"
	@echo "  make test-ubsan       - Run UBSan sanitizer tests"
	@echo "  make test-asan        - Run ASan+UBSan sanitizer tests (Linux recommended)"
	@echo ""
	@echo "C compiler selection:"
	@echo "  CC=<compiler>      - Builds grayc and the runtime (build, install, test-*, leaks, benchmark)"
	@echo "  GRAY_CC=<compiler> - Compiles .gray programs; read before CC (test-integration, benchmark, gray)"
	@echo "                       gray --cc <compiler> overrides both for one command"
	@echo ""
	@echo "  Example: CC=clang GRAY_CC=gcc make benchmark"
	@echo "    clang builds grayc and the runtime; gcc builds the benchmark programs and stopwatch"
	@echo ""
	@echo "Per-target help:"
	@echo "  make help TARGET=<name> - What a target does, the variables it reads, and an example"
endif

# ===== Test targets =====
# Delegate compiler tests to grayc/Makefile; Go tests run from the root.

test: build
	@echo ""
	@echo "=== Go Unit Tests ==="
	$(GO) test -v -count=1 ./cli/... ./internal/driver/...
	@echo ""
	@"$(MAKE)" -C grayc test-unit
	@"$(MAKE)" -C grayc test-e2e
ifdef WINDOWS
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1 -NoBuild
else
	@./grayc/grayc scripts/run_tests.gray -o run_tests && { ./run_tests; status=$$?; rm -f run_tests; exit $$status; }
endif
	@"$(MAKE)" test-ubsan
ifeq ($(shell uname -s),Linux)
	@"$(MAKE)" test-asan
endif
	@echo ""
	@echo "All test suites completed."

test-unit:
	@"$(MAKE)" -C grayc test-unit

test-e2e: build
	@"$(MAKE)" -C grayc test-e2e

ifdef WINDOWS
test-integration: build
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1 -NoBuild
else
test-integration: build
	@./grayc/grayc scripts/run_tests.gray -o run_tests && { ./run_tests; status=$$?; rm -f run_tests; exit $$status; }
endif

test-go: stubs
	@echo ""
	@echo "=== Go Unit Tests ==="
	$(GO) test -v -count=1 ./cli/... ./internal/driver/...

test-ubsan:
	@"$(MAKE)" -C grayc test-ubsan

# Leak detection is off, as in CI.
test-asan:
	@ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 "$(MAKE)" -C grayc test-asan

leaks:
	@"$(MAKE)" -C grayc leaks

# Real-workload benchmark suite. Prints a table and writes
# benchmarks/results.json. POSIX-only; no-op on Windows.
benchmark: build
ifdef WINDOWS
	@echo "make benchmark is POSIX-only for now; skipping on Windows."
else
	@./grayc/grayc benchmarks/run.gray -o run_benchmarks && { ./run_benchmarks; status=$$?; rm -f run_benchmarks; exit $$status; }
endif

# Create zero-length embed stubs. go:embed directives in
# internal/driver/embedded.go require these files to exist at `go build`
# time; the extractor detects empty stubs and falls back to the
# path search so dev builds still work. Both the runtime binaries
# themselves are gitignored — `make build` overwrites the stubs with
# real content before invoking `go build`.
stubs:
	@for dir in $(EMBED_DIR) $(CLI_EMBED_DIR); do \
	  mkdir -p $$dir/src/runtime $$dir/src/stdlib $$dir/src/util; \
	  for stub in grayc libgrayrt.a src/runtime/.stub src/stdlib/.stub src/util/.stub; do \
	    test -f $$dir/$$stub || : > $$dir/$$stub; \
	  done; \
	done

# Single-binary build: compile the C compiler first, stage the
# artifacts into internal/driver/runtime/ so go:embed picks them up, then
# build the Go CLI. The final `gray` binary contains `grayc` + `libgrayrt.a`
# as embedded assets and extracts them on first use to ~/.gray/runtime/.
build: stubs
	@echo "Building compiler..."
	@"$(MAKE)" -C grayc build
	@echo "Staging embedded runtime assets..."
	@# go:embed reads the literal path runtime/grayc on every platform; the
	@# .exe rename happens on extraction (internal/driver/embedded.go).
	@cp grayc/grayc$(EXE) $(EMBED_DIR)/grayc
	@cp grayc/libgrayrt.a $(EMBED_DIR)/libgrayrt.a
	@cp grayc/src/runtime/*.h grayc/src/runtime/*.c $(EMBED_DIR)/src/runtime/
	@cp grayc/src/stdlib/*.h grayc/src/stdlib/*.c $(EMBED_DIR)/src/stdlib/
	@# runtime.c and builtins.c include util/colors.h and util/constants.h;
	@# without these headers the compile-from-source fallback cannot build.
	@cp grayc/src/util/*.h $(EMBED_DIR)/src/util/
	@# Grayscale source in cli/ can only embed() paths under cli/, so it gets
	@# the same assets; embed-tag keeps hashing $(EMBED_DIR).
	@cp -R $(EMBED_DIR)/. $(CLI_EMBED_DIR)/
	@echo "Building gray CLI (with embedded runtime)..."
	@# Content-hash the staged assets once here so the installed wrapper does
	@# not hash them on every invocation just to name its extract dir.
	@TAG=$$("$(MAKE)" -s embed-tag) || exit 1; \
	$(GO) build -ldflags "-X main.Version=$(VERSION) -X main.BuildTime=$(BUILD_TIME) -X $(MODULE)/internal/driver.embedTag=$$TAG" -o $(BINARY_NAME) ./cli
	@echo ""
	@echo "Build complete: ./$(BINARY_NAME)"
	@echo "Run with: ./$(BINARY_NAME) <file.gray>"

install: build
	@echo "Installing Grayscale to $(INSTALL_PATH)..."
ifdef WINDOWS
	@mkdir -p "$(INSTALL_PATH)"
	@cp $(BINARY_NAME) "$(INSTALL_PATH)/$(BINARY_NAME)"
else
	@if [ -w $(INSTALL_PATH) ]; then \
		mkdir -p $(INSTALL_PATH); \
		cp $(BINARY_NAME) $(INSTALL_PATH)/$(BINARY_NAME); \
		chmod +x $(INSTALL_PATH)/$(BINARY_NAME); \
	else \
		echo "Need sudo permissions to install to $(INSTALL_PATH)"; \
		sudo mkdir -p $(INSTALL_PATH); \
		sudo cp $(BINARY_NAME) $(INSTALL_PATH)/$(BINARY_NAME); \
		sudo chmod +x $(INSTALL_PATH)/$(BINARY_NAME); \
	fi
endif
	@"$(INSTALL_PATH)/$(BINARY_NAME)" completion install || true
	@echo ""
	@echo '  ____                               _'
	@echo ' / ___|_ __ __ _ _   _ ___  ___ __ _| | ___'
	@echo '| |  _| '"'"'__/ _` | | | / __|/ __/ _` | |/ _ \'
	@echo '| |_| | | | (_| | |_| \__ \ (_| (_| | |  __/'
	@echo ' \____|_|  \__,_|\__, |___/\___\__,_|_|\___|'
	@echo '                 |___/'
	@echo 'Simple to write. Safe to run.'
	@echo ""
	@echo "Grayscale installed successfully!"
ifdef WINDOWS
	@echo ""
	@echo "If 'gray' is not found in new shells, add %USERPROFILE%\.gray\bin to your"
	@echo "user PATH (Settings > Environment Variables). For Git Bash instead:"
	@echo "  echo 'export PATH=\"\$$HOME/.gray/bin:\$$PATH\"' >> ~/.bashrc"
endif

uninstall:
	@echo "Uninstalling Grayscale..."
	@rm -f $(INSTALL_PATH)/$(BINARY_NAME)
	@# Remove standalone grayc from prior install layouts
	@rm -f $(INSTALL_PATH)/grayc
	@echo "Grayscale uninstalled"

clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(BINARY_NAME)
	@rm -rf dist/
	@# Embed assets are gitignored — delete entirely, not truncate
	@rm -f $(EMBED_DIR)/grayc $(EMBED_DIR)/libgrayrt.a
	@rm -rf $(EMBED_DIR)/src
	@"$(MAKE)" -C grayc clean
	@echo "Clean complete"

# Prints the 16-hex tag that names the extracted runtime directory: sha256 of
# grayc, libgrayrt.a, the sorted relative file list of $(EMBED_DIR)/src, and
# those files' contents. LC_ALL=C keeps the order identical across platforms.
# The release workflow runs this too, so there is one definition of the tag.
embed-tag:
	@cd $(EMBED_DIR)/src && test -n "$$(find . -type f | LC_ALL=C sort)" || \
	  { echo "embed-tag: no source files found under $(EMBED_DIR)/src (is find/sort the POSIX one?)" >&2; exit 1; }
	@{ cat $(EMBED_DIR)/grayc $(EMBED_DIR)/libgrayrt.a; \
	   cd $(EMBED_DIR)/src && files=$$(find . -type f | LC_ALL=C sort) && \
	   printf '%s\n' "$$files" && for file in $$files; do cat "$$file"; done; \
	} | { sha256sum 2>/dev/null || shasum -a 256; } | cut -c1-16
