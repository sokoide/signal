#!/usr/bin/env bash
# テスト用の両 OrbStack Linux マシンにビルドツールをインストール。
#
# 前提条件: マシンが既に存在すること（`make linux-machines` を実行）。
# OrbStack のデフォルトユーザはパスワードレス sudo を持つと想定。
# 持たない場合は sudo が対話的にパスワードを求める。
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

for m in arm64-linux-env x64-linux-env; do
    echo ">>> $m: installing build-essential"
    # in-linux.sh は VM 内で root として実行されるため、sudo は不要。
    ./in-linux.sh "$m" \
        'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential'
done

echo ">>> done. Verify:"
for m in arm64-linux-env x64-linux-env; do
    printf '%-18s ' "$m"
    ./in-linux.sh "$m" 'cc --version | head -1'
done
