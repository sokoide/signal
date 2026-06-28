/*
 * 05_altstack.c — Alternate signal stack: sigaltstack() and SA_ONSTACK
 *
 * Normally a signal handler runs on the process's regular stack.  If that
 * stack is already exhausted (for example by infinite recursion), there is
 * no stack space left to run a SIGSEGV handler, so the handler itself
 * crashes.
 *
 * sigaltstack() provides a separate stack reserved for signal handlers.
 * Combined with the SA_ONSTACK flag, it lets a specific handler run on a
 * pre-allocated memory region instead of the regular stack.
 *
 * This is the same design idea as the OS kernel keeping an "interrupt
 * stack" for hardware interrupts: when a HW interrupt occurs, the CPU
 * switches to the kernel stack to run the ISR.  sigaltstack is the
 * user-space version of that mechanism.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -O2 -g 05_altstack.c -o 05_altstack
 *
 * Note:
 *   On macOS the definition of SIGSTKSZ may require _DARWIN_C_SOURCE.
 *   The required feature-test macros are defined at the top of this file,
 *   so it builds standalone with the bare "cc ..." command above (the
 *   Makefile adds nothing beyond -Wno-deprecated-declarations on macOS).
 */

#if (defined(__APPLE__) && defined(__MACH__))
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700 /* macOS: required by <ucontext.h> */
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* Linux: sigaltstack extensions if needed */
#endif
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

/* Holds the malloc'd alternate stack memory so we can free it on clean paths. */
static void* alt_stack_mem = NULL;

/*
 * Fixed-length message write helper callable from inside a signal handler.
 * write(2) is async-signal-safe, so it can be used in a handler.
 * printf() may hold internal locks, so it is forbidden inside handlers.
 */
static void safe_puts(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    ssize_t ret = write(STDOUT_FILENO, s, len);
    (void)ret;
}

/*
 * SIGSEGV handler.
 *
 * Because the normal stack may be overflowed when this runs, the handler
 * is executed on the alternate stack thanks to SA_ONSTACK.
 *
 * Inside the handler we:
 *   1. Print the delivered signal name.
 *   2. Use sigaltstack() to verify we are actually on the alternate stack.
 *   3. Terminate the process safely with _exit().
 */
static void segv_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)info;
    (void)ucontext;

    safe_puts("\n[Caught SIGSEGV in handler]\n");

    /*
     * sigaltstack(NULL, &current) retrieves the current alternate stack info.
     * When called while a handler is running on the alternate stack, the
     * SS_ONSTACK flag is set in ss_flags.  A value of 1 confirms the handler
     * is indeed running on the alternate stack.
     */
    stack_t current;
    if (sigaltstack(NULL, &current) == -1) {
        safe_puts("  sigaltstack() failed inside handler\n");
    } else {
        if (current.ss_flags & SS_ONSTACK) {
            safe_puts("  Handler IS running on the alternate stack.\n");
        } else {
            safe_puts("  Handler is NOT running on the alternate stack (!)\n");
        }
    }

    /*
     * Use _exit(2) to terminate the process from inside a handler.
     * exit(3) flushes stdio buffers and runs atexit handlers, so it is not
     * async-signal-safe.
     */
    safe_puts("  Calling _exit(128 + SIGSEGV) safely...\n");
    _exit(128 + SIGSEGV);
}

/*
 * Helper that touches a few bytes of a stack-allocated buffer.
 *
 * It is marked noinline so the compiler cannot collapse the recursive
 * calls in overflow() into a single reused buffer.  Passing the address
 * of each invocation's local array forces the compiler to allocate the
 * full buffer on the stack for every call, which is what makes the
 * stack-overflow demo reliable at -O2.
 */
static void touch_stack(volatile char* p, size_t len) __attribute__((noinline));
static void touch_stack(volatile char* p, size_t len) {
    p[0] = 1;
    p[len / 2] = 2;
    p[len - 1] = 3;
}

/*
 * Recursive function that intentionally causes a stack overflow.
 *
 * Each call allocates an 8 KiB local array and recurses deeply.  The
 * noinline helper above prevents the compiler from optimizing away the
 * per-call allocation at -O2.  A write after the recursive call keeps
 * the call from being tail-call optimized.
 */
static volatile int g_depth = 0;

static void overflow(int n) {
    volatile char frame[8192];

    touch_stack(frame, sizeof(frame));
    g_depth++;

    if (n > 0) {
        overflow(n - 1);
    }

    /* Never reached, but prevents tail-call optimization. */
    frame[1] = 1;
}

