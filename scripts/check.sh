#!/usr/bin/env bash
# ホスト側のテスト統括スクリプト。
# 各 OrbStack Linux マシンで: ビルド、全サンプル実行、正規化し、
# （チェックモードでは）コミット済み期待出力と diff する。
#
# 使い方:
#   scripts/check.sh           # テスト実行、completed/tests/expected/ と diff
#   scripts/check.sh generate  # completed/tests/expected/ を再生成（確認後にコミット）
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
    echo "Expected outputs regenerated under completed/tests/expected/{aarch64,x86_64}/"
    exit "$rc"
fi

# 生成された各出力をコミット済み期待ファイルと比較。
for arch in aarch64 x86_64; do
    for exp in completed/tests/expected/$arch/*.txt; do
        [ -e "$exp" ] || continue
        n="$(basename "$exp")"
        got="completed/tests/out/$arch/$n"
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
