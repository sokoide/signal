/*
 * 91_printf_deadlock.c
 *
 * テーマ: シグナルハンドラ内で printf() を呼ぶと、stdio の内部ロック
 *         （stdout の FILE ロック）の奪い合いで 本当にデッドロックする
 *         ことを、glibc / macOS libsystem の【両方で確実に】再現する。
 *         しかも handler だけでなく【main も完全に停止】する、目に見える
 *         デッドロックを作る。
 *
 * ============================================================================
 * なぜ「単一スレッド + Ctrl-C 連打」では環境によって結果が変わるのか
 * ============================================================================
 *
 * printf は stdout の FILE ロック（flockfile/funlockfile 相当）を内部で
 * 取得する。このロックの【再帰性（リエントラント）】が libc で異なる:
 *
 *   glibc (Linux):  【再帰ロック】。同じスレッドなら何度でも再取得可能。
 *                   main が printf でロック保持中にシグナルが来ても、
 *                   同じ main スレッド内で走ったハンドラの printf は
 *                   ロックを再入（カウント増）して通ってしまう → 死なない。
 *
 *   macOS libsystem:【非再帰ロック】。同じスレッドでも再取得しようとすると
 *                   自分自身が離すのを待って永久ブロック = 自己デッドロック。
 *                   (lldb で確認: flockfile → _pthread_mutex_firstfit_lock_slow
 *                    → __psynch_mutexwait でカーネル内で永眠)
 *
 * つまり「main の printf 実行中に SIGINT を連打する」単一スレッド構成は、
 * macOS ではフリーズするが Linux では延々動き続ける（3.7億行/秒を確認）。
 * クロスプラットフォームな教材には使えない。
 *
 * ============================================================================
 * 本サンプルの仕掛け: スレッドを分け、循環待ちで main ごと停止させる
 * ============================================================================
 *
 * 再帰ロックが許す再入は【同じスレッド】だけ。別のスレッドからの取得は、
 * たとえ同じプロセス内であっても待たされる。これが核心。
 *
 * そこで:
 *   - main スレッドは SIGINT をブロック（pthread_sigmask）しておく
 *   - 専用の「シグナル受信スレッド」を立て、SIGINT は必ずそちらへ配送
 *
 * handler（別スレッド）が printf で stdout ロックを待って詰まるのは stdio
 * ロックだけで成立する。しかしそれだけだと「片方向の待ち」になり、main は
 * 動き続けてしまう（= デッドロックが目に見えない）。
 *
 * そこで本サンプルは第2のロック g_lock を導入し、main と handler が【逆順】
 * にロックを取得する「循環待ち（circular wait）」を作る:
 *
 *   main スレッド(A):     stdout ロック(A) を保持 → g_lock(B) を待つ   → BLOCK
 *   受信スレッド(B):      g_lock(B) を保持        → printf → stdout(A)
 *                         を待つ                                  → BLOCK
 *
 * 互いに相手が離さないロックを待ち合うため、両スレッドが完全に停止し、
 * main の出力もピタッと止まる。これが本物のデッドロック。
 *
 *   ※ g_lock は main を循環待ちに引き込むための補助ロック。デッドロックの
 *      本体はあくまで printf が内部で取る stdio ロック。handler は printf を
 *      呼んだ時点で stdio ロック待ちで詰まる。
 *
 * ============================================================================
 * 観察のポイント
 * ============================================================================
 *
 *   - main printing を数行出した後、【出力が完全に止まる】。main も handler も
 *     互いのロック待ちで停止した循環デッドロックの証拠。
 *   - 両スレッドとも進まないため、Ctrl-C を何度押してもプロセスは落ちない。
 *     必ず timeout で落とすこと。
 *
 * ============================================================================
 * ビルド・実行
 * ============================================================================
 *
 *   cc -std=c11 -Wall -Wextra -O2 -g 91_printf_deadlock.c -o 91_printf_deadlock
 *   # macOS / glibc 2.34+ は -lpthread 不要。古い Linux は末尾に -lpthread。
 *   # Makefile からは `make 91_printf_deadlock` でビルド（make run には含まれない）。
 *
 *   timeout 5 ./91_printf_deadlock
 */

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* usleep は POSIX.1-2008 で廃止されたので nanosleep で代用するヘルパー。 */
static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000L * 1000L};
    nanosleep(&ts, NULL);
}

