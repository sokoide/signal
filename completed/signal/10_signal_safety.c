/*
 * 10_signal_safety.c
 *
 * テーマ: Async-signal-safety — シグナルハンドラ内部から
 * 未定義動作やデッドロックのリスクなく呼び出せる関数。
 *
 * シグナルハンドラは任意の命令でメインプログラムに割り込める。
 * 中断されたコードがたまたまロックを保持していた場合（例: malloc() や
 * printf() の内部）、ハンドラから同じ関数を呼ぶと同じロックの獲得を
 * 試みてデッドロックする。そのため C と POSIX 標準は、ハンドラから
 * 呼び出せる「async-signal-safe」な関数の小さなセットを定義している。
 *
 * このプログラムは安全なパターンを示す:
 *   - volatile sig_atomic_t を使用してハンドラとメインの間で状態を共有。
 *   - ハンドラ内では write() や _exit() などの async-signal-safe な
 *     関数のみを呼ぶ。
 *   - メインプログラムがフラグを監視し、すべての非安全な作業を実行。
 *
 * コンパイル:
 *   cc -std=c11 -Wall -Wextra -O2 -g 10_signal_safety.c -o 10_signal_safety
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* 観測したい SIGALRM 配送の回数 */
#define MAX_SIGNALS 5

/*
 * シグナルハンドラとプログラムの他の部分の間で安全に共有できることが
 * 保証されている唯一のデータ型は "volatile sig_atomic_t"。
 * "volatile" 修飾子はコンパイラが値をレジスタにキャッシュするのを防ぎ、
 * sig_atomic_t はシグナル配送に対して読み書きがアトミックに行われる
 * 整数型。
 */
static volatile sig_atomic_t got_signal = 0;
static volatile sig_atomic_t signal_count = 0;

/*
 * write() のみを使って文字列を stderr に書き込む。write() は
 * async-signal-safe。printf() はシグナルハンドラ内では安全ではない。
 * C の stdio 層はロックとバッファを使い、シグナル到着時にそれらが
 * すでに保持されている可能性があるため。
 *
 * 長さの計算に strlen() ではなく小さなループを使うことで、
 * このヘルパー自身も async-signal-safe な関数だけを呼ぶ。
 */
static void safe_write(const char* msg) {
    int saved_errno = errno;
    size_t len = 0;
    while (msg[len] != '\0') {
        ++len;
    }
    /*
     * write() は partial write や EINTR を返し得る。このデモでは戻り値を
     * 記録せず、短い通知を best-effort で1回だけ送る。完全性が必要な
     * 通常コードでは戻り値を検査して再試行すること（ハンドラ内での
     * 無限再試行は避ける）。
     */
    ssize_t ret = write(STDERR_FILENO, msg, len);
    (void)ret;
    errno = saved_errno;
}

/*
 * SIGALRM 用の安全なシグナルハンドラ。
 *
 * 4つのことだけを行い、すべて async-signal-safe:
 *   1. volatile sig_atomic_t カウンタを更新。
 *   2. volatile sig_atomic_t フラグをセット。
 *   3. write() で短いメッセージを書き込む。
 *   4. alarm() で単発タイマを再設定。alarm() も async-signal-safe。
 *
 * alarm() は POSIX で async-signal-safe として明示的にリストされている
 * ため、タイマの再設定に使用する。ハンドラ自身はシグナルコンテキスト外
 * （main）で sigaction() を使ってインストールされている。sigaction() も
 * async-signal-safe だが、ハンドラ内でハンドラを再インストールすることは
 * めったに必要なく、明確さのために避けるのが最善。
 */
static void alarm_handler(int sig) {
    (void)sig; /* 未使用; -Wextra 警告抑制 */

    ++signal_count;
    got_signal = 1;

    safe_write(
        "[handler] SIGALRM received; only async-signal-safe write() used "
        "here\n");

    /*
     * 単発タイマを再設定し、1秒後に再度 SIGALRM を受信するようにする。
     * 希望する配送回数に達したら再設定を停止。
     */
    if (signal_count < MAX_SIGNALS) {
        alarm(1);
    }
}

/*
 * SIGTERM 用の安全な終了ハンドラ。
 *
 * ユーザ（または別のプロセス）が SIGTERM を送った場合、ハンドラの観点から
 * クリーンにシャットダウンしたい。exit() は安全ではない。なぜなら stdio
 * バッファをフラッシュし atexit ハンドラを実行し、それらがロックを
 * 保持する可能性があるため。_exit() はそれらの作業なしで即座に終了する
 * ため、async-signal-safe な終了方法。
 */
