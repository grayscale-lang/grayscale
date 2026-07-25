// main.go — Entry point for the gray CLI. Bootstraps the root Cobra
// command and provides version display with remote release lookups.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"
)

// Version information - injected at build time via ldflags
var (
	Version   = "dev"
	BuildTime = "unknown"
)

const asciiBanner = "" +
	"   ____                               _\n" +
	"  / ___|_ __ __ _ _   _ ___  ___ __ _| | ___\n" +
	" | |  _| '__/ _` | | | / __|/ __/ _` | |/ _ \\\n" +
	" | |_| | | | (_| | |_| \\__ \\ (_| (_| | |  __/\n" +
	"  \\____|_|  \\__,_|\\__, |___/\\___\\__,_|_|\\___|\n" +
	"                  |___/\n" +
	" Programming On Your Terms\n"

func main() {
	if err := rootCmd.Execute(); err != nil {
		var exitErr *ExitError
		if errors.As(err, &exitErr) {
			os.Exit(exitErr.Code)
		}
		os.Exit(1)
	}
}

func getVersionString() string {
	vi := GetVersionInfo()

	buf := bytes.Buffer{}

	// Installed line: display version + channel tag
	channelTag := ""
	switch vi.Channel {
	case "pre-release":
		channelTag = "  (pre-release)"
	case "dev":
		channelTag = "  (dev build)"
	}
	fmt.Fprintf(&buf, "\033[1mInstalled:\033[0m           %s%s\n", vi.Display, channelTag)

	// Commit line (only meaningful for dev builds)
	if vi.Commit != "" {
		fmt.Fprintf(&buf, "Commit:              %s\n", vi.Commit)
	}

	// Build time — normalise the CI underscore to a space for human reading
	fmt.Fprintf(&buf, "Built:               %s\n", strings.ReplaceAll(BuildTime, "_", " "))

	// Fetch remote release info. Use /releases so both channels are
	// reachable — /releases/latest hides pre-releases entirely and was
	// the reason `gray version` reported "Latest: v2.0.0" to people on
	// 3.0.0-alpha dev builds.
	ctx, cancel := context.WithTimeout(context.Background(), checkTimeout)
	defer cancel()

	var latestStable, latestPre string
	if releases, err := fetchAllReleases(ctx); err == nil {
		for i := range releases {
			r := &releases[i]
			if r.Prerelease {
				if latestPre == "" || compareSemver(r.TagName, latestPre) > 0 {
					latestPre = r.TagName
				}
			} else {
				if latestStable == "" || compareSemver(r.TagName, latestStable) > 0 {
					latestStable = r.TagName
				}
			}
		}
		// Cache the latest stable for CheckForUpdateAsync
		if latestStable != "" {
			writeUpdateState(&UpdateState{
				LastCheck:     time.Now().Format("2006-01-02"),
				LatestVersion: latestStable,
			})
		}
	} else {
		// Fall back to the cached stable if the network fetch failed
		if state, _ := readUpdateState(); state != nil {
			latestStable = state.LatestVersion
		}
	}

	buf.WriteString("\n")
	if latestStable != "" {
		fmt.Fprintf(&buf, "Latest stable:       %s", latestStable)
		if compareSemver(latestStable, Version) > 0 {
			fmt.Fprint(&buf, "  \033[33m← run 'gray update'\033[0m")
		}
		buf.WriteString("\n")
	}
	if latestPre != "" {
		fmt.Fprintf(&buf, "Latest pre-release:  %s", latestPre)
		if compareSemver(latestPre, Version) > 0 {
			fmt.Fprint(&buf, "  \033[33m← run 'gray update --pre'\033[0m")
		}
		buf.WriteString("\n")
	}

	fmt.Fprintf(&buf, "\nCopyright (c) 2025-Present Marshall A Burns\n")
	return buf.String()
}
