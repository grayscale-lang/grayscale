// grayc_test.go — Tests for the grayc compiler-lookup and invocation logic,
// covering statFile edge cases, GRAY_COMPILER_PATH override behavior, and
// BuildOpts argument construction.
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

func TestStatFile(t *testing.T) {
	dir := t.TempDir()

	regular := filepath.Join(dir, "a_regular_file")
	if err := os.WriteFile(regular, []byte("x"), 0o644); err != nil {
		t.Fatalf("write regular: %v", err)
	}

	subdir := filepath.Join(dir, "a_directory")
	if err := os.Mkdir(subdir, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}

	symlink := filepath.Join(dir, "a_symlink_to_file")
	if err := os.Symlink(regular, symlink); err != nil {
		t.Fatalf("symlink: %v", err)
	}

	cases := []struct {
		name string
		path string
		want bool
	}{
		{"regular file", regular, true},
		{"directory", subdir, false},
		{"nonexistent", filepath.Join(dir, "nope"), false},
		{"empty path", "", false},
		// os.Stat follows symlinks, so a symlink pointing at a regular
		// file stats as a regular file. Documenting current behavior.
		{"symlink to regular", symlink, true},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := statFile(c.path); got != c.want {
				t.Errorf("statFile(%q) = %v, want %v", c.path, got, c.want)
			}
		})
	}
}

func TestFindCompilerPathOverride(t *testing.T) {
	dir := t.TempDir()

	regular := filepath.Join(dir, "grayc")
	if err := os.WriteFile(regular, []byte("x"), 0o755); err != nil {
		t.Fatalf("write regular: %v", err)
	}
	subdir := filepath.Join(dir, "grayc_as_directory")
	if err := os.Mkdir(subdir, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}

	t.Run("regular file is returned", func(t *testing.T) {
		t.Setenv("GRAY_COMPILER_PATH", regular)
		got, err := Find()
		if err != nil {
			t.Fatalf("Find: %v", err)
		}
		if got != regular {
			t.Errorf("Find() = %q, want %q", got, regular)
		}
	})

	t.Run("directory is not returned", func(t *testing.T) {
		t.Setenv("GRAY_COMPILER_PATH", subdir)
		got, _ := Find()
		if got == subdir {
			t.Errorf("Find() returned GRAY_COMPILER_PATH directory %q; expected fall-through", subdir)
		}
	})

	t.Run("nonexistent path is not returned", func(t *testing.T) {
		nonexistent := filepath.Join(dir, "does_not_exist")
		t.Setenv("GRAY_COMPILER_PATH", nonexistent)
		got, _ := Find()
		if got == nonexistent {
			t.Errorf("Find() returned nonexistent GRAY_COMPILER_PATH %q; expected fall-through", nonexistent)
		}
	})
}

func TestBuildArgs_CCFlag(t *testing.T) {
	t.Run("CC appends --cc flag", func(t *testing.T) {
		args := buildArgs("main.gray", BuildOpts{
			CC: "/usr/local/bin/zig cc -target x86_64-linux-gnu",
		})
		// Must contain --cc followed by the CC value
		found := false
		for i, a := range args {
			if a == "--cc" && i+1 < len(args) {
				if args[i+1] != "/usr/local/bin/zig cc -target x86_64-linux-gnu" {
					t.Errorf("--cc value = %q, want zig cc command", args[i+1])
				}
				found = true
				break
			}
		}
		if !found {
			t.Errorf("args %v missing --cc flag", args)
		}
	})

	t.Run("empty CC omits --cc flag", func(t *testing.T) {
		args := buildArgs("main.gray", BuildOpts{})
		for _, a := range args {
			if a == "--cc" {
				t.Error("--cc should not appear when CC is empty")
			}
		}
	})

	t.Run("CC combined with other flags", func(t *testing.T) {
		args := buildArgs("main.gray", BuildOpts{
			Output:  "out",
			EmitC:   true,
			Time:    true,
			NoColor: true,
			CC:      "zig cc -target aarch64-linux-gnu",
		})
		expected := map[string]bool{
			"-o": false, "-c": false, "--time": false,
			"--no-color": false, "--cc": false,
		}
		for _, a := range args {
			if _, ok := expected[a]; ok {
				expected[a] = true
			}
		}
		for flag, found := range expected {
			if !found {
				t.Errorf("args %v missing flag %q", args, flag)
			}
		}
	})

	t.Run("build and file are first two args", func(t *testing.T) {
		args := buildArgs("test.gray", BuildOpts{CC: "zig cc -target x86_64-macos"})
		if len(args) < 2 {
			t.Fatalf("expected at least 2 args, got %d", len(args))
		}
		if args[0] != "build" {
			t.Errorf("args[0] = %q, want 'build'", args[0])
		}
		if args[1] != "test.gray" {
			t.Errorf("args[1] = %q, want 'test.gray'", args[1])
		}
	})
}
