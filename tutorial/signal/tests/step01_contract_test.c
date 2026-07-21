/* 契約テスト Step 1 — 最小ハンドラ。
 * step01_basics が SIGUSR1 を受けて volatile sig_atomic_t のフラグを立て、
 * それを `flag=1` として出力し exit 0 することを検証する。 */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

int main(void) {
    static const char* const needles[] = {"flag=1"};
    return check_step(1, "./tutorial/signal/step01_basics",
                      "minimal handler sets a volatile sig_atomic_t flag",
                      needles, 1);
}
