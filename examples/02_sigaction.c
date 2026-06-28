/*
 * 02_sigaction.c
 *
 * Topic: Modern signal handling with sigaction(), SA_SIGINFO, and siginfo_t.
 *
 * signal() (the old API) has undefined or implementation-defined behavior in
 * several areas: whether the handler stays installed, whether system calls are
 * restarted, and which signals are blocked while the handler runs.  sigaction()
 * fixes all of that by letting the caller specify every detail.
 *
 * What is demonstrated:
 *   - sigaction() as the recommended replacement for signal().
 *   - SA_SIGINFO: a 3-argument handler receives the signal number, a
 *     siginfo_t pointer, and the interrupted ucontext.
 *   - Reading siginfo_t fields: si_pid (sender PID), si_uid (sender UID),
 *     si_code (reason code), and si_addr (faulting address for SIGSEGV).
 *   - SA_RESTART: automatically restart a slow system call (here read())
 *     that was interrupted by a signal.
 *   - SA_RESETHAND: the handler resets to SIG_DFL after it runs once.
 *   - SA_NODEFER: the same signal is NOT added to the process signal mask
 *     while the handler runs, so the handler can be re-entered.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -O2 -g -o 02_sigaction 02_sigaction.c
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Async-signal-safe output helpers.
 *
 * A signal handler must not call stdio functions such as printf() because
 * the signal may have interrupted the program while it held a stream lock.
 * These helpers only call write(), which is async-signal-safe.
 */
static void safe_write_str(const char* s) {
    size_t n = 0;

    while (s[n] != '\0') {
        n++;
    }
    ssize_t ret = write(STDOUT_FILENO, s, n);
    (void)ret;
}

static void safe_write_int(long long v) {
    char buf[32];
    int i = (int)sizeof(buf) - 1;
    int negative = (v < 0);
    unsigned long long u;
    if (negative) {
        u = 0ULL - (unsigned long long)v; /* avoid LLONG_MIN overflow */
    } else {
        u = (unsigned long long)v;
    }

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

    ssize_t ret = write(STDOUT_FILENO, &buf[i + 1], sizeof(buf) - 2 - (size_t)i);
    (void)ret;
}

static void safe_write_hex(unsigned long long v) {
    const char hex[] = "0123456789abcdef";
    char buf[24];
    int i = (int)sizeof(buf) - 1;

    buf[i] = '\0';
    i--;

    if (v == 0) {
        buf[i] = '0';
        i--;
    } else {
        while (v > 0) {
            buf[i] = hex[v & 0xf];
            v >>= 4;
            i--;
        }
    }

    ssize_t ret = write(STDOUT_FILENO, &buf[i + 1], sizeof(buf) - 2 - (size_t)i);
    (void)ret;
}

/*
 * SA_SIGINFO handler: receives detailed information about the signal.
 *
 *   int sig          - the signal number (e.g. SIGUSR1)
 *   siginfo_t *info  - kernel-provided details about why/how it was sent
 *   void *ucontext   - machine context at the moment of interruption
 *
 * Useful siginfo_t members:
 *   si_pid   - PID of the sending process (valid for kill/raise)
 *   si_uid   - real UID of the sending process
 *   si_code  - reason code, e.g. SI_USER for kill/raise
 *   si_addr  - faulting address for SIGSEGV, SIGBUS, SIGILL, SIGFPE
 */
static void info_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;

    safe_write_str("[SA_SIGINFO] signal=");
    safe_write_int((long long)sig);
    safe_write_str("  si_pid=");
    safe_write_int((long long)info->si_pid);
    safe_write_str("  si_uid=");
    safe_write_int((long long)info->si_uid);
    safe_write_str("  si_code=");
    safe_write_int((long long)info->si_code);
    safe_write_str("\n");
}

/*
 * Simple handler for the SA_RESTART demonstration.  It only records that
 * the signal arrived; the interesting behavior is what happens to the
 * interrupted read() in main().
 */
static volatile sig_atomic_t alarm_received = 0;

