/*
 * 08_hw_interrupt.c — HW 割り込みと POSIX シグナルの構造的等価性
 *
 * このリポジトリの中心的なサンプル。
 *
 * POSIX シグナルハンドラが、CPU ハードウェア割り込みサービスルーチン（ISR）
 * と構造的に近いユーザ空間版であることを示す。
 *
 * ハードウェア割り込みの流れ (x86_64)        POSIX シグナルの流れ
 * ------------------------------             -------------------
 * 1. デバイスが IRQ を発生 (タイマ PIT)      1. kill() / setitimer() がシグナルを生成
 * 2. CPU が SS:RSP/RFLAGS/CS:RIP を          2. カーネルが完全なレジスタ状態を
 *    カーネルスタックにプッシュ                  ucontext_t（シグナルフレーム）に保存
 * 3. CPU が IDT[ベクタ] で ISR を検索       3. カーネルが sigaction テーブルを参照
 * 4. ISR を実行                              4. シグナルハンドラを実行
 * 5. iret でレジスタ復元、メインプログラム   5. sigreturn() で ucontext_t を復元、
 *    再開                                        メインプログラム再開
 *
 * このプログラムでは、setitimer(ITIMER_VIRTUAL) による SIGVTALRM が
 * プロセスの「タイマ割り込み」として機能する。ハンドラは第3引数
 * ucontext_t* を受け取る。これはトラップフレーム:
 * 割り込みされたユーザコードの完全な保存 CPU コンテキスト。
 * プログラムカウンタ（PC）、スタックポインタ（SP）、フレームポインタ（FP）を
 * 表示することで、カーネルが本当にメインプログラムの実行を途中で
 * 凍結した際の保存状態を観測する。タイマ発火のタイミングは非決定的
 * なので、複数回の PC 値が必ず異なることまでは保証しない。
 *
 * なぜハンドラ内で write() だけを使うか？
 * --------------------------------
 * シグナルハンドラはいつでもメインプログラムに割り込める。メインプログラムが
 * stdio や malloc のロックを保持している最中でも同様。ハンドラ内で
 * printf/malloc などを使うとデッドロックや状態破損を起こす。
 * write(2) は async-signal-safe が保証される数少ない POSIX 関数の一つなので、
 * すべてのハンドラ出力に使用する。
 *
 * ucontext_t に関する注記
 * ------------------
 * ucontext_t と getcontext()/setcontext() は POSIX.1-2008 で削除された。
 * Linux と macOS では引き続き広く利用可能だが、新しい移植性のあるコードでは
 * 代替メカニズムを検討すべき。ここでは保存 CPU コンテキストを可視化する
 * 最も直接的な方法として ucontext_t を使用する。
 *
 * ビルド: cc -std=c11 -Wall -Wextra -O2 -g completed/signal/08_hw_interrupt.c -o
 * 08_hw_interrupt
 * 実行:  ./08_hw_interrupt
 *
 * プラットフォーム備考
 * --------------
 * - Linux glibc x86_64:   uc_mcontext は埋め込み; レジスタは
 * gregs[REG_RIP/RSP/RBP] 経由。
 * - Linux glibc aarch64:  uc_mcontext は埋め込み; レジスタは .pc/.sp/.regs[29] 経由。
 * - macOS x86_64:         uc_mcontext は POINTER; デリファレンスして
 * __ss.__rip/__rsp/__rbp。
 * - macOS ARM64 (Apple):  uc_mcontext は POINTER; デリファレンスして
 * __ss.__pc/__sp/__fp。
 */

/*
 * macOS: <ucontext.h> には _XOPEN_SOURCE が必要（ルーチンは非推奨で
 *   そのマクロの背後にゲートされている）。_DARWIN_C_SOURCE は古い macOS
 *   で SIGSTKSZ / ucontext を確実にする。
 * Linux: _GNU_SOURCE は ucontext_t とレジスタマクロ
 *   （gregs、REG_RIP 等）を <sys/ucontext.h> から公開するために必要。
 */
#if (defined(__APPLE__) && defined(__MACH__))
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

/* タイマ「割り込み」を何回デモしてから無効化するか。 */
#define MAX_INTERRUPTS 5

/* シグナルハンドラのみがインクリメントするカウンタ。
 * sig_atomic_t により、読み書きがシグナル割り込みに対して
 * アトミックであることが保証される。 */
static volatile sig_atomic_t interrupt_count = 0;

/*
 * printf() を使わずに出力する async-signal-safe ヘルパー。
 * スタックバッファにメッセージを構築し、write(2) で書き込む。
 */

/* write(2) でヌル終端文字列を標準出力に書き込む。 */
static void safe_puts(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    ssize_t ret = write(STDOUT_FILENO, s, len);
    (void)ret;
}

#if (defined(__APPLE__) && defined(__MACH__) &&        \
     (defined(__x86_64__) || defined(__aarch64__))) || \
    (defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)))
