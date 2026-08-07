/*
 * 04_timer.c — POSIX タイマシグナルの入門
 *
 * このプログラムは2つの POSIX タイマ API と、それらが生成するシグナルを
 * 並行して動かしながら実演する。
 *
 * 1. alarm(2): 最もシンプルな「単発・秒単位」タイマ。
 *    指定された秒数後に SIGALRM を呼び出し元プロセスに送る。
 *
 * 2. setitimer(2): マイクロ秒精度の反復タイマ。
 *    3つの計測ドメインがあり、それぞれ異なるシグナルを生成する。
 *    注: setitimer() は POSIX.1-2008 で obsolescent（廃止予定）。
 *    新規コードでは timer_create() / timer_settime() を検討すべき。
 *
 *    | which            | signal      | 計測対象                              |
 *    |------------------|-------------|---------------------------------------|
 *    | ITIMER_REAL      | SIGALRM     | 実時間（wall-clock time）             |
 *    | ITIMER_VIRTUAL   | SIGVTALRM   | ユーザモード CPU 時間のみ             |
 *    | ITIMER_PROF      | SIGPROF     | ユーザ CPU 時間 + カーネル CPU 時間   |
 *
 * 重要なポイント:
 *   - ITIMER_REAL は sleep() 中も刻み続ける（実時間を計測するため）。
 *   - ITIMER_VIRTUAL は sleep() 中に進まない（CPU 時間を消費しないため）。
 *   - これにより OS の「タイマ割り込み」と「CPU 時間」の違いを実感できる。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g 04_timer.c -o 04_timer
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* 2つの timeval 間の経過マイクロ秒を計算するマクロ */
#define ELAPSED_US(start, now)                    \
    (((now).tv_sec - (start).tv_sec) * 1000000L + \
     ((now).tv_usec - (start).tv_usec))

/*
 * シグナルハンドラと通常の実行の間で安全に共有できる変数。
 *
 * volatile: コンパイラがリロードを最適化で除去するのを防ぐ。
 *           ハンドラによる変更が常に可視になる。
 * sig_atomic_t: 単一の読み書きがシグナル割り込みに対して不可分。
 *               ++ のような read-modify-write 全体は保証しないが、
 *               この例では同じシグナルがハンドラ中に自動ブロックされる。
 */
static volatile sig_atomic_t g_virtual_count = 0;
static volatile sig_atomic_t g_real_count = 0;
static volatile sig_atomic_t g_alarm_fired = 0;

/*
 * SIGVTALRM ハンドラ。
 * プロセスがユーザモードで CPU 時間を消費している間のみ発火する。
 */
static void virtual_handler(int sig) {
    (void)sig; /* 未使用パラメータ警告を抑制 */
    g_virtual_count++;
}

/*
 * SIGALRM ハンドラ。
 * ITIMER_REAL（実時間）タイマが発火したときに呼ばれる。
 */
static void real_handler(int sig) {
    (void)sig;
    g_real_count++;
}

/*
 * alarm() 用のハンドラ。
 * printf を避け、async-signal-safe な write() でメッセージを出力する。
 */
static void alarm_handler(int sig) {
    int saved_errno = errno;
    (void)sig;
    g_alarm_fired = 1;
    const char msg[] = "[alarm] SIGALRM fired (one-shot)\n";
    /* write(2) は async-signal-safe */
    ssize_t ret = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)ret;
    errno = saved_errno;
}

/*
 * CPU を消費するビジーループ。
 *
 * ITIMER_VIRTUAL はユーザモード CPU 時間を計測する。
 * sleep() は CPU を消費しないため、VIRTUAL タイマは sleep 中に進まない。
 * このループは約1秒間 CPU を占有し、VIRTUAL タイマがプロセスが
 * 能動的に CPU サイクルを消費している間のみ発火することを示す。
 */
static void busy_loop_for_one_second(void) {
    struct timeval start, now;
    /* 簡潔さのため gettimeofday() を使用。新規コードでは
     * clock_gettime(CLOCK_MONOTONIC, ...) が推奨される。
     * システムクロック調整の影響を受けないため。 */
    gettimeofday(&start, NULL);

    /* `spinner` はループ本体が最適化で除去されるのを防ぐダミー変数。
     * ループの終了条件には関与しない。 */
    volatile unsigned long spinner = 0;
    do {
        for (int i = 0; i < 1000000; i++) {
            spinner++;
        }
        gettimeofday(&now, NULL);
    } while (ELAPSED_US(start, now) < 1000000L);
}