static void alarm_handler(int sig) {
    (void)sig;

    alarm_received = 1;
    safe_write_str("[SA_RESTART] SIGALRM handler ran\n");
}

/*
 * SA_NODEFER handler.  We deliberately raise the same signal again while
 * the handler is executing.  With SA_NODEFER the signal is not blocked, so
 * the handler can be entered recursively before the first invocation returns.
 *
 * Why the ++/-- on nd_depth is safe here: the recursive entry is triggered by
 * raise(), which is a *synchronous* call inside this handler.  The second
 * invocation runs to completion and returns before raise() returns, so the two
 * invocations never touch nd_depth concurrently — the re-entrancy is fully
 * sequential.
 *
 * IMPORTANT precondition: this argument assumes no *external* asynchronous
 * SIGUSR1 arrives while the handler runs.  This demo delivers SIGUSR1 only via
 * raise() from main(), never from outside the process.  Because SA_NODEFER
 * leaves the signal unblocked, an externally-sent SIGUSR1 could enter the
 * handler truly concurrently and race on nd_depth.  In production code, prefer
 * a single read/write of a sig_atomic_t, or C11 <stdatomic.h>, so that the
 * safety does not depend on this precondition (see README "非同期シグナル安全性").
 */
static volatile sig_atomic_t nd_depth = 0;
static volatile sig_atomic_t nd_raised = 0;

static void nodefer_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)info;
    (void)ucontext;

    nd_depth++;

    safe_write_str("[SA_NODEFER] entered, depth=");
    safe_write_int((long long)nd_depth);
    safe_write_str("\n");

    /* Raise the same signal once from inside the handler.  With SA_NODEFER
     * the second delivery is not deferred, so we see depth become 2. */
    if (nd_depth == 1 && nd_raised == 0) {
        nd_raised = 1;
        safe_write_str("[SA_NODEFER] raising SIGUSR1 recursively\n");
        raise(SIGUSR1);
    }

    safe_write_str("[SA_NODEFER] leaving, depth=");
    safe_write_int((long long)nd_depth);
    safe_write_str("\n");

    nd_depth--;
}

/*
 * SA_RESETHAND handler.  The kernel will reset the disposition to SIG_DFL
 * after this handler returns, so the second raise() will use the default
 * action instead of calling us again.  We use SIGCHLD because its default
 * action is to ignore the signal, which keeps the program alive.
 *
 * A single assignment (not ++) is enough here: SA_RESETHAND means the handler
 * runs exactly once, so we just record that it fired.
 */
static volatile sig_atomic_t rese_handled = 0;

static void rese_handler(int sig) {
    (void)sig;

    rese_handled = 1;
    safe_write_str(
        "[SA_RESETHAND] SIGCHLD handled (this should run exactly once)\n");
}

/*
 * SIGSEGV handler used in a child process.  It prints the faulting address
 * from si_addr and then exits so that the fault does not propagate.
 */
static void segv_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig;
    (void)ucontext;

    safe_write_str("[SIGSEGV] faulting address: 0x");
    safe_write_hex((unsigned long long)(unsigned long)info->si_addr);
    safe_write_str("\n");

    /* _exit() is async-signal-safe; exit() is not because it flushes
     * streams and may run atexit handlers. */
    _exit(EXIT_SUCCESS);
}

