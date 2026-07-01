/*
 * 05_altstack.c — 代替シグナルスタック: sigaltstack() と SA_ONSTACK
 *
 * 通常、シグナルハンドラはプロセスの通常スタック上で実行される。
 * そのスタックがすでに枯渇している場合（例: 無限再帰）、SIGSEGV ハンドラを
 * 実行するためのスタック領域が残っていないため、ハンドラ自身がクラッシュする。
 *
 * sigaltstack() はシグナルハンドラ用に予約された独立したスタックを提供する。
 * SA_ONSTACK フラグと組み合わせることで、特定のハンドラを通常スタックではなく
 * 事前割り当てされたメモリ領域で実行できる。
 *
 * これは OS カーネルがハードウェア割り込み用に「割り込みスタック」を保持する
 * のと同じ設計思想。HW 割り込みが発生すると、CPU はカーネルスタックに切り替えて
 * ISR を実行する。sigaltstack はそのメカニズムのユーザ空間版。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g 05_altstack.c -o 05_altstack
 *
 * 注:
 *   macOS では SIGSTKSZ の定義に _DARWIN_C_SOURCE が必要な場合がある。
 *   必要な機能テストマクロはこのファイルの先頭で定義されているため、
 *   上記の "cc ..." コマンドだけでスタンドアロンでビルドできる
 *   （Makefile は macOS で -Wno-deprecated-declarations を追加するだけ）。
 */

#if (defined(__APPLE__) && defined(__MACH__))
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700 /* macOS: <ucontext.h> に必要 */
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* Linux: 必要に応じて sigaltstack 拡張 */
#endif
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

/* malloc した代替スタックメモリを保持。クリーンパスで解放するため。 */
static void* alt_stack_mem = NULL;

/*
 * シグナルハンドラ内から呼び出せる固定長メッセージ書き込みヘルパー。
 * write(2) は async-signal-safe なのでハンドラ内で使用可能。
 * printf() は内部ロックを保持する可能性があるため、ハンドラ内では禁止。
 */
static void safe_puts(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    ssize_t ret = write(STDOUT_FILENO, s, len);
    (void)ret;
}

/*
 * SIGSEGV ハンドラ。
 *
 * 実行時には通常スタックがオーバーフローしている可能性があるため、
 * SA_ONSTACK のおかげで代替スタック上で実行される。
 *
 * ハンドラ内では:
 *   1. 配送されたシグナル名を表示。
 *   2. sigaltstack() で実際に代替スタック上にいることを確認。
 *   3. _exit() でプロセスを安全に終了。
 */
static void segv_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)info;
    (void)ucontext;

    safe_puts("\n[Caught SIGSEGV in handler]\n");

    /*
     * sigaltstack(NULL, &current) は現在の代替スタック情報を取得する。
     * ハンドラが代替スタック上で実行中の場合、ss_flags に SS_ONSTACK フラグが
     * セットされる。値が 1 であれば、確かに代替スタック上で実行中であることが
     * 確認できる。
     */
    stack_t current;
    if (sigaltstack(NULL, &current) == -1) {
        safe_puts("  sigaltstack() failed inside handler\n");
    } else {
        if (current.ss_flags & SS_ONSTACK) {
            safe_puts("  Handler IS running on the alternate stack.\n");
        } else {
            safe_puts("  Handler is NOT running on the alternate stack (!)\n");
        }
    }

    /*
     * ハンドラ内からプロセスを終了するには _exit(2) を使う。
     * exit(3) は stdio バッファをフラッシュし atexit ハンドラを実行するため、
     * async-signal-safe ではない。
     */
    safe_puts("  Calling _exit(128 + SIGSEGV) safely...\n");
    _exit(128 + SIGSEGV);
}

/*
 * スタック上に割り当てたバッファの数バイトに触れるヘルパー。
 *
 * noinline 属性により、コンパイラが overflow() 内の再帰呼び出しを
 * 単一のバッファに潰すのを防ぐ。各呼び出しのローカル配列のアドレスを渡すことで、
 * すべての呼び出しで完全なバッファを強制的にスタックに割り当てさせ、
 * -O2 でもスタックオーバーフローデモが確実に動作するようにする。
 */
static void touch_stack(volatile char* p, size_t len) __attribute__((noinline));
static void touch_stack(volatile char* p, size_t len) {
    p[0] = 1;
    p[len / 2] = 2;
    p[len - 1] = 3;
}

/*
 * 意図的にスタックオーバーフローを発生させる再帰関数。
 *
 * 各呼び出しで 8 KiB のローカル配列を割り当て、深く再帰する。
 * 上記の noinline ヘルパーにより、コンパイラが -O2 で
 * 呼び出しごとの割り当てを最適化除去するのを防ぐ。
 * 再帰呼び出し後の書き込みにより、末尾呼び出し最適化も防止する。
 */
static volatile int g_depth = 0;