/*
 * 循環待ちを作るための第2ロック（g_lock）。main をデッドロックに引き込む
 * 補助ロック。デッドロックの本体はあくまで printf の stdio ロック。
 *
 *   - handler は g_lock を取得してから printf（= stdout ロック取得）を試みる
 *   - main は stdout ロックを保持したまま g_lock の取得を試みる
 *   → main は g_lock 待ち、handler は stdout ロック待ち、で双方が止まる
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * SIGINT 受信スレッド用のハンドラ。
 *
 * このハンドラは main スレッドとは【別のスレッド】（受信スレッド）で走る。
 * まず g_lock（B）を取得してから printf を呼ぶ。printf は内部で stdout
 * ロック（A）の取得を試みるが、main が保持しておりかつ別スレッドなので
 * 再帰ロックでも再入不可 → 永久ブロック。一方 main は g_lock 待ちで
 * ブロック。循環デッドロック成立。
 */
static void sigint_handler(int sig) {
    (void)sig;
    /* B(g_lock) を取得してから A(stdout) を取りに行く → A は main が保持 → BLOCK */
    pthread_mutex_lock(&g_lock);
    printf("[handler] printf in signal handler -> DEADLOCK\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_lock);
}

/*
 * シグナル受信スレッド。main 側で SIGINT をブロックしてあるため、
 * プロセスへ届いた SIGINT はこのスレッドが受け取る。
 */
static void* signal_thread(void* arg) {
    (void)arg;
    /* sigsuspend で SIGINT が届くまで待つ（マスクは空 = 全解除相当） */
    sigset_t empty;
    sigemptyset(&empty);
    while (1) {
        sigsuspend(&empty);
    }
    return NULL;
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /*
     * main スレッドで SIGINT をブロックする。これにより SIGINT は
     * signal_thread 側へ配送される（＝ロック保持者とは別スレッドで
     * ハンドラが走る）。これが両プラットフォームで確実にデッドロック
     * するための鍵。
     */
    sigset_t block_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &block_set, NULL);

    pthread_t t;
    pthread_create(&t, NULL, signal_thread, NULL);

    printf("=== 91_printf_deadlock ===\n");
    printf("main と handler が互いのロックを待ち合う循環デッドロックで\n");
    printf("両スレッドが停止します（出力が止まったら timeout で落として）。\n");
    printf("PID: %d\n", (int)getpid());
    fflush(stdout);

    /* 受信スレッドが sigsuspend に到達する余裕を与える */
    sleep_ms(50);

    /*
     * 循環デッドロックの実演。
     *
     *   main（A）: stdout ロックを保持 → g_lock(B) を待つ   → BLOCK
     *   handler(B): g_lock を保持     → printf → stdout(A)
     *               を待つ                              → BLOCK
     */
    flockfile(stdout); /* A(stdout) を保持 */

    /* 数行出力して、main がロックを保持して動いている様子を見せる。
     * stdio ロックは再帰的なので、main 自身の printf はここで再入できる。 */
    for (int i = 0; i < 3; i++) {
        printf("main printing %d\n", i);
        fflush(stdout);
        sleep_ms(200);
    }

    /*
     * main は SIGINT をブロック中なので、シグナルは受信スレッドへ配送され、
     * そこで走った handler が g_lock(B) を取得してから printf を呼ぶ。
     * handler は printf で stdout ロック(A) を待ってブロックする。
     */
    kill(getpid(), SIGINT);
    /* handler が確実に B を取得してから main が B 待ちに入るよう、少し待つ */
    sleep_ms(50);

    /*
     * main は g_lock(B) を取得しようとするが、handler が B を保持したまま
     * stdout ロック(A) の解放を待っているため、ここで永遠にブロックする。
     * 互いに相手のロックを待ち合う循環デッドロックが成立し、
     * main の出力もここで完全に止まる。
     */
    pthread_mutex_lock(&g_lock); /* ← main 停止 */
    pthread_mutex_unlock(&g_lock);
    funlockfile(stdout); /* 到達不可 */

    return 0;
}
