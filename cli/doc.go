// doc.go — Documentation generator for Grayscale source files ("gray doc").
// Extracts #doc attributes from functions, structs, and enums, then emits
// a structured Markdown reference file.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// DocEntry represents a documented item
type DocEntry struct {
	Name        string
	Kind        string // "function", "struct", "enum", "variable"
	Signature   string
	Description string
	File        string
}

// defaultDocOutputPath is used when the caller does not pass --output.
const defaultDocOutputPath = "DOCS.md"

// generateDocs is the entry point for the gray doc command. outputPath is
// the destination markdown file; an empty string falls back to
// defaultDocOutputPath so the `DOCS.md`-in-cwd behavior is
// unchanged for callers that do not pass --output.
func generateDocs(args []string, outputPath string) {
	if outputPath == "" {
		outputPath = defaultDocOutputPath
	}

	var entries []DocEntry

	for _, arg := range args {
		if strings.HasSuffix(arg, "/...") {
			baseDir := strings.TrimSuffix(arg, "/...")
			if baseDir == "." || baseDir == "" {
				baseDir = "."
			}
			entries = append(entries, collectDocsRecursive(baseDir)...)
		} else if strings.HasSuffix(arg, ".gray") {
			entries = append(entries, collectDocsFromFile(arg)...)
		} else {
			entries = append(entries, collectDocsFromDir(arg)...)
		}
	}

	if len(entries) == 0 {
		fmt.Println("No documented items found.")
		return
	}

	output := generateMarkdown(entries)

	// Warn when --output resolves to a path outside the working directory.
	if cwd, err := os.Getwd(); err == nil {
		if abs, err := filepath.Abs(outputPath); err == nil {
			rel, err := filepath.Rel(cwd, abs)
			if err != nil || strings.HasPrefix(rel, "..") {
				fmt.Printf("Warning: output path '%s' is outside the current directory\n", outputPath)
			}
		}
	}

	// Make sure the parent directory exists when --output points at a
	// nested path like docs/API.md.
	if dir := filepath.Dir(outputPath); dir != "" && dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			fmt.Printf("Error creating output directory %s: %v\n", dir, err)
			return
		}
	}

	if err := os.WriteFile(outputPath, []byte(output), 0644); err != nil {
		fmt.Printf("Error writing %s: %v\n", outputPath, err)
		return
	}

	fmt.Printf("Generated %s with %d documented item(s)\n", outputPath, len(entries))
}

