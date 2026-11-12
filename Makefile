# Grayscale Language Build System
#
# Targets that need a compiler take its name first:
#   make grayc <target>      the C compiler (grayc/)
#   make selfhost <target>   the compiler written in Grayscale (selfhost/)
.DEFAULT_GOAL := help
.PHONY: grayc selfhost compiler-grayc compiler-selfhost \
       build hooks embed-tag stubs install uninstall clean help leaks benchmark \
       generate-errors generate-sized-type-tests check-error-codes check-stray-binaries \
       test test-unit test-e2e test-integration \
       test-ubsan test-asan

COMPILER_NAMES := grayc selfhost
COMPILER_TARGETS := build install test test-unit test-e2e test-integration test-ubsan test-asan \
                    leaks benchmark generate-errors generate-sized-type-tests check-error-codes \
                    check-stray-binaries
GRAYC_ONLY_TARGETS := test-unit test-e2e test-ubsan test-asan leaks

# Reject unknown or malformed targets before running anything.
KNOWN_TARGETS := hooks embed-tag stubs uninstall clean help $(COMPILER_TARGETS)
PICKED := $(filter $(COMPILER_NAMES),$(MAKECMDGOALS))
ACTION := $(filter-out $(COMPILER_NAMES),$(MAKECMDGOALS))
UNKNOWN_TARGETS := $(filter-out $(KNOWN_TARGETS),$(ACTION))
ifneq ($(UNKNOWN_TARGETS),)
  $(error Unknown target(s): $(UNKNOWN_TARGETS). Run 'make help' for valid targets)
endif
ifneq ($(filter-out 0 1,$(words $(PICKED))),)
  $(error pick one compiler: make grayc <target> or make selfhost <target>)
endif
ifneq ($(filter-out 0 1,$(words $(ACTION))),)
  $(error pass exactly one target after the compiler. Run 'make help' for valid targets)
endif
ifneq ($(PICKED),)
  ifeq ($(ACTION),)
    $(error 'make $(PICKED)' needs a target, e.g. 'make $(PICKED) build'. Run 'make help' for valid targets)
  endif
  ifeq ($(filter $(ACTION),$(COMPILER_TARGETS)),)
    $(error '$(ACTION)' does not take a compiler: run 'make $(ACTION)')
  endif
  ifeq ($(PICKED),selfhost)
    ifneq ($(filter $(ACTION),$(GRAYC_ONLY_TARGETS)),)
      $(error '$(ACTION)' tests the C compiler: run 'make grayc $(ACTION)')
    endif
  endif
else
  ifneq ($(filter $(ACTION),$(GRAYC_ONLY_TARGETS)),)
    $(error '$(ACTION)' tests the C compiler: run 'make grayc $(ACTION)')
  endif
  ifneq ($(filter $(ACTION),$(COMPILER_TARGETS)),)
    $(error '$(ACTION)' needs a compiler: run 'make grayc $(ACTION)' or 'make selfhost $(ACTION)')
  endif
endif

grayc selfhost:
	@:

SELFHOST_BUILD := selfhost/build
SELFHOST_DRIVER := selfhost/build/selfhost_grayc.bin

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

# Version info
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
BUILD_TIME=$(shell date -u '+%Y-%m-%d_%H:%M:%S')

EMBED_DIR=cli/runtime

# The compiler the picked target runs with.
ifeq ($(PICKED),selfhost)
  COMPILER := $(SELFHOST_DRIVER)
  COMPILER_PREREQ := compiler-selfhost
else
  COMPILER := ./grayc/grayc$(EXE)
  COMPILER_PREREQ := compiler-grayc
endif

# ===== Per-target help (make help TARGET=<name>) =====
# One HELP_<target> block per name in KNOWN_TARGETS. TARGET only selects a
# block when help is the goal; on any other target it is ignored.
define HELP_build
make build - Build the gray binary (compiler embedded)

  With grayc: builds the C compiler and the runtime. With selfhost: builds grayc, then the self-hosted compiler with it. Embeds the compiler and runtime into the gray CLI and writes
  ./gray (./gray.exe on Windows).

  Variables:
    CC=<compiler>   C compiler for the runtime library (for grayc too, with make grayc)
    VERSION=<tag>   Version string baked into the binary (default: git describe)

  Example: CC=clang VERSION=v0.12.0 make selfhost build
