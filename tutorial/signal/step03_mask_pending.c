#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static volatile sig_atomic_t seen;
static void on_usr1(int s) { (void)s; seen = 1; }
int main(void) {
    /* TODO(step 3): block first, then inspect pending before unblocking. */
    (void)on_usr1; (void)seen;
    puts("TODO step 3: block SIGUSR1, inspect sigpending, then unblock");
    return 2;
}
