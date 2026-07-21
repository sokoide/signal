/*
 * 03_blocking.c
 *
 * テーマ: sigprocmask()、シグナルセット、保留シグナルによるシグナルブロック。
 *
 * プログラムは時として、シグナルに中断されずに短い一連の操作を
 * 実行しなければならない。POSIX はそのために sigprocmask() を提供する。
 * これは OS がクリティカルセクションの前後で割り込みを禁止/許可する
 * 処理のユーザ空間版である。カーネルはシグナルを配送し続けるが、
 * ブロック中はプロセスがブロックを解除するまで保留（pending）状態になる。
 *
 * デモ内容:
 *   - sigset_t 操作: sigemptyset()、sigfillset()、sigaddset()、
 *     sigdelset()、sigismember()。
 *   - sigprocmask() の SIG_BLOCK、SIG_UNBLOCK、SIG_SETMASK。
 *   - SIGINT をブロックし、ブロック中に raise し、sigpending() で
 *     保留中であることを確認する。
 *   - 標準シグナルはキューイングされない: ブロックされたシグナルを
 *     2回 raise しても、ブロック解除後の配送は1回だけ。
 *   - シグナルブロックで保護されたシンプルなクリティカルセクション。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g -o 03_blocking 03_blocking.c
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "safe_helpers.h"

/*
 * SIGINT ハンドラがインクリメントするカウンタ。
 * 通常コードと非同期ハンドラで共有されるため、volatile sig_atomic_t。
 * 同じ SIGINT はハンドラ実行中に自動ブロックされるため、この例では
 * ++ の read-modify-write が同種ハンドラと競合しない（型自体が ++ 全体の
 * 原子性を保証するわけではない）。
 */
static volatile sig_atomic_t sigint_count = 0;

static void sigint_handler(int sig) {
    (void)sig;

    sigint_count++;
    safe_write_str("[handler] SIGINT delivered, count=");
    safe_write_int((long long)sigint_count);
    safe_write_str("\n");
}

