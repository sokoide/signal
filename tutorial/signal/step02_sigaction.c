#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t count;
static void on_usr1(int signo, siginfo_t *info, void *u) {
    (void)signo; (void)u;
    if (info && info->si_pid > 0) count++;
}
int main(void) {
    /* TODO(step 2): configure SA_SIGINFO and inspect siginfo_t without printf in handler. */
    (void)on_usr1; (void)count;
    puts("TODO step 2: configure sigaction (SA_SIGINFO; consider SA_RESTART scope)");
    return 2;
}
