# C で学ぶ POSIX シグナル

Linux と macOS で動かしながら、シグナルの生成・保留・配送・ハンドラ実行を観察する教材です。シグナルは「プロセス（正確にはスレッド）への非同期通知」であり、ハードウェア割り込みとの類似は構造上の比喩として扱います。仕様（POSIX）と実装（Linux/macOS）を混同しないことを重視します。

## まず 5 分で観察する

```sh
make
./01_signal_basics
./03_blocking
./04_timer
```

各プログラムの出力を予想してから実行してください。

1. `01_signal_basics`: `raise(SIGINT)` は呼び出しスレッドへ届く。`signal()` の登録と既定動作を確認する。
2. `03_blocking`: `SIG_BLOCK` 中に送ったシグナルが pending になり、解除時に配送される。
3. `04_timer`: 実時間タイマと CPU 時間タイマで配送条件が異なる。

次に `completed/signal/` のソースを上記の順で読み、各 API の動作を確認します。TODO を埋めながら学ぶ場合は [`tutorial/signal/README.md`](tutorial/signal/README.md) と `tutorial/signal/` を使ってください。チュートリアルの導入順はそちらを正とします。

## 読む順序

| 順 | 例 | 観察する概念 |
|---:|---|---|
| 1 | `completed/signal/01_signal_basics.c` | `signal`, `raise`, `SIG_IGN`/`SIG_DFL` |
| 2 | `completed/signal/02_sigaction.c` | `sigaction`, `siginfo_t`, 各種フラグ |
| 3 | `completed/signal/03_blocking.c` | マスク、pending、クリティカルセクション |
| 4 | `completed/signal/04_timer.c` | `alarm`, `setitimer` と時間の種類 |
| 5 | `completed/signal/05_altstack.c` | `sigaltstack`, `SA_ONSTACK` |
| 6 | `completed/signal/06_realtime.c` | RT シグナル、`sigqueue`, `si_value`（対応環境のみ） |
| 7 | `completed/signal/07_selfpipe.c` | シグナルを FD イベントへ橋渡し |
| 8 | `completed/signal/08_hw_interrupt.c` | ISR との構造的類似（`ucontext_t`） |
| 9 | `completed/signal/09_fork_exec.c` | `fork`/`exec` とマスク・処理系の継承 |
| 10 | `completed/signal/10_signal_safety.c` | async-signal-safe と `sig_atomic_t` |

番外編 `completed/signal/91_printf_deadlock.c` は、ハンドラから `printf` を呼ぶ危険をスレッド間のロック競合として再現します。意図的に停止するため `make run` には含まれません。

## シグナルの基本モデル（POSIX 一般論）

シグナルは「生成 → pending → 配送 → 処理 → 通常実行へ復帰」という流れをたどります。対象スレッドのマスクでブロックされている間は pending です。マスク解除などで配送可能になった時点で、既定動作・無視・登録済みハンドラのいずれかが適用されます。非同期シグナルの配送時点はプログラムから厳密には指定できません。

`raise(sig)` は自分自身に送る関数です。単一スレッドなら `kill(getpid(), sig)` と同じ結果になりますが、マルチスレッドでは呼び出しスレッドを対象にします（`pthread_kill(pthread_self(), sig)` 相当）。`kill(pid, sig)` はプロセスを対象にし、配送先スレッドはカーネルが選びます。特定スレッドへ送りたい場合は `pthread_kill` を使います。

同じ標準シグナルが、同じ配送単位（プロセスまたは対象スレッド）ですでに pending の間に再生成されても、POSIX は個々の到着をキューしません。実装上は一つにまとめられ、配送回数を数える用途には使えません。どの標準シグナルがいつ pending になるか、プロセス指向シグナルをどのスレッドが受けるかは、マスクと実装条件に依存します。到着ごとの保持が必要なら `SIGRTMIN`〜`SIGRTMAX` をシンボルで指定してください（番号を固定しない）。

## 最小 API

```c
struct sigaction sa = {0};
sa.sa_handler = handler;                 /* または sa_sigaction */
sigemptyset(&sa.sa_mask);
sa.sa_flags = SA_RESTART;                /* 必要な場合だけ選ぶ */
sigaction(SIGINT, &sa, NULL);

sigset_t set;
sigemptyset(&set);
sigaddset(&set, SIGINT);
sigprocmask(SIG_BLOCK, &set, NULL);
/* critical section */
sigprocmask(SIG_UNBLOCK, &set, NULL);
```

`SA_RESTART` は「一部の中断されたシステムコールを再開する」要求であり、すべての API やすべてのプラットフォームで有効ではありません。再開されるかに依存しないコードでは `EINTR` を確認し、必要ならループで再試行します。`sleep` 系や一部の I/O・ioctl などは再開されないことがあります。

## サンプルで追う要点

### `sigaction` と配送情報

