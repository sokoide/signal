/*
 * 01_signal_basics.c
 *
 * Topic: Basic signal handling with signal() and raise().
 *
 * This program is intentionally small.  It shows the oldest, simplest
 * portable signal API: signal(2) and raise(3).  In real code you should
 * prefer sigaction(2) (see 02_sigaction.c), but signal() is still useful
 * for a first exposure because the ideas map one-to-one to the modern API.
 *
 * What is demonstrated:
 *   - Registering a handler with signal().
 *   - A handler receives the signal number as its int argument.
 *   - raise() sends a signal to the calling process itself.
 *   - The default action of SIGINT (interrupt from terminal) and SIGTERM
 *     (polite termination request) is to terminate the process.
 *   - SIG_IGN ignores a signal; SIG_DFL restores the default action.
 *   - SIGKILL cannot be caught, blocked, or ignored.
 *   - Why you must use write() inside a signal handler instead of printf().
 *   - A flag shared between a handler and normal code must be
 *     volatile sig_atomic_t.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -O2 -g -o 01_signal_basics 01_signal_basics.c
 */

/* Ask for POSIX.1-2008 interfaces such as the modern sigaction type. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * A flag that is written by the signal handler and read by main().
 *
 * Why volatile?  Because the compiler cannot see the call site of a signal
 * handler; without volatile it might cache the value in a register and never
 * notice that the handler changed it.
 *
 * Why sig_atomic_t?  This is the only C type that is guaranteed to be read
 * and written atomically with respect to asynchronous signal delivery on
 * every POSIX implementation.  For a simple flag it is exactly what we need.
 */
static volatile sig_atomic_t got_sigint = 0;

/*
 * Async-signal-safe string write helper.
 *
 * Inside a signal handler you may only call functions that are marked
 * "async-signal-safe" by POSIX.  write() is safe; printf()/fprintf() are
 * NOT safe because they use buffered streams that may hold locks or call
 * malloc(), and the signal may have interrupted the program while it was
 * already holding one of those locks.  Calling printf() from a handler is
 * a classic way to deadlock or corrupt the heap.
 */
static void safe_write_str(const char* s) {
    size_t n = 0;

    while (s[n] != '\0') {
        n++;
    }

    /* write() returns the number of bytes written; we ignore errors here
     * because there is very little useful recovery possible from inside a
     * handler. */
    (void)write(STDOUT_FILENO, s, n);
}

/*
 * Async-signal-safe integer printer.
 *
 * We build the decimal representation in a local buffer using only basic
 * arithmetic, then call write().  This avoids snprintf(), which is not in
 * the async-signal-safe list.
 */
static void safe_write_int(long long v) {
    char buf[32];
    int i = (int)sizeof(buf) - 1;
    int negative = (v < 0);
    unsigned long long u =
        negative ? (unsigned long long)(-v) : (unsigned long long)v;

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

    (void)write(STDOUT_FILENO, &buf[i + 1], sizeof(buf) - 2 - (size_t)i);
}

/*
 * Our SIGINT handler.
 *
 * The int argument is the signal number that was delivered.  In this file
 * we only register this for SIGINT, but a handler can be shared by many
 * signals and use the argument to tell them apart.
 */
static void sigint_handler(int sig) {
    /* Mark that we have seen the signal.  This assignment is safe because
     * got_sigint is volatile sig_atomic_t. */
    got_sigint = 1;

    safe_write_str("[handler] Caught signal ");
    safe_write_int((long long)sig);
    safe_write_str(
        " (SIGINT).  Why write()? printf() is not async-signal-safe.\n");
}

int main(void) {
    /* Print the PID so a curious reader can send signals from another
     * terminal with `kill -INT <pid>`. */
    printf("PID: %ld\n", (long)getpid());
    fflush(stdout);

    /*
     * SIGKILL and SIGSTOP are special: they can never be caught, blocked,
     * or ignored.  The kernel enforces this so that there is always a way
     * to stop a runaway process.  signal() returns SIG_ERR and sets errno
     * to EINVAL when asked to handle SIGKILL.
     */
    printf("Trying to catch SIGKILL (this must fail)...\n");
    if (signal(SIGKILL, sigint_handler) == SIG_ERR) {
        printf("  -> signal(SIGKILL) failed as expected: %s\n",
               strerror(errno));
    }
    fflush(stdout);

    /*
     * Register our handler for SIGINT.
     *
     * signal() is the historical API.  Its behavior varies subtly between
     * System V (handler is reset to SIG_DFL after the first delivery) and
     * BSD (handler remains installed).  Modern glibc follows BSD semantics
     * by default, but you cannot rely on that across all Unices.  That is
     * the main reason sigaction() is recommended for real programs.
     */
    printf("Registering SIGINT handler with signal()...\n");
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal(SIGINT)");
        return EXIT_FAILURE;
    }
    fflush(stdout);

    /* raise() sends the given signal to the calling thread/process.
     * It is exactly equivalent to kill(getpid(), SIGINT). */
    printf("Raising SIGINT to ourselves...\n");
    fflush(stdout);
    raise(SIGINT);

    /* When the handler returns, execution resumes here. */
    if (got_sigint) {
        printf("Main: handler ran and set got_sigint.\n");
    } else {
        printf("Main: got_sigint is still 0 (unexpected!).\n");
    }
    fflush(stdout);

    /*
     * SIG_IGN installs the "ignore" disposition.  SIGINT will be delivered,
     * but its default action is suppressed.  The program continues as if
     * nothing happened.
     */
    printf("Setting SIGINT to SIG_IGN and raising it again...\n");
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("signal(SIGINT, SIG_IGN)");
        return EXIT_FAILURE;
    }
    fflush(stdout);
    raise(SIGINT);
    printf("Main: SIGINT was ignored.\n");
    fflush(stdout);

    /*
     * SIG_DFL restores the default action.  For SIGINT the default is to
     * terminate the process.  After this line, raising SIGINT ends the
     * program; the final printf() will not execute.
     */
    printf("Restoring SIGINT to SIG_DFL and raising it one last time...\n");
    fflush(stdout);
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
        perror("signal(SIGINT, SIG_DFL)");
        return EXIT_FAILURE;
    }
    raise(SIGINT);

    /* This line is unreachable because the default action of SIGINT is to
     * terminate the process. */
    printf("This line should never be printed.\n");
    return EXIT_SUCCESS;
}
