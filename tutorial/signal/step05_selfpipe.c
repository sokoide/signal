#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static int pipefd[2];
static void on_usr1(int s) {
    unsigned char b = (unsigned char)s;
    (void)write(pipefd[1], &b, 1);
}
int main(void) {
    /* 演習が未実装の間のコンパイル警告抑止。実装後も残して構わない。 */
    (void)on_usr1;
    (void)pipefd;

    /*
     * TODO(step 5): ハンドラは pipe へ通知し、main が消費する。
     *   1. pipe(pipefd) でパイプを作る（0=読, 1=書）。
     *   2. on_usr1 を SIGUSR1 ハンドラとして登録。ハンドラは pipefd[1] へ
     *      シグナル番号1バイトを write 済み（実運用では戻り値と EAGAIN
     * を確認するが、 この演習では短い1バイト通知のため省略可）。
     *   3. raise(SIGUSR1)。
     *   4. main で unsigned char b; read(pipefd[0], &b, 1)。
     *   5. printf("match=%d\n", b == (unsigned char)SIGUSR1 ? 1 : 0);
     *   6. close して fflush(stdout); return 0;
     *
     * write は async-signal-safe だが即時完全完了は保証されない。この演習は
     * 短い通知の best-effort 扱い。契約テストは "match=1" を含み exit 0
     * のときのみ PASS。
     */
    puts("TODO step 5: pipe, raise, read 1 byte, print match=1");
    return 2;
}
