#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static volatile sig_atomic_t woke;
static void on_usr1(int s) { (void)s; woke = 1; }
int main(void) {
    /* TODO(step 4): close the unblock-and-wait race with sigsuspend. */
    (void)on_usr1; (void)woke;
    puts("TODO step 4: atomically swap mask and sleep with sigsuspend");
    return 2;
}
