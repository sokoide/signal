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
 * 注意:
 *    macOS (Darwin) では POSIX リアルタイムシグナル / sigqueue が
 *    サポートされていないため、コンパイル時に SIGRTMIN が定義されて
 *    いない場合はメッセージを出力して終了します。
 *
 * ビルド:
 *    cc -std=c11 -Wall -Wextra -O2 -g 06_realtime.c -o 06_realtime -lrt
 */

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
 * ハンドラから受け取ったイベントを保存する小さなリングバッファ。
 * ハンドラ内で printf するのは非同期シグナル安全ではないため、
 * イベントを貯めてメインループで一括表示します。
 */
#define MAX_EVENTS 64
static struct {
    int sig;
    int data;
} g_events[MAX_EVENTS];
static volatile sig_atomic_t g_event_count = 0;

/*
 * 標準シグナル SIGUSR1 が何回配送されたか。
 * 5 回送信しても 1 回にマージされるはずです。
 */
static volatile sig_atomic_t g_usr1_count = 0;

/*
 * SA_SIGINFO 形式のハンドラ。
 * sigqueue() で送られたデータは info->si_value に入っています。
 */
static void rt_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;

    int idx = (int)g_event_count;
    if (idx < MAX_EVENTS) {
        g_events[idx].sig = sig;
        g_events[idx].data = info->si_value.sival_int;
        g_event_count++;
    }
}

static void usr1_handler(int sig) {
    (void)sig;
    g_usr1_count++;
}

static void print_events(void) {
    int n = (int)g_event_count;
    printf("Recorded %d real-time signal event(s):\n", n);
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
     * ステップ 1: ハンドラ登録
     * ============================================================
     *
     * リアルタイムシグナルを使うには sigaction() に SA_SIGINFO を指定し、
     * sa_sigaction を使います。そうしないと siginfo_t の情報を
     * 受け取れません。
     */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = rt_handler;

    /* 一方のリアルタイムシグナルハンドラ実行中は、もう一方をブロックして
     * リングバッファの読み書きが混ざらないようにする。 */
    sigaddset(&sa.sa_mask, SIGRTMIN);
    sigaddset(&sa.sa_mask, SIGRTMIN + 1);

    /* SIGRTMIN と SIGRTMIN+1 の 2 つを使う */
    if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
        perror("sigaction(SIGRTMIN)");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGRTMIN + 1, &sa, NULL) == -1) {
        perror("sigaction(SIGRTMIN+1)");
        exit(EXIT_FAILURE);
    }

    /* 比較用に標準シグナル SIGUSR1 も登録 */
    sa.sa_handler = usr1_handler;
    sa.sa_flags = 0; /* SA_SIGINFO は不要 */
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * ステップ 2: シグナルをブロックする
     * ============================================================
     *
     * 送信中にハンドラが走るとキューの内容が処理されてしまうため、
     * 一旦マスクでブロックします。これは「割り込み禁止（cli）」に相当。
     */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGRTMIN);
    sigaddset(&block_set, SIGRTMIN + 1);
    sigaddset(&block_set, SIGUSR1);

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
     * 保留ビットは 1 つしか立たず、ブロック解除後にハンドラは
     * 1 回しか呼ばれません。
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
     * 入ります。添付データもそれぞれ保持されます。
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
     * その後で「番号の小さい SIGRTMIN」をキューします。
     * ブロック解除後、SIGRTMIN のイベントが SIGRTMIN+1 のイベントより
     * 先に記録されるはずです。
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
     * ステップ 6: ブロックを解除して配送させる
     * ============================================================
     *
     * sigprocmask(SIG_UNBLOCK) は「割り込み許可（sti）」に相当。
     * 保留中のシグナルが一斉に配送され、ハンドラが実行されます。
     */
    printf("Unblocking signals...\n");
    if (sigprocmask(SIG_UNBLOCK, &block_set, NULL) == -1) {
        perror("sigprocmask(SIG_UNBLOCK)");
        exit(EXIT_FAILURE);
    }

    /*
     * ハンドラはブロック解除直後に非同期的に実行されます。
     * すべてのハンドラが終わるのを待つため、短くスリープします。
     * （実用的なコードでは、volatile フラグで完了を確認するなどの
     * 方法が使われます。）
     */
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000L; /* 100 ms */
    nanosleep(&ts, NULL);

    /*
     * ============================================================
     * ステップ 7: 結果の表示
     * ============================================================
     */
    printf("\nSIGUSR1 handler was called %d time(s) (expected 1).\n",
           (int)g_usr1_count);

    print_events();

    /*
     * 期待される結果の解説:
     *   - SIGUSR1: 5 回送信 → 1 回しか配送されない。
     *   - SIGRTMIN: 10 回 sigqueue → 10 回すべて配送される。
     *   - SIGRTMIN+1: 5 回 sigqueue → 5 回すべて配送される。
     *   - イベント順序: SIGRTMIN の 10 個が SIGRTMIN+1 の 5 個より先に
     *     記録される（番号が小さいため）。
     */

    printf("\nDone.\n");
    return 0;
}
#endif /* SIGRTMIN */
