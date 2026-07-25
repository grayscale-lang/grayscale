// commands.go — Defines all Cobra subcommands (build, check, update, doc,
// fmt, new, watch, man, report, verify, version) and the root command wiring.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"sort"
	"strings"

	"github.com/grayscale-lang/grayscale/internal/driver"
	"github.com/spf13/cobra"
)

// ExitError carries a process exit code through the error return chain
// so that main() can forward it to os.Exit without every call site
// reaching for os.Exit directly.
type ExitError struct{ Code int }

func (e *ExitError) Error() string { return fmt.Sprintf("exit status %d", e.Code) }

var checkCmd = &cobra.Command{
	Use:   "check [file.gray | directory]",
	Short: "Type-check a file or project without compiling",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		// Allow directories for project-wide check
		info, statErr := os.Stat(args[0])
		isDir := statErr == nil && info.IsDir()
		if !isDir && !strings.HasSuffix(args[0], ".gray") {
			return fmt.Errorf("error: '%s' is not a valid Grayscale source file — expected a .gray file", args[0])
		}
		var extraArgs []string
		quiet, _ := cmd.Flags().GetString("quiet")
		if quiet == "all" {
			extraArgs = append(extraArgs, "--quiet")
		} else if quiet != "" {
			extraArgs = append(extraArgs, "--quiet", quiet)
		}
		code, err := driver.Check(args[0], extraArgs)
		if err != nil {
			return fmt.Errorf("error: %v", err)
		}
		if code != 0 {
			return &ExitError{code}
		}
		return nil
	},
}

var buildCmd = &cobra.Command{
	Use:   "build [file.gray]",
	Short: "Compile a Grayscale source file to a native binary",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		if !strings.HasSuffix(args[0], ".gray") {
			return fmt.Errorf("error: '%s' is not a valid Grayscale source file — expected a .gray file", args[0])
		}
		output, _ := cmd.Flags().GetString("output")
		verbose, _ := cmd.Flags().GetBool("verbose")
		emitC, _ := cmd.Flags().GetBool("emit-c")
		quiet, _ := cmd.Flags().GetString("quiet")
		showTime, _ := cmd.Flags().GetBool("time")
		noColor, _ := cmd.Flags().GetBool("no-color")

		opts := driver.BuildOpts{
			Output:  output,
			Verbose: verbose,
			EmitC:   emitC,
			Time:    showTime,
			NoColor: noColor,
		}
		if quiet == "all" {
			opts.Quiet = true
		} else if quiet != "" {
			opts.QuietCodes = quiet
		}
		code, err := driver.Build(args[0], opts)
		if err != nil {
			return fmt.Errorf("error: %v", err)
		}
		if code != 0 {
			return &ExitError{code}
		}
		return nil
	},
}

var installCmd = &cobra.Command{
	Use:   "install <version>",
	Short: "Install a specific Grayscale version, replacing the current install",
	Long: "Install a specific Grayscale version by exact semver, replacing the current " +
		"install. Downgrades and pre-release versions are supported.\n\n" +
		"Examples:\n" +
		"  gray install 2.5.0\n" +
		"  gray install 3.0.0-beta.2",
	Args: cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		return runInstall(args[0])
	},
}

var updateCmd = &cobra.Command{
	Use:   "update",
	Short: "Check for updates and upgrade Grayscale",
	Long: "Check for updates and upgrade Grayscale.\n\n" +
		"Without flags, installs the latest stable release and prints a note " +
		"if a newer pre-release is available.\n\n" +
		"With --pre, installs the latest pre-release (alpha, beta, or rc).",
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		confirm, _ := cmd.Flags().GetBool("confirm")
		pre, _ := cmd.Flags().GetBool("pre")
		var url string
		if len(args) > 0 {
			url = args[0]
		}
		return runUpdate(confirm, url, pre)
	},
}

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "Show version information",
	Run: func(cmd *cobra.Command, args []string) {
		rootCmd.Printf("%s\n", getVersionString())
	},
}

