#!/bin/bash
#
# check_vendor.sh — verify vendored third-party C source against
# grayc/src/vendor/MANIFEST.toml and report known security advisories.
#
# For each dependency in the manifest this:
#   * recomputes the SHA256 of every listed file and compares it to the
#     recorded hash
#   * checks that every tracked file under grayc/src/vendor/ is covered by
#     the manifest
#   * queries the OSV.dev API for advisories against the pinned version and
#     prints any that come back, grouped by CVE
#
# Exit codes:
#   0  every recorded hash matched (advisories, if any, printed as warnings)
#   1  a hash mismatch, a missing/uncovered file, or a malformed manifest
#   2  usage error
#
# Advisories never affect the exit code — they are informational, surfaced
# so a pinned version with a new CVE gets noticed on the monthly CI run. The
# manifest's `advisories_reviewed_through` date is the human review record.
#
# Requirements: bash, awk, and sha256sum or shasum. The OSV step additionally
# needs curl and jq; without them (or with --offline) the hash check still
# runs and the OSV step is skipped with a warning.

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_DIR="$PROJECT_ROOT/grayc/src/vendor"
MANIFEST="$VENDOR_DIR/MANIFEST.toml"
OSV_URL="https://api.osv.dev/v1/query"

OFFLINE=0
for arg in "$@"; do
    case "$arg" in
        --offline) OFFLINE=1 ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

if [ ! -f "$MANIFEST" ]; then
    echo "ERROR: $MANIFEST not found" >&2
    exit 1
fi

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

