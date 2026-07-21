# Completed examples

このディレクトリは教材の完成例です。各プログラムは単体でビルドでき、リポジトリ
ルートでは次のように実行できます。

```sh
make completed
make run-completed
```

最初に動きを観察してから、同じ概念を小さく実装する
[`../../tutorial/signal/`](../../tutorial/signal/README.md) へ進んでください。

| 完成例 | 主題 |
|---|---|
| `01_signal_basics.c` | 最小ハンドラ、既定動作、`raise` |
| `02_sigaction.c` | `sigaction`、配送情報、主要フラグ |
| `03_blocking.c` | シグナルマスクと pending |
| `04_timer.c` | 実時間・CPU時間タイマ |
| `05_altstack.c` | 代替スタック |
| `06_realtime.c` | リアルタイムシグナルと同期受信 |
| `07_selfpipe.c` | self-pipe によるイベントループ統合 |
| `08_hw_interrupt.c` | 保存コンテキストの観察 |
| `09_fork_exec.c` | `fork` / `exec` 後の属性 |
| `10_signal_safety.c` | async-signal-safe な最小処理 |
| `91_printf_deadlock.c` | 意図的なデッドロック実験（手動実行のみ） |

`91_printf_deadlock.c` は終了しません。`timeout 5 ./91_printf_deadlock`（macOSでは
`gtimeout`）で実行してください。
