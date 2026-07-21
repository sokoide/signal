/* 契約テスト Step 3 — マスクと pending。
 * step03_mask_pending が SIGUSR1 をブロック中に pending であることを
 * `pending=1` で出力し、ブロック解除後の配送回数を `deliveries=1` で出力して
 * exit 0 することを検証する（標準シグナルのマージをこの1回配送で示す）。 */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

int main(void) {
    static const char* const needles[] = {"pending=1", "deliveries=1"};
    return check_step(
        3, "./tutorial/signal/step03_mask_pending",
        "blocked standard signal is pending, then merges into one delivery",
        needles, 2);
}