/* 16進数の1桁を書き込む。 */
static void safe_hexdigit(int n) {
    char c = (char)((n < 10) ? ('0' + n) : ('a' + (n - 10)));
    ssize_t ret = write(STDOUT_FILENO, &c, 1);
    (void)ret;
}

/* uintptr_t 値を固定の 0x プレフィックス付き16進数で書き込む。 */
static void safe_hex(uintptr_t val) {
    safe_puts("0x");
    for (int i = (int)(sizeof(val) * 8 - 4); i >= 0; i -= 4) {
        safe_hexdigit((int)((val >> (unsigned)i) & 0xF));
    }
}
#endif

/*
 * シグナルハンドラ — 私たちの「割り込みサービスルーチン（ISR）」。
 *
 * 第3引数 void *uctx_ptr は ucontext_t へのポインタ。
 * ucontext_t には割り込みされたコンテキストの保存レジスタ状態が含まれ、
 * ハードウェアのトラップフレームと正確に対応する。
 */
static void sigvtalrm_isr(int sig, siginfo_t* info, void* uctx_ptr) {
    (void)info;

    /* 不透明な第3引数を具体的なコンテキスト型にキャスト */
    ucontext_t* uctx = (ucontext_t*)uctx_ptr;

    /*
     * プラットフォーム固有の保存レジスタ取り出し。
     * これらの値は *割り込みされた* メインループの状態。
     */
#if defined(__APPLE__) && defined(__MACH__) && defined(__x86_64__)
    /*
     * macOS/Darwin x86_64:
     * uc_mcontext はポインタ（mcontext_t）。デリファレンスして
     * __darwin_mcontext64 構造体に到達し、x86_64 保存状態を読み取る。
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL && uctx->uc_mcontext != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext->__ss.__rip);
        sp = (uintptr_t)(uctx->uc_mcontext->__ss.__rsp);
        fp = (uintptr_t)(uctx->uc_mcontext->__ss.__rbp);
    }
#elif defined(__APPLE__) && defined(__MACH__) && defined(__aarch64__)
    /*
     * macOS/Darwin ARM64 (Apple Silicon):
     * uc_mcontext はポインタ（mcontext_t）。デリファレンスして
     * __darwin_arm_thread_state64 に到達し、pc/sp/fp/lr を読み取る。
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL && uctx->uc_mcontext != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext->__ss.__pc);
        sp = (uintptr_t)(uctx->uc_mcontext->__ss.__sp);
        fp = (uintptr_t)(uctx->uc_mcontext->__ss.__fp);
    }
#elif defined(__linux__) && defined(__x86_64__)
    /*
     * Linux glibc x86_64:
     * uc_mcontext は埋め込み mcontext_t。gregs[] に汎用レジスタが入る。
     * REG_RIP/REG_RSP/REG_RBP は sys/ucontext.h で定義される。
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext.gregs[REG_RIP]);
        sp = (uintptr_t)(uctx->uc_mcontext.gregs[REG_RSP]);
        fp = (uintptr_t)(uctx->uc_mcontext.gregs[REG_RBP]);
    }
#elif defined(__linux__) && defined(__aarch64__)
    /*
     * Linux ARM64 (aarch64):
     * uc_mcontext は pc、sp、regs[29]（フレームポインタ）、
     * regs[30]（リンクレジスタ）を持つ。
     */
    uintptr_t pc = 0, sp = 0, fp = 0;
    if (uctx != NULL) {
        pc = (uintptr_t)(uctx->uc_mcontext.pc);
        sp = (uintptr_t)(uctx->uc_mcontext.sp);
        fp = (uintptr_t)(uctx->uc_mcontext.regs[29]);
    }
#else
    /* その他のプラットフォーム: ucontext のレイアウトは様々。
     * 割り込みが発火したことを示すバナーは表示するが、
     * レジスタは移植可能にデコードできない。 */
    uintptr_t pc = 0, sp = 0, fp = 0;
    (void)uctx;
#endif

    /*
     * write(2) のみを使って「割り込み」を表示。
     * シグナルハンドラから出力を生成する唯一の安全な方法。
     */
    safe_puts("\n=== Timer ISR (SIGVTALRM) ===\n");
    safe_puts("Signal number: ");
    {
        char nbuf[4] = {0};
        int n = sig;
        int pos = 0;
        if (n >= 100)
            nbuf[pos++] = (char)('0' + (n / 100));
        if (n >= 10)
            nbuf[pos++] = (char)('0' + ((n / 10) % 10));
        nbuf[pos++] = (char)('0' + (n % 10));
        ssize_t ret = write(STDOUT_FILENO, nbuf, (size_t)pos);
        (void)ret;
    }
    safe_puts("\n");

#if (defined(__APPLE__) && defined(__MACH__) &&        \
     (defined(__x86_64__) || defined(__aarch64__))) || \
    (defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)))
    safe_puts("Saved PC  (program counter) = ");
    safe_hex(pc);
    safe_puts("\n");

    safe_puts("Saved SP  (stack pointer)   = ");
    safe_hex(sp);
    safe_puts("\n");

    safe_puts("Saved FP  (frame pointer)   = ");
    safe_hex(fp);
    safe_puts("\n");
