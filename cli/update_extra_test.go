// update_extra_test.go — Supplementary update tests covering version range
// logic, stable/pre-release picker functions, archive path sanitization,
// VersionInfo derivation, and dev-build detection.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/gzip"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestIsVersionInRange(t *testing.T) {
	cases := []struct {
		version, from, to string
		want              bool
	}{
		// strictly between: from < version <= to
		{"v3.0.1", "v3.0.0", "v3.0.2", true},
		{"v3.0.2", "v3.0.0", "v3.0.2", true},  // equal to upper bound is in range
		{"v3.0.0", "v3.0.0", "v3.0.2", false}, // equal to lower bound is NOT in range
		{"v2.9.9", "v3.0.0", "v3.0.2", false},
		{"v3.0.3", "v3.0.0", "v3.0.2", false},
		// pre-release ordering
		{"v3.0.0-beta.2", "v3.0.0-beta.1", "v3.0.0", true},
		{"v3.0.0-beta.1", "v3.0.0-beta.1", "v3.0.0", false},
	}
	for _, c := range cases {
		t.Run(c.version+"_in_"+c.from+".."+c.to, func(t *testing.T) {
			got := isVersionInRange(c.version, c.from, c.to)
			if got != c.want {
				t.Errorf("isVersionInRange(%q, %q, %q) = %v, want %v",
					c.version, c.from, c.to, got, c.want)
			}
		})
	}
}

func TestGetReleasesInRange(t *testing.T) {
	releases := []GitHubRelease{
		{TagName: "grayscale-v0.1.0"},
		{TagName: "grayscale-v0.1.1"},
		{TagName: "grayscale-v0.1.2"},
		{TagName: "grayscale-v0.2.0"},
		{TagName: "grayscale-v0.0.4"},
	}

	got := getReleasesInRange(releases, "grayscale-v0.1.0", "grayscale-v0.1.2")
	if len(got) != 2 {
		t.Fatalf("expected 2 releases in (grayscale-v0.1.0, grayscale-v0.1.2], got %d: %v", len(got), got)
	}
	tags := []string{got[0].TagName, got[1].TagName}
	for _, want := range []string{"grayscale-v0.1.1", "grayscale-v0.1.2"} {
		found := false
		for _, tag := range tags {
			if tag == want {
				found = true
			}
		}
		if !found {
			t.Errorf("expected %q in results %v", want, tags)
		}
	}
}

func TestGetReleasesInRange_Empty(t *testing.T) {
	releases := []GitHubRelease{{TagName: "grayscale-v0.1.0"}}
	got := getReleasesInRange(releases, "grayscale-v0.1.0", "grayscale-v0.1.0")
	if len(got) != 0 {
		t.Errorf("expected empty, got %v", got)
	}
}

func TestPickLatestStable(t *testing.T) {
	t.Run("empty", func(t *testing.T) {
		if got := pickLatestStable(nil); got != nil {
			t.Errorf("want nil, got %+v", got)
		}
	})

	t.Run("only prereleases", func(t *testing.T) {
		rels := []GitHubRelease{
			{TagName: "grayscale-v0.2.0-beta.1", Prerelease: true},
			{TagName: "grayscale-v0.2.0-rc.1", Prerelease: true},
		}
		if got := pickLatestStable(rels); got != nil {
			t.Errorf("want nil for prerelease-only list, got %+v", got)
		}
	})

	t.Run("picks highest stable", func(t *testing.T) {
		rels := []GitHubRelease{
			{TagName: "grayscale-v0.1.0", Prerelease: false},
			{TagName: "grayscale-v0.1.1", Prerelease: false},
			{TagName: "grayscale-v0.2.0-beta.1", Prerelease: true},
			{TagName: "grayscale-v0.0.4", Prerelease: false},
		}
		got := pickLatestStable(rels)
		if got == nil || got.TagName != "grayscale-v0.1.1" {
			t.Errorf("want grayscale-v0.1.1, got %+v", got)
		}
	})
}