// collectDocsFromFile extracts documented items from a single .gray file using text scanning.
// Looks for #doc("description") followed by do/const struct/const enum declarations.
func collectDocsFromFile(filename string) []DocEntry {
	var entries []DocEntry

	absPath, err := filepath.Abs(filename)
	if err != nil {
		fmt.Printf("Error resolving path %s: %v\n", filename, err)
		return entries
	}

	f, err := os.Open(absPath)
	if err != nil {
		fmt.Printf("Error reading file %s: %v\n", filename, err)
		return entries
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	var pendingDoc string
	var inBlock bool
	var blockLines []string
	var blockKind string
	var blockName string
	braceDepth := 0

	for scanner.Scan() {
		line := scanner.Text()
		trimmed := strings.TrimSpace(line)

		// Collecting a block (struct/enum body)
		if inBlock {
			blockLines = append(blockLines, line)
			braceDepth += strings.Count(line, "{") - strings.Count(line, "}")
			if braceDepth <= 0 {
				inBlock = false
				entries = append(entries, DocEntry{
					Name:        blockName,
					Kind:        blockKind,
					Signature:   strings.Join(blockLines, "\n"),
					Description: pendingDoc,
					File:        filename,
				})
				pendingDoc = ""
				blockLines = nil
			}
			continue
		}

		// Check for #doc attribute
		if strings.HasPrefix(trimmed, "#doc(") {
			// Extract description from #doc("...")
			desc := extractDocString(trimmed)
			pendingDoc = desc
			continue
		}

		// If we have a pending doc, the next non-empty line should be a declaration
		if pendingDoc != "" && trimmed != "" {
			if strings.HasPrefix(trimmed, "do ") || strings.HasPrefix(trimmed, "private do ") {
				// Function declaration — capture the signature line
				sig := trimmed
				// Strip the body (everything after {)
				if idx := strings.Index(sig, "{"); idx >= 0 {
					sig = strings.TrimSpace(sig[:idx])
				}
				name := extractFuncName(trimmed)
				entries = append(entries, DocEntry{
					Name:        name,
					Kind:        "function",
					Signature:   sig,
					Description: pendingDoc,
					File:        filename,
				})
				pendingDoc = ""
			} else if strings.Contains(trimmed, " struct ") && strings.HasPrefix(trimmed, "const ") {
				// Struct declaration — collect until closing brace
				blockName = extractStructEnumName(trimmed)
				blockKind = "struct"
				blockLines = []string{line}
				braceDepth = strings.Count(line, "{") - strings.Count(line, "}")
				if braceDepth > 0 {
					inBlock = true
				} else {
					entries = append(entries, DocEntry{
						Name:        blockName,
						Kind:        "struct",
						Signature:   trimmed,
						Description: pendingDoc,
						File:        filename,
					})
					pendingDoc = ""
				}
			} else if strings.Contains(trimmed, " enum ") && strings.HasPrefix(trimmed, "const ") {
				blockName = extractStructEnumName(trimmed)
				blockKind = "enum"
				blockLines = []string{line}
				braceDepth = strings.Count(line, "{") - strings.Count(line, "}")
				if braceDepth > 0 {
					inBlock = true
				} else {
					entries = append(entries, DocEntry{
						Name:        blockName,
						Kind:        "enum",
						Signature:   trimmed,
						Description: pendingDoc,
						File:        filename,
					})
					pendingDoc = ""
				}
			} else if isVarDecl(trimmed) {
				// File-scope variable (const or mut)
				sig := trimmed
				name := extractVarName(trimmed)
				entries = append(entries, DocEntry{
					Name:        name,
					Kind:        "variable",
					Signature:   sig,
					Description: pendingDoc,
					File:        filename,
				})
				pendingDoc = ""
			} else {
				// Not a documentable declaration — discard pending doc
				pendingDoc = ""
			}
		}
	}

	return entries
}

// extractDocString extracts the description from #doc("some text")
func extractDocString(line string) string {
	// Find the opening quote after #doc(
	start := strings.Index(line, `"`)
	if start < 0 {
		start = strings.Index(line, `'`)
	}
	if start < 0 {
		return ""
	}
	quote := line[start]
	end := strings.LastIndex(line, string(quote))
	if end <= start {
		return ""
	}
	return line[start+1 : end]
}

// extractFuncName extracts function name from "do funcname(" or "private do funcname("
func extractFuncName(line string) string {
	line = strings.TrimPrefix(line, "private ")
	line = strings.TrimPrefix(line, "do ")
	if idx := strings.Index(line, "("); idx >= 0 {
		return strings.TrimSpace(line[:idx])
	}
	return strings.Fields(line)[0]
}

// extractStructEnumName extracts name from "const Name struct {" or "const Name enum {"
func extractStructEnumName(line string) string {
	line = strings.TrimPrefix(line, "const ")
	fields := strings.Fields(line)
	if len(fields) > 0 {
		return fields[0]
	}
	return ""
}

// isVarDecl returns true if the line is a file-scope variable declaration
// (const or mut) that is not a struct or enum.
func isVarDecl(line string) bool {
	stripped := strings.TrimPrefix(line, "private ")
	if !strings.HasPrefix(stripped, "const ") && !strings.HasPrefix(stripped, "mut ") {
		return false
	}
	// Exclude struct and enum declarations
	if strings.Contains(line, " struct ") || strings.Contains(line, " enum ") {
		return false
	}
	return strings.Contains(line, "=")
}

// extractVarName extracts the variable name from "const NAME type = ..." or "mut NAME type = ..."
func extractVarName(line string) string {
	line = strings.TrimPrefix(line, "private ")
	line = strings.TrimPrefix(line, "const ")
	line = strings.TrimPrefix(line, "mut ")
	fields := strings.Fields(line)
	if len(fields) > 0 {
		return fields[0]
	}
	return ""
}

func collectDocsFromDir(dir string) []DocEntry {
	var entries []DocEntry
	absDir, err := filepath.Abs(dir)
	if err != nil {
		fmt.Printf("Error resolving path %s: %v\n", dir, err)
		return entries
	}
	files, err := os.ReadDir(absDir)
	if err != nil {
		fmt.Printf("Error reading directory %s: %v\n", dir, err)
		return entries
	}
	for _, file := range files {
		if !file.IsDir() && strings.HasSuffix(file.Name(), ".gray") {
			entries = append(entries, collectDocsFromFile(filepath.Join(absDir, file.Name()))...)
		}
	}
	return entries
}

func collectDocsRecursive(dir string) []DocEntry {
	var entries []DocEntry
	absDir, err := filepath.Abs(dir)
	if err != nil {
		fmt.Printf("Error resolving path %s: %v\n", dir, err)
		return entries
	}
	filepath.Walk(absDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		if !info.IsDir() && strings.HasSuffix(path, ".gray") {
			entries = append(entries, collectDocsFromFile(path)...)
		}
		return nil
	})
	return entries
}

