// test_test.go — Tests for the `gray test` runner's protocol-line parsing.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func TestParseFailLine(t *testing.T) {
	cases := []struct {
		in       string
		wantName string
		wantFile string
		wantLine int
		wantMsg  string
	}{
		{
			"TestAdd\tmain.gray:12\tassertion failed",
			"TestAdd", "main.gray", 12, "assertion failed",
		},
		{
			"TestNoMsg\tmain.gray:5\t",
			"TestNoMsg", "main.gray", 5, "test failed",
		},
		{
			"TestNoLocation",
			"TestNoLocation", "", 0, "test failed",
		},
	}
	for _, c := range cases {
		t.Run(c.wantName, func(t *testing.T) {
			r := parseFailLine(c.in)
			if r.name != c.wantName {
				t.Errorf("name = %q, want %q", r.name, c.wantName)
			}
			if r.file != c.wantFile {
				t.Errorf("file = %q, want %q", r.file, c.wantFile)
			}
			if r.line != c.wantLine {
				t.Errorf("line = %d, want %d", r.line, c.wantLine)
			}
			if r.message != c.wantMsg {
				t.Errorf("message = %q, want %q", r.message, c.wantMsg)
			}
		})
	}
}

func TestWaitErrString(t *testing.T) {
	if got := waitErrString(nil); got != "no result trailer" {
		t.Errorf("waitErrString(nil) = %q, want %q", got, "no result trailer")
	}
	wrapped := errors.New("boom")
	if got := waitErrString(wrapped); got != "boom" {
		t.Errorf("waitErrString(err) = %q, want %q", got, "boom")
	}
}

func TestTestExeSuffix(t *testing.T) {
	want := ""
	if runtime.GOOS == "windows" {
		want = ".exe"
	}
	if got := testExeSuffix(); got != want {
		t.Errorf("testExeSuffix() = %q, want %q", got, want)
	}
}

func TestFileHasTest(t *testing.T) {
	cases := []struct {
		name    string
		content string
		want    bool
	}{
		{"bare attribute", "#test\ndo check_add() {\n}\n", true},
		{"bracket attribute", "#[test]\ndo check_add() {\n}\n", true},
		{"bracket attribute with others", "#[doc, test]\ndo check_add() {\n}\n", true},
		{"no attribute", "do main() {\n}\n", false},
		{"unrelated attribute", "#[doc]\ndo main() {\n}\n", false},
	}
	dir := t.TempDir()
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			path := filepath.Join(dir, c.name+".gray")
			if err := os.WriteFile(path, []byte(c.content), 0o644); err != nil {
				t.Fatalf("write test file: %v", err)
			}
			if got := fileHasTest(path); got != c.want {
				t.Errorf("fileHasTest(%q) = %v, want %v", c.content, got, c.want)
			}
		})
	}
	if fileHasTest(filepath.Join(dir, "missing.gray")) {
		t.Error("fileHasTest on a nonexistent file should return false")
	}
}

func TestSourceLine(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "main.gray")
	if err := os.WriteFile(path, []byte("line one\n  line two  \nline three\n"), 0o644); err != nil {
		t.Fatalf("write test file: %v", err)
	}

	cases := []struct {
		name string
		file string
		line int
		want string
	}{
		{"trims whitespace", path, 2, "line two"},
		{"empty file arg", "", 1, ""},
		{"zero line", path, 0, ""},
		{"line past eof", path, 100, ""},
		{"nonexistent file", filepath.Join(dir, "missing.gray"), 1, ""},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := sourceLine(c.file, c.line); got != c.want {
				t.Errorf("sourceLine(%q, %d) = %q, want %q", c.file, c.line, got, c.want)
			}
		})
	}
}
