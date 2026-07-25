// update_test.go — Tests for semver parsing, comparison, pre-release
// ordering, version range filtering, and release body changelog extraction.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import "testing"

func TestParseSemver(t *testing.T) {
	cases := []struct {
		in            string
		maj, min, pat int
		pre           string
	}{
		{"v3.0.0", 3, 0, 0, ""},
		{"3.0.0", 3, 0, 0, ""},
		{"v3.1.4", 3, 1, 4, ""},
		{"v3.0.0-beta.2", 3, 0, 0, "beta.2"},
		{"3.0.0-rc.1", 3, 0, 0, "rc.1"},
		{"v3.0.0-alpha", 3, 0, 0, "alpha"},
		// Build metadata is discarded
		{"v3.0.0+build.42", 3, 0, 0, ""},
		{"v3.0.0-beta.2+sha.abcdef", 3, 0, 0, "beta.2"},
		// Git-describe trailers are stripped
		{"v3.0.0-beta.2-4-g0cbcb58", 3, 0, 0, "beta.2"},
		{"v3.0.0-beta.2-4-g0cbcb58-dirty", 3, 0, 0, "beta.2"},
		{"v3.0.0-rc.1-12-gabcd123", 3, 0, 0, "rc.1"},
		// Missing components default to 0
		{"v2.1", 2, 1, 0, ""},
		{"v2", 2, 0, 0, ""},
		// Release-please component prefix (grayscale-v*)
		{"grayscale-v0.1.1", 0, 1, 1, ""},
		{"grayscale-v0.2.0-rc.1", 0, 2, 0, "rc.1"},
	}
	for _, c := range cases {
		t.Run(c.in, func(t *testing.T) {
			maj, min, pat, pre := parseSemver(c.in)
			if maj != c.maj || min != c.min || pat != c.pat || pre != c.pre {
				t.Errorf("parseSemver(%q) = %d.%d.%d-%q, want %d.%d.%d-%q",
					c.in, maj, min, pat, pre, c.maj, c.min, c.pat, c.pre)
			}
		})
	}
}

func TestCompareSemver(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		// Equal
		{"v3.0.0", "v3.0.0", 0},
		{"3.0.0", "v3.0.0", 0},
		{"v3.0.0-beta.2", "v3.0.0-beta.2", 0},

		// Major/minor/patch
		{"v3.0.0", "v2.9.9", 1},
		{"v2.9.9", "v3.0.0", -1},
		{"v3.1.0", "v3.0.9", 1},
		{"v3.0.1", "v3.0.0", 1},

		// Stable > pre-release (SemVer §11.3)
		{"v3.0.0", "v3.0.0-beta.2", 1},
		{"v3.0.0-beta.2", "v3.0.0", -1},
		{"v3.0.0", "v3.0.0-rc.1", 1},

		// Pre-release identifier ordering (§11.4)
		{"v3.0.0-beta.2", "v3.0.0-beta.1", 1},
		{"v3.0.0-beta.1", "v3.0.0-beta.2", -1},
		{"v3.0.0-beta.10", "v3.0.0-beta.2", 1}, // numeric compare, not string
		// numeric < alphanumeric
		{"v3.0.0-1", "v3.0.0-alpha", -1},
		{"v3.0.0-alpha", "v3.0.0-1", 1},
		// Shorter < longer when leading fields match
		{"v3.0.0-beta", "v3.0.0-beta.1", -1},
		{"v3.0.0-beta.1", "v3.0.0-beta", 1},
		// alpha < beta alphanumerically
		{"v3.0.0-alpha", "v3.0.0-beta", -1},
		{"v3.0.0-beta", "v3.0.0-alpha", 1},

		// Git-describe trailer must not affect ordering
		{"v3.0.0-beta.2-4-g0cbcb58-dirty", "v3.0.0-beta.2", 0},
		{"v3.0.0-beta.2-4-g0cbcb58", "v3.0.0-beta.1", 1},

		// Release-please component prefix (grayscale-v*)
		{"grayscale-v0.1.1", "grayscale-v0.1.0", 1},
		{"grayscale-v0.1.0", "v0.1.0", 0}, // prefix-stripped tags compare equal
	}
	for _, c := range cases {
		t.Run(c.a+"_vs_"+c.b, func(t *testing.T) {
			got := compareSemver(c.a, c.b)
			if got != c.want {
				t.Errorf("compareSemver(%q, %q) = %d, want %d", c.a, c.b, got, c.want)
			}
		})
	}
}

