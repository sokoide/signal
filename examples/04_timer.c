/*
 * 04_timer.c — Introduction to POSIX timer signals
 *
 * This program demonstrates the two POSIX timer APIs and the signals they
 * generate by running them side by side.
 *
 * 1. alarm(2): the simplest "one-shot, second-resolution" timer.
 *    It sends SIGALRM to the calling process after the specified number of
 *    seconds.
 *
 * 2. setitimer(2): a repeating timer with microsecond resolution.
 *    There are three measurement domains, each producing a different signal.
 *    Note: setitimer() is marked obsolescent in POSIX.1-2008.  New code
 *    should consider timer_create() / timer_settime() instead.
 *
 *    | which            | signal      | measures                                 |
 *    |------------------|-------------|------------------------------------------|
 *    | ITIMER_REAL      | SIGALRM     | wall-clock time                          |
 *    | ITIMER_VIRTUAL   | SIGVTALRM   | user-mode CPU time only                  |
 *    | ITIMER_PROF      | SIGPROF     | user CPU time + kernel CPU time          |
 *
 * Key points:
 *   - ITIMER_REAL keeps ticking while the process is in sleep() because it
 *     measures wall-clock time.
 *   - ITIMER_VIRTUAL does not advance during sleep() because no CPU time is
 *     being consumed.
 *   - This lets you feel the difference between an OS "timer interrupt" and
 *     "CPU time".
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -O2 -g 04_timer.c -o 04_timer
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

/*
 * Variables that can be safely shared between a signal handler and normal
 * execution.
 *
 * volatile: prevents the compiler from optimizing away reloads, so changes
 *           made by the handler are always visible.
 * sig_atomic_t: reads and writes of this type are indivisible and will not
 *               be torn by signal interruption.
 */
static volatile sig_atomic_t g_virtual_count = 0;
static volatile sig_atomic_t g_real_count = 0;

/*
 * SIGVTALRM handler.
 * Fires only while the process is consuming CPU time in user mode.
 */
static void virtual_handler(int sig) {
    (void)sig; /* silence unused-parameter warning */
    g_virtual_count++;
}

/*
 * SIGALRM handler.
 * Called when the ITIMER_REAL (wall-clock) timer fires.
 */
static void real_handler(int sig) {
    (void)sig;
    g_real_count++;
}

/*
 * Handler for alarm().
 * alarm() is used with printf avoided; we emit the message with the
 * async-signal-safe write() instead.
 */
static void alarm_handler(int sig) {
    (void)sig;
    const char msg[] = "[alarm] SIGALRM fired (one-shot)\n";
    /* write(2) is async-signal-safe */
    ssize_t ret = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)ret;
}

/*
 * A busy loop that consumes CPU.
 * sleep() does not consume CPU, so the VIRTUAL timer will not advance while
 * sleeping.  Here we deliberately occupy the CPU for roughly one second.
 */
static void busy_loop_for_one_second(void) {
    struct timeval start, now;
    /* gettimeofday() is used for brevity.  For new code, prefer
     * clock_gettime(CLOCK_MONOTONIC, ...) because it is not affected by
     * system clock adjustments. */
    gettimeofday(&start, NULL);

    /* `spinner` is a dummy variable that prevents the loop body from being
     * optimized away.  It is not part of the exit condition. */
    volatile unsigned long spinner = 0;
    do {
        for (int i = 0; i < 1000000; i++) {
            spinner++;
        }
        gettimeofday(&now, NULL);
    } while ((now.tv_sec - start.tv_sec) * 1000000L +
                 (now.tv_usec - start.tv_usec) <
             1000000L);
}

