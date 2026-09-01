// e2e_test.go — End-to-end tests that invoke the compiled gray binary
// and verify output/exit codes. TestMain builds the binary once into a
// temp directory; individual tests call runGray() to exercise commands.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// grayBin holds the path to the compiled gray binary built by TestMain.
var grayBin string

func TestMain(m *testing.M) {
	dir, err := os.MkdirTemp("", "gray-e2e-*")
	if err != nil {
		panic("e2e: cannot create temp dir: " + err.Error())
	}
	defer os.RemoveAll(dir)

	// The .exe suffix is required even for an absolute path: os/exec resolves
	// executables through PATHEXT on Windows and will not run an
	// extensionless file.
	name := "gray"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	bin := filepath.Join(dir, name)
	build := exec.Command("go", "build", "-o", bin, "./cli")
	build.Dir = findRepoRoot()
	build.Stderr = os.Stderr
	if err := build.Run(); err != nil {
		panic("e2e: go build failed: " + err.Error())
	}
	grayBin = bin

	os.Exit(m.Run())
}

// findRepoRoot walks up from the working directory to find go.mod.
func findRepoRoot() string {
	dir, _ := os.Getwd()
	for {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			panic("e2e: cannot find repo root (go.mod)")
		}
		dir = parent
	}
}

// runGray executes the gray binary with the given args and returns
// stdout, stderr, and the exit code.
func runGray(t *testing.T, args ...string) (stdout, stderr string, exitCode int) {
	t.Helper()
	var outBuf, errBuf bytes.Buffer
	cmd := exec.Command(grayBin, args...)
	cmd.Stdout = &outBuf
	cmd.Stderr = &errBuf
	err := cmd.Run()
	exitCode = 0
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			exitCode = exitErr.ExitCode()
		} else {
			t.Fatalf("exec error: %v", err)
		}
	}
	return outBuf.String(), errBuf.String(), exitCode
}

// combinedOutput returns stdout+stderr joined, for commands whose
// output destination may vary.
func combinedOutput(stdout, stderr string) string {
	return stdout + stderr
}

// ---------------------------------------------------------------------------
// gray version
// ---------------------------------------------------------------------------

func TestE2E_Version(t *testing.T) {
	stdout, stderr, code := runGray(t, "version")
	if code != 0 {
		t.Fatalf("gray version exited %d; stderr: %s", code, stderr)
	}
	out := combinedOutput(stdout, stderr)
	if !strings.Contains(out, "Installed:") {
		t.Errorf("expected 'Installed:' in output, got:\n%s", out)
	}
}

// ---------------------------------------------------------------------------
// gray report
// ---------------------------------------------------------------------------

func TestE2E_Report(t *testing.T) {
	stdout, stderr, code := runGray(t, "report")
	if code != 0 {
		t.Fatalf("gray report exited %d", code)
	}
	out := combinedOutput(stdout, stderr)
	if !strings.Contains(out, "Grayscale Bug Report Info") {
		t.Errorf("expected 'Grayscale Bug Report Info' in output, got:\n%s", out)
	}
}

// ---------------------------------------------------------------------------
// gray check
// ---------------------------------------------------------------------------

func TestE2E_Check_Valid(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "valid.gray")
	os.WriteFile(src, []byte("do main() {\n    println(\"hello\")\n}\n"), 0644)

	_, stderr, code := runGray(t, "check", src)
	if code != 0 {
		t.Fatalf("gray check exited %d on valid file", code)
	}
	if !strings.Contains(stderr, "no errors!") {
		t.Fatalf("gray check success message should end in %q, got: %q", "no errors!", stderr)
	}
}

func TestE2E_Check_Invalid(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "bad.gray")
	os.WriteFile(src, []byte("this is not valid grayscale code }{{\n"), 0644)

	_, _, code := runGray(t, "check", src)
	if code == 0 {
		t.Fatal("gray check should exit non-zero on invalid code")
	}
}

// ---------------------------------------------------------------------------
// gray fmt
// ---------------------------------------------------------------------------

func TestE2E_Fmt(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "messy.gray")
	// Tab indentation that the formatter should convert to spaces.
	os.WriteFile(src, []byte("do main() {\n\tprintln(\"hi\")\n}\n"), 0644)

	_, stderr, code := runGray(t, "fmt", src)
	if code != 0 {
		t.Fatalf("gray fmt exited %d; stderr: %s", code, stderr)
	}

	after, _ := os.ReadFile(src)
	if strings.Contains(string(after), "\t") {
		t.Error("tabs should be converted to spaces after fmt")
	}
}

func TestE2E_Fmt_CheckMode(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "messy.gray")
	original := "do main() {\n\tprintln(\"hi\")\n}\n"
	os.WriteFile(src, []byte(original), 0644)

	_, _, code := runGray(t, "fmt", "--check", src)
	if code == 0 {
		t.Fatal("gray fmt --check should exit non-zero on unformatted code")
	}

	// Verify the file was NOT modified.
	after, _ := os.ReadFile(src)
	if string(after) != original {
		t.Error("gray fmt --check should not modify the file")
	}
}

