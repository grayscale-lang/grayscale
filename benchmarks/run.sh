#!/usr/bin/env bash
#
# run.sh - build the workloads, time their compile and run phases, print a
# table, and write benchmarks/results.json.
#
# Grayscale-only absolute numbers: no cross-language baseline, no regression
# gate. POSIX only; the Makefile target skips this on Windows.
#
#   BENCH_RUNS=N            run-phase iterations   (default 5)
#   BENCH_COMPILE_RUNS=N    compile-phase iters    (default 3)
#   GRAY=/path/to/gray      compiler to benchmark  (default ../gray)
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/.." && pwd)"
gen="$here/.gen"
work="$here/workloads"
bin="$gen/bin"
runner="$gen/runner"
results="$here/results.json"

GRAY="${GRAY:-$repo_root/gray}"
RUN_K="${BENCH_RUNS:-5}"
COMPILE_K="${BENCH_COMPILE_RUNS:-3}"

export BENCH_GEN="$gen"

if [ ! -x "$GRAY" ]; then
    echo "error: gray binary not found at $GRAY (run 'make build')" >&2
    exit 1
fi

tmp="$(mktemp)"
prior="$(mktemp)"
trap 'rm -f "$tmp" "$prior"' EXIT

kb() { awk "BEGIN { printf \"%.1f\", $1 / 1024 }"; }

print_header() { # <title> [column-width]
    printf '\n%s\n' "$1"
    printf '%-16s %*s %*s %*s\n' "workload" "${2:-12}" "compile_ms" "${2:-12}" "run_ms" "${2:-12}" "bin_kb"
    printf -- '------------------------------------------------------------------\n'
}

# ---- prior run ---------------------------------------------------------------
# Read the saved results.json before this run overwrites it. Each line is
# name, compile median, run median (empty for compile-only), binary bytes.
if [ -f "$results" ]; then
    prior_timestamp="$(sed -n 's/.*"timestamp": "\([^"]*\)".*/\1/p' "$results" | sed 's/T/ /; s/Z/ UTC/')"
    awk '/"name":/ {
        name = $0; sub(/.*"name": "/, "", name); sub(/".*/, "", name)
        compile = $0; sub(/.*"compile_ms": \{[^}]*"median": /, "", compile); sub(/,.*/, "", compile)
        run = ""
        if ($0 !~ /"run_ms": null/) {
            run = $0; sub(/.*"run_ms": \{[^}]*"median": /, "", run); sub(/,.*/, "", run)
        }
        bytes = $0; sub(/.*"binary_bytes": /, "", bytes); sub(/\}.*/, "", bytes)
        printf "%s\t%s\t%s\t%s\n", name, compile, run, bytes
    }' "$results" > "$prior"
fi

if [ -s "$prior" ]; then
    echo "Benchmark suite is running. Your results from the run before this one are below."
    print_header "Prior to Latest ($prior_timestamp)"
    awk -F'\t' '{
        printf "%-16s %12s %12s %12.1f\n", $1, $2, ($3 == "" ? "-" : $3), $4 / 1024
    }' "$prior"
fi

# Start each run from a clean slate: the fixtures are deterministic and
# take under a second to rebuild, so there is nothing to preserve.
echo "==> resetting $gen"
rm -rf "$gen"
mkdir -p "$bin"

echo "==> building stopwatch"
cc -O2 -o "$runner" "$here/runner.c"

echo "==> generating fixtures"
bash "$here/gen.sh"

# Workloads with both a compile and a run phase.
workloads="micro json_roundtrip raytracer lexer life kvstore sha256"

# ---- machine info -----------------------------------------------------------
os_name="$(uname -s)"
arch="$(uname -m)"
kernel="$(uname -r)"
if [ "$os_name" = "Darwin" ]; then
    cpu="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
    cores="$(sysctl -n hw.ncpu 2>/dev/null || echo 0)"
else
    cpu="$(awk -F: '/model name/ {sub(/^ */, "", $2); print $2; exit}' /proc/cpuinfo 2>/dev/null || echo unknown)"
    cores="$(nproc 2>/dev/null || echo 0)"
fi
cpu="${cpu//\\/}"
cpu="${cpu//\"/}"
commit="$(git -C "$repo_root" rev-parse --short HEAD 2>/dev/null || echo unknown)"
gray_version="$("$GRAY" version 2>&1 | awk '{ gsub(/\033\[[0-9;]*m/, "") } /Installed/ { print $2; exit }' || true)"
[ -n "$gray_version" ] || gray_version="unknown"
timestamp="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

