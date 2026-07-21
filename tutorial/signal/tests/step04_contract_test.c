/* 契約テスト Step 4 — sigsuspend での原子的待機。
 * step04_sigsuspend がマスク交換と待機を sigsuspend で原子的に行い、
 * シグナルを取りこぼさず `woke=1` を出力して exit 0 することを検証する。
 * マスク設定を誤って sigsuspend から戻らない場合は targets.mk の timeout 5
 * で強制終了し FAIL になる。 */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

int main(void) {
    static const char* const needles[] = {"woke=1"};
    return check_step(
        4, "./tutorial/signal/step04_sigsuspend",
        "sigsuspend swaps the mask and waits without losing the signal",
        needles, 1);
}
