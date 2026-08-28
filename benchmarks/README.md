# Grayscale benchmark suite

Real-shaped Grayscale programs, timed end to end. Run with:

```
make benchmark
```

This builds `gray`, generates input fixtures, compiles and runs each workload,
prints a table, and writes `benchmarks/results.json`.

These are **Grayscale-only absolute numbers** — how fast the compiled binaries
run, how long `grayc` takes to compile them, and how big the binaries are.
There is no C/Go/Rust baseline, no ratio, no regression gate, and no CI job.
POSIX only; the `make` target prints a notice and exits 0 on Windows.

## Workloads

| Name | What it does | Exercises |
|------|--------------|-----------|
| `json_roundtrip` | Decode a large JSON object to a map, re-encode, repeat | json, maps, strings, arenas |
| `raytracer` | Render a small fixed scene to an in-memory buffer | float math, structs, recursion |
| `lexer` | Tokenize a large generated source file | char/byte scanning, string building |
| `life` | N generations of Game of Life on a fixed grid | integer arrays, tight loops, copy semantics |
| `kvstore` | Replay a generated SET/GET/DEL log against a map | `map[string:string]`, string splitting |
| `sha256` | Hash a large generated blob in chunks, many times | crypto, io, string slicing |
| `compile_stress` | Time `gray build` on a large generated `.gray` file | full `grayc` pipeline (compile phase only) |

## Layout

```
benchmarks/
  README.md      this file
  run.sh         orchestrator: builds, times, writes results.json, prints the table
  gen.sh         writes deterministic fixtures into .gen/
  runner.c       ~90-line CLOCK_MONOTONIC stopwatch, compiled once by run.sh
  workloads/     the workload .gray sources (git-tracked)
  .gen/          generated fixtures + built binaries (gitignored)
  results.json   latest run (gitignored)
```

`runner.c` runs a command `K` times after one discarded warmup and reports
min / median / mean wall time. `cc` is the only tool assumed present —
`date +%N`, `python3`, and `hyperfine` are not.

## Knobs

| Env var | Default | Effect |
|---------|---------|--------|
| `BENCH_RUNS` | 5 | Run-phase iterations per workload |
| `BENCH_COMPILE_RUNS` | 3 | Compile-phase iterations per workload |
| `GRAY` | `../gray` | Compiler binary to benchmark |

Fixtures are generated once and reused; delete `benchmarks/.gen/` to force a
rebuild.

## results.json

```json
{
  "timestamp": "...", "commit": "...", "gray_version": "...",
  "machine": { "os": "...", "arch": "...", "kernel": "...", "cpu": "...", "cores": 8 },
  "config": { "run_runs": 5, "compile_runs": 3 },
  "workloads": [
    {
      "name": "life",
      "compile_ms": { "min": 0.0, "median": 0.0, "mean": 0.0 },
      "run_ms":     { "min": 0.0, "median": 0.0, "mean": 0.0 },
      "binary_bytes": 0
    }
  ]
}
```

`run_ms` is `null` for `compile_stress`.
