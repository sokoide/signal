/*
 * harness.h — tutorial signal の契約テスト共用ハーネス。
 *
 * 各 step バイナリを実行し、その振る舞いを検証する:
 *   1. exit status が 0 であること
 *   2. stdout/stderr に needles[] のすべての文字列が含まれること
 * 両方を満たした場合のみ PASS とする。step を `return 0;` だけで抜けた
 * 空実装は、観察文字列が出ないため FAIL になる（= 形骸化防止）。
 *
 * targets.mk 側で `timeout 5 ./...contract_test` として実行されるため、
 * 待機に失敗してブロックした step バイナリはここで強制終了し FAIL になる。
 */
#ifndef TUTORIAL_SIGNAL_TEST_HARNESS_H
#define TUTORIAL_SIGNAL_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_step(int step, const char* bin, const char* desc,
                      const char* const* needles, int ncnt) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s 2>&1", bin);

    FILE* p = popen(cmd, "r");
    if (!p) {
        printf("FAIL step %d: could not run %s\n", step, bin);
        return 1;
    }

    char buf[8192];
    size_t got = fread(buf, 1, sizeof(buf) - 1, p);
    buf[got] = '\0';
    int rc = pclose(p);

    int ok = (rc == 0);
    for (int i = 0; i < ncnt && ok; i++) {
        if (strstr(buf, needles[i]) == NULL) {
            ok = 0;
        }
    }

    printf("%s step %d: %s\n", ok ? "PASS" : "FAIL", step, desc);
    return ok ? 0 : 1;
}

#endif /* TUTORIAL_SIGNAL_TEST_HARNESS_H */
