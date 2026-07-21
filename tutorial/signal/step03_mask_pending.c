#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

/* This tutorial is single-threaded, uses raise(SIGUSR1), and does not set
 * SA_NODEFER. SIGUSR1 is therefore blocked while its handler runs, so this
 * read-modify-write does not race with another on_usr1 invocation here.
 * sig_atomic_t does not itself make ++ atomic. */
static volatile sig_atomic_t deliveries;

static void on_usr1(int s) {
    (void)s;
    deliveries++;
}

int main(void) {
    /* 演習が未実装の間のコンパイル警告抑止。実装後も残して構わない。 */
    (void)on_usr1;
    (void)deliveries;

    /*
     * TODO(step 3): マスク→pending→解除の順で観察する。
     *   1. on_usr1 を SIGUSR1 ハンドラとして sigaction で登録
     *      （deliveries を加算）。
     *   2. SIGUSR1 のみを含むセットを sigprocmask(SIG_BLOCK) でブロック。
     *   3. raise(SIGUSR1) を2回送る。
     *      標準シグナルは pending 中にマージされる。
     *   4. sigpending(&pend) して sigismember(&pend, SIGUSR1) を調べ、
     *      printf("pending=%d\n", ismember ? 1 : 0);
     *   5. sigprocmask(SIG_UNBLOCK) で解除 → ハンドラが1回走る。
     *   6. printf("deliveries=%d\n", (int)deliveries);
     *   7. fflush(stdout); して return 0;
     *
     * deliveries はマージ観察用のカウンタであり、送信回数を保存する
     * 仕組みではない。詳しい制約は README の Step 3 を参照。
     * 契約テスト: "pending=1"・"deliveries=1" を含み exit 0 で PASS。
     */
    puts(
        "TODO step 3: block SIGUSR1, print pending=1, unblock, print "
        "deliveries=1");
    return 2;
}
