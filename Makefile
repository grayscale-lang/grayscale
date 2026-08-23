# Grayscale Language Build System
.PHONY: build stubs install uninstall clean help leaks \
       test test-unit test-e2e test-integration test-go \
       test-ubsan test-asan

# Reject unknown targets before running anything.
KNOWN_TARGETS := build stubs install uninstall clean help leaks \
                 test test-unit test-e2e test-integration test-go \
                 test-ubsan test-asan
UNKNOWN_TARGETS := $(filter-out $(KNOWN_TARGETS),$(MAKECMDGOALS))
ifneq ($(UNKNOWN_TARGETS),)
  $(error Unknown target(s): $(UNKNOWN_TARGETS). Run 'make help' for valid targets)
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
LDFLAGS=-ldflags "-X main.Version=$(VERSION) -X main.BuildTime=$(BUILD_TIME)"

EMBED_DIR=internal/driver/runtime

help:
	@echo "Grayscale Language Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make build     - Build the gray binary (compiler embedded)"
	@echo "  make stubs     - Create empty embed stubs (for dev go build)"
	@echo "  make install   - Install gray to $(INSTALL_PATH)"
	@echo "  make uninstall - Remove gray from $(INSTALL_PATH)"
	@echo "  make clean     - Remove built binaries"
	@echo "  make leaks     - Check compiler for memory leaks (macOS: leaks, Linux: valgrind)"
	@echo ""
	@echo "Test targets:"
	@echo "  make test             - Run the full test suite (unit + e2e + integration + Go)"
	@echo "  make test-unit        - Run C unit tests (lexer, parser, typechecker)"
	@echo "  make test-e2e         - Run end-to-end codegen tests"
	@echo "  make test-integration - Run integration tests (pass + fail)"
	@echo "  make test-go          - Run Go unit tests"
	@echo "  make test-ubsan       - Run UBSan sanitizer tests"
	@echo "  make test-asan        - Run ASan+UBSan sanitizer tests (Linux recommended)"

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
	@bash scripts/run_tests.sh
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
	@bash scripts/run_tests.sh
endif

test-go: stubs
	@echo ""
	@echo "=== Go Unit Tests ==="
	$(GO) test -v -count=1 ./cli/... ./internal/driver/...

test-ubsan:
	@"$(MAKE)" -C grayc test-ubsan

test-asan:
	@"$(MAKE)" -C grayc test-asan

leaks:
	@"$(MAKE)" -C grayc leaks

# Create zero-length embed stubs. go:embed directives in
# internal/driver/embedded.go require these files to exist at `go build`
# time; the extractor detects empty stubs and falls back to the
# path search so dev builds still work. Both the runtime binaries
# themselves are gitignored — `make build` overwrites the stubs with
# real content before invoking `go build`.
stubs:
	@mkdir -p $(EMBED_DIR)/src/runtime $(EMBED_DIR)/src/stdlib $(EMBED_DIR)/src/util
	@test -f $(EMBED_DIR)/grayc || : > $(EMBED_DIR)/grayc
	@test -f $(EMBED_DIR)/libgrayrt.a || : > $(EMBED_DIR)/libgrayrt.a
	@test -f $(EMBED_DIR)/src/runtime/.stub || : > $(EMBED_DIR)/src/runtime/.stub
	@test -f $(EMBED_DIR)/src/stdlib/.stub || : > $(EMBED_DIR)/src/stdlib/.stub
	@test -f $(EMBED_DIR)/src/util/.stub || : > $(EMBED_DIR)/src/util/.stub

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
	@echo "Building gray CLI (with embedded runtime)..."
	$(GO) build $(LDFLAGS) -o $(BINARY_NAME) ./cli
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
