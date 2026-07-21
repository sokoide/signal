/* 契約テスト Step 6 — 同期受信 (sigwait)。
 * step06_sigwait が SIGUSR1 をブロックした上で sigwait で同期的に受け取り、
 * 受け取ったシグナル番号が SIGUSR1 であることを `match=1` で出力して
 * exit 0 することを検証する。ハンドラは使わない。
 * (sigwaitinfo/sigtimedwait は macOS 未サポートのため sigwait を使う。) */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

int main(void) {
    static const char* const needles[] = {"match=1"};
    return check_step(
        6, "./tutorial/signal/step06_sigwait",
        "sigwait receives a blocked signal synchronously, no handler needed",
        needles, 1);
}
