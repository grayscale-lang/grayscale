#!/bin/bash
#
# Grayscale Integration Test Runner
#
# Copyright (c) 2025-Present Marshall A Burns
# Licensed under the MIT License. See LICENSE for details.
#
# This script runs all integration tests and reports results.
# - Tests in pass/ should execute successfully
# - Tests in fail/ should fail with expected errors
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$PROJECT_ROOT/integration-tests"
GRAY_BIN="$PROJECT_ROOT/gray"
GRAYC_BIN="$PROJECT_ROOT/grayc/grayc"

# Always rebuild to ensure we test current code
echo "Building grayc..."
(cd "$PROJECT_ROOT/grayc" && make build) || { echo "grayc build failed"; exit 1; }

echo "Building gray CLI..."
(cd "$PROJECT_ROOT" && go build -o gray ./cli) || { echo "gray build failed"; exit 1; }

# Point gray at the local grayc binary
export GRAY_COMPILER_PATH="$PROJECT_ROOT/grayc/grayc"

echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
TIMEOUT=30  # seconds per test — prevents infinite loops from hanging CI

# Portable timeout: prefer GNU timeout, fall back to perl one-liner
if command -v timeout >/dev/null 2>&1; then
    run_timeout() { timeout "$@"; }
elif command -v gtimeout >/dev/null 2>&1; then
    run_timeout() { gtimeout "$@"; }
else
    run_timeout() {
        local secs=$1; shift
        perl -e 'alarm shift; exec @ARGV' "$secs" "$@"
    }
fi

pass() { printf "  ${GREEN}PASS${NC}  %s\n" "$1"; ((++PASS_COUNT)); }
fail() { printf "  ${RED}FAIL${NC}  %s %s\n" "$1" "$2"; ((++FAIL_COUNT)); }

# run_fail_test <label> <entry.gray> <marker-file>
#
# A test may pin the exact diagnostic with marker comments in <marker-file>:
#
#     // expect-error: E5008
#     // expect-error-count: 1
#
# With expect-error the file runs through 'check' and must produce that code.
# Without it a test only has to fail somehow, which cannot tell a typechecker
# rejection apart from the C compiler choking on what the typechecker let
# through. expect-error-count additionally pins how many errors the file
# produces, so one mistake reported twice does not slip by.
# expect-not: <text> — the run's diagnostics must not contain <text>. The
# mangled-name guard in run_fail_test catches a module-mangled type name
# (mod_Type) on its shape alone; a mangled function name, or a warning code
# that must not appear, has no such shape and is named here instead.
forbidden_marker() {
    grep -E '^[[:space:]]*//[[:space:]]*expect-not:' "$1" \
        | sed -E 's|^[[:space:]]*//[[:space:]]*expect-not:[[:space:]]*||' | head -1
}

run_fail_test() {
    local label="$1" entry="$2" marker_file="$3"
    local expected_error expected_count output actual_count forbidden
    expected_error=$(grep -oE '^[[:space:]]*//[[:space:]]*expect-error:[[:space:]]*[EPW][0-9]+' "$marker_file" \
        | grep -oE '[EPW][0-9]+' | head -1)
    expected_count=$(grep -oE '^[[:space:]]*//[[:space:]]*expect-error-count:[[:space:]]*[0-9]+' "$marker_file" \
        | grep -oE '[0-9]+' | head -1)
    forbidden=$(forbidden_marker "$marker_file")
    if [ -n "$expected_error" ]; then
        output=$(run_timeout $TIMEOUT "$GRAY_BIN" check "$entry" 2>&1) || true
        actual_count=$(echo "$output" | grep -cE '^error\[' || true)
        if [ -n "$expected_count" ] && [ "$actual_count" != "$expected_count" ]; then
            fail "$label" "(expected $expected_count error(s), got $actual_count)"
        elif echo "$output" | grep -q "error\[$expected_error\]" \
            && ! echo "$output" | grep -qE "'[a-z][a-zA-Z0-9]*_[A-Z][a-zA-Z0-9]*'"; then
            if [ -n "$forbidden" ] && echo "$output" | grep -qF -- "$forbidden"; then
                fail "$label" "(output contains '$forbidden')"
            else
                pass "$label"
            fi
        else
            fail "$label" "(expected $expected_error)"
        fi
    elif run_timeout $TIMEOUT "$GRAY_BIN" "$entry" >/dev/null 2>&1; then
        fail "$label" "(expected error, got success)"
    else
        pass "$label"
    fi
}

