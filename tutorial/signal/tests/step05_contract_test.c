/* 契約テスト Step 5 — self-pipe。
 * step05_selfpipe のハンドラが pipe へ1バイトを write し、main がそれを
 * read して SIGUSR1 と一致することを `match=1` で出力し exit 0 することを
 * 検証する。write は async-signal-safe だが即時完全完了は保証されないため、
 * この演習では1バイトの短い通知だけを扱う。 */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

int main(void) {
    static const char* const needles[] = {"match=1"};
    return check_step(5, "./tutorial/signal/step05_selfpipe",
                      "handler notifies the main loop through a pipe (write is "
                      "async-signal-safe)",
                      needles, 1);
}
