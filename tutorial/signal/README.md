# Signal チュートリアル

`tutorial/signal/` は、完成例を写経する場所ではありません。POSIX シグナルの基本契約を
小さな C 演習と **振る舞い検証の契約テスト** で確認する場所です。各ソースは意図的に
`TODO(step N)` の stub から始まります。まず自力で実装し、詰まったときだけ
`completed/signal/` の関連例を比較してください。

以下のコマンドはリポジトリルートで実行します。

```sh
make test-tutorial-signal-NN   # Step NN だけ実装して確認（推奨）
make test-tutorial-signal      # 全 Step 完了後の確認
```

## このチュートリアルで説明できること

Step 1〜6 を終えると、次が説明できるようになります。

- ハンドラは「処理本体」ではなく「最小の通知」である理由（Step 1）
- `signal()` ではなく `sigaction()` を使う理由と `SA_SIGINFO` で得られる情報（Step 2）
- 標準シグナルが pending 中にマージされ、到着回数を保存しないこと（Step 3）
- フラグ確認と待機の間に存在する競合と、`sigsuspend` による原子化（Step 4）
- ハンドラから安全に通知を渡す self-pipe と `write` の境界（Step 5）
- ハンドラを使わず同期的に受ける `sigwait` の利点（Step 6）

## 契約テストの仕組み

`tests/stepNN_contract_test.c` は `tests/harness.h` 経由で、対応する `stepNN_*.c`
バイナリを実行し **振る舞いを検証** します。判定基準は両方とも満たす必要があります:

1. exit status が 0
2. stdout/stderr に各 Step 固有の観察文字列（例: `flag=1`）が含まれる

したがって `int main(void){ return 0; }` のような空実装は、観察文字列が出ないため
**FAIL になります**。これは教材が壊れているのではなく、実装前/不正解の期待どおりの状態です。
`make test-tutorial-signal` は全 Step 完了後の確認用で、テストは 5 秒で停止するため、
待機処理を誤っても端末を占有しません。

## 進め方

1. その Step の「観察したい事実」と TODO を読む。
2. `stepNN_*.c` の **その Step の TODO だけ** を実装する。
3. `make test-tutorial-signal-NN` を実行し、`PASS` の理由を説明できてから次へ進む。

## Step 一覧

各 Step は独立した1ファイル演習です。最後の「出力」列が契約テストの観察文字列です。

| Step | 対象 | 観察したい事実 | 出力 | 完成例 |
|---:|---|---|---|---|
| 1 | `step01_basics.c` | ハンドラは最小のフラグ更新だけをし、通常コードが結果を読む | `flag=1` | `completed/signal/01_signal_basics.c` |
| 2 | `step02_sigaction.c` | `SA_SIGINFO` で配送理由（`si_pid`）を受ける | `count=1` | `completed/signal/02_sigaction.c` |
| 3 | `step03_mask_pending.c` | ブロック中の標準シグナルは pending になり、到着回数は保存されない | `pending=1`・`deliveries=1` | `completed/signal/03_blocking.c` |
| 4 | `step04_sigsuspend.c` | マスク交換と待機を原子的に行い、取りこぼしを防ぐ | `woke=1` | 直接対応する完成例なし |
| 5 | `step05_selfpipe.c` | ハンドラは pipe へ通知し、イベント処理は通常コンテキストで行う | `match=1` | `completed/signal/07_selfpipe.c` |
| 6 | `step06_sigwait.c` | シグナルをブロックして同期的に取り出せば、ハンドラが不要になる | `match=1` | 直接対応する完成例なし（Linux 専用の発展例は `06_realtime.c`） |

`stepNN_*.c` が演習対象、`tests/stepNN_contract_test.c` が契約テストです。
ハンドラ内では async-signal-safe な操作だけを使い、`printf` や `malloc` は呼びません。

---

## Step 1: 最小ハンドラ

- **対象**: `step01_basics.c`（ハンドラ `on_usr1` と `main` の両方を埋める）
- **観察したい事実**: ハンドラは共有フラグ `seen` を立てるだけで、`printf` を呼んではならない。
  処理は通常コンテキストで行う。
- **実装順序**:
  1. `on_usr1` の本体に `seen = 1;` を書く（引数は `(void)signo;` で潰す）。
  2. `main` で `on_usr1` を `SIGUSR1` のハンドラとして登録（`sigaction` 推奨）。
  3. `raise(SIGUSR1)` で自分へ送る。
  4. `printf("flag=%d\n", (int)seen);` → `fflush(stdout);` → `return 0;`。
