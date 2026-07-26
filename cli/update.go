// update.go — Implements self-update, version installation, and release
// management. Handles semver parsing, GitHub release fetching, changelog
// formatting, archive extraction, and binary replacement.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"archive/tar"
	"archive/zip"
	"bufio"
	"compress/gzip"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"time"
)

// UpdateState stores the last update check info
type UpdateState struct {
	LastCheck     string `json:"last_check"`
	LatestVersion string `json:"latest_version"`
}

// GitHubRelease represents the GitHub API response for releases
type GitHubRelease struct {
	TagName    string `json:"tag_name"`
	Body       string `json:"body"`
	Prerelease bool   `json:"prerelease"`
	Assets     []struct {
		Name               string `json:"name"`
		BrowserDownloadURL string `json:"browser_download_url"`
	} `json:"assets"`
}

const (
	githubAPIURL         = "https://api.github.com/repos/grayscale-lang/grayscale/releases/latest"
	githubReleasesURL    = "https://api.github.com/repos/grayscale-lang/grayscale/releases"
	updateCheckDir       = ".gray"
	updateCheckFile      = "update_check"
	checkTimeout         = 2 * time.Second
	maxChangelogVersions = 10        // Maximum number of versions to show in changelog
	maxDownloadBytes     = 256 << 20 // 256 MiB upper bound on release archive size
	releaseTagPrefix     = "grayscale-"
)

// getUpdateStatePath returns the path to the update state file
func getUpdateStatePath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, updateCheckDir, updateCheckFile), nil
}

// readUpdateState reads the update state from disk
func readUpdateState() (*UpdateState, error) {
	path, err := getUpdateStatePath()
	if err != nil {
		return nil, err
	}

	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return &UpdateState{}, nil
		}
		return nil, err
	}

	var state UpdateState
	if err := json.Unmarshal(data, &state); err != nil {
		return &UpdateState{}, nil
	}
	return &state, nil
}

// writeUpdateState writes the update state to disk
func writeUpdateState(state *UpdateState) error {
	path, err := getUpdateStatePath()
	if err != nil {
		return err
	}

	// Ensure directory exists
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}

	data, err := json.Marshal(state)
	if err != nil {
		return err
	}

	return os.WriteFile(path, data, 0600)
}

// shouldCheckForUpdate returns true if we should check for updates (once per day)
func shouldCheckForUpdate() bool {
	state, err := readUpdateState()
	if err != nil {
		return true
	}

	if state.LastCheck == "" {
		return true
	}

	today := time.Now().Format("2006-01-02")
	return state.LastCheck != today
}

// parseVersion extracts major, minor, patch from a leading "vX.Y.Z" prefix.
// Kept for callers that don't care about pre-release suffixes.
func parseVersion(v string) (major, minor, patch int) {
	major, minor, patch, _ = parseSemver(v)
	return
}

// exactSemverRE matches a fully-qualified semver string (with optional
// leading 'v' and optional pre-release / build-metadata suffixes). Used
// by `gray install` to reject partial versions like "2.5".
var exactSemverRE = regexp.MustCompile(
	`^v?\d+\.\d+\.\d+(-[0-9A-Za-z][0-9A-Za-z.-]*)?(\+[0-9A-Za-z][0-9A-Za-z.-]*)?$`)

// gitDescribeRE matches a `git describe --dirty` trailer such as
// "-4-g0cbcb58" or "-4-g0cbcb58-dirty" appended to a semver pre-release
// label by local dev builds. Matched as a unit so identifiers that
// legitimately contain "-g" (unlikely but possible) aren't over-trimmed.
var gitDescribeRE = regexp.MustCompile(`-\d+-g[0-9a-f]+(-dirty)?$`)

// gitDescribeCaptureRE captures the components of the trailer so callers
// can surface commit count / hash / dirty state separately from the
// display version.
var gitDescribeCaptureRE = regexp.MustCompile(`-(\d+)-g([0-9a-f]+)(-dirty)?$`)

// VersionInfo holds the derived pieces of the linker-baked version string.
type VersionInfo struct {
	Display      string // e.g. "v3.0.0-alpha.6" — trailer stripped
	Channel      string // "stable", "pre-release", or "dev"
	Commit       string // git short hash, empty if not a dev build
	CommitsAhead int    // commits past the base tag
	Dirty        bool   // working tree was dirty at build time
}

// GetVersionInfo parses the linker-baked Version string into its parts.
func GetVersionInfo() VersionInfo {
	vi := VersionInfo{Display: Version}
	if m := gitDescribeCaptureRE.FindStringSubmatch(Version); m != nil {
		fmt.Sscanf(m[1], "%d", &vi.CommitsAhead)
		vi.Commit = m[2]
		vi.Dirty = m[3] != ""
		vi.Display = Version[:len(Version)-len(m[0])]
	}
	_, _, _, pre := parseSemver(vi.Display)
	switch {
	case Version == "dev":
		vi.Channel = "dev"
	case vi.Commit != "":
		vi.Channel = "dev"
	case pre != "":
		vi.Channel = "pre-release"
	default:
		vi.Channel = "stable"
	}
	return vi
}