endef

define HELP_hooks
make hooks - Enable the repo git hooks

  Points git at .githooks, so every commit runs gray fmt --check on staged
  .gray files under cli/, scripts/ and benchmarks/. Run once per clone.
endef

define HELP_embed-tag
make embed-tag - Print the content hash that names the extracted runtime directory

  Hashes the staged grayc, libgrayrt.a and runtime sources under
  cli/runtime. Run make build first so the staged assets exist.

  Variables: none

  Example: make embed-tag
endef

define HELP_stubs
make stubs - Create empty embed stubs

  Creates the zero-length runtime files under cli/runtime, so the embedded
  assets exist before a full make build has staged the real ones.

  Variables: none

  Example: make stubs
endef

define HELP_install
make install - Install gray to the install path

  Runs make build, copies ./gray to the install path (using sudo when the
  path is not writable) and installs shell completion.

  Variables:
    CC=<compiler>        C compiler for the runtime library (for grayc too, with make grayc)
    VERSION=<tag>        Version string baked into the binary (default: git describe)
    INSTALL_PATH=<dir>   Install directory (default: /usr/local/bin, ~/.gray/bin on Windows)

  Example: INSTALL_PATH=$$HOME/bin make selfhost install
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
    CC=<compiler>   C compiler for the runtime library (for grayc too, with make grayc)

  Example: CC=clang make grayc leaks
endef

define HELP_benchmark
make benchmark - Run the real-workload benchmark suite (POSIX only)

  Runs make build, then the benchmark programs, prints a table and writes
  benchmarks/results.json. Does nothing on Windows.

  Variables:
    CC=<compiler>        C compiler for the runtime library (for grayc too, with make grayc)
    GRAY_CC=<compiler>   C compiler for the benchmark programs and stopwatch (read before CC)
    VERSION=<tag>        Version string baked into the binary (default: git describe)

  Example: CC=clang GRAY_CC=gcc make selfhost benchmark
endef

define HELP_test
make test - Run the full test suite (unit + e2e + integration)

  Runs make build, then the C unit, end-to-end, integration and UBSan
  suites, plus ASan on Linux.

  Variables:
    CC=<compiler>        C compiler for the runtime library (for grayc too, with make grayc)
    GRAY_CC=<compiler>   C compiler for the integration test programs (read before CC)
    VERSION=<tag>        Version string baked into the binary (default: git describe)

  Example: CC=clang make selfhost test
endef

define HELP_test-unit
make test-unit - Run C unit tests (lexer, parser, typechecker)

  Builds and runs the compiler unit tests in grayc/.

  Variables:
    CC=<compiler>   C compiler for the unit tests

  Example: CC=clang make grayc test-unit
endef

define HELP_test-e2e
make test-e2e - Run end-to-end codegen tests

  Runs make build, then compiles and runs the codegen test programs.

  Variables:
    CC=<compiler>   C compiler for the runtime library (for grayc too, with make grayc)
    VERSION=<tag>   Version string baked into the binary (default: git describe)

  Example: CC=clang make grayc test-e2e
endef

define HELP_test-integration
make test-integration - Run integration tests (pass + fail)

  Runs make build, then compiles every integration test program and checks
  its output or diagnostics.

  Variables:
    CC=<compiler>        C compiler for the runtime library (for grayc too, with make grayc)
    GRAY_CC=<compiler>   C compiler for the test programs (read before CC)
    VERSION=<tag>        Version string baked into the binary (default: git describe)

  Example: GRAY_CC=gcc make selfhost test-integration
endef

