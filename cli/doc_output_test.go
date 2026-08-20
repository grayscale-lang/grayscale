// doc_output_test.go — Tests for the documentation generator covering
// default output path fallback, custom --output paths, and automatic
// parent directory creation.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.
//
// Contributors:
//  - @SAY-5

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// writeGraySource writes a tiny .gray source file with one documented
// function so generateDocs has at least one entry to emit.
func writeGraySource(t *testing.T, dir string) string {
	t.Helper()
	src := `module main

#doc("greet returns a friendly hello.")
do greet() -> string {
    return "hello"
}
`
	path := filepath.Join(dir, "main.gray")
	if err := os.WriteFile(path, []byte(src), 0o644); err != nil {
		t.Fatalf("write source: %v", err)
	}
	return path
}

// runFromTempDir cd's into dir for the duration of fn, then restores
// the original cwd. generateDocs writes relative paths against cwd, so
// tests that exercise the default output path need to run inside a
// scratch directory to avoid polluting the contributor's checkout.
func runFromTempDir(t *testing.T, dir string, fn func()) {
	t.Helper()
	prev, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatalf("chdir to %s: %v", dir, err)
	}
	t.Cleanup(func() { _ = os.Chdir(prev) })
	fn()
}

func TestGenerateDocs_DefaultOutputPathUnchanged(t *testing.T) {
	dir := t.TempDir()
	src := writeGraySource(t, dir)

	runFromTempDir(t, dir, func() {
		// Empty outputPath must fall back to DOCS.md in cwd, matching
		// the pre-flag behavior.
		generateDocs([]string{src}, "")
	})

	got, err := os.ReadFile(filepath.Join(dir, "DOCS.md"))
	if err != nil {
		t.Fatalf("DOCS.md should be written by default: %v", err)
	}
	if !strings.Contains(string(got), "greet") {
		t.Errorf("DOCS.md missing the documented function, got:\n%s", got)
	}
}

func TestGenerateDocs_RespectsCustomOutputPath(t *testing.T) {
	dir := t.TempDir()
	src := writeGraySource(t, dir)
	custom := filepath.Join(dir, "api-reference.md")

	generateDocs([]string{src}, custom)

	got, err := os.ReadFile(custom)
	if err != nil {
		t.Fatalf("custom output path %s should be written: %v", custom, err)
	}
	if !strings.Contains(string(got), "greet") {
		t.Errorf("custom output missing the documented function, got:\n%s", got)
	}

	// And the default DOCS.md must NOT have been written when --output
	// was specified.
	if _, err := os.Stat(filepath.Join(dir, "DOCS.md")); !os.IsNotExist(err) {
		t.Errorf("DOCS.md should not exist when --output points elsewhere; stat err=%v", err)
	}
}

func TestGenerateDocs_CreatesParentDirectories(t *testing.T) {
	dir := t.TempDir()
	src := writeGraySource(t, dir)
	nested := filepath.Join(dir, "build", "docs", "API.md")

	generateDocs([]string{src}, nested)

	if _, err := os.Stat(nested); err != nil {
		t.Fatalf("nested output %s should be created: %v", nested, err)
	}
}
