#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* This tutorial does not set SA_NODEFER, so SIGUSR1 is blocked while
 * on_usr1 runs. Repeated SIGUSR1 deliveries cannot re-enter this handler;
 * sig_atomic_t does not itself make ++ atomic. */
static volatile sig_atomic_t count;
static void on_usr1(int signo, siginfo_t* info, void* u) {
    (void)signo;
    (void)u;
    if (info && info->si_pid > 0)
        count++;
}
int main(void) {
    /* 演習が未実装の間のコンパイル警告抑止。実装後も残して構わない。 */
    (void)on_usr1;
    (void)count;

    /*
     * TODO(step 2): 上の on_usr1 は SA_SIGINFO 三引数ハンドラとして si_pid>0 で
     * count++ する実装済み。main 側で:
     *   1. struct sigaction をゼロ初期化し、sa_mask を sigemptyset、
     *      sa_sigaction = on_usr1、sa_flags = SA_SIGINFO を設定。
     *   2. sigaction(SIGUSR1, &act, NULL) で登録。
     *   3. raise(SIGUSR1) で配送（si_pid に getpid() 相当が入る）。
     *   4. printf("count=%d\n", (int)count);
     *   5. fflush(stdout); して return 0;
     *
     * ハンドラ内で printf / I/O / malloc を使わないこと。SA_RESTART は一部の
     * 待機 API しか再開しない（万能ではない）ため、本 Step では依存しない。
     * 契約テストは "count=1" を含み exit 0 のときのみ PASS とする。
     */
    puts(
        "TODO step 2: register SA_SIGINFO handler, raise SIGUSR1, print "
        "count=1");
    return 2;
}