int main(void) {
    /*
     * ============================================================
     * Step 1: allocate the alternate stack
     * ============================================================
     *
     * SIGSTKSZ is a size "typically sufficient" for running a signal
     * handler.  It must not be smaller than MINSIGSTKSZ.
     *
     * In real applications, allocate more than SIGSTKSZ according to the
     * stack usage of your handler.  Here we use SIGSTKSZ for the demo.
     */
    stack_t ss;
    /* SIGSTKSZ is the typical recommended size for an alternate stack.
     * On glibc >= 2.34 it may be a non-constant expression; on those
     * systems you may need to determine the size dynamically with
     * sysconf().  We use SIGSTKSZ directly for this demo.
     * In production, leave margin according to the handler's stack usage. */
    ss.ss_size = SIGSTKSZ;
    alt_stack_mem = malloc(ss.ss_size);
    if (alt_stack_mem == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    ss.ss_sp = alt_stack_mem;
    ss.ss_flags = 0; /* enable, not SS_DISABLE */

    if (sigaltstack(&ss, NULL) == -1) {
        perror("sigaltstack");
        free(alt_stack_mem);
        exit(EXIT_FAILURE);
    }

    printf("Allocated alternate stack at %p, size %zu bytes.\n", ss.ss_sp,
           ss.ss_size);

    /*
     * ============================================================
     * Step 2: register the SIGSEGV handler with SA_ONSTACK
     * ============================================================
     *
     * Including SA_ONSTACK in sa_flags causes this handler to run on the
     * alternate stack allocated by sigaltstack() instead of the regular
     * stack.
     *
     * Adding SA_SIGINFO gives the handler the three-argument form
     *   void handler(int sig, siginfo_t *info, void *ucontext)
     * so more detailed information is available.
     */
    struct sigaction sa;
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction(SIGSEGV)");
        free(alt_stack_mem);
        exit(EXIT_FAILURE);
    }

    printf("SIGSEGV handler installed with SA_ONSTACK.\n");
    printf("Now triggering stack overflow via deep recursion...\n");
    fflush(stdout); /* printf + fflush is safe in main context */

    /*
     * ============================================================
     * Step 2b: lower the main stack soft limit
     * ============================================================
     *
     * The default RLIMIT_STACK soft limit varies widely between
     * environments (for example, 8 MiB on many Linux systems and 64 MiB
     * under make on macOS).  With a large limit, overflow() can recurse
     * deeply enough to satisfy its termination condition before the
     * regular stack is exhausted, so the program would reach the line
     * "This line should never be printed."
     *
     * To make the alternate-stack demo reliable, lower the soft limit to
     * a small, known value before invoking overflow().  This guarantees
     * that the regular stack runs out of space and SIGSEGV is delivered
     * before the recursive function returns.
     */
    {
        const rlim_t desired_stack = 1024 * 1024; /* 1 MiB */
        struct rlimit rl;

        if (getrlimit(RLIMIT_STACK, &rl) == -1) {
            perror("getrlimit(RLIMIT_STACK)");
            free(alt_stack_mem);
            exit(EXIT_FAILURE);
        }

        if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < desired_stack) {
            rl.rlim_cur = rl.rlim_max;
        } else {
            rl.rlim_cur = desired_stack;
        }

        if (setrlimit(RLIMIT_STACK, &rl) == -1) {
            perror("setrlimit(RLIMIT_STACK)");
            free(alt_stack_mem);
            exit(EXIT_FAILURE);
        }

        printf("Main stack soft limit lowered to %llu bytes.\n",
               (unsigned long long)rl.rlim_cur);
        fflush(stdout);
    }

    /*
     * ============================================================
     * Step 3: trigger a stack overflow
     * ============================================================
     *
     * overflow() keeps allocating large local arrays recursively until the
     * regular stack is exhausted.  The CPU then detects an invalid memory
     * access and the kernel sends SIGSEGV to the process.
     *
     * Without an alternate stack, there would be no stack space left to
     * invoke the SIGSEGV handler, and the handler itself would raise another
     * SIGSEGV, killing the process immediately.
     */
    overflow(1000000);

    /*
     * This point is never reached (overflow() either crashes or _exit() is
     * called inside the handler).
     */
    printf("This line should never be printed.\n");
    free(alt_stack_mem);
    return 0;
}