func generateMarkdown(entries []DocEntry) string {
	var buf strings.Builder

	buf.WriteString("# Documentation\n\n")
	buf.WriteString("*Generated by `gray doc`*\n\n")

	var functions, structs, enums, variables []DocEntry
	for _, e := range entries {
		switch e.Kind {
		case "function":
			functions = append(functions, e)
		case "struct":
			structs = append(structs, e)
		case "enum":
			enums = append(enums, e)
		case "variable":
			variables = append(variables, e)
		}
	}

	sort.Slice(functions, func(i, j int) bool { return functions[i].Name < functions[j].Name })
	sort.Slice(structs, func(i, j int) bool { return structs[i].Name < structs[j].Name })
	sort.Slice(enums, func(i, j int) bool { return enums[i].Name < enums[j].Name })
	sort.Slice(variables, func(i, j int) bool { return variables[i].Name < variables[j].Name })

	if len(functions) > 0 {
		buf.WriteString("## Functions\n\n")
		for _, f := range functions {
			buf.WriteString(fmt.Sprintf("### %s\n\n", f.Name))
			buf.WriteString(fmt.Sprintf("```gray\n%s\n```\n\n", f.Signature))
			if f.Description != "" {
				buf.WriteString(f.Description + "\n\n")
			}
		}
	}

	if len(structs) > 0 {
		buf.WriteString("## Structs\n\n")
		for _, s := range structs {
			buf.WriteString(fmt.Sprintf("### %s\n\n", s.Name))
			buf.WriteString(fmt.Sprintf("```gray\n%s\n```\n\n", s.Signature))
			if s.Description != "" {
				buf.WriteString(s.Description + "\n\n")
			}
		}
	}

	if len(enums) > 0 {
		buf.WriteString("## Enums\n\n")
		for _, e := range enums {
			buf.WriteString(fmt.Sprintf("### %s\n\n", e.Name))
			buf.WriteString(fmt.Sprintf("```gray\n%s\n```\n\n", e.Signature))
			if e.Description != "" {
				buf.WriteString(e.Description + "\n\n")
			}
		}
	}

	if len(variables) > 0 {
		buf.WriteString("## Variables\n\n")
		for _, v := range variables {
			buf.WriteString(fmt.Sprintf("### %s\n\n", v.Name))
			buf.WriteString(fmt.Sprintf("```gray\n%s\n```\n\n", v.Signature))
			if v.Description != "" {
				buf.WriteString(v.Description + "\n\n")
			}
		}
	}

	return buf.String()
}
