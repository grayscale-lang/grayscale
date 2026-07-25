# Grayscale Language Build System
.PHONY: build stubs install uninstall clean help leaks \
       test test-unit test-e2e test-integration test-go \
       test-ubsan test-asan

BINARY_NAME=gray
INSTALL_PATH=/usr/local/bin
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
	@$(MAKE) -C grayc test-unit
	@$(MAKE) -C grayc test-e2e
	@bash scripts/run_tests.sh
	@echo ""
	@echo "All test suites completed."

test-unit:
	@$(MAKE) -C grayc test-unit

test-e2e: build
	@$(MAKE) -C grayc test-e2e

test-integration: build
	@bash scripts/run_tests.sh

test-go: stubs
	@echo ""
	@echo "=== Go Unit Tests ==="
	$(GO) test -v -count=1 ./cli/... ./internal/driver/...

test-ubsan:
	@$(MAKE) -C grayc test-ubsan

test-asan:
	@$(MAKE) -C grayc test-asan

leaks:
	@$(MAKE) -C grayc leaks

# Create zero-length embed stubs. go:embed directives in
# internal/driver/embedded.go require these files to exist at `go build`
# time; the extractor detects empty stubs and falls back to the
# path search so dev builds still work. Both the runtime binaries
# themselves are gitignored — `make build` overwrites the stubs with
# real content before invoking `go build`.
stubs:
	@mkdir -p $(EMBED_DIR)/src/runtime $(EMBED_DIR)/src/stdlib
	@test -f $(EMBED_DIR)/grayc || : > $(EMBED_DIR)/grayc
	@test -f $(EMBED_DIR)/libgrayrt.a || : > $(EMBED_DIR)/libgrayrt.a
	@test -f $(EMBED_DIR)/src/runtime/.stub || : > $(EMBED_DIR)/src/runtime/.stub
	@test -f $(EMBED_DIR)/src/stdlib/.stub || : > $(EMBED_DIR)/src/stdlib/.stub

# Single-binary build: compile the C compiler first, stage the
# artifacts into internal/driver/runtime/ so go:embed picks them up, then
# build the Go CLI. The final `gray` binary contains `grayc` + `libgrayrt.a`
# as embedded assets and extracts them on first use to ~/.gray/runtime/.
build: stubs
	@echo "Building compiler..."
	@$(MAKE) -C grayc build
	@echo "Staging embedded runtime assets..."
	@cp grayc/grayc $(EMBED_DIR)/grayc
	@cp grayc/libgrayrt.a $(EMBED_DIR)/libgrayrt.a
	@cp grayc/src/runtime/*.h grayc/src/runtime/*.c $(EMBED_DIR)/src/runtime/
	@cp grayc/src/stdlib/*.h grayc/src/stdlib/*.c $(EMBED_DIR)/src/stdlib/
	@echo "Building gray CLI (with embedded runtime)..."
	$(GO) build $(LDFLAGS) -o $(BINARY_NAME) ./cli
	@echo ""
	@echo "Build complete: ./$(BINARY_NAME)"
	@echo "Run with: ./$(BINARY_NAME) <file.gray>"

install: build
	@echo "Installing Grayscale to $(INSTALL_PATH)..."
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
	@echo ""
	@echo '  ____                               _'
	@echo ' / ___|_ __ __ _ _   _ ___  ___ __ _| | ___'
	@echo '| |  _| '"'"'__/ _` | | | / __|/ __/ _` | |/ _ \'
	@echo '| |_| | | | (_| | |_| \__ \ (_| (_| | |  __/'
	@echo ' \____|_|  \__,_|\__, |___/\___\__,_|_|\___|'
	@echo '                 |___/'
	@echo 'Programming On Your Terms'
	@echo ""
	@echo "Grayscale installed successfully!"

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
	@$(MAKE) -C grayc clean
	@echo "Clean complete"
