#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t seen;
/* TODO(step 1): replace this comment with your minimal async-signal-safe handler. */
static void on_usr1(int signo) { (void)signo; seen = 1; }

int main(void) {
    (void)on_usr1; (void)seen;
    puts("TODO step 1: install handler, send SIGUSR1, and observe flag");
    return 2;
}