# ---- run -------------------------------------------------------------------
value() { # <result-line> <key>
    printf '%s\n' "$1" | sed -n "s/.* $2=\([0-9.]*\).*/\1/p"
}

print_header "Latest Run ($(printf '%s' "$timestamp" | sed 's/T/ /; s/Z/ UTC/'))"

for name in $workloads; do
    src="$work/$name.gray"
    out="$bin/$name"

    c_line="$("$runner" "$COMPILE_K" "compile:$name" -- "$GRAY" build "$src" -o "$out")"
    r_line="$("$runner" "$RUN_K" "run:$name" -- "$out")"

    size_b="$(wc -c < "$out" | tr -d ' ')"
    printf '%-16s %12s %12s %12s\n' \
        "$name" "$(value "$c_line" median)" "$(value "$r_line" median)" "$(kb "$size_b")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" \
        "$(value "$c_line" min)" "$(value "$c_line" median)" "$(value "$c_line" mean)" \
        "$(value "$r_line" min)" "$(value "$r_line" median)" "$(value "$r_line" mean)" \
        "$size_b" >> "$tmp"
done

# Compile-stress: compile phase only (a big generated .gray file).
cs_line="$("$runner" "$COMPILE_K" "compile:compile_stress" -- "$GRAY" build "$gen/stress.gray" -o "$bin/compile_stress")"
cs_size="$(wc -c < "$bin/compile_stress" | tr -d ' ')"
printf '%-16s %12s %12s %12s\n' \
    "compile_stress" "$(value "$cs_line" median)" "-" "$(kb "$cs_size")"
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "compile_stress" \
    "$(value "$cs_line" min)" "$(value "$cs_line" median)" "$(value "$cs_line" mean)" \
    "" "" "" "$cs_size" >> "$tmp"

# ---- difference ------------------------------------------------------------
if [ -s "$prior" ]; then
    print_header "Difference (Latest subtracted by Prior to Latest)" 18
    awk -F'\t' '
    function cell(latest, before, scale,    diff, pct, diff_text, pct_text) {
        if (latest == "" || before == "") return "-"
        diff = (latest - before) / scale
        diff_text = sprintf("%.1f", diff)
        if (diff_text == "-0.0" || diff_text == "0.0") diff_text = "0.0"
        else diff_text = sprintf("%+.1f", diff)
        if (before + 0 == 0) return diff_text " (n/a)"
        pct = (latest - before) / before * 100
        pct_text = sprintf("%.1f", pct)
        if (pct_text == "-0.0" || pct_text == "0.0") pct_text = "0.0"
        else pct_text = sprintf("%+.1f", pct)
        return diff_text " (" pct_text "%)"
    }
    FNR == NR { compile[$1] = $2; run[$1] = $3; bytes[$1] = $4; next }
    {
        if (!($1 in compile)) { printf "%-16s %18s %18s %18s\n", $1, "-", "-", "-"; next }
        printf "%-16s %18s %18s %18s\n", $1, cell($3, compile[$1], 1), cell($6, run[$1], 1), cell($8, bytes[$1], 1024)
    }' "$prior" "$tmp"
else
    echo
    echo "No run prior to Latest"
fi

# ---- results.json ---------------------------------------------------------
{
    printf '{\n'
    printf '  "timestamp": "%s",\n' "$timestamp"
    printf '  "commit": "%s",\n' "$commit"
    printf '  "gray_version": "%s",\n' "$gray_version"
    printf '  "machine": {\n'
    printf '    "os": "%s",\n' "$os_name"
    printf '    "arch": "%s",\n' "$arch"
    printf '    "kernel": "%s",\n' "$kernel"
    printf '    "cpu": "%s",\n' "$cpu"
    printf '    "cores": %s\n' "$cores"
    printf '  },\n'
    printf '  "config": {"run_runs": %s, "compile_runs": %s},\n' "$RUN_K" "$COMPILE_K"
    printf '  "workloads": [\n'
    awk -F'\t' '{
        if (NR > 1) printf ",\n"
        printf "    {\"name\": \"%s\", \"compile_ms\": {\"min\": %s, \"median\": %s, \"mean\": %s}", $1, $2, $3, $4
        if ($5 == "") printf ", \"run_ms\": null"
        else printf ", \"run_ms\": {\"min\": %s, \"median\": %s, \"mean\": %s}", $5, $6, $7
        printf ", \"binary_bytes\": %s}", $8
    } END { printf "\n" }' "$tmp"
    printf '  ]\n'
    printf '}\n'
} > "$results"

echo
echo "results written to $results"