define HELP_test-ubsan
make test-ubsan - Run UBSan sanitizer tests

  Builds the compiler with UndefinedBehaviorSanitizer and runs the tests.

  Variables:
    CC=<compiler>   C compiler for the sanitizer build

  Example: CC=clang make grayc test-ubsan
endef

define HELP_test-asan
make test-asan - Run ASan+UBSan sanitizer tests (Linux recommended)

  Builds the compiler with AddressSanitizer and UBSan and runs the tests.
  Leak detection is off.

  Variables:
    CC=<compiler>   C compiler for the sanitizer build

  Example: CC=clang make grayc test-asan
endef

define HELP_generate-errors
make generate-errors - Regenerate ERRORS.md from the error registry

  Builds scripts/generate_errors.gray, runs it, and deletes the compiled binary.
  Needs ./grayc/grayc (run make build first).

  Example: make selfhost generate-errors
endef

define HELP_generate-sized-type-tests
make generate-sized-type-tests - Regenerate the generated sized-type integration tests

  Builds scripts/generate_sized_type_tests.gray, runs it, and deletes the compiled binary.
  Needs ./grayc/grayc (run make build first).

  Example: make selfhost generate-sized-type-tests
endef

define HELP_check-error-codes
make check-error-codes - Check that every emitted error code is registered and well placed

  Builds scripts/check_error_codes.gray, runs it, and deletes the compiled binary.
  Needs ./grayc/grayc (run make build first).

  Example: make selfhost check-error-codes
endef

define HELP_check-stray-binaries
make check-stray-binaries - Fail if compiled binaries are left in the repo root

  Builds scripts/check_stray_binaries.gray, runs it, and deletes the compiled binary.
  Needs ./grayc/grayc (run make build first).

  Example: make selfhost check-stray-binaries
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
	@echo "Usage: make [grayc|selfhost] <target>"
	@echo "  grayc    - the C compiler (grayc/)"
	@echo "  selfhost - the compiler written in Grayscale (selfhost/)"
	@echo ""
	@echo "Targets that take either compiler (make grayc <target> or make selfhost <target>):"
	@echo "  build                    - Build the gray binary (compiler embedded)"
	@echo "  install                  - Install gray to $(INSTALL_PATH)"
	@echo "  test                     - Run the full test suite (unit + e2e + integration)"
	@echo "  test-integration         - Run integration tests (pass + fail)"
	@echo "  benchmark                - Run the real-workload benchmark suite (POSIX only)"
	@echo "  generate-errors          - Regenerate ERRORS.md from the error registry"
	@echo "  generate-sized-type-tests - Regenerate the generated sized-type integration tests"
	@echo "  check-error-codes        - Check that every emitted error code is registered and well placed"
	@echo "  check-stray-binaries     - Fail if compiled binaries are left in the repo root"
	@echo ""
	@echo "Targets for the C compiler only (make grayc <target>):"
	@echo "  test-unit   - Run C unit tests (lexer, parser, typechecker)"
	@echo "  test-e2e    - Run end-to-end codegen tests"
	@echo "  test-ubsan  - Run UBSan sanitizer tests"
	@echo "  test-asan   - Run ASan+UBSan sanitizer tests (Linux recommended)"
	@echo "  leaks       - Check the compiler for memory leaks (macOS: leaks, Linux: valgrind)"
	@echo ""
	@echo "Targets with no compiler (make <target>):"
	@echo "  hooks      - Enable the repo git hooks (gray fmt check on commit)"
	@echo "  stubs      - Create empty embed stubs"
	@echo "  embed-tag  - Print the content hash that names the extracted runtime directory"
	@echo "  uninstall  - Remove gray from $(INSTALL_PATH)"
	@echo "  clean      - Remove built binaries"
	@echo "  help       - This list"
	@echo ""
	@echo "C compiler selection:"
	@echo "  CC=<compiler>      - Builds grayc and the runtime (build, install, test*, leaks, benchmark)"
	@echo "  GRAY_CC=<compiler> - Compiles .gray programs; read before CC (test-integration, benchmark, gray)"
	@echo "                       gray --cc <compiler> overrides both for one command"
	@echo ""
	@echo "  Example: CC=clang GRAY_CC=gcc make grayc benchmark"
	@echo "    clang builds grayc and the runtime; gcc builds the benchmark programs and stopwatch"
	@echo ""
	@echo "Per-target help:"
	@echo "  make help TARGET=<name> - What a target does, the variables it reads, and an example"
