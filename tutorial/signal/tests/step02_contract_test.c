/* 契約テスト Step 2 — sigaction + SA_SIGINFO。
 * step02_sigaction が SA_SIGINFO ハンドラで si_pid を受け取り、
 * SIGUSR1 の配送を `count=1` として出力し exit 0 することを検証する。 */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

int main(void) {
    static const char* const needles[] = {"count=1"};
    return check_step(2, "./tutorial/signal/step02_sigaction",
                      "sigaction SA_SIGINFO captures si_pid from the sender",
                      needles, 1);
}
