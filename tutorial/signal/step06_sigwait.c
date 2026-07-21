#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    /*
     * TODO(step 6): ハンドラを使わず、シグナルをブロックして同期的に取り出す。
     *   1. SIGUSR1 を含むセットを sigemptyset + sigaddset で作る。
     *   2. sigprocmask(SIG_BLOCK, &set, NULL) でブロック。
     *   3. raise(SIGUSR1) — ブロック中なので pending。
     *   4. int sig = 0; if (sigwait(&set, &sig) != 0) return 1;
     *      sigwait は成功すると 0 を返し、sig
     * に受け取ったシグナル番号を入れる。
     *   5. printf("match=%d\n", sig == SIGUSR1 ? 1 : 0);
     *   6. fflush(stdout); して return 0;
     *
     * sigwait は siginfo_t を返さない（送信元や値は取れない）点で sigwaitinfo
     * より情報が少ないが、Linux/macOS 両方で使える。sigwaitinfo / sigtimedwait
     * と リアルタイムシグナル（sigqueue 含む）は macOS で未サポートのため、
     * それらは completed/signal/06_realtime.c（Linux 専用）で別途扱う。
     * ハンドラは登録しない。契約テストは "match=1" を含み exit 0 のときのみ
     * PASS。
     */
    puts("TODO step 6: block SIGUSR1, sigwait, print match=1");
    return 2;
}
