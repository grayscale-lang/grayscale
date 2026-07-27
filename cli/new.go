// new.go — Project scaffolding command ("gray new"). Creates new Grayscale
// projects from embedded templates with optional quick-reference comments.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"bufio"
	"embed"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
)

//go:embed templates
var templatesFS embed.FS

var errNewCancelled = fmt.Errorf("new cancelled")

// quickRefBlock is prepended to the entry file of a scaffolded project
// when `gray new -c` is used. Keep it short — a dense Grayscale cheat sheet,
// not a tutorial.
const quickRefBlock = `// Grayscale Quick Reference
// ---------------------
// Variables:        mut x int = 5          mut x = 5          (inferred)
// Constants:        const PI float = 3.14
// Strings:          "Hello, ${name}!"      (interpolation; + is not supported)
// Functions:        do greet(name string) -> string { return "Hi ${name}" }
// Multi-return:     do swap(a int, b int) -> (x int, y int) { return b, a }
// If/else:          if x > 0 { } or x == 0 { } otherwise { }
// Loops:            for i in range(0, 10) { }    for_each item in items { }
// Structs:          const Point struct { x int, y int }
//                   mut p = Point{ x: 1, y: 2 }
// Imports:          import @os                   import "./sibling"
//
`

var newCmd = &cobra.Command{
	Use:   "new [project-name]",
	Short: "Scaffold a new Grayscale project",
	Long: `Scaffold a new Grayscale project with templates.

Examples:
  gray new                      Interactive mode
  gray new myproject            Create with basic template
  gray new myproject -t cli     Create with cli template
  gray new myproject -t lib -c  Create library with comments

Templates:
  basic  - Single file hello world (default)
  cli    - CLI application with arg handling
  lib    - Reusable library module
  multi  - Multi-module project
  server - HTTP server (use -s for minimal/normal)
  client - HTTP client (use -s for minimal/normal)`,
	Args: cobra.MaximumNArgs(1),
	RunE: runNew,
}

func runNew(cmd *cobra.Command, args []string) error {
	template, _ := cmd.Flags().GetString("template")
	comments, _ := cmd.Flags().GetBool("comments")
	force, _ := cmd.Flags().GetBool("force")
	serverType, _ := cmd.Flags().GetString("server-type")

	var name string

	if len(args) == 0 {
		name = promptForInput("Project name: ", "")
		if name == "" {
			return fmt.Errorf("error: project name is required")
		}

		if !cmd.Flags().Changed("template") {
			template = promptForInput("Template (basic/cli/lib/multi/server/client) [basic]: ", "basic")
			if template == "" {
				template = "basic"
			}
		}

		if (template == "server" || template == "client") && !cmd.Flags().Changed("server-type") {
			serverType = promptForInput("Type (minimal/normal) [normal]: ", "normal")
			if serverType == "" {
				serverType = "normal"
			}
		}

		if !cmd.Flags().Changed("comments") {
			commentsStr := promptForInput("Include comments? (y/N): ", "n")
			comments = strings.ToLower(commentsStr) == "y" || strings.ToLower(commentsStr) == "yes"
		}
	} else {
		name = args[0]
	}

	if !isValidProjectName(name) {
		return fmt.Errorf("error: project name must be a plain directory name with no path separators, '..' sequences, or absolute paths")
	}

	validTemplates := map[string]bool{"basic": true, "cli": true, "lib": true, "multi": true, "server": true, "client": true}
	if !validTemplates[template] {
		return fmt.Errorf("Invalid template '%s'. Choose from: basic, cli, lib, multi, server, client", template)
	}

	if template == "server" || template == "client" {
		validSubTypes := map[string]bool{"minimal": true, "normal": true}
		if !validSubTypes[serverType] {
			return fmt.Errorf("Invalid type '%s'. Choose from: minimal, normal", serverType)
		}
	}

	if err := createProject(name, template, comments, force, serverType); err != nil {
		if err == errNewCancelled {
			return nil
		}
		return fmt.Errorf("Error: %v", err)
	}

	switch template {
	case "server":
		fmt.Printf("\nDone! Run your server:\n")
		fmt.Printf("  cd %s && gray main.gray\n", name)
		fmt.Printf("  # Then visit http://localhost:8080\n")
	case "client":
		fmt.Printf("\nDone! Run your client:\n")
		fmt.Printf("  cd %s && gray main.gray\n", name)
	default:
		fmt.Printf("\nDone! Run your project:\n")
		fmt.Printf("  cd %s && gray main.gray\n", name)
	}
	return nil
}