- **ヒント**: `raise()` は呼出スレッド宛て。`kill(getpid(), sig)` はプロセス宛てで意味が異なる
  が、単一スレッドでは同じ結果に見える。
- **PASS**: `PASS step 1: minimal handler sets a volatile sig_atomic_t flag`

## Step 2: `sigaction` と `SA_SIGINFO`

- **対象**: `step02_sigaction.c`（ハンドラ `on_usr1` は `si_pid>0` で `count++` する実装済み。
  `main` を埋める）
- **観察したい事実**: `SA_SIGINFO` 三引数ハンドラで `siginfo_t.si_pid` を読める。
  `SA_RESTART` は万能ではないため、本 Step では依存しない。
- **実装順序**:
  1. `struct sigaction act` をゼロ初期化、`sigemptyset(&act.sa_mask)`、
     `act.sa_sigaction = on_usr1`、`act.sa_flags = SA_SIGINFO`。
  2. `sigaction(SIGUSR1, &act, NULL)` で登録。
  3. `raise(SIGUSR1)`。
  4. `printf("count=%d\n", (int)count);` → `fflush(stdout);` → `return 0;`。
- **ヒント**: ハンドラ内で `printf` / `malloc` を呼ばない。`si_pid` に送信元 PID が入る
  （`raise` でも `getpid()` 相当が入る）。
- **PASS**: `PASS step 2: sigaction SA_SIGINFO captures si_pid from the sender`

## Step 3: pending はキューではない

- **対象**: `step03_mask_pending.c`（ハンドラ `on_usr1` は `deliveries` を加算する実装済み。
  `main` を埋める）
- **観察したい事実**: ブロック中に標準シグナルを複数回送っても pending は1つにマージされ、
  到着回数は保存されない。ブロック解除で1回だけ配送される。
- **実装順序**:
  1. `on_usr1` を `SIGUSR1` ハンドラとして `sigaction` で登録。
  2. `SIGUSR1` のセットを `sigprocmask(SIG_BLOCK)` でブロック。
  3. `raise(SIGUSR1)` を **2回** 送る。
  4. `sigpending(&pend)` して `sigismember(&pend, SIGUSR1)` を調べ、
     `printf("pending=%d\n", ismember ? 1 : 0);`。
  5. `sigprocmask(SIG_UNBLOCK)` で解除（ハンドラが1回走る）。
  6. `printf("deliveries=%d\n", (int)deliveries);` → `fflush(stdout);` → `return 0;`。
- **ヒント**: `deliveries` は「2回送っても1回しか配送されない」ことを観察する専用で、
  送信回数やイベント数を保存する仕組みではない。標準シグナルはハンドラに届く前に
  マージされるため、Step 5 の self-pipe を使っても失われた到着は復元できない。配送後の
  処理を通常コンテキストへ渡す用途には self-pipe、到着ごとの値や回数が必要なら通常の IPC
  または対応環境のリアルタイムシグナルを使う。
- **省略した前提**: この演習は単一スレッドで `raise(SIGUSR1)` を使い、`SA_NODEFER` を
  指定しない。そのため同じ `SIGUSR1` はハンドラ実行中に自動ブロックされ、ここでは
  `deliveries++` が同種ハンドラと競合しない。`sig_atomic_t` 自体が `++` の原子性を
  保証するわけではない。
- **PASS**: `PASS step 3: blocked standard signal is pending, then merges into one delivery`

## Step 4: `sigsuspend` で待つ

- **対象**: `step04_sigsuspend.c`（ハンドラ `on_usr1` は `woke=1` をセットする実装済み。
  `main` を埋める）
- **観察したい事実**: 「フラグ確認 → `pause()`」の間にはシグナルを取りこぼす競合がある。
  先にブロックして状態を整え、`sigsuspend()` に一時マスクを渡せば、マスク交換と待機が
  原子的に行われる。
- **実装順序**:
  1. `on_usr1` を `SIGUSR1` ハンドラとして登録。
  2. `SIGUSR1` を `sigprocmask(SIG_BLOCK, &blk, &oldmask)` でブロック（`oldmask` を保存）。
  3. `raise(SIGUSR1)` — ブロック中なので pending になる。
  4. `waitmask = oldmask` をコピーし、`sigdelset(&waitmask, SIGUSR1)` で SIGUSR1 を受ける状態に。
  5. `sigsuspend(&waitmask)` で原子的にマスク交換して待機。戻り値は通常 `-1/EINTR`。
  6. 戻ったら `woke` を再確認し、`sigprocmask(SIG_SETMASK, &oldmask, NULL)` で
     元のマスクを復元する。
  7. `printf("woke=%d\n", (int)woke);` → `fflush` → `return 0;`。
