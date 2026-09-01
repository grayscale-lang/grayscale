// test.go — `gray test`: discover #test functions, compile each source file
// with `grayc --test`, run the resulting binary, and render the results.
//
// The test binary speaks a small line protocol on stdout:
//
//	GRAYTEST PASS <name>
//	GRAYTEST FAIL <name>\t<file>:<line>\t<message>
//	GRAYTEST DONE <passed> <failed>
//
// Everything else the binary prints (a println inside a test, say) is passed
// straight through.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"

	"github.com/grayscale-lang/grayscale/internal/driver"
	"github.com/spf13/cobra"
)

// testAttrRe matches a line whose first non-blank content is the #test
// attribute — the cheap prescan that decides whether a file is worth compiling.
var testAttrRe = regexp.MustCompile(`(?m)^[ \t]*#test\b`)

var testCmd = &cobra.Command{
	Use:   "test [path...]",
	Short: "Compile and run #test functions",
	Long: `Compile and run every function marked with the #test attribute.

#test functions take no parameters, return nothing, and are stripped from
'gray build' / 'gray run' output — they exist only for 'gray test'.

Examples:
  gray test                Run tests in every .gray file under the current directory
  gray test file.gray      Run tests in a single file
  gray test ./src          Run tests in every .gray file under ./src

A failed assert (or any panic) inside a #test is reported as a test failure;
the runner continues with the remaining tests. Exit status is non-zero if any
test fails or any file fails to compile.`,
	Args: cobra.ArbitraryArgs,
	RunE: runTest,
}

func runTest(cmd *cobra.Command, args []string) error {
	noColor, _ := cmd.Flags().GetBool("no-color")
	c := newTestColors(noColor)

	files, err := collectTestFiles(args)
	if err != nil {
		return fmt.Errorf("error: %v", err)
	}
	if len(files) == 0 {
		fmt.Println("No #test functions found.")
		return nil
	}

	graycPath, err := driver.Find()
	if err != nil {
		return fmt.Errorf("error: %v", err)
	}

	tmpDir, err := os.MkdirTemp("", "gray-test-*")
	if err != nil {
		return fmt.Errorf("error: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	totalPass, totalFail := 0, 0
	buildFailed := false

	for i, file := range files {
		fmt.Printf("\n  %s\n", c.bold(file))

		binPath := filepath.Join(tmpDir, "t"+strconv.Itoa(i)+testExeSuffix())
		if out, err := buildTestBinary(graycPath, file, binPath); err != nil {
			buildFailed = true
			fmt.Printf("    %s could not compile\n", c.red("✗"))
			for _, line := range strings.Split(strings.TrimRight(out, "\n"), "\n") {
				if line != "" {
					fmt.Printf("      %s\n", line)
				}
			}
			continue
		}

		results, passthrough := runTestBinary(binPath)
		for _, line := range passthrough {
			fmt.Printf("      %s\n", c.dim(line))
		}
		for _, r := range results {
			if r.passed {
				fmt.Printf("    %s %s\n", c.green("✓"), r.name)
				totalPass++
			} else {
				fmt.Printf("    %s %s\n", c.red("✗"), r.name)
				totalFail++
			}
		}
		for _, r := range results {
			if r.passed {
				continue
			}
			fmt.Printf("\n  %s  %s:%s\n", c.red("FAIL"), file, r.name)
			fmt.Printf("    %s\n", r.message)
			if src := sourceLine(r.file, r.line); src != "" {
				fmt.Printf("      %s\n", c.dim(src))
			}
		}
	}

	fmt.Printf("\n  Results: %s, %s, %d total\n",
		c.green(fmt.Sprintf("%d passed", totalPass)),
		failWord(c, totalFail),
		totalPass+totalFail)

	if totalFail > 0 || buildFailed {
		return &ExitError{1}
	}
	return nil
}

// collectTestFiles resolves the path arguments to a de-duplicated list of
// .gray files that contain a #test attribute. No args means "recursively from
// the current directory".
func collectTestFiles(args []string) ([]string, error) {
	if len(args) == 0 {
		args = []string{"."}
	}
	seen := map[string]bool{}
	var files []string
	add := func(path string) {
		abs, err := filepath.Abs(path)
		if err != nil {
			abs = path
		}
		if seen[abs] {
			return
		}
		if !fileHasTest(path) {
			return
		}
		seen[abs] = true
		files = append(files, path)
	}

	for _, arg := range args {
		info, err := os.Stat(arg)
		if err != nil {
			return nil, err
		}
		if info.IsDir() {
			err := filepath.WalkDir(arg, func(p string, d os.DirEntry, err error) error {
				if err != nil {
					return err
				}
				if !d.IsDir() && strings.HasSuffix(p, ".gray") {
					add(p)
				}
				return nil
			})
			if err != nil {
				return nil, err
			}
			continue
		}
		if !strings.HasSuffix(arg, ".gray") {
			return nil, fmt.Errorf("'%s' is not a .gray file", arg)
		}
		add(arg)
	}
	return files, nil
}

func fileHasTest(path string) bool {
	data, err := os.ReadFile(path)
	if err != nil {
		return false
	}
	return testAttrRe.Match(data)
}

// buildTestBinary compiles file to binPath with `grayc build --test`. On
// success it returns nil; on failure it returns the compiler's combined
// output so the caller can surface the diagnostics.
func buildTestBinary(graycPath, file, binPath string) (string, error) {
	cmd := exec.Command(graycPath, "build", "--test", "--no-color", "-o", binPath, file)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return string(out), err
	}
	return "", nil
}

type testResult struct {
	name    string
	passed  bool
	message string
	file    string
	line    int
}

// runTestBinary executes the compiled test binary and parses its GRAYTEST
// protocol lines. Any other stdout line is returned as passthrough.
func runTestBinary(binPath string) (results []testResult, passthrough []string) {
	cmd := exec.Command(binPath)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, []string{"error: " + err.Error()}
	}
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return nil, []string{"error: " + err.Error()}
	}

	scanner := bufio.NewScanner(stdout)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	sawDone := false
	for scanner.Scan() {
		line := scanner.Text()
		switch {
		case strings.HasPrefix(line, "GRAYTEST PASS "):
			results = append(results, testResult{
				name:   strings.TrimPrefix(line, "GRAYTEST PASS "),
				passed: true,
			})
		case strings.HasPrefix(line, "GRAYTEST FAIL "):
			results = append(results, parseFailLine(strings.TrimPrefix(line, "GRAYTEST FAIL ")))
		case strings.HasPrefix(line, "GRAYTEST DONE "):
			sawDone = true
		default:
			passthrough = append(passthrough, line)
		}
	}
	err = cmd.Wait()

	// The runner exited without printing its trailer — it crashed hard
	// (a signal the setjmp recovery can't catch, e.g. a stack overflow) part
	// way through. Surface that instead of silently dropping tests.
	if !sawDone {
		results = append(results, testResult{
			name:    "(test runner)",
			passed:  false,
			message: "test binary exited abnormally: " + waitErrString(err),
		})
	}
	return results, passthrough
}

