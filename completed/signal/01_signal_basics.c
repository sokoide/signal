/*
 * 01_signal_basics.c
 *
 * テーマ: signal() と raise() による基本的なシグナル処理。
 *
 * このプログラムは意図的に小さい。最も古く、最もシンプルな移植性のある
 * シグナルAPIである signal(2) と raise(3) を示す。
 * 実際のコードでは sigaction(2)（02_sigaction.c 参照）を使うべきだが、
 * signal() は初めて学ぶ際に概念がモダンAPIと一対一対応するため、
 * 最初の導入として依然として有用。
 *
 * デモ内容:
 *   - signal() でハンドラを登録する。
 *   - ハンドラがシグナル番号を int 引数として受け取る。
 *   - raise() が呼び出し元プロセス自身にシグナルを送る。
 *   - SIGINT（端末からの割り込み）と SIGTERM（終了要求）の
 *     デフォルト動作はプロセスを終了させる。
 *   - SIG_IGN はシグナルを無視する。SIG_DFL はデフォルト動作に戻す。
 *   - SIGKILL は捕捉・ブロック・無視のいずれもできない。
 *   - シグナルハンドラ内では printf() ではなく write() を使わねばならない理由。
 *   - ハンドラと通常コードで共有するフラグは volatile sig_atomic_t
 * でなければならない。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g -o 01_signal_basics 01_signal_basics.c
 */

/* POSIX.1-2008 インタフェース（例: モダンな sigaction 型）を有効化 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "safe_helpers.h"

/*
 * シグナルハンドラが書き込み、main() が読み取るフラグ。
 *
 * なぜ volatile か？ コンパイラはシグナルハンドラの呼び出し箇所を
 * 認識できない。volatile がなければ、レジスタに値をキャッシュして
 * ハンドラによる変更に気づかない可能性がある。
 *
 * なぜ sig_atomic_t か？ これはあらゆる POSIX 実装において、
 * 非同期シグナル配送に対して読み書きが不可分であることが保証される
 * 唯一の C の型。単純なフラグとしてこれで十分。
 */
static volatile sig_atomic_t got_sigint = 0;

/*
 * SIGINT ハンドラ。
 *
 * int 引数は配送されたシグナル番号。このファイルでは SIGINT のみ
 * 登録しているが、ハンドラは複数のシグナルで共有し、引数で
 * 区別することもできる。
 */
static void sigint_handler(int sig) {
    /* シグナルを受け取ったことを記録。got_sigint が volatile
     * sig_atomic_t なのでこの代入は安全。 */
    got_sigint = 1;

    safe_write_str("[handler] Caught signal ");
    safe_write_int((long long)sig);
    safe_write_str(
        " (SIGINT).  Why write()? printf() is not async-signal-safe.\n");
}

int main(void) {
    /* ====================================================================
     * 概念: シグナルとは何か？
     *
     * シグナルは OS がプロセスに送る非同期通知。
     * ユーザ空間におけるハードウェア割り込みの相当物:
     *   - HW 割り込み: CPU に通知、ISR を実行
     *   - シグナル:     プロセスに通知、ハンドラを実行
     * ==================================================================== */
    printf("=== 概念: シグナルとは何か ===\n");
    printf("シグナル = OS がプロセスに送る非同期通知\n");
    printf("HW 割り込み: CPU に通知、ISR を実行\n");
    printf("シグナル:    プロセスに通知、ハンドラを実行\n");
    printf("\n");

    /* ====================================================================
     * 重要: ハンドラ内で printf() ではなく write() を使う理由
     *
     * シグナルハンドラ内では async-signal-safe な関数のみ呼び出せる。
     * printf() は安全ではない。バッファリングされたストリームを使い、
     * ロックを保持したり malloc() を呼び出したりするため。
     * シグナルはロック保持中に割り込む可能性がある。
     * write() は async-signal-safe であり、正しい選択。
     * ==================================================================== */
    printf("=== 注意: ハンドラ内では printf を使ってはいけない ===\n");
    printf("printf はバッファを持つため async-signal-safe でない\n");
    printf("代わりに write() を使用（safe_write_str/int ヘルパー）\n");
    printf("\n");

    /* 別の端末から `kill -INT <pid>` でシグナルを送れるよう PID を表示 */
    printf("PID: %ld\n", (long)getpid());
    fflush(stdout);

    /*
     * SIGKILL と SIGSTOP は特別: 捕捉・ブロック・無視のいずれも不可。
     * カーネルがこれを強制することで、暴走プロセスを止める手段が
     * 常に存在するようにしている。signal() は SIGKILL に対して
     * SIG_ERR を返し、errno を EINVAL に設定する。
     */
    printf("Trying to catch SIGKILL (this must fail)...\n");
    if (signal(SIGKILL, sigint_handler) == SIG_ERR) {
        printf("  -> signal(SIGKILL) failed as expected: %s\n",
               strerror(errno));
    }
    fflush(stdout);

    /*
     * SIGINT のハンドラを登録。
     *
     * signal() は古典的な API。その動作は System V（初回配送後に
     * ハンドラが SIG_DFL に戻る）と BSD（ハンドラが維持される）で
     * 微妙に異なる。モダンな glibc はデフォルトで BSD セマンティクスに
     * 従うが、すべての Unix でそれを期待してはならない。
     * これが実プログラムで sigaction() が推奨される主な理由。
     */
    printf("Registering SIGINT handler with signal()...\n");
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal(SIGINT)");
        return EXIT_FAILURE;
    }
    fflush(stdout);

    /* raise() は指定されたシグナルを呼び出し元スレッドへ送る。
     * kill(getpid(), SIGINT) はプロセス宛てであり、マルチスレッドでは
     * 配送先スレッドの選択規則が異なる。単一スレッドのこの例では
     * 結果が同じに見えるが、API の意味は同一ではない。 */
    printf("Raising SIGINT to ourselves...\n");
    fflush(stdout);
    raise(SIGINT);

    /* ハンドラが戻ると、実行はここに再開する。 */
    if (got_sigint) {
        printf("Main: handler ran and set got_sigint.\n");
    } else {
        printf("Main: got_sigint is still 0 (unexpected!).\n");
    }
    fflush(stdout);

    /*
     * SIG_IGN は「無視」の処理方法を設定する。SIGINT は配送されるが、
     * デフォルト動作が抑止される。プログラムは何もなかったかのように
     * 続行する。
     */
    printf("Setting SIGINT to SIG_IGN and raising it again...\n");
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("signal(SIGINT, SIG_IGN)");
        return EXIT_FAILURE;
    }
    fflush(stdout);
    raise(SIGINT);
    printf("Main: SIGINT was ignored.\n");
    fflush(stdout);

    /*
     * SIG_DFL はデフォルト動作に戻す。SIGINT のデフォルトは
     * プロセスの終了。この行以降、raise(SIGINT) はプログラムを
     * 終了させる。最後の printf() は実行されない。
     */
    printf("Restoring SIGINT to SIG_DFL and raising it one last time...\n");
    fflush(stdout);
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
        perror("signal(SIGINT, SIG_DFL)");
        return EXIT_FAILURE;
    }
    raise(SIGINT);

    /* SIGINT のデフォルト動作はプロセス終了なので、この行には到達しない。 */
    printf("This line should never be printed.\n");
    return EXIT_SUCCESS;
}