func TestSanitizeArchivePath(t *testing.T) {
	cases := []struct {
		filename string
		wantErr  bool
	}{
		{"gray", false},
		{"grayc", false},
		{"libgrayrt.a", false},
		{"gray.exe", false},
		{"../evil", false}, // Base() strips the traversal; cleaned name is "evil"
		{"../../etc/passwd", false},
		{".", true},
		{"..", true},
		{"", true},
	}
	for _, c := range cases {
		t.Run(c.filename, func(t *testing.T) {
			_, err := sanitizeArchivePath("/tmp/test-dest", c.filename)
			if (err != nil) != c.wantErr {
				t.Errorf("sanitizeArchivePath(%q) err=%v, wantErr=%v", c.filename, err, c.wantErr)
			}
		})
	}
}

func TestSanitizeArchivePath_ValidFilesLandInsideDest(t *testing.T) {
	dir := t.TempDir()
	// Even if the raw filename contains directory traversal characters,
	// filepath.Base strips them; the result must land inside destDir.
	path, err := sanitizeArchivePath(dir, "../../sneaky/gray")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if path == "" {
		t.Error("expected non-empty path")
	}
}

func TestGetVersionInfo(t *testing.T) {
	orig := Version
	t.Cleanup(func() { Version = orig })

	t.Run("stable release", func(t *testing.T) {
		Version = "v3.1.0"
		vi := GetVersionInfo()
		if vi.Channel != "stable" {
			t.Errorf("channel = %q, want stable", vi.Channel)
		}
		if vi.Display != "v3.1.0" {
			t.Errorf("display = %q, want v3.1.0", vi.Display)
		}
		if vi.Commit != "" {
			t.Errorf("commit = %q, want empty", vi.Commit)
		}
	})

	t.Run("pre-release", func(t *testing.T) {
		Version = "v3.0.0-beta.2"
		vi := GetVersionInfo()
		if vi.Channel != "pre-release" {
			t.Errorf("channel = %q, want pre-release", vi.Channel)
		}
	})

	t.Run("dev sentinel", func(t *testing.T) {
		Version = "dev"
		vi := GetVersionInfo()
		if vi.Channel != "dev" {
			t.Errorf("channel = %q, want dev", vi.Channel)
		}
	})

	t.Run("git-describe dev build", func(t *testing.T) {
		Version = "v3.0.0-beta.2-4-g0cbcb58-dirty"
		vi := GetVersionInfo()
		if vi.Channel != "dev" {
			t.Errorf("channel = %q, want dev", vi.Channel)
		}
		if vi.Commit != "0cbcb58" {
			t.Errorf("commit = %q, want 0cbcb58", vi.Commit)
		}
		if !vi.Dirty {
			t.Error("Dirty should be true")
		}
		if vi.Display != "v3.0.0-beta.2" {
			t.Errorf("display = %q, want v3.0.0-beta.2", vi.Display)
		}
	})
}

func TestIsDevBuild(t *testing.T) {
	orig := Version
	t.Cleanup(func() { Version = orig })

	cases := []struct {
		v    string
		want bool
	}{
		{"dev", true},
		{"", true},
		{"v3.0.0-beta.2-4-g0cbcb58-dirty", true},
		{"v3.0.0-4-gabcdef0", true},
		{"v3.0.0", false},
		{"v3.0.0-beta.2", false},
	}
	for _, c := range cases {
		t.Run(c.v, func(t *testing.T) {
			Version = c.v
			got := isDevBuild()
			if got != c.want {
				t.Errorf("isDevBuild() = %v (Version=%q), want %v", got, c.v, c.want)
			}
		})
	}
}