// parseSemver parses a version string like "v3.0.0-beta.2" or "0.16.10" into
// its numeric components plus the raw pre-release suffix (empty for stable).
// Build metadata after '+' is discarded. A trailing git-describe suffix on
// the pre-release label is stripped back to the real identifier so a local
// dev build still orders correctly against upstream tags.
func parseSemver(v string) (major, minor, patch int, pre string) {
	v = strings.TrimPrefix(v, releaseTagPrefix)
	v = strings.TrimPrefix(v, "v")
	// Drop +build metadata
	if i := strings.Index(v, "+"); i != -1 {
		v = v[:i]
	}
	// Split off pre-release suffix
	base := v
	if i := strings.Index(v, "-"); i != -1 {
		base = v[:i]
		pre = gitDescribeRE.ReplaceAllString(v[i+1:], "")
	}
	parts := strings.Split(base, ".")
	if len(parts) >= 1 {
		fmt.Sscanf(parts[0], "%d", &major)
	}
	if len(parts) >= 2 {
		fmt.Sscanf(parts[1], "%d", &minor)
	}
	if len(parts) >= 3 {
		fmt.Sscanf(parts[2], "%d", &patch)
	}
	return
}

// comparePreIdentifiers compares two dot-separated semver pre-release
// identifier lists per SemVer 2.0 rule 11.4: numeric identifiers compare
// numerically, alphanumerics compare ASCII, numeric < alphanumeric, and a
// shorter list with equal leading fields is lower. Returns -1/0/1.
func comparePreIdentifiers(a, b string) int {
	as := strings.Split(a, ".")
	bs := strings.Split(b, ".")
	n := len(as)
	if len(bs) < n {
		n = len(bs)
	}
	for i := 0; i < n; i++ {
		var ai, bi int
		an, _ := fmt.Sscanf(as[i], "%d", &ai)
		bn, _ := fmt.Sscanf(bs[i], "%d", &bi)
		aIsNum := an == 1 && fmt.Sprint(ai) == as[i]
		bIsNum := bn == 1 && fmt.Sprint(bi) == bs[i]
		if aIsNum && bIsNum {
			if ai != bi {
				if ai < bi {
					return -1
				}
				return 1
			}
			continue
		}
		if aIsNum != bIsNum {
			if aIsNum {
				return -1
			}
			return 1
		}
		if as[i] != bs[i] {
			if as[i] < bs[i] {
				return -1
			}
			return 1
		}
	}
	if len(as) == len(bs) {
		return 0
	}
	if len(as) < len(bs) {
		return -1
	}
	return 1
}

// compareSemver compares two semver strings. Returns -1 if a < b, 0 if
// equal, 1 if a > b. Handles pre-release ordering per the SemVer spec:
// a version without a pre-release suffix is greater than the same version
// with one, and pre-release identifiers compare field-by-field.
func compareSemver(a, b string) int {
	aM, an, ap, apre := parseSemver(a)
	bM, bn, bp, bpre := parseSemver(b)
	if aM != bM {
		if aM < bM {
			return -1
		}
		return 1
	}
	if an != bn {
		if an < bn {
			return -1
		}
		return 1
	}
	if ap != bp {
		if ap < bp {
			return -1
		}
		return 1
	}
	// Base versions equal — stable > pre-release
	if apre == "" && bpre == "" {
		return 0
	}
	if apre == "" {
		return 1
	}
	if bpre == "" {
		return -1
	}
	return comparePreIdentifiers(apre, bpre)
}

// isNewerVersion returns true if remote version is higher than local per
// SemVer precedence rules (including pre-release suffixes).
func isNewerVersion(local, remote string) bool {
	return compareSemver(remote, local) > 0
}

// isVersionInRange returns true if version is between fromVersion (exclusive) and toVersion (inclusive)
// i.e., fromVersion < version <= toVersion
func isVersionInRange(version, fromVersion, toVersion string) bool {
	// version must be greater than fromVersion
	// isNewerVersion(local, remote) returns true if remote > local
	if !isNewerVersion(fromVersion, version) {
		return false
	}
	// version must be less than or equal to toVersion
	// i.e., version is NOT greater than toVersion
	// NOT(version > toVersion) = NOT(isNewerVersion(toVersion, version))
	return !isNewerVersion(toVersion, version)
}

// fetchLatestRelease fetches the latest release info from GitHub
func fetchLatestRelease(ctx context.Context) (*GitHubRelease, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", githubAPIURL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github.v3+json")
	req.Header.Set("User-Agent", "Grayscale-Language-Updater")

	client := &http.Client{Timeout: checkTimeout}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GitHub API returned status %d", resp.StatusCode)
	}

	var release GitHubRelease
	if err := json.NewDecoder(resp.Body).Decode(&release); err != nil {
		return nil, err
	}

	return &release, nil
}

