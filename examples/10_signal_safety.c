/*
 * 10_signal_safety.c
 *
 * Topic: Async-signal-safety — what functions may be called from inside a
 * signal handler without risking undefined behavior or deadlock.
 *
 * A signal handler can interrupt the main program at any instruction.  If the
 * interrupted code happened to hold a lock (for example inside malloc() or
 * printf()), calling the same function from the handler would try to acquire
 * the same lock again and deadlock.  The C and POSIX standards therefore
 * define a small set of "async-signal-safe" functions that may be called
 * from a handler.
 *
 * This program demonstrates the safe pattern:
 *   - Use volatile sig_atomic_t to share state between handler and main.
 *   - In the handler, call only async-signal-safe functions such as write()
 *     and _exit().
 *   - The main program watches the flag and performs all non-safe work.
 *
 * Compile with:
 *   cc -std=c11 -Wall -Wextra -O2 -g 10_signal_safety.c -o 10_signal_safety
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Number of SIGALRM deliveries we want to observe. */
#define MAX_SIGNALS 5

/*
 * The only data type that is guaranteed to be safely shared between a signal
 * handler and the rest of the program is "volatile sig_atomic_t".  The
 * "volatile" qualifier prevents the compiler from caching the value in a
 * register, and sig_atomic_t is an integer type that can be read or written
 * atomically with respect to signal delivery.
 */
static volatile sig_atomic_t got_signal = 0;
static volatile sig_atomic_t signal_count = 0;

/*
 * Write a string to stderr using only write(), which is async-signal-safe.
 * printf() is NOT safe inside a handler because the C stdio layer uses locks
 * and buffers that may already be held when the signal arrives.
 *
 * We compute the length with a tiny loop instead of strlen() so that this
 * helper itself calls only async-signal-safe functions.
 */
static void safe_write(const char* msg) {
    size_t len = 0;
    while (msg[len] != '\0') {
        ++len;
    }
    /*
     * write() may be interrupted; in production code you would loop until
     * the whole message is written.  For this demo a single write is fine
     * because the messages are short.
     */
    (void)write(STDERR_FILENO, msg, len);
}

/*
 * A safe signal handler for SIGALRM.
 *
 * It does only three things, all of which are async-signal-safe:
 *   1. Updates a volatile sig_atomic_t counter.
 *   2. Sets a volatile sig_atomic_t flag.
 *   3. Writes a short message with write().
 *   4. Rearms the timer with alarm(), also async-signal-safe.
 */
static void alarm_handler(int sig) {
    (void)sig; /* unused; silence -Wextra */

    ++signal_count;
    got_signal = 1;

    safe_write(
        "[handler] SIGALRM received; only async-signal-safe write() used "
        "here\n");

    /*
     * Rearm the one-shot timer so we receive another SIGALRM in a second.
     * Stop rearming once we have reached the desired number of deliveries.
     */
    if (signal_count < MAX_SIGNALS) {
        alarm(1);
    }
}

/*
 * A safe termination handler for SIGTERM.
 *
 * If the user (or another process) sends SIGTERM, we want to shut down
 * cleanly from the handler's perspective.  exit() is NOT safe because it
 * flushes stdio buffers and runs atexit handlers, which may hold locks.
 * _exit() terminates immediately without any of that work, so it is the
 * async-signal-safe way to terminate.
 */
static void term_handler(int sig) {
    (void)sig;

    safe_write(
        "[handler] SIGTERM received; calling _exit() (async-signal-safe "
        "termination)\n");
    _exit(EXIT_SUCCESS);
}

/*
 * The block below shows what a signal handler must NEVER do.  It is wrapped
 * in #if 0 so it is not compiled, but you can read the explanation.
 *
 * WHY THIS IS DANGEROUS:
 *   - printf() uses stdio locks.  If the signal interrupted the main program
 *     while it was inside printf(), the handler would try to lock the same
 *     mutex and deadlock.
 *   - malloc()/free() use heap locks.  Again, if the interrupted code was in
 *     the middle of allocating memory, the handler would deadlock.
 *   - exit() runs registered atexit handlers and flushes streams, which may
 *     call non-async-signal-safe functions and may hold locks.
 */
#if 0
static void unsafe_handler_example(int sig)
{
    printf("Got signal %d\n", sig); /* UNSAFE: stdio lock */
    malloc(1024);                  /* UNSAFE: heap lock */
    free(NULL);                    /* UNSAFE: heap lock */
    exit(EXIT_SUCCESS);            /* UNSAFE: atexit + stdio flush */
}
#endif

int main(void) {
    struct sigaction sa;

    /* Install the safe SIGALRM handler. */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = alarm_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Install the safe SIGTERM handler. */
    sa.sa_handler = term_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    printf("Main program: waiting for %d SIGALRM signals.\n", MAX_SIGNALS);
    printf(
        "(The signal handler uses only write(), alarm(), and "
        "sig_atomic_t.)\n\n");
    fflush(stdout);

    /* Start the timer.  alarm() is safe in normal code too. */
    alarm(1);

    /*
     * The main loop polls the flag set by the handler.  This is the classic
     * safe pattern: the handler does the absolute minimum, and the main flow
     * reacts to the flag.  All non-safe functions (printf, fflush, ...) run
     * here, not in the handler.
     */
    while (signal_count < MAX_SIGNALS || got_signal) {
        if (got_signal) {
            /*
             * Reset the flag.  Because both handler and main may touch
             * got_signal, it must be sig_atomic_t.  Clearing it here makes
             * the loop wait for the next delivery.
             */
            got_signal = 0;
            printf("[main]    saw got_signal; count = %d\n", (int)signal_count);
            fflush(stdout);
        }

        /*
         * A short sleep lets the CPU idle while we wait for the timer.
         * sleep() is NOT async-signal-safe, but that is fine because it is
         * called from main(), not from a signal handler.
         */
        sleep(1);
    }

    /* Cancel any pending alarm now that we are done. */
    alarm(0);

    printf("\n");
    printf("Demo complete.\n");
    printf("Final signal_count = %d\n", (int)signal_count);
    printf("\n");
    printf("Async-signal-safe examples (ok in handlers):\n");
    printf("  _exit(), write(), read(), close(), fcntl(), kill(),\n");
    printf("  sigaction(), sigprocmask(), signal(), alarm(), abort()\n");
    printf("\n");
    printf("UNSAFE in signal handlers (can deadlock or corrupt state):\n");
    printf("  printf(), malloc(), free(), exit(), fopen(), fclose(),\n");
    printf("  pthread_mutex_lock(), pthread_mutex_unlock(), longjmp()\n");
    printf("\n");
    printf("Rule of thumb: do as little as possible in a handler.\n");
    printf("Set a volatile sig_atomic_t flag and let main() do the rest.\n");

    return EXIT_SUCCESS;
}