int main(void) {
    struct sigaction sa;

    /*
     * ============================================================
     * Part 1: alarm(2) — one-shot, second-resolution timer
     * ============================================================
     *
     * alarm(seconds) schedules SIGALRM once after `seconds` seconds.
     * It has only second resolution and is one-shot.  Calling alarm()
     * again cancels any previous alarm().
     */
    printf("=== alarm(2) demo ===\n");
    fflush(stdout); /* stabilize ordering with write() in the handler */

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        exit(EXIT_FAILURE);
    }

    /* Schedule SIGALRM in one second. */
    alarm(1);

    /*
     * pause() sleeps until a signal is delivered.
     * When SIGALRM arrives, pause() returns -1.
     *
     * In this simple "alarm then pause" sequence there is no race, but in
     * general "check condition then wait" code can miss a signal that
     * arrives between the check and the wait (the classical race).  For
     * production code, use sigsuspend(2) to change the signal mask and wait
     * atomically (see the README "Advanced Topics" section).
     */
    pause();

    /*
     * alarm(0) cancels any pending alarm.
     * This is good cleanup hygiene.
     */
    alarm(0);

    /*
     * ============================================================
     * Part 2: setitimer(2) — high-resolution repeating timer
     * ============================================================
     *
     * struct itimerval contains two struct timevals:
     *   it_value    : time until the first expiration (initial value)
     *   it_interval : interval between subsequent expirations (0 = one-shot)
     */
    printf("\n=== setitimer(2) demo ===\n");

    /* Register the SIGVTALRM handler with sigaction. */
    sa.sa_handler = virtual_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGVTALRM)");
        exit(EXIT_FAILURE);
    }

    /* Register the SIGALRM handler with sigaction (for ITIMER_REAL). */
    sa.sa_handler = real_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        exit(EXIT_FAILURE);
    }

    /*
     * (A) Measure CPU time with ITIMER_VIRTUAL
     *
     * Fire SIGVTALRM every 10 ms.  The handler increments g_virtual_count,
     * and we display the total after the busy loop.
     */
    struct itimerval virtual_timer;
    virtual_timer.it_value.tv_sec = 0;
    virtual_timer.it_value.tv_usec = 10000; /* 10ms */
    virtual_timer.it_interval = virtual_timer.it_value;

    g_virtual_count = 0;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL)");
        exit(EXIT_FAILURE);
    }

    printf("Running busy loop for ~1 second with VIRTUAL timer (10ms)...\n");
    busy_loop_for_one_second();

    /*
     * To stop a timer, pass an itimerval with both members set to 0.
     * Otherwise signals would keep arriving during later work.
     */
    virtual_timer.it_value.tv_sec = 0;
    virtual_timer.it_value.tv_usec = 0;
    virtual_timer.it_interval = virtual_timer.it_value;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL, stop)");
        exit(EXIT_FAILURE);
    }

    printf("VIRTUAL timer fired %d times during busy loop.\n",
           (int)g_virtual_count);

    /*
     * (B) Demonstrate the difference between ITIMER_REAL and ITIMER_VIRTUAL
     *     using sleep().
     *
     * Point: sleep(1) suspends the process, so it consumes no CPU time.
     * Therefore ITIMER_VIRTUAL will not fire during sleep, but ITIMER_REAL
     * (wall-clock time) keeps firing regardless.
     */
    printf(
        "\n--- ITIMER_REAL fires during sleep, ITIMER_VIRTUAL does not ---\n");

    g_real_count = 0;
    g_virtual_count = 0;

    /* Set the REAL timer to 100 ms intervals. */
    struct itimerval real_timer;
    real_timer.it_value.tv_sec = 0;
    real_timer.it_value.tv_usec = 100000; /* 100ms */
    real_timer.it_interval = real_timer.it_value;
    if (setitimer(ITIMER_REAL, &real_timer, NULL) == -1) {
        perror("setitimer(ITIMER_REAL)");
        exit(EXIT_FAILURE);
    }

    /* Set the VIRTUAL timer to 100 ms intervals as well. */
    struct itimerval virtual_timer2;
    virtual_timer2.it_value.tv_sec = 0;
    virtual_timer2.it_value.tv_usec = 100000; /* 100ms */
    virtual_timer2.it_interval = virtual_timer2.it_value;
    if (setitimer(ITIMER_VIRTUAL, &virtual_timer2, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL)");
        exit(EXIT_FAILURE);
    }

    printf("sleep(1) starts...\n");
    sleep(1);
    printf("sleep(1) ended.\n");

    /* Stop both timers. */
    struct itimerval stop;
    stop.it_value.tv_sec = 0;
    stop.it_value.tv_usec = 0;
    stop.it_interval = stop.it_value;
    if (setitimer(ITIMER_REAL, &stop, NULL) == -1) {
        perror("setitimer(ITIMER_REAL, stop)");
        exit(EXIT_FAILURE);
    }
    if (setitimer(ITIMER_VIRTUAL, &stop, NULL) == -1) {
        perror("setitimer(ITIMER_VIRTUAL, stop)");
        exit(EXIT_FAILURE);
    }

    printf(
        "During sleep(1): REAL timer fired %d time(s), VIRTUAL timer fired %d "
        "time(s).\n",
        (int)g_real_count, (int)g_virtual_count);

    /*
     * (C) Note: ITIMER_PROF fires based on "user CPU time + kernel CPU time"
     *     and generates SIGPROF.  It is commonly used by profilers.
     *     We omit it here to keep the example concise, but you can use it by
     *     passing ITIMER_PROF to setitimer() exactly as above.
     */

    printf("\nDone.\n");
    return 0;
}