// fetchAllReleases fetches all releases from GitHub
func fetchAllReleases(ctx context.Context) ([]GitHubRelease, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", githubReleasesURL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github.v3+json")
	req.Header.Set("User-Agent", "Grayscale-Language-Updater")

	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GitHub API returned status %d", resp.StatusCode)
	}

	var releases []GitHubRelease
	if err := json.NewDecoder(resp.Body).Decode(&releases); err != nil {
		return nil, err
	}

	// Filter to only Grayscale-era releases (grayscale-v* tags) so that
	// legacy EZ releases (v1.x–v3.x) are excluded from version discovery.
	filtered := releases[:0]
	for _, r := range releases {
		if strings.HasPrefix(r.TagName, releaseTagPrefix) {
			filtered = append(filtered, r)
		}
	}

	return filtered, nil
}

// pickLatestPrerelease scans a release list (as returned by /releases) and
// returns the pre-release with the highest semver ordering, or nil if no
// pre-releases are present. Used both by `gray update --pre` to pick a
// target and by the default `gray update` path to decide whether to print
// the nudge about a newer pre-release.
func pickLatestPrerelease(releases []GitHubRelease) *GitHubRelease {
	var best *GitHubRelease
	for i := range releases {
		r := &releases[i]
		if !r.Prerelease {
			continue
		}
		if best == nil || compareSemver(r.TagName, best.TagName) > 0 {
			best = r
		}
	}
	return best
}

// getReleasesInRange filters releases to those between currentVersion (exclusive) and latestVersion (inclusive)
func getReleasesInRange(releases []GitHubRelease, currentVersion, latestVersion string) []GitHubRelease {
	var result []GitHubRelease
	for _, release := range releases {
		if isVersionInRange(release.TagName, currentVersion, latestVersion) {
			result = append(result, release)
		}
	}
	return result
}

// ParsedChangelog holds the parsed features and bug fixes from a release body
type ParsedChangelog struct {
	Features []string
	BugFixes []string
}

// Regexes for cleaning changelog list items (used by cleanChangelogItem).
var (
	reClosesRef  = regexp.MustCompile(`, closes \[#\d+\]\([^)]+\)`)
	reParenIssue = regexp.MustCompile(` \(#\d+\)`)
	reBareIssue  = regexp.MustCompile(` #\d+`)
	reCommitLink = regexp.MustCompile(`\s*\(\[([a-f0-9]+)\]\([^)]+\)\)\s*$`)
	reCommitBare = regexp.MustCompile(`\s*\(([a-f0-9]+)\)\s*$`)
	reIssueLink  = regexp.MustCompile(`\s*\(\[#\d+\]\([^)]+\)\)`)
	reMdLink     = regexp.MustCompile(`\[([^\]]+)\]\([^)]+\)`)
)

// cleanChangelogItem strips markdown formatting, issue references, and
// trailing metadata from a changelog list item, keeping only the scope,
// description, and commit hash.
func cleanChangelogItem(item string) string {
	item = strings.ReplaceAll(item, "**", "")

	item = strings.ReplaceAll(item, "&lt;", "<")
	item = strings.ReplaceAll(item, "&gt;", ">")
	item = strings.ReplaceAll(item, "&amp;", "&")

	item = reClosesRef.ReplaceAllString(item, "")
	item = reParenIssue.ReplaceAllString(item, "")
	item = reBareIssue.ReplaceAllString(item, "")

	// Extract and remove trailing commit hash: ([hash](url)) or (hash)
	var commitHash string
	if m := reCommitLink.FindStringSubmatch(item); m != nil {
		commitHash = m[1]
		item = item[:reCommitLink.FindStringIndex(item)[0]]
	} else if m := reCommitBare.FindStringSubmatch(item); m != nil {
		commitHash = m[1]
		item = item[:reCommitBare.FindStringIndex(item)[0]]
	}

	item = reIssueLink.ReplaceAllString(item, "")
	item = reMdLink.ReplaceAllString(item, "$1")

	if commitHash != "" {
		item = strings.TrimSpace(item) + " (" + commitHash + ")"
	}

	return strings.TrimSpace(item)
}

// parseReleaseBody extracts Features and Bug Fixes sections from a release body
func parseReleaseBody(body string) ParsedChangelog {
	result := ParsedChangelog{}

	var currentSection string
	for _, line := range strings.Split(body, "\n") {
		trimmed := strings.TrimSpace(line)

		if strings.HasPrefix(trimmed, "### Features") {
			currentSection = "features"
			continue
		}
		if strings.HasPrefix(trimmed, "### Bug Fixes") {
			currentSection = "bugfixes"
			continue
		}
		if strings.HasPrefix(trimmed, "###") {
			currentSection = ""
			continue
		}

		if currentSection == "" {
			continue
		}
		if len(trimmed) < 2 || (trimmed[0] != '*' && trimmed[0] != '-') || trimmed[1] != ' ' {
			continue
		}

		item := cleanChangelogItem(trimmed[2:])
		if item == "" {
			continue
		}

		switch currentSection {
		case "features":
			result.Features = append(result.Features, item)
		case "bugfixes":
			result.BugFixes = append(result.BugFixes, item)
		}
	}

	return result
}

