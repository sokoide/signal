#!/usr/bin/env bash
# OrbStack Linux マシン *内部* で実行される。
# 全サンプルをビルドし、各サンプルを実行し（サンプルごとに個別処理）、
# 出力を正規化して completed/tests/<out|expected>/<arch>/NN.txt に書き込む。
#
# 使い方（scripts/in-linux.sh <マシン名> 経由で呼び出し）:
#   scripts/check-linux.sh check      # completed/tests/out/<arch>/ に書き込み
#   scripts/check-linux.sh generate   # completed/tests/expected/<arch>/ に書き込み
set -uo pipefail

mode="${1:-check}"
cd "$(dirname "${BASH_SOURCE[0]}")/.."

arch="$(uname -m)"            # aarch64 | x86_64
case "$arch" in
    aarch64|x86_64) ;;
    *) echo "unsupported architecture: $arch" >&2; exit 2 ;;
esac
case "$mode" in
    generate) dest="completed/tests/expected/$arch" ;;
    *)        dest="completed/tests/out/$arch" ;;
esac
mkdir -p "$dest"
rm -f "$dest"/*.txt

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

# run_norm <番号> <バイナリ名> <タイムアウト秒数>
# 自己終了型サンプルを実行し、出力を結合して取得し、その場で正規化する。
# 終了コードにかかわらず続行する（出力は後で diff される）が、
# 予期しない非ゼロ終了については警告を出す。
# クラッシュ/ハングがクリーンな diff の背後に隠れないようにするため。
run_failed=0
run_norm() {
    local num="$1" bin="$2" tmo="$3" expected_rc="$4"
    timeout "$tmo" "./$bin" >"$dest/$num.txt" 2>&1
    local rc=$?
    if [ "$rc" -ne "$expected_rc" ]; then
        echo "[$arch] ERROR: $bin exited rc=$rc (expected $expected_rc)" >&2
        run_failed=1
    fi
    sed -i -f scripts/normalize.sed "$dest/$num.txt"
    if [ -n "${V:-}" ]; then
        echo "----- $bin (normalized output) -----"
        cat "$dest/$num.txt"
    fi
}

# 01 は SIG_DFL 下での raise(SIGINT) により終了 — シグナルによる kill（rc 130）、想定内。
run_norm 01 01_signal_basics 10 130
run_norm 02 02_sigaction     10 0
run_norm 03 03_blocking      10 0
run_norm 04 04_timer         15 0
run_norm 05 05_altstack      10 139
run_norm 06 06_realtime      15 0
run_norm 08 08_hw_interrupt  15 0
run_norm 09 09_fork_exec     10 0
run_norm 10 10_signal_safety 15 0

# 07 は対話的（select() ループ）。EOF にならない stdin（親が保持する空の FIFO）
# を与えることで select() をブロックさせ、唯一の脱出経路をセルフパイプにする。
# その後 SIGINT を注入し、パイプ経由でシャットダウンさせる。
fifo=$(mktemp -u /tmp/07.fifo.XXXXXX)
mkfifo "$fifo"
exec 9<>"$fifo"            # 書き込み FD を保持し、読み取り端が EOF を見ないようにする
./07_selfpipe >"$dest/07.txt" 2>&1 <"$fifo" &
pid07=$!
# A watchdog prevents a broken self-pipe path from hanging the whole suite.
(sleep 10; kill -TERM "$pid07" 2>/dev/null || true; sleep 1; kill -KILL "$pid07" 2>/dev/null || true) &
watchdog07=$!
sleep 0.6
kill -INT "$pid07" 2>/dev/null || true
wait "$pid07" 2>/dev/null
rc07=$?
kill "$watchdog07" 2>/dev/null || true
wait "$watchdog07" 2>/dev/null || true
if [ "$rc07" -ne 0 ]; then
    echo "[$arch] ERROR: 07 exited rc=$rc07 (expected clean shutdown via self-pipe, exit 0)" >&2
    run_failed=1
fi
exec 9>&-
rm -f "$fifo"
sed -i -f scripts/normalize.sed "$dest/07.txt"
if [ -n "${V:-}" ]; then
    echo "----- 07_selfpipe (normalized output) -----"
    cat "$dest/07.txt"
fi

echo "[$arch] wrote $dest"
exit "$run_failed"
