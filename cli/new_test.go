// new_test.go — Tests for the project scaffolding command, including
// template resolution, project creation, comment injection, and
// force-overwrite behavior across all supported templates.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"os"
	"path/filepath"
	"testing"
)

// fakeStdin replaces os.Stdin with a reader containing the given input for the
// duration of the test, then restores the original stdin.
func fakeStdin(t *testing.T, input string) {
	t.Helper()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe: %v", err)
	}
	orig := os.Stdin
	os.Stdin = r
	t.Cleanup(func() {
		os.Stdin = orig
		r.Close()
	})
	if _, err := w.WriteString(input); err != nil {
		t.Fatalf("write to pipe: %v", err)
	}
	w.Close()
}

func TestIsValidProjectName(t *testing.T) {
	cases := []struct {
		name string
		want bool
	}{
		{"myproject", true},
		{"my-project_2", true},
		{"", false},
		{"/etc/passwd", false},
		{"../escape", false},
		{"~/home", false},
		{"has space", false},
		{"2startswithdigit", false},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := isValidProjectName(c.name); got != c.want {
				t.Errorf("isValidProjectName(%q) = %v, want %v", c.name, got, c.want)
			}
		})
	}
}

func TestResolveTemplate(t *testing.T) {
	cases := []struct {
		template   string
		serverType string
		wantSrc    string
		wantEntry  string
	}{
		{"basic", "", "templates/basic", "main.gray"},
		{"cli", "", "templates/cli", "main.gray"},
		{"lib", "", "templates/lib", "main.gray"},
		{"multi", "", "templates/multi", "main.gray"},
		{"server", "minimal", "templates/server_minimal", "main.gray"},
		{"server", "normal", "templates/server_normal", "main.gray"},
		{"client", "minimal", "templates/client_minimal", "main.gray"},
		{"client", "normal", "templates/client_normal", "main.gray"},
	}
	for _, c := range cases {
		t.Run(c.template+"/"+c.serverType, func(t *testing.T) {
			src, entry, _ := resolveTemplate(c.template, c.serverType, "myproj")
			if src != c.wantSrc {
				t.Errorf("src = %q, want %q", src, c.wantSrc)
			}
			if entry != c.wantEntry {
				t.Errorf("entry = %q, want %q", entry, c.wantEntry)
			}
		})
	}
}

func TestResolveTemplate_Unknown(t *testing.T) {
	src, entry, rename := resolveTemplate("nonexistent", "", "proj")
	if src != "" || entry != "" || rename != nil {
		t.Errorf("unknown template should return empty, got src=%q entry=%q", src, entry)
	}
}

func TestCreateProject_Basic(t *testing.T) {
	parent := t.TempDir()
	name := filepath.Join(parent, "myproject")

	if err := createProject(name, "basic", false, false, ""); err != nil {
		t.Fatalf("createProject: %v", err)
	}

	if _, err := os.Stat(name); err != nil {
		t.Fatalf("project directory not created: %v", err)
	}

	entries, err := os.ReadDir(name)
	if err != nil {
		t.Fatalf("ReadDir: %v", err)
	}
	if len(entries) == 0 {
		t.Error("project directory is empty")
	}
}

func TestCreateProject_WithComments(t *testing.T) {
	parent := t.TempDir()
	name := filepath.Join(parent, "commented")

	if err := createProject(name, "basic", true, false, ""); err != nil {
		t.Fatalf("createProject with comments: %v", err)
	}

	main := filepath.Join(name, "main.gray")
	data, err := os.ReadFile(main)
	if err != nil {
		t.Fatalf("main.gray not created: %v", err)
	}
	if len(data) == 0 {
		t.Fatal("main.gray is empty")
	}
	// Quick-ref block must be prepended
	if string(data[:len("// Grayscale")]) != "// Grayscale" {
		t.Errorf("quick-ref block not prepended; file starts with %q", string(data[:20]))
	}
}

func TestCreateProject_ExistingDirErrors(t *testing.T) {
	dir := t.TempDir()
	err := createProject(dir, "basic", false, false, "")
	if err == nil {
		t.Fatal("expected error for existing dir without --force")
	}
}

func TestCreateProject_Force(t *testing.T) {
	dir := t.TempDir()
	// First creation (dir already exists — confirm deletion)
	fakeStdin(t, "y\n")
	if err := createProject(dir, "basic", false, true, ""); err != nil {
		t.Fatalf("first createProject: %v", err)
	}
	// Force overwrite (dir exists again — confirm deletion)
	fakeStdin(t, "y\n")
	if err := createProject(dir, "cli", false, true, ""); err != nil {
		t.Fatalf("forced createProject: %v", err)
	}
}

func TestCreateProject_AllTemplates(t *testing.T) {
	templates := []struct {
		template   string
		serverType string
	}{
		{"basic", ""},
		{"cli", ""},
		{"lib", ""},
		{"multi", ""},
		{"server", "minimal"},
		{"server", "normal"},
		{"client", "minimal"},
		{"client", "normal"},
	}
	for _, tt := range templates {
		t.Run(tt.template+"/"+tt.serverType, func(t *testing.T) {
			parent := t.TempDir()
			name := filepath.Join(parent, "proj")
			if err := createProject(name, tt.template, false, false, tt.serverType); err != nil {
				t.Errorf("createProject(%q, %q): %v", tt.template, tt.serverType, err)
			}
		})
	}
}