static void overflow(int n) {
    volatile char frame[8192];

    touch_stack(frame, sizeof(frame));
    g_depth++;

    if (n > 0) {
        overflow(n - 1);
    }

    /* ここには到達しないが、末尾呼び出し最適化を防ぐ。 */
    frame[1] = 1;
}

int main(void) {
    /*
     * ============================================================
     * ステップ 1: 代替スタックを割り当てる
     * ============================================================
     *
     * SIGSTKSZ はシグナルハンドラの実行に「通常は十分な」サイズ。
     * MINSIGSTKSZ より小さくてはならない。
     *
     * 実際のアプリケーションでは、ハンドラのスタック使用量に応じて
     * SIGSTKSZ よりも多く割り当てること。ここではデモ用に SIGSTKSZ を使う。
     */
    stack_t ss;
    /* SIGSTKSZ は代替スタックの標準的な推奨サイズ。
     * glibc >= 2.34 では定数式でない可能性がある。その場合は
     * sysconf() で動的にサイズを決定する必要がある。
     * ここではデモ用に SIGSTKSZ を直接使用。
     * 本番コードではハンドラのスタック使用量に応じて余裕を持たせること。 */
    ss.ss_size = SIGSTKSZ;
    alt_stack_mem = malloc(ss.ss_size);
    if (alt_stack_mem == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    ss.ss_sp = alt_stack_mem;
    ss.ss_flags = 0; /* 有効化（SS_DISABLE ではない） */

    if (sigaltstack(&ss, NULL) == -1) {
        perror("sigaltstack");
        free(alt_stack_mem);
        exit(EXIT_FAILURE);
    }

    printf("Allocated alternate stack at %p, size %zu bytes.\n", ss.ss_sp,
           ss.ss_size);

    /*
     * ============================================================
     * ステップ 2: SIGSEGV ハンドラを SA_ONSTACK で登録
     * ============================================================
     *
     * sa_flags に SA_ONSTACK を含めることで、このハンドラは
     * sigaltstack() で割り当てた代替スタック上で実行されるようになる。
     * 通常スタック上では実行されない。
     *
     * SA_SIGINFO を追加すると、ハンドラが3引数形式
     *   void handler(int sig, siginfo_t *info, void *ucontext)
     * になり、より詳細な情報が利用可能になる。
     */
    struct sigaction sa;
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction(SIGSEGV)");
        free(alt_stack_mem);
        exit(EXIT_FAILURE);
    }

    printf("SIGSEGV handler installed with SA_ONSTACK.\n");
    printf("Now triggering stack overflow via deep recursion...\n");
    fflush(stdout); /* printf + fflush は main コンテキストでは安全 */

    /*
     * ============================================================
     * ステップ 2b: メインスタックのソフトリミットを下げる
     * ============================================================
     *
     * デフォルトの RLIMIT_STACK ソフトリミットは環境によって大きく異なる
     * （例: Linux では 8 MiB、macOS の make 下では 64 MiB）。
     * リミットが大きいと、overflow() は通常スタックが枯渇する前に
     * 終了条件を満たすほど深く再帰できるため、「この行は決して表示されない」
     * という行に到達してしまう。
     *
     * 代替スタックのデモを確実に動作させるために、overflow() を呼び出す前に
     * ソフトリミットを既知の小さな値に下げる。これにより、再帰関数が戻る前に
     * 通常スタックが枯渇し SIGSEGV が配送されることが保証される。
     */
    {
        const rlim_t desired_stack = 1024 * 1024; /* 1 MiB */
        struct rlimit rl;

        if (getrlimit(RLIMIT_STACK, &rl) == -1) {
            perror("getrlimit(RLIMIT_STACK)");
            free(alt_stack_mem);
            exit(EXIT_FAILURE);
        }

        if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < desired_stack) {
            rl.rlim_cur = rl.rlim_max;
        } else {
            rl.rlim_cur = desired_stack;
        }

        if (setrlimit(RLIMIT_STACK, &rl) == -1) {
            perror("setrlimit(RLIMIT_STACK)");
            free(alt_stack_mem);
            exit(EXIT_FAILURE);
        }

        printf("Main stack soft limit lowered to %llu bytes.\n",
               (unsigned long long)rl.rlim_cur);
        fflush(stdout);
    }

    /*
     * ============================================================
     * ステップ 3: スタックオーバーフローを発生させる
     * ============================================================
     *
     * overflow() は巨大なローカル配列を再帰的に割り当て続け、
     * 通常スタックが枯渇するまで繰り返す。CPU が不正なメモリアクセスを
     * 検出し、カーネルが SIGSEGV をプロセスに送る。
     *
     * 代替スタックがない場合、SIGSEGV ハンドラを起動するスタック領域が
     * 残っておらず、ハンドラ自身が別の SIGSEGV を発生させ、
     * 即座にプロセスが強制終了される。
     */
    overflow(1000000);

    /*
     * この地点には決して到達しない（overflow() はクラッシュするか、
     * ハンドラ内で _exit() が呼ばれる）。
     */
    printf("This line should never be printed.\n");
    free(alt_stack_mem);
    return 0;
}