var docCmd = &cobra.Command{
	Use:   "doc <path>",
	Short: "Generate documentation from #doc attributes",
	Long: `Generate markdown documentation from #doc attributes in Grayscale source files.

Examples:
  gray doc .              Generate docs for current directory (no recursion)
  gray doc ./...          Generate docs recursively from current directory
  gray doc ./src          Generate docs for src directory only
  gray doc ./src/...      Generate docs recursively from src directory
  gray doc file.gray        Generate docs for a single file
  gray doc a.gray b.gray      Generate docs for multiple files

Output is written to DOCS.md by default. Use -o/--output to write
to a different path (parent directories are created as needed).`,
	Args: cobra.MinimumNArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		output, _ := cmd.Flags().GetString("output")
		generateDocs(args, output)
	},
}

var fmtCmd = &cobra.Command{
	Use:   "fmt <path>",
	Short: "Format .gray source files",
	Long: `Normalize formatting of .gray source files (indentation, trailing
whitespace, end-of-file newline, blank-line runs).

Examples:
  gray fmt .              Format .gray files in current directory (no recursion)
  gray fmt ./...          Format recursively from current directory
  gray fmt file.gray        Format a single file
  gray fmt a.gray b.gray      Format multiple files
  gray fmt --check ./...  Exit non-zero if any file would change (CI gate)

By default files are rewritten in place. Use --check for a non-mutating CI gate.`,
	Args: cobra.MinimumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		check, _ := cmd.Flags().GetBool("check")
		exit := runFmt(args, check)
		if exit != 0 {
			return &ExitError{exit}
		}
		return nil
	},
}

var reportCmd = &cobra.Command{
	Use:   "report",
	Short: "Print system info for bug reports",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Grayscale Bug Report Info")
		fmt.Println("======================")

		vi := GetVersionInfo()
		channel := "stable"
		switch vi.Channel {
		case "pre-release":
			channel = "pre-release"
		case "dev":
			channel = "dev"
		}
		fmt.Printf("Grayscale Version:  %s  (%s)\n", vi.Display, channel)

		if vi.Commit == "" {
			fmt.Println("Commit:      (released build)")
		} else {
			fmt.Printf("Commit:      %s\n", vi.Commit)
		}

		fmt.Printf("Install:     %s\n", reportInstallPath())

		fmt.Printf("OS:          %s/%s  %s\n", runtime.GOOS, runtime.GOARCH, reportKernel())
		fmt.Printf("CPU:         %s\n", reportCPUModel())

		memCmd := exec.Command("sysctl", "-n", "hw.memsize")
		if runtime.GOOS == "linux" {
			memCmd = exec.Command("sh", "-c", "grep MemTotal /proc/meminfo | awk '{print $2 * 1024}'")
		}
		if out, err := memCmd.Output(); err == nil {
			s := strings.TrimSpace(string(out))
			var bytes uint64
			fmt.Sscanf(s, "%d", &bytes)
			gb := float64(bytes) / (1024 * 1024 * 1024)
			fmt.Printf("RAM:         %.0f GB\n", gb)
		} else {
			fmt.Printf("RAM:         unknown\n")
		}

		cc, ccVer, ccTriple := reportCCompiler()
		fmt.Printf("C compiler:  %s\n", cc)
		if ccVer != "" {
			fmt.Printf("             %s\n", ccVer)
		}
		if ccTriple != "" {
			fmt.Printf("             target: %s\n", ccTriple)
		}
	},
}

// reportInstallPath returns the path to the running gray binary with $HOME
// redacted to ~ so the output is safe to paste into a public bug report.
func reportInstallPath() string {
	exe, err := os.Executable()
	if err != nil {
		return "unknown"
	}
	if home, err := os.UserHomeDir(); err == nil && home != "" && strings.HasPrefix(exe, home) {
		return "~" + strings.TrimPrefix(exe, home)
	}
	return exe
}