int main(void) {
    struct sigaction sa;
    sigset_t alarm_set, oldmask, waitmask;

    /*
     * ============================================================
     * パート1: alarm(2) — 単発・秒単位のタイマ
     * ============================================================
     *
     * alarm(seconds) は `seconds` 秒後に SIGALRM を1回だけスケジュールする。
     * 秒単位の精度で、単発のみ。alarm() を再度呼ぶと前回の alarm() は
     * キャンセルされる。
     */
    printf("=== alarm(2) demo ===\n");
    fflush(stdout); /* ハンドラ内の write() との出力順序を安定化 */

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        exit(EXIT_FAILURE);
    }

    /* alarm() と待機の間で SIGALRM を取りこぼさないよう、先にブロックする。 */
    sigemptyset(&alarm_set);
    sigaddset(&alarm_set, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &alarm_set, &oldmask) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }

    /* 1秒後に SIGALRM をスケジュール */
    alarm(1);

    /*
     * alarm() の後に pause() を呼ぶだけでは、両者の間に SIGALRM が配送される
     * 競合がある。sigsuspend() は一時マスクへの交換と待機を原子的に行う。
     */
    waitmask = oldmask;
    sigdelset(&waitmask, SIGALRM);
    while (!g_alarm_fired) {
        (void)sigsuspend(&waitmask);
    }

    if (sigprocmask(SIG_SETMASK, &oldmask, NULL) == -1) {
        perror("sigprocmask(SIG_SETMASK)");
        exit(EXIT_FAILURE);
    }

    /*
     * alarm(0) は保留中のアラームをキャンセルする。
     * よい後片付けの習慣。
     */
    alarm(0);

    /*
     * ============================================================
     * パート2: setitimer(2) — 高精度反復タイマ
     * ============================================================
     *
     * struct itimerval は2つの struct timeval を含む:
     *   it_value    : 最初の期限までの時間（初期値）
     *   it_interval : 以降の期限の間隔（0 = 単発）
     */
    printf("\n=== setitimer(2) demo ===\n");

    /* SIGVTALRM ハンドラを sigaction で登録 */
    sa.sa_handler = virtual_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGVTALRM)");
        exit(EXIT_FAILURE);
    }

    /* SIGALRM ハンドラを sigaction で登録（ITIMER_REAL 用） */
    sa.sa_handler = real_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        exit(EXIT_FAILURE);
    }

    /*
     * (A) ITIMER_VIRTUAL で CPU 時間を計測
     *
     * 10ms ごとに SIGVTALRM を発火。ハンドラが g_virtual_count を
     * インクリメントする。ビジーループ終了後に合計を表示する。
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
     * タイマを停止するには、両方のメンバを0に設定した itimerval を渡す。
     * そうしないと、後続の処理中もシグナルが届き続ける。
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
     * (B) 待機中の ITIMER_REAL と ITIMER_VIRTUAL の違いの実演
     *
     * ポイント: nanosleep() はプロセスを一時停止するため、CPU
     * 時間を消費しない。 したがって ITIMER_VIRTUAL は待機中に発火しないが、
     * ITIMER_REAL（実時間）は関係なく発火し続ける。
     */
    printf(
        "\n--- ITIMER_REAL fires during sleep, ITIMER_VIRTUAL does not ---\n");

    g_real_count = 0;
    g_virtual_count = 0;

    /* REAL タイマを 100ms 間隔に設定 */
    struct itimerval real_timer;
    real_timer.it_value.tv_sec = 0;
    real_timer.it_value.tv_usec = 100000; /* 100ms */
    real_timer.it_interval = real_timer.it_value;
    if (setitimer(ITIMER_REAL, &real_timer, NULL) == -1) {
        perror("setitimer(ITIMER_REAL)");
        exit(EXIT_FAILURE);
    }

    /* VIRTUAL タイマも 100ms 間隔に設定 */
    struct itimerval virtual_timer2;
    virtual_timer2.it_value.tv_sec = 0;
    virtual_timer2.it_value.tv_usec = 100000; /* 100ms */
    virtual_timer2.it_interval = virtual_timer2.it_value;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer2, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL)");
        exit(EXIT_FAILURE);
    }

    printf("sleep(1) starts...\n");
    struct timespec remaining = {.tv_sec = 1, .tv_nsec = 0};
    while (nanosleep(&remaining, &remaining) == -1) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(EXIT_FAILURE);
        }
    }
    printf("sleep(1) ended.\n");

    /* 両方のタイマを停止 */
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
    if (g_real_count < 2 || g_virtual_count != 0) {
        fprintf(stderr, "unexpected timer counts during sleep\n");
        return EXIT_FAILURE;
    }

    /*
     * (C) 注: ITIMER_PROF は「ユーザ CPU 時間 + カーネル CPU 時間」
     *     に基づいて発火し、SIGPROF を生成する。プロファイラで
     *     一般的に使用される。ここでは例を簡潔にするため省略するが、
     *     上記とまったく同様に ITIMER_PROF を setitimer() に渡せば
     *     使用できる。
     */

    printf("\nDone.\n");
    return 0;
}