// formatChangelog formats the changelog for display
func formatChangelog(releases []GitHubRelease, currentVersion, latestVersion string) string {
	var sb strings.Builder

	sb.WriteString(fmt.Sprintf("\nUpdating from %s -> %s\n", currentVersion, latestVersion))
	sb.WriteString("\nWhat's new since your version:\n")
	sb.WriteString(strings.Repeat("-", 40) + "\n")

	// Limit to maxChangelogVersions
	displayReleases := releases
	truncated := false
	if len(releases) > maxChangelogVersions {
		displayReleases = releases[:maxChangelogVersions]
		truncated = true
	}

	for _, release := range displayReleases {
		parsed := parseReleaseBody(release.Body)

		// Skip releases with no features or bug fixes
		if len(parsed.Features) == 0 && len(parsed.BugFixes) == 0 {
			continue
		}

		sb.WriteString(fmt.Sprintf("\n\033[1m%s\033[0m\n", release.TagName))

		if len(parsed.Features) > 0 {
			sb.WriteString("  Features:\n")
			for _, f := range parsed.Features {
				sb.WriteString(fmt.Sprintf("    - %s\n", f))
			}
		}

		if len(parsed.BugFixes) > 0 {
			sb.WriteString("  Bug Fixes:\n")
			for _, b := range parsed.BugFixes {
				sb.WriteString(fmt.Sprintf("    - %s\n", b))
			}
		}
	}

	if truncated {
		remaining := len(releases) - maxChangelogVersions
		sb.WriteString(fmt.Sprintf("\n... and %d more version(s)\n", remaining))
	}

	sb.WriteString("\n" + strings.Repeat("-", 40))

	return sb.String()
}

// CheckForUpdateAsync checks for updates and prints notice if available
// This is called at startup. It:
// 1. If cache is fresh, use cached state to print notice
// 2. If cache is stale/empty, do a synchronous check and print notice
// isDevBuild returns true when the running binary was built from an
// uncommitted or untagged source tree and should not be compared against
// published releases.  Covers three cases:
//   - Version == "dev"  (built without -ldflags, go run, etc.)
//   - contains "-dirty" (uncommitted changes on top of a tag)
//   - contains a git-describe trailer "-N-g<hash>" (commits ahead of a tag)
func isDevBuild() bool {
	if Version == "dev" || Version == "" {
		return true
	}
	if strings.Contains(Version, "dirty") {
		return true
	}
	// git-describe appends "-<commits>-g<hash>" after the base tag
	gitDescribe := regexp.MustCompile(`-\d+-g[0-9a-f]+`)
	return gitDescribe.MatchString(Version)
}

func CheckForUpdateAsync() {
	if isDevBuild() {
		return
	}

	state, _ := readUpdateState()

	// If we have cached state and it's fresh (checked today), use it
	if state != nil && state.LatestVersion != "" && !shouldCheckForUpdate() {
		if isNewerVersion(Version, state.LatestVersion) {
			fmt.Printf("Note: Grayscale %s available (you have %s). Run `gray update` to upgrade.\n\n",
				state.LatestVersion, Version)
		}
		return
	}

	// Cache is stale or empty - do a synchronous check
	ctx, cancel := context.WithTimeout(context.Background(), checkTimeout)
	defer cancel()

	release, err := fetchLatestRelease(ctx)
	if err != nil {
		// Network error - fall back to cached state if available
		if state != nil && state.LatestVersion != "" {
			if isNewerVersion(Version, state.LatestVersion) {
				fmt.Printf("Note: Grayscale %s available (you have %s). Run `gray update` to upgrade.\n\n",
					state.LatestVersion, Version)
			}
		}
		return
	}

	// Update cache
	newState := &UpdateState{
		LastCheck:     time.Now().Format("2006-01-02"),
		LatestVersion: release.TagName,
	}
	writeUpdateState(newState)

	// Print notification if newer version available
	if isNewerVersion(Version, release.TagName) {
		fmt.Printf("Note: Grayscale %s available (you have %s). Run `gray update` to upgrade.\n\n",
			release.TagName, Version)
	}
}

// runUpdate runs the interactive update command. `pre` opts into the
// latest pre-release (alpha/beta/rc) rather than the latest stable.
// The --confirm/url pair is used by the sudo re-exec path and bypasses
// all discovery.
// pickLatestStable scans a release list and returns the non-pre-release
// with the highest semver ordering, or nil if no stable releases exist.
func pickLatestStable(releases []GitHubRelease) *GitHubRelease {
	var best *GitHubRelease
	for i := range releases {
		r := &releases[i]
		if r.Prerelease {
			continue
		}
		if best == nil || compareSemver(r.TagName, best.TagName) > 0 {
			best = r
		}
	}
	return best
}