echo "========================================"
echo "  Grayscale Integration Test Suite"
echo "========================================"
echo ""

# Run pass tests (should succeed)
printf "${BOLD}PASS tests:${NC}\n"

# Core tests
for test_file in "$TEST_DIR"/pass/core/*.gray; do
    if [ -f "$test_file" ]; then
        test_name=$(basename "$test_file" .gray)
        if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$test_file" 2>&1); then
            if echo "$output" | grep -q "SOME TESTS FAILED"; then
                fail "core/$test_name" "(assertions failed)"
            else
                pass "core/$test_name"
            fi
        else
            fail "core/$test_name" "(execution error)"
        fi
    fi
done

# Stdlib tests
for test_file in "$TEST_DIR"/pass/stdlib/*.gray; do
    if [ -f "$test_file" ]; then
        test_name=$(basename "$test_file" .gray)
        if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$test_file" 2>&1); then
            if echo "$output" | grep -q "SOME TESTS FAILED"; then
                fail "stdlib/$test_name" "(assertions failed)"
            else
                pass "stdlib/$test_name"
            fi
        else
            fail "stdlib/$test_name" "(execution error)"
        fi
    fi
done

# Multi-file tests
for dir in "$TEST_DIR"/pass/multi-file/*/; do
    if [ -d "$dir" ]; then
        dir_name=$(basename "$dir")
        main_file=$(find "$dir" -name "main.gray" | head -1)
        if [ -n "$main_file" ]; then
            forbidden=$(forbidden_marker "$main_file")
            if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$main_file" 2>&1); then
                if [ -n "$forbidden" ] && echo "$output" | grep -qF -- "$forbidden"; then
                    fail "multi-file/$dir_name" "(output contains '$forbidden')"
                else
                    pass "multi-file/$dir_name"
                fi
            else
                fail "multi-file/$dir_name" "(execution error)"
            fi
        fi
    fi
done

# new template tests — single files
if [ -d "$TEST_DIR/pass/new" ]; then
    echo ""
    printf "${BOLD}NEW template tests:${NC}\n"
    for test_file in "$TEST_DIR"/pass/new/*.gray; do
        if [ -f "$test_file" ]; then
            test_name=$(basename "$test_file" .gray)
            if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$test_file" 2>&1); then
                if echo "$output" | grep -q "SOME TESTS FAILED"; then
                    fail "new/$test_name" "(assertions failed)"
                else
                    pass "new/$test_name"
                fi
            else
                fail "new/$test_name" "(execution error)"
            fi
        fi
    done

    # new template tests — multi-file
    for dir in "$TEST_DIR"/pass/new/*/; do
        if [ -d "$dir" ]; then
            dir_name=$(basename "$dir")
            main_file=$(find "$dir" -name "main.gray" | head -1)
            if [ -n "$main_file" ]; then
                if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$main_file" 2>&1); then
                    if echo "$output" | grep -q "SOME TESTS FAILED"; then
                        fail "new/$dir_name" "(assertions failed)"
                    else
                        pass "new/$dir_name"
                    fi
                else
                    fail "new/$dir_name" "(execution error)"
                fi
            fi
        fi
    done
fi

