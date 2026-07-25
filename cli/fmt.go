// fmt.go — Source formatter for .gray files ("gray fmt"). Normalizes
// indentation, trailing whitespace, blank-line runs, and EOF newlines,
// with a --check mode for CI gating.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/grayscale-lang/grayscale/internal/driver"
)

const (
	tabWidth                 = 4
	maxConsecutiveBlankLines = 2
)

// formatGraySource applies text-level normalizations to Grayscale source bytes:
//   - trailing whitespace stripped from each line
//   - leading tabs expanded to 4 spaces each
//   - runs of more than 2 consecutive blank lines collapsed to 2
//   - exactly one trailing newline at EOF
func formatGraySource(src []byte) []byte {
	lines := strings.Split(string(src), "\n")

	// Expand leading tabs and strip trailing whitespace on each line.
	for i, line := range lines {
		// Expand leading tabs: count leading tab/space mix, replace tabs with 4 spaces.
		j := 0
		var prefix strings.Builder
		for j < len(line) && (line[j] == '\t' || line[j] == ' ') {
			if line[j] == '\t' {
				prefix.WriteString(strings.Repeat(" ", tabWidth))
			} else {
				prefix.WriteByte(' ')
			}
			j++
		}
		rest := strings.TrimRight(line[j:], " \t")
		lines[i] = prefix.String() + rest
	}

	// Collapse runs of more than 2 consecutive blank lines.
	var out []string
	blank := 0
	for _, line := range lines {
		if line == "" {
			blank++
			if blank <= maxConsecutiveBlankLines {
				out = append(out, line)
			}
		} else {
			blank = 0
			out = append(out, line)
		}
	}

	// Strip trailing blank lines, then add exactly one trailing newline.
	for len(out) > 0 && out[len(out)-1] == "" {
		out = out[:len(out)-1]
	}
	return []byte(strings.Join(out, "\n") + "\n")
}

// collectFmtFiles expands the user-supplied args into a deduplicated list
// of .gray file paths. Supported forms:
//
//   - "file.gray"           — single file
//   - "dir"               — all .gray files directly inside dir (non-recursive)
//   - "dir/subdir"        — all .gray files directly inside dir/subdir
//   - "./..." or "dir/..." — recursive walk for .gray files
func collectFmtFiles(args []string) []string {
	seen := make(map[string]struct{})
	var files []string

	add := func(p string) {
		ap, err := filepath.Abs(p)
		if err != nil {
			fmt.Fprintf(os.Stderr, "gray fmt: cannot resolve %s: %v\n", p, err)
			return
		}
		if _, ok := seen[ap]; ok {
			return
		}
		seen[ap] = struct{}{}
		files = append(files, ap)
	}

	for _, arg := range args {
		if strings.HasSuffix(arg, "/...") || arg == "..." {
			baseDir := strings.TrimSuffix(arg, "/...")
			if baseDir == "" || baseDir == "." || baseDir == "..." {
				baseDir = "."
			}
			filepath.Walk(baseDir, func(p string, info os.FileInfo, err error) error {
				if err != nil {
					return nil
				}
				if !info.IsDir() && strings.HasSuffix(p, ".gray") {
					add(p)
				}
				return nil
			})
			continue
		}

		info, err := os.Stat(arg)
		if err != nil {
			fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
			continue
		}

		if info.IsDir() {
			entries, err := os.ReadDir(arg)
			if err != nil {
				fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
				continue
			}
			for _, e := range entries {
				if !e.IsDir() && strings.HasSuffix(e.Name(), ".gray") {
					add(filepath.Join(arg, e.Name()))
				}
			}
		} else if strings.HasSuffix(arg, ".gray") {
			add(arg)
		} else {
			fmt.Fprintf(os.Stderr, "gray fmt: '%s' is not a .gray file or directory\n", arg)
		}
	}
	return files
}

// runFmt is the entry point invoked by the Cobra fmtCmd. It returns the
// exit code the caller should propagate (0 success, 1 on error).
func runFmt(args []string, checkMode bool) int {
	files := collectFmtFiles(args)
	if len(files) == 0 {
		fmt.Println("gray fmt: no .gray files found")
		return 0
	}

	exit := 0
	changed := 0

	for _, path := range files {
		orig, err := os.ReadFile(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
			exit = 1
			continue
		}

		if checkMode {
			// Copy to temp, format it, compare
			tmp, err := os.CreateTemp("", "gray-fmt-check-*.gray")
			if err != nil {
				fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
				exit = 1
				continue
			}
			tmpName := tmp.Name()
			tmp.Write(orig)
			tmp.Close()

			driver.Fmt(tmpName)

			formatted, err := os.ReadFile(tmpName)
			os.Remove(tmpName)
			if err != nil {
				fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
				exit = 1
				continue
			}
			if string(orig) != string(formatted) {
				fmt.Printf("would format: %s\n", path)
				if exit == 0 {
					exit = 1
				}
			}
			continue
		}

		// Normal mode: format in place
		code, err := driver.Fmt(path)
		if err != nil || code != 0 {
			fmt.Fprintf(os.Stderr, "gray fmt: failed to format '%s'\n", path)
			exit = 1
			continue
		}
		after, err := os.ReadFile(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
			exit = 1
			continue
		}
		if string(orig) != string(after) {
			fmt.Printf("formatted: %s\n", path)
			changed++
		}
	}

	if !checkMode && changed == 0 {
		fmt.Printf("gray fmt: %d file(s) checked, already formatted\n", len(files))
	}
	return exit
}
