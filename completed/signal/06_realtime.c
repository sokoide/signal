/*
 * 06_realtime.c — POSIX リアルタイムシグナルと sigqueue()
 *
 * 標準シグナル（1–31）とは異なり、リアルタイムシグナルには2つの重要な
 * 特性がある:
 *
 * 1. キューイング
 *    標準シグナルは「マージ」される:
 * ブロック中に同じシグナルが複数回到着しても、
 *    保留は1回だけ。リアルタイムシグナルは個別にキューイングされ、
 *    すべて配送される。
 *
 * 2. データ運搬
 *    sigqueue(2) を使うと、シグナルに整数またはポインタを添付できる。
 *    受信側は siginfo_t.si_value から読み取る。
 *
 * リアルタイムシグナルの範囲:
 *    Linux: SIGRTMIN .. SIGRTMAX（通常 34 .. 64）
 *    番号の小さいシグナルが大きいものより先に配送される。
 *
 * このサンプルで使用する安全な非同期ハンドラ設計:
 *    - SIGUSR1 には最小限のハンドラ（フラグ更新のみ）。
 *      ルール: 非同期ハンドラからは volatile sig_atomic_t 以外の共有状態に
 *      決して触れてはならない。
 *    - リアルタイムシグナルにはハンドラをインストールしない（SIG_DFL）。
 *      代わりにブロックしたまま sigtimedwait(2) で同期的に収集する。
 *      siginfo_t.si_value データはメインスレッドで安全に集める。
 *      これは本番コードで推奨されるパターン（詳細は README「発展トピック」の
 *      sigwaitinfo と self-pipe trick の節を参照）。
 *
 * 注:
 *    macOS（Darwin）は POSIX リアルタイムシグナルと sigqueue() を
 *    サポートしていない。そのため、コンパイル時に SIGRTMIN が定義されて
 *    いない場合はメッセージを表示して終了する。
 *
 * ビルド:
 *    cc -std=c11 -Wall -Wextra -O2 -g 06_realtime.c -o 06_realtime -lrt
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * リアルタイムシグナルがないプラットフォーム（例: macOS）では、
 * コンパイル時に分岐して案内メッセージを表示する。
 */
#ifndef SIGRTMIN
int main(void) {
    printf(
        "POSIX real-time signals (SIGRTMIN/SIGRTMAX) are not available on "
        "this platform.\n"
        "Try running this example on Linux.\n");
    return 0;
}
#else

/*
 * メインスレッドがリアルタイムシグナルから収集したイベントデータ。
 * 非同期ハンドラはこのデータに触れないため、volatile は不要であり、
 * 競合も発生しない。
 */
#define MAX_EVENTS 64
static struct {
    int sig;
    int data;
} g_events[MAX_EVENTS];
static int g_event_count = 0;

/*
 * 標準シグナル SIGUSR1 の配送フラグ。
 * これだけが非同期ハンドラによって更新される変数なので、
 * volatile sig_atomic_t にする。sig_atomic_t が保証するのは
 * 単一の代入（書き込み）のみ — ここでは読み出し・変更・書き込みの
 * パターンではない。ブロック中に5回送っても1つの保留ビットに
 * マージされ、ハンドラは1回だけ実行されてこのフラグをセットする。
 */
static volatile sig_atomic_t g_usr1_received = 0;

/*
 * SIGUSR1 ハンドラ。
 *
 * 安全な非同期シグナルハンドラのルール:
 *   - volatile sig_atomic_t 変数だけを読み書きする。
 *   - stdio（printf など）や複雑な共有データ構造には触れない。
 * もし複数のシグナルに添付された「データ」を処理する必要があれば、
 * ハンドラ内で行わない。代わりに、このサンプルのようにリアルタイム
 * シグナルを同期的に収集する（sigwaitinfo/sigtimedwait）か、
 * self-pipe trick を使ってメインループに処理を委ねる（07_selfpipe.c 参照）。
 */
static void usr1_handler(int sig) {
    (void)sig;
    g_usr1_received = 1;
}

static void print_events(void) {
    int n = g_event_count;
    printf("Collected %d real-time signal event(s) via sigtimedwait:\n", n);
    for (int i = 0; i < n; i++) {
        printf("  event #%02d: signal=%d (SIGRTMIN%+d), data=%d\n", i + 1,
               g_events[i].sig, g_events[i].sig - SIGRTMIN, g_events[i].data);
    }
}

