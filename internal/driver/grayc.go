// grayc.go — Go wrapper for locating and invoking the grayc compiler binary.
// Provides Find, Build, Run, Check, Fmt, and Version entry points used by the gray CLI.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package driver

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// Find locates the grayc binary using priority-ordered lookup:
// 1. GRAY_COMPILER_PATH environment variable (explicit override, always wins)
// 2. Embedded runtime extracted to ~/.gray/runtime/<hash>/ (release builds)
// 3. Same directory as the running gray binary (side-by-side install)
// 4. PATH lookup
// 5. Known install locations
//
// Release builds should always hit path 2; the fallback search paths are
// retained so a dev `go build ./cli` without running `make build`
// first (which leaves the embedded assets as empty stubs) still works.
func Find() (string, error) {
	// 1. Explicit override
	if p := os.Getenv("GRAY_COMPILER_PATH"); p != "" && statFile(p) {
		return p, nil
	}

	// 2. Embedded runtime (release builds)
	if p, err := extractEmbedded(); err == nil {
		return p, nil
	}

	// 3. Same directory as gray binary
	if exe, err := os.Executable(); err == nil {
		candidate := filepath.Join(filepath.Dir(exe), "grayc")
		if statFile(candidate) {
			return candidate, nil
		}
	}

	// 4. PATH lookup
	if p, err := exec.LookPath("grayc"); err == nil {
		return p, nil
	}

	// 5. Known install locations
	for _, p := range []string{"/usr/local/bin/grayc", "/usr/bin/grayc"} {
		if statFile(p) {
			return p, nil
		}
	}

	return "", fmt.Errorf("Grayscale compiler not found. Install it or set GRAY_COMPILER_PATH")
}

// statFile reports whether path exists and points at a regular file.
// Used by Find() so directory entries like the repo's grayc/ source tree
// don't get returned as the compiler binary.
func statFile(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.Mode().IsRegular()
}

// BuildOpts configures a build invocation.
type BuildOpts struct {
	Output     string
	OptLevel   string // "O0", "O1", "O2", "O3"
	Debug      bool
	Verbose    bool
	EmitC      bool
	NoColor    bool
	Time       bool
	Quiet      bool   // Suppress all warnings
	QuietCodes string // Suppress specific warning codes (comma-separated)
}

// Run compiles and executes a Grayscale source file via grayc run.
// stdout/stderr are streamed directly to the caller's terminal.
// Returns the exit code from the compiled program.
func Run(file string, extraArgs []string) (int, error) {
	graycPath, err := Find()
	if err != nil {
		return 1, err
	}

	args := []string{"run", file}
	args = append(args, extraArgs...)

	return execute(graycPath, args)
}

// Build compiles a Grayscale source file to a native binary via grayc build.
func Build(file string, opts BuildOpts) (int, error) {
	graycPath, err := Find()
	if err != nil {
		return 1, err
	}

	args := []string{"build", file}
	if opts.Output != "" {
		args = append(args, "-o", opts.Output)
	}
	if opts.OptLevel != "" {
		args = append(args, "-"+opts.OptLevel)
	}
	if opts.Debug {
		args = append(args, "-g")
	}
	if opts.Verbose {
		args = append(args, "-v")
	}
	if opts.EmitC {
		args = append(args, "-c")
	}
	if opts.NoColor {
		args = append(args, "--no-color")
	}
	if opts.Time {
		args = append(args, "--time")
	}
	if opts.Quiet {
		args = append(args, "--quiet")
	} else if opts.QuietCodes != "" {
		args = append(args, "--quiet", opts.QuietCodes)
	}

	return execute(graycPath, args)
}

// Check type-checks a Grayscale source file without compiling.
func Check(file string, extraArgs []string) (int, error) {
	graycPath, err := Find()
	if err != nil {
		return 1, err
	}

	args := []string{"check", file}
	args = append(args, extraArgs...)
	return execute(graycPath, args)
}

// Version returns the grayc compiler version string.
func Version() (string, error) {
	graycPath, err := Find()
	if err != nil {
		return "", err
	}

	cmd := exec.Command(graycPath, "version")
	out, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to get grayc version: %w", err)
	}

	return strings.TrimSpace(string(out)), nil
}

// Fmt formats a single .gray file in place using the grayc --fmt flag.
// Returns 0 on success, non-zero on failure.
func Fmt(file string) (int, error) {
	graycPath, err := Find()
	if err != nil {
		return 1, err
	}
	return executeSilent(graycPath, []string{"--fmt", file})
}

// executeSilent runs grayc without streaming I/O, for use by fmt/check internals.
func executeSilent(graycPath string, args []string) (int, error) {
	cmd := exec.Command(graycPath, args...)
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			return exitErr.ExitCode(), nil
		}
		return 1, err
	}
	return 0, nil
}

// execute runs the grayc binary with the given args, streaming I/O.
func execute(graycPath string, args []string) (int, error) {
	cmd := exec.Command(graycPath, args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	err := cmd.Run()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok {
			return exitErr.ExitCode(), nil
		}
		return 1, err
	}
	return 0, nil
}
