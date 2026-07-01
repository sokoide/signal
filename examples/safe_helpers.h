/*
 * safe_helpers.h
 *
 * シグナルハンドラ向けの async-signal-safe な出力ヘルパー。
 *
 * これらの関数は write() のみを呼び出す。write() は async-signal-safe である。
 * printf()/fprintf() は安全ではない。なぜなら、バッファリングされたストリームを使い、
 * ロックを保持したり malloc() を呼び出したりする可能性があるため。
 * シグナルハンドラがそれらのロックを保持中に割り込むと、デッドロックやヒープ破壊を
 * 引き起こす。
 */

#ifndef SAFE_HELPERS_H
#define SAFE_HELPERS_H

#include <unistd.h>

/* ヌル終端文字列を標準出力に書き込む。 */
static inline void safe_write_str(const char* s) {
    size_t n = 0;

    while (s[n] != '\0') {
        n++;
    }

    ssize_t ret = write(STDOUT_FILENO, s, n);
    (void)ret;
}

/* signed long long 整数を10進数で標準出力に書き込む。 */
static inline void safe_write_int(long long v) {
    char buf[32];
    int i = (int)sizeof(buf) - 1;
    int negative = (v < 0);
    unsigned long long u;
    if (negative) {
        u = 0ULL - (unsigned long long)v; /* LLONG_MIN のオーバーフロー回避 */
    } else {
        u = (unsigned long long)v;
    }

    buf[i] = '\0';
    i--;

    if (u == 0) {
        buf[i] = '0';
        i--;
    } else {
        while (u > 0) {
            buf[i] = (char)('0' + (u % 10));
            u /= 10;
            i--;
        }
    }

    if (negative) {
        buf[i] = '-';
        i--;
    }

    ssize_t ret = write(STDOUT_FILENO, &buf[i + 1], sizeof(buf) - 2 - (size_t)i);
    (void)ret;
}

/* unsigned long long 整数を16進数（小文字）で標準出力に書き込む。 */
static inline void safe_write_hex(unsigned long long v) {
    const char hex[] = "0123456789abcdef";
    char buf[24];
    int i = (int)sizeof(buf) - 1;

    buf[i] = '\0';
    i--;

    if (v == 0) {
        buf[i] = '0';
        i--;
    } else {
        while (v > 0) {
            buf[i] = hex[v & 0xf];
            v >>= 4;
            i--;
        }
    }

    ssize_t ret = write(STDOUT_FILENO, &buf[i + 1], sizeof(buf) - 2 - (size_t)i);
    (void)ret;
}

#endif /* SAFE_HELPERS_H */