#else
    safe_puts(
        "Register dump: N/A (add platform-specific #ifdef for this arch)\n");
#endif

    safe_puts("==============================\n");

    /* このハンドラ実行中は同じ SIGVTALRM が自動ブロックされるため、
     * この単一ハンドラだけが更新するカウンタでは ++ が他の同種ハンドラ
     * と競合しない。sig_atomic_t 自体が read-modify-write 全体の原子性を
     * 保証するわけではない点に注意（SA_NODEFER や複数スレッドでは不十分）。 */
    ++interrupt_count;
}

/*
 * 仮想時間インターバルタイマを設定。
 * ITIMER_VIRTUAL はこのプロセスが消費したユーザモード CPU 時間のみを
 * カウントする。500ms の CPU 時間ごとにカーネルが SIGVTALRM を生成。
 */
static int arm_timer(unsigned long usec) {
    struct itimerval it;

    it.it_value.tv_sec = (time_t)(usec / 1000000UL);
    it.it_value.tv_usec = (suseconds_t)(usec % 1000000UL);

    /* 周期的発火のため同じ間隔 */
    it.it_interval = it.it_value;

    if (setitimer(ITIMER_VIRTUAL, &it, NULL) == -1) {
        return -1;
    }
    return 0;
}

/* 仮想タイマを無効化 */
static int disarm_timer(void) {
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    if (setitimer(ITIMER_VIRTUAL, &it, NULL) == -1) {
        return -1;
    }
    return 0;
}

int main(void) {
    struct sigaction sa;

    printf("=== Timer ISR vs POSIX Signal Handler ===\n");
    printf("This program demonstrates the structural correspondence\n");
    printf(
        "between hardware interrupts / ISRs and POSIX signals / handlers.\n\n");
    printf(
        "Hardware interrupt:   IRQ -> CPU saves regs -> IDT -> ISR -> iret\n");
    printf(
        "POSIX signal:         setitimer -> kernel saves ucontext_t -> "
        "sigaction table -> handler -> sigreturn\n\n");
    printf("A busy CPU loop runs.  Every 500 ms of virtual CPU time,\n");
    printf("SIGVTALRM fires and the handler prints the saved PC/SP/FP.\n\n");
    fflush(stdout);

    /*
     * ステップ 1: SA_SIGINFO 付きで SIGVTALRM ハンドラをインストール。
     * SA_SIGINFO は第3引数（ucontext_t*）を受け取るために必要。
     */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigvtalrm_isr;
    if (sigemptyset(&sa.sa_mask) == -1) {
        perror("sigemptyset");
        return EXIT_FAILURE;
    }
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    /*
     * ステップ 2: 仮想タイマを設定。
     * 500,000 マイクロ秒 = 500 ミリ秒。
     */
    if (arm_timer(500000UL) == -1) {
        perror("setitimer");
        return EXIT_FAILURE;
    }

    printf("Timer armed.  Entering busy loop...\n");
    fflush(stdout);

    /*
     * ステップ 3: CPU ビジーループ。
     * ITIMER_VIRTUAL はユーザ CPU 時間のみをカウントするため、
     * このループは仮想時間を蓄積し SIGVTALRM を繰り返し発生させる。
     * ハンドラは非同期に実行され、ちょうどハードウェア ISR のように動作。
     *
     * メインループの出力は最小限に抑え、ハンドラ内のレジスタダンプが
     * デモの焦点となるようにする。
     */
    sig_atomic_t last_count = 0;
    while (interrupt_count < MAX_INTERRUPTS) {
        /* ループが最適化されないようにする微量の処理 */
        volatile unsigned long counter = 0;
        for (unsigned long i = 0; i < 10000000UL; ++i) {
            ++counter;
        }

        /* 割り込みカウントが変化したときだけ表示し、ハンドラの
         * PC/SP/FP ダンプが目立つようにする */
        if (interrupt_count != last_count) {
            last_count = interrupt_count;
            printf("[main loop] interrupts so far = %d\n",
                   (int)interrupt_count);
            fflush(stdout);
        }
    }

    /*
     * ステップ 4: タイマを無効化して終了。
     * これはハードウェア割り込み源をマスクまたは無効化することに相当。
     */
    if (disarm_timer() == -1) {
        perror("setitimer disarm");
        return EXIT_FAILURE;
    }

    printf("\nTimer disarmed after %d interrupts.  Exiting cleanly.\n",
           (int)interrupt_count);
    return EXIT_SUCCESS;
}