func TestIsNewerVersion(t *testing.T) {
	cases := []struct {
		local, remote string
		want          bool
	}{
		{"v3.0.0", "v3.0.1", true},
		{"v3.0.1", "v3.0.0", false},
		{"v3.0.0", "v3.0.0", false},
		{"v3.0.0-beta.2", "v3.0.0", true},  // stable > pre-release
		{"v3.0.0", "v3.0.0-beta.2", false}, // stable > pre-release
		{"v3.0.0-beta.1", "v3.0.0-beta.2", true},
		{"v3.0.0-beta.2", "v3.0.0-beta.1", false},
		// A local dev build with a git-describe trailer should still be
		// treated as its underlying pre-release when comparing to upstream.
		{"v3.0.0-beta.2-4-g0cbcb58-dirty", "v3.0.0-beta.2", false},
		{"v3.0.0-beta.2-4-g0cbcb58-dirty", "v3.0.0", true},
	}
	for _, c := range cases {
		t.Run(c.local+"_to_"+c.remote, func(t *testing.T) {
			got := isNewerVersion(c.local, c.remote)
			if got != c.want {
				t.Errorf("isNewerVersion(%q, %q) = %v, want %v", c.local, c.remote, got, c.want)
			}
		})
	}
}

func TestPickLatestPrerelease(t *testing.T) {
	t.Run("empty list", func(t *testing.T) {
		if got := pickLatestPrerelease(nil); got != nil {
			t.Errorf("want nil, got %+v", got)
		}
	})

	t.Run("no prereleases", func(t *testing.T) {
		rels := []GitHubRelease{
			{TagName: "grayscale-v0.1.0", Prerelease: false},
			{TagName: "grayscale-v0.0.9", Prerelease: false},
		}
		if got := pickLatestPrerelease(rels); got != nil {
			t.Errorf("want nil, got %+v", got)
		}
	})

	t.Run("picks highest semver prerelease", func(t *testing.T) {
		rels := []GitHubRelease{
			{TagName: "grayscale-v0.2.0", Prerelease: false},
			{TagName: "grayscale-v0.2.0-beta.1", Prerelease: true},
			{TagName: "grayscale-v0.3.0-beta.2", Prerelease: true},
			{TagName: "grayscale-v0.2.0-beta.10", Prerelease: true},
			{TagName: "grayscale-v0.2.0-beta.2", Prerelease: true},
		}
		got := pickLatestPrerelease(rels)
		if got == nil || got.TagName != "grayscale-v0.3.0-beta.2" {
			t.Errorf("want grayscale-v0.3.0-beta.2, got %+v", got)
		}
	})

	t.Run("ignores stable entries when picking prerelease", func(t *testing.T) {
		rels := []GitHubRelease{
			{TagName: "grayscale-v1.0.0", Prerelease: false},
			{TagName: "grayscale-v0.2.0-beta.2", Prerelease: true},
		}
		got := pickLatestPrerelease(rels)
		if got == nil || got.TagName != "grayscale-v0.2.0-beta.2" {
			t.Errorf("want grayscale-v0.2.0-beta.2, got %+v", got)
		}
	})
}

func TestExactSemverRE(t *testing.T) {
	cases := []struct {
		in   string
		want bool
	}{
		// Fully-qualified stable
		{"3.0.0", true},
		{"v3.0.0", true},
		{"0.0.1", true},
		{"12.34.56", true},
		// Fully-qualified pre-release
		{"3.0.0-beta.2", true},
		{"v3.0.0-beta.2", true},
		{"3.0.0-rc.1", true},
		{"3.0.0-alpha", true},
		// Build metadata
		{"3.0.0+build.42", true},
		{"3.0.0-beta.2+sha.abcdef", true},
		// Partials and shorthand — all rejected
		{"3.0", false},
		{"3", false},
		{"v3", false},
		{"v3.0", false},
		// Ranges — all rejected
		{"^3.0.0", false},
		{"~3.0.0", false},
		{">=3.0.0", false},
		{"3.0.x", false},
		// Keywords — rejected
		{"latest", false},
		{"stable", false},
		{"pre", false},
		// Empty / garbage
		{"", false},
		{"abc", false},
		{"3.0.0.0", false},
		// Leading dash in pre-release is invalid per semver
		{"3.0.0-", false},
	}
	for _, c := range cases {
		t.Run(c.in, func(t *testing.T) {
			got := exactSemverRE.MatchString(c.in)
			if got != c.want {
				t.Errorf("exactSemverRE.MatchString(%q) = %v, want %v", c.in, got, c.want)
			}
		})
	}
}

func TestNormalizeTag(t *testing.T) {
	cases := []struct {
		in, want string
	}{
		{"3.0.0", "3.0.0"},
		{"v3.0.0", "3.0.0"},
		{"v3.0.0-beta.2", "3.0.0-beta.2"},
		{"3.0.0-beta.2", "3.0.0-beta.2"},
		// Only one leading v is stripped — defensive
		{"vv3.0.0", "v3.0.0"},
		{"", ""},
		// Release-please component prefix
		{"grayscale-v0.1.1", "0.1.1"},
		{"grayscale-v0.2.0-rc.1", "0.2.0-rc.1"},
	}
	for _, c := range cases {
		t.Run(c.in, func(t *testing.T) {
			got := normalizeTag(c.in)
			if got != c.want {
				t.Errorf("normalizeTag(%q) = %q, want %q", c.in, got, c.want)
			}
		})
	}
}