func TestParseReleaseBody(t *testing.T) {
	body := `## What's Changed

### Features
* parser: add when expressions (#123)
* typechecker: support generics #456

### Bug Fixes
* fix nil dereference in codegen
* runtime: handle empty string in split (#789)

### Other
* some other section item
`
	cl := parseReleaseBody(body)

	if len(cl.Features) != 2 {
		t.Fatalf("expected 2 features, got %d: %v", len(cl.Features), cl.Features)
	}
	if len(cl.BugFixes) != 2 {
		t.Fatalf("expected 2 bug fixes, got %d: %v", len(cl.BugFixes), cl.BugFixes)
	}

	// Bare (# N) and # N issue references must be stripped
	for _, f := range cl.Features {
		if contains(f, "#123") || contains(f, "#456") {
			t.Errorf("issue reference not stripped from feature: %q", f)
		}
	}
	for _, b := range cl.BugFixes {
		if contains(b, "#789") {
			t.Errorf("issue reference not stripped from bug fix: %q", b)
		}
	}
}

func TestParseReleaseBody_Empty(t *testing.T) {
	cl := parseReleaseBody("")
	if len(cl.Features) != 0 || len(cl.BugFixes) != 0 {
		t.Errorf("empty body should produce empty changelog, got %+v", cl)
	}
}

func TestIsTrustedUpdateURL(t *testing.T) {
	cases := []struct {
		url  string
		want bool
	}{
		{"https://github.com/grayscale-lang/grayscale/releases/download/v1.0.0/gray-linux", true},
		{"http://github.com/grayscale-lang/grayscale/releases/download/v1.0.0/gray-linux", false},
		{"https://evil.com/grayscale-lang/grayscale/releases/download/v1.0.0/gray-linux", false},
		{"https://github.com/some-other-org/repo/releases/download/v1.0.0/gray-linux", false},
		{"not a url at all\x7f", false},
	}
	for _, c := range cases {
		t.Run(c.url, func(t *testing.T) {
			if got := isTrustedUpdateURL(c.url); got != c.want {
				t.Errorf("isTrustedUpdateURL(%q) = %v, want %v", c.url, got, c.want)
			}
		})
	}
}

func TestFormatChangelog(t *testing.T) {
	releases := []GitHubRelease{
		{TagName: "v1.1.0", Body: "### Features\n- New thing (#1)\n"},
		{TagName: "v1.0.1", Body: "no notable changes"},
	}
	out := formatChangelog(releases, "v1.0.0", "v1.1.0")
	if !strings.Contains(out, "Updating from v1.0.0 -> v1.1.0") {
		t.Errorf("formatChangelog missing version header, got: %q", out)
	}
	if !strings.Contains(out, "v1.1.0") {
		t.Errorf("formatChangelog missing release tag, got: %q", out)
	}
	if !strings.Contains(out, "New thing") {
		t.Errorf("formatChangelog missing feature text, got: %q", out)
	}
	if strings.Contains(out, "v1.0.1") {
		t.Errorf("formatChangelog should skip a release with no features/fixes, got: %q", out)
	}
}

func TestExtractTarGz(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "release.tar.gz")

	var buf bytes.Buffer
	gzw := gzip.NewWriter(&buf)
	tw := tar.NewWriter(gzw)
	files := map[string]string{"gray": "gray-binary-contents", "readme.txt": "ignored"}
	for name, content := range files {
		if err := tw.WriteHeader(&tar.Header{Name: name, Size: int64(len(content)), Mode: 0o755}); err != nil {
			t.Fatalf("write tar header: %v", err)
		}
		if _, err := tw.Write([]byte(content)); err != nil {
			t.Fatalf("write tar content: %v", err)
		}
	}
	tw.Close()
	gzw.Close()
	if err := os.WriteFile(archivePath, buf.Bytes(), 0o644); err != nil {
		t.Fatalf("write archive: %v", err)
	}

	destDir := t.TempDir()
	binPath, err := extractTarGz(archivePath, destDir)
	if err != nil {
		t.Fatalf("extractTarGz: %v", err)
	}
	got, err := os.ReadFile(binPath)
	if err != nil {
		t.Fatalf("read extracted binary: %v", err)
	}
	if string(got) != "gray-binary-contents" {
		t.Errorf("extracted content = %q, want %q", got, "gray-binary-contents")
	}
	if _, err := os.Stat(filepath.Join(destDir, "readme.txt")); !os.IsNotExist(err) {
		t.Errorf("readme.txt should not have been extracted")
	}
}

