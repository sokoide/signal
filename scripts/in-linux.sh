#!/usr/bin/env bash
# OrbStack Linux マシン内でコマンドを実行する（ホストのリポジトリルートから）。
#
# OrbStack は macOS のファイルシステムを全マシン内の同一絶対パスにマウントするため、
# ホストリポジトリをその場で編集/ビルド/実行できる — コピー不要。
#
# 使い方: scripts/in-linux.sh <マシン名> <コマンド...>
# 例:
#   scripts/in-linux.sh x64-linux-env make
#   scripts/in-linux.sh arm64-linux-env ./06_realtime
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <machine> <command...>" >&2
    exit 2
fi

machine="$1"
shift

# マシンが起動していることを確認（既に起動していれば何もしない）。
orbctl start "$machine" >/dev/null 2>&1 || true

# ホスト上のリポジトリルートを解決。同じパスが VM 内でも使われる。
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# VM 内で root として実行。OrbStack は Linux root をマウントされたファイル
# システムの macOS 所有者にマップするため、VM から書き込まれたファイルは
# ホストユーザが所有する — 所有権の驚きがなく、パッケージインストールに
# sudo も不要。コマンドはリモートシェルのパースを受けるため、引数は
# シンプルに保つこと（引用符/スペースを埋め込まない）。

# 選択したホスト環境変数を渡す（現在は V: 詳細テスト出力用）。
prefix=""
[ -n "${V:-}" ] && prefix="export V=1; "

orb -m "$machine" -u root bash -lc "cd '$repo' && ${prefix}$*"