// printUpdateStatus prints the Installed / Latest stable / Latest
// pre-release block in the same column layout as `gray version`, with a
// yellow ← marker on whichever channel line is newer than the installed
// build. Either remote tag may be empty if the fetch was partial.
func printUpdateStatus(vi VersionInfo, latestStable, latestPre string) {
	channelTag := ""
	switch vi.Channel {
	case "pre-release":
		channelTag = "  (pre-release)"
	case "dev":
		channelTag = "  (dev build)"
	case "stable":
		channelTag = "  (stable)"
	}
	fmt.Printf("\033[1mInstalled:\033[0m           %s%s\n", vi.Display, channelTag)

	if latestStable != "" {
		fmt.Printf("Latest stable:       %s", latestStable)
		if compareSemver(latestStable, Version) > 0 {
			fmt.Print("  \033[33m← update available\033[0m")
		} else if compareSemver(latestStable, Version) == 0 {
			fmt.Print("  \033[33m← up to date\033[0m")
		}
		fmt.Println()
	}
	if latestPre != "" {
		fmt.Printf("Latest pre-release:  %s", latestPre)
		if compareSemver(latestPre, Version) > 0 {
			fmt.Print("  \033[33m← run 'gray update --pre'\033[0m")
		} else if compareSemver(latestPre, Version) == 0 {
			fmt.Print("  \033[33m← up to date\033[0m")
		}
		fmt.Println()
	}
}

func runUpdate(confirm bool, url string, pre bool) error {
	// Check for --confirm flag (used by sudo re-exec)
	if confirm {
		downloadURL := url
		fmt.Printf("Installing update...\n")
		if err := downloadAndInstall(downloadURL); err != nil {
			return fmt.Errorf("Error during update: %v", err)
		}
		fmt.Println("Successfully updated!")
		fmt.Println("Restart your terminal or run `gray version` to verify.")
		return nil
	}

	fmt.Println("Checking for updates...")
	fmt.Println()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	allReleases, err := fetchAllReleases(ctx)
	if err != nil {
		return fmt.Errorf("Error fetching release list: %v", err)
	}

	latestStable := pickLatestStable(allReleases)
	latestPre := pickLatestPrerelease(allReleases)

	var latestStableTag, latestPreTag string
	if latestStable != nil {
		latestStableTag = latestStable.TagName
	}
	if latestPre != nil {
		latestPreTag = latestPre.TagName
	}

	vi := GetVersionInfo()
	printUpdateStatus(vi, latestStableTag, latestPreTag)

	// Cache the latest stable so CheckForUpdateAsync can skip a network hop.
	if latestStableTag != "" {
		writeUpdateState(&UpdateState{
			LastCheck:     time.Now().Format("2006-01-02"),
			LatestVersion: latestStableTag,
		})
	}

	// Pre-release installs require --pre to advance. Bare `gray update`
	// refuses rather than silently downgrading to stable or auto-tracking
	// the pre-release channel — either would surprise the user.
	if vi.Channel == "pre-release" && !pre {
		fmt.Println("\nYou're on a pre-release. 'gray update' only advances stable installs.")
		fmt.Println("  • Newer pre-release:   gray update --pre")
		if latestStableTag != "" {
			fmt.Printf("  • Switch to stable:    gray install %s\n", strings.TrimPrefix(latestStableTag, "v"))
		}
		return nil
	}

	// Resolve target for the channel being advanced.
	var target *GitHubRelease
	if pre {
		target = latestPre
		if target == nil {
			fmt.Println("\nNo pre-releases are currently published.")
			return nil
		}
	} else {
		target = latestStable
		if target == nil {
			fmt.Println("\nNo stable releases are currently published.")
			return nil
		}
	}

	cmp := compareSemver(target.TagName, Version)
	if cmp == 0 {
		if pre {
			fmt.Println("\nYou're on the latest pre-release.")
		} else {
			fmt.Println("\nYou're on the latest stable release.")
		}
		return nil
	}
	if cmp < 0 {
		label := "stable"
		if pre {
			label = "pre-release"
		}
		fmt.Printf("\nYour version (%s) is already newer than the latest %s.\n", Version, label)
		return nil
	}

	if pre {
		fmt.Printf("\n\033[33mInstalling pre-release %s — may contain breaking changes, not recommended for production.\033[0m\n",
			target.TagName)
	} else {
		releasesInRange := getReleasesInRange(allReleases, Version, target.TagName)
		fmt.Print(formatChangelog(releasesInRange, Version, target.TagName))
	}

	if vi.Channel == "dev" && vi.CommitsAhead > 0 {
		noun := "commits"
		if vi.CommitsAhead == 1 {
			noun = "commit"
		}
		fmt.Printf("\n\033[33mWarning: your dev build is %d %s past %s (commit %s).\n",
			vi.CommitsAhead, noun, vi.Display, vi.Commit)
		fmt.Printf("Those %s may not be present in %s.\033[0m\n", noun, target.TagName)
	}

	fmt.Printf("\nUpgrade to %s? (y/N): ", target.TagName)
	reader := bufio.NewReader(os.Stdin)
	response, _ := reader.ReadString('\n')
	response = strings.TrimSpace(strings.ToLower(response))
	if response != "y" && response != "yes" {
		fmt.Println("Update cancelled.")
		return nil
	}

	assetName := getAssetName()
	var downloadURL string
	for _, asset := range target.Assets {
		if asset.Name == assetName {
			downloadURL = asset.BrowserDownloadURL
			break
		}
	}
	if downloadURL == "" {
		return fmt.Errorf("error: no binary available for %s/%s\nYou may need to build from source: go install github.com/grayscale-lang/grayscale/cli@latest",
			runtime.GOOS, runtime.GOARCH)
	}
	fmt.Printf("Downloading %s...\n", assetName)
	if err := downloadAndInstall(downloadURL); err != nil {
		return fmt.Errorf("Error during update: %v", err)
	}

	fmt.Printf("\n\033[1m%s\033[0m\n", target.TagName)
	if pre {
		fmt.Println("\nSuccessfully installed pre-release!")
	} else {
		fmt.Println("\nSuccessfully updated!")
	}
	fmt.Println("Restart your terminal or run `gray version` to verify.")
	promptAndVerify()
	return nil
}

