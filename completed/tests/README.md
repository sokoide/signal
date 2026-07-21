# テスト — OrbStack デュアルアーキテクチャ Linux

サンプルは **Linux** 上で、**2つのアーキテクチャ**（`aarch64` と `x86_64`）で、
[OrbStack](https://orbstack.dev) マシンを介して実際に実行することで検証される。
macOS 単独では `06_realtime` を実行できない（POSIX リアルタイムシグナルは
Darwin で未サポート）。また両アーキテクチャで実行することで、
`08_hw_interrupt` の `ucontext_t` レジスタダンプのような
アーキテクチャ依存の動作（x86_64 の `RIP/RSP/RBP` と aarch64 の `PC/SP/FP`）を
検出できる。

## 前提条件

- OrbStack がインストールされ、`orb` / `orbctl` が PATH に含まれていること。

## 初回セットアップ

```sh
make linux-machines   # arm64-linux-env と x64-linux-env を作成（ubuntu:24.04）
make linux-setup      # 両方に build-essential をインストール（VM 内で root 実行）
```

## テストの実行

```sh
make check            # 両アーキで全サンプルをビルド + 実行、期待値と diff
```

`make check` は各マシン内で `make run` もスモークテストとして実行する
（スイート全体がハングやクラッシュなく完了する必要がある）。
成功時は `ALL PASS (aarch64 + x86_64)` のみが出力される。
不一致時は `diff -u` を表示し、非ゼロで終了する。

### 詳細モード: 何が検証されているかを確認

```sh
make check V=1
```

`V=1` は各マシン内でハーネスが行うすべてを順に表示する:

1. 完全なビルドログ（各サンプルの `cc ...` 行）、
2. `make run` のスモーク出力（生の出力、実際の PID 付き）、
3. 各サンプルの **正規化された出力**（PID/UID/アドレス → `<PID>` 等）、
4. サンプルごとの `PASS <arch>/NN.txt` 行、最後に `ALL PASS`。

これは緑の `ALL PASS` が実際に何を証明しているかを確認する推奨方法。

## 検証内容

各サンプルの正規化された出力はバイト単位で比較される。ハイライト:

- **`06_realtime`** — 標準シグナル（`SIGUSR1`）は5回送信しても1回の配送に
  マージされる一方、キューイングされたリアルタイムシグナル
  （`SIGRTMIN`、`SIGRTMIN+1`）はすべて優先順位通りに配送され、
  `sigqueue` のデータも intact（15イベント）。
- **`08_hw_interrupt`** — `SIGVTALRM` が5回発火。毎回ハンドラが保存された
  `PC`/`SP`/`FP`（aarch64）または `RIP`/`RSP`/`RBP`（x86_64）を
  `ucontext_t` からダンプし、ISR と POSIX シグナルハンドラの構造的対応を示す。
  アドレスは `0x<ADDR>` に正規化される。
- **`07_selfpipe`** — ハーネスが空の FIFO を stdin に保持して `select()` を
  ブロックさせ、`SIGINT` を注入。唯一の出口経路はセルフパイプであり、
  シグナル→FD変換とクリーンシャットダウンを証明する。
- **`01_signal_basics`** — `SIG_DFL` 下での `raise(SIGINT)` により
  意図的に終了（シグナルによる kill、rc 130）、成功として扱う。

## SIGSTKSZ の正規化（libc 非依存の期待出力）

`05_altstack` と `09_fork_exec` は代替スタックサイズを表示する。
これは `SIGSTKSZ`:

```
aarch64: size 20480 bytes      # glibc SIGSTKSZ on ARM64
x86_64:  size 8192  bytes      # glibc SIGSTKSZ on x86-64
```

`SIGSTKSZ` は libc に依存する（glibc 2.34+ では実行時評価; musl は異なる値）。
そのため、期待出力は `scripts/normalize.sed` 経由で `<SIGSTKSZ>` に正規化される。
これにより、コミットされた出力が 1 つのビルド環境の値に固定されるのではなく、
ディストリビューション/libc 間で再現可能になる。全 10 サンプルは正規化後に
両アーキテクチャ間でバイト単位で一致する。

## 期待出力

`completed/tests/expected/<arch>/NN.txt` にコミット済み（サンプルごとに1ファイル）、
**正規化済み**（`scripts/normalize.sed` 参照）:

- PID / UID → `<PID>` / `<UID>`
- 16進アドレス（ASLR、アーキテクチャポインタ幅）→ `0x<ADDR>`
- CPU 速度に依存するタイマカウント（04 ビジーループ）→ `<N>`

### 再生成

意図的に出力が変わる場合（コードやメッセージの編集）:

```sh
make expected         # completed/tests/expected/{aarch64,x86_64}/*.txt を書き換える
# git diff を確認し、コミット
```

## 便利なエイリアス（対話作業用）

```sh
alias orb-arm64='cd /Users/scott/repo/sokoide/signal && orb -m arm64-linux-env'
alias orb-x64='cd /Users/scott/repo/sokoide/signal && orb -m x64-linux-env'
# SSH アクセス:
#   ssh arm64-linux-env@orb
#   ssh x64-linux-env@orb
```

## ファイル

| パス | 役割 |
|------|------|
| `scripts/in-linux.sh` | ホストリポジトリルートからマシン内でコマンドを実行 |
| `scripts/linux-setup.sh` | 両マシンにビルドツールをインストール |
| `scripts/check.sh` | ホスト統括（マシンループ、diff） |
| `scripts/check-linux.sh` | マシン内: ビルド、実行、正規化 |
| `scripts/normalize.sed` | 変動値の正規化ルール |

## 注記

- 専用マシン（`arm64-linux-env`、`x64-linux-env`）は既存の他の OrbStack
  マシンとは分離されており、汚染を防ぐ。
- `07_selfpipe` は対話的。ハーネスが起動し、`SIGINT` を注入する。
- `01_signal_basics` は `SIG_DFL` 下での `raise(SIGINT)` により
  意図的に終了する（シグナルによる kill）— ハーネスは成功として扱う。