// reportKernel returns `uname -sr` output (e.g. "Darwin 25.5.0").
func reportKernel() string {
	out, err := exec.Command("uname", "-sr").Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

// reportCPUModel returns the human-readable CPU brand string, or empty.
func reportCPUModel() string {
	switch runtime.GOOS {
	case "darwin":
		if out, err := exec.Command("sysctl", "-n", "machdep.cpu.brand_string").Output(); err == nil {
			return strings.TrimSpace(string(out))
		}
	case "linux":
		if out, err := exec.Command("sh", "-c", "grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-").Output(); err == nil {
			return strings.TrimSpace(string(out))
		}
	}
	return "unknown"
}

// reportCCompiler resolves the C compiler grayc will actually invoke
// (honouring $CC, else the first of clang/gcc/cc on PATH) and returns
// its resolved path, first line of --version output, and target triple.
func reportCCompiler() (path, version, triple string) {
	cc := os.Getenv("CC")
	if cc == "" {
		for _, candidate := range []string{"clang", "gcc", "cc"} {
			if p, err := exec.LookPath(candidate); err == nil {
				cc = p
				break
			}
		}
	}
	if cc == "" {
		return "not found", "", ""
	}
	if resolved, err := exec.LookPath(cc); err == nil {
		path = resolved
	} else {
		path = cc
	}
	if out, err := exec.Command(cc, "--version").Output(); err == nil {
		if line := strings.SplitN(strings.TrimSpace(string(out)), "\n", 2)[0]; line != "" {
			version = line
		}
	}
	if out, err := exec.Command(cc, "-dumpmachine").Output(); err == nil {
		triple = strings.TrimSpace(string(out))
	}
	if home, err := os.UserHomeDir(); err == nil && home != "" && strings.HasPrefix(path, home) {
		path = "~" + strings.TrimPrefix(path, home)
	}
	return
}

func printManUsage() {
	fmt.Println("gray man — builtin, stdlib, and language reference documentation")
	fmt.Println()
	fmt.Println("Usage:")
	fmt.Println("  gray man builtins          list all builtins")
	fmt.Println("  gray man <module>          list all items in a stdlib module")
	fmt.Println("                           (e.g. gray man math)")
	fmt.Println("  gray man <name>            docs for a specific function, type, or constant")
	fmt.Println("                           (e.g. gray man println, gray man sqrt, gray man PI)")
	fmt.Println("  gray man <name()>          same — trailing () is ignored")
	fmt.Println("  gray man <module>.<name>   qualified lookup to avoid ambiguity")
	fmt.Println("                           (e.g. gray man strings.contains, gray man math.PI)")
	fmt.Println()
	fmt.Println("Currently Supported Stdlib Modules:")
	mods := make([]string, 0, len(stdlibModules))
	for m := range stdlibModules {
		mods = append(mods, m)
	}
	sort.Strings(mods)
	fmt.Printf("  %s\n", strings.Join(mods, "  "))
	fmt.Println()
	fmt.Println("Language Reference:")
	fmt.Println("  gray man lang              overview of all language reference categories")
	fmt.Println("  gray man keywords          list all keywords")
	fmt.Println("  gray man types             list all types")
	fmt.Println("  gray man symbols           list all symbols")
	fmt.Println("  gray man attributes        list all attributes")
	fmt.Println()
	fmt.Println("  Note: for attributes, omit the # prefix (e.g. gray man flags, not gray man #flags)")
	fmt.Println("  Note: for functions, omit the () suffix (e.g. gray man math.sqrt, not gray man math.sqrt())")
	fmt.Println()
	fmt.Println("See the full language standard: https://github.com/grayscale-lang/grayscale/blob/main/STANDARD.md")
}

func printBuiltinsIndex() {
	fmt.Println("Builtins  (gray man <name> for details)")
	fmt.Println(strings.Repeat("─", 50))
	groups := []struct {
		label string
		names []string
	}{
		{"I/O        ", []string{"println", "print", "eprintln", "eprint", "input"}},
		{"Control    ", []string{"exit", "panic", "assert"}},
		{"Sleep      ", []string{"sleep_s", "sleep_ms", "sleep_ns"}},
		{"Type casts ", []string{"int", "uint", "float", "string", "char", "byte", "bool", "cast"}},
		{"Width casts", []string{"i128", "u128", "i256", "u256"}},
		{"Memory     ", []string{"new", "ref", "addr", "copy"}},
		{"Introspect ", []string{"len", "type_of", "size_of"}},
		{"Misc       ", []string{"error", "range", "c_string", "to_char", "char_count", "here"}},
		{"Types      ", []string{"SourceLocation", "Error"}},
	}
	for _, g := range groups {
		var labels []string
		for _, n := range g.names {
			if e, ok := builtinManDocs[n]; ok && e.Kind == "func" {
				labels = append(labels, n+"()")
			} else {
				labels = append(labels, n)
			}
		}
		fmt.Printf("  %s  %s\n", g.label, strings.Join(labels, "  "))
	}
}

func printManEntry(module, name, kind, sig, fields, desc, example string) {
	label := name
	if kind == "func" && strings.Contains(sig, "(") {
		label = name + "()"
	}
	fmt.Printf("%s: %s\n", module, label)
	fmt.Println(strings.Repeat("─", 44))
	fmt.Printf("Module:  %s\n", module)
	if kind == "type" {
		fmt.Printf("Kind:    type\n")
		if fields != "" {
			fmt.Println("\nFields:")
			for _, f := range strings.Split(fields, "\n") {
				parts := strings.Fields(f)
				if len(parts) >= 2 {
					fmt.Printf("  %-12s %s\n", parts[0], parts[1])
				}
			}
		}
		fmt.Printf("\n%s\n", desc)
	} else if kind == "const" {
		fmt.Printf("Kind:    constant\n")
		if sig != "" {
			fmt.Printf("Value:   %s\n", sig)
		}
		fmt.Printf("\n%s\n", desc)
	} else {
		fmt.Printf("Kind:    function\n")
		fmt.Printf("Signature:  %s\n\n", sig)
		fmt.Println(desc)
	}
	if example != "" {
		fmt.Println("\nExample:")
		for _, line := range strings.Split(example, "\n") {
			fmt.Printf("  %s\n", line)
		}
	}
}

func printBuiltinEntry(name string, entry BuiltinManEntry) {
	printManEntry("builtin", name, entry.Kind, entry.Sig, entry.Fields, entry.Desc, entry.Example)
}

func printStdlibEntry(name string, entry StdlibManEntry) {
	printManEntry(entry.Module, name, entry.Kind, entry.Sig, entry.Fields, entry.Desc, entry.Example)
}

func printStdlibModuleIndex(module string) error {
	groups, ok := stdlibModuleGroups[module]
	if !ok {
		return fmt.Errorf("gray: no documentation for module '%s'\n    try: gray man\n    See the full language standard: https://github.com/grayscale-lang/grayscale/blob/main/STANDARD.md", module)
	}
	fmt.Printf("module: %s  (gray man <name> for details)\n", module)
	fmt.Println(strings.Repeat("─", 50))
	for _, g := range groups {
		var labels []string
		for _, n := range g.Names {
			key := module + "." + n
			if e, ok := stdlibManDocs[key]; ok && e.Kind == "func" && strings.Contains(e.Sig, "(") {
				labels = append(labels, n+"()")
			} else {
				labels = append(labels, n)
			}
		}
		fmt.Printf("  %s  %s\n", g.Label, strings.Join(labels, "  "))
	}
	fmt.Println()
	fmt.Println("  Tip: omit the () when looking up functions (e.g. gray man " + module + "." + groups[0].Names[0] + ")")
	return nil
}

func printLangIndex() {
	fmt.Println("Language Reference  (gray man <category> for details)")
	fmt.Println(strings.Repeat("─", 50))
	for _, cat := range []string{"keywords", "types", "symbols", "attributes"} {
		groups := langCategories[cat]
		var allNames []string
		for _, g := range groups {
			allNames = append(allNames, g.Names...)
		}
		fmt.Printf("  %-14s %s\n", cat, strings.Join(allNames, "  "))
	}
}

func printLangCategoryIndex(category string) error {
	groups, ok := langCategories[category]
	if !ok {
		return fmt.Errorf("gray: no language reference category '%s'\n    try: gray man lang\n    See the full language standard: https://github.com/grayscale-lang/grayscale/blob/main/STANDARD.md", category)
	}
	fmt.Printf("lang: %s  (gray man <name> for details)\n", category)
	fmt.Println(strings.Repeat("─", 50))
	for _, g := range groups {
		fmt.Printf("  %s  %s\n", g.Label, strings.Join(g.Names, "  "))
	}
	if category == "attributes" {
		fmt.Println()
		fmt.Println("  Tip: omit the # when looking up attributes (e.g. gray man flags)")
	}
	return nil
}

func printLangEntry(displayName string, entry LangManEntry) {
	fmt.Printf("lang: %s\n", displayName)
	fmt.Println(strings.Repeat("─", 46))
	fmt.Printf("Kind:    %s\n", entry.Kind)
	fmt.Printf("Syntax:  %s\n", entry.Syntax)
	fmt.Printf("\n%s\n", entry.Desc)
	if entry.Example != "" {
		fmt.Println("\nExample:")
		for _, line := range strings.Split(entry.Example, "\n") {
			fmt.Printf("  %s\n", line)
		}
	}
}

var manCmd = &cobra.Command{
	Use:   "man [name]",
	Short: "Show documentation for a builtin or stdlib function",
	Args:  cobra.ArbitraryArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		if len(args) == 0 {
			printManUsage()
			return nil
		}

		name := strings.TrimSuffix(args[0], "()")

		if name == "builtins" || name == "builtin" {
			printBuiltinsIndex()
			return nil
		}

		// Language reference index and categories
		if name == "lang" || name == "language" {
			printLangIndex()
			return nil
		}
		if name == "keywords" || name == "types" || name == "symbols" || name == "attributes" {
			return printLangCategoryIndex(name)
		}

		// Module-level index (e.g. gray man math)
		if _, isMod := stdlibModules[name]; isMod {
			return printStdlibModuleIndex(name)
		}

		// Builtin lookup
		if entry, ok := builtinManDocs[name]; ok {
			printBuiltinEntry(name, entry)
			return nil
		}

		// Stdlib function/type lookup — supports "module.func" and plain "func"
		if entry, ok := stdlibManDocs[name]; ok {
			displayName := name
			if idx := strings.LastIndex(name, "."); idx >= 0 {
				displayName = name[idx+1:]
			}
			printStdlibEntry(displayName, entry)
			return nil
		}
		// Plain name: scan all module.func keys for a match
		var matchEntries []StdlibManEntry
		var matchKeys []string
		for k, e := range stdlibManDocs {
			if strings.HasSuffix(k, "."+name) {
				matchEntries = append(matchEntries, e)
				matchKeys = append(matchKeys, k)
			}
		}
		if len(matchEntries) == 1 {
			printStdlibEntry(name, matchEntries[0])
			return nil
		}
		if len(matchEntries) > 1 {
			var sb strings.Builder
			fmt.Fprintf(&sb, "gray: '%s' exists in multiple modules. Use a qualified name:", name)
			for _, k := range matchKeys {
				fmt.Fprintf(&sb, "\n    gray man %s", k)
			}
			return fmt.Errorf("%s", sb.String())
		}

		// Language reference lookup — direct key
		if entry, ok := langManDocs[name]; ok {
			printLangEntry(langDisplayName(name), entry)
			return nil
		}
		// Language reference lookup — type suffix fallback (e.g. "i8" -> "i8_type")
		if entry, ok := langManDocs[name+"_type"]; ok {
			printLangEntry(name, entry)
			return nil
		}
		// Language reference lookup — symbol alias (e.g. "pointer" -> "^")
		if target, ok := langSymbolAliases[name]; ok {
			if entry, ok := langManDocs[target]; ok {
				printLangEntry(target, entry)
				return nil
			}
		}

		return fmt.Errorf("gray: no documentation for '%s'\n    try: gray man builtins  or  gray man lang\n    See the full language standard: https://github.com/grayscale-lang/grayscale/blob/main/STANDARD.md", name)
	},
}

