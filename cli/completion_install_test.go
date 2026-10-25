// completion_install_test.go — Tests for placing the completion script:
// per-shell path selection, idempotent rewrite, and skip-without-failure.
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

func TestCompletionSiteFor(t *testing.T) {
	home := filepath.Join(string(filepath.Separator), "home", "user")
	noEnv := func(string) string { return "" }
	writable := t.TempDir()
	unwritable := filepath.Join(writable, "missing")
	fpath := func() []string { return []string{unwritable, writable} }

	tests := []struct {
		name   string
		shell  string
		getenv func(string) string
		want   string
	}{
		{"fish default", "fish", noEnv, filepath.Join(home, ".config", "fish", "completions", "gray.fish")},
		{"fish xdg", "fish", func(key string) string {
			if key == "XDG_CONFIG_HOME" {
				return "/xdg/config"
			}
			return ""
		}, filepath.Join("/xdg/config", "fish", "completions", "gray.fish")},
		{"bash default", "bash", noEnv, filepath.Join(home, ".local", "share", "bash-completion", "completions", "gray")},
		{"zsh first writable fpath", "zsh", noEnv, filepath.Join(writable, "_gray")},
		{"powershell", "powershell", noEnv, filepath.Join(home, ".gray", "completions", "gray.ps1")},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			site, ok := completionSiteFor(test.shell, home, test.getenv, fpath)
			if !ok || site.file != test.want {
				t.Errorf("got (%q, %v), want %q", site.file, ok, test.want)
			}
		})
	}

	if _, ok := completionSiteFor("zsh", home, noEnv, func() []string { return []string{unwritable} }); ok {
		t.Error("zsh with no writable fpath directory should report no site")
	}
	if _, ok := completionSiteFor("tcsh", home, noEnv, fpath); ok {
		t.Error("unknown shell should report no site")
	}
}

func TestDetectCompletionShell(t *testing.T) {
	shellEnv := func(value string) func(string) string {
		return func(string) string { return value }
	}
	cases := []struct{ goos, shell, want string }{
		{"darwin", "/bin/zsh", "zsh"},
		{"linux", "/usr/bin/fish", "fish"},
		{"linux", "/bin/bash", "bash"},
		{"linux", "/bin/sh", ""},
		{"windows", "", "powershell"},
	}
	for _, test := range cases {
		if got := detectCompletionShell(test.goos, shellEnv(test.shell)); got != test.want {
			t.Errorf("detectCompletionShell(%q, %q) = %q, want %q", test.goos, test.shell, got, test.want)
		}
	}
}

func TestWriteFileIfChangedIsIdempotent(t *testing.T) {
	path := filepath.Join(t.TempDir(), "nested", "gray.fish")
	if err := writeFileIfChanged(path, []byte("one"), true); err != nil {
		t.Fatal(err)
	}
	first, _ := os.Stat(path)
	if err := writeFileIfChanged(path, []byte("one"), true); err != nil {
		t.Fatal(err)
	}
	second, _ := os.Stat(path)
	if !first.ModTime().Equal(second.ModTime()) {
		t.Error("identical rewrite touched the file")
	}
	if err := writeFileIfChanged(path, []byte("two"), true); err != nil {
		t.Fatal(err)
	}
	if got, _ := os.ReadFile(path); string(got) != "two" {
		t.Errorf("changed script not written, got %q", got)
	}
}

func TestPlaceCompletionScript(t *testing.T) {
	home := t.TempDir()
	noEnv := func(string) string { return "" }
	generate := func(shell string) ([]byte, error) { return []byte("script for " + shell), nil }
	failing := func(string) ([]byte, error) { return nil, os.ErrInvalid }
	file := filepath.Join(home, ".config", "fish", "completions", "gray.fish")

	if got := placeCompletionScript("fish", home, noEnv, nil, generate); got != file {
		t.Fatalf("first install = %q, want %q", got, file)
	}
	first, _ := os.Stat(file)
	if got := placeCompletionScript("fish", home, noEnv, nil, generate); got != file {
		t.Fatalf("second install = %q, want %q", got, file)
	}
	second, _ := os.Stat(file)
	if !first.ModTime().Equal(second.ModTime()) {
		t.Error("second install changed the file")
	}

	if got := placeCompletionScript("fish", home, noEnv, nil, failing); got != "" {
		t.Errorf("generator failure should skip, got %q", got)
	}
	blocker := filepath.Join(home, "blocker")
	os.WriteFile(blocker, nil, 0o644)
	blocked := func(key string) string {
		if key == "XDG_CONFIG_HOME" {
			return filepath.Join(blocker, "config")
		}
		return ""
	}
	if got := placeCompletionScript("fish", home, blocked, nil, generate); got != "" {
		t.Errorf("unwritable directory should skip, got %q", got)
	}
	if got := placeCompletionScript("zsh", home, noEnv, func() []string { return nil }, generate); got != "" {
		t.Errorf("zsh without a writable fpath directory should skip, got %q", got)
	}
}

func TestCompletionInstalledLine(t *testing.T) {
	file := filepath.Join("home", "user", ".config", "fish", "completions", "gray.fish")
	if got, want := completionInstalledLine("fish", file), "Shell completion installed: "+file; got != want {
		t.Errorf("fish = %q, want %q", got, want)
	}
	script := filepath.Join("home", "user", ".gray", "completions", "gray.ps1")
	if got := completionInstalledLine("powershell", script); !strings.Contains(got, "`. \""+script+"\"`") {
		t.Errorf("powershell line %q does not say to dot-source %s", got, script)
	}
}
