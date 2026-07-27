// cross_test.go — Tests for cross-compilation commands: target map
// validation, zig lookup error handling, and crossBuildCmd flag wiring.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"os"
	"sort"
	"strings"
	"testing"
)

func TestCrossTargets_AllHaveZigTriples(t *testing.T) {
	if len(crossTargets) == 0 {
		t.Fatal("crossTargets map is empty")
	}
	for target, triple := range crossTargets {
		if triple == "" {
			t.Errorf("target %q has empty zig triple", target)
		}
	}
}

func TestCrossTargets_ExpectedEntries(t *testing.T) {
	expected := []string{
		"linux-amd64",
		"linux-arm64",
		"windows-amd64",
		"mac-arm64",
		"mac-amd64",
	}
	for _, target := range expected {
		if _, ok := crossTargets[target]; !ok {
			t.Errorf("missing expected target %q", target)
		}
	}
}

func TestCrossTargets_ZigTripleFormat(t *testing.T) {
	// Zig triples follow the pattern: <arch>-<os> or <arch>-<os>-<abi>
	for target, triple := range crossTargets {
		parts := strings.Split(triple, "-")
		if len(parts) < 2 {
			t.Errorf("target %q triple %q has fewer than 2 dash-separated parts", target, triple)
		}
	}
}

func TestCrossTargetsCmd_PrintsAllTargets(t *testing.T) {
	out := captureStdout(t, func() {
		crossTargetsCmd.Run(crossTargetsCmd, nil)
	})
	for target, triple := range crossTargets {
		if !strings.Contains(out, target) {
			t.Errorf("cross targets output missing target %q", target)
		}
		if !strings.Contains(out, triple) {
			t.Errorf("cross targets output missing triple %q for target %q", triple, target)
		}
	}
}

func TestCrossTargetsCmd_OutputIsSorted(t *testing.T) {
	out := captureStdout(t, func() {
		crossTargetsCmd.Run(crossTargetsCmd, nil)
	})
	// Extract target names from output lines
	targets := make([]string, 0, len(crossTargets))
	for target := range crossTargets {
		targets = append(targets, target)
	}
	sort.Strings(targets)

	// Verify sorted order: each target appears after the previous one
	lastIdx := -1
	for _, target := range targets {
		idx := strings.Index(out, target)
		if idx == -1 {
			t.Fatalf("target %q not found in output", target)
		}
		if idx <= lastIdx {
			t.Errorf("target %q appears before previous target in output (not sorted)", target)
		}
		lastIdx = idx
	}
}

func TestFindZig_ErrorMessage(t *testing.T) {
	// Override PATH to ensure zig is not found
	t.Setenv("PATH", t.TempDir())
	_, err := findZig()
	if err == nil {
		t.Fatal("expected error when zig is not in PATH")
	}
	msg := err.Error()
	if !strings.Contains(msg, "zig not found") {
		t.Errorf("error should mention 'zig not found', got: %q", msg)
	}
	if !strings.Contains(msg, "ziglang.org") {
		t.Errorf("error should include install URL, got: %q", msg)
	}
}

func TestCrossBuildCmd_MissingTarget(t *testing.T) {
	// Reset flags to defaults for this test
	crossBuildCmd.Flags().Set("target", "")

	err := crossBuildCmd.RunE(crossBuildCmd, []string{"main.gray"})
	if err == nil {
		t.Fatal("expected error when --target is not set")
	}
}

func TestCrossBuildCmd_InvalidTarget(t *testing.T) {
	crossBuildCmd.Flags().Set("target", "plan9-mips")

	// Capture stderr since errors are printed there
	oldStderr := os.Stderr
	r, w, _ := os.Pipe()
	os.Stderr = w

	err := crossBuildCmd.RunE(crossBuildCmd, []string{"main.gray"})

	w.Close()
	os.Stderr = oldStderr
	buf := make([]byte, 4096)
	n, _ := r.Read(buf)
	r.Close()
	stderr := string(buf[:n])

	if err == nil {
		t.Fatal("expected error for invalid target")
	}
	if !strings.Contains(stderr, "unknown target") {
		t.Errorf("stderr should mention 'unknown target', got: %q", stderr)
	}
	if !strings.Contains(stderr, "plan9-mips") {
		t.Errorf("stderr should echo the invalid target name, got: %q", stderr)
	}
}

func TestCrossBuildCmd_NonGrayFile(t *testing.T) {
	// Capture stderr
	oldStderr := os.Stderr
	r, w, _ := os.Pipe()
	os.Stderr = w

	err := crossBuildCmd.RunE(crossBuildCmd, []string{"main.txt"})

	w.Close()
	os.Stderr = oldStderr
	buf := make([]byte, 4096)
	n, _ := r.Read(buf)
	r.Close()
	stderr := string(buf[:n])

	if err == nil {
		t.Fatal("expected error for non-.gray file")
	}
	if !strings.Contains(stderr, ".gray") {
		t.Errorf("stderr should mention .gray extension, got: %q", stderr)
	}
}

func TestCrossCmd_HasSubcommands(t *testing.T) {
	cmds := crossCmd.Commands()
	names := make(map[string]bool)
	for _, c := range cmds {
		names[c.Name()] = true
	}
	if !names["build"] {
		t.Error("crossCmd missing 'build' subcommand")
	}
	if !names["targets"] {
		t.Error("crossCmd missing 'targets' subcommand")
	}
}

func TestCrossBuildCmd_HasExpectedFlags(t *testing.T) {
	flags := []string{"target", "output", "emit-c", "time", "quiet", "no-color"}
	for _, name := range flags {
		if crossBuildCmd.Flags().Lookup(name) == nil {
			t.Errorf("crossBuildCmd missing flag %q", name)
		}
	}
}