int main(void) {
    sigset_t set;
    sigset_t oldset;
    sigset_t pending;
    struct sigaction act = {0};

    printf("PID: %ld\n", (long)getpid());
    fflush(stdout);

    /*
     * SIGINT ハンドラをインストール。このデモでも sigaction() を使う。
     * signal() でも動作するが、本番コードでは推奨されない。
     */
    sigemptyset(&act.sa_mask);
    act.sa_handler = sigint_handler;
    act.sa_flags = 0;

    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction(SIGINT)");
        return EXIT_FAILURE;
    }

    /*
     * SIGINT のみを含むシグナルセットを構築。
     *
     * sigset_t は不透明なビットマスク。ビットを追加/削除する前に
     * 必ず sigemptyset() または sigfillset() で初期化すること。
     * 未初期化の sigset_t を使用するのは未定義動作。
     */
    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    /*
     * sigprocmask(SIG_BLOCK, &set, &oldset) は `set` 内のシグナルを
     * プロセスのシグナルマスクに追加する。以前のマスクは `oldset` に
     * 保存され、後で復元できる。
     * OS 低レベルのコードとのアナロジー:
     *   sigprocmask(SIG_BLOCK, ...)  ~=  cli   (割り込み禁止)
     *   sigprocmask(SIG_UNBLOCK, ...) ~=  sti   (割り込み許可)
     *
     * cli/sti が CPU 全体の割り込みフラグを制御するのに対し、
     * sigprocmask は呼び出し元プロセス（またはスレッド）の
     * シグナルマスクのみに影響する。他のプロセスやカーネルは
     * 通常通り動作し続ける。
     *
     * カーネルはシグナルを配送し続けるが、マスクされている場合は
     * ハンドラを起動せずに保留シグナルセットに保持する。
     */
    printf("\nBlocking SIGINT...\n");
    fflush(stdout);

    if (sigprocmask(SIG_BLOCK, &set, &oldset) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        return EXIT_FAILURE;
    }

    /*
     * SIGINT がブロックされている間に2回 raise する。
     *
     * 標準シグナル（SIGINT など）はキューイングされない。
     * ブロックされたシグナルを2回 raise しても、保留は正確に1回。
     * 後でブロック解除すると、ハンドラは1回だけ実行される。
     */
    printf("Raising SIGINT twice while it is blocked...\n");
    fflush(stdout);
    raise(SIGINT);
    raise(SIGINT);

    /*
     * sigpending() は呼び出し元プロセスで現在保留中のシグナルの
     * セットを取得する。シグナルがプロセスに配送されたが、
     * シグナルマスクでブロックされている場合に「保留中」となる。
     */
    if (sigpending(&pending) == -1) {
        perror("sigpending");
        return EXIT_FAILURE;
    }

    if (sigismember(&pending, SIGINT)) {
        printf("SIGINT is pending (as expected).\n");
    } else {
        printf("SIGINT is NOT pending (unexpected!).\n");
    }
    fflush(stdout);

    /*
     * SIGINT のブロックを解除。保留中のシグナルが今配送され、
     * 標準シグナルはキューイングされないため、ハンドラは正確に
     * 1回実行される。
     */
    printf("Unblocking SIGINT...\n");
    fflush(stdout);

    if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1) {
        perror("sigprocmask(SIG_UNBLOCK)");
        return EXIT_FAILURE;
    }

    printf("Main: sigint_count=%d (should be 1)\n", (int)sigint_count);
    fflush(stdout);

    /*
     * クリティカルセクションのパターン。
     *
     * 作業を壊す可能性のあるシグナルをブロックし、重要な操作を
     * 実行し、以前のマスクを復元する。これは OS がクリティカル
     * セクションの前後で cli/sti を行うのと正確に同じだが、
     * ユーザ空間ではマスクがプロセス単位である点が異なる。
     */
    {
        sigset_t crit_set;
        int important_value = 0;

        printf("\n--- Critical section demo ---\n");
        fflush(stdout);

        sigemptyset(&crit_set);
        sigaddset(&crit_set, SIGINT);
        sigaddset(&crit_set, SIGTERM);

        printf("Blocking SIGINT and SIGTERM...\n");
        fflush(stdout);
        if (sigprocmask(SIG_BLOCK, &crit_set, &oldset) == -1) {
            perror("sigprocmask(SIG_BLOCK) critical");
            return EXIT_FAILURE;
        }

        /*
         * これがクリティカルセクション。実際のプログラムでは、
         * リンクリストの更新、2つの関連データベースレコードの書き込み、
         * 一貫性を保たなければならない共有状態の変更などに相当する。
         */
        important_value += 10;
        important_value *= 2;

        printf("Critical section done, important_value=%d\n", important_value);
        fflush(stdout);

        /*
         * 以前のシグナルマスクを復元。SIG_SETMASK はマスク全体を
         * 保存されたマスクで置き換える。これはエントリ時点のマスクが
         * 不明な場合に SIG_UNBLOCK よりも安全。
         */
        if (sigprocmask(SIG_SETMASK, &oldset, NULL) == -1) {
            perror("sigprocmask(SIG_SETMASK)");
            return EXIT_FAILURE;
        }
        printf("Restored previous signal mask.\n");
        fflush(stdout);
    }

    /*
     * 残りの sigset_t 操作のデモ。
     *
     * sigfillset() はすべてのシグナルをセットに追加する。
     * sigdelset() はシグナルを削除する。
     * sigismember() は所属をテストする。
     */
    {
        sigset_t full;

        sigfillset(&full);
        printf("\nAfter sigfillset(): SIGINT member=%d\n",
               sigismember(&full, SIGINT));

        sigdelset(&full, SIGINT);
        printf("After sigdelset(SIGINT): SIGINT member=%d, SIGTERM member=%d\n",
               sigismember(&full, SIGINT), sigismember(&full, SIGTERM));
        fflush(stdout);
    }

    return EXIT_SUCCESS;
}
