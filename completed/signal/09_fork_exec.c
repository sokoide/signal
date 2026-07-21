/*
 * 09_fork_exec.c
 *
 * テーマ: fork() と exec() をまたぐシグナル処理方法の継承。
 *
 * POSIX は、プロセスが fork したり別のプログラムを exec したりしたときに
 * 何が生存するかを正確に定義している。このプログラムは各ルールを
 * 具体的な出力で示す:
 *
 *   fork():
 *     - シグナルハンドラは継承される（子プロセスは親と同じハンドラ
 *       関数ポインタで開始する）。
 *     - シグナルマスクは継承される。
 *     - 保留シグナルは *継承されない*。子の保留セットは fork() の
 *       瞬間にクリアされる。
 *     - 代替シグナルスタック（sigaltstack()）は Linux など多くの
 *       システムで継承されるが、実装定義。一部のプラットフォーム
 *       （例: macOS）では子でクリアされる。
 *
 *   exec():
 *     - 捕捉用に設定されたシグナルハンドラは SIG_DFL にリセットされる。
 *     - SIG_IGN に設定されたシグナルハンドラは無視されたまま。
 *     - シグナルマスクは保持される。
 *     - 代替シグナルスタックはクリア（無効化）される。
 *
 * 使い方:
 *   cc -std=c11 -Wall -Wextra -O2 -g 09_fork_exec.c -o 09_fork_exec
 *   ./09_fork_exec
 *
 * プログラムは "--after-exec" 引数で自身を再実行し、exec 後の状態を
 * 同じバイナリ内で検査できるようにする。
 */

/* 機能テストマクロはこのファイルが Makefile とは独立して
 * 上記の "cc ..." コマンドだけでスタンドアロンでビルドできるよう
 * ここで宣言する。
 *   macOS: _DARWIN_C_SOURCE は SIGSTKSZ を公開する（sigaltstack デモで使用）。
 *   Linux: _GNU_SOURCE は同様のインタフェース群を統一的に公開する。 */
#if (defined(__APPLE__) && defined(__MACH__))
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* malloc した代替シグナルスタックを保持。クリーン終了時に解放するため。 */
static void* alt_stack_mem = NULL;

/* 親プロセスで SIGUSR1 用にインストールする小さなハンドラ */
static void usr1_handler(int sig) {
    (void)sig; /* 未使用; -Wextra 警告抑制 */
}

/*
 * シグナルの処理方法を1行で表示する。
 * sigaction() に第2引数 NULL を渡すことで、処理方法を変更せずに問い合わせる。
 */
static void print_disposition(int sig, const char* name) {
    struct sigaction old;
    if (sigaction(sig, NULL, &old) == -1) {
        perror("sigaction");
        return;
    }

    printf("    %s: ", name);
    if (old.sa_handler == SIG_DFL) {
        printf("SIG_DFL (default action)\n");
    } else if (old.sa_handler == SIG_IGN) {
        printf("SIG_IGN (ignored)\n");
    } else {
        /* 関数ポインタはアドレスとして表示 */
        printf("caught by %p\n", (void*)old.sa_handler);
    }
}

/* 呼び出し元のマスクでシグナルが現在ブロックされているか表示 */
static void print_blocked(int sig, const char* name) {
    sigset_t mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask) == -1) {
        perror("sigprocmask");
        return;
    }

    printf("    %s is %s\n", name,
           sigismember(&mask, sig) ? "BLOCKED" : "NOT blocked");
}

/* 現在の代替シグナルスタック状態を表示 */
static void print_alt_stack(void) {
    stack_t ss;
    if (sigaltstack(NULL, &ss) == -1) {
        perror("sigaltstack");
        return;
    }

    if (ss.ss_flags & SS_DISABLE) {
        printf("    alternate stack: DISABLED\n");
    } else {
        printf("    alternate stack: ENABLED at %p, size %zu\n", ss.ss_sp,
               ss.ss_size);
    }
}

/* 現在の保留シグナルセットを人間可読形式で表示 */
static void print_pending(void) {
    sigset_t pending;
    if (sigpending(&pending) == -1) {
        perror("sigpending");
        return;
    }

    int has_sigusr1 = sigismember(&pending, SIGUSR1);
    int has_sigusr2 = sigismember(&pending, SIGUSR2);

    if (!has_sigusr1 && !has_sigusr2) {
        printf("    pending signals: (none)\n");
    } else {
        printf("    pending signals: %s%s%s\n", has_sigusr1 ? "SIGUSR1" : "",
               (has_sigusr1 && has_sigusr2) ? ", " : "",
               has_sigusr2 ? "SIGUSR2" : "");
    }
}

/*
 * 現在のプロセスの完全なシグナル環境を表示。
 * デモ内の3つの異なる実行コンテキストから呼ばれる。
 */
static void print_status(const char* label) {
    printf("\n== %s ==\n", label);
    print_disposition(SIGUSR1, "SIGUSR1");
    print_disposition(SIGPIPE, "SIGPIPE");
    print_blocked(SIGUSR2, "SIGUSR2");
    print_pending();
    print_alt_stack();
}