# Stress tests — core
if [ -d "$TEST_DIR/pass/stress/core" ]; then
    echo ""
    printf "${BOLD}STRESS tests:${NC}\n"
    for test_file in "$TEST_DIR"/pass/stress/core/*.gray; do
        if [ -f "$test_file" ]; then
            test_name=$(basename "$test_file" .gray)
            if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$test_file" 2>&1); then
                if echo "$output" | grep -q "SOME TESTS FAILED"; then
                    fail "stress/core/$test_name" "(assertions failed)"
                else
                    pass "stress/core/$test_name"
                fi
            else
                fail "stress/core/$test_name" "(execution error)"
            fi
        fi
    done
fi

# Stress tests — stdlib
if [ -d "$TEST_DIR/pass/stress/stdlib" ]; then
    for test_file in "$TEST_DIR"/pass/stress/stdlib/*.gray; do
        if [ -f "$test_file" ]; then
            test_name=$(basename "$test_file" .gray)
            if output=$(run_timeout $TIMEOUT "$GRAY_BIN" "$test_file" 2>&1); then
                if echo "$output" | grep -q "SOME TESTS FAILED"; then
                    fail "stress/stdlib/$test_name" "(assertions failed)"
                else
                    pass "stress/stdlib/$test_name"
                fi
            else
                fail "stress/stdlib/$test_name" "(execution error)"
            fi
        fi
    done
fi

# Warning tests
if [ -d "$TEST_DIR/pass/warnings" ]; then
    echo ""
    printf "${BOLD}WARNING tests:${NC}\n"
    for test_file in "$TEST_DIR"/pass/warnings/*.gray; do
        if [ -f "$test_file" ]; then
            test_name=$(basename "$test_file" .gray)
            expected_warning=$(echo "$test_name" | grep -oE '^W[0-9]+')
            output=$(run_timeout $TIMEOUT "$GRAY_BIN" check "$test_file" 2>&1) || true
            if echo "$output" | grep -q "warning\[$expected_warning\]" \
                && echo "$output" | grep -qE -- '--> .+:[0-9]+:[0-9]+$'; then
                pass "warnings/$test_name"
            else
                fail "warnings/$test_name" "(expected $expected_warning)"
            fi
        fi
    done
fi

echo ""
printf "${BOLD}FAIL tests (expecting errors):${NC}\n"

# Error tests (should fail). See run_fail_test for the marker comments a test
# can use to pin the exact diagnostic and how many errors it produces.
for test_file in "$TEST_DIR"/fail/errors/*.gray; do
    if [ -f "$test_file" ]; then
        test_name=$(basename "$test_file" .gray)
        run_fail_test "errors/$test_name" "$test_file" "$test_file"
    fi
done

# Cleanup any .graydb files
rm -f ./*.graydb

# Multi-file error tests (single files)
for test_file in "$TEST_DIR"/fail/multi-file/*.gray; do
    if [ -f "$test_file" ]; then
        test_name=$(basename "$test_file" .gray)
        run_fail_test "multi-file/$test_name" "$test_file" "$test_file"
    fi
done

# Multi-file error tests (directories with main.gray)
for dir in "$TEST_DIR"/fail/multi-file/*/; do
    if [ -d "$dir" ]; then
        dir_name=$(basename "$dir")
        main_file=$(find "$dir" -name "main.gray" | head -1)
        if [ -n "$main_file" ]; then
            run_fail_test "multi-file/$dir_name" "$main_file" "$main_file"
        fi
    fi
done

# Summary
TOTAL=$((PASS_COUNT + FAIL_COUNT))
echo ""
echo "========================================"
echo "  Test Summary"
echo "========================================"
echo ""
printf "  ${GREEN}Passed:${NC}  $PASS_COUNT\n"
printf "  ${RED}Failed:${NC}  $FAIL_COUNT\n"
printf "  Total:   $TOTAL\n"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    printf "  ${BOLD}${GREEN}ALL TESTS PASSED!${NC}\n"
    exit 0
else
    printf "  ${BOLD}${RED}SOME TESTS FAILED${NC}\n"
    exit 1
fi
