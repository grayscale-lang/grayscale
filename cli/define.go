// define.go — The -define:NAME=VALUE flag. cobra has no single-dash long
// flag with a colon, so main() lifts these arguments out of os.Args before
// cobra sees them, and the commands that compile a program forward them to
// grayc as --define=NAME=VALUE.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"fmt"
	"strings"
)

const defineFlagPrefix = "-define:"

// defineOverrides holds the NAME=VALUE pairs of every -define: argument, in
// command-line order.
var defineOverrides []string

// extractDefineFlags splits the -define:NAME=VALUE arguments out of args.
// Arguments after a bare "--" belong to the compiled program and are left
// alone. A flag without '=' or with an empty name, or a name given twice, is
// a usage error.
func extractDefineFlags(args []string) (rest []string, defines []string, err error) {
	seen := map[string]bool{}
	afterDash := false
	for _, arg := range args {
		if arg == "--" {
			afterDash = true
		}
		if afterDash || !strings.HasPrefix(arg, defineFlagPrefix) {
			rest = append(rest, arg)
			continue
		}
		override := strings.TrimPrefix(arg, defineFlagPrefix)
		name, _, found := strings.Cut(override, "=")
		if !found || name == "" {
			return nil, nil, fmt.Errorf("error: malformed flag '%s' — expected -define:NAME=VALUE", arg)
		}
		if seen[name] {
			return nil, nil, fmt.Errorf("error: '%s' is defined more than once with -define:", name)
		}
		seen[name] = true
		defines = append(defines, override)
	}
	return rest, defines, nil
}

// defineArgs turns the collected overrides into grayc arguments.
func defineArgs() []string {
	var args []string
	for _, override := range defineOverrides {
		args = append(args, "--define="+override)
	}
	return args
}
