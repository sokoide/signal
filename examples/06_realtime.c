/*
 * 06_realtime.c — POSIX real-time signals and sigqueue()
 *
 * Unlike standard signals (1–31), real-time signals have two important
 * properties:
 *
 * 1. Queuing
 *    Standard signals are "merged": if the same signal arrives several
 *    times while blocked, it is still pending only once.  Real-time signals
 *    are queued individually and every delivery is made.
 *
 * 2. Data carrying
 *    sigqueue(2) lets you attach an integer or pointer to a signal.  The
 *    receiver reads it from siginfo_t.si_value.
 *
 * Real-time signal range:
 *    Linux: SIGRTMIN .. SIGRTMAX (usually 34 .. 64)
 *    Lower-numbered signals are delivered before higher-numbered ones.
 *
 * Safe asynchronous-handler design used in this sample:
 *    - SIGUSR1 has a minimal handler that only updates a flag.
 *      The rule is: never touch shared state other than volatile
 *      sig_atomic_t from an asynchronous handler.
 *    - Real-time signals have no handler installed (SIG_DFL).  Instead they
 *      are left blocked and collected synchronously with sigtimedwait(2);
 *      siginfo_t.si_value data is then gathered safely in the main thread.
 *      This is the pattern recommended for production code (see also the
 *      README "Advanced Topics" section on sigwaitinfo and the self-pipe
 *      trick).
 *
 * Note:
 *    macOS (Darwin) does not support POSIX real-time signals or sigqueue(),
 *    so if SIGRTMIN is not defined at compile time the program prints a
 *    message and exits.
 *
 * Build:
 *    cc -std=c11 -Wall -Wextra -O2 -g 06_realtime.c -o 06_realtime -lrt
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * On platforms without real-time signals (e.g. macOS), branch at compile
 * time and print an informational message.
 */
#ifndef SIGRTMIN
int main(void) {
    printf(
        "POSIX real-time signals (SIGRTMIN/SIGRTMAX) are not available on "
        "this platform.\n"
        "Try running this example on Linux.\n");
    return 0;
}
#else

/*
 * Events collected by the main thread from real-time signals.
 * Because the asynchronous handler never touches this data, volatile is
 * unnecessary and there is no race.
 */
#define MAX_EVENTS 64
static struct {
    int sig;
    int data;
} g_events[MAX_EVENTS];
static int g_event_count = 0;

/*
 * Delivery count for the standard signal SIGUSR1.
 * This is the only variable updated by the asynchronous handler, so it is
 * volatile sig_atomic_t.  Sending it five times should merge into one
 * delivery.
 */
static volatile sig_atomic_t g_usr1_count = 0;

/*
 * SIGUSR1 handler.
 *
 * Safe asynchronous signal-handler rules:
 *   - Only read or write volatile sig_atomic_t variables.
 *   - Do not touch stdio (printf, etc.) or complex shared data structures.
 * If you need to process "data" attached to multiple signals, do not do it
 * inside the handler.  Instead collect real-time signals synchronously as
 * shown in this sample (sigwaitinfo/sigtimedwait) or use the self-pipe trick
 * to hand the work to the main loop (see 07_selfpipe.c).
 *
 * Why g_usr1_count++ is acceptable HERE (and only here):
 * Normally a read-modify-write like "++" on a sig_atomic_t is NOT atomic
 * with respect to an asynchronous signal (see the detailed comment in
 * 02_sigaction.c).  However, in *this* sample SIGUSR1 is sent five times
 * while blocked, so it is merged into a single pending bit by the kernel.
 * When the signal is unblocked, the handler runs exactly once — there is
 * never a second concurrent invocation.  SA_NODEFER is not used, so the
 * handler cannot re-enter itself either.  The single ++ is therefore safe
 * in practice.  Do NOT copy this into a handler that can be called more
 * than once or concurrently.
 */
static void usr1_handler(int sig) {
    (void)sig;
    g_usr1_count++;
}

static void print_events(void) {
    int n = g_event_count;
    printf("Collected %d real-time signal event(s) via sigtimedwait:\n", n);
    for (int i = 0; i < n; i++) {
        printf("  event #%02d: signal=%d (SIGRTMIN%+d), data=%d\n", i + 1,
               g_events[i].sig, g_events[i].sig - SIGRTMIN, g_events[i].data);
    }
}

