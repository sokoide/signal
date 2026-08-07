#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static volatile sig_atomic_t woke;
static void on_usr1(int s) {
    (void)s;
    woke = 1;
}
int main(void) {
    /* 演習が未実装の間のコンパイル警告抑止。実装後も残して構わない。 */
    (void)on_usr1;
    (void)woke;

    /*
     * TODO(step 4): 「フラグ確認 → pause()」の競合を sigsuspend で防ぐ。
     *
     * ここでは自己完結させるため、ブロック中に自分へ送って pending にしておき、
     * sigsuspend で SIGUSR1 を受けるマスクへ原子的に切り替えて待つ:
     *   1. on_usr1 を SIGUSR1 ハンドラとして登録（woke=1 をセット）。
     *   2. SIGUSR1 を sigprocmask(SIG_BLOCK, &blk, &oldmask)
     * でブロック（oldmask 保存）。
     *   3. raise(SIGUSR1) — ブロック中なので pending になる（取りこぼし無し）。
     *   4. 待機用マスク waitmask を作り、SIGUSR1 を「受ける」状態にする。
     *      oldmask をコピーして sigdelset(&waitmask, SIGUSR1)
     * する（他はそのままブロック）。
     *   5. sigsuspend(&waitmask) で原子的にマスク交換して待機。
     *      戻り値は通常 -1/EINTR。重要なのは戻った後に woke を再確認すること。
     *   6. sigprocmask(SIG_SETMASK, &oldmask, NULL) で元のマスクを復元。
     *   7. printf("woke=%d\n", (int)woke);
     *   8. fflush(stdout); して return 0;
     *
     * マスク設定を誤ると sigsuspend から戻らず timeout 5
     * で強制終了（FAIL）する。 契約テストは "woke=1" を含み exit 0 のときのみ
     * PASS。
     */
    puts("TODO step 4: block+raise, sigsuspend on an empty mask, print woke=1");
    return 2;
}
