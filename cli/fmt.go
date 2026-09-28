// fmt.go — Source formatter for .gray files ("gray fmt"). Normalizes
// indentation, trailing whitespace, blank-line runs, and EOF newlines,
// with a --check mode for CI gating.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.
//
// Contributors:
//  - @mvanhorn

package main

import (
	"fmt"
	"os"
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

// runFmt is the entry point invoked by the Cobra fmtCmd. It returns the
// exit code the caller should propagate (0 success, 1 on error).
func runFmt(args []string, checkMode bool) int {
	files, ok := expandGraySourceArgs("fmt", args)
	exit := 0
	if !ok {
		exit = 1
	}
	if len(files) == 0 {
		fmt.Println("gray fmt: no .gray files found")
		return exit
	}

	changed := 0

	for _, path := range files {
		info, err := os.Stat(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "gray fmt: %v\n", err)
			exit = 1
			continue
		}
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
			formatted = formatGraySource(formatted)
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
		after = formatGraySource(after)
		if err := os.WriteFile(path, after, info.Mode()); err != nil {
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