endif

# ===== Test targets =====
# Delegate compiler tests to grayc/Makefile.

ifneq ($(PICKED),selfhost)
test: build
	@echo ""
	@"$(MAKE)" -C grayc test-unit
	@"$(MAKE)" -C grayc test-e2e
ifdef WINDOWS
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1 -NoBuild
else
	@$(COMPILER) build scripts/run_tests.gray -o run_tests && { ./run_tests; status=$$?; rm -f run_tests; exit $$status; }
endif
	@"$(MAKE)" grayc test-ubsan
ifeq ($(shell uname -s),Linux)
	@"$(MAKE)" grayc test-asan
endif
	@echo ""
	@echo "All test suites completed."

endif

test-unit:
	@"$(MAKE)" -C grayc test-unit

test-e2e: build
	@"$(MAKE)" -C grayc test-e2e

ifneq ($(PICKED),selfhost)
ifdef WINDOWS
test-integration: build
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1 -NoBuild
else
test-integration: build
	@$(COMPILER) build scripts/run_tests.gray -o run_tests && { ./run_tests; status=$$?; rm -f run_tests; exit $$status; }
endif
endif

test-ubsan:
	@"$(MAKE)" -C grayc test-ubsan

# Leak detection is off, as in CI.
test-asan:
	@ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 "$(MAKE)" -C grayc test-asan

hooks:
	@git config core.hooksPath .githooks

leaks:
	@"$(MAKE)" -C grayc leaks

# Real-workload benchmark suite. Prints a table and writes
# benchmarks/results.json. POSIX-only; no-op on Windows.
benchmark: build
ifdef WINDOWS
	@echo "make benchmark is POSIX-only for now; skipping on Windows."
else
	@$(COMPILER) build benchmarks/run.gray -o run_benchmarks && { ./run_benchmarks; status=$$?; rm -f run_benchmarks; exit $$status; }
endif

# Create zero-length embed stubs, so the runtime directory the CLI embeds from
# exists before `make build` stages the real files. The extractor detects empty
# stubs and falls back to the path search, so dev builds still work. The
# staged runtime binaries are gitignored.
stubs:
	@for dir in $(EMBED_DIR); do \
	  mkdir -p $$dir/src/runtime $$dir/src/stdlib $$dir/src/util; \
	  for stub in grayc libgrayrt.a src/runtime/.stub src/stdlib/.stub src/util/.stub; do \
	    test -f $$dir/$$stub || : > $$dir/$$stub; \
	  done; \
	done