// normalizeTag strips a single leading 'v' so user input and GitHub tag
// names (which may or may not be prefixed) compare cleanly.
func normalizeTag(v string) string {
	v = strings.TrimPrefix(v, releaseTagPrefix)
	return strings.TrimPrefix(v, "v")
}

// runInstall installs an exact Grayscale version, replacing the current install.
// The version must be a fully-qualified semver with optional pre-release
// suffix; partial forms like "2.5" are rejected with a clean error. If
// the requested version is older than the currently running binary, a
// one-line downgrade warning is emitted and installation continues.
func runInstall(version string) error {
	if version == "" {
		return fmt.Errorf("error: missing version argument\nusage: gray install <version>    (e.g. gray install 3.0.0-beta.2)")
	}
	if !exactSemverRE.MatchString(version) {
		return fmt.Errorf("error: '%s' is not a fully-qualified semver\ngray install requires an exact version like '2.5.0' or '3.0.0-beta.2'\npartial versions, ranges, and shorthand are not accepted", version)
	}

	fmt.Printf("Current version: %s\n", Version)
	fmt.Printf("Requested:       %s\n", version)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	releases, err := fetchAllReleases(ctx)
	if err != nil {
		return fmt.Errorf("Error fetching release list: %v", err)
	}

	wanted := normalizeTag(version)
	var target *GitHubRelease
	for i := range releases {
		if normalizeTag(releases[i].TagName) == wanted {
			target = &releases[i]
			break
		}
	}
	if target == nil {
		var sb strings.Builder
		fmt.Fprintf(&sb, "error: version '%s' was not found in the release list", version)
		// Show up to five nearest versions by semver ordering, centred on
		// the requested slot. Gives the user something to retry with
		// without a separate `gray list` command.
		type tagged struct {
			tag string
			pre bool
		}
		all := make([]tagged, 0, len(releases))
		for i := range releases {
			all = append(all, tagged{releases[i].TagName, releases[i].Prerelease})
		}
		// Sort descending by semver
		for i := 0; i < len(all); i++ {
			for j := i + 1; j < len(all); j++ {
				if compareSemver(all[j].tag, all[i].tag) > 0 {
					all[i], all[j] = all[j], all[i]
				}
			}
		}
		max := 5
		if len(all) < max {
			max = len(all)
		}
		if max > 0 {
			sb.WriteString("\n\nNearby available versions:")
			for i := 0; i < max; i++ {
				label := ""
				if all[i].pre {
					label = " (pre-release)"
				}
				fmt.Fprintf(&sb, "\n  %s%s", all[i].tag, label)
			}
		}
		return fmt.Errorf("%s", sb.String())
	}

	// Downgrade warning — after we've confirmed the target exists.
	if compareSemver(target.TagName, Version) < 0 {
		fmt.Printf("\n\033[33mwarning: downgrading from %s to %s\033[0m\n",
			Version, target.TagName)
	} else if compareSemver(target.TagName, Version) == 0 {
		fmt.Printf("\nYou're already on %s.\n", target.TagName)
		return nil
	}

	if target.Prerelease {
		fmt.Printf("\n\033[33mInstalling pre-release %s — may contain breaking changes, not recommended for production.\033[0m\n",
			target.TagName)
	}

	// Platform asset lookup — same logic as runUpdate.
	assetName := getAssetName()
	var downloadURL string
	for _, asset := range target.Assets {
		if asset.Name == assetName {
			downloadURL = asset.BrowserDownloadURL
			break
		}
	}
	if downloadURL == "" {
		return fmt.Errorf("error: no binary available for %s/%s at %s\nYou may need to build from source: go install github.com/grayscale-lang/grayscale/cli@latest",
			runtime.GOOS, runtime.GOARCH, target.TagName)
	}

	fmt.Printf("Downloading %s...\n", assetName)
	if err := downloadAndInstall(downloadURL); err != nil {
		return fmt.Errorf("Error during install: %v", err)
	}

	fmt.Printf("\n\033[1m%s\033[0m\n", target.TagName)
	fmt.Println("\nSuccessfully installed!")
	fmt.Println("Restart your terminal or run `gray version` to verify.")
	promptAndVerify()
	return nil
}