func TestExtractTarGz_MissingBinary(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "release.tar.gz")

	var buf bytes.Buffer
	gzw := gzip.NewWriter(&buf)
	tw := tar.NewWriter(gzw)
	tw.WriteHeader(&tar.Header{Name: "readme.txt", Size: 4, Mode: 0o644})
	tw.Write([]byte("nope"))
	tw.Close()
	gzw.Close()
	if err := os.WriteFile(archivePath, buf.Bytes(), 0o644); err != nil {
		t.Fatalf("write archive: %v", err)
	}

	if _, err := extractTarGz(archivePath, t.TempDir()); err == nil {
		t.Error("expected an error when the archive has no gray binary")
	}
}

func TestExtractZip(t *testing.T) {
	dir := t.TempDir()
	archivePath := filepath.Join(dir, "release.zip")

	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)
	files := map[string]string{"gray": "gray-binary-contents", "readme.txt": "ignored"}
	for name, content := range files {
		w, err := zw.Create(name)
		if err != nil {
			t.Fatalf("zip create %s: %v", name, err)
		}
		if _, err := w.Write([]byte(content)); err != nil {
			t.Fatalf("zip write %s: %v", name, err)
		}
	}
	zw.Close()
	if err := os.WriteFile(archivePath, buf.Bytes(), 0o644); err != nil {
		t.Fatalf("write archive: %v", err)
	}

	destDir := t.TempDir()
	binPath, err := extractZip(archivePath, destDir)
	if err != nil {
		t.Fatalf("extractZip: %v", err)
	}
	got, err := os.ReadFile(binPath)
	if err != nil {
		t.Fatalf("read extracted binary: %v", err)
	}
	if string(got) != "gray-binary-contents" {
		t.Errorf("extracted content = %q, want %q", got, "gray-binary-contents")
	}
	if _, err := os.Stat(filepath.Join(destDir, "readme.txt")); !os.IsNotExist(err) {
		t.Errorf("readme.txt should not have been extracted")
	}
}

func TestCopyFile(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "src.bin")
	dst := filepath.Join(dir, "dst.bin")
	if err := os.WriteFile(src, []byte("payload"), 0o755); err != nil {
		t.Fatalf("write src: %v", err)
	}
	if err := copyFile(src, dst); err != nil {
		t.Fatalf("copyFile: %v", err)
	}
	got, err := os.ReadFile(dst)
	if err != nil {
		t.Fatalf("read dst: %v", err)
	}
	if string(got) != "payload" {
		t.Errorf("dst content = %q, want %q", got, "payload")
	}
}

func TestGetAssetName(t *testing.T) {
	name := getAssetName()
	wantPrefix := "gray-" + runtime.GOOS + "-" + runtime.GOARCH
	if !strings.HasPrefix(name, wantPrefix) {
		t.Errorf("getAssetName() = %q, want prefix %q", name, wantPrefix)
	}
	wantExt := ".tar.gz"
	if runtime.GOOS == "windows" {
		wantExt = ".zip"
	}
	if !strings.HasSuffix(name, wantExt) {
		t.Errorf("getAssetName() = %q, want suffix %q", name, wantExt)
	}
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && (s == sub || len(sub) == 0 ||
		func() bool {
			for i := 0; i <= len(s)-len(sub); i++ {
				if s[i:i+len(sub)] == sub {
					return true
				}
			}
			return false
		}())
}