var rootCmd = &cobra.Command{
	Use:   "gray [file.gray]",
	Short: "Programming On Your Terms",
	Long:  "Grayscale — A compiled language where simplicity meets flexibility.",
	Args:  cobra.ArbitraryArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		if len(args) == 0 {
			fmt.Printf("%sVersion: %s\n\nRun 'gray help' for usage.\n", asciiBanner, Version)
			return nil
		}
		if !strings.HasSuffix(args[0], ".gray") {
			return fmt.Errorf("error: unknown command or invalid file '%s' — expected a .gray file\n  usage: gray <file.gray>\n  help:  gray --help", args[0])
		}

		// Pass extra args through to the compiled program
		var extraArgs []string
		if cmd.ArgsLenAtDash() > 0 {
			extraArgs = args[cmd.ArgsLenAtDash():]
		} else if len(args) > 1 {
			extraArgs = args[1:]
		}

		// Prepend compiler flags (before program args)
		var compilerArgs []string
		quiet, _ := cmd.Flags().GetString("quiet")
		if quiet == "all" {
			compilerArgs = append(compilerArgs, "--quiet")
		} else if quiet != "" {
			compilerArgs = append(compilerArgs, "--quiet", quiet)
		}
		if noColor, _ := cmd.Flags().GetBool("no-color"); noColor {
			compilerArgs = append(compilerArgs, "--no-color")
		}
		compilerArgs = append(compilerArgs, extraArgs...)

		code, err := driver.Run(args[0], compilerArgs)
		if err != nil {
			return fmt.Errorf("error: %v", err)
		}
		if code != 0 {
			return &ExitError{code}
		}
		return nil
	},
}