int main(void) {
    struct sigaction act;
    pid_t pid;

    printf("PID: %ld\n", (long)getpid());
    fflush(stdout);

    /* ================================================================
     * 1. SA_SIGINFO: rich signal information
     * ================================================================ */
    printf("\n--- 1. SA_SIGINFO on SIGUSR1 ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = info_handler;
    act.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        return EXIT_FAILURE;
    }

    /* raise() from the same process produces si_code == SI_USER,
     * si_pid == getpid(), and si_uid == getuid(). */
    raise(SIGUSR1);

    /* ================================================================
     * 2. SA_RESTART: interrupted read() is automatically restarted
     * ================================================================ */
    printf("\n--- 2. SA_RESTART with SIGALRM ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = alarm_handler;
    act.sa_flags = SA_RESTART;

    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction(SIGALRM)");
        return EXIT_FAILURE;
    }

    {
        int pipefd[2];
        char c = 0;
        ssize_t n;

        if (pipe(pipefd) == -1) {
            perror("pipe");
            return EXIT_FAILURE;
        }

        pid = fork();
        if (pid == -1) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            /* Child: close the read end, wait a bit, then write a byte. */
            (void)close(pipefd[0]);
            sleep(2);
            c = '!';
            ssize_t ret = write(pipefd[1], &c, 1);
            (void)ret;
            (void)close(pipefd[1]);
            _exit(EXIT_SUCCESS);
        }

        /* Parent: close the write end and block in read(). */
        (void)close(pipefd[1]);

        /* The alarm will fire while read() is blocked.  With SA_RESTART,
         * read() will resume automatically instead of returning EINTR. */
        alarm_received = 0;
        alarm(1);

        printf("Parent: calling read() on an empty pipe...\n");
        fflush(stdout);
        n = read(pipefd[0], &c, 1);
        alarm(0); /* cancel any pending alarm */

        if (n == 1) {
            printf(
                "Parent: read() returned '%c', alarm_received=%d "
                "-> read() was restarted after the signal\n",
                c, (int)alarm_received);
        } else if (n == -1) {
            perror("Parent: read()");
        } else {
            printf("Parent: read() returned EOF unexpectedly\n");
        }
        fflush(stdout);

        (void)close(pipefd[0]);
        (void)wait(NULL);
    }

    /* ================================================================
     * 3. SA_NODEFER: re-entrant signal handling
     * ================================================================ */
    printf("\n--- 3. SA_NODEFER on SIGUSR1 ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = nodefer_handler;
    act.sa_flags = SA_SIGINFO | SA_NODEFER;

    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        perror("sigaction(SIGUSR1, SA_NODEFER)");
        return EXIT_FAILURE;
    }

    nd_depth = 0;
    nd_raised = 0;
    raise(SIGUSR1);

    printf("Main: after SA_NODEFER demo, depth should be 0 (actual=%d)\n",
           (int)nd_depth);
    fflush(stdout);

    /* ================================================================
     * 4. SA_RESETHAND: handler runs once, then resets to default
     * ================================================================ */
    printf("\n--- 4. SA_RESETHAND on SIGCHLD ---\n");
    fflush(stdout);

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = rese_handler;
    act.sa_flags = SA_RESETHAND;

    if (sigaction(SIGCHLD, &act, NULL) == -1) {
        perror("sigaction(SIGCHLD, SA_RESETHAND)");
        return EXIT_FAILURE;
    }

    rese_handled = 0;
    raise(SIGCHLD); /* handler runs */
    raise(SIGCHLD); /* default action: ignored */

    printf("Main: rese_handled=%d (should be 1 because handler was reset)\n",
           (int)rese_handled);
    fflush(stdout);

    /* ================================================================
     * 5. siginfo_t.si_addr for SIGSEGV
     * ================================================================ */
    printf("\n--- 5. SIGSEGV si_addr in a child process ---\n");
    fflush(stdout);

    pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* Child: install a SIGSEGV handler and then dereference NULL. */
        memset(&act, 0, sizeof(act));
        sigemptyset(&act.sa_mask);
        act.sa_sigaction = segv_handler;
        act.sa_flags = SA_SIGINFO;

        if (sigaction(SIGSEGV, &act, NULL) == -1) {
            perror("sigaction(SIGSEGV)");
            _exit(EXIT_FAILURE);
        }

        *(volatile int*)NULL = 0; /* guaranteed to fault */

        /* If the handler returns (it does not), this would be undefined. */
        _exit(EXIT_FAILURE);
    } else {
        int status;
        (void)wait(&status);
        if (WIFEXITED(status)) {
            printf("Main: child exited with status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Main: child killed by signal %d\n", WTERMSIG(status));
        }
        fflush(stdout);
    }

    return EXIT_SUCCESS;
}
