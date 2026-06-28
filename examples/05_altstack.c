/*
 * 05_altstack.c — 代替シグナルスタック sigaltstack() と SA_ONSTACK
 *
 * 通常、シグナルハンドラはプロセスの通常スタック上で実行されます。
 * しかし、通常スタックがすでに枯渇している場合（例: 無限再帰による
 * スタックオーバーフロー）、SIGSEGV ハンドラを実行するスタック領域が
 * 残っていないため、ハンドラ自身もクラッシュしてしまいます。
 *
 * sigaltstack() は「シグナル専用の別スタック」を用意する仕組みです。
 * これを SA_ONSTACK フラグと組み合わせると、特定のハンドラを
 * あらかじめ確保したメモリ領域上で実行できます。
 *
 * これは OS カーネルが「割り込み用カーネルスタック」を持つのと
 * 同じ設計思想です。HW 割り込みが発生した際、CPU はカーネルスタックに
 * 切り替えて ISR を実行します。sigaltstack はそのユーザ空間版です。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g 05_altstack.c -o 05_altstack
 *
 * 注意:
 *   macOS では SIGSTKSZ の定義に _DARWIN_C_SOURCE が必要な場合があります。
 *   本リポジトリの Makefile では自動的に追加しています。
 */

#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 代替シグナルスタック用に malloc したメモリを保持し、エラー経路で解放する。 */
static void* alt_stack_mem = NULL;

/*
 * シグナルハンドラ内から呼べる、固定長のメッセージ書き込みヘルパー。
 * write(2) は async-signal-safe なので、ハンドラ内で使えます。
 * printf() は内部ロックを保持する可能性があるため、ハンドラ内では禁止です。
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
 * 通常のスタックがオーバーフローしている状況で呼ばれるため、
 * このハンドラは SA_ONSTACK によって代替スタック上で実行されます。
 *
 * ハンドラ内では以下のことを行います:
 *   1. 到達したシグナル名を表示
 *   2. sigaltstack() で現在代替スタック上にいるか確認
 *   3. 安全にプロセスを終了 (_exit)
 */
static void segv_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)info;
    (void)ucontext;

    safe_puts("\n[Caught SIGSEGV in handler]\n");

    /*
     * sigaltstack(NULL, &current) で現在の代替スタック情報を取得します。
     * ハンドラ実行中に呼ぶと、ss_flags に SS_ONSTACK がセットされています。
     * これが 1 なら、ハンドラは確かに代替スタック上で動作しています。
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
     * ハンドラ内でプロセスを終了するときは _exit(2) を使います。
     * exit(3) は stdio バッファをフラッシュしたり atexit ハンドラを
     * 呼んだりするため、非同期シグナル安全ではありません。
     */
    safe_puts("  Calling _exit(128 + SIGSEGV) safely...\n");
    _exit(128 + SIGSEGV);
}

/*
 * スタックオーバーフローを意図的に引き起こす再帰関数。
 *
 * 各呼び出しで大きなローカル配列を確保し、再帰を深く掘っていきます。
 * コンパイラが末尾呼び出し最適化（tail-call optimization）を
 * 行うのを防ぐため、再帰呼び出しの後にダミーのコードを置いています。
 *
 * counter を volatile にしておくことで、最適化によって
 * 配列アクセスや再帰が削除されるのを防ぎます。
 */
static volatile int g_depth = 0;

/*
 * スタックオーバーフローを引き起こす再帰関数。
 *
 * 呼び出し回数を引数 n で制御できるように見せかけていますが、
 * main() からは十分大きな値を渡すため、実際にはスタックを使い果たして
 * SIGSEGV を起こします。
 *
 * コンパイラに「無限再帰」と警告されないよう、終了条件を明示しています。
 */
static void overflow(int n) {
    /*
     * 1 回の呼び出しで 8KiB のスタックを消費。
     * volatile を付け、かつ複数箇所に書き込むことで、
     * コンパイラがこの配列をレジスタ化・削除するのを防ぎます。
     */
    volatile char frame[8192];

    frame[0] = (char)n;
    frame[4095] = (char)(n >> 8);
    frame[8191] = (char)(n >> 16);
    g_depth++;
    /* Suppress unused variable warning: frame exists to consume stack */
    (void)frame;

    if (n > 0) {
        overflow(n - 1);
    }

    /* 到達しないが、末尾呼び出し最適化を防ぐ */
    frame[1] = 1;
}

int main(void) {
    /*
     * ============================================================
     * ステップ 1: 代替スタックの確保
     * ============================================================
     *
     * SIGSTKSZ は「通常のシグナルハンドラを実行するのに十分な」
     * サイズです。MINSIGSTKSZ より小さくすることはできません。
     *
     * 実際のアプリケーションでは、ハンドラが使うスタック量に応じて
     * SIGSTKSZ より大きな領域を確保してください。ここではデモ用に SIGSTKSZ を
     * 使います。
     */
    stack_t ss;
    /* SIGSTKSZ は代替スタックに推奨される典型的なサイズです。
     * glibc >= 2.34 では非定数式になる場合があり、その場合は sysconf() 等で
     * 動的にサイズを決める必要があります。ここではデモ用に SIGSTKSZ
     * を使います。
     * 本番アプリケーションでは、ハンドラのスタック使用量に応じて余裕を持たせて
     * ください。 */
    ss.ss_size = SIGSTKSZ;
    alt_stack_mem = malloc(ss.ss_size);
    if (alt_stack_mem == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    ss.ss_sp = alt_stack_mem;
    ss.ss_flags = 0; /* SS_DISABLE ではなく有効にする */

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
     * sa_flags に SA_ONSTACK を含めると、このハンドラは
     * 通常スタックではなく sigaltstack() で指定した代替スタック上で
     * 実行されます。
     *
     * また SA_SIGINFO を指定すると、ハンドラは
     *   void handler(int sig, siginfo_t *info, void *ucontext)
     * の 3 引数形式になり、より詳細な情報にアクセスできます。
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
    fflush(stdout); /* main コンテキストなので printf → fflush は安全 */

    /*
     * ============================================================
     * ステップ 3: スタックオーバーフローを引き起こす
     * ============================================================
     *
     * overflow() は再帰的に大きなローカル配列を確保し続け、
     * 最終的に通常スタックを使い果たします。
     * その結果 CPU は不正メモリアクセスを検出し、カーネルは SIGSEGV を
     * プロセスに送ります。
     *
     * もし代替スタックを使わなければ、SIGSEGV ハンドラを呼ぶための
     * スタックが残っておらず、ハンドラ自身で再び SIGSEGV を起こして
     * プロセスが即座に異常終了してしまいます。
     */
    overflow(1000000);

    /*
     * ここには到達しません（overflow() はクラッシュするか、
     * ハンドラ内で _exit() されるため）。
     */
    printf("This line should never be printed.\n");
    free(alt_stack_mem);
    return 0;
}
