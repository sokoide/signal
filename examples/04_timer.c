/*
 * 04_timer.c — POSIX タイマシグナル入門
 *
 * このプログラムでは、POSIX が提供する 2 つのタイマ API と、それが
 * 生成するシグナルの違いを実際に動かして学びます。
 *
 * 1. alarm(2): 最も単純な「秒単位・単発」タイマー。
 *    指定秒数後に自分自身へ SIGALRM を送ります。
 *
 * 2. setitimer(2): マイクロ秒単位で指定できる「反復」タイマー。
 *    3 種類の計測対象時間があり、それぞれ異なるシグナルが飛びます。
 *    注意: setitimer() は POSIX.1-2008 で
 * obsolescent（廃止予定）とされています。 新規コードでは timer_create() /
 * timer_settime() の検討を推奨します。
 *
 *    | which            | シグナル    | 計測する時間 |
 *    |------------------|-------------|------------------------------------------|
 *    | ITIMER_REAL      | SIGALRM     | 実時間（wall-clock time） | |
 * ITIMER_VIRTUAL   | SIGVTALRM   | ユーザモード CPU 時間のみ                |
 *    | ITIMER_PROF      | SIGPROF     | ユーザ CPU 時間 + カーネル CPU 時間 |
 *
 * 重要なポイント:
 *   - ITIMER_REAL はプロセスが sleep() していても経過する（実時間だから）。
 *   - ITIMER_VIRTUAL は sleep() 中には進まない（CPU を消費していないから）。
 *   - これは OS の「タイマ割り込み」と「CPU 時間の違い」を体感できます。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g 04_timer.c -o 04_timer
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

/*
 * シグナルハンドラと通常実行の間で安全に共有できる変数。
 * volatile:
 * コンパイラの最適化を抑制し、ハンドラでの変更を必ず見えるようにする。
 * sig_atomic_t: この型の読み書きは「不可分」であり、割り込まれても壊れない。
 */
static volatile sig_atomic_t g_virtual_count = 0;
static volatile sig_atomic_t g_real_count = 0;

/*
 * SIGVTALRM ハンドラ。
 * プロセスがユーザモードで CPU を消費している間にだけ発火します。
 */
static void virtual_handler(int sig) {
    (void)sig; /* 未使用引数の警告を抑える */
    g_virtual_count++;
}

/*
 * SIGALRM ハンドラ。
 * ITIMER_REAL（実時間）タイマーが発火したときに呼ばれます。
 */
static void real_handler(int sig) {
    (void)sig;
    g_real_count++;
}

/*
 * alarm() 用のハンドラ。
 * alarm() は printf など非同期シグナル安全でない関数を呼ばないよう、
 * 安全な write() でメッセージを出力します。
 */
