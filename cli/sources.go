// sources.go — Expands the path arguments of gray fmt, gray doc, and
// gray test into the .gray files they name, so all three commands read the
// same argument the same way.
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
)

// expandGraySourceArgs turns path arguments into a de-duplicated list of
// .gray files:
//   - "file.gray"          — that file
//   - "dir"                — the .gray files directly inside dir (non-recursive)
//   - "dir/..." or "..."   — every .gray file under dir, recursively
//
// A path that is missing, or is neither a .gray file nor a directory, is
// reported on stderr as "gray <command>: ..." and skipped; ok is false when
// that happened. Files are returned as written (joined onto their argument).
func expandGraySourceArgs(command string, args []string) (files []string, ok bool) {
	ok = true
	seen := make(map[string]bool)
	add := func(path string) {
		key := path
		if abs, err := filepath.Abs(path); err == nil {
			key = abs
		}
		if seen[key] {
			return
		}
		seen[key] = true
		files = append(files, path)
	}
	report := func(format string, a ...any) {
		fmt.Fprintf(os.Stderr, "gray %s: "+format+"\n", append([]any{command}, a...)...)
		ok = false
	}

	for _, arg := range args {
		// Accept both src/... and src\... — Windows tab completion produces
		// backslashes, and the recursive suffix should work either way.
		slashed := filepath.ToSlash(arg)
		if strings.HasSuffix(slashed, "/...") || arg == "..." {
			baseDir := filepath.FromSlash(strings.TrimSuffix(slashed, "/..."))
			if baseDir == "" || baseDir == "..." {
				baseDir = "."
			}
			info, err := os.Stat(baseDir)
			if err != nil {
				report("%v", err)
				continue
			}
			if !info.IsDir() {
				report("'%s' is not a directory", baseDir)
				continue
			}
			filepath.WalkDir(baseDir, func(path string, entry os.DirEntry, err error) error {
				if err == nil && !entry.IsDir() && strings.HasSuffix(path, ".gray") {
					add(path)
				}
				return nil
			})
			continue
		}

		info, err := os.Stat(arg)
		if err != nil {
			report("%v", err)
			continue
		}
		if info.IsDir() {
			entries, err := os.ReadDir(arg)
			if err != nil {
				report("%v", err)
				continue
			}
			for _, entry := range entries {
				if !entry.IsDir() && strings.HasSuffix(entry.Name(), ".gray") {
					add(filepath.Join(arg, entry.Name()))
				}
			}
		} else if strings.HasSuffix(arg, ".gray") {
			add(arg)
		} else {
			report("'%s' is not a .gray file or directory", arg)
		}
	}
	return files, ok
}