// promptAndVerify asks the user (only when stdin is a terminal) whether to run
// the verification test suite. It is CI-safe: if stdin is not a terminal the
// function returns silently without blocking.
func promptAndVerify() {
	stat, err := os.Stdin.Stat()
	if err != nil {
		return
	}
	if stat.Mode()&os.ModeCharDevice == 0 {
		// Not an interactive terminal (pipe, CI, etc.) — skip the prompt.
		return
	}
	fmt.Print("\nRun verification test? [y/N]: ")
	reader := bufio.NewReader(os.Stdin)
	response, _ := reader.ReadString('\n')
	response = strings.TrimSpace(strings.ToLower(response))
	if response == "y" || response == "yes" {
		code := runVerify()
		if code != 0 {
			os.Exit(code)
		}
	}
}

// getAssetName returns the expected archive name for this OS/arch
func getAssetName() string {
	osName := runtime.GOOS
	arch := runtime.GOARCH

	name := fmt.Sprintf("gray-%s-%s", osName, arch)
	if runtime.GOOS == "windows" {
		name += ".zip"
	} else {
		name += ".tar.gz"
	}
	return name
}

// downloadAndInstall downloads the archive and extracts/installs the binary.
// Instead of probing write permission with a test file (TOCTOU-prone), this
// attempts the install directly and escalates to sudo only if the actual
// file operation fails with a permission error.
func downloadAndInstall(downloadURL string) error {
	execPath, err := os.Executable()
	if err != nil {
		return fmt.Errorf("failed to get executable path: %w", err)
	}
	execPath, err = filepath.EvalSymlinks(execPath)
	if err != nil {
		return fmt.Errorf("failed to resolve symlinks: %w", err)
	}

	err = doInstall(downloadURL, execPath)
	if err == nil {
		return nil
	}

	if !errors.Is(err, os.ErrPermission) {
		return err
	}

	// Need elevated permissions
	if runtime.GOOS == "windows" {
		return fmt.Errorf("permission denied. Please run as Administrator")
	}
	fmt.Println("Root permissions required. Running with sudo...")
	cmd := exec.Command("sudo", execPath, "update", "--confirm", downloadURL)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to run with sudo: %w", err)
	}

	os.Exit(0)
	return nil // unreachable
}

const (
	trustedUpdateHost       = "github.com"
	trustedUpdatePathPrefix = "/grayscale-lang/grayscale/releases/download/"
)

// isTrustedUpdateURL returns true only when the URL points to an official Grayscale
// release asset on GitHub. Scheme must be https and the host/path must match
// the known release download prefix exactly, so --confirm cannot be used to
// fetch and install an arbitrary binary.
func isTrustedUpdateURL(rawURL string) bool {
	u, err := url.Parse(rawURL)
	if err != nil {
		return false
	}
	return u.Scheme == "https" &&
		u.Host == trustedUpdateHost &&
		strings.HasPrefix(u.Path, trustedUpdatePathPrefix)
}