static void alarm_handler(int sig) {
    (void)sig;
    const char msg[] = "[alarm] SIGALRM fired (one-shot)\n";
    /* write(2) は async-signal-safe です */
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

/*
 * CPU を消費し続ける「ビジーループ」。
 * sleep() 中は CPU を使わないため、VIRTUAL タイマーは進みません。
 * ここでは約 1 秒間、意図的に CPU を占有します。
 */
static void busy_loop_for_one_second(void) {
    struct timeval start, now;
    /* 簡潔さのため gettimeofday() を使用。新規コードでは
     * clock_gettime(CLOCK_MONOTONIC, ...)
     * を推奨（システム時刻変更の影響を受けない）。 */
    gettimeofday(&start, NULL);

    /* spinner は脱出条件には関係なく、ループ本体が最適化で消滅するのを
     * 防ぐためのダミー変数です。 */
    volatile unsigned long spinner = 0;
    do {
        for (int i = 0; i < 1000000; i++) {
            spinner++;
        }
        gettimeofday(&now, NULL);
    } while ((now.tv_sec - start.tv_sec) * 1000000L +
                 (now.tv_usec - start.tv_usec) <
             1000000L);
}

int main(void) {
    struct sigaction sa;

    /*
     * ============================================================
     * パート 1: alarm(2) — 単発・秒単位タイマー
     * ============================================================
     *
     * alarm(seconds) は、seconds 秒後に SIGALRM を 1 回だけ送ります。
     * 精度は秒単位しかなく、単発です。
     * 新しい alarm() を呼ぶと、以前の alarm() はキャンセルされます。
     */
    printf("=== alarm(2) demo ===\n");
    fflush(stdout); /* ハンドラ内の write() と出力順序を安定させる */

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        exit(EXIT_FAILURE);
    }

    /* 1 秒後に SIGALRM を送るように予約 */
    alarm(1);

    /*
     * pause() はシグナルが届くまでスリープします。
     * SIGALRM が届くと pause() は -1 で復帰します。
     */
    pause();

    /*
     * alarm(0) は、予約中の alarm をキャンセルします。
     * これはクリーンアップのお作法です。
     */
    alarm(0);

    /*
     * ============================================================
     * パート 2: setitimer(2) — 高精度反復タイマー
     * ============================================================
     *
     * struct itimerval は 2 つの struct timeval を持ちます:
     *   it_value    : 最初の発火までの時間（初期値）
     *   it_interval : 最初以降の発火間隔（0 なら単発）
     */
    printf("\n=== setitimer(2) demo ===\n");

    /* SIGVTALRM のハンドラを sigaction で登録 */
    sa.sa_handler = virtual_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGVTALRM)");
        exit(EXIT_FAILURE);
    }

    /* SIGALRM のハンドラを sigaction で登録（ITIMER_REAL 用） */
    sa.sa_handler = real_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        exit(EXIT_FAILURE);
    }

    /*
     * (A) ITIMER_VIRTUAL で CPU 時間を計測
     *
     * 10ms おきに SIGVTALRM を発火させます。
     * ハンドラ内で g_virtual_count をインクリメントし、
     * その回数を後で表示します。
     */
    struct itimerval virtual_timer;
    virtual_timer.it_value.tv_sec = 0;
    virtual_timer.it_value.tv_usec = 10000; /* 10ms */
    virtual_timer.it_interval = virtual_timer.it_value;

    g_virtual_count = 0;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL)");
        exit(EXIT_FAILURE);
    }

    printf("Running busy loop for ~1 second with VIRTUAL timer (10ms)...\n");
    busy_loop_for_one_second();

    /*
     * タイマーを停止するには、両方のメンバを 0 にした itimerval を渡します。
     * これを怠ると、後続の処理中もシグナルが届き続けます。
     */
    virtual_timer.it_value.tv_sec = 0;
    virtual_timer.it_value.tv_usec = 0;
    virtual_timer.it_interval = virtual_timer.it_value;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL, stop)");
        exit(EXIT_FAILURE);
    }

    printf("VIRTUAL timer fired %d times during busy loop.\n",
           (int)g_virtual_count);

    /*
     * (B) ITIMER_REAL vs ITIMER_VIRTUAL の違いを sleep() で実証
     *
     * ポイント: sleep(1) はプロセスを休止させるため CPU 時間を消費しません。
     * そのため ITIMER_VIRTUAL は sleep 中に発火しませんが、
     * ITIMER_REAL（実時間）は関係なく発火し続けます。
     */
    printf(
        "\n--- ITIMER_REAL fires during sleep, ITIMER_VIRTUAL does not ---\n");

    g_real_count = 0;
    g_virtual_count = 0;

    /* REAL タイマーを 100ms 間隔で設定 */
    struct itimerval real_timer;
    real_timer.it_value.tv_sec = 0;
    real_timer.it_value.tv_usec = 100000; /* 100ms */
    real_timer.it_interval = real_timer.it_value;
    if (setitimer(ITIMER_REAL, &real_timer, NULL) == -1) {
        perror("setitimer(ITIMER_REAL)");
        exit(EXIT_FAILURE);
    }

    /* VIRTUAL タイマーも 100ms 間隔で設定 */
    struct itimerval virtual_timer2;
    virtual_timer2.it_value.tv_sec = 0;
    virtual_timer2.it_value.tv_usec = 100000; /* 100ms */
    virtual_timer2.it_interval = virtual_timer2.it_value;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer2, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL)");
        exit(EXIT_FAILURE);
    }

    printf("sleep(1) starts...\n");
    sleep(1);
    printf("sleep(1) ended.\n");

    /* 両方のタイマーを停止 */
    struct itimerval stop;
    stop.it_value.tv_sec = 0;
    stop.it_value.tv_usec = 0;
    stop.it_interval = stop.it_value;
    if (setitimer(ITIMER_REAL, &stop, NULL) == -1) {
        perror("setitimer(ITIMER_REAL, stop)");
        exit(EXIT_FAILURE);
    }
    if (setitimer(ITIMER_VIRTUAL, &stop, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL, stop)");
        exit(EXIT_FAILURE);
    }

    printf(
        "During sleep(1): REAL timer fired %d time(s), VIRTUAL timer fired %d "
        "time(s).\n",
        (int)g_real_count, (int)g_virtual_count);

    /*
     * (C) 補足: ITIMER_PROF は「ユーザ CPU 時間 + カーネル CPU 時間」で
     *     SIGPROF を発生させます。プロファイラーの実装によく使われます。
     *     ここではコードを簡潔にするため省略しますが、
     *     which に ITIMER_PROF を指定すれば同じように使えます。
     */

    printf("\nDone.\n");
    return 0;
}
