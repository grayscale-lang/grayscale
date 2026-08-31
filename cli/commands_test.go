// commands_test.go — Tests for CLI command helpers including bug-report
// output, man page rendering, and builtin/stdlib documentation display.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"io"
	"os"
	"strings"
	"testing"
)

func captureStdout(t *testing.T, fn func()) string {
	t.Helper()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe: %v", err)
	}
	old := os.Stdout
	os.Stdout = w
	fn()
	w.Close()
	os.Stdout = old
	var buf strings.Builder
	io.Copy(&buf, r)
	r.Close()
	return buf.String()
}

func TestReportInstallPath(t *testing.T) {
	path := reportInstallPath()
	if path == "" {
		t.Error("reportInstallPath returned empty string")
	}
	// Must not contain a raw HOME directory — should be redacted to ~
	if home, err := os.UserHomeDir(); err == nil && home != "" {
		if strings.HasPrefix(path, home+string(os.PathSeparator)) {
			t.Errorf("HOME not redacted: %q still contains %q", path, home)
		}
	}
}

func TestReportCCompiler_WithCC(t *testing.T) {
	// Point CC at a known-good binary that exists on every Unix system
	t.Setenv("CC", "/bin/sh")
	path, _, _ := reportCCompiler()
	if path == "" || path == "not found" {
		t.Errorf("expected a path when CC=/bin/sh, got %q", path)
	}
}

func TestReportCCompiler_CCNotFound(t *testing.T) {
	t.Setenv("CC", "/no/such/binary/does/not/exist")
	// Should fall through to PATH candidates; result may be found or not,
	// but must not panic and must return a non-empty path or "not found".
	path, _, _ := reportCCompiler()
	if path == "" {
		t.Error("reportCCompiler returned empty path")
	}
}

func TestPrintManUsage(t *testing.T) {
	out := captureStdout(t, printManUsage)
	if !strings.Contains(out, "gray man") {
		t.Errorf("printManUsage output missing 'gray man': %q", out)
	}
	if !strings.Contains(out, "builtins") {
		t.Errorf("printManUsage output missing 'builtins': %q", out)
	}
}

func TestLangKeywordsAllResolvable(t *testing.T) {
	// Every name listed in the keywords category must resolve to a man
	// page, either as a language reference entry or a builtin.
	for _, g := range langCategories["keywords"] {
		for _, name := range g.Names {
			_, lang := langManDocs[name]
			_, builtin := builtinManDocs[name]
			if !lang && !builtin {
				t.Errorf("keyword %q is listed in 'gray man keywords' but has no man page", name)
			}
		}
	}
	for _, name := range []string{"bit_and", "bit_or", "bit_xor", "bit_not", "bit_shift_left", "bit_shift_right", "use"} {
		if _, ok := langManDocs[name]; !ok {
			t.Errorf("langManDocs missing entry for %q", name)
		}
	}
}

func TestLangTypesAllResolvable(t *testing.T) {
	// Every name in the types category must resolve to a man page.
	for _, g := range langCategories["types"] {
		for _, name := range g.Names {
			_, lang := langManDocs[name]
			_, langType := langManDocs[name+"_type"]
			_, builtin := builtinManDocs[name]
			if !lang && !langType && !builtin {
				t.Errorf("type %q is listed in 'gray man types' but has no man page", name)
			}
		}
	}
	// 'map' must resolve as the container type, not arrays.map().
	if _, ok := langManDocs["map"]; !ok {
		t.Error("langManDocs missing container-type entry for \"map\"")
	}
}

func TestStdlibManExamplesPopulated(t *testing.T) {
	// The generator must write the @example block through to the TSV;
	// a regression once dropped it and left every Example empty.
	if got := stdlibManDocs["arrays.append"].Example; got == "" {
		t.Error("stdlibManDocs[\"arrays.append\"] has no Example")
	}
	withExample := 0
	for _, e := range stdlibManDocs {
		if e.Kind == "func" && e.Example != "" {
			withExample++
		}
	}
	if withExample == 0 {
		t.Error("no stdlib function man entry has an Example")
	}
}

func TestPrintBuiltinsIndex(t *testing.T) {
	out := captureStdout(t, printBuiltinsIndex)
	for _, expected := range []string{"println", "exit", "len", "int"} {
		if !strings.Contains(out, expected) {
			t.Errorf("printBuiltinsIndex missing %q in output", expected)
		}
	}
}

func TestPrintManEntry_Function(t *testing.T) {
	out := captureStdout(t, func() {
		printManEntry("builtin", "println", "func", "println(v any)", "", "Prints a value with a newline.", "println(42)")
	})
	if !strings.Contains(out, "println") {
		t.Errorf("output missing function name: %q", out)
	}
	if !strings.Contains(out, "builtin") {
		t.Errorf("output missing module name: %q", out)
	}
	if !strings.Contains(out, "Signature") {
		t.Errorf("output missing Signature label: %q", out)
	}
}

func TestPrintManEntry_Type(t *testing.T) {
	out := captureStdout(t, func() {
		printManEntry("builtin", "Error", "type", "", "msg string\ncode string", "Represents an error.", "")
	})
	if !strings.Contains(out, "type") {
		t.Errorf("output missing 'type' kind: %q", out)
	}
	if !strings.Contains(out, "Fields") {
		t.Errorf("output missing Fields section: %q", out)
	}
}

func TestPrintManEntry_WithExample(t *testing.T) {
	out := captureStdout(t, func() {
		printManEntry("math", "sqrt", "func", "sqrt(x float) -> float", "", "Square root.", "mut r float = math.sqrt(2.0)")
	})
	if !strings.Contains(out, "Example") {
		t.Errorf("output missing Example section: %q", out)
	}
	if !strings.Contains(out, "sqrt") {
		t.Errorf("output missing example content: %q", out)
	}
}

func TestPrintBuiltinEntry(t *testing.T) {
	entry := BuiltinManEntry{
		Kind:    "func",
		Sig:     "println(v any)",
		Desc:    "Print with newline.",
		Example: "println(\"hi\")",
	}
	out := captureStdout(t, func() {
		printBuiltinEntry("println", entry)
	})
	if !strings.Contains(out, "println") {
		t.Errorf("printBuiltinEntry output missing name: %q", out)
	}
}

func TestPrintStdlibModuleIndex_ValidModule(t *testing.T) {
	// Pick the first available module to avoid hardcoding
	var mod string
	for k := range stdlibModules {
		mod = k
		break
	}
	if mod == "" {
		t.Skip("no stdlib modules registered")
	}
	out := captureStdout(t, func() {
		printStdlibModuleIndex(mod)
	})
	if !strings.Contains(out, mod) {
		t.Errorf("printStdlibModuleIndex(%q) output missing module name: %q", mod, out)
	}
}