# Flatten the manifest to "<dep>|<key>|<value>" (and "<dep>|file|<name>|<hash>"
# for the [dependencies.<dep>.files] table). Deliberately a tiny subset of
# TOML — regular key = "value" lines under the section headers this script
# writes about, nothing more.
parse_manifest() {
    awk '
        /^[[:space:]]*#/                   { next }
        /^[[:space:]]*\[dependencies\.[A-Za-z0-9_.-]+\.files\][[:space:]]*$/ {
            dep = $0; sub(/^[[:space:]]*\[dependencies\./, "", dep)
            sub(/\.files\][[:space:]]*$/, "", dep); section = "files"; next
        }
        /^[[:space:]]*\[dependencies\.[A-Za-z0-9_.-]+\.osv\][[:space:]]*$/ {
            dep = $0; sub(/^[[:space:]]*\[dependencies\./, "", dep)
            sub(/\.osv\][[:space:]]*$/, "", dep); section = "osv"; next
        }
        /^[[:space:]]*\[dependencies\.[A-Za-z0-9_.-]+\][[:space:]]*$/ {
            dep = $0; sub(/^[[:space:]]*\[dependencies\./, "", dep)
            sub(/\][[:space:]]*$/, "", dep); section = "root"; next
        }
        /^[[:space:]]*\[/                  { section = "other"; next }
        /=/ {
            if (dep == "" || section == "other") next
            key = $0; sub(/=.*/, "", key); gsub(/[[:space:]"]/, "", key)
            val = $0; sub(/^[^=]*=/, "", val)
            sub(/[[:space:]]*#.*$/, "", val)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", val)
            gsub(/^"|"$/, "", val)
            if (key == "" || val == "") next
            if (section == "files")     print dep "|file|" key "|" val
            else if (section == "osv")  print dep "|osv_" key "|" val
            else if (section == "root") print dep "|" key "|" val
        }
    ' "$MANIFEST"
}

MAP="$(parse_manifest)"
if [ -z "$MAP" ]; then
    echo "ERROR: no [dependencies.*] entries parsed from $MANIFEST" >&2
    exit 1
fi

field() { # <dep> <key>
    echo "$MAP" | awk -F'|' -v d="$1" -v k="$2" '$1==d && $2==k {print $3; exit}'
}

FAIL=0
DEPS="$(echo "$MAP" | cut -d'|' -f1 | sort -u)"

# Every tracked vendor source file must be listed by some dependency.
COVERED="$(echo "$MAP" | awk -F'|' '$2=="file" {print $3}' | sort -u)"
if command -v git >/dev/null 2>&1 && git -C "$PROJECT_ROOT" rev-parse >/dev/null 2>&1; then
    while IFS= read -r tracked; do
        [ -n "$tracked" ] || continue
        base="${tracked##*/}"
        if ! echo "$COVERED" | grep -qx "$base"; then
            echo "ERROR: $tracked is tracked but not listed in MANIFEST.toml"
            FAIL=1
        fi
    done < <(git -C "$PROJECT_ROOT" ls-files 'grayc/src/vendor/*.c' 'grayc/src/vendor/*.h')
fi

for dep in $DEPS; do
    version="$(field "$dep" version)"
    license="$(field "$dep" license)"
    mods="$(field "$dep" modifications)"
    reviewed="$(field "$dep" advisories_reviewed_through)"
    echo "== $dep $version  (license: ${license:-?}, modifications: ${mods:-?})"

    # --- hashes ---
    files="$(echo "$MAP" | awk -F'|' -v d="$dep" '$1==d && $2=="file" {print $3"|"$4}')"
    if [ -z "$files" ]; then
        echo "  ERROR: no files listed for $dep"
        FAIL=1
    fi
    while IFS='|' read -r fname want; do
        [ -n "$fname" ] || continue
        path="$VENDOR_DIR/$fname"
        if [ ! -f "$path" ]; then
            echo "  MISSING  $fname"
            FAIL=1
            continue
        fi
        got="$(sha256_of "$path")"
        if [ "$got" = "$want" ]; then
            echo "  ok       $fname"
        else
            echo "  MISMATCH $fname"
            echo "             recorded $want"
            echo "             actual   $got"
            FAIL=1
        fi
    done <<EOF
$files
EOF

    # --- advisories (informational only) ---
    osv_name="$(field "$dep" osv_name)"
    osv_eco="$(field "$dep" osv_ecosystem)"
    if [ -z "$osv_name" ]; then
        echo "  advisories: no [dependencies.$dep.osv] name — skipped"
    elif [ "$OFFLINE" = 1 ]; then
        echo "  advisories: --offline — OSV check skipped"
    elif ! command -v curl >/dev/null 2>&1 || ! command -v jq >/dev/null 2>&1; then
        echo "  advisories: curl/jq not available — OSV check skipped"
    else
        if [ -n "$osv_eco" ]; then
            payload="$(printf '{"version":"%s","package":{"name":"%s","ecosystem":"%s"}}' \
                "$version" "$osv_name" "$osv_eco")"
        else
            payload="$(printf '{"version":"%s","package":{"name":"%s"}}' "$version" "$osv_name")"
        fi
        resp="$(curl -sS --max-time 30 -X POST -d "$payload" "$OSV_URL" 2>/dev/null || true)"
        if [ -z "$resp" ]; then
            echo "  advisories: OSV query failed (network?) — skipped"
        else
            listing="$(printf '%s' "$resp" | jq -r '
                [ .vulns[]?
                  | { id: (((.aliases // []) | map(select(startswith("CVE-"))) | first) // .id),
                      s:  ((.summary // (.details // "")) | gsub("\\s+"; " ") | .[0:110]) } ]
                | unique_by(.id) | sort_by(.id) | .[]
                | "  - \(.id): \(.s)"' 2>/dev/null || true)"
            if [ -z "$listing" ]; then
                echo "  advisories: none reported by OSV for $osv_name $version"
            else
                count="$(printf '%s\n' "$listing" | grep -c '^  - ')"
                echo "  advisories: OSV reports $count for $version (reviewed through ${reviewed:-never}):"
                printf '%s\n' "$listing"
                echo "  NOTE: triage these, then update advisories_reviewed_through"
                echo "        in MANIFEST.toml (and bump the dependency if warranted)."
            fi
        fi
    fi
    echo
done

if [ "$FAIL" -ne 0 ]; then
    echo "check_vendor: FAILED (hash mismatch or manifest problem)"
    exit 1
fi
echo "check_vendor: all recorded hashes verified"
exit 0
