/*
 * 07_selfpipe.c — Self-Pipe Trick
 *
 * テーマ: 非同期 POSIX シグナルをファイルディスクリプタイベントに変換し、
 * 単一の select()/poll()/epoll() イベントループ内で処理できるようにする。
 *
 * 問題
 * ----
 * イベント駆動型プログラムは、select()、poll()、epoll()、kqueue() などを
 * 使ってファイルディスクリプタ上のアクティビティを待つ。これらの API は
 * FD しか監視できない。シグナルはファイルディスクリプタではないため、
 * メインループが select() でブロック中にシグナルが到着しても、それだけで
 * select() が戻ることはない。そのため、「シグナルが到着した」と
 * 「FD が読み取り可能になった」の間をつなぐ橋渡しが必要。
 *
 * 解決策（self-pipe）
 * --------------------
 * パイプを作成する。シグナルハンドラ内で、async-signal-safe な最小限の
 * アクションを実行する: パイプに1バイト書き込む。書き込むバイトは
 * シグナル番号なので、メインループはどのシグナルが到着したかを
 * 識別できる。メインループでは、パイプの読み取り端を select() に渡す
 * FD セットに含める。select() がパイプの読み取り可能を通知したら、
 * パイプを drain し、通常のプログラムコンテキストでシグナルを処理する —
 * そこで printf()、malloc()、任意のロジックが安全になる。
 *
 * なぜ安全か
 * ----------------
 * 1. write(2) は async-signal-safe なので、シグナルハンドラから呼んでも
 *    デッドロックや stdio/ヒープロックの破損リスクがない。
 * 2. 書き込み端は非ブロッキング（O_NONBLOCK）に設定する。多くのシグナルが
 *    バーストで到着しても、write() はハンドラ内で永久にブロックせず
 *    EAGAIN を返す。
 * 3. 読み取り端はハンドラではなくメインループが読み取るため、
 *    複雑な処理は通常コンテキストで行われる。
 *
 * Linux の代替
 * -----------------
 * Linux は signalfd(2) を提供する。これはシグナル到着時に読み取り可能に
 * なるファイルディスクリプタを作成する。よりクリーンだが Linux 固有。
 * Self-pipe trick は完全に POSIX 準拠で、macOS、*BSD などでも動作する。
 *
 * ビルド:
 *   cc -std=c11 -Wall -Wextra -O2 -g examples/07_selfpipe.c -o 07_selfpipe
 *
 * 実行:
 *   ./07_selfpipe
 *
 * テスト:
 *   Ctrl-C (SIGINT) を押すか、別の端末から SIGTERM を送る:
 *     kill -INT  <pid>
 *     kill -TERM <pid>
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/* パイプのファイルディスクリプタ。インデックス 0 が読み取り端、
 * インデックス 1 が書き込み端。シグナルハンドラがパイプに書き込めるように
 * グローバル変数にする。 */
static int selfpipe[2] = {-1, -1};

/* セルフパイプバッファが満杯でドロップされたシグナルの数。
 * ハンドラが書き込むので volatile sig_atomic_t。 */
static volatile sig_atomic_t selfpipe_overflow_count = 0;

/*
 * シグナルハンドラ。
 *
 * シグナルコンテキストで実行されるため、async-signal-safe な関数のみ
 * 使用する。ここでは write() だけを呼ぶ。シグナル番号を1バイト書き込む。
 * メインループがこのバイトを読み取り、通常コンテキストでシグナルを処理する。
 */
static void selfpipe_handler(int sig) {
    /* errno を保存。write() が変更する可能性があり、シグナル到着時に
     * 中断されたコードが errno をチェックしている可能性があるため。 */
    int saved_errno = errno;

    /* シグナル番号は1バイトに収まる（標準シグナルは 1-31、
     * Linux のリアルタイムシグナルでも最大 64）。 */
    unsigned char c = (unsigned char)sig;

    /* write() は async-signal-safe。書き込み端を非ブロッキングにすることで、
     * ここでブロックするのを防ぐ。メインループの drain より速くシグナルが
     * 到着すると、パイプバッファ（通常 16-64 KiB）が満杯になり、
     * write() は EAGAIN を返す。これらのドロップをカウントして
     * 制限事項を報告する。
     *
     * これは self-pipe パターンの本質的な制限。
     * Linux 固有の signalfd(2) はこの問題を回避する。 */
    /* EINTR（別の非ブロックシグナルが write を中断）の場合は再試行。
     * 本当のドロップは EAGAIN（パイプバッファ満杯）のみ。 */
    ssize_t ret;
    do {
        ret = write(selfpipe[1], &c, 1);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1 && errno == EAGAIN) {
        selfpipe_overflow_count++;
    }

    errno = saved_errno;
}

/*
 * ファイルディスクリプタを非ブロッキングモードに設定する。
 * これはセルフパイプの書き込み端にとって重要: バーストでシグナルが到着し
 * パイプバッファが満杯の場合、write() はシグナルハンドラ内でブロックせず
 * EAGAIN で -1 を返す。
 */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

/*
 * セルフパイプ経由で転送するシグナルを登録する。
 */