// doInstall performs the actual download and installation
func doInstall(downloadURL, execPath string) error {
	if !isTrustedUpdateURL(downloadURL) {
		return fmt.Errorf("download URL is not from a trusted origin: only https://github.com/grayscale-lang/grayscale/releases/download/ is accepted")
	}
	url := downloadURL
	// Download archive to temp file
	client := &http.Client{Timeout: 5 * time.Minute}
	resp, err := client.Get(url)
	if err != nil {
		return fmt.Errorf("failed to download: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("download failed with status %d", resp.StatusCode)
	}

	// Reject oversized responses before streaming begins.
	if resp.ContentLength > maxDownloadBytes {
		return fmt.Errorf("download rejected: Content-Length %d exceeds the %d-byte limit", resp.ContentLength, maxDownloadBytes)
	}

	// Create temp directory for extraction
	tmpDir, err := os.MkdirTemp("", "gray-update-*")
	if err != nil {
		return fmt.Errorf("failed to create temp directory: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	// Download archive — cap at maxDownloadBytes+1 so we can detect servers
	// that lie about Content-Length or omit the header entirely.
	archivePath := filepath.Join(tmpDir, "archive")
	archiveFile, err := os.Create(archivePath)
	if err != nil {
		return fmt.Errorf("failed to create archive file: %w", err)
	}

	n, err := io.Copy(archiveFile, io.LimitReader(resp.Body, maxDownloadBytes+1))
	archiveFile.Close()
	if err != nil {
		return fmt.Errorf("failed to download archive: %w", err)
	}
	if n > maxDownloadBytes {
		return fmt.Errorf("download aborted: response body exceeded the %d-byte limit", maxDownloadBytes)
	}

	// Extract binary from archive
	var binaryPath string
	if runtime.GOOS == "windows" {
		binaryPath, err = extractZip(archivePath, tmpDir)
	} else {
		binaryPath, err = extractTarGz(archivePath, tmpDir)
	}
	if err != nil {
		return fmt.Errorf("failed to extract archive: %w", err)
	}

	// Make executable
	if err := os.Chmod(binaryPath, 0755); err != nil {
		return fmt.Errorf("failed to set permissions: %w", err)
	}

	// Backup current executable
	backupPath := execPath + ".backup"
	if err := os.Rename(execPath, backupPath); err != nil {
		return fmt.Errorf("failed to backup current binary: %w", err)
	}

	// Copy new binary into place (can't rename across filesystems)
	if err := copyFile(binaryPath, execPath); err != nil {
		// Try to restore backup
		os.Rename(backupPath, execPath)
		return fmt.Errorf("failed to install update: %w", err)
	}

	// Make the new binary executable
	if err := os.Chmod(execPath, 0755); err != nil {
		// Try to restore backup
		os.Remove(execPath)
		os.Rename(backupPath, execPath)
		return fmt.Errorf("failed to set permissions: %w", err)
	}

	// Remove backup
	os.Remove(backupPath)

	// Post-#1461: release archives ship a single binary; the compiler and
	// runtime are embedded inside `gray`. No side-car files to copy. The
	// archive extractor still whitelists `grayc`/`libgrayrt.a` so older
	// archives stay installable, but they're ignored on the output side.

	return nil
}

// sanitizeArchivePath validates that the extracted file path is within the destination directory.
// This prevents path traversal attacks (CWE-22) from malicious archives.
func sanitizeArchivePath(destDir, filename string) (string, error) {
	// Clean the filename to remove any path traversal sequences
	cleanName := filepath.Clean(filepath.Base(filename))

	// Reject any filename that is empty, a dot, or contains path separators after cleaning
	if cleanName == "" || cleanName == "." || cleanName == ".." {
		return "", fmt.Errorf("invalid filename in archive: %s", filename)
	}

	// Construct the destination path
	destPath := filepath.Join(destDir, cleanName)

	// Resolve to absolute path and verify it's within destDir
	absDestPath, err := filepath.Abs(destPath)
	if err != nil {
		return "", fmt.Errorf("failed to resolve path: %w", err)
	}

	absDestDir, err := filepath.Abs(destDir)
	if err != nil {
		return "", fmt.Errorf("failed to resolve destination directory: %w", err)
	}

	// Ensure the destination path starts with the destination directory
	// Add separator to prevent matching partial directory names (e.g., /tmp/gray vs /tmp/gray-malicious)
	if !strings.HasPrefix(absDestPath, absDestDir+string(filepath.Separator)) && absDestPath != absDestDir {
		return "", fmt.Errorf("path traversal detected: %s", filename)
	}

	return destPath, nil
}

// extractTarGz extracts gray, grayc, and libgrayrt.a from a .tar.gz archive
func extractTarGz(archivePath, destDir string) (string, error) {
	file, err := os.Open(archivePath)
	if err != nil {
		return "", err
	}
	defer file.Close()

	gzr, err := gzip.NewReader(file)
	if err != nil {
		return "", err
	}
	defer gzr.Close()

	tr := tar.NewReader(gzr)
	wantFiles := map[string]bool{"gray": true, "gray.exe": true, "grayc": true, "libgrayrt.a": true}
	var grayBinaryPath string

	for {
		header, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return "", err
		}

		name := filepath.Base(header.Name)
		if !wantFiles[name] {
			continue
		}

		destPath, err := sanitizeArchivePath(destDir, name)
		if err != nil {
			return "", err
		}

		outFile, err := os.Create(destPath)
		if err != nil {
			return "", err
		}
		if _, err := io.Copy(outFile, tr); err != nil {
			outFile.Close()
			return "", err
		}
		outFile.Close()

		if name == "gray" || name == "gray.exe" {
			grayBinaryPath = destPath
		}
	}

	if grayBinaryPath == "" {
		return "", fmt.Errorf("gray binary not found in archive")
	}
	return grayBinaryPath, nil
}

// extractZip extracts gray, grayc, and libgrayrt.a from a .zip archive
func extractZip(archivePath, destDir string) (string, error) {
	r, err := zip.OpenReader(archivePath)
	if err != nil {
		return "", err
	}
	defer r.Close()

	wantFiles := map[string]bool{"gray": true, "gray.exe": true, "grayc": true, "libgrayrt.a": true}
	var grayBinaryPath string

	for _, f := range r.File {
		name := filepath.Base(f.Name)
		if !wantFiles[name] {
			continue
		}

		destPath, err := sanitizeArchivePath(destDir, name)
		if err != nil {
			return "", err
		}

		rc, err := f.Open()
		if err != nil {
			return "", err
		}

		outFile, err := os.Create(destPath)
		if err != nil {
			rc.Close()
			return "", err
		}

		_, err = io.Copy(outFile, rc)
		outFile.Close()
		rc.Close()
		if err != nil {
			return "", err
		}

		if name == "gray" || name == "gray.exe" {
			grayBinaryPath = destPath
		}
	}

	if grayBinaryPath == "" {
		return "", fmt.Errorf("gray binary not found in archive")
	}
	return grayBinaryPath, nil
}

// copyFile copies a file from src to dst
func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}
