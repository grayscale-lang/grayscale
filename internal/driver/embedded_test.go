// embedded_test.go — Tests for the writeIfMissing helper that powers
// embedded runtime extraction, verifying write, skip, overwrite, and mode behavior.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package driver

import (
	"os"
	"path/filepath"
	"testing"
)

func TestWriteIfMissing_WritesNewFile(t *testing.T) {
	dir := t.TempDir()
	dest := filepath.Join(dir, "file.bin")
	data := []byte("hello world")

	if err := writeIfMissing(dest, data, 0o644); err != nil {
		t.Fatalf("writeIfMissing: %v", err)
	}

	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatalf("ReadFile: %v", err)
	}
	if string(got) != string(data) {
		t.Errorf("got %q, want %q", got, data)
	}
}

func TestWriteIfMissing_SkipsExistingCorrectSize(t *testing.T) {
	dir := t.TempDir()
	dest := filepath.Join(dir, "file.bin")
	data := []byte("original content")

	os.WriteFile(dest, data, 0o644)
	// Modify mtime to detect if file gets rewritten
	info1, _ := os.Stat(dest)

	if err := writeIfMissing(dest, data, 0o644); err != nil {
		t.Fatalf("writeIfMissing: %v", err)
	}

	info2, _ := os.Stat(dest)
	if info1.ModTime() != info2.ModTime() {
		t.Error("file was rewritten when it should have been skipped")
	}
}

func TestWriteIfMissing_OverwritesWrongSize(t *testing.T) {
	dir := t.TempDir()
	dest := filepath.Join(dir, "file.bin")

	os.WriteFile(dest, []byte("old"), 0o644)

	newData := []byte("completely new content here")
	if err := writeIfMissing(dest, newData, 0o644); err != nil {
		t.Fatalf("writeIfMissing: %v", err)
	}

	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatalf("ReadFile: %v", err)
	}
	if string(got) != string(newData) {
		t.Errorf("got %q, want %q", got, newData)
	}
}

func TestWriteIfMissing_CreatesParentDirError(t *testing.T) {
	// Writing to a path whose parent does not exist should fail gracefully.
	dest := "/no/such/parent/dir/file.bin"
	err := writeIfMissing(dest, []byte("data"), 0o644)
	if err == nil {
		t.Error("expected error writing to nonexistent parent directory")
	}
}

func TestWriteIfMissing_SetsMode(t *testing.T) {
	dir := t.TempDir()
	dest := filepath.Join(dir, "exec.bin")

	if err := writeIfMissing(dest, []byte("#!/bin/sh"), 0o755); err != nil {
		t.Fatalf("writeIfMissing: %v", err)
	}

	info, err := os.Stat(dest)
	if err != nil {
		t.Fatalf("Stat: %v", err)
	}
	// Check execute bit is set for owner
	if info.Mode()&0o100 == 0 {
		t.Errorf("expected execute bit set, got mode %v", info.Mode())
	}
}