# Single-binary build: compile the C compiler first, stage the artifacts into
# cli/runtime/, then compile the gray CLI (cli/main.gray) with grayc. The
# final `gray` binary contains `grayc` + `libgrayrt.a` as embedded assets and
# extracts them on first use to ~/.gray/runtime/.
build: stubs $(COMPILER_PREREQ)
	@echo "Staging embedded runtime assets..."
	@cp $(COMPILER) $(EMBED_DIR)/grayc
	@cp grayc/libgrayrt.a $(EMBED_DIR)/libgrayrt.a
	@cp grayc/src/runtime/*.h grayc/src/runtime/*.c $(EMBED_DIR)/src/runtime/
	@cp grayc/src/stdlib/*.h grayc/src/stdlib/*.c $(EMBED_DIR)/src/stdlib/
	@# runtime.c and builtins.c include util/colors.h and util/constants.h;
	@# without these headers the compile-from-source fallback cannot build.
	@cp grayc/src/util/*.h $(EMBED_DIR)/src/util/
	@# embed() takes one file path, so a generated module lists every staged file.
	@mkdir -p $(SELFHOST_BUILD)
	@$(COMPILER) build scripts/generate_embedded_assets.gray -o $(SELFHOST_BUILD)/generate_embedded_assets.bin && $(SELFHOST_BUILD)/generate_embedded_assets.bin && rm -f $(SELFHOST_BUILD)/generate_embedded_assets.bin
	@echo "Building gray CLI (with embedded runtime)..."
	@# Content-hash the staged assets once here so the installed binary does
	@# not hash them on every invocation just to name its extract dir.
	@TAG=$$("$(MAKE)" -s --no-print-directory embed-tag) || exit 1; \
	$(COMPILER) build cli/main.gray -o $(BINARY_NAME) --define=VERSION=$(VERSION) --define=BUILD_TIME=$(BUILD_TIME) --define=EMBED_TAG=$$TAG
	@echo ""
	@echo "Build complete: ./$(BINARY_NAME)"
	@echo "Run with: ./$(BINARY_NAME) <file.gray>"

# The shipped compiler is the self-hosted one. grayc, built from source here, builds stage 1,
# stage 1 builds stage 2, and stage 2 is what gets embedded.

compiler-grayc:
	@echo "Building compiler..."
	@"$(MAKE)" -C grayc build

compiler-selfhost:
	@echo "Building grayc..."
	@"$(MAKE)" -C grayc build
	@mkdir -p $(SELFHOST_BUILD)
	@echo "Building compiler with grayc..."
	@./grayc/grayc build selfhost/driver/main.gray -o $(SELFHOST_BUILD)/selfhost_grayc_stage1.bin --quiet
	@$(SELFHOST_BUILD)/selfhost_grayc_stage1.bin build selfhost/driver/main.gray -o $(SELFHOST_DRIVER) -q all

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

# ===== Script targets =====
# Each builds a helper script, runs it, and removes the binary.

generate-errors: $(COMPILER_PREREQ)
	@$(COMPILER) build scripts/generate_errors.gray -o generate_errors && { ./generate_errors; status=$$?; rm -f generate_errors$(EXE); exit $$status; }

generate-sized-type-tests: $(COMPILER_PREREQ)
	@$(COMPILER) build scripts/generate_sized_type_tests.gray -o generate_sized_type_tests && { ./generate_sized_type_tests; status=$$?; rm -f generate_sized_type_tests$(EXE); exit $$status; }

check-error-codes: $(COMPILER_PREREQ)
	@$(COMPILER) build scripts/check_error_codes.gray -o check_error_codes && { ./check_error_codes; status=$$?; rm -f check_error_codes$(EXE); exit $$status; }

check-stray-binaries: $(COMPILER_PREREQ)
	@$(COMPILER) build scripts/check_stray_binaries.gray -o check_stray_binaries && { ./check_stray_binaries; status=$$?; rm -f check_stray_binaries$(EXE); exit $$status; }

# ===== Self-hosted compiler targets =====
# make selfhost test-integration (and make selfhost test) runs scripts/run_tests.gray against the self-hosted compiler.

ifeq ($(PICKED),selfhost)
test: test-integration
test-integration: compiler-selfhost
	@GRAY_COMPILER_PATH=$(abspath $(SELFHOST_DRIVER)) $(SELFHOST_DRIVER) build scripts/run_tests.gray -o $(SELFHOST_BUILD)/run_tests.bin -q all && { GRAY_COMPILER_PATH=$(abspath $(SELFHOST_DRIVER)) $(SELFHOST_BUILD)/run_tests.bin; status=$$?; rm -f $(SELFHOST_BUILD)/run_tests.bin; exit $$status; }
endif


clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(BINARY_NAME)
	@rm -rf dist/
	@rm -rf $(SELFHOST_BUILD) selfhost/build/selfhost_grayc.bin
	@# Embed assets are gitignored — delete entirely, not truncate
	@rm -f $(EMBED_DIR)/grayc $(EMBED_DIR)/libgrayrt.a
	@rm -rf $(EMBED_DIR)/src
	@rm -f cli/embedded_assets.gray
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
