/*
 * 03_blocking.c
 *
 * Topic: Signal blocking with sigprocmask(), signal sets, and pending signals.
 *
 * Sometimes a program must perform a short sequence of operations without
 * being interrupted by a signal.  POSIX provides sigprocmask() for this.
 * It is the user-space equivalent of an operating system disabling
 * interrupts around a critical section: signals are still delivered by the
 * kernel, but if they are blocked they remain pending until the process
 * unblocks them.
 *
 * What is demonstrated:
 *   - sigset_t operations: sigemptyset(), sigfillset(), sigaddset(),
 *     sigdelset(), and sigismember().
 *   - sigprocmask() with SIG_BLOCK, SIG_UNBLOCK, and SIG_SETMASK.
 *   - Blocking SIGINT, raising it while it is blocked, and checking that it
 *     is pending with sigpending().
 *   - The fact that standard signals are not queued: raising a blocked
 *     signal twice results in only one delivery after it is unblocked.
 *   - A simple critical section protected by blocking signals.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -O2 -g -o 03_blocking 03_blocking.c
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Counter incremented by the SIGINT handler.  It is volatile sig_atomic_t
 * because it is shared between normal code and an asynchronous handler.
 */
static volatile sig_atomic_t sigint_count = 0;

/* Async-signal-safe output helpers. */
static void safe_write_str(const char* s) {
    size_t n = 0;

    while (s[n] != '\0') {
        n++;
    }
    (void)write(STDOUT_FILENO, s, n);
}

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

static void sigint_handler(int sig) {
    (void)sig;

    sigint_count++;
    safe_write_str("[handler] SIGINT delivered, count=");
    safe_write_int((long long)sigint_count);
    safe_write_str("\n");
}

int main(void) {
    sigset_t set;
    sigset_t oldset;
    sigset_t pending;
    struct sigaction act = {0};

    printf("PID: %ld\n", (long)getpid());
    fflush(stdout);

    /*
     * Install a SIGINT handler.  We use sigaction() because it is the
     * modern, well-defined API; signal() would also work for this demo but
     * is discouraged in production code.
     */
    sigemptyset(&act.sa_mask);
    act.sa_handler = sigint_handler;
    act.sa_flags = 0;

    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction(SIGINT)");
        return EXIT_FAILURE;
    }

    /*
     * Build a signal set containing only SIGINT.
     *
     * A sigset_t is an opaque bitmask.  Always initialize it with
     * sigemptyset() or sigfillset() before adding/removing bits.  It is
     * undefined behavior to use an uninitialized sigset_t.
     */
    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    /*
     * sigprocmask(SIG_BLOCK, &set, &oldset) adds the signals in `set` to
     * the process signal mask.  The previous mask is saved in `oldset` so
     * we can restore it later.
     *
     * Analogy to low-level OS code:
     *   sigprocmask(SIG_BLOCK, ...)  ~=  cli   (disable interrupts)
     *   sigprocmask(SIG_UNBLOCK, ...) ~=  sti   (enable interrupts)
     *
     * The kernel still delivers the signal, but if it is masked it is held
     * in the pending signal set instead of invoking the handler immediately.
     */
    printf("\nBlocking SIGINT...\n");
    fflush(stdout);

    if (sigprocmask(SIG_BLOCK, &set, &oldset) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        return EXIT_FAILURE;
    }

    /*
     * Raise SIGINT twice while it is blocked.
     *
     * Standard signals (like SIGINT) are not queued.  Raising a blocked
     * signal twice leaves it pending exactly once.  When we later unblock
     * it, the handler will run only one time.
     */
    printf("Raising SIGINT twice while it is blocked...\n");
    fflush(stdout);
    raise(SIGINT);
    raise(SIGINT);

    /*
     * sigpending() retrieves the set of signals that are currently pending
     * for the calling process.  A signal is pending if it has been
     * delivered to the process but is currently blocked by the signal mask.
     */
    if (sigpending(&pending) == -1) {
        perror("sigpending");
        return EXIT_FAILURE;
    }

    if (sigismember(&pending, SIGINT)) {
        printf("SIGINT is pending (as expected).\n");
    } else {
        printf("SIGINT is NOT pending (unexpected!).\n");
    }
    fflush(stdout);

    /*
     * Unblock SIGINT.  The pending signal is now delivered, and the
     * handler runs exactly once because standard signals are not queued.
     */
    printf("Unblocking SIGINT...\n");
    fflush(stdout);

    if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1) {
        perror("sigprocmask(SIG_UNBLOCK)");
        return EXIT_FAILURE;
    }

    printf("Main: sigint_count=%d (should be 1)\n", (int)sigint_count);
    fflush(stdout);

    /*
     * Critical section pattern.
     *
     * Block the signals that could corrupt our work, perform the sensitive
     * operation, then restore the previous mask.  This is exactly what an
     * OS does with cli/sti around a critical section, except in user space
     * the mask is per-process.
     */
    {
        sigset_t crit_set;
        int important_value = 0;

        printf("\n--- Critical section demo ---\n");
        fflush(stdout);

        sigemptyset(&crit_set);
        sigaddset(&crit_set, SIGINT);
        sigaddset(&crit_set, SIGTERM);

        printf("Blocking SIGINT and SIGTERM...\n");
        fflush(stdout);
        if (sigprocmask(SIG_BLOCK, &crit_set, &oldset) == -1) {
            perror("sigprocmask(SIG_BLOCK) critical");
            return EXIT_FAILURE;
        }

        /*
         * This is the critical section.  In a real program this might be
         * updating a linked list, writing two related database records, or
         * modifying shared state that must stay consistent.
         */
        important_value += 10;
        important_value *= 2;

        printf("Critical section done, important_value=%d\n", important_value);
        fflush(stdout);

        /*
         * Restore the previous signal mask.  SIG_SETMASK replaces the
         * entire mask with the saved mask.  This is safer than SIG_UNBLOCK
         * when the mask at entry is unknown.
         */
        if (sigprocmask(SIG_SETMASK, &oldset, NULL) == -1) {
            perror("sigprocmask(SIG_SETMASK)");
            return EXIT_FAILURE;
        }
        printf("Restored previous signal mask.\n");
        fflush(stdout);
    }

    /*
     * Demonstrate the remaining sigset_t operations.
     *
     * sigfillset() puts every signal in the set.
     * sigdelset() removes a signal.
     * sigismember() tests membership.
     */
    {
        sigset_t full;

        sigfillset(&full);
        printf("\nAfter sigfillset(): SIGINT member=%d\n",
               sigismember(&full, SIGINT));

        sigdelset(&full, SIGINT);
        printf("After sigdelset(SIGINT): SIGINT member=%d, SIGTERM member=%d\n",
               sigismember(&full, SIGINT), sigismember(&full, SIGTERM));
        fflush(stdout);
    }

    return EXIT_SUCCESS;
}