static int register_selfpipe_signal(int sig) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = selfpipe_handler;

    /* すべてのセルフパイプハンドラ実行中に、全セルフパイプシグナルを
     * ブロックする。これにより selfpipe[1] と selfpipe_overflow_count への
     * アクセスが直列化される: SIGINT ハンドラが SIGTERM に割り込まれる
     * ことはない（逆も同様）。従って selfpipe_overflow_count への
     * 非不可分な ++ も実用上安全。シグナル自体をブロックするのは
     * デフォルト動作だが、明示するのは教育的。 */
    if (sigemptyset(&sa.sa_mask) == -1) {
        return -1;
    }
    if (sigaddset(&sa.sa_mask, SIGINT) == -1) {
        return -1;
    }
    if (sigaddset(&sa.sa_mask, SIGTERM) == -1) {
        return -1;
    }

    /* SA_RESTART は使わない。メインスレッドが select() でブロック中に
     * シグナルが到着した場合、select() が EINTR で戻って FD セットを
     * 再評価できるようにしたい。実際には、セルフパイプが読み取り可能に
     * なることで select() が戻るので EINTR は起こらないが、
     * 再開するのは逆効果。 */
    sa.sa_flags = 0;

    if (sigaction(sig, &sa, NULL) == -1) {
        return -1;
    }
    return 0;
}

int main(void) {
    /*
     * ステップ 1: セルフパイプを作成。
     * selfpipe[0] = 読み取り端（select で監視）
     * selfpipe[1] = 書き込み端（シグナルハンドラが書き込む）
     */
    if (pipe(selfpipe) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    /*
     * ステップ 2: パイプの両端を非ブロッキングにする。
     * 書き込み端: シグナルストームがハンドラを永久にブロックしてはならない。
     * 読み取り端: 以下の drain ループはパイプが空になるまで read() を
     * 繰り返し呼ぶ。ブロッキング読み取り端だと、空のパイプでメインループが
     * ブロックする。非ブロッキングなら EAGAIN まで drain できる。
     */
    if (set_nonblocking(selfpipe[1]) == -1) {
        perror("fcntl O_NONBLOCK (write end)");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }
    if (set_nonblocking(selfpipe[0]) == -1) {
        perror("fcntl O_NONBLOCK (read end)");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }

    /*
     * ステップ 3: セルフパイプに書き込むシグナルハンドラをインストール。
     * SIGINT（Ctrl-C）と SIGTERM（kill -TERM）を実演。
     */
    if (register_selfpipe_signal(SIGINT) == -1) {
        perror("sigaction SIGINT");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }
    if (register_selfpipe_signal(SIGTERM) == -1) {
        perror("sigaction SIGTERM");
        close(selfpipe[0]);
        close(selfpipe[1]);
        return EXIT_FAILURE;
    }

    printf("Self-pipe demo running.  PID=%d\n", (int)getpid());
    printf("Press Enter, or send SIGINT (Ctrl-C) / SIGTERM.\n");
    printf("Send signals from another terminal with:\n");
    printf("  kill -INT %d\n", (int)getpid());
    printf("  kill -TERM %d\n", (int)getpid());
    fflush(stdout);

    /*
     * ステップ 4: select() を使ったメインイベントループ。
     * 2つの FD を監視:
     *   - selfpipe[0] : FD イベントに変換されたシグナル
     *   - STDIN_FILENO: 通常の端末入力
     */
    for (;;) {
        fd_set rfds;
        int maxfd;
        int ret;

        FD_ZERO(&rfds);
        FD_SET(selfpipe[0], &rfds);
        FD_SET(STDIN_FILENO, &rfds);

        maxfd = selfpipe[0] > STDIN_FILENO ? selfpipe[0] : STDIN_FILENO;

        /* select() は監視対象の FD のいずれかが読み取り可能になるまでブロック */
        ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ret == -1) {
            /* シグナルが select() がブロックする直前に到着し、カーネルが
             * 再開した場合に EINTR が発生しうる。セルフパイプがあれば
             * 次のイテレーションでパイプが読み取り可能になるので、
             * 単にループする。 */
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        /*
         * ケース A: セルフパイプが読み取り可能 → シグナル到着。
         * バースト中に蓄積された全バイトを処理するため、
         * ループでパイプを drain する。
         */
        if (FD_ISSET(selfpipe[0], &rfds)) {
            unsigned char buf[16];
            ssize_t n;

            /*
             * パイプが空になるまで読み取りループ。
             * 多くのシグナルが非常に短時間に到着するエッジケースを処理。
             * EAGAIN はパイプが drain されたことを意味する; これは
             * 期待され安全な状態。
             */
            while ((n = read(selfpipe[0], buf, sizeof(buf))) > 0) {
                for (ssize_t i = 0; i < n; ++i) {
                    int sig = buf[i];
                    printf("[event loop] Signal converted to FD event: sig=%d",
                           sig);
                    if (sig == SIGINT) {
                        printf(" (SIGINT)\n");
                    } else if (sig == SIGTERM) {
                        printf(" (SIGTERM)\n");
                    } else {
                        printf("\n");
                    }
                    fflush(stdout);

                    /* このデモでは、どちらのシグナルでもクリーンシャットダウン */
                    if (sig == SIGINT || sig == SIGTERM) {
                        printf("[event loop] Shutting down cleanly.\n");
                        if (selfpipe_overflow_count > 0) {
                            printf(
                                "[event loop] Note: %d signal(s) were "
                                "dropped due to a full self-pipe buffer.\n",
                                (int)selfpipe_overflow_count);
                        }
                        close(selfpipe[0]);
                        close(selfpipe[1]);
                        return EXIT_SUCCESS;
                    }
                }
            }

            if (n == -1 && errno != EAGAIN && errno != EINTR) {
                perror("read selfpipe");
                break;
            }
        }

        /*
         * ケース B: stdin が読み取り可能 → 通常の入力。
         */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char line[256];
            if (fgets(line, sizeof(line), stdin) == NULL) {
                printf("[event loop] EOF on stdin, exiting.\n");
                break;
            }
            printf("[event loop] Read line: %s", line);
            fflush(stdout);
        }
    }

    /* 後片付け */
    close(selfpipe[0]);
    close(selfpipe[1]);
    return EXIT_SUCCESS;
}
