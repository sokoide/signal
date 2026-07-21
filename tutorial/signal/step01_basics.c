#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t seen;

/*
 * TODO(step 1): async-signal-safe な最小ハンドラを実装する。
 *   - 引数 signo は配送されたシグナル番号。未使用なら (void)signo; で潰す。
 *   - ハンドラの役割は「処理本体」ではなく、通常コンテキストへ渡す最小の通知。
 *     ここでは共有フラグ seen に 1 を代入するだけにする。
 *   - printf / malloc / mutex は禁止（async-signal-safe ではない）。
 */
static void on_usr1(int signo) {
    (void)signo;
    /* TODO(step 1): seen = 1; */
}

int main(void) {
    /* 演習が未実装の間のコンパイル警告抑止。実装後も残して構わない。 */
    (void)on_usr1;
    (void)seen;

    /*
     * TODO(step 1): 以下を実装する。
     *   1. on_usr1 を SIGUSR1 のハンドラとして登録（sigaction 推奨、signal
     * でも可）。
     *   2. raise(SIGUSR1) で自分（呼出スレッド）へ送る。
     *   3. printf("flag=%d\n", (int)seen);
     *   4. fflush(stdout); して return 0;
     *
     * 契約テスト(./tutorial/signal/tests/step01_contract_test)は、
     * 出力に "flag=1" を含み exit 0 のときのみ PASS とする。
     * `return 0;` だけでは観察文字列が出ないため FAIL になる。
     */
    puts("TODO step 1: install handler, raise SIGUSR1, print flag=1");
    return 2;
}