// ---------------------------------------------------------------------------
// gray doc
// ---------------------------------------------------------------------------

func TestE2E_Doc(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "lib.gray")
	os.WriteFile(src, []byte(`#doc("Adds two numbers")
do add(a int, b int) -> int {
    return a + b
}
`), 0644)

	out := filepath.Join(dir, "DOCS.md")
	_, _, code := runGray(t, "doc", "-o", out, src)
	if code != 0 {
		t.Fatalf("gray doc exited %d", code)
	}

	content, err := os.ReadFile(out)
	if err != nil {
		t.Fatalf("DOCS.md not created: %v", err)
	}
	if !strings.Contains(string(content), "add") {
		t.Errorf("DOCS.md missing 'add' entry:\n%s", content)
	}
}

func TestE2E_Doc_NoDocAttributes(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "bare.gray")
	os.WriteFile(src, []byte("do main() {\n    println(\"hi\")\n}\n"), 0644)

	stdout, stderr, code := runGray(t, "doc", src)
	if code != 0 {
		t.Fatalf("gray doc exited %d on file with no #doc", code)
	}
	out := combinedOutput(stdout, stderr)
	if !strings.Contains(out, "No documented items found") {
		t.Errorf("expected 'No documented items found' message, got:\n%s", out)
	}
}

// ---------------------------------------------------------------------------
// --help for all subcommands
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// gray test
// ---------------------------------------------------------------------------

func TestE2E_Test_PassAndFail(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "demo.gray")
	os.WriteFile(src, []byte(
		"do add(a int, b int) -> int { return a + b }\n\n"+
			"#test\ndo test_pass() { assert(add(2, 3) == 5) }\n\n"+
			"#test\ndo test_fail() { assert(add(2, 2) == 5) }\n"), 0644)

	stdout, stderr, code := runGray(t, "test", "--no-color", src)
	out := combinedOutput(stdout, stderr)
	if strings.Contains(out, "no C compiler") {
		t.Skip("no C compiler available")
	}
	if code == 0 {
		t.Fatalf("gray test should exit non-zero when a test fails; output:\n%s", out)
	}
	for _, want := range []string{"test_pass", "test_fail", "FAIL", "1 passed", "1 failed", "2 total"} {
		if !strings.Contains(out, want) {
			t.Errorf("gray test output missing %q:\n%s", want, out)
		}
	}
}

func TestE2E_Test_StrippedFromNormalBuild(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "app.gray")
	os.WriteFile(src, []byte(
		"#test\ndo test_never_runs() { assert(false) }\n\n"+
			"do main() { println(\"app ran\") }\n"), 0644)

	stdout, stderr, code := runGray(t, src)
	out := combinedOutput(stdout, stderr)
	if strings.Contains(out, "no C compiler") {
		t.Skip("no C compiler available")
	}
	if code != 0 {
		t.Fatalf("gray <file> with a #test fn exited %d; output:\n%s", code, out)
	}
	if !strings.Contains(out, "app ran") {
		t.Errorf("expected main() to run, got:\n%s", out)
	}
	if strings.Contains(out, "assert") || strings.Contains(out, "test_never_runs") {
		t.Errorf("#test function leaked into a normal build:\n%s", out)
	}
}

func TestE2E_Test_NoTests(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "plain.gray"),
		[]byte("do main() { println(\"hi\") }\n"), 0644)

	stdout, stderr, code := runGray(t, "test", dir)
	out := combinedOutput(stdout, stderr)
	if code != 0 {
		t.Fatalf("gray test with no #test functions should exit 0, got %d:\n%s", code, out)
	}
	if !strings.Contains(out, "No #test functions found") {
		t.Errorf("expected 'No #test functions found', got:\n%s", out)
	}
}

func TestE2E_Help(t *testing.T) {
	// Root help
	t.Run("root", func(t *testing.T) {
		stdout, stderr, code := runGray(t, "--help")
		if code != 0 {
			t.Fatalf("gray --help exited %d", code)
		}
		out := combinedOutput(stdout, stderr)
		if !strings.Contains(out, "Usage") {
			t.Errorf("expected 'Usage' in --help output, got:\n%s", out)
		}
	})

	cmds := []string{
		"build", "check", "doc", "fmt", "man",
		"new", "report", "update", "verify", "version",
		"cross", "install", "watch", "test",
	}
	for _, cmd := range cmds {
		t.Run(cmd, func(t *testing.T) {
			stdout, stderr, code := runGray(t, cmd, "--help")
			if code != 0 {
				t.Fatalf("gray %s --help exited %d", cmd, code)
			}
			out := combinedOutput(stdout, stderr)
			if !strings.Contains(out, "Usage") && !strings.Contains(out, "usage") {
				t.Errorf("gray %s --help missing usage text:\n%s", cmd, out)
			}
		})
	}
}