- **ヒント**: マスク設定を誤ると `sigsuspend` から戻らず、テストが `timeout 5` で
  強制終了して FAIL になる。
- **PASS**: `PASS step 4: sigsuspend swaps the mask and waits without losing the signal`

## Step 5: self-pipe

- **対象**: `step05_selfpipe.c`（ハンドラ `on_usr1` は pipe へ1バイト `write` する実装済み。
  `main` を埋める）
- **観察したい事実**: ハンドラは pipe への短い通知だけを行い、イベント処理は
  通常コンテキストで行う。`write()` は async-signal-safe だが即時完全完了は保証されない。
- **実装順序**:
  1. `pipe(pipefd)` を作る（`0` = 読み取り端、`1` = 書き込み端）。
  2. `on_usr1` を `SIGUSR1` ハンドラとして登録。
  3. `raise(SIGUSR1)`。
  4. `unsigned char b; read(pipefd[0], &b, 1);` で通知を受ける。
  5. `printf("match=%d\n", b == (unsigned char)SIGUSR1 ? 1 : 0);` → `fflush` → `return 0;`。
- **ヒント**: 実運用では書き込み端を nonblocking にし、`EAGAIN`（バッファ満杯）と
  partial write を扱う（`completed/signal/07_selfpipe.c` 参照）。この演習は1バイトの
  短い通知だけを best-effort で扱う。
- **PASS**: `PASS step 5: handler notifies the main loop through a pipe (write is async-signal-safe)`

## Step 6: 同期受信

- **対象**: `step06_sigwait.c`（`main` のみ。ハンドラは使わない）
- **観察したい事実**: 対象シグナルをブロックして `sigwait()` で同期的に取り出せば、
  ハンドラ不要で通常の制御フローの中で処理できる。イベントループやスレッドプログラムでは
  ハンドラより扱いやすい方式が多い。
- **実装順序**:
  1. `SIGUSR1` を含むセットを `sigemptyset` + `sigaddset` で作る。
  2. `sigprocmask(SIG_BLOCK, &set, NULL)` でブロック。
  3. `raise(SIGUSR1)` — ブロック中なので pending。
  4. `int sig = 0; sigwait(&set, &sig);`（成功で 0 を返し `sig` にシグナル番号）。
  5. `printf("match=%d\n", sig == SIGUSR1 ? 1 : 0);` → `fflush` → `return 0;`。
- **ヒント**: ハンドラは登録しない。ブロックしたまま `sigwait` で受け取る。
  `sigwaitinfo()` は `siginfo_t`（送信元・値）も取れて便利だが、
  **macOS は sigwaitinfo/sigtimedwait を未サポート**。本 Step は Linux/macOS 両方で
  動く `sigwait` を使う。sigwaitinfo とリアルタイムシグナルは
  `completed/signal/06_realtime.c`（Linux 専用）で扱う。
- **PASS**: `PASS step 6: sigwait receives a blocked signal synchronously, no handler needed`

---

## tutorial ↔ completed の対応

tutorial は概念を6 Step に圧縮しています。`completed/signal/` は10例＋番外編があり、
より広範な観察（タイマ・代替スタック・fork/exec・ハードウェア割り込みとの類似・安全性）を
扱います。本チュートリアルで扱わない主題と、それを読む完成例:

| 主題 | 完成例 | チュートリアルに入れない理由 |
|---|---|---|
| 実時間/CPU 時間タイマ | `04_timer.c` | 時間の種類に焦点が当たる別主題 |
| 代替スタック `sigaltstack` | `05_altstack.c` | スタック枯渇という異常系に特化 |
| リアルタイムシグナルのキューイング | `06_realtime.c`（Linux 専用） | macOS は sigwaitinfo/sigqueue 未サポート。Step 6 は `sigwait` に圧縮 |
| `ucontext_t` と ISR との類似 | `08_hw_interrupt.c` | POSIX 一般論の後の発展 |
| `fork`/`exec` と属性の継承 | `09_fork_exec.c` | プロセス境界という別軸 |
| async-signal-safe のまとめ | `10_signal_safety.c` | Step 1/5 で最小版を扱う |
| `printf` デッドロック再現 | `91_printf_deadlock.c` | 番外編（停止するため自動実行外） |

完成例を通しで読む順序は、ルート [`README.md`](../../README.md) の「読む順序」を参照してください。
