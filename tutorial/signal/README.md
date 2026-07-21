# Signal チュートリアル

`tutorial/signal/` は、完成例を写経する場所ではありません。POSIX シグナルの基本契約を
小さな C 演習と contract test で確認する場所です。各ソースは意図的に `TODO(step N)`
の stub から始まります。まず自力で実装し、詰まったときだけ `completed/signal/` の関連例を
比較してください。

```sh
make run-tutorial-signal
make test-tutorial-signal
make test-tutorial-signal-03   # 個別に実行
```

## 進め方

1. Step の「観察したい事実」と TODO を読む。
2. `stepNN_*.c` の **その Step の TODO だけ**を実装する。
3. `make test-tutorial-signal-NN` を実行する。
4. PASS の理由を説明できてから次へ進む。

初期状態ではテストは失敗します。これは教材が壊れているのではなく、実装前の
期待どおりの状態です。`make test-tutorial-signal` は全 Step 完了後の確認用です。
テストは 5 秒で停止するため、待機処理を誤っても端末を占有しません。

## Step 一覧

| Step | 対象 | 観察したい事実 | 完成例 |
|---:|---|---|---|
| 1 | `step01_basics.c` | ハンドラは最小のフラグ更新だけをし、通常コードが結果を読む | `completed/signal/01_signal_basics.c` |
| 2 | `step02_sigaction.c` | `SA_SIGINFO` で配送理由を受ける。`SA_RESTART` は万能ではない | `completed/signal/02_sigaction.c` |
| 3 | `step03_mask_pending.c` | ブロック中の標準シグナルは pending になり、到着回数は保存されない | `completed/signal/03_blocking.c` |
| 4 | `step04_sigsuspend.c` | マスク交換と待機を原子的に行い、取りこぼしを防ぐ | `completed/signal/03_blocking.c` |
| 5 | `step05_selfpipe.c` | ハンドラは pipe へ通知し、イベント処理は通常コンテキストで行う | `completed/signal/07_selfpipe.c` |
| 6 | `step06_sigwait.c` | シグナルをブロックして同期的に取り出せば、ハンドラが不要になる | `completed/signal/06_realtime.c` |

### Step 1: 最小ハンドラ

`SIGUSR1` を自分自身へ送り、`volatile sig_atomic_t` のフラグが立つことを確認します。
ハンドラに `printf` を入れてはいけません。ここで学ぶのは「ハンドラは処理本体ではなく、
通常コンテキストへ渡す最小の通知」であることです。

### Step 2: `sigaction`

`signal()` ではなく `sigaction()` で `SA_SIGINFO` ハンドラを登録します。`siginfo_t` の
`si_pid` を確認します。`SA_RESTART` を付けても、全ての待機 API が再開するわけでは
ありません。テストを通すためにハンドラから I/O やメモリ確保をしないでください。

### Step 3: pending はキューではない

先に `SIGUSR1` をブロックしてから送信し、`sigpending()` で確認します。その後に
ブロックを解除してハンドラを実行します。同じ標準シグナルを複数回送っても、pending
中の個別到着数を数える設計にはしないでください。

### Step 4: `sigsuspend` で待つ

「フラグ確認 → `pause()`」の間にはシグナルを取りこぼす競合があります。先にブロック
して状態を整え、`sigsuspend()` に一時マスクを渡してください。戻り値は通常 `-1/EINTR`
ですが、重要なのは `woke` を再確認することです。

### Step 5: self-pipe

ハンドラは1バイトだけ `write()` し、main が読み取ります。`write()` は
async-signal-safe ですが、常に即時・完全に完了するわけではありません。この小さな演習は
短い通知だけを best-effort で扱います。実運用での nonblocking FD と満杯時の扱いは
`completed/signal/07_selfpipe.c` を確認してください。

### Step 6: 同期受信

対象セットをブロックし、`sigwaitinfo()` で1件ずつ取り出します。通常の制御フローで
`siginfo_t` を読めるため、スレッドプログラムやイベントループではハンドラより扱いやすい
ことが多い方式です。

`stepNN_*.c` が演習対象、`tests/stepNN_contract_test.c` が契約テストです。ハンドラ内では
async-signal-safe な操作だけを使い、`printf` や `malloc` は呼びません。
