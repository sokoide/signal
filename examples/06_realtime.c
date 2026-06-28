/*
 * 06_realtime.c — POSIX リアルタイムシグナルと sigqueue()
 *
 * 標準シグナル（1〜31）とは異なり、リアルタイムシグナルは
 * 以下の 2 つの重要な特徴を持ちます。
 *
 * 1. キューイング（queuing）
 *    標準シグナルは「同じシグナルが複数回到着しても 1 つにまとめられる」
 *    （マージされる）のに対し、リアルタイムシグナルは個別にキューに
 *    入り、すべて配送されます。
 *
 * 2. データ配送
 *    sigqueue(2) を使うと、整数またはポインタをシグナルに添付して
 *    送信できます。受信側は siginfo_t.si_value から取り出します。
 *
 * リアルタイムシグナルの範囲:
 *    Linux: SIGRTMIN 〜 SIGRTMAX（通常 34 〜 64）
 *    番号の小さいシグナルが大きいシグナルより先に配送されます。
 *
 * このサンプルの設計（非同期ハンドラの安全な使い方）:
 *    - SIGUSR1 には「フラグだけ更新する」最小ハンドラを登録します。
 *      volatile sig_atomic_t 以外の共有状態には触らないのが鉄則です。
 *    - リアルタイムシグナルにはハンドラを登録しません（SIG_DFL）。
 *      代わりにブロックしたまま sigtimedwait(2) で「同期的に」取り出し、
 *      siginfo_t.si_value のデータはメインスレッドで安全に収集します。
 *      これが実用で推奨されるパターンです（→ README「発展トピック」の
 *      sigwaitinfo / self-pipe trick も参照）。
 *
 * 注意:
 *    macOS (Darwin) では POSIX リアルタイムシグナル / sigqueue が
 *    サポートされていないため、コンパイル時に SIGRTMIN が定義されて
 *    いない場合はメッセージを出力して終了します。
 *
 * ビルド:
 *    cc -std=c11 -Wall -Wextra -O2 -g 06_realtime.c -o 06_realtime -lrt
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * リアルタイムシグナルが利用できないプラットフォーム（macOS など）では、
 * コンパイル時に分岐してメッセージを表示する。
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
 * メインスレッドで収集したリアルタイムシグナルのイベント。
 * 非同期ハンドラからは一切触らないため、volatile 不要・競合なし。
 */
#define MAX_EVENTS 64
static struct {
    int sig;
    int data;
} g_events[MAX_EVENTS];
static int g_event_count = 0;

/*
 * 標準シグナル SIGUSR1 の配送回数。
 * 非同期ハンドラから更新する唯一の変数であり、volatile sig_atomic_t に
 * しています。5 回送信しても 1 回にマージされるはずです。
 */
static volatile sig_atomic_t g_usr1_count = 0;

/*
 * SIGUSR1 ハンドラ。
 *
 * 非同期シグナルハンドラの安全な書き方の鉄則:
 *   - volatile sig_atomic_t 型の変数の読み書き「だけ」を行う。
 *   - stdio (printf 等) や複雑な共有データ構造には触らない。
 * 複数個のシグナルから届いた「データ」を処理したい場合は、ハンドラ内で
 * 処理するのではなく、本サンプルのリアルタイムシグナルのように
 * sigwaitinfo/sigtimedwait で同期的に受け取るか、self-pipe trick を
 * 使ってメインループに回してください（→ 07_selfpipe.c）。
 */
