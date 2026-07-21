/*
 * 02_sigaction.c
 *
 * テーマ: sigaction()、SA_SIGINFO、siginfo_t によるモダンなシグナル処理。
 *
 * signal()（古いAPI）はいくつかの点で未定義または実装依存の動作を持つ:
 * ハンドラがインストールされたままか、システムコールが再開されるか、
 * ハンドラ実行中にどのシグナルがブロックされるか。sigaction() は
 * 呼び出し元がすべての詳細を指定できることで、これらを解決する。
 *
 * デモ内容:
 *   - signal() の推奨代替としての sigaction()。
 *   - SA_SIGINFO: 3引数ハンドラがシグナル番号、siginfo_t ポインタ、
 *     割り込み発生時の ucontext を受け取る。
 *   - siginfo_t フィールドの読み取り: si_pid（送信元 PID）、
 *     si_uid（送信元 UID）、si_code（理由コード）、
 *     si_addr（SIGSEGV のフォールトアドレス）。
 *   - SA_RESTART: シグナルで中断された遅いシステムコール
 *     （ここでは read()）を自動再開する。
 *   - SA_RESETHAND: ハンドラが一度実行された後、SIG_DFL にリセットする。
 *   - SA_NODEFER: ハンドラ実行中に同じシグナルがプロセスのシグナルマスクに
 *     追加されないため、ハンドラが再入可能になる。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g -o 02_sigaction 02_sigaction.c
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "safe_helpers.h"

/*
 * SA_SIGINFO ハンドラ: シグナルに関する詳細情報を受け取る。
 *
 *   int sig          - シグナル番号 (例: SIGUSR1)
 *   siginfo_t *info  - カーネルが提供する、シグナルの送信理由/方法の詳細
 *   void *ucontext   - 割り込み発生時のマシンコンテキスト
 *
 * 有用な siginfo_t メンバ:
 *   si_pid   - 送信プロセスの PID（kill/raise で有効）
 *   si_uid   - 送信プロセスの実 UID
 *   si_code  - 理由コード（kill() は通常 SI_USER、raise() は
 *               実装上 SI_TKILL など、送信 API に応じて異なる）
 *   si_addr  - SIGSEGV、SIGBUS、SIGILL、SIGFPE のフォールトアドレス
 */
static void info_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;

    safe_write_str("[SA_SIGINFO] signal=");
    safe_write_int((long long)sig);
    safe_write_str("  si_pid=");
    safe_write_int((long long)info->si_pid);
    safe_write_str("  si_uid=");
    safe_write_int((long long)info->si_uid);
    safe_write_str("  si_code=");
    safe_write_int((long long)info->si_code);
    safe_write_str("\n");
}

/*
 * SA_RESTART デモ用のシンプルなハンドラ。
 * シグナルが届いたことだけを記録する。興味深い動作は main() 内の
 * 中断された read() に何が起きるか。
 */
static volatile sig_atomic_t alarm_received = 0;

static void alarm_handler(int sig) {
    (void)sig;

    alarm_received = 1;
    safe_write_str("[SA_RESTART] SIGALRM handler ran\n");
}

/*
 * SA_NODEFER ハンドラ。ハンドラ実行中に同じシグナルを意図的に再送する。
 * SA_NODEFER によりシグナルがブロックされないため、最初の呼び出しが
 * 戻る前にハンドラが再帰的に呼び出される。
 *
 * 注: このデモでは SIGUSR1 をハンドラ自身の raise() からのみ配送する。
 * SA_NODEFER の場合、外部から送られた SIGUSR1 が nd_depth で競合する
 * 可能性がある。本番コードでは真の async-signal-safety のために
 * C11 <stdatomic.h> を使うこと。
 */
static volatile sig_atomic_t nd_depth = 0;
static volatile sig_atomic_t nd_raised = 0;

static void nodefer_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)info;
    (void)ucontext;

    nd_depth++;

    safe_write_str("[SA_NODEFER] entered, depth=");
    safe_write_int((long long)nd_depth);
    safe_write_str("\n");

    /* ハンドラ内部から同じシグナルを一度 raise する。SA_NODEFER により
     * 2度目の配送が延期されず、depth が 2 になる。 */
    if (nd_depth == 1 && nd_raised == 0) {
        nd_raised = 1;
        safe_write_str("[SA_NODEFER] raising SIGUSR1 recursively\n");
        raise(SIGUSR1);
    }

    safe_write_str("[SA_NODEFER] leaving, depth=");
    safe_write_int((long long)nd_depth);
    safe_write_str("\n");

    nd_depth--;
}

/*
 * SA_RESETHAND ハンドラ。カーネルはこのハンドラが戻った後、
 * 処理方法を SIG_DFL にリセットする。そのため、2度目の raise() は
 * デフォルト動作（無視）を使う。SIGCHLD のデフォルト動作は
 * シグナルを無視することなので、プログラムは生存し続ける。
 *
 * 代入（インクリメントではなく）で十分: SA_RESETHAND はハンドラが
 * 正確に一度だけ実行されることを意味するので、発火したことだけを記録する。
 */
static volatile sig_atomic_t rese_handled = 0;

static void rese_handler(int sig) {
    (void)sig;

    rese_handled = 1;
    safe_write_str(
        "[SA_RESETHAND] SIGCHLD handled (this should run exactly once)\n");
}