/* sigaction() を使用してシグナルハンドラを設定 */
static void set_handler(int sig, void (*handler)(int)) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

/* sigaction() を使用してシグナル処理方法を SIG_IGN に設定 */
static void set_ignore(int sig) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

/* 単一のシグナルをブロックまたはブロック解除 */
static void set_block(int sig, int block) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
}

/* 代替シグナルスタックをインストール */
static void setup_alt_stack(void) {
    stack_t ss;
    /* SIGSTKSZ は代替スタックに推奨される標準的なサイズ。
     * glibc >= 2.34 では定数式でない可能性がある。その場合は
     * sysconf() に基づいて動的に割り当てる必要がある。
     * このデモでは SIGSTKSZ を直接使用。 */
    ss.ss_size = SIGSTKSZ;
    alt_stack_mem = malloc(ss.ss_size);
    if (alt_stack_mem == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    ss.ss_sp = alt_stack_mem;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) {
        perror("sigaltstack");
        exit(EXIT_FAILURE);
    }
}

/*
 * 子プロセスが "--after-exec" 引数で exec() を呼んだ後に実行される分岐。
 * exec 後も生存するシグナル状態を表示する。
 */
static int after_exec_branch(void) {
    print_status("After exec() in child");
    fflush(stdout);
    return EXIT_SUCCESS;
}

/*
 * メインのデモ分岐。親のシグナル環境を準備し、fork し、
 * 子に検査させてから自身を exec させる。
 */
static int main_branch(char* argv0) {
    /*
     * 1. 親の設定:
     *    - SIGUSR1 を捕捉。
     *    - SIGPIPE を無視。
     *    - SIGUSR2 をブロック。
     *    - 代替シグナルスタックを設定。
     */
    set_handler(SIGUSR1, usr1_handler);
    set_ignore(SIGPIPE);
    set_block(SIGUSR2, 1);
    setup_alt_stack();

    /*
     * 保留シグナルの継承ルールを示す。
     * SIGUSR2 をブロック中に raise すると、親でのみ保留状態になる。
     * fork() はこの保留シグナルを子に *コピーしない*。
     */
    if (raise(SIGUSR2) != 0) {
        perror("raise");
        exit(EXIT_FAILURE);
    }

    printf("Parent state before fork():\n");
    print_status("Parent before fork()");
    /*
     * fork の前に stdout をフラッシュし、子が上記の行のバッファリングされた
     * コピーを継承して再表示するのを防ぐ。
     */
    fflush(stdout);

    /*
     * 2. fork() は親のメモリとシグナル状態の正確なコピーである子を作成する。
     *    ただし、保留シグナルはクリアされる。
     */
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        /*
         * 子プロセス。
         *
         * コピーオンライトにより、usr1_handler のアドレスは親と同じで、
         * シグナルマスクもコピーされる。
         * fork() 前に raise された SIGUSR2 はここでは保留状態になるべきでない。
         * なぜなら保留シグナルは子でクリアされるため。
         *
         * 代替スタックの継承はオペレーティングシステムに依存する
         * （Linux では継承、macOS ではクリアされる）。
         */
        print_status("After fork() in child");
        fflush(stdout);

        /*
         * 3. "--after-exec" で同じバイナリを exec() する。
         *
         * exec() 後:
         *   - 捕捉ハンドラ（SIGUSR1）は SIG_DFL に戻る。
         *   - 無視ハンドラ（SIGPIPE）は SIG_IGN のまま。
         *   - シグナルマスクは保持される。
         *   - 代替スタックはクリアされる。
         */
        execl(argv0, argv0, "--after-exec", (char*)NULL);

        /* execl() は失敗時にのみ戻る */
        perror("execl");
        _exit(EXIT_FAILURE);
    }

    /* 親: 子のデモ終了を待つ */
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
        printf("\nChild exited successfully.\n");
    } else {
        printf("\nChild did not exit successfully.\n");
    }

    printf("\n");
    printf("Summary of POSIX signal inheritance rules:\n");
    printf("  fork():  signal handlers are inherited.\n");
    printf("  fork():  signal mask is inherited.\n");
    printf("  fork():  pending signals are CLEARED for the child.\n");
    printf(
        "  fork():  alternate stack inheritance is implementation-defined\n");
    printf("           (inherited on Linux, often cleared on macOS).\n");
    printf("  exec():  caught handlers reset to SIG_DFL.\n");
    printf("  exec():  ignored handlers (SIG_IGN) remain ignored.\n");
    printf("  exec():  signal mask is preserved.\n");
    printf("  exec():  alternate signal stack is cleared.\n");

    free(alt_stack_mem);
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    /*
     * argv[0] は自身の再実行に使用される。欠けているとデモを続行できない。
     */
    if (argc > 1 && strcmp(argv[1], "--after-exec") == 0) {
        return after_exec_branch();
    }

    return main_branch(argv[0]);
}