int main(void) {
    printf("=== POSIX real-time signal demo ===\n");
    printf("SIGRTMIN = %d, SIGRTMAX = %d\n", SIGRTMIN, SIGRTMAX);

    if (SIGRTMIN > SIGRTMAX) {
        printf("No real-time signals available.\n");
        return 0;
    }

    /*
     * ============================================================
     * ステップ 1: 最小限の SIGUSR1 ハンドラを登録
     * ============================================================
     *
     * SIGUSR1 には g_usr1_received フラグだけをセットする安全なハンドラ。
     * SA_SIGINFO はここでは不要。
     *
     * リアルタイムシグナルにはハンドラを登録せず、ブロックしたまま
     * sigtimedwait() で同期的に収集する。
     */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = usr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * ステップ 2: 対象のシグナルをブロック
     * ============================================================
     *
     * SIGUSR1、SIGRTMIN、SIGRTMIN+1 をブロックする。これはユーザ空間で
     * 「割り込み禁止（cli）」に相当する。ブロック中に送られたシグナルは
     * 保留状態になり、後で処理される。
     */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigaddset(&block_set, SIGRTMIN);
    sigaddset(&block_set, SIGRTMIN + 1);

    if (sigprocmask(SIG_BLOCK, &block_set, &old_set) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * ステップ 3: 標準シグナルを5回送信
     * ============================================================
     *
     * SIGUSR1 は標準シグナルなので、ブロック中に5回到着しても
     * 保留ビットは1つだけ。後でハンドラは1回だけ実行される。
     */
    printf("\nSending SIGUSR1 5 times (standard signal, should merge)...\n");
    for (int i = 0; i < 5; i++) {
        if (kill(getpid(), SIGUSR1) == -1) {
            perror("kill(SIGUSR1)");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * ============================================================
     * ステップ 4: sigqueue() でリアルタイムシグナルを5回キューイング
     * ============================================================
     *
     * SIGRTMIN を sigqueue() で5回送信すると、それぞれが個別のキュー
     * エントリになり、各々が独自のデータを持つ。シグナルはブロック中なので
     * 保留状態になる。
     */
    printf(
        "Sending SIGRTMIN 5 times via sigqueue (real-time, should "
        "queue)...\n");
    for (int i = 0; i < 5; i++) {
        union sigval value;
        value.sival_int = 100 + i;
        if (sigqueue(getpid(), SIGRTMIN, value) == -1) {
            perror("sigqueue(SIGRTMIN)");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * ============================================================
     * ステップ 5: 別のリアルタイムシグナルを送信（優先順位デモ）
     * ============================================================
     *
     * 配送順のルール:
     *   - 標準シグナル間の順序は未規定。
     *   - リアルタイムシグナル間では、番号の小さい方が
     *     大きいものより先に配送される。
     *
     * 意図的に番号の大きい SIGRTMIN+1 を先にキューイングし、
     * 次に番号の小さい SIGRTMIN をキューイングする。
     * sigtimedwait() で収集すると、SIGRTMIN のイベントが
     * SIGRTMIN+1 より先に現れるはず。
     */
    printf(
        "Sending SIGRTMIN+1 first, then SIGRTMIN to show priority "
        "order...\n");
    for (int i = 0; i < 5; i++) {
        union sigval value;
        value.sival_int = 200 + i;
        if (sigqueue(getpid(), SIGRTMIN + 1, value) == -1) {
            perror("sigqueue(SIGRTMIN+1)");
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < 5; i++) {
        union sigval value;
        value.sival_int = 300 + i;
        if (sigqueue(getpid(), SIGRTMIN, value) == -1) {
            perror("sigqueue(SIGRTMIN)");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * ============================================================
     * ステップ 6: SIGUSR1 を配送してマージを確認
     * ============================================================
     *
     * SIGUSR1 のみブロック解除（ユーザ空間の「sti」に相当）すると、
     * 保留中の SIGUSR1 が即座に配送され、ハンドラが1回実行される。
     * リアルタイムシグナルはブロックされたまま。
     */
    sigset_t usr1_set;
    sigemptyset(&usr1_set);
    sigaddset(&usr1_set, SIGUSR1);
    if (sigprocmask(SIG_UNBLOCK, &usr1_set, NULL) == -1) {
        perror("sigprocmask(SIG_UNBLOCK, SIGUSR1)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * ステップ 7: リアルタイムシグナルを同期的に収集
     * ============================================================
     *
     * sigtimedwait(2) は、指定されたセット内のいずれかのシグナルが
     * 保留状態になるまで待機し、1つのシグナルをデキューして詳細を
     * siginfo_t に格納する。ハンドラが関与しないため、si_value データと
     * 共有状態をメインスレッドで安全に処理できる。
     *
     * - 戻り値はデキューされたシグナル番号。
     * - タイムアウト（ここでは 100ms）時の EAGAIN は、これ以上
     *   キューイングされたシグナルがないことを意味するためループを抜ける。
     * - 同じ番号のリアルタイムシグナルは FIFO。異なる番号間では
     *   番号の小さい方が先に来る。
     */
    sigset_t rt_set;
    sigemptyset(&rt_set);
    sigaddset(&rt_set, SIGRTMIN);
    sigaddset(&rt_set, SIGRTMIN + 1);

    while (g_event_count < MAX_EVENTS) {
        struct timespec tmo;
        tmo.tv_sec = 0;
        tmo.tv_nsec = 100 * 1000000L; /* 100 ms */

        siginfo_t info;
        int sig = sigtimedwait(&rt_set, &info, &tmo);
        if (sig == -1) {
            if (errno == EINTR) {
                continue; /* ブロック解除されたシグナルによる割り込み: 再試行 */
            }
            break; /* EAGAIN: すべての保留シグナルを収集完了 */
        }
        g_events[g_event_count].sig = sig;
        g_events[g_event_count].data = info.si_value.sival_int;
        g_event_count++;
    }

    /*
     * ============================================================
     * ステップ 8: 結果を表示
     * ============================================================
     */
    printf("\nSIGUSR1 %s (sent 5 times, merged into a single delivery).\n",
           g_usr1_received ? "was delivered" : "was NOT delivered");

    print_events();

    /*
     * 期待される結果:
     *   - SIGUSR1: 5回送信 → 1回の配送にマージ（g_usr1_received が
     *     1回セット; 標準シグナルのマージ）。
     *   - SIGRTMIN: 10回の sigqueue → 10イベントすべて収集。
     *       (ステップ4から 100..104、ステップ5から 300..304、FIFO 順)
     *   - SIGRTMIN+1: 5回の sigqueue → 5イベントすべて収集 (200..204)。
     *   - 収集順: 10個の SIGRTMIN イベントが 5個の SIGRTMIN+1 イベントより
     *     先に来る（SIGRTMIN の番号が小さいため）。
     */

    printf("\nDone.\n");
    return 0;
}
#endif /* SIGRTMIN */