`SA_SIGINFO` を指定すると `siginfo_t` で送信元 PID/UID、`si_code`、`sigqueue` の値を取得できます。`SA_NODEFER` は実行中の同じシグナルを自動ブロックしないため再入し、`SA_RESETHAND` は一回の配送後に処理を既定値へ戻します。必要性を確認してから使ってください。

### マスクと待機

マスクはスレッド単位です。単一スレッドでは `sigprocmask`、pthread では `pthread_sigmask` を使います。条件確認と待機を別々に行うと競合するため、実用コードでは `sigsuspend`、`pselect`、`ppoll`、または `sigwaitinfo` で原子的な待機を構成します。

### タイマと代替スタック

`ITIMER_REAL` は wall-clock、`ITIMER_VIRTUAL` はユーザ CPU 時間、`ITIMER_PROF` はユーザ＋カーネル CPU 時間を測り、それぞれ `SIGALRM`、`SIGVTALRM`、`SIGPROF` を生成します。`sigaltstack` と `SA_ONSTACK` は通常スタックが枯れたときのハンドラ用領域を用意しますが、スタック破壊そのものを修復する機能ではありません。

### Self-pipe と同期的受信

ハンドラではフラグを立てるか、`write` で pipe に通知し、処理は通常コンテキストで行います。Linux では `signalfd` が同じ設計を FD として提供します。ハンドラを使わず、シグナルをブロックして `sigwaitinfo`/`sigtimedwait` で取り出す方法もあります。

## async-signal-safe の境界

ハンドラから呼べる関数は POSIX が定める async-signal-safe 集合に限ります。`printf`、`malloc`、mutex 操作は含まれません。`write` は async-signal-safe ですが、呼び出しが常に即時完了するわけではありません。対象 FD が遅い・満杯ならブロックし、要求より少ないバイト数（partial write）を返すこともあります。ハンドラでは短い固定長通知にとどめ、戻り値を確認できる設計にしてください。

共有フラグには `volatile sig_atomic_t` の単一読み書きを使います。`++` のような read-modify-write の安全性は保証されないため、複数回の到着を数えるなら通常コンテキストでキューや pipe を処理します。

`91_printf_deadlock` は、main スレッドが stdio ロックを保持したまま、別スレッドのハンドラが `printf` で同じロックを待つ状況を示します。libc のロック再入性は実装差があるため、「単一スレッドなら必ずデッドロックする」と一般化しないでください。

## 標準シグナルとリアルタイムシグナル

シグナル番号は実装ごとに異なり得ます。ソースでは必ず `SIGINT` や `SIGTERM` などのマクロを使い、数値表や Linux 固定の 1〜31／34〜64 を前提にしないでください。`SIGSTKFLT`、`SIGPWR`、`SIGEMT` など非標準・拡張シグナルの有無も環境差があります。

RT シグナルは `SIGRTMIN` と `SIGRTMAX` の範囲で、同じ番号でも到着ごとにキューされ、優先順位などの規則が標準シグナルと異なります。`06_realtime` は RT シグナルを提供する環境でのみ実行してください。

## POSIX と Linux 実装を分けて読む

POSIX が規定するのは API の意味、マスク、ハンドラの呼出し規則、async-signal-safe などです。シグナル配送が「どの命令境界で起きるか」や内部データ構造は規定しません。

Linux の `do_signal`、`get_signal`、アーキテクチャ別の signal frame、`rt_sigreturn` は Linux カーネル固有の実装詳細です。`08_hw_interrupt` の `ucontext_t` ダンプやカーネルソースの読解は、この POSIX 一般論を理解した後の発展として扱ってください。macOS の Darwin 実装や `ucontext_t` の形は別物です。

## ビルドと実行

```sh
make completed       # 完成例をビルド（make と同じ）
make run-completed   # 完成例を自動実行（91 は除外）
make test-tutorial-signal-01  # 演習 Step 1 の契約を確認
make test-tutorial-signal     # 全 Step 完了後の確認
make clean
make 91_printf_deadlock
timeout 5 ./91_printf_deadlock  # 意図的に停止する番外編
```

チュートリアルは初期状態では TODO のため失敗します。これは正しい開始状態です。
各 Step を実装してから個別テストを通してください。

macOS で `timeout` が必要なら `brew install coreutils` 後に `gtimeout` を使います。`06_realtime` や Linux 固有 API は対応環境でのみ実行してください。検証手順と期待値は [`completed/tests/README.md`](completed/tests/README.md) を参照します。

## さらに学ぶ

- [`../threading/README.md`](../threading/README.md): `SIGVTALRM` を使ったユーザ空間プリエンプション。
- `man 7 signal`, `man 7 signal-safety`, `man 2 sigaction`, `man 3 sigwaitinfo`。
- Linux 固有 API: `signalfd(2)`, `pidfd_send_signal(2)`。

## リポジトリ構成

```
completed/signal/ 01〜10 の観察用プログラム、91 番外編（完成版）
tutorial/signal/ TODO 付き演習。導入は tutorial/signal/README.md
completed/tests/ 完成例の期待出力と検証資料
Makefile         ビルド・実行ターゲット
```
