#!/usr/bin/env bash
# Host-side test orchestrator.
# For each OrbStack Linux machine: build, run all samples, normalize, and
# (in check mode) diff against the committed expected outputs.
#
# Usage:
#   scripts/check.sh           # run tests, diff vs tests/expected/
#   scripts/check.sh generate  # regenerate tests/expected/ (commit after review)
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

mode="${1:-check}"
machines=(arm64-linux-env x64-linux-env)
rc=0

for m in "${machines[@]}"; do
    echo "===== $m ====="
    if ! scripts/in-linux.sh "$m" scripts/check-linux.sh "$mode"; then
        echo "[$m] run failed" >&2
        rc=1
    fi
done

if [ "$mode" = "generate" ]; then
    echo "Expected outputs regenerated under tests/expected/{aarch64,x86_64}/"
    exit "$rc"
fi

# Compare each generated output against the committed expected file.
for arch in aarch64 x86_64; do
    for exp in tests/expected/$arch/*.txt; do
        [ -e "$exp" ] || continue
        n="$(basename "$exp")"
        got="tests/out/$arch/$n"
        if [ ! -e "$got" ]; then
            echo "MISSING output: $arch/$n" >&2
            rc=1
            continue
        fi
        if ! diff -u "$exp" "$got"; then
            echo "MISMATCH: $arch/$n" >&2
            rc=1
        else
            [ -n "${V:-}" ] && echo "PASS  $arch/$n"
        fi
    done
done

if [ "$rc" -eq 0 ]; then
    echo "ALL PASS (aarch64 + x86_64)"
fi
exit "$rc"