static void term_handler(int sig) {
    (void)sig;

    safe_write(
        "[handler] SIGTERM received; calling _exit() (async-signal-safe "
        "termination)\n");
    _exit(EXIT_SUCCESS);
}

/*
 * 以下のブロックはシグナルハンドラが絶対にやってはいけないことを示す。
 * #if 0 で囲まれているためコンパイルされないが、説明を読める。
 *
 * なぜ危険か:
 *   - printf() は stdio ロックを使用。シグナルがメインプログラムの
 *     printf() 内部で割り込んだ場合、ハンドラは同じミューテックスを
 *     ロックしようとしてデッドロックする。
 *   - malloc()/free() はヒープロックを使用。同様に、中断されたコードが
 *     メモリ割り当て中だった場合、ハンドラはデッドロックする。
 *   - exit() は登録された atexit ハンドラを実行しストリームをフラッシュ
 *     するため、async-signal-safe でない関数を呼ぶ可能性があり、
 *     ロックを保持する可能性がある。
 */
#if 0
static void unsafe_handler_example(int sig)
{
    printf("Got signal %d\n", sig); /* 危険: stdio ロック */
    malloc(1024);                  /* 危険: ヒープロック */
    free(NULL);                    /* 危険: ヒープロック */
    exit(EXIT_SUCCESS);            /* 危険: atexit + stdio フラッシュ */
}
#endif

int main(void) {
    struct sigaction sa;
    sigset_t alarm_set, oldmask, waitmask;

    /* 安全な SIGALRM ハンドラをインストール */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = alarm_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* 安全な SIGTERM ハンドラをインストール */
    sa.sa_handler = term_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    printf("Main program: waiting for %d SIGALRM signals.\n", MAX_SIGNALS);
    printf(
        "(The signal handler uses only write(), alarm(), and "
        "sig_atomic_t.)\n\n");
    fflush(stdout);

    /* フラグの read-clear と待機を SIGALRM に割り込まれないようにする。 */
    sigemptyset(&alarm_set);
    sigaddset(&alarm_set, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &alarm_set, &oldmask) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }
    waitmask = oldmask;
    sigdelset(&waitmask, SIGALRM);

    /* タイマを開始。alarm() は通常コードでも安全。 */
    alarm(1);

    /*
     * メインループはハンドラがセットするフラグを sigsuspend() で待つ。
     * ハンドラは最小限のことだけを行い、
     * メインの流れがフラグに反応する。すべての非安全な関数
     * （printf、fflush 等）はここ、ハンドラではなくメインで実行される。
     */
    while (signal_count < MAX_SIGNALS || got_signal) {
        while (!got_signal) {
            (void)sigsuspend(&waitmask);
        }

        /* sigsuspend() 復帰時には SIGALRM が再びブロックされているため、
         * read-clear の間にハンドラが割り込んで通知を上書きしない。 */
        got_signal = 0;
        printf("[main]    saw got_signal; count = %d\n", (int)signal_count);
        fflush(stdout);
    }

    /* 完了したので保留中のアラームをキャンセル */
    alarm(0);
    if (sigprocmask(SIG_SETMASK, &oldmask, NULL) == -1) {
        perror("sigprocmask(SIG_SETMASK)");
        exit(EXIT_FAILURE);
    }

    printf("\n");
    printf("Demo complete.\n");
    printf("Final signal_count = %d\n", (int)signal_count);
    printf("\n");
    printf("Async-signal-safe examples (ok in handlers):\n");
    printf("  _exit(), write(), read(), close(), fcntl(), kill(),\n");
    printf("  sigaction(), signal(), sigprocmask(), alarm(), abort()\n");
    printf("\n");
    printf("UNSAFE in signal handlers (can deadlock or corrupt state):\n");
    printf("  printf(), malloc(), free(), exit(), fopen(), fclose(),\n");
    printf("  pthread_mutex_lock(), pthread_mutex_unlock()\n");
    printf("Conditionally safe: longjmp()/siglongjmp() only under POSIX's ");
    printf("restrictions\n");
    printf("\n");
    printf("Rule of thumb: do as little as possible in a handler.\n");
    printf("Set a volatile sig_atomic_t flag and let main() do the rest.\n");

    return EXIT_SUCCESS;
}