int main(void) {
    printf("=== POSIX real-time signal demo ===\n");
    printf("SIGRTMIN = %d, SIGRTMAX = %d\n", SIGRTMIN, SIGRTMAX);

    if (SIGRTMIN > SIGRTMAX) {
        printf("No real-time signals available.\n");
        return 0;
    }

    /*
     * ============================================================
     * Step 1: register the minimal SIGUSR1 handler
     * ============================================================
     *
     * SIGUSR1 gets a safe handler that only updates g_usr1_count.
     * SA_SIGINFO is not needed here.
     *
     * No handler is registered for real-time signals; they will be left
     * blocked and collected synchronously with sigtimedwait() later.
     */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = usr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * Step 2: block the signals of interest
     * ============================================================
     *
     * Block SIGUSR1, SIGRTMIN, and SIGRTMIN+1.  This is the user-space
     * equivalent of "disabling interrupts (cli)".  Signals sent while
     * blocked remain pending and are processed later.
     */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigaddset(&block_set, SIGRTMIN);
    sigaddset(&block_set, SIGRTMIN + 1);

    if (sigprocmask(SIG_BLOCK, &block_set, &old_set) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * Step 3: send the standard signal five times
     * ============================================================
     *
     * SIGUSR1 is a standard signal, so even if it arrives five times while
     * blocked there is only one pending bit.  Later the handler will run
     * only once.
     */
    printf("\nSending SIGUSR1 5 times (standard signal, should merge)...\n");
    for (int i = 0; i < 5; i++) {
        if (kill(getpid(), SIGUSR1) == -1) {
            perror("kill(SIGUSR1)");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * ============================================================
     * Step 4: queue the real-time signal five times with sigqueue()
     * ============================================================
     *
     * Sending SIGRTMIN five times via sigqueue() creates five separate
     * queued entries, each carrying its own data.  Because the signals are
     * blocked, they remain pending.
     */
    printf(
        "Sending SIGRTMIN 5 times via sigqueue (real-time, should "
        "queue)...\n");
    for (int i = 0; i < 5; i++) {
        union sigval value;
        value.sival_int = 100 + i;
        if (sigqueue(getpid(), SIGRTMIN, value) == -1) {
            perror("sigqueue(SIGRTMIN)");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * ============================================================
     * Step 5: send another real-time signal (priority-order demo)
     * ============================================================
     *
     * Delivery order rules:
     *   - Order among standard signals is unspecified.
     *   - Among real-time signals, lower-numbered signals are delivered
     *     before higher-numbered ones.
     *
     * We deliberately queue the higher-numbered SIGRTMIN+1 first and then
     * the lower-numbered SIGRTMIN.  When collected with sigtimedwait(),
     * SIGRTMIN events should appear before SIGRTMIN+1 events.
     */
    printf(
        "Sending SIGRTMIN+1 first, then SIGRTMIN to show priority "
        "order...\n");
    for (int i = 0; i < 5; i++) {
        union sigval value;
        value.sival_int = 200 + i;
        if (sigqueue(getpid(), SIGRTMIN + 1, value) == -1) {
            perror("sigqueue(SIGRTMIN+1)");
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < 5; i++) {
        union sigval value;
        value.sival_int = 300 + i;
        if (sigqueue(getpid(), SIGRTMIN, value) == -1) {
            perror("sigqueue(SIGRTMIN)");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * ============================================================
     * Step 6: deliver SIGUSR1 and observe merging
     * ============================================================
     *
     * Unblocking SIGUSR1 only (the user-space equivalent of "sti") causes
     * the pending SIGUSR1 to be delivered immediately and the handler to run
     * once.  Real-time signals remain blocked.
     */
    sigset_t usr1_set;
    sigemptyset(&usr1_set);
    sigaddset(&usr1_set, SIGUSR1);
    if (sigprocmask(SIG_UNBLOCK, &usr1_set, NULL) == -1) {
        perror("sigprocmask(SIG_UNBLOCK, SIGUSR1)");
        exit(EXIT_FAILURE);
    }

    /*
     * ============================================================
     * Step 7: collect real-time signals synchronously
     * ============================================================
     *
     * sigtimedwait(2) waits until one of the signals in the given set is
     * pending, dequeues a single signal, and stores details in siginfo_t.
     * Because no handler is involved, si_value data and any shared state
     * can be handled safely in the main thread.
     *
     * - Return value is the dequeued signal number.
     * - Timeout (100 ms here) with EAGAIN means no more queued signals are
     *   pending, so we exit the loop.
     * - Real-time signals of the same number are FIFO; across different
     *   numbers the lower-numbered signal comes first.
     */
    sigset_t rt_set;
    sigemptyset(&rt_set);
    sigaddset(&rt_set, SIGRTMIN);
    sigaddset(&rt_set, SIGRTMIN + 1);

    while (g_event_count < MAX_EVENTS) {
        struct timespec tmo;
        tmo.tv_sec = 0;
        tmo.tv_nsec = 100 * 1000000L; /* 100 ms */

        siginfo_t info;
        int sig = sigtimedwait(&rt_set, &info, &tmo);
        if (sig == -1) {
            break; /* EAGAIN: all pending signals have been collected */
        }
        g_events[g_event_count].sig = sig;
        g_events[g_event_count].data = info.si_value.sival_int;
        g_event_count++;
    }

    /*
     * ============================================================
     * Step 8: display results
     * ============================================================
     */
    printf("\nSIGUSR1 handler was called %d time(s) (expected 1).\n",
           (int)g_usr1_count);

    print_events();

    /*
     * Expected results:
     *   - SIGUSR1: sent 5 times -> delivered once (standard-signal merging).
     *   - SIGRTMIN: 10 sigqueue calls -> all 10 events collected.
     *       (100..104 from step 4, 300..304 from step 5, FIFO order)
     *   - SIGRTMIN+1: 5 sigqueue calls -> all 5 events collected (200..204).
     *   - Collection order: the 10 SIGRTMIN events come before the 5
     *     SIGRTMIN+1 events (because SIGRTMIN has the smaller number).
     */

    printf("\nDone.\n");
    return 0;
}
#endif /* SIGRTMIN */
