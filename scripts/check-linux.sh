#!/usr/bin/env bash
# Runs INSIDE an OrbStack Linux machine.
# Builds all samples, runs each (with per-sample handling), normalizes output,
# and writes it to tests/<out|expected>/<arch>/NN.txt.
#
# Usage (invoked via scripts/in-linux.sh <machine>):
#   scripts/check-linux.sh check      # write to tests/out/<arch>/
#   scripts/check-linux.sh generate   # write to tests/expected/<arch>/
set -uo pipefail

mode="${1:-check}"
cd "$(dirname "${BASH_SOURCE[0]}")/.."

arch="$(uname -m)"            # aarch64 | x86_64
case "$mode" in
    generate) dest="tests/expected/$arch" ;;
    *)        dest="tests/out/$arch" ;;
esac
mkdir -p "$dest"

echo "[$arch] build..."
if [ -n "${V:-}" ]; then
    make clean && make || { echo "[$arch] BUILD FAILED" >&2; exit 1; }
else
    if ! make clean >/dev/null 2>&1 || ! make >/dev/null 2>&1; then
        echo "[$arch] BUILD FAILED" >&2
        exit 1
    fi
fi

echo "[$arch] smoke: make run (must complete without hanging)"
if [ -n "${V:-}" ]; then
    timeout 60 make run
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "[$arch] make run SMOKE FAILED (hang or crash, rc=$rc)" >&2
        exit 1
    fi
else
    if ! timeout 60 make run >/dev/null 2>&1; then
        echo "[$arch] make run SMOKE FAILED (hang or crash)" >&2
        exit 1
    fi
fi

# run_norm <num> <bin> <timeout_secs>
# Runs a self-terminating sample, captures combined output, normalizes in place.
# Continues regardless of exit code (output is still diffed later), but warns on
# unexpected non-zero exits so a crash/hang doesn't hide behind a clean diff.
run_norm() {
    local num="$1" bin="$2" tmo="$3"
    timeout "$tmo" "./$bin" >"$dest/$num.txt" 2>&1
    local rc=$?
    # 01 exits via raise(SIGINT) under SIG_DFL (rc 130) — expected.
    # 05 deliberately _exit(128 + SIGSEGV) (rc 139) from its handler — expected.
    if [ "$rc" -ne 0 ] && [ "$num" != "01" ] && [ "$num" != "05" ]; then
        echo "[$arch] WARNING: $bin exited rc=$rc (output captured; will be flagged by diff if wrong)" >&2
    fi
    sed -i -f scripts/normalize.sed "$dest/$num.txt"
    if [ -n "${V:-}" ]; then
        echo "----- $bin (normalized output) -----"
        cat "$dest/$num.txt"
    fi
}

# 01 exits via raise(SIGINT) under SIG_DFL — killed by signal (rc 130), expected.
run_norm 01 01_signal_basics 10
run_norm 02 02_sigaction     10
run_norm 03 03_blocking      10
run_norm 04 04_timer         15
run_norm 05 05_altstack      10
run_norm 06 06_realtime      15
run_norm 08 08_hw_interrupt  15
run_norm 09 09_fork_exec     10
run_norm 10 10_signal_safety 15

# 07 is interactive (select() loop). Give it a stdin that never EOFs (an empty
# FIFO held open by the parent) so select() blocks on it and the ONLY way out
# is the self-pipe path. Then inject SIGINT and let it shut down via the pipe.
fifo=$(mktemp -u /tmp/07.fifo.XXXXXX)
mkfifo "$fifo"
exec 9<>"$fifo"            # keep a write fd open so the read end never sees EOF
./07_selfpipe >"$dest/07.txt" 2>&1 <"$fifo" &
pid07=$!
sleep 0.6
kill -INT "$pid07" 2>/dev/null || true
wait "$pid07" 2>/dev/null
rc07=$?
if [ "$rc07" -ne 0 ]; then
    echo "[$arch] WARNING: 07 exited rc=$rc07 (expected clean shutdown via self-pipe, exit 0)" >&2
fi
exec 9>&-
rm -f "$fifo"
sed -i -f scripts/normalize.sed "$dest/07.txt"
if [ -n "${V:-}" ]; then
    echo "----- 07_selfpipe (normalized output) -----"
    cat "$dest/07.txt"
fi

echo "[$arch] wrote $dest"
