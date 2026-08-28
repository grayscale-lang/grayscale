#!/usr/bin/env bash
#
# gen.sh - write deterministic benchmark fixtures into benchmarks/.gen/.
# The fixtures are deterministic; running this again just rewrites them.
# run.sh wipes .gen/ before calling this.
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
gen="$here/.gen"
mkdir -p "$gen"

echo "generating fixtures in $gen ..."

# 1. JSON blob: one flat object with 4000 string-valued keys (~300 KB).
awk 'BEGIN {
    printf "{"
    for (i = 0; i < 4000; i++) {
        if (i) printf ","
        printf "\"key_%d\":\"value number %d with filler text\"", i, i
    }
    printf "}\n"
}' > "$gen/blob.json"

# 2. Source file for the lexer: ~8000 lines of function-shaped text.
awk 'BEGIN {
    for (i = 0; i < 8000; i++) {
        printf "do func_%d(a int, b int) -> int { mut x int = a + b * %d; return x - %d }\n", i, i % 97, i % 13
    }
}' > "$gen/source.txt"

# 3. KV op log: 40000 SET/GET/DEL ops over 2000 keys. The key index is
# driven by i/5 and the op type by i%5, so every key sees every op type.
awk 'BEGIN {
    for (i = 0; i < 40000; i++) {
        k = (int(i / 5) * 7 + 11) % 2000
        m = i % 5
        if (m <= 1)      printf "SET key_%d value_%d\n", k, i
        else if (m <= 3) printf "GET key_%d\n", k
        else             printf "DEL key_%d\n", k
    }
}' > "$gen/ops.txt"

# 4. Blob to hash: ~500 KB of text.
awk 'BEGIN {
    line = ""
    for (j = 0; j < 32; j++) line = line "abcdefghijklmnopqrstuvwxyz0123456789 "
    for (i = 0; i < 12000; i++) print line
}' > "$gen/blob.bin"

# 5. Large .gray program for the compile-stress workload.
{
    awk 'BEGIN {
        for (i = 0; i < 4000; i++)
            printf "do helper_%d(n int) -> int { return n * %d + %d }\n", i, i % 7, i % 3
    }'
    echo "do main() {"
    echo "    mut total int = 0"
    awk 'BEGIN {
        for (i = 0; i < 4000; i++)
            printf "    total += helper_%d(%d)\n", i, i
    }'
    echo '    println("total=${total}")'
    echo "}"
} > "$gen/stress.gray"

echo "fixtures generated."