/*
 * 子プロセスで使用する SIGSEGV ハンドラ。
 * si_addr からフォールトアドレスを表示し、フォールトが伝播しないように
 * 終了する。
 */
static void segv_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)ucontext;

    safe_write_str("[SIGSEGV] faulting address: 0x");
    safe_write_hex((unsigned long long)(unsigned long)info->si_addr);
    safe_write_str("\n");

    /* _exit() は async-signal-safe; exit() はストリームをフラッシュし
     * atexit ハンドラを実行するため安全ではない。 */
    _exit(EXIT_SUCCESS);
}

int main(void) {
    struct sigaction act;
    pid_t pid;

    printf("PID: %ld\n", (long)getpid());
    fflush(stdout);

    /* ================================================================
     * 1. SA_SIGINFO: リッチなシグナル情報
     * ================================================================ */
    printf("\n--- 1. SA_SIGINFO on SIGUSR1 ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = info_handler;
    act.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        return EXIT_FAILURE;
    }

    /* raise() でも si_pid == getpid()、si_uid == getuid() は得られるが、
     * si_code は kill() と同じ SI_USER になるとは限らない。 */
    raise(SIGUSR1);

    /* ================================================================
     * 2. SA_RESTART: 中断された read() が自動再開される
     * ================================================================ */
    printf("\n--- 2. SA_RESTART with SIGALRM ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = alarm_handler;
    act.sa_flags = SA_RESTART;

    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        return EXIT_FAILURE;
    }

    {
        int pipefd[2];
        char c = 0;
        ssize_t n;

        if (pipe(pipefd) == -1) {
            perror("pipe");
            return EXIT_FAILURE;
        }

        pid = fork();
        if (pid == -1) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            /* 子: 読み取り端を閉じ、少し待ってから 1 バイト書き込む。 */
            (void)close(pipefd[0]);
            sleep(2);
            c = '!';
            ssize_t ret = write(pipefd[1], &c, 1);
            (void)ret;
            (void)close(pipefd[1]);
            _exit(EXIT_SUCCESS);
        }

        /* 親: 書き込み端を閉じ、read() でブロックする。 */
        (void)close(pipefd[1]);

        /* read() ブロック中にアラームが発火する。SA_RESTART により、read() は
         * EINTR を返さず自動的に再開される。 */
        alarm_received = 0;
        alarm(1);

        printf("Parent: calling read() on an empty pipe...\n");
        fflush(stdout);
        n = read(pipefd[0], &c, 1);
        alarm(0); /* 保留中のアラームをキャンセル */

        if (n == 1) {
            printf(
                "Parent: read() returned '%c', alarm_received=%d "
                "-> read() was restarted after the signal\n",
                c, (int)alarm_received);
        } else if (n == -1) {
            perror("Parent: read()");
        } else {
            printf("Parent: read() returned EOF unexpectedly\n");
        }
        fflush(stdout);

        (void)close(pipefd[0]);
        (void)wait(NULL);
    }

    /* ================================================================
     * 3. SA_NODEFER: 再入可能なシグナル処理
     * ================================================================ */
    printf("\n--- 3. SA_NODEFER on SIGUSR1 ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = nodefer_handler;
    act.sa_flags = SA_SIGINFO | SA_NODEFER;

    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        perror("sigaction(SIGUSR1, SA_NODEFER)");
        return EXIT_FAILURE;
    }

    nd_depth = 0;
    nd_raised = 0;
    raise(SIGUSR1);

    printf("Main: after SA_NODEFER demo, depth should be 0 (actual=%d)\n",
           (int)nd_depth);
    fflush(stdout);

    /* ================================================================
     * 4. SA_RESETHAND: ハンドラが一度実行されるとデフォルトにリセット
     * ================================================================ */
    printf("\n--- 4. SA_RESETHAND on SIGCHLD ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = rese_handler;
    act.sa_flags = SA_RESETHAND;

    if (sigaction(SIGCHLD, &act, NULL) == -1) {
        perror("sigaction(SIGCHLD, SA_RESETHAND)");
        return EXIT_FAILURE;
    }

    rese_handled = 0;
    raise(SIGCHLD); /* ハンドラが実行される */
    raise(SIGCHLD); /* デフォルト動作: 無視 */

    printf("Main: rese_handled=%d (should be 1 because handler was reset)\n",
           (int)rese_handled);
    fflush(stdout);

    /* ================================================================
     * 5. siginfo_t.si_addr の SIGSEGV での利用
     * ================================================================ */
    printf("\n--- 5. SIGSEGV si_addr in a child process ---\n");
    fflush(stdout);

    pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* 子プロセス: SIGSEGV ハンドラをインストールし、NULL をデリファレンス
         */
        memset(&act, 0, sizeof(act));
        sigemptyset(&act.sa_mask);
        act.sa_sigaction = segv_handler;
        act.sa_flags = SA_SIGINFO;

        if (sigaction(SIGSEGV, &act, NULL) == -1) {
            perror("sigaction(SIGSEGV)");
            _exit(EXIT_FAILURE);
        }

        *(volatile int*)NULL = 0; /* 必ずフォールトする */

        /* ハンドラが戻った場合（実際には戻らない）、未定義動作 */
        _exit(EXIT_FAILURE);
    } else {
        int status;
        (void)wait(&status);
        if (WIFEXITED(status)) {
            printf("Main: child exited with status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Main: child killed by signal %d\n", WTERMSIG(status));
        }
        fflush(stdout);
    }

    return EXIT_SUCCESS;
}