func init() {
	rootCmd.CompletionOptions.DisableDefaultCmd = true
	rootCmd.SilenceUsage = true
	rootCmd.SilenceErrors = true
	rootCmd.AddCommand(updateCmd, installCmd, checkCmd, buildCmd, reportCmd, versionCmd, docCmd, fmtCmd, newCmd, watchCmd, manCmd, verifyCmd)
	rootCmd.PersistentPreRun = func(cmd *cobra.Command, args []string) {
		CheckForUpdateAsync()
	}
	// Custom help for root only: hide -h (users have `gray help`) and -v (use `gray version`)
	defaultHelp := rootCmd.HelpFunc()
	rootCmd.SetHelpFunc(func(cmd *cobra.Command, args []string) {
		if cmd != rootCmd {
			defaultHelp(cmd, args)
			return
		}
		fmt.Fprintf(cmd.OutOrStdout(), `%s

Usage:
  gray [file.gray] [flags]
  gray [command]

Available Commands:
`, cmd.Long)
		for _, c := range cmd.Commands() {
			if c.IsAvailableCommand() || c.Name() == "help" {
				fmt.Fprintf(cmd.OutOrStdout(), "  %-12s%s\n", c.Name(), c.Short)
			}
		}
		fmt.Fprintf(cmd.OutOrStdout(), `
Flags:
  -q, --quiet string   Suppress warnings ('all' or comma-separated codes like W1001,W1002)
      --no-color       Disable colored output

Use "gray [command] --help" for more information about a command.
See the full language standard: https://github.com/grayscale-lang/grayscale/blob/main/STANDARD.md
`)
	})
	updateCmd.Flags().Bool("confirm", false, "")
	_ = updateCmd.Flags().MarkHidden("confirm")
	updateCmd.Flags().Bool("pre", false, "Install the latest pre-release (alpha/beta/rc) instead of the latest stable")

	buildCmd.Flags().StringP("output", "o", "", "Output binary name")
	buildCmd.Flags().BoolP("verbose", "v", false, "Show compilation commands")
	buildCmd.Flags().MarkHidden("verbose")
	buildCmd.Flags().Bool("emit-c", false, "Emit generated C source to a file (no binary). Uses -o for output path, or defaults to <input>.c")
	buildCmd.Flags().Bool("time", false, "Show compilation timing")
	buildCmd.Flags().Bool("no-color", false, "Disable colored output")
	rootCmd.Flags().StringP("quiet", "q", "", "Suppress warnings (use 'all' or comma-separated codes like W1001,W1002)")
	rootCmd.Flags().Bool("no-color", false, "Disable colored output")
	buildCmd.Flags().StringP("quiet", "q", "", "Suppress warnings (use 'all' or comma-separated codes like W1001,W1002)")
	checkCmd.Flags().StringP("quiet", "q", "", "Suppress warnings (use 'all' or comma-separated codes like W1001,W1002)")
	watchCmd.Flags().StringP("quiet", "q", "", "Suppress warnings (use 'all' or comma-separated codes like W1001,W1002)")

	docCmd.Flags().StringP("output", "o", defaultDocOutputPath, "Path to write generated markdown")

	fmtCmd.Flags().Bool("check", false, "Exit non-zero if any file would change; don't modify files")

	newCmd.Flags().StringP("template", "t", "basic", "Template: basic, cli, lib, multi, server, client")
	newCmd.Flags().BoolP("comments", "c", false, "Include helpful syntax comments")
	newCmd.Flags().BoolP("force", "f", false, "Overwrite existing directory")
	newCmd.Flags().StringP("server-type", "s", "normal", "Server template type: minimal, normal")
}