// isValidProjectName returns true only when name is a plain single-element
// directory name safe for use across OS and shell environments. Only ASCII
// letters, digits, hyphens, and underscores are allowed, and the name must
// start with a letter. Names containing path separators, parent references,
// absolute paths, or home-directory shortcuts are also rejected.
func isValidProjectName(name string) bool {
	if name == "" {
		return false
	}
	if filepath.IsAbs(name) || strings.HasPrefix(name, "~") {
		return false
	}
	if strings.ContainsAny(name, "/\\") || strings.Contains(name, "..") {
		return false
	}
	// Must start with a letter
	if !((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')) {
		return false
	}
	// Only allow [a-zA-Z0-9_-]
	for _, ch := range name {
		if !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
			return false
		}
	}
	cleaned := filepath.Clean(name)
	return cleaned == name && cleaned != "." && cleaned != ".."
}

func promptForInput(prompt, defaultVal string) string {
	fmt.Print(prompt)
	reader := bufio.NewReader(os.Stdin)
	input, _ := reader.ReadString('\n')
	input = strings.TrimSpace(input)
	if input == "" {
		return defaultVal
	}
	return input
}

func createProject(name, template string, comments, force bool, serverType string) error {
	if _, err := os.Stat(name); err == nil {
		if !force {
			return fmt.Errorf("directory '%s' already exists (use --force to overwrite)", name)
		}
		fmt.Printf("Directory '%s' already exists and will be permanently deleted.\n", name)
		fmt.Printf("Delete '%s/' and continue? (y/N): ", name)
		reader := bufio.NewReader(os.Stdin)
		response, _ := reader.ReadString('\n')
		response = strings.TrimSpace(strings.ToLower(response))
		if response != "y" && response != "yes" {
			fmt.Println("Cancelled.")
			return errNewCancelled
		}
		os.RemoveAll(name)
	}

	fmt.Printf("\nCreating project '%s' with template '%s'...\n", name, template)

	if err := os.MkdirAll(name, 0755); err != nil {
		return err
	}
	fmt.Printf("  created %s/\n", name)

	// Resolve which template subtree to walk, and which file (if any)
	// receives a name-based rename at scaffold time.
	src, entry, rename := resolveTemplate(template, serverType, name)

	return extractTemplate(src, name, entry, rename, comments)
}

// resolveTemplate maps a (template, serverType) pair to the embedded
// template subdirectory, the path of the entry file within that subtree
// (used by the -c flag and for name substitution), and an optional
// {oldPath: newPath} rename applied during extraction.
func resolveTemplate(template, serverType, projectName string) (src, entry string, rename map[string]string) {
	switch template {
	case "basic":
		return "templates/basic", "main.gray", nil
	case "cli":
		return "templates/cli", "main.gray", nil
	case "lib":
		return "templates/lib", "main.gray", nil
	case "multi":
		return "templates/multi", "main.gray", nil
	case "server":
		if serverType == "minimal" {
			return "templates/server_minimal", "main.gray", nil
		}
		return "templates/server_normal", "main.gray", nil
	case "client":
		if serverType == "minimal" {
			return "templates/client_minimal", "main.gray", nil
		}
		return "templates/client_normal", "main.gray", nil
	}
	return "", "", nil
}

// extractTemplate walks the embedded template subtree rooted at src and
// materialises it into destRoot, applying any rename and prepending the
// quick-reference comment block to entryFile when comments is set.
func extractTemplate(src, destRoot, entryFile string, rename map[string]string, comments bool) error {
	return fs.WalkDir(templatesFS, src, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if path == src {
			return nil
		}

		rel, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		if newName, ok := rename[rel]; ok {
			rel = newName
		}
		dest := filepath.Join(destRoot, rel)

		if d.IsDir() {
			if err := os.MkdirAll(dest, 0755); err != nil {
				return err
			}
			fmt.Printf("  created %s/\n", dest)
			return nil
		}

		data, err := templatesFS.ReadFile(path)
		if err != nil {
			return err
		}

		if comments && rel == entryFile {
			data = append([]byte(quickRefBlock), data...)
		}

		if err := os.WriteFile(dest, data, 0644); err != nil {
			return err
		}
		fmt.Printf("  created %s\n", dest)
		return nil
	})
}