func waitErrString(err error) string {
	if err == nil {
		return "no result trailer"
	}
	return err.Error()
}

// parseFailLine parses "<name>\t<file>:<line>\t<message>".
func parseFailLine(s string) testResult {
	r := testResult{passed: false, message: "test failed"}
	parts := strings.SplitN(s, "\t", 3)
	r.name = parts[0]
	if len(parts) >= 2 {
		if idx := strings.LastIndex(parts[1], ":"); idx >= 0 {
			r.file = parts[1][:idx]
			r.line, _ = strconv.Atoi(parts[1][idx+1:])
		} else {
			r.file = parts[1]
		}
	}
	if len(parts) >= 3 && parts[2] != "" {
		r.message = parts[2]
	}
	return r
}

// sourceLine returns the trimmed contents of file:line, or "" if unavailable.
func sourceLine(file string, line int) string {
	if file == "" || line <= 0 {
		return ""
	}
	data, err := os.ReadFile(file)
	if err != nil {
		return ""
	}
	lines := strings.Split(string(data), "\n")
	if line > len(lines) {
		return ""
	}
	return strings.TrimSpace(lines[line-1])
}

func testExeSuffix() string {
	if runtime.GOOS == "windows" {
		return ".exe"
	}
	return ""
}

// --- tiny color helper (honors --no-color) ---

type testColors struct{ enabled bool }

func newTestColors(noColor bool) testColors { return testColors{enabled: !noColor} }

func (c testColors) wrap(code, s string) string {
	if !c.enabled {
		return s
	}
	return "\033[" + code + "m" + s + "\033[0m"
}
func (c testColors) bold(s string) string  { return c.wrap("1", s) }
func (c testColors) green(s string) string { return c.wrap("32", s) }
func (c testColors) red(s string) string   { return c.wrap("31", s) }
func (c testColors) dim(s string) string   { return c.wrap("2", s) }

func failWord(c testColors, n int) string {
	s := fmt.Sprintf("%d failed", n)
	if n > 0 {
		return c.red(s)
	}
	return s
}
