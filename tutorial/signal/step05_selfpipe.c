#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static int pipefd[2];
static void on_usr1(int s) { unsigned char b = (unsigned char)s; (void)write(pipefd[1], &b, 1); }
int main(void) {
    /* TODO(step 5): write one byte from the handler; consume it in the main loop. */
    (void)on_usr1; (void)pipefd;
    puts("TODO step 5: notify an event loop through a pipe (write is async-signal-safe)");
    return 2;
}