static void usr1_handler(int sig) {
    (void)sig;
    g_usr1_count++;
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
     * ステップ 1: SIGUSR1 の最小ハンドラを登録
     * ============================================================
     *
     * 標準シグナル SIGUSR1 には、フラグ（g_usr1_count）だけを更新する
     * 安全なハンドラを登録します。SA_SIGINFO は不要です。
     *
     * リアルタイムシグナルにはハンドラを登録「しません」。後で
     * sigtimedwait() で同期的に取り出すため、ブロックしたまま保留させます。
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
     * ステップ 2: シグナルをブロックする
     * ============================================================
     *
     * SIGUSR1 / SIGRTMIN / SIGRTMIN+1 をすべてブロックします。これは
     * 「割り込み禁止（cli）」に相当します。ブロック中に送信された
     * シグナルは保留（pending）となり、後で処理されます。
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
     * ステップ 3: 標準シグナルを 5 回送信
     * ============================================================
     *
     * SIGUSR1 は標準シグナルなので、ブロック中に 5 回到着しても
     * 保留ビットは 1 つしか立たず、後でハンドラは 1 回しか呼ばれません。
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
     * ステップ 4: リアルタイムシグナルを 5 回 sigqueue する
     * ============================================================
     *
     * 同じ SIGRTMIN を 5 回 sigqueue すると、すべて個別にキューに
     * 入ります。添付データもそれぞれ保持されます。これらはブロック中
     * なので保留されたままです。
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
     * ステップ 5: 別のリアルタイムシグナルも送信（優先順序のデモ）
     * ============================================================
     *
     * 配送順序のルール:
     *   - 標準シグナル間の順序は保証されない。
     *   - リアルタイムシグナル間では、番号の小さいものが先。
     *
     * ここではあえて「番号の大きい SIGRTMIN+1」を先にキューし、
     * その後で「番号の小さい SIGRTMIN」をキューします。後で
     * sigtimedwait で取り出すとき、SIGRTMIN のイベントが
     * SIGRTMIN+1 のイベントより先に得られるはずです。
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
     * ステップ 6: SIGUSR1 を届けてマージを観察する
     * ============================================================
     *
     * SIGUSR1 だけブロック解除（「割り込み許可: sti」に相当）すると、
     * 保留されていた SIGUSR1 が直ちに配送されハンドラが 1 回走ります。
     * リアルタイムシグナルはまだブロックされたままです。
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
     * ステップ 7: リアルタイムシグナルを同期的に取り出す
     * ============================================================
     *
     * sigtimedwait(2) は、指定したシグナルセットのいずれかが保留される
     * まで待ち、1 個デキューして siginfo_t に詳細を格納します。
     * ハンドラを経由しないため、si_value のデータも共有状態もすべて
     * メインスレッドで安全に扱えます。
     *
     * - 戻り値はデキューされたシグナル番号。
     * - タイムアウト（ここでは 100ms）で EAGAIN → 送信済みの分をすべて
     *   処理し終えたと判断してループを抜けます。
     * - 同番号の RT シグナルは FIFO、異なる番号間は番号の小さい方が先。
     */
    sigset_t rt_set;
    sigemptyset(&rt_set);
    sigaddset(&rt_set, SIGRTMIN);
    sigaddset(&rt_set, SIGRTMIN + 1);

    struct timespec tmo;
    tmo.tv_sec = 0;
    tmo.tv_nsec = 100 * 1000000L; /* 100 ms */

    while (g_event_count < MAX_EVENTS) {
        siginfo_t info;
        int sig = sigtimedwait(&rt_set, &info, &tmo);
        if (sig == -1) {
            break; /* EAGAIN: 保留済みはすべて取り出した */
        }
        g_events[g_event_count].sig = sig;
        g_events[g_event_count].data = info.si_value.sival_int;
        g_event_count++;
    }

    /*
     * ============================================================
     * ステップ 8: 結果の表示
     * ============================================================
     */
    printf("\nSIGUSR1 handler was called %d time(s) (expected 1).\n",
           (int)g_usr1_count);

    print_events();

    /*
     * 期待される結果の解説:
     *   - SIGUSR1: 5 回送信 → 1 回しか配送されない（標準シグナルのマージ）。
     *   - SIGRTMIN: 10 回 sigqueue → 10 個すべて取り出せる。
     *       （ステップ4 の 100..104、ステップ5 の 300..304、FIFO 順）
     *   - SIGRTMIN+1: 5 回 sigqueue → 5 個すべて取り出せる（200..204）。
     *   - 取り出し順序: SIGRTMIN の 10 個が SIGRTMIN+1 の 5 個より先
     *     （番号が小さいため）。
     */

    printf("\nDone.\n");
    return 0;
}
#endif /* SIGRTMIN */
